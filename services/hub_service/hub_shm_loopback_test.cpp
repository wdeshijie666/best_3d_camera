#include "hub_shm_loopback_test.h"

#include "ipc_shmem/shm_constants.h"
#include "ipc_shmem/shm_ring_buffer.h"
#include "platform_diag/logging.h"
#include "shm_loopback_test/save_utils.h"

#include <grpcpp/grpcpp.h>

#include "camera_hub.grpc.pb.h"
#include "camera_recon_test.grpc.pb.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace camera3d::hub {
namespace {

std::string GetEnvOr(const char* key, const char* fallback) {
#if defined(_WIN32)
  char buf[2048];
  const DWORD n = ::GetEnvironmentVariableA(key, buf, static_cast<DWORD>(sizeof(buf)));
  if (n > 0 && n < sizeof(buf)) return std::string(buf, buf + n);
#else
  if (const char* p = std::getenv(key)) return std::string(p);
#endif
  return std::string(fallback);
}

}  // namespace

void TryInvokeReconShmLoopbackAfterPublishBurst(const std::string& hub_region_name, std::uint64_t client_capture_id,
                                                const std::vector<ReconLoopbackHubRawSlot>& frames) {
  if (frames.empty()) {
    CAMERA3D_LOGE("[loopback] ProcessHubRaw 跳过：frames 为空 capture_id={}", client_capture_id);
    return;
  }
  const std::string target = GetEnvOr("CAMERA3D_RECON_TEST_GRPC", "127.0.0.1:50053");
  auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
  if (!channel) {
    CAMERA3D_LOGE("[loopback] 创建到重建测试 gRPC 通道失败 target={}", target);
    return;
  }
  camera3d::recon_test::v1::ReconShmLoopbackTest::Stub stub(channel);
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(60));
  camera3d::recon_test::v1::ProcessHubRawRequest req;
  req.set_hub_region_name(hub_region_name);
  req.set_client_capture_id(client_capture_id);
  req.clear_hub_raw_frames();
  for (const auto& f : frames) {
    camera3d::recon_test::v1::HubRawFrameRef* r = req.add_hub_raw_frames();
    r->set_hub_slot_index(f.hub_slot_index);
    r->set_hub_slot_seq(f.hub_slot_seq);
    r->set_camera_index(f.camera_index);
    r->set_burst_frame_index(f.burst_frame_index);
    r->set_width(f.width);
    r->set_height(f.height);
    r->set_pixel_format(f.pixel_format);
  }
  const ReconLoopbackHubRawSlot& first = frames.front();
  req.set_hub_slot_index(first.hub_slot_index);
  req.set_hub_slot_seq(first.hub_slot_seq);
  req.set_width(first.width);
  req.set_height(first.height);
  req.set_pixel_format(first.pixel_format);

  camera3d::recon_test::v1::ProcessHubRawReply rep;
  const grpc::Status st = stub.ProcessHubRaw(&ctx, req, &rep);
  if (!st.ok()) {
    CAMERA3D_LOGE("[loopback] ProcessHubRaw gRPC 失败 code={} msg={}", static_cast<int>(st.error_code()),
                  st.error_message());
    return;
  }
  if (rep.code() != 0) {
    CAMERA3D_LOGE("[loopback] ProcessHubRaw 业务失败 code={} msg={}", rep.code(), rep.message());
    return;
  }
  CAMERA3D_LOGI("[loopback] ProcessHubRaw 成功 capture_id={} hub_raw_frames={}", client_capture_id,
                static_cast<int>(frames.size()));
}

grpc::Status HubServeTestSaveReconEchoGrpc(const camera3d::hub::v1::HubTestSaveReconEchoRequest* request,
                                            camera3d::hub::v1::HubTestSaveReconEchoReply* reply) {
  reply->mutable_status()->set_code(1);
  reply->mutable_status()->set_message("invalid echo ref");
  if (!request->has_echo_frame()) {
    reply->mutable_status()->set_message("missing echo_frame");
    return grpc::Status::OK;
  }
  const auto& ef = request->echo_frame();
  if (ef.region_name().empty()) {
    reply->mutable_status()->set_message("empty region_name");
    return grpc::Status::OK;
  }

  camera3d::ipc::ShmRingBuffer echo;
  if (!echo.CreateOrOpen(ef.region_name(), camera3d::ipc::kReconLoopbackEchoRingTotalBytes, false)) {
    reply->mutable_status()->set_code(2);
    reply->mutable_status()->set_message("echo shm open failed");
    return grpc::Status::OK;
  }
  camera3d::ipc::ShmSlotHeader meta{};
  const std::uint8_t* p = nullptr;
  std::size_t len = 0;
  if (!echo.TryReadSlot(request->echo_slot_index(), meta, p, len)) {
    reply->mutable_status()->set_code(3);
    reply->mutable_status()->set_message("echo TryReadSlot failed");
    return grpc::Status::OK;
  }
  if (meta.seq_publish != static_cast<std::uint64_t>(ef.seq())) {
    reply->mutable_status()->set_code(4);
    reply->mutable_status()->set_message("echo seq mismatch");
    return grpc::Status::OK;
  }
  if (static_cast<std::uint64_t>(len) != ef.size_bytes()) {
    reply->mutable_status()->set_code(5);
    reply->mutable_status()->set_message("echo size mismatch");
    return grpc::Status::OK;
  }

  const std::filesystem::path root =
      camera3d::shm_loopback_test::ResolveSaveRoot("CAMERA3D_SHM_LOOPBACK_SAVE_ROOT", "logs/shm_loopback");
  std::filesystem::path folder;
  int idx = 0;
  if (!camera3d::shm_loopback_test::SaveNextSequentialBinary(root, "hub_from_recon", p, len, folder, idx)) {
    reply->mutable_status()->set_code(6);
    reply->mutable_status()->set_message("save echo payload failed");
    return grpc::Status::OK;
  }
  reply->mutable_status()->set_code(0);
  reply->mutable_status()->clear_message();
  CAMERA3D_LOGI("[loopback] Hub 已保存重建回传 echo folder={} index={} capture_id={}", folder.string(), idx,
                request->client_capture_id());
  return grpc::Status::OK;
}

}  // namespace camera3d::hub
