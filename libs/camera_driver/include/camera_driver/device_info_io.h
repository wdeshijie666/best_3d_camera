#pragma once

// device_address 字符串与 DeviceInfo 互转，供 Hub Connect 与 CameraManager 统一解析。

#include "camera_driver/camera_types.h"

#include <string>

namespace camera3d::camera {

// 解析 "backend:device_key"（如 null:virtual0、daheng:SN），用于 gRPC/配置字符串与 DeviceInfo 互转。
bool ParseDeviceAddress(const std::string& address_id, DeviceInfo& out);

// 生成与 ParseDeviceAddress 对称的地址串
std::string FormatDeviceAddress(const DeviceInfo& info);

}  // namespace camera3d::camera
