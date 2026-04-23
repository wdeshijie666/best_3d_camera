#pragma once

// 非 stub 构建：后台线程连 Hub → GetDepth 拉取 camera_raw_frames，将 burst 原始图落盘（见 recon_hub_burst_receiver.cpp）。

namespace camera3d::recon {

/// 启动 detached 线程：轮询 Hub GetDepth（最新 capture），将 repeated camera_raw_frames 从 SHM 读出并写入本地目录。
/// 若环境变量 CAMERA3D_RECON_DISABLE_BURST_SAVE=1 则直接返回、不启线程。
void RunReconHubBurstSaveWatcherDetached();

}  // namespace camera3d::recon
