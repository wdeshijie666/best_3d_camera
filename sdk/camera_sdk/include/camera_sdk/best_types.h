#pragma once

// 应用层枚举与值类型：与 proto 字段语义对齐，不直接依赖 gRPC 生成头。

#include <cstdint>
#include <string>
#include <vector>

namespace camera3d::best {

/// 与常见设备 SDK 风格对齐的状态码：0 成功，负数为参数类错误，正数为运行时错误。
enum class BestStatus : int {
  kSuccess = 0,
  kInvalidParameter = -1,
  kNotConnected = 1,
  kFail = 2,
  kTimeout = 3,
  kNotSupported = 4,
};

enum class BestLogLevel {
  kTrace,
  kDebug,
  kInfo,
  kWarn,
  kError,
  kCritical,
};

/// 图像 ROI（像素）。当前 Hub 未实现 ROI 时，Set/Get 返回 kNotSupported。
struct BestROI {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

/// 共享内存帧引用，与 proto ShmFrameRef 语义一致；pixel_format 由实现侧约定枚举值。
struct BestShmFrameRef {
  std::string region_name;
  std::uint64_t seq = 0;
  std::uint64_t offset_bytes = 0;
  std::uint64_t size_bytes = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t pixel_format = 0;
  std::int64_t timestamp_unix_ns = 0;
};

/// GetDepth 中 repeated camera_raw_frames 的单项：含 burst 序号（单次硬触发多帧）。
struct BestCameraRawFrameItem : BestShmFrameRef {
  std::uint32_t camera_index = 0;
  std::string serial_number;
  std::string ip;
  std::string manager_device_id;
  std::uint32_t burst_frame_index = 0;
  std::uint32_t channels = 0;
  std::uint32_t row_step_bytes = 0;
};

/// Capture RPC 直接回传的内联图片（联调用）。
struct BestInlineImage {
  std::string name;
  std::vector<std::uint8_t> payload;
};

/// 通用整型参数通道（对齐 AIR 风格 SetValue/GetValue 的子集）。
enum class BestConfigType : int {
  kUnknown = -1,
  kExposureTimeUs = 10,
  /// values[0] 为毫分贝，即 (int)(gain_db * 1000)
  kGainDbMilli = 11,
  /// values[0] 为毫伽马，即 (int)(gamma * 1000)，对应 2D 伽马调节
  kGammaMilli = 12,
};

/// 与 camera_hub.proto HubParameterType 数值一致；当前开放 2D 相机曝光/增益/伽马。
enum class ParameterType : std::int32_t {
  kInvalid = 0,
  /// 2D 曝光（微秒），对应 proto exposure_2d 语义
  kExposure2d = 1,
  /// 2D 增益（dB）
  kGain2d = 2,
  /// 2D 伽马（线性浮点）
  kGamma2d = 3,
};

struct ParameterValue {
  ParameterType type{ParameterType::kInvalid};
  double value{0};
};

}  // namespace camera3d::best
