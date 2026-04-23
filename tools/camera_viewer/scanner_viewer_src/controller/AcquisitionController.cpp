/**
 * @file AcquisitionController.cpp
 * @brief 采集控制实现：从 DeviceController 取适配器，同步/异步采集写入数据中心
 */
#include "AcquisitionController.h"
#include "../model/device_layer/IDeviceAdapter.h"
#include "../model/data_center/UnifiedFrame.h"
#include <QMetaObject>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>

namespace scanner_viewer {

// DeviceController 持有 adapter_ 但未暴露；这里通过增加 DeviceController::GetAdapter() 或让 AcquisitionController 持有同一 adapter。
// 为保持低耦合，让 DeviceController 提供“当前适配器”接口（返回 IDeviceAdapter*），或 AcquisitionController 在构造时由 MainWindow 传入 adapter。
// 简化：MainWindow 同时持有 DeviceController 和 AcquisitionController，并在一处设置“当前适配器”给 AcquisitionController 使用。
// 更简：AcquisitionController 持有 DeviceController，DeviceController 增加 IDeviceAdapter* GetAdapter()，这样 AcquisitionController 可拿到 adapter 做采集。
// 实现：在 DeviceController 里增加 GetAdapter() 返回 IDeviceAdapter*。

AcquisitionController::AcquisitionController(DeviceController* device_ctrl, DataCenter* data_center, QObject* parent)
    : QObject(parent), device_ctrl_(device_ctrl), data_center_(data_center) {
    // adapter 由 MainWindow 通过 SetAdapter 设置，避免 DeviceController 暴露内部
}

AcquisitionController::~AcquisitionController() {
    StopContinuous();
}

void AcquisitionController::CaptureOnce() {
    if (!adapter_ || !adapter_->IsConnected() || !data_center_) {
        emit CaptureFinished(false);
        return;
    }
    // 在后台线程做同步采集，避免阻塞 UI.
    QFutureWatcher<bool>* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher]() {
        bool ok = watcher->result();
        emit CaptureFinished(ok);
        watcher->deleteLater();
    });
    IDeviceAdapter* a = adapter_;
    DataCenter* dc = data_center_;
    QFuture<bool> future = QtConcurrent::run([a, dc]() {
        UnifiedFrame frame;
        if (!a->CaptureSync(frame)) return false;
        dc->PushFrame(frame);
        return true;
    });
    watcher->setFuture(future);
}

void AcquisitionController::StartContinuous() {
    if (!adapter_ || !adapter_->IsConnected() || !data_center_ || continuous_running_) return;
    bool ok = adapter_->StartAsyncCapture([this](const UnifiedFrame& frame) {
        data_center_->PushFrame(frame);
    });
    if (ok) {
        continuous_running_ = true;
        emit ContinuousStarted();
    }
}

void AcquisitionController::StopContinuous() {
    if (!adapter_) return;
    adapter_->StopAsyncCapture();
    continuous_running_ = false;
    emit ContinuousStopped();
}

bool AcquisitionController::IsContinuousRunning() const {
    return adapter_ ? adapter_->IsAsyncCapturing() : false;
}

void AcquisitionController::SetAdapter(IDeviceAdapter* adapter) {
    if (continuous_running_) StopContinuous();
    adapter_ = adapter;
}

}  // namespace scanner_viewer
