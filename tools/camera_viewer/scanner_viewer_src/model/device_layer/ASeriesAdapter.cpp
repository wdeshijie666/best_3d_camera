/**
 * @file ASeriesAdapter.cpp
 * @brief A 系列适配器实现：连接、同步/异步采集、参数、帧格式转换
 */
#include "ASeriesAdapter.h"
#include "DeviceModelTags.h"
#include "../data_center/UnifiedFrame.h"
#include "AIRScanner.hpp"
#include "AIRFrame.hpp"
#include "AIRTypes.h"
#include <cstring>

namespace scanner_viewer {

namespace {

DeviceInfo FromAIRDeviceInfo(const air_scanner::AIRDeviceInfo& info) {
    DeviceInfo d;
    d.name = info.name;
    d.ip = info.ip;
    d.serial_number = info.serial_number;
    d.firmware_version = info.firmware_version;
    d.port = info.port;
    d.model_type = static_cast<int>(info.model);
    d.model_name = kDeviceModelASeries;
    return d;
}

}  // namespace

ASeriesAdapter::ASeriesAdapter() : scanner_(std::make_unique<air_scanner::Scanner3D>()) {}

ASeriesAdapter::~ASeriesAdapter() {
    if (IsAsyncCapturing()) {
        StopAsyncCapture();
    }
    Disconnect();
}

bool ASeriesAdapter::Connect(const DeviceInfo* device_info, unsigned int timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!scanner_) return false;
    AIRstatus st;
    if (device_info && !device_info->ip.empty()) {
        st = scanner_->Connect(device_info->ip, timeout_ms);
    } else {
        std::vector<air_scanner::AIRDeviceInfo> infos;
        if (!air_scanner::Scanner3D::EnumScanners(infos, static_cast<int>(timeout_ms)) || infos.empty()) {
            return false;
        }
        st = scanner_->Connect(infos[0], timeout_ms);
    }
    return (st == kSuccess);
}

void ASeriesAdapter::Disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (scanner_) {
        scanner_->Disconnect();
    }
}

bool ASeriesAdapter::IsConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return scanner_ ? scanner_->IsConnected() : false;
}

bool ASeriesAdapter::GetDeviceInfo(DeviceInfo& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!scanner_ || !scanner_->IsConnected()) return false;
    air_scanner::AIRDeviceInfo info;
    if (scanner_->GetScannerInfo(info) != kSuccess) return false;
    out = FromAIRDeviceInfo(info);
    return true;
}

bool ASeriesAdapter::GetParameterList(std::vector<ParamMeta>& out) const {
    out.clear();
    auto add_int = [&out](int id, const char* name, const char* desc, const char* unit, int min_v, int max_v, bool dev = false) {
        ParamMeta m;
        m.id = id;
        m.name = name;
        m.description = desc;
        m.unit = unit;
        m.min_val = min_v;
        m.max_val = max_v;
        m.developer_only = dev;
        m.kind = ParamKind::Int;
        out.push_back(m);
    };
    auto add_bool = [&out](int id, const char* name, const char* desc, bool dev = false) {
        ParamMeta m;
        m.id = id;
        m.name = name;
        m.description = desc;
        m.unit = "";
        m.min_val = 0;
        m.max_val = 1;
        m.developer_only = dev;
        m.kind = ParamKind::Bool;
        out.push_back(m);
    };
    auto add_enum = [&out](int id, const char* name, const char* desc, const char* unit,
                          std::vector<std::string> labels, std::vector<int> values = {}, bool dev = false) {
        ParamMeta m;
        m.id = id;
        m.name = name;
        m.description = desc;
        m.unit = unit;
        m.min_val = 0;
        m.max_val = static_cast<int>(labels.size()) - 1;
        m.developer_only = dev;
        m.kind = ParamKind::Enum;
        m.enum_labels = std::move(labels);
        if (!values.empty()) m.enum_values = std::move(values);
        out.push_back(m);
    };

    // 0: 编码模式（SDK 取值为 0,1,4,5,6,7,9,96,97,98,99）
    add_enum(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_PATTERN_MODE), "编码模式", "结构光编码模式", "",
             {"Gray8_PS4", "XORCode8_PS4", "XORCode8_PS6", "LineMove", "MinSWCode8_PS8", "LineMove2", "TimeMultiplexed", "Calib2", "Calib", "SolidColor", "None"},
             {0, 1, 4, 5, 6, 7, 9, 96, 97, 98, 99}, true);
    add_int(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_GRAY_THRESHOLD), "灰度阈值", "灰度阈值，范围 1-100，默认 5", "", 1, 100, true);
    add_int(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_EXPOSURE_TIME), "曝光时间", "曝光时间，单位 us，>=1677", "us", 1677, 1000000, false);
    add_int(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_GAIN), "增益", "增益范围 0-20，默认 0", "", 0, 20, false);
    add_int(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_PROJECTOR_INTENSITY), "光机强度", "光机强度百分比 1-100", "%", 1, 100, false);
    // 6: 重建模式
    add_enum(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_RECONSTRUCTION_MODE), "重建模式", "立体/左单目/右单目/融合", "",
             {"立体", "左单目", "右单目", "融合"}, {}, false);
    add_int(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_ROI_X), "ROI X", "ROI 左上角 X 坐标", "px", 0, 10000, true);
    add_int(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_ROI_Y), "ROI Y", "ROI 左上角 Y 坐标", "px", 0, 10000, true);
    add_int(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_ROI_Z), "ROI Z", "ROI 深度范围相关", "", 0, 10000, true);
    add_int(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_DOWN_SAMPLE), "下采样", "下采样因子 1/2/4/5/16 等", "", 1, 16, true);
    add_int(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_KEEP_ALIVE_TIME_OUT), "保活超时", "保活超时，单位 ms", "ms", 0, 300000, true);
    add_bool(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_ENABLE_TEXTURE), "启用纹理", "0 关闭 1 开启纹理图像");
    add_bool(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_ENABLE_OUTLIER_FILTER), "离群滤波", "0 关闭 1 开启离群点滤波");
    add_bool(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_ENABLE_SMOOTHING), "平滑", "0 关闭 1 开启平滑滤波");
    add_bool(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_ENABLE_DEPTH_COMPRESSION), "深度压缩", "0 关闭 1 开启深度压缩", true);
    add_bool(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_ENABLE_TEXTURE_SUPPLEMENTARY), "纹理补全", "0 关闭 1 开启纹理补全", true);
    add_int(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_TEXTURE_EXPOSURE_TIME), "纹理曝光", "纹理曝光时间，单位 us", "us", 100, 1000000, false);
    add_enum(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_OUTLIER_FILTER_LEVEL), "离群滤波强度", "弱/中/强", "", {"弱", "中", "强"}, {}, false);
    add_enum(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_SMOOTHING_FILTER_LEVEL), "平滑强度", "弱/中/强", "", {"弱", "中", "强"}, {}, false);
    add_int(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_CAMERA_CROP_RECT), "相机裁剪矩形", "x_left,y_left,x_right,y_right,width,height 多值", "px", 0, 10000, true);
    add_bool(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_ENABLE_REMOTE_LOGGING), "远程日志", "0 关闭 1 开启远程日志上报", true);
    add_enum(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_LOG_LEVEL), "日志级别", "trace/debug/info/warning/error/fatal/off", "",
             {"trace", "debug", "info", "warning", "error", "fatal", "off"}, {}, true);
    add_enum(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_ALIGNMENT_MODE), "对齐模式", "纹理到深度/深度到纹理/无", "",
             {"Texture2Depth", "Depth2Texture", "None"}, {}, true);
    add_enum(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_DEPTH_UNIT), "深度单位", "毫米/米", "", {"mm", "m"}, {}, true);
    add_bool(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_ENABLE_10BIT), "10bit 模式", "0 关闭 1 开启 10bit", true);
    add_int(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_MIN_FRAME_TIME_US), "最小帧间隔", "最小帧时间，单位 us，默认 26000", "us", 1000, 1000000, true);
    add_bool(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_ENABLE_AUTO_WHITE_BALANCE), "自动白平衡", "0 关闭 1 开启自动白平衡");
    add_int(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_GAMMA), "Gamma", "Gamma 值 [10,400]*0.01，默认 100 即 1.0", "", 10, 400, true);
    add_int(static_cast<int>(air_scanner::AIR_CONFIG_TYPE_WORK_DISTANCE), "工作距离", "工作距离，单位 mm", "mm", 0, 10000, false);
    return true;
}

bool ASeriesAdapter::GetParameter(int id, std::vector<int>& values) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!scanner_ || !scanner_->IsConnected()) return false;
    return scanner_->GetValue(static_cast<air_scanner::AIRConfigType>(id), values) == kSuccess;
}

bool ASeriesAdapter::SetParameter(int id, const std::vector<int>& values) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!scanner_ || !scanner_->IsConnected()) return false;
    return scanner_->SetValue(static_cast<air_scanner::AIRConfigType>(id), values) == kSuccess;
}

void ASeriesAdapter::ConvertFrame(const void* air_frame, UnifiedFrame& out) {
    const auto* f = static_cast<const air_scanner::AIRFrame*>(air_frame);
    if (!f || !f->IsValid()) return;
    const FrameConvertDemand demand = GetFrameConvertDemand();
    out.timestamp = std::chrono::steady_clock::now();
    out.frame_index = 0;

    if (demand.need_depth) {
        const int w = f->Width();
        const int h = f->Height();
        const float* depth = f->DepthMap();
        if (depth && w > 0 && h > 0) {
            out.depth.width = w;
            out.depth.height = h;
            out.depth.channels = 1;
            out.depth.is_16bit = false;
            size_t sz = static_cast<size_t>(w) * h * sizeof(float);
            out.depth.data.resize(sz);
            std::memcpy(out.depth.data.data(), depth, sz);
        }
    }

    if (demand.need_texture) {
        const int tw = f->TextureWidth();
        const int th = f->TextureHeight();
        const int tc = f->TextureChannelCount();
        for (size_t i = 0; i < f->TextureCount(); i++) {
            const uint8_t* tex = f->TextureMap(i);
            if (!tex || tw <= 0 || th <= 0) continue;
            UnifiedImage img;
            img.width = tw;
            img.height = th;
            img.channels = tc > 0 ? tc : 3;
            img.is_16bit = false;
            img.data.assign(tex, tex + static_cast<size_t>(tw) * th * img.channels);
            out.textures.push_back(std::move(img));
        }
    }
}

namespace {

void FillPointCloudFromAIR(UnifiedFrame& out, const AIRPointCloud& air_pc) {
    out.point_cloud.clear();
    if (!air_pc.points || air_pc.points_count <= 0) return;
    out.point_cloud.reserve(static_cast<size_t>(air_pc.points_count));
    for (int i = 0; i < air_pc.points_count; ++i) {
        const auto& p = air_pc.points[i];
        UnifiedPoint up;
        up.x = p.x;
        up.y = p.y;
        up.z = p.z;
        up.r = p.r;
        up.g = p.g;
        up.b = p.b;
        up.has_color = true;
        out.point_cloud.push_back(up);
    }
}

}  // namespace

bool ASeriesAdapter::CaptureSync(UnifiedFrame& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!scanner_ || !scanner_->IsConnected()) return false;
    const FrameConvertDemand demand = GetFrameConvertDemand();
    air_scanner::AIRFrame air_frame;
    if (scanner_->Capture(air_frame) != kSuccess) return false;
    ConvertFrame(&air_frame, frame);
    if (demand.need_point_cloud) {
        AIRPointCloud air_pc = {};
        if (scanner_->ResolvePointCloud(air_frame, air_pc, false, true, 1) == kSuccess)
            FillPointCloudFromAIR(frame, air_pc);
    }
    return true;
}

bool ASeriesAdapter::StartAsyncCapture(FrameCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!scanner_ || !scanner_->IsConnected() || !callback) return false;
    async_callback_ = std::move(callback);
    async_running_ = true;
    air_scanner::AIRFrameArrivedCallback sdk_cb = [this](const air_scanner::AIRFrame& air_frame, const air_scanner::AIRUserContext&) {
        if (!async_running_) return;
        const FrameConvertDemand demand = GetFrameConvertDemand();
        UnifiedFrame u;
        ConvertFrame(&air_frame, u);
        if (demand.need_point_cloud) {
            AIRPointCloud air_pc = {};
            if (scanner_ && scanner_->ResolvePointCloud(air_frame, air_pc, false, true, 1) == kSuccess)
                FillPointCloudFromAIR(u, air_pc);
        }
        if (async_callback_) async_callback_(u);
    };
    if (scanner_->RegisterAsyncCaptureCallback(sdk_cb) != kSuccess) {
        async_running_ = false;
        return false;
    }
    AIRAsyncCaptureConfig config = {};
    config.capture_mode = AIR_CAPTURE_MODE_CONTINUOUS;
    bool ok = scanner_->StartAsyncCapture(config) == kSuccess;
    if (!ok) async_running_ = false;
    return ok;
}

void ASeriesAdapter::StopAsyncCapture() {
    async_running_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (scanner_) {
        scanner_->StopAsyncCapture();
        scanner_->UnregisterAsyncCaptureCallback();
    }
    async_callback_ = nullptr;
}

bool ASeriesAdapter::IsAsyncCapturing() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return scanner_ ? scanner_->IsAsyncCapturing() : false;
}

}  // namespace scanner_viewer
