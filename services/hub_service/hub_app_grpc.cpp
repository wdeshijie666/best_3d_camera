#include "hub_app.h"

#include "hub_file_config.h"
#include "hub_device_broadcaster.h"
#include "hub_service_state.h"

#include "camera_driver/camera_manager.h"
#include "camera_driver/device_info_io.h"
#include "capture_orchestrator/hard_trigger_orchestrator.h"
#include "capture_orchestrator/multi_hard_trigger_orchestrator.h"
#include "ipc_shmem/shm_constants.h"
#include "ipc_shmem/shm_ring_buffer.h"
#include "platform_diag/logging.h"
#include "serial_port/serial_port_manager.h"
#include "serial_port/serial_projector.h"

#include <grpcpp/grpcpp.h>

#include "camera_hub.grpc.pb.h"

#ifdef CAMERA3D_ENABLE_RECON_SHM_LOOPBACK_TEST
#include "hub_shm_loopback_test.h"
#endif
#ifdef CAMERA3D_ENABLE_HUB_CAPTURE_BURST_TIFF_SAVE
#include "hub_burst_tiff_save.h"
#endif

namespace {
inline grpc::Status GrpcOk() { return grpc::Status(grpc::StatusCode::OK, ""); }

}  // namespace

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// CameraHub gRPC 实现：统一启动（相机/串口/SHM/编排）、会话与 Capture 写 SHM、GetDepth 发布元数据。
// with_detection_pipeline 请求忽略；与 recon 的进程级联调见 CAMERA3D_ENABLE_RECON_SHM_LOOPBACK_TEST。

namespace camera3d::hub {

namespace {

std::string NormalizeGrpcListenAddress(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  if (s.empty()) return "0.0.0.0:50051";
  if (s.find(':') == std::string::npos) return s + ":50051";
  return s;
}

}  // namespace

namespace {

namespace v1 = camera3d::hub::v1;

struct CameraRawShmSlot {
  std::string manager_device_id;
  std::string ip;
  std::string serial_number;
  std::uint32_t camera_index = 0;
  /// 单次硬触发内第几帧（0..frames_per_hardware_trigger-1）
  std::uint32_t burst_frame_index = 0;
  std::uint32_t raw_slot = 0;
  std::uint64_t raw_seq = 0;
  std::uint32_t w = 0;
  std::uint32_t h = 0;
  std::uint32_t fmt = 0;
  std::uint64_t payload_off = 0;
  std::uint64_t payload_size = 0;
};

struct CapturePublication {
  uint64_t client_capture_id = 0;
  /// 多路×多帧展开顺序：camera0 的 burst0..K-1，再 camera1 的 burst0..K-1，…
  std::vector<CameraRawShmSlot> camera_raw_slots;
  std::uint32_t raw_slot = 0;
  std::uint64_t raw_seq = 0;
  std::uint32_t w = 0;
  std::uint32_t h = 0;
  std::uint32_t fmt = 0;
  std::uint64_t payload_off = 0;
  std::uint64_t payload_size = 0;
  bool has_inter = false;
  std::uint32_t inter_slot = 0;
  std::uint64_t inter_seq = 0;
  bool has_final = false;
  std::uint32_t fin_slot = 0;
  std::uint64_t fin_seq = 0;
};

#ifdef CAMERA3D_ENABLE_RECON_SHM_LOOPBACK_TEST
std::vector<ReconLoopbackHubRawSlot> BuildReconLoopbackSlotsFromPublication(const CapturePublication& p) {
  std::vector<ReconLoopbackHubRawSlot> v;
  v.reserve(p.camera_raw_slots.size());
  for (const auto& s : p.camera_raw_slots) {
    ReconLoopbackHubRawSlot x;
    x.hub_slot_index = s.raw_slot;
    x.hub_slot_seq = s.raw_seq;
    x.camera_index = s.camera_index;
    x.burst_frame_index = s.burst_frame_index;
    x.width = s.w;
    x.height = s.h;
    x.pixel_format = s.fmt;
    v.push_back(x);
  }
  if (v.empty()) {
    ReconLoopbackHubRawSlot x;
    x.hub_slot_index = p.raw_slot;
    x.hub_slot_seq = p.raw_seq;
    x.width = p.w;
    x.height = p.h;
    x.pixel_format = p.fmt;
    v.push_back(x);
  }
  return v;
}
#endif

// v1::CameraHub::Service 实现：会话、采集、SHM 发布与 Hub 运行时状态机（见 README.md）。
class CameraHubServiceImpl final : public v1::CameraHub::Service {
 public:
  explicit CameraHubServiceImpl(camera3d::camera::CameraManager* mgr) : cameras_(mgr) {}

  bool UnifiedStartupFromConfig(const HubFileConfig& fc, std::string& err_out);
  void ApplySerialWatchTick();
  std::uint32_t ManagedSerialCom() const { return startup_serial_com_main_; }
  /// 启动阶段未进入 UnifiedStartup 时的失败（如缺文件、JSON 解析失败）：写日志 + 更新 hub_runtime_，进程继续。
  void ReportStartupFailure(std::int32_t code, std::string message);

  // CameraHub::Connect：按 hub_runtime_ 决定是否恢复编排并返回 session_id。
  grpc::Status Connect(grpc::ServerContext*, const v1::ConnectRequest* request,
                       v1::ConnectReply* reply) override {
    (void)request;
    TryRecoverSerialQuick();

    std::unique_lock<std::mutex> lk(mu_);
    std::int32_t sc = 0;
    std::string sm;
    hub_runtime_.Snapshot(&sc, &sm);
    reply->mutable_status()->set_code(sc);
    reply->mutable_status()->set_message(sm);
    if (sc == HubServiceStateCode::kRuntimeSerialNotConnected) {
      CAMERA3D_LOGW("[Connect] 串口未连接，拒绝建立 SDK 会话 code={} {}", sc, sm);
      return GrpcOk();
    }
    if (sc == HubServiceStateCode::kSessionNotEstablished) {
      std::string terr;
      if (!RestoreActiveSessionLocked(terr)) {
        reply->mutable_status()->set_code(HubServiceStateCode::kOrchestratorInitFailed);
        reply->mutable_status()->set_message(terr);
        CAMERA3D_LOGE("[Connect] 恢复采集编排失败: {}", terr);
        return GrpcOk();
      }
      hub_runtime_.Set(HubServiceStateCode::kReady, "");
      reply->mutable_status()->set_code(0);
      reply->mutable_status()->clear_message();
      reply->set_session_id(session_id_);
      AfterSdkConnectSerialLocked();
      CAMERA3D_LOGI("[Connect] SDK 会话已恢复 session_id={}", session_id_);
      return GrpcOk();
    }
    if (sc != HubServiceStateCode::kReady) {
      CAMERA3D_LOGW("[Connect] Hub 未就绪 code={} {}", sc, sm);
      return GrpcOk();
    }
    if (session_id_.empty()) {
      std::string terr;
      if (!RestoreActiveSessionLocked(terr)) {
        reply->mutable_status()->set_code(HubServiceStateCode::kOrchestratorInitFailed);
        reply->mutable_status()->set_message(terr);
        CAMERA3D_LOGE("[Connect] 恢复采集编排失败: {}", terr);
        return GrpcOk();
      }
      hub_runtime_.Set(HubServiceStateCode::kReady, "");
    }
    reply->mutable_status()->set_code(0);
    reply->mutable_status()->clear_message();
    reply->set_session_id(session_id_);
    AfterSdkConnectSerialLocked();
    CAMERA3D_LOGI("[Connect] SDK 会话确认 session_id={}", session_id_);
    return GrpcOk();
  }

  // CameraHub::Disconnect：匹配 session 则 TeardownSessionLocked。
  grpc::Status Disconnect(grpc::ServerContext*, const v1::DisconnectRequest* request,
                          v1::DisconnectReply* reply) override {
    std::unique_lock<std::mutex> lk(mu_);
    if (!session_id_.empty() && request->session_id() == session_id_) {
      TeardownSessionLocked();
    }
    reply->mutable_status()->set_code(0);
    reply->mutable_status()->clear_message();
    return GrpcOk();
  }

  // CameraHub::Capture：仅发送串口硬触发指令；帧由已注册回调写 SHM 并发布 job_id。
  grpc::Status Capture(grpc::ServerContext*, const v1::CaptureRequest* request,
                       v1::CaptureReply* reply) override {
    TryRecoverSerialQuick();
    std::unique_lock<std::mutex> lk(mu_);
    if (unified_config_mode_) {
      std::int32_t sc = 0;
      std::string sm;
      hub_runtime_.Snapshot(&sc, &sm);
      if (sc != HubServiceStateCode::kReady) {
        reply->mutable_status()->set_code(sc);
        reply->mutable_status()->set_message(sm);
        CAMERA3D_LOGW("[Capture] 拒绝采集 Hub 状态 code={} {}", sc, sm);
        return GrpcOk();
      }
    }
    if (session_id_.empty() || request->session_id() != session_id_) {
      reply->mutable_status()->set_code(3);
      reply->mutable_status()->set_message("invalid session");
      CAMERA3D_LOGW("[Capture] 非法会话: 请求 session_id 长度={} 与当前会话不匹配", request->session_id().size());
      return GrpcOk();
    }
    uint64_t cid = request->client_capture_id();
    if (cid == 0) {
      static std::atomic<uint64_t> k_auto_capture{1};
      cid = k_auto_capture.fetch_add(1, std::memory_order_relaxed);
    }
    if (awaiting_frame_) {
      reply->mutable_status()->set_code(10);
      reply->mutable_status()->set_message("capture in progress, wait for camera callback");
      CAMERA3D_LOGW("[Capture] 上一帧尚未完成，拒绝并发: 当前 awaiting_client_capture_id={}",
                    awaiting_client_capture_id_);
      return GrpcOk();
    }

    const bool with_rec = request->with_reconstruction_pipeline();
    const bool with_det = request->with_detection_pipeline();
    const bool async = request->async();
    const bool test_recon_loop = request->test_recon_shm_loopback();
    const bool test_inline_image = request->test_inline_image_reply();
    CAMERA3D_LOGI(
        "[Capture] 收到请求 capture_id={} req.client_capture_id={} async={} with_detection={} "
        "with_reconstruction={} projector_op={} test_inline={} test_recon_loop={} cameras={} "
        "frames_per_hardware_trigger={}",
        cid, request->client_capture_id(), async, with_det, with_rec, request->projector_op(), test_inline_image,
        test_recon_loop, session_devices_ordered_.size(), frames_per_hardware_trigger_);

    // 纯联调路径：不走串口/相机，直接用本地测试图作为“原始帧”写 SHM，再触发重建回环。
    if (test_inline_image && test_recon_loop) {
#ifndef CAMERA3D_ENABLE_CAPTURE_INLINE_IMAGE_TEST
      reply->mutable_status()->set_code(15);
      reply->mutable_status()->set_message("CAMERA3D_ENABLE_CAPTURE_INLINE_IMAGE_TEST is off at build time");
      CAMERA3D_LOGW("[Capture] 内联图联调未编译 capture_id={}", cid);
      return GrpcOk();
#else
      std::vector<std::uint8_t> inline_payload;
      std::string inline_name;
      std::string terr;
      if (!AttachInlineTestImageLocked(true, reply, terr, &inline_payload, &inline_name)) {
        reply->mutable_status()->set_code(15);
        reply->mutable_status()->set_message(terr);
        CAMERA3D_LOGW("[Capture] 内联测试图附加失败 capture_id={}: {}", cid, terr);
        return GrpcOk();
      }
      if (inline_payload.empty()) {
        reply->mutable_status()->set_code(15);
        reply->mutable_status()->set_message("inline test image payload empty");
        CAMERA3D_LOGW("[Capture] 内联测试图 payload 为空 capture_id={}", cid);
        return GrpcOk();
      }
      std::string werr;
      CapturePublication pub;
      if (!WritePayloadToShmLocked(cid, inline_payload.data(), inline_payload.size(), 0, 0, 0, with_rec, pub,
                                   werr)) {
        reply->mutable_status()->set_code(5);
        reply->mutable_status()->set_message(werr);
        CAMERA3D_LOGE("[Capture] 内联路径写 SHM 失败 capture_id={}: {}", cid, werr);
        return GrpcOk();
      }
      RememberPublicationLocked(cid, std::move(pub));
#ifdef CAMERA3D_ENABLE_RECON_SHM_LOOPBACK_TEST
      const CapturePublication& pref = publications_.at(cid);
      const std::string region = shm_.RegionName();
      lk.unlock();
      TryInvokeReconShmLoopbackAfterPublishBurst(region, cid, BuildReconLoopbackSlotsFromPublication(pref));
      lk.lock();
#else
      reply->mutable_status()->set_code(16);
      reply->mutable_status()->set_message("CAMERA3D_ENABLE_RECON_SHM_LOOPBACK_TEST is off at build time");
      CAMERA3D_LOGW("[Capture] 重建 SHM 回环未编译 capture_id={}", cid);
      return GrpcOk();
#endif
      (void)async;  // 本地图片联调路径为同步语义；async 标志仅保留兼容。
      CAMERA3D_LOGI("[inline-recon-test] capture_id={} image={} bytes={} 已走 SHM+重建回环", cid, inline_name,
                    inline_payload.size());
      reply->mutable_status()->set_code(0);
      reply->set_job_id(cid);
      reply->set_client_capture_id(cid);
      return GrpcOk();
#endif
    }

    const bool serial_open = camera3d::serial::SerialPortManager::Instance().IsOpen();

    // 采集路径仅依赖串口硬触发；不在 Capture RPC 内主动调用相机 GrabOne。
    if (!serial_open) {
      reply->mutable_status()->set_code(11);
      reply->mutable_status()->set_message("capture requires projector serial (COM) open on connect");
      CAMERA3D_LOGW("[Capture] 串口未打开，无法下发投采指令 capture_id={}（Connect 时需串口就绪）", cid);
      return GrpcOk();
    }

    pending_camera_bursts_.clear();
    last_capture_shm_error_.clear();
    awaiting_frame_ = true;
    awaiting_client_capture_id_ = cid;
    awaiting_with_rec_ = with_rec;
    awaiting_test_loopback_ = test_recon_loop;

    const auto cmd = request->projector_op() == 0
            ? camera3d::serial::ProductionCommand::kWhiteScreenToEnd
                         : static_cast<camera3d::serial::ProductionCommand>(
                               static_cast<std::uint8_t>(request->projector_op()));
    std::string serr;
    if (!SendProductionCommandOnlyLocked(cmd, serr)) {
      awaiting_frame_ = false;
      awaiting_client_capture_id_ = 0;
      awaiting_test_loopback_ = false;
      pending_camera_bursts_.clear();
      reply->mutable_status()->set_code(13);
      reply->mutable_status()->set_message(serr.empty() ? "serial send failed" : serr);
      CAMERA3D_LOGE("[Capture] 投采串口指令发送失败 capture_id={} production_cmd={} err={}", cid,
                    static_cast<unsigned>(cmd), serr.empty() ? "serial send failed" : serr);
      return GrpcOk();
    }
    CAMERA3D_LOGI("[Capture] 投采串口指令已下发 capture_id={} async={} production_cmd={} (projector_op={}) "
                  "with_reconstruction={}",
                  cid, async, static_cast<unsigned>(cmd), request->projector_op(), with_rec);

    if (!async) {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
      while (awaiting_frame_ && std::chrono::steady_clock::now() < deadline) {
        frame_cv_.wait_for(lk, std::chrono::milliseconds(200));
      }
      if (awaiting_frame_) {
        std::size_t got = 0;
        for (const auto& id : session_devices_ordered_) {
          const auto it = pending_camera_bursts_.find(id);
          const std::size_t n = (it == pending_camera_bursts_.end()) ? 0 : it->second.size();
          got = (got == 0) ? n : std::min(got, n);
        }
        awaiting_frame_ = false;
        awaiting_client_capture_id_ = 0;
        awaiting_test_loopback_ = false;
        pending_camera_bursts_.clear();
        reply->mutable_status()->set_code(14);
        reply->mutable_status()->set_message("capture timeout waiting for hardware frame");
        CAMERA3D_LOGE("[Capture] 同步等待相机帧超时(约15s) capture_id={} 每路已收burst帧数={}/{}", cid, got,
                      frames_per_hardware_trigger_);
        return GrpcOk();
      }
      CAMERA3D_LOGI("[Capture] 同步模式：相机回调已结束，准备校验 SHM 发布 capture_id={}", cid);
      if (publications_.count(cid) == 0) {
        std::string detail = last_capture_shm_error_;
        last_capture_shm_error_.clear();
        if (detail.empty()) {
          detail = "frames received but no SHM publication (internal inconsistency)";
        }
        reply->mutable_status()->set_code(17);
        reply->mutable_status()->set_message(detail);
        CAMERA3D_LOGE("[Capture] 同步采集无 SHM 发布记录 capture_id={}（GetDepth 将无法取图）detail={}", cid,
                      detail);
        return GrpcOk();
      }
    } else {
      CAMERA3D_LOGI("[Capture] 异步模式：RPC 即将返回，帧到达后由回调写 SHM capture_id={}", cid);
    }

    std::string terr;
    if (!AttachInlineTestImageLocked(test_inline_image, reply, terr)) {
      reply->mutable_status()->set_code(15);
      reply->mutable_status()->set_message(terr);
      CAMERA3D_LOGW("[Capture] AttachInlineTestImage 失败 capture_id={}: {}", cid, terr);
      return GrpcOk();
    }
    reply->mutable_status()->set_code(0);
    reply->set_job_id(cid);
    reply->set_client_capture_id(cid);
    CAMERA3D_LOGI("[Capture] RPC 返回成功 capture_id={} async={} reply.job_id={}", cid, async, cid);
    return GrpcOk();
  }

  // CameraHub::SetExposure：对会话内全部相机 SetExposureUs。
  grpc::Status SetExposure(grpc::ServerContext*, const v1::ExposureRequest* request,
                           v1::ExposureReply* reply) override {
    TryRecoverSerialQuick();
    std::scoped_lock lock(mu_);
    if (FillUnifiedGateLocked("SetExposure", reply->mutable_status())) {
      return GrpcOk();
    }
    if (session_id_.empty() || request->session_id() != session_id_) {
      reply->mutable_status()->set_code(3);
      return GrpcOk();
    }
    for (const auto& id : session_devices_ordered_) {
      if (!cameras_->SetExposureUs(id, request->microseconds())) {
        reply->mutable_status()->set_code(6);
        return GrpcOk();
      }
    }
    reply->mutable_status()->set_code(0);
    reply->set_microseconds(request->microseconds());
    return GrpcOk();
  }

  // CameraHub::GetExposure：读会话首路相机曝光。
  grpc::Status GetExposure(grpc::ServerContext*, const v1::ExposureRequest* request,
                           v1::ExposureReply* reply) override {
    TryRecoverSerialQuick();
    std::scoped_lock lock(mu_);
    if (FillUnifiedGateLocked("GetExposure", reply->mutable_status())) {
      return GrpcOk();
    }
    double v = 0;
    if (session_id_.empty() || request->session_id() != session_id_ || session_devices_ordered_.empty() ||
        !cameras_->GetExposureUs(session_devices_ordered_.front(), v)) {
      reply->mutable_status()->set_code(3);
      return GrpcOk();
    }
    reply->mutable_status()->set_code(0);
    reply->set_microseconds(v);
    return GrpcOk();
  }

  // CameraHub::SetGain：对会话内全部相机 SetGainDb。
  grpc::Status SetGain(grpc::ServerContext*, const v1::GainRequest* request, v1::GainReply* reply) override {
    TryRecoverSerialQuick();
    std::scoped_lock lock(mu_);
    if (FillUnifiedGateLocked("SetGain", reply->mutable_status())) {
      return GrpcOk();
    }
    if (session_id_.empty() || request->session_id() != session_id_) {
      reply->mutable_status()->set_code(3);
      return GrpcOk();
    }
    for (const auto& id : session_devices_ordered_) {
      if (!cameras_->SetGainDb(id, request->gain_db())) {
        reply->mutable_status()->set_code(7);
        return GrpcOk();
      }
    }
    reply->mutable_status()->set_code(0);
    reply->set_gain_db(request->gain_db());
    return GrpcOk();
  }

  // CameraHub::GetGain：读会话首路相机增益。
  grpc::Status GetGain(grpc::ServerContext*, const v1::GainRequest* request, v1::GainReply* reply) override {
    TryRecoverSerialQuick();
    std::scoped_lock lock(mu_);
    if (FillUnifiedGateLocked("GetGain", reply->mutable_status())) {
      return GrpcOk();
    }
    double v = 0;
    if (session_id_.empty() || request->session_id() != session_id_ || session_devices_ordered_.empty() ||
        !cameras_->GetGainDb(session_devices_ordered_.front(), v)) {
      reply->mutable_status()->set_code(3);
      return GrpcOk();
    }
    reply->mutable_status()->set_code(0);
    reply->set_gain_db(v);
    return GrpcOk();
  }

  // CameraHub::SetParameters：按类型对会话内全部相机下发（2D 曝光/增益/伽马）。
  grpc::Status SetParameters(grpc::ServerContext*, const v1::SetParametersRequest* request,
                             v1::SetParametersReply* reply) override {
    TryRecoverSerialQuick();
    std::scoped_lock lock(mu_);
    if (FillUnifiedGateLocked("SetParameters", reply->mutable_status())) {
      return GrpcOk();
    }
    if (session_id_.empty() || request->session_id() != session_id_) {
      reply->mutable_status()->set_code(3);
      return GrpcOk();
    }
    for (const v1::HubParameterPair& p : request->parameters()) {
      const auto t = p.type();
      switch (t) {
        case v1::HUB_PARAMETER_TYPE_EXPOSURE_2D:
          for (const auto& id : session_devices_ordered_) {
            if (!cameras_->SetExposureUs(id, p.value())) {
              reply->mutable_status()->set_code(6);
              reply->mutable_status()->set_message("SetExposureUs failed");
              return GrpcOk();
            }
          }
          break;
        case v1::HUB_PARAMETER_TYPE_GAIN_2D:
          for (const auto& id : session_devices_ordered_) {
            if (!cameras_->SetGainDb(id, p.value())) {
              reply->mutable_status()->set_code(7);
              reply->mutable_status()->set_message("SetGainDb failed");
              return GrpcOk();
            }
          }
          break;
        case v1::HUB_PARAMETER_TYPE_GAMMA_2D:
          for (const auto& id : session_devices_ordered_) {
            if (!cameras_->SetGamma(id, p.value())) {
              reply->mutable_status()->set_code(20);
              reply->mutable_status()->set_message("SetGamma failed");
              return GrpcOk();
            }
          }
          break;
        default:
          reply->mutable_status()->set_code(4);
          reply->mutable_status()->set_message("unsupported HubParameterType");
          return GrpcOk();
      }
    }
    reply->mutable_status()->set_code(0);
    return GrpcOk();
  }

  // CameraHub::GetParameters：按类型读取会话首路相机当前值。
  grpc::Status GetParameters(grpc::ServerContext*, const v1::GetParametersRequest* request,
                             v1::GetParametersReply* reply) override {
    TryRecoverSerialQuick();
    std::scoped_lock lock(mu_);
    if (FillUnifiedGateLocked("GetParameters", reply->mutable_status())) {
      return GrpcOk();
    }
    if (session_id_.empty() || request->session_id() != session_id_ || session_devices_ordered_.empty()) {
      reply->mutable_status()->set_code(3);
      return GrpcOk();
    }
    const std::string& front = session_devices_ordered_.front();
    reply->clear_parameters();
    for (int ti : request->parameter_types()) {
      const auto t = static_cast<v1::HubParameterType>(ti);
      v1::HubParameterPair* out = reply->add_parameters();
      out->set_type(t);
      double v = 0;
      bool ok = false;
      switch (t) {
        case v1::HUB_PARAMETER_TYPE_EXPOSURE_2D:
          ok = cameras_->GetExposureUs(front, v);
          break;
        case v1::HUB_PARAMETER_TYPE_GAIN_2D:
          ok = cameras_->GetGainDb(front, v);
          break;
        case v1::HUB_PARAMETER_TYPE_GAMMA_2D:
          ok = cameras_->GetGamma(front, v);
          break;
        default:
          reply->mutable_status()->set_code(4);
          reply->mutable_status()->set_message("unsupported parameter type in GetParameters");
          reply->clear_parameters();
          return GrpcOk();
      }
      if (!ok) {
        reply->mutable_status()->set_code(
            t == v1::HUB_PARAMETER_TYPE_EXPOSURE_2D ? 6 : (t == v1::HUB_PARAMETER_TYPE_GAIN_2D ? 7 : 21));
        reply->mutable_status()->set_message("GetParameter failed");
        reply->clear_parameters();
        return GrpcOk();
      }
      out->set_value(v);
    }
    reply->mutable_status()->set_code(0);
    return GrpcOk();
  }

  // CameraHub::GetDepth：按 capture_id 返回 publications_ 中 SHM 元数据（含多相机 raw）。
  grpc::Status GetDepth(grpc::ServerContext*, const v1::DepthRequest* request, v1::DepthReply* reply) override {
    TryRecoverSerialQuick();
    std::scoped_lock lock(mu_);
    if (FillUnifiedGateLocked("GetDepth", reply->mutable_status())) {
      return GrpcOk();
    }
    if (session_id_.empty() || request->session_id() != session_id_) {
      reply->mutable_status()->set_code(3);
      return GrpcOk();
    }
    uint64_t key = 0;
    if (request->client_capture_id() != 0) {
      key = request->client_capture_id();
    } else if (request->job_id() != 0) {
      key = request->job_id();
    } else {
      key = latest_client_capture_id_;
    }
    if (key == 0 || publications_.count(key) == 0) {
      reply->mutable_status()->set_code(8);
      reply->mutable_status()->set_message("no capture result for id");
      return GrpcOk();
    }
    const CapturePublication& pub = publications_.at(key);
    reply->mutable_status()->set_code(0);
    reply->set_client_capture_id(pub.client_capture_id);
    FillShmRef(reply->mutable_raw_frame(), pub, /*raw=*/true, /*inter=*/false, /*fin=*/false);
    if (pub.has_inter) {
      FillShmRef(reply->mutable_intermediate_frame(), pub, false, true, false);
    }
    if (pub.has_final) {
      FillShmRef(reply->mutable_final_frame(), pub, false, false, true);
    }
    // 兼容字段：无重建占位时 frame=raw；有 with_reconstruction_pipeline 占位时 frame=final
    if (pub.has_final) {
      reply->mutable_frame()->CopyFrom(reply->final_frame());
    } else if (pub.has_inter) {
      reply->mutable_frame()->CopyFrom(reply->intermediate_frame());
    } else {
      reply->mutable_frame()->CopyFrom(reply->raw_frame());
    }
    reply->clear_camera_raw_frames();
    for (const auto& cam : pub.camera_raw_slots) {
      v1::CameraRawFrameRef* cr = reply->add_camera_raw_frames();
      FillShmRefFromRawSlot(cr->mutable_frame(), cam);
      cr->set_camera_index(cam.camera_index);
      cr->set_serial_number(cam.serial_number);
      cr->set_ip(cam.ip);
      cr->set_manager_device_id(cam.manager_device_id);
      std::uint32_t ch = 0;
      std::uint32_t step = 0;
      InferOpenCvChannelsAndStep(cam, &ch, &step);
      cr->set_channels(ch);
      cr->set_row_step_bytes(step);
      cr->set_burst_frame_index(cam.burst_frame_index);
    }
    return GrpcOk();
  }

  // CameraHub::GetPointCloud：占位未实现。
  grpc::Status GetPointCloud(grpc::ServerContext*, const v1::PointCloudRequest*,
                             v1::PointCloudReply* reply) override {
    TryRecoverSerialQuick();
    std::scoped_lock lock(mu_);
    if (FillUnifiedGateLocked("GetPointCloud", reply->mutable_status())) {
      return GrpcOk();
    }
    reply->mutable_status()->set_code(100);
    reply->mutable_status()->set_message("point cloud pipeline not implemented");
    return GrpcOk();
  }

  // CameraHub::GetDetectionResult：Hub 不提供检测 SHM，返回业务码 101。
  grpc::Status GetDetectionResult(grpc::ServerContext*, const v1::DetectionResultRequest*,
                                  v1::DetectionResultReply* reply) override {
    TryRecoverSerialQuick();
    std::scoped_lock lock(mu_);
    if (FillUnifiedGateLocked("GetDetectionResult", reply->mutable_status())) {
      return GrpcOk();
    }
    reply->mutable_status()->set_code(101);
    reply->mutable_status()->set_message(
        "Hub 不提供检测结果；检测将与重建合并到重建服务侧（当前无独立检测服务）");
    return GrpcOk();
  }

  // CameraHub::TestSaveReconEcho：联调宏开启时委托 HubServeTestSaveReconEchoGrpc。
  grpc::Status TestSaveReconEcho(grpc::ServerContext*, const v1::HubTestSaveReconEchoRequest* request,
                                 v1::HubTestSaveReconEchoReply* reply) override {
    TryRecoverSerialQuick();
#ifdef CAMERA3D_ENABLE_RECON_SHM_LOOPBACK_TEST
    std::scoped_lock lock(mu_);
    if (FillUnifiedGateLocked("TestSaveReconEcho", reply->mutable_status())) {
      return GrpcOk();
    }
    (void)HubServeTestSaveReconEchoGrpc(request, reply);
    return GrpcOk();
#else
    std::scoped_lock lock(mu_);
    if (FillUnifiedGateLocked("TestSaveReconEcho", reply->mutable_status())) {
      return GrpcOk();
    }
    reply->mutable_status()->set_code(501);
    reply->mutable_status()->set_message("CAMERA3D_ENABLE_RECON_SHM_LOOPBACK_TEST is off at build time");
    return GrpcOk();
#endif
  }

 private:
  bool AttachInlineTestImageLocked(bool enable, v1::CaptureReply* reply, std::string& err,
                                   std::vector<std::uint8_t>* out_payload = nullptr,
                                   std::string* out_image_name = nullptr) {
    err.clear();
    if (!enable) return true;
#ifndef CAMERA3D_ENABLE_CAPTURE_INLINE_IMAGE_TEST
    err = "CAMERA3D_ENABLE_CAPTURE_INLINE_IMAGE_TEST is off at build time";
    return false;
#else
    std::filesystem::path image_path;
    if (!PickNextInlineTestImageLocked(image_path)) {
      err = "no test image under D:/best_project/test_image";
      return false;
    }
    std::ifstream in(image_path, std::ios::binary);
    if (!in) {
      err = std::string("open test image failed: ") + image_path.string();
      return false;
    }
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.empty()) {
      err = std::string("empty test image file: ") + image_path.string();
      return false;
    }
    const std::string image_name = image_path.filename().string();
    reply->set_inline_image_name(image_name);
    reply->set_inline_image_payload(reinterpret_cast<const char*>(data.data()), static_cast<int>(data.size()));
    if (out_payload) *out_payload = data;
    if (out_image_name) *out_image_name = image_name;
    return true;
#endif
  }

  bool PickNextInlineTestImageLocked(std::filesystem::path& out_path) {
    out_path.clear();
#ifndef CAMERA3D_ENABLE_CAPTURE_INLINE_IMAGE_TEST
    return false;
#else
    if (inline_test_images_.empty()) {
      const std::filesystem::path root("D:/best_project/test_image");
      std::error_code ec;
      if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) return false;
      for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        inline_test_images_.push_back(entry.path());
      }
      if (inline_test_images_.empty()) return false;
      std::sort(inline_test_images_.begin(), inline_test_images_.end(),
                [](const std::filesystem::path& a, const std::filesystem::path& b) {
                  return a.filename().string() < b.filename().string();
                });
      inline_test_next_idx_ = 0;
    }
    out_path = inline_test_images_[inline_test_next_idx_ % inline_test_images_.size()];
    ++inline_test_next_idx_;
    return true;
#endif
  }

  bool PrepareHardwareTriggerLocked(std::string& err) {
    err.clear();
    if (orchestrator_) {
      orchestrator_.reset();
    }
    if (multi_orchestrator_) {
      multi_orchestrator_.reset();
    }
    if (session_devices_ordered_.empty()) {
      err = "no session cameras";
      return false;
    }
    for (const auto& mid : session_devices_ordered_) {
      cameras_->ClearResultCallback(mid);
      if (!cameras_->SetTriggerMode(mid, camera3d::camera::TriggerMode::kHardware)) {
        err = "SetTriggerMode hardware failed: " + cameras_->GetLastErrorMessage(mid);
        return false;
      }
      cameras_->SetResultCallback(
          mid, [this, mid](camera3d::camera::CameraCaptureResultBase* r) {
            if (!r || !r->success) return;
            const auto* fr = dynamic_cast<const camera3d::camera::FrameBufferCameraResult*>(r);
            if (!fr) return;
            OnCameraFrame(mid, fr->frame);
          });
      if (!cameras_->StartStreamGrab(mid)) {
        err = "StartStreamGrab failed: " + cameras_->GetLastErrorMessage(mid);
        return false;
      }
    }
    if (session_devices_ordered_.size() == 1) {
      orchestrator_ = std::make_unique<camera3d::capture::HardTriggerOrchestrator>(
          *cameras_, camera3d::serial::SerialPortManager::Instance().PortRef(), session_devices_ordered_[0]);
      return true;
    }
    multi_orchestrator_ = std::make_unique<camera3d::capture::MultiHardTriggerOrchestrator>(
        *cameras_, camera3d::serial::SerialPortManager::Instance().PortRef(), session_devices_ordered_);
    return true;
  }

  void TeardownSessionLocked() {
    // SDK 断开会话：先发黑屏再拆编排（与 SetCommand::kBlackScreen 协议一致）。
    BeforeSdkDisconnectSerialLocked();
    for (const auto& mid : session_devices_ordered_) {
      cameras_->ClearResultCallback(mid);
    }
    if (orchestrator_) {
      orchestrator_.reset();
    }
    if (multi_orchestrator_) {
      multi_orchestrator_.reset();
    }
    session_id_.clear();
    session_devices_ordered_.clear();
    session_slot_layout_.clear();
    pending_camera_bursts_.clear();
    publications_.clear();
    finish_lru_.clear();
    latest_client_capture_id_ = 0;
    awaiting_frame_ = false;
    awaiting_client_capture_id_ = 0;
    awaiting_test_loopback_ = false;
    inline_test_images_.clear();
    inline_test_next_idx_ = 0;
    hub_runtime_.Set(HubServiceStateCode::kSessionNotEstablished,
                     "session released; call Connect to resume capture pipeline");
  }

  void OnCameraFrame(const std::string& manager_device_id, const camera3d::camera::FrameBuffer& fb) {
    std::unique_lock<std::mutex> lk(mu_);
    if (!awaiting_frame_) {
      CAMERA3D_LOGW("相机回调：无进行中的采集，丢弃 frame_id={}", fb.frame_id);
      return;
    }
    const bool known = std::find(session_devices_ordered_.begin(), session_devices_ordered_.end(),
                                 manager_device_id) != session_devices_ordered_.end();
    if (!known) {
      CAMERA3D_LOGW("相机回调：未知 manager_device_id，丢弃 frame_id={}", fb.frame_id);
      return;
    }
    pending_camera_bursts_[manager_device_id].push_back(fb);
    for (const auto& id : session_devices_ordered_) {
      const auto it = pending_camera_bursts_.find(id);
      if (it == pending_camera_bursts_.end() || it->second.size() < frames_per_hardware_trigger_) {
        return;
      }
    }

    const std::uint64_t cid = awaiting_client_capture_id_;
    const bool wr = awaiting_with_rec_;
    const bool do_test_loop = awaiting_test_loopback_;
    awaiting_frame_ = false;
    awaiting_client_capture_id_ = 0;
    awaiting_test_loopback_ = false;
    std::unordered_map<std::string, std::vector<camera3d::camera::FrameBuffer>> bursts_snapshot;
    bursts_snapshot.reserve(session_devices_ordered_.size());
    for (const auto& id : session_devices_ordered_) {
      auto& src = pending_camera_bursts_[id];
      auto& dst = bursts_snapshot[id];
      const int n = static_cast<int>(frames_per_hardware_trigger_);
      dst.assign(src.begin(), src.begin() + n);
      src.erase(src.begin(), src.begin() + n);
    }
    {
      bool all_empty = true;
      for (const auto& kv : pending_camera_bursts_) {
        if (!kv.second.empty()) {
          all_empty = false;
          break;
        }
      }
      if (all_empty) {
        pending_camera_bursts_.clear();
      }
    }

#ifdef CAMERA3D_ENABLE_HUB_CAPTURE_BURST_TIFF_SAVE
    const std::vector<std::string> burst_tiff_camera_order = session_devices_ordered_;
#endif

    std::string werr;
    CapturePublication pub;
    if (!WriteMultiCaptureToShmLocked(cid, bursts_snapshot, frames_per_hardware_trigger_, wr, pub, werr)) {
      last_capture_shm_error_ = werr.empty() ? "WriteMultiCaptureToShmLocked failed" : werr;
      CAMERA3D_LOGE("[Capture] 相机帧到齐但写 SHM 失败 capture_id={} {}", cid, werr);
    } else {
      last_capture_shm_error_.clear();
      RememberPublicationLocked(cid, std::move(pub));
      CAMERA3D_LOGI(
          "[Capture] 相机帧已收齐并写入 SHM capture_id={} cameras={} frames_per_cam={} total_slots={} "
          "with_reconstruction={}",
          cid, bursts_snapshot.size(), frames_per_hardware_trigger_,
          static_cast<unsigned>(pub.camera_raw_slots.size()), wr);
#ifdef CAMERA3D_ENABLE_HUB_ALGO_BURST_PUSH_TEST
      CAMERA3D_LOGI("[algo-burst-test] 占位：可向算法服务推送 capture_id={} 共 {} 张原始 SHM 引用（宏开启）",
                    cid, pub.camera_raw_slots.size());
#endif
#ifdef CAMERA3D_ENABLE_RECON_SHM_LOOPBACK_TEST
      if (do_test_loop) {
        const CapturePublication& p = publications_.at(cid);
        const std::string region = shm_.RegionName();
        lk.unlock();
        TryInvokeReconShmLoopbackAfterPublishBurst(region, cid, BuildReconLoopbackSlotsFromPublication(p));
        lk.lock();
      }
#endif
    }
    lk.unlock();
#ifdef CAMERA3D_ENABLE_HUB_CAPTURE_BURST_TIFF_SAVE
    HubTrySaveBurstFramesTiff(bursts_snapshot, burst_tiff_camera_order, cid, frames_per_hardware_trigger_);
#endif
    frame_cv_.notify_all();
  }

  bool SendProductionCommandOnlyLocked(camera3d::serial::ProductionCommand cmd, std::string& serr) {
    serr.clear();
    if (multi_orchestrator_) {
      return multi_orchestrator_->SendProductionCommandOnly(cmd, 3000, serr);
    }
    if (orchestrator_) {
      return orchestrator_->SendProductionCommandOnly(cmd, 3000, serr);
    }
    serr = "no orchestrator";
    CAMERA3D_LOGW("[Capture] 无法发送投采指令: {}", serr);
    return false;
  }

  bool SendSetCommandOnlyLocked(camera3d::serial::SetCommand cmd, std::string& serr) {
    serr.clear();
    auto& sp = camera3d::serial::SerialPortManager::Instance();
    if (!sp.IsOpen()) {
      serr = "serial not open";
      return false;
    }
    const camera3d::serial::ProjectorResult pr = camera3d::serial::SendSetCommand(sp.PortRef(), cmd, 3000);
    if (!pr.ok) {
      serr = pr.message.empty() ? "SendSetCommand failed" : pr.message;
      return false;
    }
    return true;
  }

  void AfterSdkConnectSerialLocked() {
    std::string serr;
    if (!SendSetCommandOnlyLocked(camera3d::serial::SetCommand::kExitBlackToTest, serr)) {
      CAMERA3D_LOGW("[Connect] 串口下发「退出黑屏进入测试画面」失败（会话仍建立）: {}", serr);
    } else {
      CAMERA3D_LOGI("[Connect] 串口已下发「退出黑屏进入测试画面」(SetCommand::kExitBlackToTest)");
    }
  }

  void BeforeSdkDisconnectSerialLocked() {
    std::string serr;
    if (!SendSetCommandOnlyLocked(camera3d::serial::SetCommand::kBlackScreen, serr)) {
      CAMERA3D_LOGW("[Disconnect] 串口下发「黑屏」失败: {}", serr);
    } else {
      CAMERA3D_LOGI("[Disconnect] 串口已下发「黑屏」(SetCommand::kBlackScreen)");
    }
  }

  bool WriteMultiCaptureToShmLocked(
      std::uint64_t client_id,
      const std::unordered_map<std::string, std::vector<camera3d::camera::FrameBuffer>>& bursts_by_id,
      std::uint32_t frames_per, bool with_rec, CapturePublication& out, std::string& err) {
    err.clear();
    out = CapturePublication{};
    out.client_capture_id = client_id;
    if (session_slot_layout_.size() != session_devices_ordered_.size()) {
      err = "session slot layout mismatch";
      return false;
    }
    if (frames_per == 0) {
      err = "frames_per is zero";
      return false;
    }
    out.camera_raw_slots.clear();
    out.camera_raw_slots.reserve(session_devices_ordered_.size() * static_cast<std::size_t>(frames_per));
    for (std::size_t i = 0; i < session_devices_ordered_.size(); ++i) {
      const std::string& mid = session_devices_ordered_[i];
      auto it = bursts_by_id.find(mid);
      if (it == bursts_by_id.end()) {
        err = "missing burst vector for camera " + mid;
        return false;
      }
      const std::vector<camera3d::camera::FrameBuffer>& vec = it->second;
      if (vec.size() < frames_per) {
        err = "insufficient burst frames for camera " + mid;
        return false;
      }
      for (std::uint32_t j = 0; j < frames_per; ++j) {
        const camera3d::camera::FrameBuffer& raw = vec[j];
        std::uint32_t slot_r = 0;
        std::uint64_t seq_r = 0;
        if (!shm_.TryWriteNextSlot(raw.bytes.data(), raw.bytes.size(), raw.width, raw.height, raw.pixel_format,
                                    &seq_r, &slot_r)) {
          err = "shm raw write failed for camera " + mid + " burst " + std::to_string(j);
          return false;
        }
        CameraRawShmSlot slot = session_slot_layout_[i];
        slot.burst_frame_index = j;
        slot.raw_slot = slot_r;
        slot.raw_seq = seq_r;
        slot.w = raw.width;
        slot.h = raw.height;
        slot.fmt = raw.pixel_format;
        camera3d::ipc::ShmSlotHeader meta{};
        const std::uint8_t* p = nullptr;
        std::size_t len = 0;
        if (!shm_.TryReadSlot(slot_r, meta, p, len)) {
          err = "shm readback raw failed";
          return false;
        }
        slot.payload_off = meta.payload_offset;
        slot.payload_size = meta.payload_size;
        out.camera_raw_slots.push_back(std::move(slot));
      }
    }

    if (!out.camera_raw_slots.empty()) {
      const CameraRawShmSlot& first = out.camera_raw_slots.front();
      out.raw_slot = first.raw_slot;
      out.raw_seq = first.raw_seq;
      out.w = first.w;
      out.h = first.h;
      out.fmt = first.fmt;
      out.payload_off = first.payload_off;
      out.payload_size = first.payload_size;
    }

    const camera3d::camera::FrameBuffer& first_fb = bursts_by_id.at(session_devices_ordered_.front())[0];
    if (with_rec) {
      std::uint32_t slot_f = 0;
      std::uint64_t seq_f = 0;
      if (!shm_.TryWriteNextSlot(first_fb.bytes.data(), first_fb.bytes.size(), first_fb.width, first_fb.height,
                                 first_fb.pixel_format, &seq_f, &slot_f)) {
        err = "shm recon-stage write failed";
        return false;
      }
      out.has_final = true;
      out.fin_slot = slot_f;
      out.fin_seq = seq_f;
    }
    return true;
  }

  void RememberPublicationLocked(uint64_t cid, CapturePublication&& pub) {
    pub.client_capture_id = cid;
    publications_[cid] = std::move(pub);
    latest_client_capture_id_ = cid;
    finish_lru_.push_back(cid);
    while (finish_lru_.size() > kMaxPublications) {
      const uint64_t old = finish_lru_.front();
      finish_lru_.pop_front();
      publications_.erase(old);
    }
  }

  bool WritePayloadToShmLocked(uint64_t client_id, const void* payload, std::size_t payload_size, uint32_t width,
                               uint32_t height, uint32_t pixel_format, bool with_rec, CapturePublication& out,
                               std::string& err) {
    err.clear();
    out = CapturePublication{};
    out.client_capture_id = client_id;
    if (!payload || payload_size == 0) {
      err = "empty payload";
      return false;
    }

    uint32_t slot_r = 0;
    uint64_t seq_r = 0;
    if (!shm_.TryWriteNextSlot(payload, payload_size, width, height, pixel_format, &seq_r, &slot_r)) {
      err = "shm raw write failed";
      return false;
    }
    out.raw_slot = slot_r;
    out.raw_seq = seq_r;
    out.w = width;
    out.h = height;
    out.fmt = pixel_format;
    camera3d::ipc::ShmSlotHeader meta{};
    const std::uint8_t* p = nullptr;
    std::size_t len = 0;
    if (!shm_.TryReadSlot(slot_r, meta, p, len)) {
      err = "shm readback raw failed";
      return false;
    }
    out.payload_off = meta.payload_offset;
    out.payload_size = meta.payload_size;

    // TODO: 与重建服务交互时，可在此写入算法输入区并等待结果，再写入 final 槽位。
    if (with_rec) {
      uint32_t slot_f = 0;
      uint64_t seq_f = 0;
      if (!shm_.TryWriteNextSlot(payload, payload_size, width, height, pixel_format, &seq_f, &slot_f)) {
        err = "shm recon-stage write failed";
        return false;
      }
      out.has_final = true;
      out.fin_slot = slot_f;
      out.fin_seq = seq_f;
    }

    if (!session_slot_layout_.empty()) {
      CameraRawShmSlot slot = session_slot_layout_[0];
      slot.burst_frame_index = 0;
      slot.raw_slot = out.raw_slot;
      slot.raw_seq = out.raw_seq;
      slot.w = width;
      slot.h = height;
      slot.fmt = pixel_format;
      slot.payload_off = out.payload_off;
      slot.payload_size = out.payload_size;
      out.camera_raw_slots = {std::move(slot)};
    } else if (!session_id_.empty()) {
      CameraRawShmSlot slot;
      slot.manager_device_id = session_id_;
      slot.ip.clear();
      slot.serial_number.clear();
      slot.camera_index = 0;
      slot.burst_frame_index = 0;
      slot.raw_slot = out.raw_slot;
      slot.raw_seq = out.raw_seq;
      slot.w = width;
      slot.h = height;
      slot.fmt = pixel_format;
      slot.payload_off = out.payload_off;
      slot.payload_size = out.payload_size;
      out.camera_raw_slots = {std::move(slot)};
    }
    return true;
  }

  void FillShmRef(v1::ShmFrameRef* dst, const CapturePublication& pub, bool raw, bool inter, bool fin) const {
    uint32_t slot = pub.raw_slot;
    uint64_t seq = pub.raw_seq;
    if (inter) {
      slot = pub.inter_slot;
      seq = pub.inter_seq;
    } else if (fin) {
      slot = pub.fin_slot;
      seq = pub.fin_seq;
    }
    camera3d::ipc::ShmSlotHeader meta{};
    const std::uint8_t* p = nullptr;
    std::size_t len = 0;
    if (!shm_.TryReadSlot(slot, meta, p, len)) {
      return;
    }
    dst->set_region_name(shm_.RegionName());
    dst->set_seq(seq);
    dst->set_offset_bytes(meta.payload_offset);
    dst->set_size_bytes(meta.payload_size);
    dst->set_width(meta.width);
    dst->set_height(meta.height);
    dst->set_pixel_format(meta.pixel_format);
    dst->set_timestamp_unix_ns(meta.timestamp_unix_ns);
    (void)raw;
  }

  void FillShmRefFromRawSlot(v1::ShmFrameRef* dst, const CameraRawShmSlot& cam) const {
    camera3d::ipc::ShmSlotHeader meta{};
    const std::uint8_t* p = nullptr;
    std::size_t len = 0;
    if (!shm_.TryReadSlot(cam.raw_slot, meta, p, len)) {
      return;
    }
    dst->set_region_name(shm_.RegionName());
    dst->set_seq(cam.raw_seq);
    dst->set_offset_bytes(meta.payload_offset);
    dst->set_size_bytes(meta.payload_size);
    dst->set_width(meta.width);
    dst->set_height(meta.height);
    dst->set_pixel_format(meta.pixel_format);
    dst->set_timestamp_unix_ns(meta.timestamp_unix_ns);
  }

  static void InferOpenCvChannelsAndStep(const CameraRawShmSlot& cam, std::uint32_t* channels,
                                         std::uint32_t* row_step_bytes) {
    *channels = 0;
    *row_step_bytes = 0;
    if (cam.w == 0 || cam.h == 0) return;
    const std::uint64_t area = static_cast<std::uint64_t>(cam.w) * cam.h;
    if (area == 0 || cam.payload_size % area != 0) return;
    const auto bpp = static_cast<std::uint32_t>(cam.payload_size / area);
    if (bpp == 0 || bpp > 16) return;
    // 仅对典型 8 位多通道整型布局填 OpenCV 语义；其它 bpp（如 16bit mono）请用 pixel_format 自行解析
    if (bpp == 1u || bpp == 3u || bpp == 4u) {
      *channels = bpp;
      *row_step_bytes = cam.w * bpp;
    }
  }

  camera3d::camera::CameraManager* cameras_ = nullptr;
  std::mutex mu_;
  std::condition_variable frame_cv_;
  std::string session_id_;
  std::vector<std::string> session_devices_ordered_;
  std::vector<CameraRawShmSlot> session_slot_layout_;
  std::unordered_map<std::string, std::vector<camera3d::camera::FrameBuffer>> pending_camera_bursts_;
  /// 单次硬触发每路相机需收满的回调帧数（来自 hub_service.json capture.frames_per_hardware_trigger）
  std::uint32_t frames_per_hardware_trigger_ = 24;
  camera3d::ipc::ShmRingBuffer shm_;
  std::unique_ptr<camera3d::capture::HardTriggerOrchestrator> orchestrator_;
  std::unique_ptr<camera3d::capture::MultiHardTriggerOrchestrator> multi_orchestrator_;
  std::vector<HubPresetCamera> preset_cameras_;
  std::uint32_t startup_serial_com_main_ = 0;
  bool unified_config_mode_ = false;
  HubServiceRuntimeState hub_runtime_;

  bool FillUnifiedGateLocked(const char* rpc_name, v1::Status* st);
  void TryRecoverSerialQuick();
  bool RestoreActiveSessionLocked(std::string& err);
  void RollbackUnifiedStartupLocked();

  bool awaiting_frame_ = false;
  uint64_t awaiting_client_capture_id_ = 0;
  bool awaiting_with_rec_ = false;
  bool awaiting_test_loopback_ = false;
  /// OnCameraFrame 中 WriteMultiCaptureToShmLocked 失败时写入，供同步 Capture 在 publications_ 缺失时回传原因。
  std::string last_capture_shm_error_;

  std::unordered_map<uint64_t, CapturePublication> publications_;
  std::list<uint64_t> finish_lru_;
  uint64_t latest_client_capture_id_ = 0;
  std::vector<std::filesystem::path> inline_test_images_;
  std::size_t inline_test_next_idx_ = 0;
  static constexpr std::size_t kMaxPublications = 128;
};

bool CameraHubServiceImpl::UnifiedStartupFromConfig(const HubFileConfig& fc, std::string& err_out) {
  std::scoped_lock<std::mutex> lk(mu_);
  unified_config_mode_ = true;
  startup_serial_com_main_ = fc.projector_com_index;
  hub_runtime_.Set(HubServiceStateCode::kStarting, "unified startup");

  preset_cameras_.clear();
  for (const auto& node : fc.cameras) {
    camera3d::camera::DeviceInfo dev{};
    dev.backend_id = node.backend_id;
    dev.serial_number = node.serial_number;
    dev.ip = node.ip;
    const std::string mid = cameras_->CreateAndOpenDevice(dev);
    if (mid.empty()) {
      err_out = camera3d::camera::FormatDeviceAddress(dev);
      hub_runtime_.Set(HubServiceStateCode::kCameraInitFailed, "open camera failed: " + err_out);
      CAMERA3D_LOGE("UnifiedStartup: 打开相机失败 {}", err_out);
      RollbackUnifiedStartupLocked();
      return false;
    }
    HubPresetCamera slot;
    slot.manager_device_id = mid;
    slot.ip = node.ip;
    slot.serial_number = node.serial_number;
    preset_cameras_.push_back(std::move(slot));
  }

  if (fc.projector_com_index == 0) {
    err_out = "projector.com_index must be > 0 in hub_service.json";
    hub_runtime_.Set(HubServiceStateCode::kSerialInitFailed, err_out);
    CAMERA3D_LOGE("UnifiedStartup: {}", err_out);
    RollbackUnifiedStartupLocked();
    return false;
  }
  if (!camera3d::serial::SerialPortManager::Instance().Open(fc.projector_com_index, 115200)) {
    err_out = camera3d::serial::SerialPortManager::Instance().GetLastErrorMessage();
    hub_runtime_.Set(HubServiceStateCode::kSerialInitFailed, std::string("serial open failed: ") + err_out);
    CAMERA3D_LOGE("UnifiedStartup: 串口打开失败 {}", err_out);
    RollbackUnifiedStartupLocked();
    return false;
  }

  if (!shm_.CreateOrOpen(camera3d::ipc::kDefaultHubRingRegionName, camera3d::ipc::kDefaultHubRingTotalBytes, true)) {
    err_out = "shm create failed";
    hub_runtime_.Set(HubServiceStateCode::kShmInitFailed, err_out);
    CAMERA3D_LOGE("UnifiedStartup: {}", err_out);
    RollbackUnifiedStartupLocked();
    return false;
  }

  session_devices_ordered_.clear();
  session_slot_layout_.clear();
  for (const auto& pc : preset_cameras_) {
    session_devices_ordered_.push_back(pc.manager_device_id);
    CameraRawShmSlot meta;
    meta.manager_device_id = pc.manager_device_id;
    meta.ip = pc.ip;
    meta.serial_number = pc.serial_number;
    meta.camera_index = static_cast<std::uint32_t>(session_slot_layout_.size());
    session_slot_layout_.push_back(std::move(meta));
  }
  session_id_ = session_devices_ordered_.front();

  if (!PrepareHardwareTriggerLocked(err_out)) {
    hub_runtime_.Set(HubServiceStateCode::kOrchestratorInitFailed, err_out);
    CAMERA3D_LOGE("UnifiedStartup: 硬触发编排失败 {}", err_out);
    RollbackUnifiedStartupLocked();
    return false;
  }

  hub_runtime_.Set(HubServiceStateCode::kReady, "");
  frames_per_hardware_trigger_ = std::max(1u, std::min(4096u, fc.frames_per_hardware_trigger));
  CAMERA3D_LOGI("UnifiedStartup: 完成 cameras={} COM{} frames_per_hardware_trigger={}", preset_cameras_.size(),
                fc.projector_com_index, frames_per_hardware_trigger_);
  return true;
}

void CameraHubServiceImpl::RollbackUnifiedStartupLocked() {
  for (const auto& mid : session_devices_ordered_) {
    cameras_->ClearResultCallback(mid);
  }
  if (orchestrator_) {
    orchestrator_.reset();
  }
  if (multi_orchestrator_) {
    multi_orchestrator_.reset();
  }
  session_id_.clear();
  session_devices_ordered_.clear();
  session_slot_layout_.clear();
  pending_camera_bursts_.clear();
  shm_ = camera3d::ipc::ShmRingBuffer();
  camera3d::serial::SerialPortManager::Instance().Close();
  cameras_->CloseAllDevices();
  preset_cameras_.clear();
  startup_serial_com_main_ = 0;
  unified_config_mode_ = false;
  publications_.clear();
  finish_lru_.clear();
  latest_client_capture_id_ = 0;
  awaiting_frame_ = false;
  awaiting_client_capture_id_ = 0;
  awaiting_test_loopback_ = false;
}

void CameraHubServiceImpl::ReportStartupFailure(std::int32_t code, std::string message) {
  std::scoped_lock<std::mutex> lk(mu_);
  RollbackUnifiedStartupLocked();
  hub_runtime_.Set(code, std::move(message));
}

bool CameraHubServiceImpl::RestoreActiveSessionLocked(std::string& err) {
  err.clear();
  session_devices_ordered_.clear();
  session_slot_layout_.clear();
  for (const auto& pc : preset_cameras_) {
    session_devices_ordered_.push_back(pc.manager_device_id);
    CameraRawShmSlot meta;
    meta.manager_device_id = pc.manager_device_id;
    meta.ip = pc.ip;
    meta.serial_number = pc.serial_number;
    meta.camera_index = static_cast<std::uint32_t>(session_slot_layout_.size());
    session_slot_layout_.push_back(std::move(meta));
  }
  if (session_devices_ordered_.empty()) {
    err = "no preset cameras";
    return false;
  }
  session_id_ = session_devices_ordered_.front();
  return PrepareHardwareTriggerLocked(err);
}

void CameraHubServiceImpl::TryRecoverSerialQuick() {
  const std::uint32_t com = startup_serial_com_main_;
  if (com == 0) {
    return;
  }
  auto& sp = camera3d::serial::SerialPortManager::Instance();
  if (sp.IsOpen()) {
    return;
  }
  CAMERA3D_LOGW("TryRecoverSerialQuick: 串口未连接，尝试打开 COM{} …", com);
  if (!sp.Open(com, 115200)) {
    const std::string em = sp.GetLastErrorMessage();
    hub_runtime_.Set(HubServiceStateCode::kRuntimeSerialNotConnected, em);
    CAMERA3D_LOGE("TryRecoverSerialQuick: 打开失败 {}", em);
    return;
  }
  {
    std::scoped_lock<std::mutex> lk(mu_);
    if (!unified_config_mode_) {
      return;
    }
    if (session_id_.empty()) {
      hub_runtime_.Set(HubServiceStateCode::kSessionNotEstablished,
                       "serial ok; call Connect to resume capture pipeline");
    } else {
      hub_runtime_.Set(HubServiceStateCode::kReady, "");
    }
  }
  CAMERA3D_LOGI("TryRecoverSerialQuick: 串口已恢复 {}", sp.CurrentPortName());
}

bool CameraHubServiceImpl::FillUnifiedGateLocked(const char* rpc_name, v1::Status* st) {
  std::int32_t c = 0;
  std::string m;
  hub_runtime_.Snapshot(&c, &m);
  if (c != HubServiceStateCode::kReady) {
    st->set_code(c);
    st->set_message(m);
    CAMERA3D_LOGW("[{}] 拒绝：Hub 状态 code={} msg={}", rpc_name, c, m);
    return true;
  }
  if (session_id_.empty()) {
    st->set_code(HubServiceStateCode::kSessionNotEstablished);
    st->set_message("call Connect to establish session with hub");
    CAMERA3D_LOGW("[{}] 拒绝：无活动会话", rpc_name);
    return true;
  }
  return false;
}

void CameraHubServiceImpl::ApplySerialWatchTick() { TryRecoverSerialQuick(); }

}  // namespace

int RunHubApp(const std::string& listen_address, camera3d::camera::CameraManager& cameras,
              const std::string& hub_config_path) {
  const std::string listen = NormalizeGrpcListenAddress(listen_address);
  if (listen != listen_address) {
    CAMERA3D_LOGI("监听地址已规范化: \"{}\" -> \"{}\"", listen_address, listen);
  }

  CameraHubServiceImpl service(&cameras);
  HubFileConfig loaded_fc{};
  bool hub_config_loaded = false;
  std::error_code ec;
  if (!std::filesystem::exists(hub_config_path, ec) || ec) {
    const std::string msg =
        std::string("hub config file not found or inaccessible: ") + hub_config_path +
        (ec ? std::string(" (") + ec.message() + ")" : std::string{});
    CAMERA3D_LOGE("[Hub startup] {} (zh: {})", msg, camera3d::hub::HubServiceStateDescribeZh(
                                                      camera3d::hub::HubServiceStateCode::kConfigFileNotFound));
    service.ReportStartupFailure(camera3d::hub::HubServiceStateCode::kConfigFileNotFound, msg);
  } else {
    std::string cerr;
    if (!LoadHubFileConfig(hub_config_path, loaded_fc, cerr)) {
      const std::string msg = std::string("hub config parse failed: ") + cerr;
      CAMERA3D_LOGE("[Hub startup] path=\"{}\" {} (zh: {})", hub_config_path, cerr,
                    camera3d::hub::HubServiceStateDescribeZh(camera3d::hub::HubServiceStateCode::kConfigInvalid));
      service.ReportStartupFailure(camera3d::hub::HubServiceStateCode::kConfigInvalid, msg);
    } else {
      hub_config_loaded = true;
      std::string serr;
      if (!service.UnifiedStartupFromConfig(loaded_fc, serr)) {
        CAMERA3D_LOGE("[Hub startup] UnifiedStartupFromConfig failed: {} (详见 Connect.status)", serr);
      }
    }
  }
  grpc::ServerBuilder builder;
  int selected_port = 0;
  builder.AddListeningPort(listen, grpc::InsecureServerCredentials(), &selected_port);
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  if (!server || selected_port == 0) {
    CAMERA3D_LOGE(
        "gRPC 启动失败 listen=\"{}\" selected_port={}。"
        "若 selected_port 为 0，多为端口已被占用（可先结束旧的 hub_service 或换端口，例如 hub_service 0.0.0.0:50052）、"
        "或监听地址格式不被接受。",
        listen, selected_port);
    return 2;
  }
  CAMERA3D_LOGI("CameraHub 监听 {} (实际绑定端口 {})", listen, selected_port);

  std::atomic<bool> stop_discovery_broadcast{false};
  std::thread discovery_broadcast_thread;
  if (hub_config_loaded && loaded_fc.discovery.enable) {
    const HubBroadcastRuntimeParams bparams =
        BuildHubBroadcastParams(&loaded_fc, selected_port);
    discovery_broadcast_thread = std::thread([bparams, &stop_discovery_broadcast] {
      RunHubDeviceBroadcastLoop(stop_discovery_broadcast, bparams);
    });
  }

  std::atomic<bool> stop_serial_monitor{false};
  std::thread serial_monitor;
  if (service.ManagedSerialCom() != 0) {
    serial_monitor = std::thread([&service, &stop_serial_monitor] {
      while (!stop_serial_monitor.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (stop_serial_monitor.load(std::memory_order_relaxed)) {
          break;
        }
        service.ApplySerialWatchTick();
      }
    });
  }

  server->Wait();
  stop_discovery_broadcast.store(true, std::memory_order_relaxed);
  if (discovery_broadcast_thread.joinable()) {
    discovery_broadcast_thread.join();
  }
  stop_serial_monitor.store(true, std::memory_order_relaxed);
  if (serial_monitor.joinable()) {
    serial_monitor.join();
  }
  return 0;
}

}  // namespace camera3d::hub
