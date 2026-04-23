# 根据 CONFIG 将 ElaWidgetTools 的 Debug/Release DLL 拷贝到 exe 目录（预编译共享库）
# 用法: cmake -DELA_WIDGET_TOOLS_ROOT=... -DDEST_DIR=... -DCONFIG=Debug|Release -P CopyElaWidgetToolsDlls.cmake
if(NOT ELA_WIDGET_TOOLS_ROOT OR NOT DEST_DIR OR NOT CONFIG)
    message(FATAL_ERROR "CopyElaWidgetToolsDlls.cmake 需要 ELA_WIDGET_TOOLS_ROOT, DEST_DIR, CONFIG")
endif()
if(CONFIG STREQUAL "Debug")
    set(_ELA_DLL "${ELA_WIDGET_TOOLS_ROOT}/bin/ElaWidgetToolsd.dll")
else()
    set(_ELA_DLL "${ELA_WIDGET_TOOLS_ROOT}/bin/ElaWidgetTools.dll")
endif()
if(EXISTS "${_ELA_DLL}")
    file(COPY "${_ELA_DLL}" DESTINATION "${DEST_DIR}")
    message(STATUS "已拷贝: ${_ELA_DLL} -> ${DEST_DIR}")
endif()
