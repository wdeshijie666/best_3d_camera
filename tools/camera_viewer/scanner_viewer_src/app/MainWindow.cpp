/**
 * @file MainWindow.cpp
 * @brief 主窗口实现：Ela 控件风格、相机参数面板、MVC 衔接
 */
#include "MainWindow.h"
#include "../model/data_center/UnifiedFrame.h"
#include "../model/device_layer/DeviceInfo.h"
#include "../model/device_layer/DeviceModelTags.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QApplication>
#include <QStringListModel>
#include <QTabWidget>
#include <QPushButton>
#include <QComboBox>
#include <QFrame>
#include <QtConcurrent/QtConcurrent>

#include <ElaPushButton.h>
#include <ElaListView.h>
#include <ElaComboBox.h>
#include <ElaApplication.h>

#include "../model/device_layer/IDeviceAdapter.h"

namespace scanner_viewer {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    data_center_ = std::make_unique<DataCenter>(this);
    device_controller_ = std::make_unique<DeviceController>(this);
    acquisition_controller_ = std::make_unique<AcquisitionController>(
        device_controller_.get(), data_center_.get(), this);
    acquisition_controller_->SetAdapter(device_controller_->GetAdapter());

    search_watcher_ = new QFutureWatcher<std::vector<DeviceInfo>>(this);
    connect(search_watcher_, &QFutureWatcher<std::vector<DeviceInfo>>::finished,
            this, &MainWindow::OnSearchDevicesFinished);

    SetupUi();
    ConnectSignals();
}

MainWindow::~MainWindow() = default;

void MainWindow::SetupUi() {
    setWindowTitle(tr(u8"Best 3D 相机 Hub Viewer"));
    setMinimumSize(1100, 720);
    resize(1280, 800);

    // 简约蓝白风格：整体浅蓝背景
    setStyleSheet(
        "QMainWindow, QWidget#centralWidget { background-color: #E8F4FC; }"
        "QGroupBox { font-weight: bold; background-color: #FFFFFF; border: 1px solid #B3D9F2; "
        "  border-radius: 6px; margin-top: 8px; padding-top: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 6px; "
        "  color: #1E5F8C; background-color: #FFFFFF; }"
        "QLabel { color: #2C5282; }"
    );

    QWidget* central = new QWidget(this);
    central->setObjectName("centralWidget");
    QHBoxLayout* main_layout = new QHBoxLayout(central);
    main_layout->setSpacing(12);
    main_layout->setContentsMargins(12, 12, 12, 12);

    // 左侧：设备 + 采集 + 参数
    QVBoxLayout* left_layout = new QVBoxLayout();
    left_layout->setSpacing(12);

    QGroupBox* device_group = new QGroupBox(tr(u8"设备列表"), this);
    device_group->setStyleSheet("");  // 使用全局 GroupBox 样式
    QVBoxLayout* device_v = new QVBoxLayout(device_group);
    device_model_ = new QStringListModel(this);
    device_list_ = new ElaListView(device_group);
    device_list_->setModel(device_model_);
    device_list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    device_list_->setMinimumHeight(100);
    search_btn_ = new ElaPushButton(tr(u8"搜索设备"), this);
    connect_btn_ = new ElaPushButton(tr(u8"连接"), this);
    disconnect_btn_ = new ElaPushButton(tr(u8"断开"), this);
    disconnect_btn_->setEnabled(false);
    device_v->addWidget(device_list_);
    device_v->addWidget(search_btn_);
    device_v->addWidget(connect_btn_);
    device_v->addWidget(disconnect_btn_);
    left_layout->addWidget(device_group);

    QGroupBox* acquire_group = new QGroupBox(tr(u8"采集"), this);
    QVBoxLayout* acquire_v = new QVBoxLayout(acquire_group);
    {
        auto* lab = new QLabel(tr(u8"投采 projector_op（串口 ProductionCommand）"), acquire_group);
        lab->setStyleSheet("color: #2C5282; font-size: 12px;");
        acquire_v->addWidget(lab);
    }
    projector_op_combo_ = new ElaComboBox(acquire_group);
    projector_op_combo_->setMinimumWidth(200);
    // 与 libs/serial_port/include/serial_port/serial_projector.h 中 ProductionCommand 数值及帧格式一致
    projector_op_combo_->addItem(
        tr(u8"0 — 默认：白屏→最后一图"),
        QVariant(static_cast<unsigned>(0)));
    projector_op_combo_->setItemData(
        0,
        tr(u8"Hub：projector_op=0 时使用 kWhiteScreenToEnd\n串口帧 EB 90 01 01 01 00"),
        Qt::ToolTipRole);
    projector_op_combo_->addItem(
        tr(u8"1 — 从第 1 张→最后一图"),
        QVariant(static_cast<unsigned>(1)));
    projector_op_combo_->setItemData(
        1,
        tr(u8"kFromImage1ToEnd\n串口帧 EB 90 01 01 02 00"),
        Qt::ToolTipRole);
    projector_op_combo_->addItem(
        tr(u8"2 — 从第 2 张→第 1 张"),
        QVariant(static_cast<unsigned>(2)));
    projector_op_combo_->setItemData(
        2,
        tr(u8"kFromImage2To1\n串口帧 EB 90 01 01 03 00"),
        Qt::ToolTipRole);
    projector_op_combo_->addItem(
        tr(u8"3 — 从第 24 张→第 23 张"),
        QVariant(static_cast<unsigned>(3)));
    projector_op_combo_->setItemData(
        3,
        tr(u8"kFromImage24To23\n串口帧 EB 90 01 01 19 00"),
        Qt::ToolTipRole);
    projector_op_combo_->addItem(
        tr(u8"4 — 从 24 张中挑 8 张"),
        QVariant(static_cast<unsigned>(4)));
    projector_op_combo_->setItemData(
        4,
        tr(u8"kPick8From24\n串口帧 EB 90 01 09 01 00"),
        Qt::ToolTipRole);
    acquire_v->addWidget(projector_op_combo_);
    capture_once_btn_ = new ElaPushButton(tr(u8"单次采集"), this);
    start_continuous_btn_ = new ElaPushButton(tr(u8"连续采集"), this);
    stop_continuous_btn_ = new ElaPushButton(tr(u8"停止连续"), this);
    stop_continuous_btn_->setEnabled(false);
    acquire_v->addWidget(capture_once_btn_);
    acquire_v->addWidget(start_continuous_btn_);
    acquire_v->addWidget(stop_continuous_btn_);
    left_layout->addWidget(acquire_group);

    QGroupBox* param_group = new QGroupBox(tr(u8"相机参数"), this);
    QVBoxLayout* param_v = new QVBoxLayout(param_group);
    param_panel_ = new CameraParamPanel(this);
    param_panel_->SetDeviceController(device_controller_.get());
    param_panel_->setMinimumWidth(280);
    param_panel_->setMinimumHeight(200);
    param_v->addWidget(param_panel_);
    left_layout->addWidget(param_group, 1);

    status_label_ = new QLabel(tr(u8"就绪"), this);
    status_label_->setStyleSheet("color: #5A7A99; font-size: 12px;");
    left_layout->addWidget(status_label_);

    main_layout->addLayout(left_layout);

    // 右侧：TabWidget（2D 图像 / 点云），蓝白风格
    view_tab_widget_ = new QTabWidget(this);
    view_tab_widget_->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #B3D9F2; border-radius: 6px; "
        "  background-color: #FFFFFF; top: -1px; }"
        "QTabBar::tab { background-color: #D6ECF8; color: #1E5F8C; padding: 8px 20px; "
        "  margin-right: 2px; border: 1px solid #B3D9F2; border-bottom: none; "
        "  border-top-left-radius: 6px; border-top-right-radius: 6px; min-width: 80px; }"
        "QTabBar::tab:selected { background-color: #FFFFFF; color: #0D3B66; font-weight: bold; "
        "  border-bottom: 1px solid #FFFFFF; margin-bottom: -1px; }"
        "QTabBar::tab:hover:!selected { background-color: #B3D9F2; }"
    );

    image_view_2d_ = new ImageView2D(this);
    image_view_2d_->SetDisplayMode(ImageView2D::DisplayMode::PreferDepth);
    image_view_2d_->setStyleSheet("background-color: #F0F8FF;");
    view_tab_widget_->addTab(image_view_2d_, tr(u8"2D 图像"));

    raw_album_page_ = new RawImageAlbumPage(this);
    raw_album_page_->setStyleSheet("background-color: #F0F8FF;");
    view_tab_widget_->addTab(raw_album_page_, tr(u8"原始图像"));
    raw_album_page_->Clear();

    // 纹理页不得作为 QTabWidget 的「未入栈」子控件：否则会像独立小窗叠在主窗口上。
    // removeTab 后页面父对象为空，同样可能成小窗，故挂到始终 hide 的 holder 上。
    texture_tab_holder_ = new QWidget(central);
    texture_tab_holder_->hide();
    texture_page_ = new QWidget(texture_tab_holder_);
    auto* texture_layout = new QVBoxLayout(texture_page_);
    texture_layout->setContentsMargins(0, 0, 0, 0);
    texture_view_2d_ = new ImageView2D(texture_page_);
    texture_view_2d_->SetDisplayMode(ImageView2D::DisplayMode::PreferTexture);
    texture_view_2d_->setStyleSheet("background-color: #F0F8FF;");
    texture_layout->addWidget(texture_view_2d_, 1);

    point_cloud_page_ = new QWidget(this);
    QVBoxLayout* point_cloud_layout = new QVBoxLayout(point_cloud_page_);
    point_cloud_layout->setContentsMargins(0, 0, 0, 0);
    point_cloud_view_3d_ = new PointCloudView3D(this);
    point_cloud_layout->addWidget(point_cloud_view_3d_, 1);
    view_tab_widget_->addTab(point_cloud_page_, tr(u8"点云"));

    main_layout->addWidget(view_tab_widget_, 1);

    setCentralWidget(central);
    SyncCaptureProjectorOpToAdapter();
}

void MainWindow::ConnectSignals() {
    connect(search_btn_, &QPushButton::clicked, this, &MainWindow::OnSearchDevices);
    connect(device_list_, &QListView::clicked, this, &MainWindow::OnDeviceListItemClicked);
    connect(device_list_, &QListView::doubleClicked, this, &MainWindow::OnConnect);
    connect(connect_btn_, &QPushButton::clicked, this, &MainWindow::OnConnect);
    connect(disconnect_btn_, &QPushButton::clicked, this, &MainWindow::OnDisconnect);
    connect(capture_once_btn_, &QPushButton::clicked, this, &MainWindow::OnCaptureOnce);
    connect(start_continuous_btn_, &QPushButton::clicked, this, &MainWindow::OnStartContinuous);
    connect(stop_continuous_btn_, &QPushButton::clicked, this, &MainWindow::OnStopContinuous);
    connect(projector_op_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        SyncCaptureProjectorOpToAdapter();
    });
    connect(view_tab_widget_, &QTabWidget::currentChanged, this, [this](int) {
        SyncFrameConvertDemandToAdapter();
    });

    connect(data_center_.get(), &DataCenter::FrameReady, this, &MainWindow::OnFrameReady, Qt::QueuedConnection);
    connect(acquisition_controller_.get(), &AcquisitionController::CaptureFinished,
            this, &MainWindow::OnCaptureOnceFinished);
    connect(device_controller_.get(), &DeviceController::Connected, this, &MainWindow::OnConnected);
    connect(device_controller_.get(), &DeviceController::Disconnected, this, &MainWindow::OnDisconnected);
    connect(device_controller_.get(), &DeviceController::ConnectionFailed, this, [this](const QString& reason) {
        status_label_->setText(reason);
        UpdateConnectionButtonState();
        QMessageBox::warning(this, tr(u8"连接失败"), reason);
    });
}

void MainWindow::OnSearchDevices() {
    if (search_watcher_->isRunning())
        return;
    status_label_->setText(tr(u8"正在搜索设备…"));
    search_watcher_->setFuture(QtConcurrent::run(DeviceController::SearchDevicesReturnList, 2000));
}

void MainWindow::OnSearchDevicesFinished() {
    std::vector<DeviceInfo> list = search_watcher_->result();
    device_controller_->SetDeviceList(std::move(list));
    QStringList display;
    for (const auto& d : device_controller_->DeviceList()) {
        const QString ip = QString::fromStdString(d.ip);
        const QString model = QString::fromStdString(d.model_name);
        display << QString("%1 [%2] %3").arg(QString::fromStdString(d.name), model, ip.isEmpty() ? QString(u8"-") : ip);
    }
    static_cast<QStringListModel*>(device_model_)->setStringList(display);
    status_label_->setText(tr(u8"搜索完成，共 %1 台设备").arg(display.size()));
}

void MainWindow::OnDeviceListItemClicked(const QModelIndex& index) {
    Q_UNUSED(index);
    UpdateConnectionButtonState();
}

void MainWindow::OnConnect() {
    QModelIndex idx = device_list_->currentIndex();
    const auto& list = device_controller_->DeviceList();
    int row = idx.isValid() ? idx.row() : -1;
    if (row < 0 || static_cast<size_t>(row) >= list.size()) return;
    connect_btn_->setEnabled(false);
    status_label_->setText(tr(u8"正在连接…"));
    device_controller_->ConnectDevice(list[static_cast<size_t>(row)], 5000);
}

void MainWindow::OnDisconnect() {
    device_controller_->DisconnectDevice();
}

void MainWindow::OnCaptureOnce() {
    capture_once_btn_->setEnabled(false);
    status_label_->setText(tr(u8"单次采集中…"));
    acquisition_controller_->CaptureOnce();
}

void MainWindow::OnCaptureOnceFinished(bool success) {
    capture_once_btn_->setEnabled(true);
    status_label_->setText(success ? tr(u8"单次采集完成") : tr(u8"单次采集失败"));
}

void MainWindow::OnStartContinuous() {
    acquisition_controller_->StartContinuous();
    if (point_cloud_view_3d_ && acquisition_controller_->IsContinuousRunning())
        point_cloud_view_3d_->SetContinuousPointCloudAcquisition(true);
    start_continuous_btn_->setEnabled(false);
    stop_continuous_btn_->setEnabled(true);
}

void MainWindow::OnStopContinuous() {
    acquisition_controller_->StopContinuous();
    if (point_cloud_view_3d_) point_cloud_view_3d_->SetContinuousPointCloudAcquisition(false);
    start_continuous_btn_->setEnabled(true);
    stop_continuous_btn_->setEnabled(false);
}

void MainWindow::OnFrameReady() {
    UnifiedFrame frame;
    if (!data_center_->GetLatestFrame(frame)) return;

    const bool image_tab_visible =
        view_tab_widget_ &&
        image_view_2d_ &&
        (view_tab_widget_->currentWidget() == image_view_2d_) &&
        image_view_2d_->isVisibleTo(this);
    if (image_tab_visible) {
        image_view_2d_->SetFrame(frame);
    }

    // 原始图像页须与数据中心同步：勿依赖当前 Tab，否则在 2D/点云页单次采集后切到「原始图像」仍为空。
    if (raw_album_page_) {
        raw_album_page_->SetFrame(frame);
    }

    const bool texture_tab_visible =
        view_tab_widget_ &&
        texture_page_ &&
        texture_view_2d_ &&
        (view_tab_widget_->indexOf(texture_page_) >= 0) &&
        (view_tab_widget_->currentWidget() == texture_page_) &&
        texture_page_->isVisibleTo(this);
    if (texture_tab_visible) {
        texture_view_2d_->SetFrame(frame);
    }

    const bool point_tab_visible =
        view_tab_widget_ &&
        point_cloud_page_ &&
        point_cloud_view_3d_ &&
        (view_tab_widget_->currentWidget() == point_cloud_page_) &&
        point_cloud_view_3d_->isVisibleTo(this);
    if (point_tab_visible && !frame.point_cloud.empty()) {
        point_cloud_view_3d_->SetFrame(frame);
    }
}

void MainWindow::UpdateConnectionButtonState() {
    if (device_controller_->IsConnected()) {
        connect_btn_->setEnabled(false);
        disconnect_btn_->setEnabled(true);
    } else {
        connect_btn_->setEnabled(device_list_->currentIndex().isValid());
        disconnect_btn_->setEnabled(false);
    }
}

void MainWindow::OnConnected() {
    status_label_->setText(tr(u8"已连接"));
    acquisition_controller_->SetAdapter(device_controller_->GetAdapter());
    UpdateConnectionButtonState();
    UpdateCaptureOnceButtonVisibility();
    if (capture_once_btn_->isVisible()) capture_once_btn_->setEnabled(true);
    start_continuous_btn_->setEnabled(true);
    param_panel_->RefreshParameterList();
    UpdateTextureTabByCurrentDevice();
    SyncFrameConvertDemandToAdapter();
    SyncCaptureProjectorOpToAdapter();
}

void MainWindow::OnDisconnected() {
    status_label_->setText(tr(u8"已断开"));
    if (point_cloud_view_3d_) point_cloud_view_3d_->SetContinuousPointCloudAcquisition(false);
    UpdateConnectionButtonState();
    capture_once_btn_->setVisible(true);
    capture_once_btn_->setEnabled(false);
    start_continuous_btn_->setEnabled(false);
    stop_continuous_btn_->setEnabled(false);
    param_panel_->RefreshParameterList();
    if (acquisition_controller_->IsContinuousRunning())
        acquisition_controller_->StopContinuous();
    DetachTextureTabFromStack();
    if (texture_view_2d_) texture_view_2d_->Clear();
    if (raw_album_page_) raw_album_page_->Clear();
    SyncFrameConvertDemandToAdapter();
}

void MainWindow::UpdateTextureTabByCurrentDevice() {
    if (!view_tab_widget_ || !texture_page_ || !texture_view_2d_) return;
    (void)kDeviceModelHikvisionMv3d;
    (void)kDeviceModelASeries;
    (void)kDeviceModelBestCameraHub;
    // Best-CameraHub 当前仅深度 SHM；纹理页保留给未来扩展或其它型号分支。
    DetachTextureTabFromStack();
    SyncFrameConvertDemandToAdapter();
}

IDeviceAdapter::FrameConvertDemand MainWindow::BuildCurrentFrameConvertDemand() const {
    IDeviceAdapter::FrameConvertDemand demand{};
    const QWidget* current = view_tab_widget_ ? view_tab_widget_->currentWidget() : nullptr;
    if (!current) {
        demand.need_depth = true;
        return demand;
    }
    if (current == image_view_2d_ || current == raw_album_page_) {
        demand.need_depth = true;
    } else if (current == texture_page_) {
        demand.need_texture = true;
    } else if (current == point_cloud_page_) {
        demand.need_point_cloud = true;
    } else {
        demand.need_depth = true;
    }
    return demand;
}

void MainWindow::SyncFrameConvertDemandToAdapter() {
    if (!device_controller_) return;
    IDeviceAdapter* adapter = device_controller_->GetAdapter();
    if (!adapter) return;
    adapter->SetFrameConvertDemand(BuildCurrentFrameConvertDemand());
}

void MainWindow::SyncCaptureProjectorOpToAdapter() {
    if (!projector_op_combo_ || !device_controller_) return;
    IDeviceAdapter* adapter = device_controller_->GetAdapter();
    if (!adapter) return;
    const QVariant v = projector_op_combo_->currentData(Qt::UserRole);
    const std::uint32_t op = v.isValid() ? static_cast<std::uint32_t>(v.toUInt()) : 0u;
    adapter->SetCaptureProjectorOp(op);
}

void MainWindow::UpdateCaptureOnceButtonVisibility() {
    if (!capture_once_btn_) return;
    DeviceInfo info;
    const bool hik_mv3d =
        device_controller_ && device_controller_->GetCurrentDeviceInfo(info) &&
        info.model_name == kDeviceModelHikvisionMv3d;
    capture_once_btn_->setVisible(!hik_mv3d);
}

void MainWindow::DetachTextureTabFromStack() {
    if (!view_tab_widget_ || !texture_page_ || !texture_tab_holder_) return;
    const int idx = view_tab_widget_->indexOf(texture_page_);
    if (idx < 0) return;
    view_tab_widget_->removeTab(idx);
    texture_page_->setParent(texture_tab_holder_);
    // 不调用 texture_page_->hide()：避免再次 insertTab 后切换页签时仍保持隐藏。
    // holder 已 hide，子树不会绘制到屏幕上。
}

}  // namespace scanner_viewer
