// 无 gRPC 时 SDK 桩：Connect 恒失败，便于先编译依赖 camera_driver / diag 的上层模块。

#include "camera_sdk/best_types.h"
#include "camera_sdk/developer_camera_sdk.h"
#include "camera_sdk/user_camera_sdk.h"

#include "camera_sdk_discovery.h"

#include "platform_diag/build_info.h"
#include "platform_diag/logging.h"

namespace camera3d::sdk {
namespace {

// 无 gRPC 时的 IDeveloperCameraSdk：接口恒失败或空操作，便于链接测试。
class DeveloperCameraSdkStub final : public IDeveloperCameraSdk {
 public:
  // Connect：仅记录 peer，返回 false。
  bool Connect(const std::string& hub_address, const std::string& device_ip, const std::string& session_hint,
               int timeout_ms, std::uint32_t projector_com_index) override {
    (void)device_ip;
    (void)session_hint;
    (void)timeout_ms;
    (void)projector_com_index;
    peer_ = hub_address;
    last_code_ = -1;
    last_msg_ = "CAMERA3D_USE_GRPC_STUB: no gRPC client";
    CAMERA3D_LOGW("camera_sdk：当前构建未链接 gRPC，Connect 无效");
    return false;
  }

  // Disconnect：清空 session。
  void Disconnect() override {
    session_.clear();
    peer_.clear();
  }

  // IsConnected：以 session_ 非空为准（stub Connect 失败时恒为 false）。
  bool IsConnected() const override { return !session_.empty(); }

  int GetLastErrorCode() const override { return last_code_; }
  std::string GetLastErrorMessage() const override { return last_msg_; }
  // GetLastHubStatusCode：桩无 Hub 业务码。
  int GetLastHubStatusCode() const override { return 0; }

  // CaptureSync/CaptureAsync：失败占位。
  bool CaptureSync(std::uint64_t* out_job_id, std::uint64_t, bool, bool, std::uint32_t, bool, bool) override {
    if (out_job_id) *out_job_id = 0;
    last_code_ = -2;
    last_msg_ = "stub";
    return false;
  }

  bool CaptureAsync(std::uint64_t* out_job_id, std::uint64_t, bool, bool, std::uint32_t, bool, bool) override {
    if (out_job_id) *out_job_id = 0;
    last_code_ = -2;
    last_msg_ = "stub";
    return false;
  }

  bool SetParameters(const std::vector<camera3d::best::ParameterValue>&) override { return false; }
  bool GetParameters(const std::vector<camera3d::best::ParameterType>&,
                     std::vector<camera3d::best::ParameterValue>*) override {
    return false;
  }

  bool GetDepthFrame(std::string*, std::uint64_t*, std::uint64_t*, std::uint64_t*, std::uint32_t*, std::uint32_t*,
                     std::uint32_t*, std::int64_t*, std::uint64_t) override {
    return false;
  }

  bool ListDepthCameraRawFrames(std::vector<camera3d::best::BestCameraRawFrameItem>* out,
                                std::uint64_t) override {
    if (out) {
      out->clear();
    }
    return true;
  }
  bool GetPointCloud(std::string*, std::uint64_t*, std::uint64_t*, std::uint64_t*, std::uint32_t*, std::uint32_t*,
                     std::uint32_t*, std::int64_t*) override {
    return false;
  }
  bool GetDetectionResult(std::string*, std::uint64_t*, std::uint64_t*, std::uint64_t*, std::uint32_t*, std::uint32_t*,
                          std::uint32_t*, std::int64_t*) override {
    return false;
  }

  bool GetLastCaptureInlineImage(std::string*, std::vector<std::uint8_t>*) const override { return false; }

  std::string LastRpcPeer() const override { return peer_; }
  std::string SessionId() const override { return session_; }

  std::string GetSdkVersion() const override { return camera3d::diag::kCamera3dStackVersion; }

  void SetDiagnosticLogLevel(const std::string& level) override {
    (void)level;
    CAMERA3D_LOGW("SetDiagnosticLogLevel ignored in stub build");
  }

  bool DiscoverDevices(std::vector<DiscoveredHubDevice>* out, std::uint16_t listen_udp_port,
                       int timeout_ms) override {
    return DiscoverHubDevicesUdp(out, listen_udp_port, timeout_ms);
  }

 private:
  std::string peer_;
  std::string session_;
  int last_code_ = 0;
  std::string last_msg_;
};

}  // namespace

IUserCameraSdk* CreateUserCameraSdk() {
  return new DeveloperCameraSdkStub();
}

IDeveloperCameraSdk* CreateDeveloperCameraSdk() {
  return new DeveloperCameraSdkStub();
}

void DestroyUserCameraSdk(IUserCameraSdk* ptr) {
  delete ptr;
}

void DestroyDeveloperCameraSdk(IDeveloperCameraSdk* ptr) {
  delete ptr;
}

}  // namespace camera3d::sdk
