#pragma once

// Hub 进程内线程安全快照：Connect/Capture 等 RPC 在未就绪时返回当前 code/message。

#include <camera3d/hub/hub_service_state_codes.h>

#include <cstdint>
#include <mutex>
#include <string>

namespace camera3d::hub {

class HubServiceRuntimeState {
 public:
  // 覆盖当前运行时状态（如启动失败、串口丢失、就绪）。
  void Set(std::int32_t code, std::string message);
  void Snapshot(std::int32_t* code, std::string* message) const;
  // 等价于 code == HubServiceStateCode::kReady。
  bool IsReady() const;

 private:
  mutable std::mutex mu_;
  std::int32_t code_ = HubServiceStateCode::kStarting;
  std::string message_{"hub starting"};
};

}  // namespace camera3d::hub
