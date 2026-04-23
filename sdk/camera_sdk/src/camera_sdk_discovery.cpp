#include "camera_sdk_discovery.h"

#include <camera3d/hub/device_discovery_beacon.h>

#include "platform_diag/logging.h"

#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace camera3d::sdk {
namespace {

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

bool DiscoverHubDevicesUdp(std::vector<DiscoveredHubDevice>* out, std::uint16_t listen_udp_port, int timeout_ms) {
  if (!out) return false;
  out->clear();
  if (timeout_ms < 50) timeout_ms = 50;
  const std::uint16_t port = listen_udp_port ? listen_udp_port : camera3d::hub::kDeviceDiscoveryUdpPort;

#if defined(_WIN32)
  EnsureWinsockOnce();
  const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) {
    CAMERA3D_LOGE("DiscoverHubDevicesUdp: socket 失败 {}", WSAGetLastError());
    return false;
  }
  BOOL reuse = TRUE;
  (void)setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

  sockaddr_in bind_addr{};
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  bind_addr.sin_port = htons(port);
  if (bind(s, reinterpret_cast<sockaddr*>(&bind_addr), static_cast<int>(sizeof(bind_addr))) != 0) {
    CAMERA3D_LOGE("DiscoverHubDevicesUdp: bind 端口 {} 失败 {}（可能已被占用）", static_cast<int>(port),
                   WSAGetLastError());
    closesocket(s);
    return false;
  }

  std::unordered_set<std::string> seen;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  while (std::chrono::steady_clock::now() < deadline) {
    const auto ms_left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    if (ms_left < 1) break;
    const DWORD ms_chunk = static_cast<DWORD>(ms_left > 500 ? 500 : ms_left);
    (void)setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms_chunk), sizeof(ms_chunk));

    char buf[2048];
    sockaddr_in from{};
    int fromlen = static_cast<int>(sizeof(from));
    const int n = recvfrom(s, buf, static_cast<int>(sizeof(buf) - 1), 0, reinterpret_cast<sockaddr*>(&from),
                           &fromlen);
    if (n == SOCKET_ERROR) {
      const int e = WSAGetLastError();
      if (e == WSAETIMEDOUT || e == WSAEWOULDBLOCK) continue;
      if (e == WSAECONNRESET) continue;
      break;
    }
    if (n <= 0) continue;
    buf[n] = '\0';
    camera3d::hub::DeviceDiscoveryBeaconData b;
    if (!camera3d::hub::TryParseDeviceDiscoveryJson(std::string_view(buf, static_cast<std::size_t>(n)), b)) {
      continue;
    }
    const std::string key = b.hub_host + ":" + std::to_string(static_cast<int>(b.hub_port)) + ":" + b.serial;
    if (!seen.insert(key).second) continue;
    DiscoveredHubDevice d;
    d.model = std::move(b.model);
    d.serial_number = std::move(b.serial);
    d.mac_address = std::move(b.mac);
    d.hub_host = std::move(b.hub_host);
    d.hub_port = b.hub_port;
    out->push_back(std::move(d));
  }
  closesocket(s);
  return true;
#else
  const int s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0) {
    CAMERA3D_LOGE("DiscoverHubDevicesUdp: socket 失败");
    return false;
  }
  const int reuse = 1;
  (void)setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in bind_addr{};
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  bind_addr.sin_port = htons(port);
  if (bind(s, reinterpret_cast<sockaddr*>(&bind_addr), static_cast<int>(sizeof(bind_addr))) != 0) {
    CAMERA3D_LOGE("DiscoverHubDevicesUdp: bind 端口 {} 失败（可能已被占用）", static_cast<int>(port));
    close(s);
    return false;
  }

  std::unordered_set<std::string> seen;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  while (std::chrono::steady_clock::now() < deadline) {
    const auto ms_left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    if (ms_left < 1) break;
    const int ms_chunk = ms_left > 500 ? 500 : static_cast<int>(ms_left);
    timeval tv{};
    tv.tv_sec = ms_chunk / 1000;
    tv.tv_usec = (ms_chunk % 1000) * 1000;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(s, &fds);
    const int sel = select(s + 1, &fds, nullptr, nullptr, &tv);
    if (sel < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (sel == 0) continue;

    char buf[2048];
    sockaddr_in from{};
    socklen_t fromlen = sizeof(from);
    const ssize_t n = recvfrom(s, buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(&from), &fromlen);
    if (n <= 0) continue;
    buf[n] = '\0';
    camera3d::hub::DeviceDiscoveryBeaconData b;
    if (!camera3d::hub::TryParseDeviceDiscoveryJson(std::string_view(buf, static_cast<std::size_t>(n)), b)) {
      continue;
    }
    const std::string key = b.hub_host + ":" + std::to_string(static_cast<int>(b.hub_port)) + ":" + b.serial;
    if (!seen.insert(key).second) continue;
    DiscoveredHubDevice d;
    d.model = std::move(b.model);
    d.serial_number = std::move(b.serial);
    d.mac_address = std::move(b.mac);
    d.hub_host = std::move(b.hub_host);
    d.hub_port = b.hub_port;
    out->push_back(std::move(d));
  }
  close(s);
  return true;
#endif
}

}  // namespace camera3d::sdk
