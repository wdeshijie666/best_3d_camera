/**
 * @file PointCloudView3D.cpp
 * @brief 3D 点云视窗：多渲染模式、深度范围、右上角透明悬浮控件
 *
 * @section vtk_perf VTK 连续刷新性能策略（与界面线程解耦）
 * - 重 CPU 部分（UnifiedPoint → vtkPoints / 颜色与 Z 标量 / Blend 逐点 LUT 混合）在 @ref PointCloudBuildPool
 *   单线程池中执行，避免阻塞 Qt 主线程导致界面卡顿。
 * - QVTK / vtkRenderWindow::Render()、Actor/Mapper 的创建与连接仅在主线程执行（见 ApplyPreparedPointCloud），
 *   符合 OpenGL 上下文与 Qt GUI 的线程约束。
 * - cloud_build_seq_ 与每帧携带的 seq 用于丢弃过期结果：高帧率下只应用“当前仍有效”的一帧，避免排队积压。
 * - 拓扑上使用单个 polyvertex（一次 InsertNextCell(n, ids)）替代每点一个 vertex cell，降低 vtkCellArray 体量。
 */
#include "PointCloudView3D.h"
#include "RangeSlider.h"
#include <ElaComboBox.h>
#include <ElaPushButton.h>
#include <ElaSlider.h>
#include <ElaSpinBox.h>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QResizeEvent>
#include <QAbstractItemView>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QFrame>
#include <QPushButton>
#include <QSpinBox>
#include <QPainter>
#include <QLinearGradient>
#include <QImage>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>
#include <cmath>
#include <algorithm>
#include <limits>
#include <numeric>

#include <vtkAutoInit.h>
VTK_MODULE_INIT(vtkRenderingOpenGL2);
VTK_MODULE_INIT(vtkInteractionStyle);

#include <QVTKOpenGLNativeWidget.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkPolyData.h>
#include <vtkUnsignedCharArray.h>
#include <vtkFloatArray.h>
#include <vtkPointData.h>
#include <vtkActor.h>
#include <vtkMapper.h>
#include <vtkPolyDataMapper.h>
#include <vtkLookupTable.h>
#include <vtkSmartPointer.h>
#include <vtkProperty.h>

#include <vtkAxesActor.h>
#include <vtkOrientationMarkerWidget.h>

namespace scanner_viewer {

namespace {

/**
 * @brief 专用于点云 vtkPolyData 构建的线程池（进程内单例）。
 *
 * 设为 maxThreadCount(1) 的原因：
 * - 若用默认全局线程池且每帧 submit 一个任务，多核会并发执行多帧的完整构建，CPU 被瞬间打满，
 *   而主线程仍只能按序应用其中少数几帧，大量计算被浪费。
 * - 串行执行时，队列中靠前的任务完成后若 seq 已过期，ApplyPreparedPointCloud 会直接 return，
 *   与“只关心最新帧”的策略一致；同时最多只有一个构建任务在跑，CPU 占用更平滑。
 *
 * 与 cloud_build_seq_ 的配合：过期帧在“应用阶段”丢弃；本池负责限制“构建阶段”的并发度。
 */
QThreadPool* PointCloudBuildPool() {
    static QThreadPool pool;
    static bool inited = false;
    if (!inited) {
        pool.setMaxThreadCount(1);
        inited = true;
    }
    return &pool;
}

}  // namespace

struct PointCloudView3D::Impl {
    QPointer<QVTKOpenGLNativeWidget> vtk_widget;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> render_window;
    vtkSmartPointer<vtkRenderer> renderer;
    vtkSmartPointer<vtkActor> points_actor;
    vtkSmartPointer<vtkLookupTable> lut_jet;   // Z 值彩色
    vtkSmartPointer<vtkLookupTable> lut_gray;   // Z 值灰度
    QImage colorbar_image;  // 256 高渐变图，用于三段式彩条中间部分
};

static vtkSmartPointer<vtkLookupTable> CreateJetLookupTable() {
    auto lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetNumberOfTableValues(256);
    lut->SetRange(0, 255);
    for (int i = 0; i < 256; ++i) {
        double t = i / 255.0;
        double r, g, b;
        if (t < 0.125) { r = 0; g = 0; b = 0.5 + 4.0 * t; }
        else if (t < 0.375) { r = 0; g = 4.0 * (t - 0.125); b = 1.0; }
        else if (t < 0.625) { r = 4.0 * (t - 0.375); g = 1.0; b = 1.0 - 4.0 * (t - 0.375); }
        else if (t < 0.875) { r = 1.0; g = 1.0 - 4.0 * (t - 0.625); b = 0; }
        else { r = 1.0 - 4.0 * (t - 0.875); g = 0; b = 0; }
        lut->SetTableValue(i, r, g, b, 1.0);
    }
    lut->Build();
    return lut;
}

static vtkSmartPointer<vtkLookupTable> CreateGrayLookupTable() {
    auto lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetNumberOfTableValues(256);
    lut->SetTableRange(0, 255);
    lut->SetSaturationRange(0, 0);
    lut->SetValueRange(0, 1);
    lut->SetRampToLinear();
    lut->Build();
    return lut;
}

PointCloudView3D::PointCloudView3D(QWidget* parent) : QWidget(parent), impl_(std::make_unique<Impl>()) {
    impl_->render_window = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    impl_->renderer = vtkSmartPointer<vtkRenderer>::New();
    impl_->renderer->SetBackground(0.7, 0.85, 1.0);
    impl_->render_window->AddRenderer(impl_->renderer);
    impl_->vtk_widget = new QVTKOpenGLNativeWidget(this);
    impl_->vtk_widget->SetRenderWindow(impl_->render_window.Get());
    impl_->render_window->GetInteractor()->SetRenderWindow(impl_->render_window);
    impl_->lut_jet = CreateJetLookupTable();
    impl_->lut_gray = CreateGrayLookupTable();

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(impl_->vtk_widget);

    CreateFloatingControls();

    vtkAxesActor* axes = vtkAxesActor::New();
    vtkOrientationMarkerWidget* widget = vtkOrientationMarkerWidget::New();
    widget->SetDefaultRenderer(impl_->renderer);
    widget->SetOrientationMarker(axes);
    widget->SetInteractor(
        impl_->vtk_widget->GetRenderWindow()->GetInteractor());
    widget->EnabledOn();
}

PointCloudView3D::~PointCloudView3D() = default;

namespace {

/**
 * @brief 将 UnifiedPoint 列表转为 vtkPolyData（几何 + 可选 RGB 标量 + 可选 Z 标量 “Elevation”）。
 *
 * 拓扑优化：使用单个 VTK_POLY_VERTEX 单元（通过 InsertNextCell(n, ids) 一次写入 n 个顶点索引），
 * 而不是对每个点调用 n 次 InsertNextCell(1, &i)。后者在百万点级别会产生巨量 cell 记录与内存/遍历开销；
 * polyvertex 在语义上仍表示“点云”，且与 vtkPolyDataMapper 显示点兼容。
 *
 * @note 本函数可在工作线程调用；不得在此函数内访问 QVTK、QWidget 或执行 Render。
 */
vtkSmartPointer<vtkPolyData> BuildPolyDataFromUnifiedPoints(
    const std::vector<UnifiedPoint>& points,
    bool use_color_for_texture,
    bool add_z_scalar
) {
    if (points.empty()) return nullptr;
    const vtkIdType n = static_cast<vtkIdType>(points.size());
    auto vtk_pts = vtkSmartPointer<vtkPoints>::New();
    vtk_pts->SetNumberOfPoints(n);
    vtkSmartPointer<vtkUnsignedCharArray> colors;
    vtkSmartPointer<vtkFloatArray> z_scalar;
    if (use_color_for_texture) {
        colors = vtkSmartPointer<vtkUnsignedCharArray>::New();
        colors->SetNumberOfComponents(3);
        colors->SetNumberOfTuples(n);
    }
    if (add_z_scalar) {
        z_scalar = vtkSmartPointer<vtkFloatArray>::New();
        z_scalar->SetNumberOfValues(n);
        z_scalar->SetName("Elevation");
    }
    for (vtkIdType i = 0; i < n; ++i) {
        const UnifiedPoint& p = points[static_cast<size_t>(i)];
        vtk_pts->SetPoint(i, p.x, p.y, p.z);
        if (colors && p.has_color) {
            unsigned char c[3] = { p.r, p.g, p.b };
            colors->SetTypedTuple(i, c);
        }
        if (z_scalar) z_scalar->SetValue(i, p.z);
    }
    auto verts = vtkSmartPointer<vtkCellArray>::New();
    std::vector<vtkIdType> ids(static_cast<size_t>(n));
    std::iota(ids.begin(), ids.end(), 0);
    // 单个 polyvertex：0..n-1 全部作为同一 cell 的顶点，替代 n 次单点 cell。
    verts->InsertNextCell(n, ids.data());
    auto poly = vtkSmartPointer<vtkPolyData>::New();
    poly->SetPoints(vtk_pts);
    poly->SetVerts(verts);
    if (colors) poly->GetPointData()->SetScalars(colors);
    if (z_scalar) poly->GetPointData()->AddArray(z_scalar);
    return poly;
}

/**
 * @brief 在工作线程中完成“与显示相关的数据准备”，生成可直接交给主线程 mapper 的 vtkPolyData。
 *
 * 包含：
 * - 调用 BuildPolyDataFromUnifiedPoints 生成几何与标量；
 * - 按 render_mode 决定是否写入 Elevation、纹理 RGB；
 * - Blend 模式：在本地构造 jet/gray LUT（与主窗口 impl_->lut_* 逻辑一致），逐点混合为 RGB 标量，
 *   主线程侧仅需 DirectScalars，无需再对 poly 做 CPU 侧改写。
 *
 * @param depth_min/max 构建时的深度范围，用于 LUT 表域（与 UI 滑条一致，避免异步与 UI 争用同一 vtkLookupTable）。
 *
 * @note 不在此函数内使用 impl_ 或任何 Qt GUI / VTK 渲染对象；LUT 为函数内临时对象。
 */
vtkSmartPointer<vtkPolyData> BuildPreparedPolyData(
    const std::vector<UnifiedPoint>& points,
    PointCloudRenderMode render_mode,
    double depth_min,
    double depth_max,
    float blend_ratio
) {
    if (points.empty()) return nullptr;
    const bool use_rgb = (render_mode == PointCloudRenderMode::Texture);
    bool has_color = false;
    for (const auto& p : points) {
        if (p.has_color) {
            has_color = true;
            break;
        }
    }
    const bool use_texture_rgb = use_rgb && has_color;
    const bool add_z = (render_mode == PointCloudRenderMode::Grayscale ||
                        render_mode == PointCloudRenderMode::ZValue ||
                        render_mode == PointCloudRenderMode::Blend);
    vtkSmartPointer<vtkPolyData> poly = BuildPolyDataFromUnifiedPoints(points, use_texture_rgb, add_z);
    if (!poly) return nullptr;

    double rmin = depth_min, rmax = depth_max;
    if (rmax <= rmin) rmax = rmin + 1.0;
    vtkSmartPointer<vtkLookupTable> lut_jet = CreateJetLookupTable();
    vtkSmartPointer<vtkLookupTable> lut_gray = CreateGrayLookupTable();
    lut_jet->SetTableRange(rmin, rmax);
    lut_jet->Build();
    lut_gray->SetTableRange(rmin, rmax);
    lut_gray->Build();

    if (render_mode == PointCloudRenderMode::Blend) {
        vtkFloatArray* zArr = vtkFloatArray::SafeDownCast(poly->GetPointData()->GetArray("Elevation"));
        if (zArr) {
            const vtkIdType nt = zArr->GetNumberOfTuples();
            auto blend_colors = vtkSmartPointer<vtkUnsignedCharArray>::New();
            blend_colors->SetNumberOfComponents(3);
            blend_colors->SetNumberOfTuples(nt);
            const float ratio = blend_ratio;
            for (vtkIdType k = 0; k < nt; ++k) {
                const double zVal = zArr->GetValue(k);
                const unsigned char* gr = lut_gray->MapValue(zVal);
                const unsigned char* jt = lut_jet->MapValue(zVal);
                const auto r = static_cast<uint8_t>(gr[0] * (1.0f - ratio) + jt[0] * ratio);
                const auto g = static_cast<uint8_t>(gr[1] * (1.0f - ratio) + jt[1] * ratio);
                const auto b = static_cast<uint8_t>(gr[2] * (1.0f - ratio) + jt[2] * ratio);
                blend_colors->SetTuple3(k, r, g, b);
            }
            poly->GetPointData()->SetScalars(blend_colors);
        }
    }
    return poly;
}

}  // namespace

void PointCloudView3D::SetFrame(const UnifiedFrame& frame) {
    if (frame.point_cloud.empty()) return;
    last_frame_ = frame;
    // 仅保存最新帧并异步重建；不在此阻塞主线程做百万点循环。
    SchedulePointCloudRebuild();
}

/**
 * @brief 将当前 last_frame_ 与 UI 参数快照后投递到 PointCloudBuildPool 中构建 vtkPolyData。
 *
 * 流程：
 * 1. cloud_build_seq_ 自增，得到本帧 seq（之后任何更新的帧都会使更小 seq 的结果失效）。
 * 2. 拷贝 point_cloud 与 mode/depth/blend 到闭包——避免与工作线程并发读写成员变量。
 * 3. QtConcurrent::run(专用池, …) 中调用 BuildPreparedPolyData（纯 CPU + VTK 数据对象，无 GL）。
 * 4. 构建完成后用 QPointer 检测对象是否仍存活；QTimer::singleShot(0, guard, …) 将回调投递到
 *    guard 所在线程（即主线程）事件队列，从而安全调用 ApplyPreparedPointCloud。
 *
 * QPointer：窗口销毁后 guard 置空，工作线程与定时回调均不再解引用已删除的 this。
 */
void PointCloudView3D::SchedulePointCloudRebuild() {
    if (last_frame_.point_cloud.empty()) return;
    const uint64_t seq = ++cloud_build_seq_;
    std::vector<UnifiedPoint> points = last_frame_.point_cloud;
    const PointCloudRenderMode mode = render_mode_;
    const double rmin = depth_min_, rmax = depth_max_;
    const float blend = blend_ratio_;

    QPointer<PointCloudView3D> guard(this);
    (void)QtConcurrent::run(PointCloudBuildPool(), [=, pts = std::move(points)]() mutable {
        vtkSmartPointer<vtkPolyData> poly = BuildPreparedPolyData(pts, mode, rmin, rmax, blend);
        if (!guard) return;
        if (!poly || poly->GetNumberOfPoints() <= 0) return;
        vtkSmartPointer<vtkPolyData> poly_keep = poly;
        QTimer::singleShot(0, guard, [=]() {
            if (!guard) return;
            guard->ApplyPreparedPointCloud(seq, mode, poly_keep);
        });
    });
}

/**
 * @brief 【主线程】把已构建的 vtkPolyData 接到 vtkPolyDataMapper / vtkActor 并触发 Render。
 *
 * 线程与安全：
 * - 仅允许从 Qt GUI 线程调用（由 QTimer::singleShot 投递保证）。
 * - 此处创建/移除 Actor、更新 impl_->lut_* 表域、调用 render_window->Render()，均依赖当前 GL 上下文。
 *
 * 序号与模式：
 * - 若 seq != cloud_build_seq_，说明已有更新的帧被调度，本结果已过期，直接丢弃。
 * - 若 built_mode != render_mode_，说明用户在异步期间切换了颜色模式，poly 的标量布局可能不匹配，
 *   丢弃并 SchedulePointCloudRebuild() 用新模式重算一帧。
 *
 * Mapper 配置：与原先同步路径一致——Texture 用 DirectScalars；Blend 已在工作线程写好 RGB；
 * Grayscale/ZValue 使用 Elevation + 主窗口 LUT 与 ScalarRange（与滑条联动 ApplyDepthRangeToMapper 兼容）。
 */
void PointCloudView3D::ApplyPreparedPointCloud(
    uint64_t seq,
    PointCloudRenderMode built_mode,
    vtkSmartPointer<vtkPolyData> poly
) {
    // 已有更新的 SetFrame / Clear / 模式切换导致序号前进，忽略过期构建结果。
    if (seq != cloud_build_seq_) return;
    if (built_mode != render_mode_) {
        // 用当前 render_mode_ 重新排队；本次 poly 与新模式不匹配。
        SchedulePointCloudRebuild();
        return;
    }
    if (!poly || poly->GetNumberOfPoints() <= 0) return;

    bool has_color = false;
    for (const auto& p : last_frame_.point_cloud) {
        if (p.has_color) {
            has_color = true;
            break;
        }
    }
    const bool use_texture_rgb = (render_mode_ == PointCloudRenderMode::Texture) && has_color;

    if (impl_->points_actor) impl_->renderer->RemoveActor(impl_->points_actor);

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(poly);
    impl_->points_actor = vtkSmartPointer<vtkActor>::New();
    impl_->points_actor->SetMapper(mapper);
    impl_->points_actor->GetProperty()->SetPointSize(2.0);
    impl_->points_actor->GetProperty()->LightingOff();

    double rmin = depth_min_, rmax = depth_max_;
    if (rmax <= rmin) rmax = rmin + 1.0;
    impl_->lut_jet->SetTableRange(rmin, rmax);
    impl_->lut_jet->Build();
    impl_->lut_gray->SetTableRange(rmin, rmax);
    impl_->lut_gray->Build();

    if (render_mode_ == PointCloudRenderMode::Texture && use_texture_rgb) {
        mapper->SetScalarModeToUsePointFieldData();
        mapper->SelectColorArray(0);
        mapper->SetColorModeToDirectScalars();
    } else if (render_mode_ == PointCloudRenderMode::Untextured) {
        mapper->SetScalarVisibility(0);
        impl_->points_actor->GetProperty()->SetColor(1.0, 1.0, 1.0);
    } else if (render_mode_ == PointCloudRenderMode::Blend) {
        mapper->SetScalarModeToUsePointData();
        mapper->SetLookupTable(nullptr);
        mapper->SetColorModeToDirectScalars();
    } else {
        poly->GetPointData()->SetActiveScalars("Elevation");
        mapper->SetScalarModeToUsePointData();
        mapper->ScalarVisibilityOn();
        mapper->SetScalarRange(rmin, rmax);
        if (render_mode_ == PointCloudRenderMode::ZValue)
            mapper->SetLookupTable(impl_->lut_jet);
        else
            mapper->SetLookupTable(impl_->lut_gray);
    }
    impl_->renderer->AddActor(impl_->points_actor);
    if (!point_cloud_continuous_acquisition_) {
        impl_->renderer->ResetCamera();
    } else if (point_cloud_reset_camera_next_frame_) {
        impl_->renderer->ResetCamera();
        point_cloud_reset_camera_next_frame_ = false;
    }
    impl_->render_window->Render();
}

void PointCloudView3D::SetContinuousPointCloudAcquisition(bool continuous) {
    point_cloud_continuous_acquisition_ = continuous;
    if (continuous) point_cloud_reset_camera_next_frame_ = true;
}

void PointCloudView3D::ApplyDepthRangeToMapper() {
    if (!impl_->points_actor) return;
    if (render_mode_ == PointCloudRenderMode::Texture || render_mode_ == PointCloudRenderMode::Untextured) return;
    double rmin = depth_min_, rmax = depth_max_;
    if (rmax <= rmin) rmax = rmin + 1.0;
    impl_->lut_jet->SetTableRange(rmin, rmax);
    impl_->lut_jet->Build();
    impl_->lut_gray->SetTableRange(rmin, rmax);
    impl_->lut_gray->Build();
    if (render_mode_ == PointCloudRenderMode::Blend) {
        // Blend 的 RGB 已在工作线程按 LUT 烘焙进 poly；深度范围变化必须整帧重算，不能只改 ScalarRange。
        if (!last_frame_.point_cloud.empty()) SchedulePointCloudRebuild();
        return;
    }
    vtkMapper* mapper = impl_->points_actor->GetMapper();
    if (mapper) {
        mapper->SetScalarRange(rmin, rmax);
        impl_->render_window->Render();
    }
}

void PointCloudView3D::SetRenderMode(PointCloudRenderMode mode) {
    if (render_mode_ == mode) return;
    render_mode_ = mode;
    if (blend_control_widget_) {
        blend_control_widget_->setVisible(mode == PointCloudRenderMode::Blend);
    }
    UpdateColorBarImage();
    // 新模式需要不同的标量布局（纹理/Z/融合等），走异步重建以不阻塞 UI。
    if (!last_frame_.point_cloud.empty())
        SchedulePointCloudRebuild();
}

void PointCloudView3D::SetDepthRange(double min_z, double max_z) {
    if (min_z > max_z) std::swap(min_z, max_z);
    depth_min_ = min_z;
    depth_max_ = max_z;
    ApplyDepthRangeToMapper();
}

void PointCloudView3D::GetDepthRange(double* min_z, double* max_z) const {
    if (min_z) *min_z = depth_min_;
    if (max_z) *max_z = depth_max_;
}

void PointCloudView3D::SetBlendRatio(float ratio) {
    blend_ratio_ = std::max(0.0f, std::min(1.0f, ratio));
    // 融合比例影响逐点 RGB，在工作线程重算；主线程仅应用新 poly。
    if (render_mode_ == PointCloudRenderMode::Blend && !last_frame_.point_cloud.empty())
        SchedulePointCloudRebuild();
}

void PointCloudView3D::Clear() {
    // 使所有已投递、尚未执行的 ApplyPreparedPointCloud 在序号比对时失效，避免清空后又把旧 Actor 加回。
    ++cloud_build_seq_;
    last_frame_.point_cloud.clear();
    if (impl_->points_actor) {
        impl_->renderer->RemoveActor(impl_->points_actor);
        impl_->points_actor = nullptr;
    }
    impl_->renderer->SetBackground(0.7, 0.85, 1.0);
    impl_->render_window->Render();
    point_cloud_reset_camera_next_frame_ = true;
}

void PointCloudView3D::ResetCamera() {
    if (impl_->renderer) {
        impl_->renderer->ResetCamera();
        impl_->render_window->Render();
    }
}

void PointCloudView3D::UpdateColorBarImage() {
    if (!label_bar_img_ || !label_bar_max_ || !label_bar_min_) return;
    const int barH = 256;
    const int barW = 30;
    impl_->colorbar_image = QImage(barW, barH, QImage::Format_RGB32);
    const bool use_jet = (render_mode_ == PointCloudRenderMode::ZValue || render_mode_ == PointCloudRenderMode::Blend);
    QColor topColor, bottomColor;
    if (use_jet) {
        topColor = QColor(255, 0, 0);
        bottomColor = QColor(128, 0, 128);
        for (int y = 0; y < barH; ++y) {
            double t = (barH - 1 - y) / static_cast<double>(barH - 1);
            double r, g, b;
            if (t < 0.125) { r = 0; g = 0; b = 0.5 + 4.0 * t; }
            else if (t < 0.375) { r = 0; g = 4.0 * (t - 0.125); b = 1.0; }
            else if (t < 0.625) { r = 4.0 * (t - 0.375); g = 1.0; b = 1.0 - 4.0 * (t - 0.375); }
            else if (t < 0.875) { r = 1.0; g = 1.0 - 4.0 * (t - 0.625); b = 0; }
            else { r = 1.0 - 4.0 * (t - 0.875); g = 0; b = 0; }
            QColor c(static_cast<int>(r * 255), static_cast<int>(g * 255), static_cast<int>(b * 255));
            for (int x = 0; x < barW; ++x) impl_->colorbar_image.setPixel(x, y, c.rgba());
        }
    } else {
        topColor = QColor(255, 255, 255);
        bottomColor = QColor(0, 0, 0);
        for (int y = 0; y < barH; ++y) {
            int v = 255 - (y * 255 / (barH - 1));
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            QColor c(v, v, v);
            for (int x = 0; x < barW; ++x) impl_->colorbar_image.setPixel(x, y, c.rgba());
        }
    }
    label_bar_max_->setStyleSheet(QString("QLabel{ border:0px; background-color: rgb(%1,%2,%3); }")
        .arg(topColor.red()).arg(topColor.green()).arg(topColor.blue()));
    label_bar_min_->setStyleSheet(QString("QLabel{ border:0px; background-color: rgb(%1,%2,%3); }")
        .arg(bottomColor.red()).arg(bottomColor.green()).arg(bottomColor.blue()));
    UpdateColorBarSize();
}

void PointCloudView3D::UpdateColorBarSize() {
    if (!range_slider_ || !widget_bar_ || !label_bar_max_ || !label_bar_img_ || !label_bar_min_) return;
    const int bar_width = 30;
    int r = range_slider_->maximum() - range_slider_->minimum();
    int h = range_slider_->geometry().height();
    if (h <= 0) h = 160;
    int low = range_slider_->low();
    int high = range_slider_->high();
    int bar_x = (widget_bar_->width() - bar_width) / 2;
    if (bar_x < 0) bar_x = 0;
    if (r == 0) {
        label_bar_max_->setGeometry(bar_x, 0, bar_width, 0);
        label_bar_img_->setGeometry(bar_x, 0, bar_width, h);
        label_bar_min_->setGeometry(bar_x, h, bar_width, 0);
        QPixmap pix = QPixmap::fromImage(impl_->colorbar_image.scaled(bar_width, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        label_bar_img_->setPixmap(pix);
        return;
    }
    int h_max = (range_slider_->maximum() - high) * h / r;
    int h_min = (low - range_slider_->minimum()) * h / r;
    int mid_h = h - h_max - h_min;
    if (mid_h < 1) mid_h = 1;
    label_bar_max_->setGeometry(bar_x, 0, bar_width, h_max);
    label_bar_img_->setGeometry(bar_x, h_max, bar_width, mid_h);
    label_bar_min_->setGeometry(bar_x, h_max + mid_h, bar_width, h_min);
    QPixmap pix = QPixmap::fromImage(impl_->colorbar_image.scaled(bar_width, mid_h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    label_bar_img_->setPixmap(pix);
}

void PointCloudView3D::CreateFloatingControls() {
    floating_panel_ = new QWidget(this);
    floating_panel_->setAttribute(Qt::WA_TranslucentBackground);
    // 半透明蓝白简约风格
    const char* panel_style =
        "QWidget { background-color: rgba(179, 217, 242, 0.75); border-radius: 6px; }"
        "QLabel { background-color: transparent; color: #1E5F8C; font-size: 11px; }"
        "QComboBox {"
        "  min-width: 88px; padding: 4px 8px;"
        "  background-color: rgba(220, 235, 250, 0.9); color: #1E5F8C;"
        "  border: 1px solid rgba(30, 95, 140, 0.6); border-radius: 4px;"
        "}"
        "QComboBox:hover { background-color: rgba(200, 225, 245, 0.95); }"
        "QComboBox::drop-down { border: none; background: transparent; width: 20px; }"
        "QComboBox QAbstractItemView {"
        "  background-color: rgba(220, 235, 250, 0.98); color: #1E5F8C;"
        "  selection-background-color: rgba(30, 95, 140, 0.35); selection-color: #1E5F8C;"
        "  border: 1px solid rgba(30, 95, 140, 0.4); border-radius: 4px; padding: 2px;"
        "}"
        "QSlider { background-color: transparent; }"
        "QSlider::groove:vertical { background: rgba(30, 95, 140, 0.25); width: 8px; border-radius: 4px; }"
        "QSlider::handle:vertical {"
        "  background: rgba(30, 95, 140, 0.95); border: 1px solid #1E5F8C; border-radius: 5px;"
        "  width: 22px; margin: 0 -7px; min-height: 28px;"
        "}"
        "QPushButton {"
        "  background-color: rgba(220, 235, 250, 0.9); color: #1E5F8C;"
        "  border: 1px solid rgba(30, 95, 140, 0.6); border-radius: 4px; padding: 4px 8px;"
        "}"
        "QPushButton:hover { background-color: rgba(200, 225, 245, 0.95); }"
        "QSpinBox {"
        "  background-color: rgba(160, 200, 230, 0.95); color: #1E5F8C; border: 1px solid rgba(30, 95, 140, 0.7); border-radius: 4px;"
        "  padding: 2px 4px; min-width: 52px;"
        "}"
        "QSpinBox::up-button, QSpinBox::down-button {"
        "  background-color: rgba(30, 95, 140, 0.35); border-left: 1px solid rgba(30, 95, 140, 0.5); width: 18px;"
        "}"
        "QSpinBox::up-button:hover, QSpinBox::down-button:hover { background-color: rgba(30, 95, 140, 0.5); }";
    floating_panel_->setStyleSheet(panel_style);
    floating_panel_->setMouseTracking(true);

    QVBoxLayout* panel_layout = new QVBoxLayout(floating_panel_);
    panel_layout->setContentsMargins(10, 10, 10, 10);
    panel_layout->setSpacing(6);

    // 颜色（渲染方式）
    QLabel* color_label = new QLabel(tr(u8"颜色"), floating_panel_);
    panel_layout->addWidget(color_label);
    ElaComboBox* mode_combo = new ElaComboBox(floating_panel_);
    mode_combo->addItem(tr(u8"灰度(高程)"), static_cast<int>(PointCloudRenderMode::Grayscale));
    mode_combo->addItem(tr(u8"Z值(高程)"), static_cast<int>(PointCloudRenderMode::ZValue));
    mode_combo->addItem(tr(u8"纹理"), static_cast<int>(PointCloudRenderMode::Texture));
    mode_combo->addItem(tr(u8"无纹理"), static_cast<int>(PointCloudRenderMode::Untextured));
    mode_combo->addItem(tr(u8"融合"), static_cast<int>(PointCloudRenderMode::Blend));
    mode_combo->setCurrentIndex(static_cast<int>(render_mode_));
    //if (QAbstractItemView* list = mode_combo->view()) {
    //    list->setStyleSheet(
    //        "QListView { background-color: rgba(220, 235, 250, 0.98); color: #1E5F8C; }"
    //        "QListView::item:hover { background-color: rgba(30, 95, 140, 0.25); }"
    //        "QListView::item:selected { background-color: rgba(30, 95, 140, 0.4); color: #1E5F8C; }"
    //    );
    //}
    connect(mode_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        SetRenderMode(static_cast<PointCloudRenderMode>(index));
    });
    panel_layout->addWidget(mode_combo);

    // 范围 + 复位
    QHBoxLayout* range_title_row = new QHBoxLayout();
    range_title_row->addWidget(new QLabel(tr(u8"范围"), floating_panel_));
    range_title_row->addStretch();
    ElaPushButton* range_reset_btn = new ElaPushButton(tr(u8"复位"), floating_panel_);
    connect(range_reset_btn, &QPushButton::clicked, this, [this]() {
        int lo = range_slider_->minimum(), hi = range_slider_->maximum();
        range_slider_->setLow(lo);
        range_slider_->setHigh(hi);
        depth_min_ = lo;
        depth_max_ = hi;
        SetDepthRange(lo, hi);
        if (min_spin_) { min_spin_->blockSignals(true); min_spin_->setValue(lo); min_spin_->blockSignals(false); }
        if (max_spin_) { max_spin_->blockSignals(true); max_spin_->setValue(hi); max_spin_->blockSignals(false); }
        UpdateColorBarSize();
    });
    range_title_row->addWidget(range_reset_btn);
    panel_layout->addLayout(range_title_row);

    const int slider_min = 0, slider_max = 3000;
    range_slider_ = new ::RangeSlider(Qt::Vertical, floating_panel_);
    range_slider_->setRange(slider_min, slider_max);
    range_slider_->setLow(static_cast<int>(depth_min_));
    range_slider_->setHigh(static_cast<int>(depth_max_));
    range_slider_->setFixedHeight(200);
    range_slider_->setStyleSheet(
        "QSlider::groove:vertical { background: rgba(30, 95, 140, 0.25); width: 10px; border-radius: 5px; }"
        "QSlider::handle:vertical { background: rgba(30, 95, 140, 0.95); border: 1px solid #1E5F8C; border-radius: 5px; width: 22px; margin: 0 -6px; min-height: 28px; }"
    );

    widget_bar_ = new QWidget(floating_panel_);
    widget_bar_->setAttribute(Qt::WA_TranslucentBackground);
    widget_bar_->setFixedHeight(200);
    widget_bar_->setMinimumWidth(44);
    label_bar_max_ = new QLabel(widget_bar_);
    label_bar_max_->setMinimumSize(24, 0);
    label_bar_img_ = new QLabel(widget_bar_);
    label_bar_img_->setMinimumSize(24, 0);
    label_bar_img_->setStyleSheet("QLabel{ border:0px; background:transparent; }");
    label_bar_min_ = new QLabel(widget_bar_);
    label_bar_min_->setMinimumSize(24, 0);

    // 彩条左侧刻度标签（参考图：白字数值沿彩条）
    QWidget* scale_container = new QWidget(floating_panel_);
    scale_container->setAttribute(Qt::WA_TranslucentBackground);
    scale_container->setFixedHeight(200);
    QVBoxLayout* scale_layout = new QVBoxLayout(scale_container);
    scale_layout->setContentsMargins(0, 0, 4, 0);
    scale_layout->setSpacing(0);
    const int scale_ticks[] = { slider_max, slider_max * 3 / 4, slider_max / 2, slider_max / 4, slider_min };
    for (int i = 0; i < 5; ++i) {
        QLabel* scale_lbl = new QLabel(QString::number(scale_ticks[i]), scale_container);
        scale_lbl->setStyleSheet("color: #1E5F8C; font-size: 10px; background: transparent;");
        scale_layout->addWidget(scale_lbl, 0, Qt::AlignRight);
        if (i < 4) scale_layout->addStretch(1);
    }

    min_spin_ = new ElaSpinBox(floating_panel_);
    max_spin_ = new ElaSpinBox(floating_panel_);
    min_spin_->setRange(slider_min, slider_max);
    max_spin_->setRange(slider_min, slider_max);
    min_spin_->setValue(static_cast<int>(depth_min_));
    max_spin_->setValue(static_cast<int>(depth_max_));
    min_spin_->setMinimumWidth(56);
    max_spin_->setMinimumWidth(56);

    connect(range_slider_, &::RangeSlider::sliderMoved, this, [this](int low, int high) {
        double lo = static_cast<double>(low), hi = static_cast<double>(high);
        if (lo > hi) std::swap(lo, hi);
        depth_min_ = lo;
        depth_max_ = hi;
        SetDepthRange(lo, hi);
        if (min_spin_) { min_spin_->blockSignals(true); min_spin_->setValue(low); min_spin_->blockSignals(false); }
        if (max_spin_) { max_spin_->blockSignals(true); max_spin_->setValue(high); max_spin_->blockSignals(false); }
        UpdateColorBarSize();
    });

    auto onMinSpin = [this](int val) {
        if (range_slider_) {
            int hi = range_slider_->high();
            if (val >= hi) val = hi - 1;
            range_slider_->setLow(val);
            depth_min_ = val;
            SetDepthRange(val, depth_max_);
            UpdateColorBarSize();
        }
    };
    auto onMaxSpin = [this](int val) {
        if (range_slider_) {
            int lo = range_slider_->low();
            if (val <= lo) val = lo + 1;
            range_slider_->setHigh(val);
            depth_max_ = val;
            SetDepthRange(depth_min_, val);
            UpdateColorBarSize();
        }
    };
    connect(min_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, onMinSpin);
    connect(max_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, onMaxSpin);

    QGridLayout* bar_row = new QGridLayout();
    bar_row->setContentsMargins(0, 0, 0, 0);
    bar_row->setSpacing(4);
    bar_row->addWidget(scale_container, 0, 0, 1, 1, Qt::AlignRight);
    bar_row->addWidget(widget_bar_, 0, 1, 1, 1, Qt::AlignLeft);
    bar_row->addWidget(range_slider_, 0, 2, 1, 1, Qt::AlignLeft);
    QVBoxLayout* spin_col = new QVBoxLayout();
    spin_col->setSpacing(4);
    spin_col->addWidget(max_spin_, 0, Qt::AlignLeft);
    spin_col->addStretch(1);
    spin_col->addWidget(min_spin_, 0, Qt::AlignLeft);
    bar_row->addLayout(spin_col, 0, 3, 1, 1);
    panel_layout->addLayout(bar_row);
    //
    blend_control_widget_ = new QWidget(floating_panel_);
    blend_control_widget_->setAttribute(Qt::WA_TranslucentBackground);
    QVBoxLayout* blend_layout = new QVBoxLayout(blend_control_widget_);
    blend_layout->setContentsMargins(0, 4, 0, 0);
    QLabel* blend_label = new QLabel(tr(u8"融合比例(彩色权重): %1%").arg(static_cast<int>(blend_ratio_ * 100)), blend_control_widget_);
    ElaSlider* blend_slider = new ElaSlider(Qt::Horizontal, blend_control_widget_);
    blend_slider->setRange(0, 100);
    blend_slider->setValue(static_cast<int>(blend_ratio_ * 100));
    blend_slider->setMinimumWidth(100);
    connect(blend_slider, &QSlider::valueChanged, this, [this, blend_label](int value) {
        float r = value / 100.0f;
        SetBlendRatio(r);
        blend_label->setText(tr(u8"融合比例(彩色权重): %1%").arg(value));
    });
    blend_layout->addWidget(blend_label);
    blend_layout->addWidget(blend_slider);
    blend_control_widget_->hide();
    panel_layout->addWidget(blend_control_widget_);

    ElaPushButton* reset_btn = new ElaPushButton(tr(u8"重置相机"), floating_panel_);
    connect(reset_btn, &QPushButton::clicked, this, &PointCloudView3D::ResetCamera);
    panel_layout->addWidget(reset_btn);

    UpdateColorBarImage();
    floating_panel_->setFixedSize(200, 440);
    floating_panel_->raise();

    // 显示/隐藏彩条子界面按钮（作为视窗子控件，隐藏 panel 后仍可点击）
    toggle_panel_btn_ = new ElaPushButton(tr(u8"隐藏"), this);
    toggle_panel_btn_->setFixedSize(44, 26);
    toggle_panel_btn_->setStyleSheet(
        "QPushButton { background-color: rgba(220, 235, 250, 0.9); color: #1E5F8C; border: 1px solid rgba(30, 95, 140, 0.6); border-radius: 4px; font-size: 11px; }"
        "QPushButton:hover { background-color: rgba(200, 225, 245, 0.95); }"
    );
    connect(toggle_panel_btn_, &QPushButton::clicked, this, [this]() {
        const bool visible = !floating_panel_->isVisible();
        floating_panel_->setVisible(visible);
        toggle_panel_btn_->setText(visible ? tr(u8"隐藏") : tr(u8"显示"));
    });
    toggle_panel_btn_->raise();
}

void PointCloudView3D::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const int panel_w = floating_panel_ ? floating_panel_->width() : 200;
    const int btn_w = toggle_panel_btn_ ? toggle_panel_btn_->width() : 44;
    const int margin = 8;
    const int gap = 4;
    // 按钮始终固定在视窗右上角
    if (toggle_panel_btn_) {
        toggle_panel_btn_->move(width() - btn_w - margin, margin);
        toggle_panel_btn_->raise();
    }
    if (floating_panel_) {
        // 面板显示时紧贴按钮左侧，隐藏时也按此逻辑布局（保持一致性）
        int panel_x = width() - btn_w - margin - gap - panel_w;
        floating_panel_->move(std::max(0, panel_x), margin);
        floating_panel_->raise();
        UpdateColorBarSize();
    }
}

}  // namespace scanner_viewer
