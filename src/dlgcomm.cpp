// dlgcomm.cpp - common dialogs: file, color, font, print, page setup,
// find and replace.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxdlgs.h>

IMPLEMENT_DYNAMIC(CCommonDialog, CDialog)

// The dialogs end themselves; these only exist for the virtual calls.
void CCommonDialog::OnOK() {
  if (!UpdateData(TRUE)) return;
  CDialog::OnOK();
}

void CCommonDialog::OnCancel() { CDialog::OnCancel(); }

// Runs a modal common dialog the way CDialog::DoModal runs a dialog:
// the owner is the safe owner and any top-level window is re-enabled.
template <typename F>
static INT_PTR _AfxRunCommonDialog(CWnd* pParentWnd, HWND* phWndOwner, F run) {
  HWND hWndTop = NULL;
  *phWndOwner = _AfxGetSafeOwner(pParentWnd->GetSafeHwnd(), &hWndTop);
  INT_PTR nResult = run() ? IDOK : IDCANCEL;
  if (hWndTop != NULL) ::EnableWindow(hWndTop, TRUE);
  return nResult;
}

// ---------------------------------------------------------------------
// CFileDialog

IMPLEMENT_DYNAMIC(CFileDialog, CCommonDialog)

CFileDialog::CFileDialog(BOOL bOpenFileDialog, LPCTSTR lpszDefExt, LPCTSTR lpszFileName,
                         DWORD dwFlags, LPCTSTR lpszFilter, CWnd* pParentWnd, DWORD dwSize,
                         BOOL bVistaStyle)
    : CCommonDialog(pParentWnd) {
  (void)bVistaStyle;
  memset(&m_ofn, 0, sizeof(m_ofn));
  // The Windows 95 / NT 4 size works everywhere.
  m_ofn.lStructSize = dwSize != 0 ? dwSize : OPENFILENAME_SIZE_VERSION_400;
  m_szFileName[0] = 0;
  m_szFileTitle[0] = 0;
  m_bOpenFileDialog = bOpenFileDialog;
  m_ofn.lpstrFile = m_szFileName;
  m_ofn.nMaxFile = MAX_PATH;
  m_ofn.lpstrFileTitle = m_szFileTitle;
  m_ofn.nMaxFileTitle = 64;
  m_ofn.Flags = dwFlags | OFN_EXPLORER;
  if (lpszDefExt != NULL) {
    m_strDefExt = lpszDefExt;
    m_ofn.lpstrDefExt = m_strDefExt;
  }
  if (lpszFileName != NULL) lstrcpyn(m_szFileName, lpszFileName, MAX_PATH);

  // MFC filters separate entries with '|'; the dialog wants NULs.
  if (lpszFilter != NULL) {
    m_strFilter = lpszFilter;
    int nLen = m_strFilter.GetLength();
    LPTSTR pch = m_strFilter.GetBuffer(nLen + 2);
    for (int i = 0; i < nLen; i++)
      if (pch[i] == _T('|')) pch[i] = 0;
    pch[nLen] = 0;
    pch[nLen + 1] = 0;
    m_strFilter.ReleaseBuffer(nLen + 1);
    m_ofn.lpstrFilter = m_strFilter;
  }
}

CFileDialog::~CFileDialog() {}

INT_PTR CFileDialog::DoModal() {
  m_ofn.hInstance = AfxGetResourceHandle();
  return _AfxRunCommonDialog(m_pParentWnd, &m_ofn.hwndOwner, [this]() {
    return m_bOpenFileDialog ? ::GetOpenFileName(&m_ofn) : ::GetSaveFileName(&m_ofn);
  });
}

CString CFileDialog::GetPathName() const {
  // With multiple selection the buffer holds the folder, then the names.
  if ((m_ofn.Flags & OFN_ALLOWMULTISELECT) && m_ofn.lpstrFile[lstrlen(m_ofn.lpstrFile) + 1]) {
    POSITION pos = GetStartPosition();
    return GetNextPathName(pos);
  }
  return m_ofn.lpstrFile;
}

CString CFileDialog::GetFileName() const {
  CString strPath = GetPathName();
  int i = strPath.ReverseFind(_T('\\'));
  int j = strPath.ReverseFind(_T('/'));
  if (j > i) i = j;
  return i >= 0 ? strPath.Mid(i + 1) : strPath;
}

CString CFileDialog::GetFileExt() const {
  CString strName = GetFileName();
  int i = strName.ReverseFind(_T('.'));
  return i >= 0 ? strName.Mid(i + 1) : CString();
}

CString CFileDialog::GetFileTitle() const {
  CString strName = GetFileName();
  int i = strName.ReverseFind(_T('.'));
  return i >= 0 ? strName.Left(i) : strName;
}

CString CFileDialog::GetFolderPath() const {
  CString strPath = GetPathName();
  int i = strPath.ReverseFind(_T('\\'));
  return i >= 0 ? strPath.Left(i) : CString();
}

CString CFileDialog::GetNextPathName(POSITION& pos) const {
  LPCTSTR lpsz = (LPCTSTR)pos;
  if (lpsz == NULL) return CString();
  if (lpsz == m_ofn.lpstrFile) {
    // A single selection is just the path.
    if (!(m_ofn.Flags & OFN_ALLOWMULTISELECT) || lpsz[lstrlen(lpsz) + 1] == 0) {
      pos = NULL;
      return lpsz;
    }
    lpsz += lstrlen(lpsz) + 1;  // skip the folder
  }
  CString strPath = m_ofn.lpstrFile;
  if (strPath.Right(1) != _T("\\")) strPath += _T('\\');
  strPath += lpsz;
  lpsz += lstrlen(lpsz) + 1;
  pos = *lpsz != 0 ? (POSITION)lpsz : NULL;
  return strPath;
}

void CFileDialog::SetDefExt(LPCSTR lpsz) {
  m_strDefExt = lpsz;
  m_ofn.lpstrDefExt = m_strDefExt.IsEmpty() ? NULL : (LPCTSTR)m_strDefExt;
}

UINT CFileDialog::OnShareViolation(LPCTSTR) { return OFN_SHAREWARN; }
BOOL CFileDialog::OnFileNameOK() { return FALSE; }
void CFileDialog::OnInitDone() {}
void CFileDialog::OnFileNameChange() {}
void CFileDialog::OnFolderChange() {}
void CFileDialog::OnTypeChange() {}

// ---------------------------------------------------------------------
// CColorDialog

IMPLEMENT_DYNAMIC(CColorDialog, CCommonDialog)

COLORREF CColorDialog::clrSavedCustom[16] = {
    RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255),
    RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255),
    RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255),
    RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255)};

COLORREF* PASCAL CColorDialog::GetSavedCustomColors() { return clrSavedCustom; }

CColorDialog::CColorDialog(COLORREF clrInit, DWORD dwFlags, CWnd* pParentWnd)
    : CCommonDialog(pParentWnd) {
  memset(&m_cc, 0, sizeof(m_cc));
  m_cc.lStructSize = sizeof(m_cc);
  m_cc.lpCustColors = clrSavedCustom;
  m_cc.Flags = dwFlags;
  if ((m_cc.rgbResult = clrInit) != 0) m_cc.Flags |= CC_RGBINIT;
}

INT_PTR CColorDialog::DoModal() {
  return _AfxRunCommonDialog(m_pParentWnd, &m_cc.hwndOwner,
                             [this]() { return ::ChooseColor(&m_cc); });
}

void CColorDialog::SetCurrentColor(COLORREF clr) {
  m_cc.rgbResult = clr;
  m_cc.Flags |= CC_RGBINIT;
}

// ---------------------------------------------------------------------
// CFontDialog

IMPLEMENT_DYNAMIC(CFontDialog, CCommonDialog)

CFontDialog::CFontDialog(LPLOGFONT lplfInitial, DWORD dwFlags, CDC* pdcPrinter,
                         CWnd* pParentWnd)
    : CCommonDialog(pParentWnd) {
  memset(&m_cf, 0, sizeof(m_cf));
  memset(&m_lf, 0, sizeof(m_lf));
  m_szStyleName[0] = 0;
  m_cf.lStructSize = sizeof(m_cf);
  m_cf.lpLogFont = &m_lf;
  m_cf.lpszStyle = m_szStyleName;
  m_cf.Flags = dwFlags | CF_USESTYLE;
  if (lplfInitial != NULL) {
    m_lf = *lplfInitial;
    m_cf.Flags |= CF_INITTOLOGFONTSTRUCT;
  }
  if (pdcPrinter != NULL) {
    m_cf.hDC = pdcPrinter->m_hAttribDC;
    m_cf.Flags |= CF_PRINTERFONTS;
  }
}

CFontDialog::CFontDialog(const CHARFORMAT& charformat, DWORD dwFlags, CDC* pdcPrinter,
                         CWnd* pParentWnd)
    : CCommonDialog(pParentWnd) {
  memset(&m_cf, 0, sizeof(m_cf));
  memset(&m_lf, 0, sizeof(m_lf));
  m_szStyleName[0] = 0;
  m_cf.lStructSize = sizeof(m_cf);
  m_cf.lpLogFont = &m_lf;
  m_cf.lpszStyle = m_szStyleName;
  m_cf.Flags = dwFlags | CF_INITTOLOGFONTSTRUCT;
  m_cf.Flags |= FillInLogFont(charformat);
  if (charformat.dwMask & CFM_COLOR) m_cf.rgbColors = charformat.crTextColor;
  if (pdcPrinter != NULL) {
    m_cf.hDC = pdcPrinter->m_hAttribDC;
    m_cf.Flags |= CF_PRINTERFONTS;
  }
}

// Fills m_lf from what cf knows, and returns the flags that leave the
// rest of the dialog blank.
DWORD CFontDialog::FillInLogFont(const CHARFORMAT& cf) {
  DWORD dwFlags = 0;
  if (cf.dwMask & CFM_SIZE) {
    HDC hDC = ::GetDC(NULL);
    m_lf.lfHeight = -::MulDiv(cf.yHeight, ::GetDeviceCaps(hDC, LOGPIXELSY), 1440);
    ::ReleaseDC(NULL, hDC);
  } else {
    dwFlags |= CF_NOSIZESEL;
  }
  if ((cf.dwMask & (CFM_BOLD | CFM_ITALIC)) == (CFM_BOLD | CFM_ITALIC)) {
    m_lf.lfWeight = (cf.dwEffects & CFE_BOLD) ? FW_BOLD : FW_NORMAL;
    m_lf.lfItalic = (BYTE)((cf.dwEffects & CFE_ITALIC) != 0);
  } else {
    dwFlags |= CF_NOSTYLESEL;
  }
  m_lf.lfUnderline = (BYTE)((cf.dwMask & CFM_UNDERLINE) && (cf.dwEffects & CFE_UNDERLINE));
  m_lf.lfStrikeOut = (BYTE)((cf.dwMask & CFM_STRIKEOUT) && (cf.dwEffects & CFE_STRIKEOUT));
  if (cf.dwMask & CFM_CHARSET)
    m_lf.lfCharSet = cf.bCharSet;
  else
    dwFlags |= CF_NOSCRIPTSEL;
  if (cf.dwMask & CFM_FACE) {
    m_lf.lfPitchAndFamily = cf.bPitchAndFamily;
    lstrcpyn(m_lf.lfFaceName, cf.szFaceName, LF_FACESIZE);
  } else {
    dwFlags |= CF_NOFACESEL;
  }
  return dwFlags;
}

void CFontDialog::GetCharFormat(CHARFORMAT& cf) const {
  cf.dwEffects = 0;
  cf.dwMask = 0;
  DWORD dwFlags = m_cf.Flags;
  if (!(dwFlags & CF_NOSTYLESEL)) {
    cf.dwMask |= CFM_BOLD | CFM_ITALIC;
    if (m_lf.lfWeight >= FW_BOLD) cf.dwEffects |= CFE_BOLD;
    if (m_lf.lfItalic) cf.dwEffects |= CFE_ITALIC;
  }
  if (!(dwFlags & CF_NOSIZESEL)) {
    cf.dwMask |= CFM_SIZE;
    cf.yHeight = m_cf.iPointSize * 2;  // tenths of a point to twips
  }
  if (!(dwFlags & CF_NOFACESEL)) {
    cf.dwMask |= CFM_FACE;
    cf.bPitchAndFamily = m_lf.lfPitchAndFamily;
    lstrcpyn(cf.szFaceName, m_lf.lfFaceName, LF_FACESIZE);
  }
  if (dwFlags & CF_EFFECTS) {
    cf.dwMask |= CFM_UNDERLINE | CFM_STRIKEOUT | CFM_COLOR;
    if (m_lf.lfUnderline) cf.dwEffects |= CFE_UNDERLINE;
    if (m_lf.lfStrikeOut) cf.dwEffects |= CFE_STRIKEOUT;
    cf.crTextColor = m_cf.rgbColors;
  }
  if (!(dwFlags & CF_NOSCRIPTSEL)) {
    cf.dwMask |= CFM_CHARSET;
    cf.bCharSet = m_lf.lfCharSet;
  }
}

INT_PTR CFontDialog::DoModal() {
  return _AfxRunCommonDialog(m_pParentWnd, &m_cf.hwndOwner,
                             [this]() { return ::ChooseFont(&m_cf); });
}

void CFontDialog::GetCurrentFont(LPLOGFONT lplf) {
  if (m_hWnd != NULL)
    SendMessage(WM_CHOOSEFONT_GETLOGFONT, 0, (LPARAM)lplf);
  else
    *lplf = m_lf;
}

// ---------------------------------------------------------------------
// CPrintDialog

IMPLEMENT_DYNAMIC(CPrintDialog, CCommonDialog)

CPrintDialog::CPrintDialog(BOOL bPrintSetupOnly, DWORD dwFlags, CWnd* pParentWnd)
    : CCommonDialog(pParentWnd), m_pd(m_pdActual) {
  memset(&m_pdActual, 0, sizeof(m_pdActual));
  m_pd.lStructSize = sizeof(m_pdActual);
  m_pd.Flags = dwFlags;
  if (bPrintSetupOnly) m_pd.Flags |= PD_PRINTSETUP;
  else m_pd.Flags |= PD_RETURNDC;
  m_pd.nCopies = 1;
  m_pd.nFromPage = m_pd.nToPage = 1;
  m_pd.nMinPage = 1;
  m_pd.nMaxPage = 0xFFFF;
}

CPrintDialog::~CPrintDialog() {
  if (m_pd.hDC != NULL) ::DeleteDC(m_pd.hDC);
}

INT_PTR CPrintDialog::DoModal() {
  return _AfxRunCommonDialog(m_pParentWnd, &m_pd.hwndOwner,
                             [this]() { return ::PrintDlg(&m_pd); });
}

BOOL CPrintDialog::GetDefaults() {
  m_pd.Flags |= PD_RETURNDEFAULT;
  BOOL bResult = ::PrintDlg(&m_pd);
  m_pd.Flags &= ~PD_RETURNDEFAULT;
  return bResult;
}

int CPrintDialog::GetCopies() const {
  if (m_pd.Flags & PD_USEDEVMODECOPIES) {
    LPDEVMODE lpDevMode = GetDevMode();
    int nCopies = lpDevMode != NULL ? lpDevMode->dmCopies : 1;
    if (m_pd.hDevMode != NULL) ::GlobalUnlock(m_pd.hDevMode);
    return nCopies;
  }
  return m_pd.nCopies;
}

LPDEVMODE CPrintDialog::GetDevMode() const {
  return m_pd.hDevMode != NULL ? (LPDEVMODE)::GlobalLock(m_pd.hDevMode) : NULL;
}

static CString _AfxDevNamesString(HGLOBAL hDevNames, int nWhich) {
  if (hDevNames == NULL) return CString();
  LPDEVNAMES lpDevNames = (LPDEVNAMES)::GlobalLock(hDevNames);
  WORD wOffset = nWhich == 0   ? lpDevNames->wDriverOffset
                 : nWhich == 1 ? lpDevNames->wDeviceOffset
                               : lpDevNames->wOutputOffset;
  CString str = (LPCTSTR)lpDevNames + wOffset;
  ::GlobalUnlock(hDevNames);
  return str;
}

CString CPrintDialog::GetDriverName() const { return _AfxDevNamesString(m_pd.hDevNames, 0); }
CString CPrintDialog::GetDeviceName() const { return _AfxDevNamesString(m_pd.hDevNames, 1); }
CString CPrintDialog::GetPortName() const { return _AfxDevNamesString(m_pd.hDevNames, 2); }

HDC CPrintDialog::GetPrinterDC() const { return m_pd.hDC; }

static HDC _AfxCreateDC(HGLOBAL hDevNames, HGLOBAL hDevMode) {
  if (hDevNames == NULL) return NULL;
  LPDEVNAMES lpDevNames = (LPDEVNAMES)::GlobalLock(hDevNames);
  LPDEVMODE lpDevMode = hDevMode != NULL ? (LPDEVMODE)::GlobalLock(hDevMode) : NULL;
  HDC hDC = ::CreateDC((LPCTSTR)lpDevNames + lpDevNames->wDriverOffset,
                       (LPCTSTR)lpDevNames + lpDevNames->wDeviceOffset,
                       (LPCTSTR)lpDevNames + lpDevNames->wOutputOffset, lpDevMode);
  if (lpDevMode != NULL) ::GlobalUnlock(hDevMode);
  ::GlobalUnlock(hDevNames);
  return hDC;
}

HDC CPrintDialog::CreatePrinterDC() {
  if (m_pd.hDC != NULL) ::DeleteDC(m_pd.hDC);
  m_pd.hDC = _AfxCreateDC(m_pd.hDevNames, m_pd.hDevMode);
  return m_pd.hDC;
}

// ---------------------------------------------------------------------
// CPageSetupDialog

IMPLEMENT_DYNAMIC(CPageSetupDialog, CCommonDialog)

CPageSetupDialog::CPageSetupDialog(DWORD dwFlags, CWnd* pParentWnd) : CCommonDialog(pParentWnd) {
  memset(&m_psd, 0, sizeof(m_psd));
  m_psd.lStructSize = sizeof(m_psd);
  m_psd.Flags = dwFlags;
}

CPageSetupDialog::~CPageSetupDialog() {}

INT_PTR CPageSetupDialog::DoModal() {
  return _AfxRunCommonDialog(m_pParentWnd, &m_psd.hwndOwner,
                             [this]() { return ::PageSetupDlg(&m_psd); });
}

LPDEVMODE CPageSetupDialog::GetDevMode() const {
  return m_psd.hDevMode != NULL ? (LPDEVMODE)::GlobalLock(m_psd.hDevMode) : NULL;
}

CString CPageSetupDialog::GetDriverName() const { return _AfxDevNamesString(m_psd.hDevNames, 0); }
CString CPageSetupDialog::GetDeviceName() const { return _AfxDevNamesString(m_psd.hDevNames, 1); }
CString CPageSetupDialog::GetPortName() const { return _AfxDevNamesString(m_psd.hDevNames, 2); }

HDC CPageSetupDialog::CreatePrinterDC() { return _AfxCreateDC(m_psd.hDevNames, m_psd.hDevMode); }

void CPageSetupDialog::GetMargins(LPRECT lpRectMargins, LPRECT lpRectMinMargins) const {
  if (lpRectMargins != NULL) *lpRectMargins = m_psd.rtMargin;
  if (lpRectMinMargins != NULL) *lpRectMinMargins = m_psd.rtMinMargin;
}

// ---------------------------------------------------------------------
// CFindReplaceDialog

IMPLEMENT_DYNAMIC(CFindReplaceDialog, CCommonDialog)

CFindReplaceDialog::CFindReplaceDialog() : CCommonDialog(NULL) {
  memset(&m_fr, 0, sizeof(m_fr));
  m_szFindWhat[0] = 0;
  m_szReplaceWith[0] = 0;
  m_fr.lStructSize = sizeof(m_fr);
  m_fr.lpstrFindWhat = m_szFindWhat;
  m_fr.wFindWhatLen = 128;
  m_fr.lpstrReplaceWith = m_szReplaceWith;
  m_fr.wReplaceWithLen = 128;
}

BOOL CFindReplaceDialog::Create(BOOL bFindDialogOnly, LPCTSTR lpszFindWhat,
                                LPCTSTR lpszReplaceWith, DWORD dwFlags, CWnd* pParentWnd) {
  m_fr.Flags |= dwFlags;
  m_fr.lCustData = (LPARAM)this;
  if (pParentWnd == NULL) pParentWnd = AfxGetMainWnd();
  m_fr.hwndOwner = pParentWnd->GetSafeHwnd();
  if (lpszFindWhat != NULL) lstrcpyn(m_szFindWhat, lpszFindWhat, 128);
  if (lpszReplaceWith != NULL) lstrcpyn(m_szReplaceWith, lpszReplaceWith, 128);

  AfxHookWindowCreate(this);
  HWND hWnd = bFindDialogOnly ? ::FindText(&m_fr) : ::ReplaceText(&m_fr);
  if (!AfxUnhookWindowCreate()) PostNcDestroy();
  return hWnd != NULL;
}

CFindReplaceDialog* PASCAL CFindReplaceDialog::GetNotifier(LPARAM lParam) {
  return (CFindReplaceDialog*)((FINDREPLACE*)lParam)->lCustData;
}

void CFindReplaceDialog::PostNcDestroy() { delete this; }
