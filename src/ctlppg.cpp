// ctlppg.cpp - COlePropertyPage, the property pages of controls, and the
// DDP_ functions.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxctl.h>

IMPLEMENT_DYNAMIC(COlePropertyPage, CDialog)

BEGIN_MESSAGE_MAP(COlePropertyPage, CDialog)
END_MESSAGE_MAP()

BEGIN_INTERFACE_MAP(COlePropertyPage, CDialog)
  INTERFACE_PART(COlePropertyPage, IID_IPropertyPage, PropertyPage)
  INTERFACE_PART(COlePropertyPage, IID_IPropertyPage2, PropertyPage)
END_INTERFACE_MAP()

COlePropertyPage::COlePropertyPage(UINT idDlg, UINT idCaption)
    : m_idDlg(idDlg),
      m_idCaption(idCaption),
      m_dwHelpContext(0),
      m_hDialog(NULL),
      m_bDirty(FALSE),
      m_bLoading(FALSE),
      m_nObjects(0),
      m_ppDisp(NULL),
      m_pPageSite(NULL) {
  m_lpszTemplateName = MAKEINTRESOURCE(idDlg);
  m_nIDHelp = idDlg;
  if (idCaption != 0) m_strPageName.LoadString(idCaption);
  AfxOleLockApp();
}

COlePropertyPage::~COlePropertyPage() {
  ClearQueue();
  for (ULONG i = 0; i < m_nObjects; i++)
    if (m_ppDisp[i] != NULL) m_ppDisp[i]->Release();
  delete[] m_ppDisp;
  if (m_pPageSite != NULL) m_pPageSite->Release();
  AfxOleUnlockApp();
}

// The last reference goes: the window, then the object.
void COlePropertyPage::OnFinalRelease() {
  if (m_hWnd != NULL) DestroyWindow();
  delete this;
}

// The COM object owns the window object, not the other way round.
void COlePropertyPage::PostNcDestroy() {}

LPDISPATCH* COlePropertyPage::GetObjectArray(ULONG* pnObjects) {
  if (pnObjects != NULL) *pnObjects = m_nObjects;
  return m_ppDisp;
}

void COlePropertyPage::SetModifiedFlag(BOOL bModified) {
  BOOL bWas = m_bDirty;
  m_bDirty = bModified;
  if (bModified && !bWas && m_pPageSite != NULL)
    m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY | PROPPAGESTATUS_VALIDATE);
}

void COlePropertyPage::SetDialogResource(HGLOBAL hDialog) { m_hDialog = hDialog; }

void COlePropertyPage::SetHelpInfo(LPCTSTR lpszDocString, LPCTSTR lpszHelpFile,
                                   DWORD dwHelpContext) {
  m_strDocString = lpszDocString != NULL ? lpszDocString : _T("");
  m_strHelpFile = lpszHelpFile != NULL ? lpszHelpFile : _T("");
  m_dwHelpContext = dwHelpContext;
}

static INT_PTR FindID(const CDWordArray& ids, UINT nID) {
  for (INT_PTR i = 0; i < ids.GetSize(); i++)
    if (ids[i] == nID) return i;
  return -1;
}

BOOL COlePropertyPage::GetControlStatus(UINT nID) { return FindID(m_dirtyIDs, nID) >= 0; }

BOOL COlePropertyPage::SetControlStatus(UINT nID, BOOL bDirty) {
  INT_PTR i = FindID(m_dirtyIDs, nID);
  BOOL bWas = i >= 0;
  if (bDirty && !bWas)
    m_dirtyIDs.Add(nID);
  else if (!bDirty && bWas)
    m_dirtyIDs.RemoveAt(i);
  return bWas;
}

void COlePropertyPage::IgnoreApply(UINT nID) {
  if (FindID(m_ignoreIDs, nID) < 0) m_ignoreIDs.Add(nID);
}

void COlePropertyPage::OnSetPageSite() {}
void COlePropertyPage::OnObjectsChanged() {}
BOOL COlePropertyPage::OnHelp(LPCTSTR) { return FALSE; }
BOOL COlePropertyPage::OnEditProperty(DISPID) { return FALSE; }

BOOL COlePropertyPage::OnInitDialog() {
  BOOL bLoading = m_bLoading;
  m_bLoading = TRUE;
  BOOL bResult = CDialog::OnInitDialog();
  m_bLoading = bLoading;
  m_dirtyIDs.RemoveAll();
  m_bDirty = FALSE;
  return bResult;
}

BOOL COlePropertyPage::PreTranslateMessage(MSG* pMsg) {
  // The frame's own keys (Enter and Escape) are its business.
  if (pMsg->message == WM_KEYDOWN && (pMsg->wParam == VK_RETURN || pMsg->wParam == VK_ESCAPE))
    return FALSE;
  return CDialog::PreTranslateMessage(pMsg);
}

// A change in a control makes it, and the page, dirty.
BOOL COlePropertyPage::OnCommand(WPARAM wParam, LPARAM lParam) {
  HWND hWndCtl = (HWND)lParam;
  UINT nID = LOWORD(wParam);
  UINT nCode = HIWORD(wParam);
  if (hWndCtl != NULL && !m_bLoading) {
    BOOL bChange = FALSE;
    switch (nCode) {
      case EN_CHANGE:
      case CBN_SELCHANGE:  // and LBN_SELCHANGE, the same value
      case CBN_EDITCHANGE:
        bChange = TRUE;
        break;
      case BN_CLICKED: {
        TCHAR szClass[16];
        ::GetClassName(hWndCtl, szClass, _countof(szClass));
        LONG lStyle = ::GetWindowLong(hWndCtl, GWL_STYLE) & 0x0F;
        bChange = lstrcmpi(szClass, _T("Button")) == 0 && lStyle != BS_PUSHBUTTON &&
                  lStyle != BS_DEFPUSHBUTTON && lStyle != BS_GROUPBOX && lStyle != BS_OWNERDRAW;
        // A radio button changes its group, which DDP_Radio names by its
        // first button.
        if (bChange && (lStyle == BS_RADIOBUTTON || lStyle == BS_AUTORADIOBUTTON)) {
          HWND hWnd = hWndCtl;
          while (hWnd != NULL && (::GetWindowLong(hWnd, GWL_STYLE) & WS_GROUP) == 0) {
            HWND hWndPrev = ::GetWindow(hWnd, GW_HWNDPREV);
            if (hWndPrev == NULL) break;
            hWnd = hWndPrev;
          }
          if (hWnd != NULL && hWnd != hWndCtl) {
            UINT nFirst = (UINT)::GetDlgCtrlID(hWnd);
            if (FindID(m_ignoreIDs, nFirst) < 0) SetControlStatus(nFirst, TRUE);
          }
        }
        break;
      }
    }
    if (bChange && FindID(m_ignoreIDs, nID) < 0) {
      SetControlStatus(nID, TRUE);
      SetModifiedFlag(TRUE);
    }
  }
  return CDialog::OnCommand(wParam, lParam);
}

// ---------------------------------------------------------------------
// The template: the dialog resource, made a child window without a frame,
// and its size in pixels.

static const BYTE* SkipSzOrOrd(const BYTE* p) {
  const WORD* pw = (const WORD*)p;
  if (*pw == 0xFFFF) return p + 4;
  while (*pw != 0) pw++;
  return (const BYTE*)(pw + 1);
}

HGLOBAL COlePropertyPage::MakeChildTemplate(SIZE* pSize) {
  const BYTE* pSrc = NULL;
  DWORD cbSrc = 0;
  if (m_hDialog != NULL) {
    pSrc = (const BYTE*)::GlobalLock(m_hDialog);
    cbSrc = (DWORD)::GlobalSize(m_hDialog);
  } else {
    HINSTANCE hInst = AfxFindResourceHandle(m_lpszTemplateName, RT_DIALOG);
    HRSRC hRes = ::FindResource(hInst, m_lpszTemplateName, RT_DIALOG);
    HGLOBAL hRsrc = hRes != NULL ? ::LoadResource(hInst, hRes) : NULL;
    if (hRsrc != NULL) {
      pSrc = (const BYTE*)::LockResource(hRsrc);
      cbSrc = ::SizeofResource(hInst, hRes);
    }
  }
  if (pSrc == NULL || cbSrc < sizeof(DLGTEMPLATE)) {
    if (m_hDialog != NULL) ::GlobalUnlock(m_hDialog);
    return NULL;
  }
  HGLOBAL hCopy = ::GlobalAlloc(GMEM_MOVEABLE, cbSrc);
  if (hCopy == NULL) {
    if (m_hDialog != NULL) ::GlobalUnlock(m_hDialog);
    return NULL;
  }
  BYTE* p = (BYTE*)::GlobalLock(hCopy);
  memcpy(p, pSrc, cbSrc);
  if (m_hDialog != NULL) ::GlobalUnlock(m_hDialog);

  // DLGTEMPLATEEX starts with dlgVer 1 and signature 0xFFFF.
  BOOL bEx = ((WORD*)p)[0] == 1 && ((WORD*)p)[1] == 0xFFFF;
  DWORD* pStyle = (DWORD*)(p + (bEx ? 12 : 0));
  DWORD* pExStyle = (DWORD*)(p + (bEx ? 8 : 4));
  short* pcx = (short*)(p + (bEx ? 22 : 14));
  short* pcy = (short*)(p + (bEx ? 24 : 16));
  DWORD dwStyle = *pStyle;
  *pStyle = (dwStyle & ~(WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | DS_MODALFRAME |
                         WS_VISIBLE | WS_DISABLED | DS_CENTER | DS_ABSALIGN)) |
            WS_CHILD | DS_CONTROL;
  *pExStyle |= WS_EX_CONTROLPARENT;
  *pExStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE);

  if (pSize != NULL) {
    // The dialog units of the template's font.
    int cxBase = LOWORD(::GetDialogBaseUnits()), cyBase = HIWORD(::GetDialogBaseUnits());
    if (dwStyle & DS_SETFONT) {
      const BYTE* q = p + (bEx ? 26 : 18);
      q = SkipSzOrOrd(q);  // menu
      q = SkipSzOrOrd(q);  // class
      q = SkipSzOrOrd(q);  // caption
      WORD wPoint = *(const WORD*)q;
      q += bEx ? 6 : 2;  // point size, then weight, italic and charset in EX
      CString strFace((LPCWSTR)q);
      HDC hdc = ::GetDC(NULL);
      LOGFONT lf;
      memset(&lf, 0, sizeof(lf));
      lf.lfHeight = -::MulDiv(wPoint, ::GetDeviceCaps(hdc, LOGPIXELSY), 72);
      lf.lfCharSet = DEFAULT_CHARSET;
      lstrcpyn(lf.lfFaceName, strFace, LF_FACESIZE);
      HFONT hFont = ::CreateFontIndirect(&lf);
      if (hFont != NULL) {
        HGDIOBJ hOld = ::SelectObject(hdc, hFont);
        TEXTMETRIC tm;
        SIZE size;
        static const TCHAR kAlphabet[] = _T("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
        if (::GetTextMetrics(hdc, &tm) &&
            ::GetTextExtentPoint32(hdc, kAlphabet, 52, &size)) {
          cxBase = (size.cx / 26 + 1) / 2;
          cyBase = tm.tmHeight;
        }
        ::SelectObject(hdc, hOld);
        ::DeleteObject(hFont);
      }
      ::ReleaseDC(NULL, hdc);
    }
    pSize->cx = ::MulDiv(*pcx, cxBase, 4);
    pSize->cy = ::MulDiv(*pcy, cyBase, 8);
  }
  ::GlobalUnlock(hCopy);
  return hCopy;
}

void COlePropertyPage::LoadFromObjects() {
  if (m_hWnd == NULL) return;
  BOOL bLoading = m_bLoading;
  m_bLoading = TRUE;
  UpdateData(FALSE);
  m_bLoading = bLoading;
  m_dirtyIDs.RemoveAll();
  m_bDirty = FALSE;
}

// ---------------------------------------------------------------------
// The controls' properties

static BOOL GetDispID(LPDISPATCH pDisp, LPCTSTR lpszProp, DISPID* pDispID) {
  CStringW strName(lpszProp);
  LPOLESTR pszName = (LPOLESTR)(LPCWSTR)strName;
  return SUCCEEDED(pDisp->GetIDsOfNames(IID_NULL, &pszName, 1, LOCALE_USER_DEFAULT, pDispID));
}

BOOL COlePropertyPage::ReadProperty(LPCTSTR lpszProp, VARTYPE vt, void* pValue) {
  // The first object gives the value.
  if (m_nObjects == 0 || m_ppDisp[0] == NULL) return FALSE;
  DISPID dispid;
  if (!GetDispID(m_ppDisp[0], lpszProp, &dispid)) return FALSE;
  DISPPARAMS params = {NULL, NULL, 0, 0};
  VARIANT var;
  ::VariantInit(&var);
  HRESULT hr = m_ppDisp[0]->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET,
                                   &params, &var, NULL, NULL);
  if (SUCCEEDED(hr) && var.vt != vt) hr = ::VariantChangeType(&var, &var, 0, vt);
  if (FAILED(hr)) {
    ::VariantClear(&var);
    return FALSE;
  }
  switch (vt) {
    case VT_UI1: *(BYTE*)pValue = var.bVal; break;
    case VT_I2: *(short*)pValue = var.iVal; break;
    case VT_I4: *(long*)pValue = var.lVal; break;
    case VT_UI4: *(ULONG*)pValue = var.ulVal; break;
    case VT_R4: *(float*)pValue = var.fltVal; break;
    case VT_R8: *(double*)pValue = var.dblVal; break;
    case VT_BOOL: *(int*)pValue = var.boolVal != VARIANT_FALSE ? 1 : 0; break;
    case VT_BSTR: *(CString*)pValue = var.bstrVal; break;
    default: ::VariantClear(&var); return FALSE;
  }
  ::VariantClear(&var);
  return TRUE;
}

void COlePropertyPage::QueueProperty(UINT nID, void* pValue, VARTYPE vt, LPCTSTR lpszProp) {
  DDPEntry* pEntry = new DDPEntry;
  pEntry->nID = nID;
  pEntry->pValue = pValue;
  pEntry->vt = vt;
  pEntry->strProp = lpszProp;
  m_ddpQueue.Add(pEntry);
}

void COlePropertyPage::ClearQueue() {
  for (INT_PTR i = 0; i < m_ddpQueue.GetSize(); i++) delete m_ddpQueue[i];
  m_ddpQueue.RemoveAll();
}

// The values of the changed controls go to every object.
BOOL COlePropertyPage::WriteProperties() {
  BOOL bOK = TRUE;
  for (INT_PTR i = 0; i < m_ddpQueue.GetSize(); i++) {
    DDPEntry* pEntry = m_ddpQueue[i];
    if (!GetControlStatus(pEntry->nID)) continue;
    VARIANT var;
    ::VariantInit(&var);
    var.vt = pEntry->vt;
    switch (pEntry->vt) {
      case VT_UI1: var.bVal = *(BYTE*)pEntry->pValue; break;
      case VT_I2: var.iVal = *(short*)pEntry->pValue; break;
      case VT_I4: var.lVal = *(long*)pEntry->pValue; break;
      case VT_UI4: var.ulVal = *(ULONG*)pEntry->pValue; break;
      case VT_R4: var.fltVal = *(float*)pEntry->pValue; break;
      case VT_R8: var.dblVal = *(double*)pEntry->pValue; break;
      case VT_BOOL: var.boolVal = *(int*)pEntry->pValue ? VARIANT_TRUE : VARIANT_FALSE; break;
      case VT_BSTR: var.bstrVal = ((CString*)pEntry->pValue)->AllocSysString(); break;
      default: continue;
    }
    for (ULONG j = 0; j < m_nObjects; j++) {
      DISPID dispid;
      if (m_ppDisp[j] == NULL || !GetDispID(m_ppDisp[j], pEntry->strProp, &dispid)) {
        bOK = FALSE;
        continue;
      }
      DISPID dispidPut = DISPID_PROPERTYPUT;
      DISPPARAMS params = {&var, &dispidPut, 1, 1};
      EXCEPINFO excep;
      memset(&excep, 0, sizeof(excep));
      HRESULT hr = m_ppDisp[j]->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT,
                                       DISPATCH_PROPERTYPUT, &params, NULL, &excep, NULL);
      if (FAILED(hr)) bOK = FALSE;
      if (excep.bstrSource != NULL) ::SysFreeString(excep.bstrSource);
      if (excep.bstrDescription != NULL) ::SysFreeString(excep.bstrDescription);
      if (excep.bstrHelpFile != NULL) ::SysFreeString(excep.bstrHelpFile);
    }
    ::VariantClear(&var);
    SetControlStatus(pEntry->nID, FALSE);
  }
  ClearQueue();
  return bOK;
}

// ---------------------------------------------------------------------
// The DDP_ functions

static COlePropertyPage* PageOf(CDataExchange* pDX) {
  CWnd* pWnd = pDX->m_pDlgWnd;
  if (pWnd == NULL || !pWnd->IsKindOf(RUNTIME_CLASS(COlePropertyPage))) return NULL;
  return (COlePropertyPage*)pWnd;
}

static void DDPExchange(CDataExchange* pDX, int id, void* pValue, VARTYPE vt, LPCTSTR pszPropName) {
  COlePropertyPage* pPage = PageOf(pDX);
  if (pPage == NULL || pszPropName == NULL) return;
  if (pDX->m_bSaveAndValidate)
    pPage->QueueProperty((UINT)id, pValue, vt, pszPropName);
  else
    pPage->ReadProperty(pszPropName, vt, pValue);
}

// The members of these types are the size of the VARIANT field.
static_assert(sizeof(int) == sizeof(long) && sizeof(UINT) == sizeof(ULONG), "32-bit int");

void AFXAPI DDP_Text(CDataExchange* pDX, int id, BYTE& member, LPCTSTR pszPropName) {
  DDPExchange(pDX, id, &member, VT_UI1, pszPropName);
}
void AFXAPI DDP_Text(CDataExchange* pDX, int id, short& member, LPCTSTR pszPropName) {
  DDPExchange(pDX, id, &member, VT_I2, pszPropName);
}
void AFXAPI DDP_Text(CDataExchange* pDX, int id, int& member, LPCTSTR pszPropName) {
  DDPExchange(pDX, id, &member, VT_I4, pszPropName);
}
void AFXAPI DDP_Text(CDataExchange* pDX, int id, UINT& member, LPCTSTR pszPropName) {
  DDPExchange(pDX, id, &member, VT_UI4, pszPropName);
}
void AFXAPI DDP_Text(CDataExchange* pDX, int id, long& member, LPCTSTR pszPropName) {
  DDPExchange(pDX, id, &member, VT_I4, pszPropName);
}
void AFXAPI DDP_Text(CDataExchange* pDX, int id, DWORD& member, LPCTSTR pszPropName) {
  DDPExchange(pDX, id, &member, VT_UI4, pszPropName);
}
void AFXAPI DDP_Text(CDataExchange* pDX, int id, float& member, LPCTSTR pszPropName) {
  DDPExchange(pDX, id, &member, VT_R4, pszPropName);
}
void AFXAPI DDP_Text(CDataExchange* pDX, int id, double& member, LPCTSTR pszPropName) {
  DDPExchange(pDX, id, &member, VT_R8, pszPropName);
}
void AFXAPI DDP_Text(CDataExchange* pDX, int id, CString& member, LPCTSTR pszPropName) {
  DDPExchange(pDX, id, &member, VT_BSTR, pszPropName);
}
void AFXAPI DDP_Check(CDataExchange* pDX, int id, int& member, LPCTSTR pszPropName) {
  DDPExchange(pDX, id, &member, VT_BOOL, pszPropName);
}
void AFXAPI DDP_Radio(CDataExchange* pDX, int id, int& member, LPCTSTR pszPropName) {
  DDPExchange(pDX, id, &member, VT_I4, pszPropName);
}
void AFXAPI DDP_LBString(CDataExchange* pDX, int id, CString& member, LPCTSTR pszPropName) {
  DDPExchange(pDX, id, &member, VT_BSTR, pszPropName);
}
void AFXAPI DDP_LBStringExact(CDataExchange* pDX, int id, CString& member, LPCTSTR pszPropName) {
  DDPExchange(pDX, id, &member, VT_BSTR, pszPropName);
}
void AFXAPI DDP_LBIndex(CDataExchange* pDX, int id, int& member, LPCTSTR pszPropName) {
  DDPExchange(pDX, id, &member, VT_I4, pszPropName);
}
void AFXAPI DDP_CBString(CDataExchange* pDX, int id, CString& member, LPCTSTR pszPropName) {
  DDPExchange(pDX, id, &member, VT_BSTR, pszPropName);
}
void AFXAPI DDP_CBStringExact(CDataExchange* pDX, int id, CString& member, LPCTSTR pszPropName) {
  DDPExchange(pDX, id, &member, VT_BSTR, pszPropName);
}
void AFXAPI DDP_CBIndex(CDataExchange* pDX, int id, int& member, LPCTSTR pszPropName) {
  DDPExchange(pDX, id, &member, VT_I4, pszPropName);
}

void AFXAPI DDP_PostProcessing(CDataExchange* pDX) {
  COlePropertyPage* pPage = PageOf(pDX);
  if (pPage == NULL) return;
  if (pDX->m_bSaveAndValidate)
    pPage->WriteProperties();
  else
    pPage->ClearQueue();
}

// ---------------------------------------------------------------------
// IPropertyPage2

STDMETHODIMP_(ULONG) COlePropertyPage::XPropertyPage::AddRef() {
  METHOD_PROLOGUE(COlePropertyPage, PropertyPage)
  return pThis->ExternalAddRef();
}
STDMETHODIMP_(ULONG) COlePropertyPage::XPropertyPage::Release() {
  METHOD_PROLOGUE(COlePropertyPage, PropertyPage)
  return pThis->ExternalRelease();
}
STDMETHODIMP COlePropertyPage::XPropertyPage::QueryInterface(REFIID iid, LPVOID* ppv) {
  METHOD_PROLOGUE(COlePropertyPage, PropertyPage)
  return (HRESULT)pThis->ExternalQueryInterface(&iid, ppv);
}

STDMETHODIMP COlePropertyPage::XPropertyPage::SetPageSite(LPPROPERTYPAGESITE pPageSite) {
  METHOD_PROLOGUE(COlePropertyPage, PropertyPage)
  if (pPageSite != NULL) pPageSite->AddRef();
  if (pThis->m_pPageSite != NULL) pThis->m_pPageSite->Release();
  pThis->m_pPageSite = pPageSite;
  pThis->OnSetPageSite();
  return S_OK;
}

STDMETHODIMP COlePropertyPage::XPropertyPage::Activate(HWND hWndParent, LPCRECT pRect, BOOL) {
  METHOD_PROLOGUE(COlePropertyPage, PropertyPage)
  if (pThis->m_hWnd != NULL) return S_OK;
  HGLOBAL hTemplate = pThis->MakeChildTemplate(NULL);
  if (hTemplate == NULL) return E_FAIL;
  LPCDLGTEMPLATE lpTemplate = (LPCDLGTEMPLATE)::GlobalLock(hTemplate);
  pThis->m_bLoading = TRUE;
  BOOL bMade = pThis->CreateDlgIndirect(lpTemplate, CWnd::FromHandle(hWndParent),
                                        AfxFindResourceHandle(pThis->m_lpszTemplateName, RT_DIALOG));
  pThis->m_bLoading = FALSE;
  ::GlobalUnlock(hTemplate);
  ::GlobalFree(hTemplate);
  if (!bMade) return E_OUTOFMEMORY;
  if (pRect != NULL)
    pThis->SetWindowPos(NULL, pRect->left, pRect->top, pRect->right - pRect->left,
                        pRect->bottom - pRect->top, SWP_NOZORDER | SWP_NOACTIVATE);
  return S_OK;
}

STDMETHODIMP COlePropertyPage::XPropertyPage::Deactivate() {
  METHOD_PROLOGUE(COlePropertyPage, PropertyPage)
  if (pThis->m_hWnd != NULL) pThis->DestroyWindow();
  return S_OK;
}

static LPOLESTR AllocOleString(const CString& str) {
  if (str.IsEmpty()) return NULL;
  CStringW strW(str);
  SIZE_T cb = (strW.GetLength() + 1) * sizeof(WCHAR);
  LPOLESTR psz = (LPOLESTR)::CoTaskMemAlloc(cb);
  if (psz != NULL) memcpy(psz, (LPCWSTR)strW, cb);
  return psz;
}

STDMETHODIMP COlePropertyPage::XPropertyPage::GetPageInfo(LPPROPPAGEINFO pPageInfo) {
  METHOD_PROLOGUE(COlePropertyPage, PropertyPage)
  if (pPageInfo == NULL) return E_POINTER;
  SIZE size = {0, 0};
  HGLOBAL hTemplate = pThis->MakeChildTemplate(&size);
  if (hTemplate == NULL) return E_FAIL;
  ::GlobalFree(hTemplate);
  pPageInfo->cb = sizeof(PROPPAGEINFO);
  pPageInfo->pszTitle = AllocOleString(pThis->m_strPageName);
  pPageInfo->size = size;
  pPageInfo->pszDocString = AllocOleString(pThis->m_strDocString);
  pPageInfo->pszHelpFile = AllocOleString(pThis->m_strHelpFile);
  pPageInfo->dwHelpContext = pThis->m_dwHelpContext;
  return S_OK;
}

STDMETHODIMP COlePropertyPage::XPropertyPage::SetObjects(ULONG cObjects, LPUNKNOWN* ppUnk) {
  METHOD_PROLOGUE(COlePropertyPage, PropertyPage)
  if (cObjects != 0 && ppUnk == NULL) return E_POINTER;
  for (ULONG i = 0; i < pThis->m_nObjects; i++)
    if (pThis->m_ppDisp[i] != NULL) pThis->m_ppDisp[i]->Release();
  delete[] pThis->m_ppDisp;
  pThis->m_ppDisp = NULL;
  pThis->m_nObjects = 0;
  if (cObjects != 0) {
    pThis->m_ppDisp = new LPDISPATCH[cObjects];
    for (ULONG i = 0; i < cObjects; i++) {
      pThis->m_ppDisp[i] = NULL;
      if (ppUnk[i] != NULL)
        ppUnk[i]->QueryInterface(IID_IDispatch, (void**)&pThis->m_ppDisp[i]);
    }
    pThis->m_nObjects = cObjects;
  }
  pThis->OnObjectsChanged();
  pThis->LoadFromObjects();
  return S_OK;
}

STDMETHODIMP COlePropertyPage::XPropertyPage::Show(UINT nCmdShow) {
  METHOD_PROLOGUE(COlePropertyPage, PropertyPage)
  if (pThis->m_hWnd == NULL) return E_UNEXPECTED;
  pThis->ShowWindow(nCmdShow);
  if (nCmdShow == SW_SHOW || nCmdShow == SW_SHOWNORMAL) {
    // The first control that takes the focus.
    CWnd* pCtl = pThis->GetNextDlgTabItem(NULL);
    if (pCtl != NULL && ::IsChild(pThis->m_hWnd, ::GetFocus()) == FALSE) pCtl->SetFocus();
  }
  return S_OK;
}

STDMETHODIMP COlePropertyPage::XPropertyPage::Move(LPCRECT pRect) {
  METHOD_PROLOGUE(COlePropertyPage, PropertyPage)
  if (pThis->m_hWnd == NULL) return E_UNEXPECTED;
  if (pRect == NULL) return E_POINTER;
  pThis->MoveWindow(pRect);
  return S_OK;
}

STDMETHODIMP COlePropertyPage::XPropertyPage::IsPageDirty() {
  METHOD_PROLOGUE(COlePropertyPage, PropertyPage)
  return pThis->m_bDirty ? S_OK : S_FALSE;
}

STDMETHODIMP COlePropertyPage::XPropertyPage::Apply() {
  METHOD_PROLOGUE(COlePropertyPage, PropertyPage)
  if (!pThis->m_bDirty) return S_OK;
  if (pThis->m_hWnd != NULL) {
    pThis->ClearQueue();
    // DDX takes the values; DDP_PostProcessing writes the changed ones.
    if (!pThis->UpdateData(TRUE)) {
      pThis->ClearQueue();
      return E_FAIL;
    }
  }
  pThis->m_dirtyIDs.RemoveAll();
  pThis->m_bDirty = FALSE;
  if (pThis->m_pPageSite != NULL) pThis->m_pPageSite->OnStatusChange(PROPPAGESTATUS_CLEAN);
  return S_OK;
}

STDMETHODIMP COlePropertyPage::XPropertyPage::Help(LPCOLESTR lpszHelpDir) {
  METHOD_PROLOGUE(COlePropertyPage, PropertyPage)
  CString strDir(lpszHelpDir != NULL ? lpszHelpDir : L"");
  if (pThis->OnHelp(strDir)) return S_OK;
  return E_NOTIMPL;
}

STDMETHODIMP COlePropertyPage::XPropertyPage::TranslateAccelerator(LPMSG lpMsg) {
  METHOD_PROLOGUE(COlePropertyPage, PropertyPage)
  if (lpMsg == NULL) return E_POINTER;
  if (pThis->m_hWnd == NULL) return S_FALSE;
  return pThis->PreTranslateMessage(lpMsg) ? S_OK : S_FALSE;
}

STDMETHODIMP COlePropertyPage::XPropertyPage::EditProperty(DISPID dispid) {
  METHOD_PROLOGUE(COlePropertyPage, PropertyPage)
  return pThis->OnEditProperty(dispid) ? S_OK : E_NOTIMPL;
}
