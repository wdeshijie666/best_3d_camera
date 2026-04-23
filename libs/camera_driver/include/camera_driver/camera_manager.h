#pragma once

// 多实例相机管理：按 backend 注册适配器原型，按 manager_device_id 管理已打开设备与会话克隆。

#include "camera_driver/camera_capture_result.h"
#include "camera_driver/camera_types.h"
#include "camera_driver/icamera_adapter.h"

#include <memory>
#include <string>
#include <vector>

namespace camera3d::camera {

// 多设备管理（对齐 AIRVision DeviceManager 思路）：按 Manager 生成的 manager_device_id 索引会话；
// 注册适配器原型后，EnumerateAll 返回各 backend 的 DeviceInfo；CreateAndOpenDevice 根据 DeviceInfo 克隆并 Open（优先 ip）。
class CameraManager {
 public:
  CameraManager();
  ~CameraManager();

  // 注册某 backend 的适配器原型（同 BackendId 后注册覆盖）。不直接用于采集，仅用于 CloneForSession。
  void RegisterAdapter(std::shared_ptr<ICameraAdapter> prototype);

  // 已注册原型对应的 backend_id 列表。
  std::vector<std::string> ListBackendIds() const;
  // 对每个原型调用 EnumerateDevices() 并拼接结果。
  std::vector<DeviceInfo> EnumerateAll() const;

  // 打开硬件并纳入管理。manager_device_id 为空则自动生成（如 cam-000001）；非空则必须唯一，否则返回空串。
  std::string CreateAndOpenDevice(const DeviceInfo& device, const std::string& manager_device_id = {});
  // 关闭并移除管理项；未找到返回 false。
  bool CloseDevice(const std::string& manager_device_id);
  // 关闭全部已管理设备（析构时亦调用）。
  void CloseAllDevices();
  // 当前已打开的 manager_device_id 列表。
  std::vector<std::string> ListManagedDeviceIds() const;
  // 槽位存在且底层适配器 IsOpen()。
  bool IsDeviceOpen(const std::string& manager_device_id) const;

  // 转调对应适配器 GrabOne；无设备返回 false。
  bool GrabOne(const std::string& manager_device_id, FrameBuffer& out, int timeout_ms);

  // 以下均转调已打开适配器；无设备返回 false（Get 类 const 保留）。
  bool SetExposureUs(const std::string& manager_device_id, double v);
  bool GetExposureUs(const std::string& manager_device_id, double& v) const;
  bool SetGainDb(const std::string& manager_device_id, double v);
  bool GetGainDb(const std::string& manager_device_id, double& v) const;
  bool SetGamma(const std::string& manager_device_id, double v);
  bool GetGamma(const std::string& manager_device_id, double& v) const;
  bool SetTriggerMode(const std::string& manager_device_id, TriggerMode m);
  bool SetRoi(const std::string& manager_device_id, const RoiRect& roi);
  bool GetRoi(const std::string& manager_device_id, RoiRect& roi) const;
  bool GetDeviceInfo(const std::string& manager_device_id, DeviceInfo& out) const;

  // 大恒等原生结果回调；nullptr 表示清除。
  void SetResultCallback(const std::string& manager_device_id, CameraResultCallback callback);
  void ClearResultCallback(const std::string& manager_device_id);

  // Start 前内部会 SetFrameCallback(cb) 再调适配器 StartAsyncGrab。
  bool StartAsyncGrab(const std::string& manager_device_id, FrameCallback cb);
  void StopAsyncGrab(const std::string& manager_device_id);

  // 转调适配器 StartStreamGrab（大恒：SetResultCallback/StartAsyncGrab 注册回调后再启动采集）。
  bool StartStreamGrab(const std::string& manager_device_id);

  // 无此管理 ID 时返回 kNotOpen / 固定提示串。
  CameraError GetLastErrorCode(const std::string& manager_device_id) const;
  std::string GetLastErrorMessage(const std::string& manager_device_id) const;

 private:
  // 在锁内查找已打开适配器；未找到返回 nullptr。
  std::shared_ptr<ICameraAdapter> TryGetAdapter(const std::string& manager_device_id) const;

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace camera3d::camera
