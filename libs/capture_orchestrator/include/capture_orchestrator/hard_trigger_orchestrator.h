#pragma once

// 单相机投采编排：保留串口 Production 命令发送能力（Hub 主路径）。

#include "camera_driver/camera_manager.h"
#include "camera_driver/camera_types.h"
#include "serial_port/iserial_port.h"
#include "serial_port/serial_projector.h"

#include <cstdint>
#include <string>
#include <vector>

namespace camera3d::capture {

// 投采编排：提供串口触发与同步/回调采集能力；Hub 主链路当前仅使用 SendProductionCommandOnly。
class HardTriggerOrchestrator {
 public:
  HardTriggerOrchestrator(camera::CameraManager& cameras, serial::ISerialPort& serial_port,
                          std::string manager_device_id);

  // 自定义原始帧下发 + 回调收多帧（内部设硬触发与 StartAsyncGrab）。
  bool FireProjectorAndCollectViaCallback(const std::vector<std::uint8_t>& projector_payload,
                                          std::vector<camera::FrameBuffer>& out_frames,
                                          std::size_t max_frames, int wait_timeout_ms,                                           std::string& out_error);

  // 发产线命令 + 回调收多帧。
  bool FireProductionAndCollectViaCallback(serial::ProductionCommand cmd, int projector_ack_timeout_ms,
                                           std::vector<camera::FrameBuffer>& out_frames,
                                           std::size_t max_frames, int wait_timeout_ms,
                                           std::string& out_error);

  // 同步路径：发自定义帧后 GrabOne 收单帧。
  bool FireProjectorAndGrab(const std::vector<std::uint8_t>& projector_payload, camera::FrameBuffer& out_frame,
                            int grab_timeout_ms, std::string& out_error);

  // 同步路径：发产线命令后 GrabOne。
  bool FireProductionAndGrab(serial::ProductionCommand cmd, camera::FrameBuffer& out_frame,
                             int projector_ack_timeout_ms,                              int grab_timeout_ms, std::string& out_error);

  // 相机已处于硬触发+异步回调时，仅通过串口触发一体机（不再改触发模式、不 Stop/Restart AsyncGrab）
  bool SendProductionCommandOnly(serial::ProductionCommand cmd, int projector_ack_timeout_ms, std::string& out_error);

 private:
  camera::CameraManager* cameras_ = nullptr;
  serial::ISerialPort* serial_ = nullptr;
  std::string managed_device_id_;
};

}  // namespace camera3d::capture
