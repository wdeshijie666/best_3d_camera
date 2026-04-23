// 与 examples/minimal_app/CMakeLists.txt 配套：InitLogging → InstallCrashHandlers → 打一条日志
#include "platform_diag/diag_config.h"
#include "platform_diag/logging.h"
#include "platform_diag/crash_handler.h"

int main() {
  camera3d::diag::DiagConfig cfg;
  cfg.log_dir = "logs";
  cfg.log_file_stem = "minimal_app";
  cfg.crash_dump_dir = "logs/crash";
  cfg.console_sink = true;
  cfg.log_level = "info";

  camera3d::diag::InitLogging(cfg, "minimal");
  camera3d::diag::InstallCrashHandlers(cfg.crash_dump_dir, "minimal_app");

  CAMERA3D_LOGI("platform_diag 示例已启动（spdlog + crash_handler，Windows 下可生成 .dmp）");
  return 0;
}
