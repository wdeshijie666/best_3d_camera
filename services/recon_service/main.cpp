// 重建侧进程：打开与 Hub 同名的 SHM 区，轮询新 seq 驱动 pipeline；可选编译 gRPC 回环联调服务。

#include "ipc_shmem/shm_constants.h"
#include "ipc_shmem/shm_ring_buffer.h"
#include "pipeline_api/reconstruction_pipeline.h"
#include "platform_diag/build_info.h"
#include "platform_diag/crash_handler.h"
#include "platform_diag/diag_config.h"
#include "platform_diag/logging.h"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#ifdef CAMERA3D_ENABLE_RECON_HUB_BURST_WATCHER
#include "recon_hub_burst_receiver.h"
#endif
#ifdef CAMERA3D_ENABLE_RECON_SHM_LOOPBACK_TEST
#include "recon_shm_loopback_grpc.h"
#endif

// 重建服务：platform_diag + Boost 共享内存 + pipeline_api 重建接口
int main(int argc, char** argv) {
#ifndef CAMERA3D_ENABLE_RECON_SHM_LOOPBACK_TEST
  (void)argc;
  (void)argv;
#endif
  camera3d::diag::DiagConfig diag;
  diag.log_file_stem = "recon_service";
  diag.crash_dump_dir = "logs/crash/recon_service";
  camera3d::diag::InitLogging(diag, "recon_service");
  camera3d::diag::InstallCrashHandlers(diag.crash_dump_dir, "recon_service");

  CAMERA3D_LOGI("{} {} 启动", camera3d::diag::kCamera3dStackName, camera3d::diag::kCamera3dStackVersion);

#ifdef CAMERA3D_ENABLE_RECON_HUB_BURST_WATCHER
  camera3d::recon::RunReconHubBurstSaveWatcherDetached();
#endif

#ifdef CAMERA3D_ENABLE_RECON_SHM_LOOPBACK_TEST
  const std::string loop_listen =
      (argc >= 2 && argv[1] && argv[1][0] != '\0') ? std::string(argv[1]) : std::string("0.0.0.0:50053");
  std::thread loop_grpc([loop_listen] { camera3d::recon::RunReconShmLoopbackGrpcServer(loop_listen); });
  loop_grpc.detach();
#endif

  camera3d::ipc::ShmRingBuffer shm;
  if (!shm.CreateOrOpen(camera3d::ipc::kDefaultHubRingRegionName,
                         camera3d::ipc::kDefaultHubRingTotalBytes, false)) {
    CAMERA3D_LOGW("未打开共享内存 {}，请确认中枢已创建", camera3d::ipc::kDefaultHubRingRegionName);
  }

  auto recon = camera3d::pipeline::CreateLoggingReconstructionPipeline();
  std::uint64_t last_seen_seq = 0;

  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(2));

    camera3d::ipc::ShmSlotHeader meta{};
    const std::uint8_t* payload = nullptr;
    std::size_t len = 0;
    std::uint32_t slot_idx = 0;
    if (shm.TryReadLatestSlot(meta, payload, len, &slot_idx) && meta.seq_publish > last_seen_seq) {
      last_seen_seq = meta.seq_publish;
      camera3d::pipeline::ShmFrameView view{};
      view.data = payload;
      view.size_bytes = len;
      view.width = meta.width;
      view.height = meta.height;
      view.pixel_format = meta.pixel_format;
      view.seq = meta.seq_publish;
      std::vector<std::uint8_t> out_blob;
      std::string msg;
      const auto re = recon->Process(view, out_blob, msg);
      CAMERA3D_LOGI("重建处理 seq={} slot={} err={} msg={}", meta.seq_publish, slot_idx,
                    static_cast<int>(re), msg);
    } else {
      CAMERA3D_LOGI("重建心跳 region={} max_seq={} slots={}", camera3d::ipc::kDefaultHubRingRegionName,
                    shm.MaxPublishedSeq(), shm.SlotCount());
    }
  }
  return 0;
}
