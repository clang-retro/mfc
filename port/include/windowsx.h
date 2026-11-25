// windowsx.h - the Win32 SDK's macro helpers: GDI object selection,
// window queries, the control message wrappers and the mouse position in
// an LPARAM. The message crackers (HANDLE_WM_*, FORWARD_WM_*) are not
// here.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_WINDOWSX_H
#define _WINPORT_WINDOWSX_H

#include <windows.h>

// Memory
#define GetInstanceModule(hInstance) ((HMODULE)(hInstance))
#define GlobalPtrHandle(lp) ((HGLOBAL)GlobalHandle(lp))
#define GlobalLockPtr(lp) ((BOOL)GlobalLock(GlobalPtrHandle(lp)))
#define GlobalUnlockPtr(lp) GlobalUnlock(GlobalPtrHandle(lp))
#define GlobalAllocPtr(flags, cb) (GlobalLock(GlobalAlloc((flags), (cb))))
#define GlobalReAllocPtr(lp, cbNew, flags) \
  (GlobalUnlockPtr(lp), GlobalLock(GlobalReAlloc(GlobalPtrHandle(lp), (cbNew), (flags))))
#define GlobalFreePtr(lp) (GlobalUnlockPtr(lp), (BOOL)(ULONG_PTR)GlobalFree(GlobalPtrHandle(lp)))

// GDI objects
#define DeletePen(hpen) DeleteObject((HGDIOBJ)(HPEN)(hpen))
#define SelectPen(hdc, hpen) ((HPEN)SelectObject((hdc), (HGDIOBJ)(HPEN)(hpen)))
#define GetStockPen(i) ((HPEN)GetStockObject(i))
#define DeleteBrush(hbr) DeleteObject((HGDIOBJ)(HBRUSH)(hbr))
#define SelectBrush(hdc, hbr) ((HBRUSH)SelectObject((hdc), (HGDIOBJ)(HBRUSH)(hbr)))
#define GetStockBrush(i) ((HBRUSH)GetStockObject(i))
#define DeleteRgn(hrgn) DeleteObject((HGDIOBJ)(HRGN)(hrgn))
#define CopyRgn(hrgnDst, hrgnSrc) CombineRgn(hrgnDst, hrgnSrc, 0, RGN_COPY)
#define IntersectRgn(hrgnResult, hrgnA, hrgnB) CombineRgn(hrgnResult, hrgnA, hrgnB, RGN_AND)
#define SubtractRgn(hrgnResult, hrgnA, hrgnB) CombineRgn(hrgnResult, hrgnA, hrgnB, RGN_DIFF)
#define UnionRgn(hrgnResult, hrgnA, hrgnB) CombineRgn(hrgnResult, hrgnA, hrgnB, RGN_OR)
#define XorRgn(hrgnResult, hrgnA, hrgnB) CombineRgn(hrgnResult, hrgnA, hrgnB, RGN_XOR)
#define DeletePalette(hpal) DeleteObject((HGDIOBJ)(HPALETTE)(hpal))
#define DeleteFont(hfont) DeleteObject((HGDIOBJ)(HFONT)(hfont))
#define SelectFont(hdc, hfont) ((HFONT)SelectObject((hdc), (HGDIOBJ)(HFONT)(hfont)))
#define GetStockFont(i) ((HFONT)GetStockObject(i))
#define DeleteBitmap(hbm) DeleteObject((HGDIOBJ)(HBITMAP)(hbm))
#define SelectBitmap(hdc, hbm) ((HBITMAP)SelectObject((hdc), (HGDIOBJ)(HBITMAP)(hbm)))
#define InsetRect(lprc, dx, dy) InflateRect((lprc), -(dx), -(dy))

// Windows
#define GetWindowInstance(hwnd) ((HMODULE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE))
#define GetWindowStyle(hwnd) ((DWORD)GetWindowLong(hwnd, GWL_STYLE))
#define GetWindowExStyle(hwnd) ((DWORD)GetWindowLong(hwnd, GWL_EXSTYLE))
#define GetWindowOwner(hwnd) GetWindow(hwnd, GW_OWNER)
#define GetFirstChild(hwnd) GetTopWindow(hwnd)
#define GetFirstSibling(hwnd) GetWindow(hwnd, GW_HWNDFIRST)
#define GetLastSibling(hwnd) GetWindow(hwnd, GW_HWNDLAST)
#define GetNextSibling(hwnd) GetWindow(hwnd, GW_HWNDNEXT)
#define GetPrevSibling(hwnd) GetWindow(hwnd, GW_HWNDPREV)
#define GetWindowID(hwnd) GetDlgCtrlID(hwnd)
#define SetWindowRedraw(hwnd, fRedraw) ((void)SendMessage(hwnd, WM_SETREDRAW, (WPARAM)(BOOL)(fRedraw), 0L))
#define SubclassWindow(hwnd, lpfn) ((WNDPROC)SetWindowLongPtr((hwnd), GWLP_WNDPROC, (LONG_PTR)(WNDPROC)(lpfn)))
#define IsMinimized(hwnd) IsIconic(hwnd)
#define IsMaximized(hwnd) IsZoomed(hwnd)
#define IsRestored(hwnd) ((GetWindowStyle(hwnd) & (WS_MINIMIZE | WS_MAXIMIZE)) == 0L)
#define SetWindowFont(hwnd, hfont, fRedraw) \
  ((void)SendMessage((hwnd), WM_SETFONT, (WPARAM)(HFONT)(hfont), (LPARAM)(BOOL)(fRedraw)))
#define GetWindowFont(hwnd) ((HFONT)SendMessage((hwnd), WM_GETFONT, 0L, 0L))
#define MapWindowRect(hwndFrom, hwndTo, lprc) MapWindowPoints((hwndFrom), (hwndTo), (POINT*)(lprc), 2)
#define IsLButtonDown() (GetKeyState(VK_LBUTTON) < 0)
#define IsRButtonDown() (GetKeyState(VK_RBUTTON) < 0)
#define IsMButtonDown() (GetKeyState(VK_MBUTTON) < 0)
#define SubclassDialog(hwndDlg, lpfn) \
  ((DLGPROC)SetWindowLongPtr(hwndDlg, DWLP_DLGPROC, (LONG_PTR)(DLGPROC)(lpfn)))
#define SetDlgMsgResult(hwnd, msg, result) \
  (((msg) == WM_CTLCOLORMSGBOX || (msg) == WM_CTLCOLOREDIT || (msg) == WM_CTLCOLORLISTBOX || \
    (msg) == WM_CTLCOLORBTN || (msg) == WM_CTLCOLORDLG || (msg) == WM_CTLCOLORSCROLLBAR || \
    (msg) == WM_CTLCOLORSTATIC || (msg) == WM_COMPAREITEM || (msg) == WM_VKEYTOITEM || \
    (msg) == WM_CHARTOITEM || (msg) == WM_QUERYDRAGICON || (msg) == WM_INITDIALOG) \
       ? (BOOL)(result) \
       : (SetWindowLongPtr((hwnd), DWLP_MSGRESULT, (LPARAM)(LRESULT)(result)), TRUE))

// The mouse position in an LPARAM, with its sign.
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))

// Static controls
#define Static_Enable(hwndCtl, fEnable) EnableWindow((hwndCtl), (fEnable))
#define Static_GetText(hwndCtl, lpch, cchMax) GetWindowText((hwndCtl), (lpch), (cchMax))
#define Static_GetTextLength(hwndCtl) GetWindowTextLength(hwndCtl)
#define Static_SetText(hwndCtl, lpsz) SetWindowText((hwndCtl), (lpsz))
#define Static_SetIcon(hwndCtl, hIcon) ((HICON)(UINT_PTR)SendMessage((hwndCtl), STM_SETICON, (WPARAM)(HICON)(hIcon), 0L))
#define Static_GetIcon(hwndCtl, hIcon) ((HICON)(UINT_PTR)SendMessage((hwndCtl), STM_GETICON, 0L, 0L))

// Buttons
#define Button_Enable(hwndCtl, fEnable) EnableWindow((hwndCtl), (fEnable))
#define Button_GetText(hwndCtl, lpch, cchMax) GetWindowText((hwndCtl), (lpch), (cchMax))
#define Button_GetTextLength(hwndCtl) GetWindowTextLength(hwndCtl)
#define Button_SetText(hwndCtl, lpsz) SetWindowText((hwndCtl), (lpsz))
#define Button_GetCheck(hwndCtl) ((int)(DWORD)SendMessage((hwndCtl), BM_GETCHECK, 0L, 0L))
#define Button_SetCheck(hwndCtl, check) ((void)SendMessage((hwndCtl), BM_SETCHECK, (WPARAM)(int)(check), 0L))
#define Button_GetState(hwndCtl) ((int)(DWORD)SendMessage((hwndCtl), BM_GETSTATE, 0L, 0L))
#define Button_SetState(hwndCtl, state) ((UINT)(DWORD)SendMessage((hwndCtl), BM_SETSTATE, (WPARAM)(int)(state), 0L))
#define Button_SetStyle(hwndCtl, style, fRedraw) \
  ((void)SendMessage((hwndCtl), BM_SETSTYLE, (WPARAM)LOWORD(style), MAKELPARAM(((fRedraw) ? TRUE : FALSE), 0)))

// Edit controls
#define Edit_Enable(hwndCtl, fEnable) EnableWindow((hwndCtl), (fEnable))
#define Edit_GetText(hwndCtl, lpch, cchMax) GetWindowText((hwndCtl), (lpch), (cchMax))
#define Edit_GetTextLength(hwndCtl) GetWindowTextLength(hwndCtl)
#define Edit_SetText(hwndCtl, lpsz) SetWindowText((hwndCtl), (lpsz))
#define Edit_LimitText(hwndCtl, cchMax) ((void)SendMessage((hwndCtl), EM_LIMITTEXT, (WPARAM)(cchMax), 0L))
#define Edit_GetLineCount(hwndCtl) ((int)(DWORD)SendMessage((hwndCtl), EM_GETLINECOUNT, 0L, 0L))
#define Edit_GetSel(hwndCtl) ((DWORD)SendMessage((hwndCtl), EM_GETSEL, 0L, 0L))
#define Edit_SetSel(hwndCtl, ichStart, ichEnd) ((void)SendMessage((hwndCtl), EM_SETSEL, (ichStart), (ichEnd)))
#define Edit_ReplaceSel(hwndCtl, lpszReplace) \
  ((void)SendMessage((hwndCtl), EM_REPLACESEL, 0L, (LPARAM)(LPCTSTR)(lpszReplace)))
#define Edit_GetModify(hwndCtl) ((BOOL)(DWORD)SendMessage((hwndCtl), EM_GETMODIFY, 0L, 0L))
#define Edit_SetModify(hwndCtl, fModified) ((void)SendMessage((hwndCtl), EM_SETMODIFY, (WPARAM)(UINT)(fModified), 0L))
#define Edit_CanUndo(hwndCtl) ((BOOL)(DWORD)SendMessage((hwndCtl), EM_CANUNDO, 0L, 0L))
#define Edit_Undo(hwndCtl) ((BOOL)(DWORD)SendMessage((hwndCtl), EM_UNDO, 0L, 0L))
#define Edit_EmptyUndoBuffer(hwndCtl) ((void)SendMessage((hwndCtl), EM_EMPTYUNDOBUFFER, 0L, 0L))
#define Edit_SetReadOnly(hwndCtl, fReadOnly) ((BOOL)(DWORD)SendMessage((hwndCtl), EM_SETREADONLY, (WPARAM)(BOOL)(fReadOnly), 0L))

// List boxes
#define ListBox_Enable(hwndCtl, fEnable) EnableWindow((hwndCtl), (fEnable))
#define ListBox_GetCount(hwndCtl) ((int)(DWORD)SendMessage((hwndCtl), LB_GETCOUNT, 0L, 0L))
#define ListBox_ResetContent(hwndCtl) ((BOOL)(DWORD)SendMessage((hwndCtl), LB_RESETCONTENT, 0L, 0L))
#define ListBox_AddString(hwndCtl, lpsz) ((int)(DWORD)SendMessage((hwndCtl), LB_ADDSTRING, 0L, (LPARAM)(LPCTSTR)(lpsz)))
#define ListBox_InsertString(hwndCtl, index, lpsz) \
  ((int)(DWORD)SendMessage((hwndCtl), LB_INSERTSTRING, (WPARAM)(int)(index), (LPARAM)(LPCTSTR)(lpsz)))
#define ListBox_DeleteString(hwndCtl, index) ((int)(DWORD)SendMessage((hwndCtl), LB_DELETESTRING, (WPARAM)(int)(index), 0L))
#define ListBox_GetText(hwndCtl, index, lpszBuffer) \
  ((int)(DWORD)SendMessage((hwndCtl), LB_GETTEXT, (WPARAM)(int)(index), (LPARAM)(LPCTSTR)(lpszBuffer)))
#define ListBox_GetCurSel(hwndCtl) ((int)(DWORD)SendMessage((hwndCtl), LB_GETCURSEL, 0L, 0L))
#define ListBox_SetCurSel(hwndCtl, index) ((int)(DWORD)SendMessage((hwndCtl), LB_SETCURSEL, (WPARAM)(int)(index), 0L))
#define ListBox_GetItemData(hwndCtl, index) ((LRESULT)(ULONG_PTR)SendMessage((hwndCtl), LB_GETITEMDATA, (WPARAM)(int)(index), 0L))
#define ListBox_SetItemData(hwndCtl, index, data) \
  ((int)(DWORD)SendMessage((hwndCtl), LB_SETITEMDATA, (WPARAM)(int)(index), (LPARAM)(data)))

// Combo boxes
#define ComboBox_Enable(hwndCtl, fEnable) EnableWindow((hwndCtl), (fEnable))
#define ComboBox_GetText(hwndCtl, lpch, cchMax) GetWindowText((hwndCtl), (lpch), (cchMax))
#define ComboBox_SetText(hwndCtl, lpsz) SetWindowText((hwndCtl), (lpsz))
#define ComboBox_GetCount(hwndCtl) ((int)(DWORD)SendMessage((hwndCtl), CB_GETCOUNT, 0L, 0L))
#define ComboBox_ResetContent(hwndCtl) ((int)(DWORD)SendMessage((hwndCtl), CB_RESETCONTENT, 0L, 0L))
#define ComboBox_AddString(hwndCtl, lpsz) ((int)(DWORD)SendMessage((hwndCtl), CB_ADDSTRING, 0L, (LPARAM)(LPCTSTR)(lpsz)))
#define ComboBox_InsertString(hwndCtl, index, lpsz) \
  ((int)(DWORD)SendMessage((hwndCtl), CB_INSERTSTRING, (WPARAM)(int)(index), (LPARAM)(LPCTSTR)(lpsz)))
#define ComboBox_DeleteString(hwndCtl, index) ((int)(DWORD)SendMessage((hwndCtl), CB_DELETESTRING, (WPARAM)(int)(index), 0L))
#define ComboBox_GetLBText(hwndCtl, index, lpszBuffer) \
  ((int)(DWORD)SendMessage((hwndCtl), CB_GETLBTEXT, (WPARAM)(int)(index), (LPARAM)(LPCTSTR)(lpszBuffer)))
#define ComboBox_GetCurSel(hwndCtl) ((int)(DWORD)SendMessage((hwndCtl), CB_GETCURSEL, 0L, 0L))
#define ComboBox_SetCurSel(hwndCtl, index) ((int)(DWORD)SendMessage((hwndCtl), CB_SETCURSEL, (WPARAM)(int)(index), 0L))
#define ComboBox_GetItemData(hwndCtl, index) ((LRESULT)(ULONG_PTR)SendMessage((hwndCtl), CB_GETITEMDATA, (WPARAM)(int)(index), 0L))
#define ComboBox_SetItemData(hwndCtl, index, data) \
  ((int)(DWORD)SendMessage((hwndCtl), CB_SETITEMDATA, (WPARAM)(int)(index), (LPARAM)(data)))

// Scroll bars
#define ScrollBar_Enable(hwndCtl, flags) EnableScrollBar((hwndCtl), SB_CTL, (flags))
#define ScrollBar_GetPos(hwndCtl) GetScrollPos((hwndCtl), SB_CTL)
#define ScrollBar_SetPos(hwndCtl, pos, fRedraw) SetScrollPos((hwndCtl), SB_CTL, (pos), (fRedraw))
#define ScrollBar_GetRange(hwndCtl, lpposMin, lpposMax) GetScrollRange((hwndCtl), SB_CTL, (lpposMin), (lpposMax))
#define ScrollBar_SetRange(hwndCtl, posMin, posMax, fRedraw) \
  SetScrollRange((hwndCtl), SB_CTL, (posMin), (posMax), (fRedraw))

#endif
