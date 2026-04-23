/**
 * @file UnifiedFrame.h
 * @brief 数据中心统一帧：深度图、纹理图、点云等，与具体相机 SDK 解耦
 */
#ifndef SCANNER_VIEWER_UNIFIED_FRAME_H
#define SCANNER_VIEWER_UNIFIED_FRAME_H

#include <vector>
#include <cstdint>
#include <chrono>

namespace scanner_viewer {

/** 统一图像：宽高、通道、数据类型（8/16 位灰度或 RGB） */
struct UnifiedImage {
    int width{0};
    int height{0};
    int channels{1};
    bool is_16bit{false};
    std::vector<uint8_t> data;
};

/** 统一点：xyz + 可选 rgb */
struct UnifiedPoint {
    float x{0.f}, y{0.f}, z{0.f};
    uint8_t r{0}, g{0}, b{0};
    bool has_color{false};
};

/** 统一帧：供 2D 视窗与 3D 视窗消费 */
struct UnifiedFrame {
    std::chrono::steady_clock::time_point timestamp;
    int frame_index{0};

    // 深度图（可选）
    UnifiedImage depth;
    /// 单次硬触发多帧原始图（如 24 张）；2D 预览仍用 depth（通常为第一帧）
    std::vector<UnifiedImage> hardware_raw_frames;
    // 纹理/彩色图（可选，多张则用 textures[0] 等）
    std::vector<UnifiedImage> textures;
    // 点云（可选，由适配器或数据中心从深度+标定解算）
    std::vector<UnifiedPoint> point_cloud;
    int point_cloud_width{0};   // 有序点云宽
    int point_cloud_height{0};  // 有序点云高

    bool IsValid() const {
        return (depth.width > 0 && depth.height > 0) ||
               (!hardware_raw_frames.empty() && hardware_raw_frames[0].width > 0) ||
               (!point_cloud.empty()) ||
               (!textures.empty() && textures[0].width > 0);
    }
};

}  // namespace scanner_viewer

#endif  // SCANNER_VIEWER_UNIFIED_FRAME_H
