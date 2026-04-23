# Best 3D 相机 Viewer：基于 scannerviewer-demo 架构，接入本仓库 camera_sdk + ipc_shmem（Hub SHM 读深度）。
# 依赖 Qt5、ElaWidgetTools、VTK、spdlog；OpenCV 可选（当前视图未用 Mat，预留链接）。

if(NOT TARGET camera_sdk)
  message(FATAL_ERROR "camera_viewer 需要 camera_sdk")
endif()
if(NOT TARGET ipc_shmem)
  message(FATAL_ERROR "camera_viewer 需要 ipc_shmem")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(_SV "${CMAKE_CURRENT_SOURCE_DIR}/scanner_viewer_src")

option(CAMERA_VIEWER_BTEST_BURST_2D_SLIDESHOW
  "2D 深度区按 0.1s 轮播多帧 raw（联调/测试，默认关；头文件见 common/camera_viewer_btest.h，便于移除整段 btest 代码）"
  OFF)

# 与参考工程一致：可用 -DTHIRD_PARTY_LIBRARY_DIR=... 覆盖（继承父工程 CACHE）。
if(NOT DEFINED THIRD_PARTY_LIBRARY_DIR OR THIRD_PARTY_LIBRARY_DIR STREQUAL "")
  set(THIRD_PARTY_LIBRARY_DIR "D:/work/depthvision/GitLab/3rdParty" CACHE PATH "第三方库根")
endif()
set(VTK_ROOT "${THIRD_PARTY_LIBRARY_DIR}/VTK" CACHE PATH "VTK 安装根")
set(SPDLOG_ROOT "${THIRD_PARTY_LIBRARY_DIR}/spdlog" CACHE PATH "spdlog 根")
set(ELA_WIDGET_TOOLS_ROOT "${THIRD_PARTY_LIBRARY_DIR}/ElaWidgetTools" CACHE PATH "ElaWidgetTools 预编译根")

list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_DIR}")

find_package(Qt5 REQUIRED COMPONENTS Widgets Core Gui Concurrent)

set(_ELA_CFG "${ELA_WIDGET_TOOLS_ROOT}/lib/cmake/ElaWidgetToolsConfig.cmake")
if(NOT EXISTS "${_ELA_CFG}")
  message(FATAL_ERROR "未找到 ElaWidgetTools: ${_ELA_CFG}。请设置 ELA_WIDGET_TOOLS_ROOT 或 THIRD_PARTY_LIBRARY_DIR。")
endif()
find_package(ElaWidgetTools REQUIRED CONFIG PATHS "${ELA_WIDGET_TOOLS_ROOT}/lib/cmake" NO_DEFAULT_PATH)

if(NOT VTK_DIR AND EXISTS "${VTK_ROOT}/lib/cmake/vtk-8.2/VTKConfig.cmake")
  set(VTK_DIR "${VTK_ROOT}/lib/cmake/vtk-8.2" CACHE PATH "VTK CMake 目录" FORCE)
elseif(NOT VTK_DIR AND EXISTS "${VTK_ROOT}/VTKConfig.cmake")
  set(VTK_DIR "${VTK_ROOT}" CACHE PATH "VTK CMake 目录" FORCE)
endif()
find_package(VTK 8.2.0 REQUIRED PATHS "${VTK_DIR}" NO_DEFAULT_PATH)
if(NOT EXISTS "${VTK_RUNTIME_DIRS}")
  if(WIN32)
    set(VTK_RUNTIME_DIRS "${VTK_INSTALL_PREFIX}/bin")
  else()
    set(VTK_RUNTIME_DIRS "${VTK_INSTALL_PREFIX}/lib")
  endif()
endif()
include(${VTK_USE_FILE})
add_definitions(-DUSE_VTK)

find_package(OpenCV QUIET COMPONENTS core imgproc)

set(CAMERA_VIEWER_SOURCES
  ${_SV}/main.cpp
  ${_SV}/app/MainWindow.cpp
  ${_SV}/app/MainWindow.h
  ${_SV}/model/data_center/DataCenter.cpp
  ${_SV}/model/data_center/DataCenter.h
  ${_SV}/model/device_layer/BestCamera3DAdapter.cpp
  ${_SV}/model/device_layer/BestCamera3DAdapter.h
  ${_SV}/controller/AcquisitionController.cpp
  ${_SV}/controller/AcquisitionController.h
  ${_SV}/controller/DeviceController.cpp
  ${_SV}/controller/DeviceController.h
  ${_SV}/view/viewer_core/ImageView2D.cpp
  ${_SV}/view/viewer_core/ImageView2D.h
  ${_SV}/view/raw_album/RawImageAlbumPage.cpp
  ${_SV}/view/raw_album/RawImageAlbumPage.h
  ${_SV}/view/viewer_core/PointCloudView3D.cpp
  ${_SV}/view/viewer_core/PointCloudView3D.h
  ${_SV}/view/viewer_core/RangeSlider.h
  ${_SV}/view/param_ui/CameraParamPanel.cpp
  ${_SV}/view/param_ui/CameraParamPanel.h
  ${_SV}/common/log/Logger.cpp
  ${_SV}/common/log/Logger.h
  ${_SV}/common/unified_image_qt.cpp
  ${_SV}/common/unified_image_qt.h
)

add_executable(camera_viewer ${CAMERA_VIEWER_SOURCES})

if(CAMERA_VIEWER_BTEST_BURST_2D_SLIDESHOW)
  target_compile_definitions(camera_viewer PRIVATE CAMERA_VIEWER_BTEST_BURST_2D_SLIDESHOW=1)
endif()

target_include_directories(camera_viewer PRIVATE
  "${_SV}"
  "${SPDLOG_ROOT}/include"
)

target_link_libraries(camera_viewer PRIVATE
  camera_sdk
  ipc_shmem
  Qt5::Widgets
  Qt5::Core
  Qt5::Gui
  Qt5::Concurrent
  ElaWidgetTools
  ${VTK_LIBRARIES}
)

if(OpenCV_FOUND)
  target_link_libraries(camera_viewer PRIVATE ${OpenCV_LIBS})
  target_compile_definitions(camera_viewer PRIVATE CAMERA_VIEWER_HAVE_OPENCV=1)
endif()

target_include_directories(camera_viewer PRIVATE ${VTK_INCLUDE_DIRS})

if(WIN32)
  get_filename_component(_QT_BIN_DIR "${Qt5_DIR}/../../../bin" ABSOLUTE)
  set(_WINDEPLOYQT "${_QT_BIN_DIR}/windeployqt.exe")
  if(EXISTS "${_WINDEPLOYQT}")
    add_custom_command(TARGET camera_viewer POST_BUILD
      COMMAND ${CMAKE_COMMAND}
        -DCONFIG=$<CONFIG>
        -DEXE=$<TARGET_FILE:camera_viewer>
        -DWINDEPLOYQT_EXE=${_WINDEPLOYQT}
        -P "${_SV}/cmake/RunWindeployqt.cmake"
      COMMENT "windeployqt camera_viewer"
    )
  endif()
  if(DEFINED VTK_RUNTIME_DIRS AND EXISTS "${VTK_RUNTIME_DIRS}")
    add_custom_command(TARGET camera_viewer POST_BUILD
      COMMAND ${CMAKE_COMMAND}
        -DVTK_RUNTIME_DIRS=${VTK_RUNTIME_DIRS}
        -DDEST_DIR=$<TARGET_FILE_DIR:camera_viewer>
        -P "${_SV}/cmake/CopyVtkDlls.cmake"
      COMMENT "拷贝 VTK DLL"
    )
  endif()
  add_custom_command(TARGET camera_viewer POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      -DELA_WIDGET_TOOLS_ROOT=${ELA_WIDGET_TOOLS_ROOT}
      -DDEST_DIR=$<TARGET_FILE_DIR:camera_viewer>
      -DCONFIG=$<CONFIG>
      -P "${_SV}/cmake/CopyElaWidgetToolsDlls.cmake"
    COMMENT "拷贝 ElaWidgetTools DLL"
  )
endif()

camera3d_register_post_build_copy_dlls(camera_viewer)

install(TARGETS camera_viewer RUNTIME DESTINATION ${CAMERA_3D_INSTALL_BINDIR})
