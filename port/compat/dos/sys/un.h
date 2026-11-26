/* AF_UNIX socket addresses for 32-bit DOS. The sockets are not available.
 *
 * Part of clang-retro MFC, an independent implementation of the MFC API.
 * SPDX-License-Identifier: AGPL-3.0-only */
#ifndef _SYS_UN_H
#define _SYS_UN_H

#include <sys/socket.h>

struct sockaddr_un {
  sa_family_t sun_family;
  char sun_path[108];
};

#endif
