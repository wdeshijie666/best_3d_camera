# 按目标的真实运行时依赖复制 DLL（而不是递归扫描 THIRD_PARTY_LIBRARY_DIR）。
# 依赖来源为 CMake 生成器表达式 $<TARGET_RUNTIME_DLLS:tgt>。
# 这样可以避免把未被目标使用的 DLL 大量拷贝到输出目录。

macro(camera3d_register_post_build_copy_dlls _target)
  if(WIN32)
    add_custom_command(TARGET ${_target} POST_BUILD
      COMMAND ${CMAKE_COMMAND}
        "-DTARGET_FILE=$<TARGET_FILE:${_target}>"
        "-DDST_DIR=$<TARGET_FILE_DIR:${_target}>"
        "-DRUNTIME_DLLS=$<TARGET_RUNTIME_DLLS:${_target}>"
        -P "${CAMERA3D_CMAKE_DIR}/CopyResolvedRuntimeDlls.cmake"
      VERBATIM)
  endif()
endmacro()
