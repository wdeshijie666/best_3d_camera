#include "camera_driver/adapters.h"
#include "camera_driver/icamera_adapter.h"

#include "platform_diag/logging.h"

namespace camera3d::camera {

namespace {

class DaHengCameraAdapterStub final : public ICameraAdapter {
 public:
  std::string BackendId() const override { return "daheng"; }

  std::vector<DeviceInfo> EnumerateDevices() override {
    SetErr(CameraError::kUnsupported, "GxIAPI not linked");
    CAMERA3D_LOGW("DaHengCameraAdapterStub：未链接 GxIAPI，EnumerateDevices 为空");
    return {};
  }

  bool Open(const std::string& device_id) override {
    (void)device_id;
    SetErr(CameraError::kBackendError, "DaHeng stub: link GxIAPI and implement Open");
    CAMERA3D_LOGW("DaHengCameraAdapterStub::Open {} — 请实现 GxIAPI 适配器", device_id);
    return false;
  }

  void Close() override {
    open_ = false;
    device_id_.clear();
  }

  bool IsOpen() const override { return open_; }
  std::string CurrentDeviceId() const override { return device_id_; }

  bool SetExposureUs(double) override { return RequireOpen(); }
  bool GetExposureUs(double&) const override { return false; }
  bool SetGainDb(double) override { return RequireOpen(); }
  bool GetGainDb(double&) const override { return false; }
  bool SetGamma(double) override { return RequireOpen(); }
  bool GetGamma(double&) const override { return false; }
  bool SetTriggerMode(TriggerMode) override { return RequireOpen(); }
  bool SetRoi(const RoiRect&) override { return RequireOpen(); }
  bool GetRoi(RoiRect&) const override { return false; }
  bool GetDeviceInfo(DeviceInfo&) const override { return false; }
  bool GrabOne(FrameBuffer&, int) override { return RequireOpen(); }

  void SetCaptureResultCallback(CameraResultCallback) override {}
  void SetFrameCallback(FrameCallback) override {}
  bool StartAsyncGrab() override { return RequireOpen(); }
  void StopAsyncGrab() override {}

  CameraError GetLastErrorCode() const override { return last_code_; }
  std::string GetLastErrorMessage() const override { return last_msg_; }

  std::shared_ptr<ICameraAdapter> CloneForSession() const override {
    return std::make_shared<DaHengCameraAdapterStub>();
  }

 private:
  bool RequireOpen() {
    if (!open_) {
      SetErr(CameraError::kNotOpen, "camera not open (stub never opens)");
      return false;
    }
    return true;
  }
  void SetErr(CameraError c, const char* msg) {
    last_code_ = c;
    last_msg_ = msg;
  }

  bool open_ = false;
  std::string device_id_;
  CameraError last_code_ = CameraError::kOk;
  std::string last_msg_;
};

}  // namespace

std::shared_ptr<ICameraAdapter> CreateDaHengCameraAdapterStub() {
  return std::make_shared<DaHengCameraAdapterStub>();
}

}  // namespace camera3d::camera
