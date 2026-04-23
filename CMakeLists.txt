# 3D 相机底层服务 + 上层 SDK 根工程
# 遵循 cmake-config-skill：统一 THIRD_PARTY_LIBRARY_DIR、安装与 *Config.cmake

cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)  # 允许 FindBoost 定位 boost_1_90_0 等未 CMake 安装的源码树
endif()
if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW)  # 允许 find_package 使用 <PackageName>_ROOT
endif()
project(camera_3d_stack LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  add_compile_options(/utf-8)
endif()

# ---------------------------------------------------------------------------
# 三方库根目录（可通过 -DTHIRD_PARTY_LIBRARY_DIR=... 或环境变量覆盖）
# ---------------------------------------------------------------------------
if(NOT DEFINED THIRD_PARTY_LIBRARY_DIR OR THIRD_PARTY_LIBRARY_DIR STREQUAL "")
  if(DEFINED ENV{THIRD_PARTY_LIBRARY_DIR} AND NOT "$ENV{THIRD_PARTY_LIBRARY_DIR}" STREQUAL "")
    set(THIRD_PARTY_LIBRARY_DIR "$ENV{THIRD_PARTY_LIBRARY_DIR}" CACHE PATH "统一三方库根目录")
  else()
    set(THIRD_PARTY_LIBRARY_DIR "D:/work/depthvision/GitLab/3rdParty" CACHE PATH "统一三方库根目录")
  endif()
endif()

message(STATUS "THIRD_PARTY_LIBRARY_DIR = ${THIRD_PARTY_LIBRARY_DIR}")

list(PREPEND CMAKE_PREFIX_PATH "${THIRD_PARTY_LIBRARY_DIR}")
list(PREPEND CMAKE_PREFIX_PATH "${THIRD_PARTY_LIBRARY_DIR}/grpc1_63_0")
list(PREPEND CMAKE_PREFIX_PATH "${THIRD_PARTY_LIBRARY_DIR}/grpc-master")
list(PREPEND CMAKE_PREFIX_PATH "${THIRD_PARTY_LIBRARY_DIR}/grpc")
list(PREPEND CMAKE_PREFIX_PATH "${THIRD_PARTY_LIBRARY_DIR}/protobuf")
list(PREPEND CMAKE_PREFIX_PATH "${THIRD_PARTY_LIBRARY_DIR}/boost_1_90_0")
list(PREPEND CMAKE_PREFIX_PATH "${THIRD_PARTY_LIBRARY_DIR}/boost")
list(PREPEND CMAKE_PREFIX_PATH "${THIRD_PARTY_LIBRARY_DIR}/spdlog")
list(PREPEND CMAKE_PREFIX_PATH "${THIRD_PARTY_LIBRARY_DIR}/spdlog/install")
list(PREPEND CMAKE_PREFIX_PATH "${THIRD_PARTY_LIBRARY_DIR}/spdlog/out/install")
list(PREPEND CMAKE_PREFIX_PATH "${THIRD_PARTY_LIBRARY_DIR}/breakpad-main")
list(PREPEND CMAKE_PREFIX_PATH "${THIRD_PARTY_LIBRARY_DIR}/breakpad")

# 统一输出目录（便于拷贝运行依赖）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# 默认安装前缀：构建目录下 install/（skill 要求）
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "install root" FORCE)
endif()

set(CAMERA_3D_INSTALL_BINDIR "bin")
set(CAMERA_3D_INSTALL_LIBDIR "lib")
set(CAMERA_3D_INSTALL_INCLUDEDIR "include")
set(CAMERA_3D_INSTALL_CMAKEDIR "cmake")

set(CAMERA3D_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include("${CAMERA3D_CMAKE_DIR}/CopyThirdPartyRuntime.cmake")

# ---------------------------------------------------------------------------
# 依赖探测
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)

# gRPC/Protobuf 根目录（参考 test_grpc 的引入方式）
if(DEFINED gRPC_ROOT AND NOT "${gRPC_ROOT}" STREQUAL "")
  set(_camera3d_grpc_root "${gRPC_ROOT}")
elseif(DEFINED GRPC_ROOT AND NOT "${GRPC_ROOT}" STREQUAL "")
  set(_camera3d_grpc_root "${GRPC_ROOT}")
elseif(EXISTS "D:/work/depthvision/GitLab/ui_version/version_use_new_sdk/3rdParty/grpc1_63_0")
  # 对齐 test_grpc 示例工程中的默认安装目录
  set(_camera3d_grpc_root "D:/work/depthvision/GitLab/ui_version/version_use_new_sdk/3rdParty/grpc1_63_0")
elseif(EXISTS "${THIRD_PARTY_LIBRARY_DIR}/grpc1_63_0")
  set(_camera3d_grpc_root "${THIRD_PARTY_LIBRARY_DIR}/grpc1_63_0")
elseif(EXISTS "${THIRD_PARTY_LIBRARY_DIR}/grpc")
  set(_camera3d_grpc_root "${THIRD_PARTY_LIBRARY_DIR}/grpc")
else()
  set(_camera3d_grpc_root "${THIRD_PARTY_LIBRARY_DIR}/grpc-master")
endif()
if(NOT IS_ABSOLUTE "${_camera3d_grpc_root}")
  get_filename_component(_camera3d_grpc_root "${CMAKE_CURRENT_SOURCE_DIR}/${_camera3d_grpc_root}" ABSOLUTE)
endif()
set(gRPC_ROOT "${_camera3d_grpc_root}" CACHE PATH "gRPC install root")
message(STATUS "gRPC_ROOT = ${gRPC_ROOT}")

list(PREPEND CMAKE_PREFIX_PATH "${gRPC_ROOT}")
list(PREPEND CMAKE_PREFIX_PATH "${gRPC_ROOT}/lib/cmake")
list(PREPEND CMAKE_PREFIX_PATH "${gRPC_ROOT}/lib/cmake/grpc")
list(PREPEND CMAKE_PREFIX_PATH "${gRPC_ROOT}/lib/cmake/protobuf")

# Boost.Interprocess（共享内存）；源码树常见为 boost_1_90_0/，无 CMake 包时用 FindBoost + BOOST_ROOT
if(EXISTS "${THIRD_PARTY_LIBRARY_DIR}/boost_1_90_0/boost/version.hpp")
  set(Boost_ROOT "${THIRD_PARTY_LIBRARY_DIR}/boost_1_90_0" CACHE PATH "Boost 根目录")
endif()
find_package(Boost CONFIG QUIET)
if(NOT Boost_FOUND)
  find_package(Boost REQUIRED)
endif()

# spdlog：优先 CONFIG（spdlog::spdlog 或 spdlog::spdlog_header_only）；否则在 THIRD_PARTY 下找头文件树做 INTERFACE（与 platform_diag 一致）
find_package(spdlog CONFIG QUIET)
set(_CAMERA3D_SPDLOG_TARGETS_READY FALSE)
if(spdlog_FOUND)
  if(TARGET spdlog::spdlog OR TARGET spdlog::spdlog_header_only)
    set(_CAMERA3D_SPDLOG_TARGETS_READY TRUE)
  endif()
endif()

if(NOT _CAMERA3D_SPDLOG_TARGETS_READY)
  set(_CAMERA3D_SPDLOG_INCLUDE "")
  set(_camera3d_spdlog_include_candidates
    "${THIRD_PARTY_LIBRARY_DIR}/spdlog/include"
    "${THIRD_PARTY_LIBRARY_DIR}/spdlog/install/include"
    "${THIRD_PARTY_LIBRARY_DIR}/spdlog/out/install/include"
    "${THIRD_PARTY_LIBRARY_DIR}/spdlog/build/install/include")
  foreach(_spd_inc IN LISTS _camera3d_spdlog_include_candidates)
    if(EXISTS "${_spd_inc}/spdlog/spdlog.h")
      set(_CAMERA3D_SPDLOG_INCLUDE "${_spd_inc}")
      break()
    endif()
  endforeach()

  if(_CAMERA3D_SPDLOG_INCLUDE)
    message(STATUS "spdlog: 使用 THIRD_PARTY 头路径（未找到可用 CMake 目标）${_CAMERA3D_SPDLOG_INCLUDE}")
    if(NOT TARGET camera3d_spdlog_headers)
      add_library(camera3d_spdlog_headers INTERFACE)
      target_include_directories(camera3d_spdlog_headers SYSTEM INTERFACE "${_CAMERA3D_SPDLOG_INCLUDE}")
      add_library(spdlog::spdlog_header_only ALIAS camera3d_spdlog_headers)
    endif()
  elseif(NOT spdlog_FOUND)
    find_package(spdlog CONFIG REQUIRED)
  else()
    message(FATAL_ERROR
      "spdlog: CMake 报告 spdlog_FOUND 但未定义 spdlog::spdlog / spdlog::spdlog_header_only，且在 THIRD_PARTY 中未找到 spdlog/spdlog.h。\n"
      "  请任选其一：\n"
      "  (1) 在 THIRD_PARTY_LIBRARY_DIR 下放置 spdlog 源码或安装树，且存在 .../include/spdlog/spdlog.h；\n"
      "  (2) 将带 spdlogConfig.cmake 的安装前缀加入 CMAKE_PREFIX_PATH（例如 spdlog 的 cmake install 目录的上一级）；\n"
      "  (3) 配置时 -DTHIRD_PARTY_LIBRARY_DIR=你的 3rdParty 根目录。")
  endif()
endif()

find_package(Protobuf CONFIG QUIET)
if(NOT Protobuf_FOUND)
  find_package(Protobuf CONFIG QUIET
    PATHS
      "${gRPC_ROOT}"
      "${gRPC_ROOT}/lib/cmake"
      "${gRPC_ROOT}/lib/cmake/protobuf")
endif()

if(NOT CMAKE_CROSSCOMPILING)
  find_package(gRPC CONFIG QUIET)
  if(NOT gRPC_FOUND)
    find_package(gRPC CONFIG QUIET
      PATHS
        "${gRPC_ROOT}"
        "${gRPC_ROOT}/lib/cmake"
        "${gRPC_ROOT}/lib/cmake/grpc")
  endif()
endif()
if(NOT Protobuf_FOUND OR NOT gRPC_FOUND)
  message(WARNING
    "未找到 Protobuf/gRPC（CONFIG）。请将带 CMake 配置的构建产物放入 THIRD_PARTY_LIBRARY_DIR，\n"
    "  并确保 CMAKE_PREFIX_PATH 能定位到 gRPCConfig.cmake / ProtobufConfig.cmake。\n"
    "  当前将使用 camera_proto_stub 占位，便于先编译相机抽象与诊断模块；接入真实 gRPC 后关闭 CAMERA3D_USE_GRPC_STUB。")
  set(CAMERA3D_USE_GRPC_STUB ON CACHE BOOL "无 gRPC 时使用桩实现联调" FORCE)
else()
  set(CAMERA3D_USE_GRPC_STUB OFF CACHE BOOL "无 gRPC 时使用桩实现联调" FORCE)
endif()

option(CAMERA3D_ENABLE_RECON_SHM_LOOPBACK_TEST
  "启用 Hub↔重建 共享内存+gRPC 回环联调（额外 RPC 与落盘；关闭此项可从编译期移除）"
  OFF)
if(CAMERA3D_ENABLE_RECON_SHM_LOOPBACK_TEST AND CAMERA3D_USE_GRPC_STUB)
  message(WARNING
    "CAMERA3D_ENABLE_RECON_SHM_LOOPBACK_TEST 需要真实 gRPC 生成代码；已强制关闭该选项。")
  set(CAMERA3D_ENABLE_RECON_SHM_LOOPBACK_TEST OFF CACHE BOOL
    "启用 Hub↔重建 共享内存+gRPC 回环联调（额外 RPC 与落盘；关闭此项可从编译期移除）"
    FORCE)
endif()

option(CAMERA3D_ENABLE_CAPTURE_INLINE_IMAGE_TEST
  "启用 Hub Capture 内联回传测试图片（从 D:/best_project/test_image 读取；仅联调用）"
  OFF)
if(CAMERA3D_ENABLE_CAPTURE_INLINE_IMAGE_TEST AND CAMERA3D_USE_GRPC_STUB)
  message(WARNING
    "CAMERA3D_ENABLE_CAPTURE_INLINE_IMAGE_TEST 需要真实 gRPC 代码生成；已强制关闭该选项。")
  set(CAMERA3D_ENABLE_CAPTURE_INLINE_IMAGE_TEST OFF CACHE BOOL
    "启用 Hub Capture 内联回传测试图片（从 D:/best_project/test_image 读取；仅联调用）"
    FORCE)
endif()

option(CAMERA3D_ENABLE_HUB_ALGO_BURST_PUSH_TEST
  "Hub 采集完成后占位日志/预留向算法服务推送多帧原始图（调试；默认关闭便于以后移除）"
  OFF)

option(CAMERA3D_ENABLE_HUB_CAPTURE_BURST_TIFF_SAVE
  "Hub 每次硬触发收齐 burst 后，将原始帧写入当前目录 hub_img_save/<时间戳>/（.tiff，需 OpenCV）"
  OFF)
if(CAMERA3D_ENABLE_HUB_CAPTURE_BURST_TIFF_SAVE AND CAMERA3D_USE_GRPC_STUB)
  message(WARNING
    "CAMERA3D_ENABLE_HUB_CAPTURE_BURST_TIFF_SAVE 需要真实 gRPC 的 hub_app_grpc；已强制关闭该选项。")
  set(CAMERA3D_ENABLE_HUB_CAPTURE_BURST_TIFF_SAVE OFF CACHE BOOL
    "Hub 每次硬触发收齐 burst 后，将原始帧写入当前目录 hub_img_save/<时间戳>/（.tiff，需 OpenCV）"
    FORCE)
endif()

find_path(BREAKPAD_CLIENT_INCLUDE_DIR
  NAMES client/linux/handler/exception_handler.h
  PATHS "${THIRD_PARTY_LIBRARY_DIR}/breakpad" "${THIRD_PARTY_LIBRARY_DIR}/google-breakpad"
  PATH_SUFFIXES src)
if(BREAKPAD_CLIENT_INCLUDE_DIR)
  set(CAMERA3D_HAVE_BREAKPAD ON CACHE BOOL "启用 Breakpad 客户端头文件路径")
else()
  set(CAMERA3D_HAVE_BREAKPAD OFF CACHE BOOL "启用 Breakpad 客户端头文件路径")
  message(STATUS "Breakpad 未在 THIRD_PARTY 中找到；Windows 下将使用 dbghelp 生成 MiniDump 作为兜底")
endif()

# ---------------------------------------------------------------------------
# 子目录
# ---------------------------------------------------------------------------
add_subdirectory(libs/platform_diag)
add_subdirectory(libs/camera_hub_api)
add_subdirectory(libs/camera_driver)
add_subdirectory(libs/serial_port)
add_subdirectory(libs/ipc_shmem)
add_subdirectory(libs/shm_loopback_test)
add_subdirectory(libs/pipeline_api)
add_subdirectory(libs/capture_orchestrator)

if(CAMERA3D_USE_GRPC_STUB)
  add_subdirectory(libs/camera_proto_stub)
else()
  add_subdirectory(libs/camera_proto)
endif()

add_subdirectory(services/hub_service)
add_subdirectory(services/recon_service)
add_subdirectory(sdk/camera_sdk)
add_subdirectory(tools/smoke_test)
add_subdirectory(tools/hub_connectivity_test)

option(CAMERA3D_BUILD_CAMERA_VIEWER
  "构建 Qt5+Ela+VTK 调试 Viewer（tools/camera_viewer，对齐 scannerviewer-demo；需 Qt5 与 ElaWidgetTools）" OFF)
if(CAMERA3D_BUILD_CAMERA_VIEWER)
  find_package(Qt5 QUIET COMPONENTS Core Widgets Gui Concurrent)
  if(Qt5_FOUND)
    add_subdirectory(tools/camera_viewer)
  else()
    message(WARNING "CAMERA3D_BUILD_CAMERA_VIEWER=ON 但未找到 Qt5（Core/Widgets/Gui/Concurrent），已跳过 camera_viewer。请设置 QT_DIR 或 CMAKE_PREFIX_PATH。")
  endif()
endif()

# DLL 复制由 camera3d_register_post_build_copy_dlls 挂载到各目标 POST_BUILD：
# 基于每个目标的真实运行时依赖复制，避免全量扫描 third-party 目录。
