# 将 VTK 运行时 DLL 目录下所有 DLL 拷贝到 exe 目录
# 用法: cmake -DVTK_RUNTIME_DIRS=... -DDEST_DIR=... -P CopyVtkDlls.cmake
if(NOT VTK_RUNTIME_DIRS OR NOT DEST_DIR)
    message(FATAL_ERROR "CopyVtkDlls.cmake 需要 VTK_RUNTIME_DIRS, DEST_DIR")
endif()
file(GLOB VTK_DLLS "${VTK_RUNTIME_DIRS}/*.dll")
foreach(dll IN LISTS VTK_DLLS)
    file(COPY "${dll}" DESTINATION "${DEST_DIR}")
endforeach()
if(VTK_DLLS)
    list(LENGTH VTK_DLLS N)
    message(STATUS "已拷贝 ${N} 个 VTK DLL 到 ${DEST_DIR}")
endif()
