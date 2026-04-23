#pragma once

// Hub reply.status.code -> SDK 建议动作（日志/弹窗分支）；与 Hub 共用 camera3d/hub 头中的数值定义。

#include <camera3d/hub/hub_service_state_codes.h>

#include <cstdint>

namespace camera3d::sdk {

/// SDK 收到 Hub reply.status.code 后建议的下一步（与 Hub 共用数值定义见
/// camera3d/hub/hub_service_state_codes.h）。
enum class HubClientAction {
  kNone = 0,
  /// 1001：Hub 仍在启动，稍后重试 Connect
  kRetryConnectLater,
  /// 1002 / 1007：检查 JSON 路径、内容与 projector.com_index 等
  kFixHubConfiguration,
  /// 1003：检查相机上电、网线、backend/device_id 与现场一致
  kFixCameraSetup,
  /// 1004 / 1010：检查 COM 号、USB/串口线、独占占用
  kFixSerialConnection,
  /// 1005：检查本机 SHM 权限或与 Hub 同机部署
  kRetryOrCheckShmService,
  /// 1006：在 Hub 日志中查编排失败原因后重试 Connect 或重启 Hub
  kRetryConnectOrRestartHub,
  /// 1011：已 Disconnect，需再次 Connect（无需改配置）
  kCallConnect,
};

inline HubClientAction RecommendedActionForHubStatus(std::int32_t code) {
  using C = camera3d::hub::HubServiceStateCode;
  if (code == C::kReady) {
    return HubClientAction::kNone;
  }
  switch (code) {
    case C::kStarting:
      return HubClientAction::kRetryConnectLater;
    case C::kConfigInvalid:
    case C::kConfigFileNotFound:
      return HubClientAction::kFixHubConfiguration;
    case C::kCameraInitFailed:
      return HubClientAction::kFixCameraSetup;
    case C::kSerialInitFailed:
    case C::kRuntimeSerialNotConnected:
      return HubClientAction::kFixSerialConnection;
    case C::kShmInitFailed:
      return HubClientAction::kRetryOrCheckShmService;
    case C::kOrchestratorInitFailed:
      return HubClientAction::kRetryConnectOrRestartHub;
    case C::kSessionNotEstablished:
      return HubClientAction::kCallConnect;
    default:
      return HubClientAction::kFixHubConfiguration;
  }
}

inline const char* HubClientActionDescribeZh(HubClientAction a) {
  switch (a) {
    case HubClientAction::kNone:
      return "无额外操作";
    case HubClientAction::kRetryConnectLater:
      return "稍后重试 Connect（Hub 启动中）";
    case HubClientAction::kFixHubConfiguration:
      return "修正 Hub 配置文件或路径后重试 Connect";
    case HubClientAction::kFixCameraSetup:
      return "检查相机与配置中的 backend/设备标识后重启 Hub 并重连";
    case HubClientAction::kFixSerialConnection:
      return "检查串口 COM、线缆与独占占用，恢复后重试 Connect";
    case HubClientAction::kRetryOrCheckShmService:
      return "检查共享内存/同机部署或权限后重试";
    case HubClientAction::kRetryConnectOrRestartHub:
      return "查看 Hub 日志中编排失败原因，修正后重试 Connect 或重启 Hub";
    case HubClientAction::kCallConnect:
      return "再次调用 Connect 恢复会话（Disconnect 后）";
    default:
      return "未知建议动作";
  }
}

/// 是否适合由应用做短延迟自动重试 Connect（不改变配置）。
inline bool HubStatusSuggestAutoRetryConnect(std::int32_t code) {
  using C = camera3d::hub::HubServiceStateCode;
  return code == C::kStarting || code == C::kRuntimeSerialNotConnected;
}

}  // namespace camera3d::sdk
