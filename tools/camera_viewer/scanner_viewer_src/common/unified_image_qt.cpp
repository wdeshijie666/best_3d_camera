/**
 * @file unified_image_qt.cpp
 * @brief UnifiedImage → QImage
 */
#include "unified_image_qt.h"
#include "model/data_center/UnifiedFrame.h"
#include <cstring>
#include <cstdint>

namespace scanner_viewer {

QImage UnifiedImageToQImage(const UnifiedImage& img) {
  if (img.width <= 0 || img.height <= 0 || img.data.empty()) {
    return QImage();
  }
  if (img.is_16bit && img.channels == 1) {
    const auto* src = reinterpret_cast<const uint16_t*>(img.data.data());
    const size_t n = static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
    if (img.data.size() < n * sizeof(uint16_t)) {
      return QImage();
    }
    uint16_t max_v = 0;
    for (size_t i = 0; i < n; i++) {
      if (src[i] > max_v) {
        max_v = src[i];
      }
    }
    if (max_v == 0) {
      max_v = 1;
    }
    QImage out(img.width, img.height, QImage::Format_Grayscale8);
    for (int y = 0; y < img.height; y++) {
      auto* line = out.scanLine(y);
      for (int x = 0; x < img.width; x++) {
        line[x] = static_cast<uchar>((src[y * img.width + x] * 255) / max_v);
      }
    }
    return out;
  }
  if (img.channels == 1) {
    QImage out(img.width, img.height, QImage::Format_Grayscale8);
    const size_t sz = static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
    if (img.data.size() >= sz) {
      std::memcpy(out.bits(), img.data.data(), sz);
    }
    return out;
  }
  if (img.channels >= 3) {
    QImage out(img.width, img.height, QImage::Format_RGB888);
    const uint8_t* src = img.data.data();
    for (int y = 0; y < img.height; y++) {
      auto* line = reinterpret_cast<uchar*>(out.scanLine(y));
      for (int x = 0; x < img.width; x++) {
        line[x * 3] = src[(y * img.width + x) * img.channels + 2];
        line[x * 3 + 1] = src[(y * img.width + x) * img.channels + 1];
        line[x * 3 + 2] = src[(y * img.width + x) * img.channels];
      }
    }
    return out;
  }
  return QImage();
}

}  // namespace scanner_viewer
