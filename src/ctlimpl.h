// ctlimpl.h - shared between the COleControl sources.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// What SendAdvise tells the advise sinks.
enum { OBJECTCODE_VIEWCHANGED = 1, OBJECTCODE_SAVED, OBJECTCODE_CLOSED, OBJECTCODE_DATACHANGED };

void AFXAPI _AfxCtlPixelsToHimetric(int cx, int cy, SIZEL* pSizeL);
void AFXAPI _AfxCtlHimetricToPixels(long cx, long cy, SIZE* pSize);

// The interface parts' IUnknown.
#define CTL_IUNKNOWN(part)                                                   \
  STDMETHODIMP_(ULONG) COleControl::X##part::AddRef() {                      \
    METHOD_PROLOGUE(COleControl, part);                                      \
    return pThis->ExternalAddRef();                                          \
  }                                                                          \
  STDMETHODIMP_(ULONG) COleControl::X##part::Release() {                     \
    METHOD_PROLOGUE(COleControl, part);                                      \
    return pThis->ExternalRelease();                                         \
  }                                                                          \
  STDMETHODIMP COleControl::X##part::QueryInterface(REFIID iid, LPVOID* ppv) { \
    METHOD_PROLOGUE(COleControl, part);                                      \
    return (HRESULT)pThis->ExternalQueryInterface(&iid, ppv);                \
  }
