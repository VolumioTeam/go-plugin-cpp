#pragma once

namespace go_plugin::log {

/**
 * AbslBridgeOptions tunes how Abseil's severities are mapped onto the five the
 * host understands. Abseil has no debug or trace severity of its own — those
 * only exist as VLOG verbosities — so the mapping has to be stated somewhere.
 */
struct AbslBridgeOptions {
    /** VLOG(n) at or above this verbosity is reported as trace rather than debug. */
    int trace_from_verbosity = 2;

    /**
     * Carry the source file and line of each line as a `caller` field. Abseil
     * knows them and the host has nowhere else to learn them.
     */
    bool include_caller = true;

    /**
     * Stop Abseil writing its own copy of every line to standard error. Left
     * on, each line reaches the host twice: once as unparsed text at the host's
     * level, once in the format it can read.
     */
    bool silence_absl_stderr = true;
};

/**
 * InstallAbslBridge routes Abseil's LOG() and VLOG() through the format the
 * host parses, so a plugin keeps its own severity and gains structured fields
 * without touching a single call site.
 *
 * Safe to call more than once; only the first call installs a sink. Call it
 * after absl::InitializeLog().
 */
void InstallAbslBridge(const AbslBridgeOptions& options = {});

}  // namespace go_plugin::log
