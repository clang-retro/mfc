// sys/mman.h - the C library's, with the declarations of POSIX shared
// memory, which picolibc's header does not have. port/src/compat_dos.cpp
// has mmap, shm_open and the rest, over the heap.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_DOS_MMAN_H
#define WINPORT_DOS_MMAN_H
#include_next <sys/mman.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif
int shm_open(const char* name, int oflag, mode_t mode);
int shm_unlink(const char* name);
#ifdef __cplusplus
}
#endif

#endif
