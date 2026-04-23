/**
 * @file DeviceModelTags.h
 * @brief DeviceInfo::model_name 常量，避免在未启用某款相机时仍 include 对应适配器头文件。
 */
#ifndef SCANNER_VIEWER_DEVICE_MODEL_TAGS_H
#define SCANNER_VIEWER_DEVICE_MODEL_TAGS_H

namespace scanner_viewer {

/** 与 HikvisionAdapter::kDeviceModelTag 一致；用于 DeviceController / MainWindow 等。 */
inline constexpr const char* kDeviceModelHikvisionMv3d = "Hikvision-MV3D";
/** 与 ASeriesAdapter 枚举写入的 model_name 一致。 */
inline constexpr const char* kDeviceModelASeries = "A-Series";
/** best_project camera_sdk / Hub 发现设备。 */
inline constexpr const char* kDeviceModelBestCameraHub = "Best-CameraHub";

}  // namespace scanner_viewer

#endif
