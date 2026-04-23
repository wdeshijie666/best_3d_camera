/**
 * @file ImageView2D.h
 * @brief 2D 图像视窗：基于 QGraphicsView 多图元，接收统一帧中的深度/纹理并显示
 */
#ifndef SCANNER_VIEWER_IMAGE_VIEW_2D_H
#define SCANNER_VIEWER_IMAGE_VIEW_2D_H

#include "../../model/data_center/UnifiedFrame.h"
#include <QGraphicsView>
#include <QGraphicsPixmapItem>
#include <QString>
#include <QWheelEvent>
#include <memory>
#include <vector>

class QLabel;
class QResizeEvent;
class QTimer;

namespace scanner_viewer {

class ImageView2D : public QGraphicsView {
    Q_OBJECT
public:
    enum class DisplayMode {
        PreferTexture,
        PreferDepth
    };

    explicit ImageView2D(QWidget* parent = nullptr);
    ~ImageView2D() override;

    /** 使用统一帧更新显示（深度或第一张纹理转 QImage 显示） */
    void SetFrame(const UnifiedFrame& frame);
    /** 切换显示优先级：深度优先用于 2D 视窗，纹理优先用于纹理视窗。 */
    void SetDisplayMode(DisplayMode mode);
    void Clear();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    /** PreferTexture 模式下校验首帧纹理尺寸与缓冲区是否一致，并给出可读原因。 */
    static bool ValidateTextureBuffer(const UnifiedImage& tex, QString* reason_out);

    void ShowTextureHint(const QString& text);
    void HideTextureHint();
    // btest: 与 ImageView2D.cpp 中 CAMERA_VIEWER_BTEST_BURST_2D_SLIDESHOW 同步；0 时成员为空永不使用
    void BtestBurst2dTeardown();
    void BtestBurst2dOnTick();
    void BtestBurst2dSetFramePath(const UnifiedFrame& frame, QImage* io_img, bool* inout_done);

    QGraphicsScene* scene_{nullptr};
    QGraphicsPixmapItem* pixmap_item_{nullptr};
    QLabel* texture_hint_overlay_{nullptr};
    bool need_auto_fit_{true};
    DisplayMode display_mode_{DisplayMode::PreferTexture};
    // btest（宏关时未使用，便于整段从 .h 删除）
    QTimer* btest_burst2d_timer_{nullptr};
    std::vector<UnifiedImage> btest_burst2d_frames_{};
    int btest_burst2d_index_{0};
};

}  // namespace scanner_viewer

#endif  // SCANNER_VIEWER_IMAGE_VIEW_2D_H
