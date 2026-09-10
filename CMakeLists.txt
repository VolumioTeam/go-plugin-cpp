cmake_minimum_required(VERSION 3.20)
project(go-plugin-cpp VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

set(GO_PLUGIN_LOG_ABSL "AUTO" CACHE STRING "Build the Abseil logging bridge (ON/OFF/AUTO)")
set_property(CACHE GO_PLUGIN_LOG_ABSL PROPERTY STRINGS ON OFF AUTO)

if(GO_PLUGIN_LOG_ABSL)
    if(GO_PLUGIN_LOG_ABSL STREQUAL "AUTO")
        find_package(absl CONFIG QUIET)
        if(absl_FOUND)
            set(GO_PLUGIN_LOG_ABSL ON)
        else()
            set(GO_PLUGIN_LOG_ABSL OFF)
            message(STATUS "go-plugin-cpp: Abseil not found, skipping the Abseil logging bridge")
        endif()
    else()
        find_package(absl CONFIG REQUIRED)
    endif()
endif()

add_subdirectory(src)

# Off unless asked for: gtest is a manifest feature, so a consumer that only
# wants the library never installs it. Build them with
#   vcpkg install --x-feature=tests
#   cmake -B build -DGO_PLUGIN_BUILD_TESTS=ON
option(GO_PLUGIN_BUILD_TESTS "Build tests" OFF)
if(GO_PLUGIN_BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)
    enable_testing()
    add_subdirectory(tests)
endif()

# Off unless asked for: the example is the only thing here that generates
# protobuf code, so with it off the library needs no protoc and no
# grpc_cpp_plugin to build.
option(GO_PLUGIN_BUILD_EXAMPLES "Build examples" OFF)
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

set(GO_PLUGIN_INSTALL_TARGETS go_plugin)
if(GO_PLUGIN_LOG_ABSL)
    list(APPEND GO_PLUGIN_INSTALL_TARGETS go_plugin_log_absl)
endif()

install(TARGETS ${GO_PLUGIN_INSTALL_TARGETS}
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
