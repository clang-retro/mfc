// ole_inplace.cpp - what OLE does for in-place activation around the
// frame: the shared menu's descriptor, and the container's accelerators
// while an object is active.
//
// A menu descriptor holds the shared menu and the widths of its six
// groups: the container's are 0, 2 and 4, the object's 1, 3 and 5.
// OleSetMenuDescriptor puts a window procedure in front of the frame's
// own: the popups of the object's groups (their WM_INITMENUPOPUP and
// WM_MENUSELECT) and the commands chosen from them go to the object's
// window; everything else goes on to the frame.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "ole.h"

#include <map>
#include <vector>

namespace {

struct Descriptor {
  HMENU menu;
  OLEMENUGROUPWIDTHS widths;
};

struct FrameState {
  WNDPROC old = NULL;    // the frame's procedure before ours
  bool active = false;   // a descriptor is set
  HMENU menu = NULL;     // its shared menu
  OLEMENUGROUPWIDTHS widths = {};
  HWND object = NULL;    // the object's window
  int group = -1;        // the group of the popup last opened, while a menu is up
};

std::map<HWND, FrameState> g_frames;

// The group a top-level position of the shared menu is in, or -1.
int GroupOf(const FrameState& f, int pos) {
  LONG start = 0;
  for (int g = 0; g < 6; g++) {
    if (pos >= start && pos < start + f.widths.width[g]) return g;
    start += f.widths.width[g];
  }
  return -1;
}

// The top-level position of a popup in the shared menu, or -1.
int PopupPosition(HMENU menu, HMENU popup) {
  int n = GetMenuItemCount(menu);
  for (int i = 0; i < n; i++)
    if (GetSubMenu(menu, i) == popup) return i;
  return -1;
}

bool HasCommand(HMENU menu, UINT id) {
  int n = GetMenuItemCount(menu);
  for (int i = 0; i < n; i++) {
    if (HMENU sub = GetSubMenu(menu, i)) {
      if (HasCommand(sub, id)) return true;
    } else if (GetMenuItemID(menu, i) == id) {
      return true;
    }
  }
  return false;
}

// The group a command of the shared menu comes from: the popup opened
// last, or else the first popup that has it.
int CommandGroup(const FrameState& f, UINT id) {
  if (f.group >= 0) return f.group;
  int n = GetMenuItemCount(f.menu);
  for (int i = 0; i < n; i++) {
    HMENU sub = GetSubMenu(f.menu, i);
    if (sub ? HasCommand(sub, id) : GetMenuItemID(f.menu, i) == id) return GroupOf(f, i);
  }
  return -1;
}

LRESULT CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto it = g_frames.find(hwnd);
  if (it == g_frames.end()) return DefWindowProcW(hwnd, msg, wp, lp);
  FrameState& f = it->second;
  WNDPROC old = f.old;
  if (msg == WM_NCDESTROY) {
    if ((WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC) == FrameProc) SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)old);
    g_frames.erase(it);
    return CallWindowProcW(old, hwnd, msg, wp, lp);
  }
  if (f.active && f.object && IsWindow(f.object)) {
    switch (msg) {
      case WM_INITMENUPOPUP:
        if (!HIWORD(lp)) {
          int pos = PopupPosition(f.menu, (HMENU)wp);
          if (pos >= 0) f.group = GroupOf(f, pos);
          if (f.group >= 0 && (f.group & 1)) return SendMessageW(f.object, msg, wp, lp);
        }
        break;
      case WM_MENUSELECT: {
        HMENU menu = (HMENU)lp;
        if (HIWORD(wp) == 0xFFFF && !menu) {
          // The menu closed: its command (posted) is still to come, so
          // the group stays; the object hears that it closed too.
          if (f.group >= 0 && (f.group & 1)) SendMessageW(f.object, msg, wp, lp);
          break;
        }
        if (menu == f.menu && (HIWORD(wp) & MF_POPUP)) f.group = GroupOf(f, LOWORD(wp));
        if (f.group >= 0 && (f.group & 1)) return SendMessageW(f.object, msg, wp, lp);
        break;
      }
      case WM_COMMAND:
        if (HIWORD(wp) == 0 && lp == 0) {
          int g = CommandGroup(f, LOWORD(wp));
          f.group = -1;
          if (g >= 0 && (g & 1)) return SendMessageW(f.object, msg, wp, lp);
        }
        break;
    }
  }
  return CallWindowProcW(old, hwnd, msg, wp, lp);
}

}  // namespace

extern "C" {

HOLEMENU WINAPI OleCreateMenuDescriptor(HMENU menu, LPOLEMENUGROUPWIDTHS widths) {
  if (!menu || !widths) return NULL;
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(Descriptor));
  if (!h) return NULL;
  Descriptor* d = (Descriptor*)GlobalLock(h);
  d->menu = menu;
  d->widths = *widths;
  GlobalUnlock(h);
  return (HOLEMENU)h;
}

HRESULT WINAPI OleDestroyMenuDescriptor(HOLEMENU h) {
  if (h) GlobalFree(h);
  return S_OK;
}

HRESULT WINAPI OleSetMenuDescriptor(HOLEMENU h, HWND frame, HWND object, LPOLEINPLACEFRAME, LPOLEINPLACEACTIVEOBJECT) {
  if (!frame || !IsWindow(frame)) return E_INVALIDARG;
  auto it = g_frames.find(frame);
  if (!h) {
    // Off: our procedure goes if it is still on top; otherwise it stays,
    // passing everything on.
    if (it == g_frames.end()) return S_OK;
    if ((WNDPROC)GetWindowLongPtrW(frame, GWLP_WNDPROC) == FrameProc) {
      SetWindowLongPtrW(frame, GWLP_WNDPROC, (LONG_PTR)it->second.old);
      g_frames.erase(it);
    } else {
      it->second.active = false;
      it->second.object = NULL;
      it->second.group = -1;
    }
    return S_OK;
  }
  if (!object) return E_INVALIDARG;
  Descriptor* d = (Descriptor*)GlobalLock(h);
  if (!d) return E_INVALIDARG;
  FrameState& f = g_frames[frame];
  f.menu = d->menu;
  f.widths = d->widths;
  GlobalUnlock(h);
  f.object = object;
  f.active = true;
  f.group = -1;
  if (!f.old) f.old = (WNDPROC)SetWindowLongPtrW(frame, GWLP_WNDPROC, (LONG_PTR)FrameProc);
  return S_OK;
}

// Whether a key message is one of a table's accelerators, and its command.
BOOL WINAPI IsAccelerator(HACCEL accel, int count, LPMSG msg, WORD* cmd) {
  if (!accel || !msg || count <= 0) return FALSE;
  bool key = msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN;
  bool chr = msg->message == WM_CHAR || msg->message == WM_SYSCHAR;
  if (!key && !chr) return FALSE;
  std::vector<ACCEL> table(count);
  int n = CopyAcceleratorTableW(accel, table.data(), count);
  bool shift = GetKeyState(VK_SHIFT) < 0, ctrl = GetKeyState(VK_CONTROL) < 0;
  bool alt = GetKeyState(VK_MENU) < 0 || ((msg->message == WM_SYSKEYDOWN || msg->message == WM_SYSCHAR) &&
                                          (msg->lParam & 0x20000000));
  for (int i = 0; i < n; i++) {
    const ACCEL& a = table[i];
    if (key) {
      if (!(a.fVirt & FVIRTKEY) || a.key != (WORD)msg->wParam) continue;
      if (((a.fVirt & FSHIFT) != 0) != shift || ((a.fVirt & FCONTROL) != 0) != ctrl) continue;
    } else {
      if ((a.fVirt & FVIRTKEY) || a.key != (WORD)msg->wParam) continue;
    }
    if (((a.fVirt & FALT) != 0) != alt) continue;
    if (cmd) *cmd = a.cmd;
    return TRUE;
  }
  return FALSE;
}

// A key the active object did not want: the container's, when it is one
// of the container's accelerators.
HRESULT WINAPI OleTranslateAccelerator(LPOLEINPLACEFRAME frame, LPOLEINPLACEFRAMEINFO info, LPMSG msg) {
  if (!frame || !info || !msg) return E_INVALIDARG;
  WORD cmd = 0;
  if (!IsAccelerator(info->haccel, (int)info->cAccelEntries, msg, &cmd)) return S_FALSE;
  return frame->TranslateAccelerator(msg, cmd);
}

}  // extern "C"
