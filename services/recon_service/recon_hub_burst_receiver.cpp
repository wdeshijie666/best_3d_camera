/**
 * @file recon_hub_burst_receiver.cpp
 * @brief 重建进程侧：gRPC 连 Hub 后轮询 GetDepth，接收 camera_raw_frames 并从 SHM 落盘。
 */
#include "recon_hub_burst_receiver.h"

#include "ipc_shmem/shm_constants.h"
#include "ipc_shmem/shm_ring_buffer.h"
#include "platform_diag/logging.h"

#include <grpcpp/grpcpp.h>

#include "camera_hub.grpc.pb.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <memory>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace camera3d::recon {
namespace {

namespace fs = std::filesystem;
namespace v1 = camera3d::hub::v1;

std::string EnvStr(const char* key, const char* fallback) {
#if defined(_WIN32)
  char buf[2048];
  const DWORD n = ::GetEnvironmentVariableA(key, buf, static_cast<DWORD>(sizeof(buf)));
  if (n > 0 && n < sizeof(buf)) {
    return std::string(buf, buf + n);
  }
#else
  if (const char* p = std::getenv(key)) {
    return std::string(p);
  }
#endif
  return std::string(fallback ? fallback : "");
}

int PollIntervalMs() {
  const std::string s = EnvStr("CAMERA3D_RECON_BURST_POLL_MS", "250");
  const long v = std::strtol(s.c_str(), nullptr, 10);
  if (v < 50) return 50;
  if (v > 10000) return 10000;
  return static_cast<int>(v);
}

std::string MakeTimestampFolderName() {
#if defined(_WIN32)
  SYSTEMTIME st{};
  ::GetLocalTime(&st);
  const int zz = static_cast<int>(st.wMilliseconds / 10);
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%04u%02u%02u_%02u%02u%02u_%02u", static_cast<unsigned>(st.wYear),
                static_cast<unsigned>(st.wMonth), static_cast<unsigned>(st.wDay),
                static_cast<unsigned>(st.wHour), static_cast<unsigned>(st.wMinute),
                static_cast<unsigned>(st.wSecond), static_cast<unsigned>(zz));
  return std::string(buf);
#else
  using clock = std::chrono::system_clock;
  const auto now = clock::now();
  const std::time_t t = clock::to_time_t(now);
  std::tm lt{};
  localtime_r(&t, &lt);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d_%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                lt.tm_hour, lt.tm_min, lt.tm_sec, static_cast<int>(ms.count() / 10));
  return std::string(buf);
#endif
}

void HubBurstWatcherThreadMain() {
  const std::string hub_target = EnvStr("CAMERA3D_HUB_GRPC", "127.0.0.1:50051");
  const int poll_ms = PollIntervalMs();
  const fs::path save_root = [] {
    const std::string from_env = EnvStr("CAMERA3D_RECON_BURST_SAVE_ROOT", "");
    if (!from_env.empty()) {
      return fs::path(from_env);
    }
    return fs::current_path() / "recon_img_save";
  }();

  std::uint64_t last_saved_capture_id = 0;

  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));

    auto channel = grpc::CreateChannel(hub_target, grpc::InsecureChannelCredentials());
    if (!channel) {
      continue;
    }
    std::unique_ptr<v1::CameraHub::Stub> stub = v1::CameraHub::NewStub(channel);
    grpc::ClientContext cctx;
    cctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(8));
    v1::ConnectRequest creq;
    creq.set_device_ip("recon_service_burst_watcher");
    creq.set_session_hint("recon_burst");
    creq.set_projector_com_index(0);
    v1::ConnectReply crpl;
    const grpc::Status cst = stub->Connect(&cctx, creq, &crpl);
    if (!cst.ok() || !crpl.has_status() || crpl.status().code() != 0 || crpl.session_id().empty()) {
      CAMERA3D_LOGW("[recon-burst] Connect 失败 peer={} grpc_ok={} hub_code={} msg={}", hub_target, cst.ok(),
                    crpl.has_status() ? crpl.status().code() : -1,
                    crpl.has_status() ? crpl.status().message() : "(no status)");
      continue;
    }
    const std::string session_id = crpl.session_id();
    CAMERA3D_LOGI("[recon-burst] 已连 Hub session_id 前缀={}…", session_id.substr(0, 8));

    for (;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));

      grpc::ClientContext dctx;
      dctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(8));
      v1::DepthRequest dreq;
      dreq.set_session_id(session_id);
      dreq.set_client_capture_id(0);
      dreq.set_job_id(0);
      v1::DepthReply drpl;
      const grpc::Status dst = stub->GetDepth(&dctx, dreq, &drpl);
      if (!dst.ok()) {
        CAMERA3D_LOGW("[recon-burst] GetDepth gRPC 失败 {}，将重连", dst.error_message());
        break;
      }
      if (!drpl.has_status()) {
        continue;
      }
      if (drpl.status().code() != 0) {
        if (drpl.status().code() == 8) {
          continue;
        }
        if (drpl.status().code() == 3) {
          CAMERA3D_LOGW("[recon-burst] GetDepth 会话无效，将重连");
          break;
        }
        continue;
      }

      const std::uint64_t capture_key = drpl.client_capture_id();
      if (capture_key == 0) {
        continue;
      }
      if (capture_key < last_saved_capture_id) {
        CAMERA3D_LOGI("[recon-burst] capture_id 回退 {}→{}，按 Hub 重启处理", last_saved_capture_id, capture_key);
        last_saved_capture_id = 0;
      }
      if (capture_key <= last_saved_capture_id) {
        continue;
      }

      const int nraw = drpl.camera_raw_frames_size();
      if (nraw <= 0) {
        continue;
      }

      const std::string& region0 = drpl.camera_raw_frames(0).frame().region_name();
      if (region0.empty()) {
        CAMERA3D_LOGW("[recon-burst] camera_raw_frames[0] 无 region_name");
        continue;
      }

      camera3d::ipc::ShmRingBuffer shm;
      if (!shm.CreateOrOpen(region0, camera3d::ipc::kDefaultHubRingTotalBytes, false)) {
        CAMERA3D_LOGW("[recon-burst] SHM CreateOrOpen 失败 region={}", region0);
        continue;
      }

      const fs::path batch = save_root / MakeTimestampFolderName();
      try {
        fs::create_directories(batch);
      } catch (const std::exception& ex) {
        CAMERA3D_LOGE("[recon-burst] 创建目录失败 {}: {}", batch.string(), ex.what());
        continue;
      }

      int ok_n = 0;
      for (int i = 0; i < nraw; ++i) {
        const auto& cref = drpl.camera_raw_frames(i);
        const auto& fr = cref.frame();
        if (fr.region_name() != region0) {
          CAMERA3D_LOGW("[recon-burst] 跳过异区名项 i={} region={}", i, fr.region_name());
          continue;
        }
        const std::uint8_t* payload = nullptr;
        std::size_t len = 0;
        if (!shm.TryReadMappedRange(fr.offset_bytes(), fr.size_bytes(), payload, len) || !payload || len == 0) {
          CAMERA3D_LOGW("[recon-burst] TryReadMappedRange 失败 i={} off={} sz={}", i,
                        static_cast<unsigned long long>(fr.offset_bytes()),
                        static_cast<unsigned long long>(fr.size_bytes()));
          continue;
        }
        std::ostringstream fn;
        fn << "raw_idx" << std::setw(3) << std::setfill('0') << i << "_cam" << cref.camera_index() << "_burst"
           << cref.burst_frame_index() << "_" << fr.width() << "x" << fr.height() << "_pf" << fr.pixel_format()
           << "_cid" << capture_key << ".bin";
        const fs::path fp = batch / fn.str();
        std::ofstream out(fp, std::ios::binary | std::ios::trunc);
        if (!out) {
          CAMERA3D_LOGW("[recon-burst] 打开文件失败 {}", fp.string());
          continue;
        }
        out.write(reinterpret_cast<const char*>(payload), static_cast<std::streamsize>(len));
        if (!out) {
          CAMERA3D_LOGW("[recon-burst] 写入失败 {}", fp.string());
          continue;
        }
        ++ok_n;
      }

      if (ok_n > 0) {
        last_saved_capture_id = capture_key;
        if (ok_n < nraw) {
          CAMERA3D_LOGW("[recon-burst] 部分失败 成功={} 期望={} capture_id={}", ok_n, nraw, capture_key);
        }
        CAMERA3D_LOGI("[recon-burst] 已保存 {}/{} 张原始图 capture_id={} 目录={}", ok_n, nraw, capture_key,
                      batch.string());
      }
    }
  }
}

}  // namespace

void RunReconHubBurstSaveWatcherDetached() {
  const std::string dis = EnvStr("CAMERA3D_RECON_DISABLE_BURST_SAVE", "0");
  if (dis == "1" || dis == "true" || dis == "TRUE") {
    CAMERA3D_LOGI("[recon-burst] 已通过 CAMERA3D_RECON_DISABLE_BURST_SAVE 关闭落盘线程");
    return;
  }
  std::thread([] { HubBurstWatcherThreadMain(); }).detach();
}

}  // namespace camera3d::recon
