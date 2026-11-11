// kernel_misc.cpp - kernel32 errors and debugging, messages, the
// environment, system information, initialization files and atoms.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#include <map>
#include <mutex>
#include <string>
#include <vector>

using namespace winport;

// ---------------------------------------------------------------------
// Errors and debugging

namespace {
UINT g_errorMode = 0;
}

extern "C" {

UINT WINAPI SetErrorMode(UINT m) {
  UINT old = g_errorMode;
  g_errorMode = m;
  return old;
}

UINT WINAPI GetErrorMode(void) { return g_errorMode; }

// Debug output goes to standard error when WINPORT_DEBUG is set, as a
// debugger would show it.
void WINAPI OutputDebugStringA(LPCSTR s) {
  static const bool on = [] {
    const char* e = getenv("WINPORT_DEBUG");
    return e && *e && strcmp(e, "0") != 0;
  }();
  if (on && s) fputs(s, stderr);
}

void WINAPI OutputDebugStringW(LPCWSTR s) {
  if (s) OutputDebugStringA(Narrow(s).c_str());
}

void WINAPI DebugBreak(void) { raise(SIGTRAP); }

BOOL WINAPI IsDebuggerPresent(void) {
#ifdef __APPLE__
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
  struct kinfo_proc info;
  size_t n = sizeof info;
  memset(&info, 0, sizeof info);
  if (sysctl(mib, 4, &info, &n, NULL, 0) != 0) return FALSE;
  return (info.kp_proc.p_flag & P_TRACED) != 0;
#else
  FILE* f = fopen("/proc/self/status", "r");
  if (!f) return FALSE;
  char line[256];
  BOOL r = FALSE;
  while (fgets(line, sizeof line, f))
    if (strncmp(line, "TracerPid:", 10) == 0) r = atoi(line + 10) != 0;
  fclose(f);
  return r;
#endif
}

void WINAPI FatalAppExitA(UINT, LPCSTR msg) {
  fprintf(stderr, "%s\n", msg ? msg : "");
  abort();
}

void WINAPI FatalAppExitW(UINT u, LPCWSTR msg) { FatalAppExitA(u, Narrow(msg).c_str()); }

// There are no structured exceptions; a raised one ends the program as
// an unhandled one would.
void WINAPI RaiseException(DWORD code, DWORD, DWORD, const ULONG_PTR*) {
  fprintf(stderr, "Unhandled exception 0x%08x\n", code);
  abort();
}

}  // extern "C"

// ---------------------------------------------------------------------
// Messages

namespace {

// System messages for the codes programs show most.
const char* SystemMessage(DWORD uid) {
  LONG id = (LONG)uid;
  switch (id) {
    case ERROR_SUCCESS: return "The operation completed successfully.";
    case ERROR_INVALID_FUNCTION: return "Incorrect function.";
    case ERROR_FILE_NOT_FOUND: return "The system cannot find the file specified.";
    case ERROR_PATH_NOT_FOUND: return "The system cannot find the path specified.";
    case ERROR_TOO_MANY_OPEN_FILES: return "The system cannot open the file.";
    case ERROR_ACCESS_DENIED: return "Access is denied.";
    case ERROR_INVALID_HANDLE: return "The handle is invalid.";
    case ERROR_NOT_ENOUGH_MEMORY: return "Not enough storage is available to process this command.";
    case ERROR_OUTOFMEMORY: return "Not enough storage is available to complete this operation.";
    case ERROR_INVALID_DRIVE: return "The system cannot find the drive specified.";
    case ERROR_NO_MORE_FILES: return "There are no more files.";
    case ERROR_WRITE_PROTECT: return "The media is write protected.";
    case ERROR_SHARING_VIOLATION:
      return "The process cannot access the file because it is being used by another process.";
    case ERROR_LOCK_VIOLATION:
      return "The process cannot access the file because another process has locked a portion of the file.";
    case ERROR_HANDLE_EOF: return "Reached the end of the file.";
    case ERROR_HANDLE_DISK_FULL: case ERROR_DISK_FULL: return "There is not enough space on the disk.";
    case ERROR_NOT_SUPPORTED: return "The request is not supported.";
    case ERROR_FILE_EXISTS: return "The file exists.";
    case ERROR_INVALID_PARAMETER: return "The parameter is incorrect.";
    case ERROR_BROKEN_PIPE: return "The pipe has been ended.";
    case ERROR_CALL_NOT_IMPLEMENTED: return "This function is not supported on this system.";
    case ERROR_INSUFFICIENT_BUFFER: return "The data area passed to a system call is too small.";
    case ERROR_INVALID_NAME: return "The filename, directory name, or volume label syntax is incorrect.";
    case ERROR_MOD_NOT_FOUND: return "The specified module could not be found.";
    case ERROR_PROC_NOT_FOUND: return "The specified procedure could not be found.";
    case ERROR_DIR_NOT_EMPTY: return "The directory is not empty.";
    case ERROR_ALREADY_EXISTS: return "Cannot create a file when that file already exists.";
    case ERROR_FILENAME_EXCED_RANGE: return "The filename or extension is too long.";
    case ERROR_NO_MORE_ITEMS: return "No more data is available.";
    case ERROR_MORE_DATA: return "More data is available.";
    case ERROR_RESOURCE_NAME_NOT_FOUND: return "The specified resource name cannot be found in the image file.";
    case ERROR_RESOURCE_TYPE_NOT_FOUND: return "The specified resource type cannot be found in the image file.";
    case E_NOTIMPL: return "Not implemented";
    case E_NOINTERFACE: return "No such interface supported";
    case E_POINTER: return "Invalid pointer";
    case E_ABORT: return "Operation aborted";
    case E_FAIL: return "Unspecified error";
    case E_UNEXPECTED: return "Catastrophic failure";
    case E_ACCESSDENIED: return "General access denied error";
    case E_HANDLE: return "Invalid handle";
    case E_OUTOFMEMORY: return "Ran out of memory";
    case E_INVALIDARG: return "The parameter is incorrect.";
    case CLASS_E_NOAGGREGATION: return "Class does not support aggregation (or class object is remote)";
    case CLASS_E_CLASSNOTAVAILABLE: return "ClassFactory cannot supply requested class";
    case REGDB_E_CLASSNOTREG: return "Class not registered";
    case CO_E_NOTINITIALIZED: return "CoInitialize has not been called.";
    case DISP_E_UNKNOWNNAME: return "Unknown name.";
    case DISP_E_MEMBERNOTFOUND: return "Member not found.";
    case DISP_E_TYPEMISMATCH: return "Type mismatch.";
    case DISP_E_BADPARAMCOUNT: return "Invalid number of parameters.";
    case DISP_E_EXCEPTION: return "Exception occurred.";
    case DISP_E_OVERFLOW: return "Out of present range.";
    case DISP_E_PARAMNOTFOUND: return "Parameter not found.";
    case DISP_E_BADVARTYPE: return "Bad variable type.";
    case STG_E_FILENOTFOUND: return "%1 could not be found.";
    case STG_E_ACCESSDENIED: return "Access Denied.";
    default: return NULL;
  }
}

// A message table resource: blocks of ids, then entries of a length,
// a flag for Unicode, and the text.
template <class C>
bool TableMessage(HMODULE h, DWORD id, std::basic_string<C>* out) {
  HRSRC r = FindResourceA(h, MAKEINTRESOURCEA(1), (LPCSTR)RT_MESSAGETABLE);
  if (!r) return false;
  const BYTE* p = (const BYTE*)LockResource(LoadResource(h, r));
  DWORD size = SizeofResource(h, r);
  auto le32 = [](const BYTE* q) { return (DWORD)q[0] | (q[1] << 8) | (q[2] << 16) | ((DWORD)q[3] << 24); };
  auto le16 = [](const BYTE* q) { return (WORD)(q[0] | (q[1] << 8)); };
  if (size < 4) return false;
  DWORD blocks = le32(p);
  for (DWORD b = 0; b < blocks && 4 + b * 12 + 12 <= size; b++) {
    const BYTE* blk = p + 4 + b * 12;
    DWORD lo = le32(blk), hi = le32(blk + 4), off = le32(blk + 8);
    if (id < lo || id > hi) continue;
    const BYTE* e = p + off;
    for (DWORD k = lo; k < id; k++) e += le16(e);
    WORD len = le16(e), flags = le16(e + 2);
    const BYTE* text = e + 4;
    out->clear();
    if (flags & 1) {
      for (const BYTE* q = text; q + 1 < e + len && le16(q); q += 2) out->push_back((C)le16(q));
    } else {
      for (const BYTE* q = text; q < e + len && *q; q++) out->push_back((C)*q);
    }
    return true;
  }
  return false;
}

template <class C>
std::basic_string<C> FromNarrow(const char* s);
template <>
std::string FromNarrow(const char* s) { return s; }
template <>
std::wstring FromNarrow(const char* s) { return Widen(s); }


// One insert's argument.
struct Arg {
  ULONG_PTR v = 0;
  ULONGLONG v64 = 0;
};

template <class C>
DWORD Format(DWORD flags, const C* src, std::basic_string<C>* out, va_list* args, bool narrowArgs) {
  const bool ignore = (flags & FORMAT_MESSAGE_IGNORE_INSERTS) != 0;
  const bool array = (flags & FORMAT_MESSAGE_ARGUMENT_ARRAY) != 0;
  const DWORD width = flags & FORMAT_MESSAGE_MAX_WIDTH_MASK;
  // The first pass finds each insert's printf form, to read the
  // arguments in order with the right sizes.
  std::map<int, std::string> specs;
  int maxArg = 0;
  for (const C* p = src; *p; p++) {
    if (*p != '%' || ignore) continue;
    if (p[1] >= '1' && p[1] <= '9') {
      const C* q = p + 1;
      int n = 0;
      while (*q >= '0' && *q <= '9' && n < 10) n = n * 10 + (*q++ - '0');
      std::string spec = "s";
      if (*q == '!') {
        const C* e = q + 1;
        while (*e && *e != '!') e++;
        spec.clear();
        for (const C* k = q + 1; k < e; k++) spec += (char)*k;
        q = *e ? e + 1 : e;
      }
      specs[n] = spec;
      if (n > maxArg) maxArg = n;
      p = q - 1;
    } else if (p[1]) {
      p++;
    }
  }
  std::vector<Arg> vals(maxArg + 1);
  if (!ignore && maxArg > 0) {
    if (!args) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return 0;
    }
    for (int i = 1; i <= maxArg; i++) {
      const std::string& s = specs.count(i) ? specs[i] : std::string("s");
      bool is64 = s.find("I64") != std::string::npos || s.find("ll") != std::string::npos;
      if (array) {
        const ULONG_PTR* a = (const ULONG_PTR*)args;
        vals[i].v = a[i - 1];
        vals[i].v64 = a[i - 1];
      } else if (is64) {
        vals[i].v64 = va_arg(*args, ULONGLONG);
      } else if (s.find_first_of("sSp") != std::string::npos) {
        vals[i].v = va_arg(*args, ULONG_PTR);
      } else {
        vals[i].v = va_arg(*args, unsigned int);
      }
    }
  }
  std::basic_string<C> r;
  size_t lineStart = 0;
  for (const C* p = src; *p; p++) {
    if (*p == '%' && p[1]) {
      C c = p[1];
      if (c >= '1' && c <= '9' && !ignore) {
        const C* q = p + 1;
        int n = 0;
        while (*q >= '0' && *q <= '9' && n < 10) n = n * 10 + (*q++ - '0');
        if (*q == '!') {
          q++;
          while (*q && *q != '!') q++;
          if (*q) q++;
        }
        std::string spec = specs[n];
        std::string fmt = "%" + spec;
        if (spec.find_first_of("sS") != std::string::npos) {
          // Strings: the caller's kind, or the other with S.
          bool wide = (sizeof(C) == sizeof(wchar_t)) != (spec.find('S') != std::string::npos);
          if (narrowArgs) wide = false;
          const void* s = (const void*)vals[n].v;
          std::string ns = s ? (wide ? Narrow((const wchar_t*)s) : std::string((const char*)s)) : "(null)";
          for (char& k : fmt)
            if (k == 'S') k = 's';
          char buf[1024];
          snprintf(buf, sizeof buf, fmt.c_str(), ns.c_str());
          r += FromNarrow<C>(buf);
        } else {
          std::string f;
          for (size_t i = 0; i < fmt.size(); i++) {
            if (fmt.compare(i, 3, "I64") == 0) {
              f += "ll";
              i += 2;
            } else {
              f += fmt[i];
            }
          }
          char buf[256];
          if (spec.find("I64") != std::string::npos || spec.find("ll") != std::string::npos)
            snprintf(buf, sizeof buf, f.c_str(), (long long)vals[n].v64);
          else if (spec.find('p') != std::string::npos)
            snprintf(buf, sizeof buf, f.c_str(), (void*)vals[n].v);
          else
            snprintf(buf, sizeof buf, f.c_str(), (unsigned int)vals[n].v);
          r += FromNarrow<C>(buf);
        }
        p = q - 1;
        continue;
      }
      if (ignore && c != '0') {
        r += *p;
        continue;
      }
      p++;
      switch (c) {
        case '0': *out = r; return (DWORD)r.size();
        case 'n': r += '\r'; r += '\n'; lineStart = r.size(); break;
        case 'r': r += '\r'; break;
        case 't': r += '\t'; break;
        case 'b': r += ' '; break;
        default: r += c; break;
      }
      continue;
    }
    if (width) {
      // With a width, line breaks in the source are spaces, and lines
      // wrap at the width.
      if (*p == '\r' || *p == '\n') {
        if (*p == '\r' && p[1] == '\n') p++;
        r += ' ';
        continue;
      }
      if (width != FORMAT_MESSAGE_MAX_WIDTH_MASK && r.size() - lineStart >= width) {
        size_t sp = r.find_last_of(' ');
        if (sp != std::basic_string<C>::npos && sp > lineStart) {
          r.replace(sp, 1, 1, '\r');
          r.insert(sp + 1, 1, '\n');
          lineStart = sp + 2;
        }
      }
    }
    r += *p;
  }
  *out = r;
  return (DWORD)r.size();
}

template <class C>
DWORD FormatMessageT(DWORD flags, LPCVOID source, DWORD id, DWORD, C* buf, DWORD size, va_list* args) {
  std::basic_string<C> src;
  if (flags & FORMAT_MESSAGE_FROM_STRING) {
    if (!source) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return 0;
    }
    src = (const C*)source;
  } else {
    bool found = false;
    if (flags & FORMAT_MESSAGE_FROM_HMODULE) found = TableMessage<C>((HMODULE)source, id, &src);
    if (!found && (flags & FORMAT_MESSAGE_FROM_SYSTEM)) {
      const char* m = SystemMessage(id);
      if (m) {
        src = FromNarrow<C>(m);
        if (!(flags & FORMAT_MESSAGE_MAX_WIDTH_MASK)) src += FromNarrow<C>("\r\n");
        found = true;
      }
    }
    if (!found) {
      SetLastError(ERROR_MR_MID_NOT_FOUND);
      return 0;
    }
  }
  std::basic_string<C> r;
  if (Format<C>(flags, src.c_str(), &r, args, false) == 0 && !r.empty()) return 0;
  if (flags & FORMAT_MESSAGE_ALLOCATE_BUFFER) {
    size_t n = r.size() + 1 > size ? r.size() + 1 : size;
    C* p = (C*)LocalAlloc(LMEM_FIXED, n * sizeof(C));
    if (!p) return 0;
    memcpy(p, r.c_str(), (r.size() + 1) * sizeof(C));
    *(C**)buf = p;
    return (DWORD)r.size();
  }
  if (!buf || r.size() + 1 > size) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return 0;
  }
  memcpy(buf, r.c_str(), (r.size() + 1) * sizeof(C));
  return (DWORD)r.size();
}

}  // namespace

extern "C" {

DWORD WINAPI FormatMessageA(DWORD flags, LPCVOID source, DWORD id, DWORD lang, LPSTR buf, DWORD size,
                            va_list* args) {
  return FormatMessageT<char>(flags, source, id, lang, buf, size, args);
}

DWORD WINAPI FormatMessageW(DWORD flags, LPCVOID source, DWORD id, DWORD lang, LPWSTR buf, DWORD size,
                            va_list* args) {
  return FormatMessageT<wchar_t>(flags, source, id, lang, buf, size, args);
}

// ---------------------------------------------------------------------
// The environment

DWORD WINAPI GetEnvironmentVariableA(LPCSTR name, LPSTR buf, DWORD n) {
  const char* v = name ? getenv(name) : NULL;
  if (!v) {
    SetLastError(ERROR_ENVVAR_NOT_FOUND);
    return 0;
  }
  return CopyOut(std::string(v), buf, n);
}

DWORD WINAPI GetEnvironmentVariableW(LPCWSTR name, LPWSTR buf, DWORD n) {
  const char* v = name ? getenv(Narrow(name).c_str()) : NULL;
  if (!v) {
    SetLastError(ERROR_ENVVAR_NOT_FOUND);
    return 0;
  }
  return CopyOut(Widen(v), buf, n);
}

BOOL WINAPI SetEnvironmentVariableA(LPCSTR name, LPCSTR value) {
  if (!name) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  int r = value ? setenv(name, value, 1) : unsetenv(name);
  if (r != 0) {
    SetErrorFromErrno();
    return FALSE;
  }
  return TRUE;
}

BOOL WINAPI SetEnvironmentVariableW(LPCWSTR name, LPCWSTR value) {
  std::string v = Narrow(value);
  return SetEnvironmentVariableA(name ? Narrow(name).c_str() : NULL, value ? v.c_str() : NULL);
}

}  // extern "C"

namespace {
std::string Expand(const std::string& s) {
  std::string r;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%') {
      size_t e = s.find('%', i + 1);
      if (e != std::string::npos) {
        std::string name = s.substr(i + 1, e - i - 1);
        const char* v = name.empty() ? NULL : getenv(name.c_str());
        if (v) {
          r += v;
          i = e;
          continue;
        }
      }
    }
    r += s[i];
  }
  return r;
}
}  // namespace

extern "C" {

// The size is in characters with the zero, whether or not it fits.
DWORD WINAPI ExpandEnvironmentStringsA(LPCSTR s, LPSTR buf, DWORD n) {
  std::string r = Expand(s ? s : "");
  if (buf && n > r.size()) memcpy(buf, r.c_str(), r.size() + 1);
  return (DWORD)r.size() + 1;
}

DWORD WINAPI ExpandEnvironmentStringsW(LPCWSTR s, LPWSTR buf, DWORD n) {
  std::wstring r = Widen(Expand(Narrow(s)).c_str());
  if (buf && n > r.size()) wmemcpy(buf, r.c_str(), r.size() + 1);
  return (DWORD)r.size() + 1;
}

BOOL WINAPI GetComputerNameA(LPSTR buf, LPDWORD n) {
  char host[256];
  if (gethostname(host, sizeof host) != 0) strcpy(host, "localhost");
  host[sizeof host - 1] = 0;
  char* dot = strchr(host, '.');
  if (dot) *dot = 0;
  std::string h(host);
  for (char& c : h)
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
  if (h.size() > MAX_COMPUTERNAME_LENGTH) h.resize(MAX_COMPUTERNAME_LENGTH);
  if (*n <= h.size()) {
    *n = (DWORD)h.size() + 1;
    SetLastError(ERROR_BUFFER_OVERFLOW);
    return FALSE;
  }
  memcpy(buf, h.c_str(), h.size() + 1);
  *n = (DWORD)h.size();
  return TRUE;
}

BOOL WINAPI GetComputerNameW(LPWSTR buf, LPDWORD n) {
  char tmp[MAX_COMPUTERNAME_LENGTH + 1];
  DWORD k = sizeof tmp;
  GetComputerNameA(tmp, &k);
  std::wstring w = Widen(tmp);
  if (*n <= w.size()) {
    *n = (DWORD)w.size() + 1;
    SetLastError(ERROR_BUFFER_OVERFLOW);
    return FALSE;
  }
  wmemcpy(buf, w.c_str(), w.size() + 1);
  *n = (DWORD)w.size();
  return TRUE;
}

// ---------------------------------------------------------------------
// The system. Programs see Windows XP SP3, the version MFC 6's era of
// programs know, on the NT platform.

DWORD WINAPI GetVersion(void) { return 5 | (1 << 8) | (2600u << 16); }

BOOL WINAPI GetVersionExA(LPOSVERSIONINFOA v) {
  if (v->dwOSVersionInfoSize < sizeof(OSVERSIONINFOA)) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  v->dwMajorVersion = 5;
  v->dwMinorVersion = 1;
  v->dwBuildNumber = 2600;
  v->dwPlatformId = VER_PLATFORM_WIN32_NT;
  strcpy(v->szCSDVersion, "Service Pack 3");
  return TRUE;
}

BOOL WINAPI GetVersionExW(LPOSVERSIONINFOW v) {
  if (v->dwOSVersionInfoSize < sizeof(OSVERSIONINFOW)) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  v->dwMajorVersion = 5;
  v->dwMinorVersion = 1;
  v->dwBuildNumber = 2600;
  v->dwPlatformId = VER_PLATFORM_WIN32_NT;
  wcscpy(v->szCSDVersion, L"Service Pack 3");
  return TRUE;
}

void WINAPI GetSystemInfo(LPSYSTEM_INFO si) {
  memset(si, 0, sizeof(*si));
#if defined(__x86_64__)
  si->wProcessorArchitecture = 9;  // AMD64
  si->dwProcessorType = 8664;
#elif defined(__i386__)
  si->wProcessorArchitecture = 0;  // INTEL
  si->dwProcessorType = 586;
#elif defined(__aarch64__)
  si->wProcessorArchitecture = 12;  // ARM64
#elif defined(__arm__)
  si->wProcessorArchitecture = 5;  // ARM
#elif defined(__powerpc__) || defined(__ppc__)
  si->wProcessorArchitecture = 3;  // PPC
  si->dwProcessorType = 604;
#endif
  si->dwPageSize = (DWORD)sysconf(_SC_PAGESIZE);
  si->lpMinimumApplicationAddress = (LPVOID)0x10000;
  si->lpMaximumApplicationAddress = sizeof(void*) == 8 ? (LPVOID)0x7ffffffeffffULL : (LPVOID)0x7ffeffff;
  long n = sysconf(_SC_NPROCESSORS_ONLN);
  si->dwNumberOfProcessors = n > 0 ? (DWORD)n : 1;
  si->dwActiveProcessorMask = si->dwNumberOfProcessors >= sizeof(DWORD_PTR) * 8
                                  ? ~(DWORD_PTR)0
                                  : (((DWORD_PTR)1 << si->dwNumberOfProcessors) - 1);
  si->dwAllocationGranularity = 65536;
  si->wProcessorLevel = 6;
}

}  // extern "C"

// ---------------------------------------------------------------------
// Initialization files. A name without a directory is in the layer's
// configuration directory, ~/.config/winport (or $XDG_CONFIG_HOME); the
// profile functions without "Private" use win.ini there.

namespace {

std::recursive_mutex& IniLock() {
  static std::recursive_mutex m;
  return m;
}

std::string ConfigDir() {
  const char* x = getenv("XDG_CONFIG_HOME");
  std::string d;
  if (x && *x) {
    d = x;
  } else {
    const char* h = getenv("HOME");
    d = std::string(h && *h ? h : "/tmp") + "/.config";
  }
  d += "/winport";
  return d;
}

std::string IniPath(const char* file) {
  std::string f = PosixPath(file ? file : "win.ini");
  if (f.find('/') != std::string::npos) return f;
  return ConfigDir() + "/" + f;
}

bool EqualNoCase(const std::string& a, const std::string& b) { return strcasecmp(a.c_str(), b.c_str()) == 0; }

std::string Trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return std::string();
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

// A file kept as its lines, so writing changes only what it must.
struct Ini {
  std::vector<std::string> lines;
  bool Load(const std::string& path) {
    lines.clear();
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string cur;
    int c;
    while ((c = fgetc(f)) != EOF) {
      if (c == '\n') {
        if (!cur.empty() && cur.back() == '\r') cur.pop_back();
        lines.push_back(cur);
        cur.clear();
      } else {
        cur += (char)c;
      }
    }
    if (!cur.empty()) lines.push_back(cur);
    fclose(f);
    return true;
  }
  bool Save(const std::string& path) {
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) {
      // Makes the directories on the way.
      std::string d = path.substr(0, slash);
      for (size_t i = 1; i <= d.size(); i++)
        if (i == d.size() || d[i] == '/') mkdir(d.substr(0, i).c_str(), 0777);
    }
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    for (auto& l : lines) fprintf(f, "%s\r\n", l.c_str());
    fclose(f);
    return true;
  }
  static bool IsSection(const std::string& l, std::string* name) {
    std::string t = Trim(l);
    if (t.size() < 2 || t[0] != '[') return false;
    size_t e = t.find(']');
    if (e == std::string::npos) return false;
    *name = Trim(t.substr(1, e - 1));
    return true;
  }
  // The section's header line, or -1.
  int FindSection(const std::string& sec) const {
    std::string n;
    for (size_t i = 0; i < lines.size(); i++)
      if (IsSection(lines[i], &n) && EqualNoCase(n, sec)) return (int)i;
    return -1;
  }
  int SectionEnd(int start) const {
    std::string n;
    size_t i = (size_t)start + 1;
    while (i < lines.size() && !IsSection(lines[i], &n)) i++;
    return (int)i;
  }
  static bool KeyOf(const std::string& l, std::string* key, std::string* value) {
    std::string t = Trim(l);
    if (t.empty() || t[0] == ';') return false;
    size_t eq = t.find('=');
    if (eq == std::string::npos) return false;
    *key = Trim(t.substr(0, eq));
    if (value) *value = Trim(t.substr(eq + 1));
    return true;
  }
  std::vector<std::string> Sections() const {
    std::vector<std::string> v;
    std::string n;
    for (auto& l : lines)
      if (IsSection(l, &n)) v.push_back(n);
    return v;
  }
  std::vector<std::pair<std::string, std::string>> Keys(const std::string& sec) const {
    std::vector<std::pair<std::string, std::string>> v;
    int s = FindSection(sec);
    if (s < 0) return v;
    int e = SectionEnd(s);
    std::string k, val;
    for (int i = s + 1; i < e; i++)
      if (KeyOf(lines[i], &k, &val)) v.push_back({k, val});
    return v;
  }
  bool Get(const std::string& sec, const std::string& key, std::string* value) const {
    for (auto& kv : Keys(sec))
      if (EqualNoCase(kv.first, key)) {
        *value = kv.second;
        return true;
      }
    return false;
  }
  // A NULL value deletes the key; a NULL key, the section.
  void Set(const std::string& sec, const char* key, const char* value) {
    int s = FindSection(sec);
    if (!key) {
      if (s >= 0) lines.erase(lines.begin() + s, lines.begin() + SectionEnd(s));
      return;
    }
    if (s < 0) {
      if (!value) return;
      if (!lines.empty() && !Trim(lines.back()).empty()) lines.push_back("");
      lines.push_back("[" + sec + "]");
      lines.push_back(std::string(key) + "=" + value);
      return;
    }
    int e = SectionEnd(s);
    std::string k;
    for (int i = s + 1; i < e; i++) {
      if (KeyOf(lines[i], &k, NULL) && EqualNoCase(k, key)) {
        if (value) lines[i] = std::string(key) + "=" + value;
        else lines.erase(lines.begin() + i);
        return;
      }
    }
    if (!value) return;
    // After the section's last non-blank line.
    int at = e;
    while (at > s + 1 && Trim(lines[at - 1]).empty()) at--;
    lines.insert(lines.begin() + at, std::string(key) + "=" + value);
  }
};

// Quotes around a whole value are dropped, as Windows does.
std::string Unquote(const std::string& v) {
  if (v.size() >= 2 && (v[0] == '"' || v[0] == '\'') && v.back() == v[0]) return v.substr(1, v.size() - 2);
  return v;
}

// Copies a list of strings, each ending in a zero, with a zero after:
// the result when there is no section or no key. Too small a buffer
// gets what fits and returns the size less two.
DWORD CopyList(const std::vector<std::string>& items, char* buf, DWORD n) {
  if (!buf || n == 0) return 0;
  if (n == 1) {
    buf[0] = 0;
    return 0;
  }
  DWORD pos = 0;
  for (auto& s : items) {
    if (pos + s.size() + 1 > n - 1) {
      size_t k = n - 2 > pos ? n - 2 - pos : 0;
      memcpy(buf + pos, s.c_str(), k);
      buf[n - 2] = 0;
      buf[n - 1] = 0;
      return n - 2;
    }
    memcpy(buf + pos, s.c_str(), s.size() + 1);
    pos += (DWORD)s.size() + 1;
  }
  buf[pos] = 0;
  return pos;
}

DWORD GetString(const char* sec, const char* key, const char* def, char* buf, DWORD n, const char* file) {
  std::lock_guard<std::recursive_mutex> g(IniLock());
  Ini ini;
  ini.Load(IniPath(file));
  if (!sec) return CopyList(ini.Sections(), buf, n);
  if (!key) {
    std::vector<std::string> keys;
    for (auto& kv : ini.Keys(sec)) keys.push_back(kv.first);
    return CopyList(keys, buf, n);
  }
  std::string v;
  if (!ini.Get(sec, key, &v)) v = Trim(def ? def : "");
  v = Unquote(v);
  if (!buf || n == 0) return 0;
  if (v.size() >= n) {
    memcpy(buf, v.c_str(), n - 1);
    buf[n - 1] = 0;
    return n - 1;
  }
  memcpy(buf, v.c_str(), v.size() + 1);
  return (DWORD)v.size();
}

BOOL WriteString(const char* sec, const char* key, const char* value, const char* file) {
  if (!sec) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  std::lock_guard<std::recursive_mutex> g(IniLock());
  std::string path = IniPath(file);
  Ini ini;
  ini.Load(path);
  ini.Set(sec, key, value);
  if (!ini.Save(path)) {
    SetErrorFromErrno();
    return FALSE;
  }
  return TRUE;
}

UINT GetInt(const char* sec, const char* key, int def, const char* file) {
  char buf[64];
  if (GetString(sec, key, "", buf, sizeof buf, file) == 0) return (UINT)def;
  // Leading digits only; anything else reads as zero.
  return (UINT)strtol(buf, NULL, 10);
}

// The wide forms go through the narrow ones in UTF-8.
struct N {
  std::string s;
  const char* p;
  explicit N(LPCWSTR w) : s(Narrow(w)), p(w ? s.c_str() : NULL) {}
};

DWORD WideList(const char* tmp, DWORD got, LPWSTR buf, DWORD n, bool list) {
  if (!buf || n == 0) return 0;
  std::wstring w = list ? Widen(tmp, (int)got + 1) : Widen(tmp);
  if (w.size() + 1 > n) {
    wmemcpy(buf, w.c_str(), n - 1);
    buf[n - 1] = 0;
    if (list && n >= 2) buf[n - 2] = 0;
    return list ? n - 2 : n - 1;
  }
  wmemcpy(buf, w.c_str(), w.size());
  buf[w.size()] = 0;
  return list ? got : (DWORD)w.size();
}

}  // namespace

extern "C" {

DWORD WINAPI GetPrivateProfileStringA(LPCSTR sec, LPCSTR key, LPCSTR def, LPSTR buf, DWORD n, LPCSTR file) {
  return GetString(sec, key, def, buf, n, file);
}

DWORD WINAPI GetPrivateProfileStringW(LPCWSTR sec, LPCWSTR key, LPCWSTR def, LPWSTR buf, DWORD n, LPCWSTR file) {
  N s(sec), k(key), d(def), f(file);
  std::vector<char> tmp(n * 4 + 4);
  DWORD got = GetString(s.p, k.p, d.p, tmp.data(), (DWORD)tmp.size(), f.p);
  return WideList(tmp.data(), got, buf, n, !sec || !key);
}

DWORD WINAPI GetProfileStringA(LPCSTR sec, LPCSTR key, LPCSTR def, LPSTR buf, DWORD n) {
  return GetString(sec, key, def, buf, n, NULL);
}

DWORD WINAPI GetProfileStringW(LPCWSTR sec, LPCWSTR key, LPCWSTR def, LPWSTR buf, DWORD n) {
  return GetPrivateProfileStringW(sec, key, def, buf, n, NULL);
}

BOOL WINAPI WritePrivateProfileStringA(LPCSTR sec, LPCSTR key, LPCSTR value, LPCSTR file) {
  return WriteString(sec, key, value, file);
}

BOOL WINAPI WritePrivateProfileStringW(LPCWSTR sec, LPCWSTR key, LPCWSTR value, LPCWSTR file) {
  N s(sec), k(key), v(value), f(file);
  return WriteString(s.p, k.p, v.p, f.p);
}

BOOL WINAPI WriteProfileStringA(LPCSTR sec, LPCSTR key, LPCSTR value) { return WriteString(sec, key, value, NULL); }

BOOL WINAPI WriteProfileStringW(LPCWSTR sec, LPCWSTR key, LPCWSTR value) {
  return WritePrivateProfileStringW(sec, key, value, NULL);
}

UINT WINAPI GetPrivateProfileIntA(LPCSTR sec, LPCSTR key, INT def, LPCSTR file) { return GetInt(sec, key, def, file); }

UINT WINAPI GetPrivateProfileIntW(LPCWSTR sec, LPCWSTR key, INT def, LPCWSTR file) {
  N s(sec), k(key), f(file);
  return GetInt(s.p, k.p, def, f.p);
}

UINT WINAPI GetProfileIntA(LPCSTR sec, LPCSTR key, INT def) { return GetInt(sec, key, def, NULL); }

UINT WINAPI GetProfileIntW(LPCWSTR sec, LPCWSTR key, INT def) {
  N s(sec), k(key);
  return GetInt(s.p, k.p, def, NULL);
}

DWORD WINAPI GetPrivateProfileSectionA(LPCSTR sec, LPSTR buf, DWORD n, LPCSTR file) {
  std::lock_guard<std::recursive_mutex> g(IniLock());
  Ini ini;
  ini.Load(IniPath(file));
  std::vector<std::string> items;
  for (auto& kv : ini.Keys(sec ? sec : "")) items.push_back(kv.first + "=" + kv.second);
  return CopyList(items, buf, n);
}

DWORD WINAPI GetPrivateProfileSectionW(LPCWSTR sec, LPWSTR buf, DWORD n, LPCWSTR file) {
  N s(sec), f(file);
  std::vector<char> tmp(n * 4 + 4);
  DWORD got = GetPrivateProfileSectionA(s.p, tmp.data(), (DWORD)tmp.size(), f.p);
  return WideList(tmp.data(), got, buf, n, true);
}

// Structures are kept as hex with a checksum byte after.
BOOL WINAPI WritePrivateProfileStructA(LPCSTR sec, LPCSTR key, LPVOID data, UINT size, LPCSTR file) {
  if (!data) return WriteString(sec, key, NULL, file);
  std::string hex;
  BYTE sum = 0;
  char b[3];
  for (UINT i = 0; i < size; i++) {
    BYTE c = ((BYTE*)data)[i];
    sum = (BYTE)(sum + c);
    snprintf(b, sizeof b, "%02X", c);
    hex += b;
  }
  snprintf(b, sizeof b, "%02X", sum);
  hex += b;
  return WriteString(sec, key, hex.c_str(), file);
}

BOOL WINAPI GetPrivateProfileStructA(LPCSTR sec, LPCSTR key, LPVOID data, UINT size, LPCSTR file) {
  std::vector<char> buf(size * 2 + 8);
  DWORD n = GetString(sec, key, "", buf.data(), (DWORD)buf.size(), file);
  if (n != size * 2 + 2) return FALSE;
  BYTE sum = 0;
  auto hexv = [](char c) { return c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0'; };
  for (UINT i = 0; i < size; i++) {
    BYTE c = (BYTE)(hexv(buf[i * 2]) * 16 + hexv(buf[i * 2 + 1]));
    ((BYTE*)data)[i] = c;
    sum = (BYTE)(sum + c);
  }
  return (BYTE)(hexv(buf[size * 2]) * 16 + hexv(buf[size * 2 + 1])) == sum;
}

BOOL WINAPI WritePrivateProfileStructW(LPCWSTR sec, LPCWSTR key, LPVOID data, UINT size, LPCWSTR file) {
  N s(sec), k(key), f(file);
  return WritePrivateProfileStructA(s.p, k.p, data, size, f.p);
}

BOOL WINAPI GetPrivateProfileStructW(LPCWSTR sec, LPCWSTR key, LPVOID data, UINT size, LPCWSTR file) {
  N s(sec), k(key), f(file);
  return GetPrivateProfileStructA(s.p, k.p, data, size, f.p);
}

}  // extern "C"

// ---------------------------------------------------------------------
// Atoms. Integer atoms are below 0xC000; string atoms count up from
// there, without case. The local and global tables are one here.

namespace {

struct AtomEntry {
  std::wstring name;
  int refs;
};

std::mutex& AtomLock() {
  static std::mutex m;
  return m;
}

std::map<ATOM, AtomEntry>& Atoms() {
  static std::map<ATOM, AtomEntry> m;
  return m;
}

bool IntAtom(const wchar_t* s, ATOM* a) {
  if (IS_INTRESOURCE(s)) {
    *a = (ATOM)(UINT_PTR)s;
    return true;
  }
  if (s[0] == L'#') {
    unsigned long v = wcstoul(s + 1, NULL, 10);
    *a = (ATOM)v;
    return true;
  }
  return false;
}

ATOM Lookup(const std::wstring& s) {
  for (auto& kv : Atoms())
    if (wcscasecmp(kv.second.name.c_str(), s.c_str()) == 0) return kv.first;
  return 0;
}

ATOM Add(const wchar_t* s) {
  ATOM a;
  if (IntAtom(s, &a)) {
    if (a == 0 || a >= 0xc000) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return 0;
    }
    return a;
  }
  if (wcslen(s) > 255) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  std::lock_guard<std::mutex> g(AtomLock());
  if ((a = Lookup(s)) != 0) {
    Atoms()[a].refs++;
    return a;
  }
  for (a = 0xc000; a != 0 && Atoms().count(a); a++) {
  }
  if (a == 0) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
  Atoms()[a] = AtomEntry{s, 1};
  return a;
}

ATOM FindA(const wchar_t* s) {
  ATOM a;
  if (IntAtom(s, &a)) return a;
  std::lock_guard<std::mutex> g(AtomLock());
  a = Lookup(s);
  if (!a) SetLastError(ERROR_FILE_NOT_FOUND);
  return a;
}

ATOM Delete(ATOM a) {
  if (a < 0xc000) return 0;
  std::lock_guard<std::mutex> g(AtomLock());
  auto it = Atoms().find(a);
  if (it == Atoms().end()) {
    SetLastError(ERROR_INVALID_HANDLE);
    return a;
  }
  if (--it->second.refs <= 0) Atoms().erase(it);
  return 0;
}

bool Name(ATOM a, std::wstring* out) {
  if (a == 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return false;
  }
  if (a < 0xc000) {
    *out = L"#" + std::to_wstring(a);
    return true;
  }
  std::lock_guard<std::mutex> g(AtomLock());
  auto it = Atoms().find(a);
  if (it == Atoms().end()) {
    SetLastError(ERROR_INVALID_HANDLE);
    return false;
  }
  *out = it->second.name;
  return true;
}

std::wstring AtomArg(LPCSTR s) {
  if (IS_INTRESOURCE(s)) return std::wstring();
  return Widen(s);
}

}  // namespace

extern "C" {

ATOM WINAPI GlobalAddAtomW(LPCWSTR s) { return Add(s); }
ATOM WINAPI GlobalAddAtomA(LPCSTR s) {
  if (IS_INTRESOURCE(s)) return Add((LPCWSTR)s);
  return Add(AtomArg(s).c_str());
}
ATOM WINAPI GlobalFindAtomW(LPCWSTR s) { return FindA(s); }
ATOM WINAPI GlobalFindAtomA(LPCSTR s) {
  if (IS_INTRESOURCE(s)) return FindA((LPCWSTR)s);
  return FindA(AtomArg(s).c_str());
}
ATOM WINAPI GlobalDeleteAtom(ATOM a) { return Delete(a); }

UINT WINAPI GlobalGetAtomNameW(ATOM a, LPWSTR buf, int n) {
  std::wstring s;
  if (!Name(a, &s) || n <= 0) return 0;
  size_t k = s.size() < (size_t)n - 1 ? s.size() : (size_t)n - 1;
  wmemcpy(buf, s.c_str(), k);
  buf[k] = 0;
  return (UINT)k;
}

UINT WINAPI GlobalGetAtomNameA(ATOM a, LPSTR buf, int n) {
  std::wstring s;
  if (!Name(a, &s) || n <= 0) return 0;
  std::string m = Narrow(s.c_str());
  size_t k = m.size() < (size_t)n - 1 ? m.size() : (size_t)n - 1;
  memcpy(buf, m.c_str(), k);
  buf[k] = 0;
  return (UINT)k;
}

ATOM WINAPI AddAtomA(LPCSTR s) { return GlobalAddAtomA(s); }
ATOM WINAPI AddAtomW(LPCWSTR s) { return GlobalAddAtomW(s); }
ATOM WINAPI FindAtomA(LPCSTR s) { return GlobalFindAtomA(s); }
ATOM WINAPI FindAtomW(LPCWSTR s) { return GlobalFindAtomW(s); }
ATOM WINAPI DeleteAtom(ATOM a) { return GlobalDeleteAtom(a); }
UINT WINAPI GetAtomNameA(ATOM a, LPSTR b, int n) { return GlobalGetAtomNameA(a, b, n); }
UINT WINAPI GetAtomNameW(ATOM a, LPWSTR b, int n) { return GlobalGetAtomNameW(a, b, n); }

}  // extern "C"
