#include "hub_service_state.h"

// HubServiceRuntimeState：互斥保护下的 code/message 快照，供 gRPC 线程读取。

namespace camera3d::hub {

// 实现 HubServiceRuntimeState::Set：覆盖 code/message。
void HubServiceRuntimeState::Set(std::int32_t code, std::string message) {
  std::scoped_lock lock(mu_);
  code_ = code;
  message_ = std::move(message);
}

// 实现 HubServiceRuntimeState::Snapshot：拷贝当前状态到输出指针（可 nullptr）。
void HubServiceRuntimeState::Snapshot(std::int32_t* code, std::string* message) const {
  std::scoped_lock lock(mu_);
  if (code) *code = code_;
  if (message) *message = message_;
}

// 实现 HubServiceRuntimeState::IsReady：code_ == kReady。
bool HubServiceRuntimeState::IsReady() const {
  std::scoped_lock lock(mu_);
  return code_ == HubServiceStateCode::kReady;
}

}  // namespace camera3d::hub
