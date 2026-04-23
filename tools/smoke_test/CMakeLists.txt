add_executable(camera_smoke_test
  main.cpp
)

target_link_libraries(camera_smoke_test PRIVATE camera_sdk)

if(WIN32)
  target_link_libraries(camera_smoke_test PRIVATE ws2_32)
endif()

camera3d_register_post_build_copy_dlls(camera_smoke_test)

install(TARGETS camera_smoke_test
  RUNTIME DESTINATION ${CAMERA_3D_INSTALL_BINDIR})
