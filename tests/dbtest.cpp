// dbtest.cpp - console tests for the ODBC classes in afxdb.h, against the
// in-memory stand-in for ODBC in odbcmock.h.
#include <afx.h>
#include <afxwin.h>
#include <afxdb.h>
#include <afxdisp.h>
#include "odbcmock.h"

static int g_failures = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      g_failures++;                                                   \
    }                                                                 \
  } while (0)

using mock::Value;

static std::string g_longName(1000, 'x');

static void ResetTables() {
  mock::State& g = mock::G();
  g.tables.clear();
  g.log.clear();
  g.readOnly = false;
  mock::Table people;
  people.name = "people";
  people.cols = {{"ID", SQL_INTEGER, true},   {"NAME", SQL_VARCHAR, false},
                 {"AGE", SQL_INTEGER, false}, {"SCORE", SQL_DOUBLE, false},
                 {"BORN", SQL_TIMESTAMP, false}, {"ACTIVE", SQL_BIT, false},
                 {"PHOTO", SQL_VARBINARY, false}};
  people.rows = {
      {Value::Int(1), Value::Text("Ada"), Value::Int(36), Value::Real(91.5), Value::Time(1815, 12, 10),
       Value::Int(1), Value::Binary(std::string("\x01\x02\x00\x03", 4))},
      {Value::Int(2), Value::Text("Brian"), Value(), Value::Real(72.25), Value::Time(1942, 1, 5, 6, 30, 0),
       Value::Int(0), Value()},
      {Value::Int(3), Value::Text("Carol"), Value::Int(29), Value::Real(88), Value(), Value::Int(1),
       Value()},
      {Value::Int(4), Value::Text("Dennis"), Value(), Value(), Value::Time(1941, 9, 9), Value::Int(0),
       Value()},
      {Value::Int(5), Value::Text(g_longName), Value::Int(50), Value::Real(-1), Value(), Value(), Value()},
  };
  g.tables.push_back(people);
  mock::Table events;
  events.name = "events";
  events.cols = {{"ID", SQL_INTEGER, true}, {"STAMP", SQL_TIMESTAMP, false}};
  events.rows = {{Value::Int(1), Value::Time(2001, 2, 3, 4, 5, 6)}};
  g.tables.push_back(events);
}

static const mock::Value& Cell(int nId, const char* pszCol) {
  static mock::Value none;
  mock::Table* t = mock::G().Find("people");
  int c = t->Find(pszCol);
  for (auto& r : t->rows)
    if (r[0].i == nId) return r[c];
  return none;
}

static bool HasRow(int nId) {
  mock::Table* t = mock::G().Find("people");
  for (auto& r : t->rows)
    if (r[0].i == nId) return true;
  return false;
}

class CPeopleSet : public CRecordset {
public:
  long m_ID;
  CString m_Name;
  long m_Age;
  double m_Score;
  COleDateTime m_Born;
  BOOL m_Active;
  CByteArray m_Photo;
  long m_paramID;
  BOOL m_bUseParam;

  CPeopleSet(CDatabase* pdb = NULL) : CRecordset(pdb), m_paramID(0), m_bUseParam(FALSE) {
    m_ID = m_Age = 0;
    m_Score = 0;
    m_Active = FALSE;
    m_nFields = 7;
    m_nDefaultType = snapshot;
  }
  virtual CString GetDefaultConnect() { return _T("ODBC;DSN=Mock"); }
  virtual CString GetDefaultSQL() { return _T("[people]"); }
  virtual void DoFieldExchange(CFieldExchange* pFX) {
    pFX->SetFieldType(CFieldExchange::outputColumn);
    RFX_Long(pFX, _T("[ID]"), m_ID);
    RFX_Text(pFX, _T("[NAME]"), m_Name);
    RFX_Long(pFX, _T("[AGE]"), m_Age);
    RFX_Double(pFX, _T("[SCORE]"), m_Score);
    RFX_Date(pFX, _T("[BORN]"), m_Born);
    RFX_Bool(pFX, _T("[ACTIVE]"), m_Active);
    RFX_Binary(pFX, _T("[PHOTO]"), m_Photo);
    if (m_bUseParam) {
      pFX->SetFieldType(CFieldExchange::param);
      RFX_Long(pFX, _T("IdParam"), m_paramID);
    }
  }
};

class CEventSet : public CRecordset {
public:
  long m_ID;
  CTime m_Stamp;
  CEventSet(CDatabase* pdb) : CRecordset(pdb), m_ID(0) { m_nFields = 2; }
  virtual CString GetDefaultSQL() { return _T("events"); }
  virtual void DoFieldExchange(CFieldExchange* pFX) {
    pFX->SetFieldType(CFieldExchange::outputColumn);
    RFX_Long(pFX, _T("ID"), m_ID);
    RFX_Date(pFX, _T("STAMP"), m_Stamp);
  }
};

class CStampSet : public CRecordset {
public:
  long m_ID;
  TIMESTAMP_STRUCT m_Stamp;
  CStampSet(CDatabase* pdb) : CRecordset(pdb), m_ID(0) {
    memset(&m_Stamp, 0, sizeof(m_Stamp));
    m_nFields = 2;
  }
  virtual CString GetDefaultSQL() { return _T("events"); }
  virtual void DoFieldExchange(CFieldExchange* pFX) {
    pFX->SetFieldType(CFieldExchange::outputColumn);
    RFX_Long(pFX, _T("ID"), m_ID);
    RFX_Date(pFX, _T("STAMP"), m_Stamp);
  }
};

// Runs a call that should throw a CDBException, and returns its code.
template <class F>
static int DBError(F f) {
  try {
    f();
  } catch (CDBException* e) {
    int n = e->m_nRetCode;
    e->Delete();
    return n;
  }
  return 0;
}

static void TestDatabase() {
  CDatabase db;
  CHECK(!db.IsOpen());
  CHECK(db.OpenEx(_T("DSN=Mock"), CDatabase::noOdbcDialog));
  CHECK(db.IsOpen());
  CHECK(db.GetConnect() == _T("ODBC;DSN=Mock;DATABASE=mockdb"));
  CHECK(db.GetDatabaseName() == _T("mockdb"));
  CHECK(db.CanUpdate());
  CHECK(db.CanTransact());
  db.Close();
  CHECK(!db.IsOpen());

  CDatabase db2;
  CHECK(db2.Open(_T("Mock"), FALSE, TRUE));  // read-only
  CHECK(!db2.CanUpdate());
  db2.Close();

  // An unknown data source fails with the driver manager's message.
  CDatabase bad;
  BOOL bThrew = FALSE;
  try {
    bad.OpenEx(_T("DSN=Nowhere"), CDatabase::noOdbcDialog);
  } catch (CDBException* e) {
    bThrew = TRUE;
    CHECK(e->m_strError == _T("Data source name not found\n"));
    CHECK(e->m_strStateNativeOrigin ==
          _T("State:IM002,Native:42,Origin:[Mock][Driver Manager]\n"));
    TCHAR sz[128];
    CHECK(e->GetErrorMessage(sz, 128));
    e->Delete();
  }
  CHECK(bThrew);
  CHECK(!bad.IsOpen());
  // An empty connect string brings up the driver's dialog; "cancel" it.
  CHECK(!bad.OpenEx(_T("")));
}

static void TestBrowse() {
  CDatabase db;
  db.OpenEx(_T("DSN=Mock"), CDatabase::noOdbcDialog);
  CPeopleSet rs(&db);
  CHECK(rs.Open());
  CHECK(rs.IsOpen());
  CHECK(rs.GetSQL() ==
        _T("SELECT \"ID\",\"NAME\",\"AGE\",\"SCORE\",\"BORN\",\"ACTIVE\",\"PHOTO\" FROM \"people\""));
  CHECK(rs.GetTableName() == _T("[people]"));
  CHECK(rs.CanUpdate() && rs.CanAppend() && rs.CanScroll());
  CHECK(!rs.IsBOF() && !rs.IsEOF());
  CHECK(rs.m_ID == 1 && rs.m_Name == _T("Ada") && rs.m_Age == 36 && rs.m_Score == 91.5);
  CHECK(rs.m_Born.GetYear() == 1815 && rs.m_Born.GetMonth() == 12 && rs.m_Born.GetDay() == 10);
  CHECK(rs.m_Active == TRUE);
  CHECK(rs.m_Photo.GetSize() == 4 && rs.m_Photo[1] == 2 && rs.m_Photo[2] == 0);
  CHECK(!rs.IsFieldNull(&rs.m_Age));
  CHECK(!rs.IsFieldNullable(&rs.m_ID));
  CHECK(rs.IsFieldNullable(&rs.m_Age));
  CHECK(rs.GetRecordCount() == 1);  // grows as records are reached

  rs.MoveNext();
  CHECK(rs.m_ID == 2 && rs.m_Name == _T("Brian"));
  CHECK(rs.IsFieldNull(&rs.m_Age));
  CHECK(rs.m_Age == AFX_RFX_LONG_PSEUDO_NULL);
  CHECK(rs.m_Born.GetHour() == 6 && rs.m_Born.GetMinute() == 30);
  CHECK(rs.m_Photo.GetSize() == 0 && rs.IsFieldNull(&rs.m_Photo));
  CHECK(rs.IsFieldNull(NULL));

  rs.MoveNext();
  rs.MoveNext();
  rs.MoveNext();
  CHECK(rs.m_ID == 5);
  CHECK(rs.m_Name.GetLength() == 1000);  // read in pieces
  CHECK(rs.IsFieldNull(&rs.m_Active));
  CRecordsetStatus status;
  rs.GetStatus(status);
  CHECK(status.m_lCurrentRecord == 4 && !status.m_bRecordCountFinal);
  rs.MoveNext();
  CHECK(rs.IsEOF() && !rs.IsBOF());
  CHECK(rs.GetRecordCount() == 5);
  rs.GetStatus(status);
  CHECK(status.m_bRecordCountFinal && status.m_lCurrentRecord == -1);
  CHECK(DBError([&] { rs.MoveNext(); }) == AFX_SQL_ERROR_NO_DATA_FOUND);

  // Scrolling back over the rows already read.
  rs.MovePrev();
  CHECK(rs.m_ID == 5);
  rs.MoveFirst();
  CHECK(rs.m_ID == 1 && rs.m_Name == _T("Ada"));
  rs.MovePrev();
  CHECK(rs.IsBOF() && !rs.IsEOF());
  CHECK(DBError([&] { rs.MovePrev(); }) == AFX_SQL_ERROR_NO_DATA_FOUND);
  rs.SetAbsolutePosition(3);
  CHECK(rs.m_ID == 3 && rs.IsFieldNull(&rs.m_Born));
  rs.Move(-2);
  CHECK(rs.m_ID == 1);
  rs.SetAbsolutePosition(-1);
  CHECK(rs.m_ID == 5);
  rs.MoveLast();
  CHECK(rs.m_ID == 5);
  rs.Move(-1);
  CHECK(rs.m_ID == 4 && rs.IsFieldNull(&rs.m_Score));

  // Values by column.
  CDBVariant var;
  rs.GetFieldValue((short)0, var);
  CHECK(var.m_dwType == DBVT_LONG && var.m_lVal == 4);
  rs.GetFieldValue(_T("NAME"), var);
  CHECK(var.m_dwType == DBVT_STRING && *var.m_pstring == _T("Dennis"));
  rs.GetFieldValue(_T("SCORE"), var);
  CHECK(var.m_dwType == DBVT_NULL);
  rs.GetFieldValue(_T("BORN"), var);
  CHECK(var.m_dwType == DBVT_DATE && var.m_pdate->year == 1941 && var.m_pdate->day == 9);
  rs.GetFieldValue(_T("ACTIVE"), var);
  CHECK(var.m_dwType == DBVT_BOOL && !var.m_boolVal);
  CString str;
  rs.GetFieldValue((short)0, str);
  CHECK(str == _T("4"));
  CODBCFieldInfo info;
  rs.GetODBCFieldInfo((short)1, info);
  CHECK(info.m_strName == _T("NAME") && info.m_nSQLType == SQL_VARCHAR);
  CHECK(rs.GetODBCFieldCount() == 7);
  CHECK(DBError([&] { rs.GetFieldValue(_T("NOPE"), var); }) == AFX_SQL_ERROR_FIELD_NOT_FOUND);

  // Bookmarks need the option.
  CHECK(DBError([&] { rs.GetBookmark(var); }) == AFX_SQL_ERROR_BOOKMARKS_NOT_ENABLED);
  rs.Close();
  CHECK(!rs.IsOpen());
  CHECK(rs.Open(CRecordset::snapshot, NULL, CRecordset::useBookmarks));
  rs.MoveNext();
  CDBVariant mark;
  rs.GetBookmark(mark);
  rs.MoveLast();
  rs.SetBookmark(mark);
  CHECK(rs.m_ID == 2);
  rs.Close();
}

// A procedure's result set, return value and parameters (the mock's
// GREET), and one with an input/output parameter and no result set (BUMP).
class CGreetSet : public CRecordset {
public:
  CString m_Word;
  long m_nLength;
  CString m_strName;
  CString m_strGreeting;
  CGreetSet(CDatabase* pdb) : CRecordset(pdb), m_nLength(-1) {
    m_nFields = 1;
    m_nParams = 3;
    m_nDefaultType = forwardOnly;
  }
  virtual CString GetDefaultSQL() { return _T("{? = CALL GREET(?, ?)}"); }
  virtual void DoFieldExchange(CFieldExchange* pFX) {
    pFX->SetFieldType(CFieldExchange::outputColumn);
    RFX_Text(pFX, _T("[WORD]"), m_Word);
    pFX->SetFieldType(CFieldExchange::outputParam);
    RFX_Long(pFX, _T("Length"), m_nLength);
    pFX->SetFieldType(CFieldExchange::inputParam);
    RFX_Text(pFX, _T("Name"), m_strName);
    pFX->SetFieldType(CFieldExchange::outputParam);
    RFX_Text(pFX, _T("Greeting"), m_strGreeting, 50);
  }
};

class CBumpSet : public CRecordset {
public:
  long m_n;
  CBumpSet(CDatabase* pdb) : CRecordset(pdb), m_n(0) {
    m_nParams = 1;
    m_nDefaultType = forwardOnly;
  }
  virtual CString GetDefaultSQL() { return _T("{CALL BUMP(?)}"); }
  virtual void DoFieldExchange(CFieldExchange* pFX) {
    pFX->SetFieldType(CFieldExchange::inoutParam);
    RFX_Long(pFX, _T("N"), m_n);
  }
};

static void TestProcedures() {
  CDatabase db;
  db.OpenEx(_T("DSN=Mock"), CDatabase::noOdbcDialog);
  CGreetSet gs(&db);
  gs.m_strName = _T("Ada");
  CHECK(gs.Open());
  CHECK(gs.GetSQL() == _T("{? = CALL GREET(?, ?)}"));
  CHECK(gs.m_Word == _T("Hello"));
  gs.MoveNext();
  CHECK(gs.m_Word == _T("Ada"));
  gs.MoveNext();
  CHECK(gs.IsEOF());
  // The output parameters come after the last result set.
  CHECK(gs.m_nLength == -1 && gs.m_strGreeting.IsEmpty());
  CHECK(!gs.FlushResultSet());
  CHECK(gs.m_nLength == 10 && gs.m_strGreeting == _T("Hello, Ada"));

  // Again with another input, and with a NULL one, which gives NULLs.
  gs.m_strName = _T("Grace");
  CHECK(gs.Requery());
  CHECK(gs.m_Word == _T("Hello"));
  while (gs.FlushResultSet()) {}
  CHECK(gs.m_nLength == 12 && gs.m_strGreeting == _T("Hello, Grace"));
  gs.SetParamNull(1);
  CHECK(gs.Requery());
  while (gs.FlushResultSet()) {}
  CHECK(gs.m_nLength == 0 && gs.m_strGreeting.IsEmpty());
  gs.Close();

  // An input/output parameter, from a procedure without a result set.
  CBumpSet bs(&db);
  bs.m_n = 5;
  CHECK(bs.Open());
  CHECK(bs.IsBOF() && bs.IsEOF());
  CHECK(bs.m_n == 15);
  CHECK(bs.Requery());
  CHECK(bs.m_n == 25);
  bs.Close();
}

static void TestFilterAndParams() {
  CDatabase db;
  db.OpenEx(_T("DSN=Mock"), CDatabase::noOdbcDialog);
  CPeopleSet rs(&db);
  rs.m_strFilter = _T("[AGE] > 30");
  rs.m_strSort = _T("[AGE] DESC");
  CHECK(rs.Open());
  CHECK(rs.GetSQL().Find(_T(" WHERE \"AGE\" > 30 ORDER BY \"AGE\" DESC")) > 0);
  CHECK(rs.m_ID == 5);
  rs.MoveNext();
  CHECK(rs.m_ID == 1);
  rs.MoveNext();
  CHECK(rs.IsEOF());

  // Requery picks up a new filter.
  rs.m_strFilter = _T("[NAME] = 'Carol'");
  rs.m_strSort.Empty();
  CHECK(rs.Requery());
  CHECK(rs.m_ID == 3 && !rs.IsEOF());
  rs.MoveNext();
  CHECK(rs.IsEOF());
  rs.Close();

  // A parameter.
  CPeopleSet ps(&db);
  ps.m_bUseParam = TRUE;
  ps.m_nParams = 1;
  ps.m_strFilter = _T("[ID] = ?");
  ps.m_paramID = 4;
  CHECK(ps.Open());
  CHECK(ps.m_ID == 4 && ps.m_Name == _T("Dennis"));
  ps.m_paramID = 2;
  ps.Requery();
  CHECK(ps.m_ID == 2 && ps.m_Name == _T("Brian"));
  ps.m_paramID = 99;
  ps.Requery();
  CHECK(ps.IsBOF() && ps.IsEOF());
  CHECK(ps.GetRecordCount() == 0);
  ps.Close();

  // An empty result.
  rs.m_strFilter = _T("[ID] > 100");
  rs.Open();
  CHECK(rs.IsBOF() && rs.IsEOF());
  CHECK(DBError([&] { rs.Edit(); }) == AFX_SQL_ERROR_NO_CURRENT_RECORD);
  rs.Close();
}

static void TestUpdate() {
  CDatabase db;
  db.OpenEx(_T("DSN=Mock"), CDatabase::noOdbcDialog);
  mock::State& g = mock::G();
  CPeopleSet rs(&db);
  rs.Open();
  rs.SetAbsolutePosition(3);
  CHECK(rs.m_ID == 3);

  // Edit one field: only it is written, and the row is found by the
  // values it was read with.
  rs.Edit();
  rs.m_Age = 30;
  CHECK(rs.IsFieldDirty(&rs.m_Age));
  CHECK(!rs.IsFieldDirty(&rs.m_Name));
  size_t nLog = g.log.size();
  CHECK(rs.Update());
  CHECK(g.log.size() == nLog + 1);
  CHECK(g.log.back() ==
        "UPDATE \"people\" SET \"AGE\"=? WHERE \"ID\"=? AND \"NAME\"=? AND \"AGE\"=? AND "
        "\"SCORE\"=? AND \"BORN\" IS NULL AND \"ACTIVE\"=?");
  CHECK(Cell(3, "AGE").i == 30);
  CHECK(rs.m_Age == 30);
  rs.MoveNext();
  rs.MovePrev();
  CHECK(rs.m_ID == 3 && rs.m_Age == 30);  // the kept row has the new value

  // Nothing changed: no statement.
  rs.Edit();
  nLog = g.log.size();
  CHECK(!rs.Update());
  CHECK(g.log.size() == nLog);
  CHECK(rs.m_nEditMode == CRecordset::noMode);

  // A null field gets a value, and another becomes null.
  rs.Edit();
  rs.m_Born.SetDate(1990, 5, 6);
  rs.SetFieldNull(&rs.m_Score);
  CHECK(rs.Update());
  CHECK(Cell(3, "BORN").kind == mock::kTime && Cell(3, "BORN").ts.year == 1990);
  CHECK(Cell(3, "SCORE").kind == mock::kNull);
  CHECK(!rs.IsFieldNull(&rs.m_Born) && rs.IsFieldNull(&rs.m_Score));

  // Cancelling puts the values back.
  rs.Edit();
  rs.m_Name = _T("Changed");
  rs.CancelUpdate();
  CHECK(rs.m_Name == _T("Carol"));
  rs.Edit();
  rs.m_Name = _T("Moved away");
  rs.MoveNext();  // moving cancels the edit
  rs.MovePrev();
  CHECK(rs.m_Name == _T("Carol"));
  CHECK(Cell(3, "NAME").s == "Carol");

  // Text and binary data.
  rs.Edit();
  rs.m_Name = _T("O'Neil");
  rs.m_Photo.SetSize(3);
  rs.m_Photo[0] = 9;
  rs.m_Photo[1] = 0;
  rs.m_Photo[2] = 7;
  CHECK(rs.Update());
  CHECK(Cell(3, "NAME").s == "O'Neil");
  CHECK(Cell(3, "PHOTO").s == std::string("\x09\x00\x07", 3));

  // Someone else changed the row: the update finds nothing.
  rs.Edit();
  rs.m_Age = 31;
  mock::G().Find("people")->rows[2][3] = Value::Real(1.0);  // SCORE was null
  CHECK(DBError([&] { rs.Update(); }) == AFX_SQL_ERROR_NO_ROWS_AFFECTED);
  CHECK(rs.m_nEditMode == CRecordset::edit);
  rs.CancelUpdate();

  // AddNew writes only the fields that were set.
  rs.MoveFirst();
  rs.AddNew();
  CHECK(rs.IsFieldNull(&rs.m_ID) && rs.IsFieldNull(&rs.m_Name));
  rs.m_ID = 6;
  rs.m_Name = _T("Eve");
  rs.m_Age = 0;  // 0 is a value, not null
  CHECK(rs.Update());
  CHECK(g.log.back() == "INSERT INTO \"people\" (\"ID\",\"NAME\",\"AGE\") VALUES (?,?,?)");
  CHECK(HasRow(6) && Cell(6, "NAME").s == "Eve" && Cell(6, "AGE").i == 0);
  CHECK(Cell(6, "SCORE").kind == mock::kNull);
  CHECK(rs.m_ID == 1 && rs.m_Name == _T("Ada"));  // back on the record before AddNew
  // A failed insert leaves the mode so the program can fix it or cancel.
  rs.AddNew();
  rs.m_Name = _T("No id");
  CHECK(DBError([&] { rs.Update(); }) == SQL_ERROR);
  CHECK(rs.m_nEditMode == CRecordset::addnew);
  rs.CancelUpdate();
  CHECK(rs.m_ID == 1);
  CHECK(DBError([&] { rs.Update(); }) == AFX_SQL_ERROR_ILLEGAL_MODE);

  // The new record shows up after Requery.
  rs.Requery();
  rs.MoveLast();
  CHECK(rs.GetRecordCount() == 6 && rs.m_ID == 6);

  // Delete.
  rs.SetAbsolutePosition(2);
  CHECK(rs.m_ID == 2);
  rs.Delete();
  CHECK(!HasRow(2));
  CHECK(rs.IsDeleted());
  CHECK(rs.IsFieldNull(&rs.m_ID));
  CHECK(DBError([&] { rs.Edit(); }) == AFX_SQL_ERROR_NO_CURRENT_RECORD);
  rs.MoveNext();
  CHECK(rs.m_ID == 3 && !rs.IsDeleted());
  rs.MovePrev();
  CHECK(rs.IsDeleted());  // still a hole without skipDeletedRecords
  rs.Close();

  CPeopleSet skip(&db);
  skip.Open(CRecordset::dynaset, NULL, CRecordset::skipDeletedRecords);
  skip.MoveNext();
  CHECK(skip.m_ID == 3);
  skip.Delete();
  skip.MovePrev();
  CHECK(skip.m_ID == 1);
  skip.MoveNext();
  CHECK(skip.m_ID == 4);  // passes over the deleted record
  skip.Close();
}

static void TestReadOnlyAndForwardOnly() {
  CDatabase db;
  db.OpenEx(_T("DSN=Mock"), CDatabase::noOdbcDialog);
  CPeopleSet ro(&db);
  ro.Open(CRecordset::snapshot, NULL, CRecordset::readOnly);
  CHECK(!ro.CanUpdate() && !ro.CanAppend());
  CHECK(DBError([&] { ro.Edit(); }) == AFX_SQL_ERROR_RECORDSET_READONLY);
  CHECK(DBError([&] { ro.AddNew(); }) == AFX_SQL_ERROR_RECORDSET_READONLY);
  ro.Close();

  CPeopleSet fo(&db);
  fo.Open(CRecordset::forwardOnly);
  CHECK(!fo.CanScroll() && !fo.CanUpdate());
  long nCount = 0;
  while (!fo.IsEOF()) {
    nCount++;
    fo.MoveNext();
  }
  CHECK(nCount == 5 && fo.GetRecordCount() == 5);
  CHECK(DBError([&] { fo.MovePrev(); }) == AFX_SQL_ERROR_NO_DATA_FOUND ||
        DBError([&] { fo.MovePrev(); }) == AFX_SQL_ERROR_RECORDSET_FORWARD_ONLY);
  fo.Requery();
  fo.MoveNext();
  CHECK(fo.m_ID == 2);
  CHECK(DBError([&] { fo.MovePrev(); }) == AFX_SQL_ERROR_RECORDSET_FORWARD_ONLY);
  CHECK(DBError([&] { fo.MoveLast(); }) == AFX_SQL_ERROR_RECORDSET_FORWARD_ONLY);
  fo.Close();

  // A whole database opened read-only.
  CDatabase rodb;
  rodb.OpenEx(_T("DSN=Mock"), CDatabase::noOdbcDialog | CDatabase::openReadOnly);
  CPeopleSet rs2(&rodb);
  rs2.Open();
  CHECK(!rs2.CanUpdate());
  rs2.Close();
}

static void TestAdHoc() {
  CDatabase db;
  db.OpenEx(_T("DSN=Mock"), CDatabase::noOdbcDialog);
  // No fields at all: the values come by column.
  CRecordset rs(&db);
  CHECK(rs.Open(CRecordset::forwardOnly, _T("SELECT NAME, AGE, SCORE, BORN, PHOTO FROM people ORDER BY ID")));
  CHECK(rs.GetODBCFieldCount() == 5);
  CHECK(rs.GetTableName() == _T("people"));
  CDBVariant var;
  rs.GetFieldValue((short)0, var);
  CHECK(var.m_dwType == DBVT_STRING && *var.m_pstring == _T("Ada"));
  rs.GetFieldValue(_T("AGE"), var);
  CHECK(var.m_dwType == DBVT_LONG && var.m_lVal == 36);
  rs.GetFieldValue(_T("SCORE"), var);
  CHECK(var.m_dwType == DBVT_DOUBLE && var.m_dblVal == 91.5);
  rs.GetFieldValue(_T("BORN"), var);
  CHECK(var.m_dwType == DBVT_DATE && var.m_pdate->year == 1815);
  rs.GetFieldValue(_T("PHOTO"), var);
  CHECK(var.m_dwType == DBVT_BINARY && var.m_pbinary->m_dwDataLength == 4);
  CString str;
  rs.GetFieldValue(_T("PHOTO"), str);
  CHECK(str == _T("01020003"));
  rs.GetFieldValue(_T("SCORE"), var, SQL_C_CHAR);
  CHECK(var.m_dwType == DBVT_STRING && *var.m_pstring == _T("91.5"));
  rs.MoveNext();
  rs.GetFieldValue(_T("AGE"), var);
  CHECK(var.m_dwType == DBVT_NULL);
  rs.Close();

  // A bad statement reports the driver's state.
  CRecordset bad(&db);
  BOOL bThrew = FALSE;
  try {
    bad.Open(CRecordset::snapshot, _T("SELECT * FROM nosuch"));
  } catch (CDBException* e) {
    bThrew = TRUE;
    CHECK(e->m_nRetCode == SQL_ERROR);
    CHECK(e->m_strStateNativeOrigin.Find(_T("State:42S02")) == 0);
    CHECK(e->m_strError == _T("Unknown table nosuch\n"));
    e->Delete();
  }
  CHECK(bThrew);
  CHECK(!bad.IsOpen());

  // CTime and TIMESTAMP_STRUCT fields.
  CEventSet ev(&db);
  ev.Open();
  CHECK(ev.m_Stamp.GetYear() == 2001 && ev.m_Stamp.GetMonth() == 2 && ev.m_Stamp.GetSecond() == 6);
  ev.Edit();
  ev.m_Stamp = CTime(2010, 11, 12, 13, 14, 15);
  CHECK(ev.Update());
  CHECK(mock::G().Find("events")->rows[0][1].Str() == "2010-11-12 13:14:15");
  ev.Close();
  CStampSet st(&db);
  st.Open();
  CHECK(st.m_Stamp.year == 2010 && st.m_Stamp.hour == 13);
  st.Close();
}

static void TestExecuteAndTransactions() {
  CDatabase db;
  db.OpenEx(_T("DSN=Mock"), CDatabase::noOdbcDialog);
  db.ExecuteSQL(_T("DELETE FROM people WHERE ID = 6"));
  CHECK(!HasRow(6));
  db.ExecuteSQL(_T("DELETE FROM people WHERE ID = 600"));  // no rows: not an error
  CHECK(DBError([&] { db.ExecuteSQL(_T("NONSENSE")); }) == SQL_ERROR);

  CHECK(db.BeginTrans());
  db.ExecuteSQL(_T("DELETE FROM people WHERE ID = 1"));
  CHECK(!HasRow(1));
  CHECK(db.Rollback());
  CHECK(HasRow(1));
  CHECK(db.BeginTrans());
  db.ExecuteSQL(_T("UPDATE people SET AGE = 37 WHERE ID = 1"));
  CHECK(db.CommitTrans());
  CHECK(Cell(1, "AGE").i == 37);
  CHECK(!db.CommitTrans());  // none pending

  // Closing the database closes its recordsets.
  CPeopleSet rs(&db);
  rs.Open();
  CHECK(rs.IsOpen());
  db.Close();
  CHECK(!rs.IsOpen());

  // A recordset can open its own connection.
  CPeopleSet own;
  CHECK(own.Open());
  CHECK(own.m_pDatabase != NULL && own.m_pDatabase->IsOpen());
  own.Close();
  CHECK(own.m_pDatabase == NULL);
  CHECK(mock::G().nOpenStatements == 0);
}

int main() {
  ResetTables();
  TestDatabase();
  TestBrowse();
  TestFilterAndParams();
  TestProcedures();
  ResetTables();
  TestUpdate();
  ResetTables();
  TestReadOnlyAndForwardOnly();
  TestAdHoc();
  TestExecuteAndTransactions();
  if (g_failures == 0) printf("PASS: all database tests\n");
  else printf("%d failure(s)\n", g_failures);
  return g_failures ? 1 : 0;
}
