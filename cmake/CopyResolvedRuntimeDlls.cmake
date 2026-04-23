# 用法（由 add_custom_command 调用）:
#   cmake -DTARGET_FILE=... -DDST_DIR=... -DRUNTIME_DLLS="a.dll;b.dll;..." -P CopyResolvedRuntimeDlls.cmake

if(NOT DEFINED TARGET_FILE OR NOT DEFINED DST_DIR)
  message(FATAL_ERROR "TARGET_FILE and DST_DIR are required")
endif()

# 生成器参数可能带引号，先去引号。
string(REGEX REPLACE "^\"(.*)\"$" "\\1" TARGET_FILE "${TARGET_FILE}")
string(REGEX REPLACE "^\"(.*)\"$" "\\1" DST_DIR "${DST_DIR}")
file(MAKE_DIRECTORY "${DST_DIR}")

set(_copied 0)
foreach(_dll IN LISTS RUNTIME_DLLS)
  if(_dll STREQUAL "")
    continue()
  endif()
  string(REGEX REPLACE "^\"(.*)\"$" "\\1" _dll "${_dll}")
  if(EXISTS "${_dll}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_dll}" "${DST_DIR}")
    math(EXPR _copied "${_copied} + 1")
  endif()
endforeach()

get_filename_component(_name "${TARGET_FILE}" NAME)
message(STATUS "[camera3d] Runtime DLL copy for ${_name}: copied ${_copied} file(s) -> ${DST_DIR}")
