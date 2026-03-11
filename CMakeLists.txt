cmake_minimum_required(VERSION 3.20)
project(go-plugin-cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_subdirectory(src)

option(GO_PLUGIN_BUILD_TESTS "Build tests" ON)
if(GO_PLUGIN_BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)
    enable_testing()
    add_subdirectory(tests)
endif()

option(GO_PLUGIN_BUILD_EXAMPLES "Build examples" ON)
if(GO_PLUGIN_BUILD_EXAMPLES)
    add_subdirectory(example)
endif()

# ── Install & packaging ───────────────────────────────────────────────────────
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/go_plugin-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/go_plugin-config.cmake"
    INSTALL_DESTINATION "lib/cmake/go_plugin"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/go_plugin-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(TARGETS go_plugin
    EXPORT go_plugin-targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT go_plugin-targets
    FILE go_plugin-targets.cmake
    NAMESPACE go_plugin::
    DESTINATION lib/cmake/go_plugin
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/go_plugin-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/go_plugin-config-version.cmake"
    DESTINATION lib/cmake/go_plugin
)
