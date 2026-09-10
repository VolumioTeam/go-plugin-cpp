#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include "plugin_fixture.hpp"

namespace go_plugin::test {
namespace {

using Server = PluginFixture;

TEST_F(Server, ReportsNoPortUntilStarted) {
    go_plugin::PluginServer server(config_);
    EXPECT_EQ(server.port(), 0);
}

TEST_F(Server, StartsAndShutsDown) {
    std::string error;
    ASSERT_TRUE(Start(&error)) << error;
    EXPECT_GT(server().port(), 0);

    server().Shutdown();
    server().Wait();
}

// A socket accepting a connection is not the same as the plugin answering.
TEST_F(Server, AnswersACallOnTheAdvertisedAddress) {
    std::string error;
    ASSERT_TRUE(Start(&error)) << error;

    auto channel = grpc::CreateChannel(target(), grpc::InsecureChannelCredentials());
    ASSERT_TRUE(channel->WaitForConnected(std::chrono::system_clock::now() + std::chrono::seconds(5)));

    auto stub = Probe::NewStub(channel);
    PingRequest request;
    request.set_text("hello");

    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    PingReply reply;
    const grpc::Status status = stub->Ping(&context, request, &reply);

    ASSERT_TRUE(status.ok()) << status.error_message();
    EXPECT_EQ(reply.text(), "hello");
}

TEST_F(Server, ServeRefusesTheWrongCookie) {
    UnsetEnv(kCookieKey);

    const auto result = go_plugin::Serve(config_);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
}

TEST_F(Server, WaitUnblocksAfterShutdown) {
    std::string error;
    ASSERT_TRUE(Start(&error)) << error;

    std::atomic<bool> wait_returned{false};
    std::thread waiter([&] {
        server().Wait();
        wait_returned = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(wait_returned) << "Wait must block while the server is up";

    server().Shutdown();
    waiter.join();
    EXPECT_TRUE(wait_returned);
}

}  // namespace
}  // namespace go_plugin::test
