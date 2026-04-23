// IUserCameraSdk / IDeveloperCameraSdk 的 gRPC 实现：Channel+Stub、会话、错误码；工厂见文件末尾。

#include "camera_sdk/best_types.h"
#include "camera_sdk/developer_camera_sdk.h"
#include "camera_sdk/hub_client_action.h"
#include "camera_sdk/user_camera_sdk.h"

#include "camera_sdk_discovery.h"

#include <camera3d/hub/hub_service_state_codes.h>

#include "platform_diag/build_info.h"
#include "platform_diag/logging.h"

#include <grpcpp/grpcpp.h>

#include "camera_hub.grpc.pb.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <memory>
#include <mutex>
#include <vector>
#include <spdlog/spdlog.h>

namespace camera3d::sdk {
namespace {

namespace v1 = camera3d::hub::v1;

// IDeveloperCameraSdk 的 gRPC 实现：CameraHub::Stub + 会话与最后一次错误缓存（mu_ 保护）。
class DeveloperCameraSdkGrpc final : public IDeveloperCameraSdk {
 public:
  // Connect：Insecure Channel，Connect RPC；业务失败或 gRPC 失败时释放 stub/channel。
  bool Connect(const std::string& hub_address, const std::string& device_ip, const std::string& session_hint,
               int timeout_ms, std::uint32_t projector_com_index) override {
    std::scoped_lock lock(mu_);
    DisconnectUnlocked();
    peer_ = hub_address;
    channel_ = grpc::CreateChannel(hub_address, grpc::InsecureChannelCredentials());
    stub_ = v1::CameraHub::NewStub(channel_);
    grpc::ClientContext ctx;
    if (timeout_ms > 0) {
      const auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(timeout_ms);
      ctx.set_deadline(deadline);
    }
    v1::ConnectRequest req;
    req.set_device_ip(device_ip);
    req.set_session_hint(session_hint);
    req.set_projector_com_index(projector_com_index);
    v1::ConnectReply rep;
    const grpc::Status st = stub_->Connect(&ctx, req, &rep);
    if (!st.ok()) {
      SetGrpcFailure(st);
      stub_.reset();
      channel_.reset();
      return false;
    }
    if (rep.status().code() != 0) {
      SetBusinessFailure(rep.status().code(), rep.status().message());
      stub_.reset();
      channel_.reset();
      return false;
    }
    session_ = rep.session_id();
    ClearError();
    return true;
  }

  // Disconnect：持锁调用 DisconnectUnlocked（向 Hub 发 DisconnectRequest）。
  void Disconnect() override {
    std::scoped_lock lock(mu_);
    DisconnectUnlocked();
  }

  // IsConnected：stub 非空且 session_id 非空。
  bool IsConnected() const override {
    std::scoped_lock lock(mu_);
    return stub_ && !session_.empty();
  }

  // GetLastErrorCode：优先 Hub 业务码，否则 gRPC error_code。
  int GetLastErrorCode() const override {
    std::scoped_lock lock(mu_);
    return last_business_ != 0 ? last_business_ : last_grpc_;
  }

  // GetLastHubStatusCode：仅 Hub reply.status.code（gRPC 失败时为 0）。
  int GetLastHubStatusCode() const override {
    std::scoped_lock lock(mu_);
    return last_business_;
  }

  // GetLastErrorMessage：最近一次 gRPC 或业务 message 文本。
  std::string GetLastErrorMessage() const override {
    std::scoped_lock lock(mu_);
    return last_msg_;
  }

  // CaptureSync：DoCapture(async=false)。
  bool CaptureSync(std::uint64_t* out_job_id, std::uint64_t client_capture_id, bool with_detection_pipeline,
                   bool with_reconstruction_pipeline, std::uint32_t projector_op,
                   bool test_recon_shm_loopback, bool test_inline_image_reply) override {
    return DoCapture(false, out_job_id, client_capture_id, with_detection_pipeline, with_reconstruction_pipeline,
                     projector_op, test_recon_shm_loopback, test_inline_image_reply);
  }

  // CaptureAsync：DoCapture(async=true)。
  bool CaptureAsync(std::uint64_t* out_job_id, std::uint64_t client_capture_id, bool with_detection_pipeline,
                    bool with_reconstruction_pipeline, std::uint32_t projector_op,
                    bool test_recon_shm_loopback, bool test_inline_image_reply) override {
    return DoCapture(true, out_job_id, client_capture_id, with_detection_pipeline, with_reconstruction_pipeline,
                     projector_op, test_recon_shm_loopback, test_inline_image_reply);
  }

  // GetLastCaptureInlineImage：读 Capture 缓存的内联图。
  bool GetLastCaptureInlineImage(std::string* out_name,
                                 std::vector<std::uint8_t>* out_payload) const override {
    std::scoped_lock lock(mu_);
    if (last_inline_image_payload_.empty()) return false;
    if (out_name) *out_name = last_inline_image_name_;
    if (out_payload) *out_payload = last_inline_image_payload_;
    return true;
  }

  // SetParameters：CameraHub::SetParameters RPC。
  bool SetParameters(const std::vector<camera3d::best::ParameterValue>& params) override {
    std::scoped_lock lock(mu_);
    if (!stub_ || session_.empty()) return false;
    grpc::ClientContext ctx;
    v1::SetParametersRequest req;
    req.set_session_id(session_);
    for (const camera3d::best::ParameterValue& pv : params) {
      if (pv.type == camera3d::best::ParameterType::kInvalid) {
        SetBusinessFailure(4, "invalid ParameterType");
        return false;
      }
      v1::HubParameterPair* p = req.add_parameters();
      p->set_type(static_cast<v1::HubParameterType>(static_cast<int>(pv.type)));
      p->set_value(pv.value);
    }
    v1::SetParametersReply rep;
    const grpc::Status st = stub_->SetParameters(&ctx, req, &rep);
    if (!st.ok()) {
      SetGrpcFailure(st);
      return false;
    }
    if (rep.status().code() != 0) {
      SetBusinessFailure(rep.status().code(), rep.status().message());
      return false;
    }
    ClearError();
    return true;
  }

  // GetParameters：CameraHub::GetParameters RPC。
  bool GetParameters(const std::vector<camera3d::best::ParameterType>& types,
                     std::vector<camera3d::best::ParameterValue>* out_values) override {
    std::scoped_lock lock(mu_);
    if (!stub_ || session_.empty() || !out_values) return false;
    out_values->clear();
    grpc::ClientContext ctx;
    v1::GetParametersRequest req;
    req.set_session_id(session_);
    for (camera3d::best::ParameterType t : types) {
      if (t == camera3d::best::ParameterType::kInvalid) {
        SetBusinessFailure(4, "invalid ParameterType");
        return false;
      }
      req.add_parameter_types(static_cast<std::int32_t>(static_cast<int>(t)));
    }
    v1::GetParametersReply rep;
    const grpc::Status st = stub_->GetParameters(&ctx, req, &rep);
    if (!st.ok()) {
      SetGrpcFailure(st);
      return false;
    }
    if (rep.status().code() != 0) {
      SetBusinessFailure(rep.status().code(), rep.status().message());
      return false;
    }
    for (const v1::HubParameterPair& p : rep.parameters()) {
      camera3d::best::ParameterValue pv;
      pv.type = static_cast<camera3d::best::ParameterType>(static_cast<int>(p.type()));
      pv.value = p.value();
      out_values->push_back(pv);
    }
    ClearError();
    return true;
  }

  // GetDepthFrame：GetDepth RPC；query 为 0 时用 last_client_capture_id_。
  bool GetDepthFrame(std::string* out_shm_region, std::uint64_t* out_seq, std::uint64_t* out_offset,
                     std::uint64_t* out_size, std::uint32_t* out_width, std::uint32_t* out_height,
                     std::uint32_t* out_pixel_format, std::int64_t* out_timestamp_unix_ns,
                     std::uint64_t query_client_capture_id) override {
    std::scoped_lock lock(mu_);
    if (!stub_ || session_.empty()) return false;
    grpc::ClientContext ctx;
    v1::DepthRequest req;
    req.set_session_id(session_);
    const std::uint64_t q = query_client_capture_id != 0 ? query_client_capture_id : last_client_capture_id_;
    req.set_job_id(q);
    req.set_client_capture_id(q);
    v1::DepthReply rep;
    const grpc::Status st = stub_->GetDepth(&ctx, req, &rep);
    if (!st.ok()) {
      CAMERA3D_LOGW("[SDK GetDepth] gRPC 失败 peer={} query_capture_id={}", peer_, q);
      SetGrpcFailure(st);
      return false;
    }
    if (rep.status().code() != 0) {
      CAMERA3D_LOGW("[SDK GetDepth] Hub 拒绝 peer={} query_capture_id={}", peer_, q);
      SetBusinessFailure(rep.status().code(), rep.status().message());
      return false;
    }
    const auto& fr = rep.frame();
    if (out_shm_region) *out_shm_region = fr.region_name();
    if (out_seq) *out_seq = fr.seq();
    if (out_offset) *out_offset = fr.offset_bytes();
    if (out_size) *out_size = fr.size_bytes();
    if (out_width) *out_width = fr.width();
    if (out_height) *out_height = fr.height();
    if (out_pixel_format) *out_pixel_format = fr.pixel_format();
    if (out_timestamp_unix_ns) *out_timestamp_unix_ns = fr.timestamp_unix_ns();
    ClearError();
    return true;
  }

  bool ListDepthCameraRawFrames(std::vector<camera3d::best::BestCameraRawFrameItem>* out,
                                std::uint64_t query_client_capture_id) override {
    if (!out) {
      return false;
    }
    out->clear();
    std::scoped_lock lock(mu_);
    if (!stub_ || session_.empty()) {
      return false;
    }
    grpc::ClientContext ctx;
    v1::DepthRequest req;
    req.set_session_id(session_);
    const std::uint64_t q = query_client_capture_id != 0 ? query_client_capture_id : last_client_capture_id_;
    req.set_job_id(q);
    req.set_client_capture_id(q);
    v1::DepthReply rep;
    const grpc::Status st = stub_->GetDepth(&ctx, req, &rep);
    if (!st.ok()) {
      CAMERA3D_LOGW("[SDK GetDepth] gRPC 失败 peer={} query_capture_id={}", peer_, q);
      SetGrpcFailure(st);
      return false;
    }
    if (rep.status().code() != 0) {
      CAMERA3D_LOGW("[SDK GetDepth] Hub 拒绝 peer={} query_capture_id={}", peer_, q);
      SetBusinessFailure(rep.status().code(), rep.status().message());
      return false;
    }
    out->reserve(static_cast<std::size_t>(rep.camera_raw_frames_size()));
    for (int i = 0; i < rep.camera_raw_frames_size(); ++i) {
      const auto& cr = rep.camera_raw_frames(i);
      const auto& fr = cr.frame();
      camera3d::best::BestCameraRawFrameItem it;
      it.region_name = fr.region_name();
      it.seq = fr.seq();
      it.offset_bytes = fr.offset_bytes();
      it.size_bytes = fr.size_bytes();
      it.width = fr.width();
      it.height = fr.height();
      it.pixel_format = fr.pixel_format();
      it.timestamp_unix_ns = fr.timestamp_unix_ns();
      it.camera_index = cr.camera_index();
      it.serial_number = cr.serial_number();
      it.ip = cr.ip();
      it.manager_device_id = cr.manager_device_id();
      it.burst_frame_index = cr.burst_frame_index();
      it.channels = cr.channels();
      it.row_step_bytes = cr.row_step_bytes();
      out->push_back(std::move(it));
    }
    ClearError();
    return true;
  }

  // GetPointCloud：GetPointCloud RPC，填充 ShmFrameRef 各字段。
  bool GetPointCloud(std::string* out_shm_region, std::uint64_t* out_seq, std::uint64_t* out_offset,
                     std::uint64_t* out_size, std::uint32_t* out_width, std::uint32_t* out_height,
                     std::uint32_t* out_pixel_format, std::int64_t* out_timestamp_unix_ns) override {
    std::scoped_lock lock(mu_);
    if (!stub_ || session_.empty()) return false;
    grpc::ClientContext ctx;
    v1::PointCloudRequest req;
    req.set_session_id(session_);
    v1::PointCloudReply rep;
    const grpc::Status st = stub_->GetPointCloud(&ctx, req, &rep);
    if (!st.ok()) {
      SetGrpcFailure(st);
      return false;
    }
    if (rep.status().code() != 0) {
      SetBusinessFailure(rep.status().code(), rep.status().message());
      return false;
    }
    const auto& fr = rep.frame();
    if (out_shm_region) *out_shm_region = fr.region_name();
    if (out_seq) *out_seq = fr.seq();
    if (out_offset) *out_offset = fr.offset_bytes();
    if (out_size) *out_size = fr.size_bytes();
    if (out_width) *out_width = fr.width();
    if (out_height) *out_height = fr.height();
    if (out_pixel_format) *out_pixel_format = fr.pixel_format();
    if (out_timestamp_unix_ns) *out_timestamp_unix_ns = fr.timestamp_unix_ns();
    ClearError();
    return true;
  }

  // GetDetectionResult：GetDetectionResult RPC。
  bool GetDetectionResult(std::string* out_shm_region, std::uint64_t* out_seq, std::uint64_t* out_offset,
                          std::uint64_t* out_size, std::uint32_t* out_width, std::uint32_t* out_height,
                          std::uint32_t* out_pixel_format, std::int64_t* out_timestamp_unix_ns) override {
    std::scoped_lock lock(mu_);
    if (!stub_ || session_.empty()) return false;
    grpc::ClientContext ctx;
    v1::DetectionResultRequest req;
    req.set_session_id(session_);
    v1::DetectionResultReply rep;
    const grpc::Status st = stub_->GetDetectionResult(&ctx, req, &rep);
    if (!st.ok()) {
      SetGrpcFailure(st);
      return false;
    }
    if (rep.status().code() != 0) {
      SetBusinessFailure(rep.status().code(), rep.status().message());
      return false;
    }
    const auto& fr = rep.frame();
    if (out_shm_region) *out_shm_region = fr.region_name();
    if (out_seq) *out_seq = fr.seq();
    if (out_offset) *out_offset = fr.offset_bytes();
    if (out_size) *out_size = fr.size_bytes();
    if (out_width) *out_width = fr.width();
    if (out_height) *out_height = fr.height();
    if (out_pixel_format) *out_pixel_format = fr.pixel_format();
    if (out_timestamp_unix_ns) *out_timestamp_unix_ns = fr.timestamp_unix_ns();
    ClearError();
    return true;
  }

  // LastRpcPeer：返回 Connect 使用的 hub_address。
  std::string LastRpcPeer() const override {
    std::scoped_lock lock(mu_);
    return peer_;
  }

  // SessionId：Hub 返回的会话串。
  std::string SessionId() const override {
    std::scoped_lock lock(mu_);
    return session_;
  }

  // GetSdkVersion：与 build_info 一致。
  std::string GetSdkVersion() const override { return camera3d::diag::kCamera3dStackVersion; }

  bool DiscoverDevices(std::vector<DiscoveredHubDevice>* out, std::uint16_t listen_udp_port,
                       int timeout_ms) override {
    const bool ok = DiscoverHubDevicesUdp(out, listen_udp_port, timeout_ms);
    if (ok) {
      std::scoped_lock lock(mu_);
      ClearError();
    }
    return ok;
  }

  // SetDiagnosticLogLevel：映射字符串到 spdlog::default_logger 级别。
  void SetDiagnosticLogLevel(const std::string& level) override {
    std::string l = level;
    for (auto& c : l) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    auto lg = spdlog::default_logger();
    if (!lg) return;
    if (l == "trace")
      lg->set_level(spdlog::level::trace);
    else if (l == "debug")
      lg->set_level(spdlog::level::debug);
    else if (l == "info")
      lg->set_level(spdlog::level::info);
    else if (l == "warn" || l == "warning")
      lg->set_level(spdlog::level::warn);
    else if (l == "error")
      lg->set_level(spdlog::level::err);
    else if (l == "critical")
      lg->set_level(spdlog::level::critical);
  }

 private:
  // DisconnectUnlocked：已持锁；发 Disconnect RPC 并清空会话与缓存。
  void DisconnectUnlocked() {
    if (stub_ && !session_.empty()) {
      grpc::ClientContext ctx;
      v1::DisconnectRequest req;
      req.set_session_id(session_);
      v1::DisconnectReply rep;
      (void)stub_->Disconnect(&ctx, req, &rep);
    }
    session_.clear();
    stub_.reset();
    channel_.reset();
    peer_.clear();
    last_inline_image_name_.clear();
    last_inline_image_payload_.clear();
  }

  // SetGrpcFailure：记录 gRPC error_code 与 message，清空业务码。
  void SetGrpcFailure(const grpc::Status& st) {
    last_grpc_ = static_cast<int>(st.error_code());
    last_business_ = 0;
    last_msg_ = st.error_message();
    CAMERA3D_LOGE("gRPC 错误 code={} msg={}", last_grpc_, last_msg_);
  }

  // SetBusinessFailure：记录 Hub 业务码并打日志（含 Hub 中文说明与 SDK 建议动作）。
  void SetBusinessFailure(int code, const std::string& m) {
    last_business_ = code;
    last_grpc_ = 0;
    last_msg_ = m;
    const auto act = RecommendedActionForHubStatus(code);
    const char* zh = camera3d::hub::HubServiceStateDescribeZh(code);
    CAMERA3D_LOGE("Hub 业务状态 code={} msg={} | 说明: {} | SDK 建议: {}", code, m, zh,
                  HubClientActionDescribeZh(act));
    if (HubStatusSuggestAutoRetryConnect(code)) {
      CAMERA3D_LOGW("可考虑在短暂退避后重试 Connect（不改变配置），当前 code={}", code);
    }
  }

  // ClearError：成功 RPC 后重置 last_* 错误状态。
  void ClearError() {
    last_business_ = 0;
    last_grpc_ = 0;
    last_msg_.clear();
  }

  // DoCapture：Capture RPC 公共路径；必要时本地生成 client_capture_id。
  bool DoCapture(bool async, std::uint64_t* out_job_id, std::uint64_t client_capture_id,
                 bool with_detection_pipeline, bool with_reconstruction_pipeline, std::uint32_t projector_op,
                 bool test_recon_shm_loopback, bool test_inline_image_reply) {
    std::scoped_lock lock(mu_);
    if (!stub_ || session_.empty()) {
      CAMERA3D_LOGW("[SDK Capture] 未连接 Hub（无 stub 或 session 为空），跳过 Capture RPC");
      return false;
    }
    std::uint64_t cid = client_capture_id;
    if (cid == 0) {
      static std::atomic<std::uint64_t> gen{1};
      cid = gen.fetch_add(1, std::memory_order_relaxed);
    }
    grpc::ClientContext ctx;
    v1::CaptureRequest req;
    req.set_session_id(session_);
    req.set_async(async);
    req.set_client_capture_id(cid);
    req.set_with_detection_pipeline(with_detection_pipeline);
    req.set_with_reconstruction_pipeline(with_reconstruction_pipeline);
    req.set_projector_op(projector_op);
    req.set_test_recon_shm_loopback(test_recon_shm_loopback);
    req.set_test_inline_image_reply(test_inline_image_reply);
    v1::CaptureReply rep;
    const grpc::Status st = stub_->Capture(&ctx, req, &rep);
    if (!st.ok()) {
      CAMERA3D_LOGW("[SDK Capture] gRPC 失败 peer={} req_capture_id={}", peer_, cid);
      SetGrpcFailure(st);
      return false;
    }
    if (rep.status().code() != 0) {
      CAMERA3D_LOGE(
          "[SDK Capture] Hub 返回业务错误 peer={} req_capture_id={} async={} with_det={} with_rec={} "
          "projector_op={}",
          peer_, cid, async, with_detection_pipeline, with_reconstruction_pipeline, projector_op);
      SetBusinessFailure(rep.status().code(), rep.status().message());
      return false;
    }
    last_client_capture_id_ = rep.client_capture_id() != 0 ? rep.client_capture_id() : rep.job_id();
    last_inline_image_name_ = rep.inline_image_name();
    last_inline_image_payload_.assign(rep.inline_image_payload().begin(), rep.inline_image_payload().end());
    if (out_job_id) *out_job_id = last_client_capture_id_;
    CAMERA3D_LOGI(
        "[SDK Capture] RPC 成功 peer={} capture_id={} async={} with_det={} with_rec={} projector_op={} "
        "test_inline={} test_recon_loop={}",
        peer_, last_client_capture_id_, async, with_detection_pipeline, with_reconstruction_pipeline,
        projector_op, test_inline_image_reply, test_recon_shm_loopback);
    ClearError();
    return true;
  }

  mutable std::mutex mu_;
  std::shared_ptr<grpc::Channel> channel_;
  std::unique_ptr<v1::CameraHub::Stub> stub_;
  std::string peer_;
  std::string session_;
  std::uint64_t last_client_capture_id_ = 0;
  std::string last_inline_image_name_;
  std::vector<std::uint8_t> last_inline_image_payload_;
  int last_business_ = 0;
  int last_grpc_ = 0;
  std::string last_msg_;
};

}  // namespace

// 实现 CreateUserCameraSdk：与 CreateDeveloperCameraSdk 相同堆类型。
IUserCameraSdk* CreateUserCameraSdk() {
  return new DeveloperCameraSdkGrpc();
}

// 实现 CreateDeveloperCameraSdk。
IDeveloperCameraSdk* CreateDeveloperCameraSdk() {
  return new DeveloperCameraSdkGrpc();
}

// 实现 DestroyUserCameraSdk：delete 堆对象。
void DestroyUserCameraSdk(IUserCameraSdk* ptr) {
  delete ptr;
}

// 实现 DestroyDeveloperCameraSdk。
void DestroyDeveloperCameraSdk(IDeveloperCameraSdk* ptr) {
  delete ptr;
}

}  // namespace camera3d::sdk
