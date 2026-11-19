// odbc.cpp - ODBC through the system's driver manager (unixODBC, or
// iODBC), loaded when a program first calls it.
//
// The layer calls the driver manager's ANSI entry points only, with text
// in UTF-8, so the driver manager's own wide type (UTF-16 in unixODBC,
// wchar_t in iODBC) never matters. The wide entry points convert their
// strings; wide data (SQL_C_WCHAR) is fetched and bound as UTF-8 text and
// converted: SQLGetData reads a column's whole value once and hands it out
// in the pieces asked for, and a wide parameter is bound to a UTF-8 copy
// that is refreshed from the program's buffer before each execution (and
// copied back, for an output parameter, after it).
//
// WINPORT_ODBC names the driver manager's library when it is not one of
// the usual ones. Without a driver manager, allocating an environment
// fails, as it does on Windows without ODBC.
//
// The definitions are weak, so that a program's own ODBC (a test's
// in-memory driver) takes their place.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <dlfcn.h>
#include <sql.h>
#include <sqlext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#define ODBC_EXPORT extern "C" __attribute__((weak))

using winport::Narrow;
using winport::Widen;

namespace {

// The driver manager's ANSI functions.
struct Manager {
  void* lib = NULL;
  SQLRETURN (*AllocHandle)(SQLSMALLINT, SQLHANDLE, SQLHANDLE*) = NULL;
  SQLRETURN (*FreeHandle)(SQLSMALLINT, SQLHANDLE) = NULL;
  SQLRETURN (*FreeStmt)(SQLHSTMT, SQLUSMALLINT) = NULL;
  SQLRETURN (*BindParameter)(SQLHSTMT, SQLUSMALLINT, SQLSMALLINT, SQLSMALLINT, SQLSMALLINT, SQLULEN, SQLSMALLINT,
                             SQLPOINTER, SQLLEN, SQLLEN*) = NULL;
  SQLRETURN (*Cancel)(SQLHSTMT) = NULL;
  SQLRETURN (*DescribeCol)(SQLHSTMT, SQLUSMALLINT, SQLCHAR*, SQLSMALLINT, SQLSMALLINT*, SQLSMALLINT*, SQLULEN*,
                           SQLSMALLINT*, SQLSMALLINT*) = NULL;
  SQLRETURN (*Disconnect)(SQLHDBC) = NULL;
  SQLRETURN (*DriverConnect)(SQLHDBC, SQLHWND, SQLCHAR*, SQLSMALLINT, SQLCHAR*, SQLSMALLINT, SQLSMALLINT*,
                             SQLUSMALLINT) = NULL;
  SQLRETURN (*EndTran)(SQLSMALLINT, SQLHANDLE, SQLSMALLINT) = NULL;
  SQLRETURN (*ExecDirect)(SQLHSTMT, SQLCHAR*, SQLINTEGER) = NULL;
  SQLRETURN (*Execute)(SQLHSTMT) = NULL;
  SQLRETURN (*Fetch)(SQLHSTMT) = NULL;
  SQLRETURN (*FetchScroll)(SQLHSTMT, SQLSMALLINT, SQLLEN) = NULL;
  SQLRETURN (*GetData)(SQLHSTMT, SQLUSMALLINT, SQLSMALLINT, SQLPOINTER, SQLLEN, SQLLEN*) = NULL;
  SQLRETURN (*GetDiagRec)(SQLSMALLINT, SQLHANDLE, SQLSMALLINT, SQLCHAR*, SQLINTEGER*, SQLCHAR*, SQLSMALLINT,
                          SQLSMALLINT*) = NULL;
  SQLRETURN (*GetInfo)(SQLHDBC, SQLUSMALLINT, SQLPOINTER, SQLSMALLINT, SQLSMALLINT*) = NULL;
  SQLRETURN (*GetStmtAttr)(SQLHSTMT, SQLINTEGER, SQLPOINTER, SQLINTEGER, SQLINTEGER*) = NULL;
  SQLRETURN (*MoreResults)(SQLHSTMT) = NULL;
  SQLRETURN (*NumResultCols)(SQLHSTMT, SQLSMALLINT*) = NULL;
  SQLRETURN (*Prepare)(SQLHSTMT, SQLCHAR*, SQLINTEGER) = NULL;
  SQLRETURN (*RowCount)(SQLHSTMT, SQLLEN*) = NULL;
  SQLRETURN (*SetConnectAttr)(SQLHDBC, SQLINTEGER, SQLPOINTER, SQLINTEGER) = NULL;
  SQLRETURN (*SetEnvAttr)(SQLHENV, SQLINTEGER, SQLPOINTER, SQLINTEGER) = NULL;
  SQLRETURN (*SetStmtAttr)(SQLHSTMT, SQLINTEGER, SQLPOINTER, SQLINTEGER) = NULL;
};

template <class F>
bool Find(void* lib, const char* name, F* out) {
  *out = (F)dlsym(lib, name);
  return *out != NULL;
}

void* OpenLibrary() {
  int flags = RTLD_NOW | RTLD_LOCAL;
#ifdef RTLD_DEEPBIND
  // Its own functions before the layer's of the same names.
  flags |= RTLD_DEEPBIND;
#endif
  if (const char* env = getenv("WINPORT_ODBC")) {
    if (*env) return dlopen(env, flags);
  }
  static const char* const names[] = {
#ifdef __APPLE__
      "libodbc.2.dylib", "/opt/homebrew/lib/libodbc.2.dylib", "/usr/local/lib/libodbc.2.dylib", "libiodbc.2.dylib",
#else
      "libodbc.so.2", "libodbc.so.1", "libodbc.so", "libiodbc.so.2",
#endif
  };
  for (const char* n : names)
    if (void* lib = dlopen(n, flags)) return lib;
  return NULL;
}

// The driver manager, or NULL without one.
const Manager* DM() {
  static Manager m;
  static bool ok = false;
  static std::once_flag once;
  std::call_once(once, [] {
    m.lib = OpenLibrary();
    if (!m.lib) {
      if (getenv("WINPORT_DEBUG")) fprintf(stderr, "winport: no ODBC driver manager found\n");
      return;
    }
    ok = Find(m.lib, "SQLAllocHandle", &m.AllocHandle) && Find(m.lib, "SQLFreeHandle", &m.FreeHandle) &&
         Find(m.lib, "SQLFreeStmt", &m.FreeStmt) &&
         Find(m.lib, "SQLBindParameter", &m.BindParameter) && Find(m.lib, "SQLCancel", &m.Cancel) &&
         Find(m.lib, "SQLDescribeCol", &m.DescribeCol) && Find(m.lib, "SQLDisconnect", &m.Disconnect) &&
         Find(m.lib, "SQLDriverConnect", &m.DriverConnect) && Find(m.lib, "SQLEndTran", &m.EndTran) &&
         Find(m.lib, "SQLExecDirect", &m.ExecDirect) && Find(m.lib, "SQLExecute", &m.Execute) &&
         Find(m.lib, "SQLFetch", &m.Fetch) && Find(m.lib, "SQLFetchScroll", &m.FetchScroll) &&
         Find(m.lib, "SQLGetData", &m.GetData) && Find(m.lib, "SQLGetDiagRec", &m.GetDiagRec) &&
         Find(m.lib, "SQLGetInfo", &m.GetInfo) && Find(m.lib, "SQLGetStmtAttr", &m.GetStmtAttr) &&
         Find(m.lib, "SQLMoreResults", &m.MoreResults) && Find(m.lib, "SQLNumResultCols", &m.NumResultCols) &&
         Find(m.lib, "SQLPrepare", &m.Prepare) && Find(m.lib, "SQLRowCount", &m.RowCount) &&
         Find(m.lib, "SQLSetConnectAttr", &m.SetConnectAttr) && Find(m.lib, "SQLSetEnvAttr", &m.SetEnvAttr) &&
         Find(m.lib, "SQLSetStmtAttr", &m.SetStmtAttr);
    if (!ok && getenv("WINPORT_DEBUG")) fprintf(stderr, "winport: the ODBC driver manager lacks functions\n");
  });
  return ok ? &m : NULL;
}

// --- Strings

// A wide argument with its length in characters (or SQL_NTS), as UTF-8.
std::string In(const SQLWCHAR* s, SQLINTEGER len) {
  if (!s) return std::string();
  return Narrow(s, len == SQL_NTS || len < 0 ? -1 : (int)len);
}

// UTF-8 out to a wide buffer of cap characters, with the length in
// characters; true when it fit.
bool Out(const std::string& s, SQLWCHAR* buf, SQLINTEGER cap, SQLINTEGER* len) {
  std::wstring w = Widen(s.c_str(), (int)s.size());
  if (len) *len = (SQLINTEGER)w.size();
  if (!buf || cap <= 0) return w.empty();
  size_t n = std::min(w.size(), (size_t)cap - 1);
  wmemcpy(buf, w.data(), n);
  buf[n] = 0;
  return n == w.size();
}

// The room an ANSI call needs for cap wide characters' worth of text.
SQLSMALLINT Room(SQLINTEGER cap) {
  SQLINTEGER n = cap > 0 ? cap * 4 + 1 : 1;
  return (SQLSMALLINT)std::min<SQLINTEGER>(n, 32767);
}

// SQLGetInfo's text answers.
bool IsTextInfo(SQLUSMALLINT t) {
  switch (t) {
    case SQL_DATA_SOURCE_NAME: case SQL_DRIVER_NAME: case SQL_DRIVER_VER: case SQL_ODBC_VER:
    case SQL_SERVER_NAME: case SQL_SEARCH_PATTERN_ESCAPE: case SQL_DATABASE_NAME: case SQL_DBMS_NAME:
    case SQL_DBMS_VER: case SQL_ACCESSIBLE_TABLES: case SQL_ACCESSIBLE_PROCEDURES: case SQL_PROCEDURES:
    case SQL_DATA_SOURCE_READ_ONLY: case SQL_EXPRESSIONS_IN_ORDERBY: case SQL_MULT_RESULT_SETS:
    case SQL_MULTIPLE_ACTIVE_TXN: case SQL_OUTER_JOINS: case SQL_SCHEMA_TERM: case SQL_PROCEDURE_TERM:
    case SQL_CATALOG_NAME_SEPARATOR: case SQL_CATALOG_TERM: case SQL_TABLE_TERM: case SQL_USER_NAME:
    case SQL_IDENTIFIER_QUOTE_CHAR: case SQL_ROW_UPDATES: case SQL_COLUMN_ALIAS: case SQL_KEYWORDS:
    case SQL_ORDER_BY_COLUMNS_IN_SELECT: case SQL_SPECIAL_CHARACTERS: case SQL_MAX_ROW_SIZE_INCLUDES_LONG:
    case SQL_NEED_LONG_DATA_LEN: case SQL_LIKE_ESCAPE_CLAUSE: case SQL_DRIVER_ODBC_VER: case SQL_INTEGRITY:
    case SQL_XOPEN_CLI_YEAR: case SQL_DESCRIBE_PARAMETER: case SQL_CATALOG_NAME: case SQL_COLLATION_SEQ:
    case SQL_DM_VER:
      return true;
  }
  return false;
}

// --- Statements' wide data and parameters

struct WideParam {
  SQLSMALLINT io = SQL_PARAM_INPUT;
  SQLWCHAR* value = NULL;
  SQLLEN cap = 0;  // bytes
  SQLLEN* ind = NULL;
  std::vector<char> text;  // what the driver reads and writes
  SQLLEN textInd = 0;
};

struct Statement {
  std::map<SQLUSMALLINT, WideParam> params;
  // The column being read as wide text, its value and how far it has
  // been handed out.
  SQLUSMALLINT column = 0;
  bool have = false, isNull = false, served = false;
  std::wstring value;
  size_t offset = 0;
};

std::mutex g_lock;
std::map<SQLHSTMT, Statement>& Statements() {
  static std::map<SQLHSTMT, Statement> m;
  return m;
}

void ForgetData(SQLHSTMT h) {
  std::lock_guard<std::mutex> g(g_lock);
  auto it = Statements().find(h);
  if (it != Statements().end()) {
    it->second.have = false;
    it->second.value.clear();
  }
}

// Before an execution: the wide input parameters' text.
void PutParams(SQLHSTMT h) {
  std::lock_guard<std::mutex> g(g_lock);
  auto it = Statements().find(h);
  if (it == Statements().end()) return;
  for (auto& kv : it->second.params) {
    WideParam& p = kv.second;
    if (p.io == SQL_PARAM_OUTPUT) continue;
    if (p.ind && *p.ind == SQL_NULL_DATA) {
      p.textInd = SQL_NULL_DATA;
      continue;
    }
    if (p.ind && (*p.ind == SQL_DATA_AT_EXEC || *p.ind <= SQL_LEN_DATA_AT_EXEC_OFFSET)) {
      p.textInd = *p.ind;
      continue;
    }
    std::string s;
    if (p.value) {
      SQLLEN chars = !p.ind || *p.ind == SQL_NTS ? -1 : *p.ind / (SQLLEN)sizeof(SQLWCHAR);
      if (chars < 0) {
        SQLLEN most = p.cap > 0 ? p.cap / (SQLLEN)sizeof(SQLWCHAR) : 0x7fffffff;
        chars = (SQLLEN)wcsnlen(p.value, (size_t)most);
      }
      s = Narrow(p.value, (int)chars);
    }
    size_t n = std::min(s.size(), p.text.size() - 1);
    memcpy(p.text.data(), s.data(), n);
    p.text[n] = 0;
    p.textInd = (SQLLEN)n;
  }
}

// After an execution: the wide output parameters, from their text.
void GetParams(SQLHSTMT h) {
  std::lock_guard<std::mutex> g(g_lock);
  auto it = Statements().find(h);
  if (it == Statements().end()) return;
  for (auto& kv : it->second.params) {
    WideParam& p = kv.second;
    if (p.io == SQL_PARAM_INPUT) continue;
    if (p.textInd == SQL_NULL_DATA) {
      if (p.ind) *p.ind = SQL_NULL_DATA;
      continue;
    }
    size_t len = p.textInd >= 0 ? std::min((size_t)p.textInd, p.text.size() - 1) : strlen(p.text.data());
    SQLINTEGER chars = 0;
    Out(std::string(p.text.data(), len), p.value, (SQLINTEGER)(p.cap / (SQLLEN)sizeof(SQLWCHAR)), &chars);
    if (p.ind) *p.ind = (SQLLEN)chars * (SQLLEN)sizeof(SQLWCHAR);
  }
}

// A column's whole value as UTF-8 text, read in pieces.
SQLRETURN ReadText(const Manager* dm, SQLHSTMT h, SQLUSMALLINT col, std::string* out, bool* isNull) {
  char buf[4096];
  bool any = false;
  for (;;) {
    SQLLEN ind = 0;
    SQLRETURN rc = dm->GetData(h, col, SQL_C_CHAR, buf, sizeof buf, &ind);
    if (rc == SQL_NO_DATA) return any ? SQL_SUCCESS : SQL_NO_DATA;
    if (!SQL_SUCCEEDED(rc)) return rc;
    any = true;
    if (ind == SQL_NULL_DATA) {
      *isNull = true;
      return SQL_SUCCESS;
    }
    bool more = rc == SQL_SUCCESS_WITH_INFO && (ind == SQL_NO_TOTAL || ind >= (SQLLEN)sizeof buf);
    size_t n = more ? sizeof buf - 1 : strnlen(buf, sizeof buf);
    out->append(buf, n);
    if (!more) return SQL_SUCCESS;
  }
}

SQLRETURN GetWideData(const Manager* dm, SQLHSTMT h, SQLUSMALLINT col, SQLPOINTER target, SQLLEN cap,
                      SQLLEN* ind) {
  Statement* st;
  {
    std::lock_guard<std::mutex> g(g_lock);
    st = &Statements()[h];
  }
  if (!st->have || st->column != col) {
    std::string text;
    bool isNull = false;
    SQLRETURN rc = ReadText(dm, h, col, &text, &isNull);
    if (!SQL_SUCCEEDED(rc)) return rc;
    st->column = col;
    st->have = true;
    st->isNull = isNull;
    st->served = false;
    st->value = Widen(text.c_str(), (int)text.size());
    st->offset = 0;
  }
  if (st->isNull) {
    if (st->served) return SQL_NO_DATA;
    st->served = true;
    if (ind) *ind = SQL_NULL_DATA;
    return SQL_SUCCESS;
  }
  size_t left = st->value.size() - st->offset;
  if (st->served && left == 0) return SQL_NO_DATA;
  st->served = true;
  if (ind) *ind = (SQLLEN)(left * sizeof(SQLWCHAR));
  SQLLEN room = cap / (SQLLEN)sizeof(SQLWCHAR);
  if (!target || room <= 0) return left ? SQL_SUCCESS_WITH_INFO : SQL_SUCCESS;
  size_t n = std::min(left, (size_t)room - 1);
  SQLWCHAR* w = (SQLWCHAR*)target;
  wmemcpy(w, st->value.data() + st->offset, n);
  w[n] = 0;
  st->offset += n;
  return n < left ? SQL_SUCCESS_WITH_INFO : SQL_SUCCESS;
}

}  // namespace

// ---------------------------------------------------------------------
// Handles and attributes

ODBC_EXPORT SQLRETURN SQL_API SQLAllocHandle(SQLSMALLINT type, SQLHANDLE in, SQLHANDLE* out) {
  const Manager* dm = DM();
  if (!dm) {
    if (out) *out = SQL_NULL_HANDLE;
    return SQL_ERROR;
  }
  SQLRETURN rc = dm->AllocHandle(type, in, out);
  if (SQL_SUCCEEDED(rc) && type == SQL_HANDLE_STMT && out) {
    std::lock_guard<std::mutex> g(g_lock);
    Statements()[*out] = Statement();
  }
  return rc;
}

ODBC_EXPORT SQLRETURN SQL_API SQLFreeHandle(SQLSMALLINT type, SQLHANDLE h) {
  const Manager* dm = DM();
  if (!dm) return SQL_INVALID_HANDLE;
  SQLRETURN rc = dm->FreeHandle(type, h);
  if (type == SQL_HANDLE_STMT && SQL_SUCCEEDED(rc)) {
    std::lock_guard<std::mutex> g(g_lock);
    Statements().erase(h);
  }
  return rc;
}

ODBC_EXPORT SQLRETURN SQL_API SQLFreeStmt(SQLHSTMT h, SQLUSMALLINT option) {
  const Manager* dm = DM();
  if (!dm) return SQL_INVALID_HANDLE;
  SQLRETURN rc = dm->FreeStmt(h, option);
  if (SQL_SUCCEEDED(rc)) {
    std::lock_guard<std::mutex> g(g_lock);
    auto it = Statements().find(h);
    if (it != Statements().end()) {
      if (option == SQL_DROP) {
        Statements().erase(it);
      } else if (option == SQL_RESET_PARAMS) {
        it->second.params.clear();
      } else if (option == SQL_CLOSE) {
        it->second.have = false;
        it->second.value.clear();
      }
    }
  }
  return rc;
}

ODBC_EXPORT SQLRETURN SQL_API SQLSetEnvAttr(SQLHENV h, SQLINTEGER attr, SQLPOINTER value, SQLINTEGER len) {
  const Manager* dm = DM();
  return dm ? dm->SetEnvAttr(h, attr, value, len) : SQL_INVALID_HANDLE;
}

ODBC_EXPORT SQLRETURN SQL_API SQLSetConnectAttrA(SQLHDBC h, SQLINTEGER attr, SQLPOINTER value, SQLINTEGER len) {
  const Manager* dm = DM();
  return dm ? dm->SetConnectAttr(h, attr, value, len) : SQL_INVALID_HANDLE;
}

ODBC_EXPORT SQLRETURN SQL_API SQLSetConnectAttrW(SQLHDBC h, SQLINTEGER attr, SQLPOINTER value, SQLINTEGER len) {
  const Manager* dm = DM();
  if (!dm) return SQL_INVALID_HANDLE;
  if (attr == SQL_ATTR_CURRENT_CATALOG || attr == SQL_ATTR_TRACEFILE || attr == SQL_ATTR_TRANSLATE_LIB) {
    // Text, its length in bytes.
    std::string s = In((const SQLWCHAR*)value, len == SQL_NTS ? SQL_NTS : len / (SQLINTEGER)sizeof(SQLWCHAR));
    return dm->SetConnectAttr(h, attr, (SQLPOINTER)s.c_str(), SQL_NTS);
  }
  return dm->SetConnectAttr(h, attr, value, len);
}

ODBC_EXPORT SQLRETURN SQL_API SQLSetStmtAttrA(SQLHSTMT h, SQLINTEGER attr, SQLPOINTER value, SQLINTEGER len) {
  const Manager* dm = DM();
  return dm ? dm->SetStmtAttr(h, attr, value, len) : SQL_INVALID_HANDLE;
}

ODBC_EXPORT SQLRETURN SQL_API SQLSetStmtAttrW(SQLHSTMT h, SQLINTEGER attr, SQLPOINTER value, SQLINTEGER len) {
  return SQLSetStmtAttrA(h, attr, value, len);  // no statement attribute is text
}

ODBC_EXPORT SQLRETURN SQL_API SQLGetStmtAttrA(SQLHSTMT h, SQLINTEGER attr, SQLPOINTER value, SQLINTEGER cap,
                                              SQLINTEGER* len) {
  const Manager* dm = DM();
  return dm ? dm->GetStmtAttr(h, attr, value, cap, len) : SQL_INVALID_HANDLE;
}

ODBC_EXPORT SQLRETURN SQL_API SQLGetStmtAttrW(SQLHSTMT h, SQLINTEGER attr, SQLPOINTER value, SQLINTEGER cap,
                                              SQLINTEGER* len) {
  return SQLGetStmtAttrA(h, attr, value, cap, len);
}

// ---------------------------------------------------------------------
// Connections

ODBC_EXPORT SQLRETURN SQL_API SQLDriverConnectA(SQLHDBC h, SQLHWND, SQLCHAR* in, SQLSMALLINT inLen, SQLCHAR* out,
                                                SQLSMALLINT outCap, SQLSMALLINT* outLen, SQLUSMALLINT) {
  const Manager* dm = DM();
  if (!dm) return SQL_INVALID_HANDLE;
  // The driver manager has no dialogs to complete a connection with: what
  // is given is all there is.
  return dm->DriverConnect(h, NULL, in, inLen, out, outCap, outLen, SQL_DRIVER_NOPROMPT);
}

ODBC_EXPORT SQLRETURN SQL_API SQLDriverConnectW(SQLHDBC h, SQLHWND hwnd, SQLWCHAR* in, SQLSMALLINT inLen,
                                                SQLWCHAR* out, SQLSMALLINT outCap, SQLSMALLINT* outLen,
                                                SQLUSMALLINT completion) {
  std::string s = In(in, inLen);
  std::vector<char> buf(Room(outCap));
  SQLSMALLINT n = 0;
  SQLRETURN rc = SQLDriverConnectA(h, hwnd, (SQLCHAR*)s.c_str(), SQL_NTS, (SQLCHAR*)buf.data(),
                                   (SQLSMALLINT)buf.size(), &n, completion);
  if (SQL_SUCCEEDED(rc)) {
    SQLINTEGER chars = 0;
    bool fit = Out(std::string(buf.data(), strnlen(buf.data(), buf.size())), out, outCap, &chars);
    if (outLen) *outLen = (SQLSMALLINT)chars;
    if (!fit && rc == SQL_SUCCESS) rc = SQL_SUCCESS_WITH_INFO;
  }
  return rc;
}

ODBC_EXPORT SQLRETURN SQL_API SQLDisconnect(SQLHDBC h) {
  const Manager* dm = DM();
  return dm ? dm->Disconnect(h) : SQL_INVALID_HANDLE;
}

ODBC_EXPORT SQLRETURN SQL_API SQLEndTran(SQLSMALLINT type, SQLHANDLE h, SQLSMALLINT completion) {
  const Manager* dm = DM();
  return dm ? dm->EndTran(type, h, completion) : SQL_INVALID_HANDLE;
}

ODBC_EXPORT SQLRETURN SQL_API SQLGetInfoA(SQLHDBC h, SQLUSMALLINT type, SQLPOINTER value, SQLSMALLINT cap,
                                          SQLSMALLINT* len) {
  const Manager* dm = DM();
  return dm ? dm->GetInfo(h, type, value, cap, len) : SQL_INVALID_HANDLE;
}

ODBC_EXPORT SQLRETURN SQL_API SQLGetInfoW(SQLHDBC h, SQLUSMALLINT type, SQLPOINTER value, SQLSMALLINT cap,
                                          SQLSMALLINT* len) {
  const Manager* dm = DM();
  if (!dm) return SQL_INVALID_HANDLE;
  if (!IsTextInfo(type)) return dm->GetInfo(h, type, value, cap, len);
  // Text: cap and the length are in bytes.
  SQLINTEGER chars = cap / (SQLINTEGER)sizeof(SQLWCHAR);
  std::vector<char> buf(Room(chars > 0 ? chars : 256));
  SQLSMALLINT n = 0;
  SQLRETURN rc = dm->GetInfo(h, type, buf.data(), (SQLSMALLINT)buf.size(), &n);
  if (SQL_SUCCEEDED(rc)) {
    SQLINTEGER got = 0;
    bool fit = Out(std::string(buf.data(), strnlen(buf.data(), buf.size())), (SQLWCHAR*)value, chars, &got);
    if (len) *len = (SQLSMALLINT)(got * (SQLINTEGER)sizeof(SQLWCHAR));
    if (!fit && value && rc == SQL_SUCCESS) rc = SQL_SUCCESS_WITH_INFO;
  }
  return rc;
}

ODBC_EXPORT SQLRETURN SQL_API SQLGetDiagRecA(SQLSMALLINT type, SQLHANDLE h, SQLSMALLINT rec, SQLCHAR* state,
                                             SQLINTEGER* native, SQLCHAR* msg, SQLSMALLINT cap, SQLSMALLINT* len) {
  const Manager* dm = DM();
  return dm ? dm->GetDiagRec(type, h, rec, state, native, msg, cap, len) : SQL_INVALID_HANDLE;
}

ODBC_EXPORT SQLRETURN SQL_API SQLGetDiagRecW(SQLSMALLINT type, SQLHANDLE h, SQLSMALLINT rec, SQLWCHAR* state,
                                             SQLINTEGER* native, SQLWCHAR* msg, SQLSMALLINT cap, SQLSMALLINT* len) {
  char st[8] = {0};
  std::vector<char> buf(Room(cap > 0 ? cap : 512));
  SQLSMALLINT n = 0;
  SQLRETURN rc = SQLGetDiagRecA(type, h, rec, (SQLCHAR*)st, native, (SQLCHAR*)buf.data(), (SQLSMALLINT)buf.size(), &n);
  if (SQL_SUCCEEDED(rc)) {
    if (state) Out(std::string(st, strnlen(st, 6)), state, 6, NULL);
    SQLINTEGER chars = 0;
    bool fit = Out(std::string(buf.data(), strnlen(buf.data(), buf.size())), msg, cap, &chars);
    if (len) *len = (SQLSMALLINT)chars;
    if (!fit && msg && rc == SQL_SUCCESS) rc = SQL_SUCCESS_WITH_INFO;
  }
  return rc;
}

// ---------------------------------------------------------------------
// Statements

ODBC_EXPORT SQLRETURN SQL_API SQLPrepareA(SQLHSTMT h, SQLCHAR* sql, SQLINTEGER len) {
  const Manager* dm = DM();
  if (!dm) return SQL_INVALID_HANDLE;
  ForgetData(h);
  return dm->Prepare(h, sql, len);
}

ODBC_EXPORT SQLRETURN SQL_API SQLPrepareW(SQLHSTMT h, SQLWCHAR* sql, SQLINTEGER len) {
  std::string s = In(sql, len);
  return SQLPrepareA(h, (SQLCHAR*)s.c_str(), SQL_NTS);
}

ODBC_EXPORT SQLRETURN SQL_API SQLExecute(SQLHSTMT h) {
  const Manager* dm = DM();
  if (!dm) return SQL_INVALID_HANDLE;
  ForgetData(h);
  PutParams(h);
  SQLRETURN rc = dm->Execute(h);
  if (SQL_SUCCEEDED(rc)) GetParams(h);
  return rc;
}

ODBC_EXPORT SQLRETURN SQL_API SQLExecDirectA(SQLHSTMT h, SQLCHAR* sql, SQLINTEGER len) {
  const Manager* dm = DM();
  if (!dm) return SQL_INVALID_HANDLE;
  ForgetData(h);
  PutParams(h);
  SQLRETURN rc = dm->ExecDirect(h, sql, len);
  if (SQL_SUCCEEDED(rc)) GetParams(h);
  return rc;
}

ODBC_EXPORT SQLRETURN SQL_API SQLExecDirectW(SQLHSTMT h, SQLWCHAR* sql, SQLINTEGER len) {
  std::string s = In(sql, len);
  return SQLExecDirectA(h, (SQLCHAR*)s.c_str(), SQL_NTS);
}

ODBC_EXPORT SQLRETURN SQL_API SQLBindParameter(SQLHSTMT h, SQLUSMALLINT n, SQLSMALLINT io, SQLSMALLINT ctype,
                                               SQLSMALLINT sqltype, SQLULEN size, SQLSMALLINT scale,
                                               SQLPOINTER value, SQLLEN cap, SQLLEN* ind) {
  const Manager* dm = DM();
  if (!dm) return SQL_INVALID_HANDLE;
  if (ctype != SQL_C_WCHAR) {
    {
      std::lock_guard<std::mutex> g(g_lock);
      auto it = Statements().find(h);
      if (it != Statements().end()) it->second.params.erase(n);
    }
    return dm->BindParameter(h, n, io, ctype, sqltype, size, scale, value, cap, ind);
  }
  // Wide text: bound as UTF-8 to a copy of the layer's.
  WideParam* p;
  {
    std::lock_guard<std::mutex> g(g_lock);
    p = &Statements()[h].params[n];
  }
  p->io = io;
  p->value = (SQLWCHAR*)value;
  p->cap = cap;
  p->ind = ind;
  size_t chars = std::max<size_t>(cap > 0 ? (size_t)cap / sizeof(SQLWCHAR) : 0, size > 0 ? (size_t)size : 0);
  p->text.assign(std::max<size_t>(chars, 1) * 4 + 1, 0);
  p->textInd = 0;
  return dm->BindParameter(h, n, io, SQL_C_CHAR, sqltype, size, scale, p->text.data(), (SQLLEN)p->text.size(),
                           &p->textInd);
}

ODBC_EXPORT SQLRETURN SQL_API SQLCancel(SQLHSTMT h) {
  const Manager* dm = DM();
  return dm ? dm->Cancel(h) : SQL_INVALID_HANDLE;
}

ODBC_EXPORT SQLRETURN SQL_API SQLNumResultCols(SQLHSTMT h, SQLSMALLINT* n) {
  const Manager* dm = DM();
  return dm ? dm->NumResultCols(h, n) : SQL_INVALID_HANDLE;
}

ODBC_EXPORT SQLRETURN SQL_API SQLRowCount(SQLHSTMT h, SQLLEN* n) {
  const Manager* dm = DM();
  return dm ? dm->RowCount(h, n) : SQL_INVALID_HANDLE;
}

ODBC_EXPORT SQLRETURN SQL_API SQLDescribeColA(SQLHSTMT h, SQLUSMALLINT col, SQLCHAR* name, SQLSMALLINT cap,
                                              SQLSMALLINT* len, SQLSMALLINT* type, SQLULEN* size,
                                              SQLSMALLINT* scale, SQLSMALLINT* nullable) {
  const Manager* dm = DM();
  return dm ? dm->DescribeCol(h, col, name, cap, len, type, size, scale, nullable) : SQL_INVALID_HANDLE;
}

ODBC_EXPORT SQLRETURN SQL_API SQLDescribeColW(SQLHSTMT h, SQLUSMALLINT col, SQLWCHAR* name, SQLSMALLINT cap,
                                              SQLSMALLINT* len, SQLSMALLINT* type, SQLULEN* size,
                                              SQLSMALLINT* scale, SQLSMALLINT* nullable) {
  std::vector<char> buf(Room(cap > 0 ? cap : 256));
  SQLSMALLINT n = 0;
  SQLRETURN rc = SQLDescribeColA(h, col, (SQLCHAR*)buf.data(), (SQLSMALLINT)buf.size(), &n, type, size, scale,
                                 nullable);
  if (SQL_SUCCEEDED(rc)) {
    SQLINTEGER chars = 0;
    bool fit = Out(std::string(buf.data(), strnlen(buf.data(), buf.size())), name, cap, &chars);
    if (len) *len = (SQLSMALLINT)chars;
    if (!fit && name && rc == SQL_SUCCESS) rc = SQL_SUCCESS_WITH_INFO;
  }
  return rc;
}

ODBC_EXPORT SQLRETURN SQL_API SQLFetch(SQLHSTMT h) {
  const Manager* dm = DM();
  if (!dm) return SQL_INVALID_HANDLE;
  ForgetData(h);
  return dm->Fetch(h);
}

ODBC_EXPORT SQLRETURN SQL_API SQLFetchScroll(SQLHSTMT h, SQLSMALLINT orientation, SQLLEN offset) {
  const Manager* dm = DM();
  if (!dm) return SQL_INVALID_HANDLE;
  ForgetData(h);
  return dm->FetchScroll(h, orientation, offset);
}

ODBC_EXPORT SQLRETURN SQL_API SQLMoreResults(SQLHSTMT h) {
  const Manager* dm = DM();
  if (!dm) return SQL_INVALID_HANDLE;
  ForgetData(h);
  return dm->MoreResults(h);
}

ODBC_EXPORT SQLRETURN SQL_API SQLGetData(SQLHSTMT h, SQLUSMALLINT col, SQLSMALLINT type, SQLPOINTER target,
                                         SQLLEN cap, SQLLEN* ind) {
  const Manager* dm = DM();
  if (!dm) return SQL_INVALID_HANDLE;
  if (type == SQL_C_WCHAR) return GetWideData(dm, h, col, target, cap, ind);
  ForgetData(h);
  return dm->GetData(h, col, type, target, cap, ind);
}
