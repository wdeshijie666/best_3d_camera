#include "capture_orchestrator/hard_trigger_orchestrator.h"

#include "platform_diag/logging.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <utility>

namespace camera3d::capture {
namespace {

void WaitForCallbackFrames(std::mutex& mu, std::condition_variable& cv,
                           const std::vector<camera::FrameBuffer>& acc, std::size_t max_frames,
                           int wait_timeout_ms) {
  std::unique_lock<std::mutex> lk(mu);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_timeout_ms);
  for (;;) {
    if (max_frames > 0 && acc.size() >= max_frames) {
      return;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      return;
    }
    cv.wait_until(lk, deadline, [&] { return (max_frames > 0 && acc.size() >= max_frames); });
  }
}

template <class SendFn>
bool RunCallbackCapture(camera::CameraManager& cameras, const std::string& manager_device_id,
                        serial::ISerialPort& serial, SendFn&& send_fn,
                        std::vector<camera::FrameBuffer>& out_frames, std::size_t max_frames,
                        int wait_timeout_ms, std::string& out_error) {
  out_frames.clear();
  out_error.clear();

  if (!serial.IsOpen()) {
    out_error = "serial not open";
    return false;
  }

  cameras.StopAsyncGrab(manager_device_id);

  if (!cameras.SetTriggerMode(manager_device_id, camera::TriggerMode::kHardware)) {
    out_error = "SetTriggerMode hardware failed: " + cameras.GetLastErrorMessage(manager_device_id);
    CAMERA3D_LOGE("HardTriggerOrchestrator: {}", out_error);
    return false;
  }

  std::mutex mu;
  std::condition_variable cv;
  std::vector<camera::FrameBuffer> acc;

  if (!cameras.StartAsyncGrab(manager_device_id, [&](const camera::FrameBuffer& fb) {
        std::lock_guard<std::mutex> lk(mu);
        acc.push_back(fb);
        cv.notify_all();
      })) {
    out_error = "StartAsyncGrab failed: " + cameras.GetLastErrorMessage(manager_device_id);
    CAMERA3D_LOGE("HardTriggerOrchestrator: {}", out_error);
    return false;
  }

  if (!send_fn()) {
    cameras.StopAsyncGrab(manager_device_id);
    return false;
  }

  WaitForCallbackFrames(mu, cv, acc, max_frames, wait_timeout_ms);

  {
    std::lock_guard<std::mutex> lk(mu);
    out_frames = acc;
  }
  cameras.StopAsyncGrab(manager_device_id);

  if (out_frames.empty()) {
    out_error = "no frame in callback within timeout";
    CAMERA3D_LOGW("HardTriggerOrchestrator: {}", out_error);
    return false;
  }
  return true;
}

}  // namespace

// 实现 HardTriggerOrchestrator::HardTriggerOrchestrator：保存三方引用与设备 ID。
HardTriggerOrchestrator::HardTriggerOrchestrator(camera::CameraManager& cameras, serial::ISerialPort& serial_port,
                                                std::string manager_device_id)
    : cameras_(&cameras), serial_(&serial_port), managed_device_id_(std::move(manager_device_id)) {}

// 实现 HardTriggerOrchestrator::FireProjectorAndCollectViaCallback：RunCallbackCapture + WriteBytes。
bool HardTriggerOrchestrator::FireProjectorAndCollectViaCallback(const std::vector<std::uint8_t>& projector_payload,
                                                                 std::vector<camera::FrameBuffer>& out_frames,
                                                                 std::size_t max_frames, int wait_timeout_ms,
                                                                 std::string& out_error) {
  if (!cameras_ || !serial_) {
    out_error = "null cameras or serial";
    return false;
  }
  if (managed_device_id_.empty()) {
    out_error = "empty manager_device_id";
    return false;
  }
  return RunCallbackCapture(
      *cameras_, managed_device_id_, *serial_,
      [&]() -> bool {
        if (!serial_->WriteBytes(projector_payload.data(), projector_payload.size())) {
          out_error = "serial write failed: " + serial_->GetLastErrorMessage();
          CAMERA3D_LOGE("HardTriggerOrchestrator: {}", out_error);
          return false;
        }
        return true;
      },
      out_frames, max_frames, wait_timeout_ms, out_error);
}

// 实现 HardTriggerOrchestrator::FireProductionAndCollectViaCallback：RunCallbackCapture + SendProductionCommand。
bool HardTriggerOrchestrator::FireProductionAndCollectViaCallback(
    serial::ProductionCommand cmd, int projector_ack_timeout_ms,
    std::vector<camera::FrameBuffer>& out_frames, std::size_t max_frames, int wait_timeout_ms,
    std::string& out_error) {
  if (!cameras_ || !serial_) {
    out_error = "null cameras or serial";
    return false;
  }
  if (managed_device_id_.empty()) {
    out_error = "empty manager_device_id";
    return false;
  }
  return RunCallbackCapture(
      *cameras_, managed_device_id_, *serial_,
      [&]() -> bool {
        auto pr = serial::SendProductionCommand(*serial_, cmd, projector_ack_timeout_ms);
        if (!pr.ok) {
          out_error = std::move(pr.message);
          CAMERA3D_LOGE("HardTriggerOrchestrator: projector {}", out_error);
          return false;
        }
        return true;
      },
      out_frames, max_frames, wait_timeout_ms, out_error);
}

// 实现 HardTriggerOrchestrator::FireProjectorAndGrab：Collect 收 1 帧取首元素。
bool HardTriggerOrchestrator::FireProjectorAndGrab(const std::vector<std::uint8_t>& projector_payload,
                                                   camera::FrameBuffer& out_frame, int grab_timeout_ms,
                                                   std::string& out_error) {
  std::vector<camera::FrameBuffer> batch;
  if (!FireProjectorAndCollectViaCallback(projector_payload, batch, 1, grab_timeout_ms, out_error)) {
    return false;
  }
  out_frame = std::move(batch.front());
  return true;
}

// 实现 HardTriggerOrchestrator::FireProductionAndGrab：Collect 收 1 帧取首元素。
bool HardTriggerOrchestrator::FireProductionAndGrab(serial::ProductionCommand cmd,
                                                    camera::FrameBuffer& out_frame,
                                                    int projector_ack_timeout_ms, int grab_timeout_ms,
                                                    std::string& out_error) {
  std::vector<camera::FrameBuffer> batch;
  if (!FireProductionAndCollectViaCallback(cmd, projector_ack_timeout_ms, batch, 1, grab_timeout_ms,
                                           out_error)) {
    return false;
  }
  out_frame = std::move(batch.front());
  return true;
}

// 实现 HardTriggerOrchestrator::SendProductionCommandOnly：仅串口 SendProductionCommand。
bool HardTriggerOrchestrator::SendProductionCommandOnly(serial::ProductionCommand cmd, int projector_ack_timeout_ms,
                                                         std::string& out_error) {
  out_error.clear();
  if (!serial_) {
    out_error = "null serial";
    return false;
  }
  if (!serial_->IsOpen()) {
    out_error = "serial not open";
    return false;
  }
  auto pr = serial::SendProductionCommand(*serial_, cmd, projector_ack_timeout_ms);
  if (!pr.ok) {
    out_error = std::move(pr.message);
    CAMERA3D_LOGE("HardTriggerOrchestrator(SendOnly): {}", out_error);
    return false;
  }
  return true;
}

}  // namespace camera3d::capture
