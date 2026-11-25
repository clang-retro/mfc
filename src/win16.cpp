// win16.cpp - the KERNEL32 functions of include/afxwin16.h, for 16-bit
// Windows. They use the C library (picolibc's DOS layer) for files,
// directories and the clock, and the 16-bit KERNEL and USER for the rest.
// The file is empty in the other builds.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "stdafx.h"

#ifdef _AFX_WIN16
#include "afximpl.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

// ---------------------------------------------------------------------
// The last error

static DWORD g_dwLastError;

/// Returns the error code of the last function that failed.
extern "C" DWORD WINAPI _AfxW16GetLastError(void) { return g_dwLastError; }

/// Sets the error code that GetLastError returns.
extern "C" void WINAPI _AfxW16SetLastError(DWORD dwErr) { g_dwLastError = dwErr; }

/// Sets the last error from errno and returns FALSE.
static BOOL FailErrno() {
  DWORD e;
  switch (errno) {
  case ENOENT: e = ERROR_FILE_NOT_FOUND; break;
  case ENOTDIR: e = ERROR_PATH_NOT_FOUND; break;
  case EMFILE: case ENFILE: e = ERROR_TOO_MANY_OPEN_FILES; break;
  case EACCES: case EPERM: case EROFS: e = ERROR_ACCESS_DENIED; break;
  case EBADF: e = ERROR_INVALID_HANDLE; break;
  case ENOMEM: e = ERROR_NOT_ENOUGH_MEMORY; break;
  case EEXIST: e = ERROR_FILE_EXISTS; break;
  case ENOSPC: e = ERROR_DISK_FULL; break;
  case EXDEV: e = ERROR_NOT_SAME_DEVICE; break;
  case ENOTEMPTY: e = ERROR_DIR_NOT_EMPTY; break;
  case EINVAL: e = ERROR_INVALID_PARAMETER; break;
  case ENAMETOOLONG: e = ERROR_FILENAME_EXCED_RANGE; break;
  case EIO: e = ERROR_GEN_FAILURE; break;
  default: e = ERROR_INVALID_FUNCTION; break;
  }
  g_dwLastError = e;
  return FALSE;
}

/// Sets the last error to e and returns FALSE.
static BOOL Fail(DWORD e) {
  g_dwLastError = e;
  return FALSE;
}

// ---------------------------------------------------------------------
// Time: FILETIME counts 100 ns units from 1 January 1601

static const LONGLONG k1601To1970 = 11644473600LL;  // seconds
static const LONGLONG kTicksPerSecond = 10000000LL;

/// Converts a FILETIME to a 64-bit count.
static LONGLONG FtToLL(const FILETIME FAR* ft) {
  return (LONGLONG)(((ULONGLONG)ft->dwHighDateTime << 32) | ft->dwLowDateTime);
}

/// Converts a 64-bit count to a FILETIME.
static void LLToFt(LONGLONG v, FILETIME FAR* ft) {
  ft->dwLowDateTime = (DWORD)v;
  ft->dwHighDateTime = (DWORD)((ULONGLONG)v >> 32);
}

/// Converts a time_t (UTC seconds since 1970) to a FILETIME.
static void TimeToFt(time_t t, FILETIME FAR* ft) {
  LLToFt(((LONGLONG)t + k1601To1970) * kTicksPerSecond, ft);
}

/// Converts a FILETIME to a time_t (seconds since 1970).
static time_t FtToTime(const FILETIME FAR* ft) {
  return (time_t)(FtToLL(ft) / kTicksPerSecond - k1601To1970);
}

/// Returns the seconds to add to UTC to get the local time at time t.
static long LocalOffset(time_t t) {
  struct tm lt, ut;
  if (localtime_r(&t, &lt) == NULL || gmtime_r(&t, &ut) == NULL) return 0;
  lt.tm_isdst = 0;
  return (long)(timegm(&lt) - timegm(&ut));
}

/// Fills a SYSTEMTIME from a struct tm and milliseconds.
static void TmToSt(const struct tm& t, WORD wMs, LPSYSTEMTIME st) {
  st->wYear = (WORD)(t.tm_year + 1900);
  st->wMonth = (WORD)(t.tm_mon + 1);
  st->wDayOfWeek = (WORD)t.tm_wday;
  st->wDay = (WORD)t.tm_mday;
  st->wHour = (WORD)t.tm_hour;
  st->wMinute = (WORD)t.tm_min;
  st->wSecond = (WORD)t.tm_sec;
  st->wMilliseconds = wMs;
}

/// Gives the current UTC time.
extern "C" void WINAPI _AfxW16GetSystemTime(LPSYSTEMTIME lpSystemTime) {
  time_t t = time(NULL);
  struct tm u;
  gmtime_r(&t, &u);
  TmToSt(u, 0, lpSystemTime);
}

/// Gives the current local time.
extern "C" void WINAPI _AfxW16GetLocalTime(LPSYSTEMTIME lpSystemTime) {
  time_t t = time(NULL);
  struct tm l;
  localtime_r(&t, &l);
  TmToSt(l, 0, lpSystemTime);
}

/// Gives the current UTC time as a FILETIME.
extern "C" void WINAPI _AfxW16GetSystemTimeAsFileTime(LPFILETIME lpFileTime) {
  TimeToFt(time(NULL), lpFileTime);
}

/// Converts a FILETIME to a SYSTEMTIME, without a change of time zone.
extern "C" BOOL WINAPI _AfxW16FileTimeToSystemTime(const FILETIME FAR* lpFileTime,
                                                   LPSYSTEMTIME lpSystemTime) {
  LONGLONG v = FtToLL(lpFileTime);
  if (v < 0) return Fail(ERROR_INVALID_PARAMETER);
  time_t t = (time_t)(v / kTicksPerSecond - k1601To1970);
  struct tm u;
  if (gmtime_r(&t, &u) == NULL) return Fail(ERROR_INVALID_PARAMETER);
  TmToSt(u, (WORD)((v / 10000) % 1000), lpSystemTime);
  return TRUE;
}

/// Converts a SYSTEMTIME to a FILETIME, without a change of time zone.
extern "C" BOOL WINAPI _AfxW16SystemTimeToFileTime(const SYSTEMTIME FAR* lpSystemTime,
                                                   LPFILETIME lpFileTime) {
  const SYSTEMTIME FAR* st = lpSystemTime;
  if (st->wMonth < 1 || st->wMonth > 12 || st->wDay < 1 || st->wDay > 31 ||
      st->wHour > 23 || st->wMinute > 59 || st->wSecond > 59 || st->wMilliseconds > 999)
    return Fail(ERROR_INVALID_PARAMETER);
  struct tm t;
  memset(&t, 0, sizeof t);
  t.tm_year = st->wYear - 1900;
  t.tm_mon = st->wMonth - 1;
  t.tm_mday = st->wDay;
  t.tm_hour = st->wHour;
  t.tm_min = st->wMinute;
  t.tm_sec = st->wSecond;
  LONGLONG secs = (LONGLONG)timegm(&t) + k1601To1970;
  LLToFt(secs * kTicksPerSecond + (LONGLONG)st->wMilliseconds * 10000, lpFileTime);
  return TRUE;
}

/// Converts a UTC FILETIME to local time.
extern "C" BOOL WINAPI _AfxW16FileTimeToLocalFileTime(const FILETIME FAR* lpFileTime,
                                                      LPFILETIME lpLocalFileTime) {
  LONGLONG v = FtToLL(lpFileTime);
  LLToFt(v + (LONGLONG)LocalOffset(FtToTime(lpFileTime)) * kTicksPerSecond, lpLocalFileTime);
  return TRUE;
}

/// Converts a local FILETIME to UTC.
extern "C" BOOL WINAPI _AfxW16LocalFileTimeToFileTime(const FILETIME FAR* lpLocalFileTime,
                                                      LPFILETIME lpFileTime) {
  LONGLONG v = FtToLL(lpLocalFileTime);
  LLToFt(v - (LONGLONG)LocalOffset(FtToTime(lpLocalFileTime)) * kTicksPerSecond, lpFileTime);
  return TRUE;
}

/// Gives the time zone from the C library (the TZ variable): only the
/// bias, without daylight saving dates.
extern "C" DWORD WINAPI _AfxW16GetTimeZoneInformation(LPTIME_ZONE_INFORMATION lpTimeZone) {
  memset(lpTimeZone, 0, sizeof *lpTimeZone);
  lpTimeZone->Bias = -LocalOffset(time(NULL)) / 60;
  return TIME_ZONE_ID_UNKNOWN;
}

/// Converts a FILETIME (local time) to a DOS date and time.
extern "C" BOOL WINAPI _AfxW16FileTimeToDosDateTime(const FILETIME FAR* lpFileTime,
                                                    LPWORD lpDate, LPWORD lpTime) {
  SYSTEMTIME st;
  if (!_AfxW16FileTimeToSystemTime(lpFileTime, &st)) return FALSE;
  if (st.wYear < 1980 || st.wYear > 2107) return Fail(ERROR_INVALID_PARAMETER);
  *lpDate = (WORD)(((st.wYear - 1980) << 9) | (st.wMonth << 5) | st.wDay);
  *lpTime = (WORD)((st.wHour << 11) | (st.wMinute << 5) | (st.wSecond / 2));
  return TRUE;
}

/// Converts a DOS date and time to a FILETIME (local time).
extern "C" BOOL WINAPI _AfxW16DosDateTimeToFileTime(WORD wDate, WORD wTime,
                                                    LPFILETIME lpFileTime) {
  SYSTEMTIME st;
  st.wYear = (WORD)((wDate >> 9) + 1980);
  st.wMonth = (WORD)((wDate >> 5) & 15);
  st.wDay = (WORD)(wDate & 31);
  st.wHour = (WORD)(wTime >> 11);
  st.wMinute = (WORD)((wTime >> 5) & 63);
  st.wSecond = (WORD)((wTime & 31) * 2);
  st.wMilliseconds = 0;
  st.wDayOfWeek = 0;
  return _AfxW16SystemTimeToFileTime(&st, lpFileTime);
}

// ---------------------------------------------------------------------
// Files: a file handle is the C library's file descriptor

/// Returns the file descriptor in a file handle.
static int Fd(HANDLE h) { return (int)(UINT)(DWORD_PTR)h; }

/// Returns the file handle for a file descriptor.
static HANDLE FromFd(int fd) { return (HANDLE)(UINT)fd; }

/// Opens or creates a file, as Win32 CreateFile does.
extern "C" HANDLE WINAPI _AfxW16CreateFile(LPCSTR lpFileName, DWORD dwAccess, DWORD dwShare,
                                           LPSECURITY_ATTRIBUTES, DWORD dwCreation,
                                           DWORD dwFlags, HANDLE) {
  (void)dwShare;
  (void)dwFlags;
  int flags;
  if ((dwAccess & GENERIC_READ) && (dwAccess & GENERIC_WRITE))
    flags = O_RDWR;
  else if (dwAccess & GENERIC_WRITE)
    flags = O_WRONLY;
  else
    flags = O_RDONLY;
  switch (dwCreation) {
  case CREATE_NEW: flags |= O_CREAT | O_EXCL; break;
  case CREATE_ALWAYS: flags |= O_CREAT | O_TRUNC; break;
  case OPEN_EXISTING: break;
  case OPEN_ALWAYS: flags |= O_CREAT; break;
  case TRUNCATE_EXISTING: flags |= O_TRUNC; break;
  default: Fail(ERROR_INVALID_PARAMETER); return INVALID_HANDLE_VALUE;
  }
  // The file names are in the ANSI code page; DOS takes them in OEM.
  char szOem[MAX_PATH];
  lstrcpyn(szOem, lpFileName, sizeof szOem);
  AnsiToOem(szOem, szOem);
  int fd = open(szOem, flags | O_BINARY, 0666);
  if (fd < 0) {
    FailErrno();
    if (dwCreation == CREATE_NEW && g_dwLastError == ERROR_FILE_EXISTS)
      g_dwLastError = ERROR_FILE_EXISTS;
    return INVALID_HANDLE_VALUE;
  }
  if (dwCreation == CREATE_ALWAYS || dwCreation == OPEN_ALWAYS)
    g_dwLastError = 0;
  return FromFd(fd);
}

/// Reads up to nToRead bytes from a file.
extern "C" BOOL WINAPI _AfxW16ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nToRead,
                                       LPDWORD lpRead, LPOVERLAPPED) {
  // read takes a 16-bit count: read in pieces of up to 32 KiB.
  BYTE FAR* p = (BYTE FAR*)lpBuffer;
  DWORD nDone = 0;
  while (nDone < nToRead) {
    DWORD nLeft = nToRead - nDone;
    unsigned n = nLeft > 0x8000UL ? 0x8000U : (unsigned)nLeft;
    // A piece must not cross a segment boundary.
    unsigned nSeg = (unsigned)(0x10000UL - (WORD)(DWORD_PTR)(p + nDone));
    if (nSeg != 0 && n > nSeg) n = nSeg;
    int r = read(Fd(hFile), p + nDone, n);
    if (r < 0) {
      if (lpRead) *lpRead = nDone;
      return FailErrno();
    }
    nDone += (unsigned)r;
    if ((unsigned)r < n) break;
  }
  if (lpRead) *lpRead = nDone;
  return TRUE;
}

/// Writes nToWrite bytes to a file.
extern "C" BOOL WINAPI _AfxW16WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nToWrite,
                                        LPDWORD lpWritten, LPOVERLAPPED) {
  const BYTE FAR* p = (const BYTE FAR*)lpBuffer;
  DWORD nDone = 0;
  while (nDone < nToWrite) {
    DWORD nLeft = nToWrite - nDone;
    unsigned n = nLeft > 0x8000UL ? 0x8000U : (unsigned)nLeft;
    unsigned nSeg = (unsigned)(0x10000UL - (WORD)(DWORD_PTR)(p + nDone));
    if (nSeg != 0 && n > nSeg) n = nSeg;
    int r = write(Fd(hFile), p + nDone, n);
    if (r < 0) {
      if (lpWritten) *lpWritten = nDone;
      return FailErrno();
    }
    nDone += (unsigned)r;
    if ((unsigned)r < n) {
      if (lpWritten) *lpWritten = nDone;
      return Fail(ERROR_DISK_FULL);
    }
  }
  if (lpWritten) *lpWritten = nDone;
  return TRUE;
}

/// Moves the file pointer. Files are smaller than 2 GiB on DOS, so the
/// high part of the distance is only its sign.
extern "C" DWORD WINAPI _AfxW16SetFilePointer(HANDLE hFile, LONG lDistance,
                                              LONG FAR* lpDistanceHigh, DWORD dwMethod) {
  if (lpDistanceHigh != NULL && *lpDistanceHigh != (lDistance < 0 ? -1L : 0L)) {
    Fail(ERROR_INVALID_PARAMETER);
    return INVALID_SET_FILE_POINTER;
  }
  int whence = dwMethod == FILE_CURRENT ? SEEK_CUR : dwMethod == FILE_END ? SEEK_END : SEEK_SET;
  off_t r = lseek(Fd(hFile), (off_t)lDistance, whence);
  if (r < 0) {
    FailErrno();
    if (g_dwLastError == ERROR_INVALID_PARAMETER) g_dwLastError = ERROR_NEGATIVE_SEEK;
    return INVALID_SET_FILE_POINTER;
  }
  if (lpDistanceHigh != NULL) *lpDistanceHigh = 0;
  g_dwLastError = 0;
  return (DWORD)r;
}

/// Moves the file pointer, with a 64-bit distance.
extern "C" BOOL WINAPI _AfxW16SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistance,
                                               PLARGE_INTEGER lpNewPointer, DWORD dwMethod) {
  LONG lHigh = liDistance.HighPart;
  DWORD r = _AfxW16SetFilePointer(hFile, (LONG)liDistance.LowPart, &lHigh, dwMethod);
  if (r == INVALID_SET_FILE_POINTER && g_dwLastError != 0) return FALSE;
  if (lpNewPointer != NULL) lpNewPointer->QuadPart = r;
  return TRUE;
}

/// Makes the end of the file the current position.
extern "C" BOOL WINAPI _AfxW16SetEndOfFile(HANDLE hFile) {
  off_t pos = lseek(Fd(hFile), 0, SEEK_CUR);
  if (pos < 0 || ftruncate(Fd(hFile), pos) < 0) return FailErrno();
  return TRUE;
}

/// Returns the size of a file.
extern "C" DWORD WINAPI _AfxW16GetFileSize(HANDLE hFile, LPDWORD lpSizeHigh) {
  struct stat st;
  if (fstat(Fd(hFile), &st) < 0) {
    FailErrno();
    return INVALID_FILE_SIZE;
  }
  if (lpSizeHigh != NULL) *lpSizeHigh = 0;
  g_dwLastError = 0;
  return (DWORD)st.st_size;
}

/// Gives the size of a file, as a 64-bit value.
extern "C" BOOL WINAPI _AfxW16GetFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpSize) {
  DWORD n = _AfxW16GetFileSize(hFile, NULL);
  if (n == INVALID_FILE_SIZE && g_dwLastError != 0) return FALSE;
  lpSize->QuadPart = n;
  return TRUE;
}

/// Writes the buffers of the file to the disk.
extern "C" BOOL WINAPI _AfxW16FlushFileBuffers(HANDLE hFile) {
  // DOS writes to the disk when a duplicate of the handle closes.
  int fd = dup(Fd(hFile));
  if (fd < 0) return FailErrno();
  close(fd);
  return TRUE;
}

static BOOL FindCloseSlot(HANDLE hFind);

/// Closes a file handle, a find handle, or the handle of an event, a
/// mutex or a semaphore.
extern "C" BOOL WINAPI _AfxW16CloseHandle(HANDLE hObject) {
  if (FindCloseSlot(hObject)) return TRUE;
  if (_AfxW16CloseSyncHandle(hObject)) return TRUE;
  if (close(Fd(hObject)) < 0) return FailErrno();
  return TRUE;
}

/// Locks a range of a file. Without SHARE, DOS has no locks; the call
/// then does nothing.
extern "C" BOOL WINAPI _AfxW16LockFile(HANDLE, DWORD, DWORD, DWORD, DWORD) { return TRUE; }

/// Unlocks a range of a file (see LockFile).
extern "C" BOOL WINAPI _AfxW16UnlockFile(HANDLE, DWORD, DWORD, DWORD, DWORD) { return TRUE; }

/// Returns the handle of the current task, for DuplicateHandle.
extern "C" HANDLE WINAPI _AfxW16GetCurrentProcess(void) { return (HANDLE)(UINT)-1; }

/// Duplicates a file handle.
extern "C" BOOL WINAPI _AfxW16DuplicateHandle(HANDLE, HANDLE hSource, HANDLE, LPHANDLE lpTarget,
                                              DWORD, BOOL, DWORD) {
  int fd = dup(Fd(hSource));
  if (fd < 0) return FailErrno();
  *lpTarget = FromFd(fd);
  return TRUE;
}

/// Gives the times of a file. DOS keeps only the time of the last write.
extern "C" BOOL WINAPI _AfxW16GetFileTime(HANDLE hFile, LPFILETIME lpCreation,
                                          LPFILETIME lpAccess, LPFILETIME lpWrite) {
  struct stat st;
  if (fstat(Fd(hFile), &st) < 0) return FailErrno();
  if (lpCreation) TimeToFt(st.st_mtime, lpCreation);
  if (lpAccess) TimeToFt(st.st_mtime, lpAccess);
  if (lpWrite) TimeToFt(st.st_mtime, lpWrite);
  return TRUE;
}

/// Sets the times of an open file. DOS keeps only the time of the last
/// write; the other times are ignored.
extern "C" BOOL WINAPI _AfxW16SetFileTime(HANDLE hFile, const FILETIME FAR*,
                                          const FILETIME FAR*, const FILETIME FAR* lpWrite) {
  if (lpWrite == NULL) return TRUE;
  // DOS function 5701h: set the date and time of an open file.
  FILETIME ftLocal;
  WORD wDate, wTime;
  _AfxW16FileTimeToLocalFileTime(lpWrite, &ftLocal);
  if (!_AfxW16FileTimeToDosDateTime(&ftLocal, &wDate, &wTime)) return FALSE;
  // AX is 0 after the call when the carry flag is clear, or -1 when set.
  unsigned r;
  int fd = Fd(hFile);
  __asm__ volatile("int $0x21\n\tsbb %%ax, %%ax"
                   : "=a"(r)
                   : "a"(0x5701), "b"(fd), "c"(wTime), "d"(wDate)
                   : "cc", "memory");
  if (r != 0) return Fail(ERROR_INVALID_HANDLE);
  return TRUE;
}

/// Converts the mode of a stat result to file attributes.
static DWORD Attributes(const struct stat& st) {
  DWORD a = 0;
  if (S_ISDIR(st.st_mode)) a |= FILE_ATTRIBUTE_DIRECTORY;
  if (!(st.st_mode & S_IWUSR)) a |= FILE_ATTRIBUTE_READONLY;
  return a != 0 ? a : FILE_ATTRIBUTE_NORMAL;
}

/// Converts an ANSI file name to OEM for DOS.
static void ToOem(LPCSTR lpszIn, char* szOut, int nOut) {
  lstrcpyn(szOut, lpszIn, nOut);
  AnsiToOem(szOut, szOut);
}

/// Returns the attributes of a file or directory.
extern "C" DWORD WINAPI _AfxW16GetFileAttributes(LPCSTR lpFileName) {
  char sz[MAX_PATH];
  ToOem(lpFileName, sz, sizeof sz);
  struct stat st;
  if (stat(sz, &st) < 0) {
    FailErrno();
    return INVALID_FILE_ATTRIBUTES;
  }
  return Attributes(st);
}

/// Sets the read-only attribute of a file. DOS keeps the others only in
/// its own calls; they are ignored.
extern "C" BOOL WINAPI _AfxW16SetFileAttributes(LPCSTR lpFileName, DWORD dwAttributes) {
  char sz[MAX_PATH];
  ToOem(lpFileName, sz, sizeof sz);
  mode_t mode = (dwAttributes & FILE_ATTRIBUTE_READONLY) ? 0444 : 0666;
  if (chmod(sz, mode) < 0) return FailErrno();
  return TRUE;
}

/// Deletes a file.
extern "C" BOOL WINAPI _AfxW16DeleteFile(LPCSTR lpFileName) {
  char sz[MAX_PATH];
  ToOem(lpFileName, sz, sizeof sz);
  if (unlink(sz) < 0) return FailErrno();
  return TRUE;
}

/// Renames a file. The new name must not exist.
extern "C" BOOL WINAPI _AfxW16MoveFile(LPCSTR lpExisting, LPCSTR lpNew) {
  char sz1[MAX_PATH], sz2[MAX_PATH];
  ToOem(lpExisting, sz1, sizeof sz1);
  ToOem(lpNew, sz2, sizeof sz2);
  if (access(sz2, F_OK) == 0) return Fail(ERROR_ALREADY_EXISTS);
  if (rename(sz1, sz2) < 0) return FailErrno();
  return TRUE;
}

/// Gives the current directory.
extern "C" DWORD WINAPI _AfxW16GetCurrentDirectory(DWORD nBufferLength, LPSTR lpBuffer) {
  char sz[MAX_PATH];
  if (getcwd(sz, sizeof sz) == NULL) {
    FailErrno();
    return 0;
  }
  OemToAnsi(sz, sz);
  DWORD n = (DWORD)strlen(sz);
  if (n + 1 > nBufferLength) return n + 1;
  memcpy(lpBuffer, sz, n + 1);
  return n;
}

/// Makes a full path from a file name: the drive, the directory from the
/// root, and the name, with "." and ".." resolved.
extern "C" DWORD WINAPI _AfxW16GetFullPathName(LPCSTR lpFileName, DWORD nBufferLength,
                                               LPSTR lpBuffer, LPSTR FAR* lpFilePart) {
  char cwd[MAX_PATH], in[MAX_PATH * 2], out[MAX_PATH * 2];
  lstrcpyn(in, lpFileName, MAX_PATH);
  for (char* p = in; *p; p++)
    if (*p == '/') *p = '\\';
  // The drive, and where the path starts.
  char drive[3] = {0, ':', 0};
  const char* rest = in;
  if (in[0] && in[1] == ':') {
    drive[0] = in[0];
    rest = in + 2;
  }
  if (_AfxW16GetCurrentDirectory(sizeof cwd, cwd) == 0) return 0;
  if (drive[0] == 0) drive[0] = cwd[0];
  if (*rest == '\\') {
    lstrcpy(out, drive);
  } else if ((drive[0] | 0x20) == (cwd[0] | 0x20)) {
    lstrcpy(out, cwd);
  } else {
    // Another drive: its current directory is not known; use its root.
    lstrcpy(out, drive);
  }
  // Add each part of the path, resolving "." and "..".
  const char* p = rest;
  while (*p) {
    while (*p == '\\') p++;
    if (!*p) break;
    const char* e = p;
    while (*e && *e != '\\') e++;
    size_t n = (size_t)(e - p);
    if (n == 1 && p[0] == '.') {
      // the same directory
    } else if (n == 2 && p[0] == '.' && p[1] == '.') {
      char* s = strrchr(out, '\\');
      if (s != NULL && s > out + 2) *s = 0;
      else out[2] = 0;
    } else {
      size_t len = strlen(out);
      if (len + 1 + n >= sizeof out) return Fail(ERROR_FILENAME_EXCED_RANGE);
      if (len == 0 || out[len - 1] != '\\') out[len++] = '\\';
      memcpy(out + len, p, n);
      out[len + n] = 0;
    }
    p = e;
  }
  if (out[2] == 0) lstrcat(out, "\\");
  DWORD len = (DWORD)strlen(out);
  if (len + 1 > nBufferLength) return len + 1;
  memcpy(lpBuffer, out, len + 1);
  if (lpFilePart != NULL) {
    char FAR* s = _fstrrchr(lpBuffer, '\\');
    *lpFilePart = s != NULL && s[1] ? s + 1 : NULL;
  }
  return len;
}

/// Gives the directory for temporary files: TEMP, TMP, or the root of the
/// current drive, with a backslash at the end.
extern "C" DWORD WINAPI _AfxW16GetTempPath(DWORD nBufferLength, LPSTR lpBuffer) {
  char sz[MAX_PATH];
  const char* env = getenv("TEMP");
  if (env == NULL || !*env) env = getenv("TMP");
  if (env != NULL && *env) {
    lstrcpyn(sz, env, MAX_PATH - 1);
  } else {
    char cwd[MAX_PATH];
    if (_AfxW16GetCurrentDirectory(sizeof cwd, cwd) == 0) return 0;
    sz[0] = cwd[0];
    sz[1] = ':';
    sz[2] = 0;
  }
  size_t n = strlen(sz);
  if (n == 0 || sz[n - 1] != '\\') {
    sz[n++] = '\\';
    sz[n] = 0;
  }
  if (n + 1 > nBufferLength) return (DWORD)n + 1;
  memcpy(lpBuffer, sz, n + 1);
  return (DWORD)n;
}

/// Makes a temporary file name in lpPath: up to 3 characters of the
/// prefix, 4 hex digits and ".TMP". With uUnique 0 the file is created.
extern "C" UINT WINAPI _AfxW16GetTempFileName(LPCSTR lpPath, LPCSTR lpPrefix, UINT uUnique,
                                              LPSTR lpTempFileName) {
  char pre[4] = {0};
  if (lpPrefix != NULL) lstrcpyn(pre, lpPrefix, sizeof pre);
  UINT n = uUnique != 0 ? uUnique : (UINT)GetTickCount();
  for (UINT nTry = 0; nTry < 0xFFFF; nTry++, n++) {
    if (n == 0) n = 1;
    char name[MAX_PATH];
    size_t len = strlen(lpPath);
    lstrcpyn(name, lpPath, MAX_PATH - 13);
    if (len > 0 && name[len - 1] != '\\' && name[len - 1] != '/') lstrcat(name, "\\");
    wsprintf(name + strlen(name), "%s%04X.TMP", (LPSTR)pre, n);
    if (uUnique != 0) {
      lstrcpy(lpTempFileName, name);
      return uUnique;
    }
    HANDLE h = _AfxW16CreateFile(name, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
      _AfxW16CloseHandle(h);
      lstrcpy(lpTempFileName, name);
      return n;
    }
    if (g_dwLastError != ERROR_FILE_EXISTS) return 0;
  }
  return 0;
}

// ---------------------------------------------------------------------
// Finding files

namespace {
// An open search: the directory and the pattern of the names.
struct FindSlot {
  DIR* pDir;
  char szDir[MAX_PATH];
  char szPattern[13];
};
const int kFindSlots = 8;
const UINT kFindBase = 0xFF00;  // the handles are kFindBase + slot
FindSlot g_find[kFindSlots];
}  // namespace

/// Returns whether name matches a DOS pattern with * and ?, case blind.
static bool Match(const char* pat, const char* name) {
  for (;;) {
    char p = *pat, c = *name;
    if (p == '*') {
      pat++;
      // "*.*" also matches names without a dot, as on DOS.
      if (pat[0] == '.' && pat[1] == '*' && pat[2] == 0) return true;
      for (;; name++) {
        if (Match(pat, name)) return true;
        if (!*name) return false;
      }
    }
    if (p == 0) return c == 0;
    if (c == 0) return p == '.' && Match(pat + 1, name);
    if (p != '?' && (p | 0x20) != (c | 0x20)) return false;
    pat++;
    name++;
  }
}

/// Fills lpFindData for the next entry of a search that matches, or
/// returns FALSE with ERROR_NO_MORE_FILES.
static BOOL FindNext(FindSlot& f, LPWIN32_FIND_DATA lpFindData) {
  for (;;) {
    struct dirent* d = readdir(f.pDir);
    if (d == NULL) return Fail(ERROR_NO_MORE_FILES);
    if (!Match(f.szPattern, d->d_name)) continue;
    char path[MAX_PATH * 2];
    lstrcpy(path, f.szDir);
    lstrcat(path, d->d_name);
    struct stat st;
    memset(lpFindData, 0, sizeof *lpFindData);
    if (stat(path, &st) == 0) {
      lpFindData->dwFileAttributes = Attributes(st);
      lpFindData->nFileSizeLow = (DWORD)st.st_size;
      TimeToFt(st.st_mtime, &lpFindData->ftLastWriteTime);
      lpFindData->ftCreationTime = lpFindData->ftLastWriteTime;
      lpFindData->ftLastAccessTime = lpFindData->ftLastWriteTime;
    } else {
      lpFindData->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    }
    lstrcpyn(lpFindData->cFileName, d->d_name, MAX_PATH);
    OemToAnsi(lpFindData->cFileName, lpFindData->cFileName);
    lstrcpyn(lpFindData->cAlternateFileName, lpFindData->cFileName, 14);
    return TRUE;
  }
}

/// Starts a search for the files that match a path with wildcards.
extern "C" HANDLE WINAPI _AfxW16FindFirstFile(LPCSTR lpFileName,
                                              LPWIN32_FIND_DATA lpFindData) {
  int i = 0;
  while (i < kFindSlots && g_find[i].pDir != NULL) i++;
  if (i == kFindSlots) {
    Fail(ERROR_TOO_MANY_OPEN_FILES);
    return INVALID_HANDLE_VALUE;
  }
  FindSlot& f = g_find[i];
  char sz[MAX_PATH];
  ToOem(lpFileName, sz, sizeof sz);
  for (char* p = sz; *p; p++)
    if (*p == '/') *p = '\\';
  char* name = strrchr(sz, '\\');
  if (name == NULL) name = sz[0] && sz[1] == ':' ? sz + 1 : NULL;
  if (name != NULL) {
    lstrcpyn(f.szPattern, name + 1, sizeof f.szPattern);
    name[1] = 0;
    lstrcpy(f.szDir, sz);
  } else {
    lstrcpyn(f.szPattern, sz, sizeof f.szPattern);
    f.szDir[0] = 0;
  }
  f.pDir = opendir(f.szDir[0] ? f.szDir : ".");
  if (f.pDir == NULL) {
    FailErrno();
    if (g_dwLastError == ERROR_FILE_NOT_FOUND) g_dwLastError = ERROR_PATH_NOT_FOUND;
    return INVALID_HANDLE_VALUE;
  }
  if (!FindNext(f, lpFindData)) {
    closedir(f.pDir);
    f.pDir = NULL;
    g_dwLastError = ERROR_FILE_NOT_FOUND;
    return INVALID_HANDLE_VALUE;
  }
  return (HANDLE)(kFindBase + i);
}

/// Returns the search of a find handle, or NULL.
static FindSlot* Slot(HANDLE hFind) {
  UINT i = (UINT)(DWORD_PTR)hFind - kFindBase;
  if (i >= (UINT)kFindSlots || g_find[i].pDir == NULL) return NULL;
  return &g_find[i];
}

/// Gives the next file of a search.
extern "C" BOOL WINAPI _AfxW16FindNextFile(HANDLE hFind, LPWIN32_FIND_DATA lpFindData) {
  FindSlot* f = Slot(hFind);
  if (f == NULL) return Fail(ERROR_INVALID_HANDLE);
  return FindNext(*f, lpFindData);
}

/// Ends a search; returns FALSE if hFind is not a find handle.
static BOOL FindCloseSlot(HANDLE hFind) {
  FindSlot* f = Slot(hFind);
  if (f == NULL) return FALSE;
  closedir(f->pDir);
  f->pDir = NULL;
  return TRUE;
}

/// Ends a search.
extern "C" BOOL WINAPI _AfxW16FindClose(HANDLE hFind) {
  if (!FindCloseSlot(hFind)) return Fail(ERROR_INVALID_HANDLE);
  return TRUE;
}

// ---------------------------------------------------------------------
// The environment, messages and text

/// Gives the value of an environment variable.
extern "C" DWORD WINAPI _AfxW16GetEnvironmentVariable(LPCSTR lpName, LPSTR lpBuffer,
                                                      DWORD nSize) {
  char szName[128];
  lstrcpyn(szName, lpName, sizeof szName);
  const char* v = getenv(szName);
  if (v == NULL) {
    Fail(ERROR_NOT_FOUND);
    return 0;
  }
  DWORD n = (DWORD)strlen(v);
  if (n + 1 > nSize) return n + 1;
  memcpy(lpBuffer, v, n + 1);
  return n;
}

/// Formats a message string with %1 to %99 inserts (each with an
/// optional !printf format!), as Win32 FormatMessage does. Only
/// FORMAT_MESSAGE_FROM_STRING is available: 16-bit Windows has no message
/// tables. With FORMAT_MESSAGE_ALLOCATE_BUFFER the result is in
/// LocalAlloc memory, which LocalFree frees.
extern "C" DWORD WINAPI _AfxW16FormatMessage(DWORD dwFlags, LPCVOID lpSource, DWORD,
                                             DWORD, LPSTR lpBuffer, DWORD nSize,
                                             va_list FAR* Arguments) {
  if (!(dwFlags & FORMAT_MESSAGE_FROM_STRING) || lpSource == NULL) {
    Fail(ERROR_NOT_SUPPORTED);
    return 0;
  }
  // The arguments, read once each, in order of their number.
  const int kMaxArgs = 99;
  CStringA out;
  LPCSTR p = (LPCSTR)lpSource;
  DWORD_PTR args[kMaxArgs];
  int nArgs = 0;
  bool bIgnore = (dwFlags & FORMAT_MESSAGE_IGNORE_INSERTS) != 0 || Arguments == NULL;
  bool bArray = (dwFlags & FORMAT_MESSAGE_ARGUMENT_ARRAY) != 0;
  while (*p) {
    if (*p != '%') {
      out += *p++;
      continue;
    }
    p++;
    if (*p >= '1' && *p <= '9') {
      int n = *p++ - '0';
      if (*p >= '0' && *p <= '9') n = n * 10 + (*p++ - '0');
      char fmt[16] = "%s";
      if (*p == '!') {
        const char* e = strchr(p + 1, '!');
        if (e != NULL && e - p < (int)sizeof fmt - 1) {
          fmt[0] = '%';
          memcpy(fmt + 1, p + 1, (size_t)(e - p - 1));
          fmt[e - p] = 0;
          p = e + 1;
        }
      }
      if (bIgnore) {
        char sz[8];
        wsprintf(sz, "%%%d", n);
        out += sz;
        continue;
      }
      // The arguments are 32-bit values or far pointers, as the format
      // says; read up to argument n.
      while (nArgs < n) {
        if (bArray)
          args[nArgs] = ((DWORD_PTR FAR*)*Arguments)[nArgs];
        else
          args[nArgs] = va_arg(*Arguments, DWORD_PTR);
        nArgs++;
      }
      CStringA s;
      size_t flen = strlen(fmt);
      char conv = fmt[flen - 1];
      if (conv == 's')
        s.Format(fmt, (LPCSTR)args[n - 1]);
      else if (conv == 'c')
        s.Format(fmt, (int)args[n - 1]);
      else if (strchr(fmt, 'l') != NULL)
        s.Format(fmt, (long)args[n - 1]);
      else
        s.Format(fmt, (int)args[n - 1]);
      out += s;
    } else if (*p == 'n') {
      out += "\r\n";
      p++;
    } else if (*p == '0') {
      break;
    } else if (*p) {
      out += *p++;  // %% %. %! and the like
    }
  }
  DWORD len = (DWORD)out.GetLength();
  if (dwFlags & FORMAT_MESSAGE_ALLOCATE_BUFFER) {
    HLOCAL h = LocalAlloc(LMEM_FIXED, (UINT)len + 1);
    if (h == NULL) {
      Fail(ERROR_NOT_ENOUGH_MEMORY);
      return 0;
    }
    char FAR* buf = (char FAR*)(char NEAR*)h;
    memcpy(buf, (LPCSTR)out, len + 1);
    *(LPSTR FAR*)lpBuffer = buf;
    return len;
  }
  if (len + 1 > nSize) {
    Fail(ERROR_INSUFFICIENT_BUFFER);
    return 0;
  }
  memcpy(lpBuffer, (LPCSTR)out, len + 1);
  return len;
}

/// Converts ANSI text to UTF-16. The ANSI code page is taken as Latin-1.
extern "C" int WINAPI _AfxW16MultiByteToWideChar(UINT, DWORD, LPCSTR lpMultiByte,
                                                 int cbMultiByte, LPWSTR lpWideChar,
                                                 int cchWideChar) {
  int n = cbMultiByte >= 0 ? cbMultiByte : lstrlen(lpMultiByte) + 1;
  if (cchWideChar == 0) return n;
  if (n > cchWideChar) {
    Fail(ERROR_INSUFFICIENT_BUFFER);
    return 0;
  }
  for (int i = 0; i < n; i++) lpWideChar[i] = (WCHAR)(BYTE)lpMultiByte[i];
  return n;
}

/// Converts UTF-16 text to ANSI (Latin-1); a character outside Latin-1
/// becomes the default character, or '?'.
extern "C" int WINAPI _AfxW16WideCharToMultiByte(UINT, DWORD, LPCWSTR lpWideChar,
                                                 int cchWideChar, LPSTR lpMultiByte,
                                                 int cbMultiByte, LPCSTR lpDefaultChar,
                                                 LPBOOL lpUsedDefaultChar) {
  int n = cchWideChar;
  if (n < 0) n = lstrlenW(lpWideChar) + 1;
  if (lpUsedDefaultChar) *lpUsedDefaultChar = FALSE;
  if (cbMultiByte == 0) return n;
  if (n > cbMultiByte) {
    Fail(ERROR_INSUFFICIENT_BUFFER);
    return 0;
  }
  for (int i = 0; i < n; i++) {
    WCHAR c = lpWideChar[i];
    if (c > 0xFF) {
      c = lpDefaultChar ? (BYTE)*lpDefaultChar : '?';
      if (lpUsedDefaultChar) *lpUsedDefaultChar = TRUE;
    }
    lpMultiByte[i] = (char)c;
  }
  return n;
}

/// Returns the length of a wide string.
extern "C" int WINAPI lstrlenW(LPCWSTR lpString) {
  if (lpString == NULL) return 0;
  int n = 0;
  while (lpString[n]) n++;
  return n;
}

/// Compares two wide strings by their character codes.
extern "C" int WINAPI lstrcmpW(LPCWSTR a, LPCWSTR b) {
  while (*a && *a == *b) a++, b++;
  return *a < *b ? -1 : *a > *b ? 1 : 0;
}

/// Returns the lower-case form of a Latin-1 character.
static WCHAR LowerW(WCHAR c) {
  if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7)) return (WCHAR)(c + 32);
  return c;
}

/// Returns the upper-case form of a Latin-1 character.
static WCHAR UpperW(WCHAR c) {
  if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) return (WCHAR)(c - 32);
  return c;
}

/// Compares two wide strings, case blind.
extern "C" int WINAPI lstrcmpiW(LPCWSTR a, LPCWSTR b) {
  while (*a && LowerW(*a) == LowerW(*b)) a++, b++;
  WCHAR ca = LowerW(*a), cb = LowerW(*b);
  return ca < cb ? -1 : ca > cb ? 1 : 0;
}

/// Changes cchLength characters to upper case.
extern "C" DWORD WINAPI CharUpperBuffW(LPWSTR lpsz, DWORD cchLength) {
  for (DWORD i = 0; i < cchLength; i++) lpsz[i] = UpperW(lpsz[i]);
  return cchLength;
}

/// Changes cchLength characters to lower case.
extern "C" DWORD WINAPI CharLowerBuffW(LPWSTR lpsz, DWORD cchLength) {
  for (DWORD i = 0; i < cchLength; i++) lpsz[i] = LowerW(lpsz[i]);
  return cchLength;
}

/// Loads a string resource as wide text.
extern "C" int WINAPI LoadStringW(HINSTANCE hInstance, UINT uID, LPWSTR lpBuffer,
                                  int cchBufferMax) {
  if (cchBufferMax <= 0) return 0;
  char sz[256];
  int n = LoadString(hInstance, uID, sz, sizeof sz);
  if (n >= cchBufferMax) n = cchBufferMax - 1;
  _AfxW16MultiByteToWideChar(CP_ACP, 0, sz, n, lpBuffer, n);
  lpBuffer[n] = 0;
  return n;
}

/// Gives the value of an environment variable as wide text.
extern "C" DWORD WINAPI GetEnvironmentVariableW(LPCWSTR lpName, LPWSTR lpBuffer, DWORD nSize) {
  char szName[128], sz[256];
  if (!_AfxW16WideCharToMultiByte(CP_ACP, 0, lpName, -1, szName, sizeof szName, NULL, NULL))
    return 0;
  DWORD n = _AfxW16GetEnvironmentVariable(szName, sz, sizeof sz);
  if (n == 0 || n >= sizeof sz) return n;
  if (n + 1 > nSize) return n + 1;
  _AfxW16MultiByteToWideChar(CP_ACP, 0, sz, (int)n + 1, lpBuffer, (int)n + 1);
  return n;
}

/// Formats a wide message string (see _AfxW16FormatMessage). The inserts
/// are taken as ANSI.
extern "C" DWORD WINAPI FormatMessageW(DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId,
                                       DWORD dwLanguageId, LPWSTR lpBuffer, DWORD nSize,
                                       va_list FAR* Arguments) {
  if (!(dwFlags & FORMAT_MESSAGE_FROM_STRING) || lpSource == NULL) {
    Fail(ERROR_NOT_SUPPORTED);
    return 0;
  }
  CStringA strSource((LPCWSTR)lpSource);
  LPSTR pszOut = NULL;
  DWORD n = _AfxW16FormatMessage(dwFlags | FORMAT_MESSAGE_ALLOCATE_BUFFER, (LPCSTR)strSource,
                                 dwMessageId, dwLanguageId, (LPSTR)&pszOut, 0, Arguments);
  if (n == 0) return 0;
  DWORD nResult = 0;
  if (dwFlags & FORMAT_MESSAGE_ALLOCATE_BUFFER) {
    HLOCAL h = LocalAlloc(LMEM_FIXED, (UINT)(n + 1) * sizeof(WCHAR));
    if (h != NULL) {
      WCHAR FAR* buf = (WCHAR FAR*)(WCHAR NEAR*)h;
      _AfxW16MultiByteToWideChar(CP_ACP, 0, pszOut, (int)n + 1, buf, (int)n + 1);
      *(LPWSTR FAR*)lpBuffer = buf;
      nResult = n;
    } else {
      Fail(ERROR_NOT_ENOUGH_MEMORY);
    }
  } else if (n + 1 <= nSize) {
    _AfxW16MultiByteToWideChar(CP_ACP, 0, pszOut, (int)n + 1, lpBuffer, (int)n + 1);
    nResult = n;
  } else {
    Fail(ERROR_INSUFFICIENT_BUFFER);
  }
  LocalFree((HLOCAL)(char NEAR*)pszOut);
  return nResult;
}

// ---------------------------------------------------------------------
// Tasks

/// Waits dwMilliseconds, letting the other tasks run.
extern "C" void WINAPI _AfxW16Sleep(DWORD dwMilliseconds) {
  DWORD dwStart = GetTickCount();
  do {
    Yield();
  } while (GetTickCount() - dwStart < dwMilliseconds);
}

/// Ends the program.
extern "C" void WINAPI _AfxW16ExitProcess(UINT uExitCode) { exit((int)uExitCode); }

namespace {
const DWORD kTlsSlots = 16;
LPVOID g_tls[kTlsSlots];
bool g_tlsUsed[kTlsSlots];
}  // namespace

/// Takes a free thread-local storage index.
extern "C" DWORD WINAPI _AfxW16TlsAlloc(void) {
  for (DWORD i = 0; i < kTlsSlots; i++) {
    if (!g_tlsUsed[i]) {
      g_tlsUsed[i] = true;
      g_tls[i] = NULL;
      return i;
    }
  }
  Fail(ERROR_NOT_ENOUGH_MEMORY);
  return TLS_OUT_OF_INDEXES;
}

/// Frees a thread-local storage index.
extern "C" BOOL WINAPI _AfxW16TlsFree(DWORD dwTlsIndex) {
  if (dwTlsIndex >= kTlsSlots || !g_tlsUsed[dwTlsIndex]) return Fail(ERROR_INVALID_PARAMETER);
  g_tlsUsed[dwTlsIndex] = false;
  return TRUE;
}

/// Returns the value at a thread-local storage index.
extern "C" LPVOID WINAPI _AfxW16TlsGetValue(DWORD dwTlsIndex) {
  if (dwTlsIndex >= kTlsSlots) {
    Fail(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  g_dwLastError = 0;
  return g_tls[dwTlsIndex];
}

/// Sets the value at a thread-local storage index.
extern "C" BOOL WINAPI _AfxW16TlsSetValue(DWORD dwTlsIndex, LPVOID lpTlsValue) {
  if (dwTlsIndex >= kTlsSlots) return Fail(ERROR_INVALID_PARAMETER);
  g_tls[dwTlsIndex] = lpTlsValue;
  return TRUE;
}

#endif  // _AFX_WIN16
