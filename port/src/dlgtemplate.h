// dlgtemplate.h - dialog templates built in memory, for the layer's own
// dialogs (printing, the OLE dialogs).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "winport.h"

#include <vector>

namespace winport {

// A dialog template in memory.
class Template {
 public:
  Template(DWORD style, int cx, int cy, const wchar_t* title) {
    style |= DS_SETFONT | DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    Dword(style);
    Dword(0);
    w_.push_back(0);  // the item count, set by Add
    Short(0);
    Short(0);
    Short(cx);
    Short(cy);
    w_.push_back(0);  // no menu
    w_.push_back(0);  // the dialog class
    String(title);
    w_.push_back(8);
    String(L"MS Shell Dlg");
  }
  enum { kButton = 0x80, kEdit = 0x81, kStatic = 0x82, kListBox = 0x83, kCombo = 0x85 };
  void Add(int cls, DWORD style, int x, int y, int cx, int cy, int id, const wchar_t* text) {
    if (w_.size() & 1) w_.push_back(0);
    Dword(style | WS_CHILD | WS_VISIBLE);
    Dword(0);
    Short(x);
    Short(y);
    Short(cx);
    Short(cy);
    w_.push_back((WORD)id);
    w_.push_back(0xFFFF);
    w_.push_back((WORD)cls);
    String(text);
    w_.push_back(0);
    w_[4]++;
  }
  LPCDLGTEMPLATEW Get() const { return (LPCDLGTEMPLATEW)w_.data(); }

 private:
  void Dword(DWORD d) {
    w_.push_back((WORD)d);
    w_.push_back((WORD)(d >> 16));
  }
  void Short(int v) { w_.push_back((WORD)(short)v); }
  void String(const wchar_t* s) {
    for (; *s; s++) w_.push_back((WORD)*s);
    w_.push_back(0);
  }
  std::vector<WORD> w_;
};

}  // namespace winport
