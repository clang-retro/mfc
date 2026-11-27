// atldbcli.h - the OLE DB consumer templates: CDataSource and CSession
// open a provider; CTable and CCommand open rowsets through an accessor
// (CAccessor over a class with a column map, CDynamicAccessor and
// CDynamicStringAccessor that bind whatever columns there are,
// CManualAccessor, CNoAccessor) and move through them with CRowset.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLDBCLI_H__
#define __ATLDBCLI_H__

#include <atlbase.h>
#ifndef __AFX_H__
#include <atlstr.h>
#endif
#ifndef __oledb_h__
#ifndef DBINITCONSTANTS
#define DBINITCONSTANTS  // the DBPROPSET_ and DBGUID_ values, as selectany
#define _ATLDBCLI_UNDEF_DBINITCONSTANTS
#endif
#include <oledb.h>
#ifdef _ATLDBCLI_UNDEF_DBINITCONSTANTS
#undef DBINITCONSTANTS
#undef _ATLDBCLI_UNDEF_DBINITCONSTANTS
#endif
#endif
#include <oledberr.h>
#include <msdasc.h>
#include <transact.h>
#include <type_traits>
#include <vector>

#if !defined(_ATL_NO_DEFAULT_LIBS) && defined(_WIN32)
__asm__(".section .drectve,\"yn\"\n"
        ".ascii \" -defaultlib:ole32 -defaultlib:oleaut32 -defaultlib:uuid\"\n"
        ".text");
#endif


// The OLE DB headers give the interfaces' IIDs but not __uuidof, which
// CComQIPtr<IRowset> and the like need.
#ifndef _ATL_OLEDB_UUIDOF_DECLARED
#define _ATL_OLEDB_UUIDOF_DECLARED
__CRT_UUID_DECL(IDBInitialize, 0x0c733a8b, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(IAccessor, 0x0c733a8c, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(IRowset, 0x0c733a7c, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(IRowsetInfo, 0x0c733a55, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(IRowsetLocate, 0x0c733a7d, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(IRowsetScroll, 0x0c733a7e, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(IRowsetChange, 0x0c733a05, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(IRowsetUpdate, 0x0c733a6d, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(IRowsetIdentity, 0x0c733a09, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(ICommand, 0x0c733a63, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(IMultipleResults, 0x0c733a90, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(IConvertType, 0x0c733a88, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(ICommandPrepare, 0x0c733a26, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(ICommandProperties, 0x0c733a79, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(ICommandText, 0x0c733a27, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(ICommandWithParameters, 0x0c733a64, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(IColumnsInfo, 0x0c733a11, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(IDBCreateCommand, 0x0c733a1d, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(IDBCreateSession, 0x0c733a5d, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(IDBProperties, 0x0c733a8a, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(IDBInfo, 0x0c733a89, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(ISessionProperties, 0x0c733a85, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(IOpenRowset, 0x0c733a69, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(IDBSchemaRowset, 0x0c733a7b, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(IErrorRecords, 0x0c733a67, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(IGetDataSource, 0x0c733a75, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
__CRT_UUID_DECL(IRowsetFind, 0x0c733a9d, 0x2a1c, 0x11ce, 0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d)
#endif

namespace ATL {

#ifdef __AFX_H__
typedef ::CString _AtlDbString;
#else
typedef ::ATL::CString _AtlDbString;
#endif

// The GUIDs this header uses, whether or not oledb.h defined them.
__declspec(selectany) extern const GUID _ATLDB_DBGUID_DEFAULT = {
    0xc8b521fb, 0x5cf3, 0x11ce, {0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d}};
__declspec(selectany) extern const GUID _ATLDB_DBPROPSET_DBINIT = {
    0xc8b522bc, 0x5cf3, 0x11ce, {0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d}};

// How CDynamicAccessor binds BLOB columns.
enum DBBLOBHANDLINGENUM {
  DBBLOBHANDLING_DEFAULT,
  DBBLOBHANDLING_NOSTREAMS,
  DBBLOBHANDLING_SKIP
};

// A wide copy of a TCHAR string, for the provider.
class _AtlDbWide {
public:
  explicit _AtlDbWide(LPCTSTR psz) : m_p(NULL) {
    if (psz == NULL) return;
#ifdef _UNICODE
    m_v.assign(psz, psz + wcslen(psz) + 1);
#else
    int n = ::MultiByteToWideChar(CP_ACP, 0, psz, -1, NULL, 0);
    m_v.resize(n > 0 ? n : 1);
    ::MultiByteToWideChar(CP_ACP, 0, psz, -1, &m_v[0], n);
#endif
    m_p = &m_v[0];
  }
  explicit _AtlDbWide(LPCWSTR psz, int) : m_p(NULL) {
    if (psz != NULL) {
      m_v.assign(psz, psz + wcslen(psz) + 1);
      m_p = &m_v[0];
    }
  }
  operator LPWSTR() { return m_p; }

private:
  std::vector<WCHAR> m_v;
  LPWSTR m_p;
};

// ---------------------------------------------------------------------
// The OLE DB type of a C++ member, for the column and parameter maps.

inline DBTYPE _GetOleDBType(char&) { return DBTYPE_I1; }
inline DBTYPE _GetOleDBType(signed char&) { return DBTYPE_I1; }
inline DBTYPE _GetOleDBType(unsigned char&) { return DBTYPE_UI1; }
inline DBTYPE _GetOleDBType(short&) { return DBTYPE_I2; }
inline DBTYPE _GetOleDBType(unsigned short&) { return DBTYPE_UI2; }
inline DBTYPE _GetOleDBType(int&) { return DBTYPE_I4; }
inline DBTYPE _GetOleDBType(unsigned int&) { return DBTYPE_UI4; }
inline DBTYPE _GetOleDBType(long&) { return DBTYPE_I4; }
inline DBTYPE _GetOleDBType(unsigned long&) { return DBTYPE_UI4; }
inline DBTYPE _GetOleDBType(LONGLONG&) { return DBTYPE_I8; }
inline DBTYPE _GetOleDBType(ULONGLONG&) { return DBTYPE_UI8; }
inline DBTYPE _GetOleDBType(float&) { return DBTYPE_R4; }
inline DBTYPE _GetOleDBType(double&) { return DBTYPE_R8; }
inline DBTYPE _GetOleDBType(bool&) { return DBTYPE_BOOL; }
inline DBTYPE _GetOleDBType(CY&) { return DBTYPE_CY; }
inline DBTYPE _GetOleDBType(DECIMAL&) { return DBTYPE_DECIMAL; }
inline DBTYPE _GetOleDBType(GUID&) { return DBTYPE_GUID; }
inline DBTYPE _GetOleDBType(VARIANT&) { return DBTYPE_VARIANT; }
inline DBTYPE _GetOleDBType(BSTR&) { return DBTYPE_BSTR; }
inline DBTYPE _GetOleDBType(DBDATE&) { return DBTYPE_DBDATE; }
inline DBTYPE _GetOleDBType(DBTIME&) { return DBTYPE_DBTIME; }
inline DBTYPE _GetOleDBType(DBTIMESTAMP&) { return DBTYPE_DBTIMESTAMP; }
inline DBTYPE _GetOleDBType(FILETIME&) { return DBTYPE_FILETIME; }
inline DBTYPE _GetOleDBType(IUnknown*&) { return DBTYPE_IUNKNOWN; }
template <size_t N>
inline DBTYPE _GetOleDBType(char (&)[N]) { return DBTYPE_STR; }
template <size_t N>
inline DBTYPE _GetOleDBType(wchar_t (&)[N]) { return DBTYPE_WSTR; }
template <size_t N>
inline DBTYPE _GetOleDBType(BYTE (&)[N]) { return DBTYPE_BYTES; }

// ---------------------------------------------------------------------
// The maps. A column map lists the members bound to the rowset's
// columns, an accessor map splits them among accessors, and a parameter
// map lists the members bound to a command's '?' parameters.

#define BEGIN_COLUMN_MAP(x)                                                     \
  typedef x _ColumnClass;                                                       \
  static ULONG _GetNumAccessors() { return 1; }                                 \
  template <class _F>                                                           \
  static void _VisitColumns(_ColumnClass* pThis, ULONG nAccessor, _F& f) {      \
    (void)pThis;                                                                \
    if (nAccessor == 0) {                                                       \
      f.Auto(true);
#define END_COLUMN_MAP() \
  }                      \
  }

#define BEGIN_ACCESSOR_MAP(x, num)                                         \
  typedef x _ColumnClass;                                                  \
  static ULONG _GetNumAccessors() { return num; }                          \
  template <class _F>                                                      \
  static void _VisitColumns(_ColumnClass* pThis, ULONG nAccessor, _F& f) { \
    (void)pThis;
#define BEGIN_ACCESSOR(num, bAuto) \
  if (nAccessor == (ULONG)(num)) { \
    f.Auto(bAuto);
#define END_ACCESSOR() }
#define END_ACCESSOR_MAP() }

#define BEGIN_PARAM_MAP(x)                              \
  template <class _F>                                   \
  static void _VisitParams(x* pThis, _F& f) {           \
    (void)pThis;                                        \
    f.ParamIO(DBPARAMIO_INPUT);
#define SET_PARAM_TYPE(type) f.ParamIO(type);
#define END_PARAM_MAP() }

#define _ATL_COLUMN(nOrdinal, wType, nLength, nPrecision, nScale, data, pLength, pStatus) \
  f.Column((DBORDINAL)(nOrdinal), (BYTE*)pThis, (BYTE*)&pThis->data, (DBLENGTH)(nLength),  \
           (DBTYPE)(wType), (BYTE)(nPrecision), (BYTE)(nScale), (BYTE*)(pLength),          \
           (BYTE*)(pStatus));

#define COLUMN_ENTRY(nOrdinal, data) \
  _ATL_COLUMN(nOrdinal, _GetOleDBType(pThis->data), sizeof(pThis->data), 0, 0, data, NULL, NULL)
#define COLUMN_ENTRY_TYPE(nOrdinal, wType, data) \
  _ATL_COLUMN(nOrdinal, wType, sizeof(pThis->data), 0, 0, data, NULL, NULL)
#define COLUMN_ENTRY_TYPE_SIZE(nOrdinal, wType, nLength, data) \
  _ATL_COLUMN(nOrdinal, wType, nLength, 0, 0, data, NULL, NULL)
#define COLUMN_ENTRY_LENGTH(nOrdinal, data, length)                                        \
  _ATL_COLUMN(nOrdinal, _GetOleDBType(pThis->data), sizeof(pThis->data), 0, 0, data, \
              &pThis->length, NULL)
#define COLUMN_ENTRY_STATUS(nOrdinal, data, status)                                        \
  _ATL_COLUMN(nOrdinal, _GetOleDBType(pThis->data), sizeof(pThis->data), 0, 0, data, NULL, \
              &pThis->status)
#define COLUMN_ENTRY_LENGTH_STATUS(nOrdinal, data, length, status)                         \
  _ATL_COLUMN(nOrdinal, _GetOleDBType(pThis->data), sizeof(pThis->data), 0, 0, data, \
              &pThis->length, &pThis->status)
#define COLUMN_ENTRY_PS(nOrdinal, nPrecision, nScale, data) \
  _ATL_COLUMN(nOrdinal, _GetOleDBType(pThis->data), sizeof(pThis->data), nPrecision, nScale, data, NULL, NULL)
#define COLUMN_ENTRY_PS_LENGTH_STATUS(nOrdinal, nPrecision, nScale, data, length, status)      \
  _ATL_COLUMN(nOrdinal, _GetOleDBType(pThis->data), sizeof(pThis->data), nPrecision, nScale, \
              data, &pThis->length, &pThis->status)

// The default command a class with a column map opens.
#define DEFINE_COMMAND(x, szCommand)                     \
  typedef x _CommandClass;                               \
  static HRESULT GetDefaultCommand(LPCTSTR* ppszCommand) { \
    *ppszCommand = szCommand;                            \
    return S_OK;                                         \
  }
#define DEFINE_COMMAND_EX(x, wszCommand)                  \
  typedef x _CommandClass;                                \
  static HRESULT GetDefaultCommand(LPCWSTR* ppwszCommand) { \
    *ppwszCommand = wszCommand;                           \
    return S_OK;                                          \
  }

// Collects the bindings of one accessor (or the parameters).
class _CAtlBindingCollector {
public:
  std::vector<DBBINDING> m_bindings;
  bool m_bAuto;
  DBPARAMIO m_eParamIO;
  _CAtlBindingCollector() : m_bAuto(true), m_eParamIO(DBPARAMIO_NOTPARAM) {}
  void Auto(bool b) { m_bAuto = b; }
  void ParamIO(DBPARAMIO e) { m_eParamIO = e; }
  void Column(DBORDINAL nOrdinal, BYTE* pBase, BYTE* pData, DBLENGTH nLength, DBTYPE wType,
              BYTE bPrecision, BYTE bScale, BYTE* pLength, BYTE* pStatus) {
    DBBINDING b;
    memset(&b, 0, sizeof(b));
    b.iOrdinal = nOrdinal;
    b.obValue = (DBBYTEOFFSET)(pData - pBase);
    b.dwPart = DBPART_VALUE;
    if (pLength != NULL) {
      b.obLength = (DBBYTEOFFSET)(pLength - pBase);
      b.dwPart |= DBPART_LENGTH;
    }
    if (pStatus != NULL) {
      b.obStatus = (DBBYTEOFFSET)(pStatus - pBase);
      b.dwPart |= DBPART_STATUS;
    }
    b.dwMemOwner = DBMEMOWNER_CLIENTOWNED;
    b.eParamIO = m_eParamIO;
    b.cbMaxLen = nLength;
    b.wType = wType;
    b.bPrecision = bPrecision;
    b.bScale = bScale;
    m_bindings.push_back(b);
  }
};

// ---------------------------------------------------------------------
// Property sets

class CDBPropSet : public tagDBPROPSET {
public:
  CDBPropSet() { Init(GUID_NULL); }
  explicit CDBPropSet(const GUID& guid) { Init(guid); }
  CDBPropSet(const CDBPropSet& propset) {
    Init(propset.guidPropertySet);
    Copy(propset);
  }
  ~CDBPropSet() { Clear(); }

  CDBPropSet& operator=(const CDBPropSet& propset) {
    if (this != &propset) {
      Clear();
      guidPropertySet = propset.guidPropertySet;
      Copy(propset);
    }
    return *this;
  }
  void SetGUID(const GUID& guid) { guidPropertySet = guid; }

  bool AddProperty(DWORD dwPropertyID, const VARIANT& var,
                   DWORD dwOptions = DBPROPOPTIONS_REQUIRED) {
    DBPROP* p = Add(dwPropertyID, dwOptions);
    return p != NULL && SUCCEEDED(::VariantCopy(&p->vValue, const_cast<VARIANT*>(&var)));
  }
  bool AddProperty(DWORD dwPropertyID, LPCSTR szValue, DWORD dwOptions = DBPROPOPTIONS_REQUIRED) {
    DBPROP* p = Add(dwPropertyID, dwOptions);
    if (p == NULL) return false;
    int n = ::MultiByteToWideChar(CP_ACP, 0, szValue, -1, NULL, 0);
    std::vector<WCHAR> w(n > 0 ? n : 1);
    ::MultiByteToWideChar(CP_ACP, 0, szValue, -1, &w[0], n);
    p->vValue.vt = VT_BSTR;
    p->vValue.bstrVal = ::SysAllocString(&w[0]);
    return p->vValue.bstrVal != NULL;
  }
  bool AddProperty(DWORD dwPropertyID, LPCWSTR szValue, DWORD dwOptions = DBPROPOPTIONS_REQUIRED) {
    DBPROP* p = Add(dwPropertyID, dwOptions);
    if (p == NULL) return false;
    p->vValue.vt = VT_BSTR;
    p->vValue.bstrVal = ::SysAllocString(szValue);
    return p->vValue.bstrVal != NULL;
  }
  bool AddProperty(DWORD dwPropertyID, bool bValue, DWORD dwOptions = DBPROPOPTIONS_REQUIRED) {
    DBPROP* p = Add(dwPropertyID, dwOptions);
    if (p == NULL) return false;
    p->vValue.vt = VT_BOOL;
    p->vValue.boolVal = bValue ? VARIANT_TRUE : VARIANT_FALSE;
    return true;
  }
#define _ATLDB_ADDPROP(type, vtype, member)                                              \
  bool AddProperty(DWORD dwPropertyID, type value, DWORD dwOptions = DBPROPOPTIONS_REQUIRED) { \
    DBPROP* p = Add(dwPropertyID, dwOptions);                                            \
    if (p == NULL) return false;                                                         \
    p->vValue.vt = vtype;                                                                \
    p->vValue.member = value;                                                            \
    return true;                                                                         \
  }
  _ATLDB_ADDPROP(BYTE, VT_UI1, bVal)
  _ATLDB_ADDPROP(short, VT_I2, iVal)
  _ATLDB_ADDPROP(long, VT_I4, lVal)
  _ATLDB_ADDPROP(int, VT_I4, lVal)
  _ATLDB_ADDPROP(float, VT_R4, fltVal)
  _ATLDB_ADDPROP(double, VT_R8, dblVal)
  _ATLDB_ADDPROP(CY, VT_CY, cyVal)
#undef _ATLDB_ADDPROP

private:
  void Init(const GUID& guid) {
    rgProperties = NULL;
    cProperties = 0;
    guidPropertySet = guid;
  }
  void Clear() {
    for (ULONG i = 0; i < cProperties; i++) ::VariantClear(&rgProperties[i].vValue);
    ::CoTaskMemFree(rgProperties);
    rgProperties = NULL;
    cProperties = 0;
  }
  void Copy(const CDBPropSet& propset) {
    for (ULONG i = 0; i < propset.cProperties; i++) {
      const DBPROP& src = propset.rgProperties[i];
      DBPROP* p = Add(src.dwPropertyID, src.dwOptions);
      if (p != NULL) ::VariantCopy(&p->vValue, const_cast<VARIANT*>(&src.vValue));
    }
  }
  DBPROP* Add(DWORD dwPropertyID, DWORD dwOptions) {
    DBPROP* p = (DBPROP*)::CoTaskMemRealloc(rgProperties, (cProperties + 1) * sizeof(DBPROP));
    if (p == NULL) return NULL;
    rgProperties = p;
    DBPROP* pNew = &rgProperties[cProperties++];
    memset(pNew, 0, sizeof(*pNew));
    pNew->dwPropertyID = dwPropertyID;
    pNew->dwOptions = dwOptions;
    pNew->colid = DB_NULLID;
    ::VariantInit(&pNew->vValue);
    return pNew;
  }
};

class CDBPropIDSet : public tagDBPROPIDSET {
public:
  CDBPropIDSet() {
    rgPropertyIDs = NULL;
    cPropertyIDs = 0;
    guidPropertySet = GUID_NULL;
  }
  explicit CDBPropIDSet(const GUID& guid) {
    rgPropertyIDs = NULL;
    cPropertyIDs = 0;
    guidPropertySet = guid;
  }
  CDBPropIDSet(const CDBPropIDSet& set) {
    rgPropertyIDs = NULL;
    cPropertyIDs = 0;
    guidPropertySet = set.guidPropertySet;
    for (ULONG i = 0; i < set.cPropertyIDs; i++) AddPropertyID(set.rgPropertyIDs[i]);
  }
  ~CDBPropIDSet() { ::CoTaskMemFree(rgPropertyIDs); }
  CDBPropIDSet& operator=(const CDBPropIDSet& set) {
    if (this != &set) {
      ::CoTaskMemFree(rgPropertyIDs);
      rgPropertyIDs = NULL;
      cPropertyIDs = 0;
      guidPropertySet = set.guidPropertySet;
      for (ULONG i = 0; i < set.cPropertyIDs; i++) AddPropertyID(set.rgPropertyIDs[i]);
    }
    return *this;
  }
  void SetGUID(const GUID& guid) { guidPropertySet = guid; }
  bool AddPropertyID(DBPROPID propid) {
    DBPROPID* p = (DBPROPID*)::CoTaskMemRealloc(rgPropertyIDs, (cPropertyIDs + 1) * sizeof(DBPROPID));
    if (p == NULL) return false;
    rgPropertyIDs = p;
    rgPropertyIDs[cPropertyIDs++] = propid;
    return true;
  }
};

// Frees what IDBProperties::GetProperties gave back.
inline void _AtlFreePropSets(ULONG cSets, DBPROPSET* pSets) {
  for (ULONG i = 0; i < cSets; i++) {
    for (ULONG j = 0; j < pSets[i].cProperties; j++) ::VariantClear(&pSets[i].rgProperties[j].vValue);
    ::CoTaskMemFree(pSets[i].rgProperties);
  }
  ::CoTaskMemFree(pSets);
}

// One property from a GetProperties-like call.
template <class I>
inline HRESULT _AtlGetOneProperty(I* p, const GUID& guid, DBPROPID propid, VARIANT* pVariant) {
  if (p == NULL || pVariant == NULL) return E_POINTER;
  CDBPropIDSet set(guid);
  set.AddPropertyID(propid);
  ULONG cSets = 0;
  DBPROPSET* pSets = NULL;
  HRESULT hr = p->GetProperties(1, &set, &cSets, &pSets);
  if (SUCCEEDED(hr) && cSets > 0 && pSets[0].cProperties > 0)
    hr = ::VariantCopy(pVariant, &pSets[0].rgProperties[0].vValue);
  if (pSets != NULL) _AtlFreePropSets(cSets, pSets);
  return hr;
}

// ---------------------------------------------------------------------
// CDataSource and CSession

class CDataSource {
public:
  CComPtr<IDBInitialize> m_spInit;

  HRESULT Open(const CLSID& clsid, DBPROPSET* pPropSet = NULL, ULONG nPropertySets = 1) {
    Close();
    HRESULT hr = ::CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, IID_IDBInitialize,
                                    (void**)&m_spInit);
    if (FAILED(hr)) return hr;
    return InitWith(pPropSet, nPropertySets);
  }
  HRESULT Open(LPCTSTR szProgID, DBPROPSET* pPropSet = NULL, ULONG nPropertySets = 1) {
    CLSID clsid;
    HRESULT hr = ::CLSIDFromProgID(_AtlDbWide(szProgID), &clsid);
    if (FAILED(hr)) return hr;
    return Open(clsid, pPropSet, nPropertySets);
  }
  HRESULT Open(const CLSID& clsid, LPCTSTR pName, LPCTSTR pUserName = NULL,
               LPCTSTR pPassword = NULL, long nInitMode = 0) {
    CDBPropSet set(_ATLDB_DBPROPSET_DBINIT);
    if (pName != NULL) set.AddProperty(DBPROP_INIT_DATASOURCE, (LPCWSTR)_AtlDbWide(pName));
    if (pUserName != NULL) set.AddProperty(DBPROP_AUTH_USERID, (LPCWSTR)_AtlDbWide(pUserName));
    if (pPassword != NULL) set.AddProperty(DBPROP_AUTH_PASSWORD, (LPCWSTR)_AtlDbWide(pPassword));
    if (nInitMode != 0) set.AddProperty(DBPROP_INIT_MODE, nInitMode);
    return Open(clsid, &set, 1);
  }
  HRESULT Open(LPCTSTR szProgID, LPCTSTR pName, LPCTSTR pUserName = NULL,
               LPCTSTR pPassword = NULL, long nInitMode = 0) {
    CLSID clsid;
    HRESULT hr = ::CLSIDFromProgID(_AtlDbWide(szProgID), &clsid);
    if (FAILED(hr)) return hr;
    return Open(clsid, pName, pUserName, pPassword, nInitMode);
  }

  // Through the OLE DB service components (oledb32): pooling, and the
  // initialization strings below.
  HRESULT OpenWithServiceComponents(const CLSID& clsid, DBPROPSET* pPropSet = NULL,
                                    ULONG nPropertySets = 1) {
    Close();
    CComPtr<IDataInitialize> spData;
    HRESULT hr = ::CoCreateInstance(CLSID_MSDAINITIALIZE, NULL, CLSCTX_INPROC_SERVER,
                                    IID_IDataInitialize, (void**)&spData);
    if (FAILED(hr)) return hr;
    hr = spData->CreateDBInstance(clsid, NULL, CLSCTX_INPROC_SERVER, NULL, IID_IDBInitialize,
                                  (IUnknown**)&m_spInit);
    if (FAILED(hr)) return hr;
    return InitWith(pPropSet, nPropertySets);
  }
  HRESULT OpenFromInitializationString(LPCOLESTR szInitializationString,
                                       bool fPromptForInfo = false) {
    Close();
    CComPtr<IDataInitialize> spData;
    HRESULT hr = ::CoCreateInstance(CLSID_MSDAINITIALIZE, NULL, CLSCTX_INPROC_SERVER,
                                    IID_IDataInitialize, (void**)&spData);
    if (FAILED(hr)) return hr;
    hr = spData->GetDataSource(NULL, CLSCTX_INPROC_SERVER, szInitializationString,
                               IID_IDBInitialize, (IUnknown**)&m_spInit);
    if (FAILED(hr)) return hr;
    if (fPromptForInfo) {
      CDBPropSet set(_ATLDB_DBPROPSET_DBINIT);
      set.AddProperty(DBPROP_INIT_PROMPT, (short)DBPROMPT_COMPLETEREQUIRED);
      InitWith(&set, 1);
      return S_OK;
    }
    return m_spInit->Initialize();
  }
  HRESULT GetInitializationString(BSTR* pInitializationString, bool bIncludePassword = false) {
    if (pInitializationString == NULL) return E_POINTER;
    CComPtr<IDataInitialize> spData;
    HRESULT hr = ::CoCreateInstance(CLSID_MSDAINITIALIZE, NULL, CLSCTX_INPROC_SERVER,
                                    IID_IDataInitialize, (void**)&spData);
    if (FAILED(hr)) return hr;
    LPOLESTR psz = NULL;
    hr = spData->GetInitializationString(m_spInit, bIncludePassword, &psz);
    if (SUCCEEDED(hr)) {
      *pInitializationString = ::SysAllocString(psz);
      ::CoTaskMemFree(psz);
    }
    return hr;
  }

  HRESULT GetProperties(ULONG ulPropIDSets, const DBPROPIDSET* pPropIDSet,
                        ULONG* pulPropertySets, DBPROPSET** ppPropsets) const {
    CComPtr<IDBProperties> sp;
    HRESULT hr = QI(IID_IDBProperties, (void**)&sp);
    if (FAILED(hr)) return hr;
    return sp->GetProperties(ulPropIDSets, pPropIDSet, pulPropertySets, ppPropsets);
  }
  HRESULT GetProperty(const GUID& guid, DBPROPID propid, VARIANT* pVariant) const {
    CComPtr<IDBProperties> sp;
    HRESULT hr = QI(IID_IDBProperties, (void**)&sp);
    if (FAILED(hr)) return hr;
    return _AtlGetOneProperty((IDBProperties*)sp, guid, propid, pVariant);
  }

  void Close() { m_spInit.Release(); }

private:
  HRESULT QI(REFIID riid, void** ppv) const {
    if (m_spInit == NULL) return E_UNEXPECTED;
    return m_spInit->QueryInterface(riid, ppv);
  }
  HRESULT InitWith(DBPROPSET* pPropSet, ULONG nPropertySets) {
    if (pPropSet != NULL && nPropertySets > 0) {
      CComPtr<IDBProperties> sp;
      HRESULT hr = QI(IID_IDBProperties, (void**)&sp);
      if (FAILED(hr)) return hr;
      hr = sp->SetProperties(nPropertySets, pPropSet);
      if (FAILED(hr)) return hr;
    }
    HRESULT hr = m_spInit->Initialize();
    if (FAILED(hr)) Close();
    return hr;
  }
};

class CSession {
public:
  CComPtr<IOpenRowset> m_spOpenRowset;

  HRESULT Open(const CDataSource& ds, DBPROPSET* pPropSet = NULL, ULONG ulPropSets = 0) {
    Close();
    if (ds.m_spInit == NULL) return E_UNEXPECTED;
    CComPtr<IDBCreateSession> spCreate;
    HRESULT hr = ds.m_spInit->QueryInterface(IID_IDBCreateSession, (void**)&spCreate);
    if (FAILED(hr)) return hr;
    hr = spCreate->CreateSession(NULL, IID_IOpenRowset, (IUnknown**)&m_spOpenRowset);
    if (FAILED(hr)) return hr;
    if (pPropSet != NULL && ulPropSets > 0) {
      CComPtr<ISessionProperties> spProps;
      hr = m_spOpenRowset->QueryInterface(IID_ISessionProperties, (void**)&spProps);
      if (SUCCEEDED(hr)) hr = spProps->SetProperties(ulPropSets, pPropSet);
      if (FAILED(hr)) Close();
    }
    return hr;
  }
  void Close() { m_spOpenRowset.Release(); }

  HRESULT StartTransaction(ISOLEVEL isoLevel = ISOLATIONLEVEL_READCOMMITTED, ULONG isoFlags = 0,
                           ITransactionOptions* pOtherOptions = NULL,
                           ULONG* pulTransactionLevel = NULL) const {
    CComPtr<ITransactionLocal> sp;
    HRESULT hr = QI(IID_ITransactionLocal, (void**)&sp);
    if (FAILED(hr)) return hr;
    return sp->StartTransaction(isoLevel, isoFlags, pOtherOptions, pulTransactionLevel);
  }
  HRESULT Abort(BOID* pboidReason = NULL, BOOL bRetaining = FALSE, BOOL bAsync = FALSE) const {
    CComPtr<ITransaction> sp;
    HRESULT hr = QI(IID_ITransaction, (void**)&sp);
    if (FAILED(hr)) return hr;
    return sp->Abort(pboidReason, bRetaining, bAsync);
  }
  HRESULT Commit(BOOL bRetaining = FALSE, DWORD grfTC = XACTTC_SYNC, DWORD grfRM = 0) const {
    CComPtr<ITransaction> sp;
    HRESULT hr = QI(IID_ITransaction, (void**)&sp);
    if (FAILED(hr)) return hr;
    return sp->Commit(bRetaining, grfTC, grfRM);
  }
  HRESULT GetTransactionInfo(XACTTRANSINFO* pInfo) const {
    CComPtr<ITransaction> sp;
    HRESULT hr = QI(IID_ITransaction, (void**)&sp);
    if (FAILED(hr)) return hr;
    return sp->GetTransactionInfo(pInfo);
  }

private:
  HRESULT QI(REFIID riid, void** ppv) const {
    if (m_spOpenRowset == NULL) return E_UNEXPECTED;
    return m_spOpenRowset->QueryInterface(riid, ppv);
  }
};

// ---------------------------------------------------------------------
// Accessors

class CAccessorBase {
public:
  struct _AccessorInfo {
    HACCESSOR hAccessor;
    bool bAutoAccessor;
  };

  CAccessorBase() : m_pBuffer(NULL) {}
  virtual ~CAccessorBase() {}

  HACCESSOR GetHAccessor(ULONG nAccessor) const {
    return nAccessor < m_accessors.size() ? m_accessors[nAccessor].hAccessor : NULL;
  }
  ULONG GetNumAccessors() const { return (ULONG)m_accessors.size(); }
  bool IsAutoAccessor(ULONG nAccessor) const {
    return nAccessor < m_accessors.size() && m_accessors[nAccessor].bAutoAccessor;
  }
  BYTE* GetBuffer() const { return m_pBuffer; }

  void ReleaseAccessors(IUnknown* pUnk) {
    if (pUnk != NULL) {
      CComPtr<IAccessor> spAccessor;
      if (SUCCEEDED(pUnk->QueryInterface(IID_IAccessor, (void**)&spAccessor))) {
        for (size_t i = 0; i < m_accessors.size(); i++)
          if (m_accessors[i].hAccessor != NULL)
            spAccessor->ReleaseAccessor(m_accessors[i].hAccessor, NULL);
      }
    }
    m_accessors.clear();
  }

  // Overridden by the accessors that bind anything.
  virtual HRESULT BindColumns(IUnknown*) { return S_OK; }
  virtual bool HasOutputColumns() const { return false; }
  virtual bool HasParameters() const { return false; }
  virtual HRESULT BindParameters(HACCESSOR*, ICommand*, void** ppParameterBuffer) {
    if (ppParameterBuffer != NULL) *ppParameterBuffer = NULL;
    return S_OK;
  }
  virtual void FreeRecordMemory(IRowset*) {}

protected:
  BYTE* m_pBuffer;
  std::vector<_AccessorInfo> m_accessors;

  static HRESULT CreateOne(IAccessor* pAccessor, DBACCESSORFLAGS dwFlags,
                           std::vector<DBBINDING>& bindings, DBLENGTH cbRowSize,
                           HACCESSOR* phAccessor) {
    std::vector<DBBINDSTATUS> status(bindings.size() + 1);
    return pAccessor->CreateAccessor(dwFlags, (DBCOUNTITEM)bindings.size(),
                                     bindings.empty() ? NULL : &bindings[0], cbRowSize,
                                     phAccessor, &status[0]);
  }
};

class CNoAccessor : public CAccessorBase {};

// Whether a column-map class also has a parameter map.
template <class T, class = void>
struct _AtlHasParamMap : std::false_type {};
template <class T>
struct _AtlHasParamMap<T, decltype((void)&T::template _VisitParams<_CAtlBindingCollector>)>
    : std::true_type {};

template <class T, class = void>
struct _AtlHasDefaultCommand : std::false_type {};
template <class T>
struct _AtlHasDefaultCommand<T, decltype((void)&T::GetDefaultCommand)> : std::true_type {};

// The columns of a class T with a column map, read into T's members.
template <class T>
class CAccessor : public T, public CAccessorBase {
public:
  CAccessor() { m_pBuffer = (BYTE*)static_cast<T*>(this); }

  virtual HRESULT BindColumns(IUnknown* pUnk) {
    m_pBuffer = (BYTE*)static_cast<T*>(this);
    CComPtr<IAccessor> spAccessor;
    HRESULT hr = pUnk->QueryInterface(IID_IAccessor, (void**)&spAccessor);
    if (FAILED(hr)) return hr;
    ULONG nAccessors = T::_GetNumAccessors();
    m_accessors.assign(nAccessors, _AccessorInfo());
    for (ULONG i = 0; i < nAccessors; i++) {
      _CAtlBindingCollector c;
      T::_VisitColumns(static_cast<T*>(this), i, c);
      m_accessors[i].bAutoAccessor = c.m_bAuto;
      m_accessors[i].hAccessor = NULL;
      hr = CreateOne(spAccessor, DBACCESSOR_ROWDATA, c.m_bindings, 0, &m_accessors[i].hAccessor);
      if (FAILED(hr)) return hr;
    }
    return S_OK;
  }
  virtual bool HasOutputColumns() const { return true; }
  virtual bool HasParameters() const { return _AtlHasParamMap<T>::value; }
  virtual HRESULT BindParameters(HACCESSOR* pHAccessor, ICommand* pCommand,
                                 void** ppParameterBuffer) {
    return BindParams<T>(pHAccessor, pCommand, ppParameterBuffer, _AtlHasParamMap<T>());
  }

private:
  template <class U>
  HRESULT BindParams(HACCESSOR* pHAccessor, ICommand* pCommand, void** ppParameterBuffer,
                     std::true_type) {
    _CAtlBindingCollector c;
    U::_VisitParams(static_cast<U*>(this), c);
    *ppParameterBuffer = static_cast<U*>(this);
    if (*pHAccessor != NULL) return S_OK;  // made already
    CComPtr<IAccessor> spAccessor;
    HRESULT hr = pCommand->QueryInterface(IID_IAccessor, (void**)&spAccessor);
    if (FAILED(hr)) return hr;
    return CreateOne(spAccessor, DBACCESSOR_PARAMETERDATA, c.m_bindings, 0, pHAccessor);
  }
  template <class U>
  HRESULT BindParams(HACCESSOR*, ICommand*, void** ppParameterBuffer, std::false_type) {
    *ppParameterBuffer = NULL;
    return S_OK;
  }
};

// Binds every column the rowset has, each as its own type.
class CDynamicAccessor : public CAccessorBase {
public:
  CDynamicAccessor(DBBLOBHANDLINGENUM eBlobHandling = DBBLOBHANDLING_DEFAULT,
                   DBLENGTH nBlobSize = 8000)
      : m_nColumns(0), m_pColumnInfo(NULL), m_pStringsBuffer(NULL), m_nBlobSize(nBlobSize),
        m_eBlobHandling(eBlobHandling) {}
  virtual ~CDynamicAccessor() { Close(); }

  void Close() {
    ::CoTaskMemFree(m_pColumnInfo);
    ::CoTaskMemFree(m_pStringsBuffer);
    m_pColumnInfo = NULL;
    m_pStringsBuffer = NULL;
    m_nColumns = 0;
    m_columns.clear();
    m_buffer.clear();
    m_pBuffer = NULL;
  }

  virtual HRESULT BindColumns(IUnknown* pUnk) {
    Close();
    CComPtr<IColumnsInfo> spInfo;
    HRESULT hr = pUnk->QueryInterface(IID_IColumnsInfo, (void**)&spInfo);
    if (FAILED(hr)) return hr;
    hr = spInfo->GetColumnInfo(&m_nColumns, &m_pColumnInfo, &m_pStringsBuffer);
    if (FAILED(hr)) return hr;
    // Each column: its value, then its length and status, aligned.
    std::vector<DBBINDING> bindings;
    DBBYTEOFFSET ob = 0;
    m_columns.resize(m_nColumns);
    for (DBORDINAL i = 0; i < m_nColumns; i++) {
      const DBCOLUMNINFO& info = m_pColumnInfo[i];
      DBTYPE wType = BindType(info.wType);
      DBLENGTH cb = ValueSize(wType, info.ulColumnSize);
      ob = Align(ob);
      DBBINDING b;
      memset(&b, 0, sizeof(b));
      b.iOrdinal = info.iOrdinal;
      b.obValue = ob;
      b.obLength = Align(ob + cb);
      b.obStatus = b.obLength + sizeof(DBLENGTH);
      b.dwPart = DBPART_VALUE | DBPART_LENGTH | DBPART_STATUS;
      b.dwMemOwner = DBMEMOWNER_CLIENTOWNED;
      b.eParamIO = DBPARAMIO_NOTPARAM;
      b.cbMaxLen = cb;
      b.wType = wType;
      b.bPrecision = info.bPrecision;
      b.bScale = info.bScale;
      m_columns[i].obValue = b.obValue;
      m_columns[i].obLength = b.obLength;
      m_columns[i].obStatus = b.obStatus;
      m_columns[i].wType = wType;
      m_columns[i].cbMaxLen = cb;
      ob = b.obStatus + sizeof(DBSTATUS);
      if (info.iOrdinal == 0) continue;  // the bookmark is not bound
      bindings.push_back(b);
    }
    m_buffer.assign((size_t)Align(ob) + 8, 0);
    m_pBuffer = &m_buffer[0];
    CComPtr<IAccessor> spAccessor;
    hr = pUnk->QueryInterface(IID_IAccessor, (void**)&spAccessor);
    if (FAILED(hr)) return hr;
    _AccessorInfo ai = {NULL, true};
    m_accessors.assign(1, ai);
    return CreateOne(spAccessor, DBACCESSOR_ROWDATA, bindings, 0, &m_accessors[0].hAccessor);
  }
  virtual bool HasOutputColumns() const { return true; }

  DBORDINAL GetColumnCount() const { return HasBookmark() ? m_nColumns - 1 : m_nColumns; }
  bool HasBookmark() const { return m_nColumns > 0 && m_pColumnInfo[0].iOrdinal == 0; }

  LPOLESTR GetColumnName(DBORDINAL nColumn) const {
    const DBCOLUMNINFO* p = Info(nColumn);
    return p != NULL ? p->pwszName : NULL;
  }
  bool GetColumnType(DBORDINAL nColumn, DBTYPE* pType) const {
    const DBCOLUMNINFO* p = Info(nColumn);
    if (p == NULL || pType == NULL) return false;
    *pType = p->wType;
    return true;
  }
  bool GetColumnFlags(DBORDINAL nColumn, DBCOLUMNFLAGS* pFlags) const {
    const DBCOLUMNINFO* p = Info(nColumn);
    if (p == NULL || pFlags == NULL) return false;
    *pFlags = p->dwFlags;
    return true;
  }
  bool GetOrdinal(LPCTSTR pColumnName, DBORDINAL* pOrdinal) const {
    if (pColumnName == NULL || pOrdinal == NULL) return false;
    _AtlDbWide wName(pColumnName);
    for (DBORDINAL i = 0; i < m_nColumns; i++) {
      if (m_pColumnInfo[i].pwszName != NULL && _wcsicmp(m_pColumnInfo[i].pwszName, wName) == 0) {
        *pOrdinal = m_pColumnInfo[i].iOrdinal;
        return true;
      }
    }
    return false;
  }

  void* GetValue(DBORDINAL nColumn) const {
    const _Column* p = Column(nColumn);
    return p != NULL ? m_pBuffer + p->obValue : NULL;
  }
  void* GetValue(LPCTSTR pColumnName) const {
    DBORDINAL n;
    return GetOrdinal(pColumnName, &n) ? GetValue(n) : NULL;
  }
  template <class ctype>
  bool GetValue(DBORDINAL nColumn, ctype* pData) const {
    void* p = GetValue(nColumn);
    if (p == NULL || pData == NULL) return false;
    memcpy(pData, p, sizeof(ctype));
    return true;
  }
  template <class ctype>
  bool GetValue(LPCTSTR pColumnName, ctype* pData) const {
    DBORDINAL n;
    return GetOrdinal(pColumnName, &n) && GetValue(n, pData);
  }
  template <class ctype>
  bool SetValue(DBORDINAL nColumn, const ctype& data) {
    void* p = GetValue(nColumn);
    if (p == NULL) return false;
    memcpy(p, &data, sizeof(ctype));
    SetStatus(nColumn, DBSTATUS_S_OK);
    return true;
  }
  template <class ctype>
  bool SetValue(LPCTSTR pColumnName, const ctype& data) {
    DBORDINAL n;
    return GetOrdinal(pColumnName, &n) && SetValue(n, data);
  }

  bool GetLength(DBORDINAL nColumn, DBLENGTH* pLength) const {
    const _Column* p = Column(nColumn);
    if (p == NULL || pLength == NULL) return false;
    *pLength = *(DBLENGTH*)(m_pBuffer + p->obLength);
    return true;
  }
  bool SetLength(DBORDINAL nColumn, DBLENGTH nLength) {
    const _Column* p = Column(nColumn);
    if (p == NULL) return false;
    *(DBLENGTH*)(m_pBuffer + p->obLength) = nLength;
    return true;
  }
  bool GetStatus(DBORDINAL nColumn, DBSTATUS* pStatus) const {
    const _Column* p = Column(nColumn);
    if (p == NULL || pStatus == NULL) return false;
    *pStatus = *(DBSTATUS*)(m_pBuffer + p->obStatus);
    return true;
  }
  bool SetStatus(DBORDINAL nColumn, DBSTATUS status) {
    const _Column* p = Column(nColumn);
    if (p == NULL) return false;
    *(DBSTATUS*)(m_pBuffer + p->obStatus) = status;
    return true;
  }

protected:
  struct _Column {
    DBBYTEOFFSET obValue, obLength, obStatus;
    DBTYPE wType;
    DBLENGTH cbMaxLen;
  };
  DBORDINAL m_nColumns;
  DBCOLUMNINFO* m_pColumnInfo;
  OLECHAR* m_pStringsBuffer;
  DBLENGTH m_nBlobSize;
  DBBLOBHANDLINGENUM m_eBlobHandling;
  std::vector<_Column> m_columns;
  std::vector<BYTE> m_buffer;

  static DBBYTEOFFSET Align(DBBYTEOFFSET ob) { return (ob + 7) & ~(DBBYTEOFFSET)7; }

  // The type each column is bound as; the string accessors change it.
  virtual DBTYPE BindType(DBTYPE wType) const {
    // Long data is read into the buffer, up to the blob size.
    return wType;
  }
  DBLENGTH ValueSize(DBTYPE wType, DBLENGTH ulColumnSize) const {
    switch (wType & ~DBTYPE_BYREF) {
      case DBTYPE_STR:
        return (ulColumnSize == 0 || ulColumnSize > m_nBlobSize ? m_nBlobSize : ulColumnSize) + 1;
      case DBTYPE_WSTR:
        return ((ulColumnSize == 0 || ulColumnSize > m_nBlobSize ? m_nBlobSize : ulColumnSize) + 1) *
               sizeof(WCHAR);
      case DBTYPE_BYTES:
        return ulColumnSize == 0 || ulColumnSize > m_nBlobSize ? m_nBlobSize : ulColumnSize;
      case DBTYPE_I1: case DBTYPE_UI1: return 1;
      case DBTYPE_I2: case DBTYPE_UI2: case DBTYPE_BOOL: return 2;
      case DBTYPE_I4: case DBTYPE_UI4: case DBTYPE_R4: case DBTYPE_ERROR: return 4;
      case DBTYPE_I8: case DBTYPE_UI8: case DBTYPE_R8: case DBTYPE_CY: case DBTYPE_DATE:
      case DBTYPE_FILETIME:
        return 8;
      case DBTYPE_GUID: return sizeof(GUID);
      case DBTYPE_DECIMAL: return sizeof(DECIMAL);
      case DBTYPE_NUMERIC: return sizeof(DB_NUMERIC);
      case DBTYPE_DBDATE: return sizeof(DBDATE);
      case DBTYPE_DBTIME: return sizeof(DBTIME);
      case DBTYPE_DBTIMESTAMP: return sizeof(DBTIMESTAMP);
      case DBTYPE_VARIANT: return sizeof(VARIANT);
      case DBTYPE_BSTR: return sizeof(BSTR);
      case DBTYPE_IUNKNOWN: case DBTYPE_IDISPATCH: return sizeof(IUnknown*);
      default: return ulColumnSize > 0 && ulColumnSize < m_nBlobSize ? ulColumnSize : 8;
    }
  }
  const DBCOLUMNINFO* Info(DBORDINAL nColumn) const {
    for (DBORDINAL i = 0; i < m_nColumns; i++)
      if (m_pColumnInfo[i].iOrdinal == nColumn) return &m_pColumnInfo[i];
    return NULL;
  }
  const _Column* Column(DBORDINAL nColumn) const {
    if (m_pBuffer == NULL) return NULL;
    for (DBORDINAL i = 0; i < m_nColumns; i++)
      if (m_pColumnInfo[i].iOrdinal == nColumn) return &m_columns[i];
    return NULL;
  }
};

// Every column as a string of BaseType (char or wchar_t).
template <class BaseType, DBTYPE OleDbType>
class CDynamicStringAccessorT : public CDynamicAccessor {
public:
  CDynamicStringAccessorT(DBLENGTH nBlobSize = 8000)
      : CDynamicAccessor(DBBLOBHANDLING_DEFAULT, nBlobSize) {}

  BaseType* GetString(DBORDINAL nColumn) const { return (BaseType*)GetValue(nColumn); }
  BaseType* GetString(LPCTSTR pColumnName) const { return (BaseType*)GetValue(pColumnName); }
  HRESULT SetString(DBORDINAL nColumn, const BaseType* data) {
    const _Column* p = Column(nColumn);
    if (p == NULL || data == NULL) return E_INVALIDARG;
    size_t n = 0;
    while (data[n] != 0) n++;
    if ((n + 1) * sizeof(BaseType) > p->cbMaxLen) return DB_S_ERRORSOCCURRED;
    memcpy(m_pBuffer + p->obValue, data, (n + 1) * sizeof(BaseType));
    SetLength(nColumn, n * sizeof(BaseType));
    SetStatus(nColumn, DBSTATUS_S_OK);
    return S_OK;
  }
  HRESULT SetString(LPCTSTR pColumnName, const BaseType* data) {
    DBORDINAL n;
    return GetOrdinal(pColumnName, &n) ? SetString(n, data) : E_INVALIDARG;
  }

protected:
  virtual DBTYPE BindType(DBTYPE) const { return OleDbType; }
};

typedef CDynamicStringAccessorT<CHAR, DBTYPE_STR> CDynamicStringAccessorA;
typedef CDynamicStringAccessorT<WCHAR, DBTYPE_WSTR> CDynamicStringAccessorW;
#ifdef _UNICODE
typedef CDynamicStringAccessorW CDynamicStringAccessor;
#else
typedef CDynamicStringAccessorA CDynamicStringAccessor;
#endif

// Bindings the program adds itself, into its own buffer.
class CManualAccessor : public CAccessorBase {
public:
  CManualAccessor() : m_pParamBuffer(NULL) {}

  HRESULT CreateAccessor(int nBindEntries, void* pBuffer, DBLENGTH nBufferSize) {
    (void)nBufferSize;
    m_pBuffer = (BYTE*)pBuffer;
    m_columnBindings.clear();
    m_columnBindings.reserve(nBindEntries);
    return S_OK;
  }
  void AddBindEntry(DBORDINAL nOrdinal, DBTYPE wType, DBLENGTH nColumnSize, void* pData,
                    void* pLength = NULL, void* pStatus = NULL) {
    m_columnBindings.push_back(Make(m_pBuffer, nOrdinal, wType, nColumnSize, pData, pLength,
                                    pStatus, DBPARAMIO_NOTPARAM));
  }
  HRESULT CreateParameterAccessor(int nBindEntries, void* pBuffer, DBLENGTH nBufferSize) {
    (void)nBufferSize;
    m_pParamBuffer = (BYTE*)pBuffer;
    m_paramBindings.clear();
    m_paramBindings.reserve(nBindEntries);
    return S_OK;
  }
  void AddParameterEntry(DBORDINAL nOrdinal, DBTYPE wType, DBLENGTH nColumnSize, void* pData,
                         void* pLength = NULL, void* pStatus = NULL,
                         DBPARAMIO eParamIO = DBPARAMIO_INPUT) {
    m_paramBindings.push_back(Make(m_pParamBuffer, nOrdinal, wType, nColumnSize, pData,
                                   pLength, pStatus, eParamIO));
  }

  virtual HRESULT BindColumns(IUnknown* pUnk) {
    if (m_columnBindings.empty()) return S_OK;
    CComPtr<IAccessor> spAccessor;
    HRESULT hr = pUnk->QueryInterface(IID_IAccessor, (void**)&spAccessor);
    if (FAILED(hr)) return hr;
    _AccessorInfo ai = {NULL, true};
    m_accessors.assign(1, ai);
    return CreateOne(spAccessor, DBACCESSOR_ROWDATA, m_columnBindings, 0,
                     &m_accessors[0].hAccessor);
  }
  virtual bool HasOutputColumns() const { return !m_columnBindings.empty(); }
  virtual bool HasParameters() const { return !m_paramBindings.empty(); }
  virtual HRESULT BindParameters(HACCESSOR* pHAccessor, ICommand* pCommand,
                                 void** ppParameterBuffer) {
    *ppParameterBuffer = m_pParamBuffer;
    if (m_paramBindings.empty() || *pHAccessor != NULL) return S_OK;
    CComPtr<IAccessor> spAccessor;
    HRESULT hr = pCommand->QueryInterface(IID_IAccessor, (void**)&spAccessor);
    if (FAILED(hr)) return hr;
    return CreateOne(spAccessor, DBACCESSOR_PARAMETERDATA, m_paramBindings, 0, pHAccessor);
  }

private:
  BYTE* m_pParamBuffer;
  std::vector<DBBINDING> m_columnBindings, m_paramBindings;

  static DBBINDING Make(BYTE* pBase, DBORDINAL nOrdinal, DBTYPE wType, DBLENGTH nColumnSize,
                        void* pData, void* pLength, void* pStatus, DBPARAMIO eParamIO) {
    DBBINDING b;
    memset(&b, 0, sizeof(b));
    b.iOrdinal = nOrdinal;
    b.obValue = (DBBYTEOFFSET)((BYTE*)pData - pBase);
    b.dwPart = DBPART_VALUE;
    if (pLength != NULL) {
      b.obLength = (DBBYTEOFFSET)((BYTE*)pLength - pBase);
      b.dwPart |= DBPART_LENGTH;
    }
    if (pStatus != NULL) {
      b.obStatus = (DBBYTEOFFSET)((BYTE*)pStatus - pBase);
      b.dwPart |= DBPART_STATUS;
    }
    b.dwMemOwner = DBMEMOWNER_CLIENTOWNED;
    b.eParamIO = eParamIO;
    b.cbMaxLen = nColumnSize;
    b.wType = wType;
    return b;
  }
};

// ---------------------------------------------------------------------
// Rowsets. CRowset<> (CRowset<CAccessorBase>) does the work; CRowset of
// a particular accessor derives from it, so any rowset converts to
// CRowset<>* (which COleDBRecordView asks for).

template <class TAccessor = CAccessorBase>
class CRowset;

template <>
class CRowset<CAccessorBase> {
public:
  CComPtr<IRowset> m_spRowset;
  CComPtr<IRowsetChange> m_spRowsetChange;
  HROW m_hRow;
  CAccessorBase* m_pAccessor;

  CRowset() : m_hRow(NULL), m_pAccessor(NULL) {}
  virtual ~CRowset() { Close(); }

  void Close() {
    if (m_spRowset != NULL) {
      ReleaseRows();
      m_spRowsetChange.Release();
      m_spRowset.Release();
    }
  }
  CRowset<>* GetRowsetBase() { return this; }
  IRowset* GetInterface() const { return m_spRowset; }
  IRowset** GetInterfacePtr() { return &m_spRowset.p; }
  void SetAccessor(CAccessorBase* pAccessor) { m_pAccessor = pAccessor; }

  HRESULT AddRefRows() {
    if (m_hRow == NULL) return S_OK;
    return m_spRowset->AddRefRows(1, &m_hRow, NULL, NULL);
  }
  HRESULT ReleaseRows() {
    if (m_spRowset == NULL || m_hRow == NULL) return S_OK;
    HRESULT hr = m_spRowset->ReleaseRows(1, &m_hRow, NULL, NULL, NULL);
    m_hRow = NULL;
    return hr;
  }

  HRESULT MoveFirst() {
    if (m_spRowset == NULL) return E_UNEXPECTED;
    ReleaseRows();
    HRESULT hr = m_spRowset->RestartPosition(DB_NULL_HCHAPTER);
    if (FAILED(hr)) return hr;
    return Fetch(0, 1);
  }
  HRESULT MoveNext() { return MoveNext(0, true); }
  HRESULT MoveNext(LONG lSkip, bool bForward = true) {
    if (m_spRowset == NULL) return E_UNEXPECTED;
    ReleaseRows();
    return Fetch(lSkip, bForward ? 1 : -1);
  }
  // Back past the current row and the one before it, then that one forward.
  HRESULT MovePrev() {
    if (m_spRowset == NULL) return E_UNEXPECTED;
    ReleaseRows();
    return Fetch(-2, 1);
  }
  HRESULT MoveLast() {
    if (m_spRowset == NULL) return E_UNEXPECTED;
    ReleaseRows();
    HRESULT hr = m_spRowset->RestartPosition(DB_NULL_HCHAPTER);
    if (FAILED(hr)) return hr;
    return Fetch(-1, 1);
  }

  HRESULT GetData() {
    if (m_hRow == NULL || m_pAccessor == NULL) return S_OK;
    HRESULT hr = S_OK;
    for (ULONG i = 0; i < m_pAccessor->GetNumAccessors(); i++) {
      if (!m_pAccessor->IsAutoAccessor(i)) continue;
      hr = GetData(i);
      if (FAILED(hr)) return hr;
    }
    return hr;
  }
  HRESULT GetData(int nAccessor) {
    if (m_hRow == NULL || m_pAccessor == NULL) return E_UNEXPECTED;
    return m_spRowset->GetData(m_hRow, m_pAccessor->GetHAccessor(nAccessor),
                               m_pAccessor->GetBuffer());
  }

  HRESULT SetData() {
    if (m_pAccessor == NULL) return E_UNEXPECTED;
    HRESULT hr = S_OK;
    for (ULONG i = 0; i < m_pAccessor->GetNumAccessors(); i++) {
      if (!m_pAccessor->IsAutoAccessor(i)) continue;
      hr = SetData(i);
      if (FAILED(hr)) return hr;
    }
    return hr;
  }
  HRESULT SetData(int nAccessor) {
    if (m_spRowsetChange == NULL || m_hRow == NULL) return E_UNEXPECTED;
    return m_spRowsetChange->SetData(m_hRow, m_pAccessor->GetHAccessor(nAccessor),
                                     m_pAccessor->GetBuffer());
  }
  HRESULT Insert(int nAccessor = 0, bool bGetHRow = false) {
    if (m_spRowsetChange == NULL || m_pAccessor == NULL) return E_UNEXPECTED;
    if (bGetHRow) {
      ReleaseRows();
      return m_spRowsetChange->InsertRow(DB_NULL_HCHAPTER, m_pAccessor->GetHAccessor(nAccessor),
                                         m_pAccessor->GetBuffer(), &m_hRow);
    }
    return m_spRowsetChange->InsertRow(DB_NULL_HCHAPTER, m_pAccessor->GetHAccessor(nAccessor),
                                       m_pAccessor->GetBuffer(), NULL);
  }
  HRESULT Delete() const {
    if (m_spRowsetChange == NULL || m_hRow == NULL) return E_UNEXPECTED;
    DBROWSTATUS status;
    return m_spRowsetChange->DeleteRows(DB_NULL_HCHAPTER, 1, &m_hRow, &status);
  }
  // With deferred updates (IRowsetUpdate): send, or throw away, the changes.
  HRESULT Update(DBCOUNTITEM* pcRows = NULL, HROW** ppRow = NULL,
                 DBROWSTATUS** ppStatus = NULL) {
    if (m_spRowset == NULL) return E_UNEXPECTED;
    CComPtr<IRowsetUpdate> sp;
    HRESULT hr = m_spRowset->QueryInterface(IID_IRowsetUpdate, (void**)&sp);
    if (FAILED(hr)) return hr;
    DBCOUNTITEM c = 0;
    HROW* pRows = NULL;
    DBROWSTATUS* pStatus = NULL;
    hr = sp->Update(DB_NULL_HCHAPTER, 0, NULL, &c, &pRows, &pStatus);
    if (pcRows != NULL) *pcRows = c;
    if (ppRow != NULL) *ppRow = pRows; else ::CoTaskMemFree(pRows);
    if (ppStatus != NULL) *ppStatus = pStatus; else ::CoTaskMemFree(pStatus);
    return hr;
  }
  HRESULT Undo(DBCOUNTITEM* pcRows = NULL, HROW** ppRow = NULL, DBROWSTATUS** ppStatus = NULL) {
    if (m_spRowset == NULL) return E_UNEXPECTED;
    CComPtr<IRowsetUpdate> sp;
    HRESULT hr = m_spRowset->QueryInterface(IID_IRowsetUpdate, (void**)&sp);
    if (FAILED(hr)) return hr;
    DBCOUNTITEM c = 0;
    HROW* pRows = NULL;
    DBROWSTATUS* pStatus = NULL;
    hr = sp->Undo(DB_NULL_HCHAPTER, 0, NULL, &c, &pRows, &pStatus);
    if (pcRows != NULL) *pcRows = c;
    if (ppRow != NULL) *ppRow = pRows; else ::CoTaskMemFree(pRows);
    if (ppStatus != NULL) *ppStatus = pStatus; else ::CoTaskMemFree(pStatus);
    return hr;
  }

  void SetupOptionalRowsetInterfaces() {
    m_spRowsetChange.Release();
    if (m_spRowset != NULL)
      m_spRowset->QueryInterface(IID_IRowsetChange, (void**)&m_spRowsetChange);
  }

protected:
  HRESULT Fetch(DBROWOFFSET lOffset, DBROWCOUNT cRows) {
    DBCOUNTITEM nFetched = 0;
    HROW* phRow = &m_hRow;
    HRESULT hr = m_spRowset->GetNextRows(DB_NULL_HCHAPTER, lOffset, cRows, &nFetched, &phRow);
    if (FAILED(hr)) {
      m_hRow = NULL;
      return hr;
    }
    if (nFetched == 0) {
      m_hRow = NULL;
      return DB_S_ENDOFROWSET;
    }
    return GetData();
  }

};

template <class TAccessor>
class CRowset : public CRowset<CAccessorBase> {};

// For commands that return no rows.
template <class TAccessor = CAccessorBase>
class CNoRowset {
public:
  void Close() {}
  IRowset* GetInterface() const { return NULL; }
  IRowset** GetInterfacePtr() { return NULL; }
  void SetAccessor(CAccessorBase*) {}
  void SetupOptionalRowsetInterfaces() {}
  CRowset<>* GetRowsetBase() { return NULL; }
};

// The accessor and the rowset in one; Bind connects them.
template <class TAccessor = CNoAccessor, template <typename T> class TRowset = CRowset>
class CAccessorRowset : public TAccessor, public TRowset<TAccessor> {
public:
  CAccessorRowset() { TRowset<TAccessor>::SetAccessor(this); }
  virtual ~CAccessorRowset() { Close(); }

  HRESULT Bind() {
    IRowset* pRowset = TRowset<TAccessor>::GetInterface();
    if (pRowset == NULL) return E_UNEXPECTED;
    TRowset<TAccessor>::SetAccessor(this);
    return TAccessor::BindColumns(pRowset);
  }
  void Close() {
    IRowset* pRowset = TRowset<TAccessor>::GetInterface();
    if (pRowset != NULL) TAccessor::ReleaseAccessors(pRowset);
    TRowset<TAccessor>::Close();
  }
  HRESULT GetColumnInfo(DBORDINAL* pulColumns, DBCOLUMNINFO** ppColumnInfo,
                        OLECHAR** ppStrings) const {
    IRowset* pRowset = const_cast<CAccessorRowset*>(this)->TRowset<TAccessor>::GetInterface();
    if (pRowset == NULL) return E_UNEXPECTED;
    CComPtr<IColumnsInfo> sp;
    HRESULT hr = pRowset->QueryInterface(IID_IColumnsInfo, (void**)&sp);
    if (FAILED(hr)) return hr;
    return sp->GetColumnInfo(pulColumns, ppColumnInfo, ppStrings);
  }
};

// ---------------------------------------------------------------------
// CTable

template <class TAccessor = CNoAccessor, template <typename T> class TRowset = CRowset>
class CTable : public CAccessorRowset<TAccessor, TRowset> {
public:
  HRESULT Open(const CSession& session, LPCTSTR szTableName, DBPROPSET* pPropSet = NULL,
               ULONG nPropertySets = 1) {
    _AtlDbWide wName(szTableName);
    DBID id;
    id.eKind = DBKIND_NAME;
    id.uName.pwszName = wName;
    return Open(session, id, pPropSet, nPropertySets);
  }
  HRESULT Open(const CSession& session, DBID& dbid, DBPROPSET* pPropSet = NULL,
               ULONG nPropertySets = 1) {
    if (session.m_spOpenRowset == NULL) return E_UNEXPECTED;
    this->Close();
    IRowset** pp = this->GetInterfacePtr();
    HRESULT hr = session.m_spOpenRowset->OpenRowset(NULL, &dbid, NULL, IID_IRowset,
                                                    pPropSet != NULL ? nPropertySets : 0,
                                                    pPropSet, (IUnknown**)pp);
    if (FAILED(hr)) return hr;
    this->SetupOptionalRowsetInterfaces();
    if (this->HasOutputColumns()) hr = this->Bind();
    return hr;
  }
};

// ---------------------------------------------------------------------
// CCommand

class CNoMultipleResults {
public:
  bool UseMultipleResults() const { return false; }
  IMultipleResults** GetMultiplePtrAddress() { return NULL; }
  IMultipleResults* GetMultiplePtr() { return NULL; }
};

class CMultipleResults {
public:
  CComPtr<IMultipleResults> m_spMultipleResults;
  bool UseMultipleResults() const { return true; }
  IMultipleResults** GetMultiplePtrAddress() { return &m_spMultipleResults.p; }
  IMultipleResults* GetMultiplePtr() { return m_spMultipleResults; }
};

class CCommandBase {
public:
  CComPtr<ICommand> m_spCommand;
  HACCESSOR m_hParameterAccessor;

  CCommandBase() : m_hParameterAccessor(NULL) {}
  ~CCommandBase() { ReleaseCommand(); }

  HRESULT CreateCommand(const CSession& session) {
    ReleaseCommand();
    if (session.m_spOpenRowset == NULL) return E_UNEXPECTED;
    CComPtr<IDBCreateCommand> spCreate;
    HRESULT hr = session.m_spOpenRowset->QueryInterface(IID_IDBCreateCommand, (void**)&spCreate);
    if (FAILED(hr)) return hr;
    return spCreate->CreateCommand(NULL, IID_ICommand, (IUnknown**)&m_spCommand);
  }
  HRESULT Create(const CSession& session, LPCWSTR wszCommand, REFGUID guidCommand) {
    HRESULT hr = CreateCommand(session);
    if (FAILED(hr)) return hr;
    CComPtr<ICommandText> spText;
    hr = m_spCommand->QueryInterface(IID_ICommandText, (void**)&spText);
    if (FAILED(hr)) return hr;
    return spText->SetCommandText(guidCommand, wszCommand);
  }
  HRESULT Prepare(ULONG cExpectedRuns = 0) {
    CComPtr<ICommandPrepare> sp;
    HRESULT hr = m_spCommand->QueryInterface(IID_ICommandPrepare, (void**)&sp);
    if (FAILED(hr)) return hr;
    return sp->Prepare(cExpectedRuns);
  }
  HRESULT Unprepare() {
    CComPtr<ICommandPrepare> sp;
    HRESULT hr = m_spCommand->QueryInterface(IID_ICommandPrepare, (void**)&sp);
    if (FAILED(hr)) return hr;
    return sp->Unprepare();
  }
  HRESULT SetParameterInfo(DB_UPARAMS ulParams, const DBORDINAL* pOrdinals,
                           const DBPARAMBINDINFO* pParamInfo) {
    CComPtr<ICommandWithParameters> sp;
    HRESULT hr = m_spCommand->QueryInterface(IID_ICommandWithParameters, (void**)&sp);
    if (FAILED(hr)) return hr;
    return sp->SetParameterInfo(ulParams, pOrdinals, pParamInfo);
  }
  void ReleaseCommand() {
    if (m_hParameterAccessor != NULL && m_spCommand != NULL) {
      CComPtr<IAccessor> sp;
      if (SUCCEEDED(m_spCommand->QueryInterface(IID_IAccessor, (void**)&sp)))
        sp->ReleaseAccessor(m_hParameterAccessor, NULL);
    }
    m_hParameterAccessor = NULL;
    m_spCommand.Release();
  }
};

template <class TAccessor = CNoAccessor, template <typename T> class TRowset = CRowset,
          class TMultiple = CNoMultipleResults>
class CCommand : public CAccessorRowset<TAccessor, TRowset>,
                 public CCommandBase,
                 public TMultiple {
public:
  // The command text in either width; NULL (or no text) runs the
  // accessor's DEFINE_COMMAND.
  HRESULT Open(const CSession& session, LPCWSTR wszCommand, DBPROPSET* pPropSet = NULL,
               DBROWCOUNT* pRowsAffected = NULL, REFGUID guidCommand = _ATLDB_DBGUID_DEFAULT,
               bool bBind = true, ULONG nPropertySets = 1) {
    HRESULT hr;
    if (wszCommand == NULL)
      hr = DefaultCommand(session, guidCommand, _AtlHasDefaultCommand<TAccessor>());
    else
      hr = Create(session, wszCommand, guidCommand);
    if (FAILED(hr)) return hr;
    return Open(pPropSet, pRowsAffected, bBind, nPropertySets);
  }
  HRESULT Open(const CSession& session, LPCSTR szCommand, DBPROPSET* pPropSet = NULL,
               DBROWCOUNT* pRowsAffected = NULL, REFGUID guidCommand = _ATLDB_DBGUID_DEFAULT,
               bool bBind = true, ULONG nPropertySets = 1) {
    if (szCommand == NULL)
      return Open(session, (LPCWSTR)NULL, pPropSet, pRowsAffected, guidCommand, bBind,
                  nPropertySets);
    int n = ::MultiByteToWideChar(CP_ACP, 0, szCommand, -1, NULL, 0);
    WCHAR* pw = new WCHAR[n > 0 ? n : 1];
    pw[0] = 0;
    ::MultiByteToWideChar(CP_ACP, 0, szCommand, -1, pw, n);
    HRESULT hr = Open(session, pw, pPropSet, pRowsAffected, guidCommand, bBind, nPropertySets);
    delete[] pw;
    return hr;
  }
  HRESULT Open(const CSession& session, int szCommand = 0, DBPROPSET* pPropSet = NULL,
               DBROWCOUNT* pRowsAffected = NULL, REFGUID guidCommand = _ATLDB_DBGUID_DEFAULT,
               bool bBind = true, ULONG nPropertySets = 1) {
    (void)szCommand;
    return Open(session, (LPCWSTR)NULL, pPropSet, pRowsAffected, guidCommand, bBind,
                nPropertySets);
  }

  // Runs the command made already, again if need be.
  HRESULT Open(DBPROPSET* pPropSet = NULL, DBROWCOUNT* pRowsAffected = NULL, bool bBind = true,
               ULONG nPropertySets = 1) {
    if (m_spCommand == NULL) return E_UNEXPECTED;
    this->CAccessorRowset<TAccessor, TRowset>::Close();
    HRESULT hr;
    if (pPropSet != NULL && nPropertySets > 0) {
      CComPtr<ICommandProperties> spProps;
      hr = m_spCommand->QueryInterface(IID_ICommandProperties, (void**)&spProps);
      if (SUCCEEDED(hr)) hr = spProps->SetProperties(nPropertySets, pPropSet);
      if (FAILED(hr)) return hr;
    }
    DBPARAMS params;
    DBPARAMS* pParams = NULL;
    if (this->HasParameters()) {
      void* pBuffer = NULL;
      hr = this->BindParameters(&m_hParameterAccessor, m_spCommand, &pBuffer);
      if (FAILED(hr)) return hr;
      params.pData = pBuffer;
      params.cParamSets = 1;
      params.hAccessor = m_hParameterAccessor;
      pParams = &params;
    }
    IRowset** ppRowset = this->GetInterfacePtr();
    if (TMultiple::UseMultipleResults()) {
      hr = m_spCommand->Execute(NULL, IID_IMultipleResults, pParams, pRowsAffected,
                                (IUnknown**)TMultiple::GetMultiplePtrAddress());
      if (FAILED(hr)) return hr;
      return GetNextResult(pRowsAffected, bBind);
    }
    if (ppRowset == NULL) {
      return m_spCommand->Execute(NULL, IID_NULL, pParams, pRowsAffected, NULL);
    }
    hr = m_spCommand->Execute(NULL, IID_IRowset, pParams, pRowsAffected, (IUnknown**)ppRowset);
    if (FAILED(hr)) return hr;
    if (*ppRowset == NULL) return hr;  // no rows to show
    this->SetupOptionalRowsetInterfaces();
    if (bBind && this->HasOutputColumns()) {
      HRESULT hrBind = this->Bind();
      if (FAILED(hrBind)) return hrBind;
    }
    return hr;
  }

  HRESULT GetNextResult(DBROWCOUNT* pulRowsAffected, bool bBind = true) {
    IMultipleResults* pMultiple = TMultiple::GetMultiplePtr();
    if (pMultiple == NULL) return E_UNEXPECTED;
    this->CAccessorRowset<TAccessor, TRowset>::Close();
    IRowset** ppRowset = this->GetInterfacePtr();
    HRESULT hr = pMultiple->GetResult(NULL, 0, IID_IRowset, pulRowsAffected,
                                      (IUnknown**)ppRowset);
    if (FAILED(hr) || hr == DB_S_NORESULT || ppRowset == NULL || *ppRowset == NULL) return hr;
    this->SetupOptionalRowsetInterfaces();
    if (bBind && this->HasOutputColumns()) {
      HRESULT hrBind = this->Bind();
      if (FAILED(hrBind)) return hrBind;
    }
    return hr;
  }

  void Close() { this->CAccessorRowset<TAccessor, TRowset>::Close(); }

private:
  template <class A = TAccessor>
  HRESULT DefaultCommand(const CSession& session, REFGUID guidCommand, std::true_type) {
    return CreateDefault(session, guidCommand, &A::GetDefaultCommand);
  }
  HRESULT DefaultCommand(const CSession&, REFGUID, std::false_type) { return E_INVALIDARG; }
  HRESULT CreateDefault(const CSession& session, REFGUID guidCommand,
                        HRESULT (*pfn)(LPCTSTR*)) {
    LPCTSTR psz = NULL;
    HRESULT hr = pfn(&psz);
    if (FAILED(hr)) return hr;
    return Create(session, _AtlDbWide(psz), guidCommand);
  }
#ifndef _UNICODE
  HRESULT CreateDefault(const CSession& session, REFGUID guidCommand,
                        HRESULT (*pfn)(LPCWSTR*)) {
    LPCWSTR psz = NULL;
    HRESULT hr = pfn(&psz);
    if (FAILED(hr)) return hr;
    return Create(session, psz, guidCommand);
  }
#endif
};

// ---------------------------------------------------------------------
// Error records (IErrorRecords) after a failure, as text.

class CDBErrorInfo {
public:
  HRESULT GetErrorRecords(IUnknown* pUnk, const IID& iid, ULONG* pcRecords) {
    m_spErrorRecords.Release();
    if (pcRecords == NULL) return E_POINTER;
    *pcRecords = 0;
    CComPtr<ISupportErrorInfo> spSupport;
    if (pUnk == NULL || FAILED(pUnk->QueryInterface(IID_ISupportErrorInfo, (void**)&spSupport)) ||
        spSupport->InterfaceSupportsErrorInfo(iid) != S_OK)
      return E_NOINTERFACE;
    return GetErrorRecords(pcRecords);
  }
  HRESULT GetErrorRecords(ULONG* pcRecords) {
    m_spErrorRecords.Release();
    if (pcRecords == NULL) return E_POINTER;
    *pcRecords = 0;
    CComPtr<IErrorInfo> spInfo;
    HRESULT hr = ::GetErrorInfo(0, &spInfo);
    if (hr != S_OK) return E_FAIL;
    hr = spInfo->QueryInterface(IID_IErrorRecords, (void**)&m_spErrorRecords);
    if (FAILED(hr)) return hr;
    return m_spErrorRecords->GetRecordCount(pcRecords);
  }
  HRESULT GetAllErrorInfo(ULONG ulRecordNum, LCID lcid, BSTR* pbstrDescription,
                          BSTR* pbstrSource = NULL, GUID* pguid = NULL,
                          DWORD* pdwHelpContext = NULL, BSTR* pbstrHelpFile = NULL) const {
    if (m_spErrorRecords == NULL) return E_UNEXPECTED;
    CComPtr<IErrorInfo> spInfo;
    HRESULT hr = m_spErrorRecords->GetErrorInfo(ulRecordNum, lcid, &spInfo);
    if (FAILED(hr)) return hr;
    if (pbstrDescription != NULL) spInfo->GetDescription(pbstrDescription);
    if (pbstrSource != NULL) spInfo->GetSource(pbstrSource);
    if (pguid != NULL) spInfo->GetGUID(pguid);
    if (pdwHelpContext != NULL) spInfo->GetHelpContext(pdwHelpContext);
    if (pbstrHelpFile != NULL) spInfo->GetHelpFile(pbstrHelpFile);
    return S_OK;
  }
  HRESULT GetBasicErrorInfo(ULONG ulRecordNum, ERRORINFO* pErrorInfo) const {
    if (m_spErrorRecords == NULL) return E_UNEXPECTED;
    return m_spErrorRecords->GetBasicErrorInfo(ulRecordNum, pErrorInfo);
  }
  HRESULT GetErrorParameters(ULONG ulRecordNum, DISPPARAMS* pdispparams) const {
    if (m_spErrorRecords == NULL) return E_UNEXPECTED;
    return m_spErrorRecords->GetErrorParameters(ulRecordNum, pdispparams);
  }

  CComPtr<IErrorRecords> m_spErrorRecords;
};

inline void AtlTraceErrorRecords(HRESULT hrErr = S_OK) {
  (void)hrErr;
  CDBErrorInfo info;
  ULONG c = 0;
  if (FAILED(info.GetErrorRecords(&c))) return;
  for (ULONG i = 0; i < c; i++) {
    BSTR bstr = NULL;
    if (SUCCEEDED(info.GetAllErrorInfo(i, ::GetUserDefaultLCID(), &bstr)) && bstr != NULL) {
      ::OutputDebugStringW(bstr);
      ::OutputDebugStringW(L"\n");
      ::SysFreeString(bstr);
    }
  }
}

}  // namespace ATL

#endif  // __ATLDBCLI_H__
