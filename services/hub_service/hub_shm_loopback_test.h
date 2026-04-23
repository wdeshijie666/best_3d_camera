#pragma once

#include <cstdint>
#include <grpcpp/grpcpp.h>
#include <string>
#include <vector>

namespace camera3d::hub::v1 {
class HubTestSaveReconEchoRequest;
class HubTestSaveReconEchoReply;
}  // namespace camera3d::hub::v1

namespace camera3d::hub {

/// 传给重建联调 RPC 的 Hub 原始槽描述（与 publication.camera_raw_slots 对齐）。
struct ReconLoopbackHubRawSlot {
  std::uint32_t hub_slot_index = 0;
  std::uint64_t hub_slot_seq = 0;
  std::uint32_t camera_index = 0;
  std::uint32_t burst_frame_index = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t pixel_format = 0;
};

// Hub 在发布 raw 到 SHM 后调用：gRPC 通知重建测试服务读 Hub 槽位（支持 burst 多帧）并回写 echo。
void TryInvokeReconShmLoopbackAfterPublishBurst(const std::string& hub_region_name, std::uint64_t client_capture_id,
                                                const std::vector<ReconLoopbackHubRawSlot>& frames);

// CameraHub::TestSaveReconEcho 的实现体（持锁方在调用处保证与会话一致）
grpc::Status HubServeTestSaveReconEchoGrpc(const camera3d::hub::v1::HubTestSaveReconEchoRequest* request,
                                            camera3d::hub::v1::HubTestSaveReconEchoReply* reply);

}  // namespace camera3d::hub
