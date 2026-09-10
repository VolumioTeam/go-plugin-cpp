find_package(GTest CONFIG REQUIRED)

# Generated into the build tree, never into the source tree.
set(PROBE_PROTO "${CMAKE_CURRENT_SOURCE_DIR}/proto/probe.proto")
set(PROBE_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen")

file(MAKE_DIRECTORY "${PROBE_OUT_DIR}")

add_custom_command(
    OUTPUT
        "${PROBE_OUT_DIR}/probe.pb.cc" "${PROBE_OUT_DIR}/probe.pb.h"
        "${PROBE_OUT_DIR}/probe.grpc.pb.cc" "${PROBE_OUT_DIR}/probe.grpc.pb.h"
    COMMAND protobuf::protoc
    ARGS
        --proto_path "${CMAKE_CURRENT_SOURCE_DIR}/proto"
        --cpp_out    "${PROBE_OUT_DIR}"
        --grpc_out   "${PROBE_OUT_DIR}"
        --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
        "${PROBE_PROTO}"
    DEPENDS "${PROBE_PROTO}" protobuf::protoc gRPC::grpc_cpp_plugin
    COMMENT "Generating the tests' Probe service"
    VERBATIM
)

add_library(probe_proto STATIC
    "${PROBE_OUT_DIR}/probe.pb.cc"
    "${PROBE_OUT_DIR}/probe.grpc.pb.cc"
)
target_link_libraries(probe_proto PUBLIC gRPC::grpc++ protobuf::libprotobuf)
target_include_directories(probe_proto PUBLIC "${PROBE_OUT_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(test_handshake test_handshake.cpp)
target_link_libraries(test_handshake PRIVATE go_plugin probe_proto GTest::gtest GTest::gtest_main)

add_executable(test_server test_server.cpp)
target_link_libraries(test_server PRIVATE go_plugin probe_proto GTest::gtest GTest::gtest_main)

add_executable(test_log test_log.cpp)
target_link_libraries(test_log PRIVATE go_plugin GTest::gtest GTest::gtest_main)

if(GO_PLUGIN_LOG_ABSL)
    add_executable(test_log_absl test_log_absl.cpp)
    target_link_libraries(test_log_absl PRIVATE go_plugin_log_absl GTest::gtest GTest::gtest_main)
endif()

include(GoogleTest)
gtest_discover_tests(test_handshake)
gtest_discover_tests(test_server)
gtest_discover_tests(test_log)
if(GO_PLUGIN_LOG_ABSL)
    gtest_discover_tests(test_log_absl)
endif()
