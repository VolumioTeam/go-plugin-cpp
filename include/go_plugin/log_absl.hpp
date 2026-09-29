#pragma once

#include "go_plugin/log.hpp"

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

/**
 * Sets the least severe level a plugin logs at, through Abseil as much as
 * through Write: Abseil's minimum severity, and the VLOG verbosity that debug
 * and trace map to under InstallAbslBridge's options. For a plugin whose host
 * moves its level while it runs.
 */
void SetAbslLevel(Level min);

}  // namespace go_plugin::log
