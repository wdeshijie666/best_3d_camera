#pragma once

// Hub 进程内周期性 UDP 广播设备发现信标（与 camera3d/hub/device_discovery_beacon.h 一致）。

#include <atomic>
#include <cstdint>
#include <string>

#include <camera3d/hub/device_discovery_beacon.h>

#include "hub_file_config.h"

namespace camera3d::hub {

struct HubBroadcastRuntimeParams {
  bool enable = true;
  std::uint16_t discovery_dest_port = kDeviceDiscoveryUdpPort;
  int interval_ms = 1500;
  int hub_grpc_port = 0;
  std::string model;
  std::string serial;
  std::string mac;
  std::string hub_host;
};

// fc 可为 nullptr（使用全部默认值）；selected_grpc_port 为 gRPC 实际绑定端口。
HubBroadcastRuntimeParams BuildHubBroadcastParams(const HubFileConfig* fc, int selected_grpc_port);

// 阻塞循环直至 stop 为 true；供独立 std::thread 运行。
void RunHubDeviceBroadcastLoop(std::atomic<bool>& stop, const HubBroadcastRuntimeParams& params);

}  // namespace camera3d::hub
