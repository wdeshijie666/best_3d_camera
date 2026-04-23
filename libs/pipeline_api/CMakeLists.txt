add_library(pipeline_api STATIC
  src/pipeline_impl.cpp
)

target_include_directories(pipeline_api
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CAMERA_3D_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(pipeline_api PUBLIC platform_diag)
