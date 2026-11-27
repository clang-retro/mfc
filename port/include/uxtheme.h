// uxtheme.h - visual styles. A program that asks for version 6 of the
// common controls in its manifest (or runs with WINPORT_VISUAL_STYLES=1)
// gets the portable layer's own theme; otherwise the controls draw the
// classic look, as Windows does with visual styles off: no theme opens,
// and the drawing calls fail for want of one. The parts and states are in
// vsstyle.h, the properties in vssym32.h.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_UXTHEME_H
#define _WINPORT_UXTHEME_H

#include <windows.h>
#include <commctrl.h>

typedef HANDLE HTHEME;
#ifndef E_PROP_ID_UNSUPPORTED
#define E_PROP_ID_UNSUPPORTED ((HRESULT)0x80070490L)
#endif

#define ETDT_DISABLE 0x00000001
#define ETDT_ENABLE 0x00000002
#define ETDT_USETABTEXTURE 0x00000004
#define ETDT_ENABLETAB (ETDT_ENABLE | ETDT_USETABTEXTURE)

#define STAP_ALLOW_NONCLIENT 0x00000001
#define STAP_ALLOW_CONTROLS 0x00000002
#define STAP_ALLOW_WEBCONTENT 0x00000004

#define DTBG_CLIPRECT 0x00000001
#define DTBG_DRAWSOLID 0x00000002
#define DTBG_OMITBORDER 0x00000004
#define DTBG_OMITCONTENT 0x00000008
#define DTBG_COMPUTINGREGION 0x00000010
#define DTBG_MIRRORDC 0x00000020

#define DTT_GRAYED 0x00000001

#define OTD_FORCE_RECT_SIZING 0x00000001
#define OTD_NONCLIENT 0x00000002

typedef enum THEMESIZE { TS_MIN, TS_TRUE, TS_DRAW } THEME_SIZE;

typedef struct _DTBGOPTS {
  DWORD dwSize;
  DWORD dwFlags;
  RECT rcClip;
} DTBGOPTS, *PDTBGOPTS;

typedef struct _MARGINS {
  int cxLeftWidth;
  int cxRightWidth;
  int cyTopHeight;
  int cyBottomHeight;
} MARGINS, *PMARGINS;

#ifdef __cplusplus
extern "C" {
#endif

HTHEME WINAPI OpenThemeData(HWND, LPCWSTR);
HTHEME WINAPI OpenThemeDataEx(HWND, LPCWSTR, DWORD);
HTHEME WINAPI OpenThemeDataForDpi(HWND, LPCWSTR, UINT);
HRESULT WINAPI CloseThemeData(HTHEME);
HTHEME WINAPI GetWindowTheme(HWND);
HRESULT WINAPI SetWindowTheme(HWND, LPCWSTR, LPCWSTR);
HRESULT WINAPI EnableThemeDialogTexture(HWND, DWORD);
BOOL WINAPI IsThemeDialogTextureEnabled(HWND);
BOOL WINAPI IsThemeActive(void);
BOOL WINAPI IsAppThemed(void);
DWORD WINAPI GetThemeAppProperties(void);
void WINAPI SetThemeAppProperties(DWORD);
BOOL WINAPI IsThemePartDefined(HTHEME, int, int);
BOOL WINAPI IsThemeBackgroundPartiallyTransparent(HTHEME, int, int);
HRESULT WINAPI DrawThemeBackground(HTHEME, HDC, int, int, LPCRECT, LPCRECT);
HRESULT WINAPI DrawThemeBackgroundEx(HTHEME, HDC, int, int, LPCRECT, const DTBGOPTS*);
HRESULT WINAPI DrawThemeParentBackground(HWND, HDC, const RECT*);
HRESULT WINAPI DrawThemeParentBackgroundEx(HWND, HDC, DWORD, const RECT*);
HRESULT WINAPI DrawThemeText(HTHEME, HDC, int, int, LPCWSTR, int, DWORD, DWORD, LPCRECT);
HRESULT WINAPI DrawThemeEdge(HTHEME, HDC, int, int, LPCRECT, UINT, UINT, LPRECT);
HRESULT WINAPI DrawThemeIcon(HTHEME, HDC, int, int, LPCRECT, HIMAGELIST, int);
HRESULT WINAPI GetThemeBackgroundContentRect(HTHEME, HDC, int, int, LPCRECT, LPRECT);
HRESULT WINAPI GetThemeBackgroundExtent(HTHEME, HDC, int, int, LPCRECT, LPRECT);
HRESULT WINAPI GetThemePartSize(HTHEME, HDC, int, int, LPCRECT, THEME_SIZE, SIZE*);
HRESULT WINAPI GetThemeTextExtent(HTHEME, HDC, int, int, LPCWSTR, int, DWORD, LPCRECT, LPRECT);
HRESULT WINAPI GetThemeColor(HTHEME, int, int, int, COLORREF*);
HRESULT WINAPI GetThemeMetric(HTHEME, HDC, int, int, int, int*);
HRESULT WINAPI GetThemeMargins(HTHEME, HDC, int, int, int, LPCRECT, MARGINS*);
HRESULT WINAPI GetThemeInt(HTHEME, int, int, int, int*);
HRESULT WINAPI GetThemeBool(HTHEME, int, int, int, BOOL*);
HRESULT WINAPI GetThemeFont(HTHEME, HDC, int, int, int, LOGFONTW*);
HRESULT WINAPI GetThemeRect(HTHEME, int, int, int, LPRECT);
HRESULT WINAPI GetThemeString(HTHEME, int, int, int, LPWSTR, int);
COLORREF WINAPI GetThemeSysColor(HTHEME, int);
HBRUSH WINAPI GetThemeSysColorBrush(HTHEME, int);
int WINAPI GetThemeSysSize(HTHEME, int);
BOOL WINAPI GetThemeSysBool(HTHEME, int);
HRESULT WINAPI GetThemeSysFont(HTHEME, int, LOGFONTW*);
HRESULT WINAPI GetThemeSysInt(HTHEME, int, int*);
HRESULT WINAPI HitTestThemeBackground(HTHEME, HDC, int, int, DWORD, LPCRECT, HRGN, POINT, WORD*);
HRESULT WINAPI BufferedPaintInit(void);
HRESULT WINAPI BufferedPaintUnInit(void);

// The system values GetThemeSysFont and GetThemeSysBool take (from
// vssym32.h); with no theme they come from the system settings.
#define TMT_CAPTIONFONT 801
#define TMT_SMALLCAPTIONFONT 802
#define TMT_MENUFONT 803
#define TMT_STATUSFONT 804
#define TMT_MSGBOXFONT 805
#define TMT_ICONTITLEFONT 806
#define TMT_FLATMENUS 1001
#define TMT_SCROLLBAR 1601  // the first system color (COLOR_SCROLLBAR)

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // _WINPORT_UXTHEME_H
