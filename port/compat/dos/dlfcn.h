/* The dynamic loader functions for 32-bit DOS. A program is one
 * executable: dlopen(NULL) gives the program, and other names fail.
 *
 * Part of clang-retro MFC, an independent implementation of the MFC API.
 * SPDX-License-Identifier: AGPL-3.0-only */
#ifndef _DLFCN_H
#define _DLFCN_H

#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_NOLOAD 4
#define RTLD_LOCAL 0
#define RTLD_GLOBAL 8
#define RTLD_DEFAULT ((void *)0)

typedef struct {
  const char *dli_fname;
  void *dli_fbase;
  const char *dli_sname;
  void *dli_saddr;
} Dl_info;

#ifdef __cplusplus
extern "C" {
#endif

void *dlopen(const char *path, int mode);
void *dlsym(void *__restrict handle, const char *__restrict name);
int dlclose(void *handle);
char *dlerror(void);
int dladdr(const void *addr, Dl_info *info);

#ifdef __cplusplus
}
#endif

#endif
