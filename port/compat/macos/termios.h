// termios.h - the C library's, with the declaration of cfmakeraw, which
// the layer uses for serial ports. mac-sdk's C library has it.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_MACOS_TERMIOS_H
#define WINPORT_MACOS_TERMIOS_H
#include_next <termios.h>

#ifdef __cplusplus
extern "C" {
#endif
void cfmakeraw(struct termios* t);
#ifdef __cplusplus
}
#endif

#endif
