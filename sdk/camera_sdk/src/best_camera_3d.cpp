// BestCamera3D：将 IDeveloperCameraSdk 布尔结果映射为 BestStatus，并填充 BestShmFrameRef 等输出。

#include "camera_sdk/best_camera_3d.h"
#include "camera_sdk/developer_camera_sdk.h"

#include "camera_sdk_discovery.h"

#include "platform_diag/build_info.h"

#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace camera3d::best {

struct BestCamera3D::Impl {
  std::unique_ptr<camera3d::sdk::IDeveloperCameraSdk> sdk;
  BestEventCallback event_cb = nullptr;
  BestUserContext event_user = nullptr;
};

namespace {

const char* LogLevelToString(BestLogLevel l) {
  switch (l) {
    case BestLogLevel::kTrace:
      return "trace";
    case BestLogLevel::kDebug:
      return "debug";
    case BestLogLevel::kInfo:
      return "info";
    case BestLogLevel::kWarn:
      return "warn";
    case BestLogLevel::kError:
      return "error";
    case BestLogLevel::kCritical:
      return "critical";
  }
  return "info";
}

BestStatus StatusFromIo(bool ok, const camera3d::sdk::IDeveloperCameraSdk* sdk) {
  if (ok) return BestStatus::kSuccess;
  if (sdk && !sdk->IsConnected()) return BestStatus::kNotConnected;
  (void)sdk;
  return BestStatus::kFail;
}

void FillShmFromSdk(bool ok, camera3d::sdk::IDeveloperCameraSdk* sdk, BestShmFrameRef& out,
                    const std::string& region, std::uint64_t seq, std::uint64_t offset, std::uint64_t size,
                    std::uint32_t w, std::uint32_t h, std::uint32_t fmt, std::int64_t ts) {
  if (!ok) return;
  out.region_name = region;
  out.seq = seq;
  out.offset_bytes = offset;
  out.size_bytes = size;
  out.width = w;
  out.height = h;
  out.pixel_format = fmt;
  out.timestamp_unix_ns = ts;
  (void)sdk;
}

}  // namespace

// --- BestCamera3D：委托 IDeveloperCameraSdk，状态码映射见 StatusFromIo ---

// 实现 BestCamera3D::BestCamera3D：创建 Impl 与默认 gRPC SDK。
BestCamera3D::BestCamera3D() : impl_(std::make_unique<Impl>()) {
  impl_->sdk.reset(camera3d::sdk::CreateDeveloperCameraSdk());
}

// 实现 BestCamera3D::~BestCamera3D：DestroyDeveloperCameraSdk。
BestCamera3D::~BestCamera3D() {
  if (impl_ && impl_->sdk) {
    camera3d::sdk::DestroyDeveloperCameraSdk(impl_->sdk.release());
  }
}

BestCamera3D::BestCamera3D(BestCamera3D&&) noexcept = default;
BestCamera3D& BestCamera3D::operator=(BestCamera3D&&) noexcept = default;

// 实现 BestCamera3D::SdkVersion：栈版本常量。
std::string BestCamera3D::SdkVersion() { return camera3d::diag::kCamera3dStackVersion; }

BestStatus BestCamera3D::DiscoverDevices(std::vector<BestDeviceInfo>& out, std::uint16_t listen_udp_port,
                                         int timeout_ms) {
  out.clear();
  std::vector<camera3d::sdk::DiscoveredHubDevice> raw;
  if (!camera3d::sdk::DiscoverHubDevicesUdp(&raw, listen_udp_port, timeout_ms)) {
    return BestStatus::kFail;
  }
  out.reserve(raw.size());
  for (const auto& d : raw) {
    BestDeviceInfo b;
    b.display_name = d.model + " " + d.serial_number;
    b.hub_host = d.hub_host;
    b.hub_port = d.hub_port;
    b.serial_number = d.serial_number;
    b.mac_address = d.mac_address;
    b.model = d.model;
    // 统一配置 Hub 下 Connect 的 device_ip 仅占位；真实设备地址需由应用或后续协议字段补充。
    b.device_address = BestDeviceInfo::DefaultSimulator().device_address;
    b.projector_com_index = 0;
    out.push_back(std::move(b));
  }
  return BestStatus::kSuccess;
}

// 实现 BestCamera3D::Connect(host)：填充 DefaultSimulator 后转调 Connect(device)。
BestStatus BestCamera3D::Connect(const std::string& hub_ip_or_hostport, unsigned timeout_ms) {
  BestDeviceInfo d = BestDeviceInfo::DefaultSimulator();
  if (hub_ip_or_hostport.find(':') != std::string::npos) {
    d.hub_host = hub_ip_or_hostport;
    d.hub_port = 0;
  } else {
    d.hub_host = hub_ip_or_hostport;
  }
  return Connect(d, timeout_ms);
}

// 实现 BestCamera3D::Connect(device)：校验 target 与 device_address 后 sdk->Connect。
BestStatus BestCamera3D::Connect(const BestDeviceInfo& device, unsigned timeout_ms) {
  if (!impl_ || !impl_->sdk) return BestStatus::kFail;
  const std::string target = device.BuildHubGrpcTarget();
  if (target.empty() || device.device_address.empty()) return BestStatus::kInvalidParameter;
  const bool ok = impl_->sdk->Connect(target, device.device_address, device.session_hint,
                                      static_cast<int>(timeout_ms), device.projector_com_index);
  return ok ? BestStatus::kSuccess : BestStatus::kFail;
}

// 实现 BestCamera3D::Disconnect。
BestStatus BestCamera3D::Disconnect() {
  if (!impl_ || !impl_->sdk) return BestStatus::kFail;
  impl_->sdk->Disconnect();
  return BestStatus::kSuccess;
}

// 实现 BestCamera3D::IsConnected。
bool BestCamera3D::IsConnected() const {
  return impl_ && impl_->sdk && impl_->sdk->IsConnected();
}

// 实现 BestCamera3D::SetDiagnosticLogLevel(level)：枚举转字符串。
BestStatus BestCamera3D::SetDiagnosticLogLevel(BestLogLevel level) {
  return SetDiagnosticLogLevel(LogLevelToString(level));
}

// 实现 BestCamera3D::SetDiagnosticLogLevel(string)。
BestStatus BestCamera3D::SetDiagnosticLogLevel(const std::string& level) {
  if (!impl_ || !impl_->sdk) return BestStatus::kFail;
  impl_->sdk->SetDiagnosticLogLevel(level);
  return BestStatus::kSuccess;
}

// 实现 BestCamera3D::CaptureSync：sdk->CaptureSync，client_capture_id 固定 0 由 SDK 生成。
BestStatus BestCamera3D::CaptureSync(std::uint64_t* out_job_id, bool with_detection_pipeline,
                                     bool with_reconstruction_pipeline, std::uint32_t projector_op,
                                     bool test_recon_shm_loopback, bool test_inline_image_reply) {
  if (!impl_ || !impl_->sdk) return BestStatus::kFail;
  const bool ok = impl_->sdk->CaptureSync(out_job_id, 0, with_detection_pipeline, with_reconstruction_pipeline,
                                           projector_op, test_recon_shm_loopback, test_inline_image_reply);
  return ok ? BestStatus::kSuccess : StatusFromIo(false, impl_->sdk.get());
}

// 实现 BestCamera3D::CaptureAsync。
BestStatus BestCamera3D::CaptureAsync(std::uint64_t* out_job_id, bool with_detection_pipeline,
                                      bool with_reconstruction_pipeline, std::uint32_t projector_op,
                                      bool test_recon_shm_loopback, bool test_inline_image_reply) {
  if (!impl_ || !impl_->sdk) return BestStatus::kFail;
  const bool ok = impl_->sdk->CaptureAsync(out_job_id, 0, with_detection_pipeline, with_reconstruction_pipeline,
                                            projector_op, test_recon_shm_loopback, test_inline_image_reply);
  return ok ? BestStatus::kSuccess : StatusFromIo(false, impl_->sdk.get());
}

// 实现 BestCamera3D::SetParameters。
BestStatus BestCamera3D::SetParameters(const std::vector<ParameterValue>& params) {
  if (!impl_ || !impl_->sdk) return BestStatus::kFail;
  const bool ok = impl_->sdk->SetParameters(params);
  return ok ? BestStatus::kSuccess : StatusFromIo(false, impl_->sdk.get());
}

// 实现 BestCamera3D::GetParameters。
BestStatus BestCamera3D::GetParameters(const std::vector<ParameterType>& types,
                                       std::vector<ParameterValue>* out_values) {
  if (!impl_ || !impl_->sdk || !out_values) return BestStatus::kFail;
  const bool ok = impl_->sdk->GetParameters(types, out_values);
  return ok ? BestStatus::kSuccess : StatusFromIo(false, impl_->sdk.get());
}

// 实现 BestCamera3D::QueryDepthFrame：GetDepthFrame + FillShmFromSdk。
BestStatus BestCamera3D::QueryDepthFrame(BestShmFrameRef& out, std::uint64_t client_capture_id) {
  if (!impl_ || !impl_->sdk) return BestStatus::kFail;
  std::string region;
  std::uint64_t seq = 0, off = 0, sz = 0;
  std::uint32_t w = 0, h = 0, fmt = 0;
  std::int64_t ts = 0;
  const bool ok =
      impl_->sdk->GetDepthFrame(&region, &seq, &off, &sz, &w, &h, &fmt, &ts, client_capture_id);
  FillShmFromSdk(ok, impl_->sdk.get(), out, region, seq, off, sz, w, h, fmt, ts);
  return ok ? BestStatus::kSuccess : StatusFromIo(false, impl_->sdk.get());
}

BestStatus BestCamera3D::QueryDepthCameraRawFrames(std::vector<BestCameraRawFrameItem>& out,
                                                   std::uint64_t client_capture_id) {
  if (!impl_ || !impl_->sdk) return BestStatus::kFail;
  out.clear();
  const bool ok = impl_->sdk->ListDepthCameraRawFrames(&out, client_capture_id);
  return ok ? BestStatus::kSuccess : StatusFromIo(false, impl_->sdk.get());
}

// 实现 BestCamera3D::GetLastCaptureInlineImage。
BestStatus BestCamera3D::GetLastCaptureInlineImage(BestInlineImage& out) {
  if (!impl_ || !impl_->sdk) return BestStatus::kFail;
  std::string name;
  std::vector<std::uint8_t> payload;
  if (!impl_->sdk->GetLastCaptureInlineImage(&name, &payload)) return BestStatus::kNotSupported;
  out.name = std::move(name);
  out.payload = std::move(payload);
  return BestStatus::kSuccess;
}

// 实现 BestCamera3D::QueryPointCloud。
BestStatus BestCamera3D::QueryPointCloud(BestShmFrameRef& out) {
  if (!impl_ || !impl_->sdk) return BestStatus::kFail;
  std::string region;
  std::uint64_t seq = 0, off = 0, sz = 0;
  std::uint32_t w = 0, h = 0, fmt = 0;
  std::int64_t ts = 0;
  const bool ok = impl_->sdk->GetPointCloud(&region, &seq, &off, &sz, &w, &h, &fmt, &ts);
  FillShmFromSdk(ok, impl_->sdk.get(), out, region, seq, off, sz, w, h, fmt, ts);
  return ok ? BestStatus::kSuccess : StatusFromIo(false, impl_->sdk.get());
}

// 实现 BestCamera3D::QueryDetectionResult。
BestStatus BestCamera3D::QueryDetectionResult(BestShmFrameRef& out) {
  if (!impl_ || !impl_->sdk) return BestStatus::kFail;
  std::string region;
  std::uint64_t seq = 0, off = 0, sz = 0;
  std::uint32_t w = 0, h = 0, fmt = 0;
  std::int64_t ts = 0;
  const bool ok = impl_->sdk->GetDetectionResult(&region, &seq, &off, &sz, &w, &h, &fmt, &ts);
  FillShmFromSdk(ok, impl_->sdk.get(), out, region, seq, off, sz, w, h, fmt, ts);
  return ok ? BestStatus::kSuccess : StatusFromIo(false, impl_->sdk.get());
}

// 实现 BestCamera3D::SetROI / GetROI：Hub 未对接，固定 kNotSupported。
BestStatus BestCamera3D::SetROI(const BestROI&) { return BestStatus::kNotSupported; }

BestStatus BestCamera3D::GetROI(BestROI&) { return BestStatus::kNotSupported; }

// 实现 BestCamera3D::SetConfig：曝光/增益毫分贝映射到 SDK。
BestStatus BestCamera3D::SetConfig(BestConfigType type, const std::vector<int>& values) {
  if (!impl_ || !impl_->sdk) return BestStatus::kFail;
  switch (type) {
    case BestConfigType::kExposureTimeUs: {
      if (values.size() < 1) return BestStatus::kInvalidParameter;
      const double us = static_cast<double>(values[0]);
      const bool ok = impl_->sdk->SetParameters({ParameterValue{ParameterType::kExposure2d, us}});
      return ok ? BestStatus::kSuccess : StatusFromIo(false, impl_->sdk.get());
    }
    case BestConfigType::kGainDbMilli: {
      if (values.size() < 1) return BestStatus::kInvalidParameter;
      const double db = static_cast<double>(values[0]) / 1000.0;
      const bool ok = impl_->sdk->SetParameters({ParameterValue{ParameterType::kGain2d, db}});
      return ok ? BestStatus::kSuccess : StatusFromIo(false, impl_->sdk.get());
    }
    case BestConfigType::kGammaMilli: {
      if (values.size() < 1) return BestStatus::kInvalidParameter;
      const double g = static_cast<double>(values[0]) / 1000.0;
      const bool ok = impl_->sdk->SetParameters({ParameterValue{ParameterType::kGamma2d, g}});
      return ok ? BestStatus::kSuccess : StatusFromIo(false, impl_->sdk.get());
    }
    case BestConfigType::kUnknown:
    default:
      return BestStatus::kNotSupported;
  }
}

// 实现 BestCamera3D::GetConfig。
BestStatus BestCamera3D::GetConfig(BestConfigType type, std::vector<int>& out_values) {
  if (!impl_ || !impl_->sdk) return BestStatus::kFail;
  out_values.clear();
  switch (type) {
    case BestConfigType::kExposureTimeUs: {
      std::vector<ParameterValue> pv;
      if (!impl_->sdk->GetParameters({ParameterType::kExposure2d}, &pv) || pv.empty())
        return StatusFromIo(false, impl_->sdk.get());
      const double us = pv[0].value;
      const double clamped = std::min(us, static_cast<double>(std::numeric_limits<int>::max()));
      out_values.push_back(static_cast<int>(std::lround(clamped)));
      return BestStatus::kSuccess;
    }
    case BestConfigType::kGainDbMilli: {
      std::vector<ParameterValue> pv;
      if (!impl_->sdk->GetParameters({ParameterType::kGain2d}, &pv) || pv.empty())
        return StatusFromIo(false, impl_->sdk.get());
      out_values.push_back(static_cast<int>(std::lround(pv[0].value * 1000.0)));
      return BestStatus::kSuccess;
    }
    case BestConfigType::kGammaMilli: {
      std::vector<ParameterValue> pv;
      if (!impl_->sdk->GetParameters({ParameterType::kGamma2d}, &pv) || pv.empty())
        return StatusFromIo(false, impl_->sdk.get());
      out_values.push_back(static_cast<int>(std::lround(pv[0].value * 1000.0)));
      return BestStatus::kSuccess;
    }
    case BestConfigType::kUnknown:
    default:
      return BestStatus::kNotSupported;
  }
}

// 实现 BestCamera3D::RegisterEventCallback：当前占位返回 kNotSupported。
BestStatus BestCamera3D::RegisterEventCallback(BestEventCallback cb, BestUserContext user) {
  if (!impl_) return BestStatus::kFail;
  (void)cb;
  (void)user;
  return BestStatus::kNotSupported;
}

// 实现 BestCamera3D::UnregisterEventCallback：清空 Impl 内回调指针。
void BestCamera3D::UnregisterEventCallback() {
  if (!impl_) return;
  impl_->event_cb = nullptr;
  impl_->event_user = nullptr;
}

// 实现 BestCamera3D::LastErrorCode。
int BestCamera3D::LastErrorCode() const {
  return (impl_ && impl_->sdk) ? impl_->sdk->GetLastErrorCode() : -1;
}

// 实现 BestCamera3D::LastHubStatusCode。
int BestCamera3D::LastHubStatusCode() const {
  return (impl_ && impl_->sdk) ? impl_->sdk->GetLastHubStatusCode() : 0;
}

// 实现 BestCamera3D::LastErrorMessage。
std::string BestCamera3D::LastErrorMessage() const {
  return (impl_ && impl_->sdk) ? impl_->sdk->GetLastErrorMessage() : std::string{};
}

// 实现 BestCamera3D::RpcPeer。
std::string BestCamera3D::RpcPeer() const {
  return (impl_ && impl_->sdk) ? impl_->sdk->LastRpcPeer() : std::string{};
}

// 实现 BestCamera3D::SessionId。
std::string BestCamera3D::SessionId() const {
  return (impl_ && impl_->sdk) ? impl_->sdk->SessionId() : std::string{};
}

}  // namespace camera3d::best
