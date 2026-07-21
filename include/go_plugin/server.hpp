#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

#include <grpcpp/grpcpp.h>

namespace go_plugin {

/**
 * HandshakeConfig holds the magic-cookie identity check that prevents the
 * plugin binary from being run by anything other than the expected host.
 * The values here must match the host's plugin.HandshakeConfig.
 */
struct HandshakeConfig {
    std::string magic_cookie_key;    // Environment variable name set by the host
    std::string magic_cookie_value;  // Expected value of that variable
    int protocol_version = 1;        // Application-level gRPC protocol version
};

/**
 * ServeConfig bundles everything needed to start a plugin gRPC server.
 */
struct ServeConfig {
    HandshakeConfig handshake;

    /**
     * gRPC service instances to register.  The caller retains ownership;
     * all pointers must remain valid for the lifetime of the PluginServer.
     */
    std::vector<grpc::Service*> services;

    /**
     * Optional custom server credentials.
     *
     * - If nullptr and PLUGIN_CLIENT_CERT is set in the environment, the
     *   library automatically configures mutual TLS (auto-mTLS) using the
     *   supplied client certificate and a freshly generated server certificate.
     * - If nullptr and PLUGIN_CLIENT_CERT is absent, insecure (plaintext)
     *   transport is used.
     * - Supply explicit credentials to override both heuristics.
     */
    std::shared_ptr<grpc::ServerCredentials> credentials;

    /**
     * Stream that receives the go-plugin handshake line.
     * Defaults to std::cout when nullptr.  Override in tests to capture output.
     */
    std::ostream* output = nullptr;
};

/**
 * PluginServer manages the lifecycle of a go-plugin gRPC plugin server.
 *
 * Typical plugin main():
 * @code
 *   MyService svc;
 *   go_plugin::PluginServer server({
 *       .handshake = {"MAGIC_KEY", "magic_value", 1},
 *       .services  = {&svc},
 *   });
 *   if (!server.Start()) { return 1; }
 *   server.Wait();   // blocks until the host closes the connection
 * @endcode
 */
class PluginServer {
public:
    explicit PluginServer(ServeConfig config);
    ~PluginServer();

    PluginServer(const PluginServer&) = delete;
    PluginServer& operator=(const PluginServer&) = delete;

    /**
     * Validates the magic cookie, picks a port, starts the gRPC server,
     * registers services and the built-in health-check service, then writes
     * the go-plugin handshake line to the configured output stream.
     *
     * @param error  If non-null, receives a human-readable error message on
     *               failure.
     * @return true on success.
     */
    bool Start(std::string* error = nullptr);

    /**
     * Blocks until the server stops (i.e. until Shutdown() is called or the
     * host process exits — see the parent-death watchdog started by Start()).
     */
    void Wait();

    /** Initiates an orderly shutdown; safe to call from any thread. */
    void Shutdown();

    /** Returns the TCP port the server is listening on (valid after Start()). */
    int port() const { return port_; }

private:
    // Watches for the host (parent) process disappearing. go-plugin hosts stop a
    // plugin by killing it, but when the host itself dies unexpectedly (e.g. it
    // is SIGKILLed) nothing signals the plugin and Wait() would block forever,
    // orphaning the process. The watchdog detects reparenting (getppid changes)
    // and calls Shutdown(), mirroring the Go go-plugin server's built-in
    // orphan protection. Started by Start(), stopped by Shutdown()/destruction.
    void StartParentWatchdog();
    void StopParentWatchdog();

    ServeConfig config_;
    std::unique_ptr<grpc::Server> server_;
    int port_ = 0;

    std::thread parent_watchdog_;
    std::mutex watchdog_mu_;
    std::condition_variable watchdog_cv_;
    bool watchdog_stop_ = false;
    pid_t host_pid_ = 0;
};

/** Returned by the Serve() convenience function. */
struct ServeResult {
    bool ok = false;
    std::string error;
};

/**
 * Convenience wrapper: constructs a PluginServer, starts it, and blocks until
 * it exits.  Equivalent to creating a PluginServer, calling Start(), and then
 * Wait().
 */
ServeResult Serve(const ServeConfig& config);

}  // namespace go_plugin
