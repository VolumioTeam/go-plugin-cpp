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

/**
 * gRPC starts a server only if a registered service has a synchronous method,
 * and a plugin with no services could not answer its host either.
 */
class ProbeService final : public Probe::Service {
public:
    grpc::Status Ping(grpc::ServerContext*, const PingRequest* request, PingReply* reply) override {
        reply->set_text(request->text());
        return grpc::Status::OK;
    }
};

/** Takes the environment back down so no test leaks a cookie into the next. */
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

    bool Start(std::string* error = nullptr) {
        std::string ignored;
        server_ = std::make_unique<go_plugin::PluginServer>(config_);
        started_ = server_->Start(error != nullptr ? error : &ignored);
        return started_;
    }

    go_plugin::PluginServer& server() { return *server_; }

    std::string handshake() const { return handshake_.str(); }

    std::string target() const { return "127.0.0.1:" + std::to_string(server_->port()); }

    go_plugin::ServeConfig config_;

private:
    ProbeService probe_;
    std::ostringstream handshake_;
    std::unique_ptr<go_plugin::PluginServer> server_;
    bool started_ = false;
};

}  // namespace go_plugin::test
