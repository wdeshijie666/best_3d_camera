#pragma once

#include "serial_port/iserial_port.h"

#include <array>
#include <cstdint>
#include <string>

namespace camera3d::serial {

enum class ProductionCommand : std::uint8_t {
  kWhiteScreenToEnd = 0,  // EB 90 01 01 01 00 — 从白屏切换到最后一张图
  kFromImage1ToEnd = 1,   // EB 90 01 01 02 00 — 从第 1 张切换到最后一张
  kFromImage2To1 = 2,     // EB 90 01 01 03 00 — 从第 2 张切换到第 1 张
  kFromImage24To23 = 3,   // EB 90 01 01 19 00 — 从第 24 张切换到第 23 张
  kPick8From24 = 4,       // EB 90 01 09 01 00 — 从 24 张中挑选 8 张
};

enum class SetCommand : std::uint8_t {
  kFixedWhite = 0,       // EB 90 01 01 00 00 — 固定白屏（部分产品无白屏）
  kExitBlackToTest = 1,  // EB 90 01 02 00 00 — 退出黑屏进入测试画面
  kBlackScreen = 2,      // EB 90 01 03 00 00 — 黑屏
  kBacklightMax = 3,     // EB 90 01 04 00 00 — 背光最大
  kBacklightDefault = 4, // EB 90 01 04 50 00 — 上电默认亮度
  kBacklightMin = 5,     // EB 90 01 04 FF 00 — 背光最小
  kTestPatternBlack = 6, // EB 90 01 05 00 00 — 黑屏（不熄灭背光）
  kTestPatternRed = 7,   // EB 90 01 05 01 00
  kTestPatternGreen = 8, // EB 90 01 05 02 00
  kTestPatternBlue = 9,  // EB 90 01 05 03 00
  kTestPatternWhite = 10,// EB 90 01 05 04 00
  kTestPatternGradient = 11, // EB 90 01 05 08 00
  kTestPatternNormal = 12    // EB 90 01 05 09 00
};

using WireFrame6 = std::array<std::uint8_t, 6>;

struct ProjectorResult {
  bool ok = false;
  std::string message;
};

WireFrame6 ProductionFrame(ProductionCommand cmd);
WireFrame6 SetFrame(SetCommand cmd);

constexpr std::array<std::uint8_t, 4> kAckSendSuccess{0xEB, 0x90, 0x00, 0xAA};
constexpr std::array<std::uint8_t, 4> kAckProductionFinish{0xEB, 0x90, 0x00, 0x55};

ProjectorResult SendProductionCommand(ISerialPort& port, ProductionCommand cmd, int read_timeout_ms = 3000);
ProjectorResult SendSetCommand(ISerialPort& port, SetCommand cmd, int read_timeout_ms = 3000);
bool PrefixMatch4(const std::uint8_t* received_six, const std::array<std::uint8_t, 4>& prefix);

}  // namespace camera3d::serial
