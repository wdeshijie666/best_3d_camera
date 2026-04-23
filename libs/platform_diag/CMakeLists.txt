add_library(platform_diag STATIC
  src/diag_config.cpp
  src/logging.cpp
  src/crash_handler.cpp
)

target_include_directories(platform_diag
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CAMERA_3D_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(platform_diag PUBLIC Threads::Threads)

if(TARGET spdlog::spdlog)
  target_link_libraries(platform_diag PUBLIC spdlog::spdlog)
elseif(TARGET spdlog::spdlog_header_only)
  target_link_libraries(platform_diag PUBLIC spdlog::spdlog_header_only)
else()
  message(FATAL_ERROR "未找到 spdlog CMake 目标 spdlog::spdlog 或 spdlog::spdlog_header_only，请检查 3rdParty 中 spdlog 安装与 spdlogConfig.cmake")
endif()

if(WIN32)
  target_link_libraries(platform_diag PRIVATE dbghelp)
endif()

target_compile_definitions(platform_diag PUBLIC
  $<$<BOOL:${CAMERA3D_HAVE_BREAKPAD}>:CAMERA3D_HAVE_BREAKPAD=1>
)

if(CAMERA3D_HAVE_BREAKPAD)
  target_include_directories(platform_diag PRIVATE "${BREAKPAD_CLIENT_INCLUDE_DIR}")
endif()

# ---- install + config ----
# camera3d_spdlog_headers 在根 CMakeLists 中定义（仅头文件 spdlog 包）；需一并导出供 find_package 传递依赖
set(_platform_diag_install_targets platform_diag)
if(TARGET camera3d_spdlog_headers)
  list(APPEND _platform_diag_install_targets camera3d_spdlog_headers)
endif()
install(TARGETS ${_platform_diag_install_targets}
  EXPORT platform_diag_targets
  ARCHIVE DESTINATION ${CAMERA_3D_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CAMERA_3D_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CAMERA_3D_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CAMERA_3D_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/ DESTINATION ${CAMERA_3D_INSTALL_INCLUDEDIR}/platform_diag)

include(CMakePackageConfigHelpers)
configure_package_config_file(
  "${CMAKE_SOURCE_DIR}/cmake/PlatformDiagConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/PlatformDiagConfig.cmake"
  INSTALL_DESTINATION ${CAMERA_3D_INSTALL_CMAKEDIR}/platform_diag
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/PlatformDiagConfigVersion.cmake"
  VERSION 0.1.0
  COMPATIBILITY SameMajorVersion
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/PlatformDiagConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/PlatformDiagConfigVersion.cmake"
  DESTINATION ${CAMERA_3D_INSTALL_CMAKEDIR}/platform_diag
)

install(EXPORT platform_diag_targets
  FILE PlatformDiagTargets.cmake
  NAMESPACE camera3d::
  DESTINATION ${CAMERA_3D_INSTALL_CMAKEDIR}/platform_diag
)
