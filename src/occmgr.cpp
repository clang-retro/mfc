// occmgr.cpp - ActiveX control containment: COccManager, the OLE controls
// of dialog templates, the CWnd functions that host controls, and the
// DDX_OC functions.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxocc.h>

// ---------------------------------------------------------------------
// Dialog templates

namespace {

// Skips a menu, class or title field: 0, an ordinal (0xFFFF and a word)
// or a string.
const WORD* SkipField(const WORD* p) {
  if (*p == 0xFFFF) return p + 2;
  while (*p != 0) p++;
  return p + 1;
}

const BYTE* AlignDword(const BYTE* p, const BYTE* pBase) {
  return pBase + (((p - pBase) + 3) & ~3);
}

BOOL IsDialogEx(const DLGTEMPLATE* pTemplate) { return ((const WORD*)pTemplate)[1] == 0xFFFF; }

struct ItemPos {
  const BYTE* pStart;
  const BYTE* pEnd;
  const WORD* pClass;
  const WORD* pTitle;
  DWORD dwStyle;
  DWORD dwExStyle;
  short x, y, cx, cy;
  UINT nID;
};

// Walks a template: the end of its header and each item.
class TemplateWalker {
public:
  explicit TemplateWalker(const DLGTEMPLATE* pTemplate) : m_pBase((const BYTE*)pTemplate) {
    m_bEx = IsDialogEx(pTemplate);
    const BYTE* p = m_pBase;
    DWORD dwStyle;
    if (m_bEx) {
      dwStyle = *(const DWORD*)(p + 12);
      m_nItems = *(const WORD*)(p + 16);
      m_nCountOffset = 16;
      p += 26;
    } else {
      dwStyle = *(const DWORD*)p;
      m_nItems = *(const WORD*)(p + 8);
      m_nCountOffset = 8;
      p += 18;
    }
    const WORD* pw = SkipField(SkipField(SkipField((const WORD*)p)));  // menu, class, title
    if (dwStyle & DS_SETFONT) {
      pw++;  // point size
      if (m_bEx) pw += 2;  // weight, italic and charset
      pw = SkipField(pw);
    }
    m_pHeaderEnd = (const BYTE*)pw;
    m_pNext = m_pHeaderEnd;
    m_nDone = 0;
  }

  BOOL Next(ItemPos* pItem) {
    if (m_nDone >= m_nItems) return FALSE;
    const BYTE* p = AlignDword(m_pNext, m_pBase);
    pItem->pStart = p;
    if (m_bEx) {
      pItem->dwExStyle = *(const DWORD*)(p + 4);
      pItem->dwStyle = *(const DWORD*)(p + 8);
      pItem->x = *(const short*)(p + 12);
      pItem->y = *(const short*)(p + 14);
      pItem->cx = *(const short*)(p + 16);
      pItem->cy = *(const short*)(p + 18);
      pItem->nID = *(const DWORD*)(p + 20);
      p += 24;
    } else {
      pItem->dwStyle = *(const DWORD*)p;
      pItem->dwExStyle = *(const DWORD*)(p + 4);
      pItem->x = *(const short*)(p + 8);
      pItem->y = *(const short*)(p + 10);
      pItem->cx = *(const short*)(p + 12);
      pItem->cy = *(const short*)(p + 14);
      pItem->nID = *(const WORD*)(p + 16);
      p += 18;
    }
    pItem->pClass = (const WORD*)p;
    pItem->pTitle = SkipField(pItem->pClass);
    const WORD* pw = SkipField(pItem->pTitle);
    WORD cbExtra = *pw++;
    pItem->pEnd = (const BYTE*)pw + cbExtra;
    m_pNext = pItem->pEnd;
    m_nDone++;
    return TRUE;
  }

  const BYTE* m_pBase;
  const BYTE* m_pHeaderEnd;
  const BYTE* m_pNext;
  BOOL m_bEx;
  int m_nItems;
  int m_nDone;
  int m_nCountOffset;
};

// An OLE control's class is its CLSID in braces.
BOOL ItemClsid(const ItemPos& item, CLSID* pClsid) {
  if (*item.pClass == 0xFFFF || *item.pClass != L'{') return FALSE;
  CStringW strClass((LPCWSTR)item.pClass);
  return SUCCEEDED(::CLSIDFromString((LPOLESTR)(LPCWSTR)strClass, pClsid));
}

}  // namespace

static COccManager* g_pOccManager = NULL;

COccManager* AFXAPI _AfxGetOccManager() {
  static COccManager s_defaultManager;
  return g_pOccManager != NULL ? g_pOccManager : &s_defaultManager;
}

COleControlContainer* COccManager::CreateContainer(CWnd* pWnd) {
  return new COleControlContainer(pWnd);
}

COleControlSite* COccManager::CreateSite(COleControlContainer* pCtrlCont) {
  return new COleControlSite(pCtrlCont);
}

BOOL COccManager::OnEvent(CCmdTarget* pCmdTarget, UINT idCtrl, AFX_EVENT* pEvent,
                          AFX_CMDHANDLERINFO* pHandlerInfo) {
  return pCmdTarget->OnCmdMsg(idCtrl, (int)CN_EVENT, pEvent, pHandlerInfo);
}

DLGTEMPLATE* PASCAL COccManager::SplitDialogTemplate(const DLGTEMPLATE* pTemplate,
                                                     _AFX_OCC_DIALOG_INFO* pInfo) {
  TemplateWalker walker(pTemplate);
  pInfo->m_pItemInfo = NULL;
  pInfo->m_pnIndex = NULL;
  pInfo->m_cItems = 0;
  pInfo->m_cAllItems = walker.m_nItems;
  pInfo->m_pNewTemplate = NULL;
  // Count the controls first.
  ItemPos item;
  int nOle = 0;
  const BYTE* pEnd = walker.m_pHeaderEnd;
  {
    TemplateWalker count(pTemplate);
    CLSID clsid;
    while (count.Next(&item)) {
      if (ItemClsid(item, &clsid)) nOle++;
      pEnd = item.pEnd;
    }
  }
  if (nOle == 0) return NULL;
  pInfo->m_pItemInfo = new _AFX_OCC_DIALOG_INFO::ItemInfo[nOle];
  pInfo->m_pnIndex = new int[nOle];

  // The new template: the header, then the other items, each aligned.
  size_t cbMax = (size_t)(pEnd - walker.m_pBase) + 4 * (size_t)walker.m_nItems + 4;
  BYTE* pNew = new BYTE[cbMax];
  memset(pNew, 0, cbMax);
  size_t cbHeader = (size_t)(walker.m_pHeaderEnd - walker.m_pBase);
  memcpy(pNew, walker.m_pBase, cbHeader);
  BYTE* pOut = pNew + cbHeader;
  int nIndex = 0, nKept = 0;
  while (walker.Next(&item)) {
    CLSID clsid;
    if (ItemClsid(item, &clsid)) {
      _AFX_OCC_DIALOG_INFO::ItemInfo& info = pInfo->m_pItemInfo[pInfo->m_cItems];
      info.nID = item.nID;
      info.clsid = clsid;
      info.bAutoRadioButton = FALSE;
      info.dwStyle = item.dwStyle;
      info.dwExStyle = item.dwExStyle;
      info.rect.SetRect(item.x, item.y, item.x + item.cx, item.y + item.cy);
      if (*item.pTitle != 0xFFFF) info.strText = CString((LPCWSTR)item.pTitle);
      pInfo->m_pnIndex[pInfo->m_cItems] = nIndex;
      pInfo->m_cItems++;
    } else {
      pOut = pNew + (((pOut - pNew) + 3) & ~3);
      size_t cb = (size_t)(item.pEnd - item.pStart);
      memcpy(pOut, item.pStart, cb);
      pOut += cb;
      nKept++;
    }
    nIndex++;
  }
  *(WORD*)(pNew + walker.m_nCountOffset) = (WORD)nKept;
  pInfo->m_pNewTemplate = (DLGTEMPLATE*)pNew;
  return (DLGTEMPLATE*)pNew;
}

const DLGTEMPLATE* COccManager::PreCreateDialog(_AFX_OCC_DIALOG_INFO* pInfo,
                                                const DLGTEMPLATE* pOrigTemplate) {
  DLGTEMPLATE* pNew = SplitDialogTemplate(pOrigTemplate, pInfo);
  return pNew != NULL ? pNew : pOrigTemplate;
}

void COccManager::PostCreateDialog(_AFX_OCC_DIALOG_INFO* pInfo) {
  delete[] (BYTE*)pInfo->m_pNewTemplate;
  delete[] pInfo->m_pItemInfo;
  delete[] pInfo->m_pnIndex;
  pInfo->m_pNewTemplate = NULL;
  pInfo->m_pItemInfo = NULL;
  pInfo->m_pnIndex = NULL;
  pInfo->m_cItems = 0;
}

BOOL COccManager::CreateDlgControls(CWnd* pWndParent, LPCTSTR lpszResourceName,
                                    _AFX_OCC_DIALOG_INFO* pInfo) {
  void* lpResource = NULL;
  if (lpszResourceName != NULL) {
    HINSTANCE hInst = AfxFindResourceHandle(lpszResourceName, RT_DLGINIT);
    HRSRC hRsrc = ::FindResource(hInst, lpszResourceName, RT_DLGINIT);
    if (hRsrc != NULL) lpResource = ::LockResource(::LoadResource(hInst, hRsrc));
  }
  return CreateDlgControls(pWndParent, lpResource, pInfo);
}

// The saved state of a control in the DLGINIT resource.
static const BYTE* FindDlgInit(void* lpResource, UINT nID, WORD* pnMsg, DWORD* pdwLen) {
  if (lpResource == NULL) return NULL;
  const BYTE* p = (const BYTE*)lpResource;
  for (;;) {
    WORD nIDC = *(const WORD*)p;
    if (nIDC == 0) return NULL;
    WORD nMsg = *(const WORD*)(p + 2);
    DWORD dwLen = *(const DWORD*)(p + 4);
    p += 8;
    if (nIDC == nID && (nMsg == WM_OCC_LOADFROMSTREAM || nMsg == WM_OCC_LOADFROMSTORAGE ||
                        nMsg == WM_OCC_INITNEW || nMsg == WM_OCC_LOADFROMSTREAM_EX ||
                        nMsg == WM_OCC_LOADFROMSTORAGE_EX)) {
      *pnMsg = nMsg;
      *pdwLen = dwLen;
      return p;
    }
    p += dwLen;
  }
}

// A data binding a control's DLGINIT record asks for, made once all the
// dialog's controls are there.
struct _AFX_DLG_BINDING {
  UINT nIDCtrl;
  UINT nIDDSC;  // the data source control
  DISPID dispid;
  VARTYPE vt;
  BOOL bSimple;  // a property bound to a field, not given the data source
  CString strField;
};

// The binding records written for DISPID_DATASOURCE (simple binding to a
// field) and other properties (given the data source), at the start of
// a WM_OCC_*_EX record's data.
#define _AFX_DISPID_DATASOURCE ((DISPID)0x80010001L)
#define _AFX_DISPID_DATAFIELD ((DISPID)0x80010002L)

// Reads a control's DLGINIT data: its license key, then (for the _EX
// records) its bindings, leaving pData and cb on its saved state.
static BSTR ReadDlgInitHeader(UINT nID, WORD* pnMsg, const BYTE** ppData, DWORD* pcb,
                              CPtrArray* pBindings) {
  const BYTE* p = *ppData;
  DWORD cb = *pcb;
  BSTR bstrLicKey = NULL;
  if (cb >= sizeof(ULONG)) {
    ULONG cchKey;
    memcpy(&cchKey, p, sizeof(cchKey));
    p += sizeof(ULONG);
    cb -= sizeof(ULONG);
    if (cchKey > 0 && cchKey * sizeof(WCHAR) <= cb) {
      bstrLicKey = ::SysAllocStringLen((const OLECHAR*)p, cchKey);
      p += cchKey * sizeof(WCHAR);
      cb -= cchKey * sizeof(WCHAR);
    }
  }
  if ((*pnMsg == WM_OCC_LOADFROMSTREAM_EX || *pnMsg == WM_OCC_LOADFROMSTORAGE_EX) &&
      cb >= 3 * sizeof(DWORD)) {
    DWORD cbSection;  // with itself
    memcpy(&cbSection, p, sizeof(cbSection));
    if (cbSection > cb || cbSection < 3 * sizeof(DWORD)) cbSection = cb;
    const BYTE* pEnd = p + cbSection;
    const BYTE* q = p + 3 * sizeof(DWORD);  // the size, the flags and the bindings' size
    while (q + sizeof(DISPID) + sizeof(WORD) <= pEnd) {
      _AFX_DLG_BINDING binding;
      binding.nIDCtrl = nID;
      DISPID dispid;
      WORD nIDDSC;
      memcpy(&dispid, q, sizeof(dispid));
      memcpy(&nIDDSC, q + sizeof(DISPID), sizeof(nIDDSC));
      q += sizeof(DISPID) + sizeof(WORD);
      binding.nIDDSC = nIDDSC;
      if (dispid == _AFX_DISPID_DATASOURCE) {
        // The bound property, its type, DISPID_DATAFIELD and the field's
        // name (a length, then the ANSI name ending in a nul).
        if (q + 4 * sizeof(DWORD) > pEnd) break;
        DWORD dwType;
        memcpy(&binding.dispid, q, sizeof(DISPID));
        memcpy(&dwType, q + sizeof(DISPID), sizeof(DWORD));
        q += 4 * sizeof(DWORD);
        const BYTE* pName = q;
        while (q < pEnd && *q != 0) q++;
        binding.strField = CString(CStringA((LPCSTR)pName, (int)(q - pName)));
        if (q < pEnd) q++;
        binding.vt = (VARTYPE)dwType;
        binding.bSimple = TRUE;
      } else {
        binding.dispid = dispid;
        binding.vt = VT_UNKNOWN;
        binding.bSimple = FALSE;
      }
      if (nIDDSC != 0) pBindings->Add(new _AFX_DLG_BINDING(binding));
    }
    p += cbSection;
    cb -= cbSection;
    *pnMsg = (WORD)(*pnMsg - (WM_OCC_LOADFROMSTREAM_EX - WM_OCC_LOADFROMSTREAM));
  }
  *ppData = p;
  *pcb = cb;
  return bstrLicKey;
}

BOOL COccManager::CreateDlgControls(CWnd* pWndParent, void* lpResource,
                                    _AFX_OCC_DIALOG_INFO* pInfo) {
  if (pInfo == NULL || pInfo->m_cItems == 0) return TRUE;
  if (!pWndParent->InitControlContainer()) return FALSE;
  // The windows of the other items, in order, to put the controls among.
  CArray<HWND, HWND> children;
  for (HWND h = ::GetWindow(pWndParent->m_hWnd, GW_CHILD); h != NULL; h = ::GetWindow(h, GW_HWNDNEXT))
    children.Add(h);
  CArray<HWND, HWND> made;
  made.SetSize(pInfo->m_cItems);
  CPtrArray bindings;  // of _AFX_DLG_BINDING
  BOOL bOK = TRUE;
  for (int i = 0; i < pInfo->m_cItems; i++) {
    _AFX_OCC_DIALOG_INFO::ItemInfo& item = pInfo->m_pItemInfo[i];
    made[i] = NULL;
    CRect rect(item.rect);
    ::MapDialogRect(pWndParent->m_hWnd, &rect);
    WORD nMsg = WM_OCC_INITNEW;
    DWORD dwLen = 0;
    const BYTE* pData = FindDlgInit(lpResource, item.nID, &nMsg, &dwLen);
    BSTR bstrLicKey = NULL;
    if (pData != NULL) bstrLicKey = ReadDlgInitHeader(item.nID, &nMsg, &pData, &dwLen, &bindings);
    CMemFile memFile;
    CFile* pPersist = NULL;
    BOOL bStorage = FALSE;
    if (pData != NULL && (nMsg == WM_OCC_LOADFROMSTREAM || nMsg == WM_OCC_LOADFROMSTORAGE)) {
      memFile.Attach((BYTE*)pData, dwLen);
      pPersist = &memFile;
      bStorage = nMsg == WM_OCC_LOADFROMSTORAGE;
    }
    COleControlSite* pSite = NULL;
    if (!pWndParent->m_pCtrlCont->CreateControl(NULL, item.clsid, item.strText, item.dwStyle,
                                                rect, item.nID, pPersist, bStorage,
                                                bstrLicKey, &pSite)) {
      TRACE(_T("Warning: a dialog's ActiveX control %u was not created.\n"), item.nID);
      if (bstrLicKey != NULL) ::SysFreeString(bstrLicKey);
      bOK = FALSE;
      continue;
    }
    if (bstrLicKey != NULL) ::SysFreeString(bstrLicKey);
    if (pPersist != NULL) memFile.Detach();
    made[i] = pSite->m_hWnd;
    if (pSite->m_hWnd == NULL) continue;
    // Its place in the tab order: after the item before it.
    int nIndex = pInfo->m_pnIndex[i];
    HWND hWndAfter = HWND_TOP;
    if (nIndex > 0) {
      if (i > 0 && pInfo->m_pnIndex[i - 1] == nIndex - 1) {
        hWndAfter = made[i - 1];
      } else {
        int nOtherBefore = nIndex - i;  // the items before it that are not controls
        if (nOtherBefore > 0 && nOtherBefore <= children.GetSize())
          hWndAfter = children[nOtherBefore - 1];
      }
    }
    if (hWndAfter != NULL)
      ::SetWindowPos(pSite->m_hWnd, hWndAfter, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
  // The bindings, now that the data source controls are there.
  COleControlContainer* pCont = pWndParent->m_pCtrlCont;
  for (int i = 0; i < bindings.GetSize(); i++) {
    _AFX_DLG_BINDING* pb = (_AFX_DLG_BINDING*)bindings[i];
    COleControlSite* pSite = pCont != NULL ? pCont->FindItem(pb->nIDCtrl) : NULL;
    COleControlSite* pDSCSite = pCont != NULL ? pCont->FindItem(pb->nIDDSC) : NULL;
    if (pSite == NULL || pDSCSite == NULL) {
      TRACE(_T("Warning: a dialog's data binding names a control that is not there.\n"));
    } else {
      CWnd* pDSCWnd = pCont->GetDlgItem((int)pb->nIDDSC);
      if (pb->bSimple)
        pSite->BindDefaultProperty(pb->dispid, pb->vt, pb->strField, pDSCWnd);
      else
        pSite->BindProperty(pb->dispid, pDSCWnd);
    }
    delete pb;
  }
  return bOK;
}

BOOL COccManager::IsDialogMessage(CWnd* pWndDlg, LPMSG lpMsg) {
  // The UI-active control gets its keys first.
  COleControlContainer* pCont = pWndDlg->m_pCtrlCont;
  if (pCont != NULL && pCont->m_pActiveObject != NULL && lpMsg->message >= WM_KEYFIRST &&
      lpMsg->message <= WM_KEYLAST && pCont->m_pActiveObject->TranslateAccelerator(lpMsg) == S_OK)
    return TRUE;
  return ::IsDialogMessage(pWndDlg->m_hWnd, lpMsg);
}

// ---------------------------------------------------------------------
// The hooks CWnd calls once containment is on

namespace {

struct FindDlgInitData {
  const void* pTemplate;
  void* pDlgInit;
};

BOOL CALLBACK FindDlgInitProc(HMODULE hModule, LPCTSTR, LPTSTR lpszName, LONG_PTR lParam) {
  FindDlgInitData* pData = (FindDlgInitData*)lParam;
  HRSRC hRsrc = ::FindResource(hModule, lpszName, RT_DIALOG);
  if (hRsrc == NULL || ::LockResource(::LoadResource(hModule, hRsrc)) != pData->pTemplate)
    return TRUE;
  HRSRC hInit = ::FindResource(hModule, lpszName, RT_DLGINIT);
  if (hInit != NULL) pData->pDlgInit = ::LockResource(::LoadResource(hModule, hInit));
  return FALSE;
}

const DLGTEMPLATE* AFXAPI HookPreCreateDialog(CWnd* pWnd, const DLGTEMPLATE* pTemplate,
                                              HINSTANCE hInst) {
  _AFX_OCC_DIALOG_INFO* pInfo = new _AFX_OCC_DIALOG_INFO;
  memset(pInfo, 0, sizeof(*pInfo));
  const DLGTEMPLATE* pNew = _AfxGetOccManager()->PreCreateDialog(pInfo, pTemplate);
  if (pInfo->m_cItems == 0) {
    _AfxGetOccManager()->PostCreateDialog(pInfo);
    delete pInfo;
    return pTemplate;
  }
  // The DLGINIT resource of the same name holds the controls' state.
  FindDlgInitData data = {pTemplate, NULL};
  if (hInst != NULL)
    ::EnumResourceNames(hInst, RT_DIALOG, (ENUMRESNAMEPROC)FindDlgInitProc, (LONG_PTR)&data);
  pInfo->m_pDlgInit = data.pDlgInit;
  pWnd->m_pOccDialogInfo = pInfo;
  return pNew;
}

void AFXAPI HookPostCreateDialog(CWnd* pWnd) {
  _AFX_OCC_DIALOG_INFO* pInfo = pWnd->m_pOccDialogInfo;
  if (pInfo == NULL) return;
  pWnd->m_pOccDialogInfo = NULL;
  _AfxGetOccManager()->PostCreateDialog(pInfo);
  delete pInfo;
}

void AFXAPI HookInitDialog(CWnd* pWnd) {
  _AFX_OCC_DIALOG_INFO* pInfo = pWnd->m_pOccDialogInfo;
  if (pInfo == NULL || pInfo->m_bCreated) return;
  pInfo->m_bCreated = TRUE;
  _AfxGetOccManager()->CreateDlgControls(pWnd, pInfo->m_pDlgInit, pInfo);
}

void AFXAPI HookDestroy(CWnd* pWnd) {
  if (pWnd->m_pCtrlCont != NULL) pWnd->m_pCtrlCont->DestroyAll();
}

void AFXAPI HookNcDestroy(CWnd* pWnd) {
  COleControlContainer* pCont = pWnd->m_pCtrlCont;
  if (pCont != NULL) {
    pWnd->m_pCtrlCont = NULL;
    pCont->DestroyAll();
    pCont->m_pWnd = NULL;
    pCont->InternalRelease();
  }
  COleControlSite* pSite = pWnd->m_pCtrlSite;
  if (pSite != NULL && pSite->m_pWndCtrl == pWnd) {
    // The control's window is going: the wrapper lets go of it.
    pSite->m_bSubclassed = FALSE;
    pSite->m_pWndCtrl = NULL;
  }
  pWnd->m_pCtrlSite = NULL;
}

BOOL AFXAPI HookDestroyControl(CWnd* pWnd) {
  COleControlSite* pSite = pWnd->m_pCtrlSite;
  if (pSite == NULL) return FALSE;
  return pSite->DestroyControl();
}

BOOL AFXAPI HookDDXControl(CWnd* pDlgWnd, int nIDC, CWnd& rControl) {
  if (pDlgWnd->m_pCtrlCont == NULL) return FALSE;
  COleControlSite* pSite = pDlgWnd->m_pCtrlCont->FindItem((UINT)nIDC);
  if (pSite == NULL) return FALSE;
  pDlgWnd->m_pCtrlCont->AttachControlSite(&rControl, (UINT)nIDC);
  return TRUE;
}

BOOL AFXAPI HookHasWindowless(CWnd* pWnd) {
  return pWnd->m_pCtrlCont != NULL && pWnd->m_pCtrlCont->m_nWindowlessControls > 0;
}

BOOL AFXAPI HookWindowlessMessage(CWnd* pWnd, UINT message, WPARAM wParam, LPARAM lParam,
                                  LRESULT* plResult) {
  return pWnd->m_pCtrlCont->HandleWindowlessMessage(message, wParam, lParam, plResult);
}

void AFXAPI HookPaintWindowless(CWnd* pWnd, HRGN hRgnUpdate) {
  HDC hDC = ::GetDC(pWnd->m_hWnd);
  if (hDC == NULL) return;
  ::SelectClipRgn(hDC, hRgnUpdate);
  CDC dc;
  dc.Attach(hDC);
  pWnd->m_pCtrlCont->OnPaint(&dc);
  dc.Detach();
  ::ReleaseDC(pWnd->m_hWnd, hDC);
}

LPDROPTARGET AFXAPI HookWindowlessDropTarget(CWnd* pWnd, CPoint point) {
  return pWnd->m_pCtrlCont != NULL ? pWnd->m_pCtrlCont->GetWindowlessDropTarget(point) : NULL;
}

const _AFX_OCC_HOOKS s_hooks = {HookPreCreateDialog,   HookPostCreateDialog, HookInitDialog,
                                HookDestroy,           HookNcDestroy,        HookDestroyControl,
                                HookDDXControl,        HookHasWindowless,    HookWindowlessMessage,
                                HookPaintWindowless,   HookWindowlessDropTarget};

}  // namespace

void AFXAPI AfxEnableControlContainer(COccManager* pOccManager) {
  if (pOccManager != NULL) g_pOccManager = pOccManager;
  _afxOccHooks = &s_hooks;
}

// ---------------------------------------------------------------------
// CWnd

BOOL CWnd::InitControlContainer() {
  if (m_pCtrlCont == NULL) {
    _afxOccHooks = &s_hooks;  // so that the window cleans up its controls
    m_pCtrlCont = _AfxGetOccManager()->CreateContainer(this);
  }
  return m_pCtrlCont != NULL;
}

BOOL CWnd::CreateControlSite(COleControlContainer*, COleControlSite** ppSite, UINT, REFCLSID) {
  if (ppSite != NULL) *ppSite = NULL;
  return FALSE;
}

BOOL CWnd::CreateControl(REFCLSID clsid, LPCTSTR pszWindowName, DWORD dwStyle, const RECT& rect,
                         CWnd* pParentWnd, UINT nID, CFile* pPersist, BOOL bStorage,
                         BSTR bstrLicKey) {
  POINT pt = {rect.left, rect.top};
  SIZE size = {rect.right - rect.left, rect.bottom - rect.top};
  return CreateControl(clsid, pszWindowName, dwStyle, &pt, &size, pParentWnd, nID, pPersist,
                       bStorage, bstrLicKey);
}

BOOL CWnd::CreateControl(LPCTSTR pszClass, LPCTSTR pszWindowName, DWORD dwStyle,
                         const RECT& rect, CWnd* pParentWnd, UINT nID, CFile* pPersist,
                         BOOL bStorage, BSTR bstrLicKey) {
  if (pszClass == NULL) return FALSE;
  CStringW strClass(pszClass);
  CLSID clsid;
  HRESULT hr = strClass.GetLength() > 0 && strClass[0] == L'{'
                   ? ::CLSIDFromString((LPOLESTR)(LPCWSTR)strClass, &clsid)
                   : ::CLSIDFromProgID(strClass, &clsid);
  if (FAILED(hr)) return FALSE;
  return CreateControl(clsid, pszWindowName, dwStyle, rect, pParentWnd, nID, pPersist, bStorage,
                       bstrLicKey);
}

BOOL CWnd::CreateControl(REFCLSID clsid, LPCTSTR pszWindowName, DWORD dwStyle, const POINT* ppt,
                         const SIZE* psize, CWnd* pParentWnd, UINT nID, CFile* pPersist,
                         BOOL bStorage, BSTR bstrLicKey) {
  if (pParentWnd == NULL || pParentWnd->m_hWnd == NULL) return FALSE;
  if (!pParentWnd->InitControlContainer()) return FALSE;
  return pParentWnd->m_pCtrlCont->CreateControl(this, clsid, pszWindowName, dwStyle, ppt, psize,
                                                nID, pPersist, bStorage, bstrLicKey, NULL);
}

LPUNKNOWN CWnd::GetControlUnknown() {
  return m_pCtrlSite != NULL ? m_pCtrlSite->m_pObject : NULL;
}

void AFX_CDECL CWnd::InvokeHelper(DISPID dwDispID, WORD wFlags, VARTYPE vtRet, void* pvRet,
                                  const BYTE* pbParamInfo, ...) {
  if (m_pCtrlSite == NULL) AfxThrowNotSupportedException();
  va_list argList;
  va_start(argList, pbParamInfo);
  try {
    m_pCtrlSite->InvokeHelperV(dwDispID, wFlags, vtRet, pvRet, pbParamInfo, argList);
  } catch (...) {
    va_end(argList);
    throw;
  }
  va_end(argList);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wvarargs"
void AFX_CDECL CWnd::SetProperty(DISPID dwDispID, VARTYPE vtProp, ...) {
  if (m_pCtrlSite == NULL) AfxThrowNotSupportedException();
  va_list argList;
  va_start(argList, vtProp);
  try {
    m_pCtrlSite->SetPropertyV(dwDispID, vtProp, argList);
  } catch (...) {
    va_end(argList);
    throw;
  }
  va_end(argList);
}
#pragma clang diagnostic pop

void CWnd::GetProperty(DISPID dwDispID, VARTYPE vtProp, void* pvProp) const {
  if (m_pCtrlSite == NULL) AfxThrowNotSupportedException();
  m_pCtrlSite->GetProperty(dwDispID, vtProp, pvProp);
}

// ---------------------------------------------------------------------
// DDX_OC: the properties of a control in a dialog

static COleControlSite* DDXSite(CDataExchange* pDX, int nIDC) {
  COleControlContainer* pCont = pDX->m_pDlgWnd->m_pCtrlCont;
  COleControlSite* pSite = pCont != NULL ? pCont->FindItem((UINT)nIDC) : NULL;
  if (pSite == NULL) AfxThrowNotSupportedException();
  return pSite;
}

template <class T>
static void DDXProp(CDataExchange* pDX, int nIDC, DISPID dispid, VARTYPE vt, T& value,
                    BOOL bReadOnly) {
  COleControlSite* pSite = DDXSite(pDX, nIDC);
  if (pDX->m_bSaveAndValidate)
    pSite->GetProperty(dispid, vt, &value);
  else if (!bReadOnly)
    pSite->SetProperty(dispid, vt, value);
}

void AFXAPI DDX_OCText(CDataExchange* pDX, int nIDC, DISPID dispid, CString& value) {
  COleControlSite* pSite = DDXSite(pDX, nIDC);
  if (pDX->m_bSaveAndValidate)
    pSite->GetProperty(dispid, VT_BSTR, &value);
  else
    pSite->SetProperty(dispid, VT_BSTR, (LPCTSTR)value);
}
void AFXAPI DDX_OCTextRO(CDataExchange* pDX, int nIDC, DISPID dispid, CString& value) {
  if (pDX->m_bSaveAndValidate) DDXSite(pDX, nIDC)->GetProperty(dispid, VT_BSTR, &value);
}
void AFXAPI DDX_OCBool(CDataExchange* pDX, int nIDC, DISPID dispid, BOOL& value) {
  DDXProp(pDX, nIDC, dispid, VT_BOOL, value, FALSE);
}
void AFXAPI DDX_OCBoolRO(CDataExchange* pDX, int nIDC, DISPID dispid, BOOL& value) {
  DDXProp(pDX, nIDC, dispid, VT_BOOL, value, TRUE);
}
void AFXAPI DDX_OCInt(CDataExchange* pDX, int nIDC, DISPID dispid, int& value) {
  long n = value;
  DDXProp(pDX, nIDC, dispid, VT_I4, n, FALSE);
  value = (int)n;
}
void AFXAPI DDX_OCInt(CDataExchange* pDX, int nIDC, DISPID dispid, long& value) {
  DDXProp(pDX, nIDC, dispid, VT_I4, value, FALSE);
}
void AFXAPI DDX_OCIntRO(CDataExchange* pDX, int nIDC, DISPID dispid, int& value) {
  long n = value;
  DDXProp(pDX, nIDC, dispid, VT_I4, n, TRUE);
  value = (int)n;
}
void AFXAPI DDX_OCIntRO(CDataExchange* pDX, int nIDC, DISPID dispid, long& value) {
  DDXProp(pDX, nIDC, dispid, VT_I4, value, TRUE);
}
void AFXAPI DDX_OCShort(CDataExchange* pDX, int nIDC, DISPID dispid, short& value) {
  COleControlSite* pSite = DDXSite(pDX, nIDC);
  if (pDX->m_bSaveAndValidate)
    pSite->GetProperty(dispid, VT_I2, &value);
  else
    pSite->SetProperty(dispid, VT_I2, (int)value);
}
void AFXAPI DDX_OCShortRO(CDataExchange* pDX, int nIDC, DISPID dispid, short& value) {
  if (pDX->m_bSaveAndValidate) DDXSite(pDX, nIDC)->GetProperty(dispid, VT_I2, &value);
}
void AFXAPI DDX_OCColor(CDataExchange* pDX, int nIDC, DISPID dispid, OLE_COLOR& value) {
  DDXProp(pDX, nIDC, dispid, VT_I4, value, FALSE);
}
void AFXAPI DDX_OCColorRO(CDataExchange* pDX, int nIDC, DISPID dispid, OLE_COLOR& value) {
  DDXProp(pDX, nIDC, dispid, VT_I4, value, TRUE);
}
void AFXAPI DDX_OCFloat(CDataExchange* pDX, int nIDC, DISPID dispid, float& value) {
  COleControlSite* pSite = DDXSite(pDX, nIDC);
  if (pDX->m_bSaveAndValidate)
    pSite->GetProperty(dispid, VT_R4, &value);
  else
    pSite->SetProperty(dispid, VT_R4, (double)value);  // promoted through the ...
}
void AFXAPI DDX_OCFloat(CDataExchange* pDX, int nIDC, DISPID dispid, double& value) {
  DDXProp(pDX, nIDC, dispid, VT_R8, value, FALSE);
}
void AFXAPI DDX_OCFloatRO(CDataExchange* pDX, int nIDC, DISPID dispid, float& value) {
  if (pDX->m_bSaveAndValidate) DDXSite(pDX, nIDC)->GetProperty(dispid, VT_R4, &value);
}
void AFXAPI DDX_OCFloatRO(CDataExchange* pDX, int nIDC, DISPID dispid, double& value) {
  DDXProp(pDX, nIDC, dispid, VT_R8, value, TRUE);
}
