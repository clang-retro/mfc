// comctl.h - what the common controls share: notifications to the
// parent, text in either character set, and their registration.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "user.h"

#include <commctrl.h>

#include <string>

namespace winport {

// Sends WM_NOTIFY from a control to a window (its parent when to is
// NULL), with the header filled in. nm may be a larger structure that
// starts with an NMHDR.
LRESULT SendNotify(HWND from, HWND to, UINT code, NMHDR* nm = NULL);

// Whether a control's parent takes Unicode notifications (WM_NOTIFYFORMAT).
bool NotifyUnicode(HWND ctl, HWND to = NULL);

// Text handed to a control, and text handed back, in either set. CopyOut
// writes at most max characters with the terminator and returns the
// count without it.
std::wstring TextIn(const void* p, bool unicode);
int CopyOut(const std::wstring& s, void* buf, int max, bool unicode);
inline bool IsCallback(const void* p, bool unicode) {
  return unicode ? p == (const void*)LPSTR_TEXTCALLBACKW : p == (const void*)LPSTR_TEXTCALLBACKA;
}

// The font controls draw with until WM_SETFONT gives them another.
HFONT ControlFont();

// The common control classes, registered with the system ones.
void RegisterCommonControls();
void RegisterBarClasses();     // comctl_bars.cpp: status bar, toolbar, tooltips
void RegisterSimpleClasses();  // comctl_simple.cpp: progress, trackbar, up-down, header, tab, hotkey, animate
void RegisterListView();       // comctl_listview.cpp
void RegisterTreeView();       // comctl_treeview.cpp

}  // namespace winport
