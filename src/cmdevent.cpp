// cmdevent.cpp - event sink maps: the events of hosted ActiveX controls
// routed to their handlers through CCmdTarget::OnEvent (CN_EVENT).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxdisp.h>

const AFX_EVENTSINKMAP* CCmdTarget::GetEventSinkMap() const { return GetThisEventSinkMap(); }

const AFX_EVENTSINKMAP* PASCAL CCmdTarget::GetThisEventSinkMap() {
  static const AFX_EVENTSINKMAP_ENTRY _eventsinkEntries[] = {
      {{NULL, DISPID_UNKNOWN, NULL, VT_EMPTY, _AFX_DISP_END, _AFX_DISP_NOFN, _AFX_DISP_NOFN, NULL,
        NULL},
       0,
       0,
       _AFX_EVENTSINK_END,
       FALSE}};
  static const AFX_EVENTSINKMAP eventsinkMap = {NULL, &_eventsinkEntries[0]};
  return &eventsinkMap;
}

static const AFX_EVENTSINKMAP_ENTRY* FindEventEntry(const AFX_EVENTSINKMAP* pMap, UINT idCtrl,
                                                    int nKind, DISPID dispid) {
  for (; pMap != NULL; pMap = pMap->pfnGetBaseMap != NULL ? pMap->pfnGetBaseMap() : NULL) {
    for (const AFX_EVENTSINKMAP_ENTRY* pEntry = pMap->lpEntries;
         pEntry->nEventKind != _AFX_EVENTSINK_END; pEntry++) {
      if (pEntry->nEventKind != nKind || pEntry->dispEntry.lDispID != dispid) continue;
      if (idCtrl >= pEntry->nCtrlIDFirst && idCtrl <= pEntry->nCtrlIDLast) return pEntry;
    }
  }
  return NULL;
}

BOOL CCmdTarget::OnEvent(UINT idCtrl, AFX_EVENT* pEvent, AFX_CMDHANDLERINFO* pHandlerInfo) {
  int nKind;
  switch (pEvent->m_eventKind) {
    case AFX_EVENT::event: nKind = _AFX_EVENTSINK_EVENT; break;
    case AFX_EVENT::propRequest: nKind = _AFX_EVENTSINK_REQUEST; break;
    case AFX_EVENT::propChanged: nKind = _AFX_EVENTSINK_CHANGED; break;
    default: return FALSE;
  }
  const AFX_EVENTSINKMAP_ENTRY* pEntry =
      FindEventEntry(GetEventSinkMap(), idCtrl, nKind, pEvent->m_dispid);
  if (pEntry == NULL) return FALSE;
  if (pHandlerInfo != NULL) {
    pHandlerInfo->pTarget = this;
    pHandlerInfo->pmf = pEntry->dispEntry.pfn;
    return TRUE;
  }

  // The arguments: the event's own, or the one BOOL* of a request.
  static DISPPARAMS noParams = {NULL, NULL, 0, 0};
  DISPPARAMS* pParams = pEvent->m_pDispParams != NULL ? pEvent->m_pDispParams : &noParams;
  VARIANT_BOOL bAllow = VARIANT_TRUE;
  VARIANT varAllow;
  DISPPARAMS requestParams = {&varAllow, NULL, 1, 0};
  if (nKind == _AFX_EVENTSINK_REQUEST) {
    varAllow.vt = VT_BYREF | VT_BOOL;
    varAllow.pboolVal = &bAllow;
    pParams = &requestParams;
  } else if (nKind == _AFX_EVENTSINK_CHANGED) {
    pParams = &noParams;
  }

  AFX_DISPMAP_ENTRY entry = pEntry->dispEntry;
  CStringA strParams;
  VARIANT* pArgs = NULL;
  DISPPARAMS rangeParams;
  if (pEntry->bRange) {
    // A range handler takes the control's ID first, which is the last of
    // the arguments as IDispatch orders them.
    strParams = "\x03";  // VTS_I4
    if (entry.lpszParams != NULL) strParams += entry.lpszParams;
    entry.lpszParams = strParams;
    UINT nArgs = pParams->cArgs;
    pArgs = new VARIANT[nArgs + 1];
    for (UINT i = 0; i < nArgs; i++) pArgs[i] = pParams->rgvarg[i];  // borrowed
    pArgs[nArgs].vt = VT_I4;
    pArgs[nArgs].lVal = (LONG)idCtrl;
    rangeParams.rgvarg = pArgs;
    rangeParams.rgdispidNamedArgs = NULL;
    rangeParams.cArgs = nArgs + 1;
    rangeParams.cNamedArgs = 0;
    pParams = &rangeParams;
  }

  VARIANT result;
  ::VariantInit(&result);
  HRESULT hr = InvokeEntry(&entry, DISPATCH_METHOD, pParams, &result, pEvent->m_pExcepInfo,
                           pEvent->m_puArgError);
  delete[] pArgs;
  if (FAILED(hr)) {
    pEvent->m_hResult = hr;
    return TRUE;
  }
  // A void handler handles the event; a BOOL one says whether it did.
  BOOL bHandled = TRUE;
  if (result.vt == VT_BOOL)
    bHandled = result.boolVal != VARIANT_FALSE;
  else if (result.vt == VT_I4 || result.vt == VT_UI4)
    bHandled = result.lVal != 0;
  ::VariantClear(&result);
  if (nKind == _AFX_EVENTSINK_REQUEST && bAllow == VARIANT_FALSE) pEvent->m_hResult = S_FALSE;
  if (nKind == _AFX_EVENTSINK_CHANGED) pEvent->m_bPropChanged = TRUE;
  return bHandled;
}
