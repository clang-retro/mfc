// daomock.h - a stand-in for the DAO engine, for the DAO tests.
//
// Wine has no DAO, so the test registers this engine's class factory for
// the DAO 3.6 engine's CLSID. Like the real one, the class is licensed:
// it is made only through IClassFactory2 with the run-time key. The
// databases are kept in memory by file name. The engine understands the
// part of Jet SQL the DAO classes and the test write: PARAMETERS, SELECT
// with WHERE (AND, OR, parentheses, comparisons, IS NULL) and ORDER BY,
// INSERT, UPDATE and DELETE. Recordsets are tables (with an index and
// Seek), dynasets over the table's rows, or snapshots of their values.
#pragma once
#include <afxdisp.h>
#include <dbdaoint.h>
#include <dbdaoerr.h>
#include <dbdaoid.h>
#include <list>
#include <vector>
#include <algorithm>
#include "daostubs.h"

namespace daomock {

// DAO 3.6's engine, as the classes look for it first.
static const GUID CLSID_DBEngine36 = {0x00000100, 0, 0x10, {0x80, 0, 0, 0xAA, 0, 0x6D, 0x2E, 0xA4}};

// ---------------------------------------------------------------------
// Errors: the engine's Errors collection holds the last failure's.

struct ErrorInfo {
  long nCode;
  CString strDescription;
};

inline std::vector<ErrorInfo>& Errors() {
  static std::vector<ErrorInfo> errors;
  return errors;
}

inline HRESULT Fail(long nCode, LPCTSTR pszDescription) {
  Errors().clear();
  Errors().push_back({nCode, pszDescription});
  return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_CONTROL, nCode);
}

inline BSTR Bstr(const CString& str) { return ::SysAllocString(CStringW(str)); }

inline bool IsMissing(const VARIANT& v) {
  return v.vt == VT_ERROR || v.vt == VT_EMPTY;
}

inline CString VarString(const VARIANT& v) {
  if (v.vt == VT_BSTR) return CString(v.bstrVal);
  COleVariant c(v);
  if (c.vt == VT_NULL || c.vt == VT_EMPTY || c.vt == VT_ERROR) return CString();
  c.ChangeType(VT_BSTR);
  return CString(c.bstrVal);
}

inline long VarLong(const VARIANT& v, long lDefault = 0) {
  if (IsMissing(v)) return lDefault;
  COleVariant c(v);
  c.ChangeType(VT_I4);
  return c.lVal;
}

inline bool VarBool(const VARIANT& v) { return VarLong(v) != 0; }

// ---------------------------------------------------------------------
// The data

struct Column {
  CString name;
  short type = dbText;
  long size = 50;
  long attrs = dbUpdatableField;
  BOOL required = FALSE;
  BOOL allowZeroLength = FALSE;
  COleVariant defaultValue;
};

struct Index {
  CString name;
  std::vector<std::pair<CString, bool>> fields;  // name, descending
  BOOL primary = FALSE, unique = FALSE, clustered = FALSE, ignoreNulls = FALSE, required = FALSE;
};

struct Row {
  long id;
  std::vector<COleVariant> v;
};

struct Table {
  CString name;
  long attrs = 0;
  std::vector<Column> cols;
  std::vector<Row> rows;
  std::vector<Index> indexes;
  long nextId = 1;
  long nextAuto = 1;
  CString validationRule, validationText;
  COleDateTime created = COleDateTime(2001, 2, 3, 4, 5, 6);
  int Find(const CString& col) const {
    for (size_t i = 0; i < cols.size(); i++)
      if (cols[i].name.CompareNoCase(col) == 0) return (int)i;
    return -1;
  }
  Row* RowById(long id) {
    for (auto& r : rows)
      if (r.id == id) return &r;
    return NULL;
  }
};

struct QueryData {
  CString name, sql;
};

struct Relation {
  CString name, table, foreignTable;
  long attrs = 0;
  std::vector<std::pair<CString, CString>> fields;
};

struct Db {
  CString name;
  std::list<Table> tables;
  std::vector<QueryData> queries;
  std::vector<Relation> relations;
  bool readOnly = false;
  long recordsAffected = 0;
  short queryTimeout = 60;
  Table* Find(const CString& table) {
    for (auto& t : tables)
      if (t.name.CompareNoCase(table) == 0) return &t;
    return NULL;
  }
  QueryData* FindQuery(const CString& q) {
    for (auto& d : queries)
      if (d.name.CompareNoCase(q) == 0) return &d;
    return NULL;
  }
};

// The databases by file name, and the state a transaction began with.
struct State {
  std::list<Db> files;
  std::vector<std::pair<Db*, std::list<Table>>> saved;
  int transactionDepth = 0;
  CString iniPath, defaultUser;
  short loginTimeout = 20;
  int idles = 0;
  int engines = 0;
  int licensed = 0;  // engines made through the licence key
  int fieldWrites = 0;  // values put into a record being edited
  Db* Find(const CString& name) {
    for (auto& d : files)
      if (d.name.CompareNoCase(name) == 0) return &d;
    return NULL;
  }
};

inline State& G() {
  static State state;
  return state;
}

// A value as the column's type would store it.
inline HRESULT Coerce(const Column& col, const VARIANT& in, COleVariant& out) {
  out.Clear();
  if (in.vt == VT_NULL || in.vt == VT_EMPTY || in.vt == VT_ERROR) {
    out.vt = VT_NULL;
    return S_OK;
  }
  VARTYPE vt;
  switch (col.type) {
    case dbBoolean: vt = VT_BOOL; break;
    case dbByte: vt = VT_UI1; break;
    case dbInteger: vt = VT_I2; break;
    case dbLong: vt = VT_I4; break;
    case dbCurrency: vt = VT_CY; break;
    case dbSingle: vt = VT_R4; break;
    case dbDouble: vt = VT_R8; break;
    case dbDate: vt = VT_DATE; break;
    case dbBinary: case dbLongBinary: vt = VT_ARRAY | VT_UI1; break;
    default: vt = VT_BSTR; break;
  }
  if (vt == (VT_ARRAY | VT_UI1)) {
    if (in.vt != (VT_ARRAY | VT_UI1)) return Fail(3421, _T("Data type conversion error."));
    out = COleVariant(in);
    return S_OK;
  }
  out = COleVariant(in);
  if (FAILED(::VariantChangeType(&out, &out, 0, vt)))
    return Fail(3421, _T("Data type conversion error."));
  if (vt == VT_BSTR && col.type == dbText && (long)::SysStringLen(out.bstrVal) > col.size)
    return Fail(3163, _T("The field is too small to accept the amount of data you attempted to add."));
  return S_OK;
}

// Jet compares text without regard to case.
inline int Compare(const VARIANT& a, const VARIANT& b) {
  bool an = a.vt == VT_NULL || a.vt == VT_EMPTY, bn = b.vt == VT_NULL || b.vt == VT_EMPTY;
  if (an || bn) return (int)bn - (int)an;
  if (a.vt == VT_BSTR || b.vt == VT_BSTR) {
    if (a.vt == VT_BSTR && b.vt == VT_BSTR) return CString(a.bstrVal).CompareNoCase(CString(b.bstrVal));
  }
  COleVariant x(a), y(b);
  if (FAILED(::VariantChangeType(&x, &x, 0, VT_R8)) || FAILED(::VariantChangeType(&y, &y, 0, VT_R8)))
    return VarString(a).CompareNoCase(VarString(b));
  return x.dblVal < y.dblVal ? -1 : x.dblVal > y.dblVal ? 1 : 0;
}

// ---------------------------------------------------------------------
// A small Jet SQL reader

struct Token {
  enum Type { kEnd, kWord, kName, kNumber, kString, kDate, kSymbol } type;
  CString text;
};

inline std::vector<Token> Tokenize(const CString& sql) {
  std::vector<Token> out;
  int i = 0, n = sql.GetLength();
  while (i < n) {
    TCHAR c = sql[i];
    if (_istspace(c)) {
      i++;
    } else if (_istalpha(c) || c == '_') {
      int j = i;
      while (j < n && (_istalnum(sql[j]) || sql[j] == '_')) j++;
      out.push_back({Token::kWord, sql.Mid(i, j - i)});
      i = j;
    } else if (c == '[') {
      int j = sql.Find(']', i);
      if (j < 0) j = n;
      out.push_back({Token::kName, sql.Mid(i + 1, j - i - 1)});
      i = j + 1;
    } else if (_istdigit(c) || (c == '-' && i + 1 < n && _istdigit(sql[i + 1]))) {
      int j = i + 1;
      while (j < n && (_istdigit(sql[j]) || sql[j] == '.')) j++;
      out.push_back({Token::kNumber, sql.Mid(i, j - i)});
      i = j;
    } else if (c == '\'' || c == '"') {
      CString s;
      int j = i + 1;
      while (j < n) {
        if (sql[j] == c && j + 1 < n && sql[j + 1] == c) {
          s += c;
          j += 2;
          continue;
        }
        if (sql[j] == c) break;
        s += sql[j++];
      }
      out.push_back({Token::kString, s});
      i = j + 1;
    } else if (c == '#') {
      int j = sql.Find('#', i + 1);
      if (j < 0) j = n;
      out.push_back({Token::kDate, sql.Mid(i + 1, j - i - 1)});
      i = j + 1;
    } else if ((c == '<' || c == '>') && i + 1 < n && (sql[i + 1] == '=' || sql[i + 1] == '>')) {
      out.push_back({Token::kSymbol, sql.Mid(i, 2)});
      i += 2;
    } else {
      out.push_back({Token::kSymbol, CString(c)});
      i++;
    }
  }
  out.push_back({Token::kEnd, CString()});
  return out;
}

struct SqlError {
  HRESULT hr;
};

// A condition, evaluated against one row.
struct Expr {
  enum Kind { kAnd, kOr, kNot, kCompare, kIsNull, kNotNull, kTrue } kind = kTrue;
  std::vector<Expr> sub;
  int col = -1;
  CString op;
  COleVariant value;
  bool Eval(const std::vector<COleVariant>& row) const {
    switch (kind) {
      case kTrue: return true;
      case kAnd:
        for (auto& e : sub)
          if (!e.Eval(row)) return false;
        return true;
      case kOr:
        for (auto& e : sub)
          if (e.Eval(row)) return true;
        return false;
      case kNot: return !sub[0].Eval(row);
      case kIsNull: return row[col].vt == VT_NULL || row[col].vt == VT_EMPTY;
      case kNotNull: return !(row[col].vt == VT_NULL || row[col].vt == VT_EMPTY);
      case kCompare: {
        const COleVariant& v = row[col];
        if (v.vt == VT_NULL || v.vt == VT_EMPTY || value.vt == VT_NULL) return false;
        int c = Compare(v, value);
        if (op == _T("=")) return c == 0;
        if (op == _T("<>")) return c != 0;
        if (op == _T("<")) return c < 0;
        if (op == _T(">")) return c > 0;
        if (op == _T("<=")) return c <= 0;
        if (op == _T(">=")) return c >= 0;
        return false;
      }
    }
    return false;
  }
};

struct Param {
  CString name;
  short type = dbText;
  COleVariant value;
};

struct Statement {
  enum Kind { kSelect, kInsert, kUpdate, kDelete } kind = kSelect;
  Table* table = NULL;
  std::vector<int> cols;  // the SELECT list, or INSERT's columns
  Expr where;
  std::vector<std::pair<int, bool>> order;  // column, descending
  std::vector<std::pair<int, COleVariant>> sets;
  std::vector<COleVariant> values;
};

inline short ParamType(const CString& t) {
  if (t.CompareNoCase(_T("Long")) == 0) return dbLong;
  if (t.CompareNoCase(_T("Short")) == 0) return dbInteger;
  if (t.CompareNoCase(_T("Byte")) == 0) return dbByte;
  if (t.CompareNoCase(_T("Bit")) == 0) return dbBoolean;
  if (t.CompareNoCase(_T("Currency")) == 0) return dbCurrency;
  if (t.CompareNoCase(_T("IEEESingle")) == 0) return dbSingle;
  if (t.CompareNoCase(_T("IEEEDouble")) == 0) return dbDouble;
  if (t.CompareNoCase(_T("DateTime")) == 0) return dbDate;
  if (t.CompareNoCase(_T("LongBinary")) == 0) return dbLongBinary;
  return dbText;
}

// The PARAMETERS clause of a statement, if it has one.
inline std::vector<Param> ParseParams(const CString& sql) {
  std::vector<Param> params;
  std::vector<Token> t = Tokenize(sql);
  if (t[0].type != Token::kWord || t[0].text.CompareNoCase(_T("PARAMETERS")) != 0) return params;
  size_t i = 1;
  while (t[i].type != Token::kEnd && !(t[i].type == Token::kSymbol && t[i].text == _T(";"))) {
    Param p;
    p.name = t[i++].text;
    if (t[i].type == Token::kWord) p.type = ParamType(t[i++].text);
    p.value.vt = VT_NULL;
    params.push_back(p);
    if (t[i].type == Token::kSymbol && t[i].text == _T(",")) i++;
  }
  return params;
}

struct Parser {
  std::vector<Token> t;
  size_t pos = 0;
  Db* db;
  const std::vector<Param>* params;

  const Token& Peek() { return t[pos]; }
  bool IsWord(LPCTSTR w) { return Peek().type == Token::kWord && Peek().text.CompareNoCase(w) == 0; }
  bool IsSym(LPCTSTR s) { return Peek().type == Token::kSymbol && Peek().text == s; }
  [[noreturn]] void Syntax() {
    throw SqlError{Fail(3075, CString(_T("Syntax error near '")) + Peek().text + _T("'."))};
  }
  void ExpectWord(LPCTSTR w) {
    if (!IsWord(w)) Syntax();
    pos++;
  }
  void ExpectSym(LPCTSTR s) {
    if (!IsSym(s)) Syntax();
    pos++;
  }
  CString Name() {
    if (Peek().type != Token::kWord && Peek().type != Token::kName) Syntax();
    return t[pos++].text;
  }
  Table* TableName() {
    CString name = Name();
    Table* table = db->Find(name);
    if (table == NULL)
      throw SqlError{Fail(3078, CString(_T("The Microsoft Jet database engine cannot find the input table or query '")) + name + _T("'."))};
    return table;
  }
  int Col(Table* table) {
    CString name = Name();
    int c = table->Find(name);
    if (c < 0) throw SqlError{Fail(3265, CString(_T("Item not found in this collection: ")) + name)};
    return c;
  }
  const Param* FindParam(const CString& name) {
    if (params != NULL)
      for (auto& p : *params)
        if (p.name.CompareNoCase(name) == 0) return &p;
    return NULL;
  }
  COleVariant Value(Table* table) {
    const Token& tok = t[pos++];
    COleVariant v;
    switch (tok.type) {
      case Token::kNumber:
        if (tok.text.Find('.') >= 0) v = COleVariant(_tcstod(tok.text, NULL));
        else v = COleVariant((long)_tcstol(tok.text, NULL, 10));
        return v;
      case Token::kString: return COleVariant(tok.text, VT_BSTR);
      case Token::kDate: {
        COleDateTime dt;
        dt.ParseDateTime(tok.text, 0, MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT));
        return COleVariant(dt);
      }
      case Token::kWord:
        if (tok.text.CompareNoCase(_T("NULL")) == 0) {
          v.vt = VT_NULL;
          return v;
        }
        if (tok.text.CompareNoCase(_T("TRUE")) == 0) return COleVariant((long)-1);
        if (tok.text.CompareNoCase(_T("FALSE")) == 0) return COleVariant((long)0);
        // fall through: a parameter
      case Token::kName: {
        const Param* p = FindParam(tok.text);
        if (p == NULL || (table != NULL && table->Find(tok.text) >= 0))
          throw SqlError{Fail(3061, _T("Too few parameters."))};
        return p->value;
      }
      default: break;
    }
    pos--;
    Syntax();
  }
  Expr Primary(Table* table) {
    Expr e;
    if (IsSym(_T("("))) {
      pos++;
      e = Or(table);
      ExpectSym(_T(")"));
      return e;
    }
    if (IsWord(_T("NOT"))) {
      pos++;
      e.kind = Expr::kNot;
      e.sub.push_back(Primary(table));
      return e;
    }
    e.col = Col(table);
    if (IsWord(_T("IS"))) {
      pos++;
      e.kind = Expr::kIsNull;
      if (IsWord(_T("NOT"))) {
        pos++;
        e.kind = Expr::kNotNull;
      }
      ExpectWord(_T("NULL"));
      return e;
    }
    if (Peek().type != Token::kSymbol) Syntax();
    e.kind = Expr::kCompare;
    e.op = t[pos++].text;
    e.value = Value(table);
    return e;
  }
  Expr And(Table* table) {
    Expr e = Primary(table);
    if (!IsWord(_T("AND"))) return e;
    Expr all;
    all.kind = Expr::kAnd;
    all.sub.push_back(e);
    while (IsWord(_T("AND"))) {
      pos++;
      all.sub.push_back(Primary(table));
    }
    return all;
  }
  Expr Or(Table* table) {
    Expr e = And(table);
    if (!IsWord(_T("OR"))) return e;
    Expr any;
    any.kind = Expr::kOr;
    any.sub.push_back(e);
    while (IsWord(_T("OR"))) {
      pos++;
      any.sub.push_back(And(table));
    }
    return any;
  }
  void End() {
    if (IsSym(_T(";"))) pos++;
    if (Peek().type != Token::kEnd) Syntax();
  }

  Statement Parse() {
    Statement st;
    if (IsWord(_T("PARAMETERS"))) {
      while (!IsSym(_T(";")) && Peek().type != Token::kEnd) pos++;
      ExpectSym(_T(";"));
    }
    if (IsWord(_T("SELECT"))) {
      pos++;
      std::vector<CString> names;
      bool all = false;
      if (IsSym(_T("*"))) {
        all = true;
        pos++;
      } else {
        for (;;) {
          names.push_back(Name());
          if (!IsSym(_T(","))) break;
          pos++;
        }
      }
      ExpectWord(_T("FROM"));
      st.table = TableName();
      if (all)
        for (size_t i = 0; i < st.table->cols.size(); i++) st.cols.push_back((int)i);
      for (auto& n : names) {
        int c = st.table->Find(n);
        if (c < 0) throw SqlError{Fail(3061, _T("Too few parameters."))};
        st.cols.push_back(c);
      }
      if (IsWord(_T("WHERE"))) {
        pos++;
        st.where = Or(st.table);
      }
      if (IsWord(_T("ORDER"))) {
        pos++;
        ExpectWord(_T("BY"));
        for (;;) {
          int c = Col(st.table);
          bool desc = false;
          if (IsWord(_T("DESC"))) {
            desc = true;
            pos++;
          } else if (IsWord(_T("ASC"))) {
            pos++;
          }
          st.order.push_back({c, desc});
          if (!IsSym(_T(","))) break;
          pos++;
        }
      }
      End();
      st.kind = Statement::kSelect;
      return st;
    }
    if (IsWord(_T("INSERT"))) {
      pos++;
      ExpectWord(_T("INTO"));
      st.table = TableName();
      ExpectSym(_T("("));
      for (;;) {
        st.cols.push_back(Col(st.table));
        if (!IsSym(_T(","))) break;
        pos++;
      }
      ExpectSym(_T(")"));
      ExpectWord(_T("VALUES"));
      ExpectSym(_T("("));
      for (size_t k = 0; k < st.cols.size(); k++) {
        if (k) ExpectSym(_T(","));
        st.values.push_back(Value(st.table));
      }
      ExpectSym(_T(")"));
      End();
      st.kind = Statement::kInsert;
      return st;
    }
    if (IsWord(_T("UPDATE"))) {
      pos++;
      st.table = TableName();
      ExpectWord(_T("SET"));
      for (;;) {
        int c = Col(st.table);
        ExpectSym(_T("="));
        st.sets.push_back({c, Value(st.table)});
        if (!IsSym(_T(","))) break;
        pos++;
      }
      if (IsWord(_T("WHERE"))) {
        pos++;
        st.where = Or(st.table);
      }
      End();
      st.kind = Statement::kUpdate;
      return st;
    }
    if (IsWord(_T("DELETE"))) {
      pos++;
      if (IsSym(_T("*"))) pos++;
      ExpectWord(_T("FROM"));
      st.table = TableName();
      if (IsWord(_T("WHERE"))) {
        pos++;
        st.where = Or(st.table);
      }
      End();
      st.kind = Statement::kDelete;
      return st;
    }
    Syntax();
  }
};

// A statement, with its parameters' values: parsed, or the error.
inline HRESULT ParseStatement(Db* db, const CString& sql, const std::vector<Param>* params,
                              Statement& st) {
  Parser p;
  p.t = Tokenize(sql);
  p.db = db;
  p.params = params;
  try {
    st = p.Parse();
  } catch (SqlError& e) {
    return e.hr;
  }
  return S_OK;
}

// INSERT, UPDATE and DELETE.
inline HRESULT RunAction(Db* db, Statement& st) {
  if (db->readOnly) return Fail(3027, _T("Cannot update. Database or object is read-only."));
  Table* table = st.table;
  db->recordsAffected = 0;
  if (st.kind == Statement::kInsert) {
    Row row;
    row.id = table->nextId++;
    row.v.resize(table->cols.size());
    for (size_t c = 0; c < table->cols.size(); c++)
      if (table->cols[c].attrs & dbAutoIncrField) row.v[c] = COleVariant(table->nextAuto++);
      else row.v[c].vt = VT_NULL;
    for (size_t k = 0; k < st.cols.size(); k++) {
      HRESULT hr = Coerce(table->cols[st.cols[k]], st.values[k], row.v[st.cols[k]]);
      if (FAILED(hr)) return hr;
    }
    table->rows.push_back(row);
    db->recordsAffected = 1;
  } else if (st.kind == Statement::kUpdate) {
    for (auto& r : table->rows) {
      if (!st.where.Eval(r.v)) continue;
      for (auto& s : st.sets) {
        HRESULT hr = Coerce(table->cols[s.first], s.second, r.v[s.first]);
        if (FAILED(hr)) return hr;
      }
      db->recordsAffected++;
    }
  } else if (st.kind == Statement::kDelete) {
    size_t before = table->rows.size();
    table->rows.erase(std::remove_if(table->rows.begin(), table->rows.end(),
                                     [&](const Row& r) { return st.where.Eval(r.v); }),
                      table->rows.end());
    db->recordsAffected = (long)(before - table->rows.size());
  } else {
    return Fail(3065, _T("Cannot execute a select query."));
  }
  return S_OK;
}

// ---------------------------------------------------------------------
// Collections of COM objects made on demand

template <class COLL, class ITEM, class OBJ>
struct VectorCollection : public COLL {
  std::vector<OBJ*>& Items() { return m_items; }
  std::vector<OBJ*> m_items;
  ~VectorCollection() {
    for (auto p : m_items) p->Release();
  }
};

// The fields of a table, a recordset, an index or a relation: each field
// is either the column of a table, or a definition not yet appended.
struct Field : public Stub_DAOField {
  Table* table = NULL;  // a table's column, by name
  Column def;           // or a definition of its own
  struct Recordset* rs = NULL;
  int rsCol = -1;
  CString foreignName;
  bool IsIID(REFIID riid) override { return IsEqualIID(riid, IID_IDAOFieldW) != 0; }
  Column& C() {
    if (table != NULL) {
      int c = table->Find(def.name);
      if (c >= 0) return table->cols[c];
    }
    return def;
  }
  STDMETHOD(get_Name)(BSTR* p) override {
    *p = Bstr(C().name);
    return S_OK;
  }
  STDMETHOD(put_Name)(BSTR b) override {
    C().name = b;
    def.name = b;
    return S_OK;
  }
  STDMETHOD(get_Type)(short* p) override {
    *p = C().type;
    return S_OK;
  }
  STDMETHOD(put_Type)(short t) override {
    C().type = t;
    return S_OK;
  }
  STDMETHOD(get_Size)(__LONG32* p) override {
    *p = C().size;
    return S_OK;
  }
  STDMETHOD(put_Size)(__LONG32 s) override {
    C().size = s;
    return S_OK;
  }
  STDMETHOD(get_Attributes)(__LONG32* p) override {
    *p = C().attrs;
    return S_OK;
  }
  STDMETHOD(put_Attributes)(__LONG32 a) override {
    C().attrs = a;
    return S_OK;
  }
  STDMETHOD(get_OrdinalPosition)(short* p) override {
    *p = 0;
    if (table != NULL) *p = (short)table->Find(def.name);
    return S_OK;
  }
  STDMETHOD(put_OrdinalPosition)(short) override { return S_OK; }
  STDMETHOD(get_Required)(VARIANT_BOOL* p) override {
    *p = C().required ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
  }
  STDMETHOD(put_Required)(VARIANT_BOOL b) override {
    C().required = b != VARIANT_FALSE;
    return S_OK;
  }
  STDMETHOD(get_AllowZeroLength)(VARIANT_BOOL* p) override {
    *p = C().allowZeroLength ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
  }
  STDMETHOD(put_AllowZeroLength)(VARIANT_BOOL b) override {
    C().allowZeroLength = b != VARIANT_FALSE;
    return S_OK;
  }
  STDMETHOD(get_CollatingOrder)(__LONG32* p) override {
    *p = dbSortGeneral;
    return S_OK;
  }
  STDMETHOD(get_ForeignName)(BSTR* p) override {
    *p = Bstr(foreignName);
    return S_OK;
  }
  STDMETHOD(put_ForeignName)(BSTR b) override {
    foreignName = b;
    return S_OK;
  }
  STDMETHOD(get_SourceField)(BSTR* p) override {
    *p = Bstr(C().name);
    return S_OK;
  }
  STDMETHOD(get_SourceTable)(BSTR* p) override {
    *p = Bstr(table != NULL ? table->name : CString());
    return S_OK;
  }
  STDMETHOD(get_ValidationRule)(BSTR* p) override {
    *p = Bstr(CString());
    return S_OK;
  }
  STDMETHOD(put_ValidationRule)(BSTR) override { return S_OK; }
  STDMETHOD(get_ValidationText)(BSTR* p) override {
    *p = Bstr(CString());
    return S_OK;
  }
  STDMETHOD(put_ValidationText)(BSTR) override { return S_OK; }
  STDMETHOD(get_DefaultValue)(VARIANT* p) override {
    ::VariantInit(p);
    return ::VariantCopy(p, &C().defaultValue);
  }
  STDMETHOD(put_DefaultValue)(VARIANT v) override {
    C().defaultValue = COleVariant(v);
    return S_OK;
  }
  STDMETHOD(get_Value)(VARIANT* p) override;
  STDMETHOD(put_Value)(VARIANT v) override;
};

struct Fields : public StubDAOFields {
  std::vector<Field*> items;
  Table* table = NULL;  // appending adds a column to it
  bool IsIID(REFIID riid) override { return IsEqualIID(riid, IID_IDAOFieldsW) != 0; }
  ~Fields() {
    for (auto p : items) p->Release();
  }
  STDMETHOD(get_Count)(short* p) override {
    *p = (short)items.size();
    return S_OK;
  }
  STDMETHOD(Refresh)() override { return S_OK; }
  int Find(const VARIANT& item) {
    if (item.vt == VT_BSTR) {
      for (size_t i = 0; i < items.size(); i++)
        if (items[i]->C().name.CompareNoCase(CString(item.bstrVal)) == 0) return (int)i;
      return -1;
    }
    long n = VarLong(item, -1);
    return n >= 0 && n < (long)items.size() ? (int)n : -1;
  }
  STDMETHOD(get_Item)(VARIANT item, DAOField** pp) override {
    int i = Find(item);
    if (i < 0) return Fail(3265, CString(_T("Item not found in this collection: ")) + VarString(item));
    *pp = items[i];
    items[i]->AddRef();
    return S_OK;
  }
  STDMETHOD(Append)(IDispatch* pObj) override {
    Field* f = static_cast<Field*>(static_cast<DAOField*>(pObj));
    for (auto p : items)
      if (p->C().name.CompareNoCase(f->def.name) == 0)
        return Fail(3191, _T("Cannot define field more than once."));
    if (table != NULL) {
      // A column added to a table: null in its rows.
      table->cols.push_back(f->def);
      for (auto& r : table->rows) {
        r.v.push_back(COleVariant());
        r.v.back().vt = VT_NULL;
      }
      f->table = table;
    }
    f->AddRef();
    items.push_back(f);
    return S_OK;
  }
  STDMETHOD(Delete)(BSTR name) override {
    for (size_t i = 0; i < items.size(); i++) {
      if (items[i]->C().name.CompareNoCase(CString(name)) != 0) continue;
      if (table != NULL) {
        int c = table->Find(CString(name));
        if (c >= 0) {
          table->cols.erase(table->cols.begin() + c);
          for (auto& r : table->rows) r.v.erase(r.v.begin() + c);
        }
      }
      items[i]->Release();
      items.erase(items.begin() + i);
      return S_OK;
    }
    return Fail(3265, CString(_T("Item not found in this collection: ")) + CString(name));
  }
};

inline Fields* TableFields(Table* table) {
  Fields* fs = new Fields;
  fs->table = table;
  for (auto& c : table->cols) {
    Field* f = new Field;
    f->table = table;
    f->def = c;
    fs->items.push_back(f);
  }
  return fs;
}

// An index's fields: their items are VARIANTs holding the fields.
struct IndexFields : public StubDAOIndexFields {
  std::vector<Field*> items;
  bool IsIID(REFIID riid) override { return IsEqualIID(riid, IID_IDAOIndexFieldsW) != 0; }
  ~IndexFields() {
    for (auto p : items) p->Release();
  }
  STDMETHOD(get_Count)(short* p) override {
    *p = (short)items.size();
    return S_OK;
  }
  STDMETHOD(get_Item)(VARIANT item, VARIANT* pv) override {
    long n = VarLong(item, -1);
    if (n < 0 || n >= (long)items.size()) return Fail(3265, _T("Item not found in this collection."));
    ::VariantInit(pv);
    pv->vt = VT_DISPATCH;
    pv->pdispVal = items[n];
    items[n]->AddRef();
    return S_OK;
  }
  STDMETHOD(Append)(IDispatch* pObj) override {
    Field* f = static_cast<Field*>(static_cast<DAOField*>(pObj));
    f->AddRef();
    items.push_back(f);
    return S_OK;
  }
};

struct IndexObj : public Stub_DAOIndex {
  Index def;
  Table* table = NULL;  // appended to it
  IndexFields* fields = new IndexFields;
  bool IsIID(REFIID riid) override { return IsEqualIID(riid, IID_IDAOIndexW) != 0; }
  ~IndexObj() { fields->Release(); }
  Index& I() {
    if (table != NULL)
      for (auto& x : table->indexes)
        if (x.name.CompareNoCase(def.name) == 0) return x;
    return def;
  }
  STDMETHOD(get_Name)(BSTR* p) override {
    *p = Bstr(I().name);
    return S_OK;
  }
  STDMETHOD(put_Name)(BSTR b) override {
    I().name = b;
    return S_OK;
  }
#define IDX_BOOL(Prop, member)                                \
  STDMETHOD(get_##Prop)(VARIANT_BOOL * p) override {          \
    *p = I().member ? VARIANT_TRUE : VARIANT_FALSE;           \
    return S_OK;                                              \
  }                                                           \
  STDMETHOD(put_##Prop)(VARIANT_BOOL b) override {            \
    I().member = b != VARIANT_FALSE;                          \
    return S_OK;                                              \
  }
  IDX_BOOL(Primary, primary)
  IDX_BOOL(Unique, unique)
  IDX_BOOL(Clustered, clustered)
  IDX_BOOL(IgnoreNulls, ignoreNulls)
  IDX_BOOL(Required, required)
#undef IDX_BOOL
  STDMETHOD(get_Foreign)(VARIANT_BOOL* p) override {
    *p = VARIANT_FALSE;
    return S_OK;
  }
  STDMETHOD(get_DistinctCount)(__LONG32* p) override {
    *p = table != NULL ? (long)table->rows.size() : 0;
    return S_OK;
  }
  STDMETHOD(get_Fields)(VARIANT* pv) override {
    ::VariantInit(pv);
    pv->vt = VT_DISPATCH;
    pv->pdispVal = fields;
    fields->AddRef();
    return S_OK;
  }
  STDMETHOD(CreateField)(VARIANT Name, VARIANT, VARIANT, DAOField** pp) override {
    Field* f = new Field;
    f->def.name = VarString(Name);
    f->def.attrs = 0;
    *pp = f;
    return S_OK;
  }
};

struct Indexes : public StubDAOIndexes {
  std::vector<IndexObj*> items;
  Table* table = NULL;
  bool IsIID(REFIID riid) override { return IsEqualIID(riid, IID_IDAOIndexesW) != 0; }
  ~Indexes() {
    for (auto p : items) p->Release();
  }
  STDMETHOD(get_Count)(short* p) override {
    *p = (short)items.size();
    return S_OK;
  }
  STDMETHOD(get_Item)(VARIANT item, DAOIndex** pp) override {
    for (size_t i = 0; i < items.size(); i++) {
      bool match = item.vt == VT_BSTR ? items[i]->I().name.CompareNoCase(CString(item.bstrVal)) == 0
                                      : VarLong(item, -1) == (long)i;
      if (!match) continue;
      *pp = items[i];
      items[i]->AddRef();
      return S_OK;
    }
    return Fail(3265, _T("Item not found in this collection."));
  }
  STDMETHOD(Append)(IDispatch* pObj) override {
    IndexObj* x = static_cast<IndexObj*>(static_cast<DAOIndex*>(pObj));
    // The index's fields come from its Fields collection.
    x->def.fields.clear();
    for (auto f : x->fields->items)
      x->def.fields.push_back({f->def.name, (f->def.attrs & dbDescending) != 0});
    if (x->def.fields.empty()) return Fail(3264, _T("No field defined--cannot append Index."));
    if (table != NULL) {
      for (auto& fld : x->def.fields)
        if (table->Find(fld.first) < 0) return Fail(3265, _T("Item not found in this collection."));
      table->indexes.push_back(x->def);
      x->table = table;
    }
    x->AddRef();
    items.push_back(x);
    return S_OK;
  }
  STDMETHOD(Delete)(BSTR name) override {
    for (size_t i = 0; i < items.size(); i++) {
      if (items[i]->I().name.CompareNoCase(CString(name)) != 0) continue;
      if (table != NULL)
        for (size_t k = 0; k < table->indexes.size(); k++)
          if (table->indexes[k].name.CompareNoCase(CString(name)) == 0)
            table->indexes.erase(table->indexes.begin() + k);
      items[i]->Release();
      items.erase(items.begin() + i);
      return S_OK;
    }
    return Fail(3265, _T("Item not found in this collection."));
  }
};

inline Indexes* TableIndexes(Table* table) {
  Indexes* xs = new Indexes;
  xs->table = table;
  for (auto& idx : table->indexes) {
    IndexObj* x = new IndexObj;
    x->def = idx;
    x->table = table;
    for (auto& f : idx.fields) {
      Field* fld = new Field;
      fld->def.name = f.first;
      fld->def.attrs = f.second ? dbDescending : 0;
      x->fields->items.push_back(fld);
    }
    xs->items.push_back(x);
  }
  return xs;
}

// ---------------------------------------------------------------------
// Recordsets

struct Recordset : public StubDAORecordset {
  Db* db = NULL;
  Table* table = NULL;
  short type = dbOpenDynaset;
  bool updatable = true;
  bool tableType = false;
  CString name;
  CString sql;
  std::vector<Param> params;
  std::vector<int> cols;                      // the table's columns, in order
  std::vector<long> ids;                      // dynasets and tables: the rows
  std::vector<std::vector<COleVariant>> snap; // snapshots: the values
  long cur = 0;                               // -1 BOF, size() EOF
  short editMode = dbEditNone;
  std::vector<COleVariant> buf;               // the record being added or edited
  long lastModified = 0;
  bool noMatch = true;
  CString index;
  bool closed = false;
  Fields* fields = NULL;
  bool IsIID(REFIID riid) override { return IsEqualIID(riid, IID_IDAORecordsetW) != 0; }
  ~Recordset() {
    if (fields != NULL) fields->Release();
  }

  long Count() const { return (long)(type == dbOpenSnapshot ? snap.size() : ids.size()); }
  bool OnRecord() const { return cur >= 0 && cur < Count(); }
  // The current record's values, or NULL if it was deleted.
  std::vector<COleVariant>* Values(long i) {
    if (type == dbOpenSnapshot) return &snap[i];
    Row* r = table->RowById(ids[i]);
    return r != NULL ? &r->v : NULL;
  }

  HRESULT Run(const Statement& st) {
    table = st.table;
    cols = st.cols;
    std::vector<Row*> rows;
    for (auto& r : table->rows)
      if (st.where.Eval(r.v)) rows.push_back(&r);
    if (!st.order.empty())
      std::stable_sort(rows.begin(), rows.end(), [&](Row* a, Row* b) {
        for (auto& o : st.order) {
          int c = Compare(a->v[o.first], b->v[o.first]);
          if (c != 0) return o.second ? c > 0 : c < 0;
        }
        return false;
      });
    ids.clear();
    snap.clear();
    for (Row* r : rows) {
      ids.push_back(r->id);
      if (type == dbOpenSnapshot) {
        std::vector<COleVariant> v;
        for (int c : cols) v.push_back(r->v[c]);
        snap.push_back(v);
      }
    }
    cur = Count() > 0 ? 0 : 0;
    return S_OK;
  }
  // A table-type recordset: every row, in the current index's order.
  void LoadTable() {
    cols.clear();
    for (size_t i = 0; i < table->cols.size(); i++) cols.push_back((int)i);
    std::vector<Row*> rows;
    for (auto& r : table->rows) rows.push_back(&r);
    const Index* idx = NULL;
    for (auto& x : table->indexes)
      if (!index.IsEmpty() && x.name.CompareNoCase(index) == 0) idx = &x;
    if (idx != NULL)
      std::stable_sort(rows.begin(), rows.end(), [&](Row* a, Row* b) {
        for (auto& f : idx->fields) {
          int c = table->Find(f.first);
          int cmp = Compare(a->v[c], b->v[c]);
          if (cmp != 0) return f.second ? cmp > 0 : cmp < 0;
        }
        return false;
      });
    ids.clear();
    for (Row* r : rows) ids.push_back(r->id);
  }
  HRESULT Requery() {
    if (tableType) {
      LoadTable();
      cur = 0;
      return S_OK;
    }
    Statement st;
    HRESULT hr = ParseStatement(db, sql, &params, st);
    if (FAILED(hr)) return hr;
    return Run(st);
  }
  HRESULT NoCurrent() { return Fail(3021, _T("No current record.")); }
  HRESULT SkipDeleted(int step) {
    while (OnRecord() && Values(cur) == NULL) cur += step;
    return S_OK;
  }

  STDMETHOD(get_BOF)(VARIANT_BOOL* p) override {
    *p = (cur < 0 || Count() == 0) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
  }
  STDMETHOD(get_EOF)(VARIANT_BOOL* p) override {
    *p = (cur >= Count()) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
  }
  STDMETHOD(get_Bookmarkable)(VARIANT_BOOL* p) override {
    *p = VARIANT_TRUE;
    return S_OK;
  }
  STDMETHOD(get_Restartable)(VARIANT_BOOL* p) override {
    *p = tableType ? VARIANT_FALSE : VARIANT_TRUE;
    return S_OK;
  }
  STDMETHOD(get_Transactions)(VARIANT_BOOL* p) override {
    *p = VARIANT_TRUE;
    return S_OK;
  }
  STDMETHOD(get_Updatable)(VARIANT_BOOL* p) override {
    *p = updatable ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
  }
  STDMETHOD(get_Type)(short* p) override {
    *p = type;
    return S_OK;
  }
  STDMETHOD(get_Name)(BSTR* p) override {
    *p = Bstr(name);
    return S_OK;
  }
  STDMETHOD(get_RecordCount)(__LONG32* p) override {
    long n = 0;
    for (long i = 0; i < Count(); i++)
      if (Values(i) != NULL) n++;
    *p = n;
    return S_OK;
  }
  STDMETHOD(get_EditMode)(short* p) override {
    *p = editMode;
    return S_OK;
  }
  STDMETHOD(get_NoMatch)(VARIANT_BOOL* p) override {
    *p = noMatch ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
  }
  STDMETHOD(get_LockEdits)(VARIANT_BOOL* p) override {
    *p = VARIANT_TRUE;
    return S_OK;
  }
  STDMETHOD(put_LockEdits)(VARIANT_BOOL) override { return S_OK; }
  STDMETHOD(get_DateCreated)(VARIANT* p) override {
    ::VariantInit(p);
    p->vt = VT_DATE;
    p->date = table->created.m_dt;
    return S_OK;
  }
  STDMETHOD(get_LastUpdated)(VARIANT* p) override { return get_DateCreated(p); }
  STDMETHOD(get_ValidationRule)(BSTR* p) override {
    *p = Bstr(table->validationRule);
    return S_OK;
  }
  STDMETHOD(get_ValidationText)(BSTR* p) override {
    *p = Bstr(table->validationText);
    return S_OK;
  }
  STDMETHOD(get_Fields)(DAOFields** pp) override {
    if (fields == NULL) {
      fields = new Fields;
      for (size_t i = 0; i < cols.size(); i++) {
        Field* f = new Field;
        f->table = table;
        f->def = table->cols[cols[i]];
        f->rs = this;
        f->rsCol = (int)i;
        fields->items.push_back(f);
      }
    }
    *pp = fields;
    fields->AddRef();
    return S_OK;
  }
  STDMETHOD(get_Indexes)(DAOIndexes** pp) override {
    *pp = TableIndexes(table);
    return S_OK;
  }
  STDMETHOD(get_Index)(BSTR* p) override {
    *p = Bstr(index);
    return S_OK;
  }
  STDMETHOD(put_Index)(BSTR b) override {
    if (!tableType) return Fail(3251, _T("Operation is not supported for this type of object."));
    index = b;
    bool found = false;
    for (auto& x : table->indexes)
      if (x.name.CompareNoCase(index) == 0) found = true;
    if (!found) return Fail(3800, _T("'Index' is not an index in this table."));
    LoadTable();
    cur = 0;
    return S_OK;
  }

  // Moving
  STDMETHOD(MoveFirst)() override {
    editMode = dbEditNone;
    cur = 0;
    return SkipDeleted(1);
  }
  STDMETHOD(MoveLast)(__LONG32) override {
    editMode = dbEditNone;
    cur = Count() - 1;
    return SkipDeleted(-1);
  }
  STDMETHOD(_30_MoveLast)() override { return MoveLast(0); }
  STDMETHOD(MoveNext)() override {
    if (cur >= Count()) return NoCurrent();
    editMode = dbEditNone;
    cur++;
    return SkipDeleted(1);
  }
  STDMETHOD(MovePrevious)() override {
    if (cur < 0) return NoCurrent();
    editMode = dbEditNone;
    cur--;
    return SkipDeleted(-1);
  }
  STDMETHOD(Move)(__LONG32 rows, VARIANT) override {
    editMode = dbEditNone;
    cur += rows;
    if (cur < -1) cur = -1;
    if (cur > Count()) cur = Count();
    return S_OK;
  }
  STDMETHOD(get_AbsolutePosition)(__LONG32* p) override {
    *p = OnRecord() ? cur : -1;
    return S_OK;
  }
  STDMETHOD(put_AbsolutePosition)(__LONG32 n) override {
    if (n < 0 || n >= Count()) return Fail(3001, _T("Invalid argument."));
    cur = n;
    return S_OK;
  }
  STDMETHOD(get_PercentPosition)(float* p) override {
    *p = Count() > 1 && OnRecord() ? 100.0f * cur / (Count() - 1) : 0.0f;
    return S_OK;
  }
  STDMETHOD(put_PercentPosition)(float f) override {
    if (Count() == 0) return NoCurrent();
    cur = (long)(f * (Count() - 1) / 100.0f + 0.5f);
    return S_OK;
  }
  // A bookmark: the row's number in the table.
  STDMETHOD(get_Bookmark)(SAFEARRAY** ppsa) override {
    if (!OnRecord()) return NoCurrent();
    long id = type == dbOpenSnapshot ? cur + 1 : ids[cur];
    *ppsa = ::SafeArrayCreateVector(VT_UI1, 0, sizeof(long));
    void* p = NULL;
    ::SafeArrayAccessData(*ppsa, &p);
    memcpy(p, &id, sizeof(id));
    ::SafeArrayUnaccessData(*ppsa);
    return S_OK;
  }
  STDMETHOD(put_Bookmark)(SAFEARRAY** ppsa) override {
    if (ppsa == NULL || *ppsa == NULL) return Fail(3159, _T("Not a valid bookmark."));
    long id = 0;
    void* p = NULL;
    ::SafeArrayAccessData(*ppsa, &p);
    memcpy(&id, p, sizeof(id));
    ::SafeArrayUnaccessData(*ppsa);
    for (long i = 0; i < Count(); i++) {
      if ((type == dbOpenSnapshot ? i + 1 : ids[i]) != id) continue;
      if (Values(i) == NULL) return Fail(3167, _T("Record is deleted."));
      cur = i;
      editMode = dbEditNone;
      return S_OK;
    }
    return Fail(3159, _T("Not a valid bookmark."));
  }
  STDMETHOD(get_LastModified)(SAFEARRAY** ppsa) override {
    if (lastModified == 0) return Fail(3021, _T("No current record."));
    *ppsa = ::SafeArrayCreateVector(VT_UI1, 0, sizeof(long));
    void* p = NULL;
    ::SafeArrayAccessData(*ppsa, &p);
    memcpy(p, &lastModified, sizeof(lastModified));
    ::SafeArrayUnaccessData(*ppsa);
    return S_OK;
  }

  // Finding: the criteria are a WHERE clause.
  HRESULT FindFrom(BSTR criteria, long start, int step) {
    Statement st;
    CString strSQL = CString(_T("SELECT * FROM [")) + table->name + _T("] WHERE ") + CString(criteria);
    HRESULT hr = ParseStatement(db, strSQL, &params, st);
    if (FAILED(hr)) return hr;
    editMode = dbEditNone;
    for (long i = start; i >= 0 && i < Count(); i += step) {
      std::vector<COleVariant>* v = Values(i);
      if (v == NULL) continue;
      std::vector<COleVariant> full = *v;
      if (type == dbOpenSnapshot) {
        full.assign(table->cols.size(), COleVariant());
        for (size_t k = 0; k < cols.size(); k++) full[cols[k]] = (*v)[k];
      }
      if (st.where.Eval(full)) {
        cur = i;
        noMatch = false;
        return S_OK;
      }
    }
    noMatch = true;
    return S_OK;
  }
  STDMETHOD(FindFirst)(BSTR c) override { return FindFrom(c, 0, 1); }
  STDMETHOD(FindLast)(BSTR c) override { return FindFrom(c, Count() - 1, -1); }
  STDMETHOD(FindNext)(BSTR c) override { return FindFrom(c, cur + 1, 1); }
  STDMETHOD(FindPrevious)(BSTR c) override { return FindFrom(c, cur - 1, -1); }

  // Seek: on a table, through its current index.
  STDMETHOD(Seek)(BSTR cmp, VARIANT k1, VARIANT k2, VARIANT k3, VARIANT, VARIANT, VARIANT, VARIANT,
                  VARIANT, VARIANT, VARIANT, VARIANT, VARIANT, VARIANT) override {
    if (!tableType || index.IsEmpty()) return Fail(3019, _T("Operation invalid without a current index."));
    const Index* idx = NULL;
    for (auto& x : table->indexes)
      if (x.name.CompareNoCase(index) == 0) idx = &x;
    VARIANT keys[3] = {k1, k2, k3};
    CString op(cmp);
    editMode = dbEditNone;
    // Jet seeks the first match in index order, or for < and <= the last.
    bool backwards = op == _T("<") || op == _T("<=");
    for (long n = 0; n < Count(); n++) {
      long i = backwards ? Count() - 1 - n : n;
      std::vector<COleVariant>* v = Values(i);
      if (v == NULL) continue;
      int c = 0;
      for (size_t f = 0; f < idx->fields.size() && f < 3 && c == 0; f++) {
        if (IsMissing(keys[f])) break;
        c = Compare((*v)[table->Find(idx->fields[f].first)], keys[f]);
        if (idx->fields[f].second) c = -c;
      }
      bool ok = op == _T("=") ? c == 0 : op == _T(">") ? c > 0 : op == _T(">=") ? c >= 0
              : op == _T("<") ? c < 0 : c <= 0;
      if (ok) {
        cur = i;
        noMatch = false;
        return S_OK;
      }
    }
    noMatch = true;
    return S_OK;
  }

  // Editing
  STDMETHOD(AddNew)() override {
    if (!updatable) return Fail(3027, _T("Cannot update. Database or object is read-only."));
    buf.assign(cols.size(), COleVariant());
    for (size_t i = 0; i < cols.size(); i++) {
      const Column& col = table->cols[cols[i]];
      buf[i] = col.defaultValue;
      if (buf[i].vt == VT_EMPTY) buf[i].vt = VT_NULL;
    }
    editMode = dbEditAdd;
    return S_OK;
  }
  STDMETHOD(Edit)() override {
    if (!updatable) return Fail(3027, _T("Cannot update. Database or object is read-only."));
    if (!OnRecord() || Values(cur) == NULL) return NoCurrent();
    std::vector<COleVariant>& v = *Values(cur);
    buf.clear();
    for (int c : cols) buf.push_back(v[c]);
    editMode = dbEditInProgress;
    return S_OK;
  }
  STDMETHOD(Update)(__LONG32, VARIANT_BOOL) override {
    if (editMode == dbEditNone) return Fail(3020, _T("Update or CancelUpdate without AddNew or Edit."));
    for (size_t i = 0; i < cols.size(); i++) {
      const Column& col = table->cols[cols[i]];
      if (col.required && buf[i].vt == VT_NULL && !(col.attrs & dbAutoIncrField))
        return Fail(3314, CString(_T("The field '")) + table->name + _T(".") + col.name + _T("' cannot contain a Null value."));
    }
    // A unique index can't have the same values twice.
    if (editMode == dbEditAdd) {
      Row row;
      row.id = table->nextId++;
      row.v.assign(table->cols.size(), COleVariant());
      for (auto& v : row.v) v.vt = VT_NULL;
      for (size_t i = 0; i < cols.size(); i++) row.v[cols[i]] = buf[i];
      for (size_t c = 0; c < table->cols.size(); c++)
        if (table->cols[c].attrs & dbAutoIncrField) row.v[c] = COleVariant(table->nextAuto++);
      for (auto& x : table->indexes) {
        if (!x.unique && !x.primary) continue;
        for (auto& r : table->rows) {
          bool same = true;
          for (auto& f : x.fields) {
            int c = table->Find(f.first);
            if (Compare(r.v[c], row.v[c]) != 0) same = false;
          }
          if (same)
            return Fail(3022, _T("The changes you requested to the table were not successful because they would create duplicate values."));
        }
      }
      table->rows.push_back(row);
      lastModified = row.id;
      // The new record is at the end of a dynaset; the current one stays.
      if (type != dbOpenSnapshot) ids.push_back(row.id);
    } else {
      std::vector<COleVariant>* v = Values(cur);
      if (v == NULL) return Fail(3167, _T("Record is deleted."));
      for (size_t i = 0; i < cols.size(); i++) (*v)[cols[i]] = buf[i];
      lastModified = ids[cur];
    }
    editMode = dbEditNone;
    return S_OK;
  }
  STDMETHOD(_30_Update)() override { return Update(dbUpdateRegular, VARIANT_FALSE); }
  STDMETHOD(CancelUpdate)(__LONG32) override {
    editMode = dbEditNone;
    return S_OK;
  }
  STDMETHOD(_30_CancelUpdate)() override { return CancelUpdate(0); }
  STDMETHOD(Delete)() override {
    if (!updatable) return Fail(3027, _T("Cannot update. Database or object is read-only."));
    if (!OnRecord() || Values(cur) == NULL) return NoCurrent();
    long id = ids[cur];
    table->rows.erase(std::remove_if(table->rows.begin(), table->rows.end(),
                                     [&](const Row& r) { return r.id == id; }),
                      table->rows.end());
    editMode = dbEditNone;
    return S_OK;
  }
  STDMETHOD(Requery)(VARIANT) override {
    editMode = dbEditNone;
    HRESULT hr = Requery();
    cur = 0;
    SkipDeleted(1);
    return hr;
  }
  STDMETHOD(Close)() override {
    closed = true;
    return S_OK;
  }
  STDMETHOD(get_Collect)(VARIANT item, VARIANT* pv) override {
    DAOFields* fs = NULL;
    get_Fields(&fs);
    DAOField* f = NULL;
    HRESULT hr = fs->get_Item(item, &f);
    fs->Release();
    if (FAILED(hr)) return hr;
    hr = f->get_Value(pv);
    f->Release();
    return hr;
  }
};

inline HRESULT Field::get_Value(VARIANT* p) {
  ::VariantInit(p);
  if (rs == NULL) return Fail(3219, _T("Invalid operation."));
  if (rs->editMode != dbEditNone) return ::VariantCopy(p, &rs->buf[rsCol]);
  if (!rs->OnRecord()) return rs->NoCurrent();
  std::vector<COleVariant>* v = rs->Values(rs->cur);
  if (v == NULL) return Fail(3167, _T("Record is deleted."));
  return ::VariantCopy(p, rs->type == dbOpenSnapshot ? &(*v)[rsCol] : &(*v)[rs->cols[rsCol]]);
}

inline HRESULT Field::put_Value(VARIANT val) {
  if (rs == NULL) return Fail(3219, _T("Invalid operation."));
  if (rs->editMode == dbEditNone) return Fail(3020, _T("Update or CancelUpdate without AddNew or Edit."));
  G().fieldWrites++;
  const Column& col = rs->table->cols[rs->cols[rsCol]];
  if (col.attrs & dbAutoIncrField) return Fail(3164, _T("Field cannot be updated."));
  return Coerce(col, val, rs->buf[rsCol]);
}

// A recordset on a statement, a table or a query of the database.
inline HRESULT OpenRecordset(Db* db, const CString& source, short type, long options,
                             const std::vector<Param>* params, DAORecordset** pp) {
  Recordset* rs = new Recordset;
  rs->db = db;
  rs->type = type;
  rs->updatable = !db->readOnly && type != dbOpenSnapshot && !(options & dbReadOnly);
  CString sql = source;
  CString upper = source;
  upper.TrimLeft();
  upper.MakeUpper();
  bool statement = upper.Find(_T("SELECT ")) == 0 || upper.Find(_T("PARAMETERS ")) == 0;
  if (!statement) {
    CString name = source;
    name.Trim(_T(" []"));
    Table* table = db->Find(name);
    QueryData* q = table == NULL ? db->FindQuery(name) : NULL;
    if (table == NULL && q == NULL) {
      rs->Release();
      return Fail(3078, CString(_T("The Microsoft Jet database engine cannot find the input table or query '")) + name + _T("'."));
    }
    rs->name = name;
    if (table != NULL && type == dbOpenTable) {
      rs->table = table;
      rs->tableType = true;
      rs->LoadTable();
      rs->cur = 0;
      *pp = rs;
      return S_OK;
    }
    if (type == dbOpenTable) {
      rs->Release();
      return Fail(3219, _T("Invalid operation."));
    }
    sql = table != NULL ? CString(_T("SELECT * FROM [")) + name + _T("]") : q->sql;
  } else if (type == dbOpenTable) {
    rs->Release();
    return Fail(3219, _T("Invalid operation."));
  } else {
    rs->name = source;
  }
  rs->sql = sql;
  if (params != NULL) rs->params = *params;
  Statement st;
  HRESULT hr = ParseStatement(db, sql, &rs->params, st);
  if (SUCCEEDED(hr) && st.kind != Statement::kSelect) hr = Fail(3219, _T("Invalid operation."));
  if (SUCCEEDED(hr)) hr = rs->Run(st);
  if (FAILED(hr)) {
    rs->Release();
    return hr;
  }
  *pp = rs;
  return S_OK;
}

// ---------------------------------------------------------------------
// Querydefs and their parameters

struct Parameter : public StubDAOParameter {
  CString name;
  short type = dbText;
  COleVariant value;
  bool IsIID(REFIID riid) override { return IsEqualIID(riid, IID_IDAOParameterW) != 0; }
  STDMETHOD(get_Name)(BSTR* p) override {
    *p = Bstr(name);
    return S_OK;
  }
  STDMETHOD(get_Type)(short* p) override {
    *p = type;
    return S_OK;
  }
  STDMETHOD(get_Value)(VARIANT* p) override {
    ::VariantInit(p);
    return ::VariantCopy(p, &value);
  }
  STDMETHOD(put_Value)(VARIANT v) override {
    Column col;
    col.type = type;
    col.size = 255;
    return Coerce(col, v, value);
  }
  STDMETHOD(get_Direction)(short* p) override {
    *p = dbParamInput;
    return S_OK;
  }
};

struct Parameters : public StubDAOParameters {
  std::vector<Parameter*> items;
  bool IsIID(REFIID riid) override { return IsEqualIID(riid, IID_IDAOParametersW) != 0; }
  ~Parameters() {
    for (auto p : items) p->Release();
  }
  STDMETHOD(get_Count)(short* p) override {
    *p = (short)items.size();
    return S_OK;
  }
  STDMETHOD(Refresh)() override { return S_OK; }
  STDMETHOD(get_Item)(VARIANT item, DAOParameter** pp) override {
    for (size_t i = 0; i < items.size(); i++) {
      bool match = item.vt == VT_BSTR ? items[i]->name.CompareNoCase(CString(item.bstrVal)) == 0
                                      : VarLong(item, -1) == (long)i;
      if (!match) continue;
      *pp = items[i];
      items[i]->AddRef();
      return S_OK;
    }
    return Fail(3265, CString(_T("Item not found in this collection: ")) + VarString(item));
  }
};

struct QueryDef : public Stub_DAOQueryDef {
  Db* db = NULL;
  QueryData data;
  bool appended = false;
  Parameters* params = NULL;
  long recordsAffected = 0;
  bool IsIID(REFIID riid) override { return IsEqualIID(riid, IID_IDAOQueryDefW) != 0; }
  ~QueryDef() {
    if (params != NULL) params->Release();
  }
  QueryData& D() {
    if (appended) {
      QueryData* q = db->FindQuery(data.name);
      if (q != NULL) return *q;
    }
    return data;
  }
  // The parameters follow the SQL; values already given are kept.
  void SyncParams() {
    std::vector<Param> ps = ParseParams(D().sql);
    if (params == NULL) params = new Parameters;
    std::vector<Parameter*> items;
    for (auto& p : ps) {
      Parameter* prm = NULL;
      for (auto old : params->items)
        if (old->name.CompareNoCase(p.name) == 0) {
          prm = old;
          prm->AddRef();
        }
      if (prm == NULL) {
        prm = new Parameter;
        prm->name = p.name;
        prm->value.vt = VT_NULL;
      }
      prm->type = p.type;
      items.push_back(prm);
    }
    for (auto old : params->items) old->Release();
    params->items = items;
  }
  std::vector<Param> ParamValues() {
    SyncParams();
    std::vector<Param> ps;
    for (auto p : params->items) ps.push_back({p->name, p->type, p->value});
    return ps;
  }
  STDMETHOD(get_Name)(BSTR* p) override {
    *p = Bstr(D().name);
    return S_OK;
  }
  STDMETHOD(put_Name)(BSTR b) override {
    D().name = b;
    return S_OK;
  }
  STDMETHOD(get_SQL)(BSTR* p) override {
    *p = Bstr(D().sql);
    return S_OK;
  }
  STDMETHOD(put_SQL)(BSTR b) override {
    D().sql = b;
    SyncParams();
    return S_OK;
  }
  STDMETHOD(get_Type)(short* p) override {
    CString s = D().sql;
    std::vector<Token> t = Tokenize(s);
    size_t i = 0;
    if (t[0].type == Token::kWord && t[0].text.CompareNoCase(_T("PARAMETERS")) == 0) {
      while (t[i].type != Token::kEnd && t[i].text != _T(";")) i++;
      if (t[i].type != Token::kEnd) i++;
    }
    CString w = t[i].text;
    *p = w.CompareNoCase(_T("SELECT")) == 0 ? dbQSelect
       : w.CompareNoCase(_T("UPDATE")) == 0 ? dbQUpdate
       : w.CompareNoCase(_T("DELETE")) == 0 ? dbQDelete
       : w.CompareNoCase(_T("INSERT")) == 0 ? dbQAppend : dbQAction;
    return S_OK;
  }
  STDMETHOD(get_Updatable)(VARIANT_BOOL* p) override {
    *p = VARIANT_TRUE;
    return S_OK;
  }
  STDMETHOD(get_ReturnsRecords)(VARIANT_BOOL* p) override {
    short t = 0;
    get_Type(&t);
    *p = t == dbQSelect ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
  }
  STDMETHOD(put_ReturnsRecords)(VARIANT_BOOL) override { return S_OK; }
  STDMETHOD(get_Connect)(BSTR* p) override {
    *p = Bstr(CString());
    return S_OK;
  }
  STDMETHOD(put_Connect)(BSTR) override { return S_OK; }
  STDMETHOD(get_ODBCTimeout)(short* p) override {
    *p = 60;
    return S_OK;
  }
  STDMETHOD(put_ODBCTimeout)(short) override { return S_OK; }
  STDMETHOD(get_DateCreated)(VARIANT* p) override {
    ::VariantInit(p);
    p->vt = VT_DATE;
    p->date = COleDateTime(2002, 3, 4, 5, 6, 7).m_dt;
    return S_OK;
  }
  STDMETHOD(get_LastUpdated)(VARIANT* p) override { return get_DateCreated(p); }
  STDMETHOD(get_RecordsAffected)(__LONG32* p) override {
    *p = recordsAffected;
    return S_OK;
  }
  STDMETHOD(get_Parameters)(DAOParameters** pp) override {
    SyncParams();
    *pp = params;
    params->AddRef();
    return S_OK;
  }
  STDMETHOD(Execute)(VARIANT) override {
    std::vector<Param> ps = ParamValues();
    Statement st;
    HRESULT hr = ParseStatement(db, D().sql, &ps, st);
    if (FAILED(hr)) return hr;
    hr = RunAction(db, st);
    recordsAffected = db->recordsAffected;
    return hr;
  }
  STDMETHOD(OpenRecordset)(VARIANT Type, VARIANT Options, VARIANT, DAORecordset** pp) override {
    std::vector<Param> ps = ParamValues();
    return daomock::OpenRecordset(db, D().sql, (short)VarLong(Type, dbOpenDynaset),
                                  VarLong(Options, 0), &ps, pp);
  }
  STDMETHOD(Close)() override { return S_OK; }
};

struct QueryDefs : public StubDAOQueryDefs {
  Db* db = NULL;
  bool IsIID(REFIID riid) override { return IsEqualIID(riid, IID_IDAOQueryDefsW) != 0; }
  STDMETHOD(get_Count)(short* p) override {
    *p = (short)db->queries.size();
    return S_OK;
  }
  STDMETHOD(Refresh)() override { return S_OK; }
  STDMETHOD(get_Item)(VARIANT item, DAOQueryDef** pp) override {
    QueryData* q = NULL;
    if (item.vt == VT_BSTR) q = db->FindQuery(CString(item.bstrVal));
    else if (VarLong(item, -1) >= 0 && VarLong(item, -1) < (long)db->queries.size())
      q = &db->queries[VarLong(item)];
    if (q == NULL) return Fail(3265, CString(_T("Item not found in this collection: ")) + VarString(item));
    QueryDef* qd = new QueryDef;
    qd->db = db;
    qd->data = *q;
    qd->appended = true;
    *pp = qd;
    return S_OK;
  }
  STDMETHOD(Append)(IDispatch* pObj) override {
    QueryDef* qd = static_cast<QueryDef*>(static_cast<DAOQueryDef*>(pObj));
    if (qd->data.name.IsEmpty()) return Fail(3219, _T("Invalid operation."));
    if (db->FindQuery(qd->data.name) != NULL || db->Find(qd->data.name) != NULL)
      return Fail(3012, CString(_T("Object '")) + qd->data.name + _T("' already exists."));
    db->queries.push_back(qd->data);
    qd->appended = true;
    return S_OK;
  }
  STDMETHOD(Delete)(BSTR name) override {
    for (size_t i = 0; i < db->queries.size(); i++)
      if (db->queries[i].name.CompareNoCase(CString(name)) == 0) {
        db->queries.erase(db->queries.begin() + i);
        return S_OK;
      }
    return Fail(3265, _T("Item not found in this collection."));
  }
};

// ---------------------------------------------------------------------
// Tabledefs

struct TableDef : public Stub_DAOTableDef {
  Db* db = NULL;
  Table* table = NULL;  // appended
  Table pending;        // its definition until then
  Fields* fields = NULL;
  Indexes* indexes = NULL;
  bool IsIID(REFIID riid) override { return IsEqualIID(riid, IID_IDAOTableDefW) != 0; }
  ~TableDef() {
    if (fields != NULL) fields->Release();
    if (indexes != NULL) indexes->Release();
  }
  Table& T() { return table != NULL ? *table : pending; }
  STDMETHOD(get_Name)(BSTR* p) override {
    *p = Bstr(T().name);
    return S_OK;
  }
  STDMETHOD(put_Name)(BSTR b) override {
    T().name = b;
    return S_OK;
  }
  STDMETHOD(get_Attributes)(__LONG32* p) override {
    *p = T().attrs;
    return S_OK;
  }
  STDMETHOD(put_Attributes)(__LONG32 a) override {
    T().attrs = a;
    return S_OK;
  }
  STDMETHOD(get_Connect)(BSTR* p) override {
    *p = Bstr(CString());
    return S_OK;
  }
  STDMETHOD(put_Connect)(BSTR) override { return S_OK; }
  STDMETHOD(get_SourceTableName)(BSTR* p) override {
    *p = Bstr(CString());
    return S_OK;
  }
  STDMETHOD(put_SourceTableName)(BSTR) override { return S_OK; }
  STDMETHOD(get_Updatable)(VARIANT_BOOL* p) override {
    *p = db->readOnly ? VARIANT_FALSE : VARIANT_TRUE;
    return S_OK;
  }
  STDMETHOD(get_ValidationRule)(BSTR* p) override {
    *p = Bstr(T().validationRule);
    return S_OK;
  }
  STDMETHOD(put_ValidationRule)(BSTR b) override {
    T().validationRule = b;
    return S_OK;
  }
  STDMETHOD(get_ValidationText)(BSTR* p) override {
    *p = Bstr(T().validationText);
    return S_OK;
  }
  STDMETHOD(put_ValidationText)(BSTR b) override {
    T().validationText = b;
    return S_OK;
  }
  STDMETHOD(get_DateCreated)(VARIANT* p) override {
    ::VariantInit(p);
    p->vt = VT_DATE;
    p->date = T().created.m_dt;
    return S_OK;
  }
  STDMETHOD(get_LastUpdated)(VARIANT* p) override { return get_DateCreated(p); }
  STDMETHOD(get_RecordCount)(__LONG32* p) override {
    *p = (long)T().rows.size();
    return S_OK;
  }
  STDMETHOD(get_Fields)(DAOFields** pp) override {
    if (fields == NULL) fields = table != NULL ? TableFields(table) : new Fields;
    *pp = fields;
    fields->AddRef();
    return S_OK;
  }
  STDMETHOD(get_Indexes)(DAOIndexes** pp) override {
    if (indexes == NULL) indexes = table != NULL ? TableIndexes(table) : new Indexes;
    *pp = indexes;
    indexes->AddRef();
    return S_OK;
  }
  STDMETHOD(CreateField)(VARIANT Name, VARIANT Type, VARIANT Size, DAOField** pp) override {
    Field* f = new Field;
    f->def.name = VarString(Name);
    f->def.type = (short)VarLong(Type, dbText);
    f->def.size = VarLong(Size, f->def.type == dbText ? 50 : 4);
    f->def.attrs = dbUpdatableField | (f->def.type == dbText ? dbVariableField : dbFixedField);
    *pp = f;
    return S_OK;
  }
  STDMETHOD(CreateIndex)(VARIANT Name, DAOIndex** pp) override {
    IndexObj* x = new IndexObj;
    x->def.name = VarString(Name);
    *pp = x;
    return S_OK;
  }
  STDMETHOD(OpenRecordset)(VARIANT Type, VARIANT Options, DAORecordset** pp) override {
    if (table == NULL) return Fail(3219, _T("Invalid operation."));
    return daomock::OpenRecordset(db, table->name, (short)VarLong(Type, dbOpenTable),
                                  VarLong(Options, 0), NULL, pp);
  }
  STDMETHOD(RefreshLink)() override { return S_OK; }
};

struct TableDefs : public StubDAOTableDefs {
  Db* db = NULL;
  bool IsIID(REFIID riid) override { return IsEqualIID(riid, IID_IDAOTableDefsW) != 0; }
  STDMETHOD(get_Count)(short* p) override {
    *p = (short)db->tables.size();
    return S_OK;
  }
  STDMETHOD(Refresh)() override { return S_OK; }
  STDMETHOD(get_Item)(VARIANT item, DAOTableDef** pp) override {
    Table* t = NULL;
    if (item.vt == VT_BSTR) {
      t = db->Find(CString(item.bstrVal));
    } else {
      long n = VarLong(item, -1);
      for (auto& x : db->tables)
        if (n-- == 0) t = &x;
    }
    if (t == NULL) return Fail(3265, CString(_T("Item not found in this collection: ")) + VarString(item));
    TableDef* td = new TableDef;
    td->db = db;
    td->table = t;
    *pp = td;
    return S_OK;
  }
  STDMETHOD(Append)(IDispatch* pObj) override {
    TableDef* td = static_cast<TableDef*>(static_cast<DAOTableDef*>(pObj));
    if (td->table != NULL) return Fail(3367, _T("Cannot append. An object with that name already exists in the collection."));
    if (db->Find(td->pending.name) != NULL)
      return Fail(3010, CString(_T("Table '")) + td->pending.name + _T("' already exists."));
    if (td->fields == NULL || td->fields->items.empty())
      return Fail(3264, _T("No field defined--cannot append TableDef or Index."));
    Table t = td->pending;
    t.cols.clear();
    for (auto f : td->fields->items) t.cols.push_back(f->def);
    if (td->indexes != NULL)
      for (auto x : td->indexes->items) {
        x->def.fields.clear();
        for (auto f : x->fields->items)
          x->def.fields.push_back({f->def.name, (f->def.attrs & dbDescending) != 0});
        t.indexes.push_back(x->def);
      }
    db->tables.push_back(t);
    td->table = &db->tables.back();
    // Its fields are the table's columns from now on.
    for (auto f : td->fields->items) f->table = td->table;
    td->fields->table = td->table;
    if (td->indexes != NULL) {
      td->indexes->table = td->table;
      for (auto x : td->indexes->items) x->table = td->table;
    }
    return S_OK;
  }
  STDMETHOD(Delete)(BSTR name) override {
    for (auto it = db->tables.begin(); it != db->tables.end(); ++it)
      if (it->name.CompareNoCase(CString(name)) == 0) {
        db->tables.erase(it);
        return S_OK;
      }
    return Fail(3265, CString(_T("Item not found in this collection: ")) + CString(name));
  }
};

// ---------------------------------------------------------------------
// Relations

struct RelationObj : public Stub_DAORelation {
  Db* db = NULL;
  Relation def;
  Fields* fields = new Fields;
  bool IsIID(REFIID riid) override { return IsEqualIID(riid, IID_IDAORelationW) != 0; }
  ~RelationObj() { fields->Release(); }
  STDMETHOD(get_Name)(BSTR* p) override {
    *p = Bstr(def.name);
    return S_OK;
  }
  STDMETHOD(get_Table)(BSTR* p) override {
    *p = Bstr(def.table);
    return S_OK;
  }
  STDMETHOD(get_ForeignTable)(BSTR* p) override {
    *p = Bstr(def.foreignTable);
    return S_OK;
  }
  STDMETHOD(get_Attributes)(__LONG32* p) override {
    *p = def.attrs;
    return S_OK;
  }
  STDMETHOD(get_Fields)(DAOFields** pp) override {
    *pp = fields;
    fields->AddRef();
    return S_OK;
  }
  STDMETHOD(CreateField)(VARIANT Name, VARIANT, VARIANT, DAOField** pp) override {
    Field* f = new Field;
    f->def.name = VarString(Name);
    *pp = f;
    return S_OK;
  }
};

struct Relations : public StubDAORelations {
  Db* db = NULL;
  bool IsIID(REFIID riid) override { return IsEqualIID(riid, IID_IDAORelationsW) != 0; }
  STDMETHOD(get_Count)(short* p) override {
    *p = (short)db->relations.size();
    return S_OK;
  }
  STDMETHOD(Refresh)() override { return S_OK; }
  STDMETHOD(get_Item)(VARIANT item, DAORelation** pp) override {
    for (size_t i = 0; i < db->relations.size(); i++) {
      bool match = item.vt == VT_BSTR ? db->relations[i].name.CompareNoCase(CString(item.bstrVal)) == 0
                                      : VarLong(item, -1) == (long)i;
      if (!match) continue;
      RelationObj* r = new RelationObj;
      r->db = db;
      r->def = db->relations[i];
      for (auto& f : r->def.fields) {
        Field* fld = new Field;
        fld->def.name = f.first;
        fld->foreignName = f.second;
        r->fields->items.push_back(fld);
      }
      *pp = r;
      return S_OK;
    }
    return Fail(3265, _T("Item not found in this collection."));
  }
  STDMETHOD(Append)(IDispatch* pObj) override {
    RelationObj* r = static_cast<RelationObj*>(static_cast<DAORelation*>(pObj));
    Table* t = db->Find(r->def.table);
    Table* ft = db->Find(r->def.foreignTable);
    if (t == NULL || ft == NULL) return Fail(3265, _T("Item not found in this collection."));
    r->def.fields.clear();
    for (auto f : r->fields->items) {
      if (t->Find(f->def.name) < 0 || ft->Find(f->foreignName) < 0)
        return Fail(3265, _T("Item not found in this collection."));
      r->def.fields.push_back({f->def.name, f->foreignName});
    }
    db->relations.push_back(r->def);
    return S_OK;
  }
  STDMETHOD(Delete)(BSTR name) override {
    for (size_t i = 0; i < db->relations.size(); i++)
      if (db->relations[i].name.CompareNoCase(CString(name)) == 0) {
        db->relations.erase(db->relations.begin() + i);
        return S_OK;
      }
    return Fail(3265, _T("Item not found in this collection."));
  }
};

// ---------------------------------------------------------------------
// Databases, workspaces and the engine

struct Workspace;

struct Database : public StubDAODatabase {
  Db* db = NULL;
  Workspace* ws = NULL;
  bool readOnly = false;
  CString connect;
  bool IsIID(REFIID riid) override { return IsEqualIID(riid, IID_IDAODatabaseW) != 0; }
  STDMETHOD(get_Name)(BSTR* p) override {
    *p = Bstr(db->name);
    return S_OK;
  }
  STDMETHOD(get_Connect)(BSTR* p) override {
    *p = Bstr(connect);
    return S_OK;
  }
  STDMETHOD(get_Version)(BSTR* p) override {
    *p = Bstr(_T("4.0"));
    return S_OK;
  }
  STDMETHOD(get_CollatingOrder)(LONG* p) override {
    *p = dbSortGeneral;
    return S_OK;
  }
  STDMETHOD(get_QueryTimeout)(short* p) override {
    *p = db->queryTimeout;
    return S_OK;
  }
  STDMETHOD(put_QueryTimeout)(short s) override {
    db->queryTimeout = s;
    return S_OK;
  }
  STDMETHOD(get_Transactions)(VARIANT_BOOL* p) override {
    *p = VARIANT_TRUE;
    return S_OK;
  }
  STDMETHOD(get_Updatable)(VARIANT_BOOL* p) override {
    *p = readOnly ? VARIANT_FALSE : VARIANT_TRUE;
    return S_OK;
  }
  STDMETHOD(get_RecordsAffected)(__LONG32* p) override {
    *p = db->recordsAffected;
    return S_OK;
  }
  STDMETHOD(get_TableDefs)(DAOTableDefs** pp) override {
    TableDefs* t = new TableDefs;
    t->db = db;
    *pp = t;
    return S_OK;
  }
  STDMETHOD(get_QueryDefs)(DAOQueryDefs** pp) override {
    QueryDefs* q = new QueryDefs;
    q->db = db;
    *pp = q;
    return S_OK;
  }
  STDMETHOD(get_Relations)(DAORelations** pp) override {
    Relations* r = new Relations;
    r->db = db;
    *pp = r;
    return S_OK;
  }
  STDMETHOD(Close)() override;
  STDMETHOD(Execute)(BSTR Query, VARIANT) override {
    if (readOnly) return Fail(3027, _T("Cannot update. Database or object is read-only."));
    Statement st;
    HRESULT hr = ParseStatement(db, CString(Query), NULL, st);
    if (FAILED(hr)) return hr;
    return RunAction(db, st);
  }
  STDMETHOD(OpenRecordset)(BSTR Name, VARIANT Type, VARIANT Options, VARIANT,
                           DAORecordset** pp) override {
    long options = VarLong(Options, 0);
    if (readOnly) options |= dbReadOnly;
    return daomock::OpenRecordset(db, CString(Name), (short)VarLong(Type, dbOpenTable), options,
                                  NULL, pp);
  }
  STDMETHOD(CreateTableDef)(VARIANT Name, VARIANT Attributes, VARIANT, VARIANT,
                            DAOTableDef** pp) override {
    TableDef* td = new TableDef;
    td->db = db;
    td->pending.name = VarString(Name);
    td->pending.attrs = VarLong(Attributes, 0);
    *pp = td;
    return S_OK;
  }
  STDMETHOD(CreateQueryDef)(VARIANT Name, VARIANT SQLText, DAOQueryDef** pp) override {
    QueryDef* qd = new QueryDef;
    qd->db = db;
    qd->data.name = VarString(Name);
    qd->data.sql = VarString(SQLText);
    // As Jet does, a querydef with a name is saved at once.
    if (!qd->data.name.IsEmpty()) {
      if (db->FindQuery(qd->data.name) != NULL) {
        qd->Release();
        return Fail(3012, _T("Object already exists."));
      }
      db->queries.push_back(qd->data);
      qd->appended = true;
    }
    qd->SyncParams();
    *pp = qd;
    return S_OK;
  }
  STDMETHOD(CreateRelation)(VARIANT Name, VARIANT Table, VARIANT ForeignTable, VARIANT Attributes,
                            DAORelation** pp) override {
    RelationObj* r = new RelationObj;
    r->db = db;
    r->def.name = VarString(Name);
    r->def.table = VarString(Table);
    r->def.foreignTable = VarString(ForeignTable);
    r->def.attrs = VarLong(Attributes, 0);
    *pp = r;
    return S_OK;
  }
};

struct Databases : public StubDAODatabases {
  std::vector<Database*> items;
  bool IsIID(REFIID riid) override { return IsEqualIID(riid, IID_IDAODatabasesW) != 0; }
  STDMETHOD(get_Count)(short* p) override {
    *p = (short)items.size();
    return S_OK;
  }
  STDMETHOD(Refresh)() override { return S_OK; }
  STDMETHOD(get_Item)(VARIANT item, DAODatabase** pp) override {
    for (size_t i = 0; i < items.size(); i++) {
      bool match = item.vt == VT_BSTR ? items[i]->db->name.CompareNoCase(CString(item.bstrVal)) == 0
                                      : VarLong(item, -1) == (long)i;
      if (!match) continue;
      *pp = items[i];
      items[i]->AddRef();
      return S_OK;
    }
    return Fail(3265, _T("Item not found in this collection."));
  }
};

struct Workspace : public StubDAOWorkspace {
  CString name, user;
  short isolate = 0;
  Databases* databases = new Databases;
  bool closed = false;
  bool IsIID(REFIID riid) override { return IsEqualIID(riid, IID_IDAOWorkspaceW) != 0; }
  ~Workspace() { databases->Release(); }
  STDMETHOD(get_Name)(BSTR* p) override {
    *p = Bstr(name);
    return S_OK;
  }
  STDMETHOD(get_UserName)(BSTR* p) override {
    *p = Bstr(user);
    return S_OK;
  }
  STDMETHOD(get_IsolateODBCTrans)(short* p) override {
    *p = isolate;
    return S_OK;
  }
  STDMETHOD(put_IsolateODBCTrans)(short s) override {
    isolate = s;
    return S_OK;
  }
  STDMETHOD(get_Databases)(DAODatabases** pp) override {
    *pp = databases;
    databases->AddRef();
    return S_OK;
  }
  STDMETHOD(Close)() override {
    closed = true;
    return S_OK;
  }
  STDMETHOD(OpenDatabase)(BSTR Name, VARIANT, VARIANT ReadOnly, VARIANT Connect,
                          DAODatabase** pp) override {
    Db* db = G().Find(CString(Name));
    if (db == NULL)
      return Fail(3024, CString(_T("Could not find file '")) + CString(Name) + _T("'."));
    Database* d = new Database;
    d->db = db;
    d->ws = this;
    d->readOnly = !IsMissing(ReadOnly) && VarBool(ReadOnly);
    d->connect = VarString(Connect);
    d->AddRef();
    databases->items.push_back(d);
    *pp = d;
    return S_OK;
  }
  STDMETHOD(CreateDatabase)(BSTR Name, BSTR, VARIANT, DAODatabase** pp) override {
    if (G().Find(CString(Name)) != NULL) return Fail(3204, _T("Database already exists."));
    G().files.push_back(Db());
    G().files.back().name = Name;
    VARIANT missing;
    missing.vt = VT_ERROR;
    missing.scode = DISP_E_PARAMNOTFOUND;
    return OpenDatabase(Name, missing, missing, missing, pp);
  }
  // Transactions keep a copy of every database's tables.
  STDMETHOD(BeginTrans)() override {
    State& g = G();
    if (g.transactionDepth++ == 0) {
      g.saved.clear();
      for (auto& d : g.files) g.saved.push_back({&d, d.tables});
    }
    return S_OK;
  }
  STDMETHOD(CommitTrans)(__LONG32) override {
    if (G().transactionDepth == 0) return Fail(3034, _T("You tried to commit or rollback a transaction without first beginning a transaction."));
    if (--G().transactionDepth == 0) G().saved.clear();
    return S_OK;
  }
  STDMETHOD(Rollback)() override {
    State& g = G();
    if (g.transactionDepth == 0) return Fail(3034, _T("You tried to commit or rollback a transaction without first beginning a transaction."));
    g.transactionDepth = 0;
    for (auto& s : g.saved) s.first->tables = s.second;
    g.saved.clear();
    return S_OK;
  }
  STDMETHOD(get_Type)(LONG* p) override {
    *p = dbUseJet;
    return S_OK;
  }
};

inline HRESULT Database::Close() {
  if (ws != NULL) {
    auto& items = ws->databases->items;
    for (size_t i = 0; i < items.size(); i++)
      if (items[i] == this) {
        items.erase(items.begin() + i);
        Release();
        break;
      }
    ws = NULL;
  }
  return S_OK;
}

struct Workspaces : public StubDAOWorkspaces {
  std::vector<Workspace*> items;
  bool IsIID(REFIID riid) override { return IsEqualIID(riid, IID_IDAOWorkspacesW) != 0; }
  ~Workspaces() {
    for (auto p : items) p->Release();
  }
  STDMETHOD(get_Count)(short* p) override {
    *p = (short)items.size();
    return S_OK;
  }
  STDMETHOD(Refresh)() override { return S_OK; }
  STDMETHOD(get_Item)(VARIANT item, DAOWorkspace** pp) override {
    for (size_t i = 0; i < items.size(); i++) {
      bool match = item.vt == VT_BSTR ? items[i]->name.CompareNoCase(CString(item.bstrVal)) == 0
                                      : VarLong(item, -1) == (long)i;
      if (!match) continue;
      *pp = items[i];
      items[i]->AddRef();
      return S_OK;
    }
    return Fail(3265, CString(_T("Item not found in this collection: ")) + VarString(item));
  }
  STDMETHOD(Append)(IDispatch* pObj) override {
    Workspace* w = static_cast<Workspace*>(static_cast<DAOWorkspace*>(pObj));
    for (auto p : items)
      if (p->name.CompareNoCase(w->name) == 0) return Fail(3367, _T("Cannot append. An object with that name already exists in the collection."));
    w->AddRef();
    items.push_back(w);
    return S_OK;
  }
};

struct Error : public StubDAOError {
  ErrorInfo info;
  bool IsIID(REFIID riid) override { return IsEqualIID(riid, IID_IDAOErrorW) != 0; }
  STDMETHOD(get_Number)(__LONG32* p) override {
    *p = info.nCode;
    return S_OK;
  }
  STDMETHOD(get_Source)(BSTR* p) override {
    *p = Bstr(_T("DAO.Database"));
    return S_OK;
  }
  STDMETHOD(get_Description)(BSTR* p) override {
    *p = Bstr(info.strDescription);
    return S_OK;
  }
  STDMETHOD(get_HelpFile)(BSTR* p) override {
    *p = Bstr(_T("jeterr40.chm"));
    return S_OK;
  }
  STDMETHOD(get_HelpContext)(__LONG32* p) override {
    *p = 5000000 + info.nCode;
    return S_OK;
  }
};

struct ErrorsColl : public StubDAOErrors {
  bool IsIID(REFIID riid) override { return IsEqualIID(riid, IID_IDAOErrorsW) != 0; }
  STDMETHOD(get_Count)(short* p) override {
    *p = (short)Errors().size();
    return S_OK;
  }
  STDMETHOD(Refresh)() override { return S_OK; }
  STDMETHOD(get_Item)(VARIANT item, DAOError** pp) override {
    long n = VarLong(item, -1);
    if (n < 0 || n >= (long)Errors().size()) return E_INVALIDARG;
    Error* e = new Error;
    e->info = Errors()[n];
    *pp = e;
    return S_OK;
  }
};

struct Engine : public Stub_DAODBEngine {
  Workspaces* workspaces = new Workspaces;
  Engine() {
    G().engines++;
    Workspace* w = new Workspace;
    w->name = _T("#Default Workspace#");
    w->user = _T("admin");
    workspaces->items.push_back(w);
  }
  ~Engine() {
    G().engines--;
    workspaces->Release();
  }
  bool IsIID(REFIID riid) override { return IsEqualIID(riid, IID_IDAODBEngineW) != 0; }
  STDMETHOD(get_Version)(BSTR* p) override {
    *p = Bstr(_T("3.6"));
    return S_OK;
  }
  STDMETHOD(get_IniPath)(BSTR* p) override {
    *p = Bstr(G().iniPath);
    return S_OK;
  }
  STDMETHOD(put_IniPath)(BSTR b) override {
    G().iniPath = b;
    return S_OK;
  }
  STDMETHOD(put_DefaultUser)(BSTR b) override {
    G().defaultUser = b;
    return S_OK;
  }
  STDMETHOD(put_DefaultPassword)(BSTR) override { return S_OK; }
  STDMETHOD(get_LoginTimeout)(short* p) override {
    *p = G().loginTimeout;
    return S_OK;
  }
  STDMETHOD(put_LoginTimeout)(short s) override {
    G().loginTimeout = s;
    return S_OK;
  }
  STDMETHOD(get_Workspaces)(DAOWorkspaces** pp) override {
    *pp = workspaces;
    workspaces->AddRef();
    return S_OK;
  }
  STDMETHOD(get_Errors)(DAOErrors** pp) override {
    *pp = new ErrorsColl;
    return S_OK;
  }
  STDMETHOD(Idle)(VARIANT) override {
    G().idles++;
    return S_OK;
  }
  STDMETHOD(CompactDatabase)(BSTR Src, BSTR Dst, VARIANT, VARIANT, VARIANT) override {
    Db* src = G().Find(CString(Src));
    if (src == NULL) return Fail(3024, CString(_T("Could not find file '")) + CString(Src) + _T("'."));
    if (G().Find(CString(Dst)) != NULL) return Fail(3204, _T("Database already exists."));
    Db copy = *src;
    copy.name = Dst;
    G().files.push_back(copy);
    return S_OK;
  }
  STDMETHOD(RepairDatabase)(BSTR Name) override {
    if (G().Find(CString(Name)) == NULL)
      return Fail(3024, CString(_T("Could not find file '")) + CString(Name) + _T("'."));
    return S_OK;
  }
  STDMETHOD(CreateWorkspace)(BSTR Name, BSTR UserName, BSTR, VARIANT, DAOWorkspace** pp) override {
    Workspace* w = new Workspace;
    w->name = Name;
    w->user = UserName;
    *pp = w;
    return S_OK;
  }
};

// The class factory: licensed, as DAO's is.
struct Factory : public IClassFactory2 {
  STDMETHOD(QueryInterface)(REFIID riid, void** ppv) {
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory) ||
        IsEqualIID(riid, IID_IClassFactory2)) {
      *ppv = this;
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return 2; }
  STDMETHOD_(ULONG, Release)() { return 1; }
  STDMETHOD(CreateInstance)(IUnknown*, REFIID, void** ppv) {
    *ppv = NULL;
    return CLASS_E_NOTLICENSED;
  }
  STDMETHOD(LockServer)(BOOL) { return S_OK; }
  STDMETHOD(GetLicInfo)(LICINFO* p) {
    p->cbLicInfo = sizeof(LICINFO);
    p->fRuntimeKeyAvail = TRUE;
    p->fLicVerified = FALSE;
    return S_OK;
  }
  STDMETHOD(RequestLicKey)(DWORD, BSTR*) { return E_NOTIMPL; }
  STDMETHOD(CreateInstanceLic)(IUnknown*, IUnknown*, REFIID riid, BSTR bstrKey, void** ppv) {
    *ppv = NULL;
    if (bstrKey == NULL || lstrcmpW(bstrKey, L"mbmabptebkjcdlgtjmskjwtsdhjbmkmwtrak") != 0)
      return CLASS_E_NOTLICENSED;
    G().licensed++;
    Engine* e = new Engine;
    HRESULT hr = e->QueryInterface(riid, ppv);
    e->Release();
    return hr;
  }
};

// Puts the engine where CoCreateInstance finds DAO 3.6.
inline DWORD Register() {
  static Factory factory;
  DWORD dwCookie = 0;
  ::CoRegisterClassObject(CLSID_DBEngine36, &factory, CLSCTX_INPROC_SERVER, REGCLS_MULTIPLEUSE,
                          &dwCookie);
  return dwCookie;
}

}  // namespace daomock
