/**
 * @file unified_image_qt.h
 * @brief UnifiedImage 与 QImage 的转换，供 2D 视窗与落盘等共用
 */
#ifndef SCANNER_VIEWER_UNIFIED_IMAGE_QT_H
#define SCANNER_VIEWER_UNIFIED_IMAGE_QT_H

#include <QImage>

namespace scanner_viewer {

struct UnifiedImage;

/// 与 ImageView2D 显示逻辑一致：8 位单通道/16 位单通道(归一化到 8 位)/多通道(按 BGR 排布转 RGB)
QImage UnifiedImageToQImage(const UnifiedImage& img);

}  // namespace scanner_viewer

#endif
