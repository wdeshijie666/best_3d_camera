set(PROTO_DIR "${CMAKE_SOURCE_DIR}/proto")
set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GEN_DIR}")

set(PROTO_FILES
  "${PROTO_DIR}/camera_hub.proto"
  "${PROTO_DIR}/camera_recon_test.proto"
)

set(GEN_SRCS
  "${GEN_DIR}/camera_hub.pb.cc"
  "${GEN_DIR}/camera_hub.grpc.pb.cc"
  "${GEN_DIR}/camera_recon_test.pb.cc"
  "${GEN_DIR}/camera_recon_test.grpc.pb.cc"
)
set(GEN_HDRS
  "${GEN_DIR}/camera_hub.pb.h"
  "${GEN_DIR}/camera_hub.grpc.pb.h"
  "${GEN_DIR}/camera_recon_test.pb.h"
  "${GEN_DIR}/camera_recon_test.grpc.pb.h"
)

# 兼容单配置/多配置生成器，优先读 Release/Debug 导入路径
get_target_property(_PROTOC protobuf::protoc IMPORTED_LOCATION_RELEASE)
if(NOT _PROTOC)
  get_target_property(_PROTOC protobuf::protoc IMPORTED_LOCATION_DEBUG)
endif()
if(NOT _PROTOC)
  get_target_property(_PROTOC protobuf::protoc IMPORTED_LOCATION)
endif()
get_target_property(_GRPC_CPP_PLUGIN gRPC::grpc_cpp_plugin IMPORTED_LOCATION_RELEASE)
if(NOT _GRPC_CPP_PLUGIN)
  get_target_property(_GRPC_CPP_PLUGIN gRPC::grpc_cpp_plugin IMPORTED_LOCATION_DEBUG)
endif()
if(NOT _GRPC_CPP_PLUGIN)
  get_target_property(_GRPC_CPP_PLUGIN gRPC::grpc_cpp_plugin IMPORTED_LOCATION)
endif()
if(NOT _PROTOC OR NOT _GRPC_CPP_PLUGIN)
  message(FATAL_ERROR
    "Could not get protoc or grpc_cpp_plugin path. "
    "Check gRPC_ROOT and ensure bin/ contains protoc and grpc_cpp_plugin.")
endif()

add_custom_command(
  OUTPUT ${GEN_SRCS} ${GEN_HDRS}
  COMMAND ${_PROTOC}
  ARGS --cpp_out=${GEN_DIR}
       --grpc_out=${GEN_DIR}
       -I ${PROTO_DIR}
       --plugin=protoc-gen-grpc=${_GRPC_CPP_PLUGIN}
       ${PROTO_FILES}
  DEPENDS ${PROTO_FILES} protobuf::protoc gRPC::grpc_cpp_plugin
  COMMENT "Generate protobuf/gRPC: camera_hub.proto camera_recon_test.proto"
  VERBATIM
)

add_library(camera_proto STATIC ${GEN_SRCS})
target_include_directories(camera_proto PUBLIC "${GEN_DIR}")
target_link_libraries(camera_proto PUBLIC protobuf::libprotobuf gRPC::grpc++)

install(TARGETS camera_proto EXPORT camera_proto_targets
  ARCHIVE DESTINATION ${CAMERA_3D_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CAMERA_3D_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CAMERA_3D_INSTALL_BINDIR})

install(DIRECTORY "${GEN_DIR}/" DESTINATION ${CAMERA_3D_INSTALL_INCLUDEDIR}/generated
  FILES_MATCHING PATTERN "*.h")
