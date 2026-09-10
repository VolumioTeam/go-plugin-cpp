#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"

#include "go_plugin/log.hpp"
#include "go_plugin/log_absl.hpp"

using go_plugin::log::Record;

namespace {

std::vector<std::string>& Lines() {
    static std::vector<std::string> lines;
    return lines;
}

class AbslBridge : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        absl::InitializeLog();
        go_plugin::log::InstallAbslBridge();
        go_plugin::log::SetSink([](const Record& record) { Lines().push_back(go_plugin::log::Encode(record)); });
    }

    void SetUp() override {
        Lines().clear();
        go_plugin::log::SetLevel(go_plugin::log::Level::Trace);
        absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
        absl::SetGlobalVLogLevel(4);
    }
};

}  // namespace

// A plugin's own severity is what was being lost, so this is the point.
TEST_F(AbslBridge, KeepsTheSeverity) {
    LOG(INFO) << "an info line";
    LOG(WARNING) << "a warning line";
    LOG(ERROR) << "an error line";

    ASSERT_EQ(Lines().size(), 3u);
    EXPECT_NE(Lines()[0].find(R"("@level":"info")"), std::string::npos) << Lines()[0];
    EXPECT_NE(Lines()[1].find(R"("@level":"warn")"), std::string::npos) << Lines()[1];
    EXPECT_NE(Lines()[2].find(R"("@level":"error")"), std::string::npos) << Lines()[2];
}

// Abseil has no debug or trace severity; only VLOG verbosities.
TEST_F(AbslBridge, MapsVerbosityOntoDebugAndTrace) {
    VLOG(1) << "a verbose line";
    VLOG(3) << "a very verbose line";

    ASSERT_EQ(Lines().size(), 2u);
    EXPECT_NE(Lines()[0].find(R"("@level":"debug")"), std::string::npos) << Lines()[0];
    EXPECT_NE(Lines()[1].find(R"("@level":"trace")"), std::string::npos) << Lines()[1];
}

TEST_F(AbslBridge, CarriesTheMessageWithoutAbseilsPrefix) {
    LOG(INFO) << "plain message";

    ASSERT_EQ(Lines().size(), 1u);
    EXPECT_NE(Lines()[0].find(R"("@message":"plain message")"), std::string::npos) << Lines()[0];
}

TEST_F(AbslBridge, CarriesTheCaller) {
    LOG(INFO) << "located";

    ASSERT_EQ(Lines().size(), 1u);
    EXPECT_NE(Lines()[0].find(R"("caller":"test_log_absl.cpp:)"), std::string::npos) << Lines()[0];
}
