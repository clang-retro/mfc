// daotest.cpp - console tests for the DAO classes in afxdao.h, against the
// in-memory stand-in for the DAO engine in daomock.h.
#include <afx.h>
#include <afxwin.h>
#include <afxdao.h>
#include <stdio.h>
#include "daomock.h"

static int g_failures = 0;
static const char* g_stage = "";
#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      g_failures++;                                                   \
    }                                                                 \
  } while (0)

// Runs a statement that should throw a CDaoException, and gives its Jet
// error code (0 if nothing was thrown).
#define DAO_ERROR(stmt, code)                                         \
  do {                                                                \
    code = 0;                                                         \
    try {                                                             \
      stmt;                                                           \
    } catch (CDaoException* e) {                                      \
      code = e->m_pErrorInfo ? e->m_pErrorInfo->m_lErrorCode : -1;    \
      e->Delete();                                                    \
    }                                                                 \
  } while (0)

static const LPCTSTR kFile = _T("C:\\people.mdb");

// The people table: ID (counter), NAME, AGE, SCORE, BORN, ACTIVE.
class CPeopleSet : public CDaoRecordset {
public:
  CPeopleSet(CDaoDatabase* pdb = NULL) : CDaoRecordset(pdb) {
    m_ID = 0;
    m_AGE = 0;
    m_SCORE = 0;
    m_ACTIVE = FALSE;
    m_nFields = 6;
    m_nDefaultType = dbOpenDynaset;
  }
  long m_ID;
  CString m_NAME;
  long m_AGE;
  double m_SCORE;
  COleDateTime m_BORN;
  BOOL m_ACTIVE;
  CString GetDefaultDBName() override { return kFile; }
  CString GetDefaultSQL() override { return _T("[people]"); }
  void DoFieldExchange(CDaoFieldExchange* pFX) override {
    pFX->SetFieldType(CDaoFieldExchange::outputColumn);
    DFX_Long(pFX, _T("[ID]"), m_ID);
    DFX_Text(pFX, _T("[NAME]"), m_NAME);
    DFX_Long(pFX, _T("[AGE]"), m_AGE);
    DFX_Double(pFX, _T("[SCORE]"), m_SCORE);
    DFX_DateTime(pFX, _T("[BORN]"), m_BORN);
    DFX_Bool(pFX, _T("[ACTIVE]"), m_ACTIVE);
  }
};

// The same, with a parameter for the lowest age.
class COlderSet : public CPeopleSet {
public:
  COlderSet(CDaoDatabase* pdb) : CPeopleSet(pdb) {
    m_nParams = 1;
    m_minAge = 0;
    m_strFilter = _T("[AGE] >= [MinAge]");
    m_strSort = _T("[AGE] DESC");
  }
  long m_minAge;
  void DoFieldExchange(CDaoFieldExchange* pFX) override {
    CPeopleSet::DoFieldExchange(pFX);
    pFX->SetFieldType(CDaoFieldExchange::param);
    DFX_Long(pFX, _T("[MinAge]"), m_minAge);
  }
};

static void TestEngine() {
  CHECK(AfxDaoGetEngine() != NULL);
  CHECK(CDaoWorkspace::GetVersion() == _T("3.6"));
  CDaoWorkspace::SetLoginTimeout(42);
  CHECK(CDaoWorkspace::GetLoginTimeout() == 42);
  CDaoWorkspace::SetIniPath(_T("Software\\Test\\Jet"));
  CHECK(CDaoWorkspace::GetIniPath() == _T("Software\\Test\\Jet"));
}

static void TestCreate(CDaoDatabase& db) {
  db.Create(kFile);
  CHECK(db.IsOpen());
  CHECK(db.GetName() == kFile);
  CHECK(db.CanUpdate());
  CHECK(db.CanTransact());
  CHECK(db.m_pWorkspace != NULL && db.m_pWorkspace->IsOpen());
  CHECK(db.GetTableDefCount() == 0);

  CDaoTableDef td(&db);
  td.Create(_T("people"));
  td.CreateField(_T("ID"), dbLong, 4, dbAutoIncrField);
  td.CreateField(_T("NAME"), dbText, 30, dbVariableField);
  td.CreateField(_T("AGE"), dbLong, 4);
  td.CreateField(_T("SCORE"), dbDouble, 8);
  td.CreateField(_T("BORN"), dbDate, 8);
  CDaoFieldInfo fi;
  fi.m_strName = _T("ACTIVE");
  fi.m_nType = dbBoolean;
  fi.m_lSize = 1;
  fi.m_lAttributes = dbFixedField;
  fi.m_bRequired = FALSE;
  fi.m_bAllowZeroLength = FALSE;
  fi.m_lCollatingOrder = 0;
  fi.m_nOrdinalPosition = 0;
  td.CreateField(fi);

  CDaoIndexFieldInfo key[1];
  key[0].m_strName = _T("ID");
  key[0].m_bDescending = FALSE;
  CDaoIndexInfo ii;
  ii.m_strName = _T("PrimaryKey");
  ii.m_pFieldInfos = key;
  ii.m_nFields = 1;
  ii.m_bPrimary = TRUE;
  ii.m_bUnique = TRUE;
  ii.m_bClustered = FALSE;
  ii.m_bIgnoreNulls = FALSE;
  ii.m_bRequired = TRUE;
  td.CreateIndex(ii);

  CDaoIndexFieldInfo byName[1];
  byName[0].m_strName = _T("NAME");
  byName[0].m_bDescending = FALSE;
  CDaoIndexInfo ni;
  ni.m_strName = _T("ByName");
  ni.m_pFieldInfos = byName;
  ni.m_nFields = 1;
  ni.m_bPrimary = FALSE;
  ni.m_bUnique = TRUE;
  ni.m_bClustered = FALSE;
  ni.m_bIgnoreNulls = FALSE;
  ni.m_bRequired = FALSE;
  td.CreateIndex(ni);
  td.Append();
  td.Close();

  CHECK(db.GetTableDefCount() == 1);
  CDaoTableDefInfo ti;
  db.GetTableDefInfo(0, ti, AFX_DAO_ALL_INFO);
  CHECK(ti.m_strName == _T("people"));
  CHECK(ti.m_lRecordCount == 0);

  // A table opened again shows its fields and indexes.
  CDaoTableDef t2(&db);
  t2.Open(_T("people"));
  CHECK(t2.GetFieldCount() == 6);
  CDaoFieldInfo f;
  t2.GetFieldInfo(1, f, AFX_DAO_ALL_INFO);
  CHECK(f.m_strName == _T("NAME"));
  CHECK(f.m_nType == dbText);
  CHECK(f.m_lSize == 30);
  t2.GetFieldInfo(_T("ID"), f);
  CHECK((f.m_lAttributes & dbAutoIncrField) != 0);
  CHECK(t2.GetIndexCount() == 2);
  CDaoIndexInfo ix;
  t2.GetIndexInfo(_T("PrimaryKey"), ix, AFX_DAO_ALL_INFO);
  CHECK(ix.m_bPrimary && ix.m_bUnique);
  CHECK(ix.m_nFields == 1 && ix.m_pFieldInfos[0].m_strName == _T("ID"));
  t2.Close();

  // Rows, through Execute.
  db.Execute(_T("INSERT INTO [people] ([NAME], [AGE], [SCORE], [BORN], [ACTIVE]) ")
             _T("VALUES ('Ann', 31, 2.5, #1970-05-06#, TRUE)"));
  CHECK(db.GetRecordsAffected() == 1);
  db.Execute(_T("INSERT INTO [people] ([NAME], [AGE], [SCORE], [ACTIVE]) ")
             _T("VALUES ('Bob', 45, 7.25, FALSE)"));
  db.Execute(_T("INSERT INTO [people] ([NAME], [AGE], [SCORE], [ACTIVE]) ")
             _T("VALUES ('Cid', 22, 1, TRUE)"));
  db.Execute(_T("INSERT INTO [people] ([NAME], [AGE], [ACTIVE]) VALUES ('Dee', 60, FALSE)"));
  db.GetTableDefInfo(_T("people"), ti, AFX_DAO_ALL_INFO);
  CHECK(ti.m_lRecordCount == 4);
}

static void TestBrowse(CDaoDatabase& db) {
  CPeopleSet rs(&db);
  rs.m_strSort = _T("[ID]");
  rs.Open();
  CHECK(rs.IsOpen());
  CHECK(rs.GetType() == dbOpenDynaset);
  CHECK(rs.CanUpdate() && rs.CanAppend() && rs.CanBookmark());
  CHECK(!rs.IsBOF() && !rs.IsEOF());
  CHECK(rs.m_ID == 1 && rs.m_NAME == _T("Ann") && rs.m_AGE == 31);
  CHECK(rs.m_SCORE == 2.5);
  CHECK(rs.m_BORN.GetYear() == 1970 && rs.m_BORN.GetMonth() == 5 && rs.m_BORN.GetDay() == 6);
  CHECK(rs.m_ACTIVE);
  CHECK(!rs.IsFieldNull(&rs.m_BORN));
  rs.MoveNext();
  CHECK(rs.m_NAME == _T("Bob") && !rs.m_ACTIVE);
  CHECK(rs.IsFieldNull(&rs.m_BORN));
  CHECK(rs.IsFieldNullable(&rs.m_BORN));
  rs.MoveLast();
  CHECK(rs.m_NAME == _T("Dee"));
  CHECK(rs.IsFieldNull(&rs.m_SCORE));
  CHECK(rs.GetRecordCount() == 4);
  CHECK(rs.GetAbsolutePosition() == 3);
  rs.MovePrev();
  CHECK(rs.m_NAME == _T("Cid"));
  rs.MoveNext();
  rs.MoveNext();
  CHECK(rs.IsEOF());
  rs.MoveFirst();
  CHECK(rs.m_NAME == _T("Ann"));
  rs.Move(2);
  CHECK(rs.m_NAME == _T("Cid"));
  rs.SetAbsolutePosition(1);
  CHECK(rs.m_NAME == _T("Bob"));

  // Field values by name and number.
  COleVariant v = rs.GetFieldValue(_T("AGE"));
  CHECK(v.vt == VT_I4 && v.lVal == 45);
  v = rs.GetFieldValue(1);
  CHECK(v.vt == VT_BSTR && CString((LPCTSTR)v.bstrVal) == _T("Bob"));  // V_BSTRT
  CHECK(rs.GetFieldCount() == 6);
  CDaoFieldInfo fi;
  rs.GetFieldInfo(2, fi);
  CHECK(fi.m_strName == _T("AGE") && fi.m_nType == dbLong);

  // Bookmarks.
  COleVariant bm = rs.GetBookmark();
  rs.MoveFirst();
  rs.SetBookmark(bm);
  CHECK(rs.m_NAME == _T("Bob"));

  // Find.
  CHECK(rs.FindFirst(_T("[AGE] > 40")));
  CHECK(rs.m_NAME == _T("Bob"));
  CHECK(rs.FindNext(_T("[AGE] > 40")));
  CHECK(rs.m_NAME == _T("Dee"));
  CHECK(!rs.FindNext(_T("[AGE] > 40")));
  CHECK(rs.m_NAME == _T("Dee"));  // a failed find leaves the record where it was
  CHECK(rs.FindLast(_T("[ACTIVE] = TRUE")));
  CHECK(rs.m_NAME == _T("Cid"));
  CHECK(rs.FindPrev(_T("[ACTIVE] = TRUE")));
  CHECK(rs.m_NAME == _T("Ann"));
  CHECK(rs.Find(AFX_DAO_FIRST, _T("[NAME] = 'dee'")));
  CHECK(rs.m_ID == 4);
  rs.Close();
  CHECK(!rs.IsOpen());

  // A filter and a sort, and Requery after changing them.
  CPeopleSet f(&db);
  f.m_strFilter = _T("[ACTIVE] = TRUE OR [AGE] > 50");
  f.m_strSort = _T("[NAME] DESC");
  f.Open(dbOpenSnapshot);
  CHECK(f.GetType() == dbOpenSnapshot);
  CHECK(!f.CanUpdate());
  CHECK(f.m_NAME == _T("Dee"));
  f.MoveNext();
  CHECK(f.m_NAME == _T("Cid"));
  f.MoveNext();
  CHECK(f.m_NAME == _T("Ann"));
  f.MoveNext();
  CHECK(f.IsEOF());
  f.m_strFilter = _T("[SCORE] IS NULL");
  f.Requery();
  CHECK(!f.IsEOF() && f.m_NAME == _T("Dee"));
  f.MoveNext();
  CHECK(f.IsEOF());
  f.Close();

  // An empty result is both BOF and EOF.
  CPeopleSet e(&db);
  e.m_strFilter = _T("[AGE] > 100");
  e.Open();
  CHECK(e.IsBOF() && e.IsEOF());
  e.Close();

  // A statement of its own, and the members it does not select.
  CPeopleSet s(&db);
  s.Open(dbOpenDynaset, _T("SELECT [ID], [NAME], [AGE], [SCORE], [BORN], [ACTIVE] FROM [people] ")
                        _T("WHERE [NAME] = 'Cid'"));
  CHECK(s.m_AGE == 22);
  s.MoveNext();
  CHECK(s.IsEOF());
  s.Close();
}

static void TestParameters(CDaoDatabase& db) {
  COlderSet rs(&db);
  rs.m_minAge = 40;
  rs.Open();
  CHECK(!rs.IsEOF() && rs.m_NAME == _T("Dee"));
  rs.MoveNext();
  CHECK(rs.m_NAME == _T("Bob"));
  rs.MoveNext();
  CHECK(rs.IsEOF());
  rs.m_minAge = 30;
  rs.Requery();
  CHECK(rs.GetRecordCount() >= 0);
  int n = 0;
  for (rs.MoveFirst(); !rs.IsEOF(); rs.MoveNext()) n++;
  CHECK(n == 3);
  CHECK(rs.GetParamValue(_T("[MinAge]")).lVal == 30);
  rs.Close();
}

static void TestUpdate(CDaoDatabase& db) {
  CPeopleSet rs(&db);
  rs.m_strSort = _T("[ID]");
  rs.Open();

  // AddNew: the members start null, and Update writes what was set.
  rs.AddNew();
  CHECK(rs.GetEditMode() == dbEditAdd);
  CHECK(rs.IsFieldNull(&rs.m_NAME));
  rs.m_NAME = _T("Eve");
  rs.m_AGE = 27;
  rs.m_ACTIVE = TRUE;
  CHECK(rs.IsFieldDirty(&rs.m_NAME));
  rs.Update();
  CHECK(rs.GetEditMode() == dbEditNone);
  rs.SetBookmark(rs.GetLastModifiedBookmark());
  CHECK(rs.m_NAME == _T("Eve") && rs.m_AGE == 27 && rs.m_ID == 5);
  CHECK(rs.IsFieldNull(&rs.m_SCORE));
  CHECK(rs.IsFieldNull(&rs.m_BORN));

  // Edit: only the changed fields go back.
  CHECK(rs.FindFirst(_T("[NAME] = 'Bob'")));
  rs.Edit();
  CHECK(rs.GetEditMode() == dbEditInProgress);
  rs.m_AGE = 46;
  rs.SetFieldNull(&rs.m_SCORE);
  rs.Update();
  CHECK(rs.m_AGE == 46 && rs.IsFieldNull(&rs.m_SCORE));
  rs.MoveFirst();
  CHECK(rs.FindFirst(_T("[NAME] = 'Bob'")));
  CHECK(rs.m_AGE == 46);
  CHECK(rs.GetFieldValue(_T("SCORE")).vt == VT_NULL);

  // CancelUpdate puts the members back.
  rs.Edit();
  rs.m_AGE = 99;
  rs.m_NAME = _T("Zed");
  rs.CancelUpdate();
  CHECK(rs.m_AGE == 46 && rs.m_NAME == _T("Bob"));
  CHECK(rs.GetEditMode() == dbEditNone);

  // SetFieldValue on the record itself.
  rs.Edit();
  rs.SetFieldValue(_T("SCORE"), COleVariant(3.75));
  rs.Update();
  CHECK(rs.m_SCORE == 3.75);
  // Text in a variant is VT_BSTRT: ANSI in an ANSI program.
  rs.Edit();
  rs.SetFieldValue(_T("NAME"), COleVariant(_T("Bobby"), VT_BSTRT));
  rs.Update();
  CHECK(rs.m_NAME == _T("Bobby"));
  COleVariant name = rs.GetFieldValue(_T("NAME"));
  CHECK(CString((LPCTSTR)name.bstrVal) == _T("Bobby"));
  rs.Edit();
  rs.SetFieldValue(_T("NAME"), _T("Bob"));
  rs.Update();
  CHECK(rs.m_NAME == _T("Bob"));

  // Delete.
  CHECK(rs.FindFirst(_T("[NAME] = 'Cid'")));
  rs.Delete();
  CHECK(rs.IsDeleted());
  rs.MoveNext();
  CHECK(rs.m_NAME == _T("Dee"));
  rs.Close();

  CPeopleSet all(&db);
  all.Open(dbOpenSnapshot);
  int n = 0;
  for (; !all.IsEOF(); all.MoveNext()) {
    CHECK(all.m_NAME != _T("Cid"));
    n++;
  }
  CHECK(n == 4);
  all.Close();

  // A duplicate in a unique index is the engine's error 3022.
  CPeopleSet d(&db);
  d.Open();
  d.AddNew();
  d.m_NAME = _T("Ann");
  long code;
  DAO_ERROR(d.Update(), code);
  CHECK(code == 3022);
  d.CancelUpdate();
  d.Close();
}

static void TestTableType(CDaoDatabase& db) {
  CDaoTableDef td(&db);
  td.Open(_T("people"));
  CPeopleSet rs(&db);
  rs.Open(&td);
  CHECK(rs.GetType() == dbOpenTable);
  CHECK(rs.GetIndexCount() == 2);
  rs.SetCurrentIndex(_T("ByName"));
  CHECK(rs.GetCurrentIndex() == _T("ByName"));
  rs.MoveFirst();
  CHECK(rs.m_NAME == _T("Ann"));
  rs.MoveNext();
  CHECK(rs.m_NAME == _T("Bob"));
  COleVariant key(_T("Dee"), VT_BSTRT);
  CHECK(rs.Seek(_T("="), &key));
  CHECK(rs.m_AGE == 60);
  COleVariant c(_T("C"), VT_BSTRT);
  CHECK(rs.Seek(_T(">="), &c));
  CHECK(rs.m_NAME == _T("Dee"));
  COleVariant z(_T("Zz"), VT_BSTRT);
  CHECK(!rs.Seek(_T("="), &z));
  rs.SetCurrentIndex(_T("PrimaryKey"));
  COleVariant id(2L);
  CHECK(rs.Seek(_T("="), &id, 1));
  CHECK(rs.m_NAME == _T("Bob"));
  rs.Close();
  td.Close();

  // A table-type recordset by the table's name.
  CPeopleSet t(&db);
  t.Open(dbOpenTable, _T("people"));
  CHECK(t.GetType() == dbOpenTable);
  CHECK(t.GetRecordCount() == 4);
  t.Close();
}

static void TestQueryDefs(CDaoDatabase& db) {
  CDaoQueryDef qd(&db);
  qd.Create(_T("Adults"), _T("PARAMETERS [Lowest] Long; SELECT * FROM [people] WHERE [AGE] >= [Lowest]"));
  qd.Append();
  CHECK(db.GetQueryDefCount() == 1);
  CHECK(qd.GetName() == _T("Adults"));
  CHECK(qd.GetParameterCount() == 1);
  CDaoParameterInfo pi;
  qd.GetParameterInfo(0, pi);
  CHECK(pi.m_strName == _T("Lowest") && pi.m_nType == dbLong);
  qd.SetParameterValue(_T("Lowest"), COleVariant(45L));
  CHECK(qd.GetParameterValue(0).lVal == 45);
  CPeopleSet rs(&db);
  rs.Open(&qd);
  int n = 0;
  for (; !rs.IsEOF(); rs.MoveNext()) {
    CHECK(rs.m_AGE >= 45);
    n++;
  }
  CHECK(n == 2);
  rs.Close();
  qd.Close();

  CDaoQueryDefInfo qi;
  db.GetQueryDefInfo(_T("Adults"), qi, AFX_DAO_ALL_INFO);
  CHECK(qi.m_strName == _T("Adults"));
  CHECK(qi.m_strSQL.Find(_T("PARAMETERS")) == 0);
  CHECK(qi.m_bReturnsRecords);

  // An action query, opened by its name.
  CDaoQueryDef up(&db);
  up.Create(_T("Older"), _T("UPDATE [people] SET [AGE] = 32 WHERE [NAME] = 'Ann'"));
  up.Append();
  up.Close();
  CDaoQueryDef q2(&db);
  q2.Open(_T("Older"));
  CHECK(q2.GetType() == dbQUpdate);
  q2.Execute();
  CHECK(q2.GetRecordsAffected() == 1);
  q2.Close();
  db.DeleteQueryDef(_T("Older"));
  CHECK(db.GetQueryDefCount() == 1);

  CPeopleSet a(&db);
  a.m_strFilter = _T("[NAME] = 'Ann'");
  a.Open();
  CHECK(a.m_AGE == 32);
  a.Close();

  // A temporary querydef is never in the collection.
  CDaoQueryDef tmp(&db);
  tmp.Create(NULL, _T("DELETE FROM [people] WHERE [AGE] > 100"));
  tmp.Execute();
  CHECK(tmp.GetRecordsAffected() == 0);
  tmp.Close();
  CHECK(db.GetQueryDefCount() == 1);
}

static void TestTransactions(CDaoDatabase& db) {
  CDaoWorkspace* ws = db.m_pWorkspace;
  ws->BeginTrans();
  db.Execute(_T("DELETE FROM [people] WHERE [AGE] > 40"));
  CHECK(db.GetRecordsAffected() == 2);
  ws->Rollback();
  CDaoTableDefInfo ti;
  db.GetTableDefInfo(_T("people"), ti, AFX_DAO_ALL_INFO);
  CHECK(ti.m_lRecordCount == 4);

  ws->BeginTrans();
  db.Execute(_T("UPDATE [people] SET [SCORE] = 10 WHERE [NAME] = 'Dee'"));
  ws->CommitTrans();
  CPeopleSet rs(&db);
  rs.m_strFilter = _T("[NAME] = 'Dee'");
  rs.Open();
  CHECK(rs.m_SCORE == 10);
  rs.Close();
}

static void TestWorkspace() {
  CDaoWorkspace ws;
  ws.Create(_T("Second"), _T("admin"), _T(""));
  ws.Append();
  CHECK(ws.IsOpen());
  CHECK(ws.GetName() == _T("Second"));
  CHECK(ws.GetUserName() == _T("admin"));
  CHECK(ws.GetWorkspaceCount() >= 2);
  CDaoWorkspaceInfo wi;
  ws.GetWorkspaceInfo(0, wi, AFX_DAO_ALL_INFO);
  CHECK(wi.m_strName == _T("#Default Workspace#"));

  CDaoDatabase db(&ws);
  db.Open(kFile);
  CHECK(db.m_pWorkspace == &ws);
  CHECK(ws.GetDatabaseCount() == 1);
  CDaoDatabaseInfo di;
  ws.GetDatabaseInfo(0, di, AFX_DAO_ALL_INFO);
  CHECK(di.m_strName == kFile);
  CHECK(di.m_bUpdatable);
  db.Close();
  ws.Close();
  CHECK(!ws.IsOpen());

  // The default workspace, opened by name.
  CDaoWorkspace def;
  def.Open();
  CHECK(def.GetName() == _T("#Default Workspace#"));
  def.Close();
}

static void TestErrors(CDaoDatabase& db) {
  long code;
  CDaoDatabase none;
  DAO_ERROR(none.Open(_T("C:\\missing.mdb")), code);
  CHECK(code == 3024);

  DAO_ERROR(db.Execute(_T("SELECT FROM WHERE")), code);
  CHECK(code != 0);

  CPeopleSet rs(&db);
  rs.m_strFilter = _T("[NOSUCH] = 1");
  DAO_ERROR(rs.Open(), code);
  CHECK(code != 0);
  CHECK(!rs.IsOpen());

  // The exception's error collection and message.
  try {
    db.Execute(_T("DELETE FROM [nosuch]"));
    CHECK(false);
  } catch (CDaoException* e) {
    CHECK(e->GetErrorCount() == 1);
    e->GetErrorInfo(0);
    CHECK(e->m_pErrorInfo->m_lErrorCode == 3078);
    CHECK(e->m_scode == MAKE_HRESULT(SEVERITY_ERROR, FACILITY_CONTROL, 3078));
    TCHAR sz[256];
    CHECK(e->GetErrorMessage(sz, 256));
    CHECK(_tcsstr(sz, _T("nosuch")) != NULL);
    e->Delete();
  }

  // Using a recordset that is not open is the library's own error.
  CPeopleSet closed(&db);
  try {
    closed.MoveNext();
    CHECK(false);
  } catch (CDaoException* e) {
    CHECK(e->m_nAfxDaoError == AFX_DAO_ERROR_OBJECT_NOT_OPEN);
    e->Delete();
  }

  // Deleting a table.
  CDaoTableDef td(&db);
  td.Create(_T("scratch"));
  td.CreateField(_T("X"), dbLong, 4);
  td.Append();
  td.Close();
  CHECK(db.GetTableDefCount() == 2);
  db.DeleteTableDef(_T("scratch"));
  CHECK(db.GetTableDefCount() == 1);
}

static void TestDefaultDatabase() {
  // A recordset with no database opens the one GetDefaultDBName names.
  CPeopleSet rs;
  rs.Open();
  CHECK(rs.m_pDatabase != NULL && rs.m_pDatabase->IsOpen());
  CHECK(rs.GetRecordCount() >= 0);
  CHECK(!rs.IsEOF());
  rs.Close();
}

int main() {
  ::CoInitialize(NULL);
  daomock::Register();
  try {
    g_stage = "TestEngine";
    TestEngine();
    {
      CDaoDatabase db;
      g_stage = "TestCreate";
      TestCreate(db);
      g_stage = "TestBrowse";
      TestBrowse(db);
      g_stage = "TestParameters";
      TestParameters(db);
      g_stage = "TestUpdate";
      TestUpdate(db);
      g_stage = "TestTableType";
      TestTableType(db);
      g_stage = "TestQueryDefs";
      TestQueryDefs(db);
      g_stage = "TestTransactions";
      TestTransactions(db);
      g_stage = "TestErrors";
      TestErrors(db);
      db.Close();
      CHECK(!db.IsOpen());
    }
    g_stage = "TestWorkspace";
    TestWorkspace();
    g_stage = "TestDefaultDatabase";
    TestDefaultDatabase();
  } catch (CDaoException* e) {
    TCHAR sz[512];
    e->GetErrorMessage(sz, 512);
    printf("FAIL unexpected DAO exception in %s: ", g_stage);
    _tprintf(_T("%s\n"), sz);
    e->Delete();
    g_failures++;
  } catch (CException* e) {
    printf("FAIL unexpected exception in %s\n", g_stage);
    e->Delete();
    g_failures++;
  }
  AfxDaoTerm();
  // The engine was made through the licence key, and is gone.
  CHECK(daomock::G().licensed == 1);
  CHECK(daomock::G().engines == 0);
  printf("%s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
  return g_failures ? 1 : 0;
}
