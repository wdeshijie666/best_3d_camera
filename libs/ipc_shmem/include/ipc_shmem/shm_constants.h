#pragma once

// Hub / 联调重建 默认共享内存区名与容量常量。

#include <cstddef>
#include <cstdint>

namespace camera3d::ipc {

// 与 Hub、重建等进程约定的默认共享内存区（Boost.Interprocess）
inline constexpr char kDefaultHubRingRegionName[] = "camera3d_hub_ring";
/// 环形槽个数：须 ≥ 单次 Capture 在 SHM 上连续写入的槽数（多路×burst + 可选 final），
/// 否则后写覆盖先写物理区，Depth 多 ref 仍指向旧 offset 会读到错误帧；viewer 也无法正确落盘全部原始图。
inline constexpr std::uint32_t kDefaultHubRingSlotCount = 64u;
/// 总字节与槽数共同决定单槽最大 payload；约保持「单槽 ~8MB 量级」以容纳常见全幅 raw。
inline constexpr std::size_t kDefaultHubRingTotalBytes = static_cast<std::size_t>(512ull * 1024 * 1024);

// Hub↔重建 联调 echo 区（重建创建/写入，Hub 只读验证；与中枢主 ring 分离）
inline constexpr char kReconLoopbackEchoRingRegionName[] = "camera3d_recon_loopback_echo";
inline constexpr std::size_t kReconLoopbackEchoRingTotalBytes = static_cast<std::size_t>(32ull * 1024 * 1024);

}  // namespace camera3d::ipc
