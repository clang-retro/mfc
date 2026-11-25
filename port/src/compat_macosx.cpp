// compat_macosx.cpp - the C library functions that the layer uses and
// that Mac OS X 10.4 and 10.5 do not have. port/compat/macosx declares
// them. The Makefile builds this file only for those versions (the
// targets powerpc-apple-macosx10.4 and 10.5, and the darwin8 and darwin9
// triples); it is empty elsewhere.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#if defined(__APPLE__) && defined(WINPORT_MACOSX_COMPAT)

#include <errno.h>
#include <fcntl.h>
#include <mach-o/getsect.h>
#include <mach-o/loader.h>
#include <mach/mach_time.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#undef sysconf

extern "C" char** environ;

extern "C" {

/// Returns the length of the string S, but not more than MAX.
size_t strnlen(const char* s, size_t max) {
  size_t n = 0;
  while (n < max && s[n]) n++;
  return n;
}

/// Returns the length of the wide string S, but not more than MAX.
size_t wcsnlen(const wchar_t* s, size_t max) {
  size_t n = 0;
  while (n < max && s[n]) n++;
  return n;
}

/// Compares not more than N wide characters of A and B, with no
/// difference between upper and lower case. Returns less than, equal to
/// or more than 0, as wcscmp does.
int wcsncasecmp(const wchar_t* a, const wchar_t* b, size_t n) {
  for (; n; n--, a++, b++) {
    wint_t x = towlower((wint_t)*a), y = towlower((wint_t)*b);
    if (x != y) return x < y ? -1 : 1;
    if (!x) break;
  }
  return 0;
}

/// Compares the wide strings A and B, with no difference between upper
/// and lower case.
int wcscasecmp(const wchar_t* a, const wchar_t* b) { return wcsncasecmp(a, b, (size_t)-1); }

/// Returns a copy of the wide string S in memory from malloc, or NULL
/// with errno ENOMEM.
wchar_t* wcsdup(const wchar_t* s) {
  size_t n = wcslen(s) + 1;
  wchar_t* d = (wchar_t*)malloc(n * sizeof(wchar_t));
  if (d) memcpy(d, s, n * sizeof(wchar_t));
  return d;
}

/// Reads the clock CLOCK into TS. CLOCK_REALTIME is the time of day from
/// gettimeofday (to the microsecond). CLOCK_MONOTONIC is the time since
/// the start of the system from mach_absolute_time. Another clock gives
/// -1 with errno EINVAL.
int clock_gettime(clockid_t clock, struct timespec* ts) {
  if (clock == CLOCK_REALTIME) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    ts->tv_sec = tv.tv_sec;
    ts->tv_nsec = tv.tv_usec * 1000;
    return 0;
  }
  if (clock == CLOCK_MONOTONIC) {
    static mach_timebase_info_data_t tb;
    if (!tb.denom) mach_timebase_info(&tb);
    unsigned long long t = mach_absolute_time();
    unsigned long long ns = t / tb.denom * tb.numer + t % tb.denom * tb.numer / tb.denom;
    ts->tv_sec = (time_t)(ns / 1000000000ULL);
    ts->tv_nsec = (long)(ns % 1000000000ULL);
    return 0;
  }
  errno = EINVAL;
  return -1;
}

/// Sets the access and modification times of the open file FD from
/// TIMES. A time with tv_nsec UTIME_OMIT stays as it is, and UTIME_NOW
/// is the current time. The times are kept to the microsecond.
int futimens(int fd, const struct timespec times[2]) {
  struct stat st;
  if (fstat(fd, &st) != 0) return -1;
  struct timeval now;
  gettimeofday(&now, NULL);
  struct timeval tv[2];
  const struct timespec old[2] = {st.st_atimespec, st.st_mtimespec};
  for (int i = 0; i < 2; i++) {
    const struct timespec* t = times ? &times[i] : NULL;
    if (!t || t->tv_nsec == UTIME_NOW) {
      tv[i] = now;
    } else if (t->tv_nsec == UTIME_OMIT) {
      tv[i].tv_sec = old[i].tv_sec;
      tv[i].tv_usec = (suseconds_t)(old[i].tv_nsec / 1000);
    } else {
      tv[i].tv_sec = t->tv_sec;
      tv[i].tv_usec = (suseconds_t)(t->tv_nsec / 1000);
    }
  }
  return futimes(fd, tv);
}

/// sysconf, which also counts the processors (with sysctl hw.ncpu) for
/// _SC_NPROCESSORS_ONLN and _SC_NPROCESSORS_CONF.
long winport_sysconf(int name) {
  if (name == _SC_NPROCESSORS_ONLN || name == _SC_NPROCESSORS_CONF) {
    int mib[2] = {CTL_HW, HW_NCPU}, n = 1;
    size_t len = sizeof n;
    if (sysctl(mib, 2, &n, &len, NULL, 0) != 0) return 1;
    return n;
  }
  return sysconf(name);
}

/// Finds the section SECTNAME of the segment SEGNAME in the loaded image
/// whose header is at MH. Returns its address in memory and stores its
/// size in SIZE, or returns NULL. The section's address in the file is
/// moved by the image's slide: the distance between where the image is
/// and where its __TEXT segment asked to be.
uint8_t* getsectiondata(const struct mach_header* mh, const char* segname, const char* sectname,
                        unsigned long* size) {
  const struct section* s = getsectbynamefromheader(mh, segname, sectname);
  if (!s) return NULL;
  const struct segment_command* text = NULL;
  const char* p = (const char*)(mh + 1);
  for (uint32_t i = 0; i < mh->ncmds; i++) {
    const struct load_command* lc = (const struct load_command*)p;
    if (lc->cmd == LC_SEGMENT && !strcmp(((const struct segment_command*)lc)->segname, "__TEXT")) {
      text = (const struct segment_command*)lc;
      break;
    }
    p += lc->cmdsize;
  }
  uintptr_t slide = text ? (uintptr_t)mh - text->vmaddr : 0;
  if (size) *size = s->size;
  return (uint8_t*)(s->addr + slide);
}

}  // extern "C"

// One file action: dup2 (FD to NEWFD), open (PATH on FD) or close (FD).
struct winport_spawn_action {
  enum Kind { kDup2, kOpen, kClose } kind;
  int fd, newfd, oflag;
  mode_t mode;
  char* path;
  winport_spawn_action* next;
};

namespace {

/// Adds the action A at the end of the list of FA. Returns 0, or ENOMEM.
int AddAction(posix_spawn_file_actions_t* fa, const winport_spawn_action& a) {
  winport_spawn_action* n = (winport_spawn_action*)malloc(sizeof *n);
  if (!n) return ENOMEM;
  *n = a;
  n->next = NULL;
  winport_spawn_action** p = &fa->first;
  while (*p) p = &(*p)->next;
  *p = n;
  return 0;
}

/// Starts a child that does the file actions FA and then runs FILE with
/// ARGV and ENVP, through execvp when SEARCH is true (FILE is looked up in
/// PATH) or execve when it is false. Stores the child's ID in PID.
/// Returns 0, or the error number. A child that cannot run FILE ends with
/// status 127, as posix_spawn's children do.
int Spawn(pid_t* pid, const char* file, const posix_spawn_file_actions_t* fa, char* const argv[],
          char* const envp[], bool search) {
  pid_t p = fork();
  if (p < 0) return errno;
  if (p == 0) {
    for (const winport_spawn_action* a = fa ? fa->first : NULL; a; a = a->next) {
      if (a->kind == winport_spawn_action::kDup2) {
        if (dup2(a->fd, a->newfd) < 0) _exit(127);
      } else if (a->kind == winport_spawn_action::kClose) {
        close(a->fd);
      } else {
        int fd = open(a->path, a->oflag, a->mode);
        if (fd < 0) _exit(127);
        if (fd != a->fd) {
          dup2(fd, a->fd);
          close(fd);
        }
      }
    }
    if (search) {
      // execvp uses environ for the child's environment.
      if (envp) environ = (char**)envp;
      execvp(file, argv);
    } else {
      execve(file, argv, envp);
    }
    _exit(127);
  }
  if (pid) *pid = p;
  return 0;
}

}  // namespace

extern "C" {

/// Makes FA an empty list of file actions.
int posix_spawn_file_actions_init(posix_spawn_file_actions_t* fa) {
  fa->first = NULL;
  return 0;
}

/// Frees the file actions of FA.
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t* fa) {
  winport_spawn_action* a = fa->first;
  while (a) {
    winport_spawn_action* n = a->next;
    free(a->path);
    free(a);
    a = n;
  }
  fa->first = NULL;
  return 0;
}

/// Adds the action "dup2(FD, NEWFD)" to FA.
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t* fa, int fd, int newfd) {
  winport_spawn_action a = {winport_spawn_action::kDup2, fd, newfd, 0, 0, NULL, NULL};
  return AddAction(fa, a);
}

/// Adds the action "open PATH with OFLAG and MODE as descriptor FD" to FA.
int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t* fa, int fd, const char* path, int oflag,
                                     mode_t mode) {
  char* copy = strdup(path);
  if (!copy) return ENOMEM;
  winport_spawn_action a = {winport_spawn_action::kOpen, fd, 0, oflag, mode, copy, NULL};
  int r = AddAction(fa, a);
  if (r) free(copy);
  return r;
}

/// Adds the action "close(FD)" to FA.
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t* fa, int fd) {
  winport_spawn_action a = {winport_spawn_action::kClose, fd, 0, 0, 0, NULL, NULL};
  return AddAction(fa, a);
}

/// Runs FILE, looked up in PATH, in a new process (see Spawn). ATTR must
/// be NULL.
int posix_spawnp(pid_t* pid, const char* file, const posix_spawn_file_actions_t* fa,
                 const posix_spawnattr_t* attr, char* const argv[], char* const envp[]) {
  if (attr) return EINVAL;
  return Spawn(pid, file, fa, argv, envp, true);
}

/// Runs the program at PATH in a new process (see Spawn). ATTR must be
/// NULL.
int posix_spawn(pid_t* pid, const char* path, const posix_spawn_file_actions_t* fa,
                const posix_spawnattr_t* attr, char* const argv[], char* const envp[]) {
  if (attr) return EINVAL;
  return Spawn(pid, path, fa, argv, envp, false);
}

}  // extern "C"

#endif
