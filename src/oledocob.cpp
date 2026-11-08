// oledocob.cpp - Active Documents: CDocObjectServer (a server document's
// IOleDocument, IOleDocumentView, IOleCommandTarget and IPrint),
// CDocObjectServerItem, and the container's COleDocObjectItem.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxole.h>
#include <afxdocob.h>

IMPLEMENT_DYNAMIC(CDocObjectServer, CCmdTarget)
IMPLEMENT_DYNAMIC(CDocObjectServerItem, COleServerItem)
IMPLEMENT_DYNAMIC(COleDocObjectItem, COleClientItem)

// ---------------------------------------------------------------------
// CDocObjectServer

BEGIN_INTERFACE_MAP(CDocObjectServer, CCmdTarget)
  INTERFACE_PART(CDocObjectServer, IID_IOleDocument, OleDocument)
  INTERFACE_PART(CDocObjectServer, IID_IOleDocumentView, OleDocumentView)
  INTERFACE_PART(CDocObjectServer, IID_IOleCommandTarget, OleCommandTarget)
  INTERFACE_PART(CDocObjectServer, IID_IPrint, Print)
END_INTERFACE_MAP()

// The standard commands, as the frameworks' command IDs.
const OLECMDMAP* PASCAL CDocObjectServer::_GetBaseCommandMap() { return NULL; }
const OLECMDMAP* CDocObjectServer::GetCommandMap() const { return &CDocObjectServer::commandMap; }
const OLECMDMAP CDocObjectServer::commandMap = {&CDocObjectServer::_GetBaseCommandMap,
                                                &CDocObjectServer::_commandEntries[0]};
const OLECMDMAP_ENTRY CDocObjectServer::_commandEntries[] = {
    ON_OLECMD_NEW() ON_OLECMD_OPEN() ON_OLECMD_SAVE() ON_OLECMD_SAVE_AS()
    ON_OLECMD_SAVE_COPY_AS() ON_OLECMD_PRINT() ON_OLECMD_PRINTPREVIEW() ON_OLECMD_PAGESETUP()
    ON_OLECMD_CUT() ON_OLECMD_COPY() ON_OLECMD_PASTE() ON_OLECMD_PASTESPECIAL()
    ON_OLECMD_UNDO() ON_OLECMD_REDO() ON_OLECMD_SELECTALL() ON_OLECMD_CLEARSELECTION()
    {NULL, 0, 0}};

CDocObjectServer::CDocObjectServer(COleServerDoc* pOwner, LPOLEDOCUMENTSITE pDocSite)
    : m_pOwner(pOwner), m_pDocSite(NULL), m_pViewSite(NULL), m_rectView(0, 0, 0, 0),
      m_nFirstPage(1) {
  SetDocSite(pDocSite);
}

CDocObjectServer::~CDocObjectServer() {
  SetDocSite(NULL);
  if (m_pViewSite != NULL) m_pViewSite->Release();
  if (m_pOwner != NULL && m_pOwner->m_pDocObjectServer == this)
    m_pOwner->m_pDocObjectServer = NULL;
}

void CDocObjectServer::SetDocSite(LPOLEDOCUMENTSITE pDocSite) {
  if (pDocSite != NULL) pDocSite->AddRef();
  if (m_pDocSite != NULL) m_pDocSite->Release();
  m_pDocSite = pDocSite;
}

void CDocObjectServer::ActivateDocObject() {
  if (m_pDocSite == NULL) return;
  // The site makes the view (CreateView) and shows it.
  m_pOwner->InternalAddRef();
  HRESULT hr = m_pDocSite->ActivateMe(NULL);
  m_pOwner->InternalRelease();
  if (FAILED(hr)) AfxThrowOleException(hr);
}

void CDocObjectServer::OnSaveViewState(CArchive&) {}
void CDocObjectServer::OnApplyViewState(CArchive&) {}
void CDocObjectServer::OnActivateView() {}

CWnd* CDocObjectServer::GetCommandWnd() const {
  if (m_pOwner->m_pInPlaceFrame != NULL) return m_pOwner->m_pInPlaceFrame;
  POSITION pos = m_pOwner->GetFirstViewPosition();
  if (pos == NULL) return NULL;
  CView* pView = m_pOwner->GetNextView(pos);
  CFrameWnd* pFrame = pView->GetParentFrame();
  return pFrame != NULL ? (CWnd*)pFrame : (CWnd*)pView;
}

// IOleDocument

STDMETHODIMP_(ULONG) CDocObjectServer::XOleDocument::AddRef() {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleDocument)
  return pThis->m_pOwner->ExternalAddRef();
}

STDMETHODIMP_(ULONG) CDocObjectServer::XOleDocument::Release() {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleDocument)
  return pThis->m_pOwner->ExternalRelease();
}

STDMETHODIMP CDocObjectServer::XOleDocument::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleDocument)
  return (HRESULT)pThis->m_pOwner->ExternalQueryInterface(&iid, ppvObj);
}

// The document has one view, the one it shows in place.
STDMETHODIMP CDocObjectServer::XOleDocument::CreateView(LPOLEINPLACESITE pIPSite, LPSTREAM pstm,
                                                        DWORD, LPOLEDOCUMENTVIEW* ppView) {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleDocument)
  if (ppView == NULL) return E_POINTER;
  *ppView = NULL;
  LPOLEDOCUMENTVIEW lpView = (LPOLEDOCUMENTVIEW)&pThis->m_xOleDocumentView;
  if (pThis->m_pViewSite != NULL && pIPSite != NULL && pThis->m_pViewSite != pIPSite)
    return E_FAIL;
  HRESULT hr = S_OK;
  if (pIPSite != NULL) hr = lpView->SetInPlaceSite(pIPSite);
  if (SUCCEEDED(hr) && pstm != NULL) hr = lpView->ApplyViewState(pstm);
  if (FAILED(hr)) return hr;
  lpView->AddRef();
  *ppView = lpView;
  return S_OK;
}

STDMETHODIMP CDocObjectServer::XOleDocument::GetDocMiscStatus(LPDWORD pdwStatus) {
  if (pdwStatus == NULL) return E_POINTER;
  *pdwStatus = 0;
  return S_OK;
}

STDMETHODIMP CDocObjectServer::XOleDocument::EnumViews(LPENUMOLEDOCUMENTVIEWS* ppEnum,
                                                       LPOLEDOCUMENTVIEW* ppView) {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleDocument)
  if (ppEnum == NULL || ppView == NULL) return E_POINTER;
  *ppEnum = NULL;
  *ppView = (LPOLEDOCUMENTVIEW)&pThis->m_xOleDocumentView;
  (*ppView)->AddRef();
  return S_OK;
}

// IOleDocumentView: the document's in-place activation, in the rectangle
// the container gives.

STDMETHODIMP_(ULONG) CDocObjectServer::XOleDocumentView::AddRef() {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleDocumentView)
  return pThis->m_pOwner->ExternalAddRef();
}

STDMETHODIMP_(ULONG) CDocObjectServer::XOleDocumentView::Release() {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleDocumentView)
  return pThis->m_pOwner->ExternalRelease();
}

STDMETHODIMP CDocObjectServer::XOleDocumentView::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleDocumentView)
  return (HRESULT)pThis->m_pOwner->ExternalQueryInterface(&iid, ppvObj);
}

STDMETHODIMP CDocObjectServer::XOleDocumentView::SetInPlaceSite(LPOLEINPLACESITE pIPSite) {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleDocumentView)
  if (pIPSite == pThis->m_pViewSite) return S_OK;
  HRESULT hr = S_OK;
  try {
    // A new site, or none: out of the old one first.
    if (pThis->m_pOwner->IsInPlaceActive()) pThis->m_pOwner->OnDeactivate();
  } catch (CException* e) {
    hr = COleException::Process(e);
    e->Delete();
  }
  if (pIPSite != NULL) pIPSite->AddRef();
  if (pThis->m_pViewSite != NULL) pThis->m_pViewSite->Release();
  pThis->m_pViewSite = pIPSite;
  return hr;
}

STDMETHODIMP CDocObjectServer::XOleDocumentView::GetInPlaceSite(LPOLEINPLACESITE* ppIPSite) {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleDocumentView)
  if (ppIPSite == NULL) return E_POINTER;
  *ppIPSite = pThis->m_pViewSite;
  if (*ppIPSite != NULL) (*ppIPSite)->AddRef();
  return S_OK;
}

STDMETHODIMP CDocObjectServer::XOleDocumentView::GetDocument(LPUNKNOWN* ppunk) {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleDocumentView)
  if (ppunk == NULL) return E_POINTER;
  return (HRESULT)pThis->m_pOwner->ExternalQueryInterface(&IID_IUnknown, (LPVOID*)ppunk);
}

STDMETHODIMP CDocObjectServer::XOleDocumentView::SetRect(LPRECT prcView) {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleDocumentView)
  if (prcView == NULL) return E_POINTER;
  pThis->m_rectView = *prcView;
  COleIPFrameWnd* pFrame = pThis->m_pOwner->m_pInPlaceFrame;
  if (pFrame != NULL) pFrame->RepositionFrame(prcView, prcView);
  return S_OK;
}

STDMETHODIMP CDocObjectServer::XOleDocumentView::GetRect(LPRECT prcView) {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleDocumentView)
  if (prcView == NULL) return E_POINTER;
  *prcView = pThis->m_rectView;
  return S_OK;
}

STDMETHODIMP CDocObjectServer::XOleDocumentView::SetRectComplex(LPRECT prcView, LPRECT, LPRECT,
                                                                 LPRECT) {
  // The view keeps its own scroll bars.
  return prcView != NULL ? SetRect(prcView) : E_POINTER;
}

STDMETHODIMP CDocObjectServer::XOleDocumentView::Show(BOOL fShow) {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleDocumentView)
  HRESULT hr = S_OK;
  try {
    COleServerDoc* pDoc = pThis->m_pOwner;
    if (fShow) {
      if (!pDoc->IsInPlaceActive()) {
        if (pThis->m_pViewSite == NULL || !pDoc->ActivateInPlace()) return E_FAIL;
        if (!pThis->m_rectView.IsRectEmpty())
          pDoc->m_pInPlaceFrame->RepositionFrame(&pThis->m_rectView, &pThis->m_rectView);
      }
      pDoc->m_pInPlaceFrame->ShowWindow(SW_SHOW);
      pThis->OnActivateView();
    } else if (pDoc->m_pInPlaceFrame != NULL) {
      pDoc->m_pInPlaceFrame->ShowWindow(SW_HIDE);
    }
  } catch (CException* e) {
    hr = COleException::Process(e);
    e->Delete();
  }
  return hr;
}

STDMETHODIMP CDocObjectServer::XOleDocumentView::UIActivate(BOOL fUIActivate) {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleDocumentView)
  HRESULT hr = S_OK;
  try {
    COleServerDoc* pDoc = pThis->m_pOwner;
    if (fUIActivate) {
      // Active in place first, then the menus and bars.
      if (!pDoc->IsInPlaceActive()) {
        if (pThis->m_pViewSite == NULL || !pDoc->ActivateInPlace()) return E_FAIL;
      } else if (!pDoc->UIActivate()) {
        hr = E_FAIL;
      }
    } else if (pDoc->IsInPlaceActive()) {
      pDoc->OnDeactivateUI(FALSE);
    }
  } catch (CException* e) {
    hr = COleException::Process(e);
    e->Delete();
  }
  return hr;
}

STDMETHODIMP CDocObjectServer::XOleDocumentView::Open() { return E_NOTIMPL; }

STDMETHODIMP CDocObjectServer::XOleDocumentView::CloseView(DWORD) {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleDocumentView)
  // Out of the container, and no longer its view.
  return SetInPlaceSite(NULL);
}

STDMETHODIMP CDocObjectServer::XOleDocumentView::SaveViewState(LPSTREAM pstm) {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleDocumentView)
  if (pstm == NULL) return E_POINTER;
  HRESULT hr = S_OK;
  COleStreamFile file;
  file.Attach(pstm);
  try {
    CArchive ar(&file, CArchive::store);
    pThis->OnSaveViewState(ar);
    ar.Close();
  } catch (CException* e) {
    hr = COleException::Process(e);
    e->Delete();
  }
  file.Detach();
  return hr;
}

STDMETHODIMP CDocObjectServer::XOleDocumentView::ApplyViewState(LPSTREAM pstm) {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleDocumentView)
  if (pstm == NULL) return E_POINTER;
  HRESULT hr = S_OK;
  COleStreamFile file;
  file.Attach(pstm);
  try {
    CArchive ar(&file, CArchive::load);
    pThis->OnApplyViewState(ar);
    ar.Close();
  } catch (CException* e) {
    hr = COleException::Process(e);
    e->Delete();
  }
  file.Detach();
  return hr;
}

STDMETHODIMP CDocObjectServer::XOleDocumentView::Clone(LPOLEINPLACESITE,
                                                        LPOLEDOCUMENTVIEW* ppViewNew) {
  if (ppViewNew != NULL) *ppViewNew = NULL;
  return E_NOTIMPL;
}

// IOleCommandTarget: the command map's commands, sent to the view's frame
// as its own menu commands would be.

static UINT CommandID(const OLECMDMAP* pMap, const GUID* pguid, ULONG cmdID, BOOL& bGroup) {
  bGroup = FALSE;
  for (; pMap != NULL; pMap = pMap->pfnGetBaseMap != NULL ? pMap->pfnGetBaseMap() : NULL) {
    for (const OLECMDMAP_ENTRY* p = pMap->lpEntries; p->cmdID != 0 || p->nID != 0; p++) {
      BOOL bSame = (pguid == NULL && p->pguid == NULL) ||
                   (pguid != NULL && p->pguid != NULL && ::IsEqualGUID(*pguid, *p->pguid));
      if (!bSame) continue;
      bGroup = TRUE;
      if (p->cmdID == cmdID) return p->nID;
    }
  }
  return 0;
}

// ---------------------------------------------------------------------
// COleCmdUI

COleCmdUI::COleCmdUI(OLECMD* rgCmds, ULONG cCmds, const GUID* pGroup)
    : m_nCmdTextFlag(0), m_rgCmds(rgCmds), m_pGuidCmdGroup(pGroup) {
  m_nIndexMax = cCmds;
}

void COleCmdUI::Enable(BOOL bOn) {
  if (m_rgCmds != NULL && m_nIndex < m_nIndexMax) {
    if (bOn)
      m_rgCmds[m_nIndex].cmdf |= OLECMDF_ENABLED;
    else
      m_rgCmds[m_nIndex].cmdf &= ~OLECMDF_ENABLED;
  }
  m_bEnableChanged = TRUE;
}

// 1 is latched, 2 (indeterminate) is ninched.
void COleCmdUI::SetCheck(int nCheck) {
  if (m_rgCmds == NULL || m_nIndex >= m_nIndexMax) return;
  OLECMD& cmd = m_rgCmds[m_nIndex];
  cmd.cmdf &= ~(OLECMDF_LATCHED | OLECMDF_NINCHED);
  if (nCheck == 1)
    cmd.cmdf |= OLECMDF_LATCHED;
  else if (nCheck == 2)
    cmd.cmdf |= OLECMDF_NINCHED;
}

void COleCmdUI::SetText(LPCTSTR lpszText) { m_strText = lpszText; }

BOOL COleCmdUI::DoUpdate(CCmdTarget* pTarget, BOOL bDisableIfNoHandler) {
  if (m_rgCmds == NULL || m_nIndex >= m_nIndexMax) return FALSE;
  m_rgCmds[m_nIndex].cmdf = OLECMDF_SUPPORTED;
  if (pTarget == NULL) return TRUE;
  m_bEnableChanged = FALSE;
  BOOL bUpdated = pTarget->OnCmdMsg(m_nID, CN_UPDATE_COMMAND_UI, this, NULL);
  if (!bUpdated || !m_bEnableChanged) {
    // No update handler: a command handler enables it.
    AFX_CMDHANDLERINFO info;
    info.pTarget = NULL;
    BOOL bHandler = pTarget->OnCmdMsg(m_nID, CN_COMMAND, NULL, &info);
    if (bHandler || !bDisableIfNoHandler) Enable(TRUE);
  }
  return bUpdated;
}

STDMETHODIMP_(ULONG) CDocObjectServer::XOleCommandTarget::AddRef() {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleCommandTarget)
  return pThis->m_pOwner->ExternalAddRef();
}

STDMETHODIMP_(ULONG) CDocObjectServer::XOleCommandTarget::Release() {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleCommandTarget)
  return pThis->m_pOwner->ExternalRelease();
}

STDMETHODIMP CDocObjectServer::XOleCommandTarget::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleCommandTarget)
  return (HRESULT)pThis->m_pOwner->ExternalQueryInterface(&iid, ppvObj);
}

STDMETHODIMP CDocObjectServer::XOleCommandTarget::QueryStatus(const GUID* pguidCmdGroup,
                                                               ULONG cCmds, OLECMD rgCmds[],
                                                               OLECMDTEXT* pcmdtext) {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleCommandTarget)
  if (rgCmds == NULL) return E_POINTER;
  if (pcmdtext != NULL) pcmdtext->cwActual = 0;
  CWnd* pWnd = pThis->GetCommandWnd();
  BOOL bAnyGroup = FALSE;
  COleCmdUI state(rgCmds, cCmds, pguidCmdGroup);
  if (pcmdtext != NULL) state.m_nCmdTextFlag = pcmdtext->cmdtextf;
  for (ULONG i = 0; i < cCmds; i++) {
    BOOL bGroup;
    UINT nID = CommandID(pThis->GetCommandMap(), pguidCmdGroup, rgCmds[i].cmdID, bGroup);
    bAnyGroup |= bGroup;
    rgCmds[i].cmdf = 0;
    if (nID == 0) continue;
    state.m_nIndex = i;
    state.m_nID = nID;
    if (pWnd == NULL)
      rgCmds[i].cmdf = OLECMDF_SUPPORTED;
    else
      state.DoUpdate(pWnd, TRUE);
  }
  // The text the first command's update handler gave.
  if (pcmdtext != NULL && pcmdtext->cmdtextf != OLECMDTEXTF_NONE && !state.m_strText.IsEmpty()) {
    CStringW strText(state.m_strText);
    ULONG cw = (ULONG)strText.GetLength() + 1;
    pcmdtext->cwActual = cw;
    if (pcmdtext->cwBuf > 0) {
      ULONG n = cw <= pcmdtext->cwBuf ? cw : pcmdtext->cwBuf;
      memcpy(pcmdtext->rgwz, (LPCWSTR)strText, (n - 1) * sizeof(WCHAR));
      pcmdtext->rgwz[n - 1] = 0;
    }
  }
  if (pguidCmdGroup != NULL && !bAnyGroup) return OLECMDERR_E_UNKNOWNGROUP;
  return S_OK;
}

STDMETHODIMP CDocObjectServer::XOleCommandTarget::Exec(const GUID* pguidCmdGroup, DWORD nCmdID,
                                                        DWORD, VARIANTARG*, VARIANTARG*) {
  METHOD_PROLOGUE_EX_(CDocObjectServer, OleCommandTarget)
  BOOL bGroup;
  UINT nID = CommandID(pThis->GetCommandMap(), pguidCmdGroup, nCmdID, bGroup);
  if (nID == 0) return bGroup || pguidCmdGroup == NULL ? OLECMDERR_E_NOTSUPPORTED
                                                       : OLECMDERR_E_UNKNOWNGROUP;
  CWnd* pWnd = pThis->GetCommandWnd();
  if (pWnd == NULL) return OLECMDERR_E_DISABLED;
  OLECMD cmd = {nCmdID, 0};
  COleCmdUI state(&cmd, 1, pguidCmdGroup);
  state.m_nIndex = 0;
  state.m_nID = nID;
  state.DoUpdate(pWnd, TRUE);
  if (!(cmd.cmdf & OLECMDF_ENABLED)) return OLECMDERR_E_DISABLED;
  pThis->m_pOwner->InternalAddRef();
  pWnd->SendMessage(WM_COMMAND, nID);
  pThis->m_pOwner->InternalRelease();
  return S_OK;
}

// IPrint: the document's view prints its pages, as it does for the
// File Print command, to the printer the container names.

#ifndef PRINT_E_CANCELLED
#define PRINT_E_CANCELLED ((HRESULT)0x80040160L)
#endif
#ifndef PRINT_E_NOSUCHPAGE
#define PRINT_E_NOSUCHPAGE ((HRESULT)0x80040161L)
#endif

STDMETHODIMP_(ULONG) CDocObjectServer::XPrint::AddRef() {
  METHOD_PROLOGUE_EX_(CDocObjectServer, Print)
  return pThis->m_pOwner->ExternalAddRef();
}

STDMETHODIMP_(ULONG) CDocObjectServer::XPrint::Release() {
  METHOD_PROLOGUE_EX_(CDocObjectServer, Print)
  return pThis->m_pOwner->ExternalRelease();
}

STDMETHODIMP CDocObjectServer::XPrint::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE_EX_(CDocObjectServer, Print)
  return (HRESULT)pThis->m_pOwner->ExternalQueryInterface(&iid, ppvObj);
}

STDMETHODIMP CDocObjectServer::XPrint::SetInitialPageNum(LONG nFirstPage) {
  METHOD_PROLOGUE_EX_(CDocObjectServer, Print)
  pThis->m_nFirstPage = nFirstPage;
  return S_OK;
}

static CView* PrintingView(COleServerDoc* pDoc) {
  POSITION pos = pDoc->GetFirstViewPosition();
  return pos != NULL ? pDoc->GetNextView(pos) : NULL;
}

// The pages the view says it has, after OnPreparePrinting.
static BOOL KnownPages(CPrintInfo* pInfo, UINT* pnMin, UINT* pnMax) {
  *pnMin = pInfo->GetMinPage();
  *pnMax = pInfo->GetMaxPage();
  return *pnMax != 0xFFFF && *pnMax >= *pnMin;
}

STDMETHODIMP CDocObjectServer::XPrint::GetPageInfo(LPLONG pnFirstPage, LPLONG pcPages) {
  METHOD_PROLOGUE_EX_(CDocObjectServer, Print)
  if (pnFirstPage != NULL) *pnFirstPage = pThis->m_nFirstPage;
  if (pcPages != NULL) *pcPages = 0;
  CView* pView = PrintingView(pThis->m_pOwner);
  if (pView == NULL) return E_UNEXPECTED;
  CPrintInfo info;
  info.m_bDocObject = TRUE;
  info.m_dwFlags = PRINTFLAG_DONTACTUALLYPRINT;
  info.m_nOffsetPage = (UINT)(pThis->m_nFirstPage - 1);
  pView->OnPreparePrinting(&info);  // the page count is set whether or not there is a printer
  UINT nMin, nMax;
  if (!KnownPages(&info, &nMin, &nMax)) return E_NOTIMPL;  // known only by printing
  if (pcPages != NULL) *pcPages = (LONG)(nMax - nMin + 1);
  return S_OK;
}

// Whether page nPage (counted from 1 within the document) is among those
// the page set asks for.
static BOOL InPageSet(const PAGESET* pSet, LONG nPage) {
  if (pSet == NULL) return TRUE;
  if ((nPage & 1) ? !pSet->fOddPages : !pSet->fEvenPages) return FALSE;
  if (pSet->cPageRange == 0) return TRUE;
  for (ULONG i = 0; i < pSet->cPageRange; i++) {
    const PAGERANGE& r = pSet->rgPages[i];
    if (nPage >= r.nFromPage && (r.nToPage == PAGESET_TOLASTPAGE || nPage <= r.nToPage))
      return TRUE;
  }
  return FALSE;
}

static HDC CreateTargetDC(const DVTARGETDEVICE* ptd) {
  if (ptd == NULL || ptd->tdDeviceNameOffset == 0) return NULL;
  const BYTE* pBase = (const BYTE*)ptd;
  LPCWSTR pszDriver = ptd->tdDriverNameOffset ? (LPCWSTR)(pBase + ptd->tdDriverNameOffset) : NULL;
  LPCWSTR pszDevice = (LPCWSTR)(pBase + ptd->tdDeviceNameOffset);
  const DEVMODEW* pdm =
      ptd->tdExtDevmodeOffset ? (const DEVMODEW*)(pBase + ptd->tdExtDevmodeOffset) : NULL;
  return ::CreateDCW(pszDriver, pszDevice, NULL, pdm);
}

STDMETHODIMP CDocObjectServer::XPrint::Print(DWORD grfFlags, DVTARGETDEVICE** pptd,
                                             PAGESET** ppPageSet, LPSTGMEDIUM pstgmOptions,
                                             LPCONTINUECALLBACK pCallback, LONG nFirstPage,
                                             LPLONG pcPagesPrinted, LPLONG pnLastPage) {
  METHOD_PROLOGUE_EX_(CDocObjectServer, Print)
  if (pcPagesPrinted != NULL) *pcPagesPrinted = 0;
  if (pnLastPage != NULL) *pnLastPage = nFirstPage - 1;
  CView* pView = PrintingView(pThis->m_pOwner);
  if (pView == NULL) return E_UNEXPECTED;
  const PAGESET* pSet = ppPageSet != NULL ? *ppPageSet : NULL;
  BOOL bPrint = !(grfFlags & PRINTFLAG_DONTACTUALLYPRINT);

  CPrintInfo info;
  info.m_bDocObject = TRUE;
  info.m_dwFlags = grfFlags;
  info.m_nOffsetPage = (UINT)(nFirstPage - 1);
  // The container's printer, unless the user is to choose one.
  if (pptd != NULL && *pptd != NULL && !(grfFlags & PRINTFLAG_PROMPTUSER))
    info.m_pPD->m_pd.hDC = CreateTargetDC(*pptd);
  BOOL bPrepared = pView->OnPreparePrinting(&info);
  if (bPrint && !bPrepared)
    return (grfFlags & PRINTFLAG_PROMPTUSER) ? PRINT_E_CANCELLED : E_FAIL;
  HDC hDC = info.m_pPD->m_pd.hDC;
  if (bPrint && hDC == NULL) return E_FAIL;

  UINT nMin, nMax;
  BOOL bKnown = KnownPages(&info, &nMin, &nMax);
  if (!bKnown) {
    nMin = info.GetMinPage();
    nMax = 0xFFFF;  // until the view stops
  }
  if (pSet != NULL && bKnown) {
    BOOL bAny = FALSE;
    for (UINT n = nMin; n <= nMax && !bAny; n++) bAny = InPageSet(pSet, (LONG)(n - nMin + 1));
    if (!bAny) return PRINT_E_NOSUCHPAGE;
  }

  if (!bPrint) {
    // Only the count: the pages that would print.
    LONG nCount = 0, nLast = nFirstPage - 1;
    UINT nEnd = bKnown ? nMax : nMin;
    for (UINT n = nMin; n <= nEnd; n++) {
      LONG nPage = (LONG)(n - nMin + 1);
      if (!InPageSet(pSet, nPage)) continue;
      nCount++;
      nLast = nFirstPage + nPage - 1;
    }
    if (pcPagesPrinted != NULL) *pcPagesPrinted = nCount;
    if (pnLastPage != NULL) *pnLastPage = nLast;
    return S_OK;
  }

  CString strTitle = pThis->m_pOwner->GetTitle();
  if (strTitle.GetLength() > 31) strTitle = strTitle.Left(31);
  CString strOutput;
  if ((grfFlags & PRINTFLAG_PRINTTOFILE) && pstgmOptions != NULL &&
      pstgmOptions->tymed == TYMED_FILE && pstgmOptions->lpszFileName != NULL)
    strOutput = pstgmOptions->lpszFileName;
  DOCINFO docInfo;
  memset(&docInfo, 0, sizeof(docInfo));
  docInfo.cbSize = sizeof(docInfo);
  docInfo.lpszDocName = strTitle;
  docInfo.lpszOutput = strOutput.IsEmpty() ? NULL : (LPCTSTR)strOutput;

  CDC dcPrint;
  dcPrint.Attach(hDC);
  dcPrint.m_bPrinting = TRUE;
  pView->OnBeginPrinting(&dcPrint, &info);
  HRESULT hr = S_OK;
  LONG nCount = 0, nLast = nFirstPage - 1;
  if (dcPrint.StartDoc(&docInfo) == SP_ERROR) {
    hr = E_FAIL;
  } else {
    for (info.m_nCurPage = nMin; info.m_nCurPage <= nMax; info.m_nCurPage++) {
      LONG nPage = (LONG)(info.m_nCurPage - nMin + 1);
      if (!InPageSet(pSet, nPage)) continue;
      pView->OnPrepareDC(&dcPrint, &info);
      if (!info.m_bContinuePrinting) break;
      info.m_rectDraw.SetRect(0, 0, dcPrint.GetDeviceCaps(HORZRES),
                              dcPrint.GetDeviceCaps(VERTRES));
      dcPrint.DPtoLP(&info.m_rectDraw);
      if (dcPrint.StartPage() < 0) {
        hr = E_FAIL;
        break;
      }
      pView->OnPrepareDC(&dcPrint, &info);
      pView->OnPrint(&dcPrint, &info);
      if (dcPrint.EndPage() < 0) {
        hr = E_FAIL;
        break;
      }
      nCount++;
      nLast = nFirstPage + nPage - 1;
      if (pCallback != NULL && pCallback->FContinuePrinting(nCount, nLast, NULL) == S_FALSE) {
        hr = PRINT_E_CANCELLED;
        break;
      }
      if (info.m_nCurPage == 0xFFFF) break;
    }
    if (hr == S_OK)
      dcPrint.EndDoc();
    else
      dcPrint.AbortDoc();
  }
  pView->OnEndPrinting(&dcPrint, &info);
  dcPrint.Detach();
  if (pcPagesPrinted != NULL) *pcPagesPrinted = nCount;
  if (pnLastPage != NULL) *pnLastPage = nLast;
  return hr;
}

// ---------------------------------------------------------------------
// COleServerDoc's part

void COleServerDoc::ActivateDocObject() {
  if (m_pDocObjectServer != NULL) m_pDocObjectServer->ActivateDocObject();
}

// ---------------------------------------------------------------------
// CDocObjectServerItem

CDocObjectServerItem::CDocObjectServerItem(COleServerDoc* pServerDoc, BOOL bAutoDelete)
    : COleServerItem(pServerDoc, bAutoDelete) {}

CDocObjectServerItem::~CDocObjectServerItem() {}

void CDocObjectServerItem::OnDoVerb(LONG iVerb) {
  COleServerDoc* pDoc = GetDocument();
  // Every showing verb shows a DocObject as a whole document.
  if (pDoc != NULL && pDoc->IsDocObject() &&
      (iVerb == OLEIVERB_PRIMARY || iVerb == OLEIVERB_SHOW || iVerb == OLEIVERB_OPEN ||
       iVerb == OLEIVERB_UIACTIVATE || iVerb == OLEIVERB_INPLACEACTIVATE)) {
    OnShow();
    return;
  }
  COleServerItem::OnDoVerb(iVerb);
}

void CDocObjectServerItem::OnShow() {
  COleServerDoc* pDoc = GetDocument();
  if (pDoc != NULL && pDoc->IsDocObject())
    pDoc->ActivateDocObject();
  else
    COleServerItem::OnShow();
}

void CDocObjectServerItem::OnHide() {
  COleServerDoc* pDoc = GetDocument();
  if (pDoc != NULL && pDoc->IsDocObject()) {
    CDocObjectServer* pServer = pDoc->m_pDocObjectServer;
    LPOLEDOCUMENTVIEW lpView = (LPOLEDOCUMENTVIEW)&pServer->m_xOleDocumentView;
    lpView->UIActivate(FALSE);
    lpView->Show(FALSE);
    return;
  }
  COleServerItem::OnHide();
}

// ---------------------------------------------------------------------
// COleDocObjectItem

BEGIN_INTERFACE_MAP(COleDocObjectItem, COleClientItem)
  INTERFACE_PART(COleDocObjectItem, IID_IOleDocumentSite, OleDocumentSite)
END_INTERFACE_MAP()

COleDocObjectItem::COleDocObjectItem(COleDocument* pContainerDoc)
    : COleClientItem(pContainerDoc), m_pActiveView(NULL), m_pIPrint(NULL), m_bInClose(FALSE) {}

COleDocObjectItem::~COleDocObjectItem() { CloseView(); }

void COleDocObjectItem::OnGetItemPosition(CRect& rPosition) {
  if (m_pView != NULL && m_pView->m_hWnd != NULL)
    m_pView->GetClientRect(&rPosition);
  else
    COleClientItem::OnGetItemPosition(rPosition);
}

HRESULT COleDocObjectItem::ActivateView(LPOLEDOCUMENTVIEW pView) {
  if (pView == NULL) return E_POINTER;
  if (m_pActiveView != pView) {
    pView->AddRef();
    if (m_pActiveView != NULL) m_pActiveView->Release();
    m_pActiveView = pView;
  }
  if (m_pIPrint == NULL && m_lpObject != NULL)
    m_lpObject->QueryInterface(IID_IPrint, (void**)&m_pIPrint);
  HRESULT hr = pView->UIActivate(TRUE);
  if (FAILED(hr)) return hr;
  CRect rect;
  OnGetItemPosition(rect);
  pView->SetRect(&rect);
  return pView->Show(TRUE);
}

void COleDocObjectItem::CloseView() {
  if (m_pIPrint != NULL) {
    m_pIPrint->Release();
    m_pIPrint = NULL;
  }
  if (m_pActiveView == NULL || m_bInClose) return;
  m_bInClose = TRUE;
  LPOLEDOCUMENTVIEW pView = m_pActiveView;
  pView->UIActivate(FALSE);
  pView->Show(FALSE);
  pView->CloseView(0);
  pView->SetInPlaceSite(NULL);
  m_pActiveView = NULL;
  pView->Release();
  m_bInClose = FALSE;
}

void COleDocObjectItem::Release(OLECLOSE dwCloseOption) {
  CloseView();
  COleClientItem::Release(dwCloseOption);
}

HRESULT COleDocObjectItem::ExecCommand(DWORD nCmdID, DWORD nCmdExecOpt,
                                       const GUID* pguidCmdGroup) {
  if (m_lpObject == NULL) return E_UNEXPECTED;
  LPOLECOMMANDTARGET lpTarget = NULL;
  HRESULT hr = m_lpObject->QueryInterface(IID_IOleCommandTarget, (void**)&lpTarget);
  if (FAILED(hr)) return hr;
  hr = lpTarget->Exec(pguidCmdGroup, nCmdID, nCmdExecOpt, NULL, NULL);
  lpTarget->Release();
  return hr;
}

HRESULT COleDocObjectItem::QueryCommand(ULONG nCmdID, DWORD* pdwStatus, OLECMDTEXT* pCmdText,
                                        const GUID* pguidCmdGroup) {
  if (pdwStatus == NULL) return E_POINTER;
  *pdwStatus = 0;
  if (m_lpObject == NULL) return E_UNEXPECTED;
  LPOLECOMMANDTARGET lpTarget = NULL;
  HRESULT hr = m_lpObject->QueryInterface(IID_IOleCommandTarget, (void**)&lpTarget);
  if (FAILED(hr)) return hr;
  OLECMD cmd = {nCmdID, 0};
  hr = lpTarget->QueryStatus(pguidCmdGroup, 1, &cmd, pCmdText);
  lpTarget->Release();
  if (SUCCEEDED(hr)) *pdwStatus = cmd.cmdf;
  return hr;
}

BOOL COleDocObjectItem::GetPageCount(LPLONG pnFirstPage, LPLONG pcPages) {
  if (m_pIPrint == NULL && m_lpObject != NULL)
    m_lpObject->QueryInterface(IID_IPrint, (void**)&m_pIPrint);
  if (m_pIPrint == NULL) return FALSE;
  LONG nFirst = 1, nPages = 0;
  if (FAILED(m_pIPrint->GetPageInfo(&nFirst, &nPages))) return FALSE;
  if (pnFirstPage != NULL) *pnFirstPage = nFirst;
  if (pcPages != NULL) *pcPages = nPages;
  return TRUE;
}

DVTARGETDEVICE* AFXAPI _AfxOleCreateTargetDevice(const PRINTDLG* ppd);

void COleDocObjectItem::DoDefaultPrinting(CView*, CPrintInfo* pInfo) {
  if (m_pIPrint == NULL && m_lpObject != NULL)
    m_lpObject->QueryInterface(IID_IPrint, (void**)&m_pIPrint);
  if (m_pIPrint == NULL) {
    // Without IPrint, its own Print command.
    ExecCommand(OLECMDID_PRINT, OLECMDEXECOPT_DONTPROMPTUSER);
    return;
  }
  // The pages and the printer the container's print job has.
  DVTARGETDEVICE* ptd = pInfo != NULL ? _AfxOleCreateTargetDevice(&pInfo->m_pPD->m_pd) : NULL;
  PAGESET pageSet;
  memset(&pageSet, 0, sizeof(pageSet));
  pageSet.cbStruct = sizeof(pageSet);
  pageSet.fOddPages = pageSet.fEvenPages = TRUE;
  pageSet.cPageRange = 1;
  pageSet.rgPages[0].nFromPage = 1;
  pageSet.rgPages[0].nToPage = PAGESET_TOLASTPAGE;
  if (pInfo != NULL && (pInfo->m_pPD->m_pd.Flags & PD_PAGENUMS)) {
    pageSet.rgPages[0].nFromPage = (LONG)pInfo->GetFromPage();
    pageSet.rgPages[0].nToPage = (LONG)pInfo->GetToPage();
  }
  PAGESET* pSet = &pageSet;
  LONG nPrinted = 0, nLast = 0;
  m_pIPrint->Print(PRINTFLAG_MAYBOTHERUSER, ptd != NULL ? &ptd : NULL, &pSet, NULL, NULL,
                   pInfo != NULL ? (LONG)pInfo->m_nOffsetPage + 1 : 1, &nPrinted, &nLast);
  if (ptd != NULL) ::CoTaskMemFree(ptd);
  if (pInfo != NULL) pInfo->m_bContinuePrinting = FALSE;  // the object printed every page
}

// The DocObject active in the view, if any.
static COleDocObjectItem* ActiveDocObject(CView* pView) {
  if (pView == NULL || !pView->GetDocument()->IsKindOf(RUNTIME_CLASS(COleDocument))) return NULL;
  COleDocument* pDoc = (COleDocument*)pView->GetDocument();
  POSITION pos = pDoc->GetStartPosition();
  while (pos != NULL) {
    CDocItem* pItem = pDoc->GetNextItem(pos);
    if (pItem->IsKindOf(RUNTIME_CLASS(COleDocObjectItem)) &&
        ((COleDocObjectItem*)pItem)->m_pView == pView &&
        ((COleDocObjectItem*)pItem)->GetActiveView() != NULL)
      return (COleDocObjectItem*)pItem;
  }
  return NULL;
}

BOOL COleDocObjectItem::OnPreparePrinting(CView* pCaller, CPrintInfo* pInfo, BOOL) {
  COleDocObjectItem* pItem = ActiveDocObject(pCaller);
  if (pItem == NULL || pInfo == NULL) return FALSE;
  LONG nFirst = 1, nPages = 0;
  if (pItem->GetPageCount(&nFirst, &nPages) && nPages > 0) {
    pInfo->SetMinPage((UINT)nFirst);
    pInfo->SetMaxPage((UINT)(nFirst + nPages - 1));
  }
  return TRUE;
}

void COleDocObjectItem::OnPrint(CView* pCaller, CPrintInfo* pInfo, BOOL) {
  COleDocObjectItem* pItem = ActiveDocObject(pCaller);
  if (pItem != NULL) pItem->DoDefaultPrinting(pCaller, pInfo);
}

// IOleDocumentSite

STDMETHODIMP_(ULONG) COleDocObjectItem::XOleDocumentSite::AddRef() {
  METHOD_PROLOGUE_EX_(COleDocObjectItem, OleDocumentSite)
  return pThis->ExternalAddRef();
}

STDMETHODIMP_(ULONG) COleDocObjectItem::XOleDocumentSite::Release() {
  METHOD_PROLOGUE_EX_(COleDocObjectItem, OleDocumentSite)
  return pThis->ExternalRelease();
}

STDMETHODIMP COleDocObjectItem::XOleDocumentSite::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE_EX_(COleDocObjectItem, OleDocumentSite)
  return (HRESULT)pThis->ExternalQueryInterface(&iid, ppvObj);
}

// The object asks to be shown: a view of it, made here if it names none,
// in the item's view.
STDMETHODIMP COleDocObjectItem::XOleDocumentSite::ActivateMe(LPOLEDOCUMENTVIEW pViewToActivate) {
  METHOD_PROLOGUE_EX_(COleDocObjectItem, OleDocumentSite)
  LPOLEINPLACESITE lpSite = (LPOLEINPLACESITE)pThis->GetInterface(&IID_IOleInPlaceSite);
  HRESULT hr;
  if (pViewToActivate == NULL) {
    if (pThis->m_pActiveView != NULL) return pThis->ActivateView(pThis->m_pActiveView);
    if (pThis->m_lpObject == NULL) return E_UNEXPECTED;
    LPOLEDOCUMENT lpDoc = NULL;
    hr = pThis->m_lpObject->QueryInterface(IID_IOleDocument, (void**)&lpDoc);
    if (FAILED(hr)) return hr;
    LPOLEDOCUMENTVIEW lpView = NULL;
    hr = lpDoc->CreateView(lpSite, NULL, 0, &lpView);
    lpDoc->Release();
    if (FAILED(hr)) return hr;
    hr = pThis->ActivateView(lpView);
    lpView->Release();
    return hr;
  }
  hr = pViewToActivate->SetInPlaceSite(lpSite);
  if (FAILED(hr)) return hr;
  return pThis->ActivateView(pViewToActivate);
}

// ---------------------------------------------------------------------
// COleDocIPFrameWnd

IMPLEMENT_DYNCREATE(COleDocIPFrameWnd, COleIPFrameWnd)

COleDocIPFrameWnd::COleDocIPFrameWnd() : m_hMenuHelpPopup(NULL), m_hHelpPopup(NULL) {}

COleDocIPFrameWnd::~COleDocIPFrameWnd() {}

// A container that keeps its Help menu (group 5) gets the object's Help
// popup at the top of it; the rest of the object's menu goes in as for any
// in-place object.
BOOL COleDocIPFrameWnd::BuildSharedMenu() {
  DestroySharedMenu();
  CDocument* pDoc = GetActiveDocument();
  CDocTemplate* pTemplate = pDoc != NULL ? pDoc->GetDocTemplate() : NULL;
  HMENU hServer = pTemplate != NULL ? pTemplate->m_hMenuInPlaceServer : NULL;
  m_hSharedMenu = ::CreateMenu();
  if (m_hSharedMenu == NULL) return FALSE;
  OLEMENUGROUPWIDTHS widths;
  memset(&widths, 0, sizeof(widths));
  if (m_lpFrame == NULL || FAILED(m_lpFrame->InsertMenus(m_hSharedMenu, &widths)))
    memset(&widths, 0, sizeof(widths));
  LONG nHelp = widths.width[5];
  widths.width[1] = widths.width[3] = widths.width[5] = 0;
  if (nHelp == 0) {
    _AfxMergeMenus(m_hSharedMenu, hServer, widths.width, 1);
  } else {
    int nNext = _AfxMergeMenus(m_hSharedMenu, hServer, widths.width, 1, 3, TRUE);
    int nCount = hServer != NULL ? ::GetMenuItemCount(hServer) : 0;
    TCHAR szText[256];
    szText[0] = 0;
    for (int i = nNext; i < nCount && m_hHelpPopup == NULL; i++) {
      m_hHelpPopup = ::GetSubMenu(hServer, i);
      if (m_hHelpPopup != NULL)
        ::GetMenuString(hServer, i, szText, sizeof(szText) / sizeof(szText[0]), MF_BYPOSITION);
    }
    int nPos = 0;
    for (int g = 0; g < 5; g++) nPos += (int)widths.width[g];
    HMENU hHelp = ::GetSubMenu(m_hSharedMenu, nPos);
    if (m_hHelpPopup != NULL && hHelp != NULL &&
        ::InsertMenu(hHelp, 0, MF_BYPOSITION | MF_POPUP | MF_STRING, (UINT_PTR)m_hHelpPopup,
                     szText))
      m_hMenuHelpPopup = hHelp;
    else
      m_hHelpPopup = NULL;
    // The Help menu is the object's: its commands go to the object.
    widths.width[5] = nHelp;
  }
  m_hOleMenu = ::OleCreateMenuDescriptor(m_hSharedMenu, &widths);
  return TRUE;
}

void COleDocIPFrameWnd::DestroySharedMenu() {
  if (m_hMenuHelpPopup != NULL) {
    for (int i = ::GetMenuItemCount(m_hMenuHelpPopup) - 1; i >= 0; i--) {
      if (::GetSubMenu(m_hMenuHelpPopup, i) == m_hHelpPopup) {
        ::RemoveMenu(m_hMenuHelpPopup, i, MF_BYPOSITION);
        break;
      }
    }
  }
  m_hMenuHelpPopup = NULL;
  m_hHelpPopup = NULL;
  COleIPFrameWnd::DestroySharedMenu();
}
