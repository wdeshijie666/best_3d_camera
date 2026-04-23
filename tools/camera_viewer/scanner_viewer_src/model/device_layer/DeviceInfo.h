/**
 * @file DeviceInfo.h
 * @brief 设备信息：与具体 SDK 解耦，供设备列表与连接使用
 */
#ifndef SCANNER_VIEWER_DEVICE_INFO_H
#define SCANNER_VIEWER_DEVICE_INFO_H

#include <string>
#include <cstdint>

namespace scanner_viewer {

struct DeviceInfo {
    std::string name;
    std::string ip;
    std::string serial_number;
    std::string firmware_version;
    std::string model_name;   // 型号名称，用于选择适配器
    uint16_t port{0};
    int model_type{0};        // 与 SDK 型号枚举对应，可选
    /// Hub gRPC Connect 的 device_address（如 null:virtual0）；由 BestCamera3DAdapter 使用。
    std::string hub_device_address;
};

}  // namespace scanner_viewer

#endif  // SCANNER_VIEWER_DEVICE_INFO_H
