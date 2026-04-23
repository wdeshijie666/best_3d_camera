#include "serial_port/serial_port_manager.h"

#include "platform_diag/logging.h"

namespace camera3d::serial {

// 实现 SerialPortManager::Instance：Meyers 单例。
SerialPortManager& SerialPortManager::Instance() {
  static SerialPortManager inst;
  return inst;
}

// 实现 SerialPortManager::SerialPortManager：创建 Win32 串口实现。
SerialPortManager::SerialPortManager() : port_(CreateSerialPortWin32()) {}

// 实现 SerialPortManager::PortRef：加锁返回 *port_。
ISerialPort& SerialPortManager::PortRef() {
  std::lock_guard<std::mutex> lock(mu_);
  return *port_;
}

// 实现 SerialPortManager::Open：加锁转调 port_->Open。
bool SerialPortManager::Open(std::uint32_t com_index, unsigned baud_rate) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!port_) {
    last_error_ = "serial implementation is null";
    return false;
  }
  if (!port_->Open(com_index, baud_rate)) {
    last_error_ = port_->GetLastErrorMessage();
    return false;
  }
  last_error_.clear();
  return true;
}

// 实现 SerialPortManager::Close：加锁 Close。
void SerialPortManager::Close() {
  std::lock_guard<std::mutex> lock(mu_);
  if (port_) {
    port_->Close();
  }
}

// 实现 SerialPortManager::IsOpen。
bool SerialPortManager::IsOpen() const {
  std::lock_guard<std::mutex> lock(mu_);
  return port_ && port_->IsOpen();
}

// 实现 SerialPortManager::CurrentPortName。
std::string SerialPortManager::CurrentPortName() const {
  std::lock_guard<std::mutex> lock(mu_);
  return port_ ? port_->CurrentPortName() : std::string{};
}

// 实现 SerialPortManager::GetLastErrorMessage：优先本层缓存。
std::string SerialPortManager::GetLastErrorMessage() const {
  std::lock_guard<std::mutex> lock(mu_);
  if (!last_error_.empty()) {
    return last_error_;
  }
  return port_ ? port_->GetLastErrorMessage() : std::string{"serial implementation is null"};
}

// 实现 SerialPortManager::WriteBytes。
bool SerialPortManager::WriteBytes(const std::uint8_t* data, std::size_t len) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!port_ || !port_->IsOpen()) {
    last_error_ = "serial not open";
    return false;
  }
  if (!port_->WriteBytes(data, len)) {
    last_error_ = port_->GetLastErrorMessage();
    return false;
  }
  last_error_.clear();
  return true;
}

// 实现 SerialPortManager::ReadBytes。
bool SerialPortManager::ReadBytes(std::vector<std::uint8_t>& out, std::size_t max_len, int timeout_ms) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!port_ || !port_->IsOpen()) {
    last_error_ = "serial not open";
    return false;
  }
  if (!port_->ReadBytes(out, max_len, timeout_ms)) {
    last_error_ = port_->GetLastErrorMessage();
    return false;
  }
  last_error_.clear();
  return true;
}

// 实现 SerialPortManager::SendFrame：SendProjectorFrame 封装。
bool SerialPortManager::SendFrame(const ProjectorRawFrame& frame) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!port_ || !port_->IsOpen()) {
    last_error_ = "serial not open";
    return false;
  }
  if (!SendProjectorFrame(*port_, frame)) {
    last_error_ = port_->GetLastErrorMessage();
    return false;
  }
  last_error_.clear();
  return true;
}

// 实现 SerialPortManager::SendProduction：SendProductionCommand 封装。
ProjectorResult SerialPortManager::SendProduction(ProductionCommand cmd, int read_timeout_ms) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!port_ || !port_->IsOpen()) {
    return {false, "serial not open"};
  }
  auto r = SendProductionCommand(*port_, cmd, read_timeout_ms);
  if (!r.ok) {
    last_error_ = r.message;
  } else {
    last_error_.clear();
  }
  return r;
}

// 实现 SerialPortManager::SendSet：SendSetCommand 封装。
ProjectorResult SerialPortManager::SendSet(SetCommand cmd, int read_timeout_ms) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!port_ || !port_->IsOpen()) {
    return {false, "serial not open"};
  }
  auto r = SendSetCommand(*port_, cmd, read_timeout_ms);
  if (!r.ok) {
    last_error_ = r.message;
  } else {
    last_error_.clear();
  }
  return r;
}

}  // namespace camera3d::serial
