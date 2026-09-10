#include <chrono>
#include <regex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "go_plugin/log.hpp"

using go_plugin::log::Field;
using go_plugin::log::Level;
using go_plugin::log::Record;

namespace {

// Capture holds every record a test produces, and restores the default sink so
// one test cannot leak its sink into the next.
class Capture {
public:
    Capture() {
        go_plugin::log::SetSink([this](const Record& record) { lines_.push_back(go_plugin::log::Encode(record)); });
    }
    ~Capture() { go_plugin::log::SetSink(nullptr); }

    const std::vector<std::string>& lines() const { return lines_; }

private:
    std::vector<std::string> lines_;
};

}  // namespace

// The host parses @timestamp with Go's "2006-01-02T15:04:05.000000Z07:00",
// which demands exactly six fractional digits and an offset written as "Z" or
// with a colon. Anything else and the host discards the parse and reports the
// whole raw line at its own level — the failure is silent and looks exactly
// like the bug this format exists to fix.
TEST(Log, TimestampMatchesTheHostsLayout) {
    const std::regex layout(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{6}(Z|[+-]\d{2}:\d{2})$)");

    for (auto tp : {std::chrono::system_clock::now(),
                    std::chrono::system_clock::time_point{},
                    std::chrono::system_clock::now() + std::chrono::hours(24 * 365)}) {
        const std::string stamp = go_plugin::log::FormatTimestamp(tp);
        EXPECT_TRUE(std::regex_match(stamp, layout)) << "timestamp not in the host's layout: " << stamp;
    }
}

TEST(Log, TimestampKeepsSixFractionalDigits) {
    // A whole second must still render six digits rather than being trimmed.
    const auto whole = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
    const std::string stamp = go_plugin::log::FormatTimestamp(whole);
    EXPECT_NE(stamp.find(".000000"), std::string::npos) << stamp;
}

TEST(Log, CarriesTheHostsKeys) {
    Capture capture;
    go_plugin::log::Info("agent registered", {Field("capability", "NoInputNoOutput")});

    ASSERT_EQ(capture.lines().size(), 1u);
    const std::string& line = capture.lines().front();
    EXPECT_NE(line.find(R"("@level":"info")"), std::string::npos) << line;
    EXPECT_NE(line.find(R"("@message":"agent registered")"), std::string::npos) << line;
    EXPECT_NE(line.find(R"("@timestamp":")"), std::string::npos) << line;
    EXPECT_NE(line.find(R"("capability":"NoInputNoOutput")"), std::string::npos) << line;
}

TEST(Log, LevelNamesAreTheOnesTheHostAccepts) {
    Capture capture;
    go_plugin::log::SetLevel(Level::Trace);
    go_plugin::log::Trace("t");
    go_plugin::log::Debug("d");
    go_plugin::log::Info("i");
    go_plugin::log::Warn("w");
    go_plugin::log::Error("e");
    go_plugin::log::SetLevel(Level::Info);

    ASSERT_EQ(capture.lines().size(), 5u);
    const char* expected[] = {"trace", "debug", "info", "warn", "error"};
    for (std::size_t i = 0; i < 5; ++i) {
        const std::string want = std::string(R"("@level":")") + expected[i] + R"(")";
        EXPECT_NE(capture.lines()[i].find(want), std::string::npos) << capture.lines()[i];
    }
}

// The host reads standard error one line at a time, so a newline that reached
// the output unescaped would split one record into two and leave the remainder
// unparseable.
TEST(Log, EscapesWhatWouldBreakTheLine) {
    Capture capture;
    go_plugin::log::Info("first\nsecond\ttabbed \"quoted\" back\\slash",
                         {Field("ctl", std::string_view("\x01", 1))});

    ASSERT_EQ(capture.lines().size(), 1u);
    const std::string& line = capture.lines().front();
    EXPECT_EQ(line.find('\n'), std::string::npos) << "an unescaped newline splits the record: " << line;
    EXPECT_NE(line.find("first\\nsecond"), std::string::npos) << line;
    EXPECT_NE(line.find("\\t"), std::string::npos) << line;
    EXPECT_NE(line.find("\\\"quoted\\\""), std::string::npos) << line;
    EXPECT_NE(line.find("back\\\\slash"), std::string::npos) << line;
    EXPECT_NE(line.find("\\u0001"), std::string::npos) << line;
}

TEST(Log, NumbersAndBoolsAreJsonLiterals) {
    Capture capture;
    go_plugin::log::Info("readings", {Field("count", 7), Field("ok", true), Field("ratio", 0.5)});

    const std::string& line = capture.lines().front();
    EXPECT_NE(line.find(R"("count":7)"), std::string::npos) << line;
    EXPECT_NE(line.find(R"("ok":true)"), std::string::npos) << line;
    EXPECT_NE(line.find(R"("ratio":0.5)"), std::string::npos) << line;
}

// The shape the README hands a caller: fields written inline, no Field spelled
// out and no vector built.
TEST(Log, TakesFieldsAsBracedPairs) {
    Capture capture;
    go_plugin::log::Info("sink opened", {{"rate", 48000}, {"path", std::string("/tmp/pipe")}});

    ASSERT_EQ(capture.lines().size(), 1u);
    const std::string& line = capture.lines().front();
    EXPECT_NE(line.find(R"("rate":48000)"), std::string::npos) << line;
    EXPECT_NE(line.find(R"("path":"/tmp/pipe")"), std::string::npos) << line;
}

// A backend hands over records its own library already decided to emit. Gating
// them again here would silently drop what a plugin meant to say, which is the
// failure this format exists to prevent.
TEST(Log, DoesNotRegateARecordFromABackend) {
    Capture capture;
    go_plugin::log::SetLevel(Level::Error);

    Record record;
    record.level = Level::Debug;
    record.message = "a backend already let this through";
    record.timestamp = std::chrono::system_clock::now();
    go_plugin::log::Submit(record);

    go_plugin::log::SetLevel(Level::Info);

    ASSERT_EQ(capture.lines().size(), 1u);
    EXPECT_NE(capture.lines().front().find(R"("@level":"debug")"), std::string::npos);
}

TEST(Log, DropsRecordsBelowTheLevel) {
    Capture capture;
    go_plugin::log::SetLevel(Level::Warn);
    go_plugin::log::Info("dropped");
    go_plugin::log::Error("kept");
    go_plugin::log::SetLevel(Level::Info);

    ASSERT_EQ(capture.lines().size(), 1u);
    EXPECT_NE(capture.lines().front().find("kept"), std::string::npos);
}
