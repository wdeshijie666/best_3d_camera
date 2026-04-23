#pragma once

// =============================================================================
// 临时联调：2D 深度区按 0.1s 轮播多帧 raw（如 24 张），仅 BestCamera/Hub 路径带 hardware_raw_frames 时有效。
// 要彻底移除时：删本头文件 + 取消 CMake 选项 + 删除 ImageView2D 中 btest_ 相关成员与 ImageView2D.cpp 的 #if 整段即可。
// =============================================================================
#ifndef CAMERA_VIEWER_BTEST_BURST_2D_SLIDESHOW
// 0=关闭，保持原有「仅显第一帧」与刷新逻辑
#define CAMERA_VIEWER_BTEST_BURST_2D_SLIDESHOW 0
#endif

#ifndef CAMERA_VIEWER_BTEST_BURST_2D_SLIDESHOW_MS
#define CAMERA_VIEWER_BTEST_BURST_2D_SLIDESHOW_MS 100
#endif
