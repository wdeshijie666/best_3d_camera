// CameraManager 实现：原型表 + 已打开设备槽位；CreateAndOpenDevice 克隆适配器并生成唯一 manager_device_id。

#include "camera_driver/camera_manager.h"
#include "camera_driver/device_info_io.h"

#include "platform_diag/logging.h"

#include <mutex>
#include <unordered_map>

namespace camera3d::camera {

struct CameraManager::Impl {
  mutable std::mutex mu;
  std::unordered_map<std::string, std::shared_ptr<ICameraAdapter>> prototypes_;
  struct Slot {
    std::string manager_id;
    DeviceInfo device;
    std::shared_ptr<ICameraAdapter> adapter;
  };
  std::unordered_map<std::string, Slot> open_;
  std::uint64_t seq = 1;
};

// --- CameraManager 成员实现：均通过 TryGetAdapter 或 open_/prototypes_ 表访问设备 ---

CameraManager::CameraManager() : impl_(std::make_unique<Impl>()) {}

// 实现 CameraManager::~CameraManager：确保释放所有已打开设备。
CameraManager::~CameraManager() { CloseAllDevices(); }

// 实现 CameraManager::RegisterAdapter：加锁写入原型表，按 BackendId 覆盖。
void CameraManager::RegisterAdapter(std::shared_ptr<ICameraAdapter> prototype) {
  if (!prototype) {
    return;
  }
  std::scoped_lock lock(impl_->mu);
  impl_->prototypes_[prototype->BackendId()] = std::move(prototype);
}

// 实现 CameraManager::ListBackendIds：遍历原型表键。
std::vector<std::string> CameraManager::ListBackendIds() const {
  std::scoped_lock lock(impl_->mu);
  std::vector<std::string> out;
  out.reserve(impl_->prototypes_.size());
  for (const auto& kv : impl_->prototypes_) {
    out.push_back(kv.first);
  }
  return out;
}

// 实现 CameraManager::EnumerateAll：对每个原型拉取 EnumerateDevices。
std::vector<DeviceInfo> CameraManager::EnumerateAll() const {
  std::scoped_lock lock(impl_->mu);
  std::vector<DeviceInfo> out;
  for (const auto& kv : impl_->prototypes_) {
    const auto& proto = kv.second;
    for (const auto& dev : proto->EnumerateDevices()) {
      out.push_back(dev);
    }
  }
  return out;
}

// 实现 CameraManager::CreateAndOpenDevice：CloneForSession + Open（优先用 ip），写入 open_。
std::string CameraManager::CreateAndOpenDevice(const DeviceInfo& device, const std::string& manager_device_id) {
  std::scoped_lock lock(impl_->mu);
  std::string mid = manager_device_id;
  if (!mid.empty() && impl_->open_.count(mid) != 0) {
    CAMERA3D_LOGW("CreateAndOpenDevice: manager id 已存在 {}", mid);
    return {};
  }
  if (mid.empty()) {
    do {
      mid = "cam-" + std::to_string(impl_->seq++);
    } while (impl_->open_.count(mid) != 0);
  }

  if (device.backend_id.empty() || (device.ip.empty() && device.serial_number.empty())) {
    CAMERA3D_LOGW("CreateAndOpenDevice: DeviceInfo 缺少 backend_id，或 ip/serial_number 同时为空");
    return {};
  }

  const auto it = impl_->prototypes_.find(device.backend_id);
  if (it == impl_->prototypes_.end()) {
    CAMERA3D_LOGW("CreateAndOpenDevice: 未注册 backend {}", device.backend_id);
    return {};
  }
  std::shared_ptr<ICameraAdapter> inst = it->second->CloneForSession();
  const std::string open_key = !device.ip.empty() ? device.ip : device.serial_number;
  if (!inst->Open(open_key)) {
    CAMERA3D_LOGW("CreateAndOpenDevice: Open 失败 {}", FormatDeviceAddress(device));
    return {};
  }

  Impl::Slot slot;
  slot.manager_id = mid;
  slot.device = device;
  slot.adapter = std::move(inst);
  impl_->open_[mid] = std::move(slot);
  CAMERA3D_LOGI("CreateAndOpenDevice: manager_id={} device={}", mid, FormatDeviceAddress(device));
  return mid;
}

// 实现 CameraManager::CloseDevice：移出表后 StopAsyncGrab+Close，避免持锁调 Close。
bool CameraManager::CloseDevice(const std::string& manager_device_id) {
  std::shared_ptr<ICameraAdapter> adapter;
  {
    std::scoped_lock lock(impl_->mu);
    const auto it = impl_->open_.find(manager_device_id);
    if (it == impl_->open_.end()) {
      return false;
    }
    adapter = std::move(it->second.adapter);
    impl_->open_.erase(it);
  }
  if (adapter) {
    adapter->StopAsyncGrab();
    adapter->Close();
  }
  return true;
}

// 实现 CameraManager::CloseAllDevices：swap 出副本后逐台关闭，缩短持锁时间。
void CameraManager::CloseAllDevices() {
  std::unordered_map<std::string, Impl::Slot> copy;
  {
    std::scoped_lock lock(impl_->mu);
    copy.swap(impl_->open_);
  }
  for (auto& kv : copy) {
    if (kv.second.adapter) {
      kv.second.adapter->StopAsyncGrab();
      kv.second.adapter->Close();
    }
  }
}

// 实现 CameraManager::ListManagedDeviceIds：收集 open_ 键。
std::vector<std::string> CameraManager::ListManagedDeviceIds() const {
  std::scoped_lock lock(impl_->mu);
  std::vector<std::string> out;
  out.reserve(impl_->open_.size());
  for (const auto& kv : impl_->open_) {
    out.push_back(kv.first);
  }
  return out;
}

// 实现 CameraManager::IsDeviceOpen：槽位存在且适配器 IsOpen。
bool CameraManager::IsDeviceOpen(const std::string& manager_device_id) const {
  std::scoped_lock lock(impl_->mu);
  const auto it = impl_->open_.find(manager_device_id);
  return it != impl_->open_.end() && it->second.adapter && it->second.adapter->IsOpen();
}

// 实现 CameraManager::TryGetAdapter：持锁查找 open_。
std::shared_ptr<ICameraAdapter> CameraManager::TryGetAdapter(const std::string& manager_device_id) const {
  std::scoped_lock lock(impl_->mu);
  const auto it = impl_->open_.find(manager_device_id);
  if (it == impl_->open_.end() || !it->second.adapter) {
    return nullptr;
  }
  return it->second.adapter;
}

// 实现 CameraManager::GrabOne：TryGetAdapter 后转调。
bool CameraManager::GrabOne(const std::string& manager_device_id, FrameBuffer& out, int timeout_ms) {
  const auto a = TryGetAdapter(manager_device_id);
  return a && a->GrabOne(out, timeout_ms);
}

// 实现 CameraManager::SetExposureUs：转调适配器。
bool CameraManager::SetExposureUs(const std::string& manager_device_id, double v) {
  const auto a = TryGetAdapter(manager_device_id);
  return a && a->SetExposureUs(v);
}

// 实现 CameraManager::GetExposureUs：转调适配器。
bool CameraManager::GetExposureUs(const std::string& manager_device_id, double& v) const {
  const auto a = TryGetAdapter(manager_device_id);
  return a && a->GetExposureUs(v);
}

// 实现 CameraManager::SetGainDb：转调适配器。
bool CameraManager::SetGainDb(const std::string& manager_device_id, double v) {
  const auto a = TryGetAdapter(manager_device_id);
  return a && a->SetGainDb(v);
}

// 实现 CameraManager::GetGainDb：转调适配器。
bool CameraManager::GetGainDb(const std::string& manager_device_id, double& v) const {
  const auto a = TryGetAdapter(manager_device_id);
  return a && a->GetGainDb(v);
}

// 实现 CameraManager::SetGamma：转调适配器。
bool CameraManager::SetGamma(const std::string& manager_device_id, double v) {
  const auto a = TryGetAdapter(manager_device_id);
  return a && a->SetGamma(v);
}

// 实现 CameraManager::GetGamma：转调适配器。
bool CameraManager::GetGamma(const std::string& manager_device_id, double& v) const {
  const auto a = TryGetAdapter(manager_device_id);
  return a && a->GetGamma(v);
}

// 实现 CameraManager::SetTriggerMode：转调适配器。
bool CameraManager::SetTriggerMode(const std::string& manager_device_id, TriggerMode m) {
  const auto a = TryGetAdapter(manager_device_id);
  return a && a->SetTriggerMode(m);
}

// 实现 CameraManager::SetRoi：转调适配器。
bool CameraManager::SetRoi(const std::string& manager_device_id, const RoiRect& roi) {
  const auto a = TryGetAdapter(manager_device_id);
  return a && a->SetRoi(roi);
}

// 实现 CameraManager::GetRoi：转调适配器。
bool CameraManager::GetRoi(const std::string& manager_device_id, RoiRect& roi) const {
  const auto a = TryGetAdapter(manager_device_id);
  return a && a->GetRoi(roi);
}

// 实现 CameraManager::GetDeviceInfo：转调适配器。
bool CameraManager::GetDeviceInfo(const std::string& manager_device_id, DeviceInfo& out) const {
  const auto a = TryGetAdapter(manager_device_id);
  return a && a->GetDeviceInfo(out);
}

// 实现 CameraManager::SetResultCallback：转调 SetCaptureResultCallback。
void CameraManager::SetResultCallback(const std::string& manager_device_id, CameraResultCallback callback) {
  const auto a = TryGetAdapter(manager_device_id);
  if (a) {
    a->SetCaptureResultCallback(std::move(callback));
  }
}

// 实现 CameraManager::ClearResultCallback：置空结果回调。
void CameraManager::ClearResultCallback(const std::string& manager_device_id) {
  const auto a = TryGetAdapter(manager_device_id);
  if (a) {
    a->SetCaptureResultCallback(nullptr);
  }
}

// 实现 CameraManager::StartAsyncGrab：先 SetFrameCallback 再 StartAsyncGrab。
bool CameraManager::StartAsyncGrab(const std::string& manager_device_id, FrameCallback cb) {
  const auto a = TryGetAdapter(manager_device_id);
  if (!a) {
    return false;
  }
  a->SetFrameCallback(std::move(cb));
  return a->StartAsyncGrab();
}

// 实现 CameraManager::StopAsyncGrab：转调适配器。
void CameraManager::StopAsyncGrab(const std::string& manager_device_id) {
  const auto a = TryGetAdapter(manager_device_id);
  if (a) {
    a->StopAsyncGrab();
  }
}

// 实现 CameraManager::StartStreamGrab：转调适配器。
bool CameraManager::StartStreamGrab(const std::string& manager_device_id) {
  const auto a = TryGetAdapter(manager_device_id);
  return a && a->StartStreamGrab();
}

// 实现 CameraManager::GetLastErrorCode：无设备返回 kNotOpen。
CameraError CameraManager::GetLastErrorCode(const std::string& manager_device_id) const {
  const auto a = TryGetAdapter(manager_device_id);
  return a ? a->GetLastErrorCode() : CameraError::kNotOpen;
}

// 实现 CameraManager::GetLastErrorMessage：无设备返回固定英文提示。
std::string CameraManager::GetLastErrorMessage(const std::string& manager_device_id) const {
  const auto a = TryGetAdapter(manager_device_id);
  return a ? a->GetLastErrorMessage() : std::string{"no such managed device"};
}

}  // namespace camera3d::camera
