set(HUB_SOURCES main.cpp hub_file_config.cpp hub_service_state.cpp hub_device_broadcaster.cpp)

if(CAMERA3D_USE_GRPC_STUB)
  list(APPEND HUB_SOURCES hub_app_stub.cpp)
else()
  list(APPEND HUB_SOURCES hub_app_grpc.cpp)
endif()

if(CAMERA3D_ENABLE_RECON_SHM_LOOPBACK_TEST AND NOT CAMERA3D_USE_GRPC_STUB)
  list(APPEND HUB_SOURCES hub_shm_loopback_test.cpp)
endif()

add_executable(hub_service ${HUB_SOURCES})

target_link_libraries(hub_service PRIVATE
  camera_hub_api
  platform_diag
  camera_driver
  ipc_shmem
  capture_orchestrator
  serial_port
)

if(CAMERA3D_USE_GRPC_STUB)
  target_link_libraries(hub_service PRIVATE camera_proto_stub)
  target_compile_definitions(hub_service PRIVATE CAMERA3D_USE_GRPC_STUB=1)
  target_include_directories(hub_service PRIVATE "${CMAKE_SOURCE_DIR}/libs/camera_proto_stub/include")
else()
  target_link_libraries(hub_service PRIVATE camera_proto gRPC::grpc++)
endif()

if(CAMERA3D_ENABLE_RECON_SHM_LOOPBACK_TEST AND NOT CAMERA3D_USE_GRPC_STUB)
  target_link_libraries(hub_service PRIVATE shm_loopback_test)
  target_compile_definitions(hub_service PRIVATE CAMERA3D_ENABLE_RECON_SHM_LOOPBACK_TEST=1)
endif()

if(CAMERA3D_ENABLE_CAPTURE_INLINE_IMAGE_TEST AND NOT CAMERA3D_USE_GRPC_STUB)
  target_compile_definitions(hub_service PRIVATE CAMERA3D_ENABLE_CAPTURE_INLINE_IMAGE_TEST=1)
endif()

if(CAMERA3D_ENABLE_HUB_ALGO_BURST_PUSH_TEST AND NOT CAMERA3D_USE_GRPC_STUB)
  target_compile_definitions(hub_service PRIVATE CAMERA3D_ENABLE_HUB_ALGO_BURST_PUSH_TEST=1)
endif()

if(CAMERA3D_ENABLE_HUB_CAPTURE_BURST_TIFF_SAVE AND NOT CAMERA3D_USE_GRPC_STUB)
  find_package(OpenCV QUIET COMPONENTS core imgcodecs)
  if(OpenCV_FOUND)
    target_sources(hub_service PRIVATE hub_burst_tiff_save.cpp)
    target_compile_definitions(hub_service PRIVATE CAMERA3D_ENABLE_HUB_CAPTURE_BURST_TIFF_SAVE=1)
    target_link_libraries(hub_service PRIVATE ${OpenCV_LIBS})
    target_include_directories(hub_service PRIVATE ${OpenCV_INCLUDE_DIRS})
  else()
    message(WARNING
      "CAMERA3D_ENABLE_HUB_CAPTURE_BURST_TIFF_SAVE=ON 但未找到 OpenCV（core+imgcodecs）；已跳过 burst TIFF 落盘源文件。")
    set(CAMERA3D_ENABLE_HUB_CAPTURE_BURST_TIFF_SAVE OFF CACHE BOOL
      "Hub 每次硬触发收齐 burst 后，将原始帧写入当前目录 hub_img_save/<时间戳>/（.tiff，需 OpenCV）"
      FORCE)
  endif()
endif()

if(WIN32)
  target_link_libraries(hub_service PRIVATE ws2_32 iphlpapi)
endif()

camera3d_register_post_build_copy_dlls(hub_service)

install(TARGETS hub_service RUNTIME DESTINATION ${CAMERA_3D_INSTALL_BINDIR})
