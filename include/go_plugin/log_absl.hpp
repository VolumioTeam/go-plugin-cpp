#pragma once

namespace go_plugin::log {

struct AbslBridgeOptions {
    /**
     * Abseil has no debug or trace severity of its own — they exist only as
     * VLOG verbosities — so the mapping has to be stated. VLOG at or above this
     * verbosity is reported as trace, below it as debug.
     */
    int trace_from_verbosity = 2;

    /** Carry Abseil's source file and line as a `caller` field. */
    bool include_caller = true;

    /**
     * Stop Abseil writing its own copy of every line. Left on, each line
     * reaches the host twice: once as unparsed text, once in the format it can
     * read.
     */
    bool silence_absl_stderr = true;
};

/**
 * Routes Abseil's LOG() and VLOG() through the format the host parses, so a
 * plugin keeps its own severity without touching a call site.
 *
 * Call after absl::InitializeLog(). Only the first call installs a sink.
 */
void InstallAbslBridge(const AbslBridgeOptions& options = {});

}  // namespace go_plugin::log
