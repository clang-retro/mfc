// sys/ioctl.h - the C library's, with the request numbers that the
// layer names for serial ports and pipes. The classic Mac OS has no
// such devices here: mac-sdk's ioctl does only FIONREAD, for files.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_MACOS_IOCTL_H
#define WINPORT_MACOS_IOCTL_H
#include_next <sys/ioctl.h>

#define FIONREAD 0x541B
#define TIOCMGET 0x5415
#define TIOCMBIS 0x5416
#define TIOCMBIC 0x5417
#define TIOCSBRK 0x5427
#define TIOCCBRK 0x5428
#define TIOCM_DTR 0x002
#define TIOCM_RTS 0x004
#define TIOCM_CTS 0x020
#define TIOCM_CD 0x040
#define TIOCM_RI 0x080
#define TIOCM_DSR 0x100

#ifdef __cplusplus
// The two-argument form, for the requests that take no argument.
static inline int ioctl(int fd, unsigned long op) { return ioctl(fd, op, (void*)0); }
#endif

#endif
