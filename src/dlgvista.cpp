// dlgvista.cpp - the Vista-style file dialog behind CFileDialog
// (IFileOpenDialog / IFileSaveDialog, with its events going to the
// CFileDialog virtuals), its customization, and CFolderPickerDialog.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
// The SDK declares IFileDialog only for Vista and later; the dialog is
// still found at run time, so the library runs on older Windows.
#if defined(_WIN32) && !defined(NTDDI_VERSION)
#define NTDDI_VERSION 0x06000000
#endif
#include "afximpl.h"
#include <afxdlgs.h>
#ifndef _WINPORT
#include <shlobj.h>
#endif

IMPLEMENT_DYNAMIC(CFolderPickerDialog, CFileDialog)

CFileDialog::~CFileDialog() {
#ifndef _WINPORT
  if (m_pIFileDialog != NULL) m_pIFileDialog->Release();
  if (m_bCoInit) ::CoUninitialize();
#endif
}

void CFileDialog::OnButtonClicked(DWORD) {}
void CFileDialog::OnCheckButtonToggled(DWORD, BOOL) {}
void CFileDialog::OnControlActivating(DWORD) {}
void CFileDialog::OnItemSelected(DWORD, DWORD) {}

#ifndef _WINPORT

// The identifiers, spelled out so nothing depends on which of them the
// SDK's uuid library has.
static const CLSID _afxClsidFileOpenDialog = {
    0xdc1c5a9c, 0xe88a, 0x4dde, {0xa5, 0xa1, 0x60, 0xf8, 0x2a, 0x20, 0xae, 0xf7}};
static const CLSID _afxClsidFileSaveDialog = {
    0xc0b4e2f3, 0xba21, 0x4773, {0x8d, 0xba, 0x33, 0x5e, 0xc9, 0x46, 0xeb, 0x8b}};
static const IID _afxIidFileDialog = {
    0x42f85136, 0xdb7e, 0x439c, {0x85, 0xf1, 0xe4, 0x07, 0x5d, 0x13, 0x5f, 0xc8}};
static const IID _afxIidFileOpenDialog = {
    0xd57c7288, 0xd4ad, 0x4768, {0xbe, 0x02, 0x9d, 0x96, 0x95, 0x32, 0xd9, 0x60}};
static const IID _afxIidFileSaveDialog = {
    0x84bccd23, 0x5fde, 0x4cdb, {0xae, 0xa4, 0xaf, 0x64, 0xb8, 0x3d, 0x78, 0xab}};
static const IID _afxIidFileDialogEvents = {
    0x973510db, 0x7d7f, 0x452b, {0x89, 0x75, 0x74, 0xa8, 0x58, 0x28, 0xd3, 0x54}};
static const IID _afxIidFileDialogCustomize = {
    0xe6fdd21a, 0x163f, 0x4975, {0x9c, 0x8c, 0xa6, 0x9f, 0x1b, 0xa3, 0x70, 0x34}};
static const IID _afxIidFileDialogControlEvents = {
    0x36116642, 0xd713, 0x4b97, {0x9b, 0x83, 0x74, 0x84, 0xa9, 0xd0, 0x04, 0x33}};
static const IID _afxIidShellItem = {
    0x43826d1e, 0xe718, 0x42ee, {0xbc, 0x55, 0xa1, 0xe2, 0x61, 0xc3, 0x7b, 0xfe}};
static const IID _afxIidUnknown = {
    0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

// A shell item for a path, through shell32 at run time (Vista and later).
static IShellItem* _AfxShellItemFromPath(LPCTSTR lpszPath) {
  typedef HRESULT(WINAPI * PFN)(PCWSTR, IBindCtx*, REFIID, void**);
  static PFN pfn = NULL;
  static BOOL bLooked = FALSE;
  if (!bLooked) {
    HMODULE hShell = ::GetModuleHandle(_T("shell32.dll"));
    if (hShell == NULL) hShell = ::LoadLibrary(_T("shell32.dll"));
    if (hShell != NULL)
      pfn = (PFN)(void*)::GetProcAddress(hShell, "SHCreateItemFromParsingName");
    bLooked = TRUE;
  }
  if (pfn == NULL || lpszPath == NULL || *lpszPath == 0) return NULL;
  CStringW strPath(lpszPath);
  IShellItem* psi = NULL;
  if (FAILED(pfn(strPath, NULL, _afxIidShellItem, (void**)&psi))) return NULL;
  return psi;
}

static CString _AfxShellItemPath(IShellItem* psi, BOOL bNonFileSys) {
  CString str;
  LPWSTR pwsz = NULL;
  HRESULT hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &pwsz);
  if (FAILED(hr) && bNonFileSys) hr = psi->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &pwsz);
  if (SUCCEEDED(hr) && pwsz != NULL) {
    str = CString(pwsz);
    ::CoTaskMemFree(pwsz);
  }
  return str;
}

// ---------------------------------------------------------------------
// The event sink: IFileDialogEvents and IFileDialogControlEvents, both
// handed to the CFileDialog virtuals.

class _AfxFileDialogEvents : public IFileDialogEvents, public IFileDialogControlEvents {
public:
  explicit _AfxFileDialogEvents(CFileDialog* pDlg) : m_nRef(1), m_pDlg(pDlg) {}

  // IUnknown
  STDMETHOD(QueryInterface)(REFIID riid, void** ppv) {
    if (ppv == NULL) return E_POINTER;
    if (IsEqualIID(riid, _afxIidUnknown) || IsEqualIID(riid, _afxIidFileDialogEvents))
      *ppv = static_cast<IFileDialogEvents*>(this);
    else if (IsEqualIID(riid, _afxIidFileDialogControlEvents))
      *ppv = static_cast<IFileDialogControlEvents*>(this);
    else {
      *ppv = NULL;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  STDMETHOD_(ULONG, AddRef)() { return (ULONG)::InterlockedIncrement(&m_nRef); }
  STDMETHOD_(ULONG, Release)() {
    LONG n = ::InterlockedDecrement(&m_nRef);
    if (n == 0) delete this;
    return (ULONG)n;
  }

  // IFileDialogEvents
  STDMETHOD(OnFileOk)(IFileDialog*) {
    m_pDlg->UpdateOFNFromShellDialog();
    return m_pDlg->OnFileNameOK() ? S_FALSE : S_OK;
  }
  STDMETHOD(OnFolderChanging)(IFileDialog*, IShellItem*) { return S_OK; }
  STDMETHOD(OnFolderChange)(IFileDialog*) {
    m_pDlg->OnFolderChange();
    return S_OK;
  }
  STDMETHOD(OnSelectionChange)(IFileDialog*) {
    m_pDlg->OnFileNameChange();
    return S_OK;
  }
  STDMETHOD(OnShareViolation)(IFileDialog*, IShellItem* psi,
                              FDE_SHAREVIOLATION_RESPONSE* pResponse) {
    CString strPath = _AfxShellItemPath(psi, FALSE);
    UINT nAnswer = m_pDlg->OnShareViolation(strPath);
    if (pResponse != NULL)
      *pResponse = nAnswer == OFN_SHAREFALLTHROUGH ? FDESVR_ACCEPT
                   : nAnswer == OFN_SHARENOWARN    ? FDESVR_REFUSE
                                                   : FDESVR_DEFAULT;
    return S_OK;
  }
  STDMETHOD(OnTypeChange)(IFileDialog* pfd) {
    UINT nIndex = 0;
    if (SUCCEEDED(pfd->GetFileTypeIndex(&nIndex))) m_pDlg->m_ofn.nFilterIndex = nIndex;
    m_pDlg->OnTypeChange();
    return S_OK;
  }
  STDMETHOD(OnOverwrite)(IFileDialog*, IShellItem*, FDE_OVERWRITE_RESPONSE* pResponse) {
    if (pResponse != NULL) *pResponse = FDEOR_DEFAULT;
    return S_OK;
  }

  // IFileDialogControlEvents
  STDMETHOD(OnItemSelected)(IFileDialogCustomize*, DWORD dwIDCtl, DWORD dwIDItem) {
    m_pDlg->OnItemSelected(dwIDCtl, dwIDItem);
    return S_OK;
  }
  STDMETHOD(OnButtonClicked)(IFileDialogCustomize*, DWORD dwIDCtl) {
    m_pDlg->OnButtonClicked(dwIDCtl);
    return S_OK;
  }
  STDMETHOD(OnCheckButtonToggled)(IFileDialogCustomize*, DWORD dwIDCtl, BOOL bChecked) {
    m_pDlg->OnCheckButtonToggled(dwIDCtl, bChecked);
    return S_OK;
  }
  STDMETHOD(OnControlActivating)(IFileDialogCustomize*, DWORD dwIDCtl) {
    m_pDlg->OnControlActivating(dwIDCtl);
    return S_OK;
  }

private:
  virtual ~_AfxFileDialogEvents() {}
  LONG m_nRef;
  CFileDialog* m_pDlg;
};

// ---------------------------------------------------------------------
// Creating and showing the dialog

IFileDialog* CFileDialog::_AfxEnsureShellDialog() {
  if (m_pIFileDialog != NULL) return m_pIFileDialog;
  const CLSID& clsid = m_bOpenFileDialog ? _afxClsidFileOpenDialog : _afxClsidFileSaveDialog;
  HRESULT hr = ::CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, _afxIidFileDialog,
                                  (void**)&m_pIFileDialog);
  if (hr == CO_E_NOTINITIALIZED && !m_bCoInit) {
    // The program has not started COM on this thread: start it for the
    // dialog's lifetime.
    if (SUCCEEDED(::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) {
      m_bCoInit = TRUE;
      hr = ::CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, _afxIidFileDialog,
                              (void**)&m_pIFileDialog);
    }
  }
  if (FAILED(hr)) m_pIFileDialog = NULL;
  return m_pIFileDialog;
}

BOOL CFileDialog::_AfxUseShellDialog() {
  // A hook or a template needs the classic dialog, as GetOpenFileName
  // itself decides.
  if (!m_bVistaStyle) return FALSE;
  if (m_ofn.Flags & (OFN_ENABLEHOOK | OFN_ENABLETEMPLATE | OFN_ENABLETEMPLATEHANDLE)) return FALSE;
  return _AfxEnsureShellDialog() != NULL;
}

INT_PTR CFileDialog::_AfxDoModalShell() {
  m_ofn.hInstance = AfxGetResourceHandle();
  ApplyOFNToShellDialog();

  _AfxFileDialogEvents* pSink = new _AfxFileDialogEvents(this);
  DWORD dwCookie = 0;
  BOOL bAdvised = SUCCEEDED(m_pIFileDialog->Advise(pSink, &dwCookie));

  HWND hWndTop = NULL;
  m_ofn.hwndOwner = _AfxGetSafeOwner(m_pParentWnd->GetSafeHwnd(), &hWndTop);
  HRESULT hr = m_pIFileDialog->Show(m_ofn.hwndOwner);
  if (hWndTop != NULL) ::EnableWindow(hWndTop, TRUE);

  if (bAdvised) m_pIFileDialog->Unadvise(dwCookie);
  pSink->Release();

  if (FAILED(hr)) return IDCANCEL;
  UpdateOFNFromShellDialog();
  return IDOK;
}

// ---------------------------------------------------------------------
// OPENFILENAME to and from the dialog

void CFileDialog::ApplyOFNToShellDialog() {
  IFileDialog* pfd = _AfxEnsureShellDialog();
  if (pfd == NULL) return;

  // The filter: NUL separated name and pattern pairs.
  if (m_ofn.lpstrFilter != NULL && *m_ofn.lpstrFilter != 0) {
    CArray<CStringW, CStringW&> strings;
    for (LPCTSTR p = m_ofn.lpstrFilter; *p != 0; p += lstrlen(p) + 1) {
      CStringW str(p);
      strings.Add(str);
    }
    int nSpecs = (int)strings.GetSize() / 2;
    if (nSpecs > 0) {
      COMDLG_FILTERSPEC* pSpecs = new COMDLG_FILTERSPEC[nSpecs];
      for (int i = 0; i < nSpecs; i++) {
        pSpecs[i].pszName = strings[i * 2];
        pSpecs[i].pszSpec = strings[i * 2 + 1];
      }
      pfd->SetFileTypes((UINT)nSpecs, pSpecs);
      pfd->SetFileTypeIndex(m_ofn.nFilterIndex != 0 ? m_ofn.nFilterIndex : 1);
      delete[] pSpecs;
    }
  }

  // The options that have a FOS_ twin.
  static const DWORD map[][2] = {
      {OFN_ALLOWMULTISELECT, FOS_ALLOWMULTISELECT}, {OFN_FILEMUSTEXIST, FOS_FILEMUSTEXIST},
      {OFN_PATHMUSTEXIST, FOS_PATHMUSTEXIST},       {OFN_OVERWRITEPROMPT, FOS_OVERWRITEPROMPT},
      {OFN_CREATEPROMPT, FOS_CREATEPROMPT},         {OFN_NOCHANGEDIR, FOS_NOCHANGEDIR},
      {OFN_NOVALIDATE, FOS_NOVALIDATE},             {OFN_NOREADONLYRETURN, FOS_NOREADONLYRETURN},
      {OFN_NOTESTFILECREATE, FOS_NOTESTFILECREATE}, {OFN_SHAREAWARE, FOS_SHAREAWARE},
      {OFN_DONTADDTORECENT, FOS_DONTADDTORECENT},   {OFN_FORCESHOWHIDDEN, FOS_FORCESHOWHIDDEN},
      {OFN_NODEREFERENCELINKS, FOS_NODEREFERENCELINKS},
  };
  FILEOPENDIALOGOPTIONS fos = 0;
  pfd->GetOptions(&fos);
  for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
    if (m_ofn.Flags & map[i][0])
      fos |= map[i][1];
    else
      fos &= ~map[i][1];
  }
  if (m_bPickFoldersMode) {
    fos |= FOS_PICKFOLDERS;
    if (m_bPickNonFileSysFoldersMode)
      fos &= ~FOS_FORCEFILESYSTEM;
    else
      fos |= FOS_FORCEFILESYSTEM;
  }
  pfd->SetOptions(fos);

  if (m_ofn.lpstrTitle != NULL) pfd->SetTitle(CStringW(m_ofn.lpstrTitle));
  if (m_ofn.lpstrDefExt != NULL) pfd->SetDefaultExtension(CStringW(m_ofn.lpstrDefExt));

  // The starting folder, then the name; a name with a folder in it gives
  // the folder too.
  if (IShellItem* psi = _AfxShellItemFromPath(m_ofn.lpstrInitialDir)) {
    pfd->SetFolder(psi);
    psi->Release();
  }
  CString strFile = m_ofn.lpstrFile != NULL ? m_ofn.lpstrFile : _T("");
  if (!strFile.IsEmpty()) {
    int i = strFile.ReverseFind(_T('\\'));
    int j = strFile.ReverseFind(_T('/'));
    if (j > i) i = j;
    if (m_bPickFoldersMode) {
      if (IShellItem* psi = _AfxShellItemFromPath(strFile)) {
        pfd->SetFolder(psi);
        psi->Release();
      }
    } else {
      if (i >= 0) {
        if (IShellItem* psi = _AfxShellItemFromPath(strFile.Left(i))) {
          pfd->SetFolder(psi);
          psi->Release();
        }
      }
      pfd->SetFileName(CStringW(strFile.Mid(i + 1)));
    }
  }
}

void CFileDialog::UpdateOFNFromShellDialog() {
  if (m_pIFileDialog == NULL) return;
  BOOL bNonFileSys = m_bPickNonFileSysFoldersMode;

  // The chosen paths: every one for a multiple selection.
  CStringArray paths;
  IFileOpenDialog* pOpen = NULL;
  if (m_bOpenFileDialog && (m_ofn.Flags & OFN_ALLOWMULTISELECT) &&
      SUCCEEDED(m_pIFileDialog->QueryInterface(_afxIidFileOpenDialog, (void**)&pOpen))) {
    IShellItemArray* pItems = NULL;
    if (FAILED(pOpen->GetResults(&pItems))) pOpen->GetSelectedItems(&pItems);
    if (pItems != NULL) {
      DWORD nCount = 0;
      pItems->GetCount(&nCount);
      for (DWORD i = 0; i < nCount; i++) {
        IShellItem* psi = NULL;
        if (SUCCEEDED(pItems->GetItemAt(i, &psi))) {
          paths.Add(_AfxShellItemPath(psi, bNonFileSys));
          psi->Release();
        }
      }
      pItems->Release();
    }
    pOpen->Release();
  } else {
    IShellItem* psi = NULL;
    if (SUCCEEDED(m_pIFileDialog->GetResult(&psi))) {
      paths.Add(_AfxShellItemPath(psi, bNonFileSys));
      psi->Release();
    }
  }
  UINT nIndex = 0;
  if (SUCCEEDED(m_pIFileDialog->GetFileTypeIndex(&nIndex))) m_ofn.nFilterIndex = nIndex;
  if (paths.GetSize() == 0) return;

  // Into the buffer as GetOpenFileName leaves it: one path, or the folder
  // and then the names, each ending in NUL with one more at the end.
  CString strFirst = paths[0];
  int iSep = strFirst.ReverseFind(_T('\\'));
  int jSep = strFirst.ReverseFind(_T('/'));
  if (jSep > iSep) iSep = jSep;
  CString strList;
  if (paths.GetSize() == 1) {
    strList = strFirst;
    strList.AppendChar(0);
  } else {
    CString strFolder = iSep >= 0 ? strFirst.Left(iSep) : CString();
    strList = strFolder;
    strList.AppendChar(0);
    for (INT_PTR k = 0; k < paths.GetSize(); k++) {
      CString strName = paths[k].Mid(strFolder.GetLength() + (strFolder.IsEmpty() ? 0 : 1));
      strList += strName;
      strList.AppendChar(0);
    }
  }
  strList.AppendChar(0);
  m_strVistaResult = strList;

  if (m_ofn.lpstrFile != NULL && m_ofn.nMaxFile > 0) {
    DWORD nCopy = (DWORD)strList.GetLength();
    if (nCopy > m_ofn.nMaxFile) nCopy = m_ofn.nMaxFile;
    memcpy(m_ofn.lpstrFile, (LPCTSTR)strList, nCopy * sizeof(TCHAR));
    m_ofn.lpstrFile[m_ofn.nMaxFile - 1] = 0;
    if (m_ofn.nMaxFile > 1 && nCopy == m_ofn.nMaxFile) m_ofn.lpstrFile[m_ofn.nMaxFile - 2] = 0;
  }
  CString strTitle = strFirst.Mid(iSep + 1);
  m_ofn.nFileOffset = (WORD)(iSep + 1);
  int iDot = strTitle.ReverseFind(_T('.'));
  m_ofn.nFileExtension = (WORD)(iDot >= 0 ? iSep + 1 + iDot + 1 : 0);
  if (m_ofn.lpstrFileTitle != NULL && m_ofn.nMaxFileTitle > 0)
    lstrcpyn(m_ofn.lpstrFileTitle, strTitle, (int)m_ofn.nMaxFileTitle);
}

// ---------------------------------------------------------------------
// Interfaces and customization

IFileOpenDialog* CFileDialog::GetIFileOpenDialog() {
  IFileOpenDialog* p = NULL;
  if (m_bVistaStyle && m_bOpenFileDialog && _AfxEnsureShellDialog() != NULL)
    m_pIFileDialog->QueryInterface(_afxIidFileOpenDialog, (void**)&p);
  return p;
}

IFileSaveDialog* CFileDialog::GetIFileSaveDialog() {
  IFileSaveDialog* p = NULL;
  if (m_bVistaStyle && !m_bOpenFileDialog && _AfxEnsureShellDialog() != NULL)
    m_pIFileDialog->QueryInterface(_afxIidFileSaveDialog, (void**)&p);
  return p;
}

IFileDialogCustomize* CFileDialog::GetIFileDialogCustomize() {
  IFileDialogCustomize* p = NULL;
  if (m_bVistaStyle && _AfxEnsureShellDialog() != NULL)
    m_pIFileDialog->QueryInterface(_afxIidFileDialogCustomize, (void**)&p);
  return p;
}

// Runs one IFileDialogCustomize call; E_NOTIMPL with the classic dialog.
template <typename F>
static HRESULT _AfxCustomize(CFileDialog* pDlg, F call) {
  IFileDialogCustomize* p = pDlg->GetIFileDialogCustomize();
  if (p == NULL) return E_NOTIMPL;
  HRESULT hr = call(p);
  p->Release();
  return hr;
}

HRESULT CFileDialog::AddCheckButton(DWORD dwIDCtl, const CString& strLabel, BOOL bChecked) {
  return _AfxCustomize(this, [&](IFileDialogCustomize* p) {
    return p->AddCheckButton(dwIDCtl, CStringW(strLabel), bChecked);
  });
}
HRESULT CFileDialog::AddComboBox(DWORD dwIDCtl) {
  return _AfxCustomize(this, [&](IFileDialogCustomize* p) { return p->AddComboBox(dwIDCtl); });
}
HRESULT CFileDialog::AddControlItem(DWORD dwIDCtl, DWORD dwIDItem, const CString& strLabel) {
  return _AfxCustomize(this, [&](IFileDialogCustomize* p) {
    return p->AddControlItem(dwIDCtl, dwIDItem, CStringW(strLabel));
  });
}
HRESULT CFileDialog::AddEditBox(DWORD dwIDCtl, const CString& strText) {
  return _AfxCustomize(
      this, [&](IFileDialogCustomize* p) { return p->AddEditBox(dwIDCtl, CStringW(strText)); });
}
HRESULT CFileDialog::AddMenu(DWORD dwIDCtl, const CString& strLabel) {
  return _AfxCustomize(
      this, [&](IFileDialogCustomize* p) { return p->AddMenu(dwIDCtl, CStringW(strLabel)); });
}
HRESULT CFileDialog::AddPushButton(DWORD dwIDCtl, const CString& strLabel) {
  return _AfxCustomize(this, [&](IFileDialogCustomize* p) {
    return p->AddPushButton(dwIDCtl, CStringW(strLabel));
  });
}
HRESULT CFileDialog::AddRadioButtonList(DWORD dwIDCtl) {
  return _AfxCustomize(this,
                       [&](IFileDialogCustomize* p) { return p->AddRadioButtonList(dwIDCtl); });
}
HRESULT CFileDialog::AddSeparator(DWORD dwIDCtl) {
  return _AfxCustomize(this, [&](IFileDialogCustomize* p) { return p->AddSeparator(dwIDCtl); });
}
HRESULT CFileDialog::AddText(DWORD dwIDCtl, const CString& strText) {
  return _AfxCustomize(
      this, [&](IFileDialogCustomize* p) { return p->AddText(dwIDCtl, CStringW(strText)); });
}
HRESULT CFileDialog::EnableOpenDropDown(DWORD dwIDCtl) {
  return _AfxCustomize(this,
                       [&](IFileDialogCustomize* p) { return p->EnableOpenDropDown(dwIDCtl); });
}
HRESULT CFileDialog::EndVisualGroup() {
  return _AfxCustomize(this, [&](IFileDialogCustomize* p) { return p->EndVisualGroup(); });
}
HRESULT CFileDialog::GetCheckButtonState(DWORD dwIDCtl, BOOL& bChecked) {
  return _AfxCustomize(this, [&](IFileDialogCustomize* p) {
    BOOL b = FALSE;
    HRESULT hr = p->GetCheckButtonState(dwIDCtl, &b);
    bChecked = b;
    return hr;
  });
}
HRESULT CFileDialog::GetControlItemState(DWORD dwIDCtl, DWORD dwIDItem, CDCONTROLSTATEF& dwState) {
  return _AfxCustomize(this, [&](IFileDialogCustomize* p) {
    return p->GetControlItemState(dwIDCtl, dwIDItem, &dwState);
  });
}
HRESULT CFileDialog::GetControlState(DWORD dwIDCtl, CDCONTROLSTATEF& dwState) {
  return _AfxCustomize(
      this, [&](IFileDialogCustomize* p) { return p->GetControlState(dwIDCtl, &dwState); });
}
HRESULT CFileDialog::GetEditBoxText(DWORD dwIDCtl, CString& strText) {
  return _AfxCustomize(this, [&](IFileDialogCustomize* p) {
    WCHAR* pwsz = NULL;
    HRESULT hr = p->GetEditBoxText(dwIDCtl, &pwsz);
    if (SUCCEEDED(hr) && pwsz != NULL) {
      strText = CString(pwsz);
      ::CoTaskMemFree(pwsz);
    }
    return hr;
  });
}
HRESULT CFileDialog::GetSelectedControlItem(DWORD dwIDCtl, DWORD& dwIDItem) {
  return _AfxCustomize(this, [&](IFileDialogCustomize* p) {
    return p->GetSelectedControlItem(dwIDCtl, &dwIDItem);
  });
}
HRESULT CFileDialog::MakeProminent(DWORD dwIDCtl) {
  return _AfxCustomize(this, [&](IFileDialogCustomize* p) { return p->MakeProminent(dwIDCtl); });
}
HRESULT CFileDialog::RemoveControlItem(DWORD dwIDCtl, DWORD dwIDItem) {
  return _AfxCustomize(
      this, [&](IFileDialogCustomize* p) { return p->RemoveControlItem(dwIDCtl, dwIDItem); });
}
HRESULT CFileDialog::SetCheckButtonState(DWORD dwIDCtl, BOOL bChecked) {
  return _AfxCustomize(
      this, [&](IFileDialogCustomize* p) { return p->SetCheckButtonState(dwIDCtl, bChecked); });
}
HRESULT CFileDialog::SetControlItemState(DWORD dwIDCtl, DWORD dwIDItem, CDCONTROLSTATEF dwState) {
  return _AfxCustomize(this, [&](IFileDialogCustomize* p) {
    return p->SetControlItemState(dwIDCtl, dwIDItem, dwState);
  });
}
HRESULT CFileDialog::SetControlItemText(DWORD dwIDCtl, DWORD dwIDItem, const CString& strLabel) {
  return _AfxCustomize(this, [&](IFileDialogCustomize* p) {
    return p->SetControlItemText(dwIDCtl, dwIDItem, CStringW(strLabel));
  });
}
HRESULT CFileDialog::SetControlLabel(DWORD dwIDCtl, const CString& strLabel) {
  return _AfxCustomize(this, [&](IFileDialogCustomize* p) {
    return p->SetControlLabel(dwIDCtl, CStringW(strLabel));
  });
}
HRESULT CFileDialog::SetControlState(DWORD dwIDCtl, CDCONTROLSTATEF dwState) {
  return _AfxCustomize(
      this, [&](IFileDialogCustomize* p) { return p->SetControlState(dwIDCtl, dwState); });
}
HRESULT CFileDialog::SetEditBoxText(DWORD dwIDCtl, const CString& strText) {
  return _AfxCustomize(this, [&](IFileDialogCustomize* p) {
    return p->SetEditBoxText(dwIDCtl, CStringW(strText));
  });
}
HRESULT CFileDialog::SetSelectedControlItem(DWORD dwIDCtl, DWORD dwIDItem) {
  return _AfxCustomize(this, [&](IFileDialogCustomize* p) {
    return p->SetSelectedControlItem(dwIDCtl, dwIDItem);
  });
}
HRESULT CFileDialog::StartVisualGroup(DWORD dwIDCtl, const CString& strLabel) {
  return _AfxCustomize(this, [&](IFileDialogCustomize* p) {
    return p->StartVisualGroup(dwIDCtl, CStringW(strLabel));
  });
}

// ---------------------------------------------------------------------
// CFolderPickerDialog's fallback: SHBrowseForFolder, starting at the
// given folder.

static int CALLBACK _AfxBrowseCallback(HWND hWnd, UINT uMsg, LPARAM, LPARAM lpData) {
  if (uMsg == BFFM_INITIALIZED && lpData != 0)
    ::SendMessage(hWnd, BFFM_SETSELECTION, TRUE, lpData);
  return 0;
}

static INT_PTR _AfxBrowseForFolder(CFileDialog* pDlg, CWnd* pParentWnd) {
  OPENFILENAME& ofn = pDlg->m_ofn;
  TCHAR szName[MAX_PATH];
  szName[0] = 0;
  CString strStart = ofn.lpstrFile != NULL ? ofn.lpstrFile : _T("");
  if (strStart.IsEmpty() && ofn.lpstrInitialDir != NULL) strStart = ofn.lpstrInitialDir;

  BROWSEINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.pszDisplayName = szName;
  bi.lpszTitle = ofn.lpstrTitle;
  bi.ulFlags = BIF_RETURNONLYFSDIRS;
  bi.lpfn = _AfxBrowseCallback;
  bi.lParam = strStart.IsEmpty() ? 0 : (LPARAM)(LPCTSTR)strStart;

  HWND hWndTop = NULL;
  bi.hwndOwner = _AfxGetSafeOwner(pParentWnd->GetSafeHwnd(), &hWndTop);
  LPITEMIDLIST pidl = ::SHBrowseForFolder(&bi);
  if (hWndTop != NULL) ::EnableWindow(hWndTop, TRUE);
  if (pidl == NULL) return IDCANCEL;

  TCHAR szPath[MAX_PATH];
  BOOL bOK = ::SHGetPathFromIDList(pidl, szPath);
  ::CoTaskMemFree(pidl);
  if (!bOK) return IDCANCEL;
  if (ofn.lpstrFile != NULL && ofn.nMaxFile > 1) {
    lstrcpyn(ofn.lpstrFile, szPath, (int)ofn.nMaxFile - 1);
    ofn.lpstrFile[lstrlen(ofn.lpstrFile) + 1] = 0;
  }
  return IDOK;
}

#else  // _WINPORT: no shell dialogs; the classic one does the work.

IFileDialog* CFileDialog::_AfxEnsureShellDialog() { return NULL; }
BOOL CFileDialog::_AfxUseShellDialog() { return FALSE; }
INT_PTR CFileDialog::_AfxDoModalShell() { return IDCANCEL; }
void CFileDialog::ApplyOFNToShellDialog() {}
void CFileDialog::UpdateOFNFromShellDialog() {}
IFileOpenDialog* CFileDialog::GetIFileOpenDialog() { return NULL; }
IFileSaveDialog* CFileDialog::GetIFileSaveDialog() { return NULL; }
IFileDialogCustomize* CFileDialog::GetIFileDialogCustomize() { return NULL; }
HRESULT CFileDialog::AddCheckButton(DWORD, const CString&, BOOL) { return E_NOTIMPL; }
HRESULT CFileDialog::AddComboBox(DWORD) { return E_NOTIMPL; }
HRESULT CFileDialog::AddControlItem(DWORD, DWORD, const CString&) { return E_NOTIMPL; }
HRESULT CFileDialog::AddEditBox(DWORD, const CString&) { return E_NOTIMPL; }
HRESULT CFileDialog::AddMenu(DWORD, const CString&) { return E_NOTIMPL; }
HRESULT CFileDialog::AddPushButton(DWORD, const CString&) { return E_NOTIMPL; }
HRESULT CFileDialog::AddRadioButtonList(DWORD) { return E_NOTIMPL; }
HRESULT CFileDialog::AddSeparator(DWORD) { return E_NOTIMPL; }
HRESULT CFileDialog::AddText(DWORD, const CString&) { return E_NOTIMPL; }
HRESULT CFileDialog::EnableOpenDropDown(DWORD) { return E_NOTIMPL; }
HRESULT CFileDialog::EndVisualGroup() { return E_NOTIMPL; }
HRESULT CFileDialog::GetCheckButtonState(DWORD, BOOL&) { return E_NOTIMPL; }
HRESULT CFileDialog::GetControlItemState(DWORD, DWORD, CDCONTROLSTATEF&) { return E_NOTIMPL; }
HRESULT CFileDialog::GetControlState(DWORD, CDCONTROLSTATEF&) { return E_NOTIMPL; }
HRESULT CFileDialog::GetEditBoxText(DWORD, CString&) { return E_NOTIMPL; }
HRESULT CFileDialog::GetSelectedControlItem(DWORD, DWORD&) { return E_NOTIMPL; }
HRESULT CFileDialog::MakeProminent(DWORD) { return E_NOTIMPL; }
HRESULT CFileDialog::RemoveControlItem(DWORD, DWORD) { return E_NOTIMPL; }
HRESULT CFileDialog::SetCheckButtonState(DWORD, BOOL) { return E_NOTIMPL; }
HRESULT CFileDialog::SetControlItemState(DWORD, DWORD, CDCONTROLSTATEF) { return E_NOTIMPL; }
HRESULT CFileDialog::SetControlItemText(DWORD, DWORD, const CString&) { return E_NOTIMPL; }
HRESULT CFileDialog::SetControlLabel(DWORD, const CString&) { return E_NOTIMPL; }
HRESULT CFileDialog::SetControlState(DWORD, CDCONTROLSTATEF) { return E_NOTIMPL; }
HRESULT CFileDialog::SetEditBoxText(DWORD, const CString&) { return E_NOTIMPL; }
HRESULT CFileDialog::SetSelectedControlItem(DWORD, DWORD) { return E_NOTIMPL; }
HRESULT CFileDialog::StartVisualGroup(DWORD, const CString&) { return E_NOTIMPL; }

#endif  // _WINPORT

// ---------------------------------------------------------------------
// CFolderPickerDialog

CFolderPickerDialog::CFolderPickerDialog(LPCTSTR lpszFolder, DWORD dwFlags, CWnd* pParentWnd,
                                         DWORD dwSize, BOOL fNonFileSystemFolders)
    : CFileDialog(TRUE, NULL, lpszFolder, dwFlags, NULL, pParentWnd, dwSize, TRUE) {
  m_bPickFoldersMode = TRUE;
  m_bPickNonFileSysFoldersMode = fNonFileSystemFolders;
}

CFolderPickerDialog::~CFolderPickerDialog() {}

INT_PTR CFolderPickerDialog::DoModal() {
  if (_AfxUseShellDialog()) return _AfxDoModalShell();
#ifndef _WINPORT
  return _AfxBrowseForFolder(this, m_pParentWnd);
#else
  return IDCANCEL;
#endif
}
