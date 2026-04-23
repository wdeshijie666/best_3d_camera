#pragma once

// Hub ↔ SDK 设备发现：UDP 广播 JSON 载荷（轻量字符串拼接/解析，不依赖 JSON 库）。
// 字段与默认值与产品约定一致；未配置时 Hub 使用默认型号/序列号。

#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>

namespace camera3d::hub {

inline constexpr const char* kDeviceDiscoveryMagic = "CAMERA3D_BEACON";
inline constexpr int kDeviceDiscoveryJsonVersion = 1;
inline constexpr std::uint16_t kDeviceDiscoveryUdpPort = 55431;

inline constexpr const char* kDefaultDiscoveryModel = "best-3d";
inline constexpr const char* kDefaultDiscoverySerial = "best-3d-p001";

struct DeviceDiscoveryBeaconData {
  std::string model = kDefaultDiscoveryModel;
  std::string serial = kDefaultDiscoverySerial;
  std::string mac;
  std::string hub_host;
  std::uint16_t hub_port = 0;
};

inline std::string JsonEscapeForBeacon(std::string_view s) {
  std::string r;
  r.reserve(s.size() + 8);
  for (char c : s) {
    if (c == '\\' || c == '"') r.push_back('\\');
    r.push_back(c);
  }
  return r;
}

inline std::string BuildDeviceDiscoveryJson(const DeviceDiscoveryBeaconData& d) {
  std::ostringstream os;
  os << "{\"m\":\"" << kDeviceDiscoveryMagic << "\",\"v\":" << kDeviceDiscoveryJsonVersion << ",\"model\":\""
     << JsonEscapeForBeacon(d.model) << "\",\"serial\":\"" << JsonEscapeForBeacon(d.serial) << "\",\"mac\":\""
     << JsonEscapeForBeacon(d.mac) << "\",\"hub_host\":\"" << JsonEscapeForBeacon(d.hub_host) << "\",\"hub_port\":"
     << static_cast<int>(d.hub_port) << "}";
  return os.str();
}

inline bool ExtractJsonStringField(std::string_view doc, std::string_view key, std::string* out) {
  const std::string pat = std::string("\"") + std::string(key) + "\":\"";
  const std::size_t pos = doc.find(pat);
  if (pos == std::string::npos) return false;
  std::size_t i = pos + pat.size();
  std::string v;
  while (i < doc.size()) {
    const char c = doc[i];
    if (c == '"') {
      *out = std::move(v);
      return true;
    }
    if (c == '\\' && i + 1 < doc.size()) {
      v.push_back(doc[i + 1]);
      i += 2;
      continue;
    }
    v.push_back(c);
    ++i;
  }
  return false;
}

inline bool ExtractJsonUIntField(std::string_view doc, std::string_view key, std::uint16_t* out) {
  const std::string pat = std::string("\"") + std::string(key) + "\":";
  std::size_t pos = doc.find(pat);
  if (pos == std::string::npos) return false;
  pos += pat.size();
  while (pos < doc.size() && (doc[pos] == ' ' || doc[pos] == '\t')) ++pos;
  if (pos >= doc.size()) return false;
  if (doc[pos] == '"') {
    ++pos;
    const std::size_t q = doc.find('"', pos);
    if (q == std::string::npos) return false;
    const std::string s(doc.substr(pos, q - pos));
    const unsigned long v = std::strtoul(s.c_str(), nullptr, 10);
    if (v == 0UL && s != "0") return false;
    *out = static_cast<std::uint16_t>(v > 65535UL ? 65535UL : v);
    return true;
  }
  std::size_t end = pos;
  while (end < doc.size() && doc[end] >= '0' && doc[end] <= '9') ++end;
  if (end == pos) return false;
  const unsigned long v = std::strtoul(std::string(doc.substr(pos, end - pos)).c_str(), nullptr, 10);
  *out = static_cast<std::uint16_t>(v > 65535UL ? 65535UL : v);
  return true;
}

// 解析成功且包含魔数时返回 true；缺省字段用 BeaconData 内默认值补齐。
inline bool TryParseDeviceDiscoveryJson(std::string_view payload, DeviceDiscoveryBeaconData& out) {
  if (payload.find(kDeviceDiscoveryMagic) == std::string::npos) return false;
  DeviceDiscoveryBeaconData d;
  std::string s;
  if (ExtractJsonStringField(payload, "model", &s) && !s.empty()) d.model = std::move(s);
  s.clear();
  if (ExtractJsonStringField(payload, "serial", &s) && !s.empty()) d.serial = std::move(s);
  s.clear();
  if (ExtractJsonStringField(payload, "mac", &s) && !s.empty()) d.mac = std::move(s);
  s.clear();
  if (ExtractJsonStringField(payload, "hub_host", &s) && !s.empty()) d.hub_host = std::move(s);
  std::uint16_t p = 0;
  if (ExtractJsonUIntField(payload, "hub_port", &p) && p != 0) d.hub_port = p;
  if (d.hub_host.empty() || d.hub_port == 0) return false;
  out = std::move(d);
  return true;
}

}  // namespace camera3d::hub
