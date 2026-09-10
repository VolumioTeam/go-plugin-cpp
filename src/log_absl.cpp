#include "go_plugin/log_absl.hpp"

#include <string>

#include "absl/base/log_severity.h"
#include "absl/log/globals.h"
#include "absl/log/log_entry.h"
#include "absl/log/log_sink.h"
#include "absl/log/log_sink_registry.h"
#include "absl/time/time.h"

#include "go_plugin/log.hpp"

namespace go_plugin::log {
namespace {

Level LevelFor(const absl::LogEntry& entry, const AbslBridgeOptions& options) {
    // A verbose entry is a VLOG, which Abseil files at info severity. Its
    // verbosity is the only thing that separates it from an ordinary info line.
    if (entry.verbosity() != absl::LogEntry::kNoVerbosityLevel) {
        return entry.verbosity() >= options.trace_from_verbosity ? Level::Trace : Level::Debug;
    }

    switch (entry.log_severity()) {
    case absl::LogSeverity::kInfo: return Level::Info;
    case absl::LogSeverity::kWarning: return Level::Warn;
    case absl::LogSeverity::kError: return Level::Error;
    case absl::LogSeverity::kFatal: return Level::Error;
    }
    return Level::Info;
}

class Bridge final : public absl::LogSink {
public:
    explicit Bridge(const AbslBridgeOptions& options) : options_(options) {}

    void Send(const absl::LogEntry& entry) override {
        Record record;
        record.level = LevelFor(entry, options_);
        // text_message() carries no Abseil prefix, which is what belongs in the
        // record: the severity and the time travel as fields of their own.
        record.message = entry.text_message();
        record.timestamp = absl::ToChronoTime(entry.timestamp());

        if (!options_.include_caller) {
            Submit(record);
            return;
        }

        std::string caller(entry.source_basename());
        caller += ':';
        caller += std::to_string(entry.source_line());
        const Field field("caller", caller);
        record.fields = &field;
        record.field_count = 1;
        Submit(record);
    }

private:
    AbslBridgeOptions options_;
};

}  // namespace

void InstallAbslBridge(const AbslBridgeOptions& options) {
    static bool installed = false;
    if (installed) return;
    installed = true;

    // Never destroyed: Abseil holds the pointer for the life of the process and
    // a LOG(FATAL) unwinds nothing.
    static Bridge bridge(options);
    absl::AddLogSink(&bridge);

    if (options.silence_absl_stderr) {
        absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfinity);
    }
}

}  // namespace go_plugin::log
