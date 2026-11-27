// fpcore.cpp - the Feature Pack's base: AFX_GLOBAL_DATA, CSettingsStore,
// CWinAppEx and its managers, and the visual managers.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxglobals.h>
#include <afxsettingsstore.h>
#include <afxwinappex.h>
#include <afxvisualmanager.h>
#include <afxbutton.h>
#include <afxeditbrowsectrl.h>
#include <afxspinbuttonctrl.h>
#include <shlobj.h>

// ---------------------------------------------------------------------
// AFX_GLOBAL_DATA

AFX_GLOBAL_DATA* AFXAPI GetGlobalData() {
  static AFX_GLOBAL_DATA* s_pData = new AFX_GLOBAL_DATA;
  return s_pData;
}

COLORREF AFXAPI AfxBlendColor(COLORREF clrFrom, COLORREF clrTo, int nPercent) {
  if (nPercent < 0) nPercent = 0;
  if (nPercent > 100) nPercent = 100;
  int r = GetRValue(clrFrom) + (GetRValue(clrTo) - GetRValue(clrFrom)) * nPercent / 100;
  int g = GetGValue(clrFrom) + (GetGValue(clrTo) - GetGValue(clrFrom)) * nPercent / 100;
  int b = GetBValue(clrFrom) + (GetBValue(clrTo) - GetBValue(clrFrom)) * nPercent / 100;
  return RGB(r, g, b);
}

AFX_GLOBAL_DATA::AFX_GLOBAL_DATA() {
  m_bUseSystemFont = FALSE;
  m_bInSettingChange = FALSE;
  m_bIsBlackHighContrast = m_bIsWhiteHighContrast = FALSE;
  m_bUseBuiltIn32BitIcons = TRUE;
  m_bMenuAnimation = m_bMenuFadeEffect = FALSE;
  m_bIsRTL = FALSE;
  m_bEnableAccessibility = FALSE;
  m_dblRibbonImageScale = 1.0;
  m_nDragFrameThicknessFloat = 4;
  m_nDragFrameThicknessDock = 3;
  m_nAutoHideToolBarSpacing = 14;
  m_nAutoHideToolBarMargin = 4;
  OSVERSIONINFO osvi;
  memset(&osvi, 0, sizeof(osvi));
  osvi.dwOSVersionInfoSize = sizeof(osvi);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  ::GetVersionEx(&osvi);
#pragma clang diagnostic pop
  bIsWindowsVista = osvi.dwMajorVersion >= 6;
  bIsWindows7 = osvi.dwMajorVersion > 6 || (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion >= 1);
  bIsOSAlphaBlendingSupport = TRUE;
  bDisableAero = FALSE;
  m_hcurStretch = ::LoadCursor(NULL, IDC_SIZEWE);
  m_hcurStretchVert = ::LoadCursor(NULL, IDC_SIZENS);
  m_hcurHand = ::LoadCursor(NULL, IDC_HAND);
  m_hcurSizeAll = ::LoadCursor(NULL, IDC_SIZEALL);
  m_hcurMoveTab = ::LoadCursor(NULL, IDC_ARROW);
  m_hcurNoMoveTab = ::LoadCursor(NULL, IDC_NO);
  HDC hdc = ::GetDC(NULL);
  m_nBitsPerPixel = ::GetDeviceCaps(hdc, BITSPIXEL);
  ::ReleaseDC(NULL, hdc);
  UpdateSysColors();
  UpdateFonts();
}

AFX_GLOBAL_DATA::~AFX_GLOBAL_DATA() { CleanUp(); }

void AFX_GLOBAL_DATA::CleanUp() {}

void AFX_GLOBAL_DATA::UpdateSysColors() {
  clrBtnFace = clrBarFace = ::GetSysColor(COLOR_BTNFACE);
  clrBtnText = clrBarText = ::GetSysColor(COLOR_BTNTEXT);
  clrBtnShadow = clrBarShadow = ::GetSysColor(COLOR_BTNSHADOW);
  clrBtnHilite = clrBarHilite = ::GetSysColor(COLOR_BTNHIGHLIGHT);
  clrBtnDkShadow = clrBarDkShadow = ::GetSysColor(COLOR_3DDKSHADOW);
  clrBtnLight = clrBarLight = ::GetSysColor(COLOR_3DLIGHT);
  clrGrayedText = ::GetSysColor(COLOR_GRAYTEXT);
  clrHilite = ::GetSysColor(COLOR_HIGHLIGHT);
  clrTextHilite = ::GetSysColor(COLOR_HIGHLIGHTTEXT);
  clrHotText = ::GetSysColor(COLOR_HOTLIGHT);
  clrHotLinkNormalText = RGB(0, 0, 255);
  clrHotLinkHoveredText = RGB(0, 0, 255);
  clrHotLinkVisitedText = RGB(128, 0, 128);
  clrWindow = ::GetSysColor(COLOR_WINDOW);
  clrWindowText = ::GetSysColor(COLOR_WINDOWTEXT);
  clrWindowFrame = ::GetSysColor(COLOR_WINDOWFRAME);
  clrCaptionText = ::GetSysColor(COLOR_CAPTIONTEXT);
  clrMenuText = ::GetSysColor(COLOR_MENUTEXT);
  clrActiveCaption = ::GetSysColor(COLOR_ACTIVECAPTION);
  clrInactiveCaption = ::GetSysColor(COLOR_INACTIVECAPTION);
  clrInactiveCaptionText = ::GetSysColor(COLOR_INACTIVECAPTIONTEXT);
  clrActiveBorder = ::GetSysColor(COLOR_ACTIVEBORDER);
  clrInactiveBorder = ::GetSysColor(COLOR_INACTIVEBORDER);
  clrActiveCaptionGradient = ::GetSysColor(COLOR_GRADIENTACTIVECAPTION);
  clrInactiveCaptionGradient = ::GetSysColor(COLOR_GRADIENTINACTIVECAPTION);
  clrAppWorkspace = ::GetSysColor(COLOR_APPWORKSPACE);
  m_bIsWhiteHighContrast = m_bIsBlackHighContrast = FALSE;
  HIGHCONTRAST hc;
  memset(&hc, 0, sizeof(hc));
  hc.cbSize = sizeof(hc);
  if (::SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON)) {
    if (GetRValue(clrWindow) + GetGValue(clrWindow) + GetBValue(clrWindow) > 3 * 128)
      m_bIsWhiteHighContrast = TRUE;
    else
      m_bIsBlackHighContrast = TRUE;
  }

  struct {
    CBrush* pBrush;
    COLORREF clr;
  } brushes[] = {{&brBtnFace, clrBtnFace},
                 {&brBarFace, clrBarFace},
                 {&brHilite, clrHilite},
                 {&brLight, AfxBlendColor(clrBtnFace, clrBtnHilite, 50)},
                 {&brWindow, clrWindow},
                 {&brBlack, RGB(0, 0, 0)},
                 {&brActiveCaption, clrActiveCaption},
                 {&brInactiveCaption, clrInactiveCaption}};
  for (auto& b : brushes) {
    b.pBrush->DeleteObject();
    b.pBrush->CreateSolidBrush(b.clr);
  }
  penHilite.DeleteObject();
  penHilite.CreatePen(PS_SOLID, 1, clrHilite);
  penBarFace.DeleteObject();
  penBarFace.CreatePen(PS_SOLID, 1, clrBarFace);
  penBarShadow.DeleteObject();
  penBarShadow.CreatePen(PS_SOLID, 1, clrBarShadow);
}

void AFX_GLOBAL_DATA::UpdateFonts() { _CreateFonts(); }

void AFX_GLOBAL_DATA::_CreateFonts() {
  NONCLIENTMETRICS ncm;
  memset(&ncm, 0, sizeof(ncm));
  ncm.cbSize = sizeof(ncm);
  LOGFONT lf;
  if (::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
    lf = ncm.lfMenuFont;
  } else {
    memset(&lf, 0, sizeof(lf));
    ::GetObject(::GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf);
  }
  SetMenuFont(&lf, TRUE);
  LOGFONT lfTip = ncm.cbSize && ncm.lfStatusFont.lfFaceName[0] ? ncm.lfStatusFont : lf;
  fontTooltip.DeleteObject();
  fontTooltip.CreateFontIndirect(&lfTip);
  LOGFONT lfGui;
  memset(&lfGui, 0, sizeof(lfGui));
  ::GetObject(::GetStockObject(DEFAULT_GUI_FONT), sizeof(lfGui), &lfGui);
  lfGui.lfWeight = FW_BOLD;
  fontDefaultGUIBold.DeleteObject();
  fontDefaultGUIBold.CreateFontIndirect(&lfGui);
  lfGui.lfWeight = FW_NORMAL;
  lfGui.lfUnderline = TRUE;
  fontDefaultGUIUnderline.DeleteObject();
  fontDefaultGUIUnderline.CreateFontIndirect(&lfGui);
  LOGFONT lfSmall = lf;
  lfSmall.lfHeight = lf.lfHeight * 2 / 3;
  fontSmall.DeleteObject();
  fontSmall.CreateFontIndirect(&lfSmall);
  LOGFONT lfMarlett = lf;
  lfMarlett.lfCharSet = SYMBOL_CHARSET;
  lstrcpy(lfMarlett.lfFaceName, _T("Marlett"));
  fontMarlett.DeleteObject();
  fontMarlett.CreateFontIndirect(&lfMarlett);
}

BOOL AFX_GLOBAL_DATA::SetMenuFont(LPLOGFONT lpLogFont, BOOL bHorz) {
  if (lpLogFont == NULL) return FALSE;
  LOGFONT lf = *lpLogFont;
  if (bHorz) {
    fontRegular.DeleteObject();
    fontRegular.CreateFontIndirect(&lf);
    LOGFONT lfBold = lf;
    lfBold.lfWeight = FW_BOLD;
    fontBold.DeleteObject();
    fontBold.CreateFontIndirect(&lfBold);
    LOGFONT lfUnder = lf;
    lfUnder.lfUnderline = TRUE;
    fontUnderline.DeleteObject();
    fontUnderline.CreateFontIndirect(&lfUnder);
  }
  LOGFONT lfVert = lf;
  lfVert.lfOrientation = lfVert.lfEscapement = 2700;
  fontVert.DeleteObject();
  fontVert.CreateFontIndirect(&lfVert);
  lfVert.lfOrientation = lfVert.lfEscapement = 900;
  fontVertCaption.DeleteObject();
  fontVertCaption.CreateFontIndirect(&lfVert);
  // The text's size in the font.
  HDC hdc = ::GetDC(NULL);
  HGDIOBJ hOld = ::SelectObject(hdc, fontRegular.GetSafeHandle());
  TEXTMETRIC tm;
  ::GetTextMetrics(hdc, &tm);
  m_nTextHeightHorz = tm.tmHeight + 2;
  m_nTextWidthHorz = tm.tmAveCharWidth + 2;
  m_nTextHeightVert = m_nTextHeightHorz;
  m_nTextWidthVert = m_nTextWidthHorz;
  ::SelectObject(hdc, hOld);
  ::ReleaseDC(NULL, hdc);
  return TRUE;
}

void AFX_GLOBAL_DATA::OnSettingChange() {
  m_bInSettingChange = TRUE;
  UpdateSysColors();
  UpdateFonts();
  m_bInSettingChange = FALSE;
}

BOOL AFX_GLOBAL_DATA::DrawParentBackground(CWnd* pWnd, CDC* pDC, LPRECT lpRect) {
  if (pWnd == NULL || pDC == NULL) return FALSE;
  CWnd* pParent = pWnd->GetParent();
  if (pParent == NULL) return FALSE;
  CRect rect;
  if (lpRect != NULL)
    rect = *lpRect;
  else
    pWnd->GetClientRect(rect);
  CPoint pt(0, 0);
  pWnd->MapWindowPoints(pParent, &pt, 1);
  int nSaved = pDC->SaveDC();
  pDC->IntersectClipRect(rect);
  pDC->OffsetWindowOrg(pt.x, pt.y);
  BOOL bRes = (BOOL)pParent->SendMessage(WM_ERASEBKGND, (WPARAM)pDC->GetSafeHdc());
  pParent->SendMessage(WM_PRINTCLIENT, (WPARAM)pDC->GetSafeHdc(), PRF_CLIENT);
  pDC->RestoreDC(nSaved);
  return bRes;
}

void AFX_GLOBAL_DATA::DrawFocusRect(CDC* pDC, const CRect& rect, BOOL) {
  if (pDC != NULL) pDC->DrawFocusRect(rect);
}

// ---------------------------------------------------------------------
// CSettingsStore

IMPLEMENT_DYNCREATE(CSettingsStore, CObject)

CSettingsStore::CSettingsStore() : m_hKey(NULL), m_hRoot(HKEY_CURRENT_USER), m_bReadOnly(FALSE), m_bAdmin(FALSE) {}

CSettingsStore::CSettingsStore(BOOL bAdmin, BOOL bReadOnly)
    : m_hKey(NULL), m_hRoot(bAdmin ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER), m_bReadOnly(bReadOnly),
      m_bAdmin(bAdmin) {}

CSettingsStore::~CSettingsStore() { Close(); }

static CString _AfxTrimKeyPath(LPCTSTR pszPath) {
  CString str = pszPath != NULL ? pszPath : _T("");
  str.TrimRight(_T('\\'));
  str.TrimLeft(_T('\\'));
  return str;
}

BOOL CSettingsStore::CreateKey(LPCTSTR pszPath) {
  if (m_bReadOnly) return Open(pszPath);
  Close();
  m_strPath = _AfxTrimKeyPath(pszPath);
  return ::RegCreateKeyEx(m_hRoot, m_strPath, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &m_hKey, NULL) == ERROR_SUCCESS;
}

BOOL CSettingsStore::Open(LPCTSTR pszPath) {
  Close();
  m_strPath = _AfxTrimKeyPath(pszPath);
  return ::RegOpenKeyEx(m_hRoot, m_strPath, 0, m_bReadOnly ? KEY_READ : KEY_ALL_ACCESS, &m_hKey) == ERROR_SUCCESS;
}

void CSettingsStore::Close() {
  if (m_hKey != NULL) ::RegCloseKey(m_hKey);
  m_hKey = NULL;
}

BOOL CSettingsStore::DeleteValue(LPCTSTR pszValue) {
  if (m_hKey == NULL || m_bReadOnly) return FALSE;
  return ::RegDeleteValue(m_hKey, pszValue) == ERROR_SUCCESS;
}

// A key and its subkeys.
static LONG _AfxDeleteKeyTree(HKEY hParent, LPCTSTR pszKey) {
  HKEY hKey;
  LONG l = ::RegOpenKeyEx(hParent, pszKey, 0, KEY_ALL_ACCESS, &hKey);
  if (l != ERROR_SUCCESS) return l;
  TCHAR szSub[256];
  while (::RegEnumKey(hKey, 0, szSub, 256) == ERROR_SUCCESS)
    if (_AfxDeleteKeyTree(hKey, szSub) != ERROR_SUCCESS) break;
  ::RegCloseKey(hKey);
  return ::RegDeleteKey(hParent, pszKey);
}

BOOL CSettingsStore::DeleteKey(LPCTSTR pszPath, BOOL bAdmin) {
  if (m_bReadOnly) return FALSE;
  CString strPath = _AfxTrimKeyPath(pszPath);
  if (strPath.IsEmpty()) return FALSE;
  return _AfxDeleteKeyTree(bAdmin ? HKEY_LOCAL_MACHINE : m_hRoot, strPath) == ERROR_SUCCESS;
}

BOOL CSettingsStore::Write(LPCTSTR pszKey, int iVal) { return Write(pszKey, (DWORD)iVal); }

BOOL CSettingsStore::Write(LPCTSTR pszKey, DWORD dwVal) {
  if (m_hKey == NULL || m_bReadOnly) return FALSE;
  return ::RegSetValueEx(m_hKey, pszKey, 0, REG_DWORD, (const BYTE*)&dwVal, sizeof(dwVal)) == ERROR_SUCCESS;
}

BOOL CSettingsStore::Write(LPCTSTR pszKey, LPCTSTR pszVal) {
  if (m_hKey == NULL || m_bReadOnly || pszVal == NULL) return FALSE;
  return ::RegSetValueEx(m_hKey, pszKey, 0, REG_SZ, (const BYTE*)pszVal,
                         (DWORD)((lstrlen(pszVal) + 1) * sizeof(TCHAR))) == ERROR_SUCCESS;
}

BOOL CSettingsStore::Write(LPCTSTR pszKey, LPBYTE pData, UINT nBytes) {
  if (m_hKey == NULL || m_bReadOnly) return FALSE;
  return ::RegSetValueEx(m_hKey, pszKey, 0, REG_BINARY, pData, nBytes) == ERROR_SUCCESS;
}

template <class F>
BOOL CSettingsStore::_WriteArchived(LPCTSTR pszKey, F fn) {
  if (m_hKey == NULL || m_bReadOnly) return FALSE;
  BOOL bRes = FALSE;
  try {
    CMemFile file;
    {
      CArchive ar(&file, CArchive::store);
      fn(ar);
      ar.Flush();
    }
    UINT nBytes = (UINT)file.GetLength();
    BYTE* p = file.Detach();
    bRes = Write(pszKey, p, nBytes);
    free(p);
  } catch (CException* e) {
    e->Delete();
  }
  return bRes;
}

template <class F>
BOOL CSettingsStore::_ReadArchived(LPCTSTR pszKey, F fn) {
  BYTE* pData = NULL;
  UINT nBytes = 0;
  if (!Read(pszKey, &pData, &nBytes)) return FALSE;
  BOOL bRes = FALSE;
  try {
    CMemFile file(pData, nBytes);
    CArchive ar(&file, CArchive::load);
    fn(ar);
    bRes = TRUE;
  } catch (CException* e) {
    e->Delete();
  }
  delete[] pData;
  return bRes;
}

BOOL CSettingsStore::Write(LPCTSTR pszKey, const CRect& rect) {
  return _WriteArchived(pszKey, [&](CArchive& ar) { ar << rect; });
}
BOOL CSettingsStore::Write(LPCTSTR pszKey, CPoint& point) {
  return _WriteArchived(pszKey, [&](CArchive& ar) { ar << point; });
}
BOOL CSettingsStore::Write(LPCTSTR pszKey, CObject& obj) {
  return _WriteArchived(pszKey, [&](CArchive& ar) { obj.Serialize(ar); });
}
BOOL CSettingsStore::Write(LPCTSTR pszKey, CObject* pObj) {
  return _WriteArchived(pszKey, [&](CArchive& ar) { ar << pObj; });
}
BOOL CSettingsStore::Write(LPCTSTR pszKey, CStringList& list) {
  return _WriteArchived(pszKey, [&](CArchive& ar) { list.Serialize(ar); });
}
BOOL CSettingsStore::Write(LPCTSTR pszKey, CObList& list) {
  return _WriteArchived(pszKey, [&](CArchive& ar) { list.Serialize(ar); });
}
BOOL CSettingsStore::Write(LPCTSTR pszKey, CStringArray& array) {
  return _WriteArchived(pszKey, [&](CArchive& ar) { array.Serialize(ar); });
}
BOOL CSettingsStore::Write(LPCTSTR pszKey, CByteArray& array) {
  return _WriteArchived(pszKey, [&](CArchive& ar) { array.Serialize(ar); });
}
BOOL CSettingsStore::Write(LPCTSTR pszKey, CWordArray& array) {
  return _WriteArchived(pszKey, [&](CArchive& ar) { array.Serialize(ar); });
}
BOOL CSettingsStore::Write(LPCTSTR pszKey, CDWordArray& array) {
  return _WriteArchived(pszKey, [&](CArchive& ar) { array.Serialize(ar); });
}
BOOL CSettingsStore::Write(LPCTSTR pszKey, CArray<CRect, CRect>& array) {
  return _WriteArchived(pszKey, [&](CArchive& ar) { array.Serialize(ar); });
}
BOOL CSettingsStore::Write(LPCTSTR pszKey, CArray<CPoint, CPoint>& array) {
  return _WriteArchived(pszKey, [&](CArchive& ar) { array.Serialize(ar); });
}
BOOL CSettingsStore::Write(LPCTSTR pszKey, CList<DWORD, DWORD>& list) {
  return _WriteArchived(pszKey, [&](CArchive& ar) { list.Serialize(ar); });
}
BOOL CSettingsStore::Write(LPCTSTR pszKey, CMap<UINT, UINT, UINT, UINT>& map) {
  return _WriteArchived(pszKey, [&](CArchive& ar) { map.Serialize(ar); });
}
#ifndef _WINPORT
BOOL CSettingsStore::Write(LPCTSTR pszKey, CMap<UINT, UINT, DWORD, DWORD>& map) {
  return _WriteArchived(pszKey, [&](CArchive& ar) { map.Serialize(ar); });
}
#endif

BOOL CSettingsStore::Read(LPCTSTR pszKey, int& iVal) {
  DWORD dw;
  if (!Read(pszKey, dw)) return FALSE;
  iVal = (int)dw;
  return TRUE;
}

BOOL CSettingsStore::Read(LPCTSTR pszKey, DWORD& dwVal) {
  if (m_hKey == NULL) return FALSE;
  DWORD dw = 0, cb = sizeof(dw), type = 0;
  if (::RegQueryValueEx(m_hKey, pszKey, NULL, &type, (BYTE*)&dw, &cb) != ERROR_SUCCESS || type != REG_DWORD)
    return FALSE;
  dwVal = dw;
  return TRUE;
}

BOOL CSettingsStore::Read(LPCTSTR pszKey, CString& sVal) {
  if (m_hKey == NULL) return FALSE;
  DWORD cb = 0, type = 0;
  if (::RegQueryValueEx(m_hKey, pszKey, NULL, &type, NULL, &cb) != ERROR_SUCCESS ||
      (type != REG_SZ && type != REG_EXPAND_SZ))
    return FALSE;
  CString str;
  LPTSTR p = str.GetBuffer(cb / sizeof(TCHAR) + 1);
  LONG l = ::RegQueryValueEx(m_hKey, pszKey, NULL, &type, (BYTE*)p, &cb);
  p[cb / sizeof(TCHAR)] = 0;
  str.ReleaseBuffer();
  if (l != ERROR_SUCCESS) return FALSE;
  sVal = str;
  return TRUE;
}

BOOL CSettingsStore::Read(LPCTSTR pszKey, BYTE** ppData, UINT* pBytes) {
  if (ppData == NULL || pBytes == NULL) return FALSE;
  *ppData = NULL;
  *pBytes = 0;
  if (m_hKey == NULL) return FALSE;
  DWORD cb = 0, type = 0;
  if (::RegQueryValueEx(m_hKey, pszKey, NULL, &type, NULL, &cb) != ERROR_SUCCESS || type != REG_BINARY)
    return FALSE;
  *ppData = new BYTE[cb ? cb : 1];
  if (::RegQueryValueEx(m_hKey, pszKey, NULL, &type, *ppData, &cb) != ERROR_SUCCESS) {
    delete[] * ppData;
    *ppData = NULL;
    return FALSE;
  }
  *pBytes = cb;
  return TRUE;
}

BOOL CSettingsStore::Read(LPCTSTR pszKey, CRect& rect) {
  return _ReadArchived(pszKey, [&](CArchive& ar) { ar >> rect; });
}
BOOL CSettingsStore::Read(LPCTSTR pszKey, CPoint& point) {
  return _ReadArchived(pszKey, [&](CArchive& ar) { ar >> point; });
}
BOOL CSettingsStore::Read(LPCTSTR pszKey, CObject& obj) {
  return _ReadArchived(pszKey, [&](CArchive& ar) { obj.Serialize(ar); });
}
BOOL CSettingsStore::Read(LPCTSTR pszKey, CObject*& pObj) {
  return _ReadArchived(pszKey, [&](CArchive& ar) { ar >> pObj; });
}
BOOL CSettingsStore::Read(LPCTSTR pszKey, CStringList& list) {
  list.RemoveAll();
  return _ReadArchived(pszKey, [&](CArchive& ar) { list.Serialize(ar); });
}
BOOL CSettingsStore::Read(LPCTSTR pszKey, CObList& list) {
  return _ReadArchived(pszKey, [&](CArchive& ar) { list.Serialize(ar); });
}
BOOL CSettingsStore::Read(LPCTSTR pszKey, CStringArray& array) {
  return _ReadArchived(pszKey, [&](CArchive& ar) { array.Serialize(ar); });
}
BOOL CSettingsStore::Read(LPCTSTR pszKey, CByteArray& array) {
  return _ReadArchived(pszKey, [&](CArchive& ar) { array.Serialize(ar); });
}
BOOL CSettingsStore::Read(LPCTSTR pszKey, CWordArray& array) {
  return _ReadArchived(pszKey, [&](CArchive& ar) { array.Serialize(ar); });
}
BOOL CSettingsStore::Read(LPCTSTR pszKey, CDWordArray& array) {
  return _ReadArchived(pszKey, [&](CArchive& ar) { array.Serialize(ar); });
}
BOOL CSettingsStore::Read(LPCTSTR pszKey, CArray<CRect, CRect>& array) {
  return _ReadArchived(pszKey, [&](CArchive& ar) { array.Serialize(ar); });
}
BOOL CSettingsStore::Read(LPCTSTR pszKey, CArray<CPoint, CPoint>& array) {
  return _ReadArchived(pszKey, [&](CArchive& ar) { array.Serialize(ar); });
}
BOOL CSettingsStore::Read(LPCTSTR pszKey, CList<DWORD, DWORD>& list) {
  list.RemoveAll();
  return _ReadArchived(pszKey, [&](CArchive& ar) { list.Serialize(ar); });
}
BOOL CSettingsStore::Read(LPCTSTR pszKey, CMap<UINT, UINT, UINT, UINT>& map) {
  map.RemoveAll();
  return _ReadArchived(pszKey, [&](CArchive& ar) { map.Serialize(ar); });
}
#ifndef _WINPORT
BOOL CSettingsStore::Read(LPCTSTR pszKey, CMap<UINT, UINT, DWORD, DWORD>& map) {
  map.RemoveAll();
  return _ReadArchived(pszKey, [&](CArchive& ar) { map.Serialize(ar); });
}
#endif

static CRuntimeClass* _afxSettingsStoreClass = NULL;

BOOL AFXAPI CSettingsStoreSP::SetRuntimeClass(CRuntimeClass* pRTI) {
  if (pRTI != NULL && !pRTI->IsDerivedFrom(RUNTIME_CLASS(CSettingsStore))) return FALSE;
  _afxSettingsStoreClass = pRTI;
  return TRUE;
}

CSettingsStore& CSettingsStoreSP::Create(BOOL bAdmin, BOOL bReadOnly) {
  delete m_pRegistry;
  m_pRegistry = NULL;
  if (_afxSettingsStoreClass != NULL) {
    CSettingsStore* p = DYNAMIC_DOWNCAST(CSettingsStore, _afxSettingsStoreClass->CreateObject());
    if (p != NULL) {
      p->m_bAdmin = bAdmin;
      p->m_hRoot = bAdmin ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
      p->m_bReadOnly = bReadOnly;
      m_pRegistry = p;
    }
  }
  if (m_pRegistry == NULL) m_pRegistry = new CSettingsStore(bAdmin, bReadOnly);
  return *m_pRegistry;
}

// ---------------------------------------------------------------------
// CWinAppEx

IMPLEMENT_DYNAMIC(CWinAppEx, CWinApp)

CContextMenuManager* afxContextMenuManager = NULL;
CKeyboardManager* afxKeyboardManager = NULL;
CMouseManager* afxMouseManager = NULL;
CShellManager* afxShellManager = NULL;
CTooltipManager* afxTooltipManager = NULL;

CString AFXAPI AFXGetRegPath(LPCTSTR lpszPostFix, LPCTSTR lpszProfileName) {
  CWinApp* pApp = AfxGetApp();
  CString str = _T("Software\\");
  if (pApp != NULL && pApp->m_pszRegistryKey != NULL && *pApp->m_pszRegistryKey) {
    str += pApp->m_pszRegistryKey;
    str += _T("\\");
  }
  if (lpszProfileName != NULL && *lpszProfileName) {
    str += lpszProfileName;
    str += _T("\\");
  } else if (pApp != NULL && pApp->m_pszProfileName != NULL) {
    str += pApp->m_pszProfileName;
    str += _T("\\");
  }
  if (lpszPostFix != NULL && *lpszPostFix) {
    str += lpszPostFix;
    str += _T("\\");
  }
  return str;
}

CWinAppEx::CWinAppEx(BOOL bResourceSmartUpdate)
    : m_bSaveState(TRUE), m_bForceImageReset(FALSE), m_nAppLook(0), m_bHiColorIcons(FALSE),
      m_strRegSection(_T("Workspace")), m_bResourceSmartUpdate(bResourceSmartUpdate),
      m_bLoadWindowPlacement(TRUE), m_bKeyboardManagerAutocreated(FALSE),
      m_bContextMenuManagerAutocreated(FALSE), m_bMouseManagerAutocreated(FALSE),
      m_bShellManagerAutocreated(FALSE), m_bTooltipManagerAutocreated(FALSE), m_iSavedVersionMajor(-1),
      m_iSavedVersionMinor(-1) {}

CWinAppEx::~CWinAppEx() {}

int CWinAppEx::ExitInstance() {
  if (m_bContextMenuManagerAutocreated) {
    delete afxContextMenuManager;
    afxContextMenuManager = NULL;
  }
  if (m_bKeyboardManagerAutocreated) {
    delete afxKeyboardManager;
    afxKeyboardManager = NULL;
  }
  if (m_bMouseManagerAutocreated) {
    delete afxMouseManager;
    afxMouseManager = NULL;
  }
  if (m_bShellManagerAutocreated) {
    delete afxShellManager;
    afxShellManager = NULL;
  }
  if (m_bTooltipManagerAutocreated) {
    delete afxTooltipManager;
    afxTooltipManager = NULL;
  }
  CMFCVisualManager::DestroyInstance(TRUE);
  return CWinApp::ExitInstance();
}

BOOL CWinAppEx::InitMouseManager() {
  if (afxMouseManager == NULL) {
    afxMouseManager = new CMouseManager;
    m_bMouseManagerAutocreated = TRUE;
  }
  return TRUE;
}
BOOL CWinAppEx::InitContextMenuManager() {
  if (afxContextMenuManager == NULL) {
    afxContextMenuManager = new CContextMenuManager;
    m_bContextMenuManagerAutocreated = TRUE;
  }
  return TRUE;
}
BOOL CWinAppEx::InitKeyboardManager() {
  if (afxKeyboardManager == NULL) {
    afxKeyboardManager = new CKeyboardManager;
    m_bKeyboardManagerAutocreated = TRUE;
  }
  return TRUE;
}
BOOL CWinAppEx::InitShellManager() {
  if (afxShellManager == NULL) {
    afxShellManager = new CShellManager;
    m_bShellManagerAutocreated = TRUE;
  }
  return TRUE;
}
BOOL CWinAppEx::InitTooltipManager() {
  if (afxTooltipManager == NULL) {
    afxTooltipManager = new CTooltipManager;
    m_bTooltipManagerAutocreated = TRUE;
  }
  return TRUE;
}
CMouseManager* CWinAppEx::GetMouseManager() {
  InitMouseManager();
  return afxMouseManager;
}
CContextMenuManager* CWinAppEx::GetContextMenuManager() {
  InitContextMenuManager();
  return afxContextMenuManager;
}
CKeyboardManager* CWinAppEx::GetKeyboardManager() {
  InitKeyboardManager();
  return afxKeyboardManager;
}
CShellManager* CWinAppEx::GetShellManager() {
  InitShellManager();
  return afxShellManager;
}
CTooltipManager* CWinAppEx::GetTooltipManager() {
  InitTooltipManager();
  return afxTooltipManager;
}

LPCTSTR CWinAppEx::SetRegistryBase(LPCTSTR lpszSectionName) {
  m_strRegSection = lpszSectionName != NULL && *lpszSectionName ? lpszSectionName : _T("Workspace");
  return m_strRegSection;
}

CString CWinAppEx::GetRegSectionPath(LPCTSTR szSectionAdd) {
  CString str = AFXGetRegPath(m_strRegSection);
  if (szSectionAdd != NULL && *szSectionAdd) {
    str += szSectionAdd;
    str += _T("\\");
  }
  return str;
}

CString CWinAppEx::_SectionPath(LPCTSTR lpszSubSection) { return GetRegSectionPath(lpszSubSection); }

int CWinAppEx::GetSectionInt(LPCTSTR lpszSubSection, LPCTSTR lpszEntry, int nDefault) {
  CSettingsStoreSP sp;
  CSettingsStore& reg = sp.Create(FALSE, TRUE);
  int n = nDefault;
  if (reg.Open(_SectionPath(lpszSubSection))) reg.Read(lpszEntry, n);
  return n;
}
CString CWinAppEx::GetSectionString(LPCTSTR lpszSubSection, LPCTSTR lpszEntry, LPCTSTR lpszDefault) {
  CSettingsStoreSP sp;
  CSettingsStore& reg = sp.Create(FALSE, TRUE);
  CString str = lpszDefault != NULL ? lpszDefault : _T("");
  if (reg.Open(_SectionPath(lpszSubSection))) reg.Read(lpszEntry, str);
  return str;
}
BOOL CWinAppEx::GetSectionBinary(LPCTSTR lpszSubSection, LPCTSTR lpszEntry, LPBYTE* ppData, UINT* pBytes) {
  CSettingsStoreSP sp;
  CSettingsStore& reg = sp.Create(FALSE, TRUE);
  return reg.Open(_SectionPath(lpszSubSection)) && reg.Read(lpszEntry, ppData, pBytes);
}
BOOL CWinAppEx::GetSectionObject(LPCTSTR lpszSubSection, LPCTSTR lpszEntry, CObject& obj) {
  CSettingsStoreSP sp;
  CSettingsStore& reg = sp.Create(FALSE, TRUE);
  return reg.Open(_SectionPath(lpszSubSection)) && reg.Read(lpszEntry, obj);
}
BOOL CWinAppEx::WriteSectionInt(LPCTSTR lpszSubSection, LPCTSTR lpszEntry, int nValue) {
  CSettingsStoreSP sp;
  CSettingsStore& reg = sp.Create(FALSE, FALSE);
  return reg.CreateKey(_SectionPath(lpszSubSection)) && reg.Write(lpszEntry, nValue);
}
BOOL CWinAppEx::WriteSectionString(LPCTSTR lpszSubSection, LPCTSTR lpszEntry, LPCTSTR lpszValue) {
  CSettingsStoreSP sp;
  CSettingsStore& reg = sp.Create(FALSE, FALSE);
  return reg.CreateKey(_SectionPath(lpszSubSection)) && reg.Write(lpszEntry, lpszValue);
}
BOOL CWinAppEx::WriteSectionBinary(LPCTSTR lpszSubSection, LPCTSTR lpszEntry, LPBYTE pData, UINT nBytes) {
  CSettingsStoreSP sp;
  CSettingsStore& reg = sp.Create(FALSE, FALSE);
  return reg.CreateKey(_SectionPath(lpszSubSection)) && reg.Write(lpszEntry, pData, nBytes);
}
BOOL CWinAppEx::WriteSectionObject(LPCTSTR lpszSubSection, LPCTSTR lpszEntry, CObject& obj) {
  CSettingsStoreSP sp;
  CSettingsStore& reg = sp.Create(FALSE, FALSE);
  return reg.CreateKey(_SectionPath(lpszSubSection)) && reg.Write(lpszEntry, obj);
}

int CWinAppEx::GetInt(LPCTSTR lpszEntry, int nDefault) { return GetSectionInt(_T(""), lpszEntry, nDefault); }
CString CWinAppEx::GetString(LPCTSTR lpszEntry, LPCTSTR lpszDefault) {
  return GetSectionString(_T(""), lpszEntry, lpszDefault);
}
BOOL CWinAppEx::GetBinary(LPCTSTR lpszEntry, LPBYTE* ppData, UINT* pBytes) {
  return GetSectionBinary(_T(""), lpszEntry, ppData, pBytes);
}
BOOL CWinAppEx::GetObject(LPCTSTR lpszEntry, CObject& obj) { return GetSectionObject(_T(""), lpszEntry, obj); }
BOOL CWinAppEx::WriteInt(LPCTSTR lpszEntry, int nValue) { return WriteSectionInt(_T(""), lpszEntry, nValue); }
BOOL CWinAppEx::WriteString(LPCTSTR lpszEntry, LPCTSTR lpszValue) {
  return WriteSectionString(_T(""), lpszEntry, lpszValue);
}
BOOL CWinAppEx::WriteBinary(LPCTSTR lpszEntry, LPBYTE pData, UINT nBytes) {
  return WriteSectionBinary(_T(""), lpszEntry, pData, nBytes);
}
BOOL CWinAppEx::WriteObject(LPCTSTR lpszEntry, CObject& obj) {
  return WriteSectionObject(_T(""), lpszEntry, obj);
}

BOOL CWinAppEx::IsStateExists(LPCTSTR lpszSectionName) {
  CSettingsStoreSP sp;
  CSettingsStore& reg = sp.Create(FALSE, TRUE);
  return reg.Open(lpszSectionName != NULL ? CString(lpszSectionName) : GetRegSectionPath());
}

BOOL CWinAppEx::DeleteState(LPCTSTR lpszSectionName) { return CleanState(lpszSectionName); }

BOOL CWinAppEx::CleanState(LPCTSTR lpszSectionName) {
  CSettingsStoreSP sp;
  CSettingsStore& reg = sp.Create(FALSE, FALSE);
  return reg.DeleteKey(lpszSectionName != NULL ? CString(lpszSectionName) : GetRegSectionPath());
}

int CWinAppEx::GetDataVersion() const {
  if (m_iSavedVersionMajor < 0 || m_iSavedVersionMinor < 0) return 0xFFFFFFFF;
  return m_iSavedVersionMajor * 100 + m_iSavedVersionMinor;
}

static const TCHAR _afxWindowPlacement[] = _T("WindowPlacement");

BOOL CWinAppEx::LoadWindowPlacementFrom(LPCTSTR lpszSection, CRect& rect, int& nFlags, int& nShowCmd) {
  CSettingsStoreSP sp;
  CSettingsStore& reg = sp.Create(FALSE, TRUE);
  if (!reg.Open(lpszSection)) return FALSE;
  return reg.Read(_T("MainWindowRect"), rect) && reg.Read(_T("Flags"), nFlags) && reg.Read(_T("ShowCmd"), nShowCmd);
}

BOOL CWinAppEx::LoadWindowPlacement(CRect& rectNormalPosition, int& nFlags, int& nShowCmd) {
  return LoadWindowPlacementFrom(GetRegSectionPath(_afxWindowPlacement), rectNormalPosition, nFlags, nShowCmd);
}

BOOL CWinAppEx::StoreWindowPlacement(const CRect& rectNormalPosition, int nFlags, int nShowCmd) {
  CSettingsStoreSP sp;
  CSettingsStore& reg = sp.Create(FALSE, FALSE);
  if (!reg.CreateKey(GetRegSectionPath(_afxWindowPlacement))) return FALSE;
  return reg.Write(_T("MainWindowRect"), rectNormalPosition) && reg.Write(_T("Flags"), nFlags) &&
         reg.Write(_T("ShowCmd"), nShowCmd);
}

BOOL CWinAppEx::ReloadWindowPlacement(CFrameWnd* pFrame) {
  if (pFrame == NULL || pFrame->m_hWnd == NULL) return FALSE;
  CRect rect;
  int nFlags = 0, nShowCmd = SW_SHOWNORMAL;
  if (!LoadWindowPlacement(rect, nFlags, nShowCmd)) return FALSE;
  WINDOWPLACEMENT wp;
  memset(&wp, 0, sizeof(wp));
  wp.length = sizeof(wp);
  pFrame->GetWindowPlacement(&wp);
  wp.rcNormalPosition = rect;
  wp.flags = (UINT)nFlags;
  wp.showCmd = (UINT)nShowCmd;
  return pFrame->SetWindowPlacement(&wp);
}

BOOL CWinAppEx::LoadState(CFrameWnd* pFrame, LPCTSTR lpszSectionName) {
  if (lpszSectionName != NULL) m_strRegSection = lpszSectionName;
  m_iSavedVersionMajor = GetInt(_T("MajorVersion"), -1);
  m_iSavedVersionMinor = GetInt(_T("MinorVersion"), -1);
  PreLoadState();
  if (afxKeyboardManager != NULL) afxKeyboardManager->LoadState(GetRegSectionPath(), pFrame);
  if (afxContextMenuManager != NULL) afxContextMenuManager->LoadState(GetRegSectionPath());
  if (pFrame != NULL && m_bLoadWindowPlacement) ReloadWindowPlacement(pFrame);
  LoadCustomState();
  return TRUE;
}

BOOL CWinAppEx::SaveState(CFrameWnd* pFrame, LPCTSTR lpszSectionName) {
  if (!m_bSaveState) return FALSE;
  if (lpszSectionName != NULL) m_strRegSection = lpszSectionName;
  PreSaveState();
  WriteInt(_T("MajorVersion"), _MFC_VER >> 8);
  WriteInt(_T("MinorVersion"), _MFC_VER & 0xFF);
  if (afxKeyboardManager != NULL) afxKeyboardManager->SaveState(GetRegSectionPath(), pFrame);
  if (afxContextMenuManager != NULL) afxContextMenuManager->SaveState(GetRegSectionPath());
  if (pFrame != NULL && pFrame->m_hWnd != NULL) {
    WINDOWPLACEMENT wp;
    memset(&wp, 0, sizeof(wp));
    wp.length = sizeof(wp);
    if (pFrame->GetWindowPlacement(&wp))
      StoreWindowPlacement(CRect(wp.rcNormalPosition), (int)wp.flags,
                           pFrame->IsIconic() ? SW_SHOWNORMAL : (int)wp.showCmd);
  }
  SaveCustomState();
  return TRUE;
}

void CWinAppEx::OnClosingMainFrame(CFrameImpl*) {}

BOOL CWinAppEx::ShowPopupMenu(UINT uiMenuResId, const CPoint& point, CWnd* pWnd) {
  if (afxContextMenuManager == NULL) return FALSE;
  return afxContextMenuManager->ShowPopupMenu(uiMenuResId, point.x, point.y, pWnd);
}

// ---------------------------------------------------------------------
// CContextMenuManager

CContextMenuManager::CContextMenuManager() : m_bDontCloseActiveMenu(FALSE) {}

CContextMenuManager::~CContextMenuManager() {
  POSITION pos = m_Menus.GetStartPosition();
  while (pos != NULL) {
    UINT id;
    HMENU h;
    m_Menus.GetNextAssoc(pos, id, h);
    ::DestroyMenu(h);
  }
}

BOOL CContextMenuManager::AddMenu(UINT uiMenuNameResId, UINT uiMenuResId) {
  CString strName;
  if (!strName.LoadString(uiMenuNameResId)) return FALSE;
  return AddMenu(strName, uiMenuResId);
}

BOOL CContextMenuManager::AddMenu(LPCTSTR lpszName, UINT uiMenuResId) {
  HMENU h;
  if (m_Menus.Lookup(uiMenuResId, h)) {
    if (lpszName != NULL) m_MenuNames.SetAt(lpszName, h);
    return TRUE;
  }
  h = ::LoadMenu(AfxFindResourceHandle(MAKEINTRESOURCE(uiMenuResId), RT_MENU), MAKEINTRESOURCE(uiMenuResId));
  if (h == NULL) return FALSE;
  m_Menus.SetAt(uiMenuResId, h);
  if (lpszName != NULL) m_MenuNames.SetAt(lpszName, h);
  return TRUE;
}

HMENU CContextMenuManager::GetMenuById(UINT nMenuResId) const {
  HMENU h = NULL;
  m_Menus.Lookup(nMenuResId, h);
  return h;
}

HMENU CContextMenuManager::GetMenuByName(LPCTSTR lpszName, UINT* puiOrigResID) const {
  HMENU h = NULL;
  if (lpszName == NULL || !m_MenuNames.Lookup(lpszName, h)) return NULL;
  if (puiOrigResID != NULL) {
    *puiOrigResID = 0;
    POSITION pos = m_Menus.GetStartPosition();
    while (pos != NULL) {
      UINT id;
      HMENU hm;
      m_Menus.GetNextAssoc(pos, id, hm);
      if (hm == h) *puiOrigResID = id;
    }
  }
  return h;
}

void CContextMenuManager::GetMenuNames(CStringList& listOfNames) const {
  listOfNames.RemoveAll();
  POSITION pos = m_MenuNames.GetStartPosition();
  while (pos != NULL) {
    CString str;
    HMENU h;
    m_MenuNames.GetNextAssoc(pos, str, h);
    listOfNames.AddTail(str);
  }
}

BOOL CContextMenuManager::ShowPopupMenu(UINT uiMenuResId, int x, int y, CWnd* pWndOwner, BOOL bOwnMessage,
                                        BOOL bRightAlign) {
  HMENU h = GetMenuById(uiMenuResId);
  if (h == NULL) {
    if (!AddMenu((LPCTSTR)NULL, uiMenuResId)) return FALSE;
    h = GetMenuById(uiMenuResId);
  }
  HMENU hPopup = ::GetSubMenu(h, 0);
  if (hPopup == NULL) return FALSE;
  return ShowPopupMenu(hPopup, x, y, pWndOwner, bOwnMessage, FALSE, bRightAlign);
}

BOOL CContextMenuManager::ShowPopupMenu(HMENU hmenuPopup, int x, int y, CWnd* pWndOwner, BOOL bOwnMessage,
                                        BOOL bAutoDestroy, BOOL bRightAlign) {
  if (hmenuPopup == NULL) return FALSE;
  CWnd* pOwner = pWndOwner != NULL ? pWndOwner : AfxGetMainWnd();
  if (x == -1 && y == -1) {
    CPoint pt;
    ::GetCursorPos(&pt);
    x = pt.x;
    y = pt.y;
  }
  UINT uFlags = TPM_LEFTBUTTON | TPM_RIGHTBUTTON | (bRightAlign ? TPM_RIGHTALIGN : TPM_LEFTALIGN);
  BOOL bRes;
  if (bOwnMessage) {
    bRes = ::TrackPopupMenu(hmenuPopup, uFlags, x, y, 0, pOwner != NULL ? pOwner->GetSafeHwnd() : NULL, NULL);
  } else {
    // The command goes to the main window's routing, as a menu command.
    HWND hOwner = pOwner != NULL ? pOwner->GetSafeHwnd() : NULL;
    UINT nCmd = (UINT)::TrackPopupMenu(hmenuPopup, uFlags | TPM_RETURNCMD | TPM_NONOTIFY, x, y, 0, hOwner, NULL);
    if (nCmd != 0 && hOwner != NULL) ::PostMessage(hOwner, WM_COMMAND, nCmd, 0);
    bRes = TRUE;
  }
  if (bAutoDestroy) ::DestroyMenu(hmenuPopup);
  return bRes;
}

UINT CContextMenuManager::TrackPopupMenu(HMENU hmenuPopup, int x, int y, CWnd* pWndOwner, BOOL bRightAlign) {
  if (hmenuPopup == NULL) return 0;
  HWND hOwner = pWndOwner != NULL ? pWndOwner->GetSafeHwnd() : AfxGetMainWnd()->GetSafeHwnd();
  UINT uFlags = TPM_LEFTBUTTON | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY |
                (bRightAlign ? TPM_RIGHTALIGN : TPM_LEFTALIGN);
  return (UINT)::TrackPopupMenu(hmenuPopup, uFlags, x, y, 0, hOwner, NULL);
}

BOOL CContextMenuManager::ResetState() { return TRUE; }

// ---------------------------------------------------------------------
// CKeyboardManager

BOOL CKeyboardManager::m_bAllAccelerators = FALSE;
CString CKeyboardManager::m_strDelimiter = _T("; ");

CKeyboardManager::CKeyboardManager() {}
CKeyboardManager::~CKeyboardManager() {}
BOOL CKeyboardManager::LoadState(LPCTSTR, CFrameWnd*) { return TRUE; }
BOOL CKeyboardManager::SaveState(LPCTSTR, CFrameWnd*) { return TRUE; }
void CKeyboardManager::ResetAll() {}

void AFXAPI CKeyboardManager::ShowAllAccelerators(BOOL bShowAll, LPCTSTR lpszDelimiter) {
  m_bAllAccelerators = bShowAll;
  if (lpszDelimiter != NULL) m_strDelimiter = lpszDelimiter;
}

CString AFXAPI CKeyboardManager::GetKeyName(const ACCEL& accel) {
  CString str;
  if (accel.fVirt & FCONTROL) str += _T("Ctrl+");
  if (accel.fVirt & FSHIFT) str += _T("Shift+");
  if (accel.fVirt & FALT) str += _T("Alt+");
  if (!(accel.fVirt & FVIRTKEY)) {
    str += (TCHAR)accel.key;
    return str;
  }
  UINT nScan = ::MapVirtualKey(accel.key, 0);
  switch (accel.key) {
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
      nScan |= 0x100;  // the extended keys
  }
  TCHAR szName[64];
  if (::GetKeyNameText((LONG)(nScan << 16), szName, 64) > 0) {
    str += szName;
  } else if (accel.key >= VK_F1 && accel.key <= VK_F24) {
    CString strF;
    strF.Format(_T("F%d"), accel.key - VK_F1 + 1);
    str += strF;
  } else {
    str += (TCHAR)accel.key;
  }
  return str;
}

BOOL AFXAPI CKeyboardManager::FindDefaultAccelerator(UINT uiCmd, CString& str, CFrameWnd* pWndFrame, BOOL) {
  str.Empty();
  HACCEL hAccel = pWndFrame != NULL ? pWndFrame->m_hAccelTable : NULL;
  if (hAccel == NULL) {
    CFrameWnd* pMain = DYNAMIC_DOWNCAST(CFrameWnd, AfxGetMainWnd());
    if (pMain != NULL) hAccel = pMain->m_hAccelTable;
  }
  if (hAccel == NULL) return FALSE;
  int n = ::CopyAcceleratorTable(hAccel, NULL, 0);
  if (n <= 0) return FALSE;
  ACCEL* p = new ACCEL[n];
  ::CopyAcceleratorTable(hAccel, p, n);
  for (int i = 0; i < n; i++) {
    if (p[i].cmd != uiCmd) continue;
    if (!str.IsEmpty()) {
      if (!m_bAllAccelerators) break;
      str += m_strDelimiter;
    }
    str += GetKeyName(p[i]);
  }
  delete[] p;
  return !str.IsEmpty();
}

BOOL AFXAPI CKeyboardManager::IsKeyPrintable(const UINT nChar) {
  return nChar >= 0x20 && nChar != 0x7F && !(nChar >= VK_F1 && nChar <= VK_F24 && nChar < 0x80);
}

UINT AFXAPI CKeyboardManager::TranslateCharToUpper(const UINT nChar) {
  if (nChar >= 'a' && nChar <= 'z') return nChar - 'a' + 'A';
  return (UINT)(UINT_PTR)::CharUpper((LPTSTR)(UINT_PTR)nChar);
}

BOOL AFXAPI CKeyboardManager::IsKeyHandled(WORD nKey, BYTE fVirt, CFrameWnd* pWndFrame, BOOL) {
  HACCEL hAccel = pWndFrame != NULL ? pWndFrame->m_hAccelTable : NULL;
  if (hAccel == NULL) return FALSE;
  int n = ::CopyAcceleratorTable(hAccel, NULL, 0);
  ACCEL* p = new ACCEL[n > 0 ? n : 1];
  ::CopyAcceleratorTable(hAccel, p, n);
  BOOL b = FALSE;
  for (int i = 0; i < n; i++)
    if (p[i].key == nKey && (p[i].fVirt & ~FNOINVERT) == (fVirt & ~FNOINVERT)) b = TRUE;
  delete[] p;
  return b;
}

// ---------------------------------------------------------------------
// CMouseManager

BOOL CMouseManager::AddView(int iViewId, UINT uiViewNameResId, UINT uiIconId) {
  CString str;
  if (!str.LoadString(uiViewNameResId)) return FALSE;
  return AddView(iViewId, str, uiIconId);
}
BOOL CMouseManager::AddView(int iViewId, LPCTSTR lpszViewName, UINT) {
  int n;
  if (lpszViewName == NULL || m_ViewsNames.Lookup(lpszViewName, n)) return FALSE;
  m_ViewsNames.SetAt(lpszViewName, iViewId);
  return TRUE;
}
UINT CMouseManager::GetViewDblClickCommand(int iId) const {
  UINT n = 0;
  m_ViewsToCommands.Lookup(iId, n);
  return n;
}
void CMouseManager::SetCommandForDblClk(int iViewId, UINT uiCmd) {
  if (uiCmd == 0)
    m_ViewsToCommands.RemoveKey(iViewId);
  else
    m_ViewsToCommands.SetAt(iViewId, uiCmd);
}
int CMouseManager::GetViewIdByName(LPCTSTR lpszName) const {
  int n = -1;
  if (lpszName != NULL) m_ViewsNames.Lookup(lpszName, n);
  return n;
}
void CMouseManager::GetViewNames(CStringList& listOfNames) const {
  listOfNames.RemoveAll();
  POSITION pos = m_ViewsNames.GetStartPosition();
  while (pos != NULL) {
    CString str;
    int n;
    m_ViewsNames.GetNextAssoc(pos, str, n);
    listOfNames.AddTail(str);
  }
}

// ---------------------------------------------------------------------
// CShellManager

CShellManager::CShellManager() : m_pMalloc(NULL) { ::SHGetMalloc(&m_pMalloc); }

CShellManager::~CShellManager() {
  if (m_pMalloc != NULL) m_pMalloc->Release();
}

int CALLBACK CShellManager::BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM, LPARAM lpData) {
  CShellManager* pThis = (CShellManager*)lpData;
  if (uMsg == BFFM_INITIALIZED && pThis != NULL && !pThis->m_strInitialPath.IsEmpty())
    ::SendMessage(hwnd, BFFM_SETSELECTION, TRUE, (LPARAM)(LPCTSTR)pThis->m_strInitialPath);
  return 0;
}

BOOL CShellManager::BrowseForFolder(CString& strOutFolder, CWnd* pWndParent, LPCTSTR lplszInitialFolder,
                                    LPCTSTR lpszTitle, UINT ulFlags, LPINT piFolderImage) {
  TCHAR szDisplay[MAX_PATH] = {0};
  BROWSEINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.hwndOwner = pWndParent != NULL ? pWndParent->GetSafeHwnd() : NULL;
  bi.pszDisplayName = szDisplay;
  bi.lpszTitle = lpszTitle;
  bi.ulFlags = ulFlags;
  bi.lpfn = BrowseCallbackProc;
  bi.lParam = (LPARAM)this;
  m_strInitialPath = lplszInitialFolder != NULL ? lplszInitialFolder : _T("");
  LPITEMIDLIST pidl = ::SHBrowseForFolder(&bi);
  if (pidl == NULL) return FALSE;
  TCHAR szPath[MAX_PATH] = {0};
  BOOL bRes = ::SHGetPathFromIDList(pidl, szPath);
  if (bRes) strOutFolder = szPath;
  if (piFolderImage != NULL) *piFolderImage = bi.iImage;
  FreeItem(pidl);
  return bRes;
}

LPITEMIDLIST CShellManager::CreateItem(UINT cbSize) {
  if (m_pMalloc == NULL) return NULL;
  BYTE* p = (BYTE*)m_pMalloc->Alloc(cbSize);
  if (p != NULL) memset(p, 0, cbSize);
  return (LPITEMIDLIST)p;
}

void CShellManager::FreeItem(LPITEMIDLIST pidl) {
  if (pidl != NULL && m_pMalloc != NULL) m_pMalloc->Free(pidl);
}

UINT CShellManager::GetItemCount(LPCITEMIDLIST pidl) {
  UINT n = 0;
  for (; pidl != NULL && pidl->mkid.cb != 0; pidl = (LPCITEMIDLIST)((const BYTE*)pidl + pidl->mkid.cb)) n++;
  return n;
}

// The size with the terminator.
UINT CShellManager::GetItemSize(LPCITEMIDLIST pidl) {
  if (pidl == NULL) return 0;
  UINT cb = sizeof(USHORT);
  for (; pidl->mkid.cb != 0; pidl = (LPCITEMIDLIST)((const BYTE*)pidl + pidl->mkid.cb)) cb += pidl->mkid.cb;
  return cb;
}

LPITEMIDLIST CShellManager::CopyItem(LPCITEMIDLIST pidlSource) {
  if (pidlSource == NULL) return NULL;
  UINT cb = GetItemSize(pidlSource);
  LPITEMIDLIST p = CreateItem(cb);
  if (p != NULL) memcpy(p, pidlSource, cb);
  return p;
}

LPITEMIDLIST CShellManager::GetNextItem(LPCITEMIDLIST pidl) {
  if (pidl == NULL || pidl->mkid.cb == 0) return NULL;
  LPITEMIDLIST p = (LPITEMIDLIST)((const BYTE*)pidl + pidl->mkid.cb);
  return p->mkid.cb != 0 ? p : NULL;
}

int CShellManager::GetParentItem(LPCITEMIDLIST lpidl, LPITEMIDLIST& lpidlParent) {
  lpidlParent = NULL;
  UINT n = GetItemCount(lpidl);
  if (n == 0) return -1;
  // All but the last item.
  UINT cb = 0;
  LPCITEMIDLIST p = lpidl;
  for (UINT i = 0; i + 1 < n; i++) {
    cb += p->mkid.cb;
    p = (LPCITEMIDLIST)((const BYTE*)p + p->mkid.cb);
  }
  lpidlParent = CreateItem(cb + sizeof(USHORT));
  if (lpidlParent == NULL) return -1;
  memcpy(lpidlParent, lpidl, cb);
  return (int)(n - 1);
}

LPITEMIDLIST CShellManager::ConcatenateItem(LPCITEMIDLIST pidl1, LPCITEMIDLIST pidl2) {
  UINT cb1 = pidl1 != NULL ? GetItemSize(pidl1) - sizeof(USHORT) : 0;
  UINT cb2 = pidl2 != NULL ? GetItemSize(pidl2) : sizeof(USHORT);
  LPITEMIDLIST p = CreateItem(cb1 + cb2);
  if (p == NULL) return NULL;
  if (cb1) memcpy(p, pidl1, cb1);
  if (pidl2 != NULL) memcpy((BYTE*)p + cb1, pidl2, cb2);
  return p;
}

HRESULT CShellManager::ItemFromPath(LPCTSTR lpszPath, LPITEMIDLIST& pidl) {
  pidl = NULL;
  if (lpszPath == NULL) return E_INVALIDARG;
  IShellFolder* pDesktop = NULL;
  HRESULT hr = ::SHGetDesktopFolder(&pDesktop);
  if (FAILED(hr)) return hr;
  CStringW strPath(lpszPath);
  hr = pDesktop->ParseDisplayName(NULL, NULL, strPath.GetBuffer(), NULL, &pidl, NULL);
  strPath.ReleaseBuffer();
  pDesktop->Release();
  return hr;
}

// ---------------------------------------------------------------------
// CTooltipManager

CTooltipManager::CTooltipManager() { memset(m_rtc, 0, sizeof(m_rtc)); }
CTooltipManager::~CTooltipManager() {}

void CTooltipManager::SetTooltipParams(UINT nTypes, CRuntimeClass* pRTC, void*) {
  for (int i = 0; i < 16; i++)
    if (nTypes & (1u << i)) m_rtc[i] = pRTC;
}

BOOL AFXAPI CTooltipManager::CreateToolTip(CToolTipCtrl*& pToolTip, CWnd* pWndParent, UINT nType) {
  pToolTip = NULL;
  if (pWndParent == NULL) return FALSE;
  CRuntimeClass* pRTC = NULL;
  if (afxTooltipManager != NULL)
    for (int i = 0; i < 16; i++)
      if ((nType & (1u << i)) && afxTooltipManager->m_rtc[i] != NULL) pRTC = afxTooltipManager->m_rtc[i];
  if (pRTC != NULL) pToolTip = DYNAMIC_DOWNCAST(CToolTipCtrl, pRTC->CreateObject());
  if (pToolTip == NULL) pToolTip = new CToolTipCtrl;
  if (!pToolTip->Create(pWndParent, TTS_ALWAYSTIP)) {
    delete pToolTip;
    pToolTip = NULL;
    return FALSE;
  }
  pToolTip->SetMaxTipWidth(640);
  return TRUE;
}

void AFXAPI CTooltipManager::DeleteToolTip(CToolTipCtrl*& pToolTip) {
  if (pToolTip == NULL) return;
  if (pToolTip->GetSafeHwnd() != NULL) pToolTip->DestroyWindow();
  delete pToolTip;
  pToolTip = NULL;
}

void CTooltipManager::SetTooltipText(TOOLINFO* pTI, CToolTipCtrl* pToolTip, UINT, const CString strText,
                                     LPCTSTR) {
  if (pTI == NULL || pToolTip == NULL) return;
  pTI->lpszText = (LPTSTR)(LPCTSTR)strText;
  pToolTip->SetToolInfo(pTI);
}

// ---------------------------------------------------------------------
// The visual managers

IMPLEMENT_DYNCREATE(CMFCVisualManager, CObject)
IMPLEMENT_DYNCREATE(CMFCVisualManagerOfficeXP, CMFCVisualManager)
IMPLEMENT_DYNCREATE(CMFCVisualManagerWindows, CMFCVisualManagerOfficeXP)
IMPLEMENT_DYNCREATE(CMFCVisualManagerOffice2003, CMFCVisualManagerOfficeXP)
IMPLEMENT_DYNCREATE(CMFCVisualManagerVS2005, CMFCVisualManagerOffice2003)
IMPLEMENT_DYNCREATE(CMFCVisualManagerOffice2007, CMFCVisualManagerOffice2003)
IMPLEMENT_DYNCREATE(CMFCVisualManagerVS2008, CMFCVisualManagerVS2005)
IMPLEMENT_DYNCREATE(CMFCVisualManagerWindows7, CMFCVisualManagerWindows)

CRuntimeClass* CMFCVisualManager::m_pRTIDefault = NULL;
CMFCVisualManager* CMFCVisualManager::m_pVisManager = NULL;
BOOL CMFCVisualManagerWindows::m_b3DTabsXPTheme = FALSE;
BOOL CMFCVisualManagerOffice2003::m_bUseGlobalTheme = TRUE;
BOOL CMFCVisualManagerOffice2003::m_bStatusBarOfficeXPLook = TRUE;
BOOL CMFCVisualManagerOffice2003::m_bDefaultWinXPColors = TRUE;
BOOL CMFCVisualManagerVS2005::m_bRoundedAutohideButtons = FALSE;
CMFCVisualManagerOffice2007::Style CMFCVisualManagerOffice2007::m_Style =
    CMFCVisualManagerOffice2007::Office2007_LunaBlue;
COLORREF CMFCVisualManagerOffice2007::m_clrCustom = (COLORREF)-1;

CMFCVisualManager::CMFCVisualManager(BOOL bTemporary)
    : m_bTemporary(bTemporary), m_bAutoDestroy(FALSE), m_bFlat(FALSE), m_bMenuFlatLook(FALSE),
      m_bShadowHighlightedImage(FALSE), m_bEmbossDisabledImage(TRUE), m_bFadeInactiveImage(FALSE) {
  OnUpdateSystemColors();
}

CMFCVisualManager::~CMFCVisualManager() {
  if (m_pVisManager == this) m_pVisManager = NULL;
}

CMFCVisualManager* AFXAPI CMFCVisualManager::GetInstance() {
  if (m_pVisManager == NULL) {
    CRuntimeClass* pRTI = m_pRTIDefault != NULL ? m_pRTIDefault : RUNTIME_CLASS(CMFCVisualManager);
    CMFCVisualManager* p = DYNAMIC_DOWNCAST(CMFCVisualManager, pRTI->CreateObject());
    if (p == NULL) p = new CMFCVisualManager;
    p->m_bAutoDestroy = TRUE;
    p->OnUpdateSystemColors();
    m_pVisManager = p;
  }
  return m_pVisManager;
}

void AFXAPI CMFCVisualManager::SetDefaultManager(CRuntimeClass* pRTI) {
  if (pRTI != NULL && !pRTI->IsDerivedFrom(RUNTIME_CLASS(CMFCVisualManager))) return;
  m_pRTIDefault = pRTI;
  DestroyInstance(TRUE);
  GetInstance();
  RedrawAll();
}

void AFXAPI CMFCVisualManager::DestroyInstance(BOOL bAutoDestroyOnly) {
  if (m_pVisManager == NULL) return;
  if (bAutoDestroyOnly && !m_pVisManager->m_bAutoDestroy) return;
  CMFCVisualManager* p = m_pVisManager;
  m_pVisManager = NULL;
  delete p;
}

static BOOL CALLBACK _AfxRedrawProc(HWND hwnd, LPARAM) {
  ::RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
  return TRUE;
}

void AFXAPI CMFCVisualManager::RedrawAll() { ::EnumThreadWindows(::GetCurrentThreadId(), _AfxRedrawProc, 0); }

void CMFCVisualManager::OnUpdateSystemColors() {
  AFX_GLOBAL_DATA& g = afxGlobalData;
  m_clrFace = g.clrBtnFace;
  m_clrHighlight = g.clrBtnFace;
  m_clrHighlightBorder = g.clrBtnShadow;
  m_clrPressed = g.clrBtnShadow;
  m_clrPressedBorder = g.clrBtnDkShadow;
}

void CMFCVisualManager::_FillFramed(CDC* pDC, const CRect& rect, COLORREF clrFill, COLORREF clrFrame) {
  pDC->FillSolidRect(rect, clrFill);
  CBrush br(clrFrame);
  pDC->FrameRect(rect, &br);
}

BOOL CMFCVisualManager::OnDrawPushButton(CDC* pDC, CRect rect, CMFCButton* pButton, COLORREF& clrText) {
  if (pButton == NULL) return FALSE;
  BOOL bPressed = pButton->IsPressed() || (pButton->IsChecked() && pButton->m_bHighlightChecked);
  BOOL bHot = pButton->IsHighlighted();
  if (m_bFlat || pButton->m_nFlatStyle != CMFCButton::BUTTONSTYLE_3D) {
    if (bPressed)
      _FillFramed(pDC, rect, m_clrPressed, m_clrPressedBorder);
    else if (bHot && pButton->m_nFlatStyle != CMFCButton::BUTTONSTYLE_NOBORDERS)
      _FillFramed(pDC, rect, m_clrHighlight, m_clrHighlightBorder);
    else
      return FALSE;
  } else {
    pDC->DrawFrameControl(rect, DFC_BUTTON, DFCS_BUTTONPUSH | (bPressed ? DFCS_PUSHED : 0));
  }
  clrText = pButton->IsWindowEnabled() ? afxGlobalData.clrBtnText : afxGlobalData.clrGrayedText;
  return TRUE;
}

void CMFCVisualManager::OnDrawButtonBorder(CDC* pDC, CRect& rect, CMFCButton* pButton, UINT uiState) {
  if (pButton == NULL) return;
  BOOL bPressed = (uiState & ODS_SELECTED) != 0 || pButton->IsPressed();
  switch (pButton->m_nFlatStyle) {
    case CMFCButton::BUTTONSTYLE_NOBORDERS:
      break;
    case CMFCButton::BUTTONSTYLE_FLAT:
      if (bPressed)
        pDC->Draw3dRect(rect, afxGlobalData.clrBtnShadow, afxGlobalData.clrBtnHilite);
      else if (pButton->IsHighlighted())
        pDC->Draw3dRect(rect, afxGlobalData.clrBtnHilite, afxGlobalData.clrBtnShadow);
      break;
    case CMFCButton::BUTTONSTYLE_SEMIFLAT:
      if (bPressed)
        pDC->Draw3dRect(rect, afxGlobalData.clrBtnDkShadow, afxGlobalData.clrBtnHilite);
      else
        pDC->Draw3dRect(rect, afxGlobalData.clrBtnHilite, afxGlobalData.clrBtnDkShadow);
      break;
    default:
      if (bPressed) {
        pDC->Draw3dRect(rect, afxGlobalData.clrBtnDkShadow, afxGlobalData.clrBtnHilite);
      } else {
        pDC->Draw3dRect(rect, afxGlobalData.clrBtnHilite, afxGlobalData.clrBtnDkShadow);
        CRect r = rect;
        r.DeflateRect(1, 1);
        pDC->Draw3dRect(r, afxGlobalData.clrBtnLight, afxGlobalData.clrBtnShadow);
      }
  }
  rect.DeflateRect(2, 2);
}

COLORREF CMFCVisualManager::GetButtonTextColor(CMFCButton* pButton, UINT) {
  return pButton != NULL && !pButton->IsWindowEnabled() ? afxGlobalData.clrGrayedText : afxGlobalData.clrBtnText;
}

void CMFCVisualManager::OnDrawCheckBox(CDC* pDC, CRect rect, BOOL bHighlighted, BOOL bChecked, BOOL bEnabled) {
  OnDrawCheckBoxEx(pDC, rect, bChecked ? 1 : 0, bHighlighted, FALSE, bEnabled);
}

void CMFCVisualManager::OnDrawCheckBoxEx(CDC* pDC, CRect rect, int nState, BOOL bHighlighted, BOOL bPressed,
                                         BOOL bEnabled) {
  UINT uState = DFCS_BUTTONCHECK;
  if (nState == 1) uState |= DFCS_CHECKED;
  if (nState == 2) uState = DFCS_BUTTON3STATE | DFCS_CHECKED;
  if (bPressed) uState |= DFCS_PUSHED;
  if (!bEnabled) uState |= DFCS_INACTIVE;
  if (bHighlighted) uState |= DFCS_HOT;
  pDC->DrawFrameControl(rect, DFC_BUTTON, uState);
}

void CMFCVisualManager::OnDrawRadioButton(CDC* pDC, CRect rect, BOOL bOn, BOOL bHighlighted, BOOL bPressed,
                                          BOOL bEnabled) {
  UINT uState = DFCS_BUTTONRADIO | (bOn ? DFCS_CHECKED : 0) | (bPressed ? DFCS_PUSHED : 0) |
                (bEnabled ? 0 : DFCS_INACTIVE) | (bHighlighted ? DFCS_HOT : 0);
  pDC->DrawFrameControl(rect, DFC_BUTTON, uState);
}

void CMFCVisualManager::OnDrawMenuArrow(CDC* pDC, CRect rect, BOOL bEnabled) {
  CPoint ptCenter = rect.CenterPoint();
  int n = rect.Width() / 4 > 4 ? 4 : (rect.Width() / 4 < 2 ? 2 : rect.Width() / 4);
  POINT pts[3] = {{ptCenter.x - n, ptCenter.y - n / 2},
                  {ptCenter.x + n + 1, ptCenter.y - n / 2},
                  {ptCenter.x, ptCenter.y + n / 2 + 1}};
  COLORREF clr = bEnabled ? afxGlobalData.clrBtnText : afxGlobalData.clrGrayedText;
  CBrush br(clr);
  CPen pen(PS_SOLID, 1, clr);
  CBrush* pOldBr = pDC->SelectObject(&br);
  CPen* pOldPen = pDC->SelectObject(&pen);
  pDC->Polygon(pts, 3);
  pDC->SelectObject(pOldBr);
  pDC->SelectObject(pOldPen);
}

BOOL CMFCVisualManager::OnDrawBrowseButton(CDC* pDC, CRect rect, CMFCEditBrowseCtrl*, AFX_BUTTON_STATE state,
                                           COLORREF& clrText) {
  if (m_bFlat) {
    COLORREF clrFill = state == ButtonsIsPressed ? m_clrPressed
                       : state == ButtonsIsHighlighted ? m_clrHighlight
                                                       : m_clrFace;
    COLORREF clrFrame = state == ButtonsIsRegular ? afxGlobalData.clrBtnShadow : m_clrHighlightBorder;
    _FillFramed(pDC, rect, clrFill, clrFrame);
  } else {
    pDC->DrawFrameControl(rect, DFC_BUTTON, DFCS_BUTTONPUSH | (state == ButtonsIsPressed ? DFCS_PUSHED : 0));
  }
  clrText = afxGlobalData.clrBtnText;
  return TRUE;
}

void CMFCVisualManager::OnDrawSpinButtons(CDC* pDC, CRect rectSpin, int nState, BOOL bOrientation,
                                          CMFCSpinButtonCtrl*) {
  // bOrientation TRUE: horizontal (left and right arrows).
  CRect rect[2] = {rectSpin, rectSpin};
  if (bOrientation) {
    rect[0].right = rectSpin.left + rectSpin.Width() / 2;
    rect[1].left = rect[0].right;
  } else {
    rect[0].bottom = rectSpin.top + rectSpin.Height() / 2;
    rect[1].top = rect[0].bottom;
  }
  for (int i = 0; i < 2; i++) {
    BOOL bPressed = (nState & (i == 0 ? AFX_SPIN_PRESSEDUP : AFX_SPIN_PRESSEDDOWN)) != 0;
    BOOL bHot = (nState & (i == 0 ? AFX_SPIN_HIGHLIGHTEDUP : AFX_SPIN_HIGHLIGHTEDDOWN)) != 0;
    UINT uKind = bOrientation ? (i == 0 ? DFCS_SCROLLLEFT : DFCS_SCROLLRIGHT) : (i == 0 ? DFCS_SCROLLUP : DFCS_SCROLLDOWN);
    if (m_bFlat) {
      _FillFramed(pDC, rect[i], bPressed ? m_clrPressed : bHot ? m_clrHighlight : m_clrFace,
                  bPressed || bHot ? m_clrHighlightBorder : afxGlobalData.clrBtnShadow);
      CRect rArrow = rect[i];
      rArrow.DeflateRect(1, 1);
      pDC->DrawFrameControl(rArrow, DFC_SCROLL, uKind | DFCS_FLAT | DFCS_TRANSPARENT |
                                                    ((nState & AFX_SPIN_DISABLED) ? DFCS_INACTIVE : 0));
    } else {
      pDC->DrawFrameControl(rect[i], DFC_SCROLL, uKind | (bPressed ? DFCS_PUSHED : 0) |
                                                     ((nState & AFX_SPIN_DISABLED) ? DFCS_INACTIVE : 0));
    }
  }
}

// ----- The looks' colors.

CMFCVisualManagerOfficeXP::CMFCVisualManagerOfficeXP(BOOL bTemporary) : CMFCVisualManager(bTemporary) {
  m_bFlat = TRUE;
  m_bMenuFlatLook = TRUE;
  CMFCVisualManagerOfficeXP::OnUpdateSystemColors();
}

void CMFCVisualManagerOfficeXP::OnUpdateSystemColors() {
  AFX_GLOBAL_DATA& g = afxGlobalData;
  m_clrFace = AfxBlendColor(g.clrBtnFace, g.clrWindow, 40);
  m_clrHighlight = AfxBlendColor(g.clrHilite, g.clrWindow, 70);
  m_clrHighlightBorder = g.clrHilite;
  m_clrPressed = AfxBlendColor(g.clrHilite, g.clrWindow, 50);
  m_clrPressedBorder = g.clrHilite;
}

CMFCVisualManagerWindows::CMFCVisualManagerWindows(BOOL bTemporary) : CMFCVisualManagerOfficeXP(bTemporary) {
  CMFCVisualManagerWindows::OnUpdateSystemColors();
}

void CMFCVisualManagerWindows::OnUpdateSystemColors() {
  CMFCVisualManagerOfficeXP::OnUpdateSystemColors();
  m_clrFace = afxGlobalData.clrBtnFace;
}

CMFCVisualManagerOffice2003::CMFCVisualManagerOffice2003() { CMFCVisualManagerOffice2003::OnUpdateSystemColors(); }

void CMFCVisualManagerOffice2003::OnUpdateSystemColors() {
  CMFCVisualManagerOfficeXP::OnUpdateSystemColors();
  m_clrHighlight = RGB(255, 238, 194);
  m_clrHighlightBorder = RGB(0, 0, 128);
  m_clrPressed = RGB(254, 128, 62);
  m_clrPressedBorder = RGB(0, 0, 128);
  m_clrFace = RGB(196, 218, 250);
}

CMFCVisualManagerVS2005::CMFCVisualManagerVS2005() { CMFCVisualManagerVS2005::OnUpdateSystemColors(); }

void CMFCVisualManagerVS2005::OnUpdateSystemColors() {
  CMFCVisualManagerOffice2003::OnUpdateSystemColors();
  m_clrFace = AfxBlendColor(afxGlobalData.clrBtnFace, afxGlobalData.clrWindow, 30);
  m_clrHighlight = AfxBlendColor(afxGlobalData.clrHilite, afxGlobalData.clrWindow, 70);
  m_clrHighlightBorder = afxGlobalData.clrHilite;
  m_clrPressed = AfxBlendColor(afxGlobalData.clrHilite, afxGlobalData.clrWindow, 50);
  m_clrPressedBorder = afxGlobalData.clrHilite;
}

CMFCVisualManagerOffice2007::CMFCVisualManagerOffice2007() { CMFCVisualManagerOffice2007::OnUpdateSystemColors(); }

BOOL AFXAPI CMFCVisualManagerOffice2007::SetStyle(Style style, LPCTSTR) {
  m_Style = style;
  if (m_pVisManager != NULL) {
    m_pVisManager->OnUpdateSystemColors();
    RedrawAll();
  }
  return TRUE;
}

void CMFCVisualManagerOffice2007::OnUpdateSystemColors() {
  CMFCVisualManagerOffice2003::OnUpdateSystemColors();
  static const COLORREF faces[] = {RGB(191, 219, 255), RGB(83, 83, 83), RGB(196, 202, 217), RGB(208, 212, 221)};
  m_clrFace = m_clrCustom != (COLORREF)-1 ? m_clrCustom : faces[m_Style];
  m_clrHighlight = RGB(255, 231, 162);
  m_clrHighlightBorder = RGB(255, 189, 105);
  m_clrPressed = RGB(251, 140, 60);
  m_clrPressedBorder = RGB(251, 140, 60);
}

CMFCVisualManagerVS2008::CMFCVisualManagerVS2008() { CMFCVisualManagerVS2008::OnUpdateSystemColors(); }

void CMFCVisualManagerVS2008::OnUpdateSystemColors() {
  CMFCVisualManagerVS2005::OnUpdateSystemColors();
  m_clrFace = RGB(233, 236, 250);
}

CMFCVisualManagerWindows7::CMFCVisualManagerWindows7() { CMFCVisualManagerWindows7::OnUpdateSystemColors(); }

void CMFCVisualManagerWindows7::OnUpdateSystemColors() {
  CMFCVisualManagerWindows::OnUpdateSystemColors();
  m_clrFace = RGB(245, 246, 247);
  m_clrHighlight = RGB(229, 243, 251);
  m_clrHighlightBorder = RGB(112, 192, 231);
  m_clrPressed = RGB(204, 232, 255);
  m_clrPressedBorder = RGB(86, 157, 229);
}
