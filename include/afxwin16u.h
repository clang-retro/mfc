// afxwin16u.h - the Win32 window, GDI, common control and OLE control
// declarations that the window classes of afxwin.h use, for 16-bit
// Windows.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
//
// 16-bit Windows has these things in one of three ways:
//
// - It has a function with a different name or form (SetBrushOrg for
//   SetBrushOrgEx). src/win16u.cpp does the Win32 function with it.
// - It has nothing like it (paths, world transforms, enhanced
//   metafiles). The function fails as Win32 fails when the system does
//   not support the call: it returns 0 or FALSE.
// - The common controls and OLE controls are not there. Only the types
//   are here, so that the headers compile.
//
// The functions are C++ functions of the library, not imports. Thus they
// do not hide a USER or GDI export of the same name in a later Windows.
#ifndef __AFXWIN16U_H__
#define __AFXWIN16U_H__

#include <ole2.h>
#include <dispatch.h>

// windows.h names a comm event CN_EVENT; MFC uses the name for the
// command code of OLE control events (afxmsg_.h).
#undef CN_EVENT

typedef float FLOAT;
typedef FLOAT FAR *PFLOAT;
typedef const void FAR *LPCVOID;

// Handles that 16-bit Windows does not have.
DECLARE_HANDLE(HENHMETAFILE);
DECLARE_HANDLE(HIMAGELIST);
DECLARE_HANDLE(HTREEITEM);
DECLARE_HANDLE(HPROPSHEETPAGE);
DECLARE_HANDLE(HMONITOR);
DECLARE_HANDLE(HKL);

// A Win32 WPARAM holds two words; a 16-bit WPARAM holds one. The
// common control messages that use the macro do not exist on 16-bit
// Windows, so the high word is lost.
#ifndef MAKEWPARAM
#define MAKEWPARAM(l, h) ((WPARAM)(WORD)(l))
#endif

// Messages. The values are those of Win32; 16-bit Windows never sends
// them, but the library sends some of them to itself.
#ifndef WM_CTLCOLORMSGBOX
// The window procedure of the library changes a WM_CTLCOLOR message to
// the one of these that its CTLCOLOR_ code gives.
#define WM_CTLCOLORMSGBOX 0x0132
#define WM_CTLCOLOREDIT 0x0133
#define WM_CTLCOLORLISTBOX 0x0134
#define WM_CTLCOLORBTN 0x0135
#define WM_CTLCOLORDLG 0x0136
#define WM_CTLCOLORSCROLLBAR 0x0137
#define WM_CTLCOLORSTATIC 0x0138
#endif
#ifndef WM_NOTIFY
#define WM_NOTIFY 0x004E
#endif
#define WM_GETICON 0x007F
#define WM_SETICON 0x0080
#define WM_PRINT 0x0317
#define WM_PRINTCLIENT 0x0318

#define BM_GETIMAGE 0x00F6
#define BM_SETIMAGE 0x00F7
#define STM_SETIMAGE 0x0172
#define STM_GETIMAGE 0x0173
#define IMAGE_BITMAP 0
#define IMAGE_ICON 1
#define IMAGE_CURSOR 2
#define IMAGE_ENHMETAFILE 3

// Edit, list box and combo box messages of Win32. They have WM_USER
// values in 16-bit Windows, which the library does not use.
#define EM_SCROLLCARET 0x00B7
#define EM_SETLIMITTEXT 0x00C5
#define EM_GETLIMITTEXT 0x00D5
#define EM_SETMARGINS 0x00D3
#define EM_GETMARGINS 0x00D4
#define EM_POSFROMCHAR 0x00D6
#define EM_CHARFROMPOS 0x00D7
#define EC_LEFTMARGIN 0x0001
#define EC_RIGHTMARGIN 0x0002
#define LB_SETLOCALE 0x01A5
#define LB_GETLOCALE 0x01A6
#define LB_INITSTORAGE 0x01A8
#define LB_ITEMFROMPOINT 0x01A9
#define LB_SETANCHORINDEX 0x019C
#define LB_GETANCHORINDEX 0x019D
#define LB_SELITEMRANGEEX 0x0183
#define CB_SETLOCALE 0x0159
#define CB_GETLOCALE 0x015A
#define CB_GETTOPINDEX 0x015B
#define CB_SETTOPINDEX 0x015C
#define CB_GETHORIZONTALEXTENT 0x015D
#define CB_SETHORIZONTALEXTENT 0x015E
#define CB_GETDROPPEDWIDTH 0x015F
#define CB_SETDROPPEDWIDTH 0x0160
#define CB_INITSTORAGE 0x0161

// DrawState.
#define DST_TEXT 0x0001
#define DST_PREFIXTEXT 0x0002
#define DST_ICON 0x0003
#define DST_BITMAP 0x0004
typedef BOOL(CALLBACK *DRAWSTATEPROC)(HDC hdc, LPARAM lData, WPARAM wData, int cx, int cy);

// Scroll bars.
#define SIF_RANGE 0x0001
#define SIF_PAGE 0x0002
#define SIF_POS 0x0004
#define SIF_DISABLENOSCROLL 0x0008
#define SIF_TRACKPOS 0x0010
#define SIF_ALL (SIF_RANGE | SIF_PAGE | SIF_POS | SIF_TRACKPOS)
typedef struct tagSCROLLINFO {
  UINT cbSize;
  UINT fMask;
  int nMin;
  int nMax;
  UINT nPage;
  int nPos;
  int nTrackPos;
} SCROLLINFO, FAR *LPSCROLLINFO;
typedef const SCROLLINFO FAR *LPCSCROLLINFO;

// Menus.
#define MIIM_STATE 0x0001
#define MIIM_ID 0x0002
#define MIIM_SUBMENU 0x0004
#define MIIM_CHECKMARKS 0x0008
#define MIIM_TYPE 0x0010
#define MIIM_DATA 0x0020
#define MIIM_STRING 0x0040
#define MIIM_BITMAP 0x0080
#define MIIM_FTYPE 0x0100
// The MFT_ types of a MENUITEMINFO are the MF_ flags.
#define MFT_STRING MF_STRING
#define MFT_BITMAP MF_BITMAP
#define MFT_SEPARATOR MF_SEPARATOR
#define MFT_OWNERDRAW MF_OWNERDRAW
#define MFT_MENUBARBREAK MF_MENUBARBREAK
#define MFT_MENUBREAK MF_MENUBREAK
typedef struct tagMENUITEMINFO {
  UINT cbSize;
  UINT fMask;
  UINT fType;
  UINT fState;
  UINT wID;
  HMENU hSubMenu;
  HBITMAP hbmpChecked;
  HBITMAP hbmpUnchecked;
  DWORD dwItemData;
  LPSTR dwTypeData;
  UINT cch;
  HBITMAP hbmpItem;
} MENUITEMINFO, FAR *LPMENUITEMINFO;
typedef const MENUITEMINFO FAR *LPCMENUITEMINFO;
typedef struct tagTPMPARAMS {
  UINT cbSize;
  RECT rcExclude;
} TPMPARAMS, FAR *LPTPMPARAMS;
typedef void MENUTEMPLATE;

// Dialog templates. The 16-bit form has byte counts and no padding;
// the library reads the template as bytes (src/dlgtmpl.cpp).
#pragma pack(push, 1)
typedef struct tagDLGTEMPLATE16 {
  DWORD style;
  BYTE cdit;
  short x;
  short y;
  short cx;
  short cy;
} DLGTEMPLATE;
#pragma pack(pop)
typedef DLGTEMPLATE FAR *LPDLGTEMPLATE;
typedef const DLGTEMPLATE FAR *LPCDLGTEMPLATE;

// GDI structures of Win32.
typedef struct tagXFORM {
  FLOAT eM11, eM12, eM21, eM22, eDx, eDy;
} XFORM, FAR *LPXFORM;
typedef struct _RGNDATAHEADER {
  DWORD dwSize, iType, nCount, nRgnSize;
  RECT rcBound;
} RGNDATAHEADER;
typedef struct _RGNDATA {
  RGNDATAHEADER rdh;
  char Buffer[1];
} RGNDATA, FAR *LPRGNDATA;
typedef struct tagCOLORADJUSTMENT {
  WORD caSize, caFlags, caIlluminantIndex, caRedGamma, caGreenGamma, caBlueGamma,
      caReferenceBlack, caReferenceWhite;
  short caContrast, caBrightness, caColorfulness, caRedGreenTint;
} COLORADJUSTMENT, FAR *LPCOLORADJUSTMENT;
typedef struct tagEXTLOGPEN {
  DWORD elpPenStyle, elpWidth;
  UINT elpBrushStyle;
  COLORREF elpColor;
  DWORD elpHatch, elpNumEntries;
  DWORD elpStyleEntry[1];
} EXTLOGPEN;
typedef struct tagDRAWTEXTPARAMS {
  UINT cbSize;
  int iTabLength, iLeftMargin, iRightMargin;
  UINT uiLengthDrawn;
} DRAWTEXTPARAMS, FAR *LPDRAWTEXTPARAMS;

// Window messages with structures.
typedef struct tagHELPINFO {
  UINT cbSize;
  int iContextType;
  int iCtrlId;
  HANDLE hItemHandle;
  DWORD dwContextId;
  POINT MousePos;
} HELPINFO, FAR *LPHELPINFO;
typedef struct tagSTYLESTRUCT {
  DWORD styleOld, styleNew;
} STYLESTRUCT, FAR *LPSTYLESTRUCT;
typedef struct tagCOPYDATASTRUCT {
  DWORD dwData, cbData;
  LPVOID lpData;
} COPYDATASTRUCT, FAR *PCOPYDATASTRUCT;
typedef struct tagNMHDR {
  HWND hwndFrom;
  UINT idFrom;
  UINT code;
} NMHDR, FAR *LPNMHDR;
typedef struct tagTRACKMOUSEEVENT {
  DWORD cbSize, dwFlags;
  HWND hwndTrack;
  DWORD dwHoverTime;
} TRACKMOUSEEVENT, FAR *LPTRACKMOUSEEVENT;

#define THREAD_PRIORITY_NORMAL 0

// The OLE control interfaces, which the connection maps of CCmdTarget
// name (include/win16/ocidl.h).
#include <ocidl.h>
// 16-bit TYPELIB has no UnRegisterTypeLib: src/win16ole.cpp deletes the
// registry keys of the version.
HRESULT UnRegisterTypeLib(REFGUID libID, WORD wVerMajor, WORD wVerMinor, LCID lcid,
                          SYSKIND syskind);
// The task allocator functions of Win32 (src/win16ole.cpp). They use the
// IMalloc of the task (CoGetMalloc with MEMCTX_TASK).
LPVOID CoTaskMemAlloc(ULONG cb);
LPVOID CoTaskMemRealloc(LPVOID pv, ULONG cb);
void CoTaskMemFree(LPVOID pv);
// Gives the number of entries of an accelerator table. 16-bit Windows has
// no CopyAcceleratorTable; this reads the table resource.
int _AfxW16AcceleratorCount(HACCEL hAccel);
#define _HRESULT_TYPEDEF_(sc) ((HRESULT)(sc))
// 16-bit Windows has no enhanced metafiles. CF_ENHMETAFILE is a clipboard
// format that no 16-bit program gives, and TYMED_ENHMF adds no medium.
#define CF_ENHMETAFILE 14
#define TYMED_ENHMF 0
// The errors of IOleCommandTarget (docobj.h), as in Win32.
#define OLECMDERR_E_FIRST ((HRESULT)0x80040100L)
#define OLECMDERR_E_NOTSUPPORTED OLECMDERR_E_FIRST
#define OLECMDERR_E_DISABLED (OLECMDERR_E_FIRST + 1)
#define OLECMDERR_E_NOHELP (OLECMDERR_E_FIRST + 2)
#define OLECMDERR_E_CANCELED (OLECMDERR_E_FIRST + 3)
#define OLECMDERR_E_UNKNOWNGROUP (OLECMDERR_E_FIRST + 4)

// The common control types come from the portable layer (port/include),
// which the build puts after the 16-bit SDK on the include path.
#include <commctrl.h>

// The library draws the toolbar and the status bar (src/win16bar.cpp).
// Their classes have names of their own: Wine can also have the 32-bit
// classes of comctl32.dll, which do not read the 16-bit structures.
#undef TOOLBARCLASSNAME
#undef STATUSCLASSNAME
#define TOOLBARCLASSNAME "AfxToolbar16"
#define STATUSCLASSNAME "AfxStatusBar16"

// The library also draws the progress bar, the trackbar, the up-down
// control and the tab control (src/win16ctl.cpp).
#undef PROGRESS_CLASS
#undef TRACKBAR_CLASS
#undef UPDOWN_CLASS
#undef WC_TABCONTROL
#define PROGRESS_CLASS "AfxProgress16"
#define TRACKBAR_CLASS "AfxTrackbar16"
#define UPDOWN_CLASS "AfxUpDown16"
#define WC_TABCONTROL "AfxTabControl16"

// The library also draws the header control and the list view
// (src/win16lv.cpp), the tree view (src/win16tv.cpp) and the tooltip
// (src/win16tt.cpp), and the hot key, the IP address control and the
// pager (src/win16misc.cpp).
#undef TOOLTIPS_CLASS
#undef HOTKEY_CLASS
#undef WC_IPADDRESS
#undef WC_PAGESCROLLER
#undef WC_HEADER
#undef WC_LISTVIEW
#undef WC_TREEVIEW
#define WC_HEADER "AfxHeader16"
#define WC_LISTVIEW "AfxListView16"
#define WC_TREEVIEW "AfxTreeView16"
#define TOOLTIPS_CLASS "AfxToolTip16"
#define HOTKEY_CLASS "AfxHotKey16"
#define WC_IPADDRESS "AfxIPAddress16"
#define WC_PAGESCROLLER "AfxPager16"

// GDI.
DWORD GetObjectType(HGDIOBJ h);
HPEN ExtCreatePen(DWORD iPenStyle, DWORD cWidth, const LOGBRUSH FAR *plbrush, DWORD cStyle,
                  const DWORD FAR *pstyle);
HBRUSH CreateDIBPatternBrushPt(LPCVOID lpPackedDIB, UINT iUsage);
HBRUSH GetSysColorBrush(int nIndex);
DWORD GetRegionData(HRGN hrgn, DWORD nCount, LPRGNDATA lpRgnData);
HRGN ExtCreateRegion(const XFORM FAR *lpx, DWORD nCount, const RGNDATA FAR *lpData);
int ExtSelectClipRgn(HDC hdc, HRGN hrgn, int mode);
BOOL SetBrushOrgEx(HDC hdc, int x, int y, LPPOINT lppt);
COLORREF GetDCBrushColor(HDC hdc);
COLORREF SetDCBrushColor(HDC hdc, COLORREF color);
COLORREF GetDCPenColor(HDC hdc);
COLORREF SetDCPenColor(HDC hdc, COLORREF color);
BOOL SetPixelV(HDC hdc, int x, int y, COLORREF color);
BOOL GetTextExtentPoint32(HDC hdc, LPCSTR lpString, int c, LPSIZE psizl);
DWORD GetFontLanguageInfo(HDC hdc);
int GetGraphicsMode(HDC hdc);
int SetGraphicsMode(HDC hdc, int iMode);
BOOL SetWorldTransform(HDC hdc, const XFORM FAR *lpxf);
BOOL ModifyWorldTransform(HDC hdc, const XFORM FAR *lpxf, DWORD mode);
BOOL GetWorldTransform(HDC hdc, LPXFORM lpxf);
BOOL GetColorAdjustment(HDC hdc, LPCOLORADJUSTMENT lpca);
BOOL SetColorAdjustment(HDC hdc, const COLORADJUSTMENT FAR *lpca);
int GetArcDirection(HDC hdc);
int SetArcDirection(HDC hdc, int dir);
BOOL GetMiterLimit(HDC hdc, PFLOAT plimit);
BOOL SetMiterLimit(HDC hdc, FLOAT limit, PFLOAT old);
BOOL AngleArc(HDC hdc, int x, int y, DWORD r, FLOAT StartAngle, FLOAT SweepAngle);
BOOL ArcTo(HDC hdc, int left, int top, int right, int bottom, int xr1, int yr1, int xr2, int yr2);
BOOL PolyBezier(HDC hdc, const POINT FAR *apt, DWORD cpt);
BOOL PolyBezierTo(HDC hdc, const POINT FAR *apt, DWORD cpt);
BOOL PolyDraw(HDC hdc, const POINT FAR *apt, const BYTE FAR *aj, int cpt);
BOOL PolyPolyline(HDC hdc, const POINT FAR *apt, const DWORD FAR *asz, DWORD csz);
BOOL PolylineTo(HDC hdc, const POINT FAR *apt, DWORD cpt);
BOOL MaskBlt(HDC hdcDest, int xDest, int yDest, int width, int height, HDC hdcSrc, int xSrc,
             int ySrc, HBITMAP hbmMask, int xMask, int yMask, DWORD rop);
BOOL PlgBlt(HDC hdcDest, const POINT FAR *lpPoint, HDC hdcSrc, int xSrc, int ySrc, int width,
            int height, HBITMAP hbmMask, int xMask, int yMask);
BOOL AbortPath(HDC hdc);
BOOL BeginPath(HDC hdc);
BOOL EndPath(HDC hdc);
BOOL CloseFigure(HDC hdc);
BOOL FillPath(HDC hdc);
BOOL FlattenPath(HDC hdc);
BOOL StrokePath(HDC hdc);
BOOL StrokeAndFillPath(HDC hdc);
BOOL WidenPath(HDC hdc);
BOOL SelectClipPath(HDC hdc, int mode);
int GetPath(HDC hdc, LPPOINT apt, LPBYTE aj, int cpt);
HDC CreateEnhMetaFile(HDC hdc, LPCSTR lpFilename, const RECT FAR *lprc, LPCSTR lpDesc);
HENHMETAFILE CloseEnhMetaFile(HDC hdc);
BOOL PlayEnhMetaFile(HDC hdc, HENHMETAFILE hmf, const RECT FAR *lprect);

// USER.
BOOL CheckMenuRadioItem(HMENU hmenu, UINT first, UINT last, UINT check, UINT flags);
HWND ChildWindowFromPointEx(HWND hwnd, POINT pt, UINT flags);
BOOL DrawAnimatedRects(HWND hwnd, int idAni, const RECT FAR *lprcFrom, const RECT FAR *lprcTo);
BOOL DrawCaption(HWND hwnd, HDC hdc, const RECT FAR *lprect, UINT flags);
BOOL DrawEdge(HDC hdc, LPRECT qrc, UINT edge, UINT grfFlags);
BOOL DrawFrameControl(HDC hdc, LPRECT lprc, UINT uType, UINT uState);
BOOL DrawState(HDC hdc, HBRUSH hbrFore, DRAWSTATEPROC qfnCallBack, LPARAM lData, WPARAM wData,
               int x, int y, int cx, int cy, UINT uFlags);
int DrawTextEx(HDC hdc, LPSTR lpchText, int cchText, LPRECT lprc, UINT format,
               LPDRAWTEXTPARAMS lpdtp);
HWND FindWindowEx(HWND hWndParent, HWND hWndChildAfter, LPCSTR lpszClass, LPCSTR lpszWindow);
HWND GetForegroundWindow();
BOOL SetForegroundWindow(HWND hWnd);
DWORD GetMenuContextHelpId(HMENU hmenu);
BOOL SetMenuContextHelpId(HMENU hmenu, DWORD dwContextHelpId);
DWORD GetWindowContextHelpId(HWND hwnd);
BOOL SetWindowContextHelpId(HWND hwnd, DWORD dwContextHelpId);
UINT GetMenuDefaultItem(HMENU hMenu, UINT fByPos, UINT gmdiFlags);
BOOL SetMenuDefaultItem(HMENU hMenu, UINT uItem, UINT fByPos);
BOOL GetMenuItemInfo(HMENU hmenu, UINT item, BOOL fByPosition, LPMENUITEMINFO lpmii);
BOOL SetMenuItemInfo(HMENU hmenu, UINT item, BOOL fByPosition, LPCMENUITEMINFO lpmii);
BOOL InsertMenuItem(HMENU hmenu, UINT item, BOOL fByPosition, LPCMENUITEMINFO lpmii);
BOOL GetScrollInfo(HWND hwnd, int nBar, LPSCROLLINFO lpsi);
int SetScrollInfo(HWND hwnd, int nBar, LPCSCROLLINFO lpsi, BOOL redraw);
int GetWindowRgn(HWND hWnd, HRGN hRgn);
int SetWindowRgn(HWND hWnd, HRGN hRgn, BOOL bRedraw);
BOOL SendNotifyMessage(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);

// Threads. 16-bit Windows has one thread in each task.
BOOL PostThreadMessage(DWORD idThread, UINT Msg, WPARAM wParam, LPARAM lParam);
DWORD ResumeThread(HANDLE hThread);
DWORD SuspendThread(HANDLE hThread);
int GetThreadPriority(HANDLE hThread);
BOOL SetThreadPriority(HANDLE hThread, int nPriority);

// More window, GDI and dialog values of Win32. 16-bit Windows ignores
// the style bits that it does not know; Windows 95 obeys them for a
// 16-bit program too.
#define WS_EX_CLIENTEDGE 0x00000200L
#define DS_CENTER 0x0800L
#define DS_CENTERMOUSE 0x1000L
#define GWLP_WNDPROC GWL_WNDPROC
#define GWLP_HINSTANCE GWW_HINSTANCE
#define GWLP_HWNDPARENT GWW_HWNDPARENT
#define GWLP_ID GWW_ID
#define GWLP_USERDATA (-21)
#define DWLP_MSGRESULT DWL_MSGRESULT
#define DWLP_DLGPROC DWL_DLGPROC
#define DWLP_USER DWL_USER
#define WM_DISPLAYCHANGE 0x007E
#define WM_MOUSEWHEEL 0x020A
#define WM_MDIREFRESHMENU 0x0234
#define WHEEL_DELTA 120
#define WHEEL_PAGESCROLL 0xFFFFU
#define SPI_GETWORKAREA 48
#define SPI_GETWHEELSCROLLLINES 104
#define VK_KANA 0x15
#define SW_SHOWDEFAULT 10
#define MA_NOACTIVATEANDEAT 4
#define OFN_EXPLORER 0x00080000L
#define HELP_FINDER 0x000B
#define OBJ_PEN 1
#define OBJ_BRUSH 2
#define OBJ_DC 3
#define OBJ_METADC 4
#define OBJ_PAL 5
#define OBJ_FONT 6
#define OBJ_BITMAP 7
#define OBJ_REGION 8
#define OBJ_METAFILE 9
#define OBJ_MEMDC 10
#define OBJ_EXTPEN 11
#define GDI_ERROR 0xFFFFFFFFL
#define CLR_INVALID 0xFFFFFFFFL
#define GM_COMPATIBLE 1
#define AD_COUNTERCLOCKWISE 1
#define DFC_BUTTON 4
#define DFCS_BUTTONCHECK 0x0000
#define DFCS_BUTTONRADIO 0x0004
#define DFCS_BUTTON3STATE 0x0008
#define DFCS_BUTTONPUSH 0x0010
#define DFCS_INACTIVE 0x0100
#define DFCS_PUSHED 0x0200
#define DFCS_CHECKED 0x0400
#define DFCS_FLAT 0x4000
#define BDR_RAISEDOUTER 0x0001
#define BDR_SUNKENOUTER 0x0002
#define BDR_RAISEDINNER 0x0004
#define BDR_SUNKENINNER 0x0008
#define EDGE_RAISED (BDR_RAISEDOUTER | BDR_RAISEDINNER)
#define EDGE_SUNKEN (BDR_SUNKENOUTER | BDR_SUNKENINNER)
#define EDGE_ETCHED (BDR_SUNKENOUTER | BDR_RAISEDINNER)
#define EDGE_BUMP (BDR_RAISEDOUTER | BDR_SUNKENINNER)
#define BF_LEFT 0x0001
#define BF_TOP 0x0002
#define BF_RIGHT 0x0004
#define BF_BOTTOM 0x0008
#define BF_RECT (BF_LEFT | BF_TOP | BF_RIGHT | BF_BOTTOM)
#define BF_ADJUST 0x2000
#ifndef DST_COMPLEX
#define DST_COMPLEX 0x0000
#endif
#ifndef DSS_DISABLED
#define DSS_NORMAL 0x0000
#define DSS_DISABLED 0x0020
#endif
#define FLASHW_STOP 0
#define FLASHW_CAPTION 0x00000001
#define FLASHW_TRAY 0x00000002
#define FLASHW_ALL (FLASHW_CAPTION | FLASHW_TRAY)
#define FLASHW_TIMER 0x00000004
typedef struct {
  UINT cbSize;
  HWND hwnd;
  DWORD dwFlags;
  UINT uCount;
  DWORD dwTimeout;
} FLASHWINFO, FAR *PFLASHWINFO;
#define MONITOR_DEFAULTTONULL 0x00000000
#define MONITOR_DEFAULTTOPRIMARY 0x00000001
#define MONITOR_DEFAULTTONEAREST 0x00000002
typedef struct tagMONITORINFO {
  DWORD cbSize;
  RECT rcMonitor;
  RECT rcWork;
  DWORD dwFlags;
} MONITORINFO, FAR *LPMONITORINFO;

#define WM_CAPTURECHANGED 0x0215
#define WM_SIZING 0x0214
#define WMSZ_LEFT 1
#define WMSZ_RIGHT 2
#define WMSZ_TOP 3
#define WMSZ_TOPLEFT 4
#define WMSZ_TOPRIGHT 5
#define WMSZ_BOTTOM 6
#define WMSZ_BOTTOMLEFT 7
#define WMSZ_BOTTOMRIGHT 8
#define HTCLOSE 20
#define WS_EX_TOOLWINDOW 0x00000080L
// 16-bit Windows has one caption height.
#define SM_CYSMCAPTION SM_CYCAPTION
#define CF_NOSCRIPTSEL 0x00800000L
// The OPENFILENAME of 16-bit Windows has no Windows 2000 fields.
#define OPENFILENAME_SIZE_VERSION_400 sizeof(OPENFILENAME)
// 16-bit Windows has one cursor with four arrows.
#define IDC_SIZEALL IDC_SIZE
// The nearest stock font of 16-bit Windows.
#define DEFAULT_GUI_FONT ANSI_VAR_FONT
// 16-bit Windows gives 0 for these device capabilities; the library then
// uses HORZRES and VERTRES.
#define PHYSICALWIDTH 110
#define PHYSICALHEIGHT 111
#define PHYSICALOFFSETX 112
#define PHYSICALOFFSETY 113
#ifndef PDERR_DNDMMISMATCH
#define PDERR_DNDMMISMATCH 0x1009
#endif
#define LR_DEFAULTCOLOR 0x0000
#define LR_LOADTRANSPARENT 0x0020
#define LR_DEFAULTSIZE 0x0040
#define LR_LOADFROMFILE 0x0010
#define LR_LOADMAP3DCOLORS 0x1000
#define LR_CREATEDIBSECTION 0x2000
#define LR_SHARED 0x8000

/// Gives a long value of a window. A far pointer fits in a LONG.
inline LONG_PTR GetWindowLongPtr(HWND hWnd, int nIndex) { return GetWindowLong(hWnd, nIndex); }
/// Sets a long value of a window and gives the old value.
inline LONG_PTR SetWindowLongPtr(HWND hWnd, int nIndex, LONG_PTR dwNewLong) {
  return SetWindowLong(hWnd, nIndex, dwNewLong);
}

// USER and KERNEL exports that windows.h of the SDK does not declare.
extern "C" {
ATOM WINAPI GlobalAddAtom(LPCSTR lpString);
ATOM WINAPI GlobalDeleteAtom(ATOM nAtom);
ATOM WINAPI GlobalFindAtom(LPCSTR lpString);
UINT WINAPI GlobalGetAtomName(ATOM nAtom, LPSTR lpBuffer, int nSize);
HWND WINAPI WindowFromDC(HDC hDC);
}

// GDI and USER functions of Win32 that src/win16u.cpp does.
HGDIOBJ GetCurrentObject(HDC hdc, UINT type);
HPALETTE CreateHalftonePalette(HDC hdc);
HRGN PathToRegion(HDC hdc);
BOOL TrackPopupMenuEx(HMENU hMenu, UINT uFlags, int x, int y, HWND hWnd, LPTPMPARAMS lptpm);
BOOL DestroyAcceleratorTable(HACCEL hAccel);
BOOL GetTextExtentExPoint(HDC hdc, LPCSTR lpszString, int cchString, int nMaxExtent,
                          LPINT lpnFit, LPINT alpDx, LPSIZE lpSize);
HANDLE LoadImage(HINSTANCE hInst, LPCSTR name, UINT type, int cx, int cy, UINT fuLoad);
HANDLE CopyImage(HANDLE h, UINT type, int cx, int cy, UINT flags);
LPWSTR lstrcpynW(LPWSTR lpString1, LPCWSTR lpString2, int iMaxLength);

// The registry. 16-bit Windows has one tree, HKEY_CLASSES_ROOT, and
// each key has one value, the one without a name. A call for another
// tree or for a named value fails with ERROR_BADKEY, which has the
// value of the Win32 ERROR_FILE_NOT_FOUND. Thus a program that calls
// SetRegistryKey keeps its settings in its INI file (CWinApp).
typedef DWORD REGSAM;
#define HKEY_CURRENT_USER ((HKEY)0x80000001UL)
#define HKEY_LOCAL_MACHINE ((HKEY)0x80000002UL)
#define KEY_READ 0x00020019L
#define KEY_WRITE 0x00020006L
#define KEY_ALL_ACCESS 0x000F003FL
#define REG_OPTION_NON_VOLATILE 0x00000000L
#define REG_CREATED_NEW_KEY 0x00000001L
#define REG_OPENED_EXISTING_KEY 0x00000002L
#define REG_NONE 0
#define REG_BINARY 3
#define REG_DWORD 4
LONG RegOpenKeyEx(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired,
                  PHKEY phkResult);
LONG RegCreateKeyEx(HKEY hKey, LPCSTR lpSubKey, DWORD Reserved, LPSTR lpClass, DWORD dwOptions,
                    REGSAM samDesired, LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                    PHKEY phkResult, LPDWORD lpdwDisposition);
LONG RegQueryValueEx(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType,
                     LPBYTE lpData, LPDWORD lpcbData);
LONG RegSetValueEx(HKEY hKey, LPCSTR lpValueName, DWORD Reserved, DWORD dwType,
                   const BYTE FAR *lpData, DWORD cbData);
LONG RegDeleteValue(HKEY hKey, LPCSTR lpValueName);

// The task. 16-bit Windows has one thread in each task: the thread ID
// is the task handle, and CreateThread fails with ERROR_NOT_SUPPORTED.
#define CREATE_SUSPENDED 0x00000004L
#define STILL_ACTIVE 0x00000103L
typedef DWORD (FAR PASCAL *LPTHREAD_START_ROUTINE)(LPVOID lpThreadParameter);
LPSTR GetCommandLine();
DWORD GetCurrentThreadId();
DWORD GetWindowThreadProcessId(HWND hWnd, LPDWORD lpdwProcessId);
HANDLE CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes, UINT dwStackSize,
                    LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter,
                    DWORD dwCreationFlags, LPDWORD lpThreadId);
void ExitThread(DWORD dwExitCode);
BOOL GetExitCodeThread(HANDLE hThread, LPDWORD lpExitCode);

// Events, mutexes and semaphores. They are objects of the task. A wait
// for an object that is not signaled cannot end, because no other
// thread can signal it: the wait gives WAIT_TIMEOUT when the time is
// up, or WAIT_FAILED for an INFINITE wait. MsgWaitForMultipleObjects
// also ends when a message comes. CloseHandle releases the objects.
#define ERROR_NOT_OWNER 288L
#define ERROR_TOO_MANY_POSTS 298L
#define ERROR_POSSIBLE_DEADLOCK 1131L
#define WAIT_OBJECT_0 0x00000000L
#define WAIT_ABANDONED 0x00000080L
#define WAIT_ABANDONED_0 0x00000080L
#define WAIT_TIMEOUT 0x00000102L
#define WAIT_FAILED 0xFFFFFFFFL
// windows.h has the QS_ values of 16-bit Windows.
#ifndef QS_INPUT
#define QS_INPUT (QS_MOUSE | QS_KEY)
#endif
HANDLE CreateEvent(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState,
                   LPCSTR lpName);
BOOL SetEvent(HANDLE hEvent);
BOOL ResetEvent(HANDLE hEvent);
BOOL PulseEvent(HANDLE hEvent);
HANDLE CreateMutex(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName);
BOOL ReleaseMutex(HANDLE hMutex);
HANDLE CreateSemaphore(LPSECURITY_ATTRIBUTES lpSemaphoreAttributes, LONG lInitialCount,
                       LONG lMaximumCount, LPCSTR lpName);
BOOL ReleaseSemaphore(HANDLE hSemaphore, LONG lReleaseCount, LPLONG lpPreviousCount);
DWORD WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);
DWORD WaitForMultipleObjects(DWORD nCount, const HANDLE FAR *lpHandles, BOOL bWaitAll,
                             DWORD dwMilliseconds);
DWORD MsgWaitForMultipleObjects(DWORD nCount, const HANDLE FAR *pHandles, BOOL fWaitAll,
                                DWORD dwMilliseconds, DWORD dwWakeMask);
// For _AfxW16CloseHandle (src/win16.cpp): releases the object if the
// handle is one of an event, a mutex or a semaphore.
BOOL _AfxW16CloseSyncHandle(HANDLE h);

#endif // __AFXWIN16U_H__
