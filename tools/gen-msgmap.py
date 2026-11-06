#!/usr/bin/env python3
"""Generate the ON_WM_* message map macros and CWnd's default handlers.

Each row: macro suffix, message, handler, return type, parameters, the
arguments the handler gets (from _t this, _m message, _w wParam, _l lParam),
and whether CWnd's own handler is written by hand (custom).

Writes include/afxmsg_wm_.h (macros), include/afxwnd_handlers_.h (the
declarations inside class CWnd) and src/wnd_handlers.inc (the default
bodies, which call Default()).
"""
import os, sys

MOUSE = ("UINT nFlags, CPoint point", "(UINT)_w, _AfxPt(_l)")
KEY = ("UINT nChar, UINT nRepCnt, UINT nFlags", "(UINT)_w, LOWORD(_l), HIWORD(_l)")
NCMOUSE = ("UINT nHitTest, CPoint point", "(UINT)_w, _AfxPt(_l)")
XBTN = ("UINT nFlags, UINT nButton, CPoint point", "LOWORD(_w), HIWORD(_w), _AfxPt(_l)")
SCROLL = ("UINT nSBCode, UINT nPos, CScrollBar* pScrollBar",
          "LOWORD(_w), (UINT)(short)HIWORD(_w), (CScrollBar*)_AfxWnd(_l)")
NONE = ("", "")

ROWS = [
    ("ACTIVATE", "WM_ACTIVATE", "OnActivate", "void", ("UINT nState, CWnd* pWndOther, BOOL bMinimized", "LOWORD(_w), _AfxWnd(_l), HIWORD(_w)"), False),
    ("ACTIVATEAPP", "WM_ACTIVATEAPP", "OnActivateApp", "void", ("BOOL bActive, DWORD dwThreadID", "(BOOL)_w, (DWORD)_l"), False),
    ("APPCOMMAND", "WM_APPCOMMAND", "OnAppCommand", "void", ("CWnd* pWnd, UINT nCmd, UINT nDevice, UINT nKey", "_AfxWnd(_w), GET_APPCOMMAND_LPARAM(_l), GET_DEVICE_LPARAM(_l), GET_KEYSTATE_LPARAM(_l)"), False),
    ("ASKCBFORMATNAME", "WM_ASKCBFORMATNAME", "OnAskCbFormatName", "void", ("UINT nMaxCount, LPTSTR lpszString", "(UINT)_w, (LPTSTR)_l"), False),
    ("CANCELMODE", "WM_CANCELMODE", "OnCancelMode", "void", NONE, False),
    ("CAPTURECHANGED", "WM_CAPTURECHANGED", "OnCaptureChanged", "void", ("CWnd* pWnd", "_AfxWnd(_l)"), False),
    ("CHANGECBCHAIN", "WM_CHANGECBCHAIN", "OnChangeCbChain", "void", ("HWND hWndRemove, HWND hWndAfter", "(HWND)_w, (HWND)_l"), False),
    ("CHAR", "WM_CHAR", "OnChar", "void", KEY, False),
    ("CHARTOITEM", "WM_CHARTOITEM", "OnCharToItem", "int", ("UINT nChar, CListBox* pListBox, UINT nIndex", "LOWORD(_w), (CListBox*)_AfxWnd(_l), HIWORD(_w)"), True),
    ("CHILDACTIVATE", "WM_CHILDACTIVATE", "OnChildActivate", "void", NONE, False),
    ("CLOSE", "WM_CLOSE", "OnClose", "void", NONE, False),
    ("COMPACTING", "WM_COMPACTING", "OnCompacting", "void", ("UINT nCpuTime", "(UINT)_w"), False),
    ("COMPAREITEM", "WM_COMPAREITEM", "OnCompareItem", "int", ("int nIDCtl, LPCOMPAREITEMSTRUCT lpCompareItemStruct", "(int)_w, (LPCOMPAREITEMSTRUCT)_l"), True),
    ("CONTEXTMENU", "WM_CONTEXTMENU", "OnContextMenu", "void", ("CWnd* pWnd, CPoint pos", "_AfxWnd(_w), _AfxPt(_l)"), False),
    ("COPYDATA", "WM_COPYDATA", "OnCopyData", "BOOL", ("CWnd* pWnd, COPYDATASTRUCT* pCopyDataStruct", "_AfxWnd(_w), (COPYDATASTRUCT*)_l"), False),
    ("CREATE", "WM_CREATE", "OnCreate", "int", ("LPCREATESTRUCT lpCreateStruct", "(LPCREATESTRUCT)_l"), False),
    ("CTLCOLOR", "WM_CTLCOLOR", "OnCtlColor", "HBRUSH", ("CDC* pDC, CWnd* pWnd, UINT nCtlColor", "_AfxDC(_w), _AfxWnd(_l), _AfxCtlType(_m)"), True),
    ("DEADCHAR", "WM_DEADCHAR", "OnDeadChar", "void", KEY, False),
    ("DELETEITEM", "WM_DELETEITEM", "OnDeleteItem", "void", ("int nIDCtl, LPDELETEITEMSTRUCT lpDeleteItemStruct", "(int)_w, (LPDELETEITEMSTRUCT)_l"), True),
    ("DESTROY", "WM_DESTROY", "OnDestroy", "void", NONE, False),
    ("DESTROYCLIPBOARD", "WM_DESTROYCLIPBOARD", "OnDestroyClipboard", "void", NONE, False),
    ("DEVICECHANGE", "WM_DEVICECHANGE", "OnDeviceChange", "BOOL", ("UINT nEventType, DWORD_PTR dwData", "(UINT)_w, (DWORD_PTR)_l"), False),
    ("DEVMODECHANGE", "WM_DEVMODECHANGE", "OnDevModeChange", "void", ("LPTSTR lpDeviceName", "(LPTSTR)_l"), False),
    ("DRAWCLIPBOARD", "WM_DRAWCLIPBOARD", "OnDrawClipboard", "void", NONE, False),
    ("DRAWITEM", "WM_DRAWITEM", "OnDrawItem", "void", ("int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct", "(int)_w, (LPDRAWITEMSTRUCT)_l"), True),
    ("DROPFILES", "WM_DROPFILES", "OnDropFiles", "void", ("HDROP hDropInfo", "(HDROP)_w"), False),
    ("ENABLE", "WM_ENABLE", "OnEnable", "void", ("BOOL bEnable", "(BOOL)_w"), False),
    ("ENDSESSION", "WM_ENDSESSION", "OnEndSession", "void", ("BOOL bEnding", "(BOOL)_w"), False),
    ("ENTERIDLE", "WM_ENTERIDLE", "OnEnterIdle", "void", ("UINT nWhy, CWnd* pWho", "(UINT)_w, _AfxWnd(_l)"), True),
    ("ENTERMENULOOP", "WM_ENTERMENULOOP", "OnEnterMenuLoop", "void", ("BOOL bIsTrackPopupMenu", "(BOOL)_w"), False),
    ("EXITMENULOOP", "WM_EXITMENULOOP", "OnExitMenuLoop", "void", ("BOOL bIsTrackPopupMenu", "(BOOL)_w"), False),
    ("ENTERSIZEMOVE", "WM_ENTERSIZEMOVE", "OnEnterSizeMove", "void", NONE, False),
    ("EXITSIZEMOVE", "WM_EXITSIZEMOVE", "OnExitSizeMove", "void", NONE, False),
    ("ERASEBKGND", "WM_ERASEBKGND", "OnEraseBkgnd", "BOOL", ("CDC* pDC", "_AfxDC(_w)"), False),
    ("FONTCHANGE", "WM_FONTCHANGE", "OnFontChange", "void", NONE, False),
    ("GETDLGCODE", "WM_GETDLGCODE", "OnGetDlgCode", "UINT", NONE, False),
    ("GETMINMAXINFO", "WM_GETMINMAXINFO", "OnGetMinMaxInfo", "void", ("MINMAXINFO* lpMMI", "(MINMAXINFO*)_l"), False),
    ("HELPINFO", "WM_HELP", "OnHelpInfo", "BOOL", ("HELPINFO* pHelpInfo", "(HELPINFO*)_l"), False),
    ("HSCROLL", "WM_HSCROLL", "OnHScroll", "void", SCROLL, True),
    ("HSCROLLCLIPBOARD", "WM_HSCROLLCLIPBOARD", "OnHScrollClipboard", "void", ("CWnd* pClipAppWnd, UINT nSBCode, UINT nPos", "_AfxWnd(_w), LOWORD(_l), HIWORD(_l)"), False),
    ("ICONERASEBKGND", "WM_ICONERASEBKGND", "OnIconEraseBkgnd", "void", ("CDC* pDC", "_AfxDC(_w)"), False),
    ("INITMENU", "WM_INITMENU", "OnInitMenu", "void", ("CMenu* pMenu", "_AfxMenu(_w)"), False),
    ("INITMENUPOPUP", "WM_INITMENUPOPUP", "OnInitMenuPopup", "void", ("CMenu* pPopupMenu, UINT nIndex, BOOL bSysMenu", "_AfxMenu(_w), LOWORD(_l), HIWORD(_l)"), False),
    ("KEYDOWN", "WM_KEYDOWN", "OnKeyDown", "void", KEY, False),
    ("KEYUP", "WM_KEYUP", "OnKeyUp", "void", KEY, False),
    ("KILLFOCUS", "WM_KILLFOCUS", "OnKillFocus", "void", ("CWnd* pNewWnd", "_AfxWnd(_w)"), False),
    ("LBUTTONDBLCLK", "WM_LBUTTONDBLCLK", "OnLButtonDblClk", "void", MOUSE, False),
    ("LBUTTONDOWN", "WM_LBUTTONDOWN", "OnLButtonDown", "void", MOUSE, False),
    ("LBUTTONUP", "WM_LBUTTONUP", "OnLButtonUp", "void", MOUSE, False),
    ("MBUTTONDBLCLK", "WM_MBUTTONDBLCLK", "OnMButtonDblClk", "void", MOUSE, False),
    ("MBUTTONDOWN", "WM_MBUTTONDOWN", "OnMButtonDown", "void", MOUSE, False),
    ("MBUTTONUP", "WM_MBUTTONUP", "OnMButtonUp", "void", MOUSE, False),
    ("MDIACTIVATE", "WM_MDIACTIVATE", "OnMDIActivate", "void", ("BOOL bActivate, CWnd* pActivateWnd, CWnd* pDeactivateWnd", "(HWND)_l == ((CWnd*)_t)->m_hWnd, _AfxWnd(_l), _AfxWnd(_w)"), False),
    ("MEASUREITEM", "WM_MEASUREITEM", "OnMeasureItem", "void", ("int nIDCtl, LPMEASUREITEMSTRUCT lpMeasureItemStruct", "(int)_w, (LPMEASUREITEMSTRUCT)_l"), True),
    ("MENUCHAR", "WM_MENUCHAR", "OnMenuChar", "LRESULT", ("UINT nChar, UINT nFlags, CMenu* pMenu", "LOWORD(_w), HIWORD(_w), _AfxMenu(_l)"), False),
    ("MENUSELECT", "WM_MENUSELECT", "OnMenuSelect", "void", ("UINT nItemID, UINT nFlags, HMENU hSysMenu", "LOWORD(_w), HIWORD(_w), (HMENU)_l"), False),
    ("MOUSEACTIVATE", "WM_MOUSEACTIVATE", "OnMouseActivate", "int", ("CWnd* pDesktopWnd, UINT nHitTest, UINT message", "_AfxWnd(_w), LOWORD(_l), HIWORD(_l)"), False),
    ("MOUSEHOVER", "WM_MOUSEHOVER", "OnMouseHover", "void", MOUSE, False),
    ("MOUSELEAVE", "WM_MOUSELEAVE", "OnMouseLeave", "void", NONE, False),
    ("MOUSEMOVE", "WM_MOUSEMOVE", "OnMouseMove", "void", MOUSE, False),
    ("MOUSEWHEEL", "WM_MOUSEWHEEL", "OnMouseWheel", "BOOL", ("UINT nFlags, short zDelta, CPoint pt", "LOWORD(_w), (short)HIWORD(_w), _AfxPt(_l)"), False),
    ("MOUSEHWHEEL", "WM_MOUSEHWHEEL", "OnMouseHWheel", "void", ("UINT nFlags, short zDelta, CPoint pt", "LOWORD(_w), (short)HIWORD(_w), _AfxPt(_l)"), False),
    ("MOVE", "WM_MOVE", "OnMove", "void", ("int x, int y", "(short)LOWORD(_l), (short)HIWORD(_l)"), False),
    ("MOVING", "WM_MOVING", "OnMoving", "void", ("UINT fwSide, LPRECT pRect", "(UINT)_w, (LPRECT)_l"), False),
    ("NCACTIVATE", "WM_NCACTIVATE", "OnNcActivate", "BOOL", ("BOOL bActive", "(BOOL)_w"), False),
    ("NCCALCSIZE", "WM_NCCALCSIZE", "OnNcCalcSize", "void", ("BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp", "(BOOL)_w, (NCCALCSIZE_PARAMS*)_l"), False),
    ("NCCREATE", "WM_NCCREATE", "OnNcCreate", "BOOL", ("LPCREATESTRUCT lpCreateStruct", "(LPCREATESTRUCT)_l"), False),
    ("NCDESTROY", "WM_NCDESTROY", "OnNcDestroy", "void", NONE, True),
    ("NCHITTEST", "WM_NCHITTEST", "OnNcHitTest", "LRESULT", ("CPoint point", "_AfxPt(_l)"), False),
    ("NCLBUTTONDBLCLK", "WM_NCLBUTTONDBLCLK", "OnNcLButtonDblClk", "void", NCMOUSE, False),
    ("NCLBUTTONDOWN", "WM_NCLBUTTONDOWN", "OnNcLButtonDown", "void", NCMOUSE, False),
    ("NCLBUTTONUP", "WM_NCLBUTTONUP", "OnNcLButtonUp", "void", NCMOUSE, False),
    ("NCMBUTTONDBLCLK", "WM_NCMBUTTONDBLCLK", "OnNcMButtonDblClk", "void", NCMOUSE, False),
    ("NCMBUTTONDOWN", "WM_NCMBUTTONDOWN", "OnNcMButtonDown", "void", NCMOUSE, False),
    ("NCMBUTTONUP", "WM_NCMBUTTONUP", "OnNcMButtonUp", "void", NCMOUSE, False),
    ("NCMOUSEHOVER", "WM_NCMOUSEHOVER", "OnNcMouseHover", "void", NCMOUSE, False),
    ("NCMOUSELEAVE", "WM_NCMOUSELEAVE", "OnNcMouseLeave", "void", NONE, False),
    ("NCMOUSEMOVE", "WM_NCMOUSEMOVE", "OnNcMouseMove", "void", NCMOUSE, False),
    ("NCPAINT", "WM_NCPAINT", "OnNcPaint", "void", NONE, False),
    ("NCRBUTTONDBLCLK", "WM_NCRBUTTONDBLCLK", "OnNcRButtonDblClk", "void", NCMOUSE, False),
    ("NCRBUTTONDOWN", "WM_NCRBUTTONDOWN", "OnNcRButtonDown", "void", NCMOUSE, False),
    ("NCRBUTTONUP", "WM_NCRBUTTONUP", "OnNcRButtonUp", "void", NCMOUSE, False),
    ("NOTIFYFORMAT", "WM_NOTIFYFORMAT", "OnNotifyFormat", "UINT", ("CWnd* pWnd, UINT nCommand", "_AfxWnd(_w), (UINT)_l"), False),
    ("PAINT", "WM_PAINT", "OnPaint", "void", NONE, False),
    ("PAINTCLIPBOARD", "WM_PAINTCLIPBOARD", "OnPaintClipboard", "void", ("CWnd* pClipAppWnd, HGLOBAL hPaintStruct", "_AfxWnd(_w), (HGLOBAL)_l"), False),
    ("PALETTECHANGED", "WM_PALETTECHANGED", "OnPaletteChanged", "void", ("CWnd* pFocusWnd", "_AfxWnd(_w)"), False),
    ("PALETTEISCHANGING", "WM_PALETTEISCHANGING", "OnPaletteIsChanging", "void", ("CWnd* pRealizeWnd", "_AfxWnd(_w)"), False),
    ("PARENTNOTIFY", "WM_PARENTNOTIFY", "OnParentNotify", "void", ("UINT message, LPARAM lParam", "(UINT)_w, _l"), True),
    ("POWERBROADCAST", "WM_POWERBROADCAST", "OnPowerBroadcast", "UINT", ("UINT nPowerEvent, LPARAM nEventData", "(UINT)_w, _l"), False),
    ("QUERYDRAGICON", "WM_QUERYDRAGICON", "OnQueryDragIcon", "HCURSOR", NONE, False),
    ("QUERYENDSESSION", "WM_QUERYENDSESSION", "OnQueryEndSession", "BOOL", NONE, False),
    ("QUERYNEWPALETTE", "WM_QUERYNEWPALETTE", "OnQueryNewPalette", "BOOL", NONE, False),
    ("QUERYOPEN", "WM_QUERYOPEN", "OnQueryOpen", "BOOL", NONE, False),
    ("RBUTTONDBLCLK", "WM_RBUTTONDBLCLK", "OnRButtonDblClk", "void", MOUSE, False),
    ("RBUTTONDOWN", "WM_RBUTTONDOWN", "OnRButtonDown", "void", MOUSE, False),
    ("RBUTTONUP", "WM_RBUTTONUP", "OnRButtonUp", "void", MOUSE, False),
    ("RENDERALLFORMATS", "WM_RENDERALLFORMATS", "OnRenderAllFormats", "void", NONE, False),
    ("RENDERFORMAT", "WM_RENDERFORMAT", "OnRenderFormat", "void", ("UINT nFormat", "(UINT)_w"), False),
    ("SETCURSOR", "WM_SETCURSOR", "OnSetCursor", "BOOL", ("CWnd* pWnd, UINT nHitTest, UINT message", "_AfxWnd(_w), LOWORD(_l), HIWORD(_l)"), False),
    ("SETFOCUS", "WM_SETFOCUS", "OnSetFocus", "void", ("CWnd* pOldWnd", "_AfxWnd(_w)"), False),
    ("SETTINGCHANGE", "WM_SETTINGCHANGE", "OnSettingChange", "void", ("UINT uFlags, LPCTSTR lpszSection", "(UINT)_w, (LPCTSTR)_l"), False),
    ("SHOWWINDOW", "WM_SHOWWINDOW", "OnShowWindow", "void", ("BOOL bShow, UINT nStatus", "(BOOL)_w, (UINT)_l"), False),
    ("SIZE", "WM_SIZE", "OnSize", "void", ("UINT nType, int cx, int cy", "(UINT)_w, (short)LOWORD(_l), (short)HIWORD(_l)"), False),
    ("SIZECLIPBOARD", "WM_SIZECLIPBOARD", "OnSizeClipboard", "void", ("CWnd* pClipAppWnd, HGLOBAL hRect", "_AfxWnd(_w), (HGLOBAL)_l"), False),
    ("SIZING", "WM_SIZING", "OnSizing", "void", ("UINT fwSide, LPRECT pRect", "(UINT)_w, (LPRECT)_l"), False),
    ("SPOOLERSTATUS", "WM_SPOOLERSTATUS", "OnSpoolerStatus", "void", ("UINT nStatus, UINT nJobs", "(UINT)_w, LOWORD(_l)"), False),
    ("STYLECHANGED", "WM_STYLECHANGED", "OnStyleChanged", "void", ("int nStyleType, LPSTYLESTRUCT lpStyleStruct", "(int)_w, (LPSTYLESTRUCT)_l"), False),
    ("STYLECHANGING", "WM_STYLECHANGING", "OnStyleChanging", "void", ("int nStyleType, LPSTYLESTRUCT lpStyleStruct", "(int)_w, (LPSTYLESTRUCT)_l"), False),
    ("SYSCHAR", "WM_SYSCHAR", "OnSysChar", "void", KEY, False),
    ("SYSCOLORCHANGE", "WM_SYSCOLORCHANGE", "OnSysColorChange", "void", NONE, True),
    ("SYSCOMMAND", "WM_SYSCOMMAND", "OnSysCommand", "void", ("UINT nID, LPARAM lParam", "(UINT)_w, _l"), False),
    ("SYSDEADCHAR", "WM_SYSDEADCHAR", "OnSysDeadChar", "void", KEY, False),
    ("SYSKEYDOWN", "WM_SYSKEYDOWN", "OnSysKeyDown", "void", KEY, False),
    ("SYSKEYUP", "WM_SYSKEYUP", "OnSysKeyUp", "void", KEY, False),
    ("TCARD", "WM_TCARD", "OnTCard", "void", ("UINT idAction, DWORD dwActionData", "(UINT)_w, (DWORD)_l"), False),
    ("THEMECHANGED", "WM_THEMECHANGED", "OnThemeChanged", "LRESULT", NONE, False),
    ("TIMECHANGE", "WM_TIMECHANGE", "OnTimeChange", "void", NONE, False),
    ("TIMER", "WM_TIMER", "OnTimer", "void", ("UINT_PTR nIDEvent", "(UINT_PTR)_w"), False),
    ("VKEYTOITEM", "WM_VKEYTOITEM", "OnVKeyToItem", "int", ("UINT nKey, CListBox* pListBox, UINT nIndex", "LOWORD(_w), (CListBox*)_AfxWnd(_l), HIWORD(_w)"), True),
    ("VSCROLL", "WM_VSCROLL", "OnVScroll", "void", SCROLL, True),
    ("VSCROLLCLIPBOARD", "WM_VSCROLLCLIPBOARD", "OnVScrollClipboard", "void", ("CWnd* pClipAppWnd, UINT nSBCode, UINT nPos", "_AfxWnd(_w), LOWORD(_l), HIWORD(_l)"), False),
    ("WINDOWPOSCHANGED", "WM_WINDOWPOSCHANGED", "OnWindowPosChanged", "void", ("WINDOWPOS* lpwndpos", "(WINDOWPOS*)_l"), False),
    ("WINDOWPOSCHANGING", "WM_WINDOWPOSCHANGING", "OnWindowPosChanging", "void", ("WINDOWPOS* lpwndpos", "(WINDOWPOS*)_l"), False),
    ("WININICHANGE", "WM_WININICHANGE", "OnWinIniChange", "void", ("LPCTSTR lpszSection", "(LPCTSTR)_l"), False),
    ("XBUTTONDBLCLK", "WM_XBUTTONDBLCLK", "OnXButtonDblClk", "void", XBTN, False),
    ("XBUTTONDOWN", "WM_XBUTTONDOWN", "OnXButtonDown", "void", XBTN, False),
    ("XBUTTONUP", "WM_XBUTTONUP", "OnXButtonUp", "void", XBTN, False),
]

# Handlers that the control classes get back from their parents.
REFLECT = [
    ("CTLCOLOR_REFLECT", "WM_CTLCOLOR", "CtlColor", "HBRUSH", ("CDC* pDC, UINT nCtlColor", "_AfxDC(_w), (UINT)_l")),
    ("DRAWITEM_REFLECT", "WM_DRAWITEM", "DrawItem", "void", ("LPDRAWITEMSTRUCT lpDrawItemStruct", "(LPDRAWITEMSTRUCT)_l")),
    ("MEASUREITEM_REFLECT", "WM_MEASUREITEM", "MeasureItem", "void", ("LPMEASUREITEMSTRUCT lpMeasureItemStruct", "(LPMEASUREITEMSTRUCT)_l")),
    ("COMPAREITEM_REFLECT", "WM_COMPAREITEM", "CompareItem", "int", ("LPCOMPAREITEMSTRUCT lpCompareItemStruct", "(LPCOMPAREITEMSTRUCT)_l")),
    ("DELETEITEM_REFLECT", "WM_DELETEITEM", "DeleteItem", "void", ("LPDELETEITEMSTRUCT lpDeleteItemStruct", "(LPDELETEITEMSTRUCT)_l")),
    ("CHARTOITEM_REFLECT", "WM_CHARTOITEM", "CharToItem", "int", ("UINT nKey, UINT nIndex", "LOWORD(_w), HIWORD(_w)")),
    ("VKEYTOITEM_REFLECT", "WM_VKEYTOITEM", "VKeyToItem", "int", ("UINT nKey, UINT nIndex", "LOWORD(_w), HIWORD(_w)")),
    ("HSCROLL_REFLECT", "WM_HSCROLL", "HScroll", "void", ("UINT nSBCode, UINT nPos", "LOWORD(_w), (UINT)(short)HIWORD(_w)")),
    ("VSCROLL_REFLECT", "WM_VSCROLL", "VScroll", "void", ("UINT nSBCode, UINT nPos", "LOWORD(_w), (UINT)(short)HIWORD(_w)")),
    ("PARENTNOTIFY_REFLECT", "WM_PARENTNOTIFY", "ParentNotify", "void", ("UINT message, LPARAM lParam", "(UINT)_w, _l")),
]

HEAD = "// Generated by tools/gen-msgmap.py; do not edit.\n"


def thunk(handler, ret, args):
    call = "static_cast<ThisClass*>(_t)->%s(%s)" % (handler, args)
    if ret == "void":
        body = "%s; return 0;" % call
    else:
        body = "return (LRESULT)(%s);" % call
    return ("[](CCmdTarget* _t, UINT _m, WPARAM _w, LPARAM _l) -> LRESULT { "
            "(void)_m; (void)_w; (void)_l; %s }" % body)


def main(root):
    out = [HEAD, "#ifndef __AFXMSG_WM__H__\n#define __AFXMSG_WM__H__\n\n"]
    for macro, msg, handler, ret, (params, args), _ in ROWS:
        out.append("#define ON_WM_%s() \\\n  {%s, 0, 0, 0, AfxSig_thunk, (AFX_PMSG)0, \\\n   %s},\n"
                   % (macro, msg, thunk(handler, ret, args)))
    for macro, msg, handler, ret, (params, args) in REFLECT:
        out.append("#define ON_WM_%s() \\\n  {WM_REFLECT_BASE + %s, 0, 0, 0, AfxSig_thunk, (AFX_PMSG)0, \\\n   %s},\n"
                   % (macro, msg, thunk(handler, ret, args)))
    out.append("\n#endif\n")
    open(os.path.join(root, "include/afxmsg_wm_.h"), "w").write("".join(out))

    decl = [HEAD]
    for macro, msg, handler, ret, (params, args), _ in ROWS:
        decl.append("afx_msg %s %s(%s);\n" % (ret, handler, params))
    open(os.path.join(root, "include/afxwnd_handlers_.h"), "w").write("".join(decl))

    body = [HEAD]
    for macro, msg, handler, ret, (params, args), custom in ROWS:
        if custom:
            continue
        names = [p.split()[-1].lstrip("*") for p in params.split(",")] if params else []
        unused = "".join(" (void)%s;" % n for n in names)
        if ret == "void":
            body.append("%s CWnd::%s(%s) {%s Default(); }\n" % (ret, handler, params, unused))
        else:
            body.append("%s CWnd::%s(%s) {%s return (%s)Default(); }\n" % (ret, handler, params, unused, ret))
    open(os.path.join(root, "src/wnd_handlers.inc"), "w").write("".join(body))


main(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
