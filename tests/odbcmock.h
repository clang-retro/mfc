// odbcmock.h - a stand-in for the ODBC driver manager, for the database
// tests.
//
// Wine forwards ODBC to the host's unixODBC, and there is no 32-bit ODBC
// driver to install for it, so the tests link these functions in place
// of odbc32.dll's. They keep tables in memory and understand the small
// part of SQL the recordset classes write: SELECT with WHERE and ORDER
// BY, INSERT, UPDATE and DELETE, with ? parameters. Like real drivers,
// they insist that SQLGetData reads columns in order, and they return
// long values in pieces.
#pragma once
#include <sql.h>
#include <sqlext.h>
#include <string>
#include <vector>
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace mock {

enum Kind { kNull, kInt, kReal, kText, kBinary, kTime };

struct Value {
  Kind kind = kNull;
  long long i = 0;
  double d = 0;
  std::string s;  // text, or binary bytes
  TIMESTAMP_STRUCT ts = {};
  static Value Int(long long v) { Value x; x.kind = kInt; x.i = v; return x; }
  static Value Real(double v) { Value x; x.kind = kReal; x.d = v; return x; }
  static Value Text(const std::string& v) { Value x; x.kind = kText; x.s = v; return x; }
  static Value Binary(const std::string& v) { Value x; x.kind = kBinary; x.s = v; return x; }
  static Value Time(int y, int mo, int d, int h = 0, int mi = 0, int s = 0) {
    Value x;
    x.kind = kTime;
    x.ts.year = (SQLSMALLINT)y; x.ts.month = (SQLUSMALLINT)mo; x.ts.day = (SQLUSMALLINT)d;
    x.ts.hour = (SQLUSMALLINT)h; x.ts.minute = (SQLUSMALLINT)mi; x.ts.second = (SQLUSMALLINT)s;
    return x;
  }
  double Num() const {
    if (kind == kInt) return (double)i;
    if (kind == kReal) return d;
    if (kind == kText) return atof(s.c_str());
    return 0;
  }
  std::string Str() const {
    char buf[64];
    switch (kind) {
    case kInt: snprintf(buf, sizeof(buf), "%lld", i); return buf;
    case kReal: snprintf(buf, sizeof(buf), "%.15g", d); return buf;
    case kTime:
      snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", ts.year, ts.month, ts.day,
               ts.hour, ts.minute, ts.second);
      return buf;
    default: return s;
    }
  }
};

inline bool Equal(const Value& a, const Value& b) {
  if (a.kind == kNull || b.kind == kNull) return false;
  if (a.kind == kTime || b.kind == kTime) return a.Str() == b.Str();
  if ((a.kind == kInt || a.kind == kReal) && (b.kind == kInt || b.kind == kReal)) return a.Num() == b.Num();
  if (a.kind == kInt || a.kind == kReal || b.kind == kInt || b.kind == kReal) return a.Num() == b.Num();
  return a.s == b.s;
}

inline int Compare(const Value& a, const Value& b) {
  if (a.kind == kNull || b.kind == kNull) return (a.kind == kNull) - (b.kind == kNull);
  if (a.kind == kText && b.kind == kText) return a.s.compare(b.s);
  if (a.kind == kTime || b.kind == kTime) return a.Str().compare(b.Str());
  return a.Num() < b.Num() ? -1 : a.Num() > b.Num() ? 1 : 0;
}

struct Column {
  std::string name;
  SQLSMALLINT type;
  bool notNull;
};

struct Table {
  std::string name;
  std::vector<Column> cols;
  std::vector<std::vector<Value>> rows;
  // Each row's identity, which a keyset cursor keeps (rows a test puts
  // in the table directly get theirs when a statement first uses it).
  std::vector<long> ids;
  long nextId = 1;
  void SyncIds() {
    if (ids.size() == rows.size()) return;
    ids.clear();
    for (size_t i = 0; i < rows.size(); i++) ids.push_back(nextId++);
  }
  int Find(const std::string& col) const {
    for (size_t i = 0; i < cols.size(); i++)
      if (strcasecmp(cols[i].name.c_str(), col.c_str()) == 0) return (int)i;
    return -1;
  }
};

struct State {
  std::vector<Table> tables;
  std::vector<std::string> log;  // every statement executed
  std::vector<Table> saved;      // the tables when a transaction began
  bool inTransaction = false;
  bool readOnly = false;
  bool noKeyset = false;         // the driver has only forward-only and static cursors
  int nOpenStatements = 0;
  Table* Find(const std::string& name) {
    for (auto& t : tables)
      if (strcasecmp(t.name.c_str(), name.c_str()) == 0) return &t;
    return nullptr;
  }
};

inline State& G() {
  static State state;
  return state;
}

// Converts a value to the column's type, as a database would store it.
inline Value Coerce(const Value& v, SQLSMALLINT type) {
  if (v.kind == kNull) return v;
  switch (type) {
  case SQL_INTEGER: case SQL_SMALLINT: case SQL_TINYINT: case SQL_BIT: case SQL_BIGINT:
    return Value::Int((long long)v.Num());
  case SQL_DOUBLE: case SQL_REAL: case SQL_FLOAT:
    return Value::Real(v.Num());
  case SQL_TIMESTAMP: case SQL_TYPE_TIMESTAMP:
    if (v.kind == kTime) return v;
    if (v.kind == kText) {
      int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
      sscanf(v.s.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s);
      return Value::Time(y, mo, d, h, mi, s);
    }
    return Value();
  case SQL_VARBINARY: case SQL_BINARY: case SQL_LONGVARBINARY:
    return Value::Binary(v.s);
  default:
    return Value::Text(v.Str());
  }
}

struct Diag {
  std::string state, message;
  void Set(const char* st, const std::string& msg) {
    state = st;
    message = "[Mock][Engine]" + msg;
  }
  void Clear() { state.clear(); message.clear(); }
};

struct Env { int tag = 1; Diag diag; };
struct Conn { int tag = 2; Diag diag; bool connected = false; };
struct Param {
  SQLSMALLINT ctype = 0;
  SQLPOINTER p = nullptr;
  SQLLEN buflen = 0;
  SQLLEN* ind = nullptr;
};
struct Stmt {
  int tag = 3;
  Diag diag;
  Conn* conn = nullptr;
  std::string sql;
  std::vector<Param> params;
  std::vector<Column> resultCols;
  std::vector<std::vector<Value>> result;
  long cur = -1;
  // A keyset-driven cursor: the identities of the result's rows, their
  // table and columns, read again by SQLFetchScroll.
  SQLULEN cursorType = SQL_CURSOR_FORWARD_ONLY;
  SQLUSMALLINT* rowStatus = nullptr;
  std::string keyTable;
  std::vector<long> keys;
  std::vector<int> keyCols;
  int lastCol = 0;
  size_t offset = 0;
  SQLLEN rowCount = -1;
  // Output parameters a procedure sets once its result set is done
  // (SQLMoreResults), as drivers return them.
  std::vector<std::pair<int, Value>> pendingOut;
};

// Puts a value into a bound output parameter.
inline void WriteParam(Stmt* stmt, int n, const Value& v) {
  if (n >= (int)stmt->params.size() || stmt->params[n].p == nullptr) return;
  Param& prm = stmt->params[n];
  if (v.kind == kNull) {
    if (prm.ind) *prm.ind = SQL_NULL_DATA;
    return;
  }
  SQLLEN len = 0;
  switch (prm.ctype) {
  case SQL_C_CHAR: {
    size_t n2 = v.s.size() < (size_t)prm.buflen ? v.s.size() : (size_t)prm.buflen - 1;
    memcpy(prm.p, v.s.data(), n2);
    ((char*)prm.p)[n2] = 0;
    len = (SQLLEN)v.s.size();
    break;
  }
  case SQL_C_WCHAR: {
    size_t nMax = (size_t)prm.buflen / sizeof(WCHAR);
    size_t n2 = v.s.size() < nMax ? v.s.size() : nMax - 1;
    WCHAR* p = (WCHAR*)prm.p;
    for (size_t k = 0; k < n2; k++) p[k] = (WCHAR)(unsigned char)v.s[k];
    p[n2] = 0;
    len = (SQLLEN)(v.s.size() * sizeof(WCHAR));
    break;
  }
  case SQL_C_SLONG: case SQL_C_LONG: *(int*)prm.p = (int)v.i; len = 4; break;
  case SQL_C_DOUBLE: *(double*)prm.p = v.kind == kReal ? v.d : (double)v.i; len = 8; break;
  default: return;
  }
  if (prm.ind) *prm.ind = len;
}

// ---------------------------------------------------------------------
// A small SQL reader

struct Token {
  enum Type { kEnd, kWord, kQuoted, kNumber, kString, kSymbol } type;
  std::string text;
};

inline std::vector<Token> Tokenize(const std::string& sql) {
  std::vector<Token> out;
  size_t i = 0;
  while (i < sql.size()) {
    char c = sql[i];
    if (isspace((unsigned char)c)) { i++; continue; }
    if (isalpha((unsigned char)c) || c == '_') {
      size_t j = i;
      while (j < sql.size() && (isalnum((unsigned char)sql[j]) || sql[j] == '_')) j++;
      out.push_back({Token::kWord, sql.substr(i, j - i)});
      i = j;
    } else if (c == '"' || c == '`') {
      size_t j = sql.find(c, i + 1);
      if (j == std::string::npos) j = sql.size();
      out.push_back({Token::kQuoted, sql.substr(i + 1, j - i - 1)});
      i = j + 1;
    } else if (isdigit((unsigned char)c) || (c == '-' && i + 1 < sql.size() && isdigit((unsigned char)sql[i + 1]))) {
      size_t j = i + 1;
      while (j < sql.size() && (isdigit((unsigned char)sql[j]) || sql[j] == '.')) j++;
      out.push_back({Token::kNumber, sql.substr(i, j - i)});
      i = j;
    } else if (c == '\'') {
      std::string s;
      size_t j = i + 1;
      while (j < sql.size()) {
        if (sql[j] == '\'' && j + 1 < sql.size() && sql[j + 1] == '\'') { s += '\''; j += 2; continue; }
        if (sql[j] == '\'') break;
        s += sql[j++];
      }
      out.push_back({Token::kString, s});
      i = j + 1;
    } else if ((c == '<' || c == '>' || c == '!') && i + 1 < sql.size() && (sql[i + 1] == '=' || sql[i + 1] == '>')) {
      out.push_back({Token::kSymbol, sql.substr(i, 2)});
      i += 2;
    } else {
      out.push_back({Token::kSymbol, std::string(1, c)});
      i++;
    }
  }
  out.push_back({Token::kEnd, ""});
  return out;
}

struct Error {
  const char* state;
  std::string message;
};

struct Cond {
  int col;
  std::string op;  // = <> < > <= >= NULL NOTNULL
  Value value;
};

struct Parser {
  std::vector<Token> t;
  size_t pos = 0;
  Stmt* stmt;
  int nextParam = 0;

  const Token& Peek() { return t[pos]; }
  bool IsWord(const char* w) { return Peek().type == Token::kWord && strcasecmp(Peek().text.c_str(), w) == 0; }
  bool IsSym(const char* s) { return Peek().type == Token::kSymbol && Peek().text == s; }
  void ExpectWord(const char* w) {
    if (!IsWord(w)) throw Error{"42000", std::string("Syntax error: expected ") + w + " near '" + Peek().text + "'"};
    pos++;
  }
  void ExpectSym(const char* s) {
    if (!IsSym(s)) throw Error{"42000", std::string("Syntax error: expected ") + s + " near '" + Peek().text + "'"};
    pos++;
  }
  std::string Name() {
    const Token& tok = Peek();
    if (tok.type != Token::kWord && tok.type != Token::kQuoted)
      throw Error{"42000", "Syntax error: expected a name near '" + tok.text + "'"};
    pos++;
    return tok.text;
  }
  Value ParamValue(int n) {
    if (n >= (int)stmt->params.size() || stmt->params[n].p == nullptr) throw Error{"07002", "A parameter is not bound"};
    const Param& prm = stmt->params[n];
    SQLLEN len = prm.ind ? *prm.ind : SQL_NTS;
    if (len == SQL_NULL_DATA) return Value();
    switch (prm.ctype) {
    case SQL_C_CHAR: {
      const char* p = (const char*)prm.p;
      return Value::Text(len == SQL_NTS ? std::string(p) : std::string(p, (size_t)len));
    }
    case SQL_C_WCHAR: {
      const WCHAR* p = (const WCHAR*)prm.p;
      size_t n2 = len == SQL_NTS ? wcslen((const wchar_t*)p) : (size_t)len / sizeof(WCHAR);
      std::string s;
      for (size_t k = 0; k < n2; k++) s += (char)p[k];
      return Value::Text(s);
    }
    case SQL_C_SLONG: case SQL_C_LONG: return Value::Int(*(const int*)prm.p);
    case SQL_C_SSHORT: case SQL_C_SHORT: return Value::Int(*(const short*)prm.p);
    case SQL_C_UTINYINT: case SQL_C_BIT: return Value::Int(*(const unsigned char*)prm.p);
    case SQL_C_SBIGINT: return Value::Int(*(const long long*)prm.p);
    case SQL_C_FLOAT: return Value::Real(*(const float*)prm.p);
    case SQL_C_DOUBLE: return Value::Real(*(const double*)prm.p);
    case SQL_C_TIMESTAMP: {
      Value v;
      v.kind = kTime;
      v.ts = *(const TIMESTAMP_STRUCT*)prm.p;
      v.ts.fraction = 0;
      return v;
    }
    case SQL_C_BINARY: return Value::Binary(std::string((const char*)prm.p, (size_t)len));
    }
    throw Error{"HY003", "Unsupported parameter type"};
  }
  Value Expr() {
    const Token& tok = Peek();
    pos++;
    if (tok.type == Token::kSymbol && tok.text == "?") return ParamValue(nextParam++);
    if (tok.type == Token::kNumber) {
      if (tok.text.find('.') != std::string::npos) return Value::Real(atof(tok.text.c_str()));
      return Value::Int(atoll(tok.text.c_str()));
    }
    if (tok.type == Token::kString) return Value::Text(tok.text);
    if (tok.type == Token::kWord && strcasecmp(tok.text.c_str(), "NULL") == 0) return Value();
    throw Error{"42000", "Syntax error: expected a value near '" + tok.text + "'"};
  }
  int Col(Table* table) {
    std::string name = Name();
    int c = table->Find(name);
    if (c < 0) throw Error{"42S22", "Unknown column " + name};
    return c;
  }
  std::vector<Cond> Where(Table* table) {
    std::vector<Cond> conds;
    if (!IsWord("WHERE")) return conds;
    pos++;
    for (;;) {
      Cond cond;
      cond.col = Col(table);
      if (IsWord("IS")) {
        pos++;
        if (IsWord("NOT")) { pos++; cond.op = "NOTNULL"; } else cond.op = "NULL";
        ExpectWord("NULL");
      } else {
        const Token& op = Peek();
        if (op.type != Token::kSymbol) throw Error{"42000", "Syntax error near '" + op.text + "'"};
        cond.op = op.text;
        pos++;
        cond.value = Expr();
      }
      conds.push_back(cond);
      if (!IsWord("AND")) break;
      pos++;
    }
    return conds;
  }
  static bool Match(const std::vector<Value>& row, const std::vector<Cond>& conds) {
    for (const Cond& c : conds) {
      const Value& v = row[c.col];
      bool ok;
      if (c.op == "NULL") ok = v.kind == kNull;
      else if (c.op == "NOTNULL") ok = v.kind != kNull;
      else if (v.kind == kNull || c.value.kind == kNull) ok = false;
      else if (c.op == "=") ok = Equal(v, c.value);
      else if (c.op == "<>" || c.op == "!=") ok = !Equal(v, c.value);
      else if (c.op == "<") ok = Compare(v, c.value) < 0;
      else if (c.op == ">") ok = Compare(v, c.value) > 0;
      else if (c.op == "<=") ok = Compare(v, c.value) <= 0;
      else if (c.op == ">=") ok = Compare(v, c.value) >= 0;
      else throw Error{"42000", "Unknown operator " + c.op};
      if (!ok) return false;
    }
    return true;
  }
  Table* TableName() {
    std::string name = Name();
    Table* table = G().Find(name);
    if (!table) throw Error{"42S02", "Unknown table " + name};
    table->SyncIds();
    return table;
  }
  void End() {
    if (Peek().type != Token::kEnd) throw Error{"42000", "Syntax error near '" + Peek().text + "'"};
  }

  // {[? =] CALL name(?, ...)}: the procedures the tests call.
  //   GREET(name, greeting): returns the greeting's length and gives
  //     "Hello, <name>" in its output parameter, after a result set of
  //     two rows (WORD: "Hello" and the name); a NULL name gives NULLs.
  //   BUMP(n): adds 10 to its input/output parameter, with no result set.
  void Call() {
    pos++;  // {
    bool bReturn = false;
    if (IsSym("?")) {
      pos++;
      ExpectSym("=");
      bReturn = true;
    }
    ExpectWord("CALL");
    std::string name = Name();
    int nArgs = 0;
    if (IsSym("(")) {
      pos++;
      while (!IsSym(")")) {
        if (IsSym("?")) nArgs++;
        else if (!IsSym(",")) throw Error{"42000", "Syntax error near '" + Peek().text + "'"};
        pos++;
      }
      pos++;
    }
    ExpectSym("}");
    int base = bReturn ? 1 : 0;
    stmt->resultCols.clear();
    stmt->result.clear();
    stmt->pendingOut.clear();
    stmt->rowCount = -1;
    if (strcasecmp(name.c_str(), "GREET") == 0 && bReturn && nArgs == 2) {
      Value who = ParamValue(base);
      stmt->resultCols.push_back({"WORD", SQL_VARCHAR, false});
      stmt->result.push_back({Value::Text("Hello")});
      stmt->result.push_back({who});
      if (who.kind == kNull) {
        stmt->pendingOut.push_back({0, Value::Int(0)});
        stmt->pendingOut.push_back({base + 1, Value()});
      } else {
        std::string greeting = "Hello, " + who.s;
        stmt->pendingOut.push_back({0, Value::Int((long long)greeting.size())});
        stmt->pendingOut.push_back({base + 1, Value::Text(greeting)});
      }
    } else if (strcasecmp(name.c_str(), "BUMP") == 0 && !bReturn && nArgs == 1) {
      Value v = ParamValue(base);
      WriteParam(stmt, base, Value::Int(v.i + 10));
      stmt->rowCount = 1;
    } else {
      throw Error{"42000", "Unknown procedure " + name};
    }
  }

  void Run() {
    State& g = G();
    if (IsSym("{")) {
      Call();
      return;
    }
    if (IsWord("SELECT")) {
      pos++;
      std::vector<std::string> names;
      bool all = false;
      if (IsSym("*")) { all = true; pos++; }
      else {
        for (;;) {
          names.push_back(Name());
          if (!IsSym(",")) break;
          pos++;
        }
      }
      ExpectWord("FROM");
      Table* table = TableName();
      std::vector<int> cols;
      if (all) for (size_t i = 0; i < table->cols.size(); i++) cols.push_back((int)i);
      for (auto& n : names) {
        int c = table->Find(n);
        if (c < 0) throw Error{"42S22", "Unknown column " + n};
        cols.push_back(c);
      }
      std::vector<Cond> conds = Where(table);
      int sortCol = -1;
      bool desc = false;
      if (IsWord("ORDER")) {
        pos++;
        ExpectWord("BY");
        sortCol = Col(table);
        if (IsWord("DESC")) { desc = true; pos++; } else if (IsWord("ASC")) pos++;
      }
      End();
      std::vector<size_t> rows;
      for (size_t i = 0; i < table->rows.size(); i++)
        if (Match(table->rows[i], conds)) rows.push_back(i);
      if (sortCol >= 0)
        std::stable_sort(rows.begin(), rows.end(), [&](size_t a, size_t b) {
          int c = Compare(table->rows[a][sortCol], table->rows[b][sortCol]);
          return desc ? c > 0 : c < 0;
        });
      stmt->resultCols.clear();
      stmt->result.clear();
      stmt->keys.clear();
      stmt->keyTable = table->name;
      stmt->keyCols = cols;
      for (int c : cols) stmt->resultCols.push_back(table->cols[c]);
      for (size_t r : rows) {
        std::vector<Value> out;
        for (int c : cols) out.push_back(table->rows[r][c]);
        stmt->result.push_back(out);
        stmt->keys.push_back(table->ids[r]);
      }
      stmt->rowCount = -1;
      return;
    }
    if (g.readOnly) throw Error{"42000", "The data source is read-only"};
    stmt->resultCols.clear();
    stmt->result.clear();
    if (IsWord("INSERT")) {
      pos++;
      ExpectWord("INTO");
      Table* table = TableName();
      std::vector<int> cols;
      ExpectSym("(");
      for (;;) {
        cols.push_back(Col(table));
        if (!IsSym(",")) break;
        pos++;
      }
      ExpectSym(")");
      ExpectWord("VALUES");
      ExpectSym("(");
      std::vector<Value> row(table->cols.size());
      for (size_t k = 0; k < cols.size(); k++) {
        if (k) ExpectSym(",");
        row[cols[k]] = Coerce(Expr(), table->cols[cols[k]].type);
      }
      ExpectSym(")");
      End();
      for (size_t c = 0; c < row.size(); c++)
        if (table->cols[c].notNull && row[c].kind == kNull) throw Error{"23000", "Column " + table->cols[c].name + " cannot be null"};
      table->rows.push_back(row);
      table->ids.push_back(table->nextId++);
      stmt->rowCount = 1;
      return;
    }
    if (IsWord("UPDATE")) {
      pos++;
      Table* table = TableName();
      ExpectWord("SET");
      std::vector<std::pair<int, Value>> sets;
      for (;;) {
        int c = Col(table);
        ExpectSym("=");
        sets.push_back({c, Coerce(Expr(), table->cols[c].type)});
        if (!IsSym(",")) break;
        pos++;
      }
      std::vector<Cond> conds = Where(table);
      End();
      stmt->rowCount = 0;
      for (auto& r : table->rows) {
        if (!Match(r, conds)) continue;
        for (auto& s : sets) r[s.first] = s.second;
        stmt->rowCount++;
      }
      return;
    }
    if (IsWord("DELETE")) {
      pos++;
      ExpectWord("FROM");
      Table* table = TableName();
      std::vector<Cond> conds = Where(table);
      End();
      size_t before = table->rows.size();
      for (size_t i = table->rows.size(); i-- > 0;) {
        if (!Match(table->rows[i], conds)) continue;
        table->rows.erase(table->rows.begin() + (long)i);
        table->ids.erase(table->ids.begin() + (long)i);
      }
      stmt->rowCount = (SQLLEN)(before - table->rows.size());
      return;
    }
    throw Error{"42000", "Unsupported statement near '" + Peek().text + "'"};
  }
};

template <class C>
inline std::string Narrow(const C* p, SQLINTEGER n) {
  std::string s;
  if (!p) return s;
  if (n == SQL_NTS) for (; *p; p++) s += (char)*p;
  else for (SQLINTEGER i = 0; i < n; i++) s += (char)p[i];
  return s;
}

template <class C>
inline void CopyOut(const std::string& s, C* p, SQLSMALLINT nMax, SQLSMALLINT* pn) {
  if (pn) *pn = (SQLSMALLINT)s.size();
  if (!p || nMax <= 0) return;
  SQLSMALLINT n = (SQLSMALLINT)std::min<size_t>(s.size(), (size_t)nMax - 1);
  for (SQLSMALLINT i = 0; i < n; i++) p[i] = (C)(unsigned char)s[i];
  p[n] = 0;
}

inline Diag* DiagOf(SQLSMALLINT type, SQLHANDLE h) {
  if (!h) return nullptr;
  if (type == SQL_HANDLE_ENV) return &((Env*)h)->diag;
  if (type == SQL_HANDLE_DBC) return &((Conn*)h)->diag;
  if (type == SQL_HANDLE_STMT) return &((Stmt*)h)->diag;
  return nullptr;
}

inline SQLRETURN Execute(Stmt* stmt) {
  stmt->diag.Clear();
  stmt->cur = -1;
  stmt->lastCol = 0;
  stmt->offset = 0;
  G().log.push_back(stmt->sql);
  try {
    Parser parser;
    parser.t = Tokenize(stmt->sql);
    parser.stmt = stmt;
    parser.Run();
  } catch (const Error& e) {
    stmt->diag.Set(e.state, e.message);
    return SQL_ERROR;
  }
  if (stmt->resultCols.empty() && stmt->rowCount == 0) return SQL_NO_DATA;
  return SQL_SUCCESS;
}

}  // namespace mock

// ---------------------------------------------------------------------
// The ODBC functions

SQLRETURN SQL_API SQLAllocHandle(SQLSMALLINT HandleType, SQLHANDLE InputHandle, SQLHANDLE* OutputHandle) {
  switch (HandleType) {
  case SQL_HANDLE_ENV: *OutputHandle = new mock::Env; return SQL_SUCCESS;
  case SQL_HANDLE_DBC: *OutputHandle = new mock::Conn; return SQL_SUCCESS;
  case SQL_HANDLE_STMT: {
    mock::Conn* conn = (mock::Conn*)InputHandle;
    if (!conn || !conn->connected) return SQL_INVALID_HANDLE;
    mock::Stmt* stmt = new mock::Stmt;
    stmt->conn = conn;
    *OutputHandle = stmt;
    mock::G().nOpenStatements++;
    return SQL_SUCCESS;
  }
  }
  return SQL_ERROR;
}

SQLRETURN SQL_API SQLFreeHandle(SQLSMALLINT HandleType, SQLHANDLE Handle) {
  if (HandleType == SQL_HANDLE_ENV) delete (mock::Env*)Handle;
  else if (HandleType == SQL_HANDLE_DBC) delete (mock::Conn*)Handle;
  else if (HandleType == SQL_HANDLE_STMT) {
    delete (mock::Stmt*)Handle;
    mock::G().nOpenStatements--;
  }
  return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLSetEnvAttr(SQLHENV, SQLINTEGER, SQLPOINTER, SQLINTEGER) { return SQL_SUCCESS; }

SQLRETURN SQL_API SQLSetConnectAttr(SQLHDBC, SQLINTEGER Attribute, SQLPOINTER Value, SQLINTEGER) {
  mock::State& g = mock::G();
  if (Attribute == SQL_ATTR_AUTOCOMMIT) {
    if ((SQLULEN)Value == SQL_AUTOCOMMIT_OFF) {
      g.saved = g.tables;
      g.inTransaction = true;
    } else {
      g.inTransaction = false;
    }
  }
  return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLEndTran(SQLSMALLINT, SQLHANDLE, SQLSMALLINT CompletionType) {
  mock::State& g = mock::G();
  if (!g.inTransaction) return SQL_SUCCESS;
  if (CompletionType == SQL_ROLLBACK) g.tables = g.saved;
  g.saved = g.tables;
  return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLSetStmtAttr(SQLHSTMT hstmt, SQLINTEGER Attribute, SQLPOINTER Value, SQLINTEGER) {
  mock::Stmt* stmt = (mock::Stmt*)hstmt;
  if (Attribute == SQL_ATTR_CURSOR_TYPE) {
    SQLULEN type = (SQLULEN)Value;
    // A driver changes a cursor type it does not have to the nearest one.
    if (type == SQL_CURSOR_DYNAMIC) type = SQL_CURSOR_KEYSET_DRIVEN;
    if (type == SQL_CURSOR_KEYSET_DRIVEN && mock::G().noKeyset) type = SQL_CURSOR_STATIC;
    stmt->cursorType = type;
    if (type != (SQLULEN)Value) {
      stmt->diag.Set("01S02", "Option value changed");
      return SQL_SUCCESS_WITH_INFO;
    }
  } else if (Attribute == SQL_ATTR_ROW_STATUS_PTR) {
    stmt->rowStatus = (SQLUSMALLINT*)Value;
  }
  return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLGetStmtAttr(SQLHSTMT hstmt, SQLINTEGER Attribute, SQLPOINTER Value, SQLINTEGER,
                                 SQLINTEGER*) {
  mock::Stmt* stmt = (mock::Stmt*)hstmt;
  if (Attribute != SQL_ATTR_CURSOR_TYPE) return SQL_ERROR;
  *(SQLULEN*)Value = stmt->cursorType;
  return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLDriverConnect(SQLHDBC hdbc, SQLHWND, SQLTCHAR* szConnStrIn, SQLSMALLINT cbConnStrIn,
                                   SQLTCHAR* szConnStrOut, SQLSMALLINT cbConnStrOutMax,
                                   SQLSMALLINT* pcbConnStrOut, SQLUSMALLINT fDriverCompletion) {
  mock::Conn* conn = (mock::Conn*)hdbc;
  std::string in = mock::Narrow(szConnStrIn, cbConnStrIn);
  std::string upper = in;
  for (auto& c : upper) c = (char)toupper((unsigned char)c);
  if (upper.find("DSN=MOCK") == std::string::npos) {
    if (fDriverCompletion != SQL_DRIVER_NOPROMPT && in.empty()) return SQL_NO_DATA;  // "cancelled"
    conn->diag.state = "IM002";
    conn->diag.message = "[Mock][Driver Manager]Data source name not found";
    return SQL_ERROR;
  }
  conn->connected = true;
  mock::CopyOut(std::string("DSN=Mock;DATABASE=mockdb"), szConnStrOut, cbConnStrOutMax, pcbConnStrOut);
  return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLDisconnect(SQLHDBC hdbc) {
  ((mock::Conn*)hdbc)->connected = false;
  return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLGetInfo(SQLHDBC, SQLUSMALLINT InfoType, SQLPOINTER InfoValue, SQLSMALLINT BufferLength,
                             SQLSMALLINT* StringLength) {
  std::string s;
  switch (InfoType) {
  case SQL_DATA_SOURCE_READ_ONLY: s = mock::G().readOnly ? "Y" : "N"; break;
  case SQL_IDENTIFIER_QUOTE_CHAR: s = "\""; break;
  case SQL_DATABASE_NAME: s = "mockdb"; break;
  case SQL_TXN_CAPABLE: *(SQLUSMALLINT*)InfoValue = SQL_TC_ALL; return SQL_SUCCESS;
  case SQL_CURSOR_COMMIT_BEHAVIOR:
  case SQL_CURSOR_ROLLBACK_BEHAVIOR: *(SQLUSMALLINT*)InfoValue = SQL_CB_PRESERVE; return SQL_SUCCESS;
  case SQL_BOOKMARK_PERSISTENCE: *(SQLUINTEGER*)InfoValue = 0; return SQL_SUCCESS;
  default: return SQL_ERROR;
  }
  SQLTCHAR* p = (SQLTCHAR*)InfoValue;
  SQLSMALLINT nChars = (SQLSMALLINT)(BufferLength / sizeof(SQLTCHAR));
  mock::CopyOut(s, p, nChars, StringLength);
  if (StringLength) *StringLength = (SQLSMALLINT)(s.size() * sizeof(SQLTCHAR));
  return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLGetDiagRec(SQLSMALLINT HandleType, SQLHANDLE Handle, SQLSMALLINT RecNumber, SQLTCHAR* Sqlstate,
                                SQLINTEGER* NativeError, SQLTCHAR* MessageText, SQLSMALLINT BufferLength,
                                SQLSMALLINT* TextLength) {
  mock::Diag* diag = mock::DiagOf(HandleType, Handle);
  if (!diag || RecNumber != 1 || diag->state.empty()) return SQL_NO_DATA;
  mock::CopyOut(diag->state, Sqlstate, 6, nullptr);
  if (NativeError) *NativeError = 42;
  mock::CopyOut(diag->message, MessageText, BufferLength, TextLength);
  return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLPrepare(SQLHSTMT hstmt, SQLTCHAR* StatementText, SQLINTEGER TextLength) {
  mock::Stmt* stmt = (mock::Stmt*)hstmt;
  stmt->sql = mock::Narrow(StatementText, TextLength);
  return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLExecute(SQLHSTMT hstmt) { return mock::Execute((mock::Stmt*)hstmt); }

SQLRETURN SQL_API SQLExecDirect(SQLHSTMT hstmt, SQLTCHAR* StatementText, SQLINTEGER TextLength) {
  SQLPrepare(hstmt, StatementText, TextLength);
  return mock::Execute((mock::Stmt*)hstmt);
}

SQLRETURN SQL_API SQLBindParameter(SQLHSTMT hstmt, SQLUSMALLINT ipar, SQLSMALLINT, SQLSMALLINT fCType, SQLSMALLINT,
                                   SQLULEN, SQLSMALLINT, SQLPOINTER rgbValue, SQLLEN cbValueMax,
                                   SQLLEN* pcbValue) {
  mock::Stmt* stmt = (mock::Stmt*)hstmt;
  if (ipar == 0) return SQL_ERROR;
  if (stmt->params.size() < ipar) stmt->params.resize(ipar);
  stmt->params[ipar - 1] = {fCType, rgbValue, cbValueMax, pcbValue};
  return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLNumResultCols(SQLHSTMT hstmt, SQLSMALLINT* ColumnCount) {
  *ColumnCount = (SQLSMALLINT)((mock::Stmt*)hstmt)->resultCols.size();
  return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLDescribeCol(SQLHSTMT hstmt, SQLUSMALLINT ColumnNumber, SQLTCHAR* ColumnName,
                                 SQLSMALLINT BufferLength, SQLSMALLINT* NameLength, SQLSMALLINT* DataType,
                                 SQLULEN* ColumnSize, SQLSMALLINT* DecimalDigits, SQLSMALLINT* Nullable) {
  mock::Stmt* stmt = (mock::Stmt*)hstmt;
  if (ColumnNumber < 1 || ColumnNumber > stmt->resultCols.size()) return SQL_ERROR;
  const mock::Column& col = stmt->resultCols[ColumnNumber - 1];
  mock::CopyOut(col.name, ColumnName, BufferLength, NameLength);
  if (DataType) *DataType = col.type;
  if (ColumnSize) *ColumnSize = col.type == SQL_VARCHAR ? 255 : col.type == SQL_TIMESTAMP ? 19 : 10;
  if (DecimalDigits) *DecimalDigits = 0;
  if (Nullable) *Nullable = col.notNull ? SQL_NO_NULLS : SQL_NULLABLE;
  return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLFetch(SQLHSTMT hstmt) {
  mock::Stmt* stmt = (mock::Stmt*)hstmt;
  if (stmt->resultCols.empty()) {
    stmt->diag.Set("24000", "No result set");
    return SQL_ERROR;
  }
  stmt->lastCol = 0;
  stmt->offset = 0;
  if (stmt->cur + 1 >= (long)stmt->result.size()) {
    stmt->cur = (long)stmt->result.size();
    return SQL_NO_DATA;
  }
  stmt->cur++;
  return SQL_SUCCESS;
}

// Scrolls a static or keyset-driven cursor. A keyset-driven one reads the
// row again: its current values, or SQL_ROW_DELETED once it is gone.
SQLRETURN SQL_API SQLFetchScroll(SQLHSTMT hstmt, SQLSMALLINT FetchOrientation, SQLLEN FetchOffset) {
  mock::Stmt* stmt = (mock::Stmt*)hstmt;
  if (FetchOrientation == SQL_FETCH_NEXT) return SQLFetch(hstmt);
  if (stmt->cursorType == SQL_CURSOR_FORWARD_ONLY) {
    stmt->diag.Set("HY106", "Fetch type out of range");
    return SQL_ERROR;
  }
  if (FetchOrientation != SQL_FETCH_ABSOLUTE) {
    stmt->diag.Set("HYC00", "Optional feature not implemented");
    return SQL_ERROR;
  }
  stmt->lastCol = 0;
  stmt->offset = 0;
  if (FetchOffset < 1 || FetchOffset > (SQLLEN)stmt->result.size()) {
    stmt->cur = FetchOffset < 1 ? -1 : (long)stmt->result.size();
    return SQL_NO_DATA;
  }
  stmt->cur = (long)FetchOffset - 1;
  SQLUSMALLINT status = SQL_ROW_SUCCESS;
  if (stmt->cursorType == SQL_CURSOR_KEYSET_DRIVEN && stmt->cur < (long)stmt->keys.size()) {
    mock::Table* table = mock::G().Find(stmt->keyTable);
    status = SQL_ROW_DELETED;
    if (table) {
      table->SyncIds();
      for (size_t i = 0; i < table->ids.size(); i++) {
        if (table->ids[i] != stmt->keys[stmt->cur]) continue;
        std::vector<mock::Value>& out = stmt->result[stmt->cur];
        for (size_t c = 0; c < stmt->keyCols.size(); c++) {
          if (!mock::Equal(out[c], table->rows[i][stmt->keyCols[c]])) status = SQL_ROW_UPDATED;
          out[c] = table->rows[i][stmt->keyCols[c]];
        }
        if (status == SQL_ROW_DELETED) status = SQL_ROW_SUCCESS;
        break;
      }
    }
  }
  if (stmt->rowStatus) *stmt->rowStatus = status;
  return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLGetData(SQLHSTMT hstmt, SQLUSMALLINT Col, SQLSMALLINT TargetType, SQLPOINTER TargetValue,
                             SQLLEN BufferLength, SQLLEN* StrLen_or_Ind) {
  mock::Stmt* stmt = (mock::Stmt*)hstmt;
  if (stmt->cur < 0 || stmt->cur >= (long)stmt->result.size()) {
    stmt->diag.Set("24000", "No current row");
    return SQL_ERROR;
  }
  if (Col < 1 || Col > stmt->resultCols.size()) {
    stmt->diag.Set("07009", "Invalid column number");
    return SQL_ERROR;
  }
  if ((int)Col < stmt->lastCol) {
    stmt->diag.Set("07009", "Columns must be read in order");
    return SQL_ERROR;
  }
  if ((int)Col != stmt->lastCol) stmt->offset = 0;
  stmt->lastCol = Col;
  const mock::Value& v = stmt->result[stmt->cur][Col - 1];
  if (v.kind == mock::kNull) {
    if (stmt->offset > 0) return SQL_NO_DATA;
    stmt->offset = 1;
    if (!StrLen_or_Ind) return SQL_ERROR;
    *StrLen_or_Ind = SQL_NULL_DATA;
    return SQL_SUCCESS;
  }
  switch (TargetType) {
  case SQL_C_CHAR:
  case SQL_C_WCHAR:
  case SQL_C_BINARY: {
    std::string s = v.kind == mock::kBinary ? v.s : v.Str();
    size_t unit = TargetType == SQL_C_WCHAR ? 2 : 1;
    size_t term = TargetType == SQL_C_BINARY ? 0 : unit;
    size_t total = s.size() * unit;
    if (stmt->offset > 0 && stmt->offset >= total) return SQL_NO_DATA;
    size_t room = (size_t)BufferLength > term ? (size_t)BufferLength - term : 0;
    room -= room % unit;
    size_t remaining = total - stmt->offset;
    size_t n = std::min(room, remaining);
    BYTE* out = (BYTE*)TargetValue;
    for (size_t k = 0; k < n / unit; k++) {
      unsigned char ch = (unsigned char)s[stmt->offset / unit + k];
      if (unit == 2) ((WCHAR*)out)[k] = ch;
      else out[k] = ch;
    }
    if (term) memset(out + n, 0, term);
    if (StrLen_or_Ind) *StrLen_or_Ind = (SQLLEN)remaining;
    stmt->offset += n;
    if (stmt->offset == 0) stmt->offset = 1;  // an empty value has been read
    if (n < remaining) {
      stmt->diag.Set("01004", "String data, right truncated");
      return SQL_SUCCESS_WITH_INFO;
    }
    return SQL_SUCCESS;
  }
  case SQL_C_SLONG: case SQL_C_LONG: *(int*)TargetValue = (int)v.Num(); break;
  case SQL_C_SSHORT: case SQL_C_SHORT: *(short*)TargetValue = (short)v.Num(); break;
  case SQL_C_UTINYINT: case SQL_C_BIT: *(unsigned char*)TargetValue = (unsigned char)v.Num(); break;
  case SQL_C_SBIGINT: *(long long*)TargetValue = (long long)v.Num(); break;
  case SQL_C_FLOAT: *(float*)TargetValue = (float)v.Num(); break;
  case SQL_C_DOUBLE: *(double*)TargetValue = v.Num(); break;
  case SQL_C_TIMESTAMP: {
    mock::Value t = mock::Coerce(v, SQL_TIMESTAMP);
    *(TIMESTAMP_STRUCT*)TargetValue = t.ts;
    break;
  }
  default:
    stmt->diag.Set("HY003", "Unsupported C type");
    return SQL_ERROR;
  }
  if (StrLen_or_Ind) *StrLen_or_Ind = 0;
  return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLRowCount(SQLHSTMT hstmt, SQLLEN* RowCount) {
  *RowCount = ((mock::Stmt*)hstmt)->rowCount;
  return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLMoreResults(SQLHSTMT hstmt) {
  mock::Stmt* stmt = (mock::Stmt*)hstmt;
  // No more result sets: a procedure's output parameters arrive.
  for (auto& out : stmt->pendingOut) mock::WriteParam(stmt, out.first, out.second);
  stmt->pendingOut.clear();
  stmt->resultCols.clear();
  stmt->result.clear();
  return SQL_NO_DATA;
}

SQLRETURN SQL_API SQLCancel(SQLHSTMT) { return SQL_SUCCESS; }
