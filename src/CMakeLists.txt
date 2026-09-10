add_library(go_plugin STATIC
    server.cpp
    tls.cpp
)

target_include_directories(go_plugin
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(go_plugin
    PUBLIC
        gRPC::grpc++
        protobuf::libprotobuf
        OpenSSL::SSL
        OpenSSL::Crypto
)

target_compile_options(go_plugin PRIVATE
    -Wall -Wextra -Wpedantic
)
