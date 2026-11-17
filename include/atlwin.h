// atlwin.h - ATL's windows: CWindow (an HWND and the functions on it),
// message maps (BEGIN_MSG_MAP), CWindowImpl (a window class whose
// messages go to the map of a C++ object, through a thunk that stands in
// for the window procedure), superclassing and subclassing,
// CContainedWindow (a child window whose messages go to its parent's
// map), notification reflection, and CDialogImpl and CSimpleDialog.
//
//   class CMyWindow : public CWindowImpl<CMyWindow> {
//   public:
//     DECLARE_WND_CLASS(_T("MyWindow"))
//     BEGIN_MSG_MAP(CMyWindow)
//       MESSAGE_HANDLER(WM_PAINT, OnPaint)
//       COMMAND_ID_HANDLER(ID_FILE_EXIT, OnExit)
//     END_MSG_MAP()
//     LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
//     LRESULT OnExit(WORD, WORD, HWND, BOOL&);
//   };
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLWIN_H__
#define __ATLWIN_H__

#include "atlbase.h"
#include <commctrl.h>

#if !defined(_ATL_NO_DEFAULT_LIBS) && defined(_WIN32)
__asm__(".section .drectve,\"yn\"\n"
        ".ascii \" -defaultlib:gdi32 -defaultlib:comctl32\"\n"
        ".text");
#endif

namespace ATL {

// ---------------------------------------------------------------------
// The argument helpers: a rectangle by pointer or reference, a menu or a
// child ID, a string or a resource ID.

class _U_RECT {
public:
  LPRECT m_lpRect;
  _U_RECT(LPRECT lpRect) : m_lpRect(lpRect) {}
  _U_RECT(RECT& rc) : m_lpRect(&rc) {}
};

class _U_MENUorID {
public:
  HMENU m_hMenu;
  _U_MENUorID(HMENU hMenu) : m_hMenu(hMenu) {}
  _U_MENUorID(UINT nID) : m_hMenu((HMENU)(UINT_PTR)nID) {}
};

class _U_STRINGorID {
public:
  LPCTSTR m_lpstr;
  _U_STRINGorID(LPCTSTR lpString) : m_lpstr(lpString) {}
  _U_STRINGorID(UINT nID) : m_lpstr(MAKEINTRESOURCE(nID)) {}
};

// ---------------------------------------------------------------------
// CWindow.

class CWindow {
public:
  HWND m_hWnd;
  static inline RECT rcDefault = {CW_USEDEFAULT, CW_USEDEFAULT, 0, 0};

  CWindow(HWND hWnd = NULL) : m_hWnd(hWnd) {}
  CWindow& operator=(HWND hWnd) {
    m_hWnd = hWnd;
    return *this;
  }
  operator HWND() const { return m_hWnd; }

  static LPCTSTR GetWndClassName() { return NULL; }
  void Attach(HWND hWndNew) { m_hWnd = hWndNew; }
  HWND Detach() {
    HWND hWnd = m_hWnd;
    m_hWnd = NULL;
    return hWnd;
  }

  HWND Create(LPCTSTR lpstrWndClass, HWND hWndParent, _U_RECT rect = NULL,
              LPCTSTR szWindowName = NULL, DWORD dwStyle = 0, DWORD dwExStyle = 0,
              _U_MENUorID MenuOrID = 0U, LPVOID lpCreateParam = NULL) {
    if (rect.m_lpRect == NULL) rect.m_lpRect = &rcDefault;
    m_hWnd = ::CreateWindowEx(dwExStyle, lpstrWndClass, szWindowName, dwStyle, rect.m_lpRect->left,
                              rect.m_lpRect->top, rect.m_lpRect->right - rect.m_lpRect->left,
                              rect.m_lpRect->bottom - rect.m_lpRect->top, hWndParent,
                              MenuOrID.m_hMenu, _AtlBaseModule.GetModuleInstance(),
                              lpCreateParam);
    return m_hWnd;
  }
  BOOL DestroyWindow() {
    if (!::DestroyWindow(m_hWnd)) return FALSE;
    m_hWnd = NULL;
    return TRUE;
  }

  // Attributes.
  DWORD GetStyle() const { return (DWORD)::GetWindowLong(m_hWnd, GWL_STYLE); }
  DWORD GetExStyle() const { return (DWORD)::GetWindowLong(m_hWnd, GWL_EXSTYLE); }
  LONG GetWindowLong(int nIndex) const { return ::GetWindowLong(m_hWnd, nIndex); }
  LONG SetWindowLong(int nIndex, LONG dwNewLong) { return ::SetWindowLong(m_hWnd, nIndex, dwNewLong); }
#ifdef _WIN64
  // (In 32-bit code these are macros naming GetWindowLong and
  // SetWindowLong.)
  LONG_PTR GetWindowLongPtr(int nIndex) const { return ::GetWindowLongPtr(m_hWnd, nIndex); }
  LONG_PTR SetWindowLongPtr(int nIndex, LONG_PTR dwNewLong) {
    return ::SetWindowLongPtr(m_hWnd, nIndex, dwNewLong);
  }
#endif
  WORD GetWindowWord(int nIndex) const { return ::GetWindowWord(m_hWnd, nIndex); }
  WORD SetWindowWord(int nIndex, WORD wNewWord) { return ::SetWindowWord(m_hWnd, nIndex, wNewWord); }
  BOOL ModifyStyle(DWORD dwRemove, DWORD dwAdd, UINT nFlags = 0) {
    return ModifyLong(GWL_STYLE, dwRemove, dwAdd, nFlags);
  }
  BOOL ModifyStyleEx(DWORD dwRemove, DWORD dwAdd, UINT nFlags = 0) {
    return ModifyLong(GWL_EXSTYLE, dwRemove, dwAdd, nFlags);
  }

  // Messages.
  LRESULT SendMessage(UINT message, WPARAM wParam = 0, LPARAM lParam = 0) {
    return ::SendMessage(m_hWnd, message, wParam, lParam);
  }
  BOOL PostMessage(UINT message, WPARAM wParam = 0, LPARAM lParam = 0) {
    return ::PostMessage(m_hWnd, message, wParam, lParam);
  }
  BOOL SendNotifyMessage(UINT message, WPARAM wParam = 0, LPARAM lParam = 0) {
    return ::SendNotifyMessage(m_hWnd, message, wParam, lParam);
  }
  static LRESULT SendMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    return ::SendMessage(hWnd, message, wParam, lParam);
  }
  // Sends the message to every descendant (or only the children).
  void SendMessageToDescendants(UINT message, WPARAM wParam = 0, LPARAM lParam = 0,
                                BOOL bDeep = TRUE) {
    for (HWND hWndChild = ::GetTopWindow(m_hWnd); hWndChild != NULL;
         hWndChild = ::GetNextWindow(hWndChild, GW_HWNDNEXT)) {
      ::SendMessage(hWndChild, message, wParam, lParam);
      if (bDeep && ::GetTopWindow(hWndChild) != NULL) {
        CWindow wnd(hWndChild);
        wnd.SendMessageToDescendants(message, wParam, lParam, bDeep);
      }
    }
  }

  // Text.
  BOOL SetWindowText(LPCTSTR lpszString) { return ::SetWindowText(m_hWnd, lpszString); }
  int GetWindowText(LPTSTR lpszStringBuf, int nMaxCount) const {
    return ::GetWindowText(m_hWnd, lpszStringBuf, nMaxCount);
  }
  int GetWindowTextLength() const { return ::GetWindowTextLength(m_hWnd); }
  BOOL GetWindowText(BSTR* pbstrText) {
    if (pbstrText == NULL) return FALSE;
    *pbstrText = NULL;
    int nLen = ::GetWindowTextLengthW(m_hWnd);
    WCHAR* p = new WCHAR[nLen + 1];
    p[0] = 0;
    ::GetWindowTextW(m_hWnd, p, nLen + 1);
    *pbstrText = ::SysAllocString(p);
    delete[] p;
    return *pbstrText != NULL;
  }
  BOOL GetWindowText(BSTR& bstrText) { return GetWindowText(&bstrText); }

  // Font and icon.
  void SetFont(HFONT hFont, BOOL bRedraw = TRUE) {
    ::SendMessage(m_hWnd, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(bRedraw, 0));
  }
  HFONT GetFont() const { return (HFONT)::SendMessage(m_hWnd, WM_GETFONT, 0, 0); }
  HICON SetIcon(HICON hIcon, BOOL bBigIcon = TRUE) {
    return (HICON)::SendMessage(m_hWnd, WM_SETICON, bBigIcon, (LPARAM)hIcon);
  }
  HICON GetIcon(BOOL bBigIcon = TRUE) const {
    return (HICON)::SendMessage(m_hWnd, WM_GETICON, bBigIcon, 0);
  }

  // Menu.
  HMENU GetMenu() const { return ::GetMenu(m_hWnd); }
  BOOL SetMenu(HMENU hMenu) { return ::SetMenu(m_hWnd, hMenu); }
  BOOL DrawMenuBar() { return ::DrawMenuBar(m_hWnd); }
  HMENU GetSystemMenu(BOOL bRevert) const { return ::GetSystemMenu(m_hWnd, bRevert); }
  BOOL HiliteMenuItem(HMENU hMenu, UINT uItemHilite, UINT uHilite) {
    return ::HiliteMenuItem(m_hWnd, hMenu, uItemHilite, uHilite);
  }

  // Size and position.
  BOOL IsIconic() const { return ::IsIconic(m_hWnd); }
  BOOL IsZoomed() const { return ::IsZoomed(m_hWnd); }
  BOOL MoveWindow(int x, int y, int nWidth, int nHeight, BOOL bRepaint = TRUE) {
    return ::MoveWindow(m_hWnd, x, y, nWidth, nHeight, bRepaint);
  }
  BOOL MoveWindow(LPCRECT lpRect, BOOL bRepaint = TRUE) {
    return ::MoveWindow(m_hWnd, lpRect->left, lpRect->top, lpRect->right - lpRect->left,
                        lpRect->bottom - lpRect->top, bRepaint);
  }
  BOOL SetWindowPos(HWND hWndInsertAfter, int x, int y, int cx, int cy, UINT nFlags) {
    return ::SetWindowPos(m_hWnd, hWndInsertAfter, x, y, cx, cy, nFlags);
  }
  BOOL SetWindowPos(HWND hWndInsertAfter, LPCRECT lpRect, UINT nFlags) {
    return ::SetWindowPos(m_hWnd, hWndInsertAfter, lpRect->left, lpRect->top,
                          lpRect->right - lpRect->left, lpRect->bottom - lpRect->top, nFlags);
  }
  UINT ArrangeIconicWindows() { return ::ArrangeIconicWindows(m_hWnd); }
  BOOL BringWindowToTop() { return ::BringWindowToTop(m_hWnd); }
  BOOL GetWindowRect(LPRECT lpRect) const { return ::GetWindowRect(m_hWnd, lpRect); }
  BOOL GetClientRect(LPRECT lpRect) const { return ::GetClientRect(m_hWnd, lpRect); }
  BOOL GetWindowPlacement(WINDOWPLACEMENT* lpwndpl) const {
    return ::GetWindowPlacement(m_hWnd, lpwndpl);
  }
  BOOL SetWindowPlacement(const WINDOWPLACEMENT* lpwndpl) {
    return ::SetWindowPlacement(m_hWnd, lpwndpl);
  }

  // Coordinates.
  BOOL ClientToScreen(LPPOINT lpPoint) const { return ::ClientToScreen(m_hWnd, lpPoint); }
  BOOL ClientToScreen(LPRECT lpRect) const {
    if (!::ClientToScreen(m_hWnd, (LPPOINT)lpRect)) return FALSE;
    return ::ClientToScreen(m_hWnd, ((LPPOINT)lpRect) + 1);
  }
  BOOL ScreenToClient(LPPOINT lpPoint) const { return ::ScreenToClient(m_hWnd, lpPoint); }
  BOOL ScreenToClient(LPRECT lpRect) const {
    if (!::ScreenToClient(m_hWnd, (LPPOINT)lpRect)) return FALSE;
    return ::ScreenToClient(m_hWnd, ((LPPOINT)lpRect) + 1);
  }
  int MapWindowPoints(HWND hWndTo, LPPOINT lpPoint, UINT nCount) const {
    return ::MapWindowPoints(m_hWnd, hWndTo, lpPoint, nCount);
  }
  int MapWindowPoints(HWND hWndTo, LPRECT lpRect) const {
    return ::MapWindowPoints(m_hWnd, hWndTo, (LPPOINT)lpRect, 2);
  }

  // Painting.
  HDC BeginPaint(LPPAINTSTRUCT lpPaint) { return ::BeginPaint(m_hWnd, lpPaint); }
  void EndPaint(LPPAINTSTRUCT lpPaint) { ::EndPaint(m_hWnd, lpPaint); }
  HDC GetDC() { return ::GetDC(m_hWnd); }
  HDC GetWindowDC() { return ::GetWindowDC(m_hWnd); }
  int ReleaseDC(HDC hDC) { return ::ReleaseDC(m_hWnd, hDC); }
  void Print(HDC hDC, DWORD dwFlags) const {
    ::SendMessage(m_hWnd, WM_PRINT, (WPARAM)hDC, dwFlags);
  }
  void PrintClient(HDC hDC, DWORD dwFlags) const {
    ::SendMessage(m_hWnd, WM_PRINTCLIENT, (WPARAM)hDC, dwFlags);
  }
  BOOL UpdateWindow() { return ::UpdateWindow(m_hWnd); }
  void SetRedraw(BOOL bRedraw = TRUE) { ::SendMessage(m_hWnd, WM_SETREDRAW, (WPARAM)bRedraw, 0); }
  BOOL GetUpdateRect(LPRECT lpRect, BOOL bErase = FALSE) {
    return ::GetUpdateRect(m_hWnd, lpRect, bErase);
  }
  int GetUpdateRgn(HRGN hRgn, BOOL bErase = FALSE) { return ::GetUpdateRgn(m_hWnd, hRgn, bErase); }
  BOOL Invalidate(BOOL bErase = TRUE) { return ::InvalidateRect(m_hWnd, NULL, bErase); }
  BOOL InvalidateRect(LPCRECT lpRect, BOOL bErase = TRUE) {
    return ::InvalidateRect(m_hWnd, lpRect, bErase);
  }
  BOOL ValidateRect(LPCRECT lpRect) { return ::ValidateRect(m_hWnd, lpRect); }
  void InvalidateRgn(HRGN hRgn, BOOL bErase = TRUE) { ::InvalidateRgn(m_hWnd, hRgn, bErase); }
  BOOL ValidateRgn(HRGN hRgn) { return ::ValidateRgn(m_hWnd, hRgn); }
  BOOL ShowWindow(int nCmdShow) { return ::ShowWindow(m_hWnd, nCmdShow); }
  BOOL IsWindowVisible() const { return ::IsWindowVisible(m_hWnd); }
  BOOL ShowOwnedPopups(BOOL bShow = TRUE) { return ::ShowOwnedPopups(m_hWnd, bShow); }
  HDC GetDCEx(HRGN hRgnClip, DWORD flags) { return ::GetDCEx(m_hWnd, hRgnClip, flags); }
  BOOL LockWindowUpdate(BOOL bLock = TRUE) { return ::LockWindowUpdate(bLock ? m_hWnd : NULL); }
  BOOL RedrawWindow(LPCRECT lpRectUpdate = NULL, HRGN hRgnUpdate = NULL,
                    UINT flags = RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE) {
    return ::RedrawWindow(m_hWnd, lpRectUpdate, hRgnUpdate, flags);
  }

  // Timers.
  UINT_PTR SetTimer(UINT_PTR nIDEvent, UINT nElapse,
                    void(CALLBACK* lpfnTimer)(HWND, UINT, UINT_PTR, DWORD) = NULL) {
    return ::SetTimer(m_hWnd, nIDEvent, nElapse, (TIMERPROC)lpfnTimer);
  }
  BOOL KillTimer(UINT_PTR nIDEvent) { return ::KillTimer(m_hWnd, nIDEvent); }

  // State.
  BOOL IsWindowEnabled() const { return ::IsWindowEnabled(m_hWnd); }
  BOOL EnableWindow(BOOL bEnable = TRUE) { return ::EnableWindow(m_hWnd, bEnable); }
  HWND SetActiveWindow() { return ::SetActiveWindow(m_hWnd); }
  HWND SetCapture() { return ::SetCapture(m_hWnd); }
  HWND SetFocus() { return ::SetFocus(m_hWnd); }
  BOOL SetForegroundWindow() { return ::SetForegroundWindow(m_hWnd); }

  // Dialog items.
  BOOL CheckDlgButton(int nIDButton, UINT nCheck) {
    return ::CheckDlgButton(m_hWnd, nIDButton, nCheck);
  }
  BOOL CheckRadioButton(int nIDFirstButton, int nIDLastButton, int nIDCheckButton) {
    return ::CheckRadioButton(m_hWnd, nIDFirstButton, nIDLastButton, nIDCheckButton);
  }
  int DlgDirList(LPTSTR lpPathSpec, int nIDListBox, int nIDStaticPath, UINT nFileType) {
    return ::DlgDirList(m_hWnd, lpPathSpec, nIDListBox, nIDStaticPath, nFileType);
  }
  UINT GetDlgItemInt(int nID, BOOL* lpTrans = NULL, BOOL bSigned = TRUE) const {
    return ::GetDlgItemInt(m_hWnd, nID, lpTrans, bSigned);
  }
  UINT GetDlgItemText(int nID, LPTSTR lpStr, int nMaxCount) const {
    return ::GetDlgItemText(m_hWnd, nID, lpStr, nMaxCount);
  }
  BOOL GetDlgItemText(int nID, BSTR& bstrText) const {
    CWindow wnd(::GetDlgItem(m_hWnd, nID));
    return wnd.GetWindowText(&bstrText);
  }
  CWindow GetNextDlgGroupItem(HWND hWndCtl, BOOL bPrevious = FALSE) const {
    return CWindow(::GetNextDlgGroupItem(m_hWnd, hWndCtl, bPrevious));
  }
  CWindow GetNextDlgTabItem(HWND hWndCtl, BOOL bPrevious = FALSE) const {
    return CWindow(::GetNextDlgTabItem(m_hWnd, hWndCtl, bPrevious));
  }
  UINT IsDlgButtonChecked(int nIDButton) const { return ::IsDlgButtonChecked(m_hWnd, nIDButton); }
  LRESULT SendDlgItemMessage(int nID, UINT message, WPARAM wParam = 0, LPARAM lParam = 0) {
    return ::SendDlgItemMessage(m_hWnd, nID, message, wParam, lParam);
  }
  BOOL SetDlgItemInt(int nID, UINT nValue, BOOL bSigned = TRUE) {
    return ::SetDlgItemInt(m_hWnd, nID, nValue, bSigned);
  }
  BOOL SetDlgItemText(int nID, LPCTSTR lpszString) {
    return ::SetDlgItemText(m_hWnd, nID, lpszString);
  }
  CWindow GetDlgItem(int nID) const { return CWindow(::GetDlgItem(m_hWnd, nID)); }
  int GetDlgCtrlID() const { return ::GetDlgCtrlID(m_hWnd); }
  int SetDlgCtrlID(int nID) { return (int)::SetWindowLong(m_hWnd, GWL_ID, nID); }

  // Scrolling.
  int GetScrollPos(int nBar) const { return ::GetScrollPos(m_hWnd, nBar); }
  BOOL GetScrollRange(int nBar, LPINT lpMinPos, LPINT lpMaxPos) const {
    return ::GetScrollRange(m_hWnd, nBar, lpMinPos, lpMaxPos);
  }
  BOOL ScrollWindow(int xAmount, int yAmount, LPCRECT lpRect = NULL, LPCRECT lpClipRect = NULL) {
    return ::ScrollWindow(m_hWnd, xAmount, yAmount, lpRect, lpClipRect);
  }
  int ScrollWindowEx(int dx, int dy, LPCRECT lpRectScroll, LPCRECT lpRectClip, HRGN hRgnUpdate,
                     LPRECT lpRectUpdate, UINT uFlags) {
    return ::ScrollWindowEx(m_hWnd, dx, dy, lpRectScroll, lpRectClip, hRgnUpdate, lpRectUpdate,
                            uFlags);
  }
  int SetScrollPos(int nBar, int nPos, BOOL bRedraw = TRUE) {
    return ::SetScrollPos(m_hWnd, nBar, nPos, bRedraw);
  }
  BOOL SetScrollRange(int nBar, int nMinPos, int nMaxPos, BOOL bRedraw = TRUE) {
    return ::SetScrollRange(m_hWnd, nBar, nMinPos, nMaxPos, bRedraw);
  }
  BOOL ShowScrollBar(UINT nBar, BOOL bShow = TRUE) { return ::ShowScrollBar(m_hWnd, nBar, bShow); }
  BOOL EnableScrollBar(UINT uSBFlags, UINT uArrowFlags = ESB_ENABLE_BOTH) {
    return ::EnableScrollBar(m_hWnd, uSBFlags, uArrowFlags);
  }
  BOOL GetScrollInfo(int nBar, LPSCROLLINFO lpScrollInfo) {
    return ::GetScrollInfo(m_hWnd, nBar, lpScrollInfo);
  }
  int SetScrollInfo(int nBar, LPSCROLLINFO lpScrollInfo, BOOL bRedraw = TRUE) {
    return ::SetScrollInfo(m_hWnd, nBar, lpScrollInfo, bRedraw);
  }

  // The window tree.
  CWindow ChildWindowFromPoint(POINT point) const {
    return CWindow(::ChildWindowFromPoint(m_hWnd, point));
  }
  CWindow ChildWindowFromPointEx(POINT point, UINT uFlags) const {
    return CWindow(::ChildWindowFromPointEx(m_hWnd, point, uFlags));
  }
  CWindow GetTopWindow() const { return CWindow(::GetTopWindow(m_hWnd)); }
  CWindow GetWindow(UINT nCmd) const { return CWindow(::GetWindow(m_hWnd, nCmd)); }
  CWindow GetLastActivePopup() const { return CWindow(::GetLastActivePopup(m_hWnd)); }
  BOOL IsChild(HWND hWnd) const { return ::IsChild(m_hWnd, hWnd); }
  CWindow GetParent() const { return CWindow(::GetParent(m_hWnd)); }
  CWindow SetParent(HWND hWndNewParent) { return CWindow(::SetParent(m_hWnd, hWndNewParent)); }
  // The first ancestor that is not a child window.
  HWND GetTopLevelParent() const {
    HWND hWndParent = m_hWnd;
    HWND hWndTmp;
    while ((hWndTmp = ::GetParent(hWndParent)) != NULL &&
           (::GetWindowLong(hWndParent, GWL_STYLE) & WS_CHILD))
      hWndParent = hWndTmp;
    return hWndParent;
  }
  HWND GetTopLevelWindow() const {
    HWND hWndParent;
    HWND hWndTmp = m_hWnd;
    do {
      hWndParent = hWndTmp;
      hWndTmp = (::GetWindowLong(hWndParent, GWL_STYLE) & WS_CHILD) ? ::GetParent(hWndParent)
                                                                   : ::GetWindow(hWndParent, GW_OWNER);
    } while (hWndTmp != NULL);
    return hWndParent;
  }

  // Misc.
  int GetDlgItemTextLength(int nID) const {
    return ::GetWindowTextLength(::GetDlgItem(m_hWnd, nID));
  }
  BOOL FlashWindow(BOOL bInvert) { return ::FlashWindow(m_hWnd, bInvert); }
  int MessageBox(LPCTSTR lpszText, LPCTSTR lpszCaption = _T(""), UINT nType = MB_OK) {
    return ::MessageBox(m_hWnd, lpszText, lpszCaption, nType);
  }
  BOOL ChangeClipboardChain(HWND hWndNewNext) { return ::ChangeClipboardChain(m_hWnd, hWndNewNext); }
  HWND SetClipboardViewer() { return ::SetClipboardViewer(m_hWnd); }
  BOOL OpenClipboard() { return ::OpenClipboard(m_hWnd); }
  BOOL CreateCaret(HBITMAP hBitmap) { return ::CreateCaret(m_hWnd, hBitmap, 0, 0); }
  BOOL CreateSolidCaret(int nWidth, int nHeight) {
    return ::CreateCaret(m_hWnd, (HBITMAP)0, nWidth, nHeight);
  }
  BOOL CreateGrayCaret(int nWidth, int nHeight) {
    return ::CreateCaret(m_hWnd, (HBITMAP)1, nWidth, nHeight);
  }
  BOOL HideCaret() { return ::HideCaret(m_hWnd); }
  BOOL ShowCaret() { return ::ShowCaret(m_hWnd); }
  void DragAcceptFiles(BOOL bAccept = TRUE);
  HICON SetIcon(HICON hIcon, BOOL bBigIcon, int) { return SetIcon(hIcon, bBigIcon); }
  BOOL WinHelp(LPCTSTR lpszHelp, UINT nCmd = HELP_CONTEXT, DWORD_PTR dwData = 0) {
    return ::WinHelp(m_hWnd, lpszHelp, nCmd, dwData);
  }
  BOOL SetWindowContextHelpId(DWORD dwContextHelpId) {
    return ::SetWindowContextHelpId(m_hWnd, dwContextHelpId);
  }
  DWORD GetWindowContextHelpId() const { return ::GetWindowContextHelpId(m_hWnd); }
  int SetHotKey(WORD wVirtualKeyCode, WORD wModifiers) {
    return (int)::SendMessage(m_hWnd, WM_SETHOTKEY, MAKEWORD(wVirtualKeyCode, wModifiers), 0);
  }
  DWORD GetHotKey() const { return (DWORD)::SendMessage(m_hWnd, WM_GETHOTKEY, 0, 0); }
  BOOL IsWindow() const { return ::IsWindow(m_hWnd); }
  BOOL IsWindowUnicode() const { return ::IsWindowUnicode(m_hWnd); }
  BOOL IsParentDialog() {
    TCHAR szBuf[8];
    if (::GetClassName(GetParent(), szBuf, 8) == 0) return FALSE;
    return lstrcmp(szBuf, _T("#32770")) == 0;
  }
  BOOL ShowWindowAsync(int nCmdShow) { return ::ShowWindowAsync(m_hWnd, nCmdShow); }
  HWND GetDescendantWindow(int nID) const {
    HWND hWndChild = ::GetDlgItem(m_hWnd, nID);
    if (hWndChild != NULL) {
      if (::GetTopWindow(hWndChild) != NULL) {
        CWindow wnd(hWndChild);
        HWND h = wnd.GetDescendantWindow(nID);
        if (h != NULL) return h;
      }
      return hWndChild;
    }
    for (hWndChild = ::GetTopWindow(m_hWnd); hWndChild != NULL;
         hWndChild = ::GetNextWindow(hWndChild, GW_HWNDNEXT)) {
      CWindow wnd(hWndChild);
      HWND h = wnd.GetDescendantWindow(nID);
      if (h != NULL) return h;
    }
    return NULL;
  }
  // Centers the window over another (its parent or owner by default),
  // inside the work area.
  BOOL CenterWindow(HWND hWndCenter = NULL) {
    DWORD dwStyle = GetStyle();
    if (hWndCenter == NULL) {
      if (dwStyle & WS_CHILD)
        hWndCenter = ::GetParent(m_hWnd);
      else
        hWndCenter = ::GetWindow(m_hWnd, GW_OWNER);
    }
    RECT rcDlg;
    ::GetWindowRect(m_hWnd, &rcDlg);
    RECT rcArea, rcCenter;
    if (!(dwStyle & WS_CHILD)) {
      if (hWndCenter != NULL) {
        DWORD dwStyleCenter = ::GetWindowLong(hWndCenter, GWL_STYLE);
        if (!(dwStyleCenter & WS_VISIBLE) || (dwStyleCenter & WS_MINIMIZE)) hWndCenter = NULL;
      }
      ::SystemParametersInfo(SPI_GETWORKAREA, 0, &rcArea, 0);
      if (hWndCenter == NULL)
        rcCenter = rcArea;
      else
        ::GetWindowRect(hWndCenter, &rcCenter);
    } else {
      HWND hWndParent = ::GetParent(m_hWnd);
      ::GetClientRect(hWndParent, &rcArea);
      ::GetClientRect(hWndCenter, &rcCenter);
      ::MapWindowPoints(hWndCenter, hWndParent, (POINT*)&rcCenter, 2);
    }
    int w = rcDlg.right - rcDlg.left, h = rcDlg.bottom - rcDlg.top;
    int xLeft = (rcCenter.left + rcCenter.right) / 2 - w / 2;
    int yTop = (rcCenter.top + rcCenter.bottom) / 2 - h / 2;
    if (xLeft + w > rcArea.right) xLeft = rcArea.right - w;
    if (xLeft < rcArea.left) xLeft = rcArea.left;
    if (yTop + h > rcArea.bottom) yTop = rcArea.bottom - h;
    if (yTop < rcArea.top) yTop = rcArea.top;
    return ::SetWindowPos(m_hWnd, NULL, xLeft, yTop, -1, -1,
                          SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  BOOL ResizeClient(int nWidth, int nHeight, BOOL bRedraw = TRUE) {
    RECT rcWnd;
    if (!GetClientRect(&rcWnd)) return FALSE;
    if (nWidth != -1) rcWnd.right = nWidth;
    if (nHeight != -1) rcWnd.bottom = nHeight;
    if (!::AdjustWindowRectEx(&rcWnd, GetStyle(),
                              (!(GetStyle() & WS_CHILD) && GetMenu() != NULL), GetExStyle()))
      return FALSE;
    UINT uFlags = SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE;
    if (!bRedraw) uFlags |= SWP_NOREDRAW;
    return SetWindowPos(NULL, 0, 0, rcWnd.right - rcWnd.left, rcWnd.bottom - rcWnd.top, uFlags);
  }
  int GetWindowRgn(HRGN hRgn) { return ::GetWindowRgn(m_hWnd, hRgn); }
  int SetWindowRgn(HRGN hRgn, BOOL bRedraw = FALSE) { return ::SetWindowRgn(m_hWnd, hRgn, bRedraw); }
  HDWP DeferWindowPos(HDWP hWinPosInfo, HWND hWndInsertAfter, int x, int y, int cx, int cy,
                      UINT uFlags) {
    return ::DeferWindowPos(hWinPosInfo, m_hWnd, hWndInsertAfter, x, y, cx, cy, uFlags);
  }
  DWORD GetWindowThreadID() { return ::GetWindowThreadProcessId(m_hWnd, NULL); }
  DWORD GetWindowProcessID() {
    DWORD dwProcessID = 0;
    ::GetWindowThreadProcessId(m_hWnd, &dwProcessID);
    return dwProcessID;
  }
  BOOL IsWindowRtl() const { return (GetExStyle() & WS_EX_LAYOUTRTL) != 0; }

private:
  BOOL ModifyLong(int nIndex, DWORD dwRemove, DWORD dwAdd, UINT nFlags) {
    DWORD dw = (DWORD)::GetWindowLong(m_hWnd, nIndex);
    DWORD dwNew = (dw & ~dwRemove) | dwAdd;
    if (dw == dwNew) return FALSE;
    ::SetWindowLong(m_hWnd, nIndex, (LONG)dwNew);
    if (nFlags != 0)
      ::SetWindowPos(m_hWnd, NULL, 0, 0, 0, 0,
                     SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | nFlags);
    return TRUE;
  }
};

// DragAcceptFiles is in shell32, which atlbase.h links.
inline void CWindow::DragAcceptFiles(BOOL bAccept) { ::DragAcceptFiles(m_hWnd, bAccept); }

// ---------------------------------------------------------------------
// Message maps.

// The message being handled (CWindowImpl::GetCurrentMessage).
struct _ATL_MSG : public MSG {
  BOOL bHandled;
  _ATL_MSG() : bHandled(TRUE) {
    hwnd = NULL;
    message = 0;
    wParam = 0;
    lParam = 0;
    time = 0;
    pt.x = pt.y = 0;
  }
  _ATL_MSG(HWND hWnd, UINT uMsg, WPARAM wParamIn, LPARAM lParamIn, BOOL bHandledIn = TRUE)
      : bHandled(bHandledIn) {
    hwnd = hWnd;
    message = uMsg;
    wParam = wParamIn;
    lParam = lParamIn;
    time = 0;
    pt.x = pt.y = 0;
  }
};

class ATL_NO_VTABLE CMessageMap {
public:
  virtual BOOL ProcessWindowMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                    LRESULT& lResult, DWORD dwMsgMapID) = 0;
};

// (ProcessWindowMessage overrides CMessageMap's when the class has it,
// so it cannot say override.)
#define BEGIN_MSG_MAP(theClass)                                                     \
public:                                                                             \
  _Pragma("clang diagnostic push")                                                  \
  _Pragma("clang diagnostic ignored \"-Winconsistent-missing-override\"")            \
  BOOL ProcessWindowMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,     \
                            LRESULT& lResult, DWORD dwMsgMapID = 0) {               \
    BOOL bHandled = TRUE;                                                           \
    (void)hWnd;                                                                     \
    (void)uMsg;                                                                     \
    (void)wParam;                                                                   \
    (void)lParam;                                                                   \
    (void)lResult;                                                                  \
    (void)bHandled;                                                                 \
    switch (dwMsgMapID) {                                                           \
    case 0:

#define ALT_MSG_MAP(msgMapID) \
  break;                      \
  case msgMapID:

#define END_MSG_MAP() \
  break;              \
  default:            \
    break;            \
    }                 \
    return FALSE;     \
    }                 \
  _Pragma("clang diagnostic pop")

#define DECLARE_EMPTY_MSG_MAP()                                                   \
public:                                                                           \
  BOOL ProcessWindowMessage(HWND, UINT, WPARAM, LPARAM, LRESULT&, DWORD = 0) {    \
    return FALSE;                                                                 \
  }

// LRESULT func(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
#define MESSAGE_HANDLER(msg, func)                         \
  if (uMsg == msg) {                                       \
    bHandled = TRUE;                                       \
    lResult = func(uMsg, wParam, lParam, bHandled);        \
    if (bHandled) return TRUE;                             \
  }
#define MESSAGE_RANGE_HANDLER(msgFirst, msgLast, func)     \
  if (uMsg >= msgFirst && uMsg <= msgLast) {               \
    bHandled = TRUE;                                       \
    lResult = func(uMsg, wParam, lParam, bHandled);        \
    if (bHandled) return TRUE;                             \
  }

// LRESULT func(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
#define COMMAND_HANDLER(id, code, func)                                                  \
  if (uMsg == WM_COMMAND && id == LOWORD(wParam) && code == HIWORD(wParam)) {            \
    bHandled = TRUE;                                                                     \
    lResult = func(HIWORD(wParam), LOWORD(wParam), (HWND)lParam, bHandled);              \
    if (bHandled) return TRUE;                                                           \
  }
#define COMMAND_ID_HANDLER(id, func)                                                     \
  if (uMsg == WM_COMMAND && id == LOWORD(wParam)) {                                      \
    bHandled = TRUE;                                                                     \
    lResult = func(HIWORD(wParam), LOWORD(wParam), (HWND)lParam, bHandled);              \
    if (bHandled) return TRUE;                                                           \
  }
#define COMMAND_CODE_HANDLER(code, func)                                                 \
  if (uMsg == WM_COMMAND && code == HIWORD(wParam)) {                                    \
    bHandled = TRUE;                                                                     \
    lResult = func(HIWORD(wParam), LOWORD(wParam), (HWND)lParam, bHandled);              \
    if (bHandled) return TRUE;                                                           \
  }
#define COMMAND_RANGE_HANDLER(idFirst, idLast, func)                                     \
  if (uMsg == WM_COMMAND && LOWORD(wParam) >= idFirst && LOWORD(wParam) <= idLast) {     \
    bHandled = TRUE;                                                                     \
    lResult = func(HIWORD(wParam), LOWORD(wParam), (HWND)lParam, bHandled);              \
    if (bHandled) return TRUE;                                                           \
  }
#define COMMAND_RANGE_CODE_HANDLER(idFirst, idLast, code, func)                          \
  if (uMsg == WM_COMMAND && code == HIWORD(wParam) && LOWORD(wParam) >= idFirst &&       \
      LOWORD(wParam) <= idLast) {                                                        \
    bHandled = TRUE;                                                                     \
    lResult = func(HIWORD(wParam), LOWORD(wParam), (HWND)lParam, bHandled);              \
    if (bHandled) return TRUE;                                                           \
  }

// LRESULT func(int idCtrl, LPNMHDR pnmh, BOOL& bHandled)
#define NOTIFY_HANDLER(id, cd, func)                                                     \
  if (uMsg == WM_NOTIFY && id == ((LPNMHDR)lParam)->idFrom &&                            \
      cd == ((LPNMHDR)lParam)->code) {                                                   \
    bHandled = TRUE;                                                                     \
    lResult = func((int)wParam, (LPNMHDR)lParam, bHandled);                              \
    if (bHandled) return TRUE;                                                           \
  }
#define NOTIFY_ID_HANDLER(id, func)                                                      \
  if (uMsg == WM_NOTIFY && id == ((LPNMHDR)lParam)->idFrom) {                            \
    bHandled = TRUE;                                                                     \
    lResult = func((int)wParam, (LPNMHDR)lParam, bHandled);                              \
    if (bHandled) return TRUE;                                                           \
  }
#define NOTIFY_CODE_HANDLER(cd, func)                                                    \
  if (uMsg == WM_NOTIFY && cd == ((LPNMHDR)lParam)->code) {                              \
    bHandled = TRUE;                                                                     \
    lResult = func((int)wParam, (LPNMHDR)lParam, bHandled);                              \
    if (bHandled) return TRUE;                                                           \
  }
#define NOTIFY_RANGE_HANDLER(idFirst, idLast, func)                                      \
  if (uMsg == WM_NOTIFY && ((LPNMHDR)lParam)->idFrom >= idFirst &&                       \
      ((LPNMHDR)lParam)->idFrom <= idLast) {                                             \
    bHandled = TRUE;                                                                     \
    lResult = func((int)wParam, (LPNMHDR)lParam, bHandled);                              \
    if (bHandled) return TRUE;                                                           \
  }
#define NOTIFY_RANGE_CODE_HANDLER(idFirst, idLast, cd, func)                             \
  if (uMsg == WM_NOTIFY && cd == ((LPNMHDR)lParam)->code &&                              \
      ((LPNMHDR)lParam)->idFrom >= idFirst && ((LPNMHDR)lParam)->idFrom <= idLast) {     \
    bHandled = TRUE;                                                                     \
    lResult = func((int)wParam, (LPNMHDR)lParam, bHandled);                              \
    if (bHandled) return TRUE;                                                           \
  }

// Another map: a base class's, a member's, or one of their alternate
// maps.
#define CHAIN_MSG_MAP(theChainClass)                                                     \
  {                                                                                      \
    if (theChainClass::ProcessWindowMessage(hWnd, uMsg, wParam, lParam, lResult))        \
      return TRUE;                                                                       \
  }
#define CHAIN_MSG_MAP_MEMBER(theChainMember)                                             \
  {                                                                                      \
    if ((theChainMember).ProcessWindowMessage(hWnd, uMsg, wParam, lParam, lResult))      \
      return TRUE;                                                                       \
  }
#define CHAIN_MSG_MAP_ALT(theChainClass, msgMapID)                                          \
  {                                                                                         \
    if (theChainClass::ProcessWindowMessage(hWnd, uMsg, wParam, lParam, lResult, msgMapID)) \
      return TRUE;                                                                          \
  }
#define CHAIN_MSG_MAP_ALT_MEMBER(theChainMember, msgMapID)                                  \
  {                                                                                         \
    if ((theChainMember).ProcessWindowMessage(hWnd, uMsg, wParam, lParam, lResult,          \
                                              msgMapID))                                    \
      return TRUE;                                                                          \
  }

// Notifications sent back to the controls that sent them (as OCM_
// messages), or on to the parent.
#define REFLECT_NOTIFICATIONS()                                               \
  {                                                                           \
    bHandled = TRUE;                                                          \
    lResult = this->ReflectNotifications(uMsg, wParam, lParam, bHandled);     \
    if (bHandled) return TRUE;                                                \
  }
#define FORWARD_NOTIFICATIONS()                                               \
  {                                                                           \
    bHandled = TRUE;                                                          \
    lResult = this->ForwardNotifications(uMsg, wParam, lParam, bHandled);     \
    if (bHandled) return TRUE;                                                \
  }
#define DEFAULT_REFLECTION_HANDLER()                                                 \
  if (this->DefaultReflectionHandler(hWnd, uMsg, wParam, lParam, lResult)) return TRUE;

#define REFLECTED_COMMAND_HANDLER(id, code, func)                                        \
  if (uMsg == OCM_COMMAND && id == LOWORD(wParam) && code == HIWORD(wParam)) {           \
    bHandled = TRUE;                                                                     \
    lResult = func(HIWORD(wParam), LOWORD(wParam), (HWND)lParam, bHandled);              \
    if (bHandled) return TRUE;                                                           \
  }
#define REFLECTED_COMMAND_ID_HANDLER(id, func)                                           \
  if (uMsg == OCM_COMMAND && id == LOWORD(wParam)) {                                     \
    bHandled = TRUE;                                                                     \
    lResult = func(HIWORD(wParam), LOWORD(wParam), (HWND)lParam, bHandled);              \
    if (bHandled) return TRUE;                                                           \
  }
#define REFLECTED_COMMAND_CODE_HANDLER(code, func)                                       \
  if (uMsg == OCM_COMMAND && code == HIWORD(wParam)) {                                   \
    bHandled = TRUE;                                                                     \
    lResult = func(HIWORD(wParam), LOWORD(wParam), (HWND)lParam, bHandled);              \
    if (bHandled) return TRUE;                                                           \
  }
#define REFLECTED_NOTIFY_HANDLER(id, cd, func)                                           \
  if (uMsg == OCM_NOTIFY && id == ((LPNMHDR)lParam)->idFrom &&                           \
      cd == ((LPNMHDR)lParam)->code) {                                                   \
    bHandled = TRUE;                                                                     \
    lResult = func((int)wParam, (LPNMHDR)lParam, bHandled);                              \
    if (bHandled) return TRUE;                                                           \
  }
#define REFLECTED_NOTIFY_ID_HANDLER(id, func)                                            \
  if (uMsg == OCM_NOTIFY && id == ((LPNMHDR)lParam)->idFrom) {                           \
    bHandled = TRUE;                                                                     \
    lResult = func((int)wParam, (LPNMHDR)lParam, bHandled);                              \
    if (bHandled) return TRUE;                                                           \
  }
#define REFLECTED_NOTIFY_CODE_HANDLER(cd, func)                                          \
  if (uMsg == OCM_NOTIFY && cd == ((LPNMHDR)lParam)->code) {                             \
    bHandled = TRUE;                                                                     \
    lResult = func((int)wParam, (LPNMHDR)lParam, bHandled);                              \
    if (bHandled) return TRUE;                                                           \
  }

// ---------------------------------------------------------------------
// Window styles by class: CWinTraits<style, exstyle>, the style used
// when Create is given none.

template <DWORD t_dwStyle = 0, DWORD t_dwExStyle = 0>
class CWinTraits {
public:
  static DWORD GetWndStyle(DWORD dwStyle) { return dwStyle == 0 ? t_dwStyle : dwStyle; }
  static DWORD GetWndExStyle(DWORD dwExStyle) { return dwExStyle == 0 ? t_dwExStyle : dwExStyle; }
};

typedef CWinTraits<WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0> CControlWinTraits;
typedef CWinTraits<WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                   WS_EX_APPWINDOW | WS_EX_WINDOWEDGE>
    CFrameWinTraits;
typedef CWinTraits<WS_OVERLAPPEDWINDOW | WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                   WS_EX_MDICHILD>
    CMDIChildWinTraits;
typedef CWinTraits<0, 0> CNullTraits;

// The style always has these, besides those of TWinTraits.
template <DWORD t_dwStyle = 0, DWORD t_dwExStyle = 0, class TWinTraits = CControlWinTraits>
class CWinTraitsOR {
public:
  static DWORD GetWndStyle(DWORD dwStyle) {
    return dwStyle | t_dwStyle | TWinTraits::GetWndStyle(dwStyle);
  }
  static DWORD GetWndExStyle(DWORD dwExStyle) {
    return dwExStyle | t_dwExStyle | TWinTraits::GetWndExStyle(dwExStyle);
  }
};

// ---------------------------------------------------------------------
// The thunk: a few bytes of code, one per window, that put the C++
// object in place of the HWND and jump to the class's window procedure.

class CStdCallThunk {
public:
  void* pThunk;
  CStdCallThunk() : pThunk(NULL) {}
  ~CStdCallThunk() {
    if (pThunk != NULL) Free(pThunk);
  }
  BOOL Init(DWORD_PTR proc, void* pThis) {
    if (pThunk == NULL) pThunk = Alloc();
    if (pThunk == NULL) return FALSE;
    BYTE* p = (BYTE*)pThunk;
#if defined(__i386__)
    // mov dword ptr [esp+4], pThis ; jmp proc
    p[0] = 0xC7;
    p[1] = 0x44;
    p[2] = 0x24;
    p[3] = 0x04;
    DWORD dwThis = (DWORD)(DWORD_PTR)pThis;
    memcpy(p + 4, &dwThis, 4);
    p[8] = 0xE9;
    DWORD dwRel = (DWORD)(proc - ((DWORD_PTR)p + 13));
    memcpy(p + 9, &dwRel, 4);
#elif defined(__x86_64__)
    // mov rcx, pThis ; mov rax, proc ; jmp rax
    p[0] = 0x48;
    p[1] = 0xB9;
    memcpy(p + 2, &pThis, 8);
    p[10] = 0x48;
    p[11] = 0xB8;
    memcpy(p + 12, &proc, 8);
    p[20] = 0xFF;
    p[21] = 0xE0;
#else
#error "ATL window thunks are written for x86 and x86-64"
#endif
    ::FlushInstructionCache(::GetCurrentProcess(), pThunk, kSize);
    return TRUE;
  }
  void* GetCodeAddress() { return pThunk; }

private:
  enum { kSize = 32 };
  // Thunks come from pages that may run code, kSize bytes each.
  struct Pool {
    CRITICAL_SECTION cs;
    void* pFree;
    Pool() : pFree(NULL) { ::InitializeCriticalSection(&cs); }
  };
  static Pool& GetPool() {
    static Pool pool;
    return pool;
  }
  static void* Alloc() {
    Pool& pool = GetPool();
    ::EnterCriticalSection(&pool.cs);
    if (pool.pFree == NULL) {
      SYSTEM_INFO si;
      ::GetSystemInfo(&si);
      BYTE* pPage = (BYTE*)::VirtualAlloc(NULL, si.dwPageSize, MEM_COMMIT | MEM_RESERVE,
                                          PAGE_EXECUTE_READWRITE);
      if (pPage != NULL) {
        for (DWORD off = 0; off + kSize <= si.dwPageSize; off += kSize) {
          *(void**)(pPage + off) = pool.pFree;
          pool.pFree = pPage + off;
        }
      }
    }
    void* p = pool.pFree;
    if (p != NULL) pool.pFree = *(void**)p;
    ::LeaveCriticalSection(&pool.cs);
    return p;
  }
  static void Free(void* p) {
    Pool& pool = GetPool();
    ::EnterCriticalSection(&pool.cs);
    *(void**)p = pool.pFree;
    pool.pFree = p;
    ::LeaveCriticalSection(&pool.cs);
  }
  CStdCallThunk(const CStdCallThunk&);
  CStdCallThunk& operator=(const CStdCallThunk&);
};

// The object a window being created belongs to, until its first message.
struct _AtlCreateWndData {
  void* m_pThis;
  DWORD m_dwThreadID;
  _AtlCreateWndData* m_pNext;
};

class CWndProcThunk {
public:
  _AtlCreateWndData cd;
  CStdCallThunk thunk;
  BOOL Init(WNDPROC proc, void* pThis) { return thunk.Init((DWORD_PTR)proc, pThis); }
  WNDPROC GetWNDPROC() { return (WNDPROC)thunk.GetCodeAddress(); }
};

class CDialogProcThunk : public CWndProcThunk {
public:
  DLGPROC GetDLGPROC() { return (DLGPROC)thunk.GetCodeAddress(); }
};

// The module's window state: the objects whose windows are being made.
class CAtlWinModule {
public:
  CComCriticalSection m_csWindowCreate;
  _AtlCreateWndData* m_pCreateWndList;
  CAtlWinModule() : m_pCreateWndList(NULL) { m_csWindowCreate.Init(); }
  ~CAtlWinModule() { m_csWindowCreate.Term(); }
  void AddCreateWndData(_AtlCreateWndData* pData, void* pObject) {
    pData->m_pThis = pObject;
    pData->m_dwThreadID = ::GetCurrentThreadId();
    m_csWindowCreate.Lock();
    pData->m_pNext = m_pCreateWndList;
    m_pCreateWndList = pData;
    m_csWindowCreate.Unlock();
  }
  void* ExtractCreateWndData() {
    void* pv = NULL;
    m_csWindowCreate.Lock();
    DWORD dwThreadID = ::GetCurrentThreadId();
    for (_AtlCreateWndData** pp = &m_pCreateWndList; *pp != NULL; pp = &(*pp)->m_pNext) {
      if ((*pp)->m_dwThreadID == dwThreadID) {
        pv = (*pp)->m_pThis;
        *pp = (*pp)->m_pNext;
        break;
      }
    }
    m_csWindowCreate.Unlock();
    return pv;
  }
};
inline CAtlWinModule _AtlWinModule;

// ---------------------------------------------------------------------
// Window classes: DECLARE_WND_CLASS and DECLARE_WND_SUPERCLASS give a
// CWindowImpl its class, registered the first time it is used.

struct _ATL_WNDCLASSINFO {
  WNDCLASSEX m_wc;
  LPCTSTR m_lpszOrigName;  // the class superclassed, or NULL
  WNDPROC pWndProc;        // its window procedure
  LPCTSTR m_lpszCursorID;
  BOOL m_bSystemCursor;
  ATOM m_atom;
  TCHAR m_szAutoName[5 + sizeof(void*) * 2 + 1];

  ATOM Register(WNDPROC* pProc) {
    if (m_atom == 0) {
      _AtlWinModule.m_csWindowCreate.Lock();
      if (m_atom == 0) {
        HINSTANCE hInst = _AtlBaseModule.GetModuleInstance();
        if (m_lpszOrigName != NULL) {
          // A superclass: the original class's look, with this procedure.
          WNDCLASSEX wc;
          wc.cbSize = sizeof(WNDCLASSEX);
          if (!::GetClassInfoEx(NULL, m_lpszOrigName, &wc) &&
              !::GetClassInfoEx(hInst, m_lpszOrigName, &wc)) {
            _AtlWinModule.m_csWindowCreate.Unlock();
            return 0;
          }
          pWndProc = wc.lpfnWndProc;
          WNDPROC pStart = m_wc.lpfnWndProc;
          LPCTSTR lpszName = m_wc.lpszClassName;
          m_wc = wc;
          m_wc.cbSize = sizeof(WNDCLASSEX);
          m_wc.lpfnWndProc = pStart;
          m_wc.lpszClassName = lpszName;
          m_wc.style &= ~CS_GLOBALCLASS;
          m_wc.hInstance = hInst;
        } else {
          m_wc.hInstance = hInst;
          if (m_lpszCursorID != NULL)
            m_wc.hCursor = ::LoadCursor(m_bSystemCursor ? NULL : _AtlBaseModule.GetResourceInstance(),
                                        m_lpszCursorID);
        }
        if (m_wc.lpszClassName == NULL || m_wc.lpszClassName[0] == 0) {
          wsprintf(m_szAutoName, _T("ATL:%p"), (void*)&m_wc);
          m_wc.lpszClassName = m_szAutoName;
        }
        // A class of the same name already registered (by another copy of
        // this one) is the one used.
        WNDCLASSEX wcTmp;
        wcTmp.cbSize = sizeof(WNDCLASSEX);
        m_atom = (ATOM)::GetClassInfoEx(m_wc.hInstance, m_wc.lpszClassName, &wcTmp);
        if (m_atom == 0) m_atom = ::RegisterClassEx(&m_wc);
      }
      _AtlWinModule.m_csWindowCreate.Unlock();
    }
    if (m_lpszOrigName != NULL && pProc != NULL) *pProc = pWndProc;
    return m_atom;
  }
};
typedef _ATL_WNDCLASSINFO CWndClassInfo;

#define DECLARE_WND_CLASS(WndClassName)                                                  \
  static ATL::CWndClassInfo& GetWndClassInfo() {                                         \
    static ATL::CWndClassInfo wc = {                                                     \
        {sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, StartWindowProc, 0, 0, \
         NULL, NULL, NULL, (HBRUSH)(COLOR_WINDOW + 1), NULL, WndClassName, NULL},        \
        NULL, NULL, IDC_ARROW, TRUE, 0, _T("")};                                         \
    return wc;                                                                           \
  }
#define DECLARE_WND_CLASS2(WndClassName, EnclosingClass) DECLARE_WND_CLASS(WndClassName)
#define DECLARE_WND_CLASS_EX(WndClassName, style, bkgnd)                                 \
  static ATL::CWndClassInfo& GetWndClassInfo() {                                         \
    static ATL::CWndClassInfo wc = {                                                     \
        {sizeof(WNDCLASSEX), style, StartWindowProc, 0, 0, NULL, NULL, NULL,             \
         (HBRUSH)(bkgnd + 1), NULL, WndClassName, NULL},                                 \
        NULL, NULL, IDC_ARROW, TRUE, 0, _T("")};                                         \
    return wc;                                                                           \
  }
#define DECLARE_WND_SUPERCLASS(WndClassName, OrigWndClassName)                           \
  static ATL::CWndClassInfo& GetWndClassInfo() {                                         \
    static ATL::CWndClassInfo wc = {                                                     \
        {sizeof(WNDCLASSEX), 0, StartWindowProc, 0, 0, NULL, NULL, NULL, NULL, NULL,     \
         WndClassName, NULL},                                                            \
        OrigWndClassName, NULL, NULL, TRUE, 0, _T("")};                                  \
    return wc;                                                                           \
  }
#define DECLARE_WND_SUPERCLASS2(WndClassName, EnclosingClass, OrigWndClassName) \
  DECLARE_WND_SUPERCLASS(WndClassName, OrigWndClassName)

// ---------------------------------------------------------------------
// The part of CWindowImpl and CDialogImpl that does not depend on the
// window procedure: the message, reflection.

#define WINSTATE_DESTROYED 0x00000001

template <class TBase = CWindow>
class ATL_NO_VTABLE CWindowImplRoot : public TBase, public CMessageMap {
public:
  CWndProcThunk m_thunk;
  const _ATL_MSG* m_pCurrentMsg;
  DWORD m_dwState;

  CWindowImplRoot() : m_pCurrentMsg(NULL), m_dwState(0) {}
  virtual ~CWindowImplRoot() {}

  const _ATL_MSG* GetCurrentMessage() const { return m_pCurrentMsg; }
  BOOL IsMsgHandled() const { return m_pCurrentMsg != NULL && m_pCurrentMsg->bHandled; }
  void SetMsgHandled(BOOL bHandled) {
    if (m_pCurrentMsg != NULL) const_cast<_ATL_MSG*>(m_pCurrentMsg)->bHandled = bHandled;
  }

  // The notification messages, on to the parent.
  LRESULT ForwardNotifications(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    switch (uMsg) {
    case WM_COMMAND:
    case WM_NOTIFY:
    case WM_PARENTNOTIFY:
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
    case WM_COMPAREITEM:
    case WM_DELETEITEM:
    case WM_VKEYTOITEM:
    case WM_CHARTOITEM:
    case WM_HSCROLL:
    case WM_VSCROLL:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORMSGBOX:
    case WM_CTLCOLORSCROLLBAR:
    case WM_CTLCOLORSTATIC:
      return ::SendMessage(::GetParent(this->m_hWnd), uMsg, wParam, lParam);
    default:
      bHandled = FALSE;
      return 0;
    }
  }

  // The notification messages, back to the control that sent them as
  // OCM_ messages.
  LRESULT ReflectNotifications(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    HWND hWndChild = NULL;
    switch (uMsg) {
    case WM_COMMAND:
      if (lParam != 0) hWndChild = (HWND)lParam;
      break;
    case WM_NOTIFY:
      hWndChild = ((LPNMHDR)lParam)->hwndFrom;
      break;
    case WM_PARENTNOTIFY:
      switch (LOWORD(wParam)) {
      case WM_CREATE:
      case WM_DESTROY:
        hWndChild = (HWND)lParam;
        break;
      default:
        hWndChild = ::GetDlgItem(this->m_hWnd, HIWORD(wParam));
        break;
      }
      break;
    case WM_DRAWITEM:
      if (wParam) hWndChild = ((LPDRAWITEMSTRUCT)lParam)->hwndItem;
      break;
    case WM_MEASUREITEM:
      if (wParam) hWndChild = ::GetDlgItem(this->m_hWnd, ((LPMEASUREITEMSTRUCT)lParam)->CtlID);
      break;
    case WM_COMPAREITEM:
      if (wParam) hWndChild = ((LPCOMPAREITEMSTRUCT)lParam)->hwndItem;
      break;
    case WM_DELETEITEM:
      if (wParam) hWndChild = ((LPDELETEITEMSTRUCT)lParam)->hwndItem;
      break;
    case WM_VKEYTOITEM:
    case WM_CHARTOITEM:
    case WM_HSCROLL:
    case WM_VSCROLL:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORMSGBOX:
    case WM_CTLCOLORSCROLLBAR:
    case WM_CTLCOLORSTATIC:
      hWndChild = (HWND)lParam;
      break;
    default:
      break;
    }
    if (hWndChild == NULL) {
      bHandled = FALSE;
      return 1;
    }
    return ::SendMessage(hWndChild, OCM__BASE + uMsg, wParam, lParam);
  }

  // What a reflected message does when the control's map does not
  // handle it: what the original would have done.
  static BOOL DefaultReflectionHandler(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                       LRESULT& lResult) {
    switch (uMsg) {
    case OCM_COMMAND:
    case OCM_NOTIFY:
    case OCM_PARENTNOTIFY:
    case OCM_DRAWITEM:
    case OCM_MEASUREITEM:
    case OCM_COMPAREITEM:
    case OCM_DELETEITEM:
    case OCM_VKEYTOITEM:
    case OCM_CHARTOITEM:
    case OCM_HSCROLL:
    case OCM_VSCROLL:
    case OCM_CTLCOLORBTN:
    case OCM_CTLCOLORDLG:
    case OCM_CTLCOLOREDIT:
    case OCM_CTLCOLORLISTBOX:
    case OCM_CTLCOLORMSGBOX:
    case OCM_CTLCOLORSCROLLBAR:
    case OCM_CTLCOLORSTATIC:
      lResult = ::DefWindowProc(hWnd, uMsg - OCM__BASE, wParam, lParam);
      return TRUE;
    default:
      return FALSE;
    }
  }
};

// ---------------------------------------------------------------------
// CWindowImpl.

template <class TBase = CWindow, class TWinTraits = CControlWinTraits>
class ATL_NO_VTABLE CWindowImplBaseT : public CWindowImplRoot<TBase> {
public:
  WNDPROC m_pfnSuperWindowProc;

  CWindowImplBaseT() : m_pfnSuperWindowProc(::DefWindowProc) {}

  static DWORD GetWndStyle(DWORD dwStyle) { return TWinTraits::GetWndStyle(dwStyle); }
  static DWORD GetWndExStyle(DWORD dwExStyle) { return TWinTraits::GetWndExStyle(dwExStyle); }

  virtual WNDPROC GetWindowProc() { return WindowProc; }

  // The first message of a window made by Create: from here on the
  // thunk is its window procedure.
  static LRESULT CALLBACK StartWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    CWindowImplBaseT<TBase, TWinTraits>* pThis =
        (CWindowImplBaseT<TBase, TWinTraits>*)_AtlWinModule.ExtractCreateWndData();
    if (pThis == NULL) return ::DefWindowProc(hWnd, uMsg, wParam, lParam);
    pThis->m_hWnd = hWnd;
    pThis->m_thunk.Init(pThis->GetWindowProc(), pThis);
    WNDPROC pProc = pThis->m_thunk.GetWNDPROC();
    ::SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)pProc);
    return pProc(hWnd, uMsg, wParam, lParam);
  }

  // hWnd is the object, put there by the thunk.
  static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    CWindowImplBaseT<TBase, TWinTraits>* pThis = (CWindowImplBaseT<TBase, TWinTraits>*)hWnd;
    _ATL_MSG msg(pThis->m_hWnd, uMsg, wParam, lParam);
    const _ATL_MSG* pOldMsg = pThis->m_pCurrentMsg;
    pThis->m_pCurrentMsg = &msg;
    LRESULT lRes = 0;
    BOOL bRet = pThis->ProcessWindowMessage(pThis->m_hWnd, uMsg, wParam, lParam, lRes, 0);
    pThis->m_pCurrentMsg = pOldMsg;
    if (!bRet) {
      if (uMsg != WM_NCDESTROY) {
        lRes = pThis->DefWindowProc(uMsg, wParam, lParam);
      } else {
        // The last message: the window's original procedure back, when
        // nobody has subclassed it since.
        LONG_PTR pfnWndProc = ::GetWindowLongPtr(pThis->m_hWnd, GWLP_WNDPROC);
        lRes = pThis->DefWindowProc(uMsg, wParam, lParam);
        if (pThis->m_pfnSuperWindowProc != ::DefWindowProc &&
            ::GetWindowLongPtr(pThis->m_hWnd, GWLP_WNDPROC) == pfnWndProc)
          ::SetWindowLongPtr(pThis->m_hWnd, GWLP_WNDPROC, (LONG_PTR)pThis->m_pfnSuperWindowProc);
        pThis->m_dwState |= WINSTATE_DESTROYED;
      }
    } else if (uMsg == WM_NCDESTROY) {
      pThis->m_dwState |= WINSTATE_DESTROYED;
    }
    if ((pThis->m_dwState & WINSTATE_DESTROYED) && pOldMsg == NULL) {
      HWND hWndThis = pThis->m_hWnd;
      pThis->m_hWnd = NULL;
      pThis->m_dwState &= ~WINSTATE_DESTROYED;
      // The object may delete itself here.
      pThis->OnFinalMessage(hWndThis);
    }
    return lRes;
  }

  HWND Create(HWND hWndParent, _U_RECT rect, LPCTSTR szWindowName, DWORD dwStyle,
              DWORD dwExStyle, _U_MENUorID MenuOrID, ATOM atom, LPVOID lpCreateParam = NULL) {
    if (atom == 0) return NULL;
    _AtlWinModule.AddCreateWndData(&this->m_thunk.cd, this);
    // A child's menu is its ID; give one when there is none.
    if (MenuOrID.m_hMenu == NULL && (dwStyle & WS_CHILD))
      MenuOrID.m_hMenu = (HMENU)(UINT_PTR)this;
    if (rect.m_lpRect == NULL) rect.m_lpRect = &TBase::rcDefault;
    HWND hWnd = ::CreateWindowEx(dwExStyle, MAKEINTATOM(atom), szWindowName, dwStyle,
                                 rect.m_lpRect->left, rect.m_lpRect->top,
                                 rect.m_lpRect->right - rect.m_lpRect->left,
                                 rect.m_lpRect->bottom - rect.m_lpRect->top, hWndParent,
                                 MenuOrID.m_hMenu, _AtlBaseModule.GetModuleInstance(),
                                 lpCreateParam);
    if (hWnd == NULL) _AtlWinModule.ExtractCreateWndData();
    return hWnd;
  }

  BOOL DestroyWindow() { return ::DestroyWindow(this->m_hWnd); }

  // An existing window's messages, to this object's map first.
  BOOL SubclassWindow(HWND hWnd) {
    if (this->m_hWnd != NULL || !::IsWindow(hWnd)) return FALSE;
    if (!this->m_thunk.Init(GetWindowProc(), this)) return FALSE;
    WNDPROC pProc = this->m_thunk.GetWNDPROC();
    WNDPROC pfnWndProc = (WNDPROC)::SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)pProc);
    if (pfnWndProc == NULL) return FALSE;
    m_pfnSuperWindowProc = pfnWndProc;
    this->m_hWnd = hWnd;
    return TRUE;
  }
  // Gives the window its procedure back; only when nobody has
  // subclassed it since, unless bForce.
  HWND UnsubclassWindow(BOOL bForce = FALSE) {
    WNDPROC pOurProc = this->m_thunk.GetWNDPROC();
    WNDPROC pActiveProc = (WNDPROC)::GetWindowLongPtr(this->m_hWnd, GWLP_WNDPROC);
    HWND hWnd = NULL;
    if (bForce || pOurProc == pActiveProc) {
      if (!::SetWindowLongPtr(this->m_hWnd, GWLP_WNDPROC, (LONG_PTR)m_pfnSuperWindowProc))
        return NULL;
      m_pfnSuperWindowProc = ::DefWindowProc;
      hWnd = this->m_hWnd;
      this->m_hWnd = NULL;
    }
    return hWnd;
  }

  LRESULT DefWindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    return ::CallWindowProc(m_pfnSuperWindowProc, this->m_hWnd, uMsg, wParam, lParam);
  }
  LRESULT DefWindowProc() {
    const _ATL_MSG* pMsg = this->m_pCurrentMsg;
    return pMsg != NULL ? DefWindowProc(pMsg->message, pMsg->wParam, pMsg->lParam) : 0;
  }

  virtual void OnFinalMessage(HWND /*hWnd*/) {}
};

typedef CWindowImplBaseT<CWindow> CWindowImplBase;

template <class T, class TBase = CWindow, class TWinTraits = CControlWinTraits>
class ATL_NO_VTABLE CWindowImpl : public CWindowImplBaseT<TBase, TWinTraits> {
public:
  using CWindowImplBaseT<TBase, TWinTraits>::StartWindowProc;
  DECLARE_WND_CLASS(NULL)

  static LPCTSTR GetWndCaption() { return NULL; }

  HWND Create(HWND hWndParent, _U_RECT rect = NULL, LPCTSTR szWindowName = NULL,
              DWORD dwStyle = 0, DWORD dwExStyle = 0, _U_MENUorID MenuOrID = 0U,
              LPVOID lpCreateParam = NULL) {
    if (T::GetWndClassInfo().m_lpszOrigName == NULL)
      T::GetWndClassInfo().m_lpszOrigName = this->GetWndClassName();
    ATOM atom = T::GetWndClassInfo().Register(&this->m_pfnSuperWindowProc);
    dwStyle = T::GetWndStyle(dwStyle);
    dwExStyle = T::GetWndExStyle(dwExStyle);
    if (szWindowName == NULL) szWindowName = T::GetWndCaption();
    return CWindowImplBaseT<TBase, TWinTraits>::Create(hWndParent, rect, szWindowName, dwStyle,
                                                       dwExStyle, MenuOrID, atom, lpCreateParam);
  }
};

// ---------------------------------------------------------------------
// CContainedWindow: a window whose messages go to another object's map
// (an ALT_MSG_MAP of its parent, usually), made as a superclass of
// lpszClassName or subclassed.

template <class TBase = CWindow, class TWinTraits = CControlWinTraits>
class CContainedWindowT : public TBase {
public:
  CWndProcThunk m_thunk;
  LPCTSTR m_lpszClassName;
  WNDPROC m_pfnSuperWindowProc;
  CMessageMap* m_pObject;
  DWORD m_dwMsgMapID;
  const _ATL_MSG* m_pCurrentMsg;

  CContainedWindowT(HWND hWnd = NULL)
      : TBase(hWnd), m_lpszClassName(TBase::GetWndClassName()),
        m_pfnSuperWindowProc(::DefWindowProc), m_pObject(NULL), m_dwMsgMapID(0),
        m_pCurrentMsg(NULL) {}
  CContainedWindowT(LPCTSTR lpszClassName, CMessageMap* pObject, DWORD dwMsgMapID = 0)
      : m_lpszClassName(lpszClassName), m_pfnSuperWindowProc(::DefWindowProc),
        m_pObject(pObject), m_dwMsgMapID(dwMsgMapID), m_pCurrentMsg(NULL) {}
  CContainedWindowT(CMessageMap* pObject, DWORD dwMsgMapID = 0)
      : m_lpszClassName(TBase::GetWndClassName()), m_pfnSuperWindowProc(::DefWindowProc),
        m_pObject(pObject), m_dwMsgMapID(dwMsgMapID), m_pCurrentMsg(NULL) {}

  void SwitchMessageMap(DWORD dwMsgMapID) { m_dwMsgMapID = dwMsgMapID; }
  const _ATL_MSG* GetCurrentMessage() const { return m_pCurrentMsg; }

  LRESULT DefWindowProc() {
    const _ATL_MSG* pMsg = m_pCurrentMsg;
    return pMsg != NULL ? DefWindowProc(pMsg->message, pMsg->wParam, pMsg->lParam) : 0;
  }
  LRESULT DefWindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    return ::CallWindowProc(m_pfnSuperWindowProc, this->m_hWnd, uMsg, wParam, lParam);
  }

  static LRESULT CALLBACK StartWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    CContainedWindowT<TBase>* pThis =
        (CContainedWindowT<TBase>*)_AtlWinModule.ExtractCreateWndData();
    if (pThis == NULL) return ::DefWindowProc(hWnd, uMsg, wParam, lParam);
    pThis->m_hWnd = hWnd;
    pThis->m_thunk.Init(WindowProc, pThis);
    WNDPROC pProc = pThis->m_thunk.GetWNDPROC();
    ::SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)pProc);
    return pProc(hWnd, uMsg, wParam, lParam);
  }

  static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    CContainedWindowT<TBase>* pThis = (CContainedWindowT<TBase>*)hWnd;
    _ATL_MSG msg(pThis->m_hWnd, uMsg, wParam, lParam);
    const _ATL_MSG* pOldMsg = pThis->m_pCurrentMsg;
    pThis->m_pCurrentMsg = &msg;
    LRESULT lRes = 0;
    BOOL bRet = pThis->m_pObject != NULL &&
                pThis->m_pObject->ProcessWindowMessage(pThis->m_hWnd, uMsg, wParam, lParam, lRes,
                                                       pThis->m_dwMsgMapID);
    pThis->m_pCurrentMsg = pOldMsg;
    if (!bRet) {
      if (uMsg != WM_NCDESTROY) {
        lRes = pThis->DefWindowProc(uMsg, wParam, lParam);
      } else {
        LONG_PTR pfnWndProc = ::GetWindowLongPtr(pThis->m_hWnd, GWLP_WNDPROC);
        lRes = pThis->DefWindowProc(uMsg, wParam, lParam);
        if (pThis->m_pfnSuperWindowProc != ::DefWindowProc &&
            ::GetWindowLongPtr(pThis->m_hWnd, GWLP_WNDPROC) == pfnWndProc)
          ::SetWindowLongPtr(pThis->m_hWnd, GWLP_WNDPROC, (LONG_PTR)pThis->m_pfnSuperWindowProc);
        pThis->m_hWnd = NULL;
      }
    }
    return lRes;
  }

  // The superclass of m_lpszClassName this window is made from.
  ATOM RegisterWndSuperclass() {
    if (m_lpszClassName == NULL) return 0;
    // One class per original class, named after it.
    TCHAR szClassName[256];
    lstrcpy(szClassName, _T("ATL:"));
    lstrcpyn(szClassName + 4, m_lpszClassName, 250);
    HINSTANCE hInst = _AtlBaseModule.GetModuleInstance();
    WNDCLASSEX wc;
    wc.cbSize = sizeof(WNDCLASSEX);
    _AtlWinModule.m_csWindowCreate.Lock();
    ATOM atom = (ATOM)::GetClassInfoEx(hInst, szClassName, &wc);
    WNDCLASSEX wcOrig;
    wcOrig.cbSize = sizeof(WNDCLASSEX);
    if (!::GetClassInfoEx(NULL, m_lpszClassName, &wcOrig) &&
        !::GetClassInfoEx(hInst, m_lpszClassName, &wcOrig)) {
      _AtlWinModule.m_csWindowCreate.Unlock();
      return 0;
    }
    m_pfnSuperWindowProc = wcOrig.lpfnWndProc;
    if (atom == 0) {
      wc = wcOrig;
      wc.cbSize = sizeof(WNDCLASSEX);
      wc.lpfnWndProc = StartWindowProc;
      wc.lpszClassName = szClassName;
      wc.style &= ~CS_GLOBALCLASS;
      wc.hInstance = hInst;
      atom = ::RegisterClassEx(&wc);
    }
    _AtlWinModule.m_csWindowCreate.Unlock();
    return atom;
  }

  HWND Create(HWND hWndParent, _U_RECT rect, LPCTSTR szWindowName = NULL, DWORD dwStyle = 0,
              DWORD dwExStyle = 0, _U_MENUorID MenuOrID = 0U, LPVOID lpCreateParam = NULL) {
    ATOM atom = RegisterWndSuperclass();
    if (atom == 0) return NULL;
    _AtlWinModule.AddCreateWndData(&m_thunk.cd, this);
    if (MenuOrID.m_hMenu == NULL && (dwStyle & WS_CHILD))
      MenuOrID.m_hMenu = (HMENU)(UINT_PTR)this;
    if (rect.m_lpRect == NULL) rect.m_lpRect = &TBase::rcDefault;
    dwStyle = TWinTraits::GetWndStyle(dwStyle);
    dwExStyle = TWinTraits::GetWndExStyle(dwExStyle);
    HWND hWnd = ::CreateWindowEx(dwExStyle, MAKEINTATOM(atom), szWindowName, dwStyle,
                                 rect.m_lpRect->left, rect.m_lpRect->top,
                                 rect.m_lpRect->right - rect.m_lpRect->left,
                                 rect.m_lpRect->bottom - rect.m_lpRect->top, hWndParent,
                                 MenuOrID.m_hMenu, _AtlBaseModule.GetModuleInstance(),
                                 lpCreateParam);
    if (hWnd == NULL) _AtlWinModule.ExtractCreateWndData();
    return hWnd;
  }
  HWND Create(CMessageMap* pObject, DWORD dwMsgMapID, HWND hWndParent, _U_RECT rect,
              LPCTSTR szWindowName = NULL, DWORD dwStyle = 0, DWORD dwExStyle = 0,
              _U_MENUorID MenuOrID = 0U, LPVOID lpCreateParam = NULL) {
    m_lpszClassName = TBase::GetWndClassName();
    m_pObject = pObject;
    m_dwMsgMapID = dwMsgMapID;
    return Create(hWndParent, rect, szWindowName, dwStyle, dwExStyle, MenuOrID, lpCreateParam);
  }

  BOOL SubclassWindow(HWND hWnd) {
    if (this->m_hWnd != NULL || !::IsWindow(hWnd)) return FALSE;
    if (!m_thunk.Init(WindowProc, this)) return FALSE;
    WNDPROC pProc = m_thunk.GetWNDPROC();
    WNDPROC pfnWndProc = (WNDPROC)::SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)pProc);
    if (pfnWndProc == NULL) return FALSE;
    m_pfnSuperWindowProc = pfnWndProc;
    this->m_hWnd = hWnd;
    return TRUE;
  }
  HWND UnsubclassWindow(BOOL bForce = FALSE) {
    WNDPROC pOurProc = m_thunk.GetWNDPROC();
    WNDPROC pActiveProc = (WNDPROC)::GetWindowLongPtr(this->m_hWnd, GWLP_WNDPROC);
    HWND hWnd = NULL;
    if (bForce || pOurProc == pActiveProc) {
      if (!::SetWindowLongPtr(this->m_hWnd, GWLP_WNDPROC, (LONG_PTR)m_pfnSuperWindowProc))
        return NULL;
      m_pfnSuperWindowProc = ::DefWindowProc;
      hWnd = this->m_hWnd;
      this->m_hWnd = NULL;
    }
    return hWnd;
  }
};

typedef CContainedWindowT<CWindow> CContainedWindow;

// ---------------------------------------------------------------------
// Dialogs.

template <class TBase = CWindow>
class ATL_NO_VTABLE CDialogImplBaseT : public CWindowImplRoot<TBase> {
public:
  virtual DLGPROC GetDialogProc() { return DialogProc; }

  static INT_PTR CALLBACK StartDialogProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    CDialogImplBaseT<TBase>* pThis = (CDialogImplBaseT<TBase>*)_AtlWinModule.ExtractCreateWndData();
    if (pThis == NULL) return 0;
    pThis->m_hWnd = hWnd;
    pThis->m_thunk.Init((WNDPROC)(void*)pThis->GetDialogProc(), pThis);
    DLGPROC pProc = (DLGPROC)(void*)pThis->m_thunk.GetWNDPROC();
    ::SetWindowLongPtr(hWnd, DWLP_DLGPROC, (LONG_PTR)pProc);
    return pProc(hWnd, uMsg, wParam, lParam);
  }

  static INT_PTR CALLBACK DialogProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    CDialogImplBaseT<TBase>* pThis = (CDialogImplBaseT<TBase>*)hWnd;
    _ATL_MSG msg(pThis->m_hWnd, uMsg, wParam, lParam);
    const _ATL_MSG* pOldMsg = pThis->m_pCurrentMsg;
    pThis->m_pCurrentMsg = &msg;
    LRESULT lRes = 0;
    BOOL bRet = pThis->ProcessWindowMessage(pThis->m_hWnd, uMsg, wParam, lParam, lRes, 0);
    pThis->m_pCurrentMsg = pOldMsg;
    if (uMsg == WM_NCDESTROY) pThis->m_dwState |= WINSTATE_DESTROYED;
    INT_PTR nRet = FALSE;
    if (bRet) {
      // These return their result directly; the others through
      // DWLP_MSGRESULT.
      switch (uMsg) {
      case WM_COMPAREITEM:
      case WM_VKEYTOITEM:
      case WM_CHARTOITEM:
      case WM_INITDIALOG:
      case WM_QUERYDRAGICON:
      case WM_CTLCOLORMSGBOX:
      case WM_CTLCOLOREDIT:
      case WM_CTLCOLORLISTBOX:
      case WM_CTLCOLORBTN:
      case WM_CTLCOLORDLG:
      case WM_CTLCOLORSCROLLBAR:
      case WM_CTLCOLORSTATIC:
        nRet = (INT_PTR)lRes;
        break;
      default:
        if (pThis->m_hWnd != NULL && (pThis->m_dwState & WINSTATE_DESTROYED) == 0)
          ::SetWindowLongPtr(pThis->m_hWnd, DWLP_MSGRESULT, (LONG_PTR)lRes);
        nRet = TRUE;
        break;
      }
    }
    if ((pThis->m_dwState & WINSTATE_DESTROYED) && pOldMsg == NULL) {
      HWND hWndThis = pThis->m_hWnd;
      pThis->m_hWnd = NULL;
      pThis->m_dwState &= ~WINSTATE_DESTROYED;
      pThis->OnFinalMessage(hWndThis);
    }
    return nRet;
  }

  BOOL MapDialogRect(LPRECT lpRect) { return ::MapDialogRect(this->m_hWnd, lpRect); }
  virtual void OnFinalMessage(HWND /*hWnd*/) {}

  // What a reflected message does in a dialog's control.
  LRESULT DefWindowProc() { return 0; }
};

typedef CDialogImplBaseT<CWindow> CDialogImplBase;

template <class T, class TBase = CWindow>
class ATL_NO_VTABLE CDialogImpl : public CDialogImplBaseT<TBase> {
public:
  // A modal dialog from T::IDD, ended by EndDialog.
  INT_PTR DoModal(HWND hWndParent = ::GetActiveWindow(), LPARAM dwInitParam = 0) {
    _AtlWinModule.AddCreateWndData(&this->m_thunk.cd, (CDialogImplBaseT<TBase>*)this);
    return ::DialogBoxParam(_AtlBaseModule.GetResourceInstance(),
                            MAKEINTRESOURCE(static_cast<T*>(this)->IDD), hWndParent,
                            T::StartDialogProc, dwInitParam);
  }
  BOOL EndDialog(int nRetCode) { return ::EndDialog(this->m_hWnd, nRetCode); }

  // A modeless one, ended by DestroyWindow.
  HWND Create(HWND hWndParent, LPARAM dwInitParam = 0) {
    _AtlWinModule.AddCreateWndData(&this->m_thunk.cd, (CDialogImplBaseT<TBase>*)this);
    HWND hWnd = ::CreateDialogParam(_AtlBaseModule.GetResourceInstance(),
                                    MAKEINTRESOURCE(static_cast<T*>(this)->IDD), hWndParent,
                                    T::StartDialogProc, dwInitParam);
    if (hWnd == NULL) _AtlWinModule.ExtractCreateWndData();
    return hWnd;
  }
  HWND Create(HWND hWndParent, RECT&, LPARAM dwInitParam = 0) { return Create(hWndParent, dwInitParam); }
  BOOL DestroyWindow() { return ::DestroyWindow(this->m_hWnd); }
};

// A dialog that only shows itself and ends with the button pressed.
template <WORD t_wDlgTemplateID, BOOL t_bCenter = TRUE>
class CSimpleDialog : public CDialogImplBase {
public:
  INT_PTR DoModal(HWND hWndParent = ::GetActiveWindow()) {
    _AtlWinModule.AddCreateWndData(&m_thunk.cd, (CDialogImplBase*)this);
    return ::DialogBoxParam(_AtlBaseModule.GetResourceInstance(),
                            MAKEINTRESOURCE(t_wDlgTemplateID), hWndParent, StartDialogProc, 0);
  }

  BEGIN_MSG_MAP(CSimpleDialog)
    MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
    COMMAND_RANGE_HANDLER(IDOK, IDNO, OnCloseCmd)
  END_MSG_MAP()

  LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
    if (t_bCenter) CenterWindow(GetParent());
    return TRUE;
  }
  LRESULT OnCloseCmd(WORD, WORD wID, HWND, BOOL&) {
    ::EndDialog(m_hWnd, wID);
    return 0;
  }
};

}  // namespace ATL

#endif  // __ATLWIN_H__
