add_library(go_plugin STATIC
    log.cpp
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

# One backend adapter per logging library, each its own target, so a consumer
# links only the one it uses and adding another touches nothing here.
if(GO_PLUGIN_LOG_ABSL)
    add_library(go_plugin_log_absl STATIC log_absl.cpp)
    target_link_libraries(go_plugin_log_absl
        PUBLIC
            go_plugin
            absl::log
            absl::log_sink
            absl::log_sink_registry
            absl::log_entry
            absl::log_globals
            absl::log_severity
            absl::time
    )
    target_compile_options(go_plugin_log_absl PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_compile_options(go_plugin PRIVATE
    -Wall -Wextra -Wpedantic
)
