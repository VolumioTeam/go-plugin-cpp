find_package(GTest CONFIG REQUIRED)

# ── test_handshake (no gRPC server, purely logical tests) ─────────────────────
add_executable(test_handshake test_handshake.cpp)
target_link_libraries(test_handshake PRIVATE go_plugin GTest::gtest GTest::gtest_main)

# ── test_server (starts a real gRPC server in-process) ───────────────────────
add_executable(test_server test_server.cpp)
target_link_libraries(test_server PRIVATE go_plugin GTest::gtest GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(test_handshake)
gtest_discover_tests(test_server)
