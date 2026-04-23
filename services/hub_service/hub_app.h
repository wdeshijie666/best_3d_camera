#pragma once

// Hub 进程入口：加载配置、初始化运行时状态并阻塞运行 gRPC 服务（见 hub_app_grpc.cpp）。

#include <cstdint>
#include <string>
#include <vector>

namespace camera3d::camera {
class CameraManager;
}

namespace camera3d::hub {

// 由配置文件 + 统一启动初始化后得到的相机槽位（从左到右）。
struct HubPresetCamera {
  std::string manager_device_id;
  std::string ip;
  std::string serial_number;
};

// 阻塞运行中枢主循环；CAMERA3D_USE_GRPC_STUB 时无 gRPC 监听，仅验证 spdlog/崩溃捕获与相机抽象。
// hub_config_path：在启动 gRPC 前尝试统一初始化；失败时写日志并更新服务状态码，进程仍继续监听。
// 状态码与 SDK 共用 camera3d/hub/hub_service_state_codes.h。Connect 仅返回状态码与 session_id，不在此创建设备。
int RunHubApp(const std::string& listen_address, camera3d::camera::CameraManager& cameras,
              const std::string& hub_config_path = "config/hub_service.json");

}  // namespace camera3d::hub
