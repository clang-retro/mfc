// shell_drop.cpp - files dropped on a window: DragAcceptFiles, the HDROP
// of WM_DROPFILES and CF_HDROP (DragQueryFile, DragQueryPoint,
// DragFinish), and the drops the backends deliver from the desktop.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "user.h"

#include <shlobj.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace winport;

namespace {

// The paths of a drop, in either character set.
bool ReadDrop(HDROP h, std::vector<std::wstring>* paths, DROPFILES* head) {
  const DROPFILES* df = (const DROPFILES*)GlobalLock((HGLOBAL)h);
  if (!df) return false;
  SIZE_T size = GlobalSize((HGLOBAL)h);
  *head = *df;
  const BYTE* p = (const BYTE*)df + df->pFiles;
  const BYTE* end = (const BYTE*)df + size;
  if (df->fWide) {
    for (const wchar_t* s = (const wchar_t*)p; (const BYTE*)(s + 1) <= end && *s;) {
      size_t n = 0;
      while ((const BYTE*)(s + n + 1) <= end && s[n]) n++;
      paths->push_back(std::wstring(s, n));
      s += n + 1;
    }
  } else {
    for (const char* s = (const char*)p; (const BYTE*)s < end && *s;) {
      size_t n = 0;
      while ((const BYTE*)s + n < end && s[n]) n++;
      paths->push_back(Widen(s, (int)n));
      s += n + 1;
    }
  }
  GlobalUnlock((HGLOBAL)h);
  return true;
}

}  // namespace

namespace winport {

std::vector<std::wstring> PathsFromUris(const std::string& list) {
  std::vector<std::wstring> out;
  size_t i = 0;
  while (i < list.size()) {
    size_t j = list.find('\n', i);
    if (j == std::string::npos) j = list.size();
    std::string line = list.substr(i, j - i);
    i = j + 1;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.empty() || line[0] == '#' || line.compare(0, 5, "file:") != 0) continue;
    // file:///path, or file://host/path for this host.
    std::string rest = line.substr(5);
    if (rest.compare(0, 2, "//") == 0) {
      size_t slash = rest.find('/', 2);
      if (slash == std::string::npos) continue;
      rest = rest.substr(slash);
    }
    std::string path;
    for (size_t k = 0; k < rest.size(); k++) {
      if (rest[k] == '%' && k + 2 < rest.size() && isxdigit((unsigned char)rest[k + 1]) &&
          isxdigit((unsigned char)rest[k + 2])) {
        path += (char)strtol(rest.substr(k + 1, 2).c_str(), NULL, 16);
        k += 2;
      } else {
        path += rest[k];
      }
    }
    if (!path.empty() && path[0] == '/') out.push_back(Widen(path.c_str(), (int)path.size()));
  }
  return out;
}

HGLOBAL MakeDrop(const std::vector<std::wstring>& paths, POINT pt, bool nc) {
  size_t chars = 1;
  for (const std::wstring& p : paths) chars += p.size() + 1;
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + chars * sizeof(wchar_t));
  if (!h) return NULL;
  DROPFILES* df = (DROPFILES*)GlobalLock(h);
  df->pFiles = sizeof(DROPFILES);
  df->pt = pt;
  df->fNC = nc;
  df->fWide = TRUE;
  wchar_t* out = (wchar_t*)(df + 1);
  for (const std::wstring& p : paths) {
    memcpy(out, p.c_str(), (p.size() + 1) * sizeof(wchar_t));
    out += p.size() + 1;
  }
  *out = 0;
  GlobalUnlock(h);
  return h;
}

HWND DropTarget(POINT screen) {
  for (HWND h = WindowFromPoint(screen); h; h = GetParent(h))
    if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_ACCEPTFILES) return h;
  return NULL;
}

bool DropFiles(POINT screen, const std::vector<std::wstring>& paths) {
  HWND h = DropTarget(screen);
  if (!h || paths.empty()) return false;
  POINT pt = screen;
  ScreenToClient(h, &pt);
  RECT client;
  GetClientRect(h, &client);
  bool nc = !PtInRect(&client, pt);
  HGLOBAL drop = MakeDrop(paths, pt, nc);
  if (!drop) return false;
  if (!PostMessageW(h, WM_DROPFILES, (WPARAM)drop, 0)) {
    GlobalFree(drop);
    return false;
  }
  return true;
}

}  // namespace winport

extern "C" {

void WINAPI DragAcceptFiles(HWND h, BOOL accept) {
  LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
  SetWindowLongPtrW(h, GWL_EXSTYLE, accept ? ex | WS_EX_ACCEPTFILES : ex & ~(LONG_PTR)WS_EX_ACCEPTFILES);
}

UINT WINAPI DragQueryFileW(HDROP h, UINT i, LPWSTR buf, UINT cch) {
  std::vector<std::wstring> paths;
  DROPFILES head;
  if (!ReadDrop(h, &paths, &head)) return 0;
  if (i == 0xFFFFFFFF) return (UINT)paths.size();
  if (i >= paths.size()) return 0;
  const std::wstring& p = paths[i];
  if (!buf) return (UINT)p.size();
  if (cch == 0) return 0;
  UINT n = (UINT)std::min<size_t>(p.size(), cch - 1);
  memcpy(buf, p.data(), n * sizeof(wchar_t));
  buf[n] = 0;
  return n;
}

UINT WINAPI DragQueryFileA(HDROP h, UINT i, LPSTR buf, UINT cch) {
  std::vector<std::wstring> paths;
  DROPFILES head;
  if (!ReadDrop(h, &paths, &head)) return 0;
  if (i == 0xFFFFFFFF) return (UINT)paths.size();
  if (i >= paths.size()) return 0;
  std::string p = Narrow(paths[i].c_str());
  if (!buf) return (UINT)p.size();
  if (cch == 0) return 0;
  UINT n = (UINT)std::min<size_t>(p.size(), cch - 1);
  memcpy(buf, p.data(), n);
  buf[n] = 0;
  return n;
}

BOOL WINAPI DragQueryPoint(HDROP h, LPPOINT pt) {
  const DROPFILES* df = (const DROPFILES*)GlobalLock((HGLOBAL)h);
  if (!df) return FALSE;
  if (pt) *pt = df->pt;
  BOOL client = !df->fNC;
  GlobalUnlock((HGLOBAL)h);
  return client;
}

void WINAPI DragFinish(HDROP h) { GlobalFree((HGLOBAL)h); }

}  // extern "C"
