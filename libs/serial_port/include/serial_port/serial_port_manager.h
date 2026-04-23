#pragma once

// 进程内单例串口：投影仪/一体机指令发送与应答读取，供 capture_orchestrator 与 Hub 统一启动打开 COM。

#include "serial_port/iserial_port.h"
#include "serial_port/projector_command.h"
#include "serial_port/serial_projector.h"

#include <memory>
#include <mutex>
#include <string>

namespace camera3d::serial {

// 串口与投影仪指令单例管理器（参考 geely 串口类的集中管理方式）。
class SerialPortManager {
 public:
  static SerialPortManager& Instance();

  // 打开 COM；失败写入 last_error_。
  bool Open(std::uint32_t com_index, unsigned baud_rate = 115200);
  void Close();
  bool IsOpen() const;
  std::string CurrentPortName() const;
  // 合并本层与 port_ 的最后错误信息。
  std::string GetLastErrorMessage() const;

  bool WriteBytes(const std::uint8_t* data, std::size_t len);
  bool ReadBytes(std::vector<std::uint8_t>& out, std::size_t max_len, int timeout_ms);

  // 发送一帧原始投影仪数据（见 serial_projector）。
  bool SendFrame(const ProjectorRawFrame& frame);
  // 发送产线流程命令并读应答帧。
  ProjectorResult SendProduction(ProductionCommand cmd, int read_timeout_ms = 3000);
  ProjectorResult SendSet(SetCommand cmd, int read_timeout_ms = 3000);

  // 供 HardTriggerOrchestrator 等组件绑定底层串口（生命周期与单例相同）
  ISerialPort& PortRef();

 private:
  SerialPortManager();

  mutable std::mutex mu_;
  std::unique_ptr<ISerialPort> port_;
  std::string last_error_;
};

}  // namespace camera3d::serial
