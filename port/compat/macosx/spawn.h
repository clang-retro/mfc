// spawn.h - posix_spawnp and the file actions, which Mac OS X has from
// 10.5. The functions are in port/src/compat_macosx.cpp, over fork and
// execvp. Only what the layer uses is here: no attributes, and the file
// actions dup2 and open.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef WINPORT_MACOSX_SPAWN_H
#define WINPORT_MACOSX_SPAWN_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct winport_spawn_action;
typedef struct {
  struct winport_spawn_action* first;
} posix_spawn_file_actions_t;
typedef void* posix_spawnattr_t;

int posix_spawn_file_actions_init(posix_spawn_file_actions_t* fa);
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t* fa);
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t* fa, int fd, int newfd);
int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t* fa, int fd, const char* path, int oflag,
                                     mode_t mode);
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t* fa, int fd);
int posix_spawnp(pid_t* pid, const char* file, const posix_spawn_file_actions_t* fa,
                 const posix_spawnattr_t* attr, char* const argv[], char* const envp[]);
int posix_spawn(pid_t* pid, const char* path, const posix_spawn_file_actions_t* fa,
                const posix_spawnattr_t* attr, char* const argv[], char* const envp[]);

#ifdef __cplusplus
}
#endif

#endif
