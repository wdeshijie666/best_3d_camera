# 根据 CONFIG 调用 windeployqt --debug 或 --release
# 用法: cmake -DCONFIG=Debug|Release -DEXE=path/to/exe -DWINDEPLOYQT_EXE=path/to/windeployqt.exe -P RunWindeployqt.cmake
if(NOT CONFIG OR NOT EXE OR NOT WINDEPLOYQT_EXE)
    message(FATAL_ERROR "RunWindeployqt.cmake 需要 CONFIG, EXE, WINDEPLOYQT_EXE")
endif()
if(NOT EXISTS "${WINDEPLOYQT_EXE}")
    message(WARNING "windeployqt 不存在: ${WINDEPLOYQT_EXE}")
    return()
endif()
set(FLAG "--release")
if(CONFIG STREQUAL "Debug")
    set(FLAG "--debug")
endif()
execute_process(
    COMMAND "${WINDEPLOYQT_EXE}" ${FLAG} --no-compiler-runtime "${EXE}"
    RESULT_VARIABLE RES
)
if(RES)
    message(WARNING "windeployqt 返回 ${RES}")
endif()
