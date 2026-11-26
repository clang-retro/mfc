// unistd.h - the C library's, with environ and the processor count
// that the layer reads.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_DOS_UNISTD_H
#define WINPORT_DOS_UNISTD_H
#include_next <unistd.h>

#ifndef _SC_NPROCESSORS_ONLN
#define _SC_NPROCESSORS_ONLN 84
#define _SC_NPROCESSORS_CONF 83
#endif

#ifdef __cplusplus
extern "C" {
#endif
extern char** environ;
#ifdef __cplusplus
}
#endif

#endif
