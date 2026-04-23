# ---------------------------------------------------------------------------
# 适配器编译开关（扩展新品牌时在此增加 option 与源文件列表即可）
# ---------------------------------------------------------------------------
option(CAMERA3D_ENABLE_ADAPTER_DAHENG
  "编译并注册大恒(daheng)适配器；关闭则不编译任何 daheng 源文件，且 post-build 跳过拷贝大恒/Galaxy 相关 DLL"
  ON)

set(_camera_driver_sources
  src/camera_manager.cpp
  src/device_info_io.cpp
  src/null_camera_adapter.cpp
)

if(CAMERA3D_ENABLE_ADAPTER_DAHENG)
  include("${CAMERA3D_CMAKE_DIR}/DaHengGalaxy.cmake")
  list(APPEND _camera_driver_sources
    src/daheng_camera_select.cpp
  )
  if(CAMERA3D_WITH_DAHENG_GALAXY AND DAHENG_GALAXY_FOUND)
    list(APPEND _camera_driver_sources src/daheng_camera_galaxy.cpp)
  else()
    list(APPEND _camera_driver_sources src/daheng_camera_adapter_stub.cpp)
  endif()
endif()

add_library(camera_driver STATIC ${_camera_driver_sources})

target_include_directories(camera_driver
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CAMERA_3D_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(camera_driver PUBLIC platform_diag)

if(CAMERA3D_ENABLE_ADAPTER_DAHENG)
  target_compile_definitions(camera_driver PUBLIC CAMERA3D_ENABLE_ADAPTER_DAHENG=1)
else()
  target_compile_definitions(camera_driver PUBLIC CAMERA3D_ENABLE_ADAPTER_DAHENG=0)
endif()

if(CAMERA3D_ENABLE_ADAPTER_DAHENG AND CAMERA3D_WITH_DAHENG_GALAXY AND DAHENG_GALAXY_FOUND)
  target_include_directories(camera_driver PRIVATE "${DAHENG_GALAXY_INCLUDE_DIR}")
  target_link_libraries(camera_driver PRIVATE "${DAHENG_GALAXY_CPP_LIB}")
  if(DAHENG_GALAXY_C_LIB)
    target_link_libraries(camera_driver PRIVATE "${DAHENG_GALAXY_C_LIB}")
  endif()
  target_compile_definitions(camera_driver PUBLIC CAMERA3D_WITH_DAHENG_GALAXY=1)
endif()

install(TARGETS camera_driver EXPORT camera_driver_targets
  ARCHIVE DESTINATION ${CAMERA_3D_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CAMERA_3D_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CAMERA_3D_INSTALL_BINDIR})

install(DIRECTORY include/ DESTINATION ${CAMERA_3D_INSTALL_INCLUDEDIR}/camera_driver)
