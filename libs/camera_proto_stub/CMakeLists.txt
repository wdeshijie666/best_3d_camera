# gRPC 未就绪时供 hub 链接的 INTERFACE；与 camera_proto 二选一（由根 CMake CAMERA3D_USE_GRPC_STUB 控制）
add_library(camera_proto_stub INTERFACE)
target_include_directories(camera_proto_stub INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
