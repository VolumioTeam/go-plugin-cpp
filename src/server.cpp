#include "go_plugin/server.hpp"
#include "go_plugin/tls.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include <grp.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/impl/rpc_service_method.h>
#include <grpcpp/support/method_handler.h>

namespace go_plugin {

namespace {

// go-plugin's GRPCController: the service a host calls to ask the plugin to
// stop. Its one method takes and returns plugin.Empty, which is zero bytes on
// the wire, so it is served over raw byte buffers and needs no generated code.
class ControllerService final : public grpc::Service {
public:
  explicit ControllerService(std::function<void()> on_shutdown) : on_shutdown_(std::move(on_shutdown)) {
    using Handler = grpc::internal::RpcMethodHandler<ControllerService, grpc::ByteBuffer, grpc::ByteBuffer>;
    AddMethod(new grpc::internal::RpcServiceMethod(
        "/plugin.GRPCController/Shutdown", grpc::internal::RpcMethod::NORMAL_RPC,
        new Handler(
            [](ControllerService *service, grpc::ServerContext *, const grpc::ByteBuffer *, grpc::ByteBuffer *reply) {
              grpc::Slice empty;
              *reply = grpc::ByteBuffer(&empty, 1);
              service->on_shutdown_();
              return grpc::Status::OK;
            },
            this)));
  }

private:
  std::function<void()> on_shutdown_;
};

bool ValidateMagicCookie(const HandshakeConfig &h) {
  const char *val = std::getenv(h.magic_cookie_key.c_str());
  return val && std::string(val) == h.magic_cookie_value;
}

// Reserves a path for the plugin's socket in the directory the host made for
// it, as go-plugin's own servers do: a file of a unique name is created and
// removed again, since a socket can only be bound where nothing is.
bool ReserveSocketPath(std::string *path, std::string *error) {
  const char *dir = std::getenv("PLUGIN_UNIX_SOCKET_DIR");
  if (!dir || !*dir)
    dir = std::getenv("TMPDIR");
  if (!dir || !*dir)
    dir = "/tmp";

  std::string name = std::string(dir) + "/pluginXXXXXX";
  if (name.size() >= sizeof(sockaddr_un{}.sun_path)) {
    *error = "socket directory " + std::string(dir) + " is too long a path for a Unix socket";
    return false;
  }
  int fd = ::mkstemp(name.data());
  if (fd < 0) {
    *error = "reserve a socket path in " + std::string(dir) + ": " + std::strerror(errno);
    return false;
  }
  ::close(fd);
  ::unlink(name.c_str());
  *path = std::move(name);
  return true;
}

// Opens the socket to a group the host names, by name or number, as go-plugin's
// own servers do.
bool SetGroupWritable(const std::string &path, const std::string &group, std::string *error) {
  gid_t gid;
  const bool numeric = std::all_of(group.begin(), group.end(), [](unsigned char c) { return std::isdigit(c); });
  if (numeric) {
    errno = 0;
    unsigned long long number = std::strtoull(group.c_str(), nullptr, 10);
    if (errno != 0 || number > static_cast<gid_t>(-1)) {
      *error = "group " + group + " is out of range";
      return false;
    }
    gid = static_cast<gid_t>(number);
  } else if (const struct group *found = ::getgrnam(group.c_str())) {
    gid = found->gr_gid;
  } else {
    *error = "no group " + group;
    return false;
  }
  if (::chown(path.c_str(), static_cast<uid_t>(-1), gid) != 0 || ::chmod(path.c_str(), 0660) != 0) {
    *error = "open the socket to group " + group + ": " + std::strerror(errno);
    return false;
  }
  return true;
}

} // namespace

// ── PluginServer ─────────────────────────────────────────────────────────────

PluginServer::PluginServer(ServeConfig config) : config_(std::move(config)) {}

PluginServer::~PluginServer() {
  StopParentWatchdog();
  if (server_) {
    server_->Shutdown();
    server_->Wait();
  }
  if (!socket_path_.empty())
    ::unlink(socket_path_.c_str());
}

bool PluginServer::Start(std::string *out_error) {
  auto fail = [&](std::string msg) {
    if (out_error)
      *out_error = std::move(msg);
    return false;
  };

  // Record the launching host's pid up front, before any startup work could let
  // it reparent us — this is the baseline the parent-death watchdog compares
  // against. The watchdog itself only starts once startup succeeds (below).
  host_pid_ = getppid();

  // ── 1. Magic cookie check ────────────────────────────────────────────
  if (!ValidateMagicCookie(config_.handshake)) {
    return fail("magic cookie mismatch: plugin not invoked by a go-plugin host "
                "(env var '" +
                config_.handshake.magic_cookie_key + "' missing or wrong)");
  }

  // ── 2. Resolve credentials (auto-mTLS if host supplied a client cert) ─
  std::string server_cert_b64; // non-empty when mTLS is active
  std::shared_ptr<grpc::ServerCredentials> creds = config_.credentials;
  if (!creds) {
    if (std::getenv("PLUGIN_CLIENT_CERT")) {
      auto mtls = tls::CreateAutoMTLSCredentials();
      if (!mtls.credentials) {
        return fail("auto-mTLS setup failed: " + mtls.error);
      }
      creds = mtls.credentials;
      server_cert_b64 = std::move(mtls.server_cert_b64);
    } else {
      creds = grpc::InsecureServerCredentials();
    }
  }

  // ── 3. Build gRPC server ─────────────────────────────────────────────
  grpc::EnableDefaultHealthCheckService(true);

  std::string socket_path;
  std::string error;
  if (!ReserveSocketPath(&socket_path, &error)) {
    return fail(std::move(error));
  }

  grpc::ServerBuilder builder;
  int selected_port = 0;
  std::string listen_addr = "unix:" + socket_path;
  builder.AddListeningPort(listen_addr, creds, &selected_port);

  for (auto *svc : config_.services) {
    builder.RegisterService(svc);
  }
  controller_ = std::make_unique<ControllerService>([this] { RequestShutdown(); });
  builder.RegisterService(controller_.get());

  server_ = builder.BuildAndStart();
  if (!server_) {
    return fail("gRPC server failed to start on " + listen_addr);
  }
  if (selected_port == 0) {
    server_->Shutdown();
    server_.reset();
    return fail("gRPC server started but did not bind " + listen_addr);
  }
  socket_path_ = std::move(socket_path);

  if (const char *group = std::getenv("PLUGIN_UNIX_SOCKET_GROUP"); group && *group) {
    if (!SetGroupWritable(socket_path_, group, &error)) {
      server_->Shutdown();
      server_.reset();
      return fail(std::move(error));
    }
  }

  // ── 4. Write go-plugin handshake ─────────────────────────────────────
  // Format: CORE-PROTO|APP-PROTO|NETWORK|ADDR|PROTOCOL|SERVER-CERT\n
  std::ostream &out = config_.output ? *config_.output : std::cout;
  out << "1"
      << "|" << config_.handshake.protocol_version << "|unix"
      << "|" << socket_path_ << "|grpc"
      << "|" << server_cert_b64 << "\n";
  out.flush();

  // Startup succeeded and Wait() is about to become the plugin's blocking point:
  // start watching the host (host_pid_, captured at the top of Start()).
  StartParentWatchdog();

  return true;
}

void PluginServer::Wait() {
  if (server_)
    server_->Wait();
}

void PluginServer::Shutdown() {
  // Stop (and, for external callers, join) the watchdog before tearing down the
  // server, so no watchdog thread outlives Shutdown() for an external caller.
  StopParentWatchdog();
  if (server_)
    server_->Shutdown();
}

void PluginServer::StartParentWatchdog() {
  parent_watchdog_ = std::thread([this]() {
    std::unique_lock<std::mutex> lock(watchdog_mu_);
    while (!watchdog_stop_) {
      if (shutdown_requested_) {
        lock.unlock();
        Shutdown();
        return;
      }
      // Reparenting (parent pid changes, typically to the reaper/init) means the
      // host that launched us is gone. Drive the same Shutdown() the host would
      // have, so we don't linger as an orphan.
      if (getppid() != host_pid_) {
        lock.unlock();
        std::cerr << "go-plugin: host process exited, shutting down" << std::endl;
        Shutdown();
        return;
      }
      watchdog_cv_.wait_for(lock, std::chrono::seconds(1));
    }
  });
}

void PluginServer::RequestShutdown() {
  {
    std::lock_guard<std::mutex> lock(watchdog_mu_);
    shutdown_requested_ = true;
  }
  watchdog_cv_.notify_all();
}

void PluginServer::StopParentWatchdog() {
  {
    std::lock_guard<std::mutex> lock(watchdog_mu_);
    watchdog_stop_ = true;
  }
  watchdog_cv_.notify_all();
  // The watchdog reaches here via its own Shutdown() call on the host-death
  // path; it must never join itself (that deadlocks). The destructor joins it.
  if (parent_watchdog_.joinable() &&
      parent_watchdog_.get_id() != std::this_thread::get_id())
    parent_watchdog_.join();
}

// ── Serve convenience function ────────────────────────────────────────────────

ServeResult Serve(const ServeConfig &config) {
  PluginServer server(config);
  std::string error;
  if (!server.Start(&error)) {
    return {false, std::move(error)};
  }
  server.Wait();
  return {true, ""};
}

} // namespace go_plugin
