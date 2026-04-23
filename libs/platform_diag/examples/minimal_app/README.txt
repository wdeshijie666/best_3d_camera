见上级文档：libs/platform_diag/EXAMPLE_CMAKE_SPDLOG.md

本目录在仓库内路径：libs/platform_diag/examples/minimal_app/

配置与编译（在 minimal_app 目录下）：

  cmake -S . -B build -DMY_SPDLOG_ROOT=D:/libs/spdlog
  cmake --build build

运行后：日志 logs/minimal_app.log；Windows 崩溃时 dumps 在 logs/crash/
