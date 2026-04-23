#include "hub_file_config.h"

// hub_service.json 解析实现：提取 hub.listen、projector.com_index、cameras[] 节点。

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace camera3d::hub {
namespace {

std::string ReadWholeFile(const std::string& path, std::string& err) {
  err.clear();
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    err = "cannot open file: " + path;
    return {};
  }
  std::ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

std::size_t SkipWs(const std::string& s, std::size_t i) {
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  return i;
}

std::size_t MatchClosingBrace(const std::string& s, std::size_t open_brace) {
  int depth = 0;
  for (std::size_t i = open_brace; i < s.size(); ++i) {
    if (s[i] == '{')
      ++depth;
    else if (s[i] == '}') {
      --depth;
      if (depth == 0) return i;
    }
  }
  return std::string::npos;
}

std::string ExtractStringField(const std::string& obj, const char* key) {
  const std::string pat = std::string("\"") + key + "\"";
  std::size_t k = obj.find(pat);
  if (k == std::string::npos) return {};
  std::size_t c = obj.find(':', k + pat.size());
  if (c == std::string::npos) return {};
  c = SkipWs(obj, c + 1);
  if (c >= obj.size() || obj[c] != '"') return {};
  const std::size_t e = obj.find('"', c + 1);
  if (e == std::string::npos) return {};
  return obj.substr(c + 1, e - c - 1);
}

std::string ExtractObjectForKey(const std::string& doc, const char* key) {
  const std::string pat = std::string("\"") + key + "\"";
  std::size_t k = doc.find(pat);
  if (k == std::string::npos) return {};
  std::size_t c = doc.find(':', k + pat.size());
  if (c == std::string::npos) return {};
  c = SkipWs(doc, c + 1);
  if (c >= doc.size() || doc[c] != '{') return {};
  const std::size_t end = MatchClosingBrace(doc, c);
  if (end == std::string::npos) return {};
  return doc.substr(c, end - c + 1);
}

}  // namespace

bool LoadHubFileConfig(const std::string& path, HubFileConfig& out, std::string& err) {
  out = HubFileConfig{};
  std::string e;
  const std::string doc = ReadWholeFile(path, e);
  if (!e.empty()) {
    err = e;
    return false;
  }
  if (doc.empty()) {
    err = "empty config file";
    return false;
  }

  const std::string hub_obj = ExtractObjectForKey(doc, "hub");
  if (!hub_obj.empty()) {
    const std::string listen = ExtractStringField(hub_obj, "listen");
    if (!listen.empty()) out.listen_address = listen;
  }

  const std::string proj_obj = ExtractObjectForKey(doc, "projector");
  if (!proj_obj.empty()) {
    const std::string com = ExtractStringField(proj_obj, "com_index");
    if (!com.empty()) {
      out.projector_com_index = static_cast<std::uint32_t>(std::strtoul(com.c_str(), nullptr, 10));
    }
  }

  const std::string disc_obj = ExtractObjectForKey(doc, "discovery");
  if (!disc_obj.empty()) {
    const std::string en = ExtractStringField(disc_obj, "enable");
    if (en == "false" || en == "0") out.discovery.enable = false;
    const std::string port = ExtractStringField(disc_obj, "udp_port");
    if (!port.empty()) {
      const unsigned long p = std::strtoul(port.c_str(), nullptr, 10);
      if (p > 0 && p <= 65535UL) out.discovery.udp_port = static_cast<std::uint16_t>(p);
    }
    const std::string ival = ExtractStringField(disc_obj, "interval_ms");
    if (!ival.empty()) {
      const long iv = std::strtol(ival.c_str(), nullptr, 10);
      if (iv >= 250 && iv <= 3'600'000) out.discovery.interval_ms = static_cast<int>(iv);
    }
    out.discovery.advertise_host = ExtractStringField(disc_obj, "advertise_host");
    out.discovery.device_model = ExtractStringField(disc_obj, "device_model");
    out.discovery.device_serial = ExtractStringField(disc_obj, "device_serial");
  }

  const std::string capture_obj = ExtractObjectForKey(doc, "capture");
  if (!capture_obj.empty()) {
    const std::string fph = ExtractStringField(capture_obj, "frames_per_hardware_trigger");
    if (!fph.empty()) {
      const unsigned long v = std::strtoul(fph.c_str(), nullptr, 10);
      if (v >= 1UL && v <= 4096UL) {
        out.frames_per_hardware_trigger = static_cast<std::uint32_t>(v);
      }
    }
  }

  std::size_t pos = 0;
  while ((pos = doc.find("\"camera\"", pos)) != std::string::npos) {
    std::size_t ob = doc.find('{', pos);
    if (ob == std::string::npos) break;
    const std::size_t ce = MatchClosingBrace(doc, ob);
    if (ce == std::string::npos) break;
    const std::string inner = doc.substr(ob, ce - ob + 1);
    HubFileCameraNode node;
    node.ip = ExtractStringField(inner, "ip");
    node.serial_number = ExtractStringField(inner, "serial_number");
    if (node.serial_number.empty()) node.serial_number = ExtractStringField(inner, "serial");
    node.backend_id = ExtractStringField(inner, "backend");
    if (node.backend_id.empty()) node.backend_id = "daheng";
    if (!node.serial_number.empty()) {
      out.cameras.push_back(std::move(node));
    }
    pos = ce + 1;
  }

  if (out.cameras.empty()) {
    err = "no cameras parsed (expect \"camera\" objects with serial_number under \"cameras\" array)";
    return false;
  }
  return true;
}

}  // namespace camera3d::hub
