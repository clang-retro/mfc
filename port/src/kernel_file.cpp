// kernel_file.cpp - kernel32 files, directories and paths over POSIX.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <wctype.h>

#include <string>
#include <vector>

using namespace winport;

// ---------------------------------------------------------------------
// Helpers shared with the rest of the layer

namespace winport {

DWORD ErrorFromErrno(int e) {
  switch (e) {
    case 0: return ERROR_SUCCESS;
    case ENOENT: return ERROR_FILE_NOT_FOUND;
    case ENOTDIR: return ERROR_PATH_NOT_FOUND;
    case EACCES: case EPERM: case EROFS: return ERROR_ACCESS_DENIED;
    case EEXIST: return ERROR_ALREADY_EXISTS;
    case ENOTEMPTY: return ERROR_DIR_NOT_EMPTY;
    case EBADF: return ERROR_INVALID_HANDLE;
    case ENOMEM: return ERROR_NOT_ENOUGH_MEMORY;
    case EINVAL: return ERROR_INVALID_PARAMETER;
    case ENOSPC: return ERROR_DISK_FULL;
    case EMFILE: case ENFILE: return ERROR_TOO_MANY_OPEN_FILES;
    case EAGAIN: return ERROR_LOCK_VIOLATION;
    case ENAMETOOLONG: return ERROR_FILENAME_EXCED_RANGE;
    case EXDEV: return ERROR_NOT_SAME_DEVICE;
    case EISDIR: return ERROR_ACCESS_DENIED;
    case EBUSY: return ERROR_BUSY;
    case EPIPE: return ERROR_BROKEN_PIPE;
    default: return ERROR_GEN_FAILURE;
  }
}

std::wstring Widen(const char* s, int n) {
  if (!s) return std::wstring();
  if (n < 0) n = (int)strlen(s);
  std::wstring w;
  int i = 0;
  while (i < n) {
    unsigned char c = (unsigned char)s[i];
    unsigned int cp;
    int len;
    if (c < 0x80) { cp = c; len = 1; }
    else if ((c & 0xe0) == 0xc0) { cp = c & 0x1f; len = 2; }
    else if ((c & 0xf0) == 0xe0) { cp = c & 0x0f; len = 3; }
    else if ((c & 0xf8) == 0xf0) { cp = c & 0x07; len = 4; }
    else { w += (wchar_t)0xfffd; i++; continue; }
    if (i + len > n) { w += (wchar_t)0xfffd; break; }
    bool ok = true;
    for (int k = 1; k < len; k++) {
      unsigned char cc = (unsigned char)s[i + k];
      if ((cc & 0xc0) != 0x80) { ok = false; break; }
      cp = (cp << 6) | (cc & 0x3f);
    }
    if (!ok) { w += (wchar_t)0xfffd; i++; continue; }
    w += (wchar_t)cp;
    i += len;
  }
  return w;
}

std::string Narrow(const wchar_t* s, int n) {
  if (!s) return std::string();
  if (n < 0) n = (int)wcslen(s);
  std::string r;
  for (int i = 0; i < n; i++) {
    unsigned int c = (unsigned int)s[i];
    if (c < 0x80) r += (char)c;
    else if (c < 0x800) { r += (char)(0xc0 | (c >> 6)); r += (char)(0x80 | (c & 0x3f)); }
    else if (c < 0x10000) {
      r += (char)(0xe0 | (c >> 12)); r += (char)(0x80 | ((c >> 6) & 0x3f)); r += (char)(0x80 | (c & 0x3f));
    } else {
      r += (char)(0xf0 | (c >> 18)); r += (char)(0x80 | ((c >> 12) & 0x3f));
      r += (char)(0x80 | ((c >> 6) & 0x3f)); r += (char)(0x80 | (c & 0x3f));
    }
  }
  return r;
}

DWORD CopyOut(const std::string& s, LPSTR buf, DWORD cch) {
  if (buf && cch > s.size()) {
    memcpy(buf, s.c_str(), s.size() + 1);
    return (DWORD)s.size();
  }
  return (DWORD)s.size() + 1;
}

DWORD CopyOut(const std::wstring& s, LPWSTR buf, DWORD cch) {
  if (buf && cch > s.size()) {
    memcpy(buf, s.c_str(), (s.size() + 1) * sizeof(wchar_t));
    return (DWORD)s.size();
  }
  return (DWORD)s.size() + 1;
}

std::string PosixPath(const char* path) {
  if (!path) return std::string();
  std::string p(path);
  for (char& c : p)
    if (c == '\\') c = '/';
  // A drive letter names the root: "C:/x" is "/x" and "C:x" is "x".
  if (p.size() >= 2 && p[1] == ':' && ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z'))) {
    p.erase(0, 2);
    if (p.empty()) p = "/";
  }
  return p;
}

std::string PosixPath(const wchar_t* path) {
  std::string n = Narrow(path);
  return PosixPath(n.c_str());
}

FILETIME FileTimeFromUnix(long long sec, long nsec) {
  return ToFileTime((ULONGLONG)(sec * 10000000LL + nsec / 100) + kEpochDelta);
}

void UnixFromFileTime(const FILETIME& ft, long long* sec, long* nsec) {
  long long v = (long long)(ToU64(ft) - kEpochDelta);
  long long s = v / 10000000LL;
  long long r = v % 10000000LL;
  if (r < 0) { r += 10000000LL; s--; }
  *sec = s;
  *nsec = (long)(r * 100);
}

}  // namespace winport

namespace {

ssize_t read_fd(int fd, void* b, size_t n) { return ::read(fd, b, n); }
bool access_ok(const std::string& p) { return access(p.c_str(), F_OK) == 0; }

struct File : Object {
  int fd;
  std::string path;
  bool deleteOnClose = false;
  bool append = false;
  File(int f) : Object(kFile), fd(f) {}
  ~File() override {
    if (fd > 2) close(fd);
    if (deleteOnClose && !path.empty()) unlink(path.c_str());
  }
};

File* GetFile(HANDLE h);

// The standard handles are files over descriptors 0 to 2 that are never
// closed.
File* StdFile(int n) {
  static File* files[3];
  static pthread_once_t once = PTHREAD_ONCE_INIT;
  pthread_once(&once, [] {
    for (int i = 0; i < 3; i++) {
      files[i] = new File(i);
      files[i]->refs = 1 << 30;
    }
  });
  return files[n];
}

File* GetFile(HANDLE h) {
  return static_cast<File*>(FromHandle(h, kFile));
}

timespec ToTimespec(const FILETIME* ft) {
  timespec ts;
  long long s;
  long ns;
  UnixFromFileTime(*ft, &s, &ns);
  ts.tv_sec = (time_t)s;
  ts.tv_nsec = ns;
  return ts;
}

void StatTimes(const struct stat& st, FILETIME* c, FILETIME* a, FILETIME* w) {
#if defined(__APPLE__)
  if (c) *c = FileTimeFromUnix(st.st_birthtimespec.tv_sec, st.st_birthtimespec.tv_nsec);
  if (a) *a = FileTimeFromUnix(st.st_atimespec.tv_sec, st.st_atimespec.tv_nsec);
  if (w) *w = FileTimeFromUnix(st.st_mtimespec.tv_sec, st.st_mtimespec.tv_nsec);
#else
  // POSIX has no creation time; the change time is the nearest.
  if (c) *c = FileTimeFromUnix(st.st_ctim.tv_sec, st.st_ctim.tv_nsec);
  if (a) *a = FileTimeFromUnix(st.st_atim.tv_sec, st.st_atim.tv_nsec);
  if (w) *w = FileTimeFromUnix(st.st_mtim.tv_sec, st.st_mtim.tv_nsec);
#endif
}

DWORD Attributes(const std::string& path, const struct stat& st) {
  DWORD a = 0;
  if (S_ISDIR(st.st_mode)) a |= FILE_ATTRIBUTE_DIRECTORY;
  if (!(st.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH))) a |= FILE_ATTRIBUTE_READONLY;
  size_t slash = path.find_last_of('/');
  const char* name = path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
  if (name[0] == '.' && strcmp(name, ".") != 0 && strcmp(name, "..") != 0) a |= FILE_ATTRIBUTE_HIDDEN;
  if (a == 0) a = FILE_ATTRIBUTE_NORMAL;
  return a;
}

HANDLE OpenFile(const std::string& path, DWORD acc, DWORD disposition, DWORD flags) {
  if (path.empty()) {
    SetLastError(ERROR_PATH_NOT_FOUND);
    return INVALID_HANDLE_VALUE;
  }
  int o = 0;
  bool rd = (acc & (GENERIC_READ | FILE_READ_DATA | GENERIC_ALL)) != 0;
  bool wr = (acc & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA | GENERIC_ALL)) != 0;
  o = rd && wr ? O_RDWR : wr ? O_WRONLY : O_RDONLY;
  bool existed = access_ok(path);
  switch (disposition) {
    case CREATE_NEW: o |= O_CREAT | O_EXCL; break;
    case CREATE_ALWAYS: o |= O_CREAT | O_TRUNC; break;
    case OPEN_EXISTING: break;
    case OPEN_ALWAYS: o |= O_CREAT; break;
    case TRUNCATE_EXISTING: o |= O_TRUNC; break;
    default: SetLastError(ERROR_INVALID_PARAMETER); return INVALID_HANDLE_VALUE;
  }
  if ((o & O_TRUNC) && !wr) o = (o & ~O_ACCMODE) | O_RDWR;
  struct stat st;
  if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
    // Directories open only with backup semantics, and then read only.
    if (!(flags & FILE_FLAG_BACKUP_SEMANTICS)) {
      SetLastError(ERROR_ACCESS_DENIED);
      return INVALID_HANDLE_VALUE;
    }
    o = O_RDONLY;
  }
  int fd = open(path.c_str(), o | O_CLOEXEC, 0666);
  if (fd < 0) {
    SetLastError(errno == ENOENT && path.find('/') != std::string::npos &&
                         [&] {
                           std::string dir = path.substr(0, path.find_last_of('/'));
                           return !dir.empty() && access(dir.c_str(), F_OK) != 0;
                         }()
                     ? ERROR_PATH_NOT_FOUND
                     : errno == EEXIST ? ERROR_FILE_EXISTS : ErrorFromErrno(errno));
    return INVALID_HANDLE_VALUE;
  }
  File* f = new File(fd);
  f->path = path;
  f->deleteOnClose = (flags & FILE_FLAG_DELETE_ON_CLOSE) != 0;
  // CREATE_ALWAYS and OPEN_ALWAYS report an existing file this way.
  SetLastError((existed && (disposition == CREATE_ALWAYS || disposition == OPEN_ALWAYS)) ? ERROR_ALREADY_EXISTS
                                                                                        : ERROR_SUCCESS);
  return NewHandle(f);
}

}  // namespace

HANDLE winport::DuplicateFile(Object* o) {
  File* f = static_cast<File*>(o);
  int fd = dup(f->fd);
  if (fd < 0) {
    SetErrorFromErrno();
    return NULL;
  }
  File* n = new File(fd);
  n->path = f->path;
  return NewHandle(n);
}

extern "C" {

HANDLE WINAPI CreateFileA(LPCSTR name, DWORD access, DWORD, LPSECURITY_ATTRIBUTES, DWORD disposition, DWORD flags,
                          HANDLE) {
  return OpenFile(PosixPath(name), access, disposition, flags);
}

HANDLE WINAPI CreateFileW(LPCWSTR name, DWORD access, DWORD, LPSECURITY_ATTRIBUTES, DWORD disposition, DWORD flags,
                          HANDLE) {
  return OpenFile(PosixPath(name), access, disposition, flags);
}

HANDLE WINAPI GetStdHandle(DWORD n) {
  int i = n == STD_INPUT_HANDLE ? 0 : n == STD_OUTPUT_HANDLE ? 1 : n == STD_ERROR_HANDLE ? 2 : -1;
  if (i < 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return INVALID_HANDLE_VALUE;
  }
  return (HANDLE)StdFile(i);
}

BOOL WINAPI ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD read, LPOVERLAPPED ov) {
  File* f = GetFile(h);
  if (read) *read = 0;
  if (!f) return FALSE;
  ssize_t r;
  if (ov) r = pread(f->fd, buf, n, (off_t)(((ULONGLONG)ov->OffsetHigh << 32) | ov->Offset));
  else do r = read_fd(f->fd, buf, n); while (r < 0 && errno == EINTR);
  if (r < 0) {
    SetErrorFromErrno();
    return FALSE;
  }
  if (read) *read = (DWORD)r;
  return TRUE;
}

BOOL WINAPI WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD written, LPOVERLAPPED ov) {
  File* f = GetFile(h);
  if (written) *written = 0;
  if (!f) return FALSE;
  size_t done = 0;
  while (done < n) {
    ssize_t r;
    if (ov)
      r = pwrite(f->fd, (const char*)buf + done, n - done,
                 (off_t)((((ULONGLONG)ov->OffsetHigh << 32) | ov->Offset) + done));
    else
      r = write(f->fd, (const char*)buf + done, n - done);
    if (r < 0) {
      if (errno == EINTR) continue;
      SetErrorFromErrno();
      if (written) *written = (DWORD)done;
      return FALSE;
    }
    done += (size_t)r;
  }
  if (written) *written = (DWORD)done;
  return TRUE;
}

BOOL WINAPI SetFilePointerEx(HANDLE h, LARGE_INTEGER dist, PLARGE_INTEGER newPos, DWORD method) {
  File* f = GetFile(h);
  if (!f) return FALSE;
  int whence = method == FILE_BEGIN ? SEEK_SET : method == FILE_CURRENT ? SEEK_CUR : method == FILE_END ? SEEK_END : -1;
  if (whence < 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (whence == SEEK_SET && dist.QuadPart < 0) {
    SetLastError(ERROR_NEGATIVE_SEEK);
    return FALSE;
  }
  off_t r = lseek(f->fd, (off_t)dist.QuadPart, whence);
  if (r < 0) {
    SetLastError(errno == EINVAL ? ERROR_NEGATIVE_SEEK : ErrorFromErrno(errno));
    return FALSE;
  }
  if (newPos) newPos->QuadPart = r;
  return TRUE;
}

DWORD WINAPI SetFilePointer(HANDLE h, LONG lo, PLONG hi, DWORD method) {
  LARGE_INTEGER d, n;
  d.QuadPart = hi ? (((LONGLONG)*hi) << 32) | (DWORD)lo : (LONGLONG)lo;
  if (!SetFilePointerEx(h, d, &n, method)) return INVALID_SET_FILE_POINTER;
  if (hi) *hi = (LONG)(n.QuadPart >> 32);
  SetLastError(ERROR_SUCCESS);
  return (DWORD)n.QuadPart;
}

BOOL WINAPI SetEndOfFile(HANDLE h) {
  File* f = GetFile(h);
  if (!f) return FALSE;
  off_t pos = lseek(f->fd, 0, SEEK_CUR);
  if (pos < 0 || ftruncate(f->fd, pos) != 0) {
    SetErrorFromErrno();
    return FALSE;
  }
  return TRUE;
}

BOOL WINAPI FlushFileBuffers(HANDLE h) {
  File* f = GetFile(h);
  if (!f) return FALSE;
  if (f->fd > 2 && fsync(f->fd) != 0 && errno != EINVAL) {
    SetErrorFromErrno();
    return FALSE;
  }
  return TRUE;
}

BOOL WINAPI GetFileSizeEx(HANDLE h, PLARGE_INTEGER size) {
  File* f = GetFile(h);
  if (!f) return FALSE;
  struct stat st;
  if (fstat(f->fd, &st) != 0) {
    SetErrorFromErrno();
    return FALSE;
  }
  size->QuadPart = st.st_size;
  return TRUE;
}

DWORD WINAPI GetFileSize(HANDLE h, LPDWORD hi) {
  LARGE_INTEGER s;
  if (!GetFileSizeEx(h, &s)) return INVALID_FILE_SIZE;
  if (hi) *hi = (DWORD)(s.QuadPart >> 32);
  SetLastError(ERROR_SUCCESS);
  return (DWORD)s.QuadPart;
}

DWORD WINAPI GetFileType(HANDLE h) {
  File* f = GetFile(h);
  if (!f) return FILE_TYPE_UNKNOWN;
  struct stat st;
  if (fstat(f->fd, &st) != 0) return FILE_TYPE_UNKNOWN;
  if (S_ISREG(st.st_mode) || S_ISDIR(st.st_mode)) return FILE_TYPE_DISK;
  if (S_ISCHR(st.st_mode)) return FILE_TYPE_CHAR;
  if (S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode)) return FILE_TYPE_PIPE;
  return FILE_TYPE_UNKNOWN;
}

BOOL WINAPI GetFileTime(HANDLE h, LPFILETIME c, LPFILETIME a, LPFILETIME w) {
  File* f = GetFile(h);
  if (!f) return FALSE;
  struct stat st;
  if (fstat(f->fd, &st) != 0) {
    SetErrorFromErrno();
    return FALSE;
  }
  StatTimes(st, c, a, w);
  return TRUE;
}

BOOL WINAPI SetFileTime(HANDLE h, const FILETIME*, const FILETIME* a, const FILETIME* w) {
  File* f = GetFile(h);
  if (!f) return FALSE;
  timespec ts[2];
  ts[0].tv_sec = 0;
  ts[0].tv_nsec = UTIME_OMIT;
  ts[1] = ts[0];
  if (a) ts[0] = ToTimespec(a);
  if (w) ts[1] = ToTimespec(w);
  if (futimens(f->fd, ts) != 0) {
    SetErrorFromErrno();
    return FALSE;
  }
  return TRUE;
}

BOOL WINAPI GetFileInformationByHandle(HANDLE h, LPBY_HANDLE_FILE_INFORMATION info) {
  File* f = GetFile(h);
  if (!f) return FALSE;
  struct stat st;
  if (fstat(f->fd, &st) != 0) {
    SetErrorFromErrno();
    return FALSE;
  }
  memset(info, 0, sizeof(*info));
  info->dwFileAttributes = Attributes(f->path, st);
  StatTimes(st, &info->ftCreationTime, &info->ftLastAccessTime, &info->ftLastWriteTime);
  info->dwVolumeSerialNumber = (DWORD)st.st_dev;
  info->nFileSizeHigh = (DWORD)((ULONGLONG)st.st_size >> 32);
  info->nFileSizeLow = (DWORD)st.st_size;
  info->nNumberOfLinks = (DWORD)st.st_nlink;
  info->nFileIndexHigh = (DWORD)((ULONGLONG)st.st_ino >> 32);
  info->nFileIndexLow = (DWORD)st.st_ino;
  return TRUE;
}

static BOOL Lock(HANDLE h, DWORD offLo, DWORD offHi, DWORD lenLo, DWORD lenHi, short type) {
  File* f = GetFile(h);
  if (!f) return FALSE;
  struct flock fl;
  memset(&fl, 0, sizeof(fl));
  fl.l_type = type;
  fl.l_whence = SEEK_SET;
  fl.l_start = (off_t)(((ULONGLONG)offHi << 32) | offLo);
  fl.l_len = (off_t)(((ULONGLONG)lenHi << 32) | lenLo);
  // Record locks are per process on POSIX; open file description locks
  // behave like Win32's where the system has them.
#ifdef F_OFD_SETLK
  int cmd = F_OFD_SETLK;
#else
  int cmd = F_SETLK;
#endif
  if (fcntl(f->fd, cmd, &fl) != 0) {
    SetLastError(type == F_UNLCK ? ERROR_NOT_LOCKED : ERROR_LOCK_VIOLATION);
    return FALSE;
  }
  return TRUE;
}

BOOL WINAPI LockFile(HANDLE h, DWORD offLo, DWORD offHi, DWORD lenLo, DWORD lenHi) {
  return Lock(h, offLo, offHi, lenLo, lenHi, F_WRLCK);
}

BOOL WINAPI UnlockFile(HANDLE h, DWORD offLo, DWORD offHi, DWORD lenLo, DWORD lenHi) {
  return Lock(h, offLo, offHi, lenLo, lenHi, F_UNLCK);
}

// ---------------------------------------------------------------------
// Names

static BOOL Result(int r) {
  if (r != 0) {
    SetErrorFromErrno();
    return FALSE;
  }
  return TRUE;
}

static BOOL DoDelete(const std::string& p) {
  struct stat st;
  if (stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
  }
  return Result(unlink(p.c_str()));
}
BOOL WINAPI DeleteFileA(LPCSTR n) { return DoDelete(PosixPath(n)); }
BOOL WINAPI DeleteFileW(LPCWSTR n) { return DoDelete(PosixPath(n)); }

static BOOL DoMove(const std::string& from, const std::string& to, DWORD flags) {
  if (!(flags & MOVEFILE_REPLACE_EXISTING) && access(to.c_str(), F_OK) == 0) {
    SetLastError(ERROR_ALREADY_EXISTS);
    return FALSE;
  }
  if (rename(from.c_str(), to.c_str()) == 0) return TRUE;
  if (errno == EXDEV && (flags & MOVEFILE_COPY_ALLOWED)) {
    if (!CopyFileA(from.c_str(), to.c_str(), FALSE)) return FALSE;
    return Result(unlink(from.c_str()));
  }
  SetErrorFromErrno();
  return FALSE;
}
BOOL WINAPI MoveFileA(LPCSTR a, LPCSTR b) { return DoMove(PosixPath(a), PosixPath(b), MOVEFILE_COPY_ALLOWED); }
BOOL WINAPI MoveFileW(LPCWSTR a, LPCWSTR b) { return DoMove(PosixPath(a), PosixPath(b), MOVEFILE_COPY_ALLOWED); }
BOOL WINAPI MoveFileExA(LPCSTR a, LPCSTR b, DWORD f) {
  if (!b) return DeleteFileA(a);
  return DoMove(PosixPath(a), PosixPath(b), f);
}
BOOL WINAPI MoveFileExW(LPCWSTR a, LPCWSTR b, DWORD f) {
  if (!b) return DeleteFileW(a);
  return DoMove(PosixPath(a), PosixPath(b), f);
}

static BOOL DoCopy(const std::string& from, const std::string& to, BOOL failIfExists) {
  int in = open(from.c_str(), O_RDONLY | O_CLOEXEC);
  if (in < 0) return Result(-1);
  struct stat st;
  fstat(in, &st);
  int out = open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | (failIfExists ? O_EXCL : 0),
                 st.st_mode & 0777);
  if (out < 0) {
    DWORD e = errno == EEXIST ? ERROR_FILE_EXISTS : ErrorFromErrno(errno);
    close(in);
    SetLastError(e);
    return FALSE;
  }
  char buf[65536];
  ssize_t r;
  BOOL ok = TRUE;
  while ((r = read_fd(in, buf, sizeof buf)) > 0) {
    if (write(out, buf, (size_t)r) != r) {
      ok = Result(-1);
      break;
    }
  }
  if (r < 0) ok = Result(-1);
  close(in);
  close(out);
  return ok;
}
BOOL WINAPI CopyFileA(LPCSTR a, LPCSTR b, BOOL f) { return DoCopy(PosixPath(a), PosixPath(b), f); }
BOOL WINAPI CopyFileW(LPCWSTR a, LPCWSTR b, BOOL f) { return DoCopy(PosixPath(a), PosixPath(b), f); }

BOOL WINAPI CreateDirectoryA(LPCSTR n, LPSECURITY_ATTRIBUTES) { return Result(mkdir(PosixPath(n).c_str(), 0777)); }
BOOL WINAPI CreateDirectoryW(LPCWSTR n, LPSECURITY_ATTRIBUTES) { return Result(mkdir(PosixPath(n).c_str(), 0777)); }
BOOL WINAPI RemoveDirectoryA(LPCSTR n) { return Result(rmdir(PosixPath(n).c_str())); }
BOOL WINAPI RemoveDirectoryW(LPCWSTR n) { return Result(rmdir(PosixPath(n).c_str())); }

static DWORD DoGetAttributes(const std::string& p) {
  struct stat st;
  if (stat(p.c_str(), &st) != 0) {
    SetErrorFromErrno();
    return INVALID_FILE_ATTRIBUTES;
  }
  return Attributes(p, st);
}
DWORD WINAPI GetFileAttributesA(LPCSTR n) { return DoGetAttributes(PosixPath(n)); }
DWORD WINAPI GetFileAttributesW(LPCWSTR n) { return DoGetAttributes(PosixPath(n)); }

static BOOL DoGetAttributesEx(const std::string& p, LPVOID out) {
  struct stat st;
  if (stat(p.c_str(), &st) != 0) return Result(-1);
  WIN32_FILE_ATTRIBUTE_DATA* d = (WIN32_FILE_ATTRIBUTE_DATA*)out;
  d->dwFileAttributes = Attributes(p, st);
  StatTimes(st, &d->ftCreationTime, &d->ftLastAccessTime, &d->ftLastWriteTime);
  d->nFileSizeHigh = (DWORD)((ULONGLONG)st.st_size >> 32);
  d->nFileSizeLow = (DWORD)st.st_size;
  return TRUE;
}
BOOL WINAPI GetFileAttributesExA(LPCSTR n, GET_FILEEX_INFO_LEVELS, LPVOID o) { return DoGetAttributesEx(PosixPath(n), o); }
BOOL WINAPI GetFileAttributesExW(LPCWSTR n, GET_FILEEX_INFO_LEVELS, LPVOID o) { return DoGetAttributesEx(PosixPath(n), o); }

// Only the read-only attribute maps to POSIX: it clears the write bits.
static BOOL DoSetAttributes(const std::string& p, DWORD a) {
  struct stat st;
  if (stat(p.c_str(), &st) != 0) return Result(-1);
  mode_t m = st.st_mode & 07777;
  if (a & FILE_ATTRIBUTE_READONLY) m &= ~(mode_t)(S_IWUSR | S_IWGRP | S_IWOTH);
  else if (!(m & S_IWUSR)) m |= S_IWUSR;
  return Result(chmod(p.c_str(), m));
}
BOOL WINAPI SetFileAttributesA(LPCSTR n, DWORD a) { return DoSetAttributes(PosixPath(n), a); }
BOOL WINAPI SetFileAttributesW(LPCWSTR n, DWORD a) { return DoSetAttributes(PosixPath(n), a); }

// ---------------------------------------------------------------------
// Finding files

}  // extern "C"

namespace {

struct Find : Object {
  DIR* dir;
  std::string dirPath;  // with a trailing slash, or empty for "."
  std::string mask;
  Find() : Object(kFind), dir(NULL) {}
  ~Find() override {
    if (dir) closedir(dir);
  }
};

// Win32 wildcards, without case: "*" and "?" (where "*.*" also matches
// names without a dot).
bool Match(const char* mask, const char* name) {
  if (strcmp(mask, "*.*") == 0 || strcmp(mask, "*") == 0) return true;
  std::string m(mask);
  if (m.size() >= 2 && m.compare(m.size() - 2, 2, ".*") == 0 && strchr(name, '.') == NULL) m.erase(m.size() - 2);
  return fnmatch(m.c_str(), name, FNM_CASEFOLD | FNM_PERIOD * 0) == 0;
}

struct Entry {
  std::string name;
  struct stat st;
};

bool NextEntry(Find* f, Entry* e) {
  while (struct dirent* d = readdir(f->dir)) {
    if (!Match(f->mask.c_str(), d->d_name)) continue;
    std::string full = f->dirPath + d->d_name;
    if (stat(full.c_str(), &e->st) != 0 && lstat(full.c_str(), &e->st) != 0) continue;
    e->name = d->d_name;
    return true;
  }
  return false;
}

template <class T>
void FillCommon(const Entry& e, T* fd) {
  memset(fd, 0, sizeof(*fd));
  fd->dwFileAttributes = Attributes(e.name, e.st);
  StatTimes(e.st, &fd->ftCreationTime, &fd->ftLastAccessTime, &fd->ftLastWriteTime);
  fd->nFileSizeHigh = (DWORD)((ULONGLONG)e.st.st_size >> 32);
  fd->nFileSizeLow = (DWORD)e.st.st_size;
}

void Fill(const Entry& e, WIN32_FIND_DATAA* fd) {
  FillCommon(e, fd);
  strncpy(fd->cFileName, e.name.c_str(), MAX_PATH - 1);
}

void Fill(const Entry& e, WIN32_FIND_DATAW* fd) {
  FillCommon(e, fd);
  std::wstring w = Widen(e.name.c_str());
  wcsncpy(fd->cFileName, w.c_str(), MAX_PATH - 1);
}

template <class T>
HANDLE DoFindFirst(const std::string& pattern, T* fd) {
  Find* f = new Find;
  size_t slash = pattern.find_last_of('/');
  std::string dir = slash == std::string::npos ? std::string() : pattern.substr(0, slash + 1);
  f->mask = slash == std::string::npos ? pattern : pattern.substr(slash + 1);
  f->dirPath = dir;
  if (f->mask.empty()) {
    delete f;
    SetLastError(ERROR_FILE_NOT_FOUND);
    return INVALID_HANDLE_VALUE;
  }
  f->dir = opendir(dir.empty() ? "." : dir.c_str());
  if (!f->dir) {
    DWORD e = errno == ENOENT ? ERROR_PATH_NOT_FOUND : ErrorFromErrno(errno);
    delete f;
    SetLastError(e);
    return INVALID_HANDLE_VALUE;
  }
  Entry e;
  if (!NextEntry(f, &e)) {
    delete f;
    SetLastError(ERROR_FILE_NOT_FOUND);
    return INVALID_HANDLE_VALUE;
  }
  Fill(e, fd);
  return NewHandle(f);
}

template <class T>
BOOL DoFindNext(HANDLE h, T* fd) {
  Find* f = static_cast<Find*>(FromHandle(h, kFind));
  if (!f) return FALSE;
  Entry e;
  if (!NextEntry(f, &e)) {
    SetLastError(ERROR_NO_MORE_FILES);
    return FALSE;
  }
  Fill(e, fd);
  return TRUE;
}

}  // namespace

extern "C" {

HANDLE WINAPI FindFirstFileA(LPCSTR p, LPWIN32_FIND_DATAA fd) { return DoFindFirst(PosixPath(p), fd); }
HANDLE WINAPI FindFirstFileW(LPCWSTR p, LPWIN32_FIND_DATAW fd) { return DoFindFirst(PosixPath(p), fd); }
BOOL WINAPI FindNextFileA(HANDLE h, LPWIN32_FIND_DATAA fd) { return DoFindNext(h, fd); }
BOOL WINAPI FindNextFileW(HANDLE h, LPWIN32_FIND_DATAW fd) { return DoFindNext(h, fd); }
BOOL WINAPI FindClose(HANDLE h) {
  Object* o = FromHandle(h, kFind);
  if (!o) return FALSE;
  return CloseHandle(h);
}

// ---------------------------------------------------------------------
// Paths

// Makes a path absolute and removes "." and ".." parts. The result keeps
// the Windows form the caller used: with backslashes when the input had
// them, so MFC's path parsing sees what it expects.
static std::string FullPath(const std::string& in) {
  std::string p = PosixPath(in.c_str());
  if (p.empty() || p[0] != '/') {
    char cwd[4096];
    if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, "/");
    p = std::string(cwd) + "/" + p;
  }
  std::vector<std::string> parts;
  size_t i = 0;
  while (i <= p.size()) {
    size_t j = p.find('/', i);
    if (j == std::string::npos) j = p.size();
    std::string part = p.substr(i, j - i);
    if (part == "..") {
      if (!parts.empty()) parts.pop_back();
    } else if (!part.empty() && part != ".") {
      parts.push_back(part);
    }
    i = j + 1;
  }
  std::string r;
  for (auto& s : parts) r += "/" + s;
  if (r.empty() || (!p.empty() && p.back() == '/' && !parts.empty())) r += "/";
  return r;
}

DWORD WINAPI GetFullPathNameA(LPCSTR name, DWORD cch, LPSTR buf, LPSTR* filePart) {
  if (!name) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  std::string r = FullPath(name);
  DWORD n = CopyOut(r, buf, cch);
  if (filePart && buf && n < cch) {
    char* s = strrchr(buf, '/');
    *filePart = s && s[1] ? s + 1 : NULL;
  }
  return n;
}

DWORD WINAPI GetFullPathNameW(LPCWSTR name, DWORD cch, LPWSTR buf, LPWSTR* filePart) {
  if (!name) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  std::wstring r = Widen(FullPath(Narrow(name)).c_str());
  DWORD n = CopyOut(r, buf, cch);
  if (filePart && buf && n < cch) {
    wchar_t* s = wcsrchr(buf, L'/');
    *filePart = s && s[1] ? s + 1 : NULL;
  }
  return n;
}

// No short names: both give the path back.
DWORD WINAPI GetShortPathNameA(LPCSTR p, LPSTR b, DWORD n) { return CopyOut(std::string(p ? p : ""), b, n); }
DWORD WINAPI GetShortPathNameW(LPCWSTR p, LPWSTR b, DWORD n) { return CopyOut(std::wstring(p ? p : L""), b, n); }
DWORD WINAPI GetLongPathNameA(LPCSTR p, LPSTR b, DWORD n) { return CopyOut(std::string(p ? p : ""), b, n); }
DWORD WINAPI GetLongPathNameW(LPCWSTR p, LPWSTR b, DWORD n) { return CopyOut(std::wstring(p ? p : L""), b, n); }

static std::string Cwd() {
  char cwd[4096];
  if (!getcwd(cwd, sizeof cwd)) return "/";
  return cwd;
}
DWORD WINAPI GetCurrentDirectoryA(DWORD n, LPSTR b) { return CopyOut(Cwd(), b, n); }
DWORD WINAPI GetCurrentDirectoryW(DWORD n, LPWSTR b) { return CopyOut(Widen(Cwd().c_str()), b, n); }
BOOL WINAPI SetCurrentDirectoryA(LPCSTR p) { return Result(chdir(PosixPath(p).c_str())); }
BOOL WINAPI SetCurrentDirectoryW(LPCWSTR p) { return Result(chdir(PosixPath(p).c_str())); }

static std::string TempDir() {
  const char* t = getenv("TMPDIR");
  if (!t || !*t) t = getenv("TMP");
  if (!t || !*t) t = getenv("TEMP");
  if (!t || !*t) t = "/tmp";
  std::string r(t);
  if (r.back() != '/') r += '/';
  return r;
}
DWORD WINAPI GetTempPathA(DWORD n, LPSTR b) { return CopyOut(TempDir(), b, n); }
DWORD WINAPI GetTempPathW(DWORD n, LPWSTR b) { return CopyOut(Widen(TempDir().c_str()), b, n); }

static UINT DoTempFileName(const std::string& dir, const std::string& prefix, UINT unique, std::string* out) {
  std::string d = PosixPath(dir.c_str());
  if (!d.empty() && d.back() != '/') d += '/';
  std::string pre = prefix.substr(0, 3);
  UINT n = unique ? unique : (UINT)(GetTickCount() ^ (getpid() << 8));
  for (int tries = 0; tries < 65536; tries++) {
    UINT u = (n + tries) & 0xffff;
    if (u == 0) continue;
    char name[32];
    snprintf(name, sizeof name, "%s%X.TMP", pre.c_str(), u);
    *out = d + name;
    if (unique) return unique;
    int fd = open(out->c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (fd >= 0) {
      close(fd);
      return u;
    }
    if (errno != EEXIST) {
      SetErrorFromErrno();
      return 0;
    }
  }
  SetLastError(ERROR_FILE_EXISTS);
  return 0;
}

UINT WINAPI GetTempFileNameA(LPCSTR dir, LPCSTR prefix, UINT unique, LPSTR out) {
  std::string r;
  UINT u = DoTempFileName(dir ? dir : "", prefix ? prefix : "", unique, &r);
  if (u) strncpy(out, r.c_str(), MAX_PATH)[MAX_PATH - 1] = 0;
  return u;
}

UINT WINAPI GetTempFileNameW(LPCWSTR dir, LPCWSTR prefix, UINT unique, LPWSTR out) {
  std::string r;
  UINT u = DoTempFileName(Narrow(dir), Narrow(prefix), unique, &r);
  if (u) wcsncpy(out, Widen(r.c_str()).c_str(), MAX_PATH)[MAX_PATH - 1] = 0;
  return u;
}

// The Windows and system directories are the layer's data directory.
static std::string SystemDir() {
  const char* d = getenv("WINPORT_WINDIR");
  return d && *d ? d : "/usr/share/winport";
}
UINT WINAPI GetWindowsDirectoryA(LPSTR b, UINT n) { return CopyOut(SystemDir(), b, n); }
UINT WINAPI GetWindowsDirectoryW(LPWSTR b, UINT n) { return CopyOut(Widen(SystemDir().c_str()), b, n); }
UINT WINAPI GetSystemDirectoryA(LPSTR b, UINT n) { return CopyOut(SystemDir(), b, n); }
UINT WINAPI GetSystemDirectoryW(LPWSTR b, UINT n) { return CopyOut(Widen(SystemDir().c_str()), b, n); }

BOOL WINAPI GetVolumeInformationA(LPCSTR, LPSTR name, DWORD nName, LPDWORD serial, LPDWORD maxLen, LPDWORD flags,
                                  LPSTR fs, DWORD nFs) {
  if (name && nName) CopyOut(std::string(), name, nName);
  if (serial) *serial = 0;
  if (maxLen) *maxLen = 255;
  if (flags) *flags = 0x3;  // case sensitive and case preserving
  if (fs && nFs) CopyOut(std::string("POSIX"), fs, nFs);
  return TRUE;
}

BOOL WINAPI GetVolumeInformationW(LPCWSTR, LPWSTR name, DWORD nName, LPDWORD serial, LPDWORD maxLen, LPDWORD flags,
                                  LPWSTR fs, DWORD nFs) {
  if (name && nName) CopyOut(std::wstring(), name, nName);
  if (serial) *serial = 0;
  if (maxLen) *maxLen = 255;
  if (flags) *flags = 0x3;
  if (fs && nFs) CopyOut(std::wstring(L"POSIX"), fs, nFs);
  return TRUE;
}

UINT WINAPI GetDriveTypeA(LPCSTR) { return 3; }   // DRIVE_FIXED
UINT WINAPI GetDriveTypeW(LPCWSTR) { return 3; }

static DWORD DoSearchPath(const char* path, const char* file, const char* ext, std::string* out) {
  std::string f = PosixPath(file);
  if (ext && strchr(f.c_str() + (f.find_last_of('/') == std::string::npos ? 0 : f.find_last_of('/')), '.') == NULL)
    f += ext;
  std::vector<std::string> dirs;
  if (f.find('/') != std::string::npos) {
    dirs.push_back("");
  } else if (path) {
    std::string p = path;
    size_t i = 0;
    while (i <= p.size()) {
      size_t j = p.find_first_of(";:", i);
      if (j == std::string::npos) j = p.size();
      dirs.push_back(PosixPath(p.substr(i, j - i).c_str()));
      i = j + 1;
    }
  } else {
    dirs.push_back(".");
    const char* env = getenv("PATH");
    std::string p = env ? env : "";
    size_t i = 0;
    while (i <= p.size()) {
      size_t j = p.find(':', i);
      if (j == std::string::npos) j = p.size();
      dirs.push_back(p.substr(i, j - i));
      i = j + 1;
    }
  }
  for (auto& d : dirs) {
    std::string c = d.empty() ? f : d + "/" + f;
    if (access(c.c_str(), F_OK) == 0) {
      *out = FullPath(c);
      return 1;
    }
  }
  SetLastError(ERROR_FILE_NOT_FOUND);
  return 0;
}

DWORD WINAPI SearchPathA(LPCSTR path, LPCSTR file, LPCSTR ext, DWORD n, LPSTR buf, LPSTR* filePart) {
  std::string r;
  if (!DoSearchPath(path, file, ext, &r)) return 0;
  DWORD k = CopyOut(r, buf, n);
  if (filePart && k < n) *filePart = strrchr(buf, '/') + 1;
  return k;
}

DWORD WINAPI SearchPathW(LPCWSTR path, LPCWSTR file, LPCWSTR ext, DWORD n, LPWSTR buf, LPWSTR* filePart) {
  std::string r;
  std::string p = Narrow(path), f = Narrow(file), e = Narrow(ext);
  if (!DoSearchPath(path ? p.c_str() : NULL, f.c_str(), ext ? e.c_str() : NULL, &r)) return 0;
  DWORD k = CopyOut(Widen(r.c_str()), buf, n);
  if (filePart && k < n) *filePart = wcsrchr(buf, L'/') + 1;
  return k;
}

// ---------------------------------------------------------------------
// The 16-bit file functions, with HFILE as a descriptor.

HFILE WINAPI _lopen(LPCSTR name, int mode) {
  int o = (mode & 3) == 1 ? O_WRONLY : (mode & 3) == 2 ? O_RDWR : O_RDONLY;
  int fd = open(PosixPath(name).c_str(), o | O_CLOEXEC);
  if (fd < 0) SetErrorFromErrno();
  return (HFILE)fd;
}

HFILE WINAPI _lcreat(LPCSTR name, int attr) {
  int fd = open(PosixPath(name).c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, (attr & 1) ? 0444 : 0666);
  if (fd < 0) SetErrorFromErrno();
  return (HFILE)fd;
}

UINT WINAPI _lread(HFILE h, LPVOID b, UINT n) {
  ssize_t r = read_fd((int)h, b, n);
  return r < 0 ? (UINT)HFILE_ERROR : (UINT)r;
}

UINT WINAPI _lwrite(HFILE h, LPCSTR b, UINT n) {
  ssize_t r = write((int)h, b, n);
  return r < 0 ? (UINT)HFILE_ERROR : (UINT)r;
}

LONG WINAPI _llseek(HFILE h, LONG off, int origin) { return (LONG)lseek((int)h, off, origin); }

HFILE WINAPI _lclose(HFILE h) { return close((int)h) == 0 ? 0 : HFILE_ERROR; }

}  // extern "C"
