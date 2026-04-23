# 独立小工程：日志 + crash_handler。在**本目录**外配置、构建，例如：
#   cmake -S . -B build -DMY_SPDLOG_ROOT=D:/libs/spdlog
#   cmake --build build
#
# MY_SPDLOG：① install 树（含 spdlogConfig.cmake） 或 ② 仅头文件，且存在 include/spdlog/spdlog.h

cmake_minimum_required(VERSION 3.16)
project(platform_diag_minimal_example LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# spdlog（默认路径可改，或：cmake -DMY_SPDLOG_ROOT=你的路径）
# ---------------------------------------------------------------------------
set(MY_SPDLOG_ROOT "D:/libs/spdlog" CACHE PATH "spdlog 根目录：install 前缀，或头文件根（含 include/spdlog/）")

list(PREPEND CMAKE_PREFIX_PATH "${MY_SPDLOG_ROOT}")
list(PREPEND CMAKE_PREFIX_PATH "${MY_SPDLOG_ROOT}/install")
list(PREPEND CMAKE_PREFIX_PATH "${MY_SPDLOG_ROOT}/out/install")

find_package(spdlog CONFIG QUIET)
if(spdlog_FOUND)
  if(NOT (TARGET spdlog::spdlog OR TARGET spdlog::spdlog_header_only))
    find_package(spdlog CONFIG REQUIRED)
  endif()
else()
  if(EXISTS "${MY_SPDLOG_ROOT}/include/spdlog/spdlog.h")
    add_library(_minimal_spdlog_hdr INTERFACE)
    target_include_directories(_minimal_spdlog_hdr SYSTEM INTERFACE "${MY_SPDLOG_ROOT}/include")
    add_library(spdlog::spdlog_header_only ALIAS _minimal_spdlog_hdr)
    message(STATUS "minimal_app: 使用头文件 spdlog: ${MY_SPDLOG_ROOT}/include")
  else()
    message(FATAL_ERROR
      "未找到 spdlog。请设置 MY_SPDLOG_ROOT（当前=${MY_SPDLOG_ROOT}），\n"
      "  或安装 spdlog 到该路径下，使存在 include/spdlog/spdlog.h 或 lib/cmake/spdlog/spdlogConfig.cmake。")
  endif()
endif()

find_package(Threads REQUIRED)

# platform_diag 源码：本文件位于 platform_diag/examples/minimal_app/，故上两级为 platform_diag 根
set(_PLATFORM_DIAG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
get_filename_component(_PLATFORM_DIAG_ROOT "${_PLATFORM_DIAG_ROOT}" ABSOLUTE)
if(NOT EXISTS "${_PLATFORM_DIAG_ROOT}/CMakeLists.txt")
  message(FATAL_ERROR "未找到 platform_diag: ${_PLATFORM_DIAG_ROOT}")
endif()
add_subdirectory("${_PLATFORM_DIAG_ROOT}" "${CMAKE_BINARY_DIR}/_build_platform_diag")

add_executable(minimal_app main.cpp)
target_link_libraries(minimal_app PRIVATE platform_diag)
