#include "camera_driver/adapters.h"
#include "camera_driver/icamera_adapter.h"

#include "platform_diag/logging.h"

namespace camera3d::camera {

namespace {

// 联调用虚拟相机：固定 640x480 灰图，无真实硬件；满足 ICameraAdapter 全接口。
class NullCameraAdapter final : public ICameraAdapter {
 public:
  std::string BackendId() const override { return "null"; }

  std::vector<DeviceInfo> EnumerateDevices() override {
    DeviceInfo d;
    d.backend_id = BackendId();
    d.serial_number = "virtual0";
    d.ip = "127.0.0.1";
    d.model_name = "NullVirtual";
    d.max_width = 640;
    d.max_height = 480;
    return {d};
  }

  bool Open(const std::string& device_id) override {
    CAMERA3D_LOGI("NullCameraAdapter::Open {}", device_id);
    open_ = true;
    device_id_ = device_id;
    ClearErr();
    return true;
  }

  void Close() override {
    StopAsyncGrab();
    open_ = false;
    device_id_.clear();
    async_frame_cb_ = nullptr;
    async_result_cb_ = nullptr;
  }

  bool IsOpen() const override { return open_; }
  std::string CurrentDeviceId() const override { return device_id_; }

  bool SetExposureUs(double) override {
    if (!open_) return SetErr(CameraError::kNotOpen, "not open");
    return Ok();
  }
  bool GetExposureUs(double& out) const override {
    if (!open_) return SetErr(CameraError::kNotOpen, "not open");
    out = 1000.0;
    ClearErr();
    return true;
  }

  bool SetGainDb(double) override {
    if (!open_) return SetErr(CameraError::kNotOpen, "not open");
    return Ok();
  }
  bool GetGainDb(double& out) const override {
    if (!open_) return SetErr(CameraError::kNotOpen, "not open");
    out = 1.0;
    ClearErr();
    return true;
  }

  bool SetGamma(double) override {
    if (!open_) return SetErr(CameraError::kNotOpen, "not open");
    return Ok();
  }
  bool GetGamma(double& out) const override {
    if (!open_) return SetErr(CameraError::kNotOpen, "not open");
    out = 1.0;
    ClearErr();
    return true;
  }

  bool SetTriggerMode(TriggerMode) override {
    if (!open_) return SetErr(CameraError::kNotOpen, "not open");
    return Ok();
  }

  bool SetRoi(const RoiRect& roi) override {
    if (!open_) return SetErr(CameraError::kNotOpen, "not open");
    roi_ = roi;
    return Ok();
  }

  bool GetRoi(RoiRect& out_roi) const override {
    if (!open_) return SetErr(CameraError::kNotOpen, "not open");
    out_roi = roi_;
    ClearErr();
    return true;
  }

  bool GetDeviceInfo(DeviceInfo& out) const override {
    if (!open_) return SetErr(CameraError::kNotOpen, "not open");
    out.backend_id = BackendId();
    out.serial_number = device_id_;
    out.ip = "127.0.0.1";
    out.model_name = "NullVirtual";
    out.max_width = 640;
    out.max_height = 480;
    ClearErr();
    return true;
  }

  bool GrabOne(FrameBuffer& out, int) override {
    if (!open_) return SetErr(CameraError::kNotOpen, "not open");
    out.width = 640;
    out.height = 480;
    out.pixel_format = 0;
    out.frame_id = ++frame_seq_;
    out.timestamp_unix_ns = 0;
    out.bytes.assign(640 * 480, std::uint8_t{0});
    return Ok();
  }

  void SetCaptureResultCallback(CameraResultCallback cb) override { async_result_cb_ = std::move(cb); }

  void SetFrameCallback(FrameCallback cb) override { async_frame_cb_ = std::move(cb); }

  bool StartAsyncGrab() override {
    if (!open_) return SetErr(CameraError::kNotOpen, "not open");
    if (!async_frame_cb_ && !async_result_cb_) {
      return SetErr(CameraError::kInvalidParameter, "frame and result callbacks are both empty");
    }
    return Ok();
  }

  void StopAsyncGrab() override {}

  CameraError GetLastErrorCode() const override { return last_code_; }
  std::string GetLastErrorMessage() const override { return last_msg_; }

  std::shared_ptr<ICameraAdapter> CloneForSession() const override {
    return std::make_shared<NullCameraAdapter>();
  }

 private:
  bool Ok() {
    ClearErr();
    return true;
  }
  bool SetErr(CameraError c, const char* msg) const {
    last_code_ = c;
    last_msg_ = msg;
    return false;
  }
  void ClearErr() const {
    last_code_ = CameraError::kOk;
    last_msg_.clear();
  }

  bool open_ = false;
  std::string device_id_;
  RoiRect roi_{};
  FrameCallback async_frame_cb_;
  CameraResultCallback async_result_cb_;
  mutable CameraError last_code_ = CameraError::kOk;
  mutable std::string last_msg_;
  std::uint64_t frame_seq_ = 0;
};

}  // namespace

std::shared_ptr<ICameraAdapter> CreateNullCameraAdapter() {
  return std::make_shared<NullCameraAdapter>();
}

}  // namespace camera3d::camera
