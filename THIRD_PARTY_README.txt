3D 相机工程 — 第三方库目录说明（THIRD_PARTY_LIBRARY_DIR）
============================================================

默认路径：D:\work\depthvision\GitLab\3rdParty（可在 CMake 配置时用 -DTHIRD_PARTY_LIBRARY_DIR=... 或环境变量覆盖）

CMake 已将下列子路径加入 CMAKE_PREFIX_PATH 前缀搜索（可按实际目录增删）：
  grpc、protobuf、boost、spdlog、breakpad

请在 THIRD_PARTY_LIBRARY_DIR 中准备（或保证 find_package 能定位到）：

1) Boost（建议 1.90.x，与现场一致）
   - Interprocess 用于共享内存 ipc_shmem；头文件库即可。
   - 若使用 CONFIG 模式：设置 Boost_DIR 或 Boost_ROOT 指向带 BoostConfig.cmake 的安装根。

2) spdlog（必需）
   - 需 spdlogConfig.cmake，目标 spdlog::spdlog 或 spdlog::spdlog_header_only；或仅头文件目录 spdlog/include（工程内建 camera3d_spdlog_headers）。
   - platform_diag 使用异步 logger + 滚动文件 + 可选控制台。
   - 中枢/检测/重建/SDK 均通过 platform_diag 打日志，勿在各 exe 内重复实现。

共享内存约定
------------
- 默认区名与容量见头文件 ipc_shmem/shm_constants.h（kDefaultHubRingRegionName 等），hub、detect、recon 已统一引用。
- ShmRingBuffer 提供 MaxPublishedSeq、TryReadSlot、TryReadLatestSlot 供检测/重建消费。

仓库内模块（非 3rdParty）
------------------------
- pipeline_api：IDetectionPipeline / IReconstructionPipeline 与 ShmFrameView；含 NoOp 与 Logging 实现。
- capture_orchestrator：HardTriggerOrchestrator（硬触发 + 串口 + 相机异步回调收帧；可选 CollectViaCallback / 两阶段 Prepare）。
- serial_port：ISerialPort（Win32 与 SpeedSerialPort 同样开启 RTS）、speed_serial_projector（SpeedSerialPort 协议）、ProjectorCommandBuilder。

3) gRPC C++ + Protobuf（可选，未找到时自动 CAMERA3D_USE_GRPC_STUB=ON）
   - 需 gRPCConfig.cmake、ProtobufConfig.cmake，以及 protoc、grpc_cpp_plugin。

4) Google Breakpad 客户端（可选）
   - 头路径下含 client/linux/handler/exception_handler.h 等时定义 CAMERA3D_HAVE_BREAKPAD。
   - Windows 上仍默认用 dbghelp 写 MiniDump；Breakpad 便于与其它平台统一崩溃产物流程。

5) 大恒 Galaxy GxIAPICPP（可选，真实取流）
   - 头文件默认：THIRD_PARTY_LIBRARY_DIR/DaHeng SDK/inc（GalaxyIncludes.h）。
   - 链接：需完整 SDK 中的 GxIAPICPPEx.lib（及运行时 DLL，通常随 Galaxy 安装包提供）。
   - CMake：先保证 -DCAMERA3D_ENABLE_ADAPTER_DAHENG=ON（默认 ON），再 -DCAMERA3D_WITH_DAHENG_GALAXY=ON -DDAHENG_GALAXY_LIB_DIR=<.../Development/C++ SDK/lib/x64>
   - 未链接 Galaxy 时仍编译大恒桩适配器；将 CAMERA3D_ENABLE_ADAPTER_DAHENG=OFF 时完全不编译大恒相关源码（见 libs/camera_driver/README.md）。

构建后 DLL 拷贝
---------------
- 目标 camera3d_copy_all_third_party_dlls 会递归拷贝 THIRD_PARTY_LIBRARY_DIR 下所有 .dll 到构建 bin（在未启用「真实大恒 Galaxy 链路」时自动跳过常见 GxI*.dll，见 cmake/CopyThirdPartyRuntimeScript.cmake）。
- 各 exe/dll 在 POST_BUILD 也会执行一次拷贝（见 cmake/CopyThirdPartyRuntime.cmake）。

后续工程项（非 3rdParty 缺失）
-----------------------------
- 若 CAMERA3D_USE_GRPC_STUB=ON：在 3rdParty 中补齐 gRPC/Protobuf 后重新 CMake 配置，并设为 OFF。
- 大恒相机适配器、串口指令与 geelydetectapp/SpeedSerialPort 对齐：在仓库内实现，不依赖 3rdParty 再增库（除 GxIAPI）。
- 重建算法库：由产品侧提供后再链接 recon_service（检测与重建合并到重建侧时一并接入）。
