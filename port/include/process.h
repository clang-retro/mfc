// process.h - the C runtime's threads (_beginthreadex and the rest) and
// process names, over the portable layer's CreateThread.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_PROCESS_H
#define _WINPORT_PROCESS_H

#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

// A function and its argument, for _beginthread's threads.
struct __winport_process_start {
  void (*fn)(void*);
  void* arg;
};
/// Runs the function in the start record P on the new thread, after it
/// frees the record.
__WINPORT_INLINE DWORD WINAPI __winport_process_run(LPVOID p) {
  struct __winport_process_start s = *(struct __winport_process_start*)p;
  free(p);
  s.fn(s.arg);
  return 0;
}

// Returns the thread's handle, or 0; the handle is the caller's to close.
__WINPORT_INLINE uintptr_t _beginthreadex(void* security, unsigned stackSize, unsigned(WINAPI* start)(void*), void* arg,
                                unsigned initFlags, unsigned* threadId) {
  DWORD id = 0;
  HANDLE h = CreateThread((LPSECURITY_ATTRIBUTES)security, stackSize, (LPTHREAD_START_ROUTINE)start, arg,
                          initFlags, &id);
  if (threadId) *threadId = id;
  return (uintptr_t)h;
}
__WINPORT_INLINE void _endthreadex(unsigned code) { ExitThread(code); }

// The handle closes itself when the thread ends; -1 on failure.
__WINPORT_INLINE uintptr_t _beginthread(void (*start)(void*), unsigned stackSize, void* arg) {
  struct __winport_process_start* s =
      (struct __winport_process_start*)malloc(sizeof(struct __winport_process_start));
  if (!s) return (uintptr_t)-1;
  s->fn = start;
  s->arg = arg;
  HANDLE h = CreateThread(NULL, stackSize, __winport_process_run, s, 0, NULL);
  if (!h) {
    free(s);
    return (uintptr_t)-1;
  }
  CloseHandle(h);
  return (uintptr_t)h;
}
__WINPORT_INLINE void _endthread(void) { ExitThread(0); }

__WINPORT_INLINE int _getpid(void) { return (int)getpid(); }

#endif  // _WINPORT_PROCESS_H
