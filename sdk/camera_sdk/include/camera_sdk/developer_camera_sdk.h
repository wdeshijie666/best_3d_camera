#pragma once

// 在 IUserCameraSdk 之上暴露会话/对端/日志级别等诊断信息；CreateDeveloperCameraSdk 为应用推荐入口。

#include "camera_sdk/user_camera_sdk.h"

#include <string>

namespace camera3d::sdk {

// 开发者版：继承用户接口并暴露诊断与扩展钩子（可继续派生自定义子类）。
// Hub 业务码与建议动作：camera3d/hub/hub_service_state_codes.h、camera_sdk/hub_client_action.h。
class CAMERA_SDK_API IDeveloperCameraSdk : public IUserCameraSdk {
 public:
  ~IDeveloperCameraSdk() override;

  // 最近一次 Connect 使用的 gRPC target 字符串。
  virtual std::string LastRpcPeer() const = 0;
  // Hub 分配的 session_id，未连接时为空。
  virtual std::string SessionId() const = 0;
  // 与 build_info 对齐的栈版本号字符串。
  virtual std::string GetSdkVersion() const = 0;
  // 映射到 spdlog::default_logger 级别（trace/debug/info/warn/error/critical）。
  virtual void SetDiagnosticLogLevel(const std::string& level) = 0;
};

CAMERA_SDK_API IDeveloperCameraSdk* CreateDeveloperCameraSdk();
CAMERA_SDK_API void DestroyDeveloperCameraSdk(IDeveloperCameraSdk* ptr);

}  // namespace camera3d::sdk
