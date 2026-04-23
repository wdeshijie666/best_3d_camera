# 将海康 MV3D RGBD 运行时 DLL 拷贝到 exe 目录（与 SimpleView 示例部署方式一致）
# 用法: cmake -DHIK_MV3D_LIB_DIR=... -DDEST_DIR=... -P CopyHikMv3dDlls.cmake
if(NOT HIK_MV3D_LIB_DIR OR NOT DEST_DIR)
    message(FATAL_ERROR "CopyHikMv3dDlls.cmake 需要 HIK_MV3D_LIB_DIR, DEST_DIR")
endif()
set(_candidates
    "${HIK_MV3D_LIB_DIR}/Mv3dRgbd.dll"
    "C:/Program Files (x86)/Common Files/Mv3dRgbdSDK/Runtime/Win64_x64/Mv3dRgbd.dll"
)
set(_copied FALSE)
foreach(_dll IN LISTS _candidates)
    if(EXISTS "${_dll}")
        file(COPY "${_dll}" DESTINATION "${DEST_DIR}")
        message(STATUS "已拷贝: ${_dll} -> ${DEST_DIR}")
        set(_copied TRUE)
        break()
    endif()
endforeach()
if(NOT _copied)
    message(WARNING "未找到 Mv3dRgbd.dll。请将 Runtime/Win64_x64 下的 DLL 手动拷贝到输出目录，或把该目录加入 PATH。")
endif()
