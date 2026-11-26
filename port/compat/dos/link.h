// link.h - the list of loaded shared objects. A 32-bit DOS program
// is one static executable, so the list is empty.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_DOS_LINK_H
#define WINPORT_DOS_LINK_H

#include <stddef.h>

struct dl_phdr_info {
  void* dlpi_addr;
  const char* dlpi_name;
};

// Calls nothing: there are no shared objects.
static inline int dl_iterate_phdr(int (*callback)(struct dl_phdr_info*, size_t, void*), void* data) {
  (void)callback;
  (void)data;
  return 0;
}

#endif
