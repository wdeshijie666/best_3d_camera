#include "hub_app.h"

#include "camera_driver/camera_manager.h"
#include "platform_diag/logging.h"

#include <chrono>
#include <thread>

namespace camera3d::hub {

int RunHubApp(const std::string& listen_address, camera3d::camera::CameraManager& cameras,
              const std::string& hub_config_path) {
  (void)hub_config_path;
  (void)cameras;
  CAMERA3D_LOGW("CameraHub 未启用 gRPC（CAMERA3D_USE_GRPC_STUB）：listen={} 无效", listen_address);
  CAMERA3D_LOGW("请在 THIRD_PARTY 提供 gRPC/Protobuf CMake 包后重新配置，并设置 CAMERA3D_USE_GRPC_STUB=OFF");
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(10));
  }
  return 0;
}

}  // namespace camera3d::hub
