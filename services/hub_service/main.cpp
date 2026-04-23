// Hub 进程入口：诊断初始化、注册相机适配器、读取监听地址后调用 RunHubApp。

#include "hub_app.h"
#include "hub_file_config.h"

#include "camera_driver/adapters.h"
#include "camera_driver/camera_manager.h"
#include "platform_diag/crash_handler.h"
#include "platform_diag/diag_config.h"
#include "platform_diag/logging.h"

#include <cstdlib>
#include <filesystem>
#include <string>

// 底层中枢：platform_diag（spdlog + 可选 MiniDump）；对上层 gRPC；对重建等 Boost 共享内存。
int main(int argc, char** argv) {
  camera3d::diag::DiagConfig diag;
  diag.log_file_stem = "hub_service";
  diag.crash_dump_dir = "logs/crash/hub_service";
  camera3d::diag::InitLogging(diag, "hub_service");
  camera3d::diag::InstallCrashHandlers(diag.crash_dump_dir, "hub_service");

  std::string config_path = "config/hub_service.json";
  if (const char* ev = std::getenv("CAMERA3D_HUB_CONFIG")) {
    if (ev[0] != '\0') {
      config_path = ev;
    }
  }

  std::string listen = "0.0.0.0:50051";
  if (argc >= 2) {
    listen = argv[1];
  } else {
    camera3d::hub::HubFileConfig fc;
    std::string cerr;
    std::error_code ec;
    if (std::filesystem::exists(config_path, ec) && !ec) {
      if (camera3d::hub::LoadHubFileConfig(config_path, fc, cerr)) {
        listen = fc.listen_address;
      }
    }
  }

  camera3d::camera::CameraManager cameras;
  cameras.RegisterAdapter(camera3d::camera::CreateNullCameraAdapter());
#if CAMERA3D_ENABLE_ADAPTER_DAHENG
  cameras.RegisterAdapter(camera3d::camera::CreateDaHengCameraAdapter());
#endif

  return camera3d::hub::RunHubApp(listen, cameras, config_path);
}
