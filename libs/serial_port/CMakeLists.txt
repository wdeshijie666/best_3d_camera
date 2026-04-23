add_library(serial_port STATIC
  src/serial_port.cpp
  src/serial_projector.cpp
  src/serial_port_manager.cpp
  src/projector_command.cpp
)

target_include_directories(serial_port
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CAMERA_3D_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(serial_port PUBLIC platform_diag)
