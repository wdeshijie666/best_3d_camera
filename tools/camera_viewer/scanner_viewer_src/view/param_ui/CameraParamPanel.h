/**
 * @file CameraParamPanel.h
 * @brief 相机参数设置面板：Ela 控件、点击参数名看说明（Tooltip）
 */
#ifndef SCANNER_VIEWER_CAMERA_PARAM_PANEL_H
#define SCANNER_VIEWER_CAMERA_PARAM_PANEL_H

#include "../../model/device_layer/IDeviceAdapter.h"
#include <QWidget>
#include <QScrollArea>
#include <vector>
#include <memory>

class QFormLayout;
class QLabel;

namespace scanner_viewer {

class DeviceController;

/** 相机参数面板：从适配器拉取参数列表，用 Ela SpinBox/CheckBox/ComboBox 展示，参数名带说明 Tooltip */
class CameraParamPanel : public QWidget {
    Q_OBJECT
public:
    explicit CameraParamPanel(QWidget* parent = nullptr);
    ~CameraParamPanel() override;

    /** 设置设备控制器，用于获取适配器与当前连接状态 */
    void SetDeviceController(DeviceController* ctrl);
    /** 连接后刷新参数列表并拉取当前值；断开后清空 */
    void RefreshParameterList();
    /** 是否仅显示非开发者参数 */
    void SetDeveloperMode(bool developer_mode);

private slots:
    void OnParamIntChanged(int value);
    void OnParamBoolChanged(int checked);
    void OnParamEnumChanged(int index);

private:
    void LoadCurrentValues();

    DeviceController* device_ctrl_{nullptr};
    QScrollArea* scroll_area_{nullptr};
    QWidget* form_widget_{nullptr};
    QFormLayout* form_layout_{nullptr};
    bool developer_mode_{false};
    std::vector<ParamMeta> param_list_;
    /** 每个参数的控件与 param id 映射（通过 property 或 map 存储） */
    struct ParamWidget {
        int param_id{0};
        QWidget* control{nullptr};
        QLabel* name_label{nullptr};
    };
    std::vector<ParamWidget> param_widgets_;
};

}  // namespace scanner_viewer

#endif  // SCANNER_VIEWER_CAMERA_PARAM_PANEL_H
