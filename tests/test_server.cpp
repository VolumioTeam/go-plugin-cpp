#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <string>
#include <thread>

#include <grpcpp/grpcpp.h>

#include "go_plugin/server.hpp"

// ── helpers ───────────────────────────────────────────────────────────────────

static void SetEnv(const char *k, const char *v) { ::setenv(k, v, 1); }
static void UnsetEnv(const char *k) { ::unsetenv(k); }

// ── basic server lifecycle ────────────────────────────────────────────────────

TEST(ServerTest, StartAndShutdown) {
  SetEnv("SRV_MAGIC", "srv_val");
  std::ostringstream out;

  go_plugin::ServeConfig cfg;
  cfg.handshake.magic_cookie_key = "SRV_MAGIC";
  cfg.handshake.magic_cookie_value = "srv_val";
  cfg.output = &out;

  go_plugin::PluginServer server(cfg);
  std::string err;
  ASSERT_TRUE(server.Start(&err)) << err;
  EXPECT_GT(server.port(), 0);

  server.Shutdown();
  server.Wait();
  UnsetEnv("SRV_MAGIC");
}

TEST(ServerTest, PortIsPositiveAfterStart) {
  SetEnv("SRV_PORT_MAGIC", "portval");
  std::ostringstream out;

  go_plugin::ServeConfig cfg;
  cfg.handshake.magic_cookie_key = "SRV_PORT_MAGIC";
  cfg.handshake.magic_cookie_value = "portval";
  cfg.output = &out;

  go_plugin::PluginServer server(cfg);
  EXPECT_EQ(server.port(), 0); // before Start()

  std::string err;
  ASSERT_TRUE(server.Start(&err)) << err;
  EXPECT_GT(server.port(), 0); // after Start()

  server.Shutdown();
  server.Wait();
  UnsetEnv("SRV_PORT_MAGIC");
}

// ── connectivity check ────────────────────────────────────────────────────────

TEST(ServerTest, ChannelConnects) {
  SetEnv("SRV_HEALTH_MAGIC", "health_val");
  std::ostringstream out;

  go_plugin::ServeConfig cfg;
  cfg.handshake.magic_cookie_key = "SRV_HEALTH_MAGIC";
  cfg.handshake.magic_cookie_value = "health_val";
  cfg.output = &out;

  go_plugin::PluginServer server(cfg);
  std::string err;
  ASSERT_TRUE(server.Start(&err)) << err;

  std::string target = "127.0.0.1:" + std::to_string(server.port());
  auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());

  // Wait up to 5 s for the channel to reach READY state.
  bool connected = channel->WaitForConnected(
      std::chrono::system_clock::now() + std::chrono::seconds(5));
  EXPECT_TRUE(connected);

  server.Shutdown();
  server.Wait();
  UnsetEnv("SRV_HEALTH_MAGIC");
}

// ── Serve() convenience function ─────────────────────────────────────────────

TEST(ServerTest, ServeFailsWithWrongCookie) {
  UnsetEnv("SRV_WRONG_MAGIC");
  std::ostringstream out;

  go_plugin::ServeConfig cfg;
  cfg.handshake.magic_cookie_key = "SRV_WRONG_MAGIC";
  cfg.handshake.magic_cookie_value = "expected";
  cfg.output = &out;

  // Serve() should return immediately with ok=false (no blocking Wait).
  auto result = go_plugin::Serve(cfg);
  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(result.error.empty());
}

// ── concurrent shutdown ───────────────────────────────────────────────────────

TEST(ServerTest, WaitUnblocksAfterShutdown) {
  SetEnv("SRV_CONC_MAGIC", "conc_val");
  std::ostringstream out;

  go_plugin::ServeConfig cfg;
  cfg.handshake.magic_cookie_key = "SRV_CONC_MAGIC";
  cfg.handshake.magic_cookie_value = "conc_val";
  cfg.output = &out;

  go_plugin::PluginServer server(cfg);
  std::string err;
  ASSERT_TRUE(server.Start(&err)) << err;

  std::atomic<bool> wait_returned{false};
  std::thread t([&] {
    server.Wait();
    wait_returned = true;
  });

  // Wait() should be blocking now
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_FALSE(wait_returned);

  server.Shutdown();
  t.join();
  EXPECT_TRUE(wait_returned);
  UnsetEnv("SRV_CONC_MAGIC");
}
