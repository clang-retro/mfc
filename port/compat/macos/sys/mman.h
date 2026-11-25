// sys/mman.h - the C library's, with POSIX shared memory. The classic
// Mac OS has no virtual memory calls for this layer; mmap and the rest
// are in port/src/macos_posix.cpp, over the heap.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_MACOS_MMAN_H
#define WINPORT_MACOS_MMAN_H
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
