#pragma once

#include "camera_sdk/user_camera_sdk.h"

namespace camera3d::sdk {

// 监听 UDP 广播端口，解析 CAMERA3D_BEACON JSON；与 Hub 侧 hub_device_broadcaster 协议一致。
bool DiscoverHubDevicesUdp(std::vector<DiscoveredHubDevice>* out, std::uint16_t listen_udp_port, int timeout_ms);

}  // namespace camera3d::sdk
