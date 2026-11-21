// winport.h - the portable Win32 layer's internal helpers: errors,
// kernel objects, text conversion and paths.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_INTERNAL_H
#define _WINPORT_INTERNAL_H

#include <windows.h>
#include <pthread.h>
#include <atomic>
#include <string>
#include <vector>

namespace winport {

// Errors: the thread's last error, and errno in Win32 terms.
DWORD ErrorFromErrno(int e);
inline void SetErrorFromErrno() { SetLastError(ErrorFromErrno(errno)); }

// Kernel objects. A HANDLE points at one of these; the magic number
// tells handles from other pointers.
enum ObjectType { kFile = 1, kEvent, kMutex, kSemaphore, kThread, kProcess, kFind, kHeap, kOther, kToken };
struct Object {
  static const DWORD kMagic = 0x6f62706a;  // "jpbo"
  DWORD magic = kMagic;
  ObjectType type;
  std::atomic<int> refs{1};
  bool named = false;  // in the table of named objects
  // The object's security descriptor, self-relative; empty for the
  // default one (its creator as owner, and the creator's default DACL).
  std::vector<unsigned char> security;
  explicit Object(ObjectType t) : type(t) {}
  virtual ~Object() {}
  // Waitable objects: whether a wait would be satisfied now, and taking
  // it (a mutex's ownership, a semaphore's count, an auto event).
  virtual bool IsSignaled(DWORD /*tid*/) { return false; }
  virtual void Acquire(DWORD /*tid*/) {}
  virtual bool Waitable() { return false; }
  // When the object will next signal by itself (a waitable timer), as a
  // FILETIME on the system clock; 0 for never. Called with WaitLock held.
  virtual ULONGLONG Due() { return 0; }
  // An object shared with other processes holds its state still between
  // Lock and Unlock (with WaitLock held); Lock is false for the others.
  virtual bool Lock() { return false; }
  virtual const char* LockKey() { return NULL; }  // the order to lock several in
  virtual void Unlock() {}
  // A mutex the last wait took from an owner that had ended.
  virtual bool TakeAbandoned() { return false; }
};
Object* FromHandle(HANDLE h, ObjectType type = (ObjectType)0);
HANDLE NewHandle(Object* o);
void Release(Object* o);
// A new handle to an open file, for DuplicateHandle.
HANDLE DuplicateFile(Object* o);
// Gives a new object the security descriptor a SECURITY_ATTRIBUTES names.
void SetObjectSecurity(Object* o, const SECURITY_ATTRIBUTES* sa);
// The descriptor of a file handle, or -1.
int FileDescriptor(HANDLE h);
// All waits sleep on one condition, which every change of a waitable
// object's state broadcasts.
pthread_mutex_t* WaitLock();
void SignalChange();  // call with WaitLock held
// Runs the calling thread's queued APCs; whether there were any.
bool DeliverApcs();

// Text: the ANSI code page is UTF-8.
std::wstring Widen(const char* s, int n = -1);
std::string Narrow(const wchar_t* s, int n = -1);
// Copies a result string into a caller's buffer with the Win32
// convention: the length without the zero when it fits, else the size
// needed with the zero.
DWORD CopyOut(const std::string& s, LPSTR buf, DWORD cch);
DWORD CopyOut(const std::wstring& s, LPWSTR buf, DWORD cch);

// Paths: a Windows path ("C:\dir\file.txt", "\dir", "dir\file") becomes
// a POSIX one: backslashes become slashes and a drive letter names the
// root.
std::string PosixPath(const char* path);
std::string PosixPath(const wchar_t* path);

// The layer's configuration directory, where the registry and .ini files
// are: $XDG_CONFIG_HOME/libmfc, or ~/.config/libmfc.
std::string ConfigDir();

// Time: FILETIME counts 100 ns intervals since 1601.
const ULONGLONG kEpochDelta = 116444736000000000ULL;  // 1601 to 1970
inline ULONGLONG ToU64(const FILETIME& ft) { return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime; }
inline FILETIME ToFileTime(ULONGLONG v) {
  FILETIME ft;
  ft.dwLowDateTime = (DWORD)v;
  ft.dwHighDateTime = (DWORD)(v >> 32);
  return ft;
}
FILETIME FileTimeFromUnix(long long sec, long nsec);
void UnixFromFileTime(const FILETIME& ft, long long* sec, long* nsec);

}  // namespace winport

#endif
