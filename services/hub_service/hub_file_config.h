#pragma once

// JSON 文件配置模型与解析（轻量字符串扫描，非完整 JSON 库）。供 UnifiedStartupFromConfig 使用。

#include <camera3d/hub/device_discovery_beacon.h>

#include <cstdint>
#include <string>
#include <vector>

namespace camera3d::hub {

// 配置文件中的一路相机（JSON 中多个 "camera" 对象顺序 = 从左到右槽位）。
struct HubFileCameraNode {
  std::string ip;
  std::string serial_number;
  std::string backend_id = "daheng";  // 与 DeviceInfo.backend_id 一致
};

// 可选 discovery 段：控制 UDP 设备发现广播（见 camera3d/hub/device_discovery_beacon.h）。
struct HubDiscoverySettings {
  bool enable = true;
  std::uint16_t udp_port = kDeviceDiscoveryUdpPort;
  int interval_ms = 1500;
  /// 广播 JSON 中 hub_host；为空则 Hub 进程自动取本机首选 IPv4。
  std::string advertise_host;
  /// 未配置时使用 kDefaultDiscoveryModel。
  std::string device_model;
  /// 为空则优先使用 cameras[0].serial_number，否则 kDefaultDiscoverySerial。
  std::string device_serial;
};

struct HubFileConfig {
  std::string listen_address = "0.0.0.0:50051";
  std::uint32_t projector_com_index = 0;
  /// 单次硬触发期望收集的相机回调帧数（每路相机各收满该数量后才落盘 SHM）；见 JSON capture.frames_per_hardware_trigger
  std::uint32_t frames_per_hardware_trigger = 24;
  std::vector<HubFileCameraNode> cameras;
  HubDiscoverySettings discovery;
};

// 从 JSON 文件加载；失败返回 false 并写 err
bool LoadHubFileConfig(const std::string& path, HubFileConfig& out, std::string& err);

}  // namespace camera3d::hub
