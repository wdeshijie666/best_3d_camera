/**
 * @file HikvisionAdapter.cpp
 * @brief 海康 MV3D_RGBD API 封装：枚举、连接、参数、同步/异步取流与 UnifiedFrame 转换。
 */
#include "HikvisionAdapter.h"
#include "../data_center/UnifiedFrame.h"

#include <Mv3dRgbdApi.h>
#include <Mv3dRgbdImgProc.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>
#ifdef _WIN32
#include <windows.h>
#endif

namespace scanner_viewer {

namespace {

/**
 * 异步采集模式开关：
 * 0 = 使用 MV3D_RGBD_RegisterFrameCallBack（默认，保持原有逻辑）；
 * 1 = 使用 while + MV3D_RGBD_FetchFrame 线程循环。
 */
#define HIK_MV3D_ASYNC_USE_FETCH_LOOP 0


/** UI 参数 id，与 A 系列 AIR_CONFIG 枚举隔离。 */
constexpr int kParamTriggerEnable = 0x4D563301;
constexpr int kParamExposureUs = 0x4D563302;
constexpr int kParamGainMilli = 0x4D563303;

std::once_flag g_mv3d_init_once;
std::mutex g_callback_map_mutex;
std::unordered_map<void*, HikvisionAdapter*> g_handle_adapter_map;

#ifdef _WIN32
bool FileExistsA(const char* path) {
    const DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

void AppendPathEnvIfNeeded(const std::string& dir) {
    if (dir.empty()) return;
    char old_path[32767] = {0};
    const DWORD len = GetEnvironmentVariableA("PATH", old_path, static_cast<DWORD>(sizeof(old_path)));
    std::string path_now = (len > 0 && len < sizeof(old_path)) ? std::string(old_path, len) : std::string();
    if (path_now.find(dir) != std::string::npos) return;
    if (!path_now.empty() && path_now.back() != ';') path_now.push_back(';');
    path_now += dir;
    SetEnvironmentVariableA("PATH", path_now.c_str());
}

void TryConfigureMv3dRuntimePath() {
    const char* candidates[] = {
        "C:\\Program Files (x86)\\Common Files\\Mv3dRgbdSDK\\Runtime\\Win64_x64",
        "D:\\work\\SDK\\Hik\\HiViewer\\Development\\Libraries\\win64"
    };
    for (const char* dir : candidates) {
        const std::string dll = std::string(dir) + "\\Mv3dRgbd.dll";
        if (FileExistsA(dll.c_str())) {
            AppendPathEnvIfNeeded(dir);
            break;
        }
    }
}
#endif

void EnsureMv3dInitialized() {
    std::call_once(g_mv3d_init_once, []() {
#ifdef _WIN32
        TryConfigureMv3dRuntimePath();
#endif
        if (MV3D_RGBD_Initialize() != MV3D_RGBD_OK) {
            std::cout << "MV3D_RGBD_Initialize failed." << std::endl;
        }
    });
}

uint32_t AllDeviceTypes() {
    return DeviceType_Ethernet | DeviceType_USB | DeviceType_Ethernet_Vir | DeviceType_USB_Vir;
}

DeviceInfo FromMv3dDeviceInfo(const MV3D_RGBD_DEVICE_INFO& d) {
    DeviceInfo out;
    out.model_name = HikvisionAdapter::kDeviceModelTag;
    out.serial_number = d.chSerialNumber;
    out.firmware_version = d.chDeviceVersion;
    out.name = (d.chUserDefinedName[0] != '\0') ? d.chUserDefinedName : d.chModelName;
    out.model_type = static_cast<int>(d.enDeviceType);
    if (d.enDeviceType == DeviceType_Ethernet || d.enDeviceType == DeviceType_Ethernet_Vir) {
        out.ip = d.SpecialInfo.stNetInfo.chCurrentIp;
    }
    return out;
}

/** RGB888 平面（RR..GG..BB..）转 BGR 交错，供 ImageView2D 现有通道顺序使用。 */
void RgbPlanarToUnifiedBgrInterleaved(const uint8_t* src, int w, int h, UnifiedImage& img) {
    const size_t plane = static_cast<size_t>(w) * h;
    const uint8_t* pr = src;
    const uint8_t* pg = pr + plane;
    const uint8_t* pb = pg + plane;
    img.width = w;
    img.height = h;
    img.channels = 3;
    img.is_16bit = false;
    img.data.resize(plane * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = static_cast<size_t>(y) * w + x;
            img.data[i * 3 + 0] = pb[i];
            img.data[i * 3 + 1] = pg[i];
            img.data[i * 3 + 2] = pr[i];
        }
    }
}

void CopyMono8(const MV3D_RGBD_IMAGE_DATA& im, UnifiedImage& out) {
    out.width = static_cast<int>(im.nWidth);
    out.height = static_cast<int>(im.nHeight);
    out.channels = 1;
    out.is_16bit = false;
    out.data.assign(im.pData, im.pData + im.nDataLen);
}

void CopyDepthC16(const MV3D_RGBD_IMAGE_DATA& im, UnifiedImage& out) {
    out.width = static_cast<int>(im.nWidth);
    out.height = static_cast<int>(im.nHeight);
    out.channels = 1;
    out.is_16bit = true;
    out.data.assign(im.pData, im.pData + im.nDataLen);
}

void AppendPointCloudXyz(const MV3D_RGBD_IMAGE_DATA& im, UnifiedFrame& frame) {
    if (!im.pData || im.nWidth == 0 || im.nHeight == 0) return;
    const size_t n = static_cast<size_t>(im.nWidth) * im.nHeight;
    const size_t point_size = sizeof(MV3D_RGBD_POINT_3D_F32);
    if (im.nDataLen < n * point_size) return;
    const auto* pts = reinterpret_cast<const MV3D_RGBD_POINT_3D_F32*>(im.pData);
    frame.point_cloud.reserve(frame.point_cloud.size() + n);
    for (size_t i = 0; i < n; ++i) {
      if (pts[i].fX == 0. && pts[i].fY == 0. && pts[i].fZ == 0.) {
        continue;
      }
      UnifiedPoint p;
      p.x = pts[i].fX;
      p.y = pts[i].fY;
      p.z = pts[i].fZ;
      p.has_color = false;
      frame.point_cloud.push_back(p);
    }
    frame.point_cloud_width = static_cast<int>(im.nWidth);
    frame.point_cloud_height = static_cast<int>(im.nHeight);
}

void AppendTexturedPointCloud(const MV3D_RGBD_IMAGE_DATA& im, UnifiedFrame& frame) {
    if (!im.pData || im.nWidth == 0 || im.nHeight == 0) return;
    const size_t n = static_cast<size_t>(im.nWidth) * im.nHeight;
    const size_t point_size = sizeof(MV3D_RGBD_POINT_XYZ_RGB);
    if (im.nDataLen < n * point_size) return;
    const auto* pts = reinterpret_cast<const MV3D_RGBD_POINT_XYZ_RGB*>(im.pData);
    frame.point_cloud.reserve(frame.point_cloud.size() + n);
    for (size_t i = 0; i < n; ++i) {
      if (pts[i].stPoint3f.fX == 0. && pts[i].stPoint3f.fY == 0. &&
          pts[i].stPoint3f.fZ == 0.) {
        continue;

    }
      UnifiedPoint p;
      p.x = pts[i].stPoint3f.fX;
      p.y = pts[i].stPoint3f.fY;
      p.z = pts[i].stPoint3f.fZ;
      p.r = pts[i].RgbInfo.stRgba.nR;
      p.g = pts[i].RgbInfo.stRgba.nG;
      p.b = pts[i].RgbInfo.stRgba.nB;
      p.has_color = true;
      frame.point_cloud.push_back(p);
    }
    frame.point_cloud_width = static_cast<int>(im.nWidth);
    frame.point_cloud_height = static_cast<int>(im.nHeight);
}

void __stdcall HikMv3dFrameCallback(MV3D_RGBD_FRAME_DATA* pst_frame_data, void* p_user) {
    if (!pst_frame_data || !p_user) return;
    HikvisionAdapter* self = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_callback_map_mutex);
        auto it = g_handle_adapter_map.find(p_user);
        if (it != g_handle_adapter_map.end()) self = it->second;
    }
    if (!self) return;
    self->NotifyMv3dFrame(pst_frame_data);
}

}  // namespace

bool HikvisionAdapter::init() {
  MV3D_RGBD_VERSION_INFO stVersion;
  MV3D_RGBD_GetSDKVersion(&stVersion);
  printf("dll version: %d.%d.%d", stVersion.nMajor, stVersion.nMinor,
       stVersion.nRevision);
  EnsureMv3dInitialized();
  return true;
}

std::vector<DeviceInfo> HikvisionAdapter::EnumerateDevices() {
    EnsureMv3dInitialized();

    std::vector<DeviceInfo> result;
    unsigned int n = 0;
    const MV3D_RGBD_STATUS st_num = MV3D_RGBD_GetDeviceNumber(
        DeviceType_Ethernet | DeviceType_USB | DeviceType_Ethernet_Vir | DeviceType_USB_Vir, &n);
    if (st_num != MV3D_RGBD_OK) {
        std::cerr << "MV3D_RGBD_GetDeviceNumber failed, code=0x" << std::hex << st_num << std::dec << std::endl;
        return result;
    }
    if (n == 0) return result;

    std::vector<MV3D_RGBD_DEVICE_INFO> devs(n);
    uint32_t filled = n;
    const MV3D_RGBD_STATUS st_list = MV3D_RGBD_GetDeviceList(
        DeviceType_Ethernet | DeviceType_USB | DeviceType_Ethernet_Vir | DeviceType_USB_Vir, devs.data(), n, &filled);
    if (st_list != MV3D_RGBD_OK) {
        std::cerr << "MV3D_RGBD_GetDeviceList failed, code=0x" << std::hex << st_list << std::dec << std::endl;
        return result;
    }

    result.reserve(filled);
    for (uint32_t i = 0; i < filled; ++i) result.push_back(FromMv3dDeviceInfo(devs[i]));
    return result;
}

HikvisionAdapter::HikvisionAdapter() {

}

HikvisionAdapter::~HikvisionAdapter() {
    StopAsyncCapture();
    Disconnect();
}

bool HikvisionAdapter::Connect(const DeviceInfo* device_info, unsigned int /*timeout_ms*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (handle_) {
        MV3D_RGBD_RegisterFrameCallBack(handle_, nullptr, nullptr);
        MV3D_RGBD_Stop(handle_);
        MV3D_RGBD_CloseDevice(&handle_);
        handle_ = nullptr;
        grabbing_ = false;
    }

    EnsureMv3dInitialized();
    void* h = nullptr;
    MV3D_RGBD_STATUS st = MV3D_RGBD_E_PARAMETER;

    if (device_info) {
        if (!device_info->serial_number.empty())
            st = MV3D_RGBD_OpenDeviceBySerialNumber(&h, device_info->serial_number.c_str());
        else if (!device_info->ip.empty())
            st = MV3D_RGBD_OpenDeviceByIp(&h, device_info->ip.c_str());
        else
            st = MV3D_RGBD_OpenDevice(&h, nullptr);
    } else {
        st = MV3D_RGBD_OpenDevice(&h, nullptr);
    }

    if (st != MV3D_RGBD_OK || !h) return false;

    handle_ = h;

    // 输出点云，便于与彩色对齐显示（与 GetPointCloudImage 例程思路一致）。
    MV3D_RGBD_PARAM pc_param{};
    pc_param.enParamType = ParamType_Enum;
    pc_param.ParamInfo.stEnumParam.nCurValue = PointCloudType_Undefined;
    MV3D_RGBD_SetParam(handle_, MV3D_RGBD_ENUM_POINT_CLOUD_OUTPUT, &pc_param);

    //固定最高帧率
     pc_param = {};
    pc_param.enParamType = ParamType_Float;
    pc_param.ParamInfo.stFloatParam.fCurValue = 30.f;
    MV3D_RGBD_SetParam(handle_, MV3D_RGBD_FLOAT_FRAMERATE, &pc_param);

    RefreshTriggerEnumMappingUnlocked();
    UpdateFloatRangeCacheUnlocked();
    grabbing_ = false;
    return true;
}

void HikvisionAdapter::Disconnect() {
    async_running_ = false;
    if (async_fetch_thread_.joinable()) async_fetch_thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    async_callback_ = nullptr;
    if (handle_) {
#if !HIK_MV3D_ASYNC_USE_FETCH_LOOP
        MV3D_RGBD_RegisterFrameCallBack(handle_, nullptr, nullptr);
        {
            std::lock_guard<std::mutex> map_lock(g_callback_map_mutex);
            g_handle_adapter_map.erase(handle_);
        }
#endif
        MV3D_RGBD_Stop(handle_);
        MV3D_RGBD_CloseDevice(&handle_);
        handle_ = nullptr;
    }
    grabbing_ = false;
}

bool HikvisionAdapter::IsConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return handle_ != nullptr;
}

bool HikvisionAdapter::GetDeviceInfo(DeviceInfo& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!handle_) return false;
    MV3D_RGBD_DEVICE_INFO info{};
    if (MV3D_RGBD_GetDeviceInfo(handle_, &info) != MV3D_RGBD_OK) return false;
    out = FromMv3dDeviceInfo(info);
    return true;
}

void HikvisionAdapter::RefreshTriggerEnumMappingUnlocked() const {
    trigger_enum_ready_ = false;
    if (!handle_) return;
    MV3D_RGBD_PARAM p{};
    if (MV3D_RGBD_GetParam(handle_, MV3D_RGBD_ENUM_TRIGGERMODE, &p) != MV3D_RGBD_OK) return;
    if (p.enParamType != ParamType_Enum) return;
    const auto& e = p.ParamInfo.stEnumParam;
    if (e.nSupportedNum == 0) return;
    trigger_value_off_ = e.nSupportValue[0];
    trigger_value_on_ = (e.nSupportedNum > 1) ? e.nSupportValue[1] : e.nSupportValue[0];
    trigger_enum_ready_ = true;
}

void HikvisionAdapter::UpdateFloatRangeCacheUnlocked() const {
    if (!handle_) return;
    MV3D_RGBD_PARAM p{};
    if (MV3D_RGBD_GetParam(handle_, MV3D_RGBD_FLOAT_EXPOSURETIME, &p) == MV3D_RGBD_OK &&
        p.enParamType == ParamType_Float) {
        exposure_min_ = p.ParamInfo.stFloatParam.fMin;
        exposure_max_ = p.ParamInfo.stFloatParam.fMax;
    }
    if (MV3D_RGBD_GetParam(handle_, MV3D_RGBD_FLOAT_GAIN, &p) == MV3D_RGBD_OK && p.enParamType == ParamType_Float) {
        gain_min_ = p.ParamInfo.stFloatParam.fMin;
        gain_max_ = p.ParamInfo.stFloatParam.fMax;
    }
}

bool HikvisionAdapter::IsTriggerOnUnlocked() const {
    if (!handle_ || !trigger_enum_ready_) return false;
    MV3D_RGBD_PARAM p{};
    if (MV3D_RGBD_GetParam(handle_, MV3D_RGBD_ENUM_TRIGGERMODE, &p) != MV3D_RGBD_OK) return false;
    if (p.enParamType != ParamType_Enum) return false;
    return p.ParamInfo.stEnumParam.nCurValue == trigger_value_on_;
}

bool HikvisionAdapter::EnsureGrabbingUnlocked() {
    if (!handle_) return false;
    if (grabbing_) return true;
    if (MV3D_RGBD_Start(handle_) != MV3D_RGBD_OK) return false;
    grabbing_ = true;
    return true;
}

bool HikvisionAdapter::GetParameterList(std::vector<ParamMeta>& out) const {
    out.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    if (!handle_) return false;
    UpdateFloatRangeCacheUnlocked();

    ParamMeta trig{};
    trig.id = kParamTriggerEnable;
    trig.name = "触发模式";
    trig.description = "关闭为连续采集；开启后由软触发或外部触发配合 MV3D_RGBD_SoftTrigger 出图。";
    trig.unit = "";
    trig.min_val = 0;
    trig.max_val = 1;
    trig.kind = ParamKind::Bool;
    out.push_back(trig);

    ParamMeta exp{};
    exp.id = kParamExposureUs;
    exp.name = "曝光时间";
    exp.description = "MV3D_RGBD_FLOAT_EXPOSURETIME，单位与相机一致（通常为 µs），界面为整数。";
    exp.unit = "";
    exp.min_val = static_cast<int>(std::floor(exposure_min_));
    exp.max_val = static_cast<int>(std::ceil(exposure_max_));
    if (exp.max_val < exp.min_val) std::swap(exp.min_val, exp.max_val);
    exp.kind = ParamKind::Int;
    out.push_back(exp);

    ParamMeta gain{};
    gain.id = kParamGainMilli;
    gain.name = "增益";
    gain.description = "MV3D_RGBD_FLOAT_GAIN；界面值为实际增益×1000（例如 1500 表示 1.5）。";
    gain.unit = "×0.001";
    gain.min_val = static_cast<int>(std::floor(gain_min_ * 1000.f));
    gain.max_val = static_cast<int>(std::ceil(gain_max_ * 1000.f));
    if (gain.max_val < gain.min_val) std::swap(gain.min_val, gain.max_val);
    gain.kind = ParamKind::Int;
    out.push_back(gain);

    return true;
}

bool HikvisionAdapter::GetParameter(int id, std::vector<int>& values) const {
    values.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    if (!handle_) return false;

    if (id == kParamTriggerEnable) {
        if (!trigger_enum_ready_) RefreshTriggerEnumMappingUnlocked();
        MV3D_RGBD_PARAM p{};
        if (MV3D_RGBD_GetParam(handle_, MV3D_RGBD_ENUM_TRIGGERMODE, &p) != MV3D_RGBD_OK) return false;
        if (p.enParamType != ParamType_Enum) return false;
        const bool on = (p.ParamInfo.stEnumParam.nCurValue == trigger_value_on_);
        values.push_back(on ? 1 : 0);
        return true;
    }
    if (id == kParamExposureUs) {
        MV3D_RGBD_PARAM p{};
        if (MV3D_RGBD_GetParam(handle_, MV3D_RGBD_FLOAT_EXPOSURETIME, &p) != MV3D_RGBD_OK) return false;
        if (p.enParamType != ParamType_Float) return false;
        values.push_back(static_cast<int>(std::lround(p.ParamInfo.stFloatParam.fCurValue)));
        return true;
    }
    if (id == kParamGainMilli) {
        MV3D_RGBD_PARAM p{};
        if (MV3D_RGBD_GetParam(handle_, MV3D_RGBD_FLOAT_GAIN, &p) != MV3D_RGBD_OK) return false;
        if (p.enParamType != ParamType_Float) return false;
        values.push_back(static_cast<int>(std::lround(p.ParamInfo.stFloatParam.fCurValue * 1000.f)));
        return true;
    }
    return false;
}

bool HikvisionAdapter::SetParameter(int id, const std::vector<int>& values) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!handle_ || values.empty()) return false;

    if (id == kParamTriggerEnable) {
        if (!trigger_enum_ready_) RefreshTriggerEnumMappingUnlocked();
        MV3D_RGBD_PARAM p{};
        p.enParamType = ParamType_Enum;
        p.ParamInfo.stEnumParam.nCurValue = (values[0] != 0) ? trigger_value_on_ : trigger_value_off_;
        return MV3D_RGBD_SetParam(handle_, MV3D_RGBD_ENUM_TRIGGERMODE, &p) == MV3D_RGBD_OK;
    }
    if (id == kParamExposureUs) {
        MV3D_RGBD_PARAM p{};
        if (MV3D_RGBD_GetParam(handle_, MV3D_RGBD_FLOAT_EXPOSURETIME, &p) != MV3D_RGBD_OK) return false;
        if (p.enParamType != ParamType_Float) return false;
        float v = static_cast<float>(values[0]);
        v = std::clamp(v, p.ParamInfo.stFloatParam.fMin, p.ParamInfo.stFloatParam.fMax);
        p.ParamInfo.stFloatParam.fCurValue = v;
        return MV3D_RGBD_SetParam(handle_, MV3D_RGBD_FLOAT_EXPOSURETIME, &p) == MV3D_RGBD_OK;
    }
    if (id == kParamGainMilli) {
        MV3D_RGBD_PARAM p{};
        if (MV3D_RGBD_GetParam(handle_, MV3D_RGBD_FLOAT_GAIN, &p) != MV3D_RGBD_OK) return false;
        if (p.enParamType != ParamType_Float) return false;
        float v = static_cast<float>(values[0]) / 1000.f;
        v = std::clamp(v, p.ParamInfo.stFloatParam.fMin, p.ParamInfo.stFloatParam.fMax);
        p.ParamInfo.stFloatParam.fCurValue = v;
        return MV3D_RGBD_SetParam(handle_, MV3D_RGBD_FLOAT_GAIN, &p) == MV3D_RGBD_OK;
    }
    return false;
}

void HikvisionAdapter::ConvertFrameData(void* device_handle, const void* frame_data, UnifiedFrame& out) {
    const auto* frame = static_cast<const MV3D_RGBD_FRAME_DATA*>(frame_data);
    const FrameConvertDemand demand = GetFrameConvertDemand();
    out = UnifiedFrame{};
    out.timestamp = std::chrono::steady_clock::now();
    out.frame_index = 0;

    std::cout << "recved img count : " << frame->nImageCount << std::endl;

    const MV3D_RGBD_IMAGE_DATA* depth_for_map = nullptr;

    for (uint32_t i = 0; i < frame->nImageCount; ++i) {
        const MV3D_RGBD_IMAGE_DATA& im = frame->stImageData[i];
        if (!im.pData || im.nWidth == 0 || im.nHeight == 0) continue;

        if (demand.need_depth && im.enImageType == ImageType_Depth) {
            CopyDepthC16(im, out.depth);
            depth_for_map = &im;
        } else if (demand.need_texture &&
                   im.enImageType == ImageType_RGB8_Planar &&
                   im.enStreamType == StreamType_Color) {
            UnifiedImage tex;
            RgbPlanarToUnifiedBgrInterleaved(
                im.pData, static_cast<int>(im.nWidth), static_cast<int>(im.nHeight), tex);
            out.textures.push_back(std::move(tex));
        } else if (demand.need_texture && im.enImageType == ImageType_Mono8) {
            UnifiedImage tex;
            CopyMono8(im, tex);
            out.textures.push_back(std::move(tex));
        } else if (demand.need_point_cloud && im.enImageType == ImageType_PointCloud) {
            AppendPointCloudXyz(im, out);
        } else if (demand.need_point_cloud && im.enImageType == ImageType_TexturedPointCloud) {
            AppendTexturedPointCloud(im, out);
        }
    }

    if (demand.need_point_cloud && out.point_cloud.empty() && depth_for_map && device_handle) {
        MV3D_RGBD_IMAGE_DATA pc_out{};
        if (MV3D_RGBD_MapDepthToPointCloud(device_handle, const_cast<MV3D_RGBD_IMAGE_DATA*>(depth_for_map), &pc_out) ==
            MV3D_RGBD_OK) {
            if (pc_out.enImageType == ImageType_TexturedPointCloud) {
                AppendTexturedPointCloud(pc_out, out);
            } else {
                AppendPointCloudXyz(pc_out, out);
            }
        }
    }

}

void HikvisionAdapter::NotifyMv3dFrame(void* mv3d_frame_data) {
    if (!async_running_.load(std::memory_order_acquire)) return;
    UnifiedFrame u;
    FrameCallback cb_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!handle_) return;
        ConvertFrameData(handle_, mv3d_frame_data, u);
        cb_copy = async_callback_;
    }
    if (cb_copy) cb_copy(u);
}

bool HikvisionAdapter::CaptureSync(UnifiedFrame& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!handle_) return false;
    if (!EnsureGrabbingUnlocked()) return false;

    if (IsTriggerOnUnlocked()) MV3D_RGBD_SoftTrigger(handle_);

    MV3D_RGBD_FRAME_DATA st{};
    const uint32_t t_out = 10000;
    if (MV3D_RGBD_FetchFrame(handle_, &st, t_out) != MV3D_RGBD_OK) return false;
    ConvertFrameData(handle_, &st, frame);
    return true;
}

bool HikvisionAdapter::StartAsyncCapture(FrameCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!handle_ || !callback) return false;
    async_callback_ = std::move(callback);
    async_running_ = true;
#if HIK_MV3D_ASYNC_USE_FETCH_LOOP
    if (!EnsureGrabbingUnlocked()) {
        async_running_ = false;
        async_callback_ = nullptr;
        return false;
    }
    void* handle_snapshot = handle_;
    async_fetch_thread_ = std::thread(&HikvisionAdapter::AsyncFetchLoop, this, handle_snapshot);
#else
    {
        std::lock_guard<std::mutex> map_lock(g_callback_map_mutex);
        g_handle_adapter_map[handle_] = this;
    }
    if (MV3D_RGBD_RegisterFrameCallBack(handle_, HikMv3dFrameCallback,
                                        handle_) !=
        MV3D_RGBD_OK) {
      {
          std::lock_guard<std::mutex> map_lock(g_callback_map_mutex);
          g_handle_adapter_map.erase(handle_);
      }
      async_running_ = false;
      return false;
    }
    if (!EnsureGrabbingUnlocked()) {
        async_running_ = false;
        return false;
    }
#endif
    return true;
}

void HikvisionAdapter::StopAsyncCapture() {
    async_running_ = false;
    if (async_fetch_thread_.joinable()) async_fetch_thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
#if !HIK_MV3D_ASYNC_USE_FETCH_LOOP
    if (handle_) {
        MV3D_RGBD_RegisterFrameCallBack(handle_, nullptr, nullptr);
        std::lock_guard<std::mutex> map_lock(g_callback_map_mutex);
        g_handle_adapter_map.erase(handle_);
    }
#endif
    async_callback_ = nullptr;
    if (handle_ && grabbing_) {
        MV3D_RGBD_Stop(handle_);
        grabbing_ = false;
    }
}

bool HikvisionAdapter::IsAsyncCapturing() const {
    return async_running_.load(std::memory_order_acquire);
}

void HikvisionAdapter::AsyncFetchLoop(void* handle_snapshot) {
    constexpr uint32_t kFetchTimeoutMs = 200;
  bool trigger_on = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    trigger_on = IsTriggerOnUnlocked();
    if (trigger_on) MV3D_RGBD_SoftTrigger(handle_snapshot);
  }
    while (async_running_.load(std::memory_order_acquire)) {
        bool trigger_on = false;
        {
            if (!handle_ || handle_ != handle_snapshot) break;
        }

        MV3D_RGBD_FRAME_DATA st{};
        const MV3D_RGBD_STATUS fetch_st = MV3D_RGBD_FetchFrame(handle_snapshot, &st, kFetchTimeoutMs);
        if (!async_running_.load(std::memory_order_acquire)) break;
        if (fetch_st != MV3D_RGBD_OK) {
            //std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        UnifiedFrame u;
        FrameCallback cb_copy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!handle_ || handle_ != handle_snapshot) break;
            ConvertFrameData(handle_snapshot, &st, u);
            cb_copy = async_callback_;
        }
        if (cb_copy) cb_copy(u);
    }
}

}  // namespace scanner_viewer
