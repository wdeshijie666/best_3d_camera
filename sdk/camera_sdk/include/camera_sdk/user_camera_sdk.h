#pragma once

// 用户侧 gRPC 客户端抽象：连接 Hub、触发采集、读写曝光增益、按 ShmFrameRef 拉取深度等元数据。
// 实现见 camera_sdk_grpc.cpp（真实 gRPC）或 camera_sdk_stub.cpp（无 gRPC 构建）。

#include "camera_sdk/best_types.h"
#include "camera_sdk/export.h"

#include <cstdint>
#include <string>
#include <vector>

namespace camera3d::sdk {

/// UDP 设备发现结果（与 Hub 广播 JSON 字段对应）。
struct DiscoveredHubDevice {
  std::string model;
  std::string serial_number;
  std::string mac_address;
  std::string hub_host;
  std::uint16_t hub_port = 0;
};

// 用户版稳定能力：连接、采集、参数与结果拉取。
// 面向应用层更推荐 camera_sdk/best_camera_3d.h 中的 BestCamera3D（Best* 命名）。
class CAMERA_SDK_API IUserCameraSdk {
 public:
  virtual ~IUserCameraSdk();

  // hub_address：gRPC 目标，一般为 "host:port"。device_ip：Hub 侧设备地址（如 null:virtual0）。
  // session_hint：可选，透传 ConnectRequest.session_hint。timeout_ms：>0 时为本次 RPC 设置 deadline。
  // projector_com_index：Hub 侧打开投影仪串口的 Windows COM 序号；0 表示不打开（联调）。
  virtual bool Connect(const std::string& hub_address, const std::string& device_ip,
                       const std::string& session_hint = {}, int timeout_ms = 0,
                       std::uint32_t projector_com_index = 0) = 0;
  // 释放会话；之后需重新 Connect 才能采集。
  virtual void Disconnect() = 0;
  // 是否已建立有效会话（以实现侧 session 非空为准）。
  virtual bool IsConnected() const = 0;

  // 最近一次失败：Hub 业务码优先，否则为 gRPC error_code（与 GetLastHubStatusCode 区分见实现）。
  virtual int GetLastErrorCode() const = 0;
  virtual std::string GetLastErrorMessage() const = 0;

  /// 最近一次各 RPC 的 reply.status.code（与 camera3d/hub/hub_service_state_codes.h 一致）。
  /// 成功或最近一次失败仅为 gRPC 层时为 0；应用可结合 camera_sdk/hub_client_action.h 判断建议动作。
  virtual int GetLastHubStatusCode() const = 0;

  // client_capture_id：非 0 时透传 Hub；0 表示由 SDK 自动生成单调 ID。
  // with_detection_pipeline：协议保留，Hub 当前忽略（检测并入重建规划）。
  // with_reconstruction_pipeline：true 时 Hub 为重建结果预留 SHM final 槽占位。
  // test_recon_shm_loopback / test_inline_image_reply：联调透传，见 Hub 编译选项。
  virtual bool CaptureSync(std::uint64_t* out_job_id, std::uint64_t client_capture_id = 0,
                           bool with_detection_pipeline = false,
                           bool with_reconstruction_pipeline = false, std::uint32_t projector_op = 0,
                           bool test_recon_shm_loopback = false,
                           bool test_inline_image_reply = false) = 0;
  // 异步采集：有串口时 Hub 发投采指令后立即返回 job_id，帧就绪后由 GetDepthFrame 等拉取。
  virtual bool CaptureAsync(std::uint64_t* out_job_id, std::uint64_t client_capture_id = 0,
                            bool with_detection_pipeline = false,
                            bool with_reconstruction_pipeline = false, std::uint32_t projector_op = 0,
                            bool test_recon_shm_loopback = false,
                            bool test_inline_image_reply = false) = 0;

  // 读取最近一次 CaptureReply 回传的内联图片（若无则返回 false）。
  virtual bool GetLastCaptureInlineImage(std::string* out_name, std::vector<std::uint8_t>* out_payload) const = 0;

  /// 批量设置相机参数（当前支持 ParameterType：2D 曝光/增益/伽马，与 Hub SetParameters 对齐）。
  virtual bool SetParameters(const std::vector<camera3d::best::ParameterValue>& params) = 0;
  /// 按类型批量读取；成功时 out_values 与 types 顺序一致。
  virtual bool GetParameters(const std::vector<camera3d::best::ParameterType>& types,
                             std::vector<camera3d::best::ParameterValue>* out_values) = 0;

  // 深度（含多相机 raw 元数据）：返回 true 时填充 SHM 区名与槽位信息，由调用方 mmap 后读 payload。
  // 后四个可选输出与 proto ShmFrameRef 对齐（为 nullptr 时不填充）。
  // query_client_capture_id：非 0 时查询指定采集；0 表示沿用最近一次 Capture 返回的 ID（与 Hub DepthRequest 一致）。
  virtual bool GetDepthFrame(std::string* out_shm_region, std::uint64_t* out_seq, std::uint64_t* out_offset,
                             std::uint64_t* out_size, std::uint32_t* out_width = nullptr,
                             std::uint32_t* out_height = nullptr, std::uint32_t* out_pixel_format = nullptr,
                             std::int64_t* out_timestamp_unix_ns = nullptr,
                             std::uint64_t query_client_capture_id = 0) = 0;
  /// 列出 GetDepth 返回的多路×多帧原始 SHM 引用（与 Hub DepthReply.camera_raw_frames 一致）；out 非空时先 clear。
  virtual bool ListDepthCameraRawFrames(std::vector<camera3d::best::BestCameraRawFrameItem>* out,
                                        std::uint64_t query_client_capture_id = 0) = 0;
  // 点云：Hub 侧多为占位未实现，成功语义以服务端返回为准。
  virtual bool GetPointCloud(std::string* out_shm_region, std::uint64_t* out_seq, std::uint64_t* out_offset,
                             std::uint64_t* out_size, std::uint32_t* out_width = nullptr,
                             std::uint32_t* out_height = nullptr, std::uint32_t* out_pixel_format = nullptr,
                             std::int64_t* out_timestamp_unix_ns = nullptr) = 0;
  // 检测：Hub 不提供 SHM 结果时失败；后续由重建服务统一产出时可再对接协议。
  virtual bool GetDetectionResult(std::string* out_shm_region, std::uint64_t* out_seq, std::uint64_t* out_offset,
                                  std::uint64_t* out_size, std::uint32_t* out_width = nullptr,
                                  std::uint32_t* out_height = nullptr, std::uint32_t* out_pixel_format = nullptr,
                                  std::int64_t* out_timestamp_unix_ns = nullptr) = 0;

  /// 不依赖 Connect：在 timeout_ms 内监听 UDP 广播（listen_udp_port==0 时使用与 Hub 约定默认端口）。
  /// 成功返回 true（无设备时 out 为空）；套接字失败返回 false。
  virtual bool DiscoverDevices(std::vector<DiscoveredHubDevice>* out, std::uint16_t listen_udp_port = 0,
                               int timeout_ms = 2000) = 0;
};

// 工厂：与 CreateDeveloperCameraSdk 在完整构建中通常指向同一 gRPC 实现堆对象。
CAMERA_SDK_API IUserCameraSdk* CreateUserCameraSdk();
CAMERA_SDK_API void DestroyUserCameraSdk(IUserCameraSdk* ptr);

}  // namespace camera3d::sdk
