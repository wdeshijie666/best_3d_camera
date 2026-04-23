#pragma once

#include "camera_driver/camera_types.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace camera3d::hub {

/// 将单次硬触发收齐的 burst 原始帧写入当前工作目录下 hub_img_save/<时间戳>/（.tiff）。
/// 仅在编译定义 CAMERA3D_ENABLE_HUB_CAPTURE_BURST_TIFF_SAVE 且链接 OpenCV 时由 hub_app_grpc 调用。
void HubTrySaveBurstFramesTiff(
    const std::unordered_map<std::string, std::vector<camera3d::camera::FrameBuffer>>& bursts_by_id,
    const std::vector<std::string>& camera_order, std::uint64_t capture_id, std::uint32_t frames_per);

}  // namespace camera3d::hub
