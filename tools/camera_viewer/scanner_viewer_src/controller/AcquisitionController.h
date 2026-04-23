/**
 * @file AcquisitionController.h
 * @brief 采集控制：同步/异步采集，将数据写入数据中心，由 View 通过数据中心取帧显示
 */
#ifndef SCANNER_VIEWER_ACQUISITION_CONTROLLER_H
#define SCANNER_VIEWER_ACQUISITION_CONTROLLER_H

#include "../model/data_center/DataCenter.h"
#include "../model/device_layer/IDeviceAdapter.h"
#include <QObject>
#include <memory>

namespace scanner_viewer {

class DeviceController;

class AcquisitionController : public QObject {
    Q_OBJECT
public:
    explicit AcquisitionController(DeviceController* device_ctrl, DataCenter* data_center, QObject* parent = nullptr);
    ~AcquisitionController() override;

    /** 同步采集一帧并写入数据中心 */
    void CaptureOnce();
    /** 异步连续采集：启/停 */
    void StartContinuous();
    void StopContinuous();
    bool IsContinuousRunning() const;

    DataCenter* GetDataCenter() const { return data_center_; }

    /** 设置当前用于采集的设备适配器（由 MainWindow 从 DeviceController 获取并设置） */
    void SetAdapter(IDeviceAdapter* adapter);

signals:
    void CaptureFinished(bool success);
    void ContinuousStarted();
    void ContinuousStopped();

private:
    DeviceController* device_ctrl_;
    DataCenter* data_center_;
    IDeviceAdapter* adapter_{nullptr};
    bool continuous_running_{false};
};

}  // namespace scanner_viewer

#endif  // SCANNER_VIEWER_ACQUISITION_CONTROLLER_H
