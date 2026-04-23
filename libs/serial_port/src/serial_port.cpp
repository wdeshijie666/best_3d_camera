#include "serial_port/iserial_port.h"

#include "platform_diag/logging.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

#include <chrono>
#include <thread>

namespace camera3d::serial {
namespace {

#ifdef _WIN32
class SerialPortWin32 final : public ISerialPort {
 public:
  bool Open(std::uint32_t port_index, unsigned baud_rate) override {
    Close();
    if (port_index == 0) {
      last_err_ = "invalid com index: 0";
      CAMERA3D_LOGE("打开串口失败：无效 COM 序号 {}", port_index);
      return false;
    }
    port_name_ = "COM" + std::to_string(port_index);
    device_path_ = (port_index > 9) ? ("\\\\.\\COM" + std::to_string(port_index)) : port_name_;
    last_err_.clear();
    const std::wstring wname(device_path_.begin(), device_path_.end());
    handle_ = CreateFileW(wname.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
      last_err_ = "CreateFile failed for " + port_name_;
      CAMERA3D_LOGE("打开串口失败 {} ({})", port_name_, device_path_);
      return false;
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(handle_, &dcb)) {
      last_err_ = "GetCommState failed";
      CAMERA3D_LOGE("GetCommState 失败");
      Close();
      return false;
    }
    dcb.BaudRate = baud_rate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    // 与 geely CSerialPort::InitPort 一致，部分一体机投采串口依赖 RTS
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    if (!SetCommState(handle_, &dcb)) {
      last_err_ = "SetCommState failed";
      CAMERA3D_LOGE("SetCommState 失败");
      Close();
      return false;
    }

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 200;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 200;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(handle_, &timeouts);

    CAMERA3D_LOGI("串口已打开 {} @ {}", port_name_, baud_rate);
    return true;
  }

  void Close() override {
    if (handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
      handle_ = INVALID_HANDLE_VALUE;
    }
    port_name_.clear();
    device_path_.clear();
  }

  bool IsOpen() const override { return handle_ != INVALID_HANDLE_VALUE; }
  std::string CurrentPortName() const override { return port_name_; }

  bool WriteBytes(const std::uint8_t* data, std::size_t len) override {
    if (handle_ == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok =
        WriteFile(handle_, data, static_cast<DWORD>(len), &written, nullptr) && written == len;
    if (!ok) last_err_ = "WriteFile failed";
    return ok;
  }

  bool ReadBytes(std::vector<std::uint8_t>& out, std::size_t max_len, int timeout_ms) override {
    if (handle_ == INVALID_HANDLE_VALUE) return false;
    out.resize(max_len);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::size_t total = 0;
    while (total < max_len && std::chrono::steady_clock::now() < deadline) {
      DWORD n = 0;
      if (!ReadFile(handle_, out.data() + total, static_cast<DWORD>(max_len - total), &n, nullptr)) {
        last_err_ = "ReadFile failed";
        return false;
      }
      total += n;
      if (n == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
    out.resize(total);
    return true;
  }

  std::string GetLastErrorMessage() const override { return last_err_; }

 private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
  std::string port_name_;
  std::string device_path_;
  std::string last_err_;
};
#endif

}  // namespace

std::unique_ptr<ISerialPort> CreateSerialPortWin32() {
#ifdef _WIN32
  return std::make_unique<SerialPortWin32>();
#else
  CAMERA3D_LOGE("CreateSerialPortWin32：当前非 Windows，返回空实现");
  return nullptr;
#endif
}

}  // namespace camera3d::serial
