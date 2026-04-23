#pragma once

// 各 backend 适配器工厂声明；实现位于 src/null_camera_adapter.cpp、daheng_* 等。

#include "camera_driver/icamera_adapter.h"

#include <memory>

namespace camera3d::camera {

std::shared_ptr<ICameraAdapter> CreateNullCameraAdapter();

#if CAMERA3D_ENABLE_ADAPTER_DAHENG
std::shared_ptr<ICameraAdapter> CreateDaHengCameraAdapter();
std::shared_ptr<ICameraAdapter> CreateDaHengCameraAdapterStub();
#endif

}  // namespace camera3d::camera
