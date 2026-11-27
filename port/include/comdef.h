// comdef.h - the compiler COM support: _bstr_t, _variant_t and
// _com_error (comutil.h), _com_ptr_t (comip.h), the dispatch helpers
// that #import's wrappers call, and smart pointers (IUnknownPtr,
// IDispatchPtr, ...) for the system's interfaces.
//
// _com_dispatch_method, _com_dispatch_propget and _com_dispatch_propput
// take the parameters' types as a wide string of VARTYPEs (L"\x0003\x0008"
// for a long then a BSTR) and the values after it: numbers, BSTRs and
// interface pointers by value, a VARIANT or DECIMAL by its address, and
// anything VT_BYREF or VT_ARRAY as its pointer. The _raw_ forms return the
// HRESULT and leave a dispatch exception's error object with the thread
// (SetErrorInfo); the others raise _com_error with it.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _INC_COMDEF
#define _INC_COMDEF

#include <windows.h>
#include <ole2.h>
#include <olectl.h>
#include <ocidl.h>
#include <comutil.h>
#include <comip.h>
#include <stdarg.h>
#include <wchar.h>

#ifdef __cplusplus

// The work of the dispatch helpers: the call, its result in *pvRet as
// vtRet, and a dispatch exception's error object in *pperrinfo.
inline HRESULT _com_invoke_helper(IDispatch* pDispatch, DISPID dwDispID, WORD wFlags, VARTYPE vtRet, void* pvRet,
                                  const wchar_t* pwParamInfo, va_list argList, IErrorInfo** pperrinfo) throw() {
  *pperrinfo = NULL;
  if (pDispatch == NULL) return E_POINTER;
  UINT nArgs = pwParamInfo != NULL ? (UINT)wcslen(pwParamInfo) : 0;
  VARIANT* rgvarg = nArgs ? new VARIANT[nArgs] : NULL;
  // The first parameter is the last in rgvarg.
  for (UINT i = 0; i < nArgs; i++) {
    VARIANT* pv = &rgvarg[nArgs - 1 - i];
    ::VariantInit(pv);
    VARTYPE vt = (VARTYPE)pwParamInfo[i];
    pv->vt = vt;
    if (vt & (VT_BYREF | VT_ARRAY)) {
      pv->byref = va_arg(argList, void*);
      continue;
    }
    switch (vt) {
      case VT_I1:
        pv->cVal = (CHAR)va_arg(argList, int);
        break;
      case VT_UI1:
        pv->bVal = (BYTE)va_arg(argList, int);
        break;
      case VT_I2:
        pv->iVal = (SHORT)va_arg(argList, int);
        break;
      case VT_UI2:
        pv->uiVal = (USHORT)va_arg(argList, int);
        break;
      case VT_BOOL:
        pv->boolVal = (VARIANT_BOOL)va_arg(argList, int);
        break;
      // A Windows long is 4 bytes and this one may be 8: either reads
      // right as a long, truncated.
      case VT_I4:
      case VT_ERROR:
        pv->lVal = (LONG)va_arg(argList, long);
        break;
      case VT_UI4:
        pv->ulVal = (ULONG)va_arg(argList, unsigned long);
        break;
      case VT_INT:
        pv->intVal = va_arg(argList, int);
        break;
      case VT_UINT:
        pv->uintVal = va_arg(argList, unsigned int);
        break;
      case VT_I8:
        pv->llVal = va_arg(argList, long long);
        break;
      case VT_UI8:
        pv->ullVal = va_arg(argList, unsigned long long);
        break;
      case VT_R4:
        pv->fltVal = (float)va_arg(argList, double);
        break;
      case VT_R8:
      case VT_DATE:
        pv->dblVal = va_arg(argList, double);
        break;
      case VT_CY:
        pv->cyVal = va_arg(argList, CY);
        break;
      case VT_BSTR:
        pv->bstrVal = va_arg(argList, BSTR);
        break;
      case VT_DISPATCH:
        pv->pdispVal = va_arg(argList, IDispatch*);
        break;
      case VT_UNKNOWN:
        pv->punkVal = va_arg(argList, IUnknown*);
        break;
      case VT_VARIANT: {
        VARIANT* p = va_arg(argList, VARIANT*);
        if (p != NULL)
          memcpy(pv, p, sizeof(VARIANT));
        else
          pv->vt = VT_EMPTY;
        break;
      }
      case VT_DECIMAL: {
        DECIMAL* p = va_arg(argList, DECIMAL*);
        if (p != NULL) pv->decVal = *p;
        pv->vt = VT_DECIMAL;
        break;
      }
      default:
        delete[] rgvarg;
        return E_INVALIDARG;
    }
  }
  DISPPARAMS dp = {rgvarg, NULL, nArgs, 0};
  DISPID dispidPut = DISPID_PROPERTYPUT;
  if (wFlags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) {
    dp.rgdispidNamedArgs = &dispidPut;
    dp.cNamedArgs = 1;
  }
  VARIANT result;
  ::VariantInit(&result);
  EXCEPINFO ei;
  memset(&ei, 0, sizeof(ei));
  UINT uArgErr = 0;
  HRESULT hr = pDispatch->Invoke(dwDispID, IID_NULL, LOCALE_USER_DEFAULT, wFlags, &dp,
                                 vtRet != VT_EMPTY ? &result : NULL, &ei, &uArgErr);
  delete[] rgvarg;
  if (hr == DISP_E_EXCEPTION) {
    if (ei.pfnDeferredFillIn != NULL) ei.pfnDeferredFillIn(&ei);
    if (ei.scode != 0)
      hr = ei.scode;
    else if (ei.wCode != 0)
      hr = _com_error::WCodeToHRESULT(ei.wCode);
    ICreateErrorInfo* pcei = NULL;
    if (SUCCEEDED(::CreateErrorInfo(&pcei))) {
      pcei->SetGUID(IID_NULL);
      pcei->SetSource(ei.bstrSource);
      pcei->SetDescription(ei.bstrDescription);
      pcei->SetHelpFile(ei.bstrHelpFile);
      pcei->SetHelpContext(ei.dwHelpContext);
      if (FAILED(pcei->QueryInterface(IID_IErrorInfo, (void**)pperrinfo))) *pperrinfo = NULL;
      pcei->Release();
    }
    ::SysFreeString(ei.bstrSource);
    ::SysFreeString(ei.bstrDescription);
    ::SysFreeString(ei.bstrHelpFile);
    return hr;
  }
  if (FAILED(hr) || vtRet == VT_EMPTY || pvRet == NULL) {
    ::VariantClear(&result);
    return hr;
  }
  if (vtRet != VT_VARIANT && result.vt != vtRet) {
    hr = ::VariantChangeType(&result, &result, 0, vtRet);
    if (FAILED(hr)) {
      ::VariantClear(&result);
      return hr;
    }
  }
  if (vtRet & (VT_ARRAY | VT_BYREF)) {
    *(void**)pvRet = result.byref;
    return hr;
  }
  switch (vtRet) {
    case VT_VARIANT:
      memcpy(pvRet, &result, sizeof(VARIANT));
      break;
    case VT_I1:
    case VT_UI1:
      *(BYTE*)pvRet = result.bVal;
      break;
    case VT_I2:
    case VT_UI2:
    case VT_BOOL:
      *(SHORT*)pvRet = result.iVal;
      break;
    case VT_I4:
    case VT_UI4:
    case VT_INT:
    case VT_UINT:
    case VT_ERROR:
      *(LONG*)pvRet = result.lVal;
      break;
    case VT_I8:
    case VT_UI8:
      *(LONGLONG*)pvRet = result.llVal;
      break;
    case VT_R4:
      *(float*)pvRet = result.fltVal;
      break;
    case VT_R8:
    case VT_DATE:
      *(double*)pvRet = result.dblVal;
      break;
    case VT_CY:
      *(CY*)pvRet = result.cyVal;
      break;
    case VT_BSTR:
      *(BSTR*)pvRet = result.bstrVal;
      break;
    case VT_DISPATCH:
      *(IDispatch**)pvRet = result.pdispVal;
      break;
    case VT_UNKNOWN:
      *(IUnknown**)pvRet = result.punkVal;
      break;
    case VT_DECIMAL: {
      DECIMAL d = result.decVal;
      d.wReserved = 0;
      *(DECIMAL*)pvRet = d;
      break;
    }
    default:
      ::VariantClear(&result);
      return E_INVALIDARG;
  }
  return hr;
}

inline HRESULT _com_dispatch_raw_vmethod(IDispatch* pDispatch, DISPID dwDispID, WORD wFlags, VARTYPE vtRet,
                                         void* pvRet, const wchar_t* pwParamInfo, va_list argList) throw() {
  IErrorInfo* perrinfo = NULL;
  HRESULT hr = _com_invoke_helper(pDispatch, dwDispID, wFlags, vtRet, pvRet, pwParamInfo, argList, &perrinfo);
  if (perrinfo != NULL) {
    ::SetErrorInfo(0, perrinfo);
    perrinfo->Release();
  }
  return hr;
}
inline HRESULT _com_dispatch_vmethod(IDispatch* pDispatch, DISPID dwDispID, WORD wFlags, VARTYPE vtRet,
                                     void* pvRet, const wchar_t* pwParamInfo, va_list argList) {
  IErrorInfo* perrinfo = NULL;
  HRESULT hr = _com_invoke_helper(pDispatch, dwDispID, wFlags, vtRet, pvRet, pwParamInfo, argList, &perrinfo);
  if (FAILED(hr)) _com_raise_error(hr, perrinfo);
  return hr;
}

inline HRESULT __cdecl _com_dispatch_method(IDispatch* pDispatch, DISPID dwDispID, WORD wFlags, VARTYPE vtRet,
                                            void* pvRet, const wchar_t* pwParamInfo, ...) {
  va_list args;
  va_start(args, pwParamInfo);
  HRESULT hr = _com_dispatch_vmethod(pDispatch, dwDispID, wFlags, vtRet, pvRet, pwParamInfo, args);
  va_end(args);
  return hr;
}
inline HRESULT __cdecl _com_dispatch_raw_method(IDispatch* pDispatch, DISPID dwDispID, WORD wFlags, VARTYPE vtRet,
                                                void* pvRet, const wchar_t* pwParamInfo, ...) throw() {
  va_list args;
  va_start(args, pwParamInfo);
  HRESULT hr = _com_dispatch_raw_vmethod(pDispatch, dwDispID, wFlags, vtRet, pvRet, pwParamInfo, args);
  va_end(args);
  return hr;
}

namespace _com_util {
// A va_list with nothing in it, for the calls without parameters.
inline HRESULT _NoArgs(bool bRaw, IDispatch* pDispatch, DISPID dwDispID, WORD wFlags, VARTYPE vtRet, void* pvRet,
                       ...) {
  va_list args;
  va_start(args, pvRet);
  HRESULT hr = bRaw ? _com_dispatch_raw_vmethod(pDispatch, dwDispID, wFlags, vtRet, pvRet, NULL, args)
                    : _com_dispatch_vmethod(pDispatch, dwDispID, wFlags, vtRet, pvRet, NULL, args);
  va_end(args);
  return hr;
}
// A property put: by reference for an object, falling back to by value.
inline HRESULT _PropPut(bool bRaw, IDispatch* pDispatch, DISPID dwDispID, VARTYPE vtProp, va_list args) {
  wchar_t szInfo[2] = {(wchar_t)vtProp, 0};
  if (vtProp == VT_DISPATCH || vtProp == VT_UNKNOWN) {
    va_list copy;
    va_copy(copy, args);
    HRESULT hr = _com_dispatch_raw_vmethod(pDispatch, dwDispID, DISPATCH_PROPERTYPUTREF, VT_EMPTY, NULL, szInfo,
                                           copy);
    va_end(copy);
    if (hr != DISP_E_MEMBERNOTFOUND) {
      if (FAILED(hr) && !bRaw) {
        IErrorInfo* perrinfo = NULL;
        if (::GetErrorInfo(0, &perrinfo) != S_OK) perrinfo = NULL;
        _com_raise_error(hr, perrinfo);
      }
      return hr;
    }
  }
  return bRaw ? _com_dispatch_raw_vmethod(pDispatch, dwDispID, DISPATCH_PROPERTYPUT, VT_EMPTY, NULL, szInfo, args)
              : _com_dispatch_vmethod(pDispatch, dwDispID, DISPATCH_PROPERTYPUT, VT_EMPTY, NULL, szInfo, args);
}
}  // namespace _com_util

inline HRESULT WINAPI _com_dispatch_propget(IDispatch* pDispatch, DISPID dwDispID, VARTYPE vtProp, void* pvProp) {
  return _com_util::_NoArgs(false, pDispatch, dwDispID, DISPATCH_PROPERTYGET, vtProp, pvProp);
}
inline HRESULT WINAPI _com_dispatch_raw_propget(IDispatch* pDispatch, DISPID dwDispID, VARTYPE vtProp,
                                                void* pvProp) throw() {
  return _com_util::_NoArgs(true, pDispatch, dwDispID, DISPATCH_PROPERTYGET, vtProp, pvProp);
}
// vtProp is promoted as an argument, which va_start does not mind here.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wvarargs"
inline HRESULT __cdecl _com_dispatch_propput(IDispatch* pDispatch, DISPID dwDispID, VARTYPE vtProp, ...) {
  va_list args;
  va_start(args, vtProp);
  HRESULT hr = _com_util::_PropPut(false, pDispatch, dwDispID, vtProp, args);
  va_end(args);
  return hr;
}
inline HRESULT __cdecl _com_dispatch_raw_propput(IDispatch* pDispatch, DISPID dwDispID, VARTYPE vtProp,
                                                 ...) throw() {
  va_list args;
  va_start(args, vtProp);
  HRESULT hr = _com_util::_PropPut(true, pDispatch, dwDispID, vtProp, args);
  va_end(args);
  return hr;
}
#pragma clang diagnostic pop

// ---------------------------------------------------------------------
// Smart pointers: _COM_SMARTPTR_TYPEDEF(IFoo, __uuidof(IFoo)) makes
// IFooPtr.

#ifndef _COM_SMARTPTR
#define _COM_SMARTPTR _com_ptr_t
#endif
#ifndef _COM_SMARTPTR_LEVEL2
#define _COM_SMARTPTR_LEVEL2 _com_IIID
#endif
#ifndef _COM_SMARTPTR_TYPEDEF
#define _COM_SMARTPTR_TYPEDEF(Interface, IID) typedef _COM_SMARTPTR<_COM_SMARTPTR_LEVEL2<Interface, &IID> > Interface##Ptr
#endif

#ifndef _COM_NO_STANDARD_GUIDS_
_COM_SMARTPTR_TYPEDEF(IUnknown, __uuidof(IUnknown));
_COM_SMARTPTR_TYPEDEF(IClassFactory, __uuidof(IClassFactory));
_COM_SMARTPTR_TYPEDEF(IClassFactory2, __uuidof(IClassFactory2));
_COM_SMARTPTR_TYPEDEF(IDispatch, __uuidof(IDispatch));
_COM_SMARTPTR_TYPEDEF(ITypeComp, __uuidof(ITypeComp));
_COM_SMARTPTR_TYPEDEF(ITypeInfo, __uuidof(ITypeInfo));
_COM_SMARTPTR_TYPEDEF(ITypeLib, __uuidof(ITypeLib));
_COM_SMARTPTR_TYPEDEF(ICreateTypeInfo, __uuidof(ICreateTypeInfo));
_COM_SMARTPTR_TYPEDEF(ICreateTypeInfo2, __uuidof(ICreateTypeInfo2));
_COM_SMARTPTR_TYPEDEF(ICreateTypeLib, __uuidof(ICreateTypeLib));
_COM_SMARTPTR_TYPEDEF(ICreateTypeLib2, __uuidof(ICreateTypeLib2));
_COM_SMARTPTR_TYPEDEF(IEnumVARIANT, __uuidof(IEnumVARIANT));
_COM_SMARTPTR_TYPEDEF(IErrorInfo, __uuidof(IErrorInfo));
_COM_SMARTPTR_TYPEDEF(ICreateErrorInfo, __uuidof(ICreateErrorInfo));
_COM_SMARTPTR_TYPEDEF(ISupportErrorInfo, __uuidof(ISupportErrorInfo));
_COM_SMARTPTR_TYPEDEF(IErrorLog, __uuidof(IErrorLog));
_COM_SMARTPTR_TYPEDEF(IPropertyBag, __uuidof(IPropertyBag));
_COM_SMARTPTR_TYPEDEF(IRecordInfo, __uuidof(IRecordInfo));
_COM_SMARTPTR_TYPEDEF(IMalloc, __uuidof(IMalloc));
_COM_SMARTPTR_TYPEDEF(ISequentialStream, __uuidof(ISequentialStream));
_COM_SMARTPTR_TYPEDEF(IStream, __uuidof(IStream));
_COM_SMARTPTR_TYPEDEF(IEnumSTATSTG, __uuidof(IEnumSTATSTG));
_COM_SMARTPTR_TYPEDEF(IStorage, __uuidof(IStorage));
_COM_SMARTPTR_TYPEDEF(ILockBytes, __uuidof(ILockBytes));
_COM_SMARTPTR_TYPEDEF(IRootStorage, __uuidof(IRootStorage));
_COM_SMARTPTR_TYPEDEF(IPersist, __uuidof(IPersist));
_COM_SMARTPTR_TYPEDEF(IPersistStream, __uuidof(IPersistStream));
_COM_SMARTPTR_TYPEDEF(IPersistStreamInit, __uuidof(IPersistStreamInit));
_COM_SMARTPTR_TYPEDEF(IPersistStorage, __uuidof(IPersistStorage));
_COM_SMARTPTR_TYPEDEF(IPersistFile, __uuidof(IPersistFile));
_COM_SMARTPTR_TYPEDEF(IPersistMemory, __uuidof(IPersistMemory));
_COM_SMARTPTR_TYPEDEF(IPersistPropertyBag, __uuidof(IPersistPropertyBag));
_COM_SMARTPTR_TYPEDEF(IEnumUnknown, __uuidof(IEnumUnknown));
_COM_SMARTPTR_TYPEDEF(IEnumString, __uuidof(IEnumString));
_COM_SMARTPTR_TYPEDEF(IEnumMoniker, __uuidof(IEnumMoniker));
_COM_SMARTPTR_TYPEDEF(IRunningObjectTable, __uuidof(IRunningObjectTable));
_COM_SMARTPTR_TYPEDEF(IBindCtx, __uuidof(IBindCtx));
_COM_SMARTPTR_TYPEDEF(IMoniker, __uuidof(IMoniker));
_COM_SMARTPTR_TYPEDEF(IROTData, __uuidof(IROTData));
_COM_SMARTPTR_TYPEDEF(IEnumFORMATETC, __uuidof(IEnumFORMATETC));
_COM_SMARTPTR_TYPEDEF(IEnumSTATDATA, __uuidof(IEnumSTATDATA));
_COM_SMARTPTR_TYPEDEF(IAdviseSink, __uuidof(IAdviseSink));
_COM_SMARTPTR_TYPEDEF(IAdviseSink2, __uuidof(IAdviseSink2));
_COM_SMARTPTR_TYPEDEF(IDataObject, __uuidof(IDataObject));
_COM_SMARTPTR_TYPEDEF(IDataAdviseHolder, __uuidof(IDataAdviseHolder));
_COM_SMARTPTR_TYPEDEF(IMessageFilter, __uuidof(IMessageFilter));
_COM_SMARTPTR_TYPEDEF(IExternalConnection, __uuidof(IExternalConnection));
_COM_SMARTPTR_TYPEDEF(IMarshal, __uuidof(IMarshal));
_COM_SMARTPTR_TYPEDEF(IGlobalInterfaceTable, __uuidof(IGlobalInterfaceTable));
_COM_SMARTPTR_TYPEDEF(IEnumConnections, __uuidof(IEnumConnections));
_COM_SMARTPTR_TYPEDEF(IConnectionPoint, __uuidof(IConnectionPoint));
_COM_SMARTPTR_TYPEDEF(IEnumConnectionPoints, __uuidof(IEnumConnectionPoints));
_COM_SMARTPTR_TYPEDEF(IConnectionPointContainer, __uuidof(IConnectionPointContainer));
_COM_SMARTPTR_TYPEDEF(IProvideClassInfo, __uuidof(IProvideClassInfo));
_COM_SMARTPTR_TYPEDEF(IProvideClassInfo2, __uuidof(IProvideClassInfo2));
_COM_SMARTPTR_TYPEDEF(IPropertyNotifySink, __uuidof(IPropertyNotifySink));
_COM_SMARTPTR_TYPEDEF(ISpecifyPropertyPages, __uuidof(ISpecifyPropertyPages));
_COM_SMARTPTR_TYPEDEF(IFont, __uuidof(IFont));
_COM_SMARTPTR_TYPEDEF(IPicture, __uuidof(IPicture));
_COM_SMARTPTR_TYPEDEF(IFontDisp, __uuidof(IFontDisp));
_COM_SMARTPTR_TYPEDEF(IPictureDisp, __uuidof(IPictureDisp));
_COM_SMARTPTR_TYPEDEF(IPropertyPageSite, __uuidof(IPropertyPageSite));
_COM_SMARTPTR_TYPEDEF(IPropertyPage, __uuidof(IPropertyPage));
_COM_SMARTPTR_TYPEDEF(IPropertyPage2, __uuidof(IPropertyPage2));
_COM_SMARTPTR_TYPEDEF(IPerPropertyBrowsing, __uuidof(IPerPropertyBrowsing));
_COM_SMARTPTR_TYPEDEF(IOleControl, __uuidof(IOleControl));
_COM_SMARTPTR_TYPEDEF(IOleControlSite, __uuidof(IOleControlSite));
_COM_SMARTPTR_TYPEDEF(ISimpleFrameSite, __uuidof(ISimpleFrameSite));
_COM_SMARTPTR_TYPEDEF(IPointerInactive, __uuidof(IPointerInactive));
_COM_SMARTPTR_TYPEDEF(IViewObjectEx, __uuidof(IViewObjectEx));
_COM_SMARTPTR_TYPEDEF(IOleInPlaceObjectWindowless, __uuidof(IOleInPlaceObjectWindowless));
_COM_SMARTPTR_TYPEDEF(IOleInPlaceSiteEx, __uuidof(IOleInPlaceSiteEx));
_COM_SMARTPTR_TYPEDEF(IOleInPlaceSiteWindowless, __uuidof(IOleInPlaceSiteWindowless));
_COM_SMARTPTR_TYPEDEF(IQuickActivate, __uuidof(IQuickActivate));
_COM_SMARTPTR_TYPEDEF(IObjectWithSite, __uuidof(IObjectWithSite));
_COM_SMARTPTR_TYPEDEF(IServiceProvider, __uuidof(IServiceProvider));
_COM_SMARTPTR_TYPEDEF(IOleAdviseHolder, __uuidof(IOleAdviseHolder));
_COM_SMARTPTR_TYPEDEF(IOleCache, __uuidof(IOleCache));
_COM_SMARTPTR_TYPEDEF(IOleCache2, __uuidof(IOleCache2));
_COM_SMARTPTR_TYPEDEF(IOleCacheControl, __uuidof(IOleCacheControl));
_COM_SMARTPTR_TYPEDEF(IParseDisplayName, __uuidof(IParseDisplayName));
_COM_SMARTPTR_TYPEDEF(IOleContainer, __uuidof(IOleContainer));
_COM_SMARTPTR_TYPEDEF(IOleClientSite, __uuidof(IOleClientSite));
_COM_SMARTPTR_TYPEDEF(IOleObject, __uuidof(IOleObject));
_COM_SMARTPTR_TYPEDEF(IOleWindow, __uuidof(IOleWindow));
_COM_SMARTPTR_TYPEDEF(IOleLink, __uuidof(IOleLink));
_COM_SMARTPTR_TYPEDEF(IOleItemContainer, __uuidof(IOleItemContainer));
_COM_SMARTPTR_TYPEDEF(IOleInPlaceUIWindow, __uuidof(IOleInPlaceUIWindow));
_COM_SMARTPTR_TYPEDEF(IOleInPlaceActiveObject, __uuidof(IOleInPlaceActiveObject));
_COM_SMARTPTR_TYPEDEF(IOleInPlaceFrame, __uuidof(IOleInPlaceFrame));
_COM_SMARTPTR_TYPEDEF(IOleInPlaceObject, __uuidof(IOleInPlaceObject));
_COM_SMARTPTR_TYPEDEF(IOleInPlaceSite, __uuidof(IOleInPlaceSite));
_COM_SMARTPTR_TYPEDEF(IContinue, __uuidof(IContinue));
_COM_SMARTPTR_TYPEDEF(IViewObject, __uuidof(IViewObject));
_COM_SMARTPTR_TYPEDEF(IViewObject2, __uuidof(IViewObject2));
_COM_SMARTPTR_TYPEDEF(IDropSource, __uuidof(IDropSource));
_COM_SMARTPTR_TYPEDEF(IDropTarget, __uuidof(IDropTarget));
_COM_SMARTPTR_TYPEDEF(IEnumOLEVERB, __uuidof(IEnumOLEVERB));
_COM_SMARTPTR_TYPEDEF(IRunnableObject, __uuidof(IRunnableObject));
#endif

#endif  // __cplusplus
#endif  // _INC_COMDEF
