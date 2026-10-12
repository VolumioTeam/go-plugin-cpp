#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <grp.h>
#include <sys/stat.h>
#include <unistd.h>

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

    EXPECT_NE(handshake().find("|unix|"), std::string::npos) << handshake();
    EXPECT_NE(handshake().find("|grpc|"), std::string::npos) << handshake();
    EXPECT_EQ(handshake().back(), '\n') << "the host reads the line, so it has to be terminated";
}

TEST_F(Handshake, AdvertisesTheListeningAddress) {
    std::string error;
    ASSERT_TRUE(Start(&error)) << error;

    ASSERT_FALSE(server().socket_path().empty());
    EXPECT_NE(handshake().find("|" + server().socket_path() + "|"), std::string::npos) << handshake();
}

TEST_F(Handshake, HasTheSixFieldsTheHostSplitsOn) {
    std::string error;
    ASSERT_TRUE(Start(&error)) << error;

    // CORE|APP|NET|ADDR|PROTO|CERT — six fields, so five separators.
    std::string line = handshake();
    if (!line.empty() && line.back() == '\n') line.pop_back();
    EXPECT_EQ(std::count(line.begin(), line.end(), '|'), 5) << line;
}

// The host makes a directory for the plugin's socket and expects it there.
TEST_F(Handshake, PutsTheSocketInTheDirectoryTheHostNames) {
    char dir[] = "/tmp/go-plugin-cpp-XXXXXX";
    ASSERT_NE(::mkdtemp(dir), nullptr);
    SetEnv("PLUGIN_UNIX_SOCKET_DIR", dir);

    std::string error;
    ASSERT_TRUE(Start(&error)) << error;

    const std::string& path = server().socket_path();
    EXPECT_EQ(path.substr(0, path.rfind('/')), dir);
    struct stat st {};
    ASSERT_EQ(::stat(path.c_str(), &st), 0);
    EXPECT_TRUE(S_ISSOCK(st.st_mode));

    server().Shutdown();
    server().Wait();
    ::unlink(path.c_str());
    ::rmdir(dir);
}

// A host that names a group, by number or by name, reaches the socket through it, as with
// go-plugin's own servers.
TEST_F(Handshake, OpensTheSocketToTheGroupTheHostNames) {
    std::vector<std::string> groups{std::to_string(::getgid())};
    const struct group* own = ::getgrgid(::getgid());
    if (own && std::string(own->gr_name).find_first_not_of("0123456789") != std::string::npos) {
        groups.emplace_back(own->gr_name);
    }
    for (const std::string& named : groups) {
        SetEnv("PLUGIN_UNIX_SOCKET_GROUP", named.c_str());
        go_plugin::PluginServer server(config_);
        std::string error;
        ASSERT_TRUE(server.Start(&error)) << named << ": " << error;

        struct stat st {};
        ASSERT_EQ(::stat(server.socket_path().c_str(), &st), 0);
        EXPECT_EQ(st.st_gid, ::getgid()) << named;
        EXPECT_EQ(st.st_mode & 0777, 0660u) << named;
    }
}

// A group the system does not know fails the start, rather than leave the host locked out.
TEST_F(Handshake, RefusesAGroupThatDoesNotExist) {
    SetEnv("PLUGIN_UNIX_SOCKET_GROUP", "no-such-group-for-go-plugin-cpp");

    std::string error;
    EXPECT_FALSE(Start(&error));
    EXPECT_NE(error.find("no-such-group-for-go-plugin-cpp"), std::string::npos) << error;
    EXPECT_TRUE(handshake().empty()) << "a plugin that cannot be reached must advertise nothing";
}

}  // namespace
}  // namespace go_plugin::test
