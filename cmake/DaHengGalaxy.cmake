# 可选：大恒 Galaxy GxIAPICPP（C++ SDK）— 头文件与 GxIAPICPPEx.lib
# 头文件默认使用 THIRD_PARTY 中的「DaHeng SDK/inc」；完整安装通常另含 lib/x64、运行时 DLL。

set(CAMERA3D_WITH_DAHENG_GALAXY OFF CACHE BOOL "链接 GxIAPICPP 并编译真实大恒相机适配器（Windows）")
set(DAHENG_GALAXY_FOUND FALSE)

set(DAHENG_GALAXY_SDK_ROOT "${THIRD_PARTY_LIBRARY_DIR}/DaHeng SDK" CACHE PATH
  "Galaxy C++ API 头文件目录（内含 inc/GalaxyIncludes.h）")
set(DAHENG_GALAXY_LIB_DIR "" CACHE PATH
  "GxIAPICPPEx.lib 所在目录（如完整 SDK 的「C++ SDK/lib/x64」）；留空则尝试 SDK_ROOT/lib/<arch>")

if(NOT CAMERA3D_WITH_DAHENG_GALAXY)
  return()
endif()

if(NOT WIN32)
  message(WARNING "CAMERA3D_WITH_DAHENG_GALAXY 当前仅在 Windows 上支持；已关闭。")
  set(CAMERA3D_WITH_DAHENG_GALAXY OFF CACHE BOOL "..." FORCE)
  return()
endif()

find_path(DAHENG_GALAXY_INCLUDE_DIR
  NAMES GalaxyIncludes.h
  PATHS "${DAHENG_GALAXY_SDK_ROOT}/inc"
  NO_DEFAULT_PATH)

if(NOT DAHENG_GALAXY_INCLUDE_DIR)
  message(WARNING "未找到 GalaxyIncludes.h（DAHENG_GALAXY_SDK_ROOT=${DAHENG_GALAXY_SDK_ROOT}）；已关闭 CAMERA3D_WITH_DAHENG_GALAXY。")
  set(CAMERA3D_WITH_DAHENG_GALAXY OFF CACHE BOOL "..." FORCE)
  return()
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_daheng_galaxy_lib_arch "x64")
else()
  set(_daheng_galaxy_lib_arch "x86")
endif()

set(_daheng_lib_search "")
if(DAHENG_GALAXY_LIB_DIR AND NOT DAHENG_GALAXY_LIB_DIR STREQUAL "")
  list(APPEND _daheng_lib_search "${DAHENG_GALAXY_LIB_DIR}")
endif()
list(APPEND _daheng_lib_search
  "${DAHENG_GALAXY_SDK_ROOT}/lib/${_daheng_galaxy_lib_arch}"
  "${DAHENG_GALAXY_SDK_ROOT}/C++ SDK/lib/${_daheng_galaxy_lib_arch}")

foreach(_dir IN LISTS _daheng_lib_search)
  if(EXISTS "${_dir}/GxIAPICPPEx.lib")
    set(DAHENG_GALAXY_LIB_DIR_RESOLVED "${_dir}")
    break()
  endif()
endforeach()

if(NOT DAHENG_GALAXY_LIB_DIR_RESOLVED)
  message(WARNING
    "未找到 GxIAPICPPEx.lib。请将完整 Galaxy C++ SDK 的 lib 路径传给 -DDAHENG_GALAXY_LIB_DIR=...\n"
    "  典型目录：<Galaxy 安装>/Development/C++ SDK/lib/x64\n"
    "  已关闭 CAMERA3D_WITH_DAHENG_GALAXY。")
  set(CAMERA3D_WITH_DAHENG_GALAXY OFF CACHE BOOL "..." FORCE)
  return()
endif()

find_library(DAHENG_GALAXY_CPP_LIB GxIAPICPPEx PATHS "${DAHENG_GALAXY_LIB_DIR_RESOLVED}" NO_DEFAULT_PATH REQUIRED)

# 部分环境需同时链接 C API（示例工程仅写 GxIAPICPPEx；若链接错误再补 GxIAPI）
find_library(DAHENG_GALAXY_C_LIB GxIAPI PATHS "${DAHENG_GALAXY_LIB_DIR_RESOLVED}" NO_DEFAULT_PATH)

set(DAHENG_GALAXY_FOUND TRUE)
message(STATUS "DaHeng Galaxy: include ${DAHENG_GALAXY_INCLUDE_DIR}")
message(STATUS "DaHeng Galaxy: lib dir ${DAHENG_GALAXY_LIB_DIR_RESOLVED}")
