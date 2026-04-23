#pragma once

// Hub 事件回调类型（当前实现多为占位，RegisterEventCallback 可返回 kNotSupported）。

#include <cstdint>
#include <string>

namespace camera3d::best {

enum class BestEventType {
  kUnknown = 0,
  kConnected = 1,
  kDisconnected = 2,
  kCaptureDone = 3,
  kStreamError = 99,
};

struct BestEventContext {
  BestEventType type = BestEventType::kUnknown;
  int error_code = 0;
  std::string message;
  std::uint64_t job_id = 0;
};

using BestUserContext = void*;
using BestEventCallback = void (*)(BestEventType type, const BestEventContext& ctx, BestUserContext user);

}  // namespace camera3d::best
