/**
 * @file DataCenter.h
 * @brief 数据中心：统一接收、缓存、分发帧数据，供 2D/3D 视窗消费；主线程通过信号投递
 */
#ifndef SCANNER_VIEWER_DATA_CENTER_H
#define SCANNER_VIEWER_DATA_CENTER_H

#include "UnifiedFrame.h"
#include <QObject>
#include <QMutex>
#include <QQueue>
#include <memory>

namespace scanner_viewer {

class DataCenter : public QObject {
    Q_OBJECT
public:
    explicit DataCenter(QObject* parent = nullptr);
    ~DataCenter() override;

    /** 写入一帧（可从采集线程调用，内部加锁并可选投递到主线程） */
    void PushFrame(const UnifiedFrame& frame);

    /** 拉取最新一帧（主线程调用，供 View 更新） */
    bool GetLatestFrame(UnifiedFrame& out);

    /** 是否保留多帧（默认仅最新一帧） */
    void SetKeepLatestOnly(bool only_latest);

signals:
    /** 有新帧可显示（主线程收槽后调用 GetLatestFrame 取数据，避免跨线程大对象拷贝） */
    void FrameReady();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace scanner_viewer

#endif  // SCANNER_VIEWER_DATA_CENTER_H
