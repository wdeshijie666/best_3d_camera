/**
 * @file MainWindow.h
 * @brief 主窗口：Ela 风格、设备列表、采集、相机参数面板、2D/3D 视窗
 */
#ifndef SCANNER_VIEWER_MAIN_WINDOW_H
#define SCANNER_VIEWER_MAIN_WINDOW_H

#include "../model/data_center/DataCenter.h"
#include "../view/viewer_core/ImageView2D.h"
#include "../view/raw_album/RawImageAlbumPage.h"
#include "../view/viewer_core/PointCloudView3D.h"
#include "../view/param_ui/CameraParamPanel.h"
#include "../controller/DeviceController.h"
#include "../controller/AcquisitionController.h"
#include <QMainWindow>
#include <QTabWidget>
#include <QLabel>
#include <QFutureWatcher>
#include <memory>
#include <vector>

#include <ElaPushButton.h>

class QListView;
class QAbstractItemModel;
class QWidget;
class ElaComboBox;

namespace scanner_viewer {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void OnSearchDevices();
    void OnSearchDevicesFinished();
    void OnDeviceListItemClicked(const QModelIndex& index);
    void OnConnect();
    void OnDisconnect();
    void OnCaptureOnce();
    void OnCaptureOnceFinished(bool success);
    void OnStartContinuous();
    void OnStopContinuous();
    void OnFrameReady();
    void OnConnected();
    void OnDisconnected();

private:
    void SetupUi();
    void ConnectSignals();
    /** 根据当前连接状态与列表选中项，统一更新连接/断开按钮的可用状态 */
    void UpdateConnectionButtonState();
    /** 仅在指定品牌相机连接后显示纹理图页签。 */
    void UpdateTextureTabByCurrentDevice();
    /** 海康 MV3D 无单次采集能力时隐藏对应按钮。 */
    void UpdateCaptureOnceButtonVisibility();
    /** 根据当前可视 Tab 计算适配器需要转换的数据类型。 */
    IDeviceAdapter::FrameConvertDemand BuildCurrentFrameConvertDemand() const;
    /** 将可视状态同步给当前适配器，减少无用转换。 */
    void SyncFrameConvertDemandToAdapter();
    /** 将「投采 projector_op」下拉框当前值写入适配器（Hub Capture 透传）。 */
    void SyncCaptureProjectorOpToAdapter();
    /** 从 Tab 移除纹理页并挂回隐藏容器，避免 removeTab 后无父窗口成小窗置顶。 */
    void DetachTextureTabFromStack();

    std::unique_ptr<DataCenter> data_center_;
    std::unique_ptr<DeviceController> device_controller_;
    std::unique_ptr<AcquisitionController> acquisition_controller_;

    QListView* device_list_{nullptr};
    QAbstractItemModel* device_model_{nullptr};
    ElaPushButton* search_btn_{nullptr};
    ElaPushButton* connect_btn_{nullptr};
    ElaPushButton* disconnect_btn_{nullptr};
    ElaComboBox* projector_op_combo_{nullptr};
    ElaPushButton* capture_once_btn_{nullptr};
    ElaPushButton* start_continuous_btn_{nullptr};
    ElaPushButton* stop_continuous_btn_{nullptr};
    QLabel* status_label_{nullptr};
    CameraParamPanel* param_panel_{nullptr};
    QTabWidget* view_tab_widget_{nullptr};
    QWidget* point_cloud_page_{nullptr};
    /** 隐藏父控件：纹理页在未插入 Tab 或 removeTab 后挂在此下，保证不可见。 */
    QWidget* texture_tab_holder_{nullptr};
    /** 纹理图页（内容区），由 insertTab 时交给 QTabWidget 管理。 */
    QWidget* texture_page_{nullptr};
    ImageView2D* image_view_2d_{nullptr};
    RawImageAlbumPage* raw_album_page_{nullptr};
    ImageView2D* texture_view_2d_{nullptr};
    PointCloudView3D* point_cloud_view_3d_{nullptr};
    QFutureWatcher<std::vector<DeviceInfo>>* search_watcher_{nullptr};
};

}  // namespace scanner_viewer

#endif  // SCANNER_VIEWER_MAIN_WINDOW_H
