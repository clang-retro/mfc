// process.h - the C runtime's threads (_beginthreadex and the rest) and
// process names, over the portable layer's CreateThread.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_PROCESS_H
#define _WINPORT_PROCESS_H

#include <windows.h>
#include <stdint.h>
#include <unistd.h>

namespace winport_process {
struct Start {
  void (*fn)(void*);
  void* arg;
};
inline DWORD WINAPI Run(LPVOID p) {
  Start s = *(Start*)p;
  delete (Start*)p;
  s.fn(s.arg);
  return 0;
}
}  // namespace winport_process

// Returns the thread's handle, or 0; the handle is the caller's to close.
inline uintptr_t _beginthreadex(void* security, unsigned stackSize, unsigned(WINAPI* start)(void*), void* arg,
                                unsigned initFlags, unsigned* threadId) {
  DWORD id = 0;
  HANDLE h = CreateThread((LPSECURITY_ATTRIBUTES)security, stackSize, (LPTHREAD_START_ROUTINE)start, arg,
                          initFlags, &id);
  if (threadId) *threadId = id;
  return (uintptr_t)h;
}
inline void _endthreadex(unsigned code) { ExitThread(code); }

// The handle closes itself when the thread ends; -1 on failure.
inline uintptr_t _beginthread(void (*start)(void*), unsigned stackSize, void* arg) {
  winport_process::Start* s = new winport_process::Start{start, arg};
  HANDLE h = CreateThread(NULL, stackSize, winport_process::Run, s, 0, NULL);
  if (!h) {
    delete s;
    return (uintptr_t)-1;
  }
  CloseHandle(h);
  return (uintptr_t)h;
}
inline void _endthread(void) { ExitThread(0); }

inline int _getpid(void) { return (int)getpid(); }

#endif  // _WINPORT_PROCESS_H
