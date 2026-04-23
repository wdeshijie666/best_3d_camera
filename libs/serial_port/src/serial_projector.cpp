#include "serial_port/serial_projector.h"

#include "platform_diag/logging.h"

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

namespace camera3d::serial {
namespace {

constexpr std::uint8_t kProductionWire[5][6] = {
    {0xEB, 0x90, 0x01, 0x01, 0x01, 0x00},
    {0xEB, 0x90, 0x01, 0x01, 0x02, 0x00},
    {0xEB, 0x90, 0x01, 0x01, 0x03, 0x00},
    {0xEB, 0x90, 0x01, 0x01, 0x19, 0x00},
    {0xEB, 0x90, 0x01, 0x09, 0x01, 0x00},
};

constexpr std::uint8_t kSetWire[13][6] = {
    {0xEB, 0x90, 0x01, 0x01, 0x00, 0x00},
    {0xEB, 0x90, 0x01, 0x02, 0x00, 0x00},
    {0xEB, 0x90, 0x01, 0x03, 0x00, 0x00},
    {0xEB, 0x90, 0x01, 0x04, 0x00, 0x00},
    {0xEB, 0x90, 0x01, 0x04, 0x50, 0x00},
    {0xEB, 0x90, 0x01, 0x04, 0xFF, 0x00},
    {0xEB, 0x90, 0x01, 0x05, 0x00, 0x00},
    {0xEB, 0x90, 0x01, 0x05, 0x01, 0x00},
    {0xEB, 0x90, 0x01, 0x05, 0x02, 0x00},
    {0xEB, 0x90, 0x01, 0x05, 0x03, 0x00},
    {0xEB, 0x90, 0x01, 0x05, 0x04, 0x00},
    {0xEB, 0x90, 0x01, 0x05, 0x08, 0x00},
    {0xEB, 0x90, 0x01, 0x05, 0x09, 0x00},
};

bool ReadAtLeastBytes(ISerialPort& port, std::vector<std::uint8_t>& acc, std::size_t need, int timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (acc.size() < need) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      break;
    }
    const int remain =
        static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
    const int slice = remain < 1 ? 1 : (remain > 500 ? 500 : remain);
    std::vector<std::uint8_t> chunk;
    if (!port.ReadBytes(chunk, need - acc.size(), slice)) {
      CAMERA3D_LOGW("SerialProjector: ReadBytes 失败 {}", port.GetLastErrorMessage());
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    if (chunk.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    acc.insert(acc.end(), chunk.begin(), chunk.end());
  }
  return acc.size() >= need;
}

bool MatchAckSend(const std::uint8_t* received_six, std::uint8_t expected_payload) {
  if (!received_six) return false;
  return received_six[0] == 0xEB && received_six[1] == 0x90 && received_six[2] == 0x00 &&
         received_six[3] == 0xAA && received_six[4] == expected_payload && received_six[5] == 0x00;
}

bool MatchAckFinish(const std::uint8_t* received_six, std::uint8_t* out_stop_image_id) {
  if (!received_six) return false;
  if (received_six[0] != 0xEB || received_six[1] != 0x90 || received_six[2] != 0x00 ||
      received_six[3] != 0x55 || received_six[5] != 0x00) {
    return false;
  }
  if (out_stop_image_id) *out_stop_image_id = received_six[4];
  return true;
}

}  // namespace

WireFrame6 ProductionFrame(ProductionCommand cmd) {
  const auto i = static_cast<unsigned>(cmd);
  WireFrame6 out{};
  if (i >= 4) {
    return out;
  }
  std::memcpy(out.data(), kProductionWire[i], 6);
  return out;
}

WireFrame6 SetFrame(SetCommand cmd) {
  const auto i = static_cast<unsigned>(cmd);
  WireFrame6 out{};
  if (i >= 3) {
    return out;
  }
  std::memcpy(out.data(), kSetWire[i], 6);
  return out;
}

bool PrefixMatch4(const std::uint8_t* received_six, const std::array<std::uint8_t, 4>& prefix) {
  if (!received_six) {
    return false;
  }
  return std::memcmp(received_six, prefix.data(), 4) == 0;
}

ProjectorResult SendProductionCommand(ISerialPort& port, ProductionCommand cmd, int read_timeout_ms) {
  ProjectorResult r;
  const unsigned idx = static_cast<unsigned>(cmd);
  if (idx >= 5) {
    r.message = "Unknown CMD!";
    return r;
  }
  if (!port.IsOpen()) {
    r.message = "serial not open";
    return r;
  }
  const auto frame = ProductionFrame(cmd);
  if (!port.WriteBytes(frame.data(), frame.size())) {
    r.message = "send cmd error";
    CAMERA3D_LOGW("SendProductionCommand: write failed");
    return r;
  }

  std::vector<std::uint8_t> first;
  if (!ReadAtLeastBytes(port, first, 6, read_timeout_ms)) {
    r.message = "send cmd error (no ack)";
    return r;
  }
  if (!MatchAckSend(first.data(), frame[4])) {
    r.message = "send cmd error (ack mismatch)";
    return r;
  }

  std::vector<std::uint8_t> second;
  if (!ReadAtLeastBytes(port, second, 6, read_timeout_ms)) {
    r.message = "production unknow error (no finish)";
    return r;
  }
  std::uint8_t stop_image_id = 0;
  if (!MatchAckFinish(second.data(), &stop_image_id)) {
    r.message = "production unknow error (finish mismatch)";
    return r;
  }

  r.ok = true;
  r.message = "Send Production Good! stop_image=" + std::to_string(stop_image_id);
  return r;
}

ProjectorResult SendSetCommand(ISerialPort& port, SetCommand cmd, int read_timeout_ms) {
  ProjectorResult r;
  const unsigned idx = static_cast<unsigned>(cmd);
  if (idx >= 13) {
    r.message = "Unknown CMD!";
    return r;
  }
  if (!port.IsOpen()) {
    r.message = "serial not open";
    return r;
  }
  const auto frame = SetFrame(cmd);
  if (!port.WriteBytes(frame.data(), frame.size())) {
    r.message = "send cmd error";
    return r;
  }

  std::vector<std::uint8_t> first;
  if (!ReadAtLeastBytes(port, first, 6, read_timeout_ms)) {
    r.message = "send cmd error (no ack)";
    return r;
  }
  if (!MatchAckSend(first.data(), frame[4])) {
    r.message = "send cmd error (ack mismatch)";
    return r;
  }

  r.ok = true;
  r.message = "Send Set Good!";
  return r;
}

}  // namespace camera3d::serial
