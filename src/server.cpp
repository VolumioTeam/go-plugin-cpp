#include "go_plugin/server.hpp"
#include "go_plugin/tls.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// POSIX socket headers for port probing
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

namespace go_plugin {

namespace {

bool ValidateMagicCookie(const HandshakeConfig &h) {
  const char *val = std::getenv(h.magic_cookie_key.c_str());
  return val && std::string(val) == h.magic_cookie_value;
}

// Returns true if we can bind a TCP socket on 127.0.0.1:port right now.
bool IsPortAvailable(int port) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return false;

  int opt = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(static_cast<uint16_t>(port));

  bool ok = ::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0;
  ::close(fd);
  return ok;
}

/**
 * Returns a port in [PLUGIN_MIN_PORT, PLUGIN_MAX_PORT] that appears to be
 * available, or 0 to let the OS choose (when the env vars are absent).
 *
 * Ports are tried in a random order to reduce collision probability when
 * multiple plugin processes are started simultaneously.
 */
int PickPort() {
  const char *min_str = std::getenv("PLUGIN_MIN_PORT");
  const char *max_str = std::getenv("PLUGIN_MAX_PORT");
  if (!min_str || !max_str)
    return 0;

  int min_port = std::stoi(min_str);
  int max_port = std::stoi(max_str);
  if (min_port < 1 || max_port > 65535 || min_port > max_port)
    return 0;

  std::vector<int> ports(static_cast<size_t>(max_port - min_port + 1));
  std::iota(ports.begin(), ports.end(), min_port);

  std::random_device rd;
  std::mt19937 gen(rd());
  std::shuffle(ports.begin(), ports.end(), gen);

  for (int p : ports) {
    if (IsPortAvailable(p))
      return p;
  }
  return 0; // fallback: let gRPC pick
}

} // namespace

// ── PluginServer ─────────────────────────────────────────────────────────────

PluginServer::PluginServer(ServeConfig config) : config_(std::move(config)) {}

PluginServer::~PluginServer() {
  if (server_) {
    server_->Shutdown();
    server_->Wait();
  }
}

bool PluginServer::Start(std::string *out_error) {
  auto fail = [&](std::string msg) {
    if (out_error)
      *out_error = std::move(msg);
    return false;
  };

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
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();

  grpc::ServerBuilder builder;
  int selected_port = 0;
  int hint_port = PickPort();
  std::string listen_addr = "127.0.0.1:" + std::to_string(hint_port);
  builder.AddListeningPort(listen_addr, creds, &selected_port);

  for (auto *svc : config_.services) {
    builder.RegisterService(svc);
  }

  server_ = builder.BuildAndStart();
  if (!server_) {
    return fail("gRPC server failed to start on " + listen_addr);
  }
  if (selected_port == 0) {
    server_->Shutdown();
    server_.reset();
    return fail("gRPC server started but reported port 0 (bind failed?)");
  }
  port_ = selected_port;

  // ── 4. Write go-plugin handshake ─────────────────────────────────────
  // Format: CORE-PROTO|APP-PROTO|NETWORK|ADDR|PROTOCOL|SERVER-CERT\n
  std::ostream &out = config_.output ? *config_.output : std::cout;
  out << "1"
      << "|" << config_.handshake.protocol_version << "|tcp"
      << "|127.0.0.1:" << port_ << "|grpc"
      << "|" << server_cert_b64 << "\n";
  out.flush();

  return true;
}

void PluginServer::Wait() {
  if (server_)
    server_->Wait();
}

void PluginServer::Shutdown() {
  if (server_)
    server_->Shutdown();
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
