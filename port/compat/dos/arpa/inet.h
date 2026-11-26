/* Byte order and address text for 32-bit DOS (port/src/compat_dos.cpp).
 *
 * Part of clang-retro MFC, an independent implementation of the MFC API.
 * SPDX-License-Identifier: AGPL-3.0-only */
#ifndef _ARPA_INET_H
#define _ARPA_INET_H
#define __ARPA_INET_H__

#include <sys/types.h>

/* The 386 is little-endian: the network order is the other order. */
#define htonl(x) ((__uint32_t)__builtin_bswap32((__uint32_t)(x)))
#define htons(x) ((__uint16_t)__builtin_bswap16((__uint16_t)(x)))
#define ntohl(x) htonl(x)
#define ntohs(x) htons(x)

struct in_addr;
#ifndef _SOCKLEN_T_DECLARED
#define _SOCKLEN_T_DECLARED
typedef __uint32_t socklen_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

in_addr_t inet_addr(const char *text);
int inet_aton(const char *text, struct in_addr *addr);
char *inet_ntoa(struct in_addr addr);
const char *inet_ntop(int family, const void *addr, char *buf, socklen_t size);
int inet_pton(int family, const char *text, void *addr);

#ifdef __cplusplus
}
#endif

#endif
