// tls.h - TLS on a connected socket, for WinInet's HTTPS: OpenSSL,
// loaded when first used, on Linux and the other Unix systems, and the
// system's Secure Transport on macOS (loaded the same way).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_TLS_H
#define WINPORT_TLS_H

#include <stddef.h>
#include <sys/types.h>

#include <string>

namespace winport {
namespace tls {

// What was wrong with the server's certificate.
enum : unsigned {
  kUnknownCa = 1,     // not from an authority the system trusts
  kNameMismatch = 2,  // not for this host
  kDateInvalid = 4,   // expired, or not valid yet
  kOther = 8,         // anything else
};

// Starts TLS on a connected socket, as a client of host, and checks the
// server's certificate. The problems in ignore (the flags above) do not
// stop it. On failure the last error is set (ERROR_INTERNET_INVALID_CA,
// ERROR_INTERNET_SEC_CERT_CN_INVALID, ... or
// ERROR_INTERNET_SECURITY_CHANNEL_ERROR) and the socket is left as it was.
bool Start(int fd, const std::string& host, unsigned ignore);

// Whether a socket has TLS started on it.
bool Active(int fd);

// send and recv for a socket, through its TLS when it has it.
ssize_t Send(int fd, const void* p, size_t n);
ssize_t Recv(int fd, void* p, size_t n);

// Ends a socket's TLS (if any) and closes it.
void Close(int fd);

}  // namespace tls
}  // namespace winport

#endif
