// daocore.cpp - the DAO engine, CDaoException, CDaoWorkspace,
// CDaoDatabase, CDaoTableDef and CDaoQueryDef.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "daoimpl.h"
#include <initguid.h>
#include <dbdaoid.h>

// DAO 3.6's engine; dbdaoid.h names 3.5's.
DEFINE_DAOGUID(_AFX_CLSID_DAODBEngine36, 0x00000100);

// ---------------------------------------------------------------------
// Helpers

_AfxDaoBstr::_AfxDaoBstr(LPCTSTR psz) {
  m_bstr = psz != NULL ? ::SysAllocString(CStringW(psz)) : NULL;
}

CString AFXAPI _AfxDaoString(BSTR bstr) {
  CString str(bstr != NULL ? (LPCWSTR)bstr : L"");
  ::SysFreeString(bstr);
  return str;
}

#ifdef _UNICODE
void AFXAPI _AfxDaoToEngine(COleVariant&) {}
void AFXAPI _AfxDaoFromEngine(COleVariant&) {}
#else
void AFXAPI _AfxDaoToEngine(COleVariant& var) {
  if (var.vt != VT_BSTR || var.bstrVal == NULL) return;
  CStringW str(CStringA((LPCSTR)var.bstrVal, (int)::SysStringByteLen(var.bstrVal)));
  BSTR bstr = ::SysAllocStringLen(str, (UINT)str.GetLength());
  if (bstr == NULL) AfxThrowMemoryException();
  var.Clear();
  var.vt = VT_BSTR;
  var.bstrVal = bstr;
}
void AFXAPI _AfxDaoFromEngine(COleVariant& var) {
  if (var.vt != VT_BSTR || var.bstrVal == NULL) return;
  CStringA str(CStringW(var.bstrVal, (int)::SysStringLen(var.bstrVal)));
  BSTR bstr = ::SysAllocStringByteLen(str, (UINT)str.GetLength());
  if (bstr == NULL) AfxThrowMemoryException();
  var.Clear();
  var.vt = VT_BSTR;
  var.bstrVal = bstr;
}
#endif

VARIANT AFXAPI _AfxDaoOptional() {
  VARIANT v;
  ::VariantInit(&v);
  v.vt = VT_ERROR;
  v.scode = DISP_E_PARAMNOTFOUND;
  return v;
}

COleVariant AFXAPI _AfxDaoItem(LPCTSTR lpszName) { return COleVariant(CString(lpszName), VT_BSTR); }

COleVariant AFXAPI _AfxDaoItem(int nIndex) { return COleVariant((long)nIndex); }

COleVariant AFXAPI _AfxDaoLong(long l) { return COleVariant(l); }

COleVariant AFXAPI _AfxDaoBool(BOOL b) {
  COleVariant v;
  v.vt = VT_BOOL;
  v.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
  return v;
}

// A name in a DFX call or in brackets, without the brackets.
CString AFXAPI _AfxDaoStripBrackets(LPCTSTR lpszName) {
  CString str(lpszName);
  str.Trim();
  if (str.GetLength() >= 2 && str[0] == '[' && str[str.GetLength() - 1] == ']')
    str = str.Mid(1, str.GetLength() - 2);
  return str;
}

static COleDateTime DateOf(const VARIANT& v) {
  COleDateTime dt;
  if (v.vt == VT_DATE) dt = v.date;
  else dt.SetStatus(COleDateTime::null);
  return dt;
}

// ---------------------------------------------------------------------
// The engine

static DAODBEngine* s_pEngine = NULL;
extern void (AFXAPI* _afxPfnDaoTerm)();

static HRESULT CreateEngine(REFCLSID clsid, DAODBEngine** ppEngine) {
  HRESULT hr = ::CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, IID_IDAODBEngineW,
                                  (void**)ppEngine);
  if (hr != CLASS_E_NOTLICENSED) return hr;
  // A licensed class: its run-time key is in the DAO SDK's headers.
  IClassFactory2* pFactory = NULL;
  hr = ::CoGetClassObject(clsid, CLSCTX_INPROC_SERVER, NULL, IID_IClassFactory2,
                          (void**)&pFactory);
  if (FAILED(hr)) return hr;
  BSTR bstrKey = ::SysAllocString(L"mbmabptebkjcdlgtjmskjwtsdhjbmkmwtrak");
  hr = pFactory->CreateInstanceLic(NULL, NULL, IID_IDAODBEngineW, bstrKey, (void**)ppEngine);
  ::SysFreeString(bstrKey);
  pFactory->Release();
  return hr;
}

void AFXAPI AfxDaoInit() {
  if (s_pEngine != NULL) return;
  HRESULT hr = CreateEngine(_AFX_CLSID_DAODBEngine36, &s_pEngine);
  if (hr == CO_E_NOTINITIALIZED) {
    ::CoInitialize(NULL);
    hr = CreateEngine(_AFX_CLSID_DAODBEngine36, &s_pEngine);
  }
  if (FAILED(hr)) hr = CreateEngine(CLSID_CDAODBEngine, &s_pEngine);
  if (FAILED(hr) || s_pEngine == NULL) {
    s_pEngine = NULL;
    AfxThrowDaoException(AFX_DAO_ERROR_ENGINE_INITIALIZATION, hr);
  }
  _afxPfnDaoTerm = AfxDaoTerm;
}

void AFXAPI AfxDaoTerm() {
  if (s_pEngine != NULL) {
    s_pEngine->Release();
    s_pEngine = NULL;
  }
  _afxPfnDaoTerm = NULL;
}

DAODBEngine* AFXAPI AfxDaoGetEngine() {
  if (s_pEngine == NULL) AfxDaoInit();
  return s_pEngine;
}

// ---------------------------------------------------------------------
// CDaoException

IMPLEMENT_DYNAMIC(CDaoException, CException)

CDaoException::CDaoException()
    : m_pErrorInfo(NULL), m_scode(S_OK), m_nAfxDaoError(NO_AFX_DAO_ERROR), m_pDAOError(NULL),
      m_pDAOErrors(NULL) {}

CDaoException::~CDaoException() {
  delete m_pErrorInfo;
  if (m_pDAOError != NULL) m_pDAOError->Release();
  if (m_pDAOErrors != NULL) m_pDAOErrors->Release();
}

void CDaoException::InitErrorsCollection() {
  if (m_pDAOErrors != NULL || s_pEngine == NULL) return;
  if (FAILED(s_pEngine->get_Errors(&m_pDAOErrors))) m_pDAOErrors = NULL;
}

short CDaoException::GetErrorCount() {
  InitErrorsCollection();
  short n = 0;
  if (m_pDAOErrors != NULL && FAILED(m_pDAOErrors->get_Count(&n))) n = 0;
  return n;
}

void CDaoException::GetErrorInfo(int nIndex) {
  InitErrorsCollection();
  if (m_pDAOErrors == NULL) return;
  if (m_pDAOError != NULL) {
    m_pDAOError->Release();
    m_pDAOError = NULL;
  }
  if (FAILED(m_pDAOErrors->get_Item(_AfxDaoItem(nIndex), &m_pDAOError)) || m_pDAOError == NULL)
    return;
  FillErrorInfo();
}

void CDaoException::FillErrorInfo() {
  if (m_pErrorInfo == NULL) m_pErrorInfo = new CDaoErrorInfo;
  LONG l = 0;
  m_pDAOError->get_Number(&l);
  m_pErrorInfo->m_lErrorCode = l;
  BSTR bstr = NULL;
  m_pDAOError->get_Source(&bstr);
  m_pErrorInfo->m_strSource = _AfxDaoString(bstr);
  bstr = NULL;
  m_pDAOError->get_Description(&bstr);
  m_pErrorInfo->m_strDescription = _AfxDaoString(bstr);
  bstr = NULL;
  m_pDAOError->get_HelpFile(&bstr);
  m_pErrorInfo->m_strHelpFile = _AfxDaoString(bstr);
  l = 0;
  m_pDAOError->get_HelpContext(&l);
  m_pErrorInfo->m_lHelpContext = l;
}

BOOL CDaoException::GetErrorMessage(LPTSTR lpszError, UINT nMaxError, PUINT pnHelpContext) {
  CString str;
  if (m_pErrorInfo != NULL) {
    str = m_pErrorInfo->m_strDescription;
    if (pnHelpContext != NULL) *pnHelpContext = (UINT)m_pErrorInfo->m_lHelpContext;
  } else {
    if (pnHelpContext != NULL) *pnHelpContext = 0;
    switch (m_nAfxDaoError) {
      case AFX_DAO_ERROR_ENGINE_INITIALIZATION: str = _T("The DAO engine could not be started."); break;
      case AFX_DAO_ERROR_DFX_BIND: str = _T("A field could not be bound."); break;
      case AFX_DAO_ERROR_OBJECT_NOT_OPEN: str = _T("The object is not open."); break;
      default: {
        LPTSTR pszMsg = NULL;
        if (::FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                FORMAT_MESSAGE_IGNORE_INSERTS,
                            NULL, (DWORD)m_scode, 0, (LPTSTR)&pszMsg, 0, NULL) != 0) {
          str = pszMsg;
          str.TrimRight();
          ::LocalFree(pszMsg);
        } else {
          str.Format(_T("DAO error 0x%08lX."), (unsigned long)m_scode);
        }
      }
    }
  }
  if (lpszError != NULL && nMaxError != 0) lstrcpyn(lpszError, str, (int)nMaxError);
  return !str.IsEmpty();
}

void AFXAPI AfxThrowDaoException(int nAfxDaoError, SCODE scode) {
  CDaoException* e = new CDaoException;
  e->m_nAfxDaoError = nAfxDaoError;
  e->m_scode = scode;
  if (nAfxDaoError == NO_AFX_DAO_ERROR && e->GetErrorCount() > 0) e->GetErrorInfo(0);
  throw e;
}

void AFXAPI AfxDaoCheck(SCODE scode, LPCSTR, LPCSTR, int, int nError, BOOL bMemOnly) {
  if (SUCCEEDED(scode)) return;
  if (bMemOnly && scode != E_OUTOFMEMORY) return;
  if (scode == E_OUTOFMEMORY) AfxThrowMemoryException();
  AfxThrowDaoException(nError, scode);
}

// ---------------------------------------------------------------------
// The information structures

CDaoIndexInfo::CDaoIndexInfo()
    : m_pFieldInfos(NULL), m_nFields(0), m_bPrimary(FALSE), m_bUnique(FALSE),
      m_bClustered(FALSE), m_bIgnoreNulls(FALSE), m_bRequired(FALSE), m_bForeign(FALSE),
      m_lDistinctCount(0), m_bCleanupFieldInfo(FALSE) {}

CDaoIndexInfo::~CDaoIndexInfo() {
  if (m_bCleanupFieldInfo) delete[] m_pFieldInfos;
}

CDaoRelationInfo::CDaoRelationInfo()
    : m_lAttributes(0), m_pFieldInfos(NULL), m_nFields(0), m_bCleanupFieldInfo(FALSE) {}

CDaoRelationInfo::~CDaoRelationInfo() {
  if (m_bCleanupFieldInfo) delete[] m_pFieldInfos;
}

#define DAO_GET_STRING(obj, prop, dest) \
  do {                                  \
    BSTR _b = NULL;                     \
    DAO_CHECK((obj)->get_##prop(&_b));  \
    dest = _AfxDaoString(_b);           \
  } while (0)
#define DAO_GET_BOOL(obj, prop, dest) \
  do {                                \
    VARIANT_BOOL _b = VARIANT_FALSE;  \
    DAO_CHECK((obj)->get_##prop(&_b)); \
    dest = _b != VARIANT_FALSE;       \
  } while (0)

void AFXAPI _AfxDaoFillWorkspaceInfo(DAOWorkspace* p, CDaoWorkspaceInfo& info, DWORD dwOptions) {
  if (dwOptions & AFX_DAO_PRIMARY_INFO) DAO_GET_STRING(p, Name, info.m_strName);
  if (dwOptions & AFX_DAO_SECONDARY_INFO) DAO_GET_STRING(p, UserName, info.m_strUserName);
  if ((dwOptions & AFX_DAO_ALL_INFO) == AFX_DAO_ALL_INFO) {
    short s = 0;
    if (SUCCEEDED(p->get_IsolateODBCTrans(&s))) info.m_bIsolateODBCTrans = s != 0;
  }
}

void AFXAPI _AfxDaoFillDatabaseInfo(DAODatabase* p, CDaoDatabaseInfo& info, DWORD dwOptions) {
  if (dwOptions & AFX_DAO_PRIMARY_INFO) {
    DAO_GET_STRING(p, Name, info.m_strName);
    DAO_GET_BOOL(p, Updatable, info.m_bUpdatable);
    DAO_GET_BOOL(p, Transactions, info.m_bTransactions);
  }
  if (dwOptions & AFX_DAO_SECONDARY_INFO) {
    DAO_GET_STRING(p, Version, info.m_strVersion);
    LONG l = 0;
    DAO_CHECK(p->get_CollatingOrder(&l));
    info.m_lCollatingOrder = l;
    short s = 0;
    DAO_CHECK(p->get_QueryTimeout(&s));
    info.m_nQueryTimeout = s;
  }
  if ((dwOptions & AFX_DAO_ALL_INFO) == AFX_DAO_ALL_INFO) DAO_GET_STRING(p, Connect, info.m_strConnect);
}

void AFXAPI _AfxDaoFillTableDefInfo(DAOTableDef* p, CDaoTableDefInfo& info, DWORD dwOptions) {
  if (dwOptions & AFX_DAO_PRIMARY_INFO) {
    DAO_GET_STRING(p, Name, info.m_strName);
    DAO_GET_BOOL(p, Updatable, info.m_bUpdatable);
    LONG l = 0;
    DAO_CHECK(p->get_Attributes(&l));
    info.m_lAttributes = l;
  }
  if (dwOptions & AFX_DAO_SECONDARY_INFO) {
    COleVariant v;
    DAO_CHECK(p->get_DateCreated(&v));
    info.m_dateCreated = DateOf(v);
    v.Clear();
    DAO_CHECK(p->get_LastUpdated(&v));
    info.m_dateLastUpdated = DateOf(v);
    DAO_GET_STRING(p, SourceTableName, info.m_strSrcTableName);
    DAO_GET_STRING(p, Connect, info.m_strConnect);
    DAO_GET_STRING(p, ValidationRule, info.m_strValidationRule);
    DAO_GET_STRING(p, ValidationText, info.m_strValidationText);
  }
  if ((dwOptions & AFX_DAO_ALL_INFO) == AFX_DAO_ALL_INFO) {
    LONG l = 0;
    DAO_CHECK(p->get_RecordCount(&l));
    info.m_lRecordCount = l;
  }
}

void AFXAPI _AfxDaoFillFieldInfo(DAOField* p, CDaoFieldInfo& info, DWORD dwOptions) {
  if (dwOptions & AFX_DAO_PRIMARY_INFO) {
    DAO_GET_STRING(p, Name, info.m_strName);
    short s = 0;
    DAO_CHECK(p->get_Type(&s));
    info.m_nType = s;
    LONG l = 0;
    DAO_CHECK(p->get_Size(&l));
    info.m_lSize = l;
    l = 0;
    DAO_CHECK(p->get_Attributes(&l));
    info.m_lAttributes = l;
  }
  if (dwOptions & AFX_DAO_SECONDARY_INFO) {
    short s = 0;
    DAO_CHECK(p->get_OrdinalPosition(&s));
    info.m_nOrdinalPosition = s;
    DAO_GET_BOOL(p, Required, info.m_bRequired);
    DAO_GET_BOOL(p, AllowZeroLength, info.m_bAllowZeroLength);
    LONG l = 0;
    DAO_CHECK(p->get_CollatingOrder(&l));
    info.m_lCollatingOrder = l;
    DAO_GET_STRING(p, ForeignName, info.m_strForeignName);
    DAO_GET_STRING(p, SourceField, info.m_strSourceField);
    DAO_GET_STRING(p, SourceTable, info.m_strSourceTable);
  }
  if ((dwOptions & AFX_DAO_ALL_INFO) == AFX_DAO_ALL_INFO) {
    DAO_GET_STRING(p, ValidationRule, info.m_strValidationRule);
    DAO_GET_STRING(p, ValidationText, info.m_strValidationText);
    COleVariant v;
    DAO_CHECK(p->get_DefaultValue(&v));
    info.m_strDefaultValue.Empty();
    if (v.vt != VT_EMPTY && v.vt != VT_NULL) {
      v.ChangeType(VT_BSTR);
      info.m_strDefaultValue = CString(v.bstrVal);
    }
  }
}

void AFXAPI _AfxDaoFillIndexInfo(DAOIndex* p, CDaoIndexInfo& info, DWORD dwOptions) {
  if (dwOptions & AFX_DAO_PRIMARY_INFO) {
    DAO_GET_STRING(p, Name, info.m_strName);
    // The index's fields: a collection of fields, given as a VARIANT.
    if (info.m_bCleanupFieldInfo) delete[] info.m_pFieldInfos;
    info.m_pFieldInfos = NULL;
    info.m_nFields = 0;
    COleVariant v;
    DAO_CHECK(p->get_Fields(&v));
    DAOIndexFields* pFields = NULL;
    if ((v.vt == VT_DISPATCH || v.vt == VT_UNKNOWN) && v.punkVal != NULL)
      v.punkVal->QueryInterface(IID_IDAOIndexFieldsW, (void**)&pFields);
    if (pFields != NULL) {
      short n = 0;
      pFields->get_Count(&n);
      if (n > 0) {
        info.m_pFieldInfos = new CDaoIndexFieldInfo[n];
        info.m_bCleanupFieldInfo = TRUE;
      }
      for (short i = 0; i < n; i++) {
        COleVariant vField;
        if (FAILED(pFields->get_Item(_AfxDaoItem(i), &vField))) continue;
        DAOField* pField = NULL;
        if ((vField.vt == VT_DISPATCH || vField.vt == VT_UNKNOWN) && vField.punkVal != NULL)
          vField.punkVal->QueryInterface(IID_IDAOFieldW, (void**)&pField);
        if (pField == NULL) continue;
        CDaoIndexFieldInfo& fi = info.m_pFieldInfos[info.m_nFields++];
        BSTR b = NULL;
        pField->get_Name(&b);
        fi.m_strName = _AfxDaoString(b);
        LONG l = 0;
        pField->get_Attributes(&l);
        fi.m_bDescending = (l & dbDescending) != 0;
        pField->Release();
      }
      pFields->Release();
    }
  }
  if (dwOptions & AFX_DAO_SECONDARY_INFO) {
    DAO_GET_BOOL(p, Primary, info.m_bPrimary);
    DAO_GET_BOOL(p, Unique, info.m_bUnique);
    DAO_GET_BOOL(p, Clustered, info.m_bClustered);
    DAO_GET_BOOL(p, IgnoreNulls, info.m_bIgnoreNulls);
    DAO_GET_BOOL(p, Required, info.m_bRequired);
    DAO_GET_BOOL(p, Foreign, info.m_bForeign);
  }
  if ((dwOptions & AFX_DAO_ALL_INFO) == AFX_DAO_ALL_INFO) {
    LONG l = 0;
    DAO_CHECK(p->get_DistinctCount(&l));
    info.m_lDistinctCount = l;
  }
}

void AFXAPI _AfxDaoFillQueryDefInfo(DAOQueryDef* p, CDaoQueryDefInfo& info, DWORD dwOptions) {
  if (dwOptions & AFX_DAO_PRIMARY_INFO) {
    DAO_GET_STRING(p, Name, info.m_strName);
    short s = 0;
    DAO_CHECK(p->get_Type(&s));
    info.m_nType = s;
  }
  if (dwOptions & AFX_DAO_SECONDARY_INFO) {
    COleVariant v;
    DAO_CHECK(p->get_DateCreated(&v));
    info.m_dateCreated = DateOf(v);
    v.Clear();
    DAO_CHECK(p->get_LastUpdated(&v));
    info.m_dateLastUpdated = DateOf(v);
    DAO_GET_BOOL(p, Updatable, info.m_bUpdatable);
    DAO_GET_BOOL(p, ReturnsRecords, info.m_bReturnsRecords);
  }
  if ((dwOptions & AFX_DAO_ALL_INFO) == AFX_DAO_ALL_INFO) {
    DAO_GET_STRING(p, SQL, info.m_strSQL);
    DAO_GET_STRING(p, Connect, info.m_strConnect);
    short s = 0;
    DAO_CHECK(p->get_ODBCTimeout(&s));
    info.m_nODBCTimeout = s;
  }
}

void AFXAPI _AfxDaoFillRelationInfo(DAORelation* p, CDaoRelationInfo& info, DWORD dwOptions) {
  if (dwOptions & AFX_DAO_PRIMARY_INFO) {
    DAO_GET_STRING(p, Name, info.m_strName);
    DAO_GET_STRING(p, Table, info.m_strTable);
    DAO_GET_STRING(p, ForeignTable, info.m_strForeignTable);
  }
  if (dwOptions & AFX_DAO_SECONDARY_INFO) {
    LONG l = 0;
    DAO_CHECK(p->get_Attributes(&l));
    info.m_lAttributes = l;
    if (info.m_bCleanupFieldInfo) delete[] info.m_pFieldInfos;
    info.m_pFieldInfos = NULL;
    info.m_nFields = 0;
    DAOFields* pFields = NULL;
    DAO_CHECK(p->get_Fields(&pFields));
    short n = 0;
    pFields->get_Count(&n);
    if (n > 0) {
      info.m_pFieldInfos = new CDaoRelationFieldInfo[n];
      info.m_bCleanupFieldInfo = TRUE;
    }
    for (short i = 0; i < n; i++) {
      DAOField* pField = NULL;
      if (FAILED(pFields->get_Item(_AfxDaoItem(i), &pField)) || pField == NULL) continue;
      CDaoRelationFieldInfo& fi = info.m_pFieldInfos[info.m_nFields++];
      BSTR b = NULL;
      pField->get_Name(&b);
      fi.m_strName = _AfxDaoString(b);
      b = NULL;
      pField->get_ForeignName(&b);
      fi.m_strForeignName = _AfxDaoString(b);
      pField->Release();
    }
    pFields->Release();
  }
}

void AFXAPI _AfxDaoFillParameterInfo(DAOParameter* p, CDaoParameterInfo& info, DWORD dwOptions) {
  if (dwOptions & AFX_DAO_PRIMARY_INFO) {
    DAO_GET_STRING(p, Name, info.m_strName);
    short s = 0;
    DAO_CHECK(p->get_Type(&s));
    info.m_nType = s;
  }
  if (dwOptions & AFX_DAO_SECONDARY_INFO) {
    info.m_varValue.Clear();
    DAO_CHECK(p->get_Value(&info.m_varValue));
    _AfxDaoFromEngine(info.m_varValue);
  }
}

static void ThrowNotOpen() { AfxThrowDaoException(AFX_DAO_ERROR_OBJECT_NOT_OPEN); }

// ---------------------------------------------------------------------
// CDaoWorkspace

IMPLEMENT_DYNAMIC(CDaoWorkspace, CObject)

CDaoWorkspace::CDaoWorkspace()
    : m_pDAOWorkspace(NULL), m_pDAOWorkspaces(NULL), m_pDAODatabases(NULL), m_nStatus(0),
      m_bOpen(FALSE), m_bNew(FALSE) {}

CDaoWorkspace::~CDaoWorkspace() {
  if (m_bOpen) {
    try {
      Close();
    } catch (CException* e) {
      e->Delete();
    }
  }
  if (m_pDAOWorkspaces != NULL) m_pDAOWorkspaces->Release();
}

void CDaoWorkspace::Create(LPCTSTR lpszName, LPCTSTR lpszUserName, LPCTSTR lpszPassword) {
  DAODBEngine* pEngine = AfxDaoGetEngine();
  DAO_CHECK(pEngine->CreateWorkspace(_AfxDaoBstr(lpszName), _AfxDaoBstr(lpszUserName),
                                     _AfxDaoBstr(lpszPassword), _AfxDaoLong(dbUseJet),
                                     &m_pDAOWorkspace));
  m_bOpen = TRUE;
  m_bNew = TRUE;
}

void CDaoWorkspace::InitWorkspacesCollection() {
  if (m_pDAOWorkspaces == NULL) DAO_CHECK(AfxDaoGetEngine()->get_Workspaces(&m_pDAOWorkspaces));
}

void CDaoWorkspace::InitDatabasesCollection() {
  if (!m_bOpen) ThrowNotOpen();
  if (m_pDAODatabases == NULL) DAO_CHECK(m_pDAOWorkspace->get_Databases(&m_pDAODatabases));
}

void CDaoWorkspace::Append() {
  if (!m_bOpen) ThrowNotOpen();
  InitWorkspacesCollection();
  DAO_CHECK(m_pDAOWorkspaces->Append(m_pDAOWorkspace));
  m_bNew = FALSE;
}

void CDaoWorkspace::Open(LPCTSTR lpszName) {
  InitWorkspacesCollection();
  COleVariant var = lpszName == NULL ? _AfxDaoItem(0) : _AfxDaoItem(lpszName);
  DAO_CHECK(m_pDAOWorkspaces->get_Item(var, &m_pDAOWorkspace));
  m_bOpen = TRUE;
  m_bNew = FALSE;
}

void CDaoWorkspace::Close() {
  // Its databases close first, and their recordsets with them.
  POSITION pos = m_mapDatabases.GetStartPosition();
  while (pos != NULL) {
    void* pKey;
    void* pValue;
    m_mapDatabases.GetNextAssoc(pos, pKey, pValue);
    ((CDaoDatabase*)pKey)->Close();
  }
  m_mapDatabases.RemoveAll();
  if (m_pDAODatabases != NULL) {
    m_pDAODatabases->Release();
    m_pDAODatabases = NULL;
  }
  if (m_pDAOWorkspace != NULL) {
    // A workspace this object made is closed; one it opened is the
    // engine's to keep.
    if (m_bNew) m_pDAOWorkspace->Close();
    m_pDAOWorkspace->Release();
    m_pDAOWorkspace = NULL;
  }
  m_bOpen = FALSE;
  m_bNew = FALSE;
}

CString CDaoWorkspace::GetName() {
  if (!m_bOpen) ThrowNotOpen();
  CString str;
  DAO_GET_STRING(m_pDAOWorkspace, Name, str);
  return str;
}

CString CDaoWorkspace::GetUserName() {
  if (!m_bOpen) ThrowNotOpen();
  CString str;
  DAO_GET_STRING(m_pDAOWorkspace, UserName, str);
  return str;
}

void CDaoWorkspace::SetIsolateODBCTrans(BOOL bIsolateODBCTrans) {
  if (!m_bOpen) ThrowNotOpen();
  DAO_CHECK(m_pDAOWorkspace->put_IsolateODBCTrans((short)(bIsolateODBCTrans ? -1 : 0)));
}

BOOL CDaoWorkspace::GetIsolateODBCTrans() {
  if (!m_bOpen) ThrowNotOpen();
  short s = 0;
  DAO_CHECK(m_pDAOWorkspace->get_IsolateODBCTrans(&s));
  return s != 0;
}

short CDaoWorkspace::GetWorkspaceCount() {
  InitWorkspacesCollection();
  short n = 0;
  DAO_CHECK(m_pDAOWorkspaces->get_Count(&n));
  return n;
}

void CDaoWorkspace::GetWorkspaceInfo(DAOWorkspace* pDAOWorkspace, CDaoWorkspaceInfo& wsinfo,
                                     DWORD dwInfoOptions) {
  try {
    _AfxDaoFillWorkspaceInfo(pDAOWorkspace, wsinfo, dwInfoOptions);
  } catch (CException*) {
    pDAOWorkspace->Release();
    throw;
  }
  pDAOWorkspace->Release();
}

void CDaoWorkspace::GetWorkspaceInfo(int nIndex, CDaoWorkspaceInfo& wsinfo, DWORD dwInfoOptions) {
  InitWorkspacesCollection();
  DAOWorkspace* p = NULL;
  DAO_CHECK(m_pDAOWorkspaces->get_Item(_AfxDaoItem(nIndex), &p));
  GetWorkspaceInfo(p, wsinfo, dwInfoOptions);
}

void CDaoWorkspace::GetWorkspaceInfo(LPCTSTR lpszName, CDaoWorkspaceInfo& wsinfo,
                                     DWORD dwInfoOptions) {
  InitWorkspacesCollection();
  DAOWorkspace* p = NULL;
  DAO_CHECK(m_pDAOWorkspaces->get_Item(_AfxDaoItem(lpszName), &p));
  GetWorkspaceInfo(p, wsinfo, dwInfoOptions);
}

short CDaoWorkspace::GetDatabaseCount() {
  InitDatabasesCollection();
  short n = 0;
  DAO_CHECK(m_pDAODatabases->get_Count(&n));
  return n;
}

static void DatabaseInfo(DAODatabases* pDatabases, const COleVariant& var, CDaoDatabaseInfo& info,
                         DWORD dwOptions) {
  DAODatabase* p = NULL;
  DAO_CHECK(pDatabases->get_Item(var, &p));
  try {
    _AfxDaoFillDatabaseInfo(p, info, dwOptions);
  } catch (CException*) {
    p->Release();
    throw;
  }
  p->Release();
}

void CDaoWorkspace::GetDatabaseInfo(int nIndex, CDaoDatabaseInfo& dbinfo, DWORD dwInfoOptions) {
  InitDatabasesCollection();
  DatabaseInfo(m_pDAODatabases, _AfxDaoItem(nIndex), dbinfo, dwInfoOptions);
}

void CDaoWorkspace::GetDatabaseInfo(LPCTSTR lpszName, CDaoDatabaseInfo& dbinfo,
                                    DWORD dwInfoOptions) {
  InitDatabasesCollection();
  DatabaseInfo(m_pDAODatabases, _AfxDaoItem(lpszName), dbinfo, dwInfoOptions);
}

void CDaoWorkspace::BeginTrans() {
  if (!m_bOpen) ThrowNotOpen();
  DAO_CHECK(m_pDAOWorkspace->BeginTrans());
}

void CDaoWorkspace::CommitTrans() {
  if (!m_bOpen) ThrowNotOpen();
  DAO_CHECK(m_pDAOWorkspace->CommitTrans(0));
}

void CDaoWorkspace::Rollback() {
  if (!m_bOpen) ThrowNotOpen();
  DAO_CHECK(m_pDAOWorkspace->Rollback());
}

CString PASCAL CDaoWorkspace::GetVersion() {
  CString str;
  DAO_GET_STRING(AfxDaoGetEngine(), Version, str);
  return str;
}

CString PASCAL CDaoWorkspace::GetIniPath() {
  CString str;
  DAO_GET_STRING(AfxDaoGetEngine(), IniPath, str);
  return str;
}

void PASCAL CDaoWorkspace::SetIniPath(LPCTSTR lpszRegistrySubkey) {
  DAO_CHECK(AfxDaoGetEngine()->put_IniPath(_AfxDaoBstr(lpszRegistrySubkey)));
}

void PASCAL CDaoWorkspace::SetDefaultUser(LPCTSTR lpszDefaultUser) {
  DAO_CHECK(AfxDaoGetEngine()->put_DefaultUser(_AfxDaoBstr(lpszDefaultUser)));
}

void PASCAL CDaoWorkspace::SetDefaultPassword(LPCTSTR lpszPassword) {
  DAO_CHECK(AfxDaoGetEngine()->put_DefaultPassword(_AfxDaoBstr(lpszPassword)));
}

short PASCAL CDaoWorkspace::GetLoginTimeout() {
  short s = 0;
  DAO_CHECK(AfxDaoGetEngine()->get_LoginTimeout(&s));
  return s;
}

void PASCAL CDaoWorkspace::SetLoginTimeout(short nSeconds) {
  DAO_CHECK(AfxDaoGetEngine()->put_LoginTimeout(nSeconds));
}

void PASCAL CDaoWorkspace::CompactDatabase(LPCTSTR lpszSrcName, LPCTSTR lpszDestName,
                                           LPCTSTR lpszLocale, int nOptions) {
  CompactDatabase(lpszSrcName, lpszDestName, lpszLocale, nOptions, NULL);
}

void PASCAL CDaoWorkspace::CompactDatabase(LPCTSTR lpszSrcName, LPCTSTR lpszDestName,
                                           LPCTSTR lpszLocale, int nOptions,
                                           LPCTSTR lpszPassword) {
  COleVariant varLocale(CString(lpszLocale != NULL ? lpszLocale : _T("")), VT_BSTR);
  COleVariant varOptions((long)nOptions);
  // The source's password goes where its locale would, as ";pwd=...".
  COleVariant varSrc;
  if (lpszPassword != NULL) varSrc = COleVariant(CString(_T(";pwd=")) + lpszPassword, VT_BSTR);
  DAO_CHECK(AfxDaoGetEngine()->CompactDatabase(
      _AfxDaoBstr(lpszSrcName), _AfxDaoBstr(lpszDestName), varLocale, varOptions,
      lpszPassword != NULL ? (VARIANT)varSrc : _AfxDaoOptional()));
}

void PASCAL CDaoWorkspace::RepairDatabase(LPCTSTR lpszName) {
  DAO_CHECK(AfxDaoGetEngine()->RepairDatabase(_AfxDaoBstr(lpszName)));
}

void PASCAL CDaoWorkspace::Idle(int nAction) {
  DAO_CHECK(AfxDaoGetEngine()->Idle(_AfxDaoLong(nAction)));
}

// ---------------------------------------------------------------------
// CDaoDatabase

IMPLEMENT_DYNAMIC(CDaoDatabase, CObject)

CDaoDatabase::CDaoDatabase(CDaoWorkspace* pWorkspace)
    : m_pWorkspace(pWorkspace), m_pDAODatabase(NULL), m_pDAOTableDefs(NULL),
      m_pDAOQueryDefs(NULL), m_pDAORelations(NULL), m_nStatus(0), m_bOpen(FALSE),
      m_bOwnWorkspace(FALSE) {}

CDaoDatabase::~CDaoDatabase() {
  if (m_bOpen) {
    try {
      Close();
    } catch (CException* e) {
      e->Delete();
    }
  }
  if (m_bOwnWorkspace) delete m_pWorkspace;
}

// Without a workspace of its own, a database opens in the default one.
void CDaoDatabase::InitWorkspace() {
  if (m_pWorkspace == NULL) {
    m_pWorkspace = new CDaoWorkspace;
    m_bOwnWorkspace = TRUE;
  }
  if (!m_pWorkspace->IsOpen()) {
    if (!m_bOwnWorkspace) ThrowNotOpen();
    m_pWorkspace->Open();
  }
}

void CDaoDatabase::InitCollections() {
  if (!m_bOpen) ThrowNotOpen();
  if (m_pDAOTableDefs == NULL) DAO_CHECK(m_pDAODatabase->get_TableDefs(&m_pDAOTableDefs));
  if (m_pDAOQueryDefs == NULL) DAO_CHECK(m_pDAODatabase->get_QueryDefs(&m_pDAOQueryDefs));
  if (m_pDAORelations == NULL) DAO_CHECK(m_pDAODatabase->get_Relations(&m_pDAORelations));
}

void CDaoDatabase::Create(LPCTSTR lpszName, LPCTSTR lpszLocale, int dwOptions) {
  InitWorkspace();
  DAO_CHECK(m_pWorkspace->m_pDAOWorkspace->CreateDatabase(
      _AfxDaoBstr(lpszName), _AfxDaoBstr(lpszLocale),
      dwOptions != 0 ? (VARIANT)_AfxDaoLong(dwOptions) : _AfxDaoOptional(), &m_pDAODatabase));
  m_bOpen = TRUE;
  m_pWorkspace->m_mapDatabases.SetAt(this, this);
}

void CDaoDatabase::Open(LPCTSTR lpszName, BOOL bExclusive, BOOL bReadOnly, LPCTSTR lpszConnect) {
  InitWorkspace();
  COleVariant varConnect(CString(lpszConnect != NULL ? lpszConnect : _T("")), VT_BSTR);
  DAO_CHECK(m_pWorkspace->m_pDAOWorkspace->OpenDatabase(
      _AfxDaoBstr(lpszName), _AfxDaoBool(bExclusive), _AfxDaoBool(bReadOnly), varConnect,
      &m_pDAODatabase));
  m_bOpen = TRUE;
  m_pWorkspace->m_mapDatabases.SetAt(this, this);
}

static void CloseAll(CMapPtrToPtr& map, void (*pfnClose)(void*)) {
  while (!map.IsEmpty()) {
    POSITION pos = map.GetStartPosition();
    void* pKey;
    void* pValue;
    map.GetNextAssoc(pos, pKey, pValue);
    map.RemoveKey(pKey);
    pfnClose(pKey);
  }
}

void CDaoDatabase::Close() {
  CloseAll(m_mapRecordsets, [](void* p) { ((CDaoRecordset*)p)->Close(); });
  CloseAll(m_mapQueryDefs, [](void* p) { ((CDaoQueryDef*)p)->Close(); });
  CloseAll(m_mapTableDefs, [](void* p) { ((CDaoTableDef*)p)->Close(); });
  if (m_pDAORelations != NULL) m_pDAORelations->Release();
  if (m_pDAOQueryDefs != NULL) m_pDAOQueryDefs->Release();
  if (m_pDAOTableDefs != NULL) m_pDAOTableDefs->Release();
  m_pDAORelations = NULL;
  m_pDAOQueryDefs = NULL;
  m_pDAOTableDefs = NULL;
  if (m_pDAODatabase != NULL) {
    m_pDAODatabase->Close();
    m_pDAODatabase->Release();
    m_pDAODatabase = NULL;
  }
  if (m_bOpen && m_pWorkspace != NULL) m_pWorkspace->m_mapDatabases.RemoveKey(this);
  m_bOpen = FALSE;
  if (m_bOwnWorkspace && m_pWorkspace != NULL && m_pWorkspace->IsOpen()) m_pWorkspace->Close();
}

BOOL CDaoDatabase::CanUpdate() {
  if (!m_bOpen) ThrowNotOpen();
  BOOL b;
  DAO_GET_BOOL(m_pDAODatabase, Updatable, b);
  return b;
}

BOOL CDaoDatabase::CanTransact() {
  if (!m_bOpen) ThrowNotOpen();
  BOOL b;
  DAO_GET_BOOL(m_pDAODatabase, Transactions, b);
  return b;
}

CString CDaoDatabase::GetName() {
  if (!m_bOpen) ThrowNotOpen();
  CString str;
  DAO_GET_STRING(m_pDAODatabase, Name, str);
  return str;
}

CString CDaoDatabase::GetConnect() {
  if (!m_bOpen) ThrowNotOpen();
  CString str;
  DAO_GET_STRING(m_pDAODatabase, Connect, str);
  return str;
}

CString CDaoDatabase::GetVersion() {
  if (!m_bOpen) ThrowNotOpen();
  CString str;
  DAO_GET_STRING(m_pDAODatabase, Version, str);
  return str;
}

short CDaoDatabase::GetQueryTimeout() {
  if (!m_bOpen) ThrowNotOpen();
  short s = 0;
  DAO_CHECK(m_pDAODatabase->get_QueryTimeout(&s));
  return s;
}

void CDaoDatabase::SetQueryTimeout(short nSeconds) {
  if (!m_bOpen) ThrowNotOpen();
  DAO_CHECK(m_pDAODatabase->put_QueryTimeout(nSeconds));
}

long CDaoDatabase::GetRecordsAffected() {
  if (!m_bOpen) ThrowNotOpen();
  LONG l = 0;
  DAO_CHECK(m_pDAODatabase->get_RecordsAffected(&l));
  return l;
}

void CDaoDatabase::Execute(LPCTSTR lpszSQL, int nOptions) {
  if (!m_bOpen) ThrowNotOpen();
  DAO_CHECK(m_pDAODatabase->Execute(_AfxDaoBstr(lpszSQL), _AfxDaoLong(nOptions)));
}

void CDaoDatabase::DeleteTableDef(LPCTSTR lpszName) {
  InitCollections();
  DAO_CHECK(m_pDAOTableDefs->Delete(_AfxDaoBstr(lpszName)));
}

void CDaoDatabase::DeleteQueryDef(LPCTSTR lpszName) {
  InitCollections();
  DAO_CHECK(m_pDAOQueryDefs->Delete(_AfxDaoBstr(lpszName)));
}

void CDaoDatabase::CreateRelation(LPCTSTR lpszName, LPCTSTR lpszTable, LPCTSTR lpszForeignTable,
                                  long lAttributes, LPCTSTR lpszField,
                                  LPCTSTR lpszForeignField) {
  CDaoRelationInfo info;
  CDaoRelationFieldInfo field;
  field.m_strName = lpszField;
  field.m_strForeignName = lpszForeignField;
  info.m_strName = lpszName;
  info.m_strTable = lpszTable;
  info.m_strForeignTable = lpszForeignTable;
  info.m_lAttributes = lAttributes;
  info.m_pFieldInfos = &field;
  info.m_nFields = 1;
  CreateRelation(info);
}

void CDaoDatabase::CreateRelation(CDaoRelationInfo& relinfo) {
  InitCollections();
  DAORelation* pRel = NULL;
  DAO_CHECK(m_pDAODatabase->CreateRelation(
      _AfxDaoItem(relinfo.m_strName), _AfxDaoItem(relinfo.m_strTable),
      _AfxDaoItem(relinfo.m_strForeignTable), _AfxDaoLong(relinfo.m_lAttributes), &pRel));
  DAOFields* pFields = NULL;
  try {
    DAO_CHECK(pRel->get_Fields(&pFields));
    for (int i = 0; i < relinfo.m_nFields; i++) {
      DAOField* pField = NULL;
      DAO_CHECK(pRel->CreateField(_AfxDaoItem(relinfo.m_pFieldInfos[i].m_strName),
                                  _AfxDaoOptional(), _AfxDaoOptional(), &pField));
      HRESULT hr = pField->put_ForeignName(_AfxDaoBstr(relinfo.m_pFieldInfos[i].m_strForeignName));
      if (SUCCEEDED(hr)) hr = pFields->Append(pField);
      pField->Release();
      DAO_CHECK(hr);
    }
    DAO_CHECK(m_pDAORelations->Append(pRel));
  } catch (CException*) {
    if (pFields != NULL) pFields->Release();
    pRel->Release();
    throw;
  }
  pFields->Release();
  pRel->Release();
}

void CDaoDatabase::DeleteRelation(LPCTSTR lpszName) {
  InitCollections();
  DAO_CHECK(m_pDAORelations->Delete(_AfxDaoBstr(lpszName)));
}

short CDaoDatabase::GetTableDefCount() {
  InitCollections();
  short n = 0;
  DAO_CHECK(m_pDAOTableDefs->get_Count(&n));
  return n;
}

// Reads one item of a collection into its information structure.
template <class COLL, class ITEM, class INFO>
static void ItemInfo(COLL* pColl, const COleVariant& var, INFO& info, DWORD dwOptions,
                     void(AFXAPI* pfnFill)(ITEM*, INFO&, DWORD)) {
  ITEM* p = NULL;
  DAO_CHECK(pColl->get_Item(var, &p));
  try {
    pfnFill(p, info, dwOptions);
  } catch (CException*) {
    p->Release();
    throw;
  }
  p->Release();
}

void CDaoDatabase::GetTableDefInfo(int nIndex, CDaoTableDefInfo& info, DWORD dwOptions) {
  InitCollections();
  ItemInfo(m_pDAOTableDefs, _AfxDaoItem(nIndex), info, dwOptions, _AfxDaoFillTableDefInfo);
}

void CDaoDatabase::GetTableDefInfo(LPCTSTR lpszName, CDaoTableDefInfo& info, DWORD dwOptions) {
  InitCollections();
  ItemInfo(m_pDAOTableDefs, _AfxDaoItem(lpszName), info, dwOptions, _AfxDaoFillTableDefInfo);
}

short CDaoDatabase::GetQueryDefCount() {
  InitCollections();
  short n = 0;
  DAO_CHECK(m_pDAOQueryDefs->get_Count(&n));
  return n;
}

void CDaoDatabase::GetQueryDefInfo(int nIndex, CDaoQueryDefInfo& info, DWORD dwOptions) {
  InitCollections();
  ItemInfo(m_pDAOQueryDefs, _AfxDaoItem(nIndex), info, dwOptions, _AfxDaoFillQueryDefInfo);
}

void CDaoDatabase::GetQueryDefInfo(LPCTSTR lpszName, CDaoQueryDefInfo& info, DWORD dwOptions) {
  InitCollections();
  ItemInfo(m_pDAOQueryDefs, _AfxDaoItem(lpszName), info, dwOptions, _AfxDaoFillQueryDefInfo);
}

short CDaoDatabase::GetRelationCount() {
  InitCollections();
  short n = 0;
  DAO_CHECK(m_pDAORelations->get_Count(&n));
  return n;
}

void CDaoDatabase::GetRelationInfo(int nIndex, CDaoRelationInfo& info, DWORD dwOptions) {
  InitCollections();
  ItemInfo(m_pDAORelations, _AfxDaoItem(nIndex), info, dwOptions, _AfxDaoFillRelationInfo);
}

void CDaoDatabase::GetRelationInfo(LPCTSTR lpszName, CDaoRelationInfo& info, DWORD dwOptions) {
  InitCollections();
  ItemInfo(m_pDAORelations, _AfxDaoItem(lpszName), info, dwOptions, _AfxDaoFillRelationInfo);
}

// ---------------------------------------------------------------------
// CDaoTableDef

IMPLEMENT_DYNAMIC(CDaoTableDef, CObject)

CDaoTableDef::CDaoTableDef(CDaoDatabase* pDatabase)
    : m_pDatabase(pDatabase), m_pDAOTableDef(NULL), m_pDAOFields(NULL), m_pDAOIndexes(NULL),
      m_bOpen(FALSE), m_bNew(FALSE) {}

CDaoTableDef::~CDaoTableDef() {
  if (m_bOpen) {
    try {
      Close();
    } catch (CException* e) {
      e->Delete();
    }
  }
}

void CDaoTableDef::Create(LPCTSTR lpszName, long lAttributes, LPCTSTR lpszSrcTable,
                          LPCTSTR lpszConnect) {
  m_pDatabase->InitCollections();
  DAO_CHECK(m_pDatabase->m_pDAODatabase->CreateTableDef(
      _AfxDaoItem(lpszName), _AfxDaoLong(lAttributes),
      lpszSrcTable != NULL ? (VARIANT)_AfxDaoItem(lpszSrcTable) : _AfxDaoOptional(),
      lpszConnect != NULL ? (VARIANT)_AfxDaoItem(lpszConnect) : _AfxDaoOptional(),
      &m_pDAOTableDef));
  m_bOpen = TRUE;
  m_bNew = TRUE;
  m_pDatabase->m_mapTableDefs.SetAt(this, this);
}

void CDaoTableDef::Append() {
  if (!m_bOpen) ThrowNotOpen();
  DAO_CHECK(m_pDatabase->m_pDAOTableDefs->Append(m_pDAOTableDef));
  m_bNew = FALSE;
}

void CDaoTableDef::Open(LPCTSTR lpszName) {
  m_pDatabase->InitCollections();
  DAO_CHECK(m_pDatabase->m_pDAOTableDefs->get_Item(_AfxDaoItem(lpszName), &m_pDAOTableDef));
  m_bOpen = TRUE;
  m_bNew = FALSE;
  m_pDatabase->m_mapTableDefs.SetAt(this, this);
}

void CDaoTableDef::Close() {
  if (m_pDAOIndexes != NULL) m_pDAOIndexes->Release();
  if (m_pDAOFields != NULL) m_pDAOFields->Release();
  if (m_pDAOTableDef != NULL) m_pDAOTableDef->Release();
  m_pDAOIndexes = NULL;
  m_pDAOFields = NULL;
  m_pDAOTableDef = NULL;
  if (m_bOpen && m_pDatabase != NULL) m_pDatabase->m_mapTableDefs.RemoveKey(this);
  m_bOpen = FALSE;
  m_bNew = FALSE;
}

void CDaoTableDef::InitFieldsCollection() {
  if (!m_bOpen) ThrowNotOpen();
  if (m_pDAOFields == NULL) DAO_CHECK(m_pDAOTableDef->get_Fields(&m_pDAOFields));
}

void CDaoTableDef::InitIndexesCollection() {
  if (!m_bOpen) ThrowNotOpen();
  if (m_pDAOIndexes == NULL) DAO_CHECK(m_pDAOTableDef->get_Indexes(&m_pDAOIndexes));
}

#define TDF_STRING_PROP(Get, Set, Prop)              \
  CString CDaoTableDef::Get() {                      \
    if (!m_bOpen) ThrowNotOpen();                    \
    CString str;                                     \
    DAO_GET_STRING(m_pDAOTableDef, Prop, str);       \
    return str;                                      \
  }                                                  \
  void CDaoTableDef::Set(LPCTSTR lpsz) {             \
    if (!m_bOpen) ThrowNotOpen();                    \
    DAO_CHECK(m_pDAOTableDef->put_##Prop(_AfxDaoBstr(lpsz))); \
  }
TDF_STRING_PROP(GetName, SetName, Name)
TDF_STRING_PROP(GetConnect, SetConnect, Connect)
TDF_STRING_PROP(GetSourceTableName, SetSourceTableName, SourceTableName)
TDF_STRING_PROP(GetValidationRule, SetValidationRule, ValidationRule)
TDF_STRING_PROP(GetValidationText, SetValidationText, ValidationText)
#undef TDF_STRING_PROP

BOOL CDaoTableDef::CanUpdate() {
  if (!m_bOpen) ThrowNotOpen();
  BOOL b;
  DAO_GET_BOOL(m_pDAOTableDef, Updatable, b);
  return b;
}

long CDaoTableDef::GetAttributes() {
  if (!m_bOpen) ThrowNotOpen();
  LONG l = 0;
  DAO_CHECK(m_pDAOTableDef->get_Attributes(&l));
  return l;
}

void CDaoTableDef::SetAttributes(long lAttributes) {
  if (!m_bOpen) ThrowNotOpen();
  DAO_CHECK(m_pDAOTableDef->put_Attributes(lAttributes));
}

COleDateTime CDaoTableDef::GetDateCreated() {
  if (!m_bOpen) ThrowNotOpen();
  COleVariant v;
  DAO_CHECK(m_pDAOTableDef->get_DateCreated(&v));
  return DateOf(v);
}

COleDateTime CDaoTableDef::GetDateLastUpdated() {
  if (!m_bOpen) ThrowNotOpen();
  COleVariant v;
  DAO_CHECK(m_pDAOTableDef->get_LastUpdated(&v));
  return DateOf(v);
}

long CDaoTableDef::GetRecordCount() {
  if (!m_bOpen) ThrowNotOpen();
  LONG l = 0;
  DAO_CHECK(m_pDAOTableDef->get_RecordCount(&l));
  return l;
}

void CDaoTableDef::RefreshLink() {
  if (!m_bOpen) ThrowNotOpen();
  DAO_CHECK(m_pDAOTableDef->RefreshLink());
}

void CDaoTableDef::CreateField(LPCTSTR lpszName, short nType, long lSize, long lAttributes) {
  CDaoFieldInfo info;
  info.m_strName = lpszName;
  info.m_nType = nType;
  info.m_lSize = lSize;
  info.m_lAttributes = lAttributes;
  info.m_nOrdinalPosition = 0;
  info.m_bRequired = FALSE;
  info.m_bAllowZeroLength = FALSE;
  info.m_lCollatingOrder = 0;
  CreateField(info);
}

void CDaoTableDef::CreateField(CDaoFieldInfo& info) {
  InitFieldsCollection();
  DAOField* pField = NULL;
  DAO_CHECK(m_pDAOTableDef->CreateField(_AfxDaoItem(info.m_strName), COleVariant(info.m_nType),
                                        _AfxDaoLong(info.m_lSize), &pField));
  HRESULT hr = S_OK;
  if (info.m_lAttributes != 0) hr = pField->put_Attributes(info.m_lAttributes);
  if (SUCCEEDED(hr) && info.m_nOrdinalPosition != 0)
    hr = pField->put_OrdinalPosition(info.m_nOrdinalPosition);
  if (SUCCEEDED(hr) && info.m_bRequired) hr = pField->put_Required(VARIANT_TRUE);
  if (SUCCEEDED(hr) && info.m_bAllowZeroLength &&
      (info.m_nType == dbText || info.m_nType == dbMemo))
    hr = pField->put_AllowZeroLength(VARIANT_TRUE);
  if (SUCCEEDED(hr) && !info.m_strValidationRule.IsEmpty())
    hr = pField->put_ValidationRule(_AfxDaoBstr(info.m_strValidationRule));
  if (SUCCEEDED(hr) && !info.m_strValidationText.IsEmpty())
    hr = pField->put_ValidationText(_AfxDaoBstr(info.m_strValidationText));
  if (SUCCEEDED(hr) && !info.m_strDefaultValue.IsEmpty())
    hr = pField->put_DefaultValue(_AfxDaoItem(info.m_strDefaultValue));
  if (SUCCEEDED(hr)) hr = m_pDAOFields->Append(pField);
  pField->Release();
  DAO_CHECK(hr);
}

void CDaoTableDef::DeleteField(LPCTSTR lpszName) {
  InitFieldsCollection();
  DAO_CHECK(m_pDAOFields->Delete(_AfxDaoBstr(lpszName)));
}

void CDaoTableDef::DeleteField(int nIndex) {
  CDaoFieldInfo info;
  GetFieldInfo(nIndex, info, AFX_DAO_PRIMARY_INFO);
  DeleteField(info.m_strName);
}

void CDaoTableDef::CreateIndex(CDaoIndexInfo& info) {
  InitIndexesCollection();
  DAOIndex* pIndex = NULL;
  DAO_CHECK(m_pDAOTableDef->CreateIndex(_AfxDaoItem(info.m_strName), &pIndex));
  DAOIndexFields* pFields = NULL;
  try {
    DAO_CHECK(pIndex->put_Primary(info.m_bPrimary ? VARIANT_TRUE : VARIANT_FALSE));
    DAO_CHECK(pIndex->put_Unique(info.m_bUnique ? VARIANT_TRUE : VARIANT_FALSE));
    DAO_CHECK(pIndex->put_Clustered(info.m_bClustered ? VARIANT_TRUE : VARIANT_FALSE));
    DAO_CHECK(pIndex->put_IgnoreNulls(info.m_bIgnoreNulls ? VARIANT_TRUE : VARIANT_FALSE));
    DAO_CHECK(pIndex->put_Required(info.m_bRequired ? VARIANT_TRUE : VARIANT_FALSE));
    COleVariant v;
    DAO_CHECK(pIndex->get_Fields(&v));
    if ((v.vt == VT_DISPATCH || v.vt == VT_UNKNOWN) && v.punkVal != NULL)
      DAO_CHECK(v.punkVal->QueryInterface(IID_IDAOIndexFieldsW, (void**)&pFields));
    if (pFields == NULL) AfxThrowDaoException(NO_AFX_DAO_ERROR, E_NOINTERFACE);
    for (int i = 0; i < info.m_nFields; i++) {
      DAOField* pField = NULL;
      DAO_CHECK(pIndex->CreateField(_AfxDaoItem(info.m_pFieldInfos[i].m_strName),
                                    _AfxDaoOptional(), _AfxDaoOptional(), &pField));
      HRESULT hr = S_OK;
      if (info.m_pFieldInfos[i].m_bDescending) hr = pField->put_Attributes(dbDescending);
      if (SUCCEEDED(hr)) hr = pFields->Append(pField);
      pField->Release();
      DAO_CHECK(hr);
    }
    DAO_CHECK(m_pDAOIndexes->Append(pIndex));
  } catch (CException*) {
    if (pFields != NULL) pFields->Release();
    pIndex->Release();
    throw;
  }
  pFields->Release();
  pIndex->Release();
}

void CDaoTableDef::DeleteIndex(LPCTSTR lpszName) {
  InitIndexesCollection();
  DAO_CHECK(m_pDAOIndexes->Delete(_AfxDaoBstr(lpszName)));
}

void CDaoTableDef::DeleteIndex(int nIndex) {
  CDaoIndexInfo info;
  GetIndexInfo(nIndex, info, AFX_DAO_PRIMARY_INFO);
  DeleteIndex(info.m_strName);
}

short CDaoTableDef::GetFieldCount() {
  InitFieldsCollection();
  short n = 0;
  DAO_CHECK(m_pDAOFields->get_Count(&n));
  return n;
}

void CDaoTableDef::GetFieldInfo(int nIndex, CDaoFieldInfo& info, DWORD dwOptions) {
  InitFieldsCollection();
  ItemInfo(m_pDAOFields, _AfxDaoItem(nIndex), info, dwOptions, _AfxDaoFillFieldInfo);
}

void CDaoTableDef::GetFieldInfo(LPCTSTR lpszName, CDaoFieldInfo& info, DWORD dwOptions) {
  InitFieldsCollection();
  ItemInfo(m_pDAOFields, _AfxDaoItem(lpszName), info, dwOptions, _AfxDaoFillFieldInfo);
}

short CDaoTableDef::GetIndexCount() {
  InitIndexesCollection();
  short n = 0;
  DAO_CHECK(m_pDAOIndexes->get_Count(&n));
  return n;
}

void CDaoTableDef::GetIndexInfo(int nIndex, CDaoIndexInfo& info, DWORD dwOptions) {
  InitIndexesCollection();
  ItemInfo(m_pDAOIndexes, _AfxDaoItem(nIndex), info, dwOptions, _AfxDaoFillIndexInfo);
}

void CDaoTableDef::GetIndexInfo(LPCTSTR lpszName, CDaoIndexInfo& info, DWORD dwOptions) {
  InitIndexesCollection();
  ItemInfo(m_pDAOIndexes, _AfxDaoItem(lpszName), info, dwOptions, _AfxDaoFillIndexInfo);
}

// ---------------------------------------------------------------------
// CDaoQueryDef

IMPLEMENT_DYNAMIC(CDaoQueryDef, CObject)

CDaoQueryDef::CDaoQueryDef(CDaoDatabase* pDatabase)
    : m_pDatabase(pDatabase), m_pDAOQueryDef(NULL), m_pDAOParameters(NULL), m_pDAOFields(NULL),
      m_bOpen(FALSE), m_bNew(FALSE) {}

CDaoQueryDef::~CDaoQueryDef() {
  if (m_bOpen) {
    try {
      Close();
    } catch (CException* e) {
      e->Delete();
    }
  }
}

// The querydef is made without a name, which the engine would take as a
// request to save it at once, and named after.
void CDaoQueryDef::Create(LPCTSTR lpszName, LPCTSTR lpszSQL) {
  m_pDatabase->InitCollections();
  DAO_CHECK(m_pDatabase->m_pDAODatabase->CreateQueryDef(
      _AfxDaoItem(_T("")),
      lpszSQL != NULL ? (VARIANT)_AfxDaoItem(lpszSQL) : _AfxDaoOptional(), &m_pDAOQueryDef));
  m_bOpen = TRUE;
  m_bNew = TRUE;
  m_pDatabase->m_mapQueryDefs.SetAt(this, this);
  if (lpszName != NULL && *lpszName != 0) DAO_CHECK(m_pDAOQueryDef->put_Name(_AfxDaoBstr(lpszName)));
}

void CDaoQueryDef::Append() {
  if (!m_bOpen) ThrowNotOpen();
  DAO_CHECK(m_pDatabase->m_pDAOQueryDefs->Append(m_pDAOQueryDef));
  m_bNew = FALSE;
}

void CDaoQueryDef::Open(LPCTSTR lpszName) {
  if (lpszName == NULL) {
    Create(NULL, NULL);
    return;
  }
  m_pDatabase->InitCollections();
  DAO_CHECK(m_pDatabase->m_pDAOQueryDefs->get_Item(_AfxDaoItem(lpszName), &m_pDAOQueryDef));
  m_bOpen = TRUE;
  m_bNew = FALSE;
  m_pDatabase->m_mapQueryDefs.SetAt(this, this);
}

void CDaoQueryDef::Close() {
  if (m_pDAOFields != NULL) m_pDAOFields->Release();
  if (m_pDAOParameters != NULL) m_pDAOParameters->Release();
  m_pDAOFields = NULL;
  m_pDAOParameters = NULL;
  if (m_pDAOQueryDef != NULL) {
    m_pDAOQueryDef->Close();
    m_pDAOQueryDef->Release();
    m_pDAOQueryDef = NULL;
  }
  if (m_bOpen && m_pDatabase != NULL) m_pDatabase->m_mapQueryDefs.RemoveKey(this);
  m_bOpen = FALSE;
  m_bNew = FALSE;
}

void CDaoQueryDef::InitParametersCollection() {
  if (!m_bOpen) ThrowNotOpen();
  if (m_pDAOParameters == NULL) DAO_CHECK(m_pDAOQueryDef->get_Parameters(&m_pDAOParameters));
}

void CDaoQueryDef::InitFieldsCollection() {
  if (!m_bOpen) ThrowNotOpen();
  if (m_pDAOFields == NULL) DAO_CHECK(m_pDAOQueryDef->get_Fields(&m_pDAOFields));
}

#define QDF_STRING_PROP(Get, Set, Prop)                       \
  CString CDaoQueryDef::Get() {                               \
    if (!m_bOpen) ThrowNotOpen();                             \
    CString str;                                              \
    DAO_GET_STRING(m_pDAOQueryDef, Prop, str);                \
    return str;                                               \
  }                                                           \
  void CDaoQueryDef::Set(LPCTSTR lpsz) {                      \
    if (!m_bOpen) ThrowNotOpen();                             \
    DAO_CHECK(m_pDAOQueryDef->put_##Prop(_AfxDaoBstr(lpsz))); \
  }
QDF_STRING_PROP(GetName, SetName, Name)
QDF_STRING_PROP(GetSQL, SetSQL, SQL)
QDF_STRING_PROP(GetConnect, SetConnect, Connect)
#undef QDF_STRING_PROP

BOOL CDaoQueryDef::CanUpdate() {
  if (!m_bOpen) ThrowNotOpen();
  BOOL b;
  DAO_GET_BOOL(m_pDAOQueryDef, Updatable, b);
  return b;
}

short CDaoQueryDef::GetType() {
  if (!m_bOpen) ThrowNotOpen();
  short s = 0;
  DAO_CHECK(m_pDAOQueryDef->get_Type(&s));
  return s;
}

COleDateTime CDaoQueryDef::GetDateCreated() {
  if (!m_bOpen) ThrowNotOpen();
  COleVariant v;
  DAO_CHECK(m_pDAOQueryDef->get_DateCreated(&v));
  return DateOf(v);
}

COleDateTime CDaoQueryDef::GetDateLastUpdated() {
  if (!m_bOpen) ThrowNotOpen();
  COleVariant v;
  DAO_CHECK(m_pDAOQueryDef->get_LastUpdated(&v));
  return DateOf(v);
}

short CDaoQueryDef::GetODBCTimeout() {
  if (!m_bOpen) ThrowNotOpen();
  short s = 0;
  DAO_CHECK(m_pDAOQueryDef->get_ODBCTimeout(&s));
  return s;
}

void CDaoQueryDef::SetODBCTimeout(short nODBCTimeout) {
  if (!m_bOpen) ThrowNotOpen();
  DAO_CHECK(m_pDAOQueryDef->put_ODBCTimeout(nODBCTimeout));
}

BOOL CDaoQueryDef::GetReturnsRecords() {
  if (!m_bOpen) ThrowNotOpen();
  BOOL b;
  DAO_GET_BOOL(m_pDAOQueryDef, ReturnsRecords, b);
  return b;
}

void CDaoQueryDef::SetReturnsRecords(BOOL bReturnsRecords) {
  if (!m_bOpen) ThrowNotOpen();
  DAO_CHECK(m_pDAOQueryDef->put_ReturnsRecords(bReturnsRecords ? VARIANT_TRUE : VARIANT_FALSE));
}

long CDaoQueryDef::GetRecordsAffected() {
  if (!m_bOpen) ThrowNotOpen();
  LONG l = 0;
  DAO_CHECK(m_pDAOQueryDef->get_RecordsAffected(&l));
  return l;
}

void CDaoQueryDef::Execute(int nOptions) {
  if (!m_bOpen) ThrowNotOpen();
  DAO_CHECK(m_pDAOQueryDef->Execute(_AfxDaoLong(nOptions)));
}

static COleVariant ParamValue(DAOParameters* pParams, const COleVariant& var) {
  DAOParameter* p = NULL;
  DAO_CHECK(pParams->get_Item(var, &p));
  COleVariant v;
  HRESULT hr = p->get_Value(&v);
  p->Release();
  DAO_CHECK(hr);
  _AfxDaoFromEngine(v);
  return v;
}

static void SetParamValue(DAOParameters* pParams, const COleVariant& var, const VARIANT& value) {
  DAOParameter* p = NULL;
  DAO_CHECK(pParams->get_Item(var, &p));
  HRESULT hr = p->put_Value(value);
  p->Release();
  DAO_CHECK(hr);
}

COleVariant CDaoQueryDef::GetParameterValue(LPCTSTR lpszName) {
  InitParametersCollection();
  return ParamValue(m_pDAOParameters, _AfxDaoItem(_AfxDaoStripBrackets(lpszName)));
}

COleVariant CDaoQueryDef::GetParameterValue(int nIndex) {
  InitParametersCollection();
  return ParamValue(m_pDAOParameters, _AfxDaoItem(nIndex));
}

void CDaoQueryDef::SetParameterValue(LPCTSTR lpszName, const COleVariant& varValue) {
  InitParametersCollection();
  COleVariant v(varValue);
  _AfxDaoToEngine(v);
  SetParamValue(m_pDAOParameters, _AfxDaoItem(_AfxDaoStripBrackets(lpszName)), v);
}

void CDaoQueryDef::SetParameterValue(int nIndex, const COleVariant& varValue) {
  InitParametersCollection();
  COleVariant v(varValue);
  _AfxDaoToEngine(v);
  SetParamValue(m_pDAOParameters, _AfxDaoItem(nIndex), v);
}

void AFXAPI _AfxDaoSetParam(CDaoQueryDef* pQueryDef, LPCTSTR lpszName, const VARIANT& value) {
  pQueryDef->InitParametersCollection();
  SetParamValue(pQueryDef->m_pDAOParameters, _AfxDaoItem(_AfxDaoStripBrackets(lpszName)), value);
}

void CDaoQueryDef::SetParameterValueNull(LPCTSTR lpszName) {
  COleVariant v;
  v.vt = VT_NULL;
  SetParameterValue(lpszName, v);
}

void CDaoQueryDef::SetParameterValueNull(int nIndex) {
  COleVariant v;
  v.vt = VT_NULL;
  SetParameterValue(nIndex, v);
}

short CDaoQueryDef::GetFieldCount() {
  InitFieldsCollection();
  short n = 0;
  DAO_CHECK(m_pDAOFields->get_Count(&n));
  return n;
}

void CDaoQueryDef::GetFieldInfo(int nIndex, CDaoFieldInfo& info, DWORD dwOptions) {
  InitFieldsCollection();
  ItemInfo(m_pDAOFields, _AfxDaoItem(nIndex), info, dwOptions, _AfxDaoFillFieldInfo);
}

void CDaoQueryDef::GetFieldInfo(LPCTSTR lpszName, CDaoFieldInfo& info, DWORD dwOptions) {
  InitFieldsCollection();
  ItemInfo(m_pDAOFields, _AfxDaoItem(lpszName), info, dwOptions, _AfxDaoFillFieldInfo);
}

short CDaoQueryDef::GetParameterCount() {
  InitParametersCollection();
  short n = 0;
  DAO_CHECK(m_pDAOParameters->get_Count(&n));
  return n;
}

void CDaoQueryDef::GetParameterInfo(int nIndex, CDaoParameterInfo& info, DWORD dwOptions) {
  InitParametersCollection();
  ItemInfo(m_pDAOParameters, _AfxDaoItem(nIndex), info, dwOptions, _AfxDaoFillParameterInfo);
}

void CDaoQueryDef::GetParameterInfo(LPCTSTR lpszName, CDaoParameterInfo& info, DWORD dwOptions) {
  InitParametersCollection();
  ItemInfo(m_pDAOParameters, _AfxDaoItem(_AfxDaoStripBrackets(lpszName)), info, dwOptions,
           _AfxDaoFillParameterInfo);
}
