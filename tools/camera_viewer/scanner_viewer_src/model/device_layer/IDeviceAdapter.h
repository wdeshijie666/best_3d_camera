/**
 * @file IDeviceAdapter.h
 * @brief 设备抽象接口：连接、参数、同步/异步采集，供多种 3D 相机型号实现
 * 遵循 3d-scanner-viewer-dedesign 技能，低耦合、可拓展
 */
#ifndef SCANNER_VIEWER_IDEVICE_ADAPTER_H
#define SCANNER_VIEWER_IDEVICE_ADAPTER_H

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>
#include <mutex>

namespace scanner_viewer {

// 前向声明：统一帧与设备信息在 data_center / DeviceInfo 中定义
struct UnifiedFrame;
struct DeviceInfo;

/** 参数控件类型：决定调参界面用 SpinBox / CheckBox / ComboBox */
enum class ParamKind {
    Int,   // 整数，用 SpinBox
    Bool,  // 0/1，用 CheckBox
    Enum   // 枚举，用 ComboBox，选项见 enum_labels
};

/** 参数元数据：名称、说明、范围、单位，用于调参界面与“点击参数名看说明” */
struct ParamMeta {
    int id{0};
    std::string name;
    std::string description;
    std::string unit;
    int min_val{0};
    int max_val{0};
    bool developer_only{false};  // 仅开发者模式显示
    ParamKind kind{ParamKind::Int};
    /** 枚举项显示名称（kind==Enum 时使用） */
    std::vector<std::string> enum_labels;
    /** 枚举项对应的 SDK 取值；若为空则使用 index 作为值 */
    std::vector<int> enum_values;
};

/** 设备抽象接口 */
class IDeviceAdapter {
public:
    /** 由视图层下发的“当前需要转换哪些数据”的统一开关。 */
    struct FrameConvertDemand {
        bool need_depth{true};
        bool need_texture{false};
        bool need_point_cloud{false};
    };

    virtual ~IDeviceAdapter() = default;

    /** 连接：device_info 可为空则使用默认/上次 */
    virtual bool Connect(const DeviceInfo* device_info, unsigned int timeout_ms) = 0;
    virtual void Disconnect() = 0;
    virtual bool IsConnected() const = 0;

    /** 设备信息（当前连接或枚举得到） */
    virtual bool GetDeviceInfo(DeviceInfo& out) const = 0;

    /** 参数：获取当前型号支持的参数列表；get/set 单参数 */
    virtual bool GetParameterList(std::vector<ParamMeta>& out) const = 0;
    virtual bool GetParameter(int id, std::vector<int>& values) const = 0;
    virtual bool SetParameter(int id, const std::vector<int>& values) = 0;

    /** 同步采集：阻塞直到一帧就绪，结果写入 frame，返回是否成功 */
    virtual bool CaptureSync(UnifiedFrame& frame) = 0;

    /**
     * Hub 路径：`CaptureSync`/`CaptureAsync` 透传至 SDK 的 projector_op（与 proto 一致）。
     * 0 表示 Hub 使用默认投采命令（见 Hub 实现）；其它值对应 serial_port::ProductionCommand 数值。
     * 非 Hub 适配器可忽略。
     */
    virtual void SetCaptureProjectorOp(std::uint32_t op) { (void)op; }
    virtual std::uint32_t CaptureProjectorOp() const { return 0; }

    /** 异步采集：启停流；帧通过 callback 回调（可能在非主线程） */
    using FrameCallback = std::function<void(const UnifiedFrame&)>;
    virtual bool StartAsyncCapture(FrameCallback callback) = 0;
    virtual void StopAsyncCapture() = 0;
    virtual bool IsAsyncCapturing() const = 0;

    /** MainWindow 视窗可见状态同步到适配器，控制后续帧转换开销。 */
    void SetFrameConvertDemand(const FrameConvertDemand& demand) {
        std::lock_guard<std::mutex> lock(demand_mutex_);
        frame_convert_demand_ = demand;
    }
    FrameConvertDemand GetFrameConvertDemand() const {
        std::lock_guard<std::mutex> lock(demand_mutex_);
        return frame_convert_demand_;
    }

protected:
    bool NeedDepthConversion() const { return GetFrameConvertDemand().need_depth; }
    bool NeedTextureConversion() const { return GetFrameConvertDemand().need_texture; }
    bool NeedPointCloudConversion() const { return GetFrameConvertDemand().need_point_cloud; }

private:
    mutable std::mutex demand_mutex_;
    FrameConvertDemand frame_convert_demand_{};
};

}  // namespace scanner_viewer

#endif  // SCANNER_VIEWER_IDEVICE_ADAPTER_H
