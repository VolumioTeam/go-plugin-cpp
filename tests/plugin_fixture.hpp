#pragma once

#include <chrono>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>

#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include "go_plugin/server.hpp"
#include "probe.grpc.pb.h"

namespace go_plugin::test {

/** Echoes its request, which is all a test needs a served method to do. */
class ProbeService final : public Probe::Service {
public:
    grpc::Status Ping(grpc::ServerContext*, const PingRequest* request, PingReply* reply) override {
        reply->set_text(request->text());
        return grpc::Status::OK;
    }
};

/**
 * PluginFixture configures a server the way a host configures one — cookie in
 * the environment, a service registered, the handshake captured — and takes
 * the environment back down afterwards so no test can leak a cookie or a port
 * range into the next.
 */
class PluginFixture : public ::testing::Test {
protected:
    static constexpr const char* kCookieKey = "GO_PLUGIN_TEST_COOKIE";
    static constexpr const char* kCookieValue = "test-cookie";

    void SetUp() override {
        SetEnv(kCookieKey, kCookieValue);
        config_.handshake.magic_cookie_key = kCookieKey;
        config_.handshake.magic_cookie_value = kCookieValue;
        config_.output = &handshake_;
        config_.services = {&probe_};
    }

    void TearDown() override {
        if (started_) {
            server_->Shutdown();
            server_->Wait();
        }
        UnsetEnv(kCookieKey);
        UnsetEnv("PLUGIN_MIN_PORT");
        UnsetEnv("PLUGIN_MAX_PORT");
    }

    static void SetEnv(const char* key, const char* value) { ::setenv(key, value, 1); }
    static void UnsetEnv(const char* key) { ::unsetenv(key); }

    /** Starts a server from the current config. Returns whether it came up. */
    bool Start(std::string* error = nullptr) {
        std::string ignored;
        server_ = std::make_unique<go_plugin::PluginServer>(config_);
        started_ = server_->Start(error != nullptr ? error : &ignored);
        return started_;
    }

    go_plugin::PluginServer& server() { return *server_; }

    /** The handshake line, newline included, as the host would read it. */
    std::string handshake() const { return handshake_.str(); }

    /** The address the handshake advertises. */
    std::string target() const { return "127.0.0.1:" + std::to_string(server_->port()); }

    go_plugin::ServeConfig config_;

private:
    ProbeService probe_;
    std::ostringstream handshake_;
    std::unique_ptr<go_plugin::PluginServer> server_;
    bool started_ = false;
};

}  // namespace go_plugin::test
