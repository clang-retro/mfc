// stdlib.h - the system's. Before 10.5, unsetenv returns nothing; here it
// returns 0, as POSIX says.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_MACOSX_STDLIB_H
#define WINPORT_MACOSX_STDLIB_H
#include_next <stdlib.h>

#include <AvailabilityMacros.h>
#if MAC_OS_X_VERSION_MAX_ALLOWED < 1050
#define unsetenv(name) (unsetenv(name), 0)
#endif

#endif
