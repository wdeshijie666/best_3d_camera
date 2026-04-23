/**
 * @file DeviceController.h
 * @brief 设备控制：搜索、连接、断开，不直接操作 View
 */
#ifndef SCANNER_VIEWER_DEVICE_CONTROLLER_H
#define SCANNER_VIEWER_DEVICE_CONTROLLER_H

#include "../model/device_layer/IDeviceAdapter.h"
#include "../model/device_layer/DeviceInfo.h"
#include <QObject>
#include <memory>
#include <vector>

namespace scanner_viewer {

class DeviceController : public QObject {
    Q_OBJECT
public:
    explicit DeviceController(QObject* parent = nullptr);
    ~DeviceController() override;

    /** 枚举当前可用设备（如 A 系列局域网发现），阻塞调用 */
    void SearchDevices(int timeout_ms = 2000);
    /** 供异步搜索使用：在后台线程中调用，返回设备列表，不修改内部列表 */
    static std::vector<DeviceInfo> SearchDevicesReturnList(int timeout_ms);
    /** 设置设备列表并发出 DeviceListUpdated（主线程在异步搜索完成后调用） */
    void SetDeviceList(std::vector<DeviceInfo> list);
    /** 连接指定设备 */
    void ConnectDevice(const DeviceInfo& info, unsigned int timeout_ms = 5000);
    void DisconnectDevice();
    bool IsConnected() const;
    bool GetCurrentDeviceInfo(DeviceInfo& out) const;

    /** 当前枚举到的设备列表 */
    const std::vector<DeviceInfo>& DeviceList() const;

    /** 获取当前适配器，供 AcquisitionController 设置采集用（A 系列或海康 MV3D，由 model_name 决定）。 */
    IDeviceAdapter* GetAdapter();

signals:
    void DeviceListUpdated();
    void Connected();
    void Disconnected();
    void ConnectionFailed(const QString& reason);

private:
    std::unique_ptr<IDeviceAdapter> adapter_;
    std::vector<DeviceInfo> device_list_;
};

}  // namespace scanner_viewer

#endif  // SCANNER_VIEWER_DEVICE_CONTROLLER_H
