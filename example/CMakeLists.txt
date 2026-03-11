find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

# ── proto/gRPC code generation ────────────────────────────────────────────────
set(GREETER_PROTO "${CMAKE_CURRENT_SOURCE_DIR}/proto/greeter.proto")

set(GREETER_OUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/plugin")

set(GREETER_PROTO_SRCS "${GREETER_OUT_DIR}/greeter.pb.cc")
set(GREETER_PROTO_HDRS "${GREETER_OUT_DIR}/greeter.pb.h")
set(GREETER_GRPC_SRCS  "${GREETER_OUT_DIR}/greeter.grpc.pb.cc")
set(GREETER_GRPC_HDRS  "${GREETER_OUT_DIR}/greeter.grpc.pb.h")

add_custom_command(
    OUTPUT
        "${GREETER_PROTO_SRCS}" "${GREETER_PROTO_HDRS}"
        "${GREETER_GRPC_SRCS}"  "${GREETER_GRPC_HDRS}"
    COMMAND protobuf::protoc
    ARGS
        --proto_path   "${CMAKE_CURRENT_SOURCE_DIR}/proto"
        --cpp_out      "${GREETER_OUT_DIR}"
        --grpc_out     "${GREETER_OUT_DIR}"
        --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
        "${GREETER_PROTO}"
    DEPENDS "${GREETER_PROTO}" protobuf::protoc gRPC::grpc_cpp_plugin
    COMMENT "Generating Greeter C++ protobuf/gRPC sources"
    VERBATIM
)

add_library(greeter_proto STATIC
    "${GREETER_PROTO_SRCS}"
    "${GREETER_GRPC_SRCS}"
)
target_link_libraries(greeter_proto PUBLIC gRPC::grpc++ protobuf::libprotobuf)
target_include_directories(greeter_proto PUBLIC "${GREETER_OUT_DIR}")

# ── C++ plugin binary ─────────────────────────────────────────────────────────
add_executable(greeter_plugin plugin/main.cpp)
target_link_libraries(greeter_plugin PRIVATE go_plugin greeter_proto)

