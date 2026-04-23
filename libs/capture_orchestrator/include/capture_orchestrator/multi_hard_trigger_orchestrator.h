#pragma once

// 多相机并行投采：保留多路场景下单次串口触发能力。

#include "camera_driver/camera_manager.h"
#include "camera_driver/camera_types.h"
#include "serial_port/iserial_port.h"
#include "serial_port/serial_projector.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace camera3d::capture {

// 多相机场景下串口仍只发一次；回调类型保留供扩展。
using MultiFrameCallback = std::function<void(const std::string& manager_device_id, const camera::FrameBuffer&)>;

class MultiHardTriggerOrchestrator {
 public:
  MultiHardTriggerOrchestrator(camera::CameraManager& cameras, serial::ISerialPort& serial_port,
                             std::vector<std::string> manager_device_ids);

  // 多路已异步就绪后，仅发一次串口产线命令。
  bool SendProductionCommandOnly(serial::ProductionCommand cmd, int projector_ack_timeout_ms,
                                  std::string& out_error);

 private:
  camera::CameraManager* cameras_ = nullptr;
  serial::ISerialPort* serial_ = nullptr;
  std::vector<std::string> mids_;
};

}  // namespace camera3d::capture
