set(CAMERA_SDK_SOURCES
  src/user_camera_sdk.cpp
  src/developer_camera_sdk.cpp
  src/best_camera_3d.cpp
  src/camera_sdk_discovery.cpp
)

if(CAMERA3D_USE_GRPC_STUB)
  list(APPEND CAMERA_SDK_SOURCES src/camera_sdk_stub.cpp)
else()
  list(APPEND CAMERA_SDK_SOURCES src/camera_sdk_grpc.cpp)
endif()

add_library(camera_sdk SHARED ${CAMERA_SDK_SOURCES})

target_compile_definitions(camera_sdk PRIVATE CAMERA_SDK_BUILD)
target_include_directories(camera_sdk
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/libs/camera_hub_api/include>"
    "$<INSTALL_INTERFACE:${CAMERA_3D_INSTALL_INCLUDEDIR}>"
  PRIVATE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
)

target_link_libraries(camera_sdk PUBLIC platform_diag)

if(CAMERA3D_USE_GRPC_STUB)
  target_compile_definitions(camera_sdk PRIVATE CAMERA3D_USE_GRPC_STUB=1)
else()
  target_link_libraries(camera_sdk PRIVATE camera_proto gRPC::grpc++)
endif()

if(WIN32)
  target_link_libraries(camera_sdk PRIVATE ws2_32)
endif()

set_target_properties(camera_sdk PROPERTIES
  OUTPUT_NAME "camera_sdk"
  WINDOWS_EXPORT_ALL_SYMBOLS OFF
)

camera3d_register_post_build_copy_dlls(camera_sdk)

install(TARGETS camera_sdk EXPORT camera_sdk_targets
  RUNTIME DESTINATION ${CAMERA_3D_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CAMERA_3D_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CAMERA_3D_INSTALL_LIBDIR})

install(DIRECTORY include/ DESTINATION ${CAMERA_3D_INSTALL_INCLUDEDIR})
install(DIRECTORY "${CMAKE_SOURCE_DIR}/libs/camera_hub_api/include/" DESTINATION ${CAMERA_3D_INSTALL_INCLUDEDIR})

if(CAMERA3D_USE_GRPC_STUB)
  set(CAMERA3D_SDK_GRPC_STUB_VALUE 1)
else()
  set(CAMERA3D_SDK_GRPC_STUB_VALUE 0)
endif()

include(CMakePackageConfigHelpers)
configure_package_config_file(
  "${CMAKE_SOURCE_DIR}/cmake/CameraSDKConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/CameraSDKConfig.cmake"
  INSTALL_DESTINATION ${CAMERA_3D_INSTALL_CMAKEDIR}/camera_sdk
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/CameraSDKConfigVersion.cmake"
  VERSION 0.1.0
  COMPATIBILITY SameMajorVersion
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/CameraSDKConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/CameraSDKConfigVersion.cmake"
  DESTINATION ${CAMERA_3D_INSTALL_CMAKEDIR}/camera_sdk
)

install(EXPORT camera_sdk_targets
  FILE CameraSDKTargets.cmake
  NAMESPACE camera3d::
  DESTINATION ${CAMERA_3D_INSTALL_CMAKEDIR}/camera_sdk)
