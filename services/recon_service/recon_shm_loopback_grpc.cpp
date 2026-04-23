#include "recon_shm_loopback_grpc.h"

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
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace camera3d::recon {
namespace {

namespace fs = std::filesystem;

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

std::string NormalizeListen(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
  if (s.empty()) return "0.0.0.0:50053";
  if (s.find(':') == std::string::npos) return s + ":50053";
  return s;
}

class ReconShmLoopbackTestServiceImpl final : public camera3d::recon_test::v1::ReconShmLoopbackTest::Service {
 public:
  grpc::Status ProcessHubRaw(grpc::ServerContext* /*ctx*/,
                              const camera3d::recon_test::v1::ProcessHubRawRequest* request,
                              camera3d::recon_test::v1::ProcessHubRawReply* reply) override {
    reply->set_code(1);
    reply->set_message("init");
    camera3d::ipc::ShmRingBuffer hub;
    if (!hub.CreateOrOpen(request->hub_region_name(), camera3d::ipc::kDefaultHubRingTotalBytes, false)) {
      reply->set_code(2);
      reply->set_message("hub shm open failed");
      return grpc::Status::OK;
    }

    camera3d::ipc::ShmSlotHeader meta{};
    const std::uint8_t* payload = nullptr;
    std::size_t len = 0;

    const int nburst = request->hub_raw_frames_size();
    if (nburst > 0) {
      const fs::path root =
          camera3d::shm_loopback_test::ResolveSaveRoot("CAMERA3D_SHM_LOOPBACK_SAVE_ROOT", "logs/shm_loopback");
      const std::string ts = camera3d::shm_loopback_test::MakeTimestampFolderName();
      const fs::path batch =
          root / "recon_from_hub" / (std::to_string(request->client_capture_id()) + "_" + ts);
      try {
        fs::create_directories(batch);
      } catch (const std::exception& ex) {
        reply->set_code(5);
        reply->set_message(std::string("mkdir failed: ") + ex.what());
        return grpc::Status::OK;
      }

      int saved = 0;
      for (int i = 0; i < nburst; ++i) {
        const auto& fr = request->hub_raw_frames(i);
        if (!hub.TryReadSlot(fr.hub_slot_index(), meta, payload, len)) {
          reply->set_code(3);
          reply->set_message("hub TryReadSlot failed at burst item " + std::to_string(i));
          return grpc::Status::OK;
        }
        if (meta.seq_publish != fr.hub_slot_seq()) {
          reply->set_code(4);
          reply->set_message("hub seq mismatch at burst item " + std::to_string(i));
          return grpc::Status::OK;
        }
        std::ostringstream fn;
        fn << "raw_idx" << std::setw(3) << std::setfill('0') << i << "_cam" << fr.camera_index() << "_burst"
           << fr.burst_frame_index() << ".bin";
        const fs::path fp = batch / fn.str();
        std::ofstream out(fp, std::ios::binary | std::ios::trunc);
        if (!out) {
          reply->set_code(5);
          reply->set_message("open file failed: " + fp.string());
          return grpc::Status::OK;
        }
        out.write(reinterpret_cast<const char*>(payload), static_cast<std::streamsize>(len));
        if (!out) {
          reply->set_code(5);
          reply->set_message("write file failed: " + fp.string());
          return grpc::Status::OK;
        }
        ++saved;
      }

      const auto& fr0 = request->hub_raw_frames(0);
      if (!hub.TryReadSlot(fr0.hub_slot_index(), meta, payload, len)) {
        reply->set_code(3);
        reply->set_message("hub TryReadSlot failed for echo (first slot)");
        return grpc::Status::OK;
      }
      if (meta.seq_publish != fr0.hub_slot_seq()) {
        reply->set_code(4);
        reply->set_message("hub seq mismatch for echo (first slot)");
        return grpc::Status::OK;
      }

      CAMERA3D_LOGI("[loopback] 重建已保存 Hub burst 原始帧 n={} folder={} capture_id={}", saved, batch.string(),
                    request->client_capture_id());
    } else {
      if (!hub.TryReadSlot(request->hub_slot_index(), meta, payload, len)) {
        reply->set_code(3);
        reply->set_message("hub TryReadSlot failed");
        return grpc::Status::OK;
      }
      if (meta.seq_publish != request->hub_slot_seq()) {
        reply->set_code(4);
        reply->set_message("hub seq mismatch");
        return grpc::Status::OK;
      }

      const fs::path root =
          camera3d::shm_loopback_test::ResolveSaveRoot("CAMERA3D_SHM_LOOPBACK_SAVE_ROOT", "logs/shm_loopback");
      fs::path folder;
      int idx = 0;
      if (!camera3d::shm_loopback_test::SaveNextSequentialBinary(root, "recon_from_hub", payload, len, folder,
                                                                 idx)) {
        reply->set_code(5);
        reply->set_message("recon save failed");
        return grpc::Status::OK;
      }
      CAMERA3D_LOGI("[loopback] 重建已保存 Hub 原始帧(单槽) folder={} index={} capture_id={}", folder.string(), idx,
                    request->client_capture_id());
    }

    camera3d::ipc::ShmRingBuffer echo;
    if (!echo.CreateOrOpen(camera3d::ipc::kReconLoopbackEchoRingRegionName,
                           camera3d::ipc::kReconLoopbackEchoRingTotalBytes, false)) {
      if (!echo.CreateOrOpen(camera3d::ipc::kReconLoopbackEchoRingRegionName,
                             camera3d::ipc::kReconLoopbackEchoRingTotalBytes, true)) {
        reply->set_code(6);
        reply->set_message("echo shm create/open failed");
        return grpc::Status::OK;
      }
    }

    std::uint64_t echo_seq = 0;
    std::uint32_t echo_slot = 0;
    if (!echo.TryWriteNextSlot(payload, len, meta.width, meta.height, meta.pixel_format, &echo_seq, &echo_slot)) {
      reply->set_code(7);
      reply->set_message("echo write failed");
      return grpc::Status::OK;
    }

    camera3d::ipc::ShmSlotHeader em{};
    const std::uint8_t* ep = nullptr;
    std::size_t elen = 0;
    if (!echo.TryReadSlot(echo_slot, em, ep, elen)) {
      reply->set_code(8);
      reply->set_message("echo readback failed");
      return grpc::Status::OK;
    }

    const std::string hub_target = GetEnvOr("CAMERA3D_HUB_GRPC", "127.0.0.1:50051");
    auto ch = grpc::CreateChannel(hub_target, grpc::InsecureChannelCredentials());
    camera3d::hub::v1::CameraHub::Stub hub_stub(ch);
    grpc::ClientContext hctx;
    hctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(30));
    camera3d::hub::v1::HubTestSaveReconEchoRequest hreq;
    camera3d::hub::v1::HubTestSaveReconEchoReply hres;
    hreq.set_client_capture_id(request->client_capture_id());
    hreq.set_echo_slot_index(echo_slot);
    auto* ef = hreq.mutable_echo_frame();
    ef->set_region_name(echo.RegionName());
    ef->set_seq(echo_seq);
    ef->set_offset_bytes(em.payload_offset);
    ef->set_size_bytes(em.payload_size);
    ef->set_width(em.width);
    ef->set_height(em.height);
    ef->set_pixel_format(em.pixel_format);
    ef->set_timestamp_unix_ns(em.timestamp_unix_ns);
    const grpc::Status hst = hub_stub.TestSaveReconEcho(&hctx, hreq, &hres);
    if (!hst.ok()) {
      reply->set_code(9);
      reply->set_message(std::string("hub TestSaveReconEcho grpc failed: ") + hst.error_message());
      return grpc::Status::OK;
    }
    if (!hres.has_status() || hres.status().code() != 0) {
      reply->set_code(10);
      reply->set_message(hres.has_status() ? hres.status().message() : "hub status missing");
      return grpc::Status::OK;
    }

    reply->set_code(0);
    reply->clear_message();
    return grpc::Status::OK;
  }
};

}  // namespace

void RunReconShmLoopbackGrpcServer(const std::string& listen_address) {
  const std::string listen = NormalizeListen(listen_address);
  ReconShmLoopbackTestServiceImpl svc;
  grpc::ServerBuilder builder;
  int selected = 0;
  builder.AddListeningPort(listen, grpc::InsecureServerCredentials(), &selected);
  builder.RegisterService(&svc);
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  if (!server || selected == 0) {
    CAMERA3D_LOGE("[loopback] ReconShmLoopbackTest gRPC 启动失败 listen={} selected_port={}", listen, selected);
    return;
  }
  CAMERA3D_LOGI("[loopback] ReconShmLoopbackTest 监听 {} (绑定端口 {})", listen, selected);
  server->Wait();
}

}  // namespace camera3d::recon
