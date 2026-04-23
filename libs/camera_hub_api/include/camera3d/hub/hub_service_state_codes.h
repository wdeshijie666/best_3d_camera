#pragma once

// Hub 与 SDK 共用的 reply.status.code 数值与中文简述；含义以本头为准，禁止魔数散落。

#include <cstdint>

namespace camera3d::hub {

// Hub 与上层 SDK 共用的业务状态码（各 RPC 的 reply.status.code，gRPC 仍为 OK）。
// 含义以本头文件为准，Hub 与 SDK 应包含同一路径，避免硬编码魔数。
struct HubServiceStateCode {
  static constexpr std::int32_t kReady = 0;
  static constexpr std::int32_t kStarting = 1001;
  /// 配置文件内容无效或解析失败（详见 status.message）
  static constexpr std::int32_t kConfigInvalid = 1002;
  static constexpr std::int32_t kCameraInitFailed = 1003;
  static constexpr std::int32_t kSerialInitFailed = 1004;
  static constexpr std::int32_t kShmInitFailed = 1005;
  static constexpr std::int32_t kOrchestratorInitFailed = 1006;
  /// 配置的 Hub JSON 路径不存在或不可读
  static constexpr std::int32_t kConfigFileNotFound = 1007;
  /// 启动完成后串口丢失或重连失败（后台监控线程会更新）
  static constexpr std::int32_t kRuntimeSerialNotConnected = 1010;
  /// 基础设施就绪但会话未建立（如 Disconnect 后需再次 Connect 恢复采集编排）
  static constexpr std::int32_t kSessionNotEstablished = 1011;
};

// 简短中文说明，便于 SDK 弹窗/日志（message 仍以服务端返回为准）。
inline const char* HubServiceStateDescribeZh(std::int32_t code) {
  switch (code) {
    case HubServiceStateCode::kReady:
      return "就绪";
    case HubServiceStateCode::kStarting:
      return "启动中";
    case HubServiceStateCode::kConfigInvalid:
      return "配置无效或解析失败";
    case HubServiceStateCode::kCameraInitFailed:
      return "相机初始化失败";
    case HubServiceStateCode::kSerialInitFailed:
      return "串口初始化失败";
    case HubServiceStateCode::kShmInitFailed:
      return "共享内存初始化失败";
    case HubServiceStateCode::kOrchestratorInitFailed:
      return "硬触发编排初始化失败";
    case HubServiceStateCode::kConfigFileNotFound:
      return "配置文件不存在";
    case HubServiceStateCode::kRuntimeSerialNotConnected:
      return "串口未连接";
    case HubServiceStateCode::kSessionNotEstablished:
      return "会话未建立，请先 Connect";
    default:
      return "未知 Hub 状态码";
  }
}

}  // namespace camera3d::hub
