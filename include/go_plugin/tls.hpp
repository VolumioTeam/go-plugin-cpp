#pragma once

#include <memory>
#include <string>
#include <vector>

#include <grpcpp/security/server_credentials.h>

namespace go_plugin {
namespace tls {

/**
 * Result from CreateAutoMTLSCredentials().
 */
struct AutoMTLSResult {
    /** Non-null gRPC server credentials on success. */
    std::shared_ptr<grpc::ServerCredentials> credentials;

    /**
     * Base64-encoded DER certificate to append to the go-plugin handshake
     * line (the SERVER-CERT field).  Empty if credentials is null.
     */
    std::string server_cert_b64;

    /** Non-empty string describes the failure when credentials is null. */
    std::string error;
};

/**
 * Creates mutual-TLS server credentials for a go-plugin auto-mTLS session.
 *
 * The function:
 *   1. Reads the PEM client certificate from the PLUGIN_CLIENT_CERT
 *      environment variable (set by the go-plugin host).
 *   2. Generates a fresh RSA-2048 self-signed server certificate.
 *   3. Returns gRPC SslServerCredentials that require the host to present
 *      the supplied client certificate.
 *
 * The returned server_cert_b64 must be written as the last field of the
 * go-plugin handshake line so the host can verify the plugin's identity.
 *
 * Returns {nullptr, "", error} when PLUGIN_CLIENT_CERT is absent or invalid.
 */
AutoMTLSResult CreateAutoMTLSCredentials();

}  // namespace tls
}  // namespace go_plugin
