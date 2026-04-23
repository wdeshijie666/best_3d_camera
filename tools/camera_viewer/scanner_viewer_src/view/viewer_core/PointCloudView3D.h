/**
 * @file PointCloudView3D.h
 * @brief 3D 点云视窗：VTK 嵌入 Qt，多种渲染模式、深度范围可调、右上角悬浮控件
 *
 * 连续高帧率点云刷新时，vtkPolyData 的构建（点坐标、颜色、Blend LUT）在后台线程完成，
 * 主线程只做 Mapper/Actor 与 Render；详见 PointCloudView3D.cpp 中 SchedulePointCloudRebuild /
 * ApplyPreparedPointCloud 及文件头 @section vtk_perf。
 */
#ifndef SCANNER_VIEWER_POINT_CLOUD_VIEW_3D_H
#define SCANNER_VIEWER_POINT_CLOUD_VIEW_3D_H

#include "../../model/data_center/UnifiedFrame.h"
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <QWidget>

class QResizeEvent;
class QLabel;
class QPushButton;
class QSpinBox;
class RangeSlider;  // 定义在 RangeSlider.h，全局命名空间

// VTK 前向声明
class vtkGenericOpenGLRenderWindow;
class vtkRenderer;
class vtkActor;
class vtkPolyData;
class QVTKOpenGLNativeWidget;

namespace scanner_viewer {

/** 点云渲染方式：纹理、Z值、灰度、无纹理、融合（与参考工程一致） */
enum class PointCloudRenderMode {
    Grayscale = 0, ///< 按 Z 高程着色（灰度 LUT）
    ZValue = 1,    ///< 按 Z 高程着色（彩色 Jet LUT）
    Texture = 2,   ///< 纹理/点云自带 RGB
    Untextured = 3,///< 无纹理，纯白点云
    Blend = 4      ///< 灰度与彩色按比例融合
};

class PointCloudView3D : public QWidget {
    Q_OBJECT
public:
    explicit PointCloudView3D(QWidget* parent = nullptr);
    ~PointCloudView3D() override;

    void SetFrame(const UnifiedFrame& frame);
    void Clear();
    void ResetCamera();

    /**
     * 连续采集为 true 时：仅在本轮连续流的第一帧点云更新后执行 ResetCamera。
     * 单次采集（本接口保持 false）时：每一帧点云更新都会 ResetCamera。
     * 开始连续采集时应调用 SetContinuousPointCloudAcquisition(true) 以重新标记“待首帧重置”。
     */
    void SetContinuousPointCloudAcquisition(bool continuous);

    /** 设置渲染方式 */
    void SetRenderMode(PointCloudRenderMode mode);
    PointCloudRenderMode GetRenderMode() const { return render_mode_; }
    /** 设置深度范围（用于 Z 值/灰度/融合渲染的 min/max） */
    void SetDepthRange(double min_z, double max_z);
    void GetDepthRange(double* min_z, double* max_z) const;
    /** 设置融合比例（仅 Blend 模式有效），0.0=纯灰度，1.0=纯彩色 */
    void SetBlendRatio(float ratio);
    float GetBlendRatio() const { return blend_ratio_; }

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void CreateFloatingControls();
    /**
     * 异步调度点云重建：
     * - 递增 cloud_build_seq_，快照 point_cloud 与 render_mode_/depth/blend；
     * - 在专用单线程池（PointCloudBuildPool）中执行 BuildPreparedPolyData；
     * - 通过 QTimer::singleShot(0, …) 回到主线程调用 ApplyPreparedPointCloud。
     */
    void SchedulePointCloudRebuild();
    /**
     * 主线程专用：将 vtkPolyData 接入 vtkPolyDataMapper / vtkActor 并触发 OpenGL 渲染。
     * @param seq 与调度时 cloud_build_seq_ 对齐；不一致则本结果为过期帧，丢弃。
     * @param built_mode 工作线程构建 poly 时使用的渲染模式；若用户已切换 render_mode_ 则重新 Schedule。
     * @param poly 引用计数由 vtkSmartPointer 管理，仅在本线程及 VTK 管线中使用。
     */
    void ApplyPreparedPointCloud(uint64_t seq, PointCloudRenderMode built_mode, vtkSmartPointer<vtkPolyData> poly);
    void ApplyDepthRangeToMapper();
    /** 参考 widget_color_bar_setting：根据 RangeSlider 的 low/high 更新三段彩条几何与显示 */
    void UpdateColorBarSize();
    /** 根据当前渲染模式（ZValue/Grayscale/Blend）更新彩条渐变图与上下色块 */
    void UpdateColorBarImage();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    PointCloudRenderMode render_mode_{PointCloudRenderMode::Grayscale};
    double depth_min_{0.0};
    double depth_max_{1000.0};
    float blend_ratio_{0.5f};
    UnifiedFrame last_frame_;  ///< 用于切换渲染/范围时重绘
    QWidget* floating_panel_{nullptr};
    // 三段式彩条（与参考工程一致）：上色块、渐变条、下色块
    QWidget* widget_bar_{nullptr};
    QLabel* label_bar_max_{nullptr};
    QLabel* label_bar_img_{nullptr};
    QLabel* label_bar_min_{nullptr};
    ::RangeSlider* range_slider_{nullptr};
    QSpinBox* min_spin_{nullptr};   // 下限数值输入（与参考图一致）
    QSpinBox* max_spin_{nullptr};   // 上限数值输入
    QWidget* blend_control_widget_{nullptr};
    QPushButton* toggle_panel_btn_{nullptr};  ///< 显示/隐藏彩条子界面

    /** 当前是否为连续点云流（与单次采集区分 ResetCamera 策略）。 */
    bool point_cloud_continuous_acquisition_{false};
    /**
     * 连续模式下：为 true 时下一次点云应用到渲染器后执行 ResetCamera，随后置 false。
     * 单次模式下忽略此标志，每帧均 ResetCamera。
     */
    bool point_cloud_reset_camera_next_frame_{true};

    /**
     * 点云异步构建代数序号：每次 SchedulePointCloudRebuild / Clear 等会使其前进。
     * ApplyPreparedPointCloud 比对 seq 与当前值，用于丢弃高帧率下已过时的构建结果，避免“旧帧覆盖新帧”。
     */
    std::atomic<uint64_t> cloud_build_seq_{0};
};

}  // namespace scanner_viewer

#endif  // SCANNER_VIEWER_POINT_CLOUD_VIEW_3D_H
