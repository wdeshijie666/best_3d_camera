/**
 * @file ASeriesAdapter.h
 * @brief AIever-A 系列面阵相机适配器：封装 AIR Scanner SDK，实现 IDeviceAdapter
 */
#ifndef SCANNER_VIEWER_ASERIES_ADAPTER_H
#define SCANNER_VIEWER_ASERIES_ADAPTER_H

#include "IDeviceAdapter.h"
#include "DeviceInfo.h"
#include <memory>
#include <mutex>
#include <atomic>

namespace air_scanner {
class Scanner3D;
}

namespace scanner_viewer {

class ASeriesAdapter : public IDeviceAdapter {
public:
    ASeriesAdapter();
    ~ASeriesAdapter() override;

    bool Connect(const DeviceInfo* device_info, unsigned int timeout_ms) override;
    void Disconnect() override;
    bool IsConnected() const override;
    bool GetDeviceInfo(DeviceInfo& out) const override;

    bool GetParameterList(std::vector<ParamMeta>& out) const override;
    bool GetParameter(int id, std::vector<int>& values) const override;
    bool SetParameter(int id, const std::vector<int>& values) override;

    bool CaptureSync(UnifiedFrame& frame) override;
    bool StartAsyncCapture(FrameCallback callback) override;
    void StopAsyncCapture() override;
    bool IsAsyncCapturing() const override;

private:
    /** 将 SDK AIRFrame 转为 UnifiedFrame */
    void ConvertFrame(const void* air_frame, UnifiedFrame& out);

    std::unique_ptr<air_scanner::Scanner3D> scanner_;
    mutable std::mutex mutex_;
    FrameCallback async_callback_;
    std::atomic<bool> async_running_{false};
};

}  // namespace scanner_viewer

#endif  // SCANNER_VIEWER_ASERIES_ADAPTER_H
