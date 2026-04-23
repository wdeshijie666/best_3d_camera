#pragma once

// 串口传输最小抽象，便于单测与 Win32 实现注入（见 CreateSerialPortWin32）。

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace camera3d::serial {

// 串口抽象：一体机光源/投采指令下发；帧格式与 geelydetectapp 串口协议对齐后填入 ProjectorCommandBuilder
class ISerialPort {
 public:
  virtual ~ISerialPort() = default;

  // port_index：Windows COM 序号（3 表示 COM3）；baud_rate 如 115200。
  virtual bool Open(std::uint32_t port_index, unsigned baud_rate) = 0;
  virtual void Close() = 0;
  virtual bool IsOpen() const = 0;
  // 人类可读端口名，如 "COM3"。
  virtual std::string CurrentPortName() const = 0;

  // 原始字节写读；ReadBytes 可阻塞至 timeout_ms。
  virtual bool WriteBytes(const std::uint8_t* data, std::size_t len) = 0;
  virtual bool ReadBytes(std::vector<std::uint8_t>& out, std::size_t max_len, int timeout_ms) = 0;

  virtual std::string GetLastErrorMessage() const = 0;
};

std::unique_ptr<ISerialPort> CreateSerialPortWin32();

}  // namespace camera3d::serial
