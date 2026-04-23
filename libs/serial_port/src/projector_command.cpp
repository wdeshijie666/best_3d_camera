#include "serial_port/projector_command.h"
#include "serial_port/iserial_port.h"

#include "platform_diag/logging.h"

namespace camera3d::serial {

ProjectorRawFrame ProjectorCommandBuilder::BuildPlaceholderFireCapture() {
  return FromProductionCommand(ProductionCommand::kWhiteScreenToEnd);
}

ProjectorRawFrame ProjectorCommandBuilder::FromProductionCommand(ProductionCommand cmd) {
  const auto w = ProductionFrame(cmd);
  ProjectorRawFrame f;
  f.bytes.assign(w.begin(), w.end());
  return f;
}

ProjectorRawFrame ProjectorCommandBuilder::FromSetCommand(SetCommand cmd) {
  const auto w = SetFrame(cmd);
  ProjectorRawFrame f;
  f.bytes.assign(w.begin(), w.end());
  return f;
}

std::vector<std::uint8_t> ProjectorCommandBuilder::Serialize(const ProjectorRawFrame& frame) {
  return frame.bytes;
}

bool SendProjectorFrame(ISerialPort& port, const ProjectorRawFrame& frame) {
  if (!port.IsOpen()) {
    CAMERA3D_LOGE("SendProjectorFrame: serial not open");
    return false;
  }
  if (frame.bytes.empty()) {
    CAMERA3D_LOGE("SendProjectorFrame: empty frame");
    return false;
  }
  return port.WriteBytes(frame.bytes.data(), frame.bytes.size());
}

}  // namespace camera3d::serial
