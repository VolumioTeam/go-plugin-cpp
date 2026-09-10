#include <algorithm>
#include <string>

#include <gtest/gtest.h>

#include "plugin_fixture.hpp"

namespace go_plugin::test {
namespace {

using Handshake = PluginFixture;

TEST_F(Handshake, FailsWithMissingCookie) {
    UnsetEnv(kCookieKey);

    std::string error;
    EXPECT_FALSE(Start(&error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(handshake().empty()) << "a refused plugin must advertise nothing";
}

TEST_F(Handshake, FailsWithWrongCookieValue) {
    SetEnv(kCookieKey, "not-the-expected-value");

    std::string error;
    EXPECT_FALSE(Start(&error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(handshake().empty());
}

TEST_F(Handshake, SucceedsWithCorrectCookie) {
    std::string error;
    ASSERT_TRUE(Start(&error)) << error;
    EXPECT_FALSE(handshake().empty());
}

TEST_F(Handshake, StatesTheCoreAndAppProtocolVersions) {
    config_.handshake.protocol_version = 3;

    std::string error;
    ASSERT_TRUE(Start(&error)) << error;

    // The core protocol version is always 1; the app version is the plugin's.
    EXPECT_EQ(handshake().substr(0, 2), "1|");
    EXPECT_NE(handshake().find("|3|"), std::string::npos) << handshake();
}

TEST_F(Handshake, NamesTheTransportAndProtocol) {
    std::string error;
    ASSERT_TRUE(Start(&error)) << error;

    EXPECT_NE(handshake().find("|tcp|"), std::string::npos) << handshake();
    EXPECT_NE(handshake().find("|grpc|"), std::string::npos) << handshake();
    EXPECT_EQ(handshake().back(), '\n') << "the host reads the line, so it has to be terminated";
}

TEST_F(Handshake, AdvertisesTheListeningAddress) {
    std::string error;
    ASSERT_TRUE(Start(&error)) << error;

    EXPECT_GT(server().port(), 0);
    EXPECT_NE(handshake().find(target()), std::string::npos) << handshake();
}

TEST_F(Handshake, HasTheSixFieldsTheHostSplitsOn) {
    std::string error;
    ASSERT_TRUE(Start(&error)) << error;

    // CORE|APP|NET|ADDR|PROTO|CERT — six fields, so five separators.
    std::string line = handshake();
    if (!line.empty() && line.back() == '\n') line.pop_back();
    EXPECT_EQ(std::count(line.begin(), line.end(), '|'), 5) << line;
}

TEST_F(Handshake, RespectsThePortRangeTheHostAsksFor) {
    SetEnv("PLUGIN_MIN_PORT", "19900");
    SetEnv("PLUGIN_MAX_PORT", "19999");

    std::string error;
    ASSERT_TRUE(Start(&error)) << error;

    EXPECT_GE(server().port(), 19900);
    EXPECT_LE(server().port(), 19999);
}

}  // namespace
}  // namespace go_plugin::test
