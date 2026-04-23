#pragma once

// 单路相机后端抽象：枚举设备、开关流、曝光/增益、硬触发采集与异步帧回调。

#include "camera_driver/camera_capture_result.h"
#include "camera_driver/camera_types.h"

#include <memory>
#include <string>
#include <vector>

namespace camera3d::camera {

// 单台相机会话：每台物理设备由 CameraManager 克隆独立实例（见 CloneForSession），避免多开共用一个适配器状态。
class ICameraAdapter {
 public:
  virtual ~ICameraAdapter() = default;

  // 后端标识，须与 DeviceInfo.backend_id、CameraManager 注册键一致。
  virtual std::string BackendId() const = 0;

  // 枚举当前机器上该后端可见设备（未 Open 也可调用）。
  virtual std::vector<DeviceInfo> EnumerateDevices() = 0;

  // 打开指定设备键（可为 SN、IP 或后端定义的唯一键）；失败时保持未打开状态。
  virtual bool Open(const std::string& device_key) = 0;
  virtual void Close() = 0;
  virtual bool IsOpen() const = 0;
  // 当前已打开的设备键（SN/IP）；未打开时行为由实现定义，通常返回空串。
  virtual std::string CurrentDeviceId() const = 0;

  virtual bool SetExposureUs(double microseconds) = 0;
  virtual bool GetExposureUs(double& out_microseconds) const = 0;

  virtual bool SetGainDb(double gain_db) = 0;
  virtual bool GetGainDb(double& out_gain_db) const = 0;

  virtual bool SetGamma(double gamma) = 0;
  virtual bool GetGamma(double& out_gamma) const = 0;

  // 软/硬触发等；硬触发下 GrabOne/异步回调由外部光机或硬件触发。
  virtual bool SetTriggerMode(TriggerMode mode) = 0;

  virtual bool SetRoi(const RoiRect& roi) = 0;
  virtual bool GetRoi(RoiRect& out_roi) const = 0;

  // 已打开设备的静态/能力信息。
  virtual bool GetDeviceInfo(DeviceInfo& out) const = 0;

  // 阻塞抓取一帧；timeout_ms 语义由厂商 SDK 封装解释。
  virtual bool GrabOne(FrameBuffer& out, int timeout_ms) = 0;

  // 由 CameraManager 按设备会话转发：大恒等在原生回调里调用 on_result/on_frame。
  virtual void SetCaptureResultCallback(CameraResultCallback cb) = 0;
  virtual void SetFrameCallback(FrameCallback cb) = 0;
  // 连续采集：帧经 SetFrameCallback 投递；与 GrabOne 互斥策略由实现保证。
  virtual bool StartAsyncGrab() = 0;
  virtual void StopAsyncGrab() = 0;

  // 在 Open 且（如需）已 RegisterCaptureCallback 之后启动底层取流（大恒等须先注册回调再 StartGrab）。
  // 默认实现为无操作并成功；仅 polling（GrabOne）的实现可在 GrabOne 内隐式调用等价逻辑。
  virtual bool StartStreamGrab() { return true; }

  // 最近一次失败原因；线程安全由实现保证。
  virtual CameraError GetLastErrorCode() const = 0;
  virtual std::string GetLastErrorMessage() const = 0;

  // 用于 CameraManager 为每台已连接设备创建新的适配器实例（原型仅作注册模板，不直接参与多路会话）。
  virtual std::shared_ptr<ICameraAdapter> CloneForSession() const = 0;
};

}  // namespace camera3d::camera
