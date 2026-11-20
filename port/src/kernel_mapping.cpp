// kernel_mapping.cpp - file mappings: CreateFileMapping, MapViewOfFile
// and the rest, over mmap. A named mapping without a file is POSIX shared
// memory, so other processes can open it by name.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"
#include "file.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <map>
#include <string>

namespace winport {
namespace {

struct Mapping : Object {
  int fd = -1;
  ULONGLONG size = 0;
  DWORD protect = PAGE_READONLY;
  std::wstring name;     // empty if unnamed
  std::string shmName;   // the shared memory object this process made
  Mapping() : Object(kOther) {}
  ~Mapping() override;
};

pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
std::map<std::wstring, Mapping*> g_named;  // this process's named mappings
struct View {
  void* base;
  size_t length;
};
std::map<char*, View> g_views;  // by the address MapViewOfFile gave

Mapping::~Mapping() {
  pthread_mutex_lock(&g_lock);
  auto it = g_named.find(name);
  if (!name.empty() && it != g_named.end() && it->second == this) g_named.erase(it);
  pthread_mutex_unlock(&g_lock);
  if (!shmName.empty()) shm_unlink(shmName.c_str());
  if (fd >= 0) close(fd);
}

Mapping* GetMapping(HANDLE h) {
  Object* o = FromHandle(h, kOther);
  Mapping* m = o ? dynamic_cast<Mapping*>(o) : NULL;
  if (m == NULL) SetLastError(ERROR_INVALID_HANDLE);
  return m;
}

// The shared memory name for a mapping name: short enough for every
// system (macOS allows 31 characters), and per user.
std::string ShmName(const std::wstring& name) {
  // "Global\" and "Local\" prefixes name the same thing here.
  std::wstring n = name;
  if (n.compare(0, 7, L"Global\\") == 0) n = n.substr(7);
  else if (n.compare(0, 6, L"Local\\") == 0) n = n.substr(6);
  unsigned long long h = 1469598103934665603ULL;
  for (wchar_t c : n) {
    h ^= (unsigned)c;
    h *= 1099511628211ULL;
  }
  char buf[40];
  snprintf(buf, sizeof buf, "/wpm%x.%016llx", (unsigned)getuid(), h);
  return buf;
}

bool Writable(DWORD protect) {
  DWORD p = protect & 0xFF;
  return p == PAGE_READWRITE || p == PAGE_EXECUTE_READWRITE;
}

HANDLE Create(HANDLE hFile, LPSECURITY_ATTRIBUTES sa, DWORD protect, DWORD hi, DWORD lo, const wchar_t* name) {
  ULONGLONG size = ((ULONGLONG)hi << 32) | lo;
  std::wstring wname = name ? name : L"";
  DWORD p = protect & 0xFF;
  if (p != PAGE_READONLY && p != PAGE_READWRITE && p != PAGE_WRITECOPY && p != PAGE_EXECUTE_READ &&
      p != PAGE_EXECUTE_READWRITE && p != PAGE_EXECUTE_WRITECOPY) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  if (!wname.empty()) {
    // An existing one in this process: another handle to it.
    pthread_mutex_lock(&g_lock);
    auto it = g_named.find(wname);
    if (it != g_named.end()) {
      Mapping* m = it->second;
      m->refs.fetch_add(1);
      pthread_mutex_unlock(&g_lock);
      SetLastError(ERROR_ALREADY_EXISTS);
      return NewHandle(m);
    }
    pthread_mutex_unlock(&g_lock);
  }
  Mapping* m = new Mapping();
  m->protect = protect;
  m->name = wname;
  bool existed = false;
  if (hFile == INVALID_HANDLE_VALUE || hFile == NULL) {
    if (size == 0) {
      Release(m);
      SetLastError(ERROR_INVALID_PARAMETER);
      return NULL;
    }
    if (!wname.empty()) {
      std::string sn = ShmName(wname);
      m->fd = shm_open(sn.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
      if (m->fd >= 0) {
        m->shmName = sn;
      } else if (errno == EEXIST) {
        m->fd = shm_open(sn.c_str(), O_RDWR, 0600);
        existed = true;
      }
    } else {
      // Unnamed: a shared memory object nobody else can find.
      char buf[48];
      static std::atomic<unsigned> serial{0};
      snprintf(buf, sizeof buf, "/wpm%x.%x.%x", (unsigned)getpid(), serial.fetch_add(1), (unsigned)getuid());
      m->fd = shm_open(buf, O_RDWR | O_CREAT | O_EXCL, 0600);
      if (m->fd >= 0) shm_unlink(buf);
    }
    if (m->fd < 0) {
      SetErrorFromErrno();
      Release(m);
      return NULL;
    }
    struct stat st;
    if (existed && fstat(m->fd, &st) == 0 && st.st_size > 0) {
      m->size = (ULONGLONG)st.st_size;
    } else {
      if (ftruncate(m->fd, (off_t)size) != 0) {
        SetErrorFromErrno();
        Release(m);
        return NULL;
      }
      m->size = size;
    }
  } else {
    int ffd = FileDescriptor(hFile);
    if (ffd < 0) {
      Release(m);
      SetLastError(ERROR_INVALID_HANDLE);
      return NULL;
    }
    struct stat st;
    if (fstat(ffd, &st) != 0) {
      SetErrorFromErrno();
      Release(m);
      return NULL;
    }
    if (size == 0) size = (ULONGLONG)st.st_size;
    if (size == 0) {
      // Nothing to map.
      Release(m);
      SetLastError(1006);  // ERROR_FILE_INVALID
      return NULL;
    }
    if (size > (ULONGLONG)st.st_size) {
      // A larger mapping makes the file larger, if it may be written.
      if (!Writable(protect) || ftruncate(ffd, (off_t)size) != 0) {
        Release(m);
        SetLastError(Writable(protect) ? ERROR_DISK_FULL : ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
      }
    }
    m->fd = dup(ffd);
    m->size = size;
  }
  SetObjectSecurity(m, sa);
  if (!wname.empty()) {
    pthread_mutex_lock(&g_lock);
    g_named[wname] = m;
    pthread_mutex_unlock(&g_lock);
  }
  SetLastError(existed ? ERROR_ALREADY_EXISTS : 0);
  return NewHandle(m);
}

HANDLE Open(const wchar_t* name) {
  if (name == NULL || !*name) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  std::wstring wname = name;
  pthread_mutex_lock(&g_lock);
  auto it = g_named.find(wname);
  if (it != g_named.end()) {
    Mapping* m = it->second;
    m->refs.fetch_add(1);
    pthread_mutex_unlock(&g_lock);
    return NewHandle(m);
  }
  pthread_mutex_unlock(&g_lock);
  // Another process's.
  int fd = shm_open(ShmName(wname).c_str(), O_RDWR, 0600);
  struct stat st;
  if (fd < 0 || fstat(fd, &st) != 0) {
    if (fd >= 0) close(fd);
    SetLastError(ERROR_FILE_NOT_FOUND);
    return NULL;
  }
  Mapping* m = new Mapping();
  m->fd = fd;
  m->size = (ULONGLONG)st.st_size;
  m->protect = PAGE_READWRITE;
  m->name = wname;
  pthread_mutex_lock(&g_lock);
  g_named[wname] = m;
  pthread_mutex_unlock(&g_lock);
  return NewHandle(m);
}

}  // namespace
}  // namespace winport

using namespace winport;

extern "C" {

HANDLE WINAPI CreateFileMappingW(HANDLE hFile, LPSECURITY_ATTRIBUTES sa, DWORD protect, DWORD hi, DWORD lo,
                                 LPCWSTR name) {
  return Create(hFile, sa, protect, hi, lo, name);
}
HANDLE WINAPI CreateFileMappingA(HANDLE hFile, LPSECURITY_ATTRIBUTES sa, DWORD protect, DWORD hi, DWORD lo,
                                 LPCSTR name) {
  std::wstring w = name ? Widen(name) : std::wstring();
  return Create(hFile, sa, protect, hi, lo, name ? w.c_str() : NULL);
}
HANDLE WINAPI OpenFileMappingW(DWORD, BOOL, LPCWSTR name) { return Open(name); }
HANDLE WINAPI OpenFileMappingA(DWORD, BOOL, LPCSTR name) {
  std::wstring w = name ? Widen(name) : std::wstring();
  return Open(name ? w.c_str() : NULL);
}

LPVOID WINAPI MapViewOfFileEx(HANDLE h, DWORD access, DWORD hi, DWORD lo, SIZE_T bytes, LPVOID want) {
  Mapping* m = GetMapping(h);
  if (m == NULL) return NULL;
  ULONGLONG offset = ((ULONGLONG)hi << 32) | lo;
  if (offset % 65536 != 0 || offset > m->size) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  if (bytes == 0) bytes = (SIZE_T)(m->size - offset);
  if (offset + bytes > m->size) {
    SetLastError(ERROR_ACCESS_DENIED);
    return NULL;
  }
  int prot = PROT_READ;
  int flags = MAP_SHARED;
  if (access & FILE_MAP_COPY && !(access & FILE_MAP_WRITE)) {
    prot |= PROT_WRITE;
    flags = MAP_PRIVATE;
  } else if (access & FILE_MAP_WRITE) {
    if (!Writable(m->protect)) {
      SetLastError(ERROR_ACCESS_DENIED);
      return NULL;
    }
    prot |= PROT_WRITE;
  }
  if ((m->protect & 0xFF) == PAGE_WRITECOPY || (m->protect & 0xFF) == PAGE_EXECUTE_WRITECOPY) {
    if (access & FILE_MAP_WRITE) prot |= PROT_WRITE;
    flags = MAP_PRIVATE;
  }
  if (access & FILE_MAP_EXECUTE) prot |= PROT_EXEC;
  void* p = mmap(want, bytes, prot, flags | (want ? MAP_FIXED : 0), m->fd, (off_t)offset);
  if (p == MAP_FAILED) {
    SetErrorFromErrno();
    return NULL;
  }
  pthread_mutex_lock(&g_lock);
  g_views[(char*)p] = View{p, bytes};
  pthread_mutex_unlock(&g_lock);
  return p;
}
LPVOID WINAPI MapViewOfFile(HANDLE h, DWORD access, DWORD hi, DWORD lo, SIZE_T bytes) {
  return MapViewOfFileEx(h, access, hi, lo, bytes, NULL);
}

// The view an address falls in, with the lock held.
static std::map<char*, View>::iterator FindView(LPCVOID addr) {
  auto it = g_views.upper_bound((char*)addr);
  if (it == g_views.begin()) return g_views.end();
  --it;
  if ((char*)addr < it->first + it->second.length) return it;
  return g_views.end();
}

BOOL WINAPI UnmapViewOfFile(LPCVOID addr) {
  pthread_mutex_lock(&g_lock);
  auto it = FindView(addr);
  if (it == g_views.end()) {
    pthread_mutex_unlock(&g_lock);
    SetLastError(487);  // ERROR_INVALID_ADDRESS
    return FALSE;
  }
  View v = it->second;
  g_views.erase(it);
  pthread_mutex_unlock(&g_lock);
  munmap(v.base, v.length);
  return TRUE;
}

BOOL WINAPI FlushViewOfFile(LPCVOID addr, SIZE_T bytes) {
  pthread_mutex_lock(&g_lock);
  auto it = FindView(addr);
  if (it == g_views.end()) {
    pthread_mutex_unlock(&g_lock);
    SetLastError(487);
    return FALSE;
  }
  View v = it->second;
  pthread_mutex_unlock(&g_lock);
  // From the page addr is on to the end of the range, or of the view.
  long page = sysconf(_SC_PAGESIZE);
  char* start = (char*)((UINT_PTR)addr & ~(UINT_PTR)(page - 1));
  char* end = bytes ? (char*)addr + bytes : (char*)v.base + v.length;
  if (end > (char*)v.base + v.length) end = (char*)v.base + v.length;
  if (msync(start, (size_t)(end - start), MS_SYNC) != 0) {
    SetErrorFromErrno();
    return FALSE;
  }
  return TRUE;
}

}  // extern "C"
