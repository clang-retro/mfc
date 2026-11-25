// kernel_module.cpp - kernel32 modules, resources, processes and the
// command line.
//
// Modules are the program and the shared libraries it loads. Resources
// are compiled .res files that the program links in as data: the
// generated file registers them at startup with _WinportAddResources,
// and the layer finds the module they belong to from the address.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"
#include <shellapi.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <crt_externs.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#else
#include <link.h>
#endif

#include <mutex>
#include <string>
#include <vector>

extern char** environ;
extern "C" void _WinportAddResources(const void* anchor, const void* data, size_t size);

using namespace winport;

namespace {

// ---------------------------------------------------------------------
// Modules

struct Resource {
  WORD typeId, nameId;  // 0 when named by a string
  std::wstring type, name;
  WORD lang;
  const BYTE* data;
  DWORD size;
};

struct Module {
  static const DWORD kMagic = 0x646f6d6a;  // "jmod"
  DWORD magic = kMagic;
  void* dl = NULL;
  std::string path;
  bool system = false;  // stands for a Windows system DLL
  int loads = 1;
  std::vector<Resource> res;
};

std::recursive_mutex& ModuleLock() {
  // Never destroyed, so that static destructors can still use it.
  static std::recursive_mutex* m = new std::recursive_mutex;
  return *m;
}

std::vector<Module*>& Modules() {
  static std::vector<Module*> v;
  return v;
}

std::string RealPath(const char* p) {
  char buf[PATH_MAX];
  if (p && realpath(p, buf)) return buf;
  return p ? p : "";
}

std::string ExecutablePath() {
  static std::string path = [] {
#ifdef __APPLE__
    char buf[PATH_MAX];
    uint32_t n = sizeof buf;
    if (_NSGetExecutablePath(buf, &n) == 0) return RealPath(buf);
    return std::string();
#elif defined(macintosh)
    // The application's name (the low-memory global CurApName, a Pascal
    // string at 0x910) in the folder that the Finder made the current
    // one when it started the application. The first call is at startup.
    const unsigned char* app = (const unsigned char*)0x910;
    char dir[PATH_MAX];
    if (!getcwd(dir, sizeof dir)) return std::string();
    std::string p = dir;
    if (p.empty() || p.back() != '/') p += '/';
    p.append((const char*)app + 1, app[0] > 31 ? 31 : app[0]);
    return p;
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n < 0) return std::string();
    buf[n] = 0;
    return std::string(buf);
#endif
  }();
  return path;
}

Module* MainModule() {
  static Module* m = [] {
    Module* p = new Module;
    p->dl = dlopen(NULL, RTLD_NOW);
    p->path = ExecutablePath();
    p->loads = 1 << 30;
    Modules().push_back(p);
    return p;
  }();
  return m;
}

// The Windows system DLLs are this layer: one module stands for them
// all, and GetProcAddress looks in the whole program.
Module* SystemModule() {
  static Module* m = [] {
    Module* p = new Module;
    p->dl = RTLD_DEFAULT;
    p->system = true;
    p->loads = 1 << 30;
    return p;
  }();
  return m;
}

std::string Lower(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  return s;
}

std::string BaseName(const std::string& p) {
  size_t s = p.find_last_of("/\\");
  return s == std::string::npos ? p : p.substr(s + 1);
}

std::string StripExt(const std::string& n) {
  size_t d = n.find('.');
  return d == std::string::npos ? n : n.substr(0, d);
}

bool IsSystemDll(const std::string& name) {
  static const char* const names[] = {
      "kernel32", "user32", "gdi32", "advapi32", "shell32", "shlwapi", "comctl32", "comdlg32", "ole32",
      "oleaut32", "olepro32", "oledlg", "winspool", "winspool.drv", "uxtheme", "imm32", "version", "winmm", "msacm32",
      "ws2_32", "wsock32", "wininet", "urlmon", "msimg32", "rpcrt4", "odbc32", "riched20", "riched32",
      "msftedit", "mpr", "ntdll", "htmlhelp", "hhctrl", "dwmapi", "secur32", "crypt32", "msvcrt"};
  std::string b = Lower(BaseName(name));
  std::string s = StripExt(b);
  for (const char* n : names)
    if (s == n || b == n) return true;
  return false;
}

#ifdef __APPLE__
// On macOS a generated resource file puts a record of its resources in a
// section of its image rather than registering them from a constructor,
// as constructors in different files run in no set order there: the
// layer reads the section of each image as it is loaded (the callback
// only notes them; they register at the next look at a module).
struct ResourceRecord {
  const void* data;
  size_t size;
};
std::mutex& PendingLock() {
  static auto* m = new std::mutex;
  return *m;
}
std::vector<ResourceRecord>& Pending() {
  static auto* v = new std::vector<ResourceRecord>;
  return *v;
}
void OnAddImage(const struct mach_header* mh, intptr_t) {
  unsigned long size = 0;
#ifdef __LP64__
  uint8_t* p = getsectiondata((const struct mach_header_64*)mh, "__DATA", "__winport_res", &size);
#else
  uint8_t* p = getsectiondata(mh, "__DATA", "__winport_res", &size);
#endif
  if (p == NULL) return;
  std::lock_guard<std::mutex> g(PendingLock());
  for (unsigned long i = 0; i + sizeof(ResourceRecord) <= size; i += sizeof(ResourceRecord))
    Pending().push_back(*(const ResourceRecord*)(p + i));
}
void ScanImageResources() {
  static std::once_flag once;
  std::call_once(once, [] { _dyld_register_func_for_add_image(OnAddImage); });
  std::vector<ResourceRecord> todo;
  {
    std::lock_guard<std::mutex> g(PendingLock());
    todo.swap(Pending());
  }
  for (const ResourceRecord& r : todo) _WinportAddResources(r.data, r.data, r.size);
}
#else
void ScanImageResources() {}
#endif

Module* ToModule(HMODULE h) {
  ScanImageResources();
  if (h == NULL) return MainModule();
  Module* m = (Module*)h;
  if (m == SystemModule()) return m;
  std::lock_guard<std::recursive_mutex> g(ModuleLock());
  for (Module* p : Modules())
    if (p == m) return m;
  return NULL;
}

// The module that holds an address: the program or a loaded library.
Module* ModuleAt(const void* addr) {
  Dl_info info;
  std::string path;
  if (dladdr(addr, &info) && info.dli_fname) path = RealPath(info.dli_fname);
  std::lock_guard<std::recursive_mutex> g(ModuleLock());
  Module* main = MainModule();
  if (path.empty() || path == main->path) return main;
  for (Module* p : Modules())
    if (p->path == path) return p;
  // A library whose resources register before LoadLibrary returns.
  Module* m = new Module;
  m->path = path;
  m->loads = 0;
  Modules().push_back(m);
  return m;
}

// ---------------------------------------------------------------------
// Resources

WORD Le16(const BYTE* p) { return (WORD)(p[0] | (p[1] << 8)); }
DWORD Le32(const BYTE* p) { return (DWORD)p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24); }

// Reads a type or name: an ordinal, or a string of UTF-16 code units.
const BYTE* ReadId(const BYTE* p, const BYTE* end, WORD* id, std::wstring* s) {
  if (p + 2 > end) return NULL;
  if (Le16(p) == 0xffff) {
    if (p + 4 > end) return NULL;
    *id = Le16(p + 2);
    return p + 4;
  }
  *id = 0;
  while (p + 2 <= end && Le16(p) != 0) {
    unsigned int c = Le16(p);
    p += 2;
    if (c >= 0xd800 && c < 0xdc00 && p + 2 <= end) {
      unsigned int lo = Le16(p);
      p += 2;
      c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00);
    }
    *s += (wchar_t)towupper((wint_t)c);
  }
  return p + 2;
}

bool ParseId(const wchar_t* in, WORD* id, std::wstring* s) {
  if (IS_INTRESOURCE(in)) {
    *id = (WORD)(UINT_PTR)in;
    return true;
  }
  if (in[0] == L'#') {
    *id = (WORD)wcstoul(in + 1, NULL, 10);
    return true;
  }
  *id = 0;
  for (const wchar_t* p = in; *p; p++) *s += (wchar_t)towupper((wint_t)*p);
  return true;
}

bool ParseIdA(const char* in, WORD* id, std::wstring* s) {
  if (IS_INTRESOURCE(in)) {
    *id = (WORD)(UINT_PTR)in;
    return true;
  }
  std::wstring w = Widen(in);
  return ParseId(w.c_str(), id, s);
}

const Resource* Find(Module* m, WORD typeId, const std::wstring& type, WORD nameId, const std::wstring& name,
                     WORD lang, bool anyLang) {
  const Resource* first = NULL;
  for (const Resource& r : m->res) {
    if (r.typeId != typeId || (typeId == 0 && r.type != type)) continue;
    if (r.nameId != nameId || (nameId == 0 && r.name != name)) continue;
    if (!anyLang && r.lang == lang) return &r;
    if (!first) first = &r;
  }
  if (anyLang || PRIMARYLANGID(lang) == LANG_NEUTRAL) return first;
  // A language without a match falls back to any of the name's.
  for (const Resource& r : m->res)
    if (&r == first) return first;
  return first;
}

HRSRC DoFind(HMODULE h, WORD typeId, const std::wstring& type, WORD nameId, const std::wstring& name, WORD lang,
             bool anyLang) {
  Module* m = ToModule(h);
  if (!m) {
    SetLastError(ERROR_INVALID_HANDLE);
    return NULL;
  }
  std::lock_guard<std::recursive_mutex> g(ModuleLock());
  const Resource* r = Find(m, typeId, type, nameId, name, lang, anyLang);
  if (!r) {
    SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND);
    return NULL;
  }
  return (HRSRC)r;
}

}  // namespace

// Registers a compiled .res file's resources with the module that holds
// `anchor`, normally an address in the generated file itself.
extern "C" void _WinportAddResources(const void* anchor, const void* data, size_t size) {
  Module* m = ModuleAt(anchor);
  std::lock_guard<std::recursive_mutex> g(ModuleLock());
  const BYTE* p = (const BYTE*)data;
  const BYTE* end = p + size;
  while (p + 8 <= end) {
    DWORD dataSize = Le32(p), headerSize = Le32(p + 4);
    if (headerSize < 16 || p + headerSize + dataSize > end) break;
    Resource r;
    const BYTE* q = ReadId(p + 8, p + headerSize, &r.typeId, &r.type);
    if (q) q = ReadId(q, p + headerSize, &r.nameId, &r.name);
    if (!q) break;
    q = p + ((q - p + 3) & ~3);
    r.lang = q + 8 <= p + headerSize ? Le16(q + 6) : 0;
    r.data = p + headerSize;
    r.size = dataSize;
    if (dataSize != 0 || r.typeId != 0) m->res.push_back(r);
    p += (headerSize + dataSize + 3) & ~3u;
  }
}

extern "C" {

HMODULE WINAPI GetModuleHandleA(LPCSTR name) {
  if (!name) return (HMODULE)MainModule();
  std::string n(name);
  if (IsSystemDll(n)) return (HMODULE)SystemModule();
  std::string want = Lower(StripExt(BaseName(n)));
  std::lock_guard<std::recursive_mutex> g(ModuleLock());
  for (Module* p : Modules()) {
    std::string b = Lower(StripExt(BaseName(p->path)));
    if (b != want && !(b.compare(0, 3, "lib") == 0 && b.substr(3) == want)) continue;
    if (!p->dl) {
      // Known from its resources, or freed: only if it is still loaded.
      void* h = dlopen(p->path.c_str(), RTLD_NOW | RTLD_NOLOAD);
      if (!h) continue;
      dlclose(h);
    }
    return (HMODULE)p;
  }
  SetLastError(ERROR_MOD_NOT_FOUND);
  return NULL;
}

HMODULE WINAPI GetModuleHandleW(LPCWSTR name) {
  if (!name) return (HMODULE)MainModule();
  return GetModuleHandleA(Narrow(name).c_str());
}

DWORD WINAPI GetModuleFileNameA(HMODULE h, LPSTR buf, DWORD n) {
  Module* m = ToModule(h);
  if (!m || n == 0) {
    SetLastError(ERROR_INVALID_HANDLE);
    return 0;
  }
  std::string p = m->system ? std::string("/usr/lib/winport/kernel32.dll") : m->path;
  // Too small a buffer gets the truncated name and the size, as on XP.
  DWORD k = (DWORD)p.size();
  if (k >= n) {
    memcpy(buf, p.c_str(), n - 1);
    buf[n - 1] = 0;
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return n;
  }
  memcpy(buf, p.c_str(), k + 1);
  return k;
}

DWORD WINAPI GetModuleFileNameW(HMODULE h, LPWSTR buf, DWORD n) {
  char tmp[PATH_MAX];
  if (!GetModuleFileNameA(h, tmp, sizeof tmp)) return 0;
  std::wstring w = Widen(tmp);
  DWORD k = (DWORD)w.size();
  if (n == 0) return 0;
  if (k >= n) {
    wmemcpy(buf, w.c_str(), n - 1);
    buf[n - 1] = 0;
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return n;
  }
  wmemcpy(buf, w.c_str(), k + 1);
  return k;
}

// Calls the library's own DllMain, if it has one (not one it gets from
// another library or the program).
typedef BOOL(WINAPI* DllMainProc)(HINSTANCE, DWORD, LPVOID);
static BOOL CallDllMain(Module* m, DWORD reason) {
  void* p = dlsym(m->dl, "DllMain");
  if (!p) return TRUE;
  Dl_info info;
  if (!dladdr(p, &info) || RealPath(info.dli_fname) != m->path) return TRUE;
  return ((DllMainProc)p)((HINSTANCE)m, reason, NULL);
}

HMODULE WINAPI LoadLibraryExA(LPCSTR name, HANDLE, DWORD) {
  if (!name) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  if (IsSystemDll(name)) return (HMODULE)SystemModule();
  // "x.dll" is tried as given, then as x.so or x.dylib and libx.so.
  std::string p = PosixPath(name);
  std::vector<std::string> tries{p};
  std::string stem = p;
  if (Lower(stem).size() > 4 && Lower(stem).compare(stem.size() - 4, 4, ".dll") == 0) stem.erase(stem.size() - 4);
#ifdef __APPLE__
  const char* ext = ".dylib";
#else
  const char* ext = ".so";
#endif
  tries.push_back(stem + ext);
  size_t slash = stem.find_last_of('/');
  std::string dir = slash == std::string::npos ? "" : stem.substr(0, slash + 1);
  std::string base = slash == std::string::npos ? stem : stem.substr(slash + 1);
  tries.push_back(dir + "lib" + base + ext);
  tries.push_back(dir + "lib" + Lower(base) + ext);
  void* dl = NULL;
  std::string got;
  for (auto& t : tries) {
    // Names without a directory are also looked for next to the program.
    if (t.find('/') == std::string::npos) {
      std::string exe = ExecutablePath();
      std::string here = exe.substr(0, exe.find_last_of('/') + 1) + t;
      if ((dl = dlopen(here.c_str(), RTLD_NOW | RTLD_LOCAL))) {
        got = here;
        break;
      }
    }
    if ((dl = dlopen(t.c_str(), RTLD_NOW | RTLD_LOCAL))) {
      got = t;
      break;
    }
  }
  if (!dl) {
    SetLastError(ERROR_MOD_NOT_FOUND);
    return NULL;
  }
  std::lock_guard<std::recursive_mutex> g(ModuleLock());
  for (Module* m : Modules()) {
    if (m->dl == dl) {
      m->loads++;
      dlclose(dl);
      return (HMODULE)m;
    }
  }
  // The library's own resources registered it by path when it loaded.
  Module* m = NULL;
  for (Module* r : Modules()) {
    if (!r->dl && !r->path.empty()) {
      void* h = dlopen(r->path.c_str(), RTLD_NOW | RTLD_NOLOAD);
      if (h) dlclose(h);
      if (h == dl) {
        m = r;
        m->dl = dl;
        m->loads = 1;
        break;
      }
    }
  }
  if (!m) {
    m = new Module;
    m->dl = dl;
    m->path = RealPath(got.c_str());
    Modules().push_back(m);
  }
  // A Windows-style library's DllMain (an MFC regular DLL has one) runs
  // as it would on Windows.
  if (!CallDllMain(m, DLL_PROCESS_ATTACH)) {
    dlclose(dl);
    m->dl = NULL;
    m->loads = 0;
    SetLastError(ERROR_DLL_INIT_FAILED);
    return NULL;
  }
  return (HMODULE)m;
}

BOOL WINAPI GetModuleHandleExA(DWORD flags, LPCSTR name, HMODULE* phModule) {
  if (phModule == NULL) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *phModule = NULL;
  Module* m;
  if (flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) {
    m = ModuleAt(name);
  } else {
    m = (Module*)GetModuleHandleA(name);
    if (m == NULL) return FALSE;
  }
  // The count goes up unless asked not to (the program and the system
  // DLLs never go away).
  if (!(flags & (GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT | GET_MODULE_HANDLE_EX_FLAG_PIN)) &&
      m->dl != NULL && !m->system && m != MainModule()) {
    std::lock_guard<std::recursive_mutex> g(ModuleLock());
    m->loads++;
  }
  *phModule = (HMODULE)m;
  return TRUE;
}

BOOL WINAPI GetModuleHandleExW(DWORD flags, LPCWSTR name, HMODULE* phModule) {
  if (flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) return GetModuleHandleExA(flags, (LPCSTR)name, phModule);
  return GetModuleHandleExA(flags, name ? Narrow(name).c_str() : NULL, phModule);
}

// The libraries a program links with load before it starts. On Windows
// their DllMains run then (an MFC extension DLL's adds it to the
// program's list of DLLs); here the program's startup calls this, which
// calls them, the libraries each depends on first, and calls them again
// as the process ends.
static std::vector<Module*>& AttachedModules() {
  static std::vector<Module*>* v = new std::vector<Module*>;
  return *v;
}

static void DetachLinkedLibraries() {
  std::vector<Module*>& v = AttachedModules();
  while (!v.empty()) {
    Module* m = v.back();
    v.pop_back();
    void* p = dlsym(m->dl, "DllMain");
    if (p) ((DllMainProc)p)((HINSTANCE)m, DLL_PROCESS_DETACH, (LPVOID)1);
  }
}

#ifndef __APPLE__
static int AddLibrary(struct dl_phdr_info* info, size_t, void* data) {
  if (info->dlpi_name && info->dlpi_name[0]) ((std::vector<std::string>*)data)->push_back(info->dlpi_name);
  return 0;
}
#endif

void WINAPI _WinportAttachLinkedLibraries() {
  static bool done = false;
  if (done) return;
  done = true;
  std::vector<std::string> paths;
#ifdef __APPLE__
  for (uint32_t i = 1; i < _dyld_image_count(); i++) paths.push_back(_dyld_get_image_name(i));
#else
  dl_iterate_phdr(AddLibrary, &paths);
#endif
  // Loaded in the order the program names them, each before those it
  // needs: the last are the ones the others depend on.
  for (size_t i = paths.size(); i-- > 0;) {
    void* dl = dlopen(paths[i].c_str(), RTLD_NOW | RTLD_NOLOAD);
    if (!dl) continue;
    void* p = dlsym(dl, "DllMain");
    Dl_info info;
    std::string path = RealPath(paths[i].c_str());
    if (!p || !dladdr(p, &info) || RealPath(info.dli_fname) != path) {
      dlclose(dl);
      continue;
    }
    Module* m = ModuleAt(p);
    {
      std::lock_guard<std::recursive_mutex> g(ModuleLock());
      if (m == MainModule() || m->dl != NULL) {
        dlclose(dl);
        continue;
      }
      m->dl = dl;
      m->loads = 1 << 30;  // FreeLibrary does not unload it
    }
    if (((DllMainProc)p)((HINSTANCE)m, DLL_PROCESS_ATTACH, (LPVOID)1)) AttachedModules().push_back(m);
  }
  if (!AttachedModules().empty()) atexit(DetachLinkedLibraries);
}

HMODULE WINAPI LoadLibraryA(LPCSTR name) { return LoadLibraryExA(name, NULL, 0); }
HMODULE WINAPI LoadLibraryW(LPCWSTR name) { return LoadLibraryExA(name ? Narrow(name).c_str() : NULL, NULL, 0); }
HMODULE WINAPI LoadLibraryExW(LPCWSTR name, HANDLE f, DWORD fl) {
  return LoadLibraryExA(name ? Narrow(name).c_str() : NULL, f, fl);
}

BOOL WINAPI FreeLibrary(HMODULE h) {
  Module* m = ToModule(h);
  if (!m) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (m->system || m == MainModule()) return TRUE;
  std::lock_guard<std::recursive_mutex> g(ModuleLock());
  if (--m->loads <= 0 && m->dl) {
    CallDllMain(m, DLL_PROCESS_DETACH);
    dlclose(m->dl);
    // Its resources went with it; they register again if it comes back.
    m->res.clear();
    m->dl = NULL;
    m->loads = 0;
  }
  return TRUE;
}

void WINAPI FreeLibraryAndExitThread(HMODULE h, DWORD code) {
  FreeLibrary(h);
  ExitThread(code);
}

FARPROC WINAPI GetProcAddress(HMODULE h, LPCSTR name) {
  Module* m = ToModule(h);
  if (!m || !m->dl) {
    SetLastError(ERROR_INVALID_HANDLE);
    return NULL;
  }
  if (IS_INTRESOURCE(name)) {
    SetLastError(ERROR_PROC_NOT_FOUND);
    return NULL;
  }
  void* p = dlsym(m->dl, name);
  if (!p) {
    SetLastError(ERROR_PROC_NOT_FOUND);
    return NULL;
  }
  return (FARPROC)p;
}

BOOL WINAPI DisableThreadLibraryCalls(HMODULE) { return TRUE; }

HRSRC WINAPI FindResourceExW(HMODULE h, LPCWSTR type, LPCWSTR name, WORD lang) {
  WORD ti, ni;
  std::wstring ts, ns;
  ParseId(type, &ti, &ts);
  ParseId(name, &ni, &ns);
  return DoFind(h, ti, ts, ni, ns, lang, false);
}

HRSRC WINAPI FindResourceExA(HMODULE h, LPCSTR type, LPCSTR name, WORD lang) {
  WORD ti, ni;
  std::wstring ts, ns;
  ParseIdA(type, &ti, &ts);
  ParseIdA(name, &ni, &ns);
  return DoFind(h, ti, ts, ni, ns, lang, false);
}

HRSRC WINAPI FindResourceW(HMODULE h, LPCWSTR name, LPCWSTR type) {
  WORD ti, ni;
  std::wstring ts, ns;
  ParseId(type, &ti, &ts);
  ParseId(name, &ni, &ns);
  return DoFind(h, ti, ts, ni, ns, 0, true);
}

HRSRC WINAPI FindResourceA(HMODULE h, LPCSTR name, LPCSTR type) {
  WORD ti, ni;
  std::wstring ts, ns;
  ParseIdA(type, &ti, &ts);
  ParseIdA(name, &ni, &ns);
  return DoFind(h, ti, ts, ni, ns, 0, true);
}

HGLOBAL WINAPI LoadResource(HMODULE, HRSRC r) {
  if (!r) {
    SetLastError(ERROR_INVALID_HANDLE);
    return NULL;
  }
  return (HGLOBAL)((const Resource*)r)->data;
}

LPVOID WINAPI LockResource(HGLOBAL h) { return (LPVOID)h; }
DWORD WINAPI SizeofResource(HMODULE, HRSRC r) { return r ? ((const Resource*)r)->size : 0; }
BOOL WINAPI FreeResource(HGLOBAL) { return FALSE; }

}  // extern "C"

namespace {

// Ids for the enumeration callbacks: an ordinal, or a string the
// enumeration keeps alive.
template <class C>
const C* IdFor(WORD id, const std::wstring& s, std::basic_string<C>* keep);
template <>
const wchar_t* IdFor(WORD id, const std::wstring& s, std::wstring* keep) {
  if (id) return MAKEINTRESOURCEW(id);
  *keep = s;
  return keep->c_str();
}
template <>
const char* IdFor(WORD id, const std::wstring& s, std::string* keep) {
  if (id) return MAKEINTRESOURCEA(id);
  *keep = Narrow(s.c_str());
  return keep->c_str();
}

template <class C, class P>
BOOL EnumTypes(HMODULE h, P proc, LONG_PTR param) {
  Module* m = ToModule(h);
  if (!m) return FALSE;
  std::vector<std::pair<WORD, std::wstring>> seen;
  for (const Resource& r : m->res) {
    bool dup = false;
    for (auto& s : seen) dup |= s.first == r.typeId && s.second == r.type;
    if (dup) continue;
    seen.push_back({r.typeId, r.type});
    std::basic_string<C> keep;
    if (!proc(h, (C*)IdFor<C>(r.typeId, r.type, &keep), param)) break;
  }
  return TRUE;
}

template <class C, class P>
BOOL EnumNames(HMODULE h, WORD ti, const std::wstring& ts, const C* type, P proc, LONG_PTR param) {
  Module* m = ToModule(h);
  if (!m) return FALSE;
  std::vector<std::pair<WORD, std::wstring>> seen;
  for (const Resource& r : m->res) {
    if (r.typeId != ti || (ti == 0 && r.type != ts)) continue;
    bool dup = false;
    for (auto& s : seen) dup |= s.first == r.nameId && s.second == r.name;
    if (dup) continue;
    seen.push_back({r.nameId, r.name});
    std::basic_string<C> keep;
    if (!proc(h, type, (C*)IdFor<C>(r.nameId, r.name, &keep), param)) break;
  }
  if (seen.empty()) {
    SetLastError(ERROR_RESOURCE_TYPE_NOT_FOUND);
    return FALSE;
  }
  return TRUE;
}

}  // namespace

extern "C" {

BOOL WINAPI EnumResourceTypesA(HMODULE h, ENUMRESTYPEPROCA p, LONG_PTR l) { return EnumTypes<char>(h, p, l); }
BOOL WINAPI EnumResourceTypesW(HMODULE h, ENUMRESTYPEPROCW p, LONG_PTR l) { return EnumTypes<wchar_t>(h, p, l); }

BOOL WINAPI EnumResourceNamesA(HMODULE h, LPCSTR type, ENUMRESNAMEPROCA p, LONG_PTR l) {
  WORD ti;
  std::wstring ts;
  ParseIdA(type, &ti, &ts);
  return EnumNames<char>(h, ti, ts, type, p, l);
}

BOOL WINAPI EnumResourceNamesW(HMODULE h, LPCWSTR type, ENUMRESNAMEPROCW p, LONG_PTR l) {
  WORD ti;
  std::wstring ts;
  ParseId(type, &ti, &ts);
  return EnumNames<wchar_t>(h, ti, ts, type, p, l);
}

// ---------------------------------------------------------------------
// The command line

}  // extern "C"

namespace {

std::vector<std::string> Argv() {
  std::vector<std::string> v;
#ifdef __APPLE__
  int argc = *_NSGetArgc();
  char** argv = *_NSGetArgv();
  for (int i = 0; i < argc; i++) v.push_back(argv[i]);
#else
  FILE* f = fopen("/proc/self/cmdline", "rb");
  if (f) {
    std::string cur;
    int c;
    while ((c = fgetc(f)) != EOF) {
      if (c == 0) {
        v.push_back(cur);
        cur.clear();
      } else {
        cur += (char)c;
      }
    }
    fclose(f);
  }
#endif
  return v;
}

// Quotes an argument so that the Windows rules read it back unchanged.
std::string Quote(const std::string& a) {
  if (!a.empty() && a.find_first_of(" \t\"") == std::string::npos) return a;
  std::string r = "\"";
  size_t slashes = 0;
  for (char c : a) {
    if (c == '\\') {
      slashes++;
    } else if (c == '"') {
      r.append(slashes * 2 + 1, '\\');
      r += '"';
      slashes = 0;
      continue;
    } else {
      slashes = 0;
    }
    r += c;
  }
  r.append(slashes, '\\');
  r += '"';
  return r;
}

std::string& CommandLine() {
  static std::string s = [] {
    std::string r;
    for (auto& a : Argv()) {
      if (!r.empty()) r += ' ';
      r += Quote(a);
    }
    return r;
  }();
  return s;
}

// Splits a command line by the Windows rules.
std::vector<std::string> SplitCommandLine(const std::string& s) {
  std::vector<std::string> v;
  size_t i = 0, n = s.size();
  while (i < n) {
    while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
    if (i >= n) break;
    std::string a;
    bool quoted = false;
    while (i < n && (quoted || (s[i] != ' ' && s[i] != '\t'))) {
      if (s[i] == '\\') {
        size_t k = 0;
        while (i < n && s[i] == '\\') {
          k++;
          i++;
        }
        if (i < n && s[i] == '"') {
          a.append(k / 2, '\\');
          if (k % 2) {
            a += '"';
            i++;
          }
        } else {
          a.append(k, '\\');
        }
      } else if (s[i] == '"') {
        if (quoted && i + 1 < n && s[i + 1] == '"') {
          a += '"';
          i += 2;
          continue;
        }
        quoted = !quoted;
        i++;
      } else {
        a += s[i++];
      }
    }
    v.push_back(a);
  }
  return v;
}

// ---------------------------------------------------------------------
// Processes

struct Process : Object {
  pid_t pid;
  bool done = false;
  DWORD exitCode = STILL_ACTIVE;
  explicit Process(pid_t p) : Object(kProcess), pid(p) {}
  bool Waitable() override { return true; }
  bool IsSignaled(DWORD) override { return done; }
};

// The children this program started, while they run, so that OpenProcess
// gives one of them the handle its reaper signals. (Guarded by WaitLock.)
std::vector<Process*>& Children() {
  static auto* v = new std::vector<Process*>;
  return *v;
}

void* Reaper(void* arg) {
  Process* p = (Process*)arg;
  int status = 0;
  while (waitpid(p->pid, &status, 0) < 0 && errno == EINTR) {
  }
  pthread_mutex_lock(WaitLock());
  p->done = true;
  p->exitCode = WIFEXITED(status) ? (DWORD)WEXITSTATUS(status) : WIFSIGNALED(status) ? 0xC000013A : 1;
  auto& kids = Children();
  for (size_t i = 0; i < kids.size(); i++)
    if (kids[i] == p) {
      kids.erase(kids.begin() + i);
      Release(p);
      break;
    }
  SignalChange();
  pthread_mutex_unlock(WaitLock());
  Release(p);
  return NULL;
}

// Another program's process cannot be waited for with waitpid: this
// looks every little while whether it is still there. Its exit code is
// not known.
void* Watcher(void* arg) {
  Process* p = (Process*)arg;
  while (kill(p->pid, 0) == 0 || errno == EPERM) {
    pthread_mutex_lock(WaitLock());
    bool alone = p->refs.load() == 1;  // every handle is closed
    pthread_mutex_unlock(WaitLock());
    if (alone) break;
    usleep(20000);
  }
  pthread_mutex_lock(WaitLock());
  p->done = true;
  p->exitCode = 0;
  SignalChange();
  pthread_mutex_unlock(WaitLock());
  Release(p);
  return NULL;
}

}  // namespace

extern "C" {

LPSTR WINAPI GetCommandLineA(void) { return (LPSTR)CommandLine().c_str(); }

LPWSTR WINAPI GetCommandLineW(void) {
  static std::wstring w = Widen(CommandLine().c_str());
  return (LPWSTR)w.c_str();
}

void WINAPI GetStartupInfoA(LPSTARTUPINFOA si) {
  memset(si, 0, sizeof(*si));
  si->cb = sizeof(*si);
  si->wShowWindow = 10;  // SW_SHOWDEFAULT
}

void WINAPI GetStartupInfoW(LPSTARTUPINFOW si) {
  memset(si, 0, sizeof(*si));
  si->cb = sizeof(*si);
  si->wShowWindow = 10;
}

BOOL WINAPI CreateProcessA(LPCSTR app, LPSTR cmd, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD flags,
                           LPVOID env, LPCSTR dir, LPSTARTUPINFOA si, LPPROCESS_INFORMATION pi) {
  std::vector<std::string> args = SplitCommandLine(cmd ? cmd : "");
  std::string prog = app ? PosixPath(app) : args.empty() ? std::string() : PosixPath(args[0].c_str());
  if (prog.empty()) {
    SetLastError(ERROR_FILE_NOT_FOUND);
    return FALSE;
  }
  // "prog.exe" may be "prog" here.
  if (access(prog.c_str(), X_OK) != 0 && prog.size() > 4 && Lower(prog).compare(prog.size() - 4, 4, ".exe") == 0) {
    std::string bare = prog.substr(0, prog.size() - 4);
    if (prog.find('/') == std::string::npos || access(bare.c_str(), X_OK) == 0) prog = bare;
  }
  std::vector<char*> argv;
  for (auto& a : args) argv.push_back((char*)a.c_str());
  if (argv.empty()) argv.push_back((char*)prog.c_str());
  argv.push_back(NULL);
  // An environment block is strings each ending in a zero, then a zero.
  std::vector<char*> envp;
  if (env) {
    for (char* e = (char*)env; *e; e += strlen(e) + 1) envp.push_back(e);
    envp.push_back(NULL);
  }
  // The working directory changes for the child alone.
  std::string savedDir;
  char cwd[PATH_MAX];
  if (dir && getcwd(cwd, sizeof cwd)) savedDir = cwd;
  if (dir && chdir(PosixPath(dir).c_str()) != 0) {
    SetErrorFromErrno();
    return FALSE;
  }
  // The child's standard handles: pipes and files the caller names, and
  // nothing for a handle it leaves out.
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_t* pa = NULL;
  if (si && (si->dwFlags & STARTF_USESTDHANDLES)) {
    posix_spawn_file_actions_init(&actions);
    pa = &actions;
    HANDLE std[3] = {si->hStdInput, si->hStdOutput, si->hStdError};
    for (int i = 0; i < 3; i++) {
      int fd = std[i] && std[i] != INVALID_HANDLE_VALUE ? FileDescriptor(std[i]) : -1;
      if (fd >= 0)
        posix_spawn_file_actions_adddup2(pa, fd, i);
      else
        posix_spawn_file_actions_addopen(pa, i, "/dev/null", i == 0 ? O_RDONLY : O_WRONLY, 0);
    }
  }
  pid_t pid;
  int r = posix_spawnp(&pid, prog.c_str(), pa, NULL, argv.data(), env ? envp.data() : environ);
  if (pa) posix_spawn_file_actions_destroy(pa);
  if (!savedDir.empty() && chdir(savedDir.c_str()) != 0) {
  }
  if (r != 0) {
    SetLastError(r == ENOENT ? ERROR_FILE_NOT_FOUND : ErrorFromErrno(r));
    return FALSE;
  }
  Process* p = new Process(pid);
  p->refs += 2;  // the reaper's, and the list of children's
  pthread_mutex_lock(WaitLock());
  Children().push_back(p);
  pthread_mutex_unlock(WaitLock());
  pthread_t t;
  pthread_create(&t, NULL, Reaper, p);
  pthread_detach(t);
  (void)flags;
  if (pi) {
    pi->hProcess = NewHandle(p);
    // There is no handle to the child's thread; this one is for closing.
    pi->hThread = CreateEventA(NULL, TRUE, FALSE, NULL);
    pi->dwProcessId = (DWORD)pid;
    pi->dwThreadId = (DWORD)pid;
  } else {
    Release(p);
  }
  return TRUE;
}

BOOL WINAPI CreateProcessW(LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta, BOOL inherit,
                           DWORD flags, LPVOID env, LPCWSTR dir, LPSTARTUPINFOW siw, LPPROCESS_INFORMATION pi) {
  std::string a = Narrow(app), c = Narrow(cmd), d = Narrow(dir);
  // A Unicode environment is converted only when the caller says so.
  std::string envA;
  if (env && (flags & 0x400)) {  // CREATE_UNICODE_ENVIRONMENT
    for (const wchar_t* e = (const wchar_t*)env; *e; e += wcslen(e) + 1) {
      envA += Narrow(e);
      envA += '\0';
    }
    envA += '\0';
    env = (LPVOID)envA.data();
  }
  STARTUPINFOA si = {};
  si.cb = sizeof si;
  if (siw) {
    // The same, but for the strings, which a child here does not use.
    si.dwX = siw->dwX, si.dwY = siw->dwY, si.dwXSize = siw->dwXSize, si.dwYSize = siw->dwYSize;
    si.dwFlags = siw->dwFlags;
    si.wShowWindow = siw->wShowWindow;
    si.hStdInput = siw->hStdInput, si.hStdOutput = siw->hStdOutput, si.hStdError = siw->hStdError;
  }
  return CreateProcessA(app ? a.c_str() : NULL, cmd ? (LPSTR)c.c_str() : NULL, pa, ta, inherit, flags, env,
                        dir ? d.c_str() : NULL, &si, pi);
}

UINT WINAPI WinExec(LPCSTR cmd, UINT) {
  PROCESS_INFORMATION pi;
  STARTUPINFOA si = {};
  si.cb = sizeof si;
  std::string c(cmd ? cmd : "");
  if (!CreateProcessA(NULL, (LPSTR)c.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    return GetLastError() == ERROR_FILE_NOT_FOUND ? 2 : 0;
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return 33;
}

HANDLE WINAPI OpenProcess(DWORD, BOOL, DWORD pid) {
  if (pid == (DWORD)getpid()) return GetCurrentProcess();
  if (kill((pid_t)pid, 0) != 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  pthread_mutex_lock(WaitLock());
  for (Process* p : Children())
    if (p->pid == (pid_t)pid) {
      p->refs++;
      pthread_mutex_unlock(WaitLock());
      return NewHandle(p);
    }
  pthread_mutex_unlock(WaitLock());
  // Not a child: a thread watches for its end.
  Process* p = new Process((pid_t)pid);
  p->refs++;  // the watcher's
  pthread_t t;
  pthread_create(&t, NULL, Watcher, p);
  pthread_detach(t);
  return NewHandle(p);
}

BOOL WINAPI TerminateProcess(HANDLE h, UINT code) {
  if (h == GetCurrentProcess()) _exit((int)code);
  Process* p = static_cast<Process*>(FromHandle(h, kProcess));
  if (!p) return FALSE;
  if (kill(p->pid, SIGKILL) != 0) {
    SetErrorFromErrno();
    return FALSE;
  }
  return TRUE;
}

BOOL WINAPI GetExitCodeProcess(HANDLE h, LPDWORD code) {
  if (h == GetCurrentProcess()) {
    *code = STILL_ACTIVE;
    return TRUE;
  }
  Process* p = static_cast<Process*>(FromHandle(h, kProcess));
  if (!p) return FALSE;
  pthread_mutex_lock(WaitLock());
  *code = p->exitCode;
  pthread_mutex_unlock(WaitLock());
  return TRUE;
}

void WINAPI ExitProcess(UINT code) { exit((int)code); }

}  // extern "C"

// ShellExecute: the desktop's opener (open on macOS, xdg-open elsewhere)
// for a document or link, which is what the "open" verb asks for. The
// opener runs on its own; a child that has ended is reaped as SIGCHLD's
// default would leave it.
static HINSTANCE ShellExecuteCore(const std::string& verb, const std::string& file) {
  if (!verb.empty() && verb != "open") return (HINSTANCE)(intptr_t)SE_ERR_NOASSOC;
  if (file.empty()) return (HINSTANCE)(intptr_t)ERROR_FILE_NOT_FOUND;
#if defined(__APPLE__)
  const char* opener = "open";
#else
  const char* opener = "xdg-open";
#endif
  bool isLink = file.find("://") != std::string::npos || file.compare(0, 7, "mailto:") == 0;
  std::string target = isLink ? file : PosixPath(file.c_str());
  char* argv[] = {(char*)opener, (char*)target.c_str(), NULL};
  pid_t pid;
  if (posix_spawnp(&pid, opener, NULL, NULL, argv, environ) != 0)
    return (HINSTANCE)(intptr_t)ERROR_FILE_NOT_FOUND;
  int status;
  waitpid(pid, &status, WNOHANG);
  return (HINSTANCE)(intptr_t)42;
}

extern "C" HINSTANCE WINAPI ShellExecuteA(HWND, LPCSTR verb, LPCSTR file, LPCSTR, LPCSTR, INT) {
  return ShellExecuteCore(verb ? verb : "", file ? file : "");
}

extern "C" HINSTANCE WINAPI ShellExecuteW(HWND, LPCWSTR verb, LPCWSTR file, LPCWSTR, LPCWSTR,
                                          INT) {
  return ShellExecuteCore(verb ? Narrow(verb) : std::string(), file ? Narrow(file) : std::string());
}

// WinHelp: there is no WinHelp viewer here, so the help is a document
// next to the .hlp file the program names (the same name, as .html, .htm,
// .pdf or .txt, or a folder of that name with an index.html), opened with
// the desktop's opener, or with the command WINPORT_HELP names (given the
// document as its argument). A context (HELP_CONTEXT, HELP_CONTEXTPOPUP)
// is the HTML page's anchor: help.html#131072. Closing the viewer
// (HELP_QUIT) and placing it are left to the viewer.
static bool IsFile(const std::string& p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static std::string HelpDocument(std::string file) {
  size_t window = file.find('>');
  if (window != std::string::npos) file.resize(window);
  std::string path = PosixPath(file.c_str());
  size_t dot = path.rfind('.');
  size_t slash = path.rfind('/');
  std::string ext = dot != std::string::npos && (slash == std::string::npos || dot > slash) ? path.substr(dot) : "";
  for (char& c : ext) c = (char)tolower((unsigned char)c);
  if (ext != ".hlp" && ext != ".chm" && IsFile(path)) return path;
  std::string base = ext.empty() ? path : path.substr(0, dot);
  for (const char* e : {".html", ".htm", ".pdf", ".txt", ".HTML", ".HTM", ".PDF", ".TXT"})
    if (IsFile(base + e)) return base + e;
  if (IsFile(base + "/index.html")) return base + "/index.html";
  return std::string();
}

static BOOL WinHelpCore(const char* file, UINT cmd, ULONG_PTR data) {
  switch (cmd) {
    case HELP_QUIT:
    case HELP_SETWINPOS:
    case HELP_SETPOPUP_POS:
    case HELP_SETINDEX:
    case HELP_TCARD_DATA:
    case HELP_TCARD_OTHER_CALLER:
      return TRUE;
  }
  if (!file || !*file) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  std::string doc = HelpDocument(file);
  if (doc.empty()) {
    SetLastError(ERROR_FILE_NOT_FOUND);
    return FALSE;
  }
  std::string target = doc;
  size_t dot = doc.rfind('.');
  std::string ext = dot == std::string::npos ? "" : doc.substr(dot);
  for (char& c : ext) c = (char)tolower((unsigned char)c);
  if ((cmd == HELP_CONTEXT || cmd == HELP_CONTEXTPOPUP) && (ext == ".html" || ext == ".htm"))
    target = "file://" + doc + "#" + std::to_string((unsigned long)data);
  const char* custom = getenv("WINPORT_HELP");
  std::vector<char*> argv;
  std::string script;
  if (custom && *custom) {
    script = std::string(custom) + " \"$1\"";
    argv = {(char*)"sh", (char*)"-c", (char*)script.c_str(), (char*)"sh", (char*)target.c_str(), NULL};
  } else {
#if defined(__APPLE__)
    argv = {(char*)"open", (char*)target.c_str(), NULL};
#else
    argv = {(char*)"xdg-open", (char*)target.c_str(), NULL};
#endif
  }
  pid_t pid;
  if (posix_spawnp(&pid, argv[0], NULL, NULL, argv.data(), environ) != 0) {
    SetLastError(ERROR_FILE_NOT_FOUND);
    return FALSE;
  }
  int status;
  waitpid(pid, &status, WNOHANG);
  return TRUE;
}

extern "C" BOOL WINAPI WinHelpA(HWND, LPCSTR file, UINT cmd, ULONG_PTR data) {
  return WinHelpCore(file, cmd, data);
}

extern "C" BOOL WINAPI WinHelpW(HWND, LPCWSTR file, UINT cmd, ULONG_PTR data) {
  std::string f = file ? Narrow(file) : std::string();
  return WinHelpCore(file ? f.c_str() : NULL, cmd, data);
}
