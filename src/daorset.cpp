// daorset.cpp - CDaoRecordset, CDaoFieldExchange and the DFX functions.
//
// The bound members are read from the current record's fields (Fixup)
// and written to them on Update, but only those that changed: a member's
// value is kept when AddNew or Edit begins and compared then, unless it
// was bound without the field cache, when it is written if marked dirty.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "daoimpl.h"

// Internal DFX operations, after the documented ones.
enum { _AFX_DFX_ISDIRTY = CDaoFieldExchange::MaxDFXOperation + 1 };

static void ThrowNotOpen() { AfxThrowDaoException(AFX_DAO_ERROR_OBJECT_NOT_OPEN); }

// ---------------------------------------------------------------------
// CDaoFieldExchange

CDaoFieldExchange::CDaoFieldExchange(UINT nOperation, CDaoRecordset* prs, void* pvField)
    : m_nOperation(nOperation), m_prs(prs), m_nFieldType(none), m_nFieldFound(0),
      m_pvField(pvField), m_nField(0), m_nParam(0), m_pstrSQL(NULL) {}

BOOL CDaoFieldExchange::IsValidOperation() {
  if (m_nFieldType == param)
    return m_nOperation == AddToParameterList || m_nOperation == BindParam;
  if (m_nFieldType == outputColumn)
    return m_nOperation != AddToParameterList && m_nOperation != BindParam;
  return FALSE;
}

void CDaoFieldExchange::AppendParamType(CString& strParam, short nType) {
  LPCTSTR psz;
  switch (nType) {
    case VT_BOOL: psz = _T("Bit"); break;
    case VT_UI1: psz = _T("Byte"); break;
    case VT_I2: psz = _T("Short"); break;
    case VT_I4: psz = _T("Long"); break;
    case VT_CY: psz = _T("Currency"); break;
    case VT_R4: psz = _T("IEEESingle"); break;
    case VT_R8: psz = _T("IEEEDouble"); break;
    case VT_DATE: psz = _T("DateTime"); break;
    case VT_ARRAY | VT_UI1: psz = _T("LongBinary"); break;
    default: psz = _T("Text"); break;
  }
  strParam += _T(" ");
  strParam += psz;
}

// ---------------------------------------------------------------------
// The DFX functions share this: what each knows is how its member type
// becomes a VARIANT, comes back from one, and is set to null.

struct _AfxDfxType {
  VARTYPE vt;
  void (*pfnToVar)(void* pv, COleVariant& var);
  void (*pfnFromVar)(void* pv, COleVariant& var);
  void (*pfnSetNull)(void* pv);
};

static void DfxExchange(CDaoFieldExchange* pFX, LPCTSTR lpszName, void* pv,
                        const _AfxDfxType& type, DWORD dwBindOptions) {
  CDaoRecordset* prs = pFX->m_prs;
  if (pFX->m_nFieldType == CDaoFieldExchange::param) {
    pFX->m_nParam++;
    switch (pFX->m_nOperation) {
      case CDaoFieldExchange::AddToParameterList: {
        CString& str = *pFX->m_pstrSQL;
        str += pFX->m_nParam == 1 ? _T("PARAMETERS ") : _T(", ");
        str += lpszName;
        pFX->AppendParamType(str, (short)type.vt);
        break;
      }
      case CDaoFieldExchange::BindParam: {
        COleVariant var;
        type.pfnToVar(pv, var);
        prs->m_arrParamNames.Add(_AfxDaoStripBrackets(lpszName));
        prs->m_arrParamValues.Add(var);
        break;
      }
    }
    return;
  }
  if (pFX->m_nFieldType != CDaoFieldExchange::outputColumn) return;

  int nField = pFX->m_nField++;  // from 0 here
  switch (pFX->m_nOperation) {
    case CDaoFieldExchange::AddToSelectList: {
      CString& str = *pFX->m_pstrSQL;
      if (!str.IsEmpty()) str += _T(", ");
      str += lpszName;
      break;
    }
    case CDaoFieldExchange::BindField: {
      prs->m_arrFieldPtrs.SetAtGrow(nField, pv);
      DWORD dwFlags = (dwBindOptions & AFX_DAO_ENABLE_FIELD_CACHE) ? 0 : CDaoRecordset::fieldNoCache;
      prs->m_arrFieldFlags.SetAtGrow(nField, dwFlags);
      prs->m_arrFieldCache.SetAtGrow(nField, COleVariant());
      prs->m_arrNullValues.SetAtGrow(nField, COleVariant());
      break;
    }
    case CDaoFieldExchange::Fixup: {
      COleVariant var;
      DAOField* pField = prs->GetDAOField(_AfxDaoItem(_AfxDaoStripBrackets(lpszName)));
      HRESULT hr = pField->get_Value(&var);
      pField->Release();
      DAO_CHECK(hr);
      DWORD& dwFlags = prs->m_arrFieldFlags[nField];
      dwFlags &= ~(CDaoRecordset::fieldDirty | CDaoRecordset::fieldNull);
      if (var.vt == VT_NULL || var.vt == VT_EMPTY) {
        type.pfnSetNull(pv);
        dwFlags |= CDaoRecordset::fieldNull;
        COleVariant varNull;
        type.pfnToVar(pv, varNull);
        prs->m_arrNullValues[nField] = varNull;
      } else {
        type.pfnFromVar(pv, var);
      }
      break;
    }
    case CDaoFieldExchange::SetFieldNull:
      if (pFX->m_pvField == NULL || pFX->m_pvField == pv) {
        type.pfnSetNull(pv);
        prs->m_arrFieldFlags[nField] |= CDaoRecordset::fieldNull;
        COleVariant varNull;
        type.pfnToVar(pv, varNull);
        prs->m_arrNullValues[nField] = varNull;
        pFX->m_nFieldFound = (UINT)nField + 1;
      }
      break;
    case CDaoFieldExchange::MarkForAddNew:
    case CDaoFieldExchange::MarkForEdit: {
      COleVariant var;
      type.pfnToVar(pv, var);
      prs->m_arrFieldCache[nField] = var;
      prs->m_arrFieldFlags[nField] &= ~CDaoRecordset::fieldDirty;
      prs->m_arrFieldFlags[nField] |= CDaoRecordset::fieldCached;
      break;
    }
    case _AFX_DFX_ISDIRTY:
    case CDaoFieldExchange::SetDirtyField: {
      if (pFX->m_pvField != NULL && pFX->m_pvField != pv) break;
      DWORD& dwFlags = prs->m_arrFieldFlags[nField];
      COleVariant var;
      type.pfnToVar(pv, var);
      BOOL bChanged = (dwFlags & CDaoRecordset::fieldDirty) != 0;
      if (!bChanged && prs->m_bCheckCacheForDirtyFields &&
          (dwFlags & (CDaoRecordset::fieldNoCache | CDaoRecordset::fieldCached)) ==
              CDaoRecordset::fieldCached)
        bChanged = !(var == prs->m_arrFieldCache[nField]);
      if (!bChanged) break;
      if (pFX->m_nOperation == _AFX_DFX_ISDIRTY) {
        pFX->m_nFieldFound = (UINT)nField + 1;
        break;
      }
      // A member set to null, and not given a value since, is written as
      // null; one given a value is no longer null.
      BOOL bNull = (dwFlags & CDaoRecordset::fieldNull) != 0 &&
                   (prs->m_arrNullValues[nField] == var);
      if (bNull) {
        var.Clear();
        var.vt = VT_NULL;
      } else {
        dwFlags &= ~CDaoRecordset::fieldNull;
      }
      DAOField* pField = prs->GetDAOField(_AfxDaoItem(_AfxDaoStripBrackets(lpszName)));
      HRESULT hr = pField->put_Value(var);
      pField->Release();
      DAO_CHECK(hr);
      break;
    }
  }
}

// Text
static void TextToVar(void* pv, COleVariant& var) {
  var = COleVariant(*(CString*)pv, VT_BSTR);
}
static void TextFromVar(void* pv, COleVariant& var) {
  if (var.vt != VT_BSTR) var.ChangeType(VT_BSTR);
  *(CString*)pv = CString(var.bstrVal);
}
static void TextNull(void* pv) { ((CString*)pv)->Empty(); }

void AFXAPI DFX_Text(CDaoFieldExchange* pFX, LPCTSTR lpszName, CString& value, int,
                     DWORD dwBindOptions) {
  static const _AfxDfxType type = {VT_BSTR, TextToVar, TextFromVar, TextNull};
  DfxExchange(pFX, lpszName, &value, type, dwBindOptions);
}

// Binary
static void BinaryToVar(void* pv, COleVariant& var) { var = COleVariant(*(CByteArray*)pv); }
static void BinaryFromVar(void* pv, COleVariant& var) {
  CByteArray& arr = *(CByteArray*)pv;
  arr.RemoveAll();
  if (var.vt != (VT_ARRAY | VT_UI1) || var.parray == NULL) return;
  long lo = 0, hi = -1;
  ::SafeArrayGetLBound(var.parray, 1, &lo);
  ::SafeArrayGetUBound(var.parray, 1, &hi);
  arr.SetSize(hi - lo + 1);
  void* p = NULL;
  if (arr.GetSize() > 0 && SUCCEEDED(::SafeArrayAccessData(var.parray, &p))) {
    memcpy(arr.GetData(), p, (size_t)arr.GetSize());
    ::SafeArrayUnaccessData(var.parray);
  }
}
static void BinaryNull(void* pv) { ((CByteArray*)pv)->RemoveAll(); }

void AFXAPI DFX_Binary(CDaoFieldExchange* pFX, LPCTSTR lpszName, CByteArray& value, int,
                       DWORD dwBindOptions) {
  static const _AfxDfxType type = {VT_ARRAY | VT_UI1, BinaryToVar, BinaryFromVar, BinaryNull};
  DfxExchange(pFX, lpszName, &value, type, dwBindOptions);
}

// The numbers, through VariantChangeType.
template <class T, VARTYPE VT>
struct _AfxDfxNumber {
  static void ToVar(void* pv, COleVariant& var) {
    var.Clear();
    var.vt = VT;
    switch (VT) {
      case VT_BOOL: var.boolVal = *(BOOL*)pv ? VARIANT_TRUE : VARIANT_FALSE; break;
      case VT_UI1: var.bVal = *(BYTE*)pv; break;
      case VT_I2: var.iVal = *(short*)pv; break;
      case VT_I4: var.lVal = *(long*)pv; break;
      case VT_R4: var.fltVal = *(float*)pv; break;
      case VT_R8: var.dblVal = *(double*)pv; break;
    }
  }
  static void FromVar(void* pv, COleVariant& var) {
    if (var.vt != VT) var.ChangeType(VT);
    switch (VT) {
      case VT_BOOL: *(BOOL*)pv = var.boolVal != VARIANT_FALSE; break;
      case VT_UI1: *(BYTE*)pv = var.bVal; break;
      case VT_I2: *(short*)pv = var.iVal; break;
      case VT_I4: *(long*)pv = var.lVal; break;
      case VT_R4: *(float*)pv = var.fltVal; break;
      case VT_R8: *(double*)pv = var.dblVal; break;
    }
  }
  static void SetNull(void* pv) { *(T*)pv = 0; }
  static const _AfxDfxType type;
};
template <class T, VARTYPE VT>
const _AfxDfxType _AfxDfxNumber<T, VT>::type = {VT, ToVar, FromVar, SetNull};

void AFXAPI DFX_Bool(CDaoFieldExchange* pFX, LPCTSTR lpszName, BOOL& value, DWORD dwBindOptions) {
  DfxExchange(pFX, lpszName, &value, _AfxDfxNumber<BOOL, VT_BOOL>::type, dwBindOptions);
}
void AFXAPI DFX_Byte(CDaoFieldExchange* pFX, LPCTSTR lpszName, BYTE& value, DWORD dwBindOptions) {
  DfxExchange(pFX, lpszName, &value, _AfxDfxNumber<BYTE, VT_UI1>::type, dwBindOptions);
}
void AFXAPI DFX_Short(CDaoFieldExchange* pFX, LPCTSTR lpszName, short& value,
                      DWORD dwBindOptions) {
  DfxExchange(pFX, lpszName, &value, _AfxDfxNumber<short, VT_I2>::type, dwBindOptions);
}
void AFXAPI DFX_Long(CDaoFieldExchange* pFX, LPCTSTR lpszName, long& value, DWORD dwBindOptions) {
  DfxExchange(pFX, lpszName, &value, _AfxDfxNumber<long, VT_I4>::type, dwBindOptions);
}
void AFXAPI DFX_Single(CDaoFieldExchange* pFX, LPCTSTR lpszName, float& value,
                       DWORD dwBindOptions) {
  DfxExchange(pFX, lpszName, &value, _AfxDfxNumber<float, VT_R4>::type, dwBindOptions);
}
void AFXAPI DFX_Double(CDaoFieldExchange* pFX, LPCTSTR lpszName, double& value,
                       DWORD dwBindOptions) {
  DfxExchange(pFX, lpszName, &value, _AfxDfxNumber<double, VT_R8>::type, dwBindOptions);
}

// Currency and dates: a null value is the class's own null status.
static void CurrencyToVar(void* pv, COleVariant& var) {
  COleCurrency& cy = *(COleCurrency*)pv;
  var.Clear();
  if (cy.GetStatus() == COleCurrency::null) {
    var.vt = VT_NULL;
  } else {
    var.vt = VT_CY;
    var.cyVal = cy.m_cur;
  }
}
static void CurrencyFromVar(void* pv, COleVariant& var) {
  if (var.vt != VT_CY) var.ChangeType(VT_CY);
  *(COleCurrency*)pv = COleCurrency(var.cyVal);
}
static void CurrencyNull(void* pv) { ((COleCurrency*)pv)->SetStatus(COleCurrency::null); }

void AFXAPI DFX_Currency(CDaoFieldExchange* pFX, LPCTSTR lpszName, COleCurrency& value,
                         DWORD dwBindOptions) {
  static const _AfxDfxType type = {VT_CY, CurrencyToVar, CurrencyFromVar, CurrencyNull};
  DfxExchange(pFX, lpszName, &value, type, dwBindOptions);
}

static void DateToVar(void* pv, COleVariant& var) {
  COleDateTime& dt = *(COleDateTime*)pv;
  var.Clear();
  if (dt.GetStatus() == COleDateTime::null) {
    var.vt = VT_NULL;
  } else {
    var.vt = VT_DATE;
    var.date = dt.m_dt;
  }
}
static void DateFromVar(void* pv, COleVariant& var) {
  if (var.vt != VT_DATE) var.ChangeType(VT_DATE);
  *(COleDateTime*)pv = COleDateTime(var.date);
}
static void DateNull(void* pv) { ((COleDateTime*)pv)->SetStatus(COleDateTime::null); }

void AFXAPI DFX_DateTime(CDaoFieldExchange* pFX, LPCTSTR lpszName, COleDateTime& value,
                         DWORD dwBindOptions) {
  static const _AfxDfxType type = {VT_DATE, DateToVar, DateFromVar, DateNull};
  DfxExchange(pFX, lpszName, &value, type, dwBindOptions);
}

// Long binary: global memory.
static void LongBinaryToVar(void* pv, COleVariant& var) {
  CLongBinary& lb = *(CLongBinary*)pv;
  var.Clear();
  SAFEARRAY* psa = ::SafeArrayCreateVector(VT_UI1, 0, (ULONG)lb.m_dwDataLength);
  if (psa == NULL) AfxThrowMemoryException();
  if (lb.m_dwDataLength != 0 && lb.m_hData != NULL) {
    void* pDst = NULL;
    ::SafeArrayAccessData(psa, &pDst);
    void* pSrc = ::GlobalLock(lb.m_hData);
    memcpy(pDst, pSrc, (size_t)lb.m_dwDataLength);
    ::GlobalUnlock(lb.m_hData);
    ::SafeArrayUnaccessData(psa);
  }
  var.vt = VT_ARRAY | VT_UI1;
  var.parray = psa;
}
static void LongBinaryFromVar(void* pv, COleVariant& var) {
  CLongBinary& lb = *(CLongBinary*)pv;
  if (lb.m_hData != NULL) ::GlobalFree(lb.m_hData);
  lb.m_hData = NULL;
  lb.m_dwDataLength = 0;
  if (var.vt != (VT_ARRAY | VT_UI1) || var.parray == NULL) return;
  long lo = 0, hi = -1;
  ::SafeArrayGetLBound(var.parray, 1, &lo);
  ::SafeArrayGetUBound(var.parray, 1, &hi);
  DWORD cb = (DWORD)(hi - lo + 1);
  lb.m_hData = ::GlobalAlloc(GMEM_MOVEABLE, cb ? cb : 1);
  if (lb.m_hData == NULL) AfxThrowMemoryException();
  if (cb != 0) {
    void* pSrc = NULL;
    ::SafeArrayAccessData(var.parray, &pSrc);
    memcpy(::GlobalLock(lb.m_hData), pSrc, cb);
    ::GlobalUnlock(lb.m_hData);
    ::SafeArrayUnaccessData(var.parray);
  }
  lb.m_dwDataLength = cb;
}
static void LongBinaryNull(void* pv) {
  CLongBinary& lb = *(CLongBinary*)pv;
  if (lb.m_hData != NULL) ::GlobalFree(lb.m_hData);
  lb.m_hData = NULL;
  lb.m_dwDataLength = 0;
}

void AFXAPI DFX_LongBinary(CDaoFieldExchange* pFX, LPCTSTR lpszName, CLongBinary& value, DWORD,
                           DWORD dwBindOptions) {
  static const _AfxDfxType type = {VT_ARRAY | VT_UI1, LongBinaryToVar, LongBinaryFromVar,
                                   LongBinaryNull};
  DfxExchange(pFX, lpszName, &value, type, dwBindOptions);
}

// ---------------------------------------------------------------------
// CDaoRecordset

IMPLEMENT_DYNAMIC(CDaoRecordset, CObject)

CDaoRecordset::CDaoRecordset(CDaoDatabase* pDatabase)
    : m_pDatabase(pDatabase), m_nFields(0), m_nParams(0), m_bCheckCacheForDirtyFields(TRUE),
      m_pDAORecordset(NULL), m_pDAOFields(NULL), m_pDAOIndexes(NULL), m_pQueryDef(NULL),
      m_pTableDef(NULL), m_nDefaultType(dbOpenDynaset), m_nStatus(0), m_bOwnDatabase(FALSE),
      m_bOpen(FALSE), m_bDeleted(FALSE), m_bScrollable(TRUE), m_bAppendable(FALSE),
      m_bUpdatable(FALSE), m_nOpenType(0), m_pTempQueryDef(NULL), m_bOpenSQL(FALSE),
      m_nOpenOptions(0), m_bOwnQueryDef(FALSE), m_nEditMode(dbEditNone) {}

CDaoRecordset::~CDaoRecordset() {
  if (m_bOpen) {
    try {
      Close();
    } catch (CException* e) {
      e->Delete();
    }
  }
  if (m_bOwnDatabase) delete m_pDatabase;
}

CString CDaoRecordset::GetDefaultDBName() { return CString(); }

CString CDaoRecordset::GetDefaultSQL() { return CString(); }

void CDaoRecordset::DoFieldExchange(CDaoFieldExchange*) {}

void CDaoRecordset::ExchangeAll(UINT nOperation) {
  CDaoFieldExchange fx(nOperation, this);
  DoFieldExchange(&fx);
}

void CDaoRecordset::ThrowIfNotOpen() const {
  if (!m_bOpen) ThrowNotOpen();
}

BOOL CDaoRecordset::IsOpen() const { return m_bOpen; }

DAOField* CDaoRecordset::GetDAOField(const VARIANT& var) {
  ThrowIfNotOpen();
  if (m_pDAOFields == NULL) DAO_CHECK(m_pDAORecordset->get_Fields(&m_pDAOFields));
  DAOField* pField = NULL;
  DAO_CHECK(m_pDAOFields->get_Item(var, &pField));
  return pField;
}

// The SELECT statement for a table or querydef name, with the filter and
// sort; a whole statement keeps its own, with the filter and sort added.
void CDaoRecordset::BuildSQL(LPCTSTR lpszSQL) {
  CString strSource = lpszSQL != NULL ? CString(lpszSQL) : GetDefaultSQL();
  strSource.Trim();
  CString strUpper = strSource;
  strUpper.MakeUpper();
  BOOL bStatement = strUpper.Find(_T("SELECT ")) == 0 || strUpper.Find(_T("PARAMETERS ")) == 0 ||
                    strUpper.Find(_T("TRANSFORM ")) == 0;
  if (!bStatement && m_nOpenType == dbOpenTable) {
    m_strSQL = _AfxDaoStripBrackets(strSource);
    return;
  }
  CString strSQL;
  if (bStatement) {
    strSQL = strSource;
  } else {
    CString strSelect;
    if (m_nFields > 0) {
      CDaoFieldExchange fx(CDaoFieldExchange::AddToSelectList, this);
      fx.m_pstrSQL = &strSelect;
      DoFieldExchange(&fx);
    }
    if (strSelect.IsEmpty()) strSelect = _T("*");
    strSQL = _T("SELECT ") + strSelect + _T(" FROM ") + strSource;
  }
  if (!m_strFilter.IsEmpty() || !m_strSort.IsEmpty()) {
    // Any ORDER BY of the statement's own goes after the filter.
    CString strOrder;
    strUpper = strSQL;
    strUpper.MakeUpper();
    int nOrder = strUpper.Find(_T(" ORDER BY "));
    if (nOrder >= 0) {
      strOrder = strSQL.Mid(nOrder);
      strSQL = strSQL.Left(nOrder);
      strUpper = strUpper.Left(nOrder);
    }
    if (!m_strFilter.IsEmpty())
      strSQL += (strUpper.Find(_T(" WHERE ")) >= 0 ? _T(" AND (") : _T(" WHERE (")) + m_strFilter +
                _T(")");
    if (!m_strSort.IsEmpty())
      strOrder += (strOrder.IsEmpty() ? _T(" ORDER BY ") : _T(", ")) + m_strSort;
    strSQL += strOrder;
  }
  if (m_nParams > 0) {
    CString strParams;
    CDaoFieldExchange fx(CDaoFieldExchange::AddToParameterList, this);
    fx.m_pstrSQL = &strParams;
    DoFieldExchange(&fx);
    if (!strParams.IsEmpty()) strSQL = strParams + _T("; ") + strSQL;
  }
  m_strSQL = strSQL;
}

void CDaoRecordset::BindFields() {
  m_arrFieldPtrs.RemoveAll();
  m_arrFieldFlags.RemoveAll();
  m_arrFieldCache.RemoveAll();
  m_arrNullValues.RemoveAll();
  m_arrParamNames.RemoveAll();
  m_arrParamValues.RemoveAll();
  CDaoFieldExchange fx(CDaoFieldExchange::BindField, this);
  DoFieldExchange(&fx);
  if (m_arrFieldPtrs.GetSize() != m_nFields) AfxThrowDaoException(AFX_DAO_ERROR_DFX_BIND);
  if (m_nParams > 0) {
    CDaoFieldExchange fxParams(CDaoFieldExchange::BindParam, this);
    DoFieldExchange(&fxParams);
  }
}

void CDaoRecordset::SetCursorAttributes() {
  VARIANT_BOOL b = VARIANT_FALSE;
  DAO_CHECK(m_pDAORecordset->get_Updatable(&b));
  m_bUpdatable = b != VARIANT_FALSE;
  m_bAppendable = m_bUpdatable;
  m_bScrollable = m_nOpenType != dbOpenForwardOnly;
}

void CDaoRecordset::Fixup() {
  m_bDeleted = FALSE;
  if (IsEOF() || IsBOF()) {
    ExchangeAll(CDaoFieldExchange::SetFieldNull);
    ClearFieldFlags(fieldDirty);
  } else if (m_nFields > 0) {
    ExchangeAll(CDaoFieldExchange::Fixup);
  }
}

void CDaoRecordset::ClearFieldFlags(DWORD dwFlags) {
  for (INT_PTR i = 0; i < m_arrFieldFlags.GetSize(); i++) m_arrFieldFlags[i] &= ~dwFlags;
}

void CDaoRecordset::OpenFromSQL(int nOpenType, int nOptions) {
  COleVariant varType((long)nOpenType);
  VARIANT varOptions = nOptions != 0 ? (VARIANT)COleVariant((long)nOptions) : _AfxDaoOptional();
  if (m_nParams > 0 && m_nOpenType != dbOpenTable) {
    // Parameters need a querydef: a temporary one, which is given them.
    m_pTempQueryDef = new CDaoQueryDef(m_pDatabase);
    m_bOwnQueryDef = TRUE;
    m_pTempQueryDef->Create(NULL, m_strSQL);
    for (INT_PTR i = 0; i < m_arrParamNames.GetSize(); i++)
      _AfxDaoSetParam(m_pTempQueryDef, m_arrParamNames[i], m_arrParamValues[i]);
    DAO_CHECK(m_pTempQueryDef->m_pDAOQueryDef->OpenRecordset(varType, varOptions,
                                                             _AfxDaoOptional(), &m_pDAORecordset));
  } else {
    DAO_CHECK(m_pDatabase->m_pDAODatabase->OpenRecordset(
        _AfxDaoBstr(m_strSQL), varType, varOptions, _AfxDaoOptional(), &m_pDAORecordset));
  }
}

// A recordset opens on the database it was given, or on the one
// GetDefaultDBName names.
static void OpenDatabaseFor(CDaoRecordset* prs) {
  if (prs->m_pDatabase == NULL) {
    prs->m_pDatabase = new CDaoDatabase;
    prs->m_bOwnDatabase = TRUE;
  }
  if (!prs->m_pDatabase->IsOpen()) prs->m_pDatabase->Open(prs->GetDefaultDBName());
}

void CDaoRecordset::Open(int nOpenType, LPCTSTR lpszSQL, int nOptions) {
  if (m_bOpen) Close();
  if (nOpenType == AFX_DAO_USE_DEFAULT_TYPE) nOpenType = m_nDefaultType;
  OpenDatabaseFor(this);
  m_nOpenType = (short)nOpenType;
  m_bOpenSQL = lpszSQL != NULL;
  m_strOpenSQL = lpszSQL != NULL ? lpszSQL : _T("");
  m_nOpenOptions = nOptions;
  m_strOpenFilter = m_strFilter;
  m_strOpenSort = m_strSort;
  BindFields();
  BuildSQL(lpszSQL);
  OpenFromSQL(nOpenType, nOptions);
  m_bOpen = TRUE;
  m_pDatabase->m_mapRecordsets.SetAt(this, this);
  SetCursorAttributes();
  if (nOptions & dbReadOnly) m_bUpdatable = m_bAppendable = FALSE;
  if (nOptions & dbAppendOnly) m_bUpdatable = FALSE;
  Fixup();
}

void CDaoRecordset::Open(CDaoQueryDef* pQueryDef, int nOpenType, int nOptions) {
  if (m_bOpen) Close();
  if (pQueryDef == NULL || !pQueryDef->IsOpen()) ThrowNotOpen();
  if (nOpenType == AFX_DAO_USE_DEFAULT_TYPE) nOpenType = m_nDefaultType;
  m_pQueryDef = pQueryDef;
  if (m_pDatabase == NULL) m_pDatabase = pQueryDef->m_pDatabase;
  m_nOpenType = (short)nOpenType;
  m_nOpenOptions = nOptions;
  BindFields();
  for (INT_PTR i = 0; i < m_arrParamNames.GetSize(); i++)
    _AfxDaoSetParam(pQueryDef, m_arrParamNames[i], m_arrParamValues[i]);
  m_strSQL = pQueryDef->GetSQL();
  DAO_CHECK(pQueryDef->m_pDAOQueryDef->OpenRecordset(
      COleVariant((long)nOpenType),
      nOptions != 0 ? (VARIANT)COleVariant((long)nOptions) : _AfxDaoOptional(), _AfxDaoOptional(),
      &m_pDAORecordset));
  m_bOpen = TRUE;
  m_pDatabase->m_mapRecordsets.SetAt(this, this);
  SetCursorAttributes();
  if (nOptions & dbReadOnly) m_bUpdatable = m_bAppendable = FALSE;
  Fixup();
}

void CDaoRecordset::Open(CDaoTableDef* pTableDef, int nOpenType, int nOptions) {
  if (m_bOpen) Close();
  if (pTableDef == NULL || !pTableDef->IsOpen()) ThrowNotOpen();
  if (nOpenType == AFX_DAO_USE_DEFAULT_TYPE) nOpenType = dbOpenTable;
  m_pTableDef = pTableDef;
  if (m_pDatabase == NULL) m_pDatabase = pTableDef->m_pDatabase;
  if (nOpenType != dbOpenTable) {
    // Any other type is a query on the table.
    CString strName = _T("[") + pTableDef->GetName() + _T("]");
    Open(nOpenType, strName, nOptions);
    return;
  }
  m_nOpenType = (short)nOpenType;
  BindFields();
  m_strSQL = pTableDef->GetName();
  DAO_CHECK(pTableDef->m_pDAOTableDef->OpenRecordset(
      COleVariant((long)nOpenType),
      nOptions != 0 ? (VARIANT)COleVariant((long)nOptions) : _AfxDaoOptional(), &m_pDAORecordset));
  m_bOpen = TRUE;
  m_pDatabase->m_mapRecordsets.SetAt(this, this);
  SetCursorAttributes();
  if (nOptions & dbReadOnly) m_bUpdatable = m_bAppendable = FALSE;
  Fixup();
}

void CDaoRecordset::Close() {
  if (m_pDAORecordset != NULL) {
    if (m_nEditMode != dbEditNone) m_pDAORecordset->CancelUpdate(dbUpdateRegular);
    if (m_pDAOIndexes != NULL) m_pDAOIndexes->Release();
    if (m_pDAOFields != NULL) m_pDAOFields->Release();
    m_pDAOIndexes = NULL;
    m_pDAOFields = NULL;
    m_pDAORecordset->Close();
    m_pDAORecordset->Release();
    m_pDAORecordset = NULL;
  }
  m_nEditMode = dbEditNone;
  if (m_bOwnQueryDef) delete m_pTempQueryDef;
  m_pTempQueryDef = NULL;
  m_bOwnQueryDef = FALSE;
  if (m_bOpen && m_pDatabase != NULL) m_pDatabase->m_mapRecordsets.RemoveKey(this);
  m_bOpen = FALSE;
  m_bDeleted = FALSE;
  m_pQueryDef = NULL;
  m_pTableDef = NULL;
}

BOOL CDaoRecordset::IsBOF() const {
  ThrowIfNotOpen();
  VARIANT_BOOL b = VARIANT_FALSE;
  DAO_CHECK(m_pDAORecordset->get_BOF(&b));
  return b != VARIANT_FALSE;
}

BOOL CDaoRecordset::IsEOF() const {
  ThrowIfNotOpen();
  VARIANT_BOOL b = VARIANT_FALSE;
  DAO_CHECK(m_pDAORecordset->get_EOF(&b));
  return b != VARIANT_FALSE;
}

BOOL CDaoRecordset::CanAppend() const {
  ThrowIfNotOpen();
  return m_bAppendable;
}

BOOL CDaoRecordset::CanUpdate() const {
  ThrowIfNotOpen();
  return m_bUpdatable;
}

BOOL CDaoRecordset::CanBookmark() {
  ThrowIfNotOpen();
  VARIANT_BOOL b = VARIANT_FALSE;
  DAO_CHECK(m_pDAORecordset->get_Bookmarkable(&b));
  return b != VARIANT_FALSE;
}

BOOL CDaoRecordset::CanRestart() {
  ThrowIfNotOpen();
  VARIANT_BOOL b = VARIANT_FALSE;
  DAO_CHECK(m_pDAORecordset->get_Restartable(&b));
  return b != VARIANT_FALSE;
}

BOOL CDaoRecordset::CanTransact() {
  ThrowIfNotOpen();
  VARIANT_BOOL b = VARIANT_FALSE;
  DAO_CHECK(m_pDAORecordset->get_Transactions(&b));
  return b != VARIANT_FALSE;
}

short CDaoRecordset::GetType() {
  ThrowIfNotOpen();
  short s = 0;
  DAO_CHECK(m_pDAORecordset->get_Type(&s));
  return s;
}

CString CDaoRecordset::GetName() {
  ThrowIfNotOpen();
  BSTR b = NULL;
  DAO_CHECK(m_pDAORecordset->get_Name(&b));
  return _AfxDaoString(b);
}

short CDaoRecordset::GetEditMode() {
  ThrowIfNotOpen();
  short s = 0;
  DAO_CHECK(m_pDAORecordset->get_EditMode(&s));
  return s;
}

long CDaoRecordset::GetRecordCount() {
  ThrowIfNotOpen();
  long l = 0;
  DAO_CHECK(m_pDAORecordset->get_RecordCount(&l));
  return l;
}

COleDateTime CDaoRecordset::GetDateCreated() {
  ThrowIfNotOpen();
  COleVariant v;
  DAO_CHECK(m_pDAORecordset->get_DateCreated(&v));
  COleDateTime dt;
  if (v.vt == VT_DATE) dt = v.date;
  else dt.SetStatus(COleDateTime::null);
  return dt;
}

COleDateTime CDaoRecordset::GetDateLastUpdated() {
  ThrowIfNotOpen();
  COleVariant v;
  DAO_CHECK(m_pDAORecordset->get_LastUpdated(&v));
  COleDateTime dt;
  if (v.vt == VT_DATE) dt = v.date;
  else dt.SetStatus(COleDateTime::null);
  return dt;
}

CString CDaoRecordset::GetValidationRule() {
  ThrowIfNotOpen();
  BSTR b = NULL;
  DAO_CHECK(m_pDAORecordset->get_ValidationRule(&b));
  return _AfxDaoString(b);
}

CString CDaoRecordset::GetValidationText() {
  ThrowIfNotOpen();
  BSTR b = NULL;
  DAO_CHECK(m_pDAORecordset->get_ValidationText(&b));
  return _AfxDaoString(b);
}

BOOL CDaoRecordset::GetLockingMode() {
  ThrowIfNotOpen();
  VARIANT_BOOL b = VARIANT_FALSE;
  DAO_CHECK(m_pDAORecordset->get_LockEdits(&b));
  return b != VARIANT_FALSE;
}

void CDaoRecordset::SetLockingMode(BOOL bPessimistic) {
  ThrowIfNotOpen();
  DAO_CHECK(m_pDAORecordset->put_LockEdits(bPessimistic ? VARIANT_TRUE : VARIANT_FALSE));
}

// ----- Moving: an edit in progress is dropped, as the engine drops it.

void CDaoRecordset::MoveNext() {
  ThrowIfNotOpen();
  m_nEditMode = dbEditNone;
  DAO_CHECK(m_pDAORecordset->MoveNext());
  Fixup();
}

void CDaoRecordset::MovePrev() {
  ThrowIfNotOpen();
  m_nEditMode = dbEditNone;
  DAO_CHECK(m_pDAORecordset->MovePrevious());
  Fixup();
}

void CDaoRecordset::MoveFirst() {
  ThrowIfNotOpen();
  m_nEditMode = dbEditNone;
  DAO_CHECK(m_pDAORecordset->MoveFirst());
  Fixup();
}

void CDaoRecordset::MoveLast() {
  ThrowIfNotOpen();
  m_nEditMode = dbEditNone;
  DAO_CHECK(m_pDAORecordset->MoveLast(0));
  Fixup();
}

void CDaoRecordset::Move(long lRows) {
  ThrowIfNotOpen();
  m_nEditMode = dbEditNone;
  DAO_CHECK(m_pDAORecordset->Move(lRows, _AfxDaoOptional()));
  Fixup();
}

long CDaoRecordset::GetAbsolutePosition() {
  ThrowIfNotOpen();
  long l = -1;
  DAO_CHECK(m_pDAORecordset->get_AbsolutePosition(&l));
  return l;
}

void CDaoRecordset::SetAbsolutePosition(long lPosition) {
  ThrowIfNotOpen();
  DAO_CHECK(m_pDAORecordset->put_AbsolutePosition(lPosition));
  Fixup();
}

float CDaoRecordset::GetPercentPosition() {
  ThrowIfNotOpen();
  float f = 0;
  DAO_CHECK(m_pDAORecordset->get_PercentPosition(&f));
  return f;
}

void CDaoRecordset::SetPercentPosition(float fPosition) {
  ThrowIfNotOpen();
  DAO_CHECK(m_pDAORecordset->put_PercentPosition(fPosition));
  Fixup();
}

// A bookmark is an array of bytes in a VARIANT.
static COleVariant BookmarkVariant(SAFEARRAY* psa) {
  COleVariant v;
  if (psa != NULL) {
    v.vt = VT_ARRAY | VT_UI1;
    v.parray = psa;
  }
  return v;
}

COleVariant CDaoRecordset::GetBookmark() {
  ThrowIfNotOpen();
  SAFEARRAY* psa = NULL;
  DAO_CHECK(m_pDAORecordset->get_Bookmark(&psa));
  return BookmarkVariant(psa);
}

void CDaoRecordset::SetBookmark(COleVariant varBookmark) {
  ThrowIfNotOpen();
  if (varBookmark.vt != (VT_ARRAY | VT_UI1)) AfxThrowDaoException(NO_AFX_DAO_ERROR, E_INVALIDARG);
  m_nEditMode = dbEditNone;
  DAO_CHECK(m_pDAORecordset->put_Bookmark(&varBookmark.parray));
  Fixup();
}

COleVariant CDaoRecordset::GetLastModifiedBookmark() {
  ThrowIfNotOpen();
  SAFEARRAY* psa = NULL;
  DAO_CHECK(m_pDAORecordset->get_LastModified(&psa));
  return BookmarkVariant(psa);
}

// ----- Finding

BOOL CDaoRecordset::Find(long lFindType, LPCTSTR lpszFilter) {
  ThrowIfNotOpen();
  _AfxDaoBstr bstr(lpszFilter);
  HRESULT hr;
  switch (lFindType) {
    case AFX_DAO_FIRST: hr = m_pDAORecordset->FindFirst(bstr); break;
    case AFX_DAO_LAST: hr = m_pDAORecordset->FindLast(bstr); break;
    case AFX_DAO_PREV: hr = m_pDAORecordset->FindPrevious(bstr); break;
    default: hr = m_pDAORecordset->FindNext(bstr); break;
  }
  DAO_CHECK(hr);
  m_nEditMode = dbEditNone;
  VARIANT_BOOL bNoMatch = VARIANT_TRUE;
  DAO_CHECK(m_pDAORecordset->get_NoMatch(&bNoMatch));
  if (bNoMatch != VARIANT_FALSE) return FALSE;
  Fixup();
  return TRUE;
}

BOOL CDaoRecordset::FindFirst(LPCTSTR lpszFilter) { return Find(AFX_DAO_FIRST, lpszFilter); }
BOOL CDaoRecordset::FindLast(LPCTSTR lpszFilter) { return Find(AFX_DAO_LAST, lpszFilter); }
BOOL CDaoRecordset::FindNext(LPCTSTR lpszFilter) { return Find(AFX_DAO_NEXT, lpszFilter); }
BOOL CDaoRecordset::FindPrev(LPCTSTR lpszFilter) { return Find(AFX_DAO_PREV, lpszFilter); }

CString CDaoRecordset::GetCurrentIndex() {
  ThrowIfNotOpen();
  BSTR b = NULL;
  DAO_CHECK(m_pDAORecordset->get_Index(&b));
  return _AfxDaoString(b);
}

void CDaoRecordset::SetCurrentIndex(LPCTSTR lpszIndex) {
  ThrowIfNotOpen();
  DAO_CHECK(m_pDAORecordset->put_Index(_AfxDaoBstr(lpszIndex)));
  Fixup();
}

BOOL CDaoRecordset::Seek(LPCTSTR lpszComparison, COleVariant* pKey1, COleVariant* pKey2,
                         COleVariant* pKey3) {
  COleVariant keys[3];
  WORD nKeys = 0;
  COleVariant* p[3] = {pKey1, pKey2, pKey3};
  for (int i = 0; i < 3 && p[i] != NULL; i++) keys[nKeys++] = *p[i];
  return Seek(lpszComparison, nKeys > 0 ? keys : NULL, nKeys);
}

BOOL CDaoRecordset::Seek(LPCTSTR lpszComparison, COleVariant* pKeyArray, WORD nKeys) {
  ThrowIfNotOpen();
  if (nKeys > 13) nKeys = 13;
  COleVariant given[13];
  VARIANT keys[13];
  for (int i = 0; i < 13; i++) {
    if (i < nKeys) {
      given[i] = pKeyArray[i];
      _AfxDaoToEngine(given[i]);
      keys[i] = given[i];
    } else {
      keys[i] = _AfxDaoOptional();
    }
  }
  DAO_CHECK(m_pDAORecordset->Seek(_AfxDaoBstr(lpszComparison), keys[0], keys[1], keys[2], keys[3],
                                  keys[4], keys[5], keys[6], keys[7], keys[8], keys[9], keys[10],
                                  keys[11], keys[12]));
  m_nEditMode = dbEditNone;
  VARIANT_BOOL bNoMatch = VARIANT_TRUE;
  DAO_CHECK(m_pDAORecordset->get_NoMatch(&bNoMatch));
  if (bNoMatch != VARIANT_FALSE) return FALSE;
  Fixup();
  return TRUE;
}

// ----- Editing

void CDaoRecordset::AddNew() {
  ThrowIfNotOpen();
  DAO_CHECK(m_pDAORecordset->AddNew());
  m_nEditMode = dbEditAdd;
  // The members start null; what is set in them before Update is added.
  ExchangeAll(CDaoFieldExchange::SetFieldNull);
  ClearFieldFlags(fieldDirty);
  ExchangeAll(CDaoFieldExchange::MarkForAddNew);
}

void CDaoRecordset::Edit() {
  ThrowIfNotOpen();
  DAO_CHECK(m_pDAORecordset->Edit());
  m_nEditMode = dbEditInProgress;
  ExchangeAll(CDaoFieldExchange::MarkForEdit);
}

void CDaoRecordset::Update() {
  ThrowIfNotOpen();
  if (m_nEditMode == dbEditNone) AfxThrowDaoException(NO_AFX_DAO_ERROR, E_UNEXPECTED);
  ExchangeAll(CDaoFieldExchange::SetDirtyField);
  DAO_CHECK(m_pDAORecordset->Update(dbUpdateRegular, VARIANT_FALSE));
  m_nEditMode = dbEditNone;
  for (INT_PTR i = 0; i < m_arrFieldFlags.GetSize(); i++)
    m_arrFieldFlags[i] &= ~(fieldDirty | fieldCached);
  // The current record is the one before AddNew, or the one edited.
  Fixup();
}

void CDaoRecordset::Delete() {
  ThrowIfNotOpen();
  DAO_CHECK(m_pDAORecordset->Delete());
  m_nEditMode = dbEditNone;
  m_bDeleted = TRUE;
  ExchangeAll(CDaoFieldExchange::SetFieldNull);
  ClearFieldFlags(fieldDirty);
}

void CDaoRecordset::CancelUpdate() {
  ThrowIfNotOpen();
  if (m_nEditMode == dbEditNone) return;
  DAO_CHECK(m_pDAORecordset->CancelUpdate(dbUpdateRegular));
  m_nEditMode = dbEditNone;
  ClearFieldFlags(fieldDirty | fieldCached);
  Fixup();
}

void CDaoRecordset::Requery() {
  ThrowIfNotOpen();
  m_nEditMode = dbEditNone;
  // New parameter values, or a new filter or sort, need the recordset
  // opened again; otherwise the engine runs the same query again.
  if (m_pQueryDef != NULL) {
    if (m_nParams > 0) {
      CDaoQueryDef* pQueryDef = m_pQueryDef;
      Open(pQueryDef, m_nOpenType, m_nOpenOptions);
      return;
    }
  } else if (m_nOpenType != dbOpenTable &&
             (m_nParams > 0 || m_strFilter != m_strOpenFilter || m_strSort != m_strOpenSort)) {
    CString strSQL = m_strOpenSQL;
    Open(m_nOpenType, m_bOpenSQL ? (LPCTSTR)strSQL : NULL, m_nOpenOptions);
    return;
  }
  DAO_CHECK(m_pDAORecordset->Requery(_AfxDaoOptional()));
  Fixup();
}

// ----- Fields by their members

int CDaoRecordset::FieldIndex(void* pv) {
  for (INT_PTR i = 0; i < m_arrFieldPtrs.GetSize(); i++)
    if (m_arrFieldPtrs[i] == pv) return (int)i;
  AfxThrowDaoException(AFX_DAO_ERROR_DFX_BIND);
  return -1;
}

void CDaoRecordset::SetFieldDirty(void* pv, BOOL bDirty) {
  ThrowIfNotOpen();
  if (pv == NULL) {
    for (INT_PTR i = 0; i < m_arrFieldFlags.GetSize(); i++) {
      if (bDirty) m_arrFieldFlags[i] |= fieldDirty;
      else m_arrFieldFlags[i] &= ~fieldDirty;
    }
    return;
  }
  int n = FieldIndex(pv);
  if (bDirty) m_arrFieldFlags[n] |= fieldDirty;
  else m_arrFieldFlags[n] &= ~fieldDirty;
}

void CDaoRecordset::SetFieldNull(void* pv, BOOL bNull) {
  ThrowIfNotOpen();
  if (pv != NULL) FieldIndex(pv);  // a bound member
  if (bNull) {
    // The members take their null values, written as null on Update.
    CDaoFieldExchange fx(CDaoFieldExchange::SetFieldNull, this, pv);
    DoFieldExchange(&fx);
    for (INT_PTR i = 0; i < m_arrFieldPtrs.GetSize(); i++)
      if (pv == NULL || m_arrFieldPtrs[i] == pv) m_arrFieldFlags[i] |= fieldDirty;
  } else {
    for (INT_PTR i = 0; i < m_arrFieldPtrs.GetSize(); i++) {
      if (pv != NULL && m_arrFieldPtrs[i] != pv) continue;
      m_arrFieldFlags[i] &= ~fieldNull;
      m_arrFieldFlags[i] |= fieldDirty;
    }
  }
}

BOOL CDaoRecordset::IsFieldDirty(void* pv) {
  ThrowIfNotOpen();
  if (pv != NULL) FieldIndex(pv);
  CDaoFieldExchange fx(_AFX_DFX_ISDIRTY, this, pv);
  DoFieldExchange(&fx);
  return fx.m_nFieldFound != 0;
}

BOOL CDaoRecordset::IsFieldNull(void* pv) {
  ThrowIfNotOpen();
  if (pv == NULL) {
    for (INT_PTR i = 0; i < m_arrFieldFlags.GetSize(); i++)
      if ((m_arrFieldFlags[i] & fieldNull) == 0) return FALSE;
    return TRUE;
  }
  return (m_arrFieldFlags[FieldIndex(pv)] & fieldNull) != 0;
}

BOOL CDaoRecordset::IsFieldNullable(void* pv) {
  ThrowIfNotOpen();
  int n = FieldIndex(pv);
  CDaoFieldInfo info;
  GetFieldInfo(n, info, AFX_DAO_SECONDARY_INFO | AFX_DAO_PRIMARY_INFO);
  return !info.m_bRequired;
}

// ----- Fields by name or number

void CDaoRecordset::GetFieldValue(LPCTSTR lpszName, COleVariant& varValue) {
  DAOField* pField = GetDAOField(_AfxDaoItem(_AfxDaoStripBrackets(lpszName)));
  varValue.Clear();
  HRESULT hr = pField->get_Value(&varValue);
  pField->Release();
  DAO_CHECK(hr);
  _AfxDaoFromEngine(varValue);
}

void CDaoRecordset::GetFieldValue(int nIndex, COleVariant& varValue) {
  DAOField* pField = GetDAOField(_AfxDaoItem(nIndex));
  varValue.Clear();
  HRESULT hr = pField->get_Value(&varValue);
  pField->Release();
  DAO_CHECK(hr);
  _AfxDaoFromEngine(varValue);
}

COleVariant CDaoRecordset::GetFieldValue(LPCTSTR lpszName) {
  COleVariant v;
  GetFieldValue(lpszName, v);
  return v;
}

COleVariant CDaoRecordset::GetFieldValue(int nIndex) {
  COleVariant v;
  GetFieldValue(nIndex, v);
  return v;
}

void CDaoRecordset::SetFieldValue(LPCTSTR lpszName, const COleVariant& varValue) {
  DAOField* pField = GetDAOField(_AfxDaoItem(_AfxDaoStripBrackets(lpszName)));
  COleVariant v(varValue);
  _AfxDaoToEngine(v);
  HRESULT hr = pField->put_Value(v);
  pField->Release();
  DAO_CHECK(hr);
}

void CDaoRecordset::SetFieldValue(int nIndex, const COleVariant& varValue) {
  DAOField* pField = GetDAOField(_AfxDaoItem(nIndex));
  COleVariant v(varValue);
  _AfxDaoToEngine(v);
  HRESULT hr = pField->put_Value(v);
  pField->Release();
  DAO_CHECK(hr);
}

void CDaoRecordset::SetFieldValue(LPCTSTR lpszName, LPCTSTR lpszValue) {
  SetFieldValue(lpszName, COleVariant(lpszValue, VT_BSTRT));
}

void CDaoRecordset::SetFieldValue(int nIndex, LPCTSTR lpszValue) {
  SetFieldValue(nIndex, COleVariant(lpszValue, VT_BSTRT));
}

void CDaoRecordset::SetFieldValueNull(LPCTSTR lpszName) {
  COleVariant v;
  v.vt = VT_NULL;
  SetFieldValue(lpszName, v);
}

void CDaoRecordset::SetFieldValueNull(int nIndex) {
  COleVariant v;
  v.vt = VT_NULL;
  SetFieldValue(nIndex, v);
}

// Parameters are the querydef's: the one opened, or the temporary one.
static CDaoQueryDef* ParamQueryDef(CDaoRecordset* prs) {
  if (prs->m_pQueryDef != NULL) return prs->m_pQueryDef;
  if (prs->m_pTempQueryDef != NULL) return prs->m_pTempQueryDef;
  AfxThrowDaoException(AFX_DAO_ERROR_OBJECT_NOT_OPEN);
  return NULL;
}

COleVariant CDaoRecordset::GetParamValue(int nIndex) {
  ThrowIfNotOpen();
  return ParamQueryDef(this)->GetParameterValue(nIndex);
}

COleVariant CDaoRecordset::GetParamValue(LPCTSTR lpszName) {
  ThrowIfNotOpen();
  return ParamQueryDef(this)->GetParameterValue(lpszName);
}

void CDaoRecordset::SetParamValue(int nIndex, const COleVariant& var) {
  ThrowIfNotOpen();
  ParamQueryDef(this)->SetParameterValue(nIndex, var);
}

void CDaoRecordset::SetParamValue(LPCTSTR lpszName, const COleVariant& var) {
  ThrowIfNotOpen();
  ParamQueryDef(this)->SetParameterValue(lpszName, var);
}

void CDaoRecordset::SetParamValueNull(int nIndex) {
  COleVariant v;
  v.vt = VT_NULL;
  SetParamValue(nIndex, v);
}

void CDaoRecordset::SetParamValueNull(LPCTSTR lpszName) {
  COleVariant v;
  v.vt = VT_NULL;
  SetParamValue(lpszName, v);
}

short CDaoRecordset::GetFieldCount() {
  ThrowIfNotOpen();
  if (m_pDAOFields == NULL) DAO_CHECK(m_pDAORecordset->get_Fields(&m_pDAOFields));
  short n = 0;
  DAO_CHECK(m_pDAOFields->get_Count(&n));
  return n;
}

static void FieldInfo(CDaoRecordset* prs, const COleVariant& var, CDaoFieldInfo& info,
                      DWORD dwOptions) {
  DAOField* p = prs->GetDAOField(var);
  try {
    _AfxDaoFillFieldInfo(p, info, dwOptions);
  } catch (CException*) {
    p->Release();
    throw;
  }
  p->Release();
}

void CDaoRecordset::GetFieldInfo(int nIndex, CDaoFieldInfo& info, DWORD dwOptions) {
  FieldInfo(this, _AfxDaoItem(nIndex), info, dwOptions);
}

void CDaoRecordset::GetFieldInfo(LPCTSTR lpszName, CDaoFieldInfo& info, DWORD dwOptions) {
  FieldInfo(this, _AfxDaoItem(_AfxDaoStripBrackets(lpszName)), info, dwOptions);
}

short CDaoRecordset::GetIndexCount() {
  ThrowIfNotOpen();
  if (m_pDAOIndexes == NULL) DAO_CHECK(m_pDAORecordset->get_Indexes(&m_pDAOIndexes));
  short n = 0;
  DAO_CHECK(m_pDAOIndexes->get_Count(&n));
  return n;
}

static void IndexInfo(CDaoRecordset* prs, const COleVariant& var, CDaoIndexInfo& info,
                      DWORD dwOptions) {
  prs->GetIndexCount();  // the collection
  DAOIndex* p = NULL;
  DAO_CHECK(prs->m_pDAOIndexes->get_Item(var, &p));
  try {
    _AfxDaoFillIndexInfo(p, info, dwOptions);
  } catch (CException*) {
    p->Release();
    throw;
  }
  p->Release();
}

void CDaoRecordset::GetIndexInfo(int nIndex, CDaoIndexInfo& info, DWORD dwOptions) {
  IndexInfo(this, _AfxDaoItem(nIndex), info, dwOptions);
}

void CDaoRecordset::GetIndexInfo(LPCTSTR lpszName, CDaoIndexInfo& info, DWORD dwOptions) {
  IndexInfo(this, _AfxDaoItem(lpszName), info, dwOptions);
}
