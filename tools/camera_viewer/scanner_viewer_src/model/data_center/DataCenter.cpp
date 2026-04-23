/**
 * @file DataCenter.cpp
 * @brief 数据中心实现：线程安全队列 + 主线程信号
 */
#include "DataCenter.h"
#include <QMutexLocker>

namespace scanner_viewer {

struct DataCenter::Impl {
    QMutex mutex;
    QQueue<UnifiedFrame> queue_;
    static const int kMaxQueueSize = 3;
    bool keep_latest_only_ = true;
};

DataCenter::DataCenter(QObject* parent) : QObject(parent), impl_(std::make_unique<Impl>()) {}

DataCenter::~DataCenter() = default;

void DataCenter::PushFrame(const UnifiedFrame& frame) {
    QMutexLocker lock(&impl_->mutex);
    if (impl_->keep_latest_only_) {
        impl_->queue_.clear();
    }
    impl_->queue_.enqueue(frame);
    while (impl_->queue_.size() > impl_->kMaxQueueSize) {
        impl_->queue_.dequeue();
    }
    // 无参信号，主线程槽里再 GetLatestFrame 取数据，避免跨线程大对象拷贝
    emit FrameReady();
}

bool DataCenter::GetLatestFrame(UnifiedFrame& out) {
    QMutexLocker lock(&impl_->mutex);
    if (impl_->queue_.isEmpty()) return false;
    out = impl_->queue_.back();
    return true;
}

void DataCenter::SetKeepLatestOnly(bool only_latest) {
    QMutexLocker lock(&impl_->mutex);
    impl_->keep_latest_only_ = only_latest;
}

}  // namespace scanner_viewer
