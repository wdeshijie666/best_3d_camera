#pragma once

#include "serial_port/serial_projector.h"

#include <cstdint>
#include <vector>

namespace camera3d::serial {

class ISerialPort;

// 一体机下行完整帧（固定 6 字节，无额外 opcode 前缀）
struct ProjectorRawFrame {
  std::vector<std::uint8_t> bytes;
};

class ProjectorCommandBuilder {
 public:
  // 默认投采：与 Production(0) 相同（白屏起到最后一张）
  static ProjectorRawFrame BuildPlaceholderFireCapture();

  static ProjectorRawFrame FromProductionCommand(ProductionCommand cmd);
  static ProjectorRawFrame FromSetCommand(SetCommand cmd);

  static std::vector<std::uint8_t> Serialize(const ProjectorRawFrame& frame);
};

// 仅写串口，不等待屏幕应答；需要握手请用 SendProductionCommand
bool SendProjectorFrame(ISerialPort& port, const ProjectorRawFrame& frame);

}  // namespace camera3d::serial
