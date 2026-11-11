// user_clipboard.cpp - the clipboard: formats and their data, delayed
// rendering, text in its three forms, and clipboard viewers.
//
// The clipboard lives in the process, and its text is shared with other
// programs through the backend: when the program changes the clipboard
// it takes the system's, and when another program has taken it, its
// text comes in as the clipboard is opened or asked about. Data handed
// to SetClipboardData belongs to the clipboard until it is emptied. Text set in one of
// CF_TEXT, CF_OEMTEXT and CF_UNICODETEXT can be read in the others; the
// ANSI and OEM code pages are both UTF-8 here.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "user.h"

#include <string.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>

using namespace winport;

namespace {

struct Entry {
  UINT format;
  HANDLE data;  // NULL until a delayed format is rendered
  bool synthesized;
};

struct Clipboard {
  std::recursive_mutex lock;
  std::vector<Entry> entries;
  HWND owner = NULL;
  HWND openWindow = NULL;
  DWORD openThread = 0;
  bool open = false;
  bool changed = false;
  DWORD sequence = 1;
  HWND viewer = NULL;
};

Clipboard& Board() {
  static auto* c = new Clipboard;
  return *c;
}

bool IsText(UINT f) { return f == CF_TEXT || f == CF_OEMTEXT || f == CF_UNICODETEXT; }

void FreeData(UINT format, HANDLE h) {
  if (!h) return;
  switch (format) {
    case CF_BITMAP:
    case CF_DSPBITMAP:
    case CF_PALETTE:
      DeleteObject(h);
      break;
    case CF_ENHMETAFILE:
    case CF_DSPENHMETAFILE:
      DeleteEnhMetaFile((HENHMETAFILE)h);
      break;
    case CF_OWNERDISPLAY:
      break;
    default:
      if (format >= CF_GDIOBJFIRST && format <= CF_GDIOBJLAST)
        DeleteObject(h);
      else if (!(format >= CF_PRIVATEFIRST && format <= CF_PRIVATELAST))
        GlobalFree(h);
      break;
  }
}

Entry* Find(Clipboard& c, UINT format) {
  for (Entry& e : c.entries)
    if (e.format == format) return &e;
  return NULL;
}

// Makes a text format from another one that is on the clipboard.
HANDLE Synthesize(Clipboard& c, UINT format) {
  Entry* src = NULL;
  for (Entry& e : c.entries)
    if (IsText(e.format) && e.format != format && !e.synthesized && e.data) src = &e;
  if (!src) return NULL;
  const void* p = GlobalLock(src->data);
  if (!p) return NULL;
  HGLOBAL out;
  if (format == CF_UNICODETEXT) {
    std::wstring w = Widen((const char*)p);
    out = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
    if (out) memcpy(GlobalLock(out), w.c_str(), (w.size() + 1) * sizeof(wchar_t));
  } else {
    std::string a = src->format == CF_UNICODETEXT ? Narrow((const wchar_t*)p) : std::string((const char*)p);
    out = GlobalAlloc(GMEM_MOVEABLE, a.size() + 1);
    if (out) memcpy(GlobalLock(out), a.c_str(), a.size() + 1);
  }
  GlobalUnlock(src->data);
  if (out) GlobalUnlock(out);
  return out;
}

bool OpenedByCaller(Clipboard& c) { return c.open && c.openThread == GetCurrentThreadId(); }

// Which of the other programs' clipboards is on ours.
bool g_imported = false;
DWORD g_importedGeneration = 0;

// Brings in the text another program has put on the system's clipboard,
// replacing what was here.
void Import(Clipboard& c) {
  if (c.open && !OpenedByCaller(c)) return;
  DWORD generation;
  if (!GetBackend()->ClipboardForeign(&generation)) return;
  if (g_imported && generation == g_importedGeneration) return;
  g_imported = true;
  g_importedGeneration = generation;
  std::wstring text;
  bool have = GetBackend()->ClipboardFetch(&text);
  for (Entry& e : c.entries)
    if (!e.synthesized) FreeData(e.format, e.data);
  c.entries.clear();
  if (c.owner && IsWindow(c.owner)) PostMessageW(c.owner, WM_DESTROYCLIPBOARD, 0, 0);
  c.owner = NULL;
  c.sequence++;
  if (have && !text.empty()) {
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (h) {
      memcpy(GlobalLock(h), text.c_str(), (text.size() + 1) * sizeof(wchar_t));
      GlobalUnlock(h);
      c.entries.push_back({CF_UNICODETEXT, h, false});
    }
  }
}

}  // namespace

extern "C" {

BOOL WINAPI OpenClipboard(HWND h) {
  Clipboard& c = Board();
  std::lock_guard<std::recursive_mutex> g(c.lock);
  if (c.open && c.openThread != GetCurrentThreadId()) {
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
  }
  c.open = true;
  c.openThread = GetCurrentThreadId();
  c.openWindow = h;
  c.changed = false;
  Import(c);
  return TRUE;
}

BOOL WINAPI CloseClipboard(void) {
  HWND viewer;
  bool changed;
  {
    // What the program put here goes to the system's clipboard, as text.
    Clipboard& c = Board();
    std::unique_lock<std::recursive_mutex> g(c.lock);
    if (OpenedByCaller(c) && c.changed) {
      bool text = false;
      for (Entry& e : c.entries)
        if (IsText(e.format)) text = true;
      g.unlock();
      std::wstring s;
      if (HANDLE h = text ? GetClipboardData(CF_UNICODETEXT) : NULL) {
        if (const wchar_t* p = (const wchar_t*)GlobalLock(h)) {
          s = p;
          GlobalUnlock(h);
        }
      }
      GetBackend()->ClipboardOwn(s);
      g_imported = false;
    }
  }
  {
    Clipboard& c = Board();
    std::lock_guard<std::recursive_mutex> g(c.lock);
    if (!OpenedByCaller(c)) {
      SetLastError(ERROR_CLIPBOARD_NOT_OPEN);
      return FALSE;
    }
    c.open = false;
    c.openWindow = NULL;
    changed = c.changed;
    c.changed = false;
    viewer = c.viewer;
  }
  if (changed && viewer && IsWindow(viewer)) SendMessageW(viewer, WM_DRAWCLIPBOARD, 0, 0);
  return TRUE;
}

BOOL WINAPI EmptyClipboard(void) {
  HWND oldOwner;
  std::vector<Entry> old;
  {
    Clipboard& c = Board();
    std::lock_guard<std::recursive_mutex> g(c.lock);
    if (!OpenedByCaller(c)) {
      SetLastError(ERROR_CLIPBOARD_NOT_OPEN);
      return FALSE;
    }
    oldOwner = c.owner;
    old.swap(c.entries);
    c.owner = c.openWindow;
    c.changed = true;
    c.sequence++;
  }
  if (oldOwner && IsWindow(oldOwner)) SendMessageW(oldOwner, WM_DESTROYCLIPBOARD, 0, 0);
  for (Entry& e : old)
    if (!e.synthesized) FreeData(e.format, e.data);
  return TRUE;
}

HANDLE WINAPI SetClipboardData(UINT format, HANDLE data) {
  Clipboard& c = Board();
  std::lock_guard<std::recursive_mutex> g(c.lock);
  // Delayed rendering may set data while the owner is asked, with the
  // clipboard not open.
  if (!OpenedByCaller(c) && data == NULL) {
    SetLastError(ERROR_CLIPBOARD_NOT_OPEN);
    return NULL;
  }
  if (!format) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  Entry* e = Find(c, format);
  if (e) {
    if (e->data && e->data != data && !e->synthesized) FreeData(format, e->data);
    e->data = data;
    e->synthesized = false;
  } else {
    c.entries.push_back({format, data, false});
  }
  c.changed = true;
  c.sequence++;
  return data;
}

HANDLE WINAPI GetClipboardData(UINT format) {
  HWND owner;
  {
    Clipboard& c = Board();
    std::lock_guard<std::recursive_mutex> g(c.lock);
    if (!OpenedByCaller(c)) {
      SetLastError(ERROR_CLIPBOARD_NOT_OPEN);
      return NULL;
    }
    Entry* e = Find(c, format);
    if (e && e->data) return e->data;
    if (!e && IsText(format)) {
      HANDLE made = Synthesize(c, format);
      if (made) {
        c.entries.push_back({format, made, true});
        return made;
      }
      return NULL;
    }
    if (!e) return NULL;
    owner = c.owner;
  }
  // A delayed format: its owner renders it now.
  if (owner && IsWindow(owner)) SendMessageW(owner, WM_RENDERFORMAT, format, 0);
  Clipboard& c = Board();
  std::lock_guard<std::recursive_mutex> g(c.lock);
  Entry* e = Find(c, format);
  return e ? e->data : NULL;
}

BOOL WINAPI IsClipboardFormatAvailable(UINT format) {
  Clipboard& c = Board();
  std::lock_guard<std::recursive_mutex> g(c.lock);
  Import(c);
  if (Find(c, format)) return TRUE;
  if (IsText(format))
    for (Entry& e : c.entries)
      if (IsText(e.format)) return TRUE;
  return FALSE;
}

int WINAPI CountClipboardFormats(void) {
  Clipboard& c = Board();
  std::lock_guard<std::recursive_mutex> g(c.lock);
  Import(c);
  int n = 0;
  bool text = false;
  for (Entry& e : c.entries) {
    n++;
    if (IsText(e.format)) text = true;
  }
  if (text)
    for (UINT f : {CF_TEXT, CF_OEMTEXT, CF_UNICODETEXT})
      if (!Find(c, f)) n++;
  return n;
}

UINT WINAPI EnumClipboardFormats(UINT format) {
  Clipboard& c = Board();
  std::lock_guard<std::recursive_mutex> g(c.lock);
  if (!OpenedByCaller(c)) {
    SetLastError(ERROR_CLIPBOARD_NOT_OPEN);
    return 0;
  }
  std::vector<UINT> all;
  bool text = false;
  for (Entry& e : c.entries) {
    all.push_back(e.format);
    if (IsText(e.format)) text = true;
  }
  if (text)
    for (UINT f : {CF_TEXT, CF_OEMTEXT, CF_UNICODETEXT})
      if (!Find(c, f)) all.push_back(f);
  if (format == 0) return all.empty() ? 0 : all[0];
  for (size_t i = 0; i + 1 < all.size(); i++)
    if (all[i] == format) return all[i + 1];
  SetLastError(ERROR_SUCCESS);
  return 0;
}

int WINAPI GetClipboardFormatNameW(UINT format, LPWSTR name, int max) {
  if (format < 0xC000) return 0;
  return (int)GlobalGetAtomNameW((ATOM)format, name, max);
}

int WINAPI GetClipboardFormatNameA(UINT format, LPSTR name, int max) {
  if (format < 0xC000) return 0;
  return (int)GlobalGetAtomNameA((ATOM)format, name, max);
}

int WINAPI GetPriorityClipboardFormat(UINT* formats, int n) {
  if (CountClipboardFormats() == 0) return 0;
  for (int i = 0; i < n; i++)
    if (IsClipboardFormatAvailable(formats[i])) return (int)formats[i];
  return -1;
}

HWND WINAPI GetClipboardOwner(void) {
  Clipboard& c = Board();
  std::lock_guard<std::recursive_mutex> g(c.lock);
  return c.owner;
}

HWND WINAPI GetOpenClipboardWindow(void) {
  Clipboard& c = Board();
  std::lock_guard<std::recursive_mutex> g(c.lock);
  return c.open ? c.openWindow : NULL;
}

DWORD WINAPI GetClipboardSequenceNumber(void) {
  Clipboard& c = Board();
  std::lock_guard<std::recursive_mutex> g(c.lock);
  return c.sequence;
}

HWND WINAPI SetClipboardViewer(HWND h) {
  Clipboard& c = Board();
  std::lock_guard<std::recursive_mutex> g(c.lock);
  HWND next = c.viewer;
  c.viewer = h;
  return next;
}

HWND WINAPI GetClipboardViewer(void) {
  Clipboard& c = Board();
  std::lock_guard<std::recursive_mutex> g(c.lock);
  return c.viewer;
}

BOOL WINAPI ChangeClipboardChain(HWND remove, HWND next) {
  HWND viewer;
  {
    Clipboard& c = Board();
    std::lock_guard<std::recursive_mutex> g(c.lock);
    viewer = c.viewer;
    if (viewer == remove) {
      c.viewer = next;
      return TRUE;
    }
  }
  if (viewer && IsWindow(viewer)) SendMessageW(viewer, WM_CHANGECBCHAIN, (WPARAM)remove, (LPARAM)next);
  return TRUE;
}

}  // extern "C"
