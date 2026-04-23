#pragma once

// 描述 Hub 地址、设备串及投影仪 COM 等连接参数；BestCamera3D::Connect(BestDeviceInfo) 优先使用本结构。

#include <cstdint>
#include <string>

namespace camera3d::best {

/// 描述一台 3D 相机在 Hub 上的接入信息；连接时可只填 IP/域名，也可填完整结构。
struct BestDeviceInfo {
  /// 展示名或用户备注
  std::string display_name;
  /// Hub 主机（不含端口），或已带端口时与 hub_port 组合规则见 BuildHubGrpcTarget
  std::string hub_host;
  /// 当 hub_host 中不含 ':' 时，与 hub_host 组成 gRPC 目标；为 0 时默认 50051
  std::uint16_t hub_port = 50051;
  /// Hub 侧设备地址，如 null:virtual0、galaxy:...
  std::string device_address;
  std::string serial_number;
  std::string firmware_version;
  std::string mac_address;
  std::string model;
  /// 透传至 Hub ConnectRequest.session_hint
  std::string session_hint;
  /// Hub ConnectRequest.projector_com_index：投影仪/一体机串口 COM 序号，0 表示不打开
  std::uint32_t projector_com_index = 0;
  std::string extra_metadata;

  /// 本仓库默认联调：本机 Hub + 空设备仿真
  static BestDeviceInfo DefaultSimulator() {
    BestDeviceInfo d;
    d.display_name = "NullVirtualDevice";
    d.hub_host = "127.0.0.1";
    d.hub_port = 50051;
    d.device_address = "null:virtual0";
    d.model = "null";
    return d;
  }

  /// 生成 gRPC Channel 目标字符串，一般为 "host:port"
  std::string BuildHubGrpcTarget() const {
    if (hub_host.empty()) return {};
    if (hub_host.find(':') != std::string::npos) return hub_host;
    const int p = hub_port ? hub_port : 50051;
    return hub_host + ":" + std::to_string(p);
  }
};

}  // namespace camera3d::best
