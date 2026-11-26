// shell_folder.cpp - the standard folders (SHGetFolderPath): the XDG
// folders and the home folder on Linux, the folders under ~/Library on
// macOS. A program's settings in CSIDL_APPDATA land in ~/.config (or
// ~/Library/Application Support), and CSIDL_PROFILE is the home folder.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <windows.h>
#include <shlobj.h>

#include "winport.h"

#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdio.h>
#include <string>

namespace {

std::string Home() {
  const char* h = getenv("HOME");
  if (h && *h) return h;
  struct passwd* pw = getpwuid(getuid());
  return pw && pw->pw_dir ? pw->pw_dir : "/";
}

// An XDG base folder: the variable if it names an absolute path, else the
// default under the home folder.
std::string Base(const char* var, const char* fallback) {
  const char* v = getenv(var);
  if (v && v[0] == '/') return v;
  return Home() + "/" + fallback;
}

// A user folder from ~/.config/user-dirs.dirs (XDG_DOCUMENTS_DIR="$HOME/
// Documents"), else the usual name under the home folder.
std::string UserDir(const char* key, const char* fallback) {
  // stdio, not <fstream>: the libc++ of Mac OS X 10.0 to 10.3 has no streams.
  FILE* f = fopen((Base("XDG_CONFIG_HOME", ".config") + "/user-dirs.dirs").c_str(), "r");
  if (!f) return Home() + "/" + fallback;
  std::string want = std::string(key) + "=";
  char buf[1024];
  while (fgets(buf, sizeof buf, f)) {
    std::string line(buf);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    if (line.compare(0, want.size(), want) != 0) continue;
    std::string v = line.substr(want.size());
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    if (v.compare(0, 5, "$HOME") == 0) v = Home() + v.substr(5);
    if (!v.empty() && v[0] == '/') {
      fclose(f);
      return v;
    }
  }
  fclose(f);
  return Home() + "/" + fallback;
}

// The folder for a CSIDL, or "" if there is none here.
std::string Folder(int csidl) {
  std::string home = Home();
#ifdef __APPLE__
  std::string lib = home + "/Library";
  switch (csidl) {
    case CSIDL_APPDATA:
    case CSIDL_LOCAL_APPDATA: return lib + "/Application Support";
    case CSIDL_COMMON_APPDATA: return "/Library/Application Support";
    case CSIDL_INTERNET_CACHE: return lib + "/Caches";
    case CSIDL_COOKIES: return lib + "/Cookies";
    case CSIDL_FONTS: return lib + "/Fonts";
    case CSIDL_PERSONAL: return home + "/Documents";
    case CSIDL_COMMON_DOCUMENTS: return "/Users/Shared";
    case CSIDL_DESKTOP:
    case CSIDL_DESKTOPDIRECTORY: return home + "/Desktop";
    case CSIDL_MYMUSIC: return home + "/Music";
    case CSIDL_MYVIDEO: return home + "/Movies";
    case CSIDL_MYPICTURES: return home + "/Pictures";
    case CSIDL_PROGRAMS:
    case CSIDL_PROGRAM_FILES: return "/Applications";
  }
#else
  switch (csidl) {
    case CSIDL_APPDATA: return Base("XDG_CONFIG_HOME", ".config");
    case CSIDL_LOCAL_APPDATA: return Base("XDG_DATA_HOME", ".local/share");
    case CSIDL_COMMON_APPDATA: return "/etc/xdg";
    case CSIDL_INTERNET_CACHE: return Base("XDG_CACHE_HOME", ".cache");
    case CSIDL_FONTS: return Base("XDG_DATA_HOME", ".local/share") + "/fonts";
    case CSIDL_PROGRAMS: return Base("XDG_DATA_HOME", ".local/share") + "/applications";
    case CSIDL_PERSONAL: return UserDir("XDG_DOCUMENTS_DIR", "Documents");
    case CSIDL_COMMON_DOCUMENTS: return "/usr/share/doc";
    case CSIDL_DESKTOP:
    case CSIDL_DESKTOPDIRECTORY: return UserDir("XDG_DESKTOP_DIR", "Desktop");
    case CSIDL_TEMPLATES: return UserDir("XDG_TEMPLATES_DIR", "Templates");
    case CSIDL_MYMUSIC: return UserDir("XDG_MUSIC_DIR", "Music");
    case CSIDL_MYVIDEO: return UserDir("XDG_VIDEOS_DIR", "Videos");
    case CSIDL_MYPICTURES: return UserDir("XDG_PICTURES_DIR", "Pictures");
    case CSIDL_PROGRAM_FILES: return "/opt";
  }
#endif
  switch (csidl) {
    case CSIDL_PROFILE: return home;
    case CSIDL_WINDOWS:
    case CSIDL_SYSTEM: {
      char dir[MAX_PATH];
      UINT n = csidl == CSIDL_WINDOWS ? GetWindowsDirectoryA(dir, MAX_PATH) : GetSystemDirectoryA(dir, MAX_PATH);
      return n && n < MAX_PATH ? std::string(dir, n) : "";
    }
  }
  return "";
}

bool IsDir(const std::string& p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Makes a folder and those above it.
bool MakeDirs(const std::string& p) {
  if (p.empty() || IsDir(p)) return true;
  size_t slash = p.find_last_of('/');
  if (slash != std::string::npos && slash > 0 && !MakeDirs(p.substr(0, slash))) return false;
  return mkdir(p.c_str(), 0700) == 0 || errno == EEXIST;
}

// The path, or an HRESULT: S_FALSE when the folder is known but missing.
HRESULT Get(int flags, DWORD type, std::string* out) {
  int csidl = flags & ~CSIDL_FLAG_MASK;
  if (type != SHGFP_TYPE_CURRENT && type != SHGFP_TYPE_DEFAULT) return E_INVALIDARG;
  std::string p = Folder(csidl);
  if (p.empty()) return E_INVALIDARG;
  *out = p;
  if (flags & CSIDL_FLAG_CREATE) {
    if (!MakeDirs(p)) return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
  } else if (!(flags & CSIDL_FLAG_DONT_VERIFY) && !IsDir(p)) {
    return S_FALSE;
  }
  return S_OK;
}

}  // namespace

extern "C" {

HRESULT WINAPI SHGetFolderPathA(HWND, int csidl, HANDLE, DWORD type, LPSTR path) {
  if (!path) return E_INVALIDARG;
  path[0] = 0;
  std::string p;
  HRESULT hr = Get(csidl, type, &p);
  if (hr != S_OK) return hr;  // S_FALSE for a folder that is not there
  if (p.size() >= MAX_PATH) return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
  memcpy(path, p.c_str(), p.size() + 1);
  return S_OK;
}

HRESULT WINAPI SHGetFolderPathW(HWND, int csidl, HANDLE, DWORD type, LPWSTR path) {
  if (!path) return E_INVALIDARG;
  path[0] = 0;
  std::string p;
  HRESULT hr = Get(csidl, type, &p);
  // The wide form fails for a folder that is not there.
  if (hr == S_FALSE) return E_FAIL;
  if (hr != S_OK) return hr;
  std::wstring w = winport::Widen(p.c_str());
  if (w.size() >= MAX_PATH) return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
  wmemcpy(path, w.c_str(), w.size() + 1);
  return S_OK;
}

BOOL WINAPI SHGetSpecialFolderPathA(HWND h, LPSTR path, int csidl, BOOL create) {
  return SHGetFolderPathA(h, csidl | (create ? CSIDL_FLAG_CREATE : 0), NULL, SHGFP_TYPE_CURRENT, path) == S_OK;
}

BOOL WINAPI SHGetSpecialFolderPathW(HWND h, LPWSTR path, int csidl, BOOL create) {
  return SHGetFolderPathW(h, csidl | (create ? CSIDL_FLAG_CREATE : 0), NULL, SHGFP_TYPE_CURRENT, path) == S_OK;
}

}  // extern "C"
