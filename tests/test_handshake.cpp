#include <gtest/gtest.h>

#include <cstdlib>
#include <sstream>
#include <string>
#include <thread>

#include "go_plugin/server.hpp"

// ── helpers ───────────────────────────────────────────────────────────────────

static void SetEnv(const char *key, const char *val) { ::setenv(key, val, 1); }
static void UnsetEnv(const char *key) { ::unsetenv(key); }

// ── magic-cookie validation ───────────────────────────────────────────────────

TEST(HandshakeTest, FailsWithMissingCookie) {
  UnsetEnv("TEST_MAGIC_MISSING");
  std::ostringstream out;
  go_plugin::ServeConfig cfg;
  cfg.handshake.magic_cookie_key = "TEST_MAGIC_MISSING";
  cfg.handshake.magic_cookie_value = "expected";
  cfg.output = &out;

  go_plugin::PluginServer server(cfg);
  std::string err;
  EXPECT_FALSE(server.Start(&err));
  EXPECT_FALSE(err.empty());
  // Nothing should have been written to the output
  EXPECT_TRUE(out.str().empty());
}

TEST(HandshakeTest, FailsWithWrongCookieValue) {
  SetEnv("TEST_MAGIC_WRONG", "bad_value");
  std::ostringstream out;
  go_plugin::ServeConfig cfg;
  cfg.handshake.magic_cookie_key = "TEST_MAGIC_WRONG";
  cfg.handshake.magic_cookie_value = "expected_value";
  cfg.output = &out;

  go_plugin::PluginServer server(cfg);
  std::string err;
  EXPECT_FALSE(server.Start(&err));
  EXPECT_FALSE(err.empty());
  EXPECT_TRUE(out.str().empty());
  UnsetEnv("TEST_MAGIC_WRONG");
}

TEST(HandshakeTest, SucceedsWithCorrectCookie) {
  SetEnv("TEST_MAGIC_OK", "correct_value");
  std::ostringstream out;
  go_plugin::ServeConfig cfg;
  cfg.handshake.magic_cookie_key = "TEST_MAGIC_OK";
  cfg.handshake.magic_cookie_value = "correct_value";
  cfg.output = &out;

  go_plugin::PluginServer server(cfg);
  std::string err;
  ASSERT_TRUE(server.Start(&err)) << "Start failed: " << err;
  server.Shutdown();
  server.Wait();
  UnsetEnv("TEST_MAGIC_OK");
}

// ── handshake line format ─────────────────────────────────────────────────────

TEST(HandshakeTest, OutputContainsCoreProtocolVersion) {
  SetEnv("TEST_FORMAT_KEY", "test_value");
  std::ostringstream out;
  go_plugin::ServeConfig cfg;
  cfg.handshake.magic_cookie_key = "TEST_FORMAT_KEY";
  cfg.handshake.magic_cookie_value = "test_value";
  cfg.handshake.protocol_version = 3;
  cfg.output = &out;

  go_plugin::PluginServer server(cfg);
  std::string err;
  ASSERT_TRUE(server.Start(&err)) << err;

  std::string line = out.str();
  // Must start with "1|" (core protocol version is always 1)
  EXPECT_EQ(line.substr(0, 2), "1|");
  // App protocol version field must be "3"
  EXPECT_NE(line.find("|3|"), std::string::npos);
  server.Shutdown();
  server.Wait();
  UnsetEnv("TEST_FORMAT_KEY");
}

TEST(HandshakeTest, OutputContainsTcpAndGrpc) {
  SetEnv("TEST_PROTO_KEY", "proto_val");
  std::ostringstream out;
  go_plugin::ServeConfig cfg;
  cfg.handshake.magic_cookie_key = "TEST_PROTO_KEY";
  cfg.handshake.magic_cookie_value = "proto_val";
  cfg.output = &out;

  go_plugin::PluginServer server(cfg);
  std::string err;
  ASSERT_TRUE(server.Start(&err)) << err;

  std::string line = out.str();
  EXPECT_NE(line.find("|tcp|"), std::string::npos);
  EXPECT_NE(line.find("|grpc|"), std::string::npos);
  EXPECT_EQ(line.back(), '\n');

  server.Shutdown();
  server.Wait();
  UnsetEnv("TEST_PROTO_KEY");
}

TEST(HandshakeTest, OutputContainsListeningAddress) {
  SetEnv("TEST_ADDR_KEY", "addr_val");
  std::ostringstream out;
  go_plugin::ServeConfig cfg;
  cfg.handshake.magic_cookie_key = "TEST_ADDR_KEY";
  cfg.handshake.magic_cookie_value = "addr_val";
  cfg.output = &out;

  go_plugin::PluginServer server(cfg);
  std::string err;
  ASSERT_TRUE(server.Start(&err)) << err;

  EXPECT_GT(server.port(), 0);
  std::string expected_addr = "127.0.0.1:" + std::to_string(server.port());
  EXPECT_NE(out.str().find(expected_addr), std::string::npos);

  server.Shutdown();
  server.Wait();
  UnsetEnv("TEST_ADDR_KEY");
}

TEST(HandshakeTest, HandshakeHasSixPipeFields) {
  SetEnv("TEST_FIELDS_KEY", "fields_val");
  std::ostringstream out;
  go_plugin::ServeConfig cfg;
  cfg.handshake.magic_cookie_key = "TEST_FIELDS_KEY";
  cfg.handshake.magic_cookie_value = "fields_val";
  cfg.output = &out;

  go_plugin::PluginServer server(cfg);
  std::string err;
  ASSERT_TRUE(server.Start(&err)) << err;

  // go-plugin expects exactly 6 pipe-separated fields (5 separators + trailing
  // pipe before optional cert): CORE|APP|NET|ADDR|PROTO|CERT\n
  std::string line = out.str();
  // strip trailing newline
  if (!line.empty() && line.back() == '\n')
    line.pop_back();
  int pipes = static_cast<int>(std::count(line.begin(), line.end(), '|'));
  EXPECT_EQ(pipes, 5);

  server.Shutdown();
  server.Wait();
  UnsetEnv("TEST_FIELDS_KEY");
}

// ── port range env vars ───────────────────────────────────────────────────────

TEST(HandshakeTest, RespectsPortRange) {
  SetEnv("TEST_RANGE_KEY", "range_val");
  SetEnv("PLUGIN_MIN_PORT", "19900");
  SetEnv("PLUGIN_MAX_PORT", "19999");
  std::ostringstream out;

  go_plugin::ServeConfig cfg;
  cfg.handshake.magic_cookie_key = "TEST_RANGE_KEY";
  cfg.handshake.magic_cookie_value = "range_val";
  cfg.output = &out;

  go_plugin::PluginServer server(cfg);
  std::string err;
  ASSERT_TRUE(server.Start(&err)) << err;

  EXPECT_GE(server.port(), 19900);
  EXPECT_LE(server.port(), 19999);

  server.Shutdown();
  server.Wait();
  UnsetEnv("TEST_RANGE_KEY");
  UnsetEnv("PLUGIN_MIN_PORT");
  UnsetEnv("PLUGIN_MAX_PORT");
}
