/**
 * @file ImageView2D.cpp
 * @brief 2D 视窗实现：UnifiedImage 转 QImage，QGraphicsScene 显示
 */
#include "ImageView2D.h"
#include "common/camera_viewer_btest.h"
#include "common/unified_image_qt.h"
#include <QGraphicsScene>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QResizeEvent>
#include <QTimer>
#include <QWheelEvent>
#include <cstdint>

namespace scanner_viewer {

/** 将 float 深度图转为 8 位灰度 QImage（按 min/max 归一化） */
static QImage DepthFloatToQImage(const float* depth, int w, int h) {
    if (!depth || w <= 0 || h <= 0) return QImage();
    float min_v = 1e9f, max_v = -1e9f;
    const int n = w * h;
    for (int i = 0; i < n; i++) {
        float v = depth[i];
        if (v > 0) { if (v < min_v) min_v = v; if (v > max_v) max_v = v; }
    }
    if (max_v <= min_v) max_v = min_v + 1.f;
    QImage out(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; y++) {
        auto* line = out.scanLine(y);
        for (int x = 0; x < w; x++) {
            float v = depth[y * w + x];
            if (v <= 0) { line[x] = 0; continue; }
            line[x] = static_cast<uchar>(255.f * (v - min_v) / (max_v - min_v));
        }
    }
    return out;
}

bool ImageView2D::ValidateTextureBuffer(const UnifiedImage& tex, QString* reason_out) {
    if (tex.width <= 0 || tex.height <= 0) {
        if (reason_out) *reason_out = QObject::tr(u8"纹理宽高无效（≤0）。");
        return false;
    }
    const size_t n = static_cast<size_t>(tex.width) * tex.height;
    if (tex.channels <= 0) {
        if (reason_out) *reason_out = QObject::tr(u8"纹理通道数无效。");
        return false;
    }
    size_t need = 0;
    if (tex.is_16bit && tex.channels == 1) {
        need = n * sizeof(uint16_t);
    } else if (tex.channels == 1) {
        need = n;
    } else {
        need = n * static_cast<size_t>(tex.channels);
    }
    if (tex.data.size() < need) {
        if (reason_out) {
            *reason_out = QObject::tr(u8"纹理缓冲区长度与宽高×通道不匹配（期望至少 %1 字节，实际 %2）。可能丢包或像素格式与 channels 声明不一致。")
                              .arg(static_cast<qulonglong>(need))
                              .arg(static_cast<qulonglong>(tex.data.size()));
        }
        return false;
    }
    if (UnifiedImageToQImage(tex).isNull()) {
        if (reason_out) *reason_out = QObject::tr(u8"纹理数据无法按当前通道/位深解析为图像。");
        return false;
    }
    return true;
}

ImageView2D::ImageView2D(QWidget* parent) : QGraphicsView(parent) {
    scene_ = new QGraphicsScene(this);
    setScene(scene_);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    pixmap_item_ = scene_->addPixmap(QPixmap());
    pixmap_item_->setTransformationMode(Qt::SmoothTransformation);
}

void ImageView2D::BtestBurst2dTeardown() {
#if (CAMERA_VIEWER_BTEST_BURST_2D_SLIDESHOW + 0) == 1
  if (btest_burst2d_timer_) {
    btest_burst2d_timer_->stop();
  }
  btest_burst2d_frames_.clear();
  btest_burst2d_index_ = 0;
  // QTimer* 不 delete，交还给 ImageView2D 析构（this 为 parent）
#endif
}

void ImageView2D::BtestBurst2dOnTick() {
#if (CAMERA_VIEWER_BTEST_BURST_2D_SLIDESHOW + 0) == 1
  if (btest_burst2d_frames_.empty() || !pixmap_item_) return;
  btest_burst2d_index_ = (btest_burst2d_index_ + 1) % static_cast<int>(btest_burst2d_frames_.size());
  const QImage n = UnifiedImageToQImage(btest_burst2d_frames_[static_cast<std::size_t>(btest_burst2d_index_)]);
  if (n.isNull()) return;
  pixmap_item_->setPixmap(QPixmap::fromImage(n));
  scene_->setSceneRect(pixmap_item_->boundingRect());
#endif
}

void ImageView2D::BtestBurst2dSetFramePath(const UnifiedFrame& frame, QImage* io_img, bool* out_took) {
  *out_took = false;
#if (CAMERA_VIEWER_BTEST_BURST_2D_SLIDESHOW + 0) == 1
  if (display_mode_ != DisplayMode::PreferDepth) return;
  const size_t nburst = frame.hardware_raw_frames.size();
  if (nburst <= 1) {
    // SetFrame 入口已 BtestBurst2dTeardown()，勿再二重清理
    return;
  }
  btest_burst2d_frames_ = frame.hardware_raw_frames;
  btest_burst2d_index_ = 0;
  *io_img = UnifiedImageToQImage(btest_burst2d_frames_[0]);
  if (io_img->isNull()) {
    BtestBurst2dTeardown();
    btest_burst2d_frames_.clear();
    return;
  }
  if (!btest_burst2d_timer_) {
    btest_burst2d_timer_ = new QTimer(this);
    btest_burst2d_timer_->setInterval(CAMERA_VIEWER_BTEST_BURST_2D_SLIDESHOW_MS);
    QObject::connect(btest_burst2d_timer_, &QTimer::timeout, this, [this]() { BtestBurst2dOnTick(); });
  }
  btest_burst2d_timer_->start();
  *out_took = true;
#endif
}

ImageView2D::~ImageView2D() { BtestBurst2dTeardown(); }

void ImageView2D::ShowTextureHint(const QString& text) {
    if (!texture_hint_overlay_) {
        texture_hint_overlay_ = new QLabel(this);
        texture_hint_overlay_->setAlignment(Qt::AlignCenter);
        texture_hint_overlay_->setWordWrap(true);
        texture_hint_overlay_->setStyleSheet(
            "QLabel { background-color: rgba(255, 248, 220, 235); color: #6B4423; padding: 20px; "
            "border-radius: 8px; border: 1px solid #D4A574; font-size: 13px; }");
    }
    texture_hint_overlay_->setText(text);
    texture_hint_overlay_->setGeometry(rect().adjusted(48, 48, -48, -48));
    texture_hint_overlay_->raise();
    texture_hint_overlay_->show();
}

void ImageView2D::HideTextureHint() {
    if (texture_hint_overlay_) texture_hint_overlay_->hide();
}

void ImageView2D::SetFrame(const UnifiedFrame& frame) {
    BtestBurst2dTeardown();
    QImage img;
    const bool has_texture = !frame.textures.empty() && frame.textures[0].width > 0;
    auto load_depth = [&]() {
        const size_t pix_count = static_cast<size_t>(frame.depth.width) * frame.depth.height;
        if (frame.depth.is_16bit) {
            img = UnifiedImageToQImage(frame.depth);
        } else if (frame.depth.data.size() >= pix_count * sizeof(float)) {
            const auto* d = reinterpret_cast<const float*>(frame.depth.data.data());
            img = DepthFloatToQImage(d, frame.depth.width, frame.depth.height);
        } else {
            img = UnifiedImageToQImage(frame.depth);
        }
    };
    if (display_mode_ == DisplayMode::PreferDepth) {
        bool btest = false;
        BtestBurst2dSetFramePath(frame, &img, &btest);
        HideTextureHint();
        if (!btest) {
            const bool has_depth = frame.depth.width > 0 && !frame.depth.data.empty();
            if (has_depth) load_depth();
            if (img.isNull() && has_texture) img = UnifiedImageToQImage(frame.textures[0]);
        }
    } else {
        // 纹理页：无数据或缓冲区异常时给出明确提示，避免误用深度图当纹理且无说明。
        if (frame.textures.empty()) {
            pixmap_item_->setPixmap(QPixmap());
            scene_->setSceneRect(0, 0, 1, 1);
            ShowTextureHint(tr(u8"当前帧无纹理数据。\n请确认相机已输出彩色/纹理流（如海康 ImageMode、A 系列纹理开关等）。"));
            return;
        }
        QString why;
        if (!ValidateTextureBuffer(frame.textures[0], &why)) {
            pixmap_item_->setPixmap(QPixmap());
            scene_->setSceneRect(0, 0, 1, 1);
            ShowTextureHint(why);
            return;
        }
        HideTextureHint();
        img = UnifiedImageToQImage(frame.textures[0]);
        if (img.isNull()) {
            pixmap_item_->setPixmap(QPixmap());
            scene_->setSceneRect(0, 0, 1, 1);
            ShowTextureHint(tr(u8"纹理数据解析失败（内部格式与 UnifiedImage 字段不一致）。"));
            return;
        }
    }
    if (!img.isNull()) {
        pixmap_item_->setPixmap(QPixmap::fromImage(img));
        scene_->setSceneRect(pixmap_item_->boundingRect());
        if (need_auto_fit_) {
            resetTransform();
            fitInView(pixmap_item_, Qt::KeepAspectRatio);
            centerOn(pixmap_item_);
            need_auto_fit_ = false;
        }
    }
}

void ImageView2D::SetDisplayMode(DisplayMode mode) {
    display_mode_ = mode;
}

void ImageView2D::Clear() {
    BtestBurst2dTeardown();
    pixmap_item_->setPixmap(QPixmap());
    need_auto_fit_ = true;
    HideTextureHint();
}

void ImageView2D::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    if (texture_hint_overlay_ && texture_hint_overlay_->isVisible())
        texture_hint_overlay_->setGeometry(rect().adjusted(48, 48, -48, -48));
}

void ImageView2D::wheelEvent(QWheelEvent* event) {
    need_auto_fit_ = false;
    qreal k = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    scale(k, k);
}

}  // namespace scanner_viewer
