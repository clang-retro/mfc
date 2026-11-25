// oleauto.h - automation functions: BSTRs, variants, safe arrays,
// conversions and type libraries.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_OLEAUTO_H
#define _WINPORT_OLEAUTO_H
// The name the system header uses, which MFC tests for.
#define _OLEAUTO_H_

#include <oaidl.h>

#ifdef __cplusplus
extern "C" {
#endif

BSTR WINAPI SysAllocString(const OLECHAR*);
BSTR WINAPI SysAllocStringLen(const OLECHAR*, UINT);
BSTR WINAPI SysAllocStringByteLen(LPCSTR, UINT);
INT WINAPI SysReAllocString(BSTR*, const OLECHAR*);
INT WINAPI SysReAllocStringLen(BSTR*, const OLECHAR*, UINT);
void WINAPI SysFreeString(BSTR);
UINT WINAPI SysStringLen(BSTR);
UINT WINAPI SysStringByteLen(BSTR);

#define VARIANT_NOVALUEPROP 0x01
#define VARIANT_ALPHABOOL 0x02
#define VARIANT_NOUSEROVERRIDE 0x04
#define VARIANT_LOCALBOOL 0x10
#define VAR_TIMEVALUEONLY ((DWORD)0x00000001)
#define VAR_DATEVALUEONLY ((DWORD)0x00000002)
void WINAPI VariantInit(VARIANTARG*);
HRESULT WINAPI VariantClear(VARIANTARG*);
HRESULT WINAPI VariantCopy(VARIANTARG*, const VARIANTARG*);
HRESULT WINAPI VariantCopyInd(VARIANT*, const VARIANTARG*);
HRESULT WINAPI VariantChangeType(VARIANTARG*, const VARIANTARG*, USHORT, VARTYPE);
HRESULT WINAPI VariantChangeTypeEx(VARIANTARG*, const VARIANTARG*, LCID, USHORT, VARTYPE);
INT WINAPI SystemTimeToVariantTime(LPSYSTEMTIME, DOUBLE*);
INT WINAPI VariantTimeToSystemTime(DOUBLE, LPSYSTEMTIME);
INT WINAPI DosDateTimeToVariantTime(USHORT, USHORT, DOUBLE*);
INT WINAPI VariantTimeToDosDateTime(DOUBLE, USHORT*, USHORT*);
HRESULT WINAPI VarDateFromStr(LPCOLESTR, LCID, ULONG, DATE*);
HRESULT WINAPI VarBstrFromDate(DATE, LCID, ULONG, BSTR*);
HRESULT WINAPI VarCyFromStr(LPCOLESTR, LCID, ULONG, CY*);
HRESULT WINAPI VarBstrFromCy(CY, LCID, ULONG, BSTR*);
HRESULT WINAPI VarR8FromStr(LPCOLESTR, LCID, ULONG, DOUBLE*);
HRESULT WINAPI VarBstrFromR8(DOUBLE, LCID, ULONG, BSTR*);
HRESULT WINAPI VarI4FromStr(LPCOLESTR, LCID, ULONG, LONG*);
HRESULT WINAPI VarDecFromStr(LPCOLESTR, LCID, ULONG, DECIMAL*);
HRESULT WINAPI VarBstrFromDec(const DECIMAL*, LCID, ULONG, BSTR*);
HRESULT WINAPI VarDecCmp(const DECIMAL*, const DECIMAL*);
HRESULT WINAPI VarDecFromI4(LONG, DECIMAL*);
HRESULT WINAPI VarDecFromR8(DOUBLE, DECIMAL*);
HRESULT WINAPI VarR8FromDec(const DECIMAL*, DOUBLE*);
HRESULT WINAPI VarCyFromR8(DOUBLE, CY*);
HRESULT WINAPI VarR8FromCy(CY, DOUBLE*);
#define VARCMP_LT 0
#define VARCMP_EQ 1
#define VARCMP_GT 2
#define VARCMP_NULL 3
HRESULT WINAPI VarCmp(LPVARIANT pvarLeft, LPVARIANT pvarRight, LCID lcid, ULONG dwFlags);
HRESULT WINAPI VarBstrCmp(BSTR bstrLeft, BSTR bstrRight, LCID lcid, ULONG dwFlags);

SAFEARRAY* WINAPI SafeArrayCreate(VARTYPE, UINT, SAFEARRAYBOUND*);
SAFEARRAY* WINAPI SafeArrayCreateVector(VARTYPE, LONG, ULONG);
HRESULT WINAPI SafeArrayAllocDescriptor(UINT, SAFEARRAY**);
HRESULT WINAPI SafeArrayAllocDescriptorEx(VARTYPE, UINT, SAFEARRAY**);
HRESULT WINAPI SafeArrayAllocData(SAFEARRAY*);
HRESULT WINAPI SafeArrayDestroy(SAFEARRAY*);
HRESULT WINAPI SafeArrayDestroyData(SAFEARRAY*);
HRESULT WINAPI SafeArrayDestroyDescriptor(SAFEARRAY*);
HRESULT WINAPI SafeArrayRedim(SAFEARRAY*, SAFEARRAYBOUND*);
UINT WINAPI SafeArrayGetDim(SAFEARRAY*);
UINT WINAPI SafeArrayGetElemsize(SAFEARRAY*);
HRESULT WINAPI SafeArrayGetUBound(SAFEARRAY*, UINT, LONG*);
HRESULT WINAPI SafeArrayGetLBound(SAFEARRAY*, UINT, LONG*);
HRESULT WINAPI SafeArrayLock(SAFEARRAY*);
HRESULT WINAPI SafeArrayUnlock(SAFEARRAY*);
HRESULT WINAPI SafeArrayAccessData(SAFEARRAY*, void**);
HRESULT WINAPI SafeArrayUnaccessData(SAFEARRAY*);
HRESULT WINAPI SafeArrayGetElement(SAFEARRAY*, LONG*, void*);
HRESULT WINAPI SafeArrayPutElement(SAFEARRAY*, LONG*, void*);
HRESULT WINAPI SafeArrayCopy(SAFEARRAY*, SAFEARRAY**);
HRESULT WINAPI SafeArrayPtrOfIndex(SAFEARRAY*, LONG*, void**);
HRESULT WINAPI SafeArrayGetVartype(SAFEARRAY*, VARTYPE*);

typedef enum tagREGKIND { REGKIND_DEFAULT, REGKIND_REGISTER, REGKIND_NONE } REGKIND;
HRESULT WINAPI LoadTypeLib(LPCOLESTR, ITypeLib**);
HRESULT WINAPI LoadTypeLibEx(LPCOLESTR, REGKIND, ITypeLib**);
HRESULT WINAPI LoadRegTypeLib(REFGUID, WORD, WORD, LCID, ITypeLib**);
HRESULT WINAPI RegisterTypeLib(ITypeLib*, LPCOLESTR, LPCOLESTR);
HRESULT WINAPI UnRegisterTypeLib(REFGUID, WORD, WORD, LCID, SYSKIND);
HRESULT WINAPI QueryPathOfRegTypeLib(REFGUID, USHORT, USHORT, LCID, BSTR*);
HRESULT WINAPI CreateTypeLib(SYSKIND, LPCOLESTR, ICreateTypeLib**);
HRESULT WINAPI CreateTypeLib2(SYSKIND, LPCOLESTR, ICreateTypeLib2**);
ULONG WINAPI LHashValOfNameSys(SYSKIND, LCID, const OLECHAR*);
#define LHashValOfName(lcid, szName) LHashValOfNameSys(SYS_WIN32, lcid, szName)

HRESULT WINAPI DispGetParam(DISPPARAMS*, UINT, VARTYPE, VARIANT*, UINT*);
HRESULT WINAPI DispGetIDsOfNames(ITypeInfo*, OLECHAR**, UINT, DISPID*);
HRESULT WINAPI DispInvoke(void*, ITypeInfo*, DISPID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*);
HRESULT WINAPI DispCallFunc(void*, ULONG_PTR, CALLCONV, VARTYPE, UINT, VARTYPE*, VARIANTARG**, VARIANT*);
HRESULT WINAPI CreateStdDispatch(IUnknown*, void*, ITypeInfo*, IUnknown**);

HRESULT WINAPI SetErrorInfo(ULONG, IErrorInfo*);
HRESULT WINAPI GetErrorInfo(ULONG, IErrorInfo**);
HRESULT WINAPI CreateErrorInfo(ICreateErrorInfo**);

#define ACTIVEOBJECT_STRONG 0x0
#define ACTIVEOBJECT_WEAK 0x1
HRESULT WINAPI RegisterActiveObject(IUnknown*, REFCLSID, DWORD, DWORD*);
HRESULT WINAPI RevokeActiveObject(DWORD, void*);
HRESULT WINAPI GetActiveObject(REFCLSID, void*, IUnknown**);

#ifdef __cplusplus
}  // extern "C"
#endif

// Where long is wider than LONG (64-bit Linux and macOS), code written
// for Windows passes a long for a LONG. C has no overloads, so only C++
// gets this one.
#if defined(__LP64__) && defined(__cplusplus)
__WINPORT_INLINE HRESULT VarI4FromStr(LPCOLESTR s, LCID lcid, ULONG flags, long* out) {
  LONG v = 0;
  HRESULT hr = VarI4FromStr(s, lcid, flags, &v);
  if (SUCCEEDED(hr) && out) *out = v;
  return hr;
}
#endif

#endif
