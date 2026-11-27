// atldb.h - the OLE DB provider templates: a data source, its sessions,
// commands and rowsets made from ATL classes, whose rows are an array of
// a class of the provider's own (CRowsetImpl over m_rgRowData) and whose
// columns that class names (BEGIN_PROVIDER_COLUMN_MAP).
//
//   class CMyDS : CComObjectRootEx<...>, CComCoClass<CMyDS, &CLSID_MyProv>,
//       IDBCreateSessionImpl<CMyDS, CMySession>, IDBInitializeImpl<CMyDS>,
//       IDBPropertiesImpl<CMyDS>, IPersistImpl<CMyDS>, IInternalConnectionImpl<CMyDS>
//   class CMySession : ..., IGetDataSourceImpl<CMySession>,
//       IOpenRowsetImpl<CMySession>, ISessionPropertiesImpl<CMySession>,
//       IObjectWithSiteSessionImpl<CMySession>, IDBSchemaRowsetImpl<CMySession>,
//       IDBCreateCommandImpl<CMySession, CMyCommand>
//   class CMyCommand : ..., IAccessorImpl<CMyCommand>, ICommandTextImpl<CMyCommand>,
//       ICommandPropertiesImpl<CMyCommand>, IObjectWithSiteImpl<CMyCommand>,
//       IConvertTypeImpl<CMyCommand>, IColumnsInfoImpl<CMyCommand>
//   class CMyRowset : public CRowsetImpl<CMyRowset, CMyRow, CMyCommand>
//
// The properties of each object come from its BEGIN_PROPSET_MAP, with
// the defaults of OLE DB's own for the properties PROPERTY_INFO_ENTRY
// names. Values are converted between the rows' types and the
// consumer's bindings here, without the data conversion library. Rows
// may be changed, inserted and deleted (IRowsetChangeImpl); bookmarks,
// IRowsetLocate and deferred updates (IRowsetUpdate) are not here.
//
// Windows only, like the consumer templates (atldbcli.h).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLDB_H__
#define __ATLDB_H__

#include <atlbase.h>
#include <atlcom.h>
#include <atlcoll.h>
#include <atldbcli.h>
#include <msdadc.h>
#include <type_traits>
#include <vector>

namespace ATL {

// The data source's state (IDBInitializeImpl::m_dwStatus).
#define DSF_MASK_INIT 0xFFFFF00F
#define DSF_PERSIST_DIRTY 0x00000001
#define DSF_INITIALIZED 0x00000010

// The flags BEGIN_PROPERTY_SET_EX takes.
#define UPROPSET_HIDDEN 0x00000001
#define UPROPSET_PASSTHROUGH 0x00000002
#define UPROPSET_USERINIT 0x00000004

// ---------------------------------------------------------------------
// Values: the DBTYPEs a row's members and a consumer's bindings have,
// and the conversions between them.

inline DBLENGTH _AtlDbTypeSize(DBTYPE wType) {
  switch (wType & ~DBTYPE_BYREF) {
    case DBTYPE_I1: case DBTYPE_UI1: return 1;
    case DBTYPE_I2: case DBTYPE_UI2: case DBTYPE_BOOL: return 2;
    case DBTYPE_I4: case DBTYPE_UI4: case DBTYPE_R4: case DBTYPE_ERROR: return 4;
    case DBTYPE_I8: case DBTYPE_UI8: case DBTYPE_R8: case DBTYPE_CY: case DBTYPE_DATE:
    case DBTYPE_FILETIME: return 8;
    case DBTYPE_DECIMAL: return sizeof(DECIMAL);
    case DBTYPE_GUID: return sizeof(GUID);
    case DBTYPE_VARIANT: return sizeof(VARIANT);
    case DBTYPE_BSTR: return sizeof(BSTR);
    case DBTYPE_DBDATE: return sizeof(DBDATE);
    case DBTYPE_DBTIME: return sizeof(DBTIME);
    case DBTYPE_DBTIMESTAMP: return sizeof(DBTIMESTAMP);
    case DBTYPE_IUNKNOWN: case DBTYPE_IDISPATCH: return sizeof(void*);
    default: return 0;
  }
}
inline bool _AtlDbIsVarLength(DBTYPE wType) {
  wType &= ~DBTYPE_BYREF;
  return wType == DBTYPE_STR || wType == DBTYPE_WSTR || wType == DBTYPE_BYTES;
}
// The types the conversions know.
inline bool _AtlDbKnownType(DBTYPE wType) {
  switch (wType) {
    case DBTYPE_EMPTY: case DBTYPE_NULL:
    case DBTYPE_I1: case DBTYPE_UI1: case DBTYPE_I2: case DBTYPE_UI2: case DBTYPE_I4:
    case DBTYPE_UI4: case DBTYPE_I8: case DBTYPE_UI8: case DBTYPE_R4: case DBTYPE_R8:
    case DBTYPE_CY: case DBTYPE_DATE: case DBTYPE_BOOL: case DBTYPE_DECIMAL: case DBTYPE_ERROR:
    case DBTYPE_BSTR: case DBTYPE_STR: case DBTYPE_WSTR: case DBTYPE_BYTES: case DBTYPE_VARIANT:
    case DBTYPE_DBDATE: case DBTYPE_DBTIME: case DBTYPE_DBTIMESTAMP: case DBTYPE_FILETIME:
    case DBTYPE_GUID:
      return true;
    default:
      return false;
  }
}
// Whether a value of one type can be had as the other.
inline bool _AtlDbCanConvert(DBTYPE wFrom, DBTYPE wTo) {
  wFrom &= ~DBTYPE_BYREF;
  wTo &= ~DBTYPE_BYREF;
  if (!_AtlDbKnownType(wFrom) || !_AtlDbKnownType(wTo)) return false;
  if (wFrom == wTo) return true;
  bool bTextFrom = wFrom == DBTYPE_STR || wFrom == DBTYPE_WSTR || wFrom == DBTYPE_BSTR ||
                   wFrom == DBTYPE_VARIANT;
  bool bTextTo = wTo == DBTYPE_STR || wTo == DBTYPE_WSTR || wTo == DBTYPE_BSTR ||
                 wTo == DBTYPE_VARIANT;
  if (wFrom == DBTYPE_BYTES || wTo == DBTYPE_BYTES) return false;
  if (wFrom == DBTYPE_GUID || wTo == DBTYPE_GUID) return bTextFrom || bTextTo;
  return true;
}

// The date types through DATE.
inline bool _AtlDbDateFrom(DBTYPE wType, const void* p, DATE* pd) {
  SYSTEMTIME st = {};
  switch (wType) {
    case DBTYPE_DBDATE: {
      const DBDATE* d = (const DBDATE*)p;
      st.wYear = (WORD)d->year; st.wMonth = d->month; st.wDay = d->day;
      break;
    }
    case DBTYPE_DBTIME: {
      const DBTIME* t = (const DBTIME*)p;
      st.wYear = 1899; st.wMonth = 12; st.wDay = 30;
      st.wHour = t->hour; st.wMinute = t->minute; st.wSecond = t->second;
      break;
    }
    case DBTYPE_DBTIMESTAMP: {
      const DBTIMESTAMP* t = (const DBTIMESTAMP*)p;
      st.wYear = (WORD)t->year; st.wMonth = t->month; st.wDay = t->day;
      st.wHour = t->hour; st.wMinute = t->minute; st.wSecond = t->second;
      st.wMilliseconds = (WORD)(t->fraction / 1000000);
      break;
    }
    case DBTYPE_FILETIME:
      if (!::FileTimeToSystemTime((const FILETIME*)p, &st)) return false;
      break;
    default:
      return false;
  }
  return ::SystemTimeToVariantTime(&st, pd) != FALSE;
}
inline bool _AtlDbDateTo(DATE d, DBTYPE wType, void* p) {
  SYSTEMTIME st;
  if (!::VariantTimeToSystemTime(d, &st)) return false;
  switch (wType) {
    case DBTYPE_DBDATE: {
      DBDATE* r = (DBDATE*)p;
      r->year = (SHORT)st.wYear; r->month = st.wMonth; r->day = st.wDay;
      return true;
    }
    case DBTYPE_DBTIME: {
      DBTIME* r = (DBTIME*)p;
      r->hour = st.wHour; r->minute = st.wMinute; r->second = st.wSecond;
      return true;
    }
    case DBTYPE_DBTIMESTAMP: {
      DBTIMESTAMP* r = (DBTIMESTAMP*)p;
      r->year = (SHORT)st.wYear; r->month = st.wMonth; r->day = st.wDay;
      r->hour = st.wHour; r->minute = st.wMinute; r->second = st.wSecond;
      r->fraction = (ULONG)st.wMilliseconds * 1000000;
      return true;
    }
    case DBTYPE_FILETIME:
      return ::SystemTimeToFileTime(&st, (FILETIME*)p) != FALSE;
    default:
      return false;
  }
}

// A value as a VARIANT; text and GUIDs become BSTRs.
inline HRESULT _AtlDbToVariant(DBTYPE wType, const void* p, DBLENGTH cb, VARIANT* pv) {
  ::VariantInit(pv);
  switch (wType) {
    case DBTYPE_EMPTY: return S_OK;
    case DBTYPE_NULL: pv->vt = VT_NULL; return S_OK;
    case DBTYPE_VARIANT: return ::VariantCopy(pv, (VARIANT*)p);
    case DBTYPE_BSTR:
      pv->vt = VT_BSTR;
      pv->bstrVal = ::SysAllocString(*(BSTR*)p != NULL ? *(BSTR*)p : L"");
      return pv->bstrVal != NULL ? S_OK : E_OUTOFMEMORY;
    case DBTYPE_WSTR:
      pv->vt = VT_BSTR;
      pv->bstrVal = ::SysAllocStringLen((const OLECHAR*)p, (UINT)(cb / sizeof(WCHAR)));
      return pv->bstrVal != NULL ? S_OK : E_OUTOFMEMORY;
    case DBTYPE_STR: {
      int n = ::MultiByteToWideChar(CP_ACP, 0, (LPCSTR)p, (int)cb, NULL, 0);
      pv->vt = VT_BSTR;
      pv->bstrVal = ::SysAllocStringLen(NULL, (UINT)n);
      if (pv->bstrVal == NULL) return E_OUTOFMEMORY;
      ::MultiByteToWideChar(CP_ACP, 0, (LPCSTR)p, (int)cb, pv->bstrVal, n);
      return S_OK;
    }
    case DBTYPE_GUID: {
      OLECHAR sz[40];
      ::StringFromGUID2(*(const GUID*)p, sz, 40);
      pv->vt = VT_BSTR;
      pv->bstrVal = ::SysAllocString(sz);
      return pv->bstrVal != NULL ? S_OK : E_OUTOFMEMORY;
    }
    case DBTYPE_DBDATE: case DBTYPE_DBTIME: case DBTYPE_DBTIMESTAMP: case DBTYPE_FILETIME:
      pv->vt = VT_DATE;
      return _AtlDbDateFrom(wType, p, &pv->date) ? S_OK : DISP_E_TYPEMISMATCH;
    case DBTYPE_DECIMAL:
      pv->decVal = *(const DECIMAL*)p;
      pv->vt = VT_DECIMAL;
      return S_OK;
    default: {
      DBLENGTH n = _AtlDbTypeSize(wType);
      if (n == 0 || !_AtlDbKnownType(wType)) return DISP_E_TYPEMISMATCH;
      memcpy(&pv->bVal, p, (size_t)n);
      pv->vt = (VARTYPE)wType;
      return S_OK;
    }
  }
}

// The status of a conversion that failed.
inline DBSTATUS _AtlDbStatusOf(HRESULT hr) {
  return hr == DISP_E_OVERFLOW ? DBSTATUS_E_DATAOVERFLOW : DBSTATUS_E_CANTCONVERTVALUE;
}

// Text into a buffer of cbMax bytes: as much as fits, with its zero.
inline DBSTATUS _AtlDbPutText(LPCWSTR psz, UINT cch, DBTYPE wType, void* p, DBLENGTH cbMax,
                              DBLENGTH* pcb) {
  if (wType == DBTYPE_WSTR) {
    *pcb = (DBLENGTH)cch * sizeof(WCHAR);
    if (cbMax < sizeof(WCHAR)) return cch == 0 ? DBSTATUS_S_OK : DBSTATUS_S_TRUNCATED;
    DBLENGTH nFit = cbMax / sizeof(WCHAR) - 1;
    DBLENGTH n = cch < nFit ? cch : nFit;
    memcpy(p, psz, (size_t)n * sizeof(WCHAR));
    ((WCHAR*)p)[n] = 0;
    return n < cch ? DBSTATUS_S_TRUNCATED : DBSTATUS_S_OK;
  }
  int n = ::WideCharToMultiByte(CP_ACP, 0, psz, (int)cch, NULL, 0, NULL, NULL);
  *pcb = (DBLENGTH)n;
  if (cbMax == 0) return n == 0 ? DBSTATUS_S_OK : DBSTATUS_S_TRUNCATED;
  if ((DBLENGTH)n < cbMax) {
    ::WideCharToMultiByte(CP_ACP, 0, psz, (int)cch, (LPSTR)p, n, NULL, NULL);
    ((char*)p)[n] = 0;
    return DBSTATUS_S_OK;
  }
  CTempBuffer<char> buf;
  char* pBuf = buf.Allocate((size_t)n + 1);
  ::WideCharToMultiByte(CP_ACP, 0, psz, (int)cch, pBuf, n, NULL, NULL);
  memcpy(p, pBuf, (size_t)cbMax - 1);
  ((char*)p)[cbMax - 1] = 0;
  return DBSTATUS_S_TRUNCATED;
}

// A VARIANT as a value of wType, in a buffer of cbMax bytes for the
// types of variable length.
inline DBSTATUS _AtlDbFromVariant(VARIANT* pv, DBTYPE wType, void* p, DBLENGTH cbMax,
                                  DBLENGTH* pcb) {
  *pcb = _AtlDbTypeSize(wType);
  CComVariant v;
  HRESULT hr;
  switch (wType) {
    case DBTYPE_VARIANT:
      ::VariantInit((VARIANT*)p);
      hr = ::VariantCopy((VARIANT*)p, pv);
      return SUCCEEDED(hr) ? DBSTATUS_S_OK : _AtlDbStatusOf(hr);
    case DBTYPE_BSTR:
    case DBTYPE_STR:
    case DBTYPE_WSTR:
    case DBTYPE_GUID: {
      hr = ::VariantChangeType(&v, pv, VARIANT_ALPHABOOL, VT_BSTR);
      if (FAILED(hr)) return _AtlDbStatusOf(hr);
      UINT cch = ::SysStringLen(v.bstrVal);
      if (wType == DBTYPE_BSTR) {
        *(BSTR*)p = ::SysAllocStringLen(v.bstrVal, cch);
        return *(BSTR*)p != NULL ? DBSTATUS_S_OK : DBSTATUS_E_CANTCREATE;
      }
      if (wType == DBTYPE_GUID)
        return SUCCEEDED(::CLSIDFromString(v.bstrVal, (GUID*)p)) ? DBSTATUS_S_OK
                                                                 : DBSTATUS_E_CANTCONVERTVALUE;
      return _AtlDbPutText(v.bstrVal, cch, wType, p, cbMax, pcb);
    }
    case DBTYPE_DBDATE: case DBTYPE_DBTIME: case DBTYPE_DBTIMESTAMP: case DBTYPE_FILETIME:
      hr = ::VariantChangeType(&v, pv, 0, VT_DATE);
      if (FAILED(hr)) return _AtlDbStatusOf(hr);
      return _AtlDbDateTo(v.date, wType, p) ? DBSTATUS_S_OK : DBSTATUS_E_DATAOVERFLOW;
    case DBTYPE_DECIMAL:
      hr = ::VariantChangeType(&v, pv, 0, VT_DECIMAL);
      if (FAILED(hr)) return _AtlDbStatusOf(hr);
      *(DECIMAL*)p = v.decVal;
      return DBSTATUS_S_OK;
    default:
      if (*pcb == 0 || !_AtlDbKnownType(wType)) return DBSTATUS_E_CANTCONVERTVALUE;
      hr = ::VariantChangeType(&v, pv, 0, (VARTYPE)wType);
      if (FAILED(hr)) return _AtlDbStatusOf(hr);
      memcpy(p, &v.bVal, (size_t)*pcb);
      return DBSTATUS_S_OK;
  }
}

// One value from wSrc to wDst: the status, and in *pcbDst the length the
// whole value has (in bytes, without a terminating zero).
inline DBSTATUS _AtlDbConvert(DBTYPE wSrc, const void* pSrc, DBLENGTH cbSrc, DBTYPE wDst,
                              void* pDst, DBLENGTH cbDstMax, DBLENGTH* pcbDst) {
  DBLENGTH cbDummy;
  if (pcbDst == NULL) pcbDst = &cbDummy;
  *pcbDst = 0;
  if (!_AtlDbCanConvert(wSrc, wDst)) return DBSTATUS_E_CANTCONVERTVALUE;
  if (wSrc == wDst) {
    switch (wDst) {
      case DBTYPE_BYTES: {
        *pcbDst = cbSrc;
        DBLENGTH n = cbSrc < cbDstMax ? cbSrc : cbDstMax;
        memcpy(pDst, pSrc, (size_t)n);
        return n < cbSrc ? DBSTATUS_S_TRUNCATED : DBSTATUS_S_OK;
      }
      case DBTYPE_STR: {
        *pcbDst = cbSrc;
        if (cbDstMax == 0) return cbSrc == 0 ? DBSTATUS_S_OK : DBSTATUS_S_TRUNCATED;
        DBLENGTH n = cbSrc < cbDstMax - 1 ? cbSrc : cbDstMax - 1;
        memcpy(pDst, pSrc, (size_t)n);
        ((char*)pDst)[n] = 0;
        return n < cbSrc ? DBSTATUS_S_TRUNCATED : DBSTATUS_S_OK;
      }
      case DBTYPE_WSTR:
        return _AtlDbPutText((LPCWSTR)pSrc, (UINT)(cbSrc / sizeof(WCHAR)), DBTYPE_WSTR, pDst,
                             cbDstMax, pcbDst);
      case DBTYPE_BSTR:
      case DBTYPE_VARIANT:
        break;  // copies of their own, below
      default:
        *pcbDst = _AtlDbTypeSize(wDst);
        memcpy(pDst, pSrc, (size_t)*pcbDst);
        return DBSTATUS_S_OK;
    }
  }
  if ((wSrc == DBTYPE_STR || wSrc == DBTYPE_WSTR) && (wDst == DBTYPE_STR || wDst == DBTYPE_WSTR)) {
    // Text to text, without a BSTR's limit.
    CComVariant v;
    HRESULT hr = _AtlDbToVariant(wSrc, pSrc, cbSrc, &v);
    if (FAILED(hr)) return _AtlDbStatusOf(hr);
    return _AtlDbPutText(v.bstrVal, ::SysStringLen(v.bstrVal), wDst, pDst, cbDstMax, pcbDst);
  }
  CComVariant v;
  HRESULT hr = _AtlDbToVariant(wSrc, pSrc, cbSrc, &v);
  if (FAILED(hr)) return _AtlDbStatusOf(hr);
  return _AtlDbFromVariant(&v, wDst, pDst, cbDstMax, pcbDst);
}

// ---------------------------------------------------------------------
// Columns: what a provider's row class has, and where (the column map).

struct ATLCOLUMNINFO {
  LPOLESTR pwszName;
  ITypeInfo* pTypeInfo;
  DBORDINAL iOrdinal;
  DBCOLUMNFLAGS dwFlags;
  DBLENGTH ulColumnSize;
  DBTYPE wType;
  BYTE bPrecision;
  BYTE bScale;
  DBID columnid;
  DBBYTEOFFSET cbOffset;
};

// A member's DBTYPE and size, from its C++ type.
template <class T>
struct _AtlDbTypeOf {
  static const DBTYPE type = DBTYPE_BYTES;
  static const DBLENGTH size = sizeof(T);
};
#define _ATL_DBTYPE_OF(t, dbt)                  \
  template <>                                   \
  struct _AtlDbTypeOf<t> {                      \
    static const DBTYPE type = dbt;             \
    static const DBLENGTH size = sizeof(t);     \
  };
_ATL_DBTYPE_OF(char, DBTYPE_I1)
_ATL_DBTYPE_OF(signed char, DBTYPE_I1)
_ATL_DBTYPE_OF(unsigned char, DBTYPE_UI1)
_ATL_DBTYPE_OF(short, DBTYPE_I2)
_ATL_DBTYPE_OF(unsigned short, DBTYPE_UI2)
_ATL_DBTYPE_OF(int, DBTYPE_I4)
_ATL_DBTYPE_OF(unsigned int, DBTYPE_UI4)
_ATL_DBTYPE_OF(long, sizeof(long) == 8 ? DBTYPE_I8 : DBTYPE_I4)
_ATL_DBTYPE_OF(unsigned long, sizeof(long) == 8 ? DBTYPE_UI8 : DBTYPE_UI4)
_ATL_DBTYPE_OF(long long, DBTYPE_I8)
_ATL_DBTYPE_OF(unsigned long long, DBTYPE_UI8)
_ATL_DBTYPE_OF(float, DBTYPE_R4)
_ATL_DBTYPE_OF(double, DBTYPE_R8)
_ATL_DBTYPE_OF(bool, DBTYPE_UI1)
_ATL_DBTYPE_OF(CY, DBTYPE_CY)
_ATL_DBTYPE_OF(DECIMAL, DBTYPE_DECIMAL)
_ATL_DBTYPE_OF(GUID, DBTYPE_GUID)
_ATL_DBTYPE_OF(VARIANT, DBTYPE_VARIANT)
_ATL_DBTYPE_OF(DBDATE, DBTYPE_DBDATE)
_ATL_DBTYPE_OF(DBTIME, DBTYPE_DBTIME)
_ATL_DBTYPE_OF(DBTIMESTAMP, DBTYPE_DBTIMESTAMP)
_ATL_DBTYPE_OF(FILETIME, DBTYPE_FILETIME)
_ATL_DBTYPE_OF(BSTR, DBTYPE_BSTR)
#undef _ATL_DBTYPE_OF
// Arrays of characters are text (their size counting the zero); of bytes,
// bytes.
template <size_t N>
struct _AtlDbTypeOf<char[N]> {
  static const DBTYPE type = DBTYPE_STR;
  static const DBLENGTH size = N;
};
template <size_t N>
struct _AtlDbTypeOf<WCHAR[N]> {
  static const DBTYPE type = DBTYPE_WSTR;
  static const DBLENGTH size = N;
};
template <size_t N>
struct _AtlDbTypeOf<unsigned char[N]> {
  static const DBTYPE type = DBTYPE_BYTES;
  static const DBLENGTH size = N;
};

inline BYTE _AtlDbPrecision(DBTYPE wType) {
  switch (wType) {
    case DBTYPE_I1: case DBTYPE_UI1: return 3;
    case DBTYPE_I2: case DBTYPE_UI2: return 5;
    case DBTYPE_I4: case DBTYPE_UI4: return 10;
    case DBTYPE_I8: return 19;
    case DBTYPE_UI8: return 20;
    case DBTYPE_R4: return 7;
    case DBTYPE_R8: return 15;
    case DBTYPE_CY: return 19;
    case DBTYPE_DECIMAL: return 28;
    default: return (BYTE)~0;
  }
}

// One entry of a column map. ulColumnSize is in characters for text,
// in bytes for the rest.
inline ATLCOLUMNINFO _AtlProviderColumn(LPCOLESTR pwszName, DBORDINAL iOrdinal, DBTYPE wType,
                                        DBLENGTH cbSize, DBBYTEOFFSET cbOffset,
                                        BYTE bPrecision = (BYTE)~0, BYTE bScale = (BYTE)~0,
                                        DBCOLUMNFLAGS dwFlags = (DBCOLUMNFLAGS)~0) {
  ATLCOLUMNINFO c;
  memset(&c, 0, sizeof(c));
  c.pwszName = (LPOLESTR)pwszName;
  c.iOrdinal = iOrdinal;
  c.wType = wType;
  c.ulColumnSize = wType == DBTYPE_WSTR ? cbSize / sizeof(WCHAR) : cbSize;
  if (wType == DBTYPE_STR || wType == DBTYPE_WSTR) c.ulColumnSize = c.ulColumnSize > 0 ? c.ulColumnSize - 1 : 0;
  c.bPrecision = bPrecision != (BYTE)~0 ? bPrecision : _AtlDbPrecision(wType);
  c.bScale = bScale;
  c.dwFlags = dwFlags != (DBCOLUMNFLAGS)~0
                  ? dwFlags
                  : (_AtlDbIsVarLength(wType) ? 0 : DBCOLUMNFLAGS_ISFIXEDLENGTH) |
                        DBCOLUMNFLAGS_WRITEUNKNOWN;
  c.columnid.eKind = DBKIND_NAME;
  c.columnid.uName.pwszName = (LPOLESTR)pwszName;
  c.cbOffset = cbOffset;
  return c;
}

#define _ATL_DBOFFSET(c, m)                                    \
  _Pragma("clang diagnostic push")                             \
  _Pragma("clang diagnostic ignored \"-Winvalid-offsetof\"")    \
  offsetof(c, m)                                               \
  _Pragma("clang diagnostic pop")
#define _ATL_DBMEMBERTYPE(m) ATL::_AtlDbTypeOf<std::remove_cv_t<decltype(((_Class*)0)->m)> >

#define BEGIN_PROVIDER_COLUMN_MAP(theClass)                                       \
public:                                                                           \
  typedef theClass _Class;                                                        \
  static ATL::ATLCOLUMNINFO* GetColumnInfo(void* /*pThis*/, DBORDINAL* pcCols) {  \
    static ATL::ATLCOLUMNINFO _rgColumns[] = {
#define PROVIDER_COLUMN_ENTRY(name, ordinal, member)                                            \
  ATL::_AtlProviderColumn(OLESTR(name), ordinal, _ATL_DBMEMBERTYPE(member)::type,               \
                          _ATL_DBMEMBERTYPE(member)::size, _ATL_DBOFFSET(_Class, member)),
#define PROVIDER_COLUMN_ENTRY_STR(name, ordinal, member) PROVIDER_COLUMN_ENTRY(name, ordinal, member)
#define PROVIDER_COLUMN_ENTRY_WSTR(name, ordinal, member) PROVIDER_COLUMN_ENTRY(name, ordinal, member)
#define PROVIDER_COLUMN_ENTRY_TYPE_SIZE(name, ordinal, dbtype, size, member) \
  ATL::_AtlProviderColumn(OLESTR(name), ordinal, dbtype, size, _ATL_DBOFFSET(_Class, member)),
#define PROVIDER_COLUMN_ENTRY_FIXED(name, ordinal, dbtype, member)                        \
  ATL::_AtlProviderColumn(OLESTR(name), ordinal, dbtype, sizeof(((_Class*)0)->member), \
                          _ATL_DBOFFSET(_Class, member)),
#define PROVIDER_COLUMN_ENTRY_LENGTH(name, ordinal, size, member)                                \
  ATL::_AtlProviderColumn(OLESTR(name), ordinal, _ATL_DBMEMBERTYPE(member)::type, size,          \
                          _ATL_DBOFFSET(_Class, member)),
#define PROVIDER_COLUMN_ENTRY_TYPE_PS(name, ordinal, dbtype, precision, scale, member)     \
  ATL::_AtlProviderColumn(OLESTR(name), ordinal, dbtype, sizeof(((_Class*)0)->member),  \
                          _ATL_DBOFFSET(_Class, member), precision, scale),
#define PROVIDER_COLUMN_ENTRY_TYPE_PS_FLAGS(name, ordinal, dbtype, precision, scale, flags, member) \
  ATL::_AtlProviderColumn(OLESTR(name), ordinal, dbtype, sizeof(((_Class*)0)->member),            \
                          _ATL_DBOFFSET(_Class, member), precision, scale, flags),
#define PROVIDER_COLUMN_ENTRY_FLAGS(name, ordinal, flags, member)                                 \
  ATL::_AtlProviderColumn(OLESTR(name), ordinal, _ATL_DBMEMBERTYPE(member)::type,                 \
                          _ATL_DBMEMBERTYPE(member)::size, _ATL_DBOFFSET(_Class, member),         \
                          (BYTE)~0, (BYTE)~0, flags),
#define END_PROVIDER_COLUMN_MAP()                                \
  }                                                              \
  ;                                                              \
  if (pcCols != NULL) *pcCols = sizeof(_rgColumns) / sizeof(_rgColumns[0]); \
  return _rgColumns;                                             \
  }

// A column by its ordinal.
inline ATLCOLUMNINFO* _AtlFindColumn(ATLCOLUMNINFO* pCols, DBORDINAL cCols, DBORDINAL iOrdinal) {
  for (DBORDINAL i = 0; i < cCols; i++)
    if (pCols[i].iOrdinal == iOrdinal) return &pCols[i];
  return NULL;
}

// ---------------------------------------------------------------------
// Properties: each object's come from its map, the types, flags and
// defaults of the ones PROPERTY_INFO_ENTRY names from OLE DB.
//
//   BEGIN_PROPSET_MAP(CMyDS)
//     BEGIN_PROPERTY_SET(DBPROPSET_DATASOURCEINFO)
//       PROPERTY_INFO_ENTRY(ACTIVESESSIONS)
//       PROPERTY_INFO_ENTRY_VALUE(PROVIDERFRIENDLYNAME, OLESTR("My provider"))
//     END_PROPERTY_SET(DBPROPSET_DATASOURCEINFO)
//     CHAIN_PROPERTY_SET(CMyCommand)       // described here, set there
//   END_PROPSET_MAP()

#define _ATL_DBPROP_VT_HWND (sizeof(void*) == 8 ? VT_I8 : VT_I4)
#define _ATL_DBPROP_ACTIVESESSIONS VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, 0
#define _ATL_DBPROP_ASYNCTXNABORT VT_BOOL, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_ASYNCTXNCOMMIT VT_BOOL, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_BYREFACCESSORS VT_BOOL, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_CATALOGLOCATION VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, DBPROPVAL_CL_START
#define _ATL_DBPROP_CATALOGTERM VT_BSTR, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, OLESTR("")
#define _ATL_DBPROP_CATALOGUSAGE VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, 0
#define _ATL_DBPROP_COLUMNDEFINITION VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, 0
#define _ATL_DBPROP_CONCATNULLBEHAVIOR VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, DBPROPVAL_CB_NULL
#define _ATL_DBPROP_CONNECTIONSTATUS VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, DBPROPVAL_CS_INITIALIZED
#define _ATL_DBPROP_DATASOURCENAME VT_BSTR, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, OLESTR("")
#define _ATL_DBPROP_DATASOURCEREADONLY VT_BOOL, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, VARIANT_TRUE
#define _ATL_DBPROP_DATASOURCE_TYPE VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, DBPROPVAL_DST_TDP
#define _ATL_DBPROP_DBMSNAME VT_BSTR, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, OLESTR("")
#define _ATL_DBPROP_DBMSVER VT_BSTR, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, OLESTR("")
#define _ATL_DBPROP_DSOTHREADMODEL VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, DBPROPVAL_RT_FREETHREAD
#define _ATL_DBPROP_GROUPBY VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, 0
#define _ATL_DBPROP_HETEROGENEOUSTABLES VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, 0
#define _ATL_DBPROP_IDENTIFIERCASE VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, DBPROPVAL_IC_UPPER
#define _ATL_DBPROP_MAXINDEXSIZE VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, 0
#define _ATL_DBPROP_MAXROWSIZE VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, 0
#define _ATL_DBPROP_MAXROWSIZEINCLUDESBLOB VT_BOOL, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, VARIANT_TRUE
#define _ATL_DBPROP_MAXTABLESINSELECT VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, 0
#define _ATL_DBPROP_MULTIPLEPARAMSETS VT_BOOL, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_MULTIPLERESULTS VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, 0
#define _ATL_DBPROP_MULTIPLESTORAGEOBJECTS VT_BOOL, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_MULTITABLEUPDATE VT_BOOL, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_NULLCOLLATION VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, DBPROPVAL_NC_LOW
#define _ATL_DBPROP_ORDERBYCOLUMNSINSELECT VT_BOOL, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_OUTPUTPARAMETERAVAILABILITY VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, DBPROPVAL_OA_NOTSUPPORTED
#define _ATL_DBPROP_PERSISTENTIDTYPE VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, DBPROPVAL_PT_NAME
#define _ATL_DBPROP_PREPAREABORTBEHAVIOR VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, DBPROPVAL_CB_DELETE
#define _ATL_DBPROP_PREPARECOMMITBEHAVIOR VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, DBPROPVAL_CB_DELETE
#define _ATL_DBPROP_PROCEDURETERM VT_BSTR, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, OLESTR("")
#define _ATL_DBPROP_PROVIDERFRIENDLYNAME VT_BSTR, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, OLESTR("")
#define _ATL_DBPROP_PROVIDERNAME VT_BSTR, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, OLESTR("")
#define _ATL_DBPROP_PROVIDEROLEDBVER VT_BSTR, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, OLESTR("02.00")
#define _ATL_DBPROP_PROVIDERVER VT_BSTR, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, OLESTR("01.00.0000")
#define _ATL_DBPROP_QUOTEDIDENTIFIERCASE VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, DBPROPVAL_IC_SENSITIVE
#define _ATL_DBPROP_ROWSETCONVERSIONSONCOMMAND VT_BOOL, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, VARIANT_TRUE
#define _ATL_DBPROP_SCHEMATERM VT_BSTR, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, OLESTR("")
#define _ATL_DBPROP_SCHEMAUSAGE VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, 0
#define _ATL_DBPROP_SQLSUPPORT VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, 0
#define _ATL_DBPROP_STRUCTUREDSTORAGE VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, 0
#define _ATL_DBPROP_SUBQUERIES VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, 0
#define _ATL_DBPROP_SUPPORTEDTXNDDL VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, 0
#define _ATL_DBPROP_SUPPORTEDTXNISOLEVELS VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, 0
#define _ATL_DBPROP_SUPPORTEDTXNISORETAIN VT_I4, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, 0
#define _ATL_DBPROP_TABLETERM VT_BSTR, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, OLESTR("Table")
#define _ATL_DBPROP_USERNAME VT_BSTR, DBPROPFLAGS_DATASOURCEINFO | DBPROPFLAGS_READ, OLESTR("")
#define _ATL_DBPROP_AUTH_CACHE_AUTHINFO VT_BOOL, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, VARIANT_TRUE
#define _ATL_DBPROP_AUTH_ENCRYPT_PASSWORD VT_BOOL, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, VARIANT_FALSE
#define _ATL_DBPROP_AUTH_INTEGRATED VT_BSTR, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, OLESTR("")
#define _ATL_DBPROP_AUTH_MASK_PASSWORD VT_BOOL, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, VARIANT_FALSE
#define _ATL_DBPROP_AUTH_PASSWORD VT_BSTR, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, OLESTR("")
#define _ATL_DBPROP_AUTH_PERSIST_ENCRYPTED VT_BOOL, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, VARIANT_FALSE
#define _ATL_DBPROP_AUTH_PERSIST_SENSITIVE_AUTHINFO VT_BOOL, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, VARIANT_FALSE
#define _ATL_DBPROP_AUTH_USERID VT_BSTR, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, OLESTR("")
#define _ATL_DBPROP_INIT_ASYNCH VT_I4, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, 0
#define _ATL_DBPROP_INIT_BINDFLAGS VT_I4, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, 0
#define _ATL_DBPROP_INIT_CATALOG VT_BSTR, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, OLESTR("")
#define _ATL_DBPROP_INIT_DATASOURCE VT_BSTR, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, OLESTR("")
#define _ATL_DBPROP_INIT_HWND _ATL_DBPROP_VT_HWND, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, 0
#define _ATL_DBPROP_INIT_IMPERSONATION_LEVEL VT_I4, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, DB_IMP_LEVEL_ANONYMOUS
#define _ATL_DBPROP_INIT_LCID VT_I4, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, 0
#define _ATL_DBPROP_INIT_LOCATION VT_BSTR, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, OLESTR("")
#define _ATL_DBPROP_INIT_LOCKOWNER VT_BSTR, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, OLESTR("")
#define _ATL_DBPROP_INIT_MODE VT_I4, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, DB_MODE_READ
#define _ATL_DBPROP_INIT_OLEDBSERVICES VT_I4, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, DBPROPVAL_OS_ENABLEALL
#define _ATL_DBPROP_INIT_PROMPT VT_I2, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, DBPROMPT_NOPROMPT
#define _ATL_DBPROP_INIT_PROTECTION_LEVEL VT_I4, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, DB_PROT_LEVEL_NONE
#define _ATL_DBPROP_INIT_PROVIDERSTRING VT_BSTR, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, OLESTR("")
#define _ATL_DBPROP_INIT_TIMEOUT VT_I4, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, 0
#define _ATL_DBPROP_INIT_GENERALTIMEOUT VT_I4, DBPROPFLAGS_DBINIT | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, 0
#define _ATL_DBPROP_IAccessor VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_TRUE
#define _ATL_DBPROP_IColumnsInfo VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_TRUE
#define _ATL_DBPROP_IConvertType VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_TRUE
#define _ATL_DBPROP_IRowset VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_TRUE
#define _ATL_DBPROP_IRowsetInfo VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_TRUE
#define _ATL_DBPROP_IRowsetIdentity VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_TRUE
#define _ATL_DBPROP_IColumnsRowset VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_IConnectionPointContainer VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_IRowsetChange VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_IRowsetFind VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_IRowsetIndex VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_IRowsetLocate VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_IRowsetResynch VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_IRowsetScroll VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_IRowsetUpdate VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_ISupportErrorInfo VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_ISequentialStream VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_IDBAsynchStatus VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_IRowsetRefresh VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_ABORTPRESERVE VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_ACCESSORDER VT_I4, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, DBPROPVAL_AO_RANDOM
#define _ATL_DBPROP_APPENDONLY VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_BLOCKINGSTORAGEOBJECTS VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_TRUE
#define _ATL_DBPROP_BOOKMARKS VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_BOOKMARKSKIPPED VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_BOOKMARKTYPE VT_I4, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, DBPROPVAL_BMK_NUMERIC
#define _ATL_DBPROP_CACHEDEFERRED VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_CANFETCHBACKWARDS VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, VARIANT_TRUE
#define _ATL_DBPROP_CANHOLDROWS VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, VARIANT_TRUE
#define _ATL_DBPROP_CANSCROLLBACKWARDS VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, VARIANT_TRUE
#define _ATL_DBPROP_CHANGEINSERTEDROWS VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_COLUMNRESTRICT VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_COMMANDTIMEOUT VT_I4, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, 0
#define _ATL_DBPROP_COMMITPRESERVE VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_DEFERRED VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_DELAYSTORAGEOBJECTS VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_IMMOBILEROWS VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_TRUE
#define _ATL_DBPROP_LITERALBOOKMARKS VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_LITERALIDENTITY VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_TRUE
#define _ATL_DBPROP_LOCKMODE VT_I4, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, DBPROPVAL_LM_NONE
#define _ATL_DBPROP_MAXOPENROWS VT_I4, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, 0
#define _ATL_DBPROP_MAXPENDINGROWS VT_I4, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, 0
#define _ATL_DBPROP_MAXROWS VT_I4, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, 0
#define _ATL_DBPROP_MAYWRITECOLUMN VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_MEMORYUSAGE VT_I4, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, 0
#define _ATL_DBPROP_NOTIFICATIONGRANULARITY VT_I4, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, DBPROPVAL_NT_SINGLEROW
#define _ATL_DBPROP_NOTIFICATIONPHASES VT_I4, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, 0
#define _ATL_DBPROP_ORDEREDBOOKMARKS VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_OTHERINSERT VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_OTHERUPDATEDELETE VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_OWNINSERT VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_TRUE
#define _ATL_DBPROP_OWNUPDATEDELETE VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_TRUE
#define _ATL_DBPROP_QUICKRESTART VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_TRUE
#define _ATL_DBPROP_REENTRANTEVENTS VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_TRUE
#define _ATL_DBPROP_REMOVEDELETED VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_TRUE
#define _ATL_DBPROP_REPORTMULTIPLECHANGES VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_RETURNPENDINGINSERTS VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_ROWRESTRICT VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_TRUE
#define _ATL_DBPROP_ROWTHREADMODEL VT_I4, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, DBPROPVAL_RT_FREETHREAD
#define _ATL_DBPROP_SERVERCURSOR VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_SERVERDATAONINSERT VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_STRONGIDENTITY VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_TRUE
#define _ATL_DBPROP_TRANSACTEDOBJECT VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_UNIQUEROWS VT_BOOL, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, VARIANT_FALSE
#define _ATL_DBPROP_UPDATABILITY VT_I4, DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ, 0
#define _ATL_DBPROP_SESS_AUTOCOMMITISOLEVELS VT_I4, DBPROPFLAGS_SESSION | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, 0

struct _ATL_PROPMAP_ENTRY {
  int nKind;  // 1 a set begins, 2 a property, 3 the set ends, 4 a chain, 0 the end
  const GUID* pguidSet;
  DBPROPID dwPropId;
  VARTYPE vt;
  DWORD dwFlags;
  LONG_PTR lValue;  // the default: a number, or for VT_BSTR the text
  DWORD dwOptions;
  LPCOLESTR pwszName;
  const _ATL_PROPMAP_ENTRY* (*pfnChain)();
};

#define _ATL_DBPROP_W2(x) L##x
#define _ATL_DBPROP_W(x) _ATL_DBPROP_W2(x)
#define BEGIN_PROPSET_MAP(Class)                                  \
public:                                                           \
  static const ATL::_ATL_PROPMAP_ENTRY* _GetPropMap() {           \
    static const ATL::_ATL_PROPMAP_ENTRY _map[] = {
#define BEGIN_PROPERTY_SET(guid) {1, &guid, 0, VT_EMPTY, 0, 0, 0, NULL, NULL},
#define BEGIN_PROPERTY_SET_EX(guid, flags) BEGIN_PROPERTY_SET(guid)
#define PROPERTY_INFO_ENTRY_EX(dwPropID, vt, dwFlags, value, options) \
  {2, NULL, DBPROP_##dwPropID, (VARTYPE)(vt), (DWORD)(dwFlags), (LONG_PTR)(value), (DWORD)(options), \
   _ATL_DBPROP_W(#dwPropID), NULL},
#define _ATL_PROPINFO_ENTRY(dwPropID, vt, dwFlags, value) \
  PROPERTY_INFO_ENTRY_EX(dwPropID, vt, dwFlags, value, 0)
#define _ATL_PROPINFO_ENTRY_X(dwPropID, info) _ATL_PROPINFO_ENTRY(dwPropID, info)
#define PROPERTY_INFO_ENTRY(dwPropID) _ATL_PROPINFO_ENTRY_X(dwPropID, _ATL_DBPROP_##dwPropID)
#define _ATL_PROPINFO_VALUE(dwPropID, newvalue, vt, dwFlags, value) \
  PROPERTY_INFO_ENTRY_EX(dwPropID, vt, dwFlags, newvalue, 0)
#define _ATL_PROPINFO_VALUE_X(dwPropID, newvalue, info) _ATL_PROPINFO_VALUE(dwPropID, newvalue, info)
#define PROPERTY_INFO_ENTRY_VALUE(dwPropID, value) \
  _ATL_PROPINFO_VALUE_X(dwPropID, value, _ATL_DBPROP_##dwPropID)
#define END_PROPERTY_SET(guid) {3, &guid, 0, VT_EMPTY, 0, 0, 0, NULL, NULL},
#define CHAIN_PROPERTY_SET(Class) {4, NULL, 0, VT_EMPTY, 0, 0, 0, NULL, &Class::_GetPropMap},
#define END_PROPSET_MAP()                              \
  {0, NULL, 0, VT_EMPTY, 0, 0, 0, NULL, NULL}          \
  }                                                    \
  ;                                                    \
  return _map;                                         \
  }

// One property of an object: what its map says, and its value now.
struct _ATL_PROPVAL {
  const GUID* pguidSet;
  ULONG iSet;
  DBPROPID dwPropId;
  VARTYPE vt;
  DWORD dwFlags;
  DWORD dwOptions;
  LPCOLESTR pwszName;
  CComVariant var;
  CComVariant varDefault;
  bool bInfoOnly;  // from a chained map: described, not kept here
};

// The set a DBPROPSET_*ALL stands for.
inline const GUID* _AtlDbAllSet(const GUID& guid) {
  if (InlineIsEqualGUID(guid, DBPROPSET_DBINITALL)) return &DBPROPSET_DBINIT;
  if (InlineIsEqualGUID(guid, DBPROPSET_DATASOURCEINFOALL)) return &DBPROPSET_DATASOURCEINFO;
  if (InlineIsEqualGUID(guid, DBPROPSET_ROWSETALL)) return &DBPROPSET_ROWSET;
  if (InlineIsEqualGUID(guid, DBPROPSET_SESSIONALL)) return &DBPROPSET_SESSION;
  if (InlineIsEqualGUID(guid, DBPROPSET_DATASOURCEALL)) return &DBPROPSET_DATASOURCE;
  return NULL;
}

// The properties of an object whose map is PropClass's.
template <class PropClass>
class CUtlProps {
public:
  std::vector<_ATL_PROPVAL> m_rgProps;
  std::vector<const GUID*> m_rgSets;  // the sets, in the map's order
  bool m_bPropsInit;

  CUtlProps() : m_bPropsInit(false) {}
  virtual ~CUtlProps() {}

  HRESULT FInit(CUtlProps<PropClass>* pCopyProps = NULL) {
    _InitProps();
    if (pCopyProps != NULL) _CopyProps(pCopyProps);
    return S_OK;
  }

  // Overridable: whether a value may be set, and what to do once it is.
  virtual HRESULT IsValidValue(ULONG /*iCurSet*/, DBPROP* /*pDBProp*/) { return S_OK; }
  virtual HRESULT OnPropertyChanged(ULONG /*iCurSet*/, DBPROP* /*pDBProp*/) { return S_OK; }
  virtual HRESULT OnInterfaceRequested(REFIID /*riid*/) { return S_OK; }

  HRESULT GetPropValue(const GUID* pguidPropSet, DBPROPID dwPropId, VARIANT* pvValue) {
    if (pvValue == NULL) return E_INVALIDARG;
    _ATL_PROPVAL* p = _FindProp(*pguidPropSet, dwPropId, false);
    if (p == NULL) return E_FAIL;
    ::VariantInit(pvValue);
    return ::VariantCopy(pvValue, &p->var);
  }
  HRESULT SetPropValue(const GUID* pguidPropSet, DBPROPID dwPropId, VARIANT* pvValue) {
    if (pvValue == NULL) return E_INVALIDARG;
    _ATL_PROPVAL* p = _FindProp(*pguidPropSet, dwPropId, false);
    if (p == NULL) return E_FAIL;
    return p->var.Copy(pvValue);
  }
  // A VT_BOOL property, or bDefault where the map has none.
  bool _GetBoolProp(const GUID& guidSet, DBPROPID dwPropId, bool bDefault) {
    _ATL_PROPVAL* p = _FindProp(guidSet, dwPropId, false);
    if (p == NULL || p->var.vt != VT_BOOL) return bDefault;
    return p->var.boolVal != VARIANT_FALSE;
  }
  LONG _GetLongProp(const GUID& guidSet, DBPROPID dwPropId, LONG lDefault) {
    _ATL_PROPVAL* p = _FindProp(guidSet, dwPropId, false);
    if (p == NULL) return lDefault;
    CComVariant v;
    if (FAILED(v.ChangeType(VT_I4, &p->var))) return lDefault;
    return v.lVal;
  }

  // The values another object's map shares with this one.
  template <class Other>
  void _CopyProps(CUtlProps<Other>* pOther) {
    _InitProps();
    pOther->_InitProps();
    for (size_t i = 0; i < m_rgProps.size(); i++) {
      if (m_rgProps[i].bInfoOnly) continue;
      _ATL_PROPVAL* p = pOther->_FindProp(*m_rgProps[i].pguidSet, m_rgProps[i].dwPropId, false);
      if (p != NULL) m_rgProps[i].var = p->var;
    }
  }

  void _InitProps() {
    if (m_bPropsInit) return;
    m_bPropsInit = true;
    _AddMap(PropClass::_GetPropMap(), false);
  }
  _ATL_PROPVAL* _FindProp(const GUID& guidSet, DBPROPID dwPropId, bool bInfo) {
    _InitProps();
    for (size_t i = 0; i < m_rgProps.size(); i++) {
      _ATL_PROPVAL& p = m_rgProps[i];
      if (p.dwPropId == dwPropId && InlineIsEqualGUID(*p.pguidSet, guidSet) && (bInfo || !p.bInfoOnly))
        return &p;
    }
    return NULL;
  }
  bool _HasSet(const GUID& guidSet, bool bInfo) {
    _InitProps();
    for (size_t i = 0; i < m_rgProps.size(); i++)
      if (InlineIsEqualGUID(*m_rgProps[i].pguidSet, guidSet) && (bInfo || !m_rgProps[i].bInfoOnly))
        return true;
    return false;
  }

  // IDBProperties::GetProperties and the others. pguidOnly, when given,
  // is the one set that may be had.
  HRESULT _GetProperties(ULONG cPropertyIDSets, const DBPROPIDSET rgPropertyIDSets[],
                         ULONG* pcPropertySets, DBPROPSET** prgPropertySets,
                         const GUID* pguidOnly = NULL) {
    if (pcPropertySets != NULL) *pcPropertySets = 0;
    if (prgPropertySets != NULL) *prgPropertySets = NULL;
    if (pcPropertySets == NULL || prgPropertySets == NULL) return E_INVALIDARG;
    if (cPropertyIDSets > 0 && rgPropertyIDSets == NULL) return E_INVALIDARG;
    for (ULONG i = 0; i < cPropertyIDSets; i++)
      if (rgPropertyIDSets[i].cPropertyIDs > 0 && rgPropertyIDSets[i].rgPropertyIDs == NULL)
        return E_INVALIDARG;
    _InitProps();
    // What is asked for: every set here, or the ones named.
    std::vector<DBPROPIDSET> asked;
    if (cPropertyIDSets == 0) {
      for (size_t s = 0; s < m_rgSets.size(); s++) {
        if (pguidOnly != NULL && !InlineIsEqualGUID(*m_rgSets[s], *pguidOnly)) continue;
        if (!_HasSet(*m_rgSets[s], false)) continue;
        DBPROPIDSET d = {NULL, 0, *m_rgSets[s]};
        asked.push_back(d);
      }
    } else {
      asked.assign(rgPropertyIDSets, rgPropertyIDSets + cPropertyIDSets);
    }
    if (asked.empty()) return S_OK;
    DBPROPSET* pSets = (DBPROPSET*)::CoTaskMemAlloc(asked.size() * sizeof(DBPROPSET));
    if (pSets == NULL) return E_OUTOFMEMORY;
    ULONG nGood = 0, nBad = 0;
    for (size_t s = 0; s < asked.size(); s++) {
      const DBPROPIDSET& a = asked[s];
      DBPROPSET& out = pSets[s];
      out.guidPropertySet = a.guidPropertySet;
      out.cProperties = 0;
      out.rgProperties = NULL;
      bool bAllowed = pguidOnly == NULL || InlineIsEqualGUID(a.guidPropertySet, *pguidOnly);
      std::vector<DBPROPID> ids;
      if (a.cPropertyIDs == 0) {
        if (bAllowed)
          for (size_t i = 0; i < m_rgProps.size(); i++)
            if (!m_rgProps[i].bInfoOnly && InlineIsEqualGUID(*m_rgProps[i].pguidSet, a.guidPropertySet))
              ids.push_back(m_rgProps[i].dwPropId);
        if (ids.empty()) {
          nBad++;
          continue;
        }
      } else {
        ids.assign(a.rgPropertyIDs, a.rgPropertyIDs + a.cPropertyIDs);
      }
      out.rgProperties = (DBPROP*)::CoTaskMemAlloc(ids.size() * sizeof(DBPROP));
      if (out.rgProperties == NULL) {
        for (size_t k = 0; k < s; k++) _FreeSet(pSets[k]);
        ::CoTaskMemFree(pSets);
        return E_OUTOFMEMORY;
      }
      out.cProperties = (ULONG)ids.size();
      for (size_t i = 0; i < ids.size(); i++) {
        DBPROP& prop = out.rgProperties[i];
        memset(&prop, 0, sizeof(prop));
        prop.dwPropertyID = ids[i];
        prop.colid = DB_NULLID;
        ::VariantInit(&prop.vValue);
        _ATL_PROPVAL* p = bAllowed ? _FindProp(a.guidPropertySet, ids[i], false) : NULL;
        if (p == NULL) {
          prop.dwStatus = DBPROPSTATUS_NOTSUPPORTED;
          nBad++;
          continue;
        }
        prop.dwOptions = p->dwOptions;
        prop.dwStatus = DBPROPSTATUS_OK;
        ::VariantCopy(&prop.vValue, &p->var);
        nGood++;
      }
    }
    *pcPropertySets = (ULONG)asked.size();
    *prgPropertySets = pSets;
    if (nBad == 0) return S_OK;
    return nGood == 0 ? DB_E_ERRORSOCCURRED : DB_S_ERRORSOCCURRED;
  }

  // SetProperties: pguidNotSettable, when given, is a set whose values
  // are fixed now (DBINIT once the data source is initialized).
  HRESULT _SetProperties(ULONG cPropertySets, DBPROPSET rgPropertySets[],
                         const GUID* pguidOnly = NULL, const GUID* pguidNotSettable = NULL) {
    if (cPropertySets > 0 && rgPropertySets == NULL) return E_INVALIDARG;
    for (ULONG s = 0; s < cPropertySets; s++)
      if (rgPropertySets[s].cProperties > 0 && rgPropertySets[s].rgProperties == NULL)
        return E_INVALIDARG;
    _InitProps();
    ULONG nGood = 0, nBad = 0;
    bool bRequiredFailed = false;
    for (ULONG s = 0; s < cPropertySets; s++) {
      DBPROPSET& set = rgPropertySets[s];
      ULONG iSet = _SetIndex(set.guidPropertySet);
      for (ULONG i = 0; i < set.cProperties; i++) {
        DBPROP& prop = set.rgProperties[i];
        DBPROPSTATUS st = _SetOne(set.guidPropertySet, iSet, prop, pguidOnly, pguidNotSettable);
        prop.dwStatus = st;
        if (st == DBPROPSTATUS_OK) {
          nGood++;
        } else {
          nBad++;
          if (prop.dwOptions != DBPROPOPTIONS_OPTIONAL) bRequiredFailed = true;
          else prop.dwStatus = st == DBPROPSTATUS_NOTSUPPORTED ? st : DBPROPSTATUS_NOTSET;
        }
      }
    }
    if (nBad == 0) return S_OK;
    return (nGood == 0 || bRequiredFailed) ? DB_E_ERRORSOCCURRED : DB_S_ERRORSOCCURRED;
  }

  // GetPropertyInfo: what each property is, chained maps' too.
  HRESULT _GetPropertyInfo(ULONG cPropertyIDSets, const DBPROPIDSET rgPropertyIDSets[],
                           ULONG* pcPropertyInfoSets, DBPROPINFOSET** prgPropertyInfoSets,
                           OLECHAR** ppDescBuffer) {
    if (pcPropertyInfoSets != NULL) *pcPropertyInfoSets = 0;
    if (prgPropertyInfoSets != NULL) *prgPropertyInfoSets = NULL;
    if (ppDescBuffer != NULL) *ppDescBuffer = NULL;
    if (pcPropertyInfoSets == NULL || prgPropertyInfoSets == NULL) return E_INVALIDARG;
    if (cPropertyIDSets > 0 && rgPropertyIDSets == NULL) return E_INVALIDARG;
    _InitProps();
    std::vector<DBPROPIDSET> asked;
    if (cPropertyIDSets == 0) {
      for (size_t s = 0; s < m_rgSets.size(); s++) {
        DBPROPIDSET d = {NULL, 0, *m_rgSets[s]};
        asked.push_back(d);
      }
    } else {
      asked.assign(rgPropertyIDSets, rgPropertyIDSets + cPropertyIDSets);
    }
    if (asked.empty()) return S_OK;
    // The properties of each set, and room for their names.
    std::vector<std::vector<_ATL_PROPVAL*> > found(asked.size());
    std::vector<std::vector<DBPROPID> > ids(asked.size());
    size_t cchDesc = 0;
    ULONG nGood = 0, nBad = 0;
    for (size_t s = 0; s < asked.size(); s++) {
      const GUID* pAll = _AtlDbAllSet(asked[s].guidPropertySet);
      const GUID& guid = pAll != NULL ? *pAll : asked[s].guidPropertySet;
      if (asked[s].cPropertyIDs == 0) {
        for (size_t i = 0; i < m_rgProps.size(); i++)
          if (InlineIsEqualGUID(*m_rgProps[i].pguidSet, guid)) {
            found[s].push_back(&m_rgProps[i]);
            ids[s].push_back(m_rgProps[i].dwPropId);
          }
        if (found[s].empty()) nBad++;
      } else {
        for (ULONG i = 0; i < asked[s].cPropertyIDs; i++) {
          _ATL_PROPVAL* p = _FindProp(guid, asked[s].rgPropertyIDs[i], true);
          found[s].push_back(p);
          ids[s].push_back(asked[s].rgPropertyIDs[i]);
          if (p == NULL) nBad++;
        }
      }
      for (size_t i = 0; i < found[s].size(); i++)
        if (found[s][i] != NULL) {
          nGood++;
          cchDesc += wcslen(found[s][i]->pwszName) + 1;
        }
    }
    OLECHAR* pDesc = NULL;
    if (ppDescBuffer != NULL && cchDesc > 0) {
      pDesc = (OLECHAR*)::CoTaskMemAlloc(cchDesc * sizeof(OLECHAR));
      if (pDesc == NULL) return E_OUTOFMEMORY;
    }
    OLECHAR* pNext = pDesc;
    DBPROPINFOSET* pSets = (DBPROPINFOSET*)::CoTaskMemAlloc(asked.size() * sizeof(DBPROPINFOSET));
    if (pSets == NULL) {
      ::CoTaskMemFree(pDesc);
      return E_OUTOFMEMORY;
    }
    for (size_t s = 0; s < asked.size(); s++) {
      DBPROPINFOSET& out = pSets[s];
      out.guidPropertySet = asked[s].guidPropertySet;
      out.cPropertyInfos = (ULONG)found[s].size();
      out.rgPropertyInfos = NULL;
      if (found[s].empty()) continue;
      out.rgPropertyInfos = (DBPROPINFO*)::CoTaskMemAlloc(found[s].size() * sizeof(DBPROPINFO));
      for (size_t i = 0; i < found[s].size() && out.rgPropertyInfos != NULL; i++) {
        DBPROPINFO& info = out.rgPropertyInfos[i];
        _ATL_PROPVAL* p = found[s][i];
        info.dwPropertyID = ids[s][i];
        info.pwszDescription = NULL;
        ::VariantInit(&info.vValues);
        if (p == NULL) {
          info.dwFlags = DBPROPFLAGS_NOTSUPPORTED;
          info.vtType = VT_EMPTY;
          continue;
        }
        info.dwFlags = p->dwFlags;
        info.vtType = p->vt;
        if (pNext != NULL) {
          size_t n = wcslen(p->pwszName) + 1;
          memcpy(pNext, p->pwszName, n * sizeof(OLECHAR));
          info.pwszDescription = pNext;
          pNext += n;
        }
      }
    }
    *pcPropertyInfoSets = (ULONG)asked.size();
    *prgPropertyInfoSets = pSets;
    if (ppDescBuffer != NULL) *ppDescBuffer = pDesc;
    if (nBad == 0) return S_OK;
    return nGood == 0 ? DB_E_ERRORSOCCURRED : DB_S_ERRORSOCCURRED;
  }

private:
  void _AddMap(const _ATL_PROPMAP_ENTRY* pMap, bool bInfoOnly) {
    const GUID* pguidSet = NULL;
    for (; pMap->nKind != 0; pMap++) {
      switch (pMap->nKind) {
        case 1:
          pguidSet = pMap->pguidSet;
          _SetIndex(*pguidSet);
          break;
        case 2: {
          if (pguidSet == NULL || _FindProp(*pguidSet, pMap->dwPropId, true) != NULL) break;
          _ATL_PROPVAL p;
          p.pguidSet = pguidSet;
          p.iSet = _SetIndex(*pguidSet);
          p.dwPropId = pMap->dwPropId;
          p.vt = pMap->vt;
          p.dwFlags = pMap->dwFlags;
          p.dwOptions = pMap->dwOptions;
          p.pwszName = pMap->pwszName;
          p.bInfoOnly = bInfoOnly;
          _DefaultValue(pMap, &p.varDefault);
          p.var = p.varDefault;
          m_rgProps.push_back(p);
          break;
        }
        case 3:
          pguidSet = NULL;
          break;
        case 4:
          if (pMap->pfnChain != NULL) _AddMap(pMap->pfnChain(), true);
          break;
      }
    }
  }
  ULONG _SetIndex(const GUID& guid) {
    for (size_t i = 0; i < m_rgSets.size(); i++)
      if (InlineIsEqualGUID(*m_rgSets[i], guid)) return (ULONG)i;
    m_rgSets.push_back(&guid);
    return (ULONG)m_rgSets.size() - 1;
  }
  static void _DefaultValue(const _ATL_PROPMAP_ENTRY* p, CComVariant* pv) {
    switch (p->vt) {
      case VT_BOOL: *pv = p->lValue != 0; break;  // CComVariant(bool) is VT_BOOL
      case VT_BSTR: *pv = p->lValue != 0 ? (LPCOLESTR)p->lValue : L""; break;
      case VT_I2: pv->Clear(); pv->vt = VT_I2; pv->iVal = (SHORT)p->lValue; break;
      case VT_I8: pv->Clear(); pv->vt = VT_I8; pv->llVal = (LONGLONG)p->lValue; break;
      case VT_EMPTY: pv->Clear(); break;
      default: pv->Clear(); pv->vt = VT_I4; pv->lVal = (LONG)p->lValue; break;
    }
  }
  static void _FreeSet(DBPROPSET& set) {
    for (ULONG i = 0; i < set.cProperties && set.rgProperties != NULL; i++)
      ::VariantClear(&set.rgProperties[i].vValue);
    ::CoTaskMemFree(set.rgProperties);
  }
  DBPROPSTATUS _SetOne(const GUID& guidSet, ULONG iSet, DBPROP& prop, const GUID* pguidOnly,
                       const GUID* pguidNotSettable) {
    if (pguidOnly != NULL && !InlineIsEqualGUID(guidSet, *pguidOnly)) return DBPROPSTATUS_NOTSUPPORTED;
    _ATL_PROPVAL* p = _FindProp(guidSet, prop.dwPropertyID, false);
    if (p == NULL) return DBPROPSTATUS_NOTSUPPORTED;
    if (prop.dwOptions != DBPROPOPTIONS_REQUIRED && prop.dwOptions != DBPROPOPTIONS_OPTIONAL)
      return DBPROPSTATUS_BADOPTION;
    // VT_EMPTY: back to the default.
    CComVariant v;
    if (prop.vValue.vt == VT_EMPTY) {
      v = p->varDefault;
    } else if (prop.vValue.vt == p->vt) {
      v = prop.vValue;
    } else if (FAILED(v.ChangeType(p->vt, &prop.vValue))) {
      return DBPROPSTATUS_BADVALUE;
    }
    bool bSame = v == p->var;
    bool bSettable = (p->dwFlags & DBPROPFLAGS_WRITE) != 0 &&
                     (pguidNotSettable == NULL || !InlineIsEqualGUID(guidSet, *pguidNotSettable));
    if (!bSettable) return bSame ? DBPROPSTATUS_OK : DBPROPSTATUS_NOTSETTABLE;
    DBPROP checked = prop;
    checked.vValue = v;  // shallow: v keeps it
    if (FAILED(IsValidValue(iSet, &checked))) return DBPROPSTATUS_BADVALUE;
    p->var = v;
    p->dwOptions = prop.dwOptions;
    OnPropertyChanged(iSet, &checked);
    return DBPROPSTATUS_OK;
  }
};

// IDBProperties for a data source: before Initialize only the DBINIT
// properties may be had, and after it they may not be changed.
template <class T>
class ATL_NO_VTABLE IDBPropertiesImpl : public IDBProperties, public CUtlProps<T> {
public:
  STDMETHOD(GetProperties)(ULONG cPropertyIDSets, const DBPROPIDSET rgPropertyIDSets[],
                           ULONG* pcPropertySets, DBPROPSET** prgPropertySets) override {
    T* pT = static_cast<T*>(this);
    bool bInit = (pT->m_dwStatus & DSF_INITIALIZED) != 0;
    return this->_GetProperties(cPropertyIDSets, rgPropertyIDSets, pcPropertySets, prgPropertySets,
                                bInit ? NULL : &DBPROPSET_DBINIT);
  }
  STDMETHOD(GetPropertyInfo)(ULONG cPropertyIDSets, const DBPROPIDSET rgPropertyIDSets[],
                             ULONG* pcPropertyInfoSets, DBPROPINFOSET** prgPropertyInfoSets,
                             OLECHAR** ppDescBuffer) override {
    return this->_GetPropertyInfo(cPropertyIDSets, rgPropertyIDSets, pcPropertyInfoSets,
                                  prgPropertyInfoSets, ppDescBuffer);
  }
  STDMETHOD(SetProperties)(ULONG cPropertySets, DBPROPSET rgPropertySets[]) override {
    T* pT = static_cast<T*>(this);
    bool bInit = (pT->m_dwStatus & DSF_INITIALIZED) != 0;
    return this->_SetProperties(cPropertySets, rgPropertySets, bInit ? NULL : &DBPROPSET_DBINIT,
                                bInit ? &DBPROPSET_DBINIT : NULL);
  }
};

template <class T, class PropClass = T>
class ATL_NO_VTABLE ISessionPropertiesImpl : public ISessionProperties, public CUtlProps<PropClass> {
public:
  STDMETHOD(GetProperties)(ULONG cPropertyIDSets, const DBPROPIDSET rgPropertyIDSets[],
                           ULONG* pcPropertySets, DBPROPSET** prgPropertySets) override {
    return this->_GetProperties(cPropertyIDSets, rgPropertyIDSets, pcPropertySets, prgPropertySets);
  }
  STDMETHOD(SetProperties)(ULONG cPropertySets, DBPROPSET rgPropertySets[]) override {
    return this->_SetProperties(cPropertySets, rgPropertySets);
  }
};

template <class T, class PropClass = T>
class ATL_NO_VTABLE ICommandPropertiesImpl : public ICommandProperties, public CUtlProps<PropClass> {
public:
  STDMETHOD(GetProperties)(const ULONG cPropertyIDSets, const DBPROPIDSET rgPropertyIDSets[],
                           ULONG* pcPropertySets, DBPROPSET** prgPropertySets) override {
    return this->_GetProperties(cPropertyIDSets, rgPropertyIDSets, pcPropertySets, prgPropertySets);
  }
  STDMETHOD(SetProperties)(ULONG cPropertySets, DBPROPSET rgPropertySets[]) override {
    return this->_SetProperties(cPropertySets, rgPropertySets);
  }
};

// ---------------------------------------------------------------------
// Pieces every object of a provider may have.

// What converts values (IConvertTypeImpl's base). The conversions are the
// header's own; m_spConvert is there for a provider that wants the data
// conversion library itself.
class CConvertHelper {
public:
  CComPtr<IDataConvert> m_spConvert;
  HRESULT FinalConstruct() { return S_OK; }
};

template <class T>
class ATL_NO_VTABLE IConvertTypeImpl : public IConvertType, public CConvertHelper {
public:
  STDMETHOD(CanConvert)(DBTYPE wFromType, DBTYPE wToType, DBCONVERTFLAGS dwConvertFlags) override {
    if (dwConvertFlags & ~(DBCONVERTFLAGS_COLUMN | DBCONVERTFLAGS_PARAMETER | DBCONVERTFLAGS_ISFIXEDLENGTH |
                           DBCONVERTFLAGS_ISLONG | DBCONVERTFLAGS_FROMVARIANT))
      return DB_E_BADCONVERTFLAG;
    if ((dwConvertFlags & DBCONVERTFLAGS_COLUMN) && (dwConvertFlags & DBCONVERTFLAGS_PARAMETER))
      return DB_E_BADCONVERTFLAG;
    DBTYPE wFrom = wFromType & ~DBTYPE_BYREF, wTo = wToType & ~DBTYPE_BYREF;
    if (!_AtlDbKnownType(wFrom) && wFrom != DBTYPE_NUMERIC) return DB_E_BADTYPE;
    if (!_AtlDbKnownType(wTo) && wTo != DBTYPE_NUMERIC) return DB_E_BADTYPE;
    return _AtlDbCanConvert(wFrom, wTo) ? S_OK : S_FALSE;
  }
};

// IColumnsInfo over T::GetColumnInfo.
template <class T>
class ATL_NO_VTABLE IColumnsInfoImpl : public IColumnsInfo {
public:
  STDMETHOD(GetColumnInfo)(DBORDINAL* pcColumns, DBCOLUMNINFO** prgInfo,
                           OLECHAR** ppStringsBuffer) override {
    if (pcColumns != NULL) *pcColumns = 0;
    if (prgInfo != NULL) *prgInfo = NULL;
    if (ppStringsBuffer != NULL) *ppStringsBuffer = NULL;
    if (pcColumns == NULL || prgInfo == NULL || ppStringsBuffer == NULL) return E_INVALIDARG;
    T* pT = static_cast<T*>(this);
    DBORDINAL cCols = 0;
    ATLCOLUMNINFO* pCols = T::GetColumnInfo(pT, &cCols);
    if (cCols == 0) return S_OK;
    size_t cch = 0;
    for (DBORDINAL i = 0; i < cCols; i++)
      if (pCols[i].pwszName != NULL) cch += wcslen(pCols[i].pwszName) + 1;
    DBCOLUMNINFO* pInfo = (DBCOLUMNINFO*)::CoTaskMemAlloc((size_t)cCols * sizeof(DBCOLUMNINFO));
    OLECHAR* pStrings = cch > 0 ? (OLECHAR*)::CoTaskMemAlloc(cch * sizeof(OLECHAR)) : NULL;
    if (pInfo == NULL || (cch > 0 && pStrings == NULL)) {
      ::CoTaskMemFree(pInfo);
      ::CoTaskMemFree(pStrings);
      return E_OUTOFMEMORY;
    }
    OLECHAR* pNext = pStrings;
    for (DBORDINAL i = 0; i < cCols; i++) {
      DBCOLUMNINFO& c = pInfo[i];
      c.pwszName = NULL;
      c.pTypeInfo = NULL;
      c.iOrdinal = pCols[i].iOrdinal;
      c.dwFlags = pCols[i].dwFlags;
      c.ulColumnSize = pCols[i].ulColumnSize;
      c.wType = pCols[i].wType;
      c.bPrecision = pCols[i].bPrecision;
      c.bScale = pCols[i].bScale;
      c.columnid = pCols[i].columnid;
      if (pCols[i].pwszName != NULL) {
        size_t n = wcslen(pCols[i].pwszName) + 1;
        memcpy(pNext, pCols[i].pwszName, n * sizeof(OLECHAR));
        c.pwszName = pNext;
        if (c.columnid.eKind == DBKIND_NAME || c.columnid.eKind == DBKIND_GUID_NAME ||
            c.columnid.eKind == DBKIND_PGUID_NAME)
          c.columnid.uName.pwszName = pNext;
        pNext += n;
      }
    }
    *pcColumns = cCols;
    *prgInfo = pInfo;
    *ppStringsBuffer = pStrings;
    return S_OK;
  }
  STDMETHOD(MapColumnIDs)(DBORDINAL cColumnIDs, const DBID rgColumnIDs[],
                          DBORDINAL rgColumns[]) override {
    if (cColumnIDs == 0) return S_OK;
    if (rgColumnIDs == NULL || rgColumns == NULL) return E_INVALIDARG;
    T* pT = static_cast<T*>(this);
    DBORDINAL cCols = 0;
    ATLCOLUMNINFO* pCols = T::GetColumnInfo(pT, &cCols);
    DBORDINAL nFound = 0;
    for (DBORDINAL i = 0; i < cColumnIDs; i++) {
      rgColumns[i] = DB_INVALIDCOLUMN;
      const DBID& id = rgColumnIDs[i];
      for (DBORDINAL c = 0; c < cCols; c++) {
        const DBID& mine = pCols[c].columnid;
        bool bMatch = false;
        if ((id.eKind == DBKIND_NAME || id.eKind == DBKIND_GUID_NAME) && id.uName.pwszName != NULL &&
            pCols[c].pwszName != NULL)
          bMatch = ::lstrcmpiW(id.uName.pwszName, pCols[c].pwszName) == 0;
        else if (id.eKind == mine.eKind && id.eKind == DBKIND_GUID_PROPID)
          bMatch = InlineIsEqualGUID(id.uGuid.guid, mine.uGuid.guid) &&
                   id.uName.ulPropid == mine.uName.ulPropid;
        if (bMatch) {
          rgColumns[i] = pCols[c].iOrdinal;
          nFound++;
          break;
        }
      }
    }
    if (nFound == cColumnIDs) return S_OK;
    return nFound == 0 ? DB_E_ERRORSOCCURRED : DB_S_ERRORSOCCURRED;
  }
};

// An accessor: a copy of the consumer's bindings.
struct ATLBINDINGS {
  DBBINDING* pBindings;
  DBREFCOUNT dwRef;
  DBCOUNTITEM cBindings;
  DBACCESSORFLAGS dwAccessorFlags;
};

class IAccessorImplBase : public IAccessor {
public:
  CAtlMap<HACCESSOR, ATLBINDINGS*> m_rgBindings;
  ULONG_PTR m_nNextAccessor;

  IAccessorImplBase() : m_nNextAccessor(0) {}
  virtual ~IAccessorImplBase() { _ReleaseAllAccessors(); }

  ATLBINDINGS* _FindAccessor(HACCESSOR h) {
    ATLBINDINGS* p = NULL;
    return m_rgBindings.Lookup(h, p) ? p : NULL;
  }
  static ATLBINDINGS* _CopyBindings(const DBBINDING* rgBindings, DBCOUNTITEM cBindings,
                                    DBACCESSORFLAGS dwFlags) {
    ATLBINDINGS* p = new (std::nothrow) ATLBINDINGS;
    if (p == NULL) return NULL;
    p->dwRef = 1;
    p->cBindings = cBindings;
    p->dwAccessorFlags = dwFlags;
    p->pBindings = NULL;
    if (cBindings > 0) {
      p->pBindings = new (std::nothrow) DBBINDING[(size_t)cBindings];
      if (p->pBindings == NULL) {
        delete p;
        return NULL;
      }
      for (DBCOUNTITEM i = 0; i < cBindings; i++) {
        p->pBindings[i] = rgBindings[i];
        p->pBindings[i].pTypeInfo = NULL;
        p->pBindings[i].pBindExt = NULL;
        if (rgBindings[i].pObject != NULL) {
          p->pBindings[i].pObject = new (std::nothrow) DBOBJECT(*rgBindings[i].pObject);
        }
      }
    }
    return p;
  }
  static void _FreeBindings(ATLBINDINGS* p) {
    if (p == NULL) return;
    for (DBCOUNTITEM i = 0; i < p->cBindings; i++) delete p->pBindings[i].pObject;
    delete[] p->pBindings;
    delete p;
  }
  HACCESSOR _NewHandle() {
    HACCESSOR h;
    do {
      h = (HACCESSOR)++m_nNextAccessor;
    } while (h == DB_NULL_HACCESSOR || _FindAccessor(h) != NULL);
    return h;
  }
  void _ReleaseAllAccessors() {
    POSITION pos = m_rgBindings.GetStartPosition();
    while (pos != NULL) _FreeBindings(m_rgBindings.GetNextValue(pos));
    m_rgBindings.RemoveAll();
  }
  // A command's accessors, for the rowset it made: the same handles.
  void _InheritAccessors(IAccessorImplBase* pFrom) {
    POSITION pos = pFrom->m_rgBindings.GetStartPosition();
    while (pos != NULL) {
      HACCESSOR h = pFrom->m_rgBindings.GetKeyAt(pos);
      ATLBINDINGS* p = pFrom->m_rgBindings.GetNextValue(pos);
      if (_FindAccessor(h) != NULL) continue;
      ATLBINDINGS* pCopy = _CopyBindings(p->pBindings, p->cBindings, p->dwAccessorFlags);
      if (pCopy != NULL) m_rgBindings.SetAt(h, pCopy);
      if ((ULONG_PTR)h > m_nNextAccessor) m_nNextAccessor = (ULONG_PTR)h;
    }
  }
};

template <class T, class BindType = ATLBINDINGS,
          class BindingVector = CAtlMap<HACCESSOR, BindType*> >
class ATL_NO_VTABLE IAccessorImpl : public IAccessorImplBase {
public:
  HRESULT FinalConstruct() { return S_OK; }
  void FinalRelease() { _ReleaseAllAccessors(); }

  STDMETHOD(AddRefAccessor)(HACCESSOR hAccessor, DBREFCOUNT* pcRefCount) override {
    ATLBINDINGS* p = _FindAccessor(hAccessor);
    if (p == NULL) return DB_E_BADACCESSORHANDLE;
    p->dwRef++;
    if (pcRefCount != NULL) *pcRefCount = p->dwRef;
    return S_OK;
  }
  STDMETHOD(CreateAccessor)(DBACCESSORFLAGS dwAccessorFlags, DBCOUNTITEM cBindings,
                            const DBBINDING rgBindings[], DBLENGTH /*cbRowSize*/,
                            HACCESSOR* phAccessor, DBBINDSTATUS rgStatus[]) override {
    if (phAccessor == NULL) return E_INVALIDARG;
    *phAccessor = DB_NULL_HACCESSOR;
    if (cBindings > 0 && rgBindings == NULL) return E_INVALIDARG;
    if (dwAccessorFlags & ~(DBACCESSOR_PASSBYREF | DBACCESSOR_ROWDATA | DBACCESSOR_PARAMETERDATA |
                            DBACCESSOR_OPTIMIZED | DBACCESSOR_INHERITED))
      return DB_E_BADACCESSORFLAGS;
    if ((dwAccessorFlags & (DBACCESSOR_ROWDATA | DBACCESSOR_PARAMETERDATA)) == 0)
      return DB_E_BADACCESSORFLAGS;
    if (dwAccessorFlags & DBACCESSOR_PASSBYREF) return DB_E_BYREFACCESSORNOTSUPPORTED;
    if (cBindings == 0 && !(dwAccessorFlags & DBACCESSOR_PARAMETERDATA)) return DB_E_NULLACCESSORNOTSUPPORTED;
    // Rows' bindings name the columns there are.
    T* pT = static_cast<T*>(this);
    DBORDINAL cCols = 0;
    ATLCOLUMNINFO* pCols = T::GetColumnInfo(pT, &cCols);
    bool bRow = (dwAccessorFlags & DBACCESSOR_ROWDATA) != 0;
    bool bBad = false;
    for (DBCOUNTITEM i = 0; i < cBindings; i++) {
      DBBINDSTATUS st = DBBINDSTATUS_OK;
      const DBBINDING& b = rgBindings[i];
      DBTYPE wType = b.wType & ~DBTYPE_BYREF;
      if ((b.dwPart & (DBPART_VALUE | DBPART_LENGTH | DBPART_STATUS)) == 0 ||
          (b.dwPart & ~(DBPART_VALUE | DBPART_LENGTH | DBPART_STATUS)))
        st = DBBINDSTATUS_BADBINDINFO;
      else if (!_AtlDbKnownType(wType))
        st = DBBINDSTATUS_UNSUPPORTEDCONVERSION;
      else if ((b.wType & DBTYPE_BYREF) && !_AtlDbIsVarLength(wType))
        st = DBBINDSTATUS_UNSUPPORTEDCONVERSION;
      else if (bRow && cCols > 0) {
        ATLCOLUMNINFO* pCol = _AtlFindColumn(pCols, cCols, b.iOrdinal);
        if (pCol == NULL)
          st = DBBINDSTATUS_BADORDINAL;
        else if (!_AtlDbCanConvert(pCol->wType, wType))
          st = DBBINDSTATUS_UNSUPPORTEDCONVERSION;
      }
      if (rgStatus != NULL) rgStatus[i] = st;
      if (st != DBBINDSTATUS_OK) bBad = true;
    }
    if (bBad) return DB_E_ERRORSOCCURRED;
    ATLBINDINGS* p = _CopyBindings(rgBindings, cBindings, dwAccessorFlags);
    if (p == NULL) return E_OUTOFMEMORY;
    HACCESSOR h = _NewHandle();
    m_rgBindings.SetAt(h, p);
    *phAccessor = h;
    return S_OK;
  }
  STDMETHOD(GetBindings)(HACCESSOR hAccessor, DBACCESSORFLAGS* pdwAccessorFlags,
                         DBCOUNTITEM* pcBindings, DBBINDING** prgBindings) override {
    if (pdwAccessorFlags != NULL) *pdwAccessorFlags = DBACCESSOR_INVALID;
    if (pcBindings != NULL) *pcBindings = 0;
    if (prgBindings != NULL) *prgBindings = NULL;
    if (pdwAccessorFlags == NULL || pcBindings == NULL || prgBindings == NULL) return E_INVALIDARG;
    ATLBINDINGS* p = _FindAccessor(hAccessor);
    if (p == NULL) return DB_E_BADACCESSORHANDLE;
    *pdwAccessorFlags = p->dwAccessorFlags;
    if (p->cBindings == 0) return S_OK;
    DBBINDING* pOut = (DBBINDING*)::CoTaskMemAlloc((size_t)p->cBindings * sizeof(DBBINDING));
    if (pOut == NULL) return E_OUTOFMEMORY;
    for (DBCOUNTITEM i = 0; i < p->cBindings; i++) {
      pOut[i] = p->pBindings[i];
      if (p->pBindings[i].pObject != NULL) {
        pOut[i].pObject = (DBOBJECT*)::CoTaskMemAlloc(sizeof(DBOBJECT));
        if (pOut[i].pObject != NULL) *pOut[i].pObject = *p->pBindings[i].pObject;
      }
    }
    *pcBindings = p->cBindings;
    *prgBindings = pOut;
    return S_OK;
  }
  STDMETHOD(ReleaseAccessor)(HACCESSOR hAccessor, DBREFCOUNT* pcRefCount) override {
    ATLBINDINGS* p = _FindAccessor(hAccessor);
    if (p == NULL) return DB_E_BADACCESSORHANDLE;
    DBREFCOUNT n = --p->dwRef;
    if (pcRefCount != NULL) *pcRefCount = n;
    if (n == 0) {
      m_rgBindings.RemoveKey(hAccessor);
      _FreeBindings(p);
    }
    return S_OK;
  }
};

}  // namespace ATL

// The connection a session keeps to its data source (and a rowset to
// its command): the data source counts its sessions with it.
struct IInternalConnection : public IUnknown {
  STDMETHOD(AddConnection)() = 0;
  STDMETHOD(ReleaseConnection)() = 0;
};
// {72A7994A-3092-11D1-BB82-00C04FB66618}
__CRT_UUID_DECL(IInternalConnection, 0x72a7994a, 0x3092, 0x11d1, 0xbb, 0x82, 0x00, 0xc0, 0x4f, 0xb6,
                0x66, 0x18)
#define IID_IInternalConnection __uuidof(IInternalConnection)

// Counting a session or rowset as one of its site's connections.
inline void _AtlDbConnect(IUnknown* pUnkSite, bool bAdd) {
  if (pUnkSite == NULL) return;
  IInternalConnection* p = NULL;
  if (FAILED(pUnkSite->QueryInterface(IID_IInternalConnection, (void**)&p)) || p == NULL) return;
  if (bAdd) p->AddConnection(); else p->ReleaseConnection();
  p->Release();
}

namespace ATL {

template <class T>
class ATL_NO_VTABLE IInternalConnectionImpl : public IInternalConnection {
public:
  LONG m_cSessionsOpen;
  IInternalConnectionImpl() : m_cSessionsOpen(0) {}
  STDMETHOD(AddConnection)() override {
    ::InterlockedIncrement(&m_cSessionsOpen);
    return S_OK;
  }
  STDMETHOD(ReleaseConnection)() override {
    ::InterlockedDecrement(&m_cSessionsOpen);
    return S_OK;
  }
};
template <class T>
class ATL_NO_VTABLE IInternalCommandConnectionImpl : public IInternalConnectionImpl<T> {};

// ---------------------------------------------------------------------
// The data source.

template <class T>
class ATL_NO_VTABLE IDBInitializeImpl : public IDBInitialize {
public:
  DWORD m_dwStatus;
  IDBInitializeImpl() : m_dwStatus(0) {}
  STDMETHOD(Initialize)() override {
    if (m_dwStatus & DSF_INITIALIZED) return DB_E_ALREADYINITIALIZED;
    m_dwStatus |= DSF_INITIALIZED;
    return S_OK;
  }
  STDMETHOD(Uninitialize)() override {
    T* pT = static_cast<T*>(this);
    if constexpr (std::is_base_of<IInternalConnectionImpl<T>, T>::value) {
      if (pT->m_cSessionsOpen > 0) return DB_E_OBJECTOPEN;
    }
    (void)pT;
    m_dwStatus &= ~DSF_INITIALIZED;
    return S_OK;
  }
};

// Sessions, made only once the data source is initialized; each one's
// site is the data source.
template <class T, class SessionClass>
class ATL_NO_VTABLE IDBCreateSessionImpl : public IDBCreateSession {
public:
  STDMETHOD(CreateSession)(IUnknown* pUnkOuter, REFIID riid, IUnknown** ppDBSession) override {
    if (ppDBSession == NULL) return E_INVALIDARG;
    *ppDBSession = NULL;
    T* pT = static_cast<T*>(this);
    if (!(pT->m_dwStatus & DSF_INITIALIZED)) return E_UNEXPECTED;
    if (pUnkOuter != NULL && !InlineIsEqualGUID(riid, IID_IUnknown)) return DB_E_NOAGGREGATION;
    CComPolyObject<SessionClass>* pSession = NULL;
    HRESULT hr = CComPolyObject<SessionClass>::CreateInstance(pUnkOuter, &pSession);
    if (FAILED(hr)) return hr;
    CComPtr<IUnknown> spUnk(pSession);
    CComPtr<IObjectWithSite> spSite;
    if (SUCCEEDED(pSession->QueryInterface(IID_IObjectWithSite, (void**)&spSite)))
      spSite->SetSite(pT->GetUnknown());
    return pSession->QueryInterface(riid, (void**)ppDBSession);
  }
};

// ---------------------------------------------------------------------
// Sessions.

// A session's site, whose connection the data source counts.
template <class T>
class ATL_NO_VTABLE IObjectWithSiteSessionImpl : public IObjectWithSiteImpl<T> {
public:
  ~IObjectWithSiteSessionImpl() { _Disconnect(); }
  STDMETHOD(SetSite)(IUnknown* pUnkSite) override {
    _Disconnect();
    this->m_spUnkSite = pUnkSite;
    _AtlDbConnect(pUnkSite, true);
    return S_OK;
  }

private:
  void _Disconnect() {
    if (this->m_spUnkSite == NULL) return;
    _AtlDbConnect(this->m_spUnkSite, false);
    this->m_spUnkSite.Release();
  }
};

template <class T>
class ATL_NO_VTABLE IGetDataSourceImpl : public IGetDataSource {
public:
  STDMETHOD(GetDataSource)(REFIID riid, IUnknown** ppDataSource) override {
    if (ppDataSource == NULL) return E_INVALIDARG;
    *ppDataSource = NULL;
    T* pT = static_cast<T*>(this);
    if (pT->m_spUnkSite == NULL) return E_FAIL;
    return pT->m_spUnkSite->QueryInterface(riid, (void**)ppDataSource);
  }
};

// Commands, each with the session as its site.
template <class T, class CommandClass>
class ATL_NO_VTABLE IDBCreateCommandImpl : public IDBCreateCommand {
public:
  STDMETHOD(CreateCommand)(IUnknown* pUnkOuter, REFIID riid, IUnknown** ppvCommand) override {
    if (ppvCommand == NULL) return E_INVALIDARG;
    *ppvCommand = NULL;
    if (pUnkOuter != NULL && !InlineIsEqualGUID(riid, IID_IUnknown)) return DB_E_NOAGGREGATION;
    T* pT = static_cast<T*>(this);
    CComPolyObject<CommandClass>* pCommand = NULL;
    HRESULT hr = CComPolyObject<CommandClass>::CreateInstance(pUnkOuter, &pCommand);
    if (FAILED(hr)) return hr;
    CComPtr<IUnknown> spUnk(pCommand);
    CComPtr<IObjectWithSite> spSite;
    if (SUCCEEDED(pCommand->QueryInterface(IID_IObjectWithSite, (void**)&spSite)))
      spSite->SetSite(pT->GetUnknown());
    return pCommand->QueryInterface(riid, (void**)ppvCommand);
  }
};

// Making a rowset of a session or a command: its properties, its site,
// then its Execute; RowsetClass's object is pRowsetObj after.
template <class RowsetClass, class Creator>
HRESULT _AtlCreateRowset(Creator* pCreator, IUnknown* pUnkOuter, REFIID riid, ULONG cPropertySets,
                         DBPROPSET rgPropertySets[], IUnknown** ppRowset,
                         RowsetClass*& pRowsetObj, CComPtr<IUnknown>& spRowset) {
  pRowsetObj = NULL;
  if (ppRowset != NULL) *ppRowset = NULL;
  if (pUnkOuter != NULL && !InlineIsEqualGUID(riid, IID_IUnknown)) return DB_E_NOAGGREGATION;
  CComPolyObject<RowsetClass>* pPoly = NULL;
  HRESULT hr = CComPolyObject<RowsetClass>::CreateInstance(pUnkOuter, &pPoly);
  if (FAILED(hr)) return hr;
  spRowset = pPoly;
  pRowsetObj = &pPoly->m_contained;
  pRowsetObj->_InitProps();
  if constexpr (std::is_base_of<CUtlProps<typename RowsetClass::_RowsetCreatorClass>, Creator>::value)
    pRowsetObj->_CopyProps(static_cast<CUtlProps<typename RowsetClass::_RowsetCreatorClass>*>(pCreator));
  HRESULT hrProps = S_OK;
  if (cPropertySets > 0) {
    hrProps = pRowsetObj->_SetProperties(cPropertySets, rgPropertySets, &DBPROPSET_ROWSET);
    if (hrProps == DB_E_ERRORSOCCURRED) return hrProps;
  }
  if constexpr (std::is_base_of<IAccessorImplBase, Creator>::value)
    pRowsetObj->_InheritAccessors(static_cast<IAccessorImplBase*>(pCreator));
  pRowsetObj->SetSite(pCreator->GetUnknown());
  return hrProps;
}

template <class SessionClass>
class ATL_NO_VTABLE IOpenRowsetImpl : public IOpenRowset {
public:
  template <class RowsetClass>
  HRESULT CreateRowset(IUnknown* pUnkOuter, DBID* pTableID, DBID* pIndexID, REFIID riid,
                       ULONG cPropertySets, DBPROPSET rgPropertySets[], IUnknown** ppRowset,
                       RowsetClass*& pRowsetObj) {
    pRowsetObj = NULL;
    if (ppRowset != NULL) *ppRowset = NULL;
    if (pTableID == NULL && pIndexID == NULL) return E_INVALIDARG;
    if (cPropertySets > 0 && rgPropertySets == NULL) return E_INVALIDARG;
    if (pIndexID != NULL) return DB_E_NOINDEX;
    if (pTableID->eKind != DBKIND_NAME || pTableID->uName.pwszName == NULL) return DB_E_NOTABLE;
    SessionClass* pT = static_cast<SessionClass*>(this);
    CComPtr<IUnknown> spRowset;
    HRESULT hr = _AtlCreateRowset<RowsetClass>(pT, pUnkOuter, riid, cPropertySets, rgPropertySets,
                                               ppRowset, pRowsetObj, spRowset);
    if (FAILED(hr)) return hr;
    HRESULT hrProps = hr;
    pRowsetObj->m_strCommandText = pTableID->uName.pwszName;
    DBROWCOUNT cRowsAffected = 0;
    hr = pRowsetObj->Execute(NULL, &cRowsAffected);
    if (FAILED(hr)) return hr;
    if (ppRowset == NULL || InlineIsEqualGUID(riid, IID_NULL)) return hrProps;
    hr = spRowset->QueryInterface(riid, (void**)ppRowset);
    return FAILED(hr) ? hr : hrProps;
  }
};

// ---------------------------------------------------------------------
// Commands.

template <class T, class CommandBase = ICommand>
class ATL_NO_VTABLE ICommandImpl : public CommandBase {
public:
  bool m_bIsExecuting;
  bool m_bCancelWhenExecuting;
  bool m_bCancel;
  ICommandImpl() : m_bIsExecuting(false), m_bCancelWhenExecuting(true), m_bCancel(false) {}

  STDMETHOD(Cancel)() override {
    m_bCancel = true;
    return m_bIsExecuting && !m_bCancelWhenExecuting ? DB_E_CANTCANCEL : S_OK;
  }
  STDMETHOD(GetDBSession)(REFIID riid, IUnknown** ppSession) override {
    if (ppSession == NULL) return E_INVALIDARG;
    *ppSession = NULL;
    T* pT = static_cast<T*>(this);
    if (pT->m_spUnkSite == NULL) return S_FALSE;
    return pT->m_spUnkSite->QueryInterface(riid, (void**)ppSession);
  }

  template <class RowsetClass>
  HRESULT CreateRowset(IUnknown* pUnkOuter, REFIID riid, DBPARAMS* pParams,
                       DBROWCOUNT* pcRowsAffected, IUnknown** ppRowset, RowsetClass*& pRowsetObj) {
    pRowsetObj = NULL;
    if (ppRowset != NULL) *ppRowset = NULL;
    if (!InlineIsEqualGUID(riid, IID_NULL) && ppRowset == NULL) return E_INVALIDARG;
    T* pT = static_cast<T*>(this);
    if (pT->m_strCommandText.Length() == 0) return DB_E_NOCOMMAND;
    CComPtr<IUnknown> spRowset;
    HRESULT hr = _AtlCreateRowset<RowsetClass>(pT, pUnkOuter, riid, 0, NULL, ppRowset,
                                               pRowsetObj, spRowset);
    if (FAILED(hr)) return hr;
    pRowsetObj->m_strCommandText = pT->m_strCommandText;
    DBROWCOUNT cRows = 0;
    m_bIsExecuting = true;
    m_bCancel = false;
    hr = pRowsetObj->Execute(pParams, pcRowsAffected != NULL ? pcRowsAffected : &cRows);
    m_bIsExecuting = false;
    if (FAILED(hr)) return hr;
    if (ppRowset == NULL || InlineIsEqualGUID(riid, IID_NULL)) return hr;
    HRESULT hrQI = spRowset->QueryInterface(riid, (void**)ppRowset);
    return FAILED(hrQI) ? hrQI : hr;
  }
};

template <class T>
class ATL_NO_VTABLE ICommandTextImpl : public ICommandImpl<T, ICommandText> {
public:
  CComBSTR m_strCommandText;
  GUID m_guidDialect;
  ICommandTextImpl() : m_guidDialect(DBGUID_DEFAULT) {}

  STDMETHOD(GetCommandText)(GUID* pguidDialect, LPOLESTR* ppwszCommand) override {
    if (ppwszCommand == NULL) {
      if (pguidDialect != NULL) *pguidDialect = DB_NULLGUID;
      return E_INVALIDARG;
    }
    *ppwszCommand = NULL;
    if (m_strCommandText.Length() == 0) {
      if (pguidDialect != NULL) *pguidDialect = DB_NULLGUID;
      return DB_E_NOCOMMAND;
    }
    UINT n = m_strCommandText.Length();
    *ppwszCommand = (LPOLESTR)::CoTaskMemAlloc((n + 1) * sizeof(OLECHAR));
    if (*ppwszCommand == NULL) return E_OUTOFMEMORY;
    memcpy(*ppwszCommand, (BSTR)m_strCommandText, (n + 1) * sizeof(OLECHAR));
    HRESULT hr = S_OK;
    if (pguidDialect != NULL) {
      if (!InlineIsEqualGUID(*pguidDialect, m_guidDialect) &&
          !InlineIsEqualGUID(*pguidDialect, DBGUID_DEFAULT))
        hr = DB_S_DIALECTIGNORED;
      *pguidDialect = m_guidDialect;
    }
    return hr;
  }
  STDMETHOD(SetCommandText)(REFGUID rguidDialect, LPCOLESTR pwszCommand) override {
    if (this->m_bIsExecuting) return DB_E_OBJECTOPEN;
    if (!InlineIsEqualGUID(rguidDialect, DBGUID_DEFAULT) && !InlineIsEqualGUID(rguidDialect, DBGUID_SQL))
      return DB_E_DIALECTNOTSUPPORTED;
    m_strCommandText = pwszCommand;
    m_guidDialect = rguidDialect;
    return S_OK;
  }
};

// ---------------------------------------------------------------------
// Rowsets.

// A row handle: its place in the rowset's array, and its count.
class CSimpleRow {
public:
  typedef DBCOUNTITEM KeyType;
  DBREFCOUNT m_dwRef;
  KeyType m_iRowset;
  DBPENDINGSTATUS m_status;

  CSimpleRow(KeyType iRowsetCur) : m_dwRef(0), m_iRowset(iRowsetCur), m_status(DBPENDINGSTATUS_UNCHANGED) {}
  DBREFCOUNT AddRefRow() { return ++m_dwRef; }
  DBREFCOUNT ReleaseRow() { return --m_dwRef; }
  HRESULT Compare(CSimpleRow* pRow) {
    return pRow != NULL && pRow->m_iRowset == m_iRowset ? S_OK : S_FALSE;
  }
};

// The number of rows in an array of either kind (CAtlArray, CSimpleArray).
template <class A>
auto _AtlRowCount(A& a, int) -> decltype((size_t)a.GetCount()) { return (size_t)a.GetCount(); }
template <class A>
auto _AtlRowCount(A& a, long) -> decltype((size_t)a.GetSize()) { return (size_t)a.GetSize(); }

// IRowset over the array of rows. Row handles stay the same for a row
// while they are held; deleting a row moves the rows after it.
template <class T, class RowsetInterface = IRowset, class RowClass = CSimpleRow,
          class MapClass = CAtlMap<typename RowClass::KeyType, RowClass*> >
class ATL_NO_VTABLE IRowsetImpl : public RowsetInterface {
public:
  MapClass m_rgRowHandles;
  DBROWOFFSET m_iRowset;  // the next fetch position
  bool m_bReset;          // at the start: going back counts from the end
  bool m_bExternalFetch;
  bool m_bIRowsetUpdate;
  bool m_bCanScrollBack;
  bool m_bCanFetchBack;
  bool m_bRemoveDeleted;

  IRowsetImpl()
      : m_iRowset(0), m_bReset(true), m_bExternalFetch(false), m_bIRowsetUpdate(false),
        m_bCanScrollBack(true), m_bCanFetchBack(true), m_bRemoveDeleted(true) {}
  virtual ~IRowsetImpl() {
    POSITION pos = m_rgRowHandles.GetStartPosition();
    while (pos != NULL) delete m_rgRowHandles.GetNextValue(pos);
    m_rgRowHandles.RemoveAll();
  }

  // Overridable: a column's status in a row (DBSTATUS_S_ISNULL for no
  // value), and what a change to it stores.
  DBSTATUS GetDBStatus(RowClass* /*pRow*/, ATLCOLUMNINFO* /*pColInfo*/) { return DBSTATUS_S_OK; }

  RowClass* _FindRow(HROW hRow) {
    RowClass* p = NULL;
    if (!m_rgRowHandles.Lookup((typename RowClass::KeyType)hRow, p)) return NULL;
    return p;
  }
  // The handle for row n, made if there is none.
  RowClass* _RowAt(DBCOUNTITEM n) {
    POSITION pos = m_rgRowHandles.GetStartPosition();
    while (pos != NULL) {
      RowClass* p = m_rgRowHandles.GetNextValue(pos);
      if (p->m_iRowset == n && p->m_status != DBPENDINGSTATUS_DELETED) return p;
    }
    RowClass* p = new (std::nothrow) RowClass(n);
    if (p == NULL) return NULL;
    m_rgRowHandles.SetAt((typename RowClass::KeyType)p, p);
    return p;
  }
  size_t _RowCount() { return _AtlRowCount(static_cast<T*>(this)->m_rgRowData, 0); }

  STDMETHOD(AddRefRows)(DBCOUNTITEM cRows, const HROW rghRows[], DBREFCOUNT rgRefCounts[],
                        DBROWSTATUS rgRowStatus[]) override {
    if (cRows == 0) return S_OK;
    if (rghRows == NULL) return E_INVALIDARG;
    DBCOUNTITEM nBad = 0;
    for (DBCOUNTITEM i = 0; i < cRows; i++) {
      RowClass* p = _FindRow(rghRows[i]);
      DBROWSTATUS st = DBROWSTATUS_S_OK;
      DBREFCOUNT n = 0;
      if (p == NULL) st = DBROWSTATUS_E_INVALID;
      else if (p->m_status == DBPENDINGSTATUS_DELETED) st = DBROWSTATUS_E_DELETED;
      else n = p->AddRefRow();
      if (st != DBROWSTATUS_S_OK) nBad++;
      if (rgRefCounts != NULL) rgRefCounts[i] = n;
      if (rgRowStatus != NULL) rgRowStatus[i] = st;
    }
    if (nBad == 0) return S_OK;
    return nBad == cRows ? DB_E_ERRORSOCCURRED : DB_S_ERRORSOCCURRED;
  }

  STDMETHOD(GetData)(HROW hRow, HACCESSOR hAccessor, void* pDstData) override {
    T* pT = static_cast<T*>(this);
    RowClass* pRow = _FindRow(hRow);
    if (pRow == NULL) return DB_E_BADROWHANDLE;
    if (pRow->m_status == DBPENDINGSTATUS_DELETED) return DB_E_DELETEDROW;
    ATLBINDINGS* pBind = pT->_FindAccessor(hAccessor);
    if (pBind == NULL) return DB_E_BADACCESSORHANDLE;
    if (!(pBind->dwAccessorFlags & DBACCESSOR_ROWDATA)) return DB_E_BADACCESSORTYPE;
    if (pBind->cBindings == 0) return S_OK;
    if (pDstData == NULL) return E_INVALIDARG;
    DBORDINAL cCols = 0;
    ATLCOLUMNINFO* pCols = T::GetColumnInfo(pT, &cCols);
    BYTE* pSrcRow = (BYTE*)&pT->m_rgRowData[pRow->m_iRowset];
    DBCOUNTITEM nBad = 0;
    for (DBCOUNTITEM i = 0; i < pBind->cBindings; i++) {
      const DBBINDING& b = pBind->pBindings[i];
      BYTE* pData = (BYTE*)pDstData;
      ATLCOLUMNINFO* pCol = _AtlFindColumn(pCols, cCols, b.iOrdinal);
      DBSTATUS st = DBSTATUS_E_UNAVAILABLE;
      DBLENGTH cb = 0;
      if (pCol != NULL) {
        st = pT->GetDBStatus(pRow, pCol);
        if (st == DBSTATUS_S_OK) st = _GetValue(pCol, pSrcRow + pCol->cbOffset, b, pData, &cb);
        else if (st == DBSTATUS_S_ISNULL) cb = 0;
      }
      if (b.dwPart & DBPART_LENGTH) *(DBLENGTH*)(pData + b.obLength) = cb;
      if (b.dwPart & DBPART_STATUS) *(DBSTATUS*)(pData + b.obStatus) = st;
      if (st != DBSTATUS_S_OK && st != DBSTATUS_S_ISNULL && st != DBSTATUS_S_TRUNCATED) nBad++;
    }
    if (nBad == 0) return S_OK;
    return nBad == pBind->cBindings ? DB_E_ERRORSOCCURRED : DB_S_ERRORSOCCURRED;
  }

  STDMETHOD(GetNextRows)(HCHAPTER /*hReserved*/, DBROWOFFSET lRowsOffset, DBROWCOUNT cRows,
                         DBCOUNTITEM* pcRowsObtained, HROW** prghRows) override {
    if (pcRowsObtained == NULL || prghRows == NULL) return E_INVALIDARG;
    *pcRowsObtained = 0;
    if (cRows == 0) return S_OK;
    T* pT = static_cast<T*>(this);
    m_bCanScrollBack = pT->_GetBoolProp(DBPROPSET_ROWSET, DBPROP_CANSCROLLBACKWARDS, true);
    m_bCanFetchBack = pT->_GetBoolProp(DBPROPSET_ROWSET, DBPROP_CANFETCHBACKWARDS, true);
    if (lRowsOffset < 0 && !m_bCanScrollBack) return DB_E_CANTSCROLLBACKWARDS;
    if (cRows < 0 && !m_bCanFetchBack) return DB_E_CANTFETCHBACKWARDS;
    DBROWOFFSET n = (DBROWOFFSET)_RowCount();
    DBROWOFFSET p = m_iRowset;
    if (m_bReset && (lRowsOffset < 0 || cRows < 0)) p = n;
    m_bReset = false;
    DBROWOFFSET start = p + lRowsOffset;
    if (start < 0 || start > n) {
      m_iRowset = start < 0 ? 0 : n;
      return DB_S_ENDOFROWSET;
    }
    DBROWCOUNT want = cRows < 0 ? -cRows : cRows;
    std::vector<DBROWOFFSET> got;
    if (cRows > 0) {
      for (DBROWOFFSET i = start; i < n && (DBROWCOUNT)got.size() < want; i++) got.push_back(i);
      m_iRowset = start + (DBROWOFFSET)got.size();
    } else {
      for (DBROWOFFSET i = start - 1; i >= 0 && (DBROWCOUNT)got.size() < want; i--) got.push_back(i);
      m_iRowset = start - (DBROWOFFSET)got.size();
    }
    if (got.empty()) return DB_S_ENDOFROWSET;
    bool bAllocated = false;
    if (*prghRows == NULL) {
      *prghRows = (HROW*)::CoTaskMemAlloc(got.size() * sizeof(HROW));
      if (*prghRows == NULL) return E_OUTOFMEMORY;
      bAllocated = true;
    }
    for (size_t i = 0; i < got.size(); i++) {
      RowClass* pRow = _RowAt((DBCOUNTITEM)got[i]);
      if (pRow == NULL) {
        for (size_t k = 0; k < i; k++) ReleaseRows(1, &(*prghRows)[k], NULL, NULL, NULL);
        if (bAllocated) {
          ::CoTaskMemFree(*prghRows);
          *prghRows = NULL;
        }
        return E_OUTOFMEMORY;
      }
      pRow->AddRefRow();
      (*prghRows)[i] = (HROW)pRow;
    }
    *pcRowsObtained = got.size();
    return (DBROWCOUNT)got.size() < want ? DB_S_ENDOFROWSET : S_OK;
  }

  STDMETHOD(ReleaseRows)(DBCOUNTITEM cRows, const HROW rghRows[], DBROWOPTIONS /*rgRowOptions*/[],
                         DBREFCOUNT rgRefCounts[], DBROWSTATUS rgRowStatus[]) override {
    if (cRows == 0) return S_OK;
    if (rghRows == NULL) return E_INVALIDARG;
    DBCOUNTITEM nBad = 0;
    for (DBCOUNTITEM i = 0; i < cRows; i++) {
      RowClass* p = _FindRow(rghRows[i]);
      DBREFCOUNT n = 0;
      DBROWSTATUS st = DBROWSTATUS_S_OK;
      if (p == NULL || p->m_dwRef == 0) {
        st = DBROWSTATUS_E_INVALID;
        nBad++;
      } else {
        n = p->ReleaseRow();
        if (n == 0) {
          m_rgRowHandles.RemoveKey((typename RowClass::KeyType)p);
          delete p;
        }
      }
      if (rgRefCounts != NULL) rgRefCounts[i] = n;
      if (rgRowStatus != NULL) rgRowStatus[i] = st;
    }
    if (nBad == 0) return S_OK;
    return nBad == cRows ? DB_E_ERRORSOCCURRED : DB_S_ERRORSOCCURRED;
  }

  STDMETHOD(RestartPosition)(HCHAPTER /*hReserved*/) override {
    m_iRowset = 0;
    m_bReset = true;
    return S_OK;
  }

protected:
  // A column's value into a binding: by value, or by reference (memory
  // the consumer frees with CoTaskMemFree).
  static DBSTATUS _GetValue(ATLCOLUMNINFO* pCol, BYTE* pSrc, const DBBINDING& b, BYTE* pData,
                            DBLENGTH* pcb) {
    DBLENGTH cbSrc = _SourceLength(pCol, pSrc);
    DBTYPE wType = b.wType & ~DBTYPE_BYREF;
    if (b.wType & DBTYPE_BYREF) {
      DBLENGTH cbNeed = 0;
      _AtlDbConvert(pCol->wType, pSrc, cbSrc, wType, NULL, 0, &cbNeed);
      DBLENGTH cbAlloc = cbNeed + sizeof(WCHAR);
      BYTE* p = (BYTE*)::CoTaskMemAlloc((size_t)cbAlloc);
      if (p == NULL) return DBSTATUS_E_CANTCREATE;
      DBSTATUS st = _AtlDbConvert(pCol->wType, pSrc, cbSrc, wType, p, cbAlloc, pcb);
      if (st != DBSTATUS_S_OK && st != DBSTATUS_S_TRUNCATED) {
        ::CoTaskMemFree(p);
        p = NULL;
      }
      if (b.dwPart & DBPART_VALUE) *(BYTE**)(pData + b.obValue) = p;
      else ::CoTaskMemFree(p);
      return st;
    }
    if (b.dwPart & DBPART_VALUE)
      return _AtlDbConvert(pCol->wType, pSrc, cbSrc, wType, pData + b.obValue, b.cbMaxLen, pcb);
    // The length alone: converted where it goes nowhere.
    CTempBuffer<BYTE> buf;
    DBLENGTH cbMax = _AtlDbIsVarLength(wType) ? cbSrc * 2 + 16 : sizeof(VARIANT) + sizeof(DBTIMESTAMP);
    BYTE* p = buf.Allocate((size_t)cbMax);
    DBSTATUS st = _AtlDbConvert(pCol->wType, pSrc, cbSrc, wType, p, cbMax, pcb);
    if (wType == DBTYPE_BSTR && st == DBSTATUS_S_OK) ::SysFreeString(*(BSTR*)p);
    if (wType == DBTYPE_VARIANT && st == DBSTATUS_S_OK) ::VariantClear((VARIANT*)p);
    return st == DBSTATUS_S_TRUNCATED ? DBSTATUS_S_OK : st;
  }

public:
  // How long a column's value in a row is: text up to its zero.
  static DBLENGTH _SourceLength(ATLCOLUMNINFO* pCol, const BYTE* pSrc) {
    switch (pCol->wType) {
      case DBTYPE_STR: {
        DBLENGTH n = 0, nMax = pCol->ulColumnSize + 1;
        while (n < nMax && pSrc[n] != 0) n++;
        return n;
      }
      case DBTYPE_WSTR: {
        DBLENGTH n = 0, nMax = pCol->ulColumnSize + 1;
        const WCHAR* p = (const WCHAR*)pSrc;
        while (n < nMax && p[n] != 0) n++;
        return n * sizeof(WCHAR);
      }
      case DBTYPE_BYTES:
        return pCol->ulColumnSize;
      default:
        return _AtlDbTypeSize(pCol->wType);
    }
  }
  // A column's room in a row, in bytes.
  static DBLENGTH _ColumnBytes(ATLCOLUMNINFO* pCol) {
    switch (pCol->wType) {
      case DBTYPE_STR: return pCol->ulColumnSize + 1;
      case DBTYPE_WSTR: return (pCol->ulColumnSize + 1) * sizeof(WCHAR);
      case DBTYPE_BYTES: return pCol->ulColumnSize;
      default: return _AtlDbTypeSize(pCol->wType);
    }
  }
};

// IRowsetChange: the consumer's values into the rows. T::FlushData
// writes a change through to wherever the rows came from.
template <class T, class Storage, class BaseInterface = IRowsetChange, class RowClass = CSimpleRow,
          class MapClass = CAtlMap<typename RowClass::KeyType, RowClass*> >
class ATL_NO_VTABLE IRowsetChangeImpl : public BaseInterface {
public:
  HRESULT FlushData(HROW /*hRow*/, HACCESSOR /*hAccessor*/) { return S_OK; }
  // Overridable: a status the consumer gave (DBSTATUS_S_ISNULL,
  // DBSTATUS_S_DEFAULT) for a column of a row.
  HRESULT SetDBStatus(DBSTATUS* pdbStatus, RowClass* /*pRow*/, ATLCOLUMNINFO* /*pColInfo*/) {
    return *pdbStatus == DBSTATUS_S_DEFAULT ? S_OK : E_FAIL;
  }

  STDMETHOD(DeleteRows)(HCHAPTER /*hReserved*/, DBCOUNTITEM cRows, const HROW rghRows[],
                        DBROWSTATUS rgRowStatus[]) override {
    T* pT = static_cast<T*>(this);
    if (cRows == 0) return S_OK;
    if (rghRows == NULL) return E_INVALIDARG;
    if (!_Allowed(DBPROPVAL_UP_DELETE)) return DB_E_NOTSUPPORTED;
    DBCOUNTITEM nBad = 0;
    for (DBCOUNTITEM i = 0; i < cRows; i++) {
      RowClass* pRow = pT->_FindRow(rghRows[i]);
      DBROWSTATUS st = DBROWSTATUS_S_OK;
      if (pRow == NULL) st = DBROWSTATUS_E_INVALID;
      else if (pRow->m_status == DBPENDINGSTATUS_DELETED) st = DBROWSTATUS_E_DELETED;
      if (st == DBROWSTATUS_S_OK) {
        DBCOUNTITEM n = pRow->m_iRowset;
        pT->m_rgRowData.RemoveAt(n);
        pRow->m_status = DBPENDINGSTATUS_DELETED;
        // The rows after it move up; so does the fetch position.
        POSITION pos = pT->m_rgRowHandles.GetStartPosition();
        while (pos != NULL) {
          RowClass* p = pT->m_rgRowHandles.GetNextValue(pos);
          if (p != pRow && p->m_status != DBPENDINGSTATUS_DELETED && p->m_iRowset > n) p->m_iRowset--;
        }
        if (pT->m_iRowset > (DBROWOFFSET)n) pT->m_iRowset--;
        pT->FlushData(rghRows[i], DB_NULL_HACCESSOR);
      } else {
        nBad++;
      }
      if (rgRowStatus != NULL) rgRowStatus[i] = st;
    }
    if (nBad == 0) return S_OK;
    return nBad == cRows ? DB_E_ERRORSOCCURRED : DB_S_ERRORSOCCURRED;
  }

  STDMETHOD(SetData)(HROW hRow, HACCESSOR hAccessor, void* pSrcData) override {
    T* pT = static_cast<T*>(this);
    if (!_Allowed(DBPROPVAL_UP_CHANGE)) return DB_E_NOTSUPPORTED;
    RowClass* pRow = pT->_FindRow(hRow);
    if (pRow == NULL) return DB_E_BADROWHANDLE;
    if (pRow->m_status == DBPENDINGSTATUS_DELETED) return DB_E_DELETEDROW;
    ATLBINDINGS* pBind = pT->_FindAccessor(hAccessor);
    if (pBind == NULL) return DB_E_BADACCESSORHANDLE;
    if (!(pBind->dwAccessorFlags & DBACCESSOR_ROWDATA)) return DB_E_BADACCESSORTYPE;
    if (pBind->cBindings == 0) return S_OK;
    if (pSrcData == NULL) return E_INVALIDARG;
    // The row is changed where it is, so that what SetDBStatus stores in
    // m_rgRowData stays; it is put back when every binding failed.
    Storage saved = pT->m_rgRowData[pRow->m_iRowset];
    DBCOUNTITEM nBad = _Apply(pRow, pBind, (BYTE*)pSrcData, pT->m_rgRowData[pRow->m_iRowset]);
    if (nBad == pBind->cBindings) {
      pT->m_rgRowData[pRow->m_iRowset] = saved;
      return DB_E_ERRORSOCCURRED;
    }
    HRESULT hr = pT->FlushData(hRow, hAccessor);
    if (FAILED(hr)) return hr;
    return nBad == 0 ? S_OK : DB_S_ERRORSOCCURRED;
  }

  STDMETHOD(InsertRow)(HCHAPTER /*hReserved*/, HACCESSOR hAccessor, void* pData,
                       HROW* phRow) override {
    T* pT = static_cast<T*>(this);
    if (phRow != NULL) *phRow = DB_NULL_HROW;
    if (!_Allowed(DBPROPVAL_UP_INSERT)) return DB_E_NOTSUPPORTED;
    ATLBINDINGS* pBind = pT->_FindAccessor(hAccessor);
    if (pBind == NULL) return DB_E_BADACCESSORHANDLE;
    if (!(pBind->dwAccessorFlags & DBACCESSOR_ROWDATA)) return DB_E_BADACCESSORTYPE;
    if (pBind->cBindings > 0 && pData == NULL) return E_INVALIDARG;
    Storage row = Storage();
    DBCOUNTITEM nBad = _Apply(NULL, pBind, (BYTE*)pData, row);
    if (pBind->cBindings > 0 && nBad == pBind->cBindings) return DB_E_ERRORSOCCURRED;
    size_t nBefore = pT->_RowCount();
    pT->m_rgRowData.Add(row);
    if (pT->_RowCount() != nBefore + 1) return E_OUTOFMEMORY;
    RowClass* pRow = pT->_RowAt((DBCOUNTITEM)nBefore);
    if (pRow == NULL) return E_OUTOFMEMORY;
    pRow->AddRefRow();
    HROW h = (HROW)pRow;
    HRESULT hr = pT->FlushData(h, hAccessor);
    if (phRow != NULL)
      *phRow = h;
    else
      pT->ReleaseRows(1, &h, NULL, NULL, NULL);
    if (FAILED(hr)) return hr;
    return nBad == 0 ? S_OK : DB_S_ERRORSOCCURRED;
  }

protected:
  bool _Allowed(LONG lWhat) {
    T* pT = static_cast<T*>(this);
    if (pT->_FindProp(DBPROPSET_ROWSET, DBPROP_UPDATABILITY, false) == NULL) return true;
    return (pT->_GetLongProp(DBPROPSET_ROWSET, DBPROP_UPDATABILITY, 0) & lWhat) != 0;
  }
  // The bindings' values into a copy of a row; the number that failed.
  DBCOUNTITEM _Apply(RowClass* pRow, ATLBINDINGS* pBind, BYTE* pData, Storage& row) {
    T* pT = static_cast<T*>(this);
    DBORDINAL cCols = 0;
    ATLCOLUMNINFO* pCols = T::GetColumnInfo(pT, &cCols);
    DBCOUNTITEM nBad = 0;
    for (DBCOUNTITEM i = 0; i < pBind->cBindings; i++) {
      const DBBINDING& b = pBind->pBindings[i];
      ATLCOLUMNINFO* pCol = _AtlFindColumn(pCols, cCols, b.iOrdinal);
      DBSTATUS stIn = (b.dwPart & DBPART_STATUS) ? *(DBSTATUS*)(pData + b.obStatus) : DBSTATUS_S_OK;
      DBSTATUS st = DBSTATUS_S_OK;
      BYTE* pDst = pCol != NULL ? (BYTE*)&row + pCol->cbOffset : NULL;
      if (pCol == NULL) {
        st = DBSTATUS_E_UNAVAILABLE;
      } else if (stIn == DBSTATUS_S_IGNORE) {
      } else if (stIn == DBSTATUS_S_ISNULL || stIn == DBSTATUS_S_DEFAULT) {
        DBSTATUS stSet = stIn;
        if (FAILED(pT->SetDBStatus(&stSet, pRow, pCol)))
          st = stIn == DBSTATUS_S_ISNULL ? DBSTATUS_E_INTEGRITYVIOLATION : DBSTATUS_E_UNAVAILABLE;
        else if (stIn == DBSTATUS_S_DEFAULT)
          memset(pDst, 0, (size_t)T::_ColumnBytes(pCol));
      } else if (stIn != DBSTATUS_S_OK) {
        st = DBSTATUS_E_BADSTATUS;
      } else if (!(b.dwPart & DBPART_VALUE)) {
        st = DBSTATUS_E_UNAVAILABLE;
      } else {
        DBTYPE wType = b.wType & ~DBTYPE_BYREF;
        const BYTE* pSrc = pData + b.obValue;
        if (b.wType & DBTYPE_BYREF) pSrc = *(const BYTE* const*)pSrc;
        DBLENGTH cbSrc;
        if (b.dwPart & DBPART_LENGTH)
          cbSrc = *(DBLENGTH*)(pData + b.obLength);
        else if (wType == DBTYPE_STR)
          cbSrc = pSrc != NULL ? strlen((const char*)pSrc) : 0;
        else if (wType == DBTYPE_WSTR)
          cbSrc = pSrc != NULL ? wcslen((const WCHAR*)pSrc) * sizeof(WCHAR) : 0;
        else if (wType == DBTYPE_BYTES)
          cbSrc = b.cbMaxLen;
        else
          cbSrc = _AtlDbTypeSize(wType);
        DBLENGTH cbRoom = T::_ColumnBytes(pCol);
        if (pSrc == NULL) {
          st = DBSTATUS_E_UNAVAILABLE;
        } else if (pCol->wType == DBTYPE_BSTR) {
          // The row owns its BSTR: the new one replaces it.
          BSTR bstr = NULL;
          DBLENGTH cb = 0;
          st = _AtlDbConvert(wType, pSrc, cbSrc, DBTYPE_BSTR, &bstr, sizeof(BSTR), &cb);
          if (st == DBSTATUS_S_OK) {
            ::SysFreeString(*(BSTR*)pDst);
            *(BSTR*)pDst = bstr;
          }
        } else {
          CTempBuffer<BYTE> buf;
          BYTE* pTmp = buf.Allocate((size_t)cbRoom + sizeof(WCHAR));
          memset(pTmp, 0, (size_t)cbRoom + sizeof(WCHAR));
          DBLENGTH cb = 0;
          st = _AtlDbConvert(wType, pSrc, cbSrc, pCol->wType, pTmp, cbRoom, &cb);
          if (st == DBSTATUS_S_TRUNCATED) st = DBSTATUS_E_CANTCONVERTVALUE;  // it does not fit
          if (st == DBSTATUS_S_OK) memcpy(pDst, pTmp, (size_t)cbRoom);
        }
      }
      if (b.dwPart & DBPART_STATUS) *(DBSTATUS*)(pData + b.obStatus) = st;
      if (st != DBSTATUS_S_OK) nBad++;
    }
    return nBad;
  }
};

template <class T, class RowClass = CSimpleRow>
class ATL_NO_VTABLE IRowsetIdentityImpl : public IRowsetIdentity {
public:
  STDMETHOD(IsSameRow)(HROW hThisRow, HROW hThatRow) override {
    T* pT = static_cast<T*>(this);
    RowClass* p1 = pT->_FindRow(hThisRow);
    RowClass* p2 = pT->_FindRow(hThatRow);
    if (p1 == NULL || p2 == NULL) return DB_E_BADROWHANDLE;
    if (p1->m_status == DBPENDINGSTATUS_DELETED || p2->m_status == DBPENDINGSTATUS_DELETED)
      return DB_E_DELETEDROW;
    return p1 == p2 || p1->Compare(p2) == S_OK ? S_OK : S_FALSE;
  }
};

template <class T, class PropClass = T>
class ATL_NO_VTABLE IRowsetInfoImpl : public IRowsetInfo, public CUtlProps<PropClass> {
public:
  STDMETHOD(GetProperties)(const ULONG cPropertyIDSets, const DBPROPIDSET rgPropertyIDSets[],
                           ULONG* pcPropertySets, DBPROPSET** prgPropertySets) override {
    return this->_GetProperties(cPropertyIDSets, rgPropertyIDSets, pcPropertySets, prgPropertySets);
  }
  STDMETHOD(GetReferencedRowset)(DBORDINAL iOrdinal, REFIID /*riid*/,
                                 IUnknown** ppReferencedRowset) override {
    if (ppReferencedRowset == NULL) return E_INVALIDARG;
    *ppReferencedRowset = NULL;
    T* pT = static_cast<T*>(this);
    DBORDINAL cCols = 0;
    ATLCOLUMNINFO* pCols = T::GetColumnInfo(pT, &cCols);
    if (iOrdinal == 0 || _AtlFindColumn(pCols, cCols, iOrdinal) == NULL) return DB_E_BADORDINAL;
    return DB_E_NOTAREFERENCECOLUMN;
  }
  STDMETHOD(GetSpecification)(REFIID riid, IUnknown** ppSpecification) override {
    if (ppSpecification == NULL) return E_INVALIDARG;
    *ppSpecification = NULL;
    T* pT = static_cast<T*>(this);
    if (pT->m_spUnkSite == NULL) return S_FALSE;
    return pT->m_spUnkSite->QueryInterface(riid, (void**)ppSpecification);
  }
};

// A rowset over an array of Storage rows, which Execute fills. Its
// properties are CreatorClass's (the command's).
template <class T, class Storage, class CreatorClass, class ArrayType = CAtlArray<Storage>,
          class RowClass = CSimpleRow, class RowsetInterface = IRowsetImpl<T, IRowset, RowClass> >
class ATL_NO_VTABLE CRowsetImpl : public CComObjectRootEx<typename CreatorClass::_ThreadModel>,
                                  public IAccessorImpl<T>,
                                  public IRowsetIdentityImpl<T, RowClass>,
                                  public RowsetInterface,
                                  public IRowsetInfoImpl<T, CreatorClass>,
                                  public IColumnsInfoImpl<T>,
                                  public IConvertTypeImpl<T> {
public:
  typedef CreatorClass _RowsetCreatorClass;
  typedef ArrayType _RowsetArrayType;
  typedef Storage _RowsetStorage;
  typedef CRowsetImpl<T, Storage, CreatorClass, ArrayType, RowClass, RowsetInterface> _RowsetBaseClass;

  BEGIN_COM_MAP(CRowsetImpl)
    COM_INTERFACE_ENTRY(IAccessor)
    COM_INTERFACE_ENTRY(IRowset)
    COM_INTERFACE_ENTRY(IRowsetInfo)
    COM_INTERFACE_ENTRY(IColumnsInfo)
    COM_INTERFACE_ENTRY(IConvertType)
    COM_INTERFACE_ENTRY(IRowsetIdentity)
  END_COM_MAP()

  CComBSTR m_strCommandText;
  CComBSTR m_strIndexText;
  ArrayType m_rgRowData;
  CComPtr<IUnknown> m_spUnkSite;

  HRESULT FinalConstruct() {
    HRESULT hr = IConvertTypeImpl<T>::FinalConstruct();
    if (SUCCEEDED(hr)) hr = IAccessorImpl<T>::FinalConstruct();
    if (SUCCEEDED(hr)) hr = this->FInit();
    return hr;
  }
  void FinalRelease() {
    IAccessorImpl<T>::FinalRelease();
    _SetSite(NULL);
  }
  ~CRowsetImpl() { _SetSite(NULL); }

  static ATLCOLUMNINFO* GetColumnInfo(T* pv, DBORDINAL* pcInfo) {
    return Storage::GetColumnInfo(pv, pcInfo);
  }
  HRESULT SetSite(IUnknown* pUnkSite) {
    _SetSite(pUnkSite);
    return S_OK;
  }
  HRESULT GetSite(REFIID riid, void** ppvSite) {
    if (ppvSite == NULL) return E_POINTER;
    *ppvSite = NULL;
    if (m_spUnkSite == NULL) return E_FAIL;
    return m_spUnkSite->QueryInterface(riid, ppvSite);
  }
  // The table and index names OpenRowset gives.
  HRESULT ValidateCommandID(DBID* pTableID, DBID* pIndexID) {
    if (pTableID != NULL && (pTableID->eKind != DBKIND_NAME || pTableID->uName.pwszName == NULL))
      return DB_E_NOTABLE;
    if (pIndexID != NULL && (pIndexID->eKind != DBKIND_NAME || pIndexID->uName.pwszName == NULL))
      return DB_E_NOINDEX;
    return S_OK;
  }
  HRESULT SetCommandText(DBID* pTableID, DBID* pIndexID) {
    HRESULT hr = ValidateCommandID(pTableID, pIndexID);
    if (FAILED(hr)) return hr;
    if (pTableID != NULL) m_strCommandText = pTableID->uName.pwszName;
    if (pIndexID != NULL) m_strIndexText = pIndexID->uName.pwszName;
    return S_OK;
  }

private:
  void _SetSite(IUnknown* pUnkSite) {
    _AtlDbConnect(m_spUnkSite, false);
    m_spUnkSite = pUnkSite;
    _AtlDbConnect(pUnkSite, true);
  }
};

// ---------------------------------------------------------------------
// Schema rowsets: the session's tables, columns and types.
//
//   BEGIN_SCHEMA_MAP(CMySession)
//     SCHEMA_ENTRY(DBSCHEMA_TABLES, CMyTablesRowset)
//   END_SCHEMA_MAP()
// where CMyTablesRowset : CSchemaRowsetImpl<CMyTablesRowset, CTABLESRow,
// CMySession> has Execute(DBROWCOUNT*, ULONG cRestrictions, const VARIANT*).

typedef HRESULT (*_ATL_SCHEMA_CREATOR)(void* pSession, IUnknown* pUnkOuter, ULONG cRestrictions,
                                       const VARIANT rgRestrictions[], REFIID riid,
                                       ULONG cPropertySets, DBPROPSET rgPropertySets[],
                                       IUnknown** ppRowset);
struct _ATL_SCHEMAMAP_ENTRY {
  const GUID* pguidSchema;
  _ATL_SCHEMA_CREATOR pfnCreate;
};

template <class SessionClass, class SchemaRowsetClass>
HRESULT _AtlCreateSchemaRowset(void* pv, IUnknown* pUnkOuter, ULONG cRestrictions,
                               const VARIANT rgRestrictions[], REFIID riid, ULONG cPropertySets,
                               DBPROPSET rgPropertySets[], IUnknown** ppRowset) {
  SessionClass* pSession = (SessionClass*)pv;
  SchemaRowsetClass* pRowsetObj = NULL;
  CComPtr<IUnknown> spRowset;
  HRESULT hr = _AtlCreateRowset<SchemaRowsetClass>(pSession, pUnkOuter, riid, cPropertySets,
                                                   rgPropertySets, ppRowset, pRowsetObj, spRowset);
  if (FAILED(hr)) return hr;
  HRESULT hrProps = hr;
  DBROWCOUNT cRows = 0;
  hr = pRowsetObj->Execute(&cRows, cRestrictions, rgRestrictions);
  if (FAILED(hr)) return hr;
  if (ppRowset == NULL) return hrProps;
  hr = spRowset->QueryInterface(riid, (void**)ppRowset);
  return FAILED(hr) ? hr : hrProps;
}

#define BEGIN_SCHEMA_MAP(SchemaClass)                                   \
  typedef SchemaClass _SchemaClass;                                     \
  static const ATL::_ATL_SCHEMAMAP_ENTRY* _GetSchemaMap() {             \
    static const ATL::_ATL_SCHEMAMAP_ENTRY _map[] = {
#define SCHEMA_ENTRY(guid, rowsetClass) \
  {&guid, &ATL::_AtlCreateSchemaRowset<_SchemaClass, rowsetClass>},
#define END_SCHEMA_MAP() \
  {NULL, NULL}           \
  }                      \
  ;                      \
  return _map;           \
  }

template <class SessionClass>
class ATL_NO_VTABLE IDBSchemaRowsetImpl : public IDBSchemaRowset {
public:
  // Overridable: the restrictions each schema supports (bits, one for each
  // restriction column).
  void SetRestrictions(ULONG cRestrictions, GUID* /*rguidSchema*/, ULONG* rgRestrictions) {
    for (ULONG i = 0; i < cRestrictions; i++) rgRestrictions[i] = 0;
  }

  STDMETHOD(GetRowset)(IUnknown* pUnkOuter, REFGUID rguidSchema, ULONG cRestrictions,
                       const VARIANT rgRestrictions[], REFIID riid, ULONG cPropertySets,
                       DBPROPSET rgPropertySets[], IUnknown** ppRowset) override {
    if (ppRowset == NULL) return E_INVALIDARG;
    *ppRowset = NULL;
    if (cRestrictions > 0 && rgRestrictions == NULL) return E_INVALIDARG;
    if (cPropertySets > 0 && rgPropertySets == NULL) return E_INVALIDARG;
    if (pUnkOuter != NULL && !InlineIsEqualGUID(riid, IID_IUnknown)) return DB_E_NOAGGREGATION;
    SessionClass* pT = static_cast<SessionClass*>(this);
    for (const _ATL_SCHEMAMAP_ENTRY* p = SessionClass::_GetSchemaMap(); p->pguidSchema != NULL; p++)
      if (InlineIsEqualGUID(*p->pguidSchema, rguidSchema))
        return p->pfnCreate(pT, pUnkOuter, cRestrictions, rgRestrictions, riid, cPropertySets,
                            rgPropertySets, ppRowset);
    return E_INVALIDARG;
  }
  STDMETHOD(GetSchemas)(ULONG* pcSchemas, GUID** prgSchemas, ULONG** prgRestrictionSupport) override {
    if (pcSchemas != NULL) *pcSchemas = 0;
    if (prgSchemas != NULL) *prgSchemas = NULL;
    if (prgRestrictionSupport != NULL) *prgRestrictionSupport = NULL;
    if (pcSchemas == NULL || prgSchemas == NULL) return E_INVALIDARG;
    SessionClass* pT = static_cast<SessionClass*>(this);
    ULONG n = 0;
    const _ATL_SCHEMAMAP_ENTRY* pMap = SessionClass::_GetSchemaMap();
    while (pMap[n].pguidSchema != NULL) n++;
    if (n == 0) return S_OK;
    GUID* pGuids = (GUID*)::CoTaskMemAlloc(n * sizeof(GUID));
    ULONG* pRestr = prgRestrictionSupport != NULL ? (ULONG*)::CoTaskMemAlloc(n * sizeof(ULONG)) : NULL;
    if (pGuids == NULL || (prgRestrictionSupport != NULL && pRestr == NULL)) {
      ::CoTaskMemFree(pGuids);
      ::CoTaskMemFree(pRestr);
      return E_OUTOFMEMORY;
    }
    for (ULONG i = 0; i < n; i++) pGuids[i] = *pMap[i].pguidSchema;
    if (pRestr != NULL) pT->SetRestrictions(n, pGuids, pRestr);
    *pcSchemas = n;
    *prgSchemas = pGuids;
    if (prgRestrictionSupport != NULL) *prgRestrictionSupport = pRestr;
    return S_OK;
  }
};

template <class T, class Storage, class CreatorClass, class ArrayType = CAtlArray<Storage>,
          class RowClass = CSimpleRow, class RowsetInterface = IRowsetImpl<T, IRowset, RowClass> >
class ATL_NO_VTABLE CSchemaRowsetImpl
    : public CRowsetImpl<T, Storage, CreatorClass, ArrayType, RowClass, RowsetInterface> {
public:
  HRESULT Execute(DBROWCOUNT* pcRowsAffected, ULONG /*cRestrictions*/, const VARIANT* /*rgRestrictions*/) {
    if (pcRowsAffected != NULL) *pcRowsAffected = 0;
    return S_OK;
  }
  // Whether a restriction (VT_BSTR, or VT_EMPTY for none) lets a value by.
  static bool _Matches(ULONG cRestrictions, const VARIANT* rgRestrictions, ULONG iRestriction,
                       LPCWSTR pszValue) {
    if (iRestriction >= cRestrictions || rgRestrictions == NULL) return true;
    const VARIANT& v = rgRestrictions[iRestriction];
    if (v.vt != VT_BSTR || v.bstrVal == NULL) return true;
    return ::lstrcmpiW(v.bstrVal, pszValue != NULL ? pszValue : L"") == 0;
  }
};

// The rows of DBSCHEMA_TABLES.
class CTABLESRow {
public:
  WCHAR m_szCatalog[129];
  WCHAR m_szSchema[129];
  WCHAR m_szTable[129];
  WCHAR m_szType[129];
  GUID m_guid;
  WCHAR m_szDesc[129];
  ULONG m_ulPropID;

  CTABLESRow() {
    memset(this, 0, sizeof(*this));
    lstrcpyW(m_szType, L"TABLE");
  }

  BEGIN_PROVIDER_COLUMN_MAP(CTABLESRow)
    PROVIDER_COLUMN_ENTRY("TABLE_CATALOG", 1, m_szCatalog)
    PROVIDER_COLUMN_ENTRY("TABLE_SCHEMA", 2, m_szSchema)
    PROVIDER_COLUMN_ENTRY("TABLE_NAME", 3, m_szTable)
    PROVIDER_COLUMN_ENTRY("TABLE_TYPE", 4, m_szType)
    PROVIDER_COLUMN_ENTRY("TABLE_GUID", 5, m_guid)
    PROVIDER_COLUMN_ENTRY("DESCRIPTION", 6, m_szDesc)
    PROVIDER_COLUMN_ENTRY("TABLE_PROPID", 7, m_ulPropID)
  END_PROVIDER_COLUMN_MAP()
};

// The rows of DBSCHEMA_COLUMNS.
class CCOLUMNSRow {
public:
  WCHAR m_szTableCatalog[129];
  WCHAR m_szTableSchema[129];
  WCHAR m_szTableName[129];
  WCHAR m_szColumnName[129];
  GUID m_guidColumn;
  ULONG m_ulColumnPropID;
  ULONG m_ulOrdinalPosition;
  VARIANT_BOOL m_bColumnHasDefault;
  WCHAR m_szColumnDefault[129];
  ULONG m_ulColumnFlags;
  VARIANT_BOOL m_bIsNullable;
  USHORT m_nDataType;
  GUID m_guidType;
  ULONG m_ulCharMaxLength;
  ULONG m_ulCharOctetLength;
  USHORT m_nNumericPrecision;
  short m_nNumericScale;
  ULONG m_ulDateTimePrecision;
  WCHAR m_szCharSetCatalog[129];
  WCHAR m_szCharSetSchema[129];
  WCHAR m_szCharSetName[129];
  WCHAR m_szCollationCatalog[129];
  WCHAR m_szCollationSchema[129];
  WCHAR m_szCollationName[129];
  WCHAR m_szDomainCatalog[129];
  WCHAR m_szDomainSchema[129];
  WCHAR m_szDomainName[129];
  WCHAR m_szDescription[129];

  CCOLUMNSRow() { memset(this, 0, sizeof(*this)); }

  BEGIN_PROVIDER_COLUMN_MAP(CCOLUMNSRow)
    PROVIDER_COLUMN_ENTRY("TABLE_CATALOG", 1, m_szTableCatalog)
    PROVIDER_COLUMN_ENTRY("TABLE_SCHEMA", 2, m_szTableSchema)
    PROVIDER_COLUMN_ENTRY("TABLE_NAME", 3, m_szTableName)
    PROVIDER_COLUMN_ENTRY("COLUMN_NAME", 4, m_szColumnName)
    PROVIDER_COLUMN_ENTRY("COLUMN_GUID", 5, m_guidColumn)
    PROVIDER_COLUMN_ENTRY("COLUMN_PROPID", 6, m_ulColumnPropID)
    PROVIDER_COLUMN_ENTRY("ORDINAL_POSITION", 7, m_ulOrdinalPosition)
    PROVIDER_COLUMN_ENTRY_FIXED("COLUMN_HASDEFAULT", 8, DBTYPE_BOOL, m_bColumnHasDefault)
    PROVIDER_COLUMN_ENTRY("COLUMN_DEFAULT", 9, m_szColumnDefault)
    PROVIDER_COLUMN_ENTRY("COLUMN_FLAGS", 10, m_ulColumnFlags)
    PROVIDER_COLUMN_ENTRY_FIXED("IS_NULLABLE", 11, DBTYPE_BOOL, m_bIsNullable)
    PROVIDER_COLUMN_ENTRY("DATA_TYPE", 12, m_nDataType)
    PROVIDER_COLUMN_ENTRY("TYPE_GUID", 13, m_guidType)
    PROVIDER_COLUMN_ENTRY("CHARACTER_MAXIMUM_LENGTH", 14, m_ulCharMaxLength)
    PROVIDER_COLUMN_ENTRY("CHARACTER_OCTET_LENGTH", 15, m_ulCharOctetLength)
    PROVIDER_COLUMN_ENTRY("NUMERIC_PRECISION", 16, m_nNumericPrecision)
    PROVIDER_COLUMN_ENTRY("NUMERIC_SCALE", 17, m_nNumericScale)
    PROVIDER_COLUMN_ENTRY("DATETIME_PRECISION", 18, m_ulDateTimePrecision)
    PROVIDER_COLUMN_ENTRY("CHARACTER_SET_CATALOG", 19, m_szCharSetCatalog)
    PROVIDER_COLUMN_ENTRY("CHARACTER_SET_SCHEMA", 20, m_szCharSetSchema)
    PROVIDER_COLUMN_ENTRY("CHARACTER_SET_NAME", 21, m_szCharSetName)
    PROVIDER_COLUMN_ENTRY("COLLATION_CATALOG", 22, m_szCollationCatalog)
    PROVIDER_COLUMN_ENTRY("COLLATION_SCHEMA", 23, m_szCollationSchema)
    PROVIDER_COLUMN_ENTRY("COLLATION_NAME", 24, m_szCollationName)
    PROVIDER_COLUMN_ENTRY("DOMAIN_CATALOG", 25, m_szDomainCatalog)
    PROVIDER_COLUMN_ENTRY("DOMAIN_SCHEMA", 26, m_szDomainSchema)
    PROVIDER_COLUMN_ENTRY("DOMAIN_NAME", 27, m_szDomainName)
    PROVIDER_COLUMN_ENTRY("DESCRIPTION", 28, m_szDescription)
  END_PROVIDER_COLUMN_MAP()
};

// The rows of DBSCHEMA_PROVIDER_TYPES.
class CPROVIDER_TYPERow {
public:
  WCHAR m_szName[129];
  USHORT m_nType;
  ULONG m_ulSize;
  WCHAR m_szPrefix[129];
  WCHAR m_szSuffix[129];
  WCHAR m_szCreateParams[129];
  VARIANT_BOOL m_bIsNullable;
  VARIANT_BOOL m_bCaseSensitive;
  ULONG m_bSearchable;
  VARIANT_BOOL m_bUnsignedAttribute;
  VARIANT_BOOL m_bFixedPrecScale;
  VARIANT_BOOL m_bAutoUniqueValue;
  WCHAR m_szLocalTypeName[129];
  short m_nMinScale;
  short m_nMaxScale;
  GUID m_guidType;
  WCHAR m_szTypeLib[129];
  WCHAR m_szVersion[129];
  VARIANT_BOOL m_bIsLong;
  VARIANT_BOOL m_bBestMatch;

  CPROVIDER_TYPERow() { memset(this, 0, sizeof(*this)); }

  BEGIN_PROVIDER_COLUMN_MAP(CPROVIDER_TYPERow)
    PROVIDER_COLUMN_ENTRY("TYPE_NAME", 1, m_szName)
    PROVIDER_COLUMN_ENTRY("DATA_TYPE", 2, m_nType)
    PROVIDER_COLUMN_ENTRY("COLUMN_SIZE", 3, m_ulSize)
    PROVIDER_COLUMN_ENTRY("LITERAL_PREFIX", 4, m_szPrefix)
    PROVIDER_COLUMN_ENTRY("LITERAL_SUFFIX", 5, m_szSuffix)
    PROVIDER_COLUMN_ENTRY("CREATE_PARAMS", 6, m_szCreateParams)
    PROVIDER_COLUMN_ENTRY_FIXED("IS_NULLABLE", 7, DBTYPE_BOOL, m_bIsNullable)
    PROVIDER_COLUMN_ENTRY_FIXED("CASE_SENSITIVE", 8, DBTYPE_BOOL, m_bCaseSensitive)
    PROVIDER_COLUMN_ENTRY("SEARCHABLE", 9, m_bSearchable)
    PROVIDER_COLUMN_ENTRY_FIXED("UNSIGNED_ATTRIBUTE", 10, DBTYPE_BOOL, m_bUnsignedAttribute)
    PROVIDER_COLUMN_ENTRY_FIXED("FIXED_PREC_SCALE", 11, DBTYPE_BOOL, m_bFixedPrecScale)
    PROVIDER_COLUMN_ENTRY_FIXED("AUTO_UNIQUE_VALUE", 12, DBTYPE_BOOL, m_bAutoUniqueValue)
    PROVIDER_COLUMN_ENTRY("LOCAL_TYPE_NAME", 13, m_szLocalTypeName)
    PROVIDER_COLUMN_ENTRY("MINIMUM_SCALE", 14, m_nMinScale)
    PROVIDER_COLUMN_ENTRY("MAXIMUM_SCALE", 15, m_nMaxScale)
    PROVIDER_COLUMN_ENTRY("GUID", 16, m_guidType)
    PROVIDER_COLUMN_ENTRY("TYPELIB", 17, m_szTypeLib)
    PROVIDER_COLUMN_ENTRY("VERSION", 18, m_szVersion)
    PROVIDER_COLUMN_ENTRY_FIXED("IS_LONG", 19, DBTYPE_BOOL, m_bIsLong)
    PROVIDER_COLUMN_ENTRY_FIXED("BEST_MATCH", 20, DBTYPE_BOOL, m_bBestMatch)
  END_PROVIDER_COLUMN_MAP()
};

}  // namespace ATL

#endif  // __ATLDB_H__
