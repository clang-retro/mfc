// win16ctl.h - what the common control windows of 16-bit Windows share
// (win16ctl.cpp and the other win16 control files): the data of a
// window, drawing, notifications and the registration of a class.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#ifdef _AFX_WIN16

#include <commctrl.h>

namespace afx16 {

/// Gives the data of a control window, which WM_NCCREATE keeps in the
/// window extra bytes.
template <class T>
T* StateOf(HWND hWnd) {
  return (T*)::GetWindowLong(hWnd, 0);
}

/// Makes the data of a control window and keeps it in the window.
template <class T>
T* AttachState(HWND hWnd) {
  T* p = new T;
  p->hwnd = hWnd;
  ::SetWindowLong(hWnd, 0, (LONG)p);
  return p;
}

/// Removes the data of a control window and deletes it.
template <class T>
void DetachState(HWND hWnd, T* p) {
  ::SetWindowLong(hWnd, 0, 0);
  delete p;
}

/// Gives the style of a window.
inline DWORD StyleOf(HWND hWnd) { return (DWORD)::GetWindowLong(hWnd, GWL_STYLE); }

/// Gives the smaller of two numbers.
inline long MinL(long a, long b) { return a < b ? a : b; }

/// Gives the larger of two numbers.
inline long MaxL(long a, long b) { return a > b ? a : b; }

/// Gives n in the range lo to hi.
inline long ClampL(long n, long lo, long hi) { return n < lo ? lo : (n > hi ? hi : n); }

/// Fills a rectangle with a color.
void FillColor(HDC hdc, const RECT& rc, COLORREF clr);

/// Draws a line from (x1, y1) to (x2, y2) in a color. The last point is
/// not drawn.
void DrawLine(HDC hdc, int x1, int y1, int x2, int y2, COLORREF clr);

/// Fills a polygon with a color, without an outline.
void FillPolygon(HDC hdc, const POINT* pts, int n, COLORREF clr);

/// Sends WM_NOTIFY with a notification header to the parent of a
/// control. Gives the result of the parent.
LRESULT SendNotify(HWND hWnd, UINT nCode, NMHDR* pHdr);

/// Sends WM_HSCROLL or WM_VSCROLL to the parent of a control, packed as
/// 16-bit Windows packs it: the code in wParam, the position in the low
/// word and the control in the high word of lParam.
void SendScroll(HWND hWnd, BOOL bVertical, UINT nCode, int nPos);

/// Gives the brush of the background of a control: the brush that the
/// parent gives for WM_CTLCOLOR, or the face color.
HBRUSH ParentBrush(HWND hWnd, HDC hdc);

/// Draws a raised button face in a rectangle: the face color, a
/// highlight edge on the top and left, and a shadow edge on the bottom
/// and right. A pushed face has the sunken edge.
void DrawButtonFace(HDC hdc, RECT rc, BOOL bPushed);

/// Draws a small triangle that points in a direction (0 up, 1 down,
/// 2 left, 3 right) in the middle of a rectangle. A disabled triangle is
/// embossed.
void DrawArrow(HDC hdc, const RECT& rc, int nDir, BOOL bEnabled, BOOL bPushed);

/// Gives the height of the characters of a font.
int FontHeight(HWND hWnd, HFONT hFont);

/// Registers one control class of the module with four bytes of window
/// data. Gives TRUE when the class is there.
BOOL RegisterCtlClass(LPCSTR lpszClass, WNDPROC pfn);

/// Registers a control class with the name of the library and with the
/// name of comctl32.dll. Only the first must succeed.
BOOL RegisterCtlClasses(LPCSTR lpszName, LPCSTR lpszComctlName, WNDPROC pfn);

}  // namespace afx16

#endif  // _AFX_WIN16
