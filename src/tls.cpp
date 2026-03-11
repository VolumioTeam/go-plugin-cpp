#include "go_plugin/tls.hpp"

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace go_plugin {
namespace tls {

namespace {

// Wraps a BIO* in an RAII handle.
struct BioDeleter {
  void operator()(BIO *b) const { BIO_free_all(b); }
};
using BioPtr = std::unique_ptr<BIO, BioDeleter>;

struct X509Deleter {
  void operator()(X509 *c) const { X509_free(c); }
};
using X509Ptr = std::unique_ptr<X509, X509Deleter>;

struct EvpPkeyDeleter {
  void operator()(EVP_PKEY *k) const { EVP_PKEY_free(k); }
};
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, EvpPkeyDeleter>;

std::string LastOpenSSLError() {
  BioPtr bio(BIO_new(BIO_s_mem()));
  ERR_print_errors(bio.get());
  BUF_MEM *bptr = nullptr;
  BIO_get_mem_ptr(bio.get(), &bptr);
  return bptr ? std::string(bptr->data, bptr->length) : "unknown OpenSSL error";
}

struct GeneratedCert {
  std::string cert_pem;
  std::string key_pem;
  std::vector<uint8_t> cert_der;
};

GeneratedCert GenerateSelfSignedCert() {
  // ── Key generation ────────────────────────────────────────────────────
  EvpPkeyPtr pkey;
  {
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr),
                                                                    EVP_PKEY_CTX_free);
    if (!ctx)
      throw std::runtime_error("EVP_PKEY_CTX_new_id: " + LastOpenSSLError());
    if (EVP_PKEY_keygen_init(ctx.get()) <= 0)
      throw std::runtime_error("EVP_PKEY_keygen_init: " + LastOpenSSLError());
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx.get(), 2048) <= 0)
      throw std::runtime_error("set_rsa_keygen_bits: " + LastOpenSSLError());
    EVP_PKEY *raw = nullptr;
    if (EVP_PKEY_keygen(ctx.get(), &raw) <= 0)
      throw std::runtime_error("EVP_PKEY_keygen: " + LastOpenSSLError());
    pkey.reset(raw);
  }

  // ── Certificate ───────────────────────────────────────────────────────
  X509Ptr cert(X509_new());
  if (!cert)
    throw std::runtime_error("X509_new: " + LastOpenSSLError());

  X509_set_version(cert.get(), 2); // X.509v3

  // Serial number = 1
  ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1);

  // Valid for one year
  X509_gmtime_adj(X509_get_notBefore(cert.get()), 0);
  X509_gmtime_adj(X509_get_notAfter(cert.get()), 365 * 24 * 3600L);

  X509_set_pubkey(cert.get(), pkey.get());

  X509_NAME *name = X509_get_subject_name(cert.get());
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char *>("plugin"), -1, -1, 0);
  X509_set_issuer_name(cert.get(), name);

  if (!X509_sign(cert.get(), pkey.get(), EVP_sha256()))
    throw std::runtime_error("X509_sign: " + LastOpenSSLError());

  // ── Serialize certificate to PEM ─────────────────────────────────────
  std::string cert_pem;
  {
    BioPtr bio(BIO_new(BIO_s_mem()));
    PEM_write_bio_X509(bio.get(), cert.get());
    BUF_MEM *bptr = nullptr;
    BIO_get_mem_ptr(bio.get(), &bptr);
    cert_pem.assign(bptr->data, bptr->length);
  }

  // ── Serialize certificate to DER ─────────────────────────────────────
  std::vector<uint8_t> cert_der;
  {
    int len = i2d_X509(cert.get(), nullptr);
    if (len < 0)
      throw std::runtime_error("i2d_X509: " + LastOpenSSLError());
    cert_der.resize(static_cast<size_t>(len));
    unsigned char *ptr = cert_der.data();
    i2d_X509(cert.get(), &ptr);
  }

  // ── Serialize private key to PEM ─────────────────────────────────────
  std::string key_pem;
  {
    BioPtr bio(BIO_new(BIO_s_mem()));
    PEM_write_bio_PrivateKey(bio.get(), pkey.get(), nullptr, nullptr, 0, nullptr, nullptr);
    BUF_MEM *bptr = nullptr;
    BIO_get_mem_ptr(bio.get(), &bptr);
    key_pem.assign(bptr->data, bptr->length);
  }

  return {std::move(cert_pem), std::move(key_pem), std::move(cert_der)};
}

// Base64-encode raw bytes (no newlines, standard alphabet).
std::string Base64Encode(const uint8_t *data, size_t len) {
  BioPtr b64(BIO_push(BIO_new(BIO_f_base64()), BIO_new(BIO_s_mem())));
  BIO_set_flags(b64.get(), BIO_FLAGS_BASE64_NO_NL);
  BIO_write(b64.get(), data, static_cast<int>(len));
  BIO_flush(b64.get());

  // The mem BIO is the second in the chain; get it via BIO_next.
  BUF_MEM *bptr = nullptr;
  BIO_get_mem_ptr(BIO_next(b64.get()), &bptr);
  return {bptr->data, bptr->length};
}

} // namespace

AutoMTLSResult CreateAutoMTLSCredentials() {
  const char *env = std::getenv("PLUGIN_CLIENT_CERT");
  if (!env || *env == '\0') {
    return {nullptr, "", "PLUGIN_CLIENT_CERT environment variable not set"};
  }
  std::string client_cert_pem(env);

  GeneratedCert gen;
  try {
    gen = GenerateSelfSignedCert();
  } catch (const std::exception &e) {
    return {nullptr, "", std::string("cert generation failed: ") + e.what()};
  }

  grpc::SslServerCredentialsOptions ssl_opts(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  ssl_opts.pem_key_cert_pairs.push_back({gen.key_pem, gen.cert_pem});
  ssl_opts.pem_root_certs = client_cert_pem;

  auto creds = grpc::SslServerCredentials(ssl_opts);
  std::string b64 = Base64Encode(gen.cert_der.data(), gen.cert_der.size());

  return {std::move(creds), std::move(b64), ""};
}

} // namespace tls
} // namespace go_plugin
