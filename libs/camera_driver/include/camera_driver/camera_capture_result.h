#pragma once

// 异步采集结果基类与 FrameBuffer 派生；适配器在回调中投递具体类型。

#include "camera_driver/camera_types.h"

#include <cstdint>
#include <functional>
#include <string>

namespace camera3d::camera {

// 与 AIRVision device_manager::setResultCallback(CaptureResult*) 对齐：回调入参为结果基类指针，
// 业务侧 dynamic_cast 到具体派生类型读取数据。

enum class CameraResultKind : std::uint32_t {
  kUnknown = 0,
  kFrameBuffer = 1,
  kDahengGalaxyNative = 2,
};

struct CameraCaptureResultBase {
  bool success = false;              // 本结果是否表示一次成功采集
  std::string error_message;         // success 为 false 时的可读原因

  CameraCaptureResultBase() = default;
  virtual ~CameraCaptureResultBase() = default;

  // 派生类型鉴别；供回调里 dynamic_cast 或 switch。
  virtual CameraResultKind kind() const noexcept = 0;
  // 深拷贝；调用方负责 delete 返回值（若走裸指针接口）。
  virtual CameraCaptureResultBase* clone() const = 0;

  CameraCaptureResultBase(const CameraCaptureResultBase&) = default;
  CameraCaptureResultBase& operator=(const CameraCaptureResultBase&) = default;
};

// 2D 原始帧（当前 ICameraAdapter 异步抓图路径）
struct FrameBufferCameraResult final : CameraCaptureResultBase {
  FrameBuffer frame;  // 成功时的像素载荷与几何信息

  FrameBufferCameraResult() = default;
  explicit FrameBufferCameraResult(const FrameBuffer& in) : frame(in) { success = true; }

  CameraResultKind kind() const noexcept override { return CameraResultKind::kFrameBuffer; }
  CameraCaptureResultBase* clone() const override { return new FrameBufferCameraResult(*this); }
};

using CameraResultCallback = std::function<void(CameraCaptureResultBase*)>;

}  // namespace camera3d::camera
