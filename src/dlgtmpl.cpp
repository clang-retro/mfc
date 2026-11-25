// dlgtmpl.cpp - dialog templates built in memory, for the framework's
// own dialogs when the program has no resources for them.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxext.h>

// The layout is the classic DLGTEMPLATE (not DLGTEMPLATEEX): the header,
// then the menu, class and caption (each a 0 word or a string), the font
// when DS_SETFONT is set, then each item aligned to a DWORD.

#ifdef _AFX_WIN16
// 16-bit Windows: the header is 13 bytes (the item count is a byte),
// the strings are ANSI bytes, the predefined class of an item is one
// byte, and nothing is aligned.

/// Starts a 16-bit template: the header, no menu, the default class,
/// the caption and, if there is one, the font.
CAfxDlgTemplate::CAfxDlgTemplate(DWORD dwStyle, int cx, int cy, LPCWSTR lpszCaption,
                                 LPCWSTR lpszFont, WORD wPointSize) {
  DLGTEMPLATE dt;
  memset(&dt, 0, sizeof(dt));
  dt.style = dwStyle | (lpszFont != NULL ? DS_SETFONT : 0);
  dt.cx = (short)cx;
  dt.cy = (short)cy;
  m_data.SetSize(sizeof(dt));
  memcpy(m_data.GetData(), &dt, sizeof(dt));
  m_data.Add(0);  // no menu
  m_data.Add(0);  // the default dialog class
  AddString(lpszCaption != NULL ? lpszCaption : L"");
  if (lpszFont != NULL) {
    AddWord(wPointSize);
    AddString(lpszFont);
  }
}

/// Does nothing: the 16-bit template has no alignment.
void CAfxDlgTemplate::Align(int) {}

/// Adds a 16-bit word, low byte first.
void CAfxDlgTemplate::AddWord(WORD w) {
  m_data.Add(LOBYTE(w));
  m_data.Add(HIBYTE(w));
}

/// Adds a string as ANSI bytes and a 0 byte. A character that is not
/// ASCII becomes '?'.
void CAfxDlgTemplate::AddString(LPCWSTR lpsz) {
  do {
    m_data.Add((BYTE)(*lpsz < 0x80 ? *lpsz : '?'));
  } while (*lpsz++ != 0);
}

/// Adds a control: its position, ID, style, predefined class, text and
/// an empty creation data block, then counts it in the header.
void CAfxDlgTemplate::AddItem(WORD nClass, DWORD dwStyle, int x, int y, int cx, int cy, WORD nID,
                              LPCWSTR lpszText) {
  AddWord((WORD)x);
  AddWord((WORD)y);
  AddWord((WORD)cx);
  AddWord((WORD)cy);
  AddWord(nID);
  DWORD dwFull = dwStyle | WS_CHILD | WS_VISIBLE;
  AddWord(LOWORD(dwFull));
  AddWord(HIWORD(dwFull));
  m_data.Add((BYTE)nClass);
  AddString(lpszText != NULL ? lpszText : L"");
  m_data.Add(0);  // no creation data

  DLGTEMPLATE* pdt = (DLGTEMPLATE*)m_data.GetData();
  pdt->cdit++;
}

#else
CAfxDlgTemplate::CAfxDlgTemplate(DWORD dwStyle, int cx, int cy, LPCWSTR lpszCaption,
                                 LPCWSTR lpszFont, WORD wPointSize) {
  DLGTEMPLATE dt;
  memset(&dt, 0, sizeof(dt));
  dt.style = dwStyle | (lpszFont != NULL ? DS_SETFONT : 0);
  dt.cx = (short)cx;
  dt.cy = (short)cy;
  m_data.SetSize(sizeof(dt));
  memcpy(m_data.GetData(), &dt, sizeof(dt));
  AddWord(0);  // no menu
  AddWord(0);  // the default dialog class
  AddString(lpszCaption != NULL ? lpszCaption : L"");
  if (lpszFont != NULL) {
    AddWord(wPointSize);
    AddString(lpszFont);
  }
}

void CAfxDlgTemplate::Align(int n) {
  while (m_data.GetSize() % n) m_data.Add(0);
}

void CAfxDlgTemplate::AddWord(WORD w) {
  m_data.Add(LOBYTE(w));
  m_data.Add(HIBYTE(w));
}

void CAfxDlgTemplate::AddString(LPCWSTR lpsz) {
  do {
    AddWord((WORD)*lpsz);
  } while (*lpsz++ != 0);
}

void CAfxDlgTemplate::AddItem(WORD nClass, DWORD dwStyle, int x, int y, int cx, int cy, WORD nID,
                              LPCWSTR lpszText) {
  Align(4);
  DLGITEMTEMPLATE item;
  memset(&item, 0, sizeof(item));
  item.style = dwStyle | WS_CHILD | WS_VISIBLE;
  item.x = (short)x;
  item.y = (short)y;
  item.cx = (short)cx;
  item.cy = (short)cy;
  item.id = nID;
  INT_PTR nOffset = m_data.GetSize();
  m_data.SetSize(nOffset + sizeof(item));
  memcpy(m_data.GetData() + nOffset, &item, sizeof(item));
  AddWord(0xFFFF);
  AddWord(nClass);
  AddString(lpszText != NULL ? lpszText : L"");
  AddWord(0);  // no creation data

  // The item count is in the header.
  DLGTEMPLATE* pdt = (DLGTEMPLATE*)m_data.GetData();
  pdt->cdit++;
}

#endif

BOOL AFXAPI _AfxCreateDialogBarIndirect(CDialogBar* pBar, CWnd* pParentWnd,
                                        LPCDLGTEMPLATE lpTemplate, UINT nStyle, UINT nID) {
  pBar->m_dwStyle = nStyle & CBRS_ALL;
  if (!pBar->CreateDlgIndirect(lpTemplate, pParentWnd, AfxGetInstanceHandle())) return FALSE;
  pBar->SetDlgCtrlID(nID);
  CRect rect;
  pBar->GetWindowRect(&rect);
  pBar->m_sizeDefault = rect.Size();
  pBar->SetWindowPos(NULL, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  if (nStyle & WS_VISIBLE) pBar->ShowWindow(SW_SHOWNOACTIVATE);
  return TRUE;
}
