// oledisp.cpp - COleDispatchDriver, which calls automation objects
// through IDispatch, and the OLE exceptions.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxdisp.h>

IMPLEMENT_DYNAMIC(COleException, CException)
IMPLEMENT_DYNAMIC(COleDispatchException, CException)

// ---------------------------------------------------------------------
// Exceptions

COleException::COleException() : m_sc(S_OK) {}

SCODE PASCAL COleException::Process(const CException* pAnyException) {
  if (pAnyException == NULL) return E_UNEXPECTED;
  if (pAnyException->IsKindOf(RUNTIME_CLASS(COleException)))
    return ((const COleException*)pAnyException)->m_sc;
  if (pAnyException->IsKindOf(RUNTIME_CLASS(CMemoryException))) return E_OUTOFMEMORY;
  if (pAnyException->IsKindOf(RUNTIME_CLASS(CNotSupportedException))) return E_NOTIMPL;
  return E_UNEXPECTED;
}

// The system's text for an SCODE.
static BOOL _AfxSystemMessage(SCODE sc, LPTSTR lpszError, UINT nMaxError) {
  if (lpszError == NULL || nMaxError == 0) return FALSE;
  DWORD n = ::FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                            (DWORD)sc, 0, lpszError, nMaxError, NULL);
  if (n == 0) {
    CString str;
    str.Format(_T("OLE error 0x%08lX"), (unsigned long)sc);
    lstrcpyn(lpszError, str, (int)nMaxError);
    return TRUE;
  }
  // Without the trailing line break.
  while (n > 0 && (lpszError[n - 1] == '\r' || lpszError[n - 1] == '\n')) lpszError[--n] = 0;
  return TRUE;
}

BOOL COleException::GetErrorMessage(LPTSTR lpszError, UINT nMaxError, PUINT pnHelpContext) const {
  if (pnHelpContext != NULL) *pnHelpContext = 0;
  return _AfxSystemMessage(m_sc, lpszError, nMaxError);
}

BOOL COleException::GetErrorMessage(LPTSTR lpszError, UINT nMaxError, PUINT pnHelpContext) {
  return ((const COleException*)this)->GetErrorMessage(lpszError, nMaxError, pnHelpContext);
}

COleDispatchException::COleDispatchException(LPCTSTR lpszDescription, UINT nHelpID, WORD wCode)
    : m_wCode(wCode), m_scError(0), m_dwHelpContext(nHelpID) {
  if (lpszDescription != NULL) m_strDescription = lpszDescription;
  m_strSource = AfxGetAppName() != NULL ? AfxGetAppName() : _T("");
}

// Fills an EXCEPINFO for an exception thrown inside an automation call.
void PASCAL COleDispatchException::Process(EXCEPINFO* pInfo, const CException* e) {
  memset(pInfo, 0, sizeof(*pInfo));
  if (e != NULL && e->IsKindOf(RUNTIME_CLASS(COleDispatchException))) {
    const COleDispatchException* pDisp = (const COleDispatchException*)e;
    pInfo->wCode = pDisp->m_wCode;
    pInfo->scode = pDisp->m_scError;
    pInfo->bstrSource = ::SysAllocString(CStringW(pDisp->m_strSource));
    pInfo->bstrDescription = ::SysAllocString(CStringW(pDisp->m_strDescription));
    if (!pDisp->m_strHelpFile.IsEmpty())
      pInfo->bstrHelpFile = ::SysAllocString(CStringW(pDisp->m_strHelpFile));
    pInfo->dwHelpContext = pDisp->m_dwHelpContext;
  } else {
    pInfo->scode = COleException::Process(e);
    TCHAR szMessage[256];
    if (e != NULL && ((CException*)e)->GetErrorMessage(szMessage, 256))
      pInfo->bstrDescription = ::SysAllocString(CStringW(szMessage));
  }
}

BOOL COleDispatchException::GetErrorMessage(LPTSTR lpszError, UINT nMaxError,
                                            PUINT pnHelpContext) const {
  if (lpszError == NULL || nMaxError == 0) return FALSE;
  if (pnHelpContext != NULL) *pnHelpContext = m_dwHelpContext;
  if (m_strDescription.IsEmpty()) return _AfxSystemMessage(m_scError, lpszError, nMaxError);
  lstrcpyn(lpszError, m_strDescription, (int)nMaxError);
  return TRUE;
}

BOOL COleDispatchException::GetErrorMessage(LPTSTR lpszError, UINT nMaxError,
                                            PUINT pnHelpContext) {
  return ((const COleDispatchException*)this)->GetErrorMessage(lpszError, nMaxError,
                                                                pnHelpContext);
}

void AFXAPI AfxThrowOleException(SCODE sc) {
  COleException* e = new COleException;
  e->m_sc = sc;
  throw e;
}

void AFXAPI AfxCheckError(SCODE sc) {
  if (FAILED(sc)) {
    if (sc == E_OUTOFMEMORY) AfxThrowMemoryException();
    AfxThrowOleException(sc);
  }
}

void AFXAPI AfxThrowOleDispatchException(WORD wCode, LPCTSTR lpszDescription, UINT nHelpID) {
  throw new COleDispatchException(lpszDescription, nHelpID, wCode);
}

void AFXAPI AfxThrowOleDispatchException(WORD wCode, UINT nDescriptionID, UINT nHelpID) {
  CString str;
  str.LoadString(nDescriptionID);
  throw new COleDispatchException(str, nHelpID == (UINT)-1 ? nDescriptionID : nHelpID, wCode);
}

// ---------------------------------------------------------------------
// COleDispatchDriver

COleDispatchDriver::COleDispatchDriver() : m_lpDispatch(NULL), m_bAutoRelease(TRUE) {}

COleDispatchDriver::COleDispatchDriver(LPDISPATCH lpDispatch, BOOL bAutoRelease)
    : m_lpDispatch(lpDispatch), m_bAutoRelease(bAutoRelease) {}

COleDispatchDriver::COleDispatchDriver(const COleDispatchDriver& dispatchSrc)
    : m_lpDispatch(dispatchSrc.m_lpDispatch), m_bAutoRelease(TRUE) {
  if (m_lpDispatch != NULL) m_lpDispatch->AddRef();
}

const COleDispatchDriver& COleDispatchDriver::operator=(const COleDispatchDriver& dispatchSrc) {
  if (this != &dispatchSrc) {
    LPDISPATCH lpDispatch = dispatchSrc.m_lpDispatch;
    if (lpDispatch != NULL) lpDispatch->AddRef();
    ReleaseDispatch();
    m_lpDispatch = lpDispatch;
    m_bAutoRelease = TRUE;
  }
  return *this;
}

BOOL COleDispatchDriver::CreateDispatch(REFCLSID clsid, COleException* pError) {
  ReleaseDispatch();
  LPUNKNOWN lpUnknown = NULL;
  // A local server first, then whatever the class offers.
  SCODE sc = ::CoCreateInstance(clsid, NULL, CLSCTX_ALL | CLSCTX_REMOTE_SERVER, IID_IUnknown,
                                (void**)&lpUnknown);
  if (sc == E_INVALIDARG)
    sc = ::CoCreateInstance(clsid, NULL, CLSCTX_ALL, IID_IUnknown, (void**)&lpUnknown);
  if (SUCCEEDED(sc)) {
    ::OleRun(lpUnknown);
    sc = lpUnknown->QueryInterface(IID_IDispatch, (void**)&m_lpDispatch);
    lpUnknown->Release();
  }
  if (FAILED(sc)) {
    m_lpDispatch = NULL;
    if (pError != NULL) pError->m_sc = sc;
    return FALSE;
  }
  m_bAutoRelease = TRUE;
  return TRUE;
}

BOOL COleDispatchDriver::CreateDispatch(LPCTSTR lpszProgID, COleException* pError) {
  CLSID clsid;
  CStringW strProgID(lpszProgID);
  SCODE sc = ::CLSIDFromProgID(strProgID, &clsid);
  if (FAILED(sc)) {
    if (pError != NULL) pError->m_sc = sc;
    return FALSE;
  }
  return CreateDispatch(clsid, pError);
}

void COleDispatchDriver::AttachDispatch(LPDISPATCH lpDispatch, BOOL bAutoRelease) {
  ReleaseDispatch();
  m_lpDispatch = lpDispatch;
  m_bAutoRelease = bAutoRelease;
}

LPDISPATCH COleDispatchDriver::DetachDispatch() {
  LPDISPATCH lpDispatch = m_lpDispatch;
  m_lpDispatch = NULL;
  return lpDispatch;
}

void COleDispatchDriver::ReleaseDispatch() {
  if (m_lpDispatch != NULL) {
    if (m_bAutoRelease) m_lpDispatch->Release();
    m_lpDispatch = NULL;
  }
}

// A VTS_PI4 argument is a long*, and VT_I4 by reference is 32 bits; where
// long is wider, the object gets a 32-bit copy, written back afterwards.
struct _AFX_LONGREF {
  long* pLong;
  LONG l;
};

// Builds one argument from the next value in the list.
static void _AfxArgFromList(VARIANT* pArg, BYTE bType, va_list& argList, _AFX_LONGREF* pLongRef) {
  ::VariantInit(pArg);
  if (bType & VT_MFCBYREF) {
    // A pointer to the value, passed as it is.
    VARTYPE vt = (VARTYPE)(bType & ~VT_MFCBYREF);
    pArg->vt = (VARTYPE)(vt | VT_BYREF);
    pArg->byref = va_arg(argList, void*);
    if (sizeof(long) != sizeof(LONG) && (vt == VT_I4 || vt == VT_ERROR) && pArg->byref != NULL) {
      pLongRef->pLong = (long*)pArg->byref;
      pLongRef->l = (LONG)*pLongRef->pLong;
      pArg->plVal = &pLongRef->l;
    }
    return;
  }
  switch (bType) {
    case VT_I2:
      pArg->vt = VT_I2;
      pArg->iVal = (SHORT)va_arg(argList, int);
      break;
    case VT_I4:
      pArg->vt = VT_I4;
      pArg->lVal = (LONG)va_arg(argList, long);
      break;
    case VT_R4:
      pArg->vt = VT_R4;
      pArg->fltVal = (float)va_arg(argList, double);
      break;
    case VT_R8:
      pArg->vt = VT_R8;
      pArg->dblVal = va_arg(argList, double);
      break;
    case VT_DATE:
      pArg->vt = VT_DATE;
      pArg->date = va_arg(argList, DATE);
      break;
    case VT_CY:
      pArg->vt = VT_CY;
      pArg->cyVal = *va_arg(argList, CY*);
      break;
    case VT_BSTR: {
      LPCOLESTR lpsz = va_arg(argList, LPOLESTR);
      pArg->vt = VT_BSTR;
      pArg->bstrVal = ::SysAllocString(lpsz);
      if (lpsz != NULL && pArg->bstrVal == NULL) AfxThrowMemoryException();
      break;
    }
#ifndef _UNICODE
    case VT_BSTRA: {
      LPCSTR lpsz = va_arg(argList, LPCSTR);
      pArg->vt = VT_BSTR;
      if (lpsz != NULL) {
        pArg->bstrVal = ::SysAllocString(CStringW(lpsz));
        if (pArg->bstrVal == NULL) AfxThrowMemoryException();
      }
      break;
    }
#endif
    case VT_DISPATCH:
      pArg->vt = VT_DISPATCH;
      pArg->pdispVal = va_arg(argList, LPDISPATCH);
      if (pArg->pdispVal != NULL) pArg->pdispVal->AddRef();
      break;
    case VT_UNKNOWN:
      pArg->vt = VT_UNKNOWN;
      pArg->punkVal = va_arg(argList, LPUNKNOWN);
      if (pArg->punkVal != NULL) pArg->punkVal->AddRef();
      break;
    case VT_ERROR:
      pArg->vt = VT_ERROR;
      pArg->scode = va_arg(argList, SCODE);
      break;
    case VT_BOOL:
      pArg->vt = VT_BOOL;
      pArg->boolVal = va_arg(argList, int) ? VARIANT_TRUE : VARIANT_FALSE;
      break;
    case VT_VARIANT: {
      // Wrappers pass the address of a const VARIANT&.
      const VARIANT* pVar = va_arg(argList, const VARIANT*);
      AfxCheckError(::VariantCopy(pArg, (VARIANT*)pVar));
      break;
    }
    case VT_I1:
      pArg->vt = VT_I1;
      pArg->cVal = (CHAR)va_arg(argList, int);
      break;
    case VT_UI1:
      pArg->vt = VT_UI1;
      pArg->bVal = (BYTE)va_arg(argList, int);
      break;
    case VT_UI2:
      pArg->vt = VT_UI2;
      pArg->uiVal = (USHORT)va_arg(argList, int);
      break;
    case VT_UI4:
      pArg->vt = VT_UI4;
      pArg->ulVal = va_arg(argList, ULONG);
      break;
    case VT_I8:
      pArg->vt = VT_I8;
      pArg->llVal = va_arg(argList, LONGLONG);
      break;
    case VT_UI8:
      pArg->vt = VT_UI8;
      pArg->ullVal = va_arg(argList, ULONGLONG);
      break;
    default:
      AfxThrowInvalidArgException();
  }
}

// Stores the result where the caller wants it, in the type it asked for.
static void _AfxStoreResult(VARIANT* pResult, VARTYPE vtRet, void* pvRet) {
  if (vtRet == VT_VARIANT) {
    // The caller's VARIANT takes the result as it is.
    ::VariantClear((VARIANT*)pvRet);
    memcpy(pvRet, pResult, sizeof(VARIANT));
    pResult->vt = VT_EMPTY;
    return;
  }
  VARTYPE vtWant = vtRet;
#ifndef _UNICODE
  if (vtWant == VT_BSTRA) vtWant = VT_BSTR;
#endif
  if (pResult->vt != vtWant) AfxCheckError(::VariantChangeType(pResult, pResult, 0, vtWant));
  switch (vtRet) {
    case VT_I2: *(short*)pvRet = pResult->iVal; break;
    case VT_I4: *(long*)pvRet = pResult->lVal; break;
    case VT_R4: *(float*)pvRet = pResult->fltVal; break;
    case VT_R8: *(double*)pvRet = pResult->dblVal; break;
    case VT_DATE: *(DATE*)pvRet = pResult->date; break;
    case VT_CY: *(CY*)pvRet = pResult->cyVal; break;
    case VT_BSTR:
#ifndef _UNICODE
    case VT_BSTRA:
#endif
      // A string comes back as a CString.
      *(CString*)pvRet = CString(pResult->bstrVal != NULL ? pResult->bstrVal : L"");
      break;
    case VT_DISPATCH:
      *(LPDISPATCH*)pvRet = pResult->pdispVal;
      pResult->vt = VT_EMPTY;  // the caller owns the reference
      break;
    case VT_UNKNOWN:
      *(LPUNKNOWN*)pvRet = pResult->punkVal;
      pResult->vt = VT_EMPTY;
      break;
    case VT_ERROR: *(SCODE*)pvRet = pResult->scode; break;
    case VT_BOOL: *(BOOL*)pvRet = pResult->boolVal != 0; break;
    case VT_I1: *(CHAR*)pvRet = pResult->cVal; break;
    case VT_UI1: *(BYTE*)pvRet = pResult->bVal; break;
    case VT_UI2: *(USHORT*)pvRet = pResult->uiVal; break;
    case VT_UI4: *(ULONG*)pvRet = pResult->ulVal; break;
    case VT_I8: *(LONGLONG*)pvRet = pResult->llVal; break;
    case VT_UI8: *(ULONGLONG*)pvRet = pResult->ullVal; break;
    default: AfxThrowInvalidArgException();
  }
}

void COleDispatchDriver::InvokeHelperV(DISPID dwDispID, WORD wFlags, VARTYPE vtRet, void* pvRet,
                                       const BYTE* pbParamInfo, va_list argList) {
  if (m_lpDispatch == NULL) AfxThrowOleException(E_UNEXPECTED);

  DISPPARAMS dispparams;
  memset(&dispparams, 0, sizeof(dispparams));
  UINT nArgs = pbParamInfo != NULL ? (UINT)strlen((LPCSTR)pbParamInfo) : 0;
  DISPID dispidNamed = DISPID_PROPERTYPUT;
  if (wFlags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) {
    // The value being set is the named argument.
    dispparams.cNamedArgs = 1;
    dispparams.rgdispidNamedArgs = &dispidNamed;
  }

  // The arguments go in right to left.
  VARIANT* rgArgs = NULL;
  if (nArgs > 0) {
    rgArgs = new VARIANT[nArgs];
    for (UINT i = 0; i < nArgs; i++) ::VariantInit(&rgArgs[i]);
  }
  dispparams.cArgs = nArgs;
  dispparams.rgvarg = rgArgs;

  VARIANT varResult;
  ::VariantInit(&varResult);
  EXCEPINFO excepInfo;
  memset(&excepInfo, 0, sizeof(excepInfo));
  UINT nArgErr = (UINT)-1;
  SCODE sc = S_OK;
  // A copy, because a va_list parameter may be an array that has decayed
  // to a pointer and cannot bind to the helper's reference.
  va_list args;
  va_copy(args, argList);
  _AFX_LONGREF* rgLongRefs = nArgs > 0 ? new _AFX_LONGREF[nArgs]() : NULL;
  try {
    for (UINT i = 0; i < nArgs; i++)
      _AfxArgFromList(&rgArgs[nArgs - 1 - i], pbParamInfo[i], args, &rgLongRefs[i]);
    sc = m_lpDispatch->Invoke(dwDispID, IID_NULL, 0, wFlags, &dispparams,
                              vtRet == VT_EMPTY ? NULL : &varResult, &excepInfo, &nArgErr);
  } catch (...) {
    va_end(args);
    for (UINT i = 0; i < nArgs; i++) ::VariantClear(&rgArgs[i]);
    delete[] rgArgs;
    delete[] rgLongRefs;
    throw;
  }
  va_end(args);
  for (UINT i = 0; i < nArgs; i++)
    if (rgLongRefs[i].pLong != NULL) *rgLongRefs[i].pLong = rgLongRefs[i].l;
  delete[] rgLongRefs;
  for (UINT i = 0; i < nArgs; i++) ::VariantClear(&rgArgs[i]);
  delete[] rgArgs;

  if (FAILED(sc)) {
    ::VariantClear(&varResult);
    if (sc != DISP_E_EXCEPTION) AfxThrowOleException(sc);
    // The object raised an error: pass on its description.
    if (excepInfo.pfnDeferredFillIn != NULL) excepInfo.pfnDeferredFillIn(&excepInfo);
    COleDispatchException* e = new COleDispatchException(NULL, 0, excepInfo.wCode);
    e->m_scError = excepInfo.scode;
    if (excepInfo.bstrSource != NULL) e->m_strSource = CString(excepInfo.bstrSource);
    if (excepInfo.bstrDescription != NULL) e->m_strDescription = CString(excepInfo.bstrDescription);
    if (excepInfo.bstrHelpFile != NULL) e->m_strHelpFile = CString(excepInfo.bstrHelpFile);
    e->m_dwHelpContext = excepInfo.dwHelpContext;
    ::SysFreeString(excepInfo.bstrSource);
    ::SysFreeString(excepInfo.bstrDescription);
    ::SysFreeString(excepInfo.bstrHelpFile);
    throw e;
  }

  if (vtRet != VT_EMPTY && pvRet != NULL) {
    try {
      _AfxStoreResult(&varResult, vtRet, pvRet);
    } catch (...) {
      ::VariantClear(&varResult);
      throw;
    }
  }
  ::VariantClear(&varResult);
}

void AFX_CDECL COleDispatchDriver::InvokeHelper(DISPID dwDispID, WORD wFlags, VARTYPE vtRet,
                                                void* pvRet, const BYTE* pbParamInfo, ...) {
  va_list argList;
  va_start(argList, pbParamInfo);
  try {
    InvokeHelperV(dwDispID, wFlags, vtRet, pvRet, pbParamInfo, argList);
  } catch (...) {
    va_end(argList);
    throw;
  }
  va_end(argList);
}

// MFC's signature passes the property type, a VARTYPE, before the
// value; it is promoted to int on the way in, which is harmless here.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wvarargs"
void AFX_CDECL COleDispatchDriver::SetProperty(DISPID dwDispID, VARTYPE vtProp, ...) {
  va_list argList;
  va_start(argList, vtProp);
  // A VT_BSTR value is an LPCTSTR, as MFC's wrappers pass it.
  BYTE rgbParams[2] = {(BYTE)(vtProp == VT_BSTR ? VT_BSTRT : vtProp), 0};
  WORD wFlags = (vtProp == VT_DISPATCH) ? DISPATCH_PROPERTYPUTREF : DISPATCH_PROPERTYPUT;
  try {
    InvokeHelperV(dwDispID, wFlags, VT_EMPTY, NULL, rgbParams, argList);
  } catch (...) {
    va_end(argList);
    throw;
  }
  va_end(argList);
}

#pragma clang diagnostic pop

void COleDispatchDriver::GetProperty(DISPID dwDispID, VARTYPE vtProp, void* pvProp) const {
  ((COleDispatchDriver*)this)->InvokeHelper(dwDispID, DISPATCH_PROPERTYGET, vtProp, pvProp, NULL);
}
