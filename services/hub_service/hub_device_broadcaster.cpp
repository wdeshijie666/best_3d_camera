#include "hub_device_broadcaster.h"

#include "platform_diag/logging.h"

#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace camera3d::hub {
namespace {

#if defined(_WIN32)
struct PrimaryNic {
  std::string ipv4 = "127.0.0.1";
  std::string mac = "00:00:00:00:00:00";
};

std::string FormatMac(const BYTE* addr, ULONG len) {
  if (!addr || len == 0) return "00:00:00:00:00:00";
  char buf[64]{};
  const ULONG n = len < 8 ? len : 8u;
  int p = 0;
  for (ULONG i = 0; i < n; ++i) {
    if (i) buf[p++] = ':';
    static const char* hx = "0123456789ABCDEF";
    buf[p++] = hx[(addr[i] >> 4) & 0xF];
    buf[p++] = hx[addr[i] & 0xF];
  }
  return std::string(buf);
}

PrimaryNic ProbePrimaryNicWin() {
  PrimaryNic out;
  ULONG buf_len = 16 * 1024;
  std::vector<char> buf(buf_len);
  const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
  ULONG ret = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()),
                                     &buf_len);
  if (ret == ERROR_BUFFER_OVERFLOW) {
    buf.resize(buf_len);
    ret = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()),
                               &buf_len);
  }
  if (ret != NO_ERROR) return out;

  for (PIP_ADAPTER_ADDRESSES a = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()); a; a = a->Next) {
    if (a->OperStatus != IfOperStatusUp) continue;
    // 跳过环回等虚拟接口（IF_TYPE_SOFTWARE_LOOPBACK=24，避免额外头依赖写死常量）
    if (a->IfType == 24u) continue;
    for (PIP_ADAPTER_UNICAST_ADDRESS u = a->FirstUnicastAddress; u; u = u->Next) {
      if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
      const auto* sin = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
      char ipbuf[64]{};
      if (!InetNtopA(AF_INET, &sin->sin_addr, ipbuf, sizeof(ipbuf))) continue;
      if (std::string_view(ipbuf) == "127.0.0.1") continue;
      out.ipv4 = ipbuf;
      out.mac = FormatMac(a->PhysicalAddress, a->PhysicalAddressLength);
      return out;
    }
  }
  return out;
}
#else
struct PrimaryNic {
  std::string ipv4 = "127.0.0.1";
  std::string mac = "00:00:00:00:00:00";
};

PrimaryNic ProbePrimaryNicPosix() {
  PrimaryNic out;
  ifaddrs* ifa = nullptr;
  if (getifaddrs(&ifa) != 0 || !ifa) return out;
  for (ifaddrs* p = ifa; p; p = p->ifa_next) {
    if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
    const auto* sin = reinterpret_cast<sockaddr_in*>(p->ifa_addr);
    char ipbuf[INET_ADDRSTRLEN]{};
    if (!inet_ntop(AF_INET, &sin->sin_addr, ipbuf, sizeof(ipbuf))) continue;
    if (std::string_view(ipbuf) == "127.0.0.1") continue;
    out.ipv4 = ipbuf;
    break;
  }
  freeifaddrs(ifa);
  return out;
}
#endif

PrimaryNic ProbePrimaryNic() {
#if defined(_WIN32)
  return ProbePrimaryNicWin();
#else
  return ProbePrimaryNicPosix();
#endif
}

#if defined(_WIN32)
void EnsureWinsockOnce() {
  static std::once_flag once;
  std::call_once(once, [] {
    WSADATA w{};
    (void)WSAStartup(MAKEWORD(2, 2), &w);
  });
}
#endif

}  // namespace

HubBroadcastRuntimeParams BuildHubBroadcastParams(const HubFileConfig* fc, int selected_grpc_port) {
  HubBroadcastRuntimeParams r;
  const PrimaryNic nic = ProbePrimaryNic();
  r.enable = fc ? fc->discovery.enable : true;
  r.discovery_dest_port = fc ? fc->discovery.udp_port : kDeviceDiscoveryUdpPort;
  r.interval_ms = fc ? fc->discovery.interval_ms : 1500;
  r.hub_grpc_port = selected_grpc_port;
  if (fc && !fc->discovery.device_model.empty()) {
    r.model = fc->discovery.device_model;
  } else {
    r.model = kDefaultDiscoveryModel;
  }
  if (fc && !fc->discovery.device_serial.empty()) {
    r.serial = fc->discovery.device_serial;
  } else if (fc && !fc->cameras.empty() && !fc->cameras[0].serial_number.empty()) {
    r.serial = fc->cameras[0].serial_number;
  } else {
    r.serial = kDefaultDiscoverySerial;
  }
  if (fc && !fc->discovery.advertise_host.empty()) {
    r.hub_host = fc->discovery.advertise_host;
  } else {
    r.hub_host = nic.ipv4;
  }
  r.mac = nic.mac;
  return r;
}

void RunHubDeviceBroadcastLoop(std::atomic<bool>& stop, const HubBroadcastRuntimeParams& params) {
  if (!params.enable || params.hub_grpc_port <= 0 || params.hub_host.empty()) {
    CAMERA3D_LOGW("设备发现广播未启动（enable={} hub_port={} hub_host 空={}）", params.enable, params.hub_grpc_port,
                   params.hub_host.empty());
    return;
  }

#if defined(_WIN32)
  EnsureWinsockOnce();
  const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) {
    CAMERA3D_LOGE("设备发现广播：创建 UDP socket 失败 {}", WSAGetLastError());
    return;
  }
  BOOL bro = TRUE;
  if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&bro), sizeof(bro)) != 0) {
    CAMERA3D_LOGE("设备发现广播：SO_BROADCAST 失败 {}", WSAGetLastError());
    closesocket(s);
    return;
  }
  sockaddr_in dst{};
  dst.sin_family = AF_INET;
  dst.sin_port = htons(params.discovery_dest_port);
  dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);

  DeviceDiscoveryBeaconData beacon;
  beacon.model = params.model;
  beacon.serial = params.serial;
  beacon.mac = params.mac;
  beacon.hub_host = params.hub_host;
  beacon.hub_port = static_cast<std::uint16_t>(params.hub_grpc_port);

  CAMERA3D_LOGI("设备发现广播已启动：目标 UDP {} 间隔 {}ms 型号={} 序列={} hub={}:{}", params.discovery_dest_port,
                params.interval_ms, beacon.model, beacon.serial, beacon.hub_host,
                static_cast<int>(beacon.hub_port));

  while (!stop.load(std::memory_order_relaxed)) {
    const std::string json = BuildDeviceDiscoveryJson(beacon);
    const int r = sendto(s, json.data(), static_cast<int>(json.size()), 0, reinterpret_cast<sockaddr*>(&dst),
                         static_cast<int>(sizeof(dst)));
    if (r == SOCKET_ERROR) {
      CAMERA3D_LOGW("设备发现广播 sendto 失败 {}", WSAGetLastError());
    }
    const int step = 200;
    int left = params.interval_ms < step ? step : params.interval_ms;
    while (left > 0 && !stop.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(step));
      left -= step;
    }
  }
  closesocket(s);
#else
  const int s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0) {
    CAMERA3D_LOGE("设备发现广播：创建 UDP socket 失败");
    return;
  }
  const int bro = 1;
  if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, &bro, sizeof(bro)) != 0) {
    CAMERA3D_LOGE("设备发现广播：SO_BROADCAST 失败");
    close(s);
    return;
  }
  sockaddr_in dst{};
  dst.sin_family = AF_INET;
  dst.sin_port = htons(params.discovery_dest_port);
  dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);

  DeviceDiscoveryBeaconData beacon;
  beacon.model = params.model;
  beacon.serial = params.serial;
  beacon.mac = params.mac;
  beacon.hub_host = params.hub_host;
  beacon.hub_port = static_cast<std::uint16_t>(params.hub_grpc_port);

  CAMERA3D_LOGI("设备发现广播已启动：目标 UDP {} 间隔 {}ms 型号={} 序列={} hub={}:{}", params.discovery_dest_port,
                params.interval_ms, beacon.model, beacon.serial, beacon.hub_host,
                static_cast<int>(beacon.hub_port));

  while (!stop.load(std::memory_order_relaxed)) {
    const std::string json = BuildDeviceDiscoveryJson(beacon);
    const ssize_t r =
        sendto(s, json.data(), json.size(), 0, reinterpret_cast<sockaddr*>(&dst), static_cast<int>(sizeof(dst)));
    if (r < 0) {
      CAMERA3D_LOGW("设备发现广播 sendto 失败");
    }
    const int step = 200;
    int left = params.interval_ms < step ? step : params.interval_ms;
    while (left > 0 && !stop.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(step));
      left -= step;
    }
  }
  close(s);
#endif
}

}  // namespace camera3d::hub
