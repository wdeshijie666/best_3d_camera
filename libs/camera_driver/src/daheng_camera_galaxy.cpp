#include "camera_driver/icamera_adapter.h"

#include "platform_diag/logging.h"

#include <GalaxyIncludes.h>

#include <cstring>
#include <cstdio>
#include <exception>
#include <regex>
#include <mutex>
#include <string>
#include <vector>

namespace camera3d::camera {
namespace {

void GalaxyEnsureInit() {
  static std::once_flag once;
  std::call_once(once, [] {
    try {
      IGXFactory::GetInstance().Init();
    } catch (CGalaxyException& e) {
      CAMERA3D_LOGE("IGXFactory::Init 失败: {} ({})", e.what(), e.GetErrorCode());
    }
  });
}

bool LooksLikeIpv4(const std::string& s) {
  static const std::regex kIpv4(R"(^(\d{1,3}\.){3}\d{1,3}$)");
  if (!std::regex_match(s, kIpv4)) return false;
  int a = 0, b = 0, c = 0, d = 0;
  if (std::sscanf(s.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) != 4) return false;
  return a >= 0 && a <= 255 && b >= 0 && b <= 255 && c >= 0 && c <= 255 && d >= 0 && d <= 255;
}

class DaHengGalaxyAdapter final : public camera3d::camera::ICameraAdapter {
  class CaptureBridge final : public ICaptureEventHandler {
   public:
    explicit CaptureBridge(DaHengGalaxyAdapter* outer) : outer_(outer) {}
    void DoOnImageCaptured(CImageDataPointer& img, void* pUserParam) override {
      (void)pUserParam;
      if (outer_) {
        outer_->OnGalaxyCaptured(img);
      }
    }

   private:
    DaHengGalaxyAdapter* outer_;
  };

 public:
  DaHengGalaxyAdapter() : capture_bridge_(this) {}
  ~DaHengGalaxyAdapter() override { Close(); }

  std::string BackendId() const override { return "daheng"; }

  std::vector<DeviceInfo> EnumerateDevices() override {
    std::vector<DeviceInfo> out;
    try {
      GalaxyEnsureInit();
      GxIAPICPP::gxdeviceinfo_vector devices;
      IGXFactory::GetInstance().UpdateDeviceList(1000, devices);
      out.reserve(devices.size());
      for (size_t i = 0; i < devices.size(); ++i) {
        DeviceInfo d;
        d.backend_id = "daheng";
        d.serial_number = std::string(devices[i].GetSN().c_str());
        // 枚举阶段不打开设备，型号名若 SDK 提供 GetModelName/GetDisplayName 可在此补全
        d.model_name.clear();
        d.max_width = 0;
        d.max_height = 0;
        out.push_back(std::move(d));
      }
    } catch (const CGalaxyException& e) {
      SetErr(CameraError::kBackendError, e.what());
      CAMERA3D_LOGW("EnumerateDevices: Galaxy {}", e.what());
    }
    return out;
  }

  bool Open(const std::string& device_id) override {
    Close();
    try {
      GalaxyEnsureInit();
      // 每次连接前先刷新设备列表，避免 SDK 设备缓存过期导致刚上电设备不可见。
      GxIAPICPP::gxdeviceinfo_vector refreshed;
      IGXFactory::GetInstance().UpdateDeviceList(1000, refreshed);
      if (LooksLikeIpv4(device_id)) {
        device_ = IGXFactory::GetInstance().OpenDeviceByIP(GxIAPICPP::gxstring(device_id.c_str()),
                                                           GX_ACCESS_EXCLUSIVE);
      } else {
        device_ = IGXFactory::GetInstance().OpenDeviceBySN(GxIAPICPP::gxstring(device_id.c_str()),
                                                           GX_ACCESS_EXCLUSIVE);
      }
      if (device_->GetStreamCount() <= 0) {
        SetErr(CameraError::kBackendError, "no stream on device");
        device_->Close();
        device_ = CGXDevicePointer();
        return false;
      }
      stream_ = device_->OpenStream(0);
      remote_ = device_->GetRemoteFeatureControl();
      device_id_ = device_id;
      ApplyTriggerMode(trigger_mode_);
      grab_running_ = false;
      open_ = true;
      SetErr(CameraError::kOk, "");
      return true;
    } catch (const CGalaxyException& e) {
      CleanupAfterFailure();
      SetErr(CameraError::kBackendError, e.what());
      CAMERA3D_LOGW("Open {}: {}", device_id, e.what());
      return false;
    }
  }

  void Close() override {
    StopAsyncGrab();
    if (!open_) {
      return;
    }
    try {
      if (!remote_.IsNull()) {
        remote_->GetCommandFeature("AcquisitionStop")->Execute();
        try {
          remote_->GetEnumFeature("TriggerMode")->SetValue("Off");
        } catch (...) {
        }
      }
      if (!stream_.IsNull()) {
        stream_->StopGrab();
        stream_->Close();
      }
      if (!device_.IsNull()) {
        device_->Close();
      }
    } catch (const CGalaxyException& e) {
      CAMERA3D_LOGW("Close: {}", e.what());
    }
    stream_ = CGXStreamPointer();
    device_ = CGXDevicePointer();
    remote_ = CGXFeatureControlPointer();
    device_id_.clear();
    open_ = false;
  }

  bool IsOpen() const override { return open_; }
  std::string CurrentDeviceId() const override { return device_id_; }

  bool SetExposureUs(double microseconds) override {
    if (!RequireOpen()) {
      return false;
    }
    try {
      remote_->GetFloatFeature("ExposureTime")->SetValue(microseconds);
      return true;
    } catch (const CGalaxyException& e) {
      SetErr(CameraError::kBackendError, e.what());
      return false;
    }
  }

  bool GetExposureUs(double& out_microseconds) const override {
    if (!open_ || remote_.IsNull()) {
      return false;
    }
    try {
      out_microseconds = remote_->GetFloatFeature("ExposureTime")->GetValue();
      return true;
    } catch (...) {
      return false;
    }
  }

  bool SetGainDb(double gain_db) override {
    if (!RequireOpen()) {
      return false;
    }
    try {
      remote_->GetFloatFeature("Gain")->SetValue(gain_db);
      return true;
    } catch (const CGalaxyException& e) {
      SetErr(CameraError::kBackendError, e.what());
      return false;
    }
  }

  bool GetGainDb(double& out_gain_db) const override {
    if (!open_ || remote_.IsNull()) {
      return false;
    }
    try {
      out_gain_db = remote_->GetFloatFeature("Gain")->GetValue();
      return true;
    } catch (...) {
      return false;
    }
  }

  bool SetGamma(double gamma) override {
    if (!RequireOpen()) {
      return false;
    }
    try {
      remote_->GetFloatFeature("Gamma")->SetValue(gamma);
      return true;
    } catch (const CGalaxyException& e) {
      (void)e;
      return false;
    }
  }

  bool GetGamma(double& out_gamma) const override {
    if (!open_ || remote_.IsNull()) {
      return false;
    }
    try {
      out_gamma = remote_->GetFloatFeature("Gamma")->GetValue();
      return true;
    } catch (...) {
      return false;
    }
  }

  bool SetTriggerMode(TriggerMode mode) override {
    trigger_mode_ = mode;
    if (!open_ || remote_.IsNull()) {
      return true;
    }
    try {
      ApplyTriggerMode(mode);
      return true;
    } catch (const CGalaxyException& e) {
      SetErr(CameraError::kBackendError, e.what());
      return false;
    }
  }

  bool SetRoi(const RoiRect& roi) override {
    if (!RequireOpen()) {
      return false;
    }
    try {
      if (roi.IsFullFrame()) {
        const int64_t w = remote_->GetIntFeature("WidthMax")->GetValue();
        const int64_t h = remote_->GetIntFeature("HeightMax")->GetValue();
        remote_->GetIntFeature("OffsetX")->SetValue(0);
        remote_->GetIntFeature("OffsetY")->SetValue(0);
        remote_->GetIntFeature("Width")->SetValue(w);
        remote_->GetIntFeature("Height")->SetValue(h);
        return true;
      }
      remote_->GetIntFeature("OffsetX")->SetValue(static_cast<int64_t>(roi.offset_x));
      remote_->GetIntFeature("OffsetY")->SetValue(static_cast<int64_t>(roi.offset_y));
      remote_->GetIntFeature("Width")->SetValue(static_cast<int64_t>(roi.width));
      remote_->GetIntFeature("Height")->SetValue(static_cast<int64_t>(roi.height));
      return true;
    } catch (const CGalaxyException& e) {
      SetErr(CameraError::kBackendError, e.what());
      return false;
    }
  }

  bool GetRoi(RoiRect& out_roi) const override {
    if (!open_ || remote_.IsNull()) {
      return false;
    }
    try {
      out_roi.offset_x = static_cast<std::uint32_t>(remote_->GetIntFeature("OffsetX")->GetValue());
      out_roi.offset_y = static_cast<std::uint32_t>(remote_->GetIntFeature("OffsetY")->GetValue());
      out_roi.width = static_cast<std::uint32_t>(remote_->GetIntFeature("Width")->GetValue());
      out_roi.height = static_cast<std::uint32_t>(remote_->GetIntFeature("Height")->GetValue());
      return true;
    } catch (...) {
      return false;
    }
  }

  bool GetDeviceInfo(DeviceInfo& out) const override {
    if (!open_ || device_.IsNull()) {
      return false;
    }
    try {
      const auto& info = device_->GetDeviceInfo();
      out.backend_id = "daheng";
      out.model_name = info.GetModelName().c_str();
      out.serial_number = info.GetSN().c_str();
      out.ip = LooksLikeIpv4(device_id_) ? device_id_ : std::string{};
      out.max_width = static_cast<std::uint32_t>(remote_->GetIntFeature("WidthMax")->GetValue());
      out.max_height = static_cast<std::uint32_t>(remote_->GetIntFeature("HeightMax")->GetValue());
      return true;
    } catch (...) {
      return false;
    }
  }

  bool GrabOne(FrameBuffer& out, int timeout_ms) override {
    if (!RequireOpen()) {
      return false;
    }
    {
      std::lock_guard<std::mutex> lock(cb_mu_);
      if (capture_registered_) {
        SetErr(CameraError::kInvalidParameter, "async grab active; call StopAsyncGrab first");
        return false;
      }
    }
    if (!grab_running_) {
      if (!StartStreamGrab()) {
        return false;
      }
    }
    CImageDataPointer img;
    try {
      img = stream_->DQBuf(static_cast<uint32_t>(timeout_ms));
      if (img.IsNull()) {
        SetErr(CameraError::kTimeout, "DQBuf returned null");
        return false;
      }
      if (img->GetStatus() != GX_FRAME_STATUS_SUCCESS) {
        stream_->QBuf(img);
        SetErr(CameraError::kBackendError, "frame status not success");
        return false;
      }
      const std::uint64_t payload = img->GetPayloadSize();
      out.bytes.resize(static_cast<size_t>(payload));
      if (payload > 0 && !out.bytes.empty()) {
        std::memcpy(out.bytes.data(), img->GetBuffer(), static_cast<size_t>(payload));
      }
      out.width = static_cast<std::uint32_t>(img->GetWidth());
      out.height = static_cast<std::uint32_t>(img->GetHeight());
      out.pixel_format = static_cast<std::uint32_t>(img->GetPixelFormat());
      out.frame_id = img->GetFrameID();
      out.timestamp_unix_ns = static_cast<std::int64_t>(img->GetTimeStamp());
      stream_->QBuf(img);
      SetErr(CameraError::kOk, "");
      return true;
    } catch (const CGalaxyException& e) {
      if (!img.IsNull()) {
        try {
          stream_->QBuf(img);
        } catch (...) {
        }
      }
      const std::string w = e.what();
      // GetErrorCode() 在部分 SDK 头文件中为非 const；此处仅用文案判断超时
      if (w.find("timeout") != std::string::npos || w.find("Timeout") != std::string::npos ||
          w.find("TIMEOUT") != std::string::npos) {
        SetErr(CameraError::kTimeout, e.what());
      } else {
        SetErr(CameraError::kBackendError, e.what());
      }
      return false;
    }
  }

  void SetCaptureResultCallback(CameraResultCallback cb) override {
    bool need_register = false;
    bool need_unregister = false;
    {
      std::lock_guard<std::mutex> lock(cb_mu_);
      user_result_cb_ = std::move(cb);
      const bool want = static_cast<bool>(user_result_cb_) || static_cast<bool>(user_frame_cb_);
      need_register = want && !capture_registered_;
      need_unregister = !want && capture_registered_;
    }
    if (!open_ || stream_.IsNull()) return;
    try {
      if (need_register) {
        StopStreamGrabIfRunning();
        stream_->RegisterCaptureCallback(&capture_bridge_, nullptr);
        std::lock_guard<std::mutex> lock(cb_mu_);
        capture_registered_ = true;
      } else if (need_unregister) {
        stream_->UnregisterCaptureCallback();
        std::lock_guard<std::mutex> lock(cb_mu_);
        capture_registered_ = false;
        StopStreamGrabIfRunning();
      }
    } catch (const CGalaxyException& e) {
      CAMERA3D_LOGW("SetCaptureResultCallback register/unregister failed: {}", e.what());
      SetErr(CameraError::kBackendError, e.what());
    }
  }

  void SetFrameCallback(FrameCallback cb) override {
    bool need_register = false;
    bool need_unregister = false;
    {
      std::lock_guard<std::mutex> lock(cb_mu_);
      user_frame_cb_ = std::move(cb);
      const bool want = static_cast<bool>(user_result_cb_) || static_cast<bool>(user_frame_cb_);
      need_register = want && !capture_registered_;
      need_unregister = !want && capture_registered_;
    }
    if (!open_ || stream_.IsNull()) return;
    try {
      if (need_register) {
        StopStreamGrabIfRunning();
        stream_->RegisterCaptureCallback(&capture_bridge_, nullptr);
        std::lock_guard<std::mutex> lock(cb_mu_);
        capture_registered_ = true;
      } else if (need_unregister) {
        stream_->UnregisterCaptureCallback();
        std::lock_guard<std::mutex> lock(cb_mu_);
        capture_registered_ = false;
        StopStreamGrabIfRunning();
      }
    } catch (const CGalaxyException& e) {
      CAMERA3D_LOGW("SetFrameCallback register/unregister failed: {}", e.what());
      SetErr(CameraError::kBackendError, e.what());
    }
  }

  bool StartAsyncGrab() override {
    if (!RequireOpen()) {
      return false;
    }
    {
      std::lock_guard<std::mutex> lock(cb_mu_);
      if (!user_frame_cb_ && !user_result_cb_) {
        SetErr(CameraError::kInvalidParameter, "frame and result callbacks are both empty");
        return false;
      }
    }
    try {
      StopStreamGrabIfRunning();
      if (!stream_.IsNull()) {
        try {
          stream_->UnregisterCaptureCallback();
        } catch (...) {
        }
      }
      stream_->RegisterCaptureCallback(&capture_bridge_, nullptr);
      std::lock_guard<std::mutex> lock(cb_mu_);
      capture_registered_ = true;
    } catch (const CGalaxyException& e) {
      SetErr(CameraError::kBackendError, e.what());
      CAMERA3D_LOGW("RegisterCaptureCallback: {}", e.what());
      std::lock_guard<std::mutex> lock(cb_mu_);
      capture_registered_ = false;
      return false;
    }
    if (!StartStreamGrab()) {
      try {
        if (!stream_.IsNull()) {
          stream_->UnregisterCaptureCallback();
        }
      } catch (const CGalaxyException& e) {
        CAMERA3D_LOGW("StartAsyncGrab rollback UnregisterCaptureCallback: {}", e.what());
      }
      std::lock_guard<std::mutex> lock(cb_mu_);
      capture_registered_ = false;
      return false;
    }
    SetErr(CameraError::kOk, "");
    return true;
  }

  bool StartStreamGrab() override {
    if (!RequireOpen()) {
      return false;
    }
    if (grab_running_) {
      return true;
    }
    try {
      stream_->StartGrab();
      remote_->GetCommandFeature("AcquisitionStart")->Execute();
      grab_running_ = true;
      SetErr(CameraError::kOk, "");
      return true;
    } catch (const CGalaxyException& e) {
      grab_running_ = false;
      SetErr(CameraError::kBackendError, e.what());
      CAMERA3D_LOGW("StartStreamGrab: {}", e.what());
      return false;
    }
  }

  void StopAsyncGrab() override {
    try {
      if (!stream_.IsNull()) {
        stream_->UnregisterCaptureCallback();
      }
    } catch (const CGalaxyException& e) {
      CAMERA3D_LOGW("UnregisterCaptureCallback: {}", e.what());
    }
    {
      std::lock_guard<std::mutex> lock(cb_mu_);
      capture_registered_ = false;
      user_frame_cb_ = nullptr;
      user_result_cb_ = nullptr;
    }
    StopStreamGrabIfRunning();
  }

  CameraError GetLastErrorCode() const override { return last_code_; }
  std::string GetLastErrorMessage() const override { return last_msg_; }

  std::shared_ptr<ICameraAdapter> CloneForSession() const override {
    return std::shared_ptr<ICameraAdapter>(new DaHengGalaxyAdapter());
  }

 private:
  static void CopyImageToFrameBuffer(FrameBuffer& out, CImageDataPointer& img) {
    const std::uint64_t payload = img->GetPayloadSize();
    out.bytes.resize(static_cast<size_t>(payload));
    if (payload > 0 && !out.bytes.empty()) {
      std::memcpy(out.bytes.data(), img->GetBuffer(), static_cast<size_t>(payload));
    }
    out.width = static_cast<std::uint32_t>(img->GetWidth());
    out.height = static_cast<std::uint32_t>(img->GetHeight());
    out.pixel_format = static_cast<std::uint32_t>(img->GetPixelFormat());
    out.frame_id = img->GetFrameID();
    out.timestamp_unix_ns = static_cast<std::int64_t>(img->GetTimeStamp());
  }

  void OnGalaxyCaptured(CImageDataPointer& img) {
    FrameCallback frame_cb;
    CameraResultCallback result_cb;
    {
      std::lock_guard<std::mutex> lock(cb_mu_);
      frame_cb = user_frame_cb_;
      result_cb = user_result_cb_;
    }
    try {
      if (result_cb) {
        FrameBufferCameraResult stack_res;
        if (!img.IsNull() && img->GetStatus() == GX_FRAME_STATUS_SUCCESS) {
          CopyImageToFrameBuffer(stack_res.frame, img);
          stack_res.success = true;
        } else {
          stack_res.success = false;
          stack_res.error_message = "galaxy frame not success";
        }
        result_cb(&stack_res);
      }
      if (frame_cb && !img.IsNull() && img->GetStatus() == GX_FRAME_STATUS_SUCCESS) {
        FrameBuffer fb;
        CopyImageToFrameBuffer(fb, img);
        frame_cb(fb);
      }
    } catch (const std::exception& e) {
      CAMERA3D_LOGW("OnGalaxyCaptured 用户回调异常: {}", e.what());
    } catch (...) {
      CAMERA3D_LOGW("OnGalaxyCaptured 用户回调未知异常");
    }
    // RegisterCaptureCallback 模式下 Galaxy 禁止在回调内 stream_->QBuf（会报 Can't call QBuf after register capture callback）；
    // 缓冲区由 SDK 在回调返回后自行回收，勿在此 QBuf。
  }

  void ApplyTriggerMode(TriggerMode mode) {
    remote_->GetEnumFeature("TriggerMode")->SetValue("On");
    if (mode == TriggerMode::kSoftware) {
      remote_->GetEnumFeature("TriggerSource")->SetValue("Software");
    } else {
      // 与 geely DaHengCamera.cpp 中注释的硬触发片段一致；具体线号以机型手册为准
      remote_->GetEnumFeature("TriggerSource")->SetValue("Line0");
      remote_->GetEnumFeature("TriggerActivation")->SetValue("RisingEdge");
    }
  }

  void StopStreamGrabIfRunning() {
    if (!grab_running_) {
      return;
    }
    try {
      if (!remote_.IsNull()) {
        try {
          remote_->GetCommandFeature("AcquisitionStop")->Execute();
        } catch (...) {
        }
      }
      if (!stream_.IsNull()) {
        stream_->StopGrab();
      }
    } catch (const CGalaxyException& e) {
      CAMERA3D_LOGW("StopStreamGrabIfRunning: {}", e.what());
    }
    grab_running_ = false;
  }

  void CleanupAfterFailure() {
    grab_running_ = false;
    try {
      if (!stream_.IsNull()) {
        stream_->StopGrab();
        stream_->Close();
      }
    } catch (...) {
    }
    try {
      if (!device_.IsNull()) {
        device_->Close();
      }
    } catch (...) {
    }
    stream_ = CGXStreamPointer();
    device_ = CGXDevicePointer();
    remote_ = CGXFeatureControlPointer();
    device_id_.clear();
    open_ = false;
  }

  bool RequireOpen() {
    if (!open_ || stream_.IsNull() || remote_.IsNull()) {
      SetErr(CameraError::kNotOpen, "camera not open");
      return false;
    }
    return true;
  }

  void SetErr(CameraError c, const char* msg) {
    last_code_ = c;
    last_msg_ = msg ? msg : "";
  }

  // SDK 的 IGXFeatureControl / IGXDevice 等 getter 非 const；适配器接口仍有 const 查询成员，故句柄用 mutable。
  mutable CGXDevicePointer device_;
  mutable CGXStreamPointer stream_;
  mutable CGXFeatureControlPointer remote_;
  std::string device_id_;
  bool open_ = false;
  TriggerMode trigger_mode_ = TriggerMode::kSoftware;
  CameraError last_code_ = CameraError::kOk;
  std::string last_msg_;

  CaptureBridge capture_bridge_;
  std::mutex cb_mu_;
  FrameCallback user_frame_cb_;
  CameraResultCallback user_result_cb_;
  bool capture_registered_ = false;
  bool grab_running_ = false;
};

}  // namespace

std::shared_ptr<ICameraAdapter> CreateDaHengGalaxyCameraAdapter() {
  return std::shared_ptr<ICameraAdapter>(new DaHengGalaxyAdapter());
}

}  // namespace camera3d::camera
