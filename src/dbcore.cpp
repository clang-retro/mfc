// dbcore.cpp - CDBException, CDatabase and CRecordset.
//
// A recordset keeps every row it has fetched (a forward-only one only the
// current row), so scrolling works over any driver, and the record count
// grows as rows are reached, as with MFC. Changes are written with
// INSERT, UPDATE and DELETE statements; UPDATE and DELETE find the row by
// the values it was fetched with.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "dbimpl.h"
#include <wctype.h>

IMPLEMENT_DYNAMIC(CDBException, CException)
IMPLEMENT_DYNAMIC(CDatabase, CObject)
IMPLEMENT_DYNAMIC(CRecordset, CObject)

// ---------------------------------------------------------------------
// CDBException

static LPCTSTR AfxSqlErrorText(RETCODE nRetCode) {
  switch (nRetCode) {
  case AFX_SQL_ERROR_API_CONFORMANCE: return _T("The ODBC driver does not support a required function.");
  case AFX_SQL_ERROR_CONNECT_FAIL: return _T("Connection to the data source failed.");
  case AFX_SQL_ERROR_DATA_TRUNCATED: return _T("Data truncated.");
  case AFX_SQL_ERROR_DYNASET_NOT_SUPPORTED: return _T("Dynasets are not supported by the ODBC driver.");
  case AFX_SQL_ERROR_EMPTY_COLUMN_LIST: return _T("The recordset has no columns to select.");
  case AFX_SQL_ERROR_FIELD_SCHEMA_MISMATCH: return _T("The recordset's fields do not match the result columns.");
  case AFX_SQL_ERROR_ILLEGAL_MODE: return _T("Update called without AddNew or Edit.");
  case AFX_SQL_ERROR_MULTIPLE_ROWS_AFFECTED: return _T("More than one record was changed.");
  case AFX_SQL_ERROR_NO_CURRENT_RECORD: return _T("There is no current record.");
  case AFX_SQL_ERROR_NO_ROWS_AFFECTED: return _T("No records were changed.");
  case AFX_SQL_ERROR_RECORDSET_READONLY: return _T("The recordset is read-only.");
  case AFX_SQL_ERROR_SQL_CONFORMANCE: return _T("The ODBC driver does not support the SQL needed.");
  case AFX_SQL_ERROR_SQL_NO_TOTAL: return _T("The size of the data is not known.");
  case AFX_SQL_ERROR_RECORDSET_FORWARD_ONLY: return _T("The recordset only moves forward.");
  case AFX_SQL_ERROR_SNAPSHOT_NOT_SUPPORTED: return _T("Snapshots are not supported by the ODBC driver.");
  case AFX_SQL_ERROR_TX_NOT_SUPPORTED: return _T("The data source does not support transactions.");
  case AFX_SQL_ERROR_ROW_FETCH: return _T("A record could not be fetched.");
  case AFX_SQL_ERROR_INCORRECT_ODBC: return _T("The ODBC driver is not suitable.");
  case AFX_SQL_ERROR_UPDATE_DELETE_FAILED: return _T("The record could not be changed.");
  case AFX_SQL_ERROR_DYNAMIC_CURSOR_NOT_SUPPORTED: return _T("Dynamic cursors are not supported.");
  case AFX_SQL_ERROR_FIELD_NOT_FOUND: return _T("The field is not part of the recordset.");
  case AFX_SQL_ERROR_BOOKMARKS_NOT_SUPPORTED: return _T("Bookmarks are not supported.");
  case AFX_SQL_ERROR_BOOKMARKS_NOT_ENABLED: return _T("The recordset was not opened with bookmarks.");
  case AFX_SQL_ERROR_NO_DATA_FOUND: return _T("Moved past the beginning or end of the recordset.");
  case SQL_INVALID_HANDLE: return _T("Invalid ODBC handle.");
  }
  return NULL;
}

CDBException::CDBException(RETCODE nRetCode) : m_nRetCode(nRetCode) {}

CDBException::~CDBException() {}

BOOL CDBException::GetErrorMessage(LPTSTR lpszError, UINT nMaxError, PUINT pnHelpContext) const {
  if (pnHelpContext != NULL) *pnHelpContext = 0;
  if (lpszError == NULL || nMaxError == 0) return FALSE;
  lstrcpyn(lpszError, m_strError, nMaxError);
  return TRUE;
}

BOOL CDBException::GetErrorMessage(LPTSTR lpszError, UINT nMaxError, PUINT pnHelpContext) {
  return ((const CDBException*)this)->GetErrorMessage(lpszError, nMaxError, pnHelpContext);
}

void CDBException::BuildErrorString(CDatabase* pdb, HSTMT hstmt, BOOL) {
  m_strError.Empty();
  m_strStateNativeOrigin.Empty();
  LPCTSTR pszText = AfxSqlErrorText(m_nRetCode);
  if (pszText != NULL) {
    m_strError = pszText;
    m_strError += _T("\n");
    return;
  }
  SQLSMALLINT nType;
  SQLHANDLE h;
  if (hstmt != SQL_NULL_HSTMT) {
    nType = SQL_HANDLE_STMT;
    h = hstmt;
  } else if (pdb != NULL && pdb->m_hdbc != SQL_NULL_HDBC) {
    nType = SQL_HANDLE_DBC;
    h = pdb->m_hdbc;
  } else {
    nType = SQL_HANDLE_ENV;
    h = CDatabase::GetHENV();
  }
  for (SQLSMALLINT nRec = 1; nRec < 32; nRec++) {
    SQLTCHAR szState[8];
    SQLTCHAR szMessage[SQL_MAX_MESSAGE_LENGTH];
    SQLINTEGER nNative = 0;
    SQLSMALLINT cbMessage = 0;
    RETCODE rc = ::SQLGetDiagRec(nType, h, nRec, szState, &nNative, szMessage,
                                 SQL_MAX_MESSAGE_LENGTH, &cbMessage);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) break;
    // The message starts with its origin: "[vendor][component]".
    CString strMessage = (LPCTSTR)szMessage;
    int nOrigin = 0;
    while (nOrigin < strMessage.GetLength() && strMessage[nOrigin] == _T('[')) {
      int nClose = strMessage.Find(_T(']'), nOrigin);
      if (nClose < 0) break;
      nOrigin = nClose + 1;
    }
    CString strLine;
    strLine.Format(_T("State:%s,Native:%ld,Origin:%s\n"), (LPCTSTR)szState, (long)nNative,
                   (LPCTSTR)strMessage.Left(nOrigin));
    m_strStateNativeOrigin += strLine;
    m_strError += strMessage.Mid(nOrigin);
    m_strError += _T("\n");
  }
  if (m_strError.IsEmpty()) m_strError.Format(_T("ODBC error %d.\n"), (int)m_nRetCode);
}

void AFXAPI AfxThrowDBException(RETCODE nRetCode, CDatabase* pdb, HSTMT hstmt) {
  CDBException* pException = new CDBException(nRetCode);
  pException->BuildErrorString(pdb, hstmt);
  throw pException;
}

// ---------------------------------------------------------------------
// CDatabase

static HENV g_henv = SQL_NULL_HENV;

static void __cdecl FreeHENV() {
  if (g_henv != SQL_NULL_HENV) ::SQLFreeHandle(SQL_HANDLE_ENV, g_henv);
  g_henv = SQL_NULL_HENV;
}

HENV AFXAPI CDatabase::GetHENV() {
  AfxLockGlobals(0);
  if (g_henv == SQL_NULL_HENV) {
    SQLHANDLE h = SQL_NULL_HANDLE;
    if (SQL_SUCCEEDED(::SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &h))) {
      ::SQLSetEnvAttr(h, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
      g_henv = h;
      atexit(FreeHENV);
    }
  }
  AfxUnlockGlobals(0);
  if (g_henv == SQL_NULL_HENV) AfxThrowDBException(AFX_SQL_ERROR_INCORRECT_ODBC, NULL, NULL);
  return g_henv;
}

CDatabase::CDatabase()
    : m_hdbc(SQL_NULL_HDBC), m_dwLoginTimeout(15), m_dwQueryTimeout(15), m_bUpdatable(FALSE),
      m_bTransactions(FALSE), m_bTransactionPending(FALSE),
      m_nCursorCommitBehavior(SQL_CB_PRESERVE), m_nCursorRollbackBehavior(SQL_CB_PRESERVE),
      m_chIDQuoteChar(_T('"')) {}

CDatabase::~CDatabase() { Close(); }

BOOL CDatabase::Check(RETCODE nRetCode) const {
  return nRetCode == SQL_SUCCESS || nRetCode == SQL_SUCCESS_WITH_INFO ||
         nRetCode == SQL_NO_DATA || nRetCode == SQL_NEED_DATA;
}

BOOL CDatabase::Open(LPCTSTR lpszDSN, BOOL bExclusive, BOOL bReadOnly, LPCTSTR lpszConnect,
                     BOOL bUseCursorLib) {
  if (bExclusive) AfxThrowNotSupportedException();
  CString strConnect = lpszConnect != NULL ? lpszConnect : _T("");
  if (strConnect.Left(5).CompareNoCase(_T("ODBC;")) == 0) strConnect = strConnect.Mid(5);
  if (lpszDSN != NULL && *lpszDSN != 0) {
    CString strDSN;
    strDSN.Format(_T("DSN=%s;"), lpszDSN);
    strConnect = strDSN + strConnect;
  }
  DWORD dwOptions = 0;
  if (bReadOnly) dwOptions |= openReadOnly;
  if (bUseCursorLib) dwOptions |= useCursorLib;
  return OpenEx(_T("ODBC;") + strConnect, dwOptions);
}

BOOL CDatabase::OpenEx(LPCTSTR lpszConnectString, DWORD dwOptions) {
  if (IsOpen()) Close();
  if (dwOptions & openExclusive) AfxThrowNotSupportedException();
  CString strConnect = lpszConnectString != NULL ? lpszConnectString : _T("");
  if (strConnect.Left(5).CompareNoCase(_T("ODBC;")) == 0) strConnect = strConnect.Mid(5);

  SQLHANDLE hdbc = SQL_NULL_HANDLE;
  RETCODE rc = ::SQLAllocHandle(SQL_HANDLE_DBC, GetHENV(), &hdbc);
  if (!Check(rc)) AfxThrowDBException(rc, this, SQL_NULL_HSTMT);
  m_hdbc = hdbc;
  if (m_dwLoginTimeout != 0)
    ::SQLSetConnectAttr(m_hdbc, SQL_ATTR_LOGIN_TIMEOUT, (SQLPOINTER)(ULONG_PTR)m_dwLoginTimeout, 0);
  if (dwOptions & openReadOnly)
    ::SQLSetConnectAttr(m_hdbc, SQL_ATTR_ACCESS_MODE, (SQLPOINTER)SQL_MODE_READ_ONLY, 0);

  SQLUSMALLINT nCompletion = SQL_DRIVER_COMPLETE;
  HWND hWnd = NULL;
  if (dwOptions & noOdbcDialog) {
    nCompletion = SQL_DRIVER_NOPROMPT;
  } else {
    if (dwOptions & forceOdbcDialog) nCompletion = SQL_DRIVER_PROMPT;
    CWinApp* pApp = AfxGetApp();
    if (pApp != NULL && pApp->m_pMainWnd != NULL) hWnd = pApp->m_pMainWnd->GetSafeHwnd();
    if (hWnd == NULL) hWnd = ::GetDesktopWindow();
  }
  SQLTCHAR szOut[1024];
  szOut[0] = 0;
  SQLSMALLINT cbOut = 0;
  rc = ::SQLDriverConnect(m_hdbc, hWnd, (SQLTCHAR*)(LPCTSTR)strConnect, SQL_NTS, szOut,
                          (SQLSMALLINT)(sizeof(szOut) / sizeof(szOut[0])), &cbOut, nCompletion);
  if (rc == SQL_NO_DATA) {  // the user cancelled the dialog
    Free();
    return FALSE;
  }
  if (!SQL_SUCCEEDED(rc)) {
    CDBException* pException = new CDBException(rc);
    pException->BuildErrorString(this, SQL_NULL_HSTMT);
    Free();
    throw pException;
  }
  m_strConnect = _T("ODBC;");
  m_strConnect += (LPCTSTR)szOut;
  GetConnectInfo();
  if (dwOptions & openReadOnly) m_bUpdatable = FALSE;
  return TRUE;
}

void CDatabase::GetConnectInfo() {
  TCHAR sz[64];
  SQLSMALLINT cb = 0;
  sz[0] = 0;
  m_bUpdatable = TRUE;
  if (SQL_SUCCEEDED(::SQLGetInfo(m_hdbc, SQL_DATA_SOURCE_READ_ONLY, sz, sizeof(sz), &cb)) &&
      (sz[0] == _T('Y') || sz[0] == _T('y')))
    m_bUpdatable = FALSE;
  SQLUSMALLINT nValue = 0;
  m_bTransactions = SQL_SUCCEEDED(::SQLGetInfo(m_hdbc, SQL_TXN_CAPABLE, &nValue, sizeof(nValue), NULL)) &&
                    nValue != SQL_TC_NONE;
  nValue = SQL_CB_PRESERVE;
  if (SQL_SUCCEEDED(::SQLGetInfo(m_hdbc, SQL_CURSOR_COMMIT_BEHAVIOR, &nValue, sizeof(nValue), NULL)))
    m_nCursorCommitBehavior = nValue;
  nValue = SQL_CB_PRESERVE;
  if (SQL_SUCCEEDED(::SQLGetInfo(m_hdbc, SQL_CURSOR_ROLLBACK_BEHAVIOR, &nValue, sizeof(nValue), NULL)))
    m_nCursorRollbackBehavior = nValue;
  sz[0] = 0;
  m_chIDQuoteChar = _T('"');
  if (SQL_SUCCEEDED(::SQLGetInfo(m_hdbc, SQL_IDENTIFIER_QUOTE_CHAR, sz, sizeof(sz), &cb)))
    m_chIDQuoteChar = sz[0] != 0 ? sz[0] : _T(' ');
}

void CDatabase::Free() {
  if (m_hdbc != SQL_NULL_HDBC) ::SQLFreeHandle(SQL_HANDLE_DBC, m_hdbc);
  m_hdbc = SQL_NULL_HDBC;
}

void CDatabase::Close() {
  while (!m_listRecordsets.IsEmpty()) {
    CRecordset* pSet = (CRecordset*)m_listRecordsets.GetHead();
    pSet->Close();  // removes itself
  }
  if (m_hdbc == SQL_NULL_HDBC) return;
  if (m_bTransactionPending) Rollback();
  ::SQLDisconnect(m_hdbc);
  Free();
  m_strConnect.Empty();
  m_bUpdatable = FALSE;
  m_bTransactions = FALSE;
}

CString CDatabase::GetDatabaseName() const {
  TCHAR sz[256];
  sz[0] = 0;
  SQLSMALLINT cb = 0;
  if (m_hdbc == SQL_NULL_HDBC ||
      !SQL_SUCCEEDED(::SQLGetInfo(m_hdbc, SQL_DATABASE_NAME, sz, sizeof(sz), &cb)))
    return CString();
  return CString(sz);
}

DWORD CDatabase::GetBookmarkPersistence() const {
  SQLUINTEGER n = 0;
  if (m_hdbc != SQL_NULL_HDBC) ::SQLGetInfo(m_hdbc, SQL_BOOKMARK_PERSISTENCE, &n, sizeof(n), NULL);
  return n;
}

BOOL CDatabase::BeginTrans() {
  if (!m_bTransactions) return FALSE;
  RETCODE rc = ::SQLSetConnectAttr(m_hdbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0);
  m_bTransactionPending = SQL_SUCCEEDED(rc);
  return m_bTransactionPending;
}

static BOOL EndTrans(CDatabase* pdb, BOOL& bPending, SQLSMALLINT nCompletion) {
  if (!bPending) return FALSE;
  RETCODE rc = ::SQLEndTran(SQL_HANDLE_DBC, pdb->m_hdbc, nCompletion);
  ::SQLSetConnectAttr(pdb->m_hdbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
  bPending = FALSE;
  return SQL_SUCCEEDED(rc);
}

BOOL CDatabase::CommitTrans() { return EndTrans(this, m_bTransactionPending, SQL_COMMIT); }

BOOL CDatabase::Rollback() { return EndTrans(this, m_bTransactionPending, SQL_ROLLBACK); }

void CDatabase::ExecuteSQL(LPCTSTR lpszSQL) {
  if (!IsOpen()) AfxThrowDBException(AFX_SQL_ERROR_CONNECT_FAIL, this, SQL_NULL_HSTMT);
  SQLHANDLE hstmt = SQL_NULL_HANDLE;
  RETCODE rc = ::SQLAllocHandle(SQL_HANDLE_STMT, m_hdbc, &hstmt);
  if (!Check(rc)) AfxThrowDBException(rc, this, SQL_NULL_HSTMT);
  try {
    OnSetOptions(hstmt);
    BindParameters(hstmt);
    rc = ::SQLExecDirect(hstmt, (SQLTCHAR*)lpszSQL, SQL_NTS);
    if (!Check(rc)) AfxThrowDBException(rc, this, hstmt);
    // Move past any results, so the statement finishes.
    while (::SQLMoreResults(hstmt) == SQL_SUCCESS) {
    }
  } catch (CException*) {
    ::SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    throw;
  }
  ::SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
}

void CDatabase::Cancel() {}

void CDatabase::OnSetOptions(HSTMT hstmt) {
  if (m_dwQueryTimeout != (DWORD)-1)
    ::SQLSetStmtAttr(hstmt, SQL_ATTR_QUERY_TIMEOUT, (SQLPOINTER)(ULONG_PTR)m_dwQueryTimeout, 0);
}

void CDatabase::BindParameters(HSTMT) {}

void CDatabase::ReplaceBrackets(CString& strSQL) const {
  CString strOut;
  BOOL bInLiteral = FALSE;
  for (int i = 0; i < strSQL.GetLength(); i++) {
    TCHAR ch = strSQL[i];
    if (ch == _T('\'')) bInLiteral = !bInLiteral;
    if (!bInLiteral && (ch == _T('[') || ch == _T(']'))) {
      if (m_chIDQuoteChar != _T(' ')) strOut += m_chIDQuoteChar;
      continue;
    }
    strOut += ch;
  }
  strSQL = strOut;
}

// ---------------------------------------------------------------------
// CRecordset

CRecordset::CRecordset(CDatabase* pDatabase)
    : m_hstmt(SQL_NULL_HSTMT), m_pDatabase(pDatabase), m_nFields(0), m_nParams(0),
      m_nEditMode(noMode), m_nDefaultType(snapshot), m_nOpenType(snapshot), m_dwOptions(none),
      m_nLockMode(optimistic), m_bBOF(TRUE), m_bEOF(TRUE), m_lRecordCount(0), m_nResultCols(0),
      m_bDatabaseOwned(FALSE), m_nRowBase(0), m_nCurRow(-1), m_bAllFetched(FALSE),
      m_pEditBackup(NULL) {}

CRecordset::~CRecordset() {
  Close();
  FreeFields();
}

CString CRecordset::GetDefaultConnect() { return _T("ODBC;"); }

CString CRecordset::GetDefaultSQL() { return CString(); }

void CRecordset::DoFieldExchange(CFieldExchange*) {}

void CRecordset::OnSetOptions(HSTMT hstmt) {
  if (m_pDatabase != NULL) m_pDatabase->OnSetOptions(hstmt);
}

void CRecordset::OnSetUpdateOptions(HSTMT hstmt) {
  if (m_pDatabase != NULL) m_pDatabase->OnSetOptions(hstmt);
}

BOOL CRecordset::Check(RETCODE nRetCode) const {
  return nRetCode == SQL_SUCCESS || nRetCode == SQL_SUCCESS_WITH_INFO ||
         nRetCode == SQL_NO_DATA || nRetCode == SQL_NEED_DATA;
}

void CRecordset::ThrowDBException(RETCODE nRetCode, HSTMT hstmt) {
  AfxThrowDBException(nRetCode, m_pDatabase, hstmt);
}

void CRecordset::_AddField(const _AFX_RFX_FIELD& field, BOOL bParam) {
  _AFX_RFX_FIELD* pField = new _AFX_RFX_FIELD(field);
  if (bParam)
    m_rgParams.Add(pField);
  else
    m_rgFields.Add(pField);
}

void CRecordset::FreeFields() {
  for (INT_PTR i = 0; i < m_rgFields.GetSize(); i++) delete (_AFX_RFX_FIELD*)m_rgFields[i];
  for (INT_PTR i = 0; i < m_rgParams.GetSize(); i++) delete (_AFX_RFX_FIELD*)m_rgParams[i];
  m_rgFields.RemoveAll();
  m_rgParams.RemoveAll();
}

void CRecordset::FreeParamBuffers() {
  for (INT_PTR i = 0; i < m_rgParamBuffers.GetSize(); i++)
    delete (CByteArray*)m_rgParamBuffers[i];
  m_rgParamBuffers.RemoveAll();
}

void CRecordset::ClearRows() {
  for (INT_PTR i = 0; i < m_rgRows.GetSize(); i++) delete (_AFX_DB_ROW*)m_rgRows[i];
  m_rgRows.RemoveAll();
}

void CRecordset::PrepareFields() {
  FreeFields();
  CFieldExchange fx(CFieldExchange::AllocCache, this);
  DoFieldExchange(&fx);
  if (m_rgParamNull.GetSize() < m_rgParams.GetSize()) {
    INT_PTR nOld = m_rgParamNull.GetSize();
    m_rgParamNull.SetSize(m_rgParams.GetSize());
    for (INT_PTR i = nOld; i < m_rgParamNull.GetSize(); i++) m_rgParamNull[i] = FALSE;
  }
}

static BOOL StartsWithWord(const CString& str, LPCTSTR pszWord) {
  int n = lstrlen(pszWord);
  if (str.GetLength() < n || str.Left(n).CompareNoCase(pszWord) != 0) return FALSE;
  return str.GetLength() == n || _istspace(str[n]);
}

// The table a single-table SELECT reads, or empty.
static CString TableOfSelect(const CString& strSQL) {
  CString strUpper = strSQL;
  strUpper.MakeUpper();
  int nFrom = strUpper.Find(_T(" FROM "));
  if (nFrom < 0) return CString();
  int i = nFrom + 6;
  while (i < strSQL.GetLength() && _istspace(strSQL[i])) i++;
  int nStart = i;
  BOOL bBracket = FALSE;
  while (i < strSQL.GetLength()) {
    TCHAR ch = strSQL[i];
    if (ch == _T('[')) bBracket = TRUE;
    if (ch == _T(']')) bBracket = FALSE;
    if (!bBracket && (_istspace(ch) || ch == _T(','))) break;
    i++;
  }
  CString strTable = strSQL.Mid(nStart, i - nStart);
  while (i < strSQL.GetLength() && _istspace(strSQL[i])) i++;
  CString strRest = strUpper.Mid(i);
  if (strRest.Left(1) == _T(",") || strRest.Find(_T(" JOIN ")) >= 0 || StartsWithWord(strRest, _T("JOIN")) ||
      StartsWithWord(strRest, _T("INNER")) || StartsWithWord(strRest, _T("LEFT")) ||
      StartsWithWord(strRest, _T("RIGHT")))
    return CString();
  return strTable;
}

void CRecordset::BuildSQL(LPCTSTR lpszSQL) {
  CString strSQL = lpszSQL != NULL ? lpszSQL : _T("");
  strSQL.Trim();
  m_strTableName.Empty();
  if (strSQL.Left(1) == _T("{")) {
    m_strSQL = strSQL;  // a procedure call
  } else if (StartsWithWord(strSQL, _T("SELECT"))) {
    m_strSQL = strSQL;
    m_strTableName = TableOfSelect(strSQL);
  } else {
    m_strTableName = strSQL;
    CString strColumns;
    for (INT_PTR i = 0; i < m_rgFields.GetSize(); i++) {
      if (i) strColumns += _T(",");
      strColumns += ((_AFX_RFX_FIELD*)m_rgFields[i])->strName;
    }
    if (strColumns.IsEmpty()) ThrowDBException(AFX_SQL_ERROR_EMPTY_COLUMN_LIST);
    m_strSQL = _T("SELECT ") + strColumns + _T(" FROM ") + strSQL;
  }
  if (!m_strFilter.IsEmpty()) m_strSQL += _T(" WHERE ") + m_strFilter;
  if (!m_strSort.IsEmpty()) m_strSQL += _T(" ORDER BY ") + m_strSort;
  if (m_pDatabase != NULL) m_pDatabase->ReplaceBrackets(m_strSQL);
}

void CRecordset::BindParams(HSTMT hstmt) {
  FreeParamBuffers();
  for (INT_PTR i = 0; i < m_rgParams.GetSize(); i++) {
    const _AFX_RFX_FIELD& field = *(_AFX_RFX_FIELD*)m_rgParams[i];
    CByteArray bytes;
    _AfxGetFieldBytes(field, bytes);
    _AfxBindFieldBytes(this, hstmt, (SQLUSMALLINT)(i + 1), field, m_rgParamNull[i], bytes,
                       m_rgParamBuffers);
  }
}

// The C type a result column is read as when no field covers it.
static SQLSMALLINT DefaultCType(SQLSMALLINT nSqlType) {
  switch (nSqlType) {
  case SQL_BIT: return SQL_C_BIT;
  case SQL_TINYINT: return SQL_C_UTINYINT;
  case SQL_SMALLINT: return SQL_C_SSHORT;
  case SQL_INTEGER: return SQL_C_SLONG;
  case SQL_REAL: return SQL_C_FLOAT;
  case SQL_FLOAT:
  case SQL_DOUBLE: return SQL_C_DOUBLE;
  case SQL_DATE:
  case SQL_TIME:
  case SQL_TIMESTAMP:
  case SQL_TYPE_DATE:
  case SQL_TYPE_TIME:
  case SQL_TYPE_TIMESTAMP: return SQL_C_TIMESTAMP;
  case SQL_BINARY:
  case SQL_VARBINARY:
  case SQL_LONGVARBINARY: return SQL_C_BINARY;
  }
  return SQL_C_TCHAR;  // text, and numbers that may not fit a double
}

void CRecordset::ExecuteQuery() {
  SQLHANDLE hstmt = SQL_NULL_HANDLE;
  RETCODE rc = ::SQLAllocHandle(SQL_HANDLE_STMT, m_pDatabase->m_hdbc, &hstmt);
  if (!Check(rc)) ThrowDBException(rc);
  m_hstmt = hstmt;
  OnSetOptions(m_hstmt);
  if (m_rgParams.GetSize() > 0) {
    rc = ::SQLPrepare(m_hstmt, (SQLTCHAR*)(LPCTSTR)m_strSQL, SQL_NTS);
    if (!Check(rc)) ThrowDBException(rc, m_hstmt);
    BindParams(m_hstmt);
    rc = ::SQLExecute(m_hstmt);
  } else {
    rc = ::SQLExecDirect(m_hstmt, (SQLTCHAR*)(LPCTSTR)m_strSQL, SQL_NTS);
  }
  if (!Check(rc)) ThrowDBException(rc, m_hstmt);
  SQLSMALLINT nCols = 0;
  rc = ::SQLNumResultCols(m_hstmt, &nCols);
  if (!Check(rc)) ThrowDBException(rc, m_hstmt);
  m_nResultCols = nCols;
  if ((INT_PTR)m_nResultCols < m_rgFields.GetSize())
    ThrowDBException(AFX_SQL_ERROR_FIELD_SCHEMA_MISMATCH);
  m_rgColCType.SetSize(m_nResultCols);
  for (UINT nCol = 0; nCol < m_nResultCols; nCol++) {
    SQLTCHAR szName[128];
    SQLSMALLINT cbName = 0, nSqlType = 0, nDigits = 0, nNullable = SQL_NULLABLE_UNKNOWN;
    SQLULEN nSize = 0;
    ::SQLDescribeCol(m_hstmt, (SQLUSMALLINT)(nCol + 1), szName, 128, &cbName, &nSqlType, &nSize,
                     &nDigits, &nNullable);
    m_rgColCType[nCol] = (DWORD)(WORD)DefaultCType(nSqlType);
    if ((INT_PTR)nCol < m_rgFields.GetSize())
      ((_AFX_RFX_FIELD*)m_rgFields[nCol])->bNullable = nNullable != SQL_NO_NULLS;
  }
}

void CRecordset::SetCurrentToNull() {
  for (INT_PTR i = 0; i < m_rgFields.GetSize(); i++) {
    _AFX_RFX_FIELD& field = *(_AFX_RFX_FIELD*)m_rgFields[i];
    field.bNull = TRUE;
    field.bDirty = FALSE;
    _AfxSetFieldPseudoNull(field);
  }
}

_AFX_DB_ROW* CRecordset::SaveMembers() const {
  INT_PTR nFields = m_rgFields.GetSize();
  _AFX_DB_ROW* pRow = new _AFX_DB_ROW((int)(nFields > (INT_PTR)m_nResultCols ? nFields : m_nResultCols));
  for (INT_PTR i = 0; i < nFields; i++) {
    const _AFX_RFX_FIELD& field = *(_AFX_RFX_FIELD*)m_rgFields[i];
    pRow->pCells[i].bNull = field.bNull;
    pRow->pCells[i].nCType = field.nCType;
    _AfxGetFieldBytes(field, pRow->pCells[i].data);
  }
  return pRow;
}

void CRecordset::RestoreMembers(const _AFX_DB_ROW* pRow) {
  for (INT_PTR i = 0; i < m_rgFields.GetSize() && i < pRow->nCells; i++) {
    _AFX_RFX_FIELD& field = *(_AFX_RFX_FIELD*)m_rgFields[i];
    const _AFX_DB_CELL& cell = pRow->pCells[i];
    _AfxSetFieldBytes(field, cell.data.GetData(), cell.data.GetSize());
    field.bNull = cell.bNull;
    field.bDirty = FALSE;
  }
}

BOOL CRecordset::FetchRow() {
  RETCODE rc = ::SQLFetch(m_hstmt);
  if (rc == SQL_NO_DATA) {
    m_bAllFetched = TRUE;
    return FALSE;
  }
  if (!Check(rc)) ThrowDBException(rc, m_hstmt);
  INT_PTR nFields = m_rgFields.GetSize();
  for (INT_PTR i = 0; i < nFields; i++)
    _AfxGetFieldData(this, m_hstmt, (SQLUSMALLINT)(i + 1), *(_AFX_RFX_FIELD*)m_rgFields[i]);
  _AFX_DB_ROW* pRow = SaveMembers();
  // The columns no field covers, for GetFieldValue.
  for (UINT nCol = (UINT)nFields; nCol < m_nResultCols; nCol++) {
    _AFX_DB_CELL& cell = pRow->pCells[nCol];
    cell.nCType = (SQLSMALLINT)m_rgColCType[nCol];
    if (cell.nCType == SQL_C_TCHAR || cell.nCType == SQL_C_BINARY) {
      rc = _AfxGetLongData(m_hstmt, (SQLUSMALLINT)(nCol + 1), cell.nCType, cell.data, cell.bNull);
    } else {
      BYTE buf[32];
      memset(buf, 0, sizeof(buf));
      SQLLEN nInd = 0;
      rc = ::SQLGetData(m_hstmt, (SQLUSMALLINT)(nCol + 1), cell.nCType, buf, sizeof(buf), &nInd);
      cell.bNull = nInd == SQL_NULL_DATA;
      int nSize = cell.nCType == SQL_C_TIMESTAMP ? (int)sizeof(TIMESTAMP_STRUCT)
                  : cell.nCType == SQL_C_DOUBLE  ? (int)sizeof(double)
                  : cell.nCType == SQL_C_SSHORT  ? (int)sizeof(short)
                  : (cell.nCType == SQL_C_BIT || cell.nCType == SQL_C_UTINYINT) ? 1
                                                                                : 4;
      cell.data.SetSize(nSize);
      memcpy(cell.data.GetData(), buf, nSize);
    }
    if (!Check(rc)) {
      delete pRow;
      ThrowDBException(rc, m_hstmt);
    }
  }
  if (m_nOpenType == forwardOnly) {
    m_nRowBase += (long)m_rgRows.GetSize();
    ClearRows();
  }
  m_rgRows.Add(pRow);
  m_lRecordCount++;
  return TRUE;
}

void CRecordset::LoadRow(long nRow) { RestoreMembers((_AFX_DB_ROW*)m_rgRows[nRow - m_nRowBase]); }

BOOL CRecordset::Open(UINT nOpenType, LPCTSTR lpszSQL, DWORD dwOptions) {
  if (IsOpen()) Close();
  if (nOpenType == AFX_DB_USE_DEFAULT_TYPE) nOpenType = m_nDefaultType;
  m_nOpenType = nOpenType;
  m_dwOptions = dwOptions;
  if (m_pDatabase == NULL) {
    m_pDatabase = new CDatabase;
    m_bDatabaseOwned = TRUE;
  }
  if (!m_pDatabase->IsOpen()) {
    if (!m_pDatabase->OpenEx(GetDefaultConnect(), 0)) return FALSE;
  }
  PrepareFields();
  m_strRequerySQL = lpszSQL != NULL ? CString(lpszSQL) : GetDefaultSQL();
  BuildSQL(m_strRequerySQL);
  try {
    ExecuteQuery();
    m_pDatabase->m_listRecordsets.AddTail(this);
    m_lRecordCount = 0;
    m_nRowBase = 0;
    m_bAllFetched = FALSE;
    ClearRows();
    if (FetchRow()) {
      m_nCurRow = 0;
      m_bBOF = m_bEOF = FALSE;
    } else {
      m_nCurRow = -1;
      m_bBOF = m_bEOF = TRUE;
      SetCurrentToNull();
    }
  } catch (CException*) {
    POSITION pos = m_pDatabase->m_listRecordsets.Find(this);
    if (pos != NULL) m_pDatabase->m_listRecordsets.RemoveAt(pos);
    if (m_hstmt != SQL_NULL_HSTMT) ::SQLFreeHandle(SQL_HANDLE_STMT, m_hstmt);
    m_hstmt = SQL_NULL_HSTMT;
    throw;
  }
  return TRUE;
}

void CRecordset::Close() {
  if (m_pEditBackup != NULL) {
    delete m_pEditBackup;
    m_pEditBackup = NULL;
  }
  m_nEditMode = noMode;
  if (m_hstmt != SQL_NULL_HSTMT) {
    ::SQLFreeHandle(SQL_HANDLE_STMT, m_hstmt);
    m_hstmt = SQL_NULL_HSTMT;
  }
  if (m_pDatabase != NULL) {
    POSITION pos = m_pDatabase->m_listRecordsets.Find(this);
    if (pos != NULL) m_pDatabase->m_listRecordsets.RemoveAt(pos);
  }
  ClearRows();
  FreeParamBuffers();
  m_bBOF = m_bEOF = TRUE;
  m_nCurRow = -1;
  m_lRecordCount = 0;
  m_nResultCols = 0;
  if (m_bDatabaseOwned) {
    CDatabase* pDatabase = m_pDatabase;
    m_pDatabase = NULL;
    m_bDatabaseOwned = FALSE;
    delete pDatabase;
  }
}

BOOL CRecordset::Requery() {
  if (!IsOpen()) return FALSE;
  if (m_nEditMode != noMode) CancelUpdate();
  ::SQLFreeHandle(SQL_HANDLE_STMT, m_hstmt);
  m_hstmt = SQL_NULL_HSTMT;
  BuildSQL(m_strRequerySQL);
  ExecuteQuery();
  ClearRows();
  m_lRecordCount = 0;
  m_nRowBase = 0;
  m_bAllFetched = FALSE;
  if (FetchRow()) {
    m_nCurRow = 0;
    m_bBOF = m_bEOF = FALSE;
  } else {
    m_nCurRow = -1;
    m_bBOF = m_bEOF = TRUE;
    SetCurrentToNull();
  }
  return TRUE;
}

BOOL CRecordset::FlushResultSet() {
  if (!IsOpen()) return FALSE;
  RETCODE rc = ::SQLMoreResults(m_hstmt);
  if (rc == SQL_NO_DATA) return FALSE;
  if (!Check(rc)) ThrowDBException(rc, m_hstmt);
  SQLSMALLINT nCols = 0;
  ::SQLNumResultCols(m_hstmt, &nCols);
  m_nResultCols = nCols;
  ClearRows();
  m_lRecordCount = 0;
  m_nRowBase = 0;
  m_bAllFetched = FALSE;
  m_nCurRow = -1;
  m_bBOF = m_bEOF = FALSE;
  return TRUE;
}

void CRecordset::Cancel() {
  if (m_hstmt != SQL_NULL_HSTMT) ::SQLCancel(m_hstmt);
}

void CRecordset::Move(long nRows, WORD wFetchType) {
  if (!IsOpen()) ThrowDBException(AFX_SQL_ERROR_NO_CURRENT_RECORD);
  if (m_nEditMode != noMode) CancelUpdate();
  if (nRows == 0 && wFetchType == SQL_FETCH_RELATIVE) {
    if (m_nCurRow >= 0 && m_nCurRow < m_lRecordCount) LoadRow(m_nCurRow);
    return;
  }
  long nTarget;
  long nStep = 1;
  switch (wFetchType) {
  case SQL_FETCH_FIRST:
    nTarget = 0;
    break;
  case SQL_FETCH_LAST:
    if (m_nOpenType == forwardOnly) ThrowDBException(AFX_SQL_ERROR_RECORDSET_FORWARD_ONLY);
    while (!m_bAllFetched && FetchRow()) {
    }
    nTarget = m_lRecordCount - 1;
    nStep = -1;
    break;
  case SQL_FETCH_ABSOLUTE:
    if (nRows > 0) {
      nTarget = nRows - 1;
    } else {
      while (!m_bAllFetched && FetchRow()) {
      }
      nTarget = m_lRecordCount + nRows;
      nStep = -1;
    }
    break;
  case SQL_FETCH_PRIOR:
    nRows = -1;
    // fall through
  default:
    if (wFetchType == SQL_FETCH_NEXT) nRows = 1;
    if (nRows > 0 && m_bEOF) ThrowDBException(AFX_SQL_ERROR_NO_DATA_FOUND);
    if (nRows < 0 && m_bBOF) ThrowDBException(AFX_SQL_ERROR_NO_DATA_FOUND);
    nTarget = m_nCurRow + nRows;
    if (nRows < 0) nStep = -1;
    break;
  }
  if (m_nOpenType == forwardOnly && nTarget < m_nCurRow)
    ThrowDBException(AFX_SQL_ERROR_RECORDSET_FORWARD_ONLY);
  for (;;) {
    while (nTarget >= m_lRecordCount && !m_bAllFetched && FetchRow()) {
    }
    if (nTarget < 0 || nTarget >= m_lRecordCount) break;
    if (!(m_dwOptions & skipDeletedRecords) || m_nOpenType == forwardOnly) break;
    if (!((_AFX_DB_ROW*)m_rgRows[nTarget - m_nRowBase])->bDeleted) break;
    nTarget += nStep;
  }
  if (nTarget < 0) {
    m_nCurRow = -1;
    m_bBOF = TRUE;
    m_bEOF = m_lRecordCount == 0;
    SetCurrentToNull();
  } else if (nTarget >= m_lRecordCount) {
    m_nCurRow = m_lRecordCount;
    m_bEOF = TRUE;
    m_bBOF = m_lRecordCount == 0;
    SetCurrentToNull();
  } else {
    m_nCurRow = nTarget;
    m_bBOF = m_bEOF = FALSE;
    LoadRow(m_nCurRow);
  }
}

void CRecordset::SetAbsolutePosition(long nRows) { Move(nRows, SQL_FETCH_ABSOLUTE); }

void CRecordset::GetBookmark(CDBVariant& varBookmark) {
  if (!(m_dwOptions & useBookmarks)) ThrowDBException(AFX_SQL_ERROR_BOOKMARKS_NOT_ENABLED);
  CheckCurrentRecord();
  varBookmark.Clear();
  varBookmark.m_dwType = DBVT_LONG;
  varBookmark.m_lVal = m_nCurRow + 1;
}

void CRecordset::SetBookmark(const CDBVariant& varBookmark) {
  if (!(m_dwOptions & useBookmarks)) ThrowDBException(AFX_SQL_ERROR_BOOKMARKS_NOT_ENABLED);
  if (varBookmark.m_dwType != DBVT_LONG) ThrowDBException(AFX_SQL_ERROR_BOOKMARKS_NOT_SUPPORTED);
  Move(varBookmark.m_lVal, SQL_FETCH_ABSOLUTE);
}

void CRecordset::GetStatus(CRecordsetStatus& rStatus) const {
  rStatus.m_lCurrentRecord = (m_bBOF || m_bEOF) ? -1 : m_nCurRow;
  rStatus.m_bRecordCountFinal = m_bAllFetched;
}

BOOL CRecordset::IsDeleted() const {
  if (m_bBOF || m_bEOF || m_nCurRow < m_nRowBase || m_nCurRow - m_nRowBase >= m_rgRows.GetSize())
    return FALSE;
  return ((_AFX_DB_ROW*)m_rgRows[m_nCurRow - m_nRowBase])->bDeleted;
}

BOOL CRecordset::CanUpdate() const {
  return IsOpen() && m_pDatabase->CanUpdate() && !m_strTableName.IsEmpty() &&
         m_nOpenType != forwardOnly && !(m_dwOptions & (readOnly | appendOnly));
}

BOOL CRecordset::CanAppend() const {
  return IsOpen() && m_pDatabase->CanUpdate() && !m_strTableName.IsEmpty() &&
         m_nOpenType != forwardOnly && !(m_dwOptions & readOnly);
}

void CRecordset::CheckCurrentRecord() const {
  if (!IsOpen() || m_bBOF || m_bEOF || IsDeleted())
    AfxThrowDBException(AFX_SQL_ERROR_NO_CURRENT_RECORD, m_pDatabase, SQL_NULL_HSTMT);
}

void CRecordset::ThrowIfReadOnly() const {
  if (!CanUpdate()) AfxThrowDBException(AFX_SQL_ERROR_RECORDSET_READONLY, m_pDatabase, SQL_NULL_HSTMT);
}

int CRecordset::FindField(void* pv) const {
  for (INT_PTR i = 0; i < m_rgFields.GetSize(); i++)
    if (((_AFX_RFX_FIELD*)m_rgFields[i])->pv == pv) return (int)i;
  AfxThrowDBException(AFX_SQL_ERROR_FIELD_NOT_FOUND, m_pDatabase, SQL_NULL_HSTMT);
  return -1;
}

BOOL CRecordset::IsFieldDirty(void* pv) {
  if (m_nEditMode != noMode) MarkDirtyFields(m_pEditBackup);
  if (pv == NULL) {
    for (INT_PTR i = 0; i < m_rgFields.GetSize(); i++)
      if (((_AFX_RFX_FIELD*)m_rgFields[i])->bDirty) return TRUE;
    return FALSE;
  }
  return ((_AFX_RFX_FIELD*)m_rgFields[FindField(pv)])->bDirty;
}

BOOL CRecordset::IsFieldNull(void* pv) {
  if (m_nEditMode != noMode) MarkDirtyFields(m_pEditBackup);
  if (pv == NULL) {
    for (INT_PTR i = 0; i < m_rgFields.GetSize(); i++)
      if (((_AFX_RFX_FIELD*)m_rgFields[i])->bNull) return TRUE;
    return FALSE;
  }
  return ((_AFX_RFX_FIELD*)m_rgFields[FindField(pv)])->bNull;
}

BOOL CRecordset::IsFieldNullable(void* pv) {
  if (pv == NULL) return TRUE;
  return ((_AFX_RFX_FIELD*)m_rgFields[FindField(pv)])->bNullable;
}

void CRecordset::SetFieldDirty(void* pv, BOOL bDirty) {
  for (INT_PTR i = 0; i < m_rgFields.GetSize(); i++) {
    _AFX_RFX_FIELD& field = *(_AFX_RFX_FIELD*)m_rgFields[i];
    if (pv == NULL || field.pv == pv) field.bDirty = bDirty;
  }
  if (pv != NULL) FindField(pv);
}

void CRecordset::SetFieldNull(void* pv, BOOL bNull) {
  if (pv != NULL) FindField(pv);
  for (INT_PTR i = 0; i < m_rgFields.GetSize(); i++) {
    _AFX_RFX_FIELD& field = *(_AFX_RFX_FIELD*)m_rgFields[i];
    if (pv != NULL && field.pv != pv) continue;
    if (bNull) _AfxSetFieldPseudoNull(field);
    field.bNull = bNull;
    field.bDirty = TRUE;
  }
}

void CRecordset::SetParamNull(int nIndex, BOOL bNull) {
  if (nIndex < 0) return;
  if (nIndex >= m_rgParamNull.GetSize()) {
    INT_PTR nOld = m_rgParamNull.GetSize();
    m_rgParamNull.SetSize(nIndex + 1);
    for (INT_PTR i = nOld; i < m_rgParamNull.GetSize(); i++) m_rgParamNull[i] = FALSE;
  }
  m_rgParamNull[nIndex] = (BYTE)bNull;
}

// Marks the fields whose members changed since pOriginal (or since AddNew
// set them to null). Returns TRUE if any field is dirty.
BOOL CRecordset::MarkDirtyFields(const _AFX_DB_ROW* pOriginal) {
  BOOL bAny = FALSE;
  for (INT_PTR i = 0; i < m_rgFields.GetSize(); i++) {
    _AFX_RFX_FIELD& field = *(_AFX_RFX_FIELD*)m_rgFields[i];
    CByteArray bytes;
    _AfxGetFieldBytes(field, bytes);
    BOOL bChanged;
    if (m_nEditMode == addnew || pOriginal == NULL) {
      bChanged = bytes.GetSize() != field.nPseudoNull ||
                 (field.nPseudoNull && memcmp(bytes.GetData(), field.rgPseudoNull, field.nPseudoNull) != 0);
    } else {
      const CByteArray& original = pOriginal->pCells[i].data;
      bChanged = bytes.GetSize() != original.GetSize() ||
                 (bytes.GetSize() && memcmp(bytes.GetData(), original.GetData(), bytes.GetSize()) != 0);
    }
    if (bChanged) {
      field.bDirty = TRUE;
      BOOL bPseudo = bytes.GetSize() == field.nPseudoNull &&
                     (!field.nPseudoNull || memcmp(bytes.GetData(), field.rgPseudoNull, field.nPseudoNull) == 0);
      if (!bPseudo) field.bNull = FALSE;
    }
    if ((m_dwOptions & noDirtyFieldCheck) && m_nEditMode == edit) field.bDirty = TRUE;
    if (field.bDirty) bAny = TRUE;
  }
  return bAny;
}

void CRecordset::Edit() {
  CheckCurrentRecord();
  ThrowIfReadOnly();
  if (m_nEditMode != noMode) CancelUpdate();
  m_pEditBackup = SaveMembers();
  for (INT_PTR i = 0; i < m_rgFields.GetSize(); i++) ((_AFX_RFX_FIELD*)m_rgFields[i])->bDirty = FALSE;
  m_nEditMode = edit;
}

void CRecordset::AddNew() {
  if (!CanAppend()) ThrowDBException(AFX_SQL_ERROR_RECORDSET_READONLY);
  if (m_nEditMode != noMode) CancelUpdate();
  m_pEditBackup = SaveMembers();
  for (INT_PTR i = 0; i < m_rgFields.GetSize(); i++) {
    _AFX_RFX_FIELD& field = *(_AFX_RFX_FIELD*)m_rgFields[i];
    _AfxSetFieldPseudoNull(field);
    field.bNull = TRUE;
    field.bDirty = FALSE;
  }
  m_nEditMode = addnew;
}

void CRecordset::CancelUpdate() {
  if (m_nEditMode == noMode) return;
  if (m_pEditBackup != NULL) {
    RestoreMembers(m_pEditBackup);
    delete m_pEditBackup;
    m_pEditBackup = NULL;
  }
  m_nEditMode = noMode;
}

// Appends " WHERE ..." matching the current record as it was fetched, and
// lists the fields whose fetched values are parameters.
void CRecordset::AppendWhereCurrent(CString& strSQL, CPtrArray& rgBind, CByteArray& rgBindRow) const {
  const _AFX_DB_ROW* pRow = (_AFX_DB_ROW*)m_rgRows[m_nCurRow - m_nRowBase];
  CString strWhere;
  for (INT_PTR i = 0; i < m_rgFields.GetSize(); i++) {
    _AFX_RFX_FIELD* pField = (_AFX_RFX_FIELD*)m_rgFields[i];
    // Long data cannot be compared in most databases.
    if (pField->nKind == _AFX_RFX_BINARY || pField->nKind == _AFX_RFX_LONGBINARY ||
        pField->nSqlType == SQL_LONGVARCHAR || pField->nSqlType == SQL_WLONGVARCHAR)
      continue;
    if (!strWhere.IsEmpty()) strWhere += _T(" AND ");
    strWhere += pField->strName;
    if (pRow->pCells[i].bNull) {
      strWhere += _T(" IS NULL");
    } else {
      strWhere += _T("=?");
      rgBind.Add(pField);
      rgBindRow.Add(1);  // from the fetched row
    }
  }
  if (strWhere.IsEmpty()) AfxThrowDBException(AFX_SQL_ERROR_UPDATE_DELETE_FAILED, m_pDatabase, SQL_NULL_HSTMT);
  strSQL += _T(" WHERE ") + strWhere;
}

void CRecordset::ExecuteUpdateSQL(const CString& strSQLIn, const CPtrArray& rgBind,
                                  const CByteArray& rgBindRow, const _AFX_DB_ROW* pRow) {
  CString strSQL = strSQLIn;
  m_pDatabase->ReplaceBrackets(strSQL);
  SQLHANDLE hstmt = SQL_NULL_HANDLE;
  RETCODE rc = ::SQLAllocHandle(SQL_HANDLE_STMT, m_pDatabase->m_hdbc, &hstmt);
  if (!Check(rc)) ThrowDBException(rc);
  CPtrArray rgBuffers;
  SQLLEN nRows = 0;
  try {
    OnSetUpdateOptions(hstmt);
    rc = ::SQLPrepare(hstmt, (SQLTCHAR*)(LPCTSTR)strSQL, SQL_NTS);
    if (!Check(rc)) ThrowDBException(rc, hstmt);
    for (INT_PTR i = 0; i < rgBind.GetSize(); i++) {
      const _AFX_RFX_FIELD& field = *(_AFX_RFX_FIELD*)rgBind[i];
      if (rgBindRow[i] && pRow != NULL) {
        INT_PTR nField = 0;
        while (nField < m_rgFields.GetSize() && m_rgFields[nField] != rgBind[i]) nField++;
        const _AFX_DB_CELL& cell = pRow->pCells[nField];
        _AfxBindFieldBytes(this, hstmt, (SQLUSMALLINT)(i + 1), field, cell.bNull, cell.data, rgBuffers);
      } else {
        CByteArray bytes;
        _AfxGetFieldBytes(field, bytes);
        _AfxBindFieldBytes(this, hstmt, (SQLUSMALLINT)(i + 1), field, field.bNull, bytes, rgBuffers);
      }
    }
    rc = ::SQLExecute(hstmt);
    if (rc == SQL_NO_DATA) {
      nRows = 0;
    } else {
      if (!Check(rc)) ThrowDBException(rc, hstmt);
      if (!SQL_SUCCEEDED(::SQLRowCount(hstmt, &nRows))) nRows = 1;
    }
  } catch (CException*) {
    ::SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    for (INT_PTR i = 0; i < rgBuffers.GetSize(); i++) delete (CByteArray*)rgBuffers[i];
    throw;
  }
  ::SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
  for (INT_PTR i = 0; i < rgBuffers.GetSize(); i++) delete (CByteArray*)rgBuffers[i];
  if (nRows == 0) ThrowDBException(AFX_SQL_ERROR_NO_ROWS_AFFECTED);
  if (nRows > 1) ThrowDBException(AFX_SQL_ERROR_MULTIPLE_ROWS_AFFECTED);
}

BOOL CRecordset::Update() {
  if (m_nEditMode == noMode) ThrowDBException(AFX_SQL_ERROR_ILLEGAL_MODE);
  if (!MarkDirtyFields(m_pEditBackup)) {
    CancelUpdate();
    return FALSE;
  }
  CString strSQL;
  CPtrArray rgBind;
  CByteArray rgBindRow;
  if (m_nEditMode == addnew) {
    CString strColumns, strValues;
    for (INT_PTR i = 0; i < m_rgFields.GetSize(); i++) {
      _AFX_RFX_FIELD* pField = (_AFX_RFX_FIELD*)m_rgFields[i];
      if (!pField->bDirty) continue;
      if (!strColumns.IsEmpty()) {
        strColumns += _T(",");
        strValues += _T(",");
      }
      strColumns += pField->strName;
      strValues += _T("?");
      rgBind.Add(pField);
      rgBindRow.Add(0);
    }
    strSQL = _T("INSERT INTO ") + m_strTableName + _T(" (") + strColumns + _T(") VALUES (") + strValues + _T(")");
    ExecuteUpdateSQL(strSQL, rgBind, rgBindRow, NULL);
    // The new record is not part of the recordset until Requery; the
    // record that was current before AddNew is current again.
    CancelUpdate();
    return TRUE;
  }
  CString strSet;
  for (INT_PTR i = 0; i < m_rgFields.GetSize(); i++) {
    _AFX_RFX_FIELD* pField = (_AFX_RFX_FIELD*)m_rgFields[i];
    if (!pField->bDirty) continue;
    if (!strSet.IsEmpty()) strSet += _T(",");
    strSet += pField->strName + _T("=?");
    rgBind.Add(pField);
    rgBindRow.Add(0);
  }
  strSQL = _T("UPDATE ") + m_strTableName + _T(" SET ") + strSet;
  AppendWhereCurrent(strSQL, rgBind, rgBindRow);
  ExecuteUpdateSQL(strSQL, rgBind, rgBindRow, (_AFX_DB_ROW*)m_rgRows[m_nCurRow - m_nRowBase]);
  // The fetched row now holds the new values.
  _AFX_DB_ROW* pNew = SaveMembers();
  _AFX_DB_ROW*& pOld = (_AFX_DB_ROW*&)m_rgRows.ElementAt(m_nCurRow - m_nRowBase);
  for (int i = (int)m_rgFields.GetSize(); i < pOld->nCells && i < pNew->nCells; i++) {
    pNew->pCells[i].bNull = pOld->pCells[i].bNull;
    pNew->pCells[i].nCType = pOld->pCells[i].nCType;
    pNew->pCells[i].data.Copy(pOld->pCells[i].data);
  }
  delete pOld;
  pOld = pNew;
  delete m_pEditBackup;
  m_pEditBackup = NULL;
  for (INT_PTR i = 0; i < m_rgFields.GetSize(); i++) ((_AFX_RFX_FIELD*)m_rgFields[i])->bDirty = FALSE;
  m_nEditMode = noMode;
  return TRUE;
}

void CRecordset::Delete() {
  if (m_nEditMode != noMode) CancelUpdate();
  CheckCurrentRecord();
  ThrowIfReadOnly();
  CString strSQL = _T("DELETE FROM ") + m_strTableName;
  CPtrArray rgBind;
  CByteArray rgBindRow;
  AppendWhereCurrent(strSQL, rgBind, rgBindRow);
  ExecuteUpdateSQL(strSQL, rgBind, rgBindRow, (_AFX_DB_ROW*)m_rgRows[m_nCurRow - m_nRowBase]);
  ((_AFX_DB_ROW*)m_rgRows[m_nCurRow - m_nRowBase])->bDeleted = TRUE;
  SetCurrentToNull();
}

// ---------------------------------------------------------------------
// Field information and values by column

void CRecordset::GetODBCFieldInfo(short nIndex, CODBCFieldInfo& fieldinfo) {
  if (!IsOpen() || nIndex < 0 || (UINT)nIndex >= m_nResultCols)
    ThrowDBException(AFX_SQL_ERROR_FIELD_NOT_FOUND);
  SQLTCHAR szName[256];
  SQLSMALLINT cbName = 0, nSqlType = 0, nScale = 0, nNullable = 0;
  SQLULEN nPrecision = 0;
  RETCODE rc = ::SQLDescribeCol(m_hstmt, (SQLUSMALLINT)(nIndex + 1), szName, 256, &cbName,
                                &nSqlType, &nPrecision, &nScale, &nNullable);
  if (!Check(rc)) ThrowDBException(rc, m_hstmt);
  fieldinfo.m_strName = (LPCTSTR)szName;
  fieldinfo.m_nSQLType = nSqlType;
  fieldinfo.m_nPrecision = (UDWORD)nPrecision;
  fieldinfo.m_nScale = nScale;
  fieldinfo.m_nNullability = nNullable;
}

short CRecordset::GetFieldIndexByName(LPCTSTR lpszFieldName) {
  for (short i = 0; (UINT)i < m_nResultCols; i++) {
    CODBCFieldInfo info;
    GetODBCFieldInfo(i, info);
    if (info.m_strName.CompareNoCase(lpszFieldName) == 0) return i;
  }
  ThrowDBException(AFX_SQL_ERROR_FIELD_NOT_FOUND);
  return -1;
}

void CRecordset::GetODBCFieldInfo(LPCTSTR lpszName, CODBCFieldInfo& fieldinfo) {
  GetODBCFieldInfo(GetFieldIndexByName(lpszName), fieldinfo);
}

static void SetVariantString(CDBVariant& var, const CString& str) {
  var.m_dwType = DBVT_STRING;
  var.m_pstring = new CString(str);
}

static CString FormatTimestamp(const TIMESTAMP_STRUCT& ts) {
  CString str;
  str.Format(_T("%04d-%02d-%02d %02d:%02d:%02d"), ts.year, ts.month, ts.day, ts.hour, ts.minute,
             ts.second);
  return str;
}

// A value in its C form as a variant.
static void VariantFromC(CDBVariant& var, SQLSMALLINT nCType, const BYTE* p, INT_PTR n) {
  switch (nCType) {
  case SQL_C_BIT:
    var.m_dwType = DBVT_BOOL;
    var.m_boolVal = n > 0 && p[0] != 0;
    break;
  case SQL_C_UTINYINT:
    var.m_dwType = DBVT_UCHAR;
    var.m_chVal = n > 0 ? p[0] : 0;
    break;
  case SQL_C_SSHORT:
    var.m_dwType = DBVT_SHORT;
    memcpy(&var.m_iVal, p, sizeof(short));
    break;
  case SQL_C_SLONG:
    var.m_dwType = DBVT_LONG;
    memcpy(&var.m_lVal, p, sizeof(long));
    break;
  case SQL_C_FLOAT:
    var.m_dwType = DBVT_SINGLE;
    memcpy(&var.m_fltVal, p, sizeof(float));
    break;
  case SQL_C_DOUBLE:
    var.m_dwType = DBVT_DOUBLE;
    memcpy(&var.m_dblVal, p, sizeof(double));
    break;
  case SQL_C_TIMESTAMP:
    var.m_dwType = DBVT_DATE;
    var.m_pdate = new TIMESTAMP_STRUCT;
    memcpy(var.m_pdate, p, sizeof(TIMESTAMP_STRUCT));
    break;
  case SQL_C_SBIGINT: {
    LONGLONG ll;
    memcpy(&ll, p, sizeof(ll));
    CString str;
    str.Format(_T("%lld"), ll);
    SetVariantString(var, str);
    break;
  }
  case SQL_C_BINARY: {
    var.m_dwType = DBVT_BINARY;
    var.m_pbinary = new CLongBinary;
    var.m_pbinary->m_hData = ::GlobalAlloc(GMEM_MOVEABLE, n ? n : 1);
    if (n) {
      void* pDest = ::GlobalLock(var.m_pbinary->m_hData);
      memcpy(pDest, p, n);
      ::GlobalUnlock(var.m_pbinary->m_hData);
    }
    var.m_pbinary->m_dwDataLength = (DWORD_PTR)n;
    break;
  }
  default:
    SetVariantString(var, n ? CString((LPCTSTR)p, (int)(n / sizeof(TCHAR))) : CString());
    break;
  }
}

static CString StringFromVariant(const CDBVariant& var) {
  CString str;
  switch (var.m_dwType) {
  case DBVT_BOOL: str = var.m_boolVal ? _T("1") : _T("0"); break;
  case DBVT_UCHAR: str.Format(_T("%u"), var.m_chVal); break;
  case DBVT_SHORT: str.Format(_T("%d"), var.m_iVal); break;
  case DBVT_LONG: str.Format(_T("%ld"), var.m_lVal); break;
  case DBVT_SINGLE: str.Format(_T("%g"), var.m_fltVal); break;
  case DBVT_DOUBLE: str.Format(_T("%.15g"), var.m_dblVal); break;
  case DBVT_DATE: str = FormatTimestamp(*var.m_pdate); break;
  case DBVT_STRING: str = *var.m_pstring; break;
  case DBVT_BINARY: {
    const BYTE* p = (const BYTE*)::GlobalLock(var.m_pbinary->m_hData);
    for (DWORD_PTR i = 0; i < var.m_pbinary->m_dwDataLength; i++) {
      CString strByte;
      strByte.Format(_T("%02X"), p[i]);
      str += strByte;
    }
    ::GlobalUnlock(var.m_pbinary->m_hData);
    break;
  }
  }
  return str;
}

void CRecordset::GetFieldValue(short nIndex, CDBVariant& varValue, short nFieldType) {
  varValue.Clear();
  if (!IsOpen() || nIndex < 0 || (UINT)nIndex >= m_nResultCols)
    ThrowDBException(AFX_SQL_ERROR_FIELD_NOT_FOUND);
  CheckCurrentRecord();
  const _AFX_DB_ROW* pRow = (_AFX_DB_ROW*)m_rgRows[m_nCurRow - m_nRowBase];
  if (nIndex < m_rgFields.GetSize()) {
    // A field's member holds the value (perhaps edited since the fetch).
    const _AFX_RFX_FIELD& field = *(_AFX_RFX_FIELD*)m_rgFields[nIndex];
    if (!field.bNull) {
      CByteArray bytes;
      _AfxGetFieldBytes(field, bytes);
      if (field.nKind == _AFX_RFX_DATE) {
        TIMESTAMP_STRUCT ts;
        field.pfnToTimestamp(bytes.GetData(), ts);
        VariantFromC(varValue, SQL_C_TIMESTAMP, (const BYTE*)&ts, sizeof(ts));
      } else if (field.nKind == _AFX_RFX_POD && field.nSqlType == SQL_BIT) {
        varValue.m_dwType = DBVT_BOOL;
        varValue.m_boolVal = *(const BOOL*)bytes.GetData() != 0;
      } else {
        VariantFromC(varValue, field.nCType, bytes.GetData(), bytes.GetSize());
      }
    }
  } else {
    const _AFX_DB_CELL& cell = pRow->pCells[nIndex];
    if (!cell.bNull) VariantFromC(varValue, cell.nCType, cell.data.GetData(), cell.data.GetSize());
  }
  if (varValue.m_dwType != DBVT_NULL &&
      (nFieldType == SQL_C_CHAR || nFieldType == SQL_C_WCHAR) && varValue.m_dwType != DBVT_STRING) {
    CString str = StringFromVariant(varValue);
    varValue.Clear();
    SetVariantString(varValue, str);
  }
}

void CRecordset::GetFieldValue(LPCTSTR lpszName, CDBVariant& varValue, short nFieldType) {
  GetFieldValue(GetFieldIndexByName(lpszName), varValue, nFieldType);
}

void CRecordset::GetFieldValue(short nIndex, CString& strValue) {
  CDBVariant var;
  GetFieldValue(nIndex, var);
  strValue = StringFromVariant(var);
}

void CRecordset::GetFieldValue(LPCTSTR lpszName, CString& strValue) {
  GetFieldValue(GetFieldIndexByName(lpszName), strValue);
}
