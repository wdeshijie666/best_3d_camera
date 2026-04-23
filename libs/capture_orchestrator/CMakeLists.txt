add_library(capture_orchestrator STATIC
  src/hard_trigger_orchestrator.cpp
  src/multi_hard_trigger_orchestrator.cpp
)

target_include_directories(capture_orchestrator
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CAMERA_3D_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(capture_orchestrator PUBLIC camera_driver serial_port platform_diag)
