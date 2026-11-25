// fcntl.h - the system's, with O_CLOEXEC, which Mac OS X has from 10.7.
// Before that, a descriptor stays open in a child program; the layer
// starts few child programs, so the flag is 0.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_MACOSX_FCNTL_H
#define WINPORT_MACOSX_FCNTL_H
#include_next <fcntl.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#endif
