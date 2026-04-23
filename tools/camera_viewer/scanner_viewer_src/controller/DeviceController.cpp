/**
 * @file DeviceController.cpp
 * @brief 设备控制：UDP 发现 Best-CameraHub，连接 BestCamera3DAdapter。
 */
#include "DeviceController.h"

#include "../model/device_layer/BestCamera3DAdapter.h"

namespace scanner_viewer {

namespace {

std::unique_ptr<IDeviceAdapter> MakeAdapterFor(const DeviceInfo* info) {
  (void)info;
  return std::make_unique<BestCamera3DAdapter>();
}

}  // namespace

DeviceController::DeviceController(QObject* parent) : QObject(parent) {
  adapter_ = std::make_unique<BestCamera3DAdapter>();
}

DeviceController::~DeviceController() = default;

void DeviceController::SearchDevices(int timeout_ms) { SetDeviceList(SearchDevicesReturnList(timeout_ms)); }

std::vector<DeviceInfo> DeviceController::SearchDevicesReturnList(int timeout_ms) {
  return BestCamera3DAdapter::EnumerateDevices(timeout_ms);
}

void DeviceController::SetDeviceList(std::vector<DeviceInfo> list) {
  device_list_ = std::move(list);
  emit DeviceListUpdated();
}

void DeviceController::ConnectDevice(const DeviceInfo& info, unsigned int timeout_ms) {
  if (!adapter_) {
    emit ConnectionFailed(tr(u8"内部错误：适配器未初始化。"));
    return;
  }
  if (adapter_->IsConnected()) {
    adapter_->Disconnect();
  }
  adapter_ = MakeAdapterFor(&info);
  if (!adapter_) {
    emit ConnectionFailed(tr(u8"无法创建设备适配器。"));
    return;
  }
  if (adapter_->Connect(&info, timeout_ms)) {
    emit Connected();
  } else {
    emit ConnectionFailed(tr(u8"连接失败，请检查 Hub 是否运行及网络。"));
  }
}

void DeviceController::DisconnectDevice() {
  if (adapter_) adapter_->Disconnect();
  emit Disconnected();
}

bool DeviceController::IsConnected() const { return adapter_ && adapter_->IsConnected(); }

bool DeviceController::GetCurrentDeviceInfo(DeviceInfo& out) const {
  return adapter_ && adapter_->GetDeviceInfo(out);
}

const std::vector<DeviceInfo>& DeviceController::DeviceList() const { return device_list_; }

IDeviceAdapter* DeviceController::GetAdapter() { return adapter_.get(); }

}  // namespace scanner_viewer
