// win16ole.cpp - the OLE parts of CCmdTarget, CWnd, CView and
// CDocTemplate, for 16-bit Windows. The 16-bit library has no OLE
// automation, no OLE controls and no OLE drag and drop yet: these
// virtual functions are here so that the classes are complete, and each
// one says that the object has nothing of OLE. The file is empty in the
// other builds.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "stdafx.h"

#ifdef _AFX_WIN16
#include "afximpl.h"

/// Gives NULL: the object has no dispatch map.
const AFX_DISPMAP* CCmdTarget::GetDispatchMap() const { return NULL; }

/// Gives NULL: the object has no interface map.
const AFX_INTERFACEMAP* CCmdTarget::GetInterfaceMap() const { return NULL; }

/// Gives NULL: the object has no connection map.
const AFX_CONNECTIONMAP* CCmdTarget::GetConnectionMap() const { return NULL; }

/// Gives NULL: the object has no event sink map.
const AFX_EVENTSINKMAP* CCmdTarget::GetEventSinkMap() const { return NULL; }

/// Deletes the object when its last OLE reference goes, as on Win32.
void CCmdTarget::OnFinalRelease() { delete this; }

/// Fails with E_NOTIMPL: there are no type libraries.
HRESULT CCmdTarget::GetTypeInfoOfGuid(LCID lcid, REFGUID guid, LPTYPEINFO* ppTypeInfo) {
  (void)lcid;
  (void)guid;
  if (ppTypeInfo != NULL) *ppTypeInfo = NULL;
  return E_NOTIMPL;
}

/// Gives FALSE: no OLE control sends events.
BOOL CCmdTarget::OnEvent(UINT idCtrl, AFX_EVENT* pEvent, AFX_CMDHANDLERINFO* pHandlerInfo) {
  (void)idCtrl;
  (void)pEvent;
  (void)pHandlerInfo;
  return FALSE;
}

/// Gives FALSE: a window cannot hold OLE controls.
BOOL CWnd::CreateControlSite(COleControlContainer* pContainer, COleControlSite** ppSite, UINT nID,
                             REFCLSID clsid) {
  (void)pContainer;
  (void)nID;
  (void)clsid;
  if (ppSite != NULL) *ppSite = NULL;
  return FALSE;
}

/// Gives DROPEFFECT_NONE (0): a view takes no OLE drops.
DROPEFFECT CView::OnDragEnter(COleDataObject* pDataObject, DWORD dwKeyState, CPoint point) {
  (void)pDataObject;
  (void)dwKeyState;
  (void)point;
  return 0;
}

/// Gives DROPEFFECT_NONE (0): a view takes no OLE drops.
DROPEFFECT CView::OnDragOver(COleDataObject* pDataObject, DWORD dwKeyState, CPoint point) {
  (void)pDataObject;
  (void)dwKeyState;
  (void)point;
  return 0;
}

/// Does nothing: a view takes no OLE drops.
void CView::OnDragLeave() {}

/// Gives FALSE: a view takes no OLE drops.
BOOL CView::OnDrop(COleDataObject* pDataObject, DROPEFFECT dropEffect, CPoint point) {
  (void)pDataObject;
  (void)dropEffect;
  (void)point;
  return FALSE;
}

/// Gives (DROPEFFECT)-1, so that OnDrop is called (and fails).
DROPEFFECT CView::OnDropEx(COleDataObject* pDataObject, DROPEFFECT dropDefault,
                           DROPEFFECT dropList, CPoint point) {
  (void)pDataObject;
  (void)dropDefault;
  (void)dropList;
  (void)point;
  return (DROPEFFECT)-1;
}

/// Gives DROPEFFECT_NONE (0): a view does not scroll for an OLE drag.
DROPEFFECT CView::OnDragScroll(DWORD dwKeyState, CPoint point) {
  (void)dwKeyState;
  (void)point;
  return 0;
}

/// Gives NULL: a document cannot be an OLE server.
CFrameWnd* CDocTemplate::CreateOleFrame(CWnd* pParentWnd, CDocument* pDoc, BOOL bCreateView) {
  (void)pParentWnd;
  (void)pDoc;
  (void)bCreateView;
  return NULL;
}

#endif  // _AFX_WIN16
