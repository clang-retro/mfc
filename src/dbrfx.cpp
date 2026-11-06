// dbrfx.cpp - record field exchange: the RFX functions, which describe a
// recordset's fields, and moving field values between member variables,
// ODBC and the fetched rows. Also CLongBinary and CDBVariant.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "dbimpl.h"

IMPLEMENT_DYNAMIC(CLongBinary, CObject)

CLongBinary::CLongBinary() : m_hData(NULL), m_dwDataLength(0) {}

CLongBinary::~CLongBinary() {
  if (m_hData != NULL) ::GlobalFree(m_hData);
}

CDBVariant::CDBVariant() : m_dwType(DBVT_NULL) { m_pstring = NULL; }

CDBVariant::~CDBVariant() { Clear(); }

void CDBVariant::Clear() {
  switch (m_dwType) {
  case DBVT_DATE: delete m_pdate; break;
  case DBVT_STRING: delete m_pstring; break;
  case DBVT_BINARY: delete m_pbinary; break;
  case DBVT_ASTRING: delete m_pstringA; break;
  case DBVT_WSTRING: delete m_pstringW; break;
  }
  m_dwType = DBVT_NULL;
  m_pstring = NULL;
}

// ---------------------------------------------------------------------
// CFieldExchange

CFieldExchange::CFieldExchange(UINT nOperation, CRecordset* prs, void* pvField)
    : m_nOperation(nOperation), m_prs(prs), m_nFieldType(outputColumn), m_nFields(0),
      m_nParams(0), m_pvField(pvField), m_pstr(NULL) {}

BOOL CFieldExchange::IsFieldType(UINT* pnField) {
  if (m_nFieldType == outputColumn) {
    *pnField = ++m_nFields;
    return TRUE;
  }
  *pnField = ++m_nParams;
  return TRUE;
}

// ---------------------------------------------------------------------
// Member bytes

void AFXAPI _AfxGetFieldBytes(const _AFX_RFX_FIELD& field, CByteArray& bytes) {
  switch (field.nKind) {
  case _AFX_RFX_STRING: {
    const CString& str = *(const CString*)field.pv;
    bytes.SetSize(str.GetLength() * (INT_PTR)sizeof(TCHAR));
    if (bytes.GetSize()) memcpy(bytes.GetData(), (LPCTSTR)str, bytes.GetSize());
    break;
  }
  case _AFX_RFX_TEXTBUF: {
    LPCTSTR psz = (LPCTSTR)field.pv;
    int n = 0;
    while (n < field.nSize && psz[n] != 0) n++;
    bytes.SetSize(n * (INT_PTR)sizeof(TCHAR));
    if (n) memcpy(bytes.GetData(), psz, bytes.GetSize());
    break;
  }
  case _AFX_RFX_BINARY:
    bytes.Copy(*(const CByteArray*)field.pv);
    break;
  case _AFX_RFX_LONGBINARY: {
    const CLongBinary& lb = *(const CLongBinary*)field.pv;
    bytes.SetSize(lb.m_hData != NULL ? (INT_PTR)lb.m_dwDataLength : 0);
    if (bytes.GetSize()) {
      const void* p = ::GlobalLock(lb.m_hData);
      memcpy(bytes.GetData(), p, bytes.GetSize());
      ::GlobalUnlock(lb.m_hData);
    }
    break;
  }
  default:
    bytes.SetSize(field.nSize);
    memcpy(bytes.GetData(), field.pv, field.nSize);
    break;
  }
}

void AFXAPI _AfxSetFieldBytes(const _AFX_RFX_FIELD& field, const BYTE* p, INT_PTR nBytes) {
  switch (field.nKind) {
  case _AFX_RFX_STRING: {
    int nChars = (int)(nBytes / sizeof(TCHAR));
    CString& str = *(CString*)field.pv;
    if (nChars == 0)
      str.Empty();
    else
      str = CString((LPCTSTR)p, nChars);
    break;
  }
  case _AFX_RFX_TEXTBUF: {
    int nChars = (int)(nBytes / sizeof(TCHAR));
    if (nChars > field.nSize - 1) nChars = field.nSize - 1;
    if (nChars < 0) nChars = 0;
    LPTSTR psz = (LPTSTR)field.pv;
    if (nChars) memcpy(psz, p, nChars * sizeof(TCHAR));
    if (field.nSize > 0) psz[nChars] = 0;
    break;
  }
  case _AFX_RFX_BINARY: {
    CByteArray& arr = *(CByteArray*)field.pv;
    arr.SetSize(nBytes);
    if (nBytes) memcpy(arr.GetData(), p, nBytes);
    break;
  }
  case _AFX_RFX_LONGBINARY: {
    CLongBinary& lb = *(CLongBinary*)field.pv;
    if (lb.m_hData != NULL) ::GlobalFree(lb.m_hData);
    lb.m_hData = ::GlobalAlloc(GMEM_MOVEABLE, nBytes ? nBytes : 1);
    if (lb.m_hData == NULL) AfxThrowMemoryException();
    if (nBytes) {
      void* pDest = ::GlobalLock(lb.m_hData);
      memcpy(pDest, p, nBytes);
      ::GlobalUnlock(lb.m_hData);
    }
    lb.m_dwDataLength = (DWORD_PTR)nBytes;
    break;
  }
  default:
    if (nBytes == field.nSize) memcpy(field.pv, p, field.nSize);
    break;
  }
}

void AFXAPI _AfxSetFieldPseudoNull(const _AFX_RFX_FIELD& field) {
  _AfxSetFieldBytes(field, field.rgPseudoNull, field.nPseudoNull);
}

// ---------------------------------------------------------------------
// ODBC data

RETCODE AFXAPI _AfxGetLongData(HSTMT hstmt, SQLUSMALLINT nCol, SQLSMALLINT nCType,
                               CByteArray& data, BOOL& bNull) {
  data.SetSize(0);
  bNull = FALSE;
  const int nTerm = nCType == SQL_C_WCHAR ? (int)sizeof(WCHAR) : nCType == SQL_C_CHAR ? 1 : 0;
  BYTE buf[512];
  for (;;) {
    SQLLEN nInd = 0;
    RETCODE rc = ::SQLGetData(hstmt, nCol, nCType, buf, sizeof(buf), &nInd);
    if (rc == SQL_NO_DATA) return SQL_SUCCESS;  // read it all
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return rc;
    if (nInd == SQL_NULL_DATA) {
      bNull = TRUE;
      return SQL_SUCCESS;
    }
    // A piece fills the buffer (less the terminator) when more remains.
    INT_PTR nPiece;
    if (nInd == SQL_NO_TOTAL || nInd > (SQLLEN)(sizeof(buf) - nTerm))
      nPiece = sizeof(buf) - nTerm;
    else
      nPiece = nInd;
    if (nTerm == (int)sizeof(WCHAR)) nPiece &= ~(INT_PTR)1;
    INT_PTR nOld = data.GetSize();
    data.SetSize(nOld + nPiece);
    memcpy(data.GetData() + nOld, buf, nPiece);
    if (rc == SQL_SUCCESS) return SQL_SUCCESS;
    if (nInd != SQL_NO_TOTAL && nInd <= (SQLLEN)(sizeof(buf) - nTerm)) return SQL_SUCCESS;
  }
}

void AFXAPI _AfxGetFieldData(CRecordset* pSet, HSTMT hstmt, SQLUSMALLINT nCol,
                             _AFX_RFX_FIELD& field) {
  RETCODE rc;
  BOOL bNull = FALSE;
  switch (field.nKind) {
  case _AFX_RFX_STRING:
  case _AFX_RFX_TEXTBUF:
  case _AFX_RFX_BINARY:
  case _AFX_RFX_LONGBINARY: {
    CByteArray data;
    SQLSMALLINT nCType = (field.nKind == _AFX_RFX_STRING || field.nKind == _AFX_RFX_TEXTBUF)
                             ? (SQLSMALLINT)SQL_C_TCHAR
                             : (SQLSMALLINT)SQL_C_BINARY;
    rc = _AfxGetLongData(hstmt, nCol, nCType, data, bNull);
    if (!pSet->Check(rc)) pSet->ThrowDBException(rc, hstmt);
    if (!bNull) _AfxSetFieldBytes(field, data.GetData(), data.GetSize());
    break;
  }
  case _AFX_RFX_DATE: {
    TIMESTAMP_STRUCT ts;
    memset(&ts, 0, sizeof(ts));
    SQLLEN nInd = 0;
    rc = ::SQLGetData(hstmt, nCol, SQL_C_TIMESTAMP, &ts, sizeof(ts), &nInd);
    if (!pSet->Check(rc)) pSet->ThrowDBException(rc, hstmt);
    bNull = nInd == SQL_NULL_DATA;
    if (!bNull) field.pfnFromTimestamp(field.pv, ts);
    break;
  }
  default: {
    BYTE buf[32];
    memset(buf, 0, sizeof(buf));
    SQLLEN nInd = 0;
    rc = ::SQLGetData(hstmt, nCol, field.nCType, buf, field.nSize, &nInd);
    if (!pSet->Check(rc)) pSet->ThrowDBException(rc, hstmt);
    bNull = nInd == SQL_NULL_DATA;
    if (!bNull) memcpy(field.pv, buf, field.nSize);
    break;
  }
  }
  field.bNull = bNull;
  if (bNull) _AfxSetFieldPseudoNull(field);
}

void AFXAPI _AfxBindFieldBytes(CRecordset* pSet, HSTMT hstmt, SQLUSMALLINT nParam,
                               const _AFX_RFX_FIELD& field, BOOL bNull, const CByteArray& bytes,
                               CPtrArray& rgBuffers) {
  // The buffer holds the length indicator, then the data.
  const INT_PTR nHeader = 8;
  SQLSMALLINT nCType = field.nCType;
  SQLULEN nColumnSize = field.nColumnSize;
  SQLSMALLINT nScale = field.nScale;
  INT_PTR nData = bytes.GetSize();
  CByteArray* pBuffer = new CByteArray;
  rgBuffers.Add(pBuffer);
  switch (field.nKind) {
  case _AFX_RFX_STRING:
  case _AFX_RFX_TEXTBUF:
    nCType = SQL_C_TCHAR;
    pBuffer->SetSize(nHeader + nData + sizeof(TCHAR));
    memset(pBuffer->GetData(), 0, pBuffer->GetSize());
    if (nData) memcpy(pBuffer->GetData() + nHeader, bytes.GetData(), nData);
    if (nColumnSize < (SQLULEN)(nData / sizeof(TCHAR))) nColumnSize = nData / sizeof(TCHAR);
    if (nColumnSize == 0) nColumnSize = 1;
    break;
  case _AFX_RFX_BINARY:
  case _AFX_RFX_LONGBINARY:
    nCType = SQL_C_BINARY;
    pBuffer->SetSize(nHeader + nData + 1);
    if (nData) memcpy(pBuffer->GetData() + nHeader, bytes.GetData(), nData);
    if (nColumnSize < (SQLULEN)nData) nColumnSize = nData;
    if (nColumnSize == 0) nColumnSize = 1;
    break;
  case _AFX_RFX_DATE: {
    nCType = SQL_C_TIMESTAMP;
    TIMESTAMP_STRUCT ts;
    memset(&ts, 0, sizeof(ts));
    if (!bNull && nData == field.nSize) field.pfnToTimestamp(bytes.GetData(), ts);
    nData = sizeof(ts);
    pBuffer->SetSize(nHeader + nData);
    memcpy(pBuffer->GetData() + nHeader, &ts, nData);
    nColumnSize = 19;
    nScale = 0;
    break;
  }
  default:
    pBuffer->SetSize(nHeader + 16);
    memset(pBuffer->GetData(), 0, pBuffer->GetSize());
    if (nData > 16) nData = 16;
    if (nData) memcpy(pBuffer->GetData() + nHeader, bytes.GetData(), nData);
    if (nCType == SQL_C_TIMESTAMP) {
      nColumnSize = 19;
      nScale = 0;
    }
    break;
  }
  SQLLEN* pnInd = (SQLLEN*)pBuffer->GetData();
  *pnInd = bNull ? SQL_NULL_DATA : (SQLLEN)nData;
  BYTE* pData = pBuffer->GetData() + nHeader;
  RETCODE rc = ::SQLBindParameter(hstmt, nParam, SQL_PARAM_INPUT, nCType, field.nSqlType,
                                  nColumnSize, nScale, pData,
                                  (SQLLEN)(pBuffer->GetSize() - nHeader), pnInd);
  if (!pSet->Check(rc)) pSet->ThrowDBException(rc, hstmt);
}

// ---------------------------------------------------------------------
// The RFX functions: each describes one field when the recordset opens.

static void RfxField(CFieldExchange* pFX, LPCTSTR szName, void* pv, UINT nKind,
                     SQLSMALLINT nCType, SQLSMALLINT nSqlType, int nSize, SQLULEN nColumnSize,
                     short nScale, const void* pPseudoNull, int nPseudoNull,
                     _AFX_FROM_TIMESTAMP pfnFrom = NULL, _AFX_TO_TIMESTAMP pfnTo = NULL) {
  if (pFX->m_nOperation == CFieldExchange::Name) {
    if (pFX->m_nFieldType == CFieldExchange::outputColumn && pFX->m_pstr != NULL) {
      if (!pFX->m_pstr->IsEmpty()) *pFX->m_pstr += _T(",");
      *pFX->m_pstr += szName;
    }
    return;
  }
  if (pFX->m_nOperation != CFieldExchange::AllocCache) return;
  _AFX_RFX_FIELD field;
  field.strName = szName;
  field.pv = pv;
  field.nKind = nKind;
  field.nCType = nCType;
  field.nSqlType = nSqlType;
  field.nColumnSize = nColumnSize;
  field.nScale = nScale;
  field.nSize = nSize;
  field.nParamType = pFX->m_nFieldType;
  field.bNull = FALSE;
  field.bDirty = FALSE;
  field.bNullable = TRUE;
  memset(field.rgPseudoNull, 0, sizeof(field.rgPseudoNull));
  field.nPseudoNull = nPseudoNull <= (int)sizeof(field.rgPseudoNull) ? nPseudoNull : 0;
  if (field.nPseudoNull) memcpy(field.rgPseudoNull, pPseudoNull, field.nPseudoNull);
  field.pfnFromTimestamp = pfnFrom;
  field.pfnToTimestamp = pfnTo;
  pFX->m_prs->_AddField(field, pFX->m_nFieldType != CFieldExchange::outputColumn);
}

void AFXAPI RFX_Text(CFieldExchange* pFX, LPCTSTR szName, CString& value, int nMaxLength,
                     int nColumnType, short nScale) {
  RfxField(pFX, szName, &value, _AFX_RFX_STRING, SQL_C_TCHAR, (SQLSMALLINT)nColumnType, 0,
           nMaxLength > 0 ? nMaxLength : 1, nScale, NULL, 0);
}

void AFXAPI RFX_Text(CFieldExchange* pFX, LPCTSTR szName, LPTSTR value, int nMaxLength,
                     int nColumnType, short nScale) {
  RfxField(pFX, szName, value, _AFX_RFX_TEXTBUF, SQL_C_TCHAR, (SQLSMALLINT)nColumnType,
           nMaxLength, nMaxLength > 1 ? nMaxLength - 1 : 1, nScale, NULL, 0);
}

void AFXAPI RFX_Bool(CFieldExchange* pFX, LPCTSTR szName, BOOL& value) {
  BOOL bPseudo = AFX_RFX_BOOL_PSEUDO_NULL;
  RfxField(pFX, szName, &value, _AFX_RFX_POD, SQL_C_SLONG, SQL_BIT, sizeof(BOOL), 1, 0, &bPseudo,
           sizeof(bPseudo));
}

void AFXAPI RFX_Byte(CFieldExchange* pFX, LPCTSTR szName, BYTE& value) {
  BYTE bPseudo = AFX_RFX_BYTE_PSEUDO_NULL;
  RfxField(pFX, szName, &value, _AFX_RFX_POD, SQL_C_UTINYINT, SQL_TINYINT, sizeof(BYTE), 3, 0,
           &bPseudo, sizeof(bPseudo));
}

void AFXAPI RFX_Int(CFieldExchange* pFX, LPCTSTR szName, int& value) {
  int nPseudo = AFX_RFX_INT_PSEUDO_NULL;
  RfxField(pFX, szName, &value, _AFX_RFX_POD, SQL_C_SLONG, SQL_INTEGER, sizeof(int), 10, 0,
           &nPseudo, sizeof(nPseudo));
}

void AFXAPI RFX_Long(CFieldExchange* pFX, LPCTSTR szName, long& value) {
  long lPseudo = AFX_RFX_LONG_PSEUDO_NULL;
  RfxField(pFX, szName, &value, _AFX_RFX_POD, SQL_C_SLONG, SQL_INTEGER, sizeof(long), 10, 0,
           &lPseudo, sizeof(lPseudo));
}

void AFXAPI RFX_BigInt(CFieldExchange* pFX, LPCTSTR szName, LONGLONG& value) {
  LONGLONG llPseudo = AFX_RFX_BIGINT_PSEUDO_NULL;
  RfxField(pFX, szName, &value, _AFX_RFX_POD, SQL_C_SBIGINT, SQL_BIGINT, sizeof(LONGLONG), 19, 0,
           &llPseudo, sizeof(llPseudo));
}

void AFXAPI RFX_Single(CFieldExchange* pFX, LPCTSTR szName, float& value) {
  float fPseudo = AFX_RFX_SINGLE_PSEUDO_NULL;
  RfxField(pFX, szName, &value, _AFX_RFX_POD, SQL_C_FLOAT, SQL_REAL, sizeof(float), 7, 0,
           &fPseudo, sizeof(fPseudo));
}

void AFXAPI RFX_Double(CFieldExchange* pFX, LPCTSTR szName, double& value) {
  double dPseudo = AFX_RFX_DOUBLE_PSEUDO_NULL;
  RfxField(pFX, szName, &value, _AFX_RFX_POD, SQL_C_DOUBLE, SQL_DOUBLE, sizeof(double), 15, 0,
           &dPseudo, sizeof(dPseudo));
}

void AFXAPI RFX_Date(CFieldExchange* pFX, LPCTSTR szName, TIMESTAMP_STRUCT& value) {
  TIMESTAMP_STRUCT tsPseudo;
  tsPseudo.year = tsPseudo.month = tsPseudo.day = AFX_RFX_TIMESTAMP_PSEUDO_NULL;
  tsPseudo.hour = tsPseudo.minute = tsPseudo.second = AFX_RFX_TIMESTAMP_PSEUDO_NULL;
  tsPseudo.fraction = AFX_RFX_TIMESTAMP_PSEUDO_NULL;
  RfxField(pFX, szName, &value, _AFX_RFX_POD, SQL_C_TIMESTAMP, SQL_TIMESTAMP,
           sizeof(TIMESTAMP_STRUCT), 19, 0, &tsPseudo, sizeof(tsPseudo));
}

static void CTimeFromTimestamp(void* pv, const TIMESTAMP_STRUCT& ts) {
  if (ts.year < 1970 || ts.year > 3000) {
    *(CTime*)pv = CTime();
    return;
  }
  *(CTime*)pv = CTime(ts.year, ts.month, ts.day, ts.hour, ts.minute, ts.second);
}

static void CTimeToTimestamp(const void* pv, TIMESTAMP_STRUCT& ts) {
  CTime t;
  memcpy((void*)&t, pv, sizeof(CTime));
  memset(&ts, 0, sizeof(ts));
  if (t.GetTime() == 0) return;
  ts.year = (SQLSMALLINT)t.GetYear();
  ts.month = (SQLUSMALLINT)t.GetMonth();
  ts.day = (SQLUSMALLINT)t.GetDay();
  ts.hour = (SQLUSMALLINT)t.GetHour();
  ts.minute = (SQLUSMALLINT)t.GetMinute();
  ts.second = (SQLUSMALLINT)t.GetSecond();
}

void AFXAPI RFX_Date(CFieldExchange* pFX, LPCTSTR szName, CTime& value) {
  CTime tPseudo;
  RfxField(pFX, szName, &value, _AFX_RFX_DATE, SQL_C_TIMESTAMP, SQL_TIMESTAMP, sizeof(CTime), 19,
           0, &tPseudo, sizeof(tPseudo), CTimeFromTimestamp, CTimeToTimestamp);
}

void AFXAPI RFX_Binary(CFieldExchange* pFX, LPCTSTR szName, CByteArray& value, int nMaxLength) {
  RfxField(pFX, szName, &value, _AFX_RFX_BINARY, SQL_C_BINARY, SQL_VARBINARY, 0,
           nMaxLength > 0 ? nMaxLength : 1, 0, NULL, 0);
}

void AFXAPI RFX_LongBinary(CFieldExchange* pFX, LPCTSTR szName, CLongBinary& value) {
  RfxField(pFX, szName, &value, _AFX_RFX_LONGBINARY, SQL_C_BINARY, SQL_LONGVARBINARY, 0, 1, 0,
           NULL, 0);
}

// For dbole.cpp, which adds the COleDateTime form.
void AFXAPI _AfxRfxDate(CFieldExchange* pFX, LPCTSTR szName, void* pv, int nSize,
                        const void* pPseudoNull, _AFX_FROM_TIMESTAMP pfnFrom,
                        _AFX_TO_TIMESTAMP pfnTo) {
  RfxField(pFX, szName, pv, _AFX_RFX_DATE, SQL_C_TIMESTAMP, SQL_TIMESTAMP, nSize, 19, 0,
           pPseudoNull, nSize, pfnFrom, pfnTo);
}
