add_library(shm_loopback_test STATIC
  src/save_utils.cpp
)
target_include_directories(shm_loopback_test PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(shm_loopback_test PUBLIC cxx_std_17)

install(TARGETS shm_loopback_test EXPORT shm_loopback_test_targets
  ARCHIVE DESTINATION ${CAMERA_3D_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CAMERA_3D_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CAMERA_3D_INSTALL_BINDIR})
