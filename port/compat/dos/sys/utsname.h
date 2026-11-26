/* uname for 32-bit DOS (port/src/compat_dos.cpp).
 *
 * Part of clang-retro MFC, an independent implementation of the MFC API.
 * SPDX-License-Identifier: AGPL-3.0-only */
#ifndef _SYS_UTSNAME_H
#define _SYS_UTSNAME_H

struct utsname {
  char sysname[32];  /* "MS-DOS" */
  char nodename[32]; /* "PC" */
  char release[32];  /* the DOS version, such as "6.22" */
  char version[32];  /* the same */
  char machine[32];  /* "i386" */
};

#ifdef __cplusplus
extern "C" {
#endif

int uname(struct utsname *u);

#ifdef __cplusplus
}
#endif

#endif
