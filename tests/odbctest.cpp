// odbctest.cpp - console tests for the ODBC classes against a real
// driver: the system's driver manager (unixODBC or iODBC) with the SQLite 3
// driver, through the port's ODBC layer.
//
// Without a driver manager or the driver the test notes that and passes,
// unless MFC_TEST_ODBC=1 says they are there (as on CI).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <afx.h>
#include <afxwin.h>
#include <afxdb.h>

#include <stdlib.h>
#include <string.h>
#include <string>
#include <unistd.h>

static int g_failures = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      g_failures++;                                                   \
    }                                                                 \
  } while (0)

// Text outside ASCII, to see it go in and come back whole.
static const wchar_t kWide[] = L"Zoë 日本 €";
#ifdef _UNICODE
#define WIDE_T L"Zo\u00eb \u65e5\u672c \u20ac"
#else
// The narrow build's text is UTF-8.
#define WIDE_T "Zo\xc3\xab \xe6\x97\xa5\xe6\x9c\xac \xe2\x82\xac"
#endif

class CPeopleSet : public CRecordset {
public:
  long m_ID;
  CString m_Name;
  long m_Age;
  double m_Score;
  long m_paramAge;

  CPeopleSet(CDatabase* pdb) : CRecordset(pdb), m_ID(0), m_Age(0), m_Score(0), m_paramAge(0) {
    m_nFields = 4;
    m_nDefaultType = snapshot;
  }
  virtual CString GetDefaultSQL() { return _T("people"); }
  virtual void DoFieldExchange(CFieldExchange* pFX) {
    pFX->SetFieldType(CFieldExchange::outputColumn);
    RFX_Long(pFX, _T("id"), m_ID);
    RFX_Text(pFX, _T("name"), m_Name);
    RFX_Long(pFX, _T("age"), m_Age);
    RFX_Double(pFX, _T("score"), m_Score);
    if (m_nParams) {
      pFX->SetFieldType(CFieldExchange::param);
      RFX_Long(pFX, _T("minAge"), m_paramAge);
    }
  }
};

// The driver: SQLite3 as the driver manager knows it, or what
// MFC_TEST_ODBC_DRIVER names (a driver's library, where none is installed).
static const char* Driver() {
  const char* s = getenv("MFC_TEST_ODBC_DRIVER");
  return s && *s ? s : "SQLite3";
}

static bool Required() {
  const char* s = getenv("MFC_TEST_ODBC");
  return s && *s == '1';
}

static bool Skip(const char* why) {
  if (Required()) {
    printf("FAIL: %s\n", why);
    g_failures++;
    return false;
  }
  printf("note: %s; skipping\n", why);
  return true;
}

// The raw API, wide: text in, text out in pieces.
static void TestWideApi(const char* path) {
  SQLHENV env = SQL_NULL_HENV;
  SQLHDBC dbc = SQL_NULL_HDBC;
  SQLHSTMT st = SQL_NULL_HSTMT;
  CHECK(SQL_SUCCEEDED(::SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env)));
  CHECK(SQL_SUCCEEDED(::SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0)));
  CHECK(SQL_SUCCEEDED(::SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc)));
  wchar_t conn[600];
  swprintf(conn, 600, L"Driver=%ls;Database=%ls", (const wchar_t*)CStringW(Driver()), (const wchar_t*)CStringW(path));
  SQLWCHAR out[600];
  SQLSMALLINT outLen = 0;
  SQLRETURN rc = ::SQLDriverConnectW(dbc, NULL, (SQLWCHAR*)conn, SQL_NTS, out, 600, &outLen, SQL_DRIVER_NOPROMPT);
  CHECK(SQL_SUCCEEDED(rc));
  if (!SQL_SUCCEEDED(rc)) {
    SQLCHAR state[6], msg[256];
    SQLINTEGER native = 0;
    if (SQL_SUCCEEDED(::SQLGetDiagRecA(SQL_HANDLE_DBC, dbc, 1, state, &native, msg, 256, NULL)))
      printf("  %s: %s\n", state, msg);
    return;
  }
  CHECK(outLen > 0 && wcsstr((const wchar_t*)out, L"odbctest-api.db") != NULL);
  CHECK(outLen == (SQLSMALLINT)wcslen((const wchar_t*)out));

  SQLWCHAR dbms[64] = {0};
  SQLSMALLINT dbmsLen = 0;
  CHECK(SQL_SUCCEEDED(::SQLGetInfoW(dbc, SQL_DBMS_NAME, dbms, sizeof(dbms), &dbmsLen)));
  CHECK(wcscmp((const wchar_t*)dbms, L"SQLite") == 0);
  CHECK(dbmsLen == (SQLSMALLINT)(wcslen(L"SQLite") * sizeof(SQLWCHAR)));

  CHECK(SQL_SUCCEEDED(::SQLAllocHandle(SQL_HANDLE_STMT, dbc, &st)));
  CHECK(SQL_SUCCEEDED(::SQLExecDirectW(st, (SQLWCHAR*)L"CREATE TABLE notes (id INTEGER, body TEXT)", SQL_NTS)));
  CHECK(SQL_SUCCEEDED(::SQLPrepareW(st, (SQLWCHAR*)L"INSERT INTO notes VALUES (?, ?)", SQL_NTS)));
  SQLINTEGER id = 7;
  SQLLEN idInd = 0;
  SQLWCHAR body[32];
  wcscpy((wchar_t*)body, kWide);
  SQLLEN bodyInd = SQL_NTS;
  CHECK(SQL_SUCCEEDED(::SQLBindParameter(st, 1, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER, 0, 0, &id, 0, &idInd)));
  CHECK(SQL_SUCCEEDED(::SQLBindParameter(st, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 31, 0, body,
                                         sizeof(body), &bodyInd)));
  CHECK(SQL_SUCCEEDED(::SQLExecute(st)));
  // The parameter is read again for each execution.
  id = 8;
  wcscpy((wchar_t*)body, L"plain");
  CHECK(SQL_SUCCEEDED(::SQLExecute(st)));
  SQLLEN rows = 0;
  CHECK(SQL_SUCCEEDED(::SQLRowCount(st, &rows)) && rows == 1);
  ::SQLFreeStmt(st, SQL_RESET_PARAMS);

  CHECK(SQL_SUCCEEDED(::SQLExecDirectW(st, (SQLWCHAR*)L"SELECT id, body FROM notes ORDER BY id", SQL_NTS)));
  SQLSMALLINT cols = 0;
  CHECK(SQL_SUCCEEDED(::SQLNumResultCols(st, &cols)) && cols == 2);
  SQLWCHAR colName[32];
  SQLSMALLINT nameLen = 0, type = 0, digits = 0, nullable = 0;
  SQLULEN size = 0;
  CHECK(SQL_SUCCEEDED(::SQLDescribeColW(st, 2, colName, 32, &nameLen, &type, &size, &digits, &nullable)));
  CHECK(wcscmp((const wchar_t*)colName, L"body") == 0 && nameLen == 4);
  CHECK(SQL_SUCCEEDED(::SQLFetch(st)));
  // In pieces of three characters (and the terminator).
  std::wstring got;
  SQLWCHAR piece[4];
  SQLLEN ind = 0;
  int pieces = 0;
  for (;;) {
    rc = ::SQLGetData(st, 2, SQL_C_WCHAR, piece, sizeof(piece), &ind);
    if (rc == SQL_NO_DATA) break;
    CHECK(SQL_SUCCEEDED(rc));
    if (!SQL_SUCCEEDED(rc)) break;
    if (pieces == 0) CHECK(ind == (SQLLEN)(wcslen(kWide) * sizeof(SQLWCHAR)));
    got += (const wchar_t*)piece;
    pieces++;
    if (rc == SQL_SUCCESS) break;
  }
  CHECK(got == kWide);
  CHECK(pieces == 3);  // eight characters, three at a time
  CHECK(SQL_SUCCEEDED(::SQLFetch(st)));
  CHECK(::SQLFetch(st) == SQL_NO_DATA);
  ::SQLFreeStmt(st, SQL_CLOSE);

  // A diagnostic, wide.
  CHECK(::SQLExecDirectW(st, (SQLWCHAR*)L"SELECT * FROM nowhere", SQL_NTS) == SQL_ERROR);
  SQLWCHAR state[6], msg[256];
  SQLINTEGER native = 0;
  SQLSMALLINT msgLen = 0;
  CHECK(SQL_SUCCEEDED(::SQLGetDiagRecW(SQL_HANDLE_STMT, st, 1, state, &native, msg, 256, &msgLen)));
  CHECK(wcslen((const wchar_t*)state) == 5);
  CHECK(wcsstr((const wchar_t*)msg, L"nowhere") != NULL);
  CHECK(msgLen == (SQLSMALLINT)wcslen((const wchar_t*)msg));

  ::SQLFreeHandle(SQL_HANDLE_STMT, st);
  CHECK(SQL_SUCCEEDED(::SQLDisconnect(dbc)));
  ::SQLFreeHandle(SQL_HANDLE_DBC, dbc);
  ::SQLFreeHandle(SQL_HANDLE_ENV, env);
}

// The classes: CDatabase, CRecordset with a parameter, and CDBException.
static void TestClasses(const char* path) {
  CDatabase db;
  CString conn;
  conn.Format(_T("Driver=%s;Database=%s"), (LPCTSTR)CString(Driver()), (LPCTSTR)CString(path));
  try {
    CHECK(db.OpenEx(conn, CDatabase::noOdbcDialog));
  } catch (CDBException* e) {
    printf("FAIL: OpenEx: %s\n", (const char*)CStringA(e->m_strError));
    g_failures++;
    e->Delete();
    return;
  }
  CHECK(db.IsOpen());
  CHECK(db.GetConnect().Find(_T("odbctest-classes.db")) >= 0);

  db.ExecuteSQL(_T("CREATE TABLE people (id INTEGER PRIMARY KEY, name TEXT, age INTEGER, score REAL)"));
  db.ExecuteSQL(_T("INSERT INTO people VALUES (1, 'Ada', 36, 91.5)"));
  db.ExecuteSQL(_T("INSERT INTO people VALUES (2, '") WIDE_T _T("', 29, 88)"));
  db.ExecuteSQL(_T("INSERT INTO people VALUES (3, 'Carol', 51, 72.25)"));

  {
    CPeopleSet rs(&db);
    rs.m_strSort = _T("id");
    CHECK(rs.Open(CRecordset::forwardOnly, NULL, CRecordset::readOnly));
    int n = 0;
    while (!rs.IsEOF()) {
      n++;
      if (rs.m_ID == 1) CHECK(rs.m_Name == _T("Ada") && rs.m_Age == 36 && rs.m_Score == 91.5);
      if (rs.m_ID == 2) CHECK(rs.m_Name == WIDE_T && rs.m_Age == 29);
      rs.MoveNext();
    }
    CHECK(n == 3);
    rs.Close();
  }
  {
    CPeopleSet rs(&db);
    rs.m_nParams = 1;
    rs.m_paramAge = 30;
    rs.m_strFilter = _T("age > ?");
    rs.m_strSort = _T("age DESC");
    CHECK(rs.Open(CRecordset::forwardOnly, NULL, CRecordset::readOnly));
    CHECK(!rs.IsEOF() && rs.m_Name == _T("Carol"));
    rs.MoveNext();
    CHECK(!rs.IsEOF() && rs.m_Name == _T("Ada"));
    rs.MoveNext();
    CHECK(rs.IsEOF());
    rs.Close();
  }

  // Transactions.
  if (db.CanTransact()) {
    CHECK(db.BeginTrans());
    db.ExecuteSQL(_T("DELETE FROM people WHERE id = 3"));
    CHECK(db.Rollback());
    CPeopleSet rs(&db);
    rs.m_strFilter = _T("id = 3");
    CHECK(rs.Open(CRecordset::forwardOnly, NULL, CRecordset::readOnly));
    CHECK(!rs.IsEOF());
    rs.Close();
  }

  // An error comes back as a CDBException with the driver's message.
  bool thrown = false;
  try {
    db.ExecuteSQL(_T("SELECT * FROM nowhere"));
  } catch (CDBException* e) {
    thrown = true;
    CHECK(e->m_strError.Find(_T("nowhere")) >= 0);
    CHECK(e->m_strStateNativeOrigin.Find(_T("State:")) >= 0);
    e->Delete();
  }
  CHECK(thrown);
  db.Close();
  CHECK(!db.IsOpen());
}

int main() {
  // No driver manager: allocating an environment fails.
  SQLHENV env = SQL_NULL_HENV;
  if (!SQL_SUCCEEDED(::SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env))) {
    if (Skip("no ODBC driver manager")) return 0;
    return 1;
  }
  // No SQLite 3 driver: connecting says the driver is not there (IM002).
  ::SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
  SQLHDBC dbc = SQL_NULL_HDBC;
  ::SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);
  CStringA conn;
  conn.Format("Driver=%s;Database=:memory:", Driver());
  SQLRETURN rc = ::SQLDriverConnectA(dbc, NULL, (SQLCHAR*)(const char*)conn, SQL_NTS, NULL, 0, NULL,
                                     SQL_DRIVER_NOPROMPT);
  bool driver = SQL_SUCCEEDED(rc);
  if (driver) ::SQLDisconnect(dbc);
  ::SQLFreeHandle(SQL_HANDLE_DBC, dbc);
  ::SQLFreeHandle(SQL_HANDLE_ENV, env);
  if (!driver) {
    if (Skip("no SQLite3 ODBC driver")) return 0;
    return 1;
  }

  const char* dir = getenv("MFC_TEST_OUT");
  CStringA base = dir && *dir ? CStringA(dir) : CStringA("/tmp");
  CStringA path1 = base + "/odbctest-api.db", path2 = base + "/odbctest-classes.db";
  unlink(path1);
  unlink(path2);
  TestWideApi(path1);
  TestClasses(path2);
  unlink(path1);
  unlink(path2);

  if (g_failures == 0) printf("PASS: all ODBC tests\n");
  else printf("%d failure(s)\n", g_failures);
  return g_failures ? 1 : 0;
}
