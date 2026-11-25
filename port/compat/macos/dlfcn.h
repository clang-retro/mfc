// dlfcn.h - the dynamic loader functions for the classic Mac OS, which
// has no dynamic loader for this layer: a program is one static
// application. dlopen(NULL) gives a handle for the program, and dlsym
// finds nothing in it; every other dlopen fails.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_MACOS_DLFCN_H
#define WINPORT_MACOS_DLFCN_H

#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_NOLOAD 4
#define RTLD_LOCAL 0
#define RTLD_GLOBAL 8
#define RTLD_DEFAULT ((void*)0)

typedef struct {
  const char* dli_fname;
  void* dli_fbase;
  const char* dli_sname;
  void* dli_saddr;
} Dl_info;

#ifdef __cplusplus
extern "C" {
#endif

// Gives a handle for the program itself when path is NULL; no library
// can be loaded.
static inline void* dlopen(const char* path, int mode) {
  static char program;
  (void)mode;
  return path ? (void*)0 : (void*)&program;
}

// Finds no symbol: the program has no table of its symbols.
static inline void* dlsym(void* handle, const char* name) {
  (void)handle;
  (void)name;
  return (void*)0;
}

// Releases a handle: there is nothing to release.
static inline int dlclose(void* handle) {
  (void)handle;
  return 0;
}

// Gives the reason for the last failure.
static inline char* dlerror(void) {
  return (char*)"the classic Mac OS has no dynamic libraries";
}

// Finds no information for an address.
static inline int dladdr(const void* addr, Dl_info* info) {
  (void)addr;
  (void)info;
  return 0;
}

#ifdef __cplusplus
}
#endif

#endif
