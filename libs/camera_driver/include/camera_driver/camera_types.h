#pragma once

// 相机通用类型：DeviceInfo、FrameBuffer、回调类型；与 ICameraAdapter 参数一致。

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace camera3d::camera {

// 统一错误码（业务层可扩展）
enum class CameraError : int {
  kOk = 0,
  kNotOpen = 1,
  kTimeout = 2,
  kInvalidParameter = 3,
  kBackendError = 4,
  kUnsupported = 5,
};

// 触发模式：与厂商 SDK 映射在适配器内完成
enum class TriggerMode : std::uint8_t {
  kSoftware = 0,
  kHardware = 1,
};

// ROI（像素坐标，左上宽高）
struct RoiRect {
  std::uint32_t offset_x = 0;  // 左上角列
  std::uint32_t offset_y = 0;  // 左上角行
  std::uint32_t width = 0;     // 0 可表示全幅（与 IsFullFrame 一致）
  std::uint32_t height = 0;
  // width/height 均为 0 时视为全幅 ROI。
  bool IsFullFrame() const { return width == 0 && height == 0; }
};

// 设备描述（枚举/连接后查询）
struct DeviceInfo {
  std::string backend_id;    // 与 ICameraAdapter::BackendId 对应
  std::string serial_number; // 厂商侧唯一键（如 SN）
  std::string ip;            // 设备 IPv4（可选；部分后端如大恒可按 IP 直连）
  std::string model_name;
  std::uint32_t max_width = 0;   // 传感器最大分辨率（可选）
  std::uint32_t max_height = 0;
};

// 采集一帧的元数据（pixel_format 与 Galaxy/OpenCV 枚举对齐由适配器文档说明）
struct FrameBuffer {
  std::vector<std::uint8_t> bytes;  // 原始像素或压缩载荷
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t pixel_format = 0;  // 实现约定枚举
  std::uint64_t frame_id = 0;      // 单调或厂商帧号
  std::int64_t timestamp_unix_ns = 0;
};

// 异步抓图时每帧回调。
using FrameCallback = std::function<void(const FrameBuffer&)>;

}  // namespace camera3d::camera
