// PsychoVault original source. My TLS client. I drive the handshake myself, read
// the certificate the far side sent, and check both the chain and the name
// through the security provider the operating system maintains. A build without a
// provider refuses the connection rather than sending anything in the clear.
#include "Psycho_transport.hpp"

#ifdef _WIN32
#define NOMINMAX
// The security headers are user-mode ones, and the socket header has to come before
// windows.h or winsock refuses the order.
#include <winsock2.h>
#define SECURITY_WIN32
#define SECURITY_WINDOWS
#include <windows.h>
#include <schannel.h>
#include <security.h>
#include <wincrypt.h>
#endif

namespace pv {

struct TlsStream::Impl {
  ~Impl() {
    close();
  }
  void close() {
#ifdef _WIN32
    if (context.dwLower || context.dwUpper)
      DeleteSecurityContext(&context);
    context = {};
    if (credentials.dwLower || credentials.dwUpper)
      FreeCredentialsHandle(&credentials);
    credentials = {};
#endif
    alive = false;
  }
  bool alive = false;
  std::string server_name;
  // The subject from the certificate, filled in once the handshake is verified.
  std::string subject;
  Bytes incoming, plain;
#ifdef _WIN32
  CredHandle credentials{};
  CtxtHandle context{};
  SecPkgContext_StreamSizes sizes{};
#endif
};

TlsStream::TlsStream() : impl_(std::make_unique<Impl>()) {}
TlsStream::~TlsStream() = default;

const std::string &TlsStream::peer_subject() const {
  static const std::string empty;
  return impl_ ? impl_->subject : empty;
}

#ifdef _WIN32
namespace {
std::wstring widen(const std::string &text) {
  if (text.empty())
    return {};
  int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0);
  std::wstring out(size_t(size), 0);
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), size);
  return out;
}
std::string status_text(const char *where, long status) {
  char buffer[24];
  std::snprintf(buffer, sizeof buffer, "%08lx", static_cast<unsigned long>(status));
  return std::string(where) + " (0x" + buffer + ")";
}

// I check the name and the chain against the roots Windows trusts. A chain I
// could not build is a refusal, never a warning.
bool verify_certificate(PCCERT_CONTEXT leaf, const std::string &server_name, std::string &error) {
  if (!leaf) {
    error = "the far side presented no certificate, so I stopped";
    return false;
  }
  CERT_CHAIN_PARA params{};
  params.cbSize = sizeof params;
  // Revocation checking is a flag on the call, not on the parameters, and asking is
  // the difference between "this certificate is revoked" and "nobody told me either
  // way". I ask, and I keep the two answers apart below.
  PCCERT_CHAIN_CONTEXT chain = nullptr;
  if (!CertGetCertificateChain(nullptr, leaf, nullptr, leaf->hCertStore, &params,
                               CERT_CHAIN_CACHE_END_CERT |
                                   CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT,
                               nullptr, &chain) ||
      !chain) {
    error = "I could not build a certificate chain for this server, so I stopped";
    return false;
  }
  DWORD status = chain->TrustStatus.dwErrorStatus;
  if (status & CERT_TRUST_IS_REVOKED) {
    error = "the certificate this server sent is revoked, so I stopped";
    CertFreeCertificateChain(chain);
    return false;
  }
  if ((status & CERT_TRUST_IS_UNTRUSTED_ROOT) || (status & CERT_TRUST_IS_PARTIAL_CHAIN) ||
      (status & CERT_TRUST_IS_NOT_TIME_VALID) || (status & CERT_TRUST_IS_NOT_SIGNATURE_VALID)) {
    error = "the certificate this server sent does not chain to a root I trust, so I stopped";
    CertFreeCertificateChain(chain);
    return false;
  }
  auto wide_name = widen(server_name);
  SSL_EXTRA_CERT_CHAIN_POLICY_PARA extra{};
  extra.cbSize = sizeof extra;
  extra.pwszServerName = const_cast<wchar_t *>(wide_name.c_str());
  CERT_CHAIN_POLICY_PARA policy{};
  policy.cbSize = sizeof policy;
  policy.pvExtraPolicyPara = &extra;
  CERT_CHAIN_POLICY_STATUS policy_status{};
  policy_status.cbSize = sizeof policy_status;
  bool ok = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &policy, &policy_status) != FALSE &&
            policy_status.dwError == 0;
  CertFreeCertificateChain(chain);
  if (!ok) {
    error = "the certificate this server sent is not for " + server_name + ", so I stopped";
    return false;
  }
  return true;
}
} // namespace

std::unique_ptr<TlsStream> TlsStream::open(Stream &underneath, const std::string &server_name,
                                           std::string &error) {
  auto stream = std::unique_ptr<TlsStream>(new TlsStream());
  auto &impl = *stream->impl_;
  impl.server_name = server_name;
  stream->under_ = &underneath;
  SCHANNEL_CRED credential{};
  credential.dwVersion = SCHANNEL_CRED_VERSION;
  credential.grbitEnabledProtocols = 0; // the provider picks TLS 1.2 and above
  credential.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS;
  credential.dwMinimumCipherStrength = 128;
  TimeStamp expiry{};
  // The provider name is the one Schannel registers for user-mode TLS.
  if (AcquireCredentialsHandleW(nullptr, const_cast<wchar_t *>(UNISP_NAME_W), SECPKG_CRED_OUTBOUND,
                                nullptr, &credential, nullptr, nullptr, &impl.credentials,
                                &expiry) != SEC_E_OK) {
    error = status_text("I could not set up TLS", long(GetLastError()));
    return nullptr;
  }
  auto wide_name = widen(server_name);
  DWORD request_flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
                        ISC_REQ_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM |
                        ISC_REQ_MANUAL_CRED_VALIDATION;
  bool first = true;
  for (int round = 0; round < 12; ++round) {
    SecBuffer out_buffer{};
    out_buffer.BufferType = SECBUFFER_TOKEN;
    SecBufferDesc out_desc{SECBUFFER_VERSION, 1, &out_buffer};
    SecBuffer in_buffers[2]{};
    SecBufferDesc in_desc{SECBUFFER_VERSION, 0, in_buffers};
    if (!first && !impl.incoming.empty()) {
      in_buffers[0].BufferType = SECBUFFER_TOKEN;
      in_buffers[0].pvBuffer = impl.incoming.data();
      in_buffers[0].cbBuffer = DWORD(impl.incoming.size());
      in_desc.cBuffers = 1;
    }
    DWORD attributes = 0;
    SECURITY_STATUS status =
        InitializeSecurityContextW(&impl.credentials, first ? nullptr : &impl.context,
                                   const_cast<wchar_t *>(wide_name.c_str()), request_flags, 0, 0,
                                   first || impl.incoming.empty() ? nullptr : &in_desc, 0,
                                   &impl.context, &out_desc, &attributes, &expiry);
    first = false;
    Bytes extra;
    if (in_desc.cBuffers > 0 && in_buffers[0].BufferType == SECBUFFER_EXTRA && in_buffers[0].cbBuffer > 0) {
      auto *from = static_cast<const uint8_t *>(in_buffers[0].pvBuffer);
      extra.assign(from, from + in_buffers[0].cbBuffer);
    }
    impl.incoming = std::move(extra);
    if (out_buffer.cbBuffer > 0 && out_buffer.pvBuffer) {
      bool sent = underneath.write_all(static_cast<const uint8_t *>(out_buffer.pvBuffer),
                                       out_buffer.cbBuffer, error);
      FreeContextBuffer(out_buffer.pvBuffer);
      if (!sent)
        return nullptr;
    }
    if (status == SEC_E_OK)
      break;
    if (status == SEC_I_CONTINUE_NEEDED)
      ;
    else if (status == SEC_E_INCOMPLETE_MESSAGE)
      ;
    else {
      error = status_text(("the TLS handshake with " + server_name + " failed").c_str(), long(status));
      return nullptr;
    }
    if (impl.incoming.empty()) {
      // The handshake needs more bytes from the far side before it can continue.
      uint8_t chunk[16384];
      size_t got = 0;
      if (!underneath.read_some(chunk, sizeof chunk, got, error)) {
        if (error.empty())
          error = "the server closed during the TLS handshake";
        return nullptr;
      }
      impl.incoming.insert(impl.incoming.end(), chunk, chunk + got);
      first = false;
    }
    if (status == SEC_E_OK)
      break;
  }
  if (!impl.context.dwLower && !impl.context.dwUpper) {
    error = "the TLS handshake with " + server_name + " did not finish";
    return nullptr;
  }
  if (QueryContextAttributesW(&impl.context, SECPKG_ATTR_STREAM_SIZES, &impl.sizes) != SEC_E_OK) {
    error = "TLS is up but the provider gave me no stream sizes";
    return nullptr;
  }
  PCCERT_CONTEXT leaf = nullptr;
  if (QueryContextAttributesW(&impl.context, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &leaf) != SEC_E_OK ||
      !leaf) {
    error = "TLS is up but the server sent no certificate I can check";
    return nullptr;
  }
  bool verified = verify_certificate(leaf, server_name, error);
  // The subject the certificate actually carries, which is what peer_subject means.
  char subject[512] = {0};
  if (CertGetNameStringA(leaf, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, subject,
                         sizeof subject) > 1)
    impl.subject = subject;
  CertFreeCertificateContext(leaf);
  if (!verified)
    return nullptr;
  impl.alive = true;
  return stream;
}

bool TlsStream::write_all(const uint8_t *data, size_t size, std::string &error) {
  if (!impl_->alive || !under_) {
    error = "this TLS stream is closed";
    return false;
  }
  size_t at = 0;
  while (at < size) {
    size_t take = std::min(size - at, size_t(impl_->sizes.cbMaximumMessage));
    Bytes record(size_t(impl_->sizes.cbHeader) + take + size_t(impl_->sizes.cbTrailer));
    SecBuffer buffers[4]{};
    buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
    buffers[0].pvBuffer = record.data();
    buffers[0].cbBuffer = impl_->sizes.cbHeader;
    buffers[1].BufferType = SECBUFFER_DATA;
    buffers[1].pvBuffer = record.data() + impl_->sizes.cbHeader;
    buffers[1].cbBuffer = DWORD(take);
    std::memcpy(buffers[1].pvBuffer, data + at, take);
    buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
    buffers[2].pvBuffer = record.data() + impl_->sizes.cbHeader + take;
    buffers[2].cbBuffer = impl_->sizes.cbTrailer;
    SecBufferDesc desc{SECBUFFER_VERSION, 4, buffers};
    if (EncryptMessage(&impl_->context, 0, &desc, 0) != SEC_E_OK) {
      error = "I could not encrypt a TLS record";
      return false;
    }
    size_t total =
        size_t(buffers[0].cbBuffer) + size_t(buffers[1].cbBuffer) + size_t(buffers[2].cbBuffer);
    if (!under_->write_all(record.data(), total, error))
      return false;
    at += take;
  }
  return true;
}

bool TlsStream::read_some(uint8_t *out, size_t capacity, size_t &got, std::string &error) {
  got = 0;
  auto &impl = *impl_;
  if (!impl.alive || !under_) {
    error = "this TLS stream is closed";
    return false;
  }
  // Whatever I already decrypted goes out first, so a record larger than the
  // caller's buffer is not lost.
  if (!impl.plain.empty()) {
    size_t take = std::min(capacity, impl.plain.size());
    std::memcpy(out, impl.plain.data(), take);
    impl.plain.erase(impl.plain.begin(), impl.plain.begin() + std::ptrdiff_t(take));
    got = take;
    return true;
  }
  for (int round = 0; round < 32; ++round) {
    if (impl.incoming.empty()) {
      uint8_t chunk[16384];
      size_t read = 0;
      if (!under_->read_some(chunk, sizeof chunk, read, error)) {
        if (error.empty())
          impl.alive = false;
        return false;
      }
      impl.incoming.assign(chunk, chunk + read);
    }
    SecBuffer buffers[4]{};
    buffers[0].BufferType = SECBUFFER_DATA;
    buffers[0].pvBuffer = impl.incoming.data();
    buffers[0].cbBuffer = DWORD(impl.incoming.size());
    buffers[1].BufferType = SECBUFFER_EMPTY;
    buffers[2].BufferType = SECBUFFER_EMPTY;
    buffers[3].BufferType = SECBUFFER_EMPTY;
    SecBufferDesc desc{SECBUFFER_VERSION, 4, buffers};
    SECURITY_STATUS status = DecryptMessage(&impl.context, &desc, 0, nullptr);
    if (status == SEC_I_CONTEXT_EXPIRED) {
      impl.alive = false;
      return false;
    }
    if (status == SEC_I_RENEGOTIATE) {
      error = "the server asked to renegotiate TLS, and I refuse that";
      return false;
    }
    if (status == SEC_E_INCOMPLETE_MESSAGE) {
      uint8_t chunk[16384];
      size_t read = 0;
      if (!under_->read_some(chunk, sizeof chunk, read, error))
        return false;
      impl.incoming.insert(impl.incoming.end(), chunk, chunk + read);
      continue;
    }
    if (status != SEC_E_OK) {
      error = "a TLS record from the server would not decrypt";
      return false;
    }
    Bytes extra;
    for (auto &buffer : buffers) {
      if (buffer.BufferType == SECBUFFER_DATA && buffer.cbBuffer > 0) {
        auto *from = static_cast<const uint8_t *>(buffer.pvBuffer);
        impl.plain.insert(impl.plain.end(), from, from + buffer.cbBuffer);
      } else if (buffer.BufferType == SECBUFFER_EXTRA && buffer.cbBuffer > 0) {
        auto *from = static_cast<const uint8_t *>(buffer.pvBuffer);
        extra.assign(from, from + buffer.cbBuffer);
      }
    }
    impl.incoming = std::move(extra);
    if (!impl.plain.empty()) {
      size_t take = std::min(capacity, impl.plain.size());
      std::memcpy(out, impl.plain.data(), take);
      impl.plain.erase(impl.plain.begin(), impl.plain.begin() + std::ptrdiff_t(take));
      got = take;
      return true;
    }
    if (impl.incoming.empty()) {
      // A control record carried nothing for the page. Reading again is the right
      // move; returning a successful zero-byte read would leave my HTTP framing
      // waiting for bytes that never arrive.
      continue;
    }
  }
  error = "the TLS stream did not give me a record I could read";
  return false;
}

bool TlsStream::alive() const {
  return impl_ && impl_->alive;
}

#else // not Windows

std::unique_ptr<TlsStream> TlsStream::open(Stream &, const std::string &server_name,
                                           std::string &error) {
  error = "This build has no TLS provider, so I refuse " + server_name +
          " rather than send anything unencrypted";
  return nullptr;
}
bool TlsStream::write_all(const uint8_t *, size_t, std::string &error) {
  error = "no TLS provider in this build";
  return false;
}
bool TlsStream::read_some(uint8_t *, size_t, size_t &got, std::string &error) {
  got = 0;
  error = "no TLS provider in this build";
  return false;
}
bool TlsStream::alive() const {
  return false;
}
#endif
} // namespace pv
