add_library(ipc_shmem STATIC
  src/shm_ring_buffer.cpp
)

target_include_directories(ipc_shmem
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CAMERA_3D_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(ipc_shmem PUBLIC platform_diag)

if(TARGET Boost::headers)
  target_link_libraries(ipc_shmem PUBLIC Boost::headers)
else()
  target_include_directories(ipc_shmem SYSTEM PUBLIC ${Boost_INCLUDE_DIRS})
endif()
