/**
 * @file HikvisionAdapter.h
 * @brief 海康威视 MV3D RGB-D（HiViewer）相机适配器：实现 IDeviceAdapter，与 A 系列并列使用。
 *
 * 设备枚举、打开设备、取流与 SimpleView_Point3D 等官方 C 例程一致；深度转点云优先使用帧内点云图，
 * 否则调用 MV3D_RGBD_MapDepthToPointCloud。
 */
#ifndef SCANNER_VIEWER_HIKVISION_ADAPTER_H
#define SCANNER_VIEWER_HIKVISION_ADAPTER_H

#include "IDeviceAdapter.h"
#include "DeviceInfo.h"
#include <atomic>
#include <mutex>
#include <thread>

namespace scanner_viewer {

/**
 * 与 DeviceInfo::model_name 匹配，用于 DeviceController 在连接时选择本适配器。
 * 搜索列表中由 EnumerateDevices() 写入该标记。
 */
class HikvisionAdapter : public IDeviceAdapter {
public:
    /** 写入 DeviceInfo::model_name，勿与 "A-Series" 冲突。 */
    static constexpr const char* kDeviceModelTag = "Hikvision-MV3D";

    HikvisionAdapter();
    ~HikvisionAdapter() override;

    static bool init();


    /**
     * 枚举网口 / USB / 虚拟设备（与 MV3D_RGBD_GetDeviceList 一致）。
     * 首次调用会执行 MV3D_RGBD_Initialize（进程内一次）。
     */
    static std::vector<DeviceInfo> EnumerateDevices();

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

    /**
     * 注册到 MV3D_RGBD_RegisterFrameCallBack 的 C 回调会转到此函数。
     * 在 SDK 内部线程执行，勿在 UI 线程直接调用。
     */
    void NotifyMv3dFrame(void* mv3d_frame_data);

private:
    /**
     * 将一帧 MV3D 数据转为 UnifiedFrame（深度、彩色、点云）；必要时用标定做深度→点云。
     * @param frame_data 指向 MV3D_RGBD_FRAME_DATA，在 .cpp 中与 SDK 头一致。
     */
    void ConvertFrameData(void* device_handle, const void* frame_data, UnifiedFrame& out);

    /** 取流未启动时调用 MV3D_RGBD_Start；已启动则直接返回 true。 */
    bool EnsureGrabbingUnlocked();

    /** 连接后缓存触发枚举的 Off/On 取值，供布尔型“触发开关”映射。 */
    void RefreshTriggerEnumMappingUnlocked() const;

    /** 从设备读取曝光/增益浮点范围，填充 ParamMeta 与 Set 时的 clamp。 */
    void UpdateFloatRangeCacheUnlocked() const;

    /** 在已持锁情况下判断当前是否为触发开（依赖 MV3D_RGBD_ENUM_TRIGGERMODE）。 */
    bool IsTriggerOnUnlocked() const;
    /** FetchFrame 拉流线程主循环（仅在 HIK_MV3D_ASYNC_USE_FETCH_LOOP=1 时使用）。 */
    void AsyncFetchLoop(void* handle_snapshot);

    void* handle_{nullptr};
    mutable std::mutex mutex_;
    FrameCallback async_callback_;
    std::atomic<bool> async_running_{false};
    std::thread async_fetch_thread_;

    bool grabbing_{false};
    /** 触发枚举缓存；在 const GetParameter 中也会刷新，故为 mutable。 */
    mutable bool trigger_enum_ready_{false};
    mutable uint32_t trigger_value_off_{0};
    mutable uint32_t trigger_value_on_{1};

    /** 曝光、增益的浮点范围（来自 MV3D_RGBD_GetParam），供界面 SpinBox 边界与 Set 时 clamp。 */
    mutable float exposure_min_{1.f};
    mutable float exposure_max_{100000.f};
    mutable float gain_min_{0.f};
    mutable float gain_max_{24.f};
};

}  // namespace scanner_viewer

#endif  // SCANNER_VIEWER_HIKVISION_ADAPTER_H
