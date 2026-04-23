# 3D 相机客户端 - 基于 3d-scanner-viewer-dedesign 技能
# 面阵相机（AIever-A 系列）集成，MVC + 数据中心，CMake 构建
cmake_minimum_required(VERSION 3.16)
project(ScannerViewer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options(/utf-8)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# -----------------------------------------------------------------------------
# 相机类型编译开关（源码中由 SCANNER_VIEWER_ENABLE_* 宏控制；关闭则不编译对应适配器、不链接对应 SDK）
# -----------------------------------------------------------------------------
option(SCANNER_VIEWER_ENABLE_A_SERIES "编译并启用 AIever-A 系列相机（AIR Scanner SDK）" OFF)
option(SCANNER_VIEWER_ENABLE_HIK_MV3D "编译并启用海康 MV3D / HiViewer SDK" ON)
if(NOT SCANNER_VIEWER_ENABLE_A_SERIES AND NOT SCANNER_VIEWER_ENABLE_HIK_MV3D)
    message(FATAL_ERROR "须至少开启 SCANNER_VIEWER_ENABLE_A_SERIES 或 SCANNER_VIEWER_ENABLE_HIK_MV3D 之一。")
endif()
message(STATUS "SCANNER_VIEWER_ENABLE_A_SERIES: ${SCANNER_VIEWER_ENABLE_A_SERIES}")
message(STATUS "SCANNER_VIEWER_ENABLE_HIK_MV3D: ${SCANNER_VIEWER_ENABLE_HIK_MV3D}")

# -----------------------------------------------------------------------------
# 路径配置（可通过 -D 覆盖）
# -----------------------------------------------------------------------------
set(SCANNER_VIEWER_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(REFACTOR_A_UI_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/.. CACHE PATH "refactor_a_ui 根目录")

# 第三方库统一根目录：请将 VTK、spdlog、ElaWidgetTools（预编译包）、海康 SDK、AIR SDK 等放到该目录下约定子路径，
# 或通过 -DTHIRD_PARTY_LIBRARY_DIR=... 指向本机集中存放位置。Qt 仍从环境变量 QT_DIR / CMAKE_PREFIX_PATH 获取。
set(THIRD_PARTY_LIBRARY_DIR "D:/work/depthvision/GitLab/3rdParty" CACHE PATH
    "第三方库统一根目录（可通过 -DTHIRD_PARTY_LIBRARY_DIR=... 覆盖）")

# 以下路径默认均由 THIRD_PARTY_LIBRARY_DIR 派生，亦可单独 -D 覆盖各 CACHE 变量。
set(VTK_ROOT "${THIRD_PARTY_LIBRARY_DIR}/VTK" CACHE PATH "VTK 安装根目录（默认 THIRD_PARTY_LIBRARY_DIR/VTK）")
set(SPDLOG_ROOT "${THIRD_PARTY_LIBRARY_DIR}/spdlog" CACHE PATH "spdlog 根目录（默认 THIRD_PARTY_LIBRARY_DIR/spdlog）")
set(ELA_WIDGET_TOOLS_ROOT "${THIRD_PARTY_LIBRARY_DIR}/ElaWidgetTools" CACHE PATH
    "ElaWidgetTools 预编译包根目录（含 include、lib、lib/cmake；默认 THIRD_PARTY_LIBRARY_DIR/ElaWidgetTools）")
# 海康 HiViewer MV3D：将 Development 目录整体放到 THIRD_PARTY_LIBRARY_DIR/HikHiViewer/Development
set(HIK_MV3D_ROOT "${THIRD_PARTY_LIBRARY_DIR}/HiViewer/Development" CACHE PATH
    "海康 HiViewer MV3D SDK 根目录（默认 THIRD_PARTY_LIBRARY_DIR/HikHiViewer/Development）")
# AIR Scanner SDK：仅 A 系列开启时使用；默认 THIRD_PARTY_LIBRARY_DIR/AIRScanner，可回退到工程内 sdk
set(AIR_SDK_DIR "${THIRD_PARTY_LIBRARY_DIR}/AIeveR-A SDK" CACHE PATH
    "AIR Scanner SDK 根目录（默认 THIRD_PARTY_LIBRARY_DIR/AIeveR-A SDK）")


message(STATUS "THIRD_PARTY_LIBRARY_DIR = ${THIRD_PARTY_LIBRARY_DIR}")
message(STATUS "VTK_ROOT = ${VTK_ROOT}")
message(STATUS "SPDLOG_ROOT = ${SPDLOG_ROOT}")
message(STATUS "ELA_WIDGET_TOOLS_ROOT = ${ELA_WIDGET_TOOLS_ROOT}")
message(STATUS "HIK_MV3D_ROOT = ${HIK_MV3D_ROOT}")
message(STATUS "AIR_SDK_DIR = ${AIR_SDK_DIR}")

set(HIK_MV3D_INCLUDES "${HIK_MV3D_ROOT}/Includes")
set(HIK_MV3D_LIB_DIR "${HIK_MV3D_ROOT}/Libraries/win64")
set(HIK_MV3D_IMPORT_LIB "${HIK_MV3D_LIB_DIR}/Mv3dRgbd.lib")
if(SCANNER_VIEWER_ENABLE_HIK_MV3D)
    option(HIK_MV3D_ASYNC_USE_FETCH_LOOP "海康异步采集改用 while+MV3D_RGBD_FetchFrame（OFF=注册回调）" OFF)
    if(WIN32)
        if(NOT EXISTS "${HIK_MV3D_IMPORT_LIB}")
            message(FATAL_ERROR "未找到海康 MV3D 导入库: ${HIK_MV3D_IMPORT_LIB}。请安装 SDK 或设置 HIK_MV3D_ROOT，或关闭 SCANNER_VIEWER_ENABLE_HIK_MV3D。")
        endif()
        if(NOT EXISTS "${HIK_MV3D_INCLUDES}/Mv3dRgbdApi.h")
            message(FATAL_ERROR "未找到海康 MV3D 头文件: ${HIK_MV3D_INCLUDES}/Mv3dRgbdApi.h")
        endif()
    endif()
endif()

if(SCANNER_VIEWER_ENABLE_A_SERIES)
    if(NOT EXISTS "${AIR_SDK_DIR}/include/AIRScanner.hpp")
        message(FATAL_ERROR "AIR SDK 未找到，请设置 AIR_SDK_DIR。当前: ${AIR_SDK_DIR}（或关闭 SCANNER_VIEWER_ENABLE_A_SERIES）")
    endif()
endif()
# -----------------------------------------------------------------------------
# Qt5（不放入 THIRD_PARTY_LIBRARY_DIR；由 QT_DIR / CMAKE_PREFIX_PATH 指定安装位置）
# ElaWidgetTools 的导入目标依赖 Qt5::Widgets，须在 find_package(ElaWidgetTools) 之前找到 Qt。
# -----------------------------------------------------------------------------
set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} $ENV{QT_DIR})
find_package(Qt5 REQUIRED COMPONENTS Widgets Core Gui Concurrent)
set(QT_VERSION_MAJOR 5)

set(_ELA_WIDGET_TOOLS_CONFIG "${ELA_WIDGET_TOOLS_ROOT}/lib/cmake/ElaWidgetToolsConfig.cmake")
if(NOT EXISTS "${_ELA_WIDGET_TOOLS_CONFIG}")
    message(FATAL_ERROR "未找到 ElaWidgetTools CMake 包: ${_ELA_WIDGET_TOOLS_CONFIG}。请将预编译库置于 ELA_WIDGET_TOOLS_ROOT 或通过 -DELA_WIDGET_TOOLS_ROOT=... 指定。")
endif()
find_package(ElaWidgetTools REQUIRED CONFIG PATHS "${ELA_WIDGET_TOOLS_ROOT}/lib/cmake" NO_DEFAULT_PATH)

# -----------------------------------------------------------------------------
# AIR Scanner SDK（仅启用 A 系列时）
# -----------------------------------------------------------------------------
if(SCANNER_VIEWER_ENABLE_A_SERIES)
    set(AIRScanner_DIR "${AIR_SDK_DIR}/lib/cmake/AIRScanner")
    find_package(AIRScanner REQUIRED)
    set(AIR_SDK_BIN "${AIR_SDK_DIR}/bin")
endif()

# -----------------------------------------------------------------------------
# VTK 8.2（请设置 VTK_DIR 或 VTK_ROOT，指向含 VTKConfig.cmake 的目录，如 lib/cmake/vtk-8.2）
# -----------------------------------------------------------------------------
if(NOT VTK_DIR AND EXISTS "${VTK_ROOT}/lib/cmake/vtk-8.2/VTKConfig.cmake")
    set(VTK_DIR "${VTK_ROOT}/lib/cmake/vtk-8.2" CACHE PATH "VTK CMake 配置目录" FORCE)
elseif(NOT VTK_DIR AND EXISTS "${VTK_ROOT}/VTKConfig.cmake")
    set(VTK_DIR "${VTK_ROOT}" CACHE PATH "VTK CMake 配置目录" FORCE)
endif()
# VTK 8.2 的 COMPONENTS 使用带 vtk 前缀的模块名（与 Modules/*.cmake 文件名一致）
#find_package(VTK 8.2 REQUIRED
#    COMPONENTS
#        vtkCommonCore
#        vtkCommonDataModel
#        vtkRenderingCore
#        vtkRenderingOpenGL2
#        vtkInteractionStyle
#        vtkGUISupportQt
#)
find_package(VTK 8.2.0 REQUIRED PATHS ${VTK_DIR} NO_DEFAULT_PATH)

# VTKConfig.cmake 里 VTK_RUNTIME_DIRS 可能被写死为构建机路径，若不存在则用当前 VTK 安装路径
if(NOT EXISTS "${VTK_RUNTIME_DIRS}")
    if(WIN32)
        set(VTK_RUNTIME_DIRS "${VTK_INSTALL_PREFIX}/bin")
    else()
        set(VTK_RUNTIME_DIRS "${VTK_INSTALL_PREFIX}/lib")
    endif()
    message(STATUS "VTK 运行时目录: ${VTK_RUNTIME_DIRS}")
endif()

include(${VTK_USE_FILE})
add_definitions(-DUSE_VTK)

# -----------------------------------------------------------------------------
# 应用源
# -----------------------------------------------------------------------------
set(APP_SOURCES
    main.cpp
    app/MainWindow.cpp
    app/MainWindow.h
)

# Model
set(MODEL_SOURCES
    model/device_layer/IDeviceAdapter.h
    model/device_layer/DeviceInfo.h
    model/device_layer/DeviceModelTags.h
    model/data_center/UnifiedFrame.h
    model/data_center/DataCenter.cpp
    model/data_center/DataCenter.h
)
if(SCANNER_VIEWER_ENABLE_A_SERIES)
    list(APPEND MODEL_SOURCES
        model/device_layer/ASeriesAdapter.cpp
        model/device_layer/ASeriesAdapter.h
    )
endif()
if(SCANNER_VIEWER_ENABLE_HIK_MV3D)
    list(APPEND MODEL_SOURCES
        model/device_layer/HikvisionAdapter.cpp
        model/device_layer/HikvisionAdapter.h
    )
endif()

# View（含 Q_OBJECT 的头文件需列入以便 AUTOMOC 生成 moc_xxx.cpp）
set(VIEW_SOURCES
    view/viewer_core/ImageView2D.cpp
    view/viewer_core/ImageView2D.h
    view/viewer_core/PointCloudView3D.cpp
    view/viewer_core/PointCloudView3D.h
    view/viewer_core/RangeSlider.h
    view/param_ui/CameraParamPanel.cpp
    view/param_ui/CameraParamPanel.h
)

# Controller
set(CONTROLLER_SOURCES
    controller/AcquisitionController.cpp
    controller/AcquisitionController.h
    controller/DeviceController.cpp
    controller/DeviceController.h
)

# Common（可复用日志模块，仅依赖 spdlog）
set(COMMON_SOURCES
    common/log/Logger.cpp
    common/log/Logger.h
)

add_executable(${PROJECT_NAME}
    ${APP_SOURCES}
    ${MODEL_SOURCES}
    ${VIEW_SOURCES}
    ${CONTROLLER_SOURCES}
    ${COMMON_SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${SCANNER_VIEWER_ROOT}
    ${SPDLOG_ROOT}/include
)
if(SCANNER_VIEWER_ENABLE_A_SERIES)
    target_include_directories(${PROJECT_NAME} PRIVATE ${AIR_SDK_DIR}/include)
endif()
if(SCANNER_VIEWER_ENABLE_HIK_MV3D)
    target_include_directories(${PROJECT_NAME} PRIVATE ${HIK_MV3D_INCLUDES})
endif()

set(_SV_COMPILE_DEFS
    SCANNER_VIEWER_ENABLE_A_SERIES=$<IF:$<BOOL:${SCANNER_VIEWER_ENABLE_A_SERIES}>,1,0>
    SCANNER_VIEWER_ENABLE_HIK_MV3D=$<IF:$<BOOL:${SCANNER_VIEWER_ENABLE_HIK_MV3D}>,1,0>
)
if(SCANNER_VIEWER_ENABLE_HIK_MV3D)
    list(APPEND _SV_COMPILE_DEFS
        HIK_MV3D_ASYNC_USE_FETCH_LOOP=$<IF:$<BOOL:${HIK_MV3D_ASYNC_USE_FETCH_LOOP}>,1,0>
    )
else()
    list(APPEND _SV_COMPILE_DEFS HIK_MV3D_ASYNC_USE_FETCH_LOOP=0)
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE ${_SV_COMPILE_DEFS})

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Widgets
    Qt5::Core
    Qt5::Gui
    Qt5::Concurrent
    ElaWidgetTools
    ${VTK_LIBRARIES}
)
if(SCANNER_VIEWER_ENABLE_A_SERIES)
    target_link_libraries(${PROJECT_NAME} PRIVATE AIRScanner::AIRScanner)
endif()
if(SCANNER_VIEWER_ENABLE_HIK_MV3D AND WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE "${HIK_MV3D_IMPORT_LIB}")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${VTK_INCLUDE_DIRS})

# -----------------------------------------------------------------------------
# 编译时自动拷贝依赖 DLL 到 exe 目录，区分 Debug/Release（仅 Windows）
# -----------------------------------------------------------------------------
if(WIN32)
    if(SCANNER_VIEWER_ENABLE_A_SERIES)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -DAIR_SDK_BIN=${AIR_SDK_DIR}/bin
                -DDEST_DIR=$<TARGET_FILE_DIR:${PROJECT_NAME}>
                -DCONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyAirSdkDlls.cmake
            COMMENT "拷贝 AIR SDK DLL (${PROJECT_NAME}: $<CONFIG>)"
        )
    endif()

    # 2) Qt：windeployqt 拷贝 Qt DLL 及 platforms 等插件（通过脚本根据 CONFIG 传 --debug/--release，兼容 CMake 3.16）
    get_filename_component(QT_BIN_DIR "${Qt5_DIR}/../../../bin" ABSOLUTE)
    set(WINDEPLOYQT_EXE "${QT_BIN_DIR}/windeployqt.exe")
    if(EXISTS "${WINDEPLOYQT_EXE}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -DCONFIG=$<CONFIG>
                -DEXE=$<TARGET_FILE:${PROJECT_NAME}>
                -DWINDEPLOYQT_EXE=${WINDEPLOYQT_EXE}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunWindeployqt.cmake
            COMMENT "windeployqt: 拷贝 Qt 依赖 ($<CONFIG>)"
        )
    else()
        message(WARNING "未找到 windeployqt: ${WINDEPLOYQT_EXE}，请手动拷贝 Qt DLL")
    endif()

    # 3) VTK：从 VTK 运行时目录拷贝所有 DLL
    if(DEFINED VTK_RUNTIME_DIRS AND EXISTS "${VTK_RUNTIME_DIRS}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -DVTK_RUNTIME_DIRS=${VTK_RUNTIME_DIRS}
                -DDEST_DIR=$<TARGET_FILE_DIR:${PROJECT_NAME}>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyVtkDlls.cmake
            COMMENT "拷贝 VTK DLL 到 exe 目录"
        )
    else()
        message(WARNING "VTK_RUNTIME_DIRS 未设置或不存在，请确保 VTK 运行时 DLL 在 PATH 或手动拷贝")
    endif()

    if(SCANNER_VIEWER_ENABLE_HIK_MV3D)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -DHIK_MV3D_LIB_DIR=${HIK_MV3D_LIB_DIR}
                -DDEST_DIR=$<TARGET_FILE_DIR:${PROJECT_NAME}>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyHikMv3dDlls.cmake
            COMMENT "拷贝海康 MV3D DLL (${PROJECT_NAME})"
        )
    endif()

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DELA_WIDGET_TOOLS_ROOT=${ELA_WIDGET_TOOLS_ROOT}
            -DDEST_DIR=$<TARGET_FILE_DIR:${PROJECT_NAME}>
            -DCONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyElaWidgetToolsDlls.cmake
        COMMENT "拷贝 ElaWidgetTools DLL ($<CONFIG>)"
    )
endif()

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
