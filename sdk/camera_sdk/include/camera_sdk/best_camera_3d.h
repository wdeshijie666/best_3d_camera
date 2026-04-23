#pragma once

// 应用首选封装：组合 IDeveloperCameraSdk，将 Hub 错误映射为 BestStatus，并提供 BestShmFrameRef 等值类型 API。

#include "camera_sdk/best_device_info.h"
#include "camera_sdk/best_event.h"
#include "camera_sdk/best_types.h"
#include "camera_sdk/export.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace camera3d::best {

/// 面向应用的标准 3D 相机访问入口；内部组合现有 gRPC 版 IUserCameraSdk / IDeveloperCameraSdk。
class CAMERA_SDK_API BestCamera3D {
 public:
  BestCamera3D();
  ~BestCamera3D();

  BestCamera3D(const BestCamera3D&) = delete;
  BestCamera3D& operator=(const BestCamera3D&) = delete;
  BestCamera3D(BestCamera3D&&) noexcept;
  BestCamera3D& operator=(BestCamera3D&&) noexcept;

  static std::string SdkVersion();

  /// 局域网 UDP 广播发现 Hub（无需已连接实例）；listen_udp_port 为 0 时使用与 Hub 约定默认端口。
  static BestStatus DiscoverDevices(std::vector<BestDeviceInfo>& out, std::uint16_t listen_udp_port = 0,
                                    int timeout_ms = 2000);

  /// 仅 Hub：参数为 "ip" 或 "host:port"；设备地址使用 DefaultSimulator().device_address
  BestStatus Connect(const std::string& hub_ip_or_hostport, unsigned timeout_ms = 10000);

  /// 使用完整设备信息连接（推荐）
  BestStatus Connect(const BestDeviceInfo& device, unsigned timeout_ms = 10000);

  // 断开 Hub 会话；成功返回 kSuccess。
  BestStatus Disconnect();
  bool IsConnected() const;

  BestStatus SetDiagnosticLogLevel(BestLogLevel level);
  BestStatus SetDiagnosticLogLevel(const std::string& level);

  // 同步/异步采集；参数语义与 IUserCameraSdk::Capture* 一致。
  BestStatus CaptureSync(std::uint64_t* out_job_id = nullptr, bool with_detection_pipeline = false,
                         bool with_reconstruction_pipeline = false, std::uint32_t projector_op = 0,
                         bool test_recon_shm_loopback = false, bool test_inline_image_reply = false);
  BestStatus CaptureAsync(std::uint64_t* out_job_id = nullptr, bool with_detection_pipeline = false,
                          bool with_reconstruction_pipeline = false, std::uint32_t projector_op = 0,
                          bool test_recon_shm_loopback = false, bool test_inline_image_reply = false);

  /// 统一参数写入（与 ParameterType 一致；当前开放 2D 曝光/增益/伽马）。
  BestStatus SetParameters(const std::vector<ParameterValue>& params);
  BestStatus GetParameters(const std::vector<ParameterType>& types, std::vector<ParameterValue>* out_values);

  // 查询某次采集的深度帧 SHM 引用；client_capture_id 为 0 时使用最近一次采集 ID。
  BestStatus QueryDepthFrame(BestShmFrameRef& out, std::uint64_t client_capture_id = 0);
  /// 查询 GetDepth 返回的多路×多帧原始 SHM 元数据（单次硬触发 burst）；空列表表示 Hub 未填 camera_raw_frames。
  BestStatus QueryDepthCameraRawFrames(std::vector<BestCameraRawFrameItem>& out,
                                       std::uint64_t client_capture_id = 0);
  BestStatus GetLastCaptureInlineImage(BestInlineImage& out);
  BestStatus QueryPointCloud(BestShmFrameRef& out);
  BestStatus QueryDetectionResult(BestShmFrameRef& out);

  BestStatus SetROI(const BestROI& roi);
  BestStatus GetROI(BestROI& roi);

  BestStatus SetConfig(BestConfigType type, const std::vector<int>& values);
  BestStatus GetConfig(BestConfigType type, std::vector<int>& out_values);

  /// 当前版本无 Hub 事件推送，注册始终返回 kNotSupported
  BestStatus RegisterEventCallback(BestEventCallback cb, BestUserContext user);
  void UnregisterEventCallback();

  // 最近一次 SDK 调用的综合错误码（含 gRPC）；细分为 Hub 业务码见 LastHubStatusCode。
  int LastErrorCode() const;
  std::string LastErrorMessage() const;
  /// 最近一次 Hub reply.status.code；与 camera3d/hub/hub_service_state_codes.h 对齐。
  int LastHubStatusCode() const;
  // 当前 gRPC 对端与 Hub 会话 ID（未连接为空字符串）。
  std::string RpcPeer() const;
  std::string SessionId() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace camera3d::best
