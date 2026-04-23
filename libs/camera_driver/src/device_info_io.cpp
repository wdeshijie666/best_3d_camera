#include "camera_driver/device_info_io.h"

namespace camera3d::camera {

// 实现 ParseDeviceAddress：按首个 ':' 拆 backend 与 serial_number。
bool ParseDeviceAddress(const std::string& address_id, DeviceInfo& out) {
  out = DeviceInfo{};
  const auto pos = address_id.find(':');
  if (pos == std::string::npos || pos == 0 || pos + 1 >= address_id.size()) {
    return false;
  }
  out.backend_id = address_id.substr(0, pos);
  out.serial_number = address_id.substr(pos + 1);
  return !out.backend_id.empty() && !out.serial_number.empty();
}

// 实现 FormatDeviceAddress：与 ParseDeviceAddress 对称拼接。
std::string FormatDeviceAddress(const DeviceInfo& info) {
  if (info.backend_id.empty() || info.serial_number.empty()) return {};
  return info.backend_id + ":" + info.serial_number;
}

}  // namespace camera3d::camera
