#include "capture_orchestrator/multi_hard_trigger_orchestrator.h"

#include "platform_diag/logging.h"

namespace camera3d::capture {

// 实现 MultiHardTriggerOrchestrator::MultiHardTriggerOrchestrator。
MultiHardTriggerOrchestrator::MultiHardTriggerOrchestrator(camera::CameraManager& cameras,
                                                         serial::ISerialPort& serial_port,
                                                         std::vector<std::string> manager_device_ids)
    : cameras_(&cameras), serial_(&serial_port), mids_(std::move(manager_device_ids)) {}

// 实现 MultiHardTriggerOrchestrator::SendProductionCommandOnly：单次 SendProductionCommand。
bool MultiHardTriggerOrchestrator::SendProductionCommandOnly(serial::ProductionCommand cmd,
                                                             int projector_ack_timeout_ms,
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
    CAMERA3D_LOGE("MultiHardTriggerOrchestrator(SendOnly): {}", out_error);
    return false;
  }
  return true;
}

}  // namespace camera3d::capture
