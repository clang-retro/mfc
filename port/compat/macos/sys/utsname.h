// sys/utsname.h - the system's name for the classic Mac OS. The version
// comes from Gestalt('sysv').
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_MACOS_UTSNAME_H
#define WINPORT_MACOS_UTSNAME_H

#include <stdio.h>
#include <string.h>

struct utsname {
  char sysname[32];
  char nodename[32];
  char release[32];
  char version[32];
  char machine[32];
};

#ifdef __cplusplus
extern "C" {
#endif

// Gets the System's version from Gestalt ('sysv'), as BCD 0xMMmb.
long _WinportMacSystemVersion(void);

// Fills in the names: "MacOS", "Macintosh", the System's version and the
// CPU.
static inline int uname(struct utsname* u) {
  long v = _WinportMacSystemVersion();
  strcpy(u->sysname, "MacOS");
  strcpy(u->nodename, "Macintosh");
  snprintf(u->release, sizeof u->release, "%lx.%lx.%lx", (v >> 8) & 0xFF, (v >> 4) & 0xF, v & 0xF);
  strcpy(u->version, u->release);
#ifdef __m68k__
  strcpy(u->machine, "m68k");
#else
  strcpy(u->machine, "powerpc");
#endif
  return 0;
}

#ifdef __cplusplus
}
#endif

#endif
