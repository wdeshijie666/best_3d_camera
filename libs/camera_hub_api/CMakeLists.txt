add_library(camera_hub_api INTERFACE)
target_include_directories(camera_hub_api INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CAMERA_3D_INSTALL_INCLUDEDIR}>")

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
        DESTINATION "${CAMERA_3D_INSTALL_INCLUDEDIR}")
