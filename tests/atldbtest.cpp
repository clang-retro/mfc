// atldbtest.cpp - the OLE DB provider templates (atldb.h): a provider made
// of ATL classes, registered with CoRegisterClassObject, read and changed
// through the consumer templates (atldbcli.h):
//   - the data source's properties (DBINIT before Initialize, the others
//     after, the descriptions of GetPropertyInfo, a value refused by
//     IsValidValue) and its count of sessions;
//   - CTable over the People table: moving both ways, the values in an
//     accessor of other types than the rows' (text to a number and back),
//     lengths and statuses, a null from GetDBStatus;
//   - CCommand with CDynamicAccessor: the column information, a command the
//     rowset refuses;
//   - changing, inserting and deleting rows (IRowsetChangeImpl), written back
//     by FlushData, and the UPDATABILITY that refuses them;
//   - a schema rowset (DBSCHEMA_TABLES) with a restriction.
// Exits with 1 on a failure.
#include <atlbase.h>
#include <atlcom.h>
#include <atlstr.h>
#include <atldb.h>
#include <stdio.h>
#include <vector>

CComModule _Module;

static int g_nFailures;
#define CHECK(cond)                                           \
  do {                                                        \
    if (!(cond)) {                                            \
      printf("FAIL line %d: %s\n", __LINE__, #cond);          \
      g_nFailures++;                                          \
    }                                                         \
  } while (0)

// {4A3C2E8B-5D1F-4F67-9C2A-8B7E6D5C4B3A}
static const CLSID CLSID_PeopleProvider = {
    0x4a3c2e8b, 0x5d1f, 0x4f67, {0x9c, 0x2a, 0x8b, 0x7e, 0x6d, 0x5c, 0x4b, 0x3a}};

// ---------------------------------------------------------------------
// The provider: the People table, kept here.

class CPerson {
public:
  LONG m_nID;
  WCHAR m_szName[32];
  double m_dScore;
  char m_szCity[16];

  BEGIN_PROVIDER_COLUMN_MAP(CPerson)
    PROVIDER_COLUMN_ENTRY("ID", 1, m_nID)
    PROVIDER_COLUMN_ENTRY("Name", 2, m_szName)
    PROVIDER_COLUMN_ENTRY("Score", 3, m_dScore)
    PROVIDER_COLUMN_ENTRY_STR("City", 4, m_szCity)
  END_PROVIDER_COLUMN_MAP()
};
static std::vector<CPerson> g_people;
static int g_nFlushes;

static void ResetPeople() {
  static const struct {
    LONG id;
    const WCHAR* name;
    double score;
    const char* city;
  } kRows[] = {{1, L"Ada", 91.5, "London"},
               {2, L"Brian", 78.0, "Toronto"},
               {3, L"Grace", 88.25, "Arlington"},
               {4, L"Linus", -1, ""}};  // a score of -1 is no score
  g_people.clear();
  for (const auto& r : kRows) {
    CPerson p;
    memset(&p, 0, sizeof(p));
    p.m_nID = r.id;
    lstrcpyW(p.m_szName, r.name);
    p.m_dScore = r.score;
    lstrcpyA(p.m_szCity, r.city);
    g_people.push_back(p);
  }
}

class CPeopleCommand;
class CPeopleSession;

class ATL_NO_VTABLE CPeopleCommand : public CComObjectRootEx<CComSingleThreadModel>,
                                     public IAccessorImpl<CPeopleCommand>,
                                     public ICommandTextImpl<CPeopleCommand>,
                                     public ICommandPropertiesImpl<CPeopleCommand>,
                                     public IObjectWithSiteImpl<CPeopleCommand>,
                                     public IConvertTypeImpl<CPeopleCommand>,
                                     public IColumnsInfoImpl<CPeopleCommand>,
                                     public IInternalCommandConnectionImpl<CPeopleCommand> {
public:
  BEGIN_COM_MAP(CPeopleCommand)
    COM_INTERFACE_ENTRY(ICommand)
    COM_INTERFACE_ENTRY(IObjectWithSite)
    COM_INTERFACE_ENTRY(IAccessor)
    COM_INTERFACE_ENTRY(ICommandProperties)
    COM_INTERFACE_ENTRY2(ICommandText, ICommand)
    COM_INTERFACE_ENTRY(IColumnsInfo)
    COM_INTERFACE_ENTRY(IConvertType)
    COM_INTERFACE_ENTRY(IInternalConnection)
  END_COM_MAP()

  HRESULT FinalConstruct() {
    HRESULT hr = IConvertTypeImpl<CPeopleCommand>::FinalConstruct();
    if (FAILED(hr)) return hr;
    hr = IAccessorImpl<CPeopleCommand>::FinalConstruct();
    if (FAILED(hr)) return hr;
    return CUtlProps<CPeopleCommand>::FInit();
  }
  void FinalRelease() { IAccessorImpl<CPeopleCommand>::FinalRelease(); }

  STDMETHOD(Execute)(IUnknown* pUnkOuter, REFIID riid, DBPARAMS* pParams,
                     DBROWCOUNT* pcRowsAffected, IUnknown** ppRowset) override;
  static ATLCOLUMNINFO* GetColumnInfo(CPeopleCommand* pv, DBORDINAL* pcInfo) {
    return CPerson::GetColumnInfo(pv, pcInfo);
  }

  BEGIN_PROPSET_MAP(CPeopleCommand)
    BEGIN_PROPERTY_SET(DBPROPSET_ROWSET)
      PROPERTY_INFO_ENTRY(IAccessor)
      PROPERTY_INFO_ENTRY(IColumnsInfo)
      PROPERTY_INFO_ENTRY(IConvertType)
      PROPERTY_INFO_ENTRY(IRowset)
      PROPERTY_INFO_ENTRY(IRowsetIdentity)
      PROPERTY_INFO_ENTRY(IRowsetInfo)
      PROPERTY_INFO_ENTRY_EX(IRowsetChange, VT_BOOL,
                             DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, VARIANT_FALSE, 0)
      PROPERTY_INFO_ENTRY_EX(UPDATABILITY, VT_I4,
                             DBPROPFLAGS_ROWSET | DBPROPFLAGS_READ | DBPROPFLAGS_WRITE, 0, 0)
      PROPERTY_INFO_ENTRY(BOOKMARKS)
      PROPERTY_INFO_ENTRY(CANFETCHBACKWARDS)
      PROPERTY_INFO_ENTRY(CANHOLDROWS)
      PROPERTY_INFO_ENTRY(CANSCROLLBACKWARDS)
      PROPERTY_INFO_ENTRY(OWNINSERT)
      PROPERTY_INFO_ENTRY(REMOVEDELETED)
    END_PROPERTY_SET(DBPROPSET_ROWSET)
  END_PROPSET_MAP()
};

class CPeopleRowset : public CRowsetImpl<CPeopleRowset, CPerson, CPeopleCommand>,
                      public IRowsetChangeImpl<CPeopleRowset, CPerson> {
public:
  typedef CRowsetImpl<CPeopleRowset, CPerson, CPeopleCommand> _Base;
  BEGIN_COM_MAP(CPeopleRowset)
    COM_INTERFACE_ENTRY(IRowsetChange)
    COM_INTERFACE_ENTRY_CHAIN(_Base)
  END_COM_MAP()

  HRESULT Execute(DBPARAMS* /*pParams*/, DBROWCOUNT* pcRowsAffected) {
    CStringW strCommand((BSTR)this->m_strCommandText);
    if (strCommand.CompareNoCase(L"People") != 0 && strCommand.CompareNoCase(L"select * from People") != 0)
      return DB_E_ERRORSINCOMMAND;
    for (size_t i = 0; i < g_people.size(); i++) this->m_rgRowData.Add(g_people[i]);
    if (pcRowsAffected != NULL) *pcRowsAffected = (DBROWCOUNT)g_people.size();
    return S_OK;
  }
  // No score is a null.
  DBSTATUS GetDBStatus(CSimpleRow* pRow, ATLCOLUMNINFO* pCol) {
    if (pCol->iOrdinal == 3 && this->m_rgRowData[pRow->m_iRowset].m_dScore < 0) return DBSTATUS_S_ISNULL;
    return DBSTATUS_S_OK;
  }
  HRESULT SetDBStatus(DBSTATUS* pStatus, CSimpleRow* pRow, ATLCOLUMNINFO* pCol) {
    // A null score is stored as -1 (the default for the others is zero).
    if (*pStatus == DBSTATUS_S_DEFAULT) return S_OK;
    if (*pStatus != DBSTATUS_S_ISNULL || pCol->iOrdinal != 3) return E_FAIL;
    if (pRow != NULL) this->m_rgRowData[pRow->m_iRowset].m_dScore = -1;
    return S_OK;
  }
  HRESULT FlushData(HROW, HACCESSOR) {
    g_nFlushes++;
    g_people.assign(&this->m_rgRowData[0], &this->m_rgRowData[0] + this->m_rgRowData.GetCount());
    return S_OK;
  }
};

STDMETHODIMP CPeopleCommand::Execute(IUnknown* pUnkOuter, REFIID riid, DBPARAMS* pParams,
                                     DBROWCOUNT* pcRowsAffected, IUnknown** ppRowset) {
  CPeopleRowset* pRowset;
  return CreateRowset(pUnkOuter, riid, pParams, pcRowsAffected, ppRowset, pRowset);
}

class CPeopleTables;

class ATL_NO_VTABLE CPeopleSession : public CComObjectRootEx<CComSingleThreadModel>,
                                     public IGetDataSourceImpl<CPeopleSession>,
                                     public IOpenRowsetImpl<CPeopleSession>,
                                     public ISessionPropertiesImpl<CPeopleSession>,
                                     public IObjectWithSiteSessionImpl<CPeopleSession>,
                                     public IDBSchemaRowsetImpl<CPeopleSession>,
                                     public IDBCreateCommandImpl<CPeopleSession, CPeopleCommand> {
public:
  HRESULT FinalConstruct() { return FInit(); }

  STDMETHOD(OpenRowset)(IUnknown* pUnkOuter, DBID* pTableID, DBID* pIndexID, REFIID riid,
                        ULONG cPropertySets, DBPROPSET rgPropertySets[], IUnknown** ppRowset) override {
    CPeopleRowset* pRowset;
    return CreateRowset(pUnkOuter, pTableID, pIndexID, riid, cPropertySets, rgPropertySets,
                        ppRowset, pRowset);
  }
  void SetRestrictions(ULONG cRestrictions, GUID* /*rguidSchema*/, ULONG* rgRestrictions) {
    for (ULONG i = 0; i < cRestrictions; i++) rgRestrictions[i] = 0x0c;  // TABLE_NAME, TABLE_TYPE
  }

  BEGIN_PROPSET_MAP(CPeopleSession)
    BEGIN_PROPERTY_SET(DBPROPSET_SESSION)
      PROPERTY_INFO_ENTRY(SESS_AUTOCOMMITISOLEVELS)
    END_PROPERTY_SET(DBPROPSET_SESSION)
  END_PROPSET_MAP()

  BEGIN_COM_MAP(CPeopleSession)
    COM_INTERFACE_ENTRY(IGetDataSource)
    COM_INTERFACE_ENTRY(IOpenRowset)
    COM_INTERFACE_ENTRY(ISessionProperties)
    COM_INTERFACE_ENTRY(IObjectWithSite)
    COM_INTERFACE_ENTRY(IDBCreateCommand)
    COM_INTERFACE_ENTRY(IDBSchemaRowset)
  END_COM_MAP()

  BEGIN_SCHEMA_MAP(CPeopleSession)
    SCHEMA_ENTRY(DBSCHEMA_TABLES, CPeopleTables)
  END_SCHEMA_MAP()
};

// The tables, for DBSCHEMA_TABLES: People, and a view no one can open.
class CPeopleTables : public CSchemaRowsetImpl<CPeopleTables, CTABLESRow, CPeopleSession> {
public:
  HRESULT Execute(DBROWCOUNT* pcRowsAffected, ULONG cRestrictions, const VARIANT* rgRestrictions) {
    static const WCHAR* const kTables[][2] = {{L"People", L"TABLE"}, {L"Nobody", L"VIEW"}};
    for (const auto& t : kTables) {
      if (!this->_Matches(cRestrictions, rgRestrictions, 2, t[0])) continue;
      if (!this->_Matches(cRestrictions, rgRestrictions, 3, t[1])) continue;
      CTABLESRow row;
      lstrcpyW(row.m_szTable, t[0]);
      lstrcpyW(row.m_szType, t[1]);
      lstrcpyW(row.m_szDesc, L"made by atldbtest");
      this->m_rgRowData.Add(row);
    }
    if (pcRowsAffected != NULL) *pcRowsAffected = (DBROWCOUNT)this->m_rgRowData.GetCount();
    return S_OK;
  }
};

class ATL_NO_VTABLE CPeopleSource : public CComObjectRootEx<CComSingleThreadModel>,
                                    public CComCoClass<CPeopleSource, &CLSID_PeopleProvider>,
                                    public IDBCreateSessionImpl<CPeopleSource, CPeopleSession>,
                                    public IDBInitializeImpl<CPeopleSource>,
                                    public IDBPropertiesImpl<CPeopleSource>,
                                    public IPersistImpl<CPeopleSource>,
                                    public IInternalConnectionImpl<CPeopleSource> {
public:
  HRESULT FinalConstruct() { return FInit(); }
  // Only the People data source.
  HRESULT IsValidValue(ULONG /*iCurSet*/, DBPROP* pDBProp) override {
    if (pDBProp->dwPropertyID == DBPROP_INIT_DATASOURCE && pDBProp->vValue.vt == VT_BSTR &&
        pDBProp->vValue.bstrVal != NULL && lstrcmpiW(pDBProp->vValue.bstrVal, L"people") != 0)
      return E_FAIL;
    return S_OK;
  }

  BEGIN_PROPSET_MAP(CPeopleSource)
    BEGIN_PROPERTY_SET(DBPROPSET_DATASOURCEINFO)
      PROPERTY_INFO_ENTRY(ACTIVESESSIONS)
      PROPERTY_INFO_ENTRY(DATASOURCEREADONLY)
      PROPERTY_INFO_ENTRY(PROVIDEROLEDBVER)
      PROPERTY_INFO_ENTRY_VALUE(PROVIDERFRIENDLYNAME, OLESTR("People of atldbtest"))
      PROPERTY_INFO_ENTRY(DSOTHREADMODEL)
    END_PROPERTY_SET(DBPROPSET_DATASOURCEINFO)
    BEGIN_PROPERTY_SET(DBPROPSET_DBINIT)
      PROPERTY_INFO_ENTRY(AUTH_PASSWORD)
      PROPERTY_INFO_ENTRY(AUTH_USERID)
      PROPERTY_INFO_ENTRY(INIT_DATASOURCE)
      PROPERTY_INFO_ENTRY_VALUE(INIT_PROMPT, DBPROMPT_NOPROMPT)
      PROPERTY_INFO_ENTRY(INIT_TIMEOUT)
    END_PROPERTY_SET(DBPROPSET_DBINIT)
    CHAIN_PROPERTY_SET(CPeopleCommand)
  END_PROPSET_MAP()

  BEGIN_COM_MAP(CPeopleSource)
    COM_INTERFACE_ENTRY(IDBCreateSession)
    COM_INTERFACE_ENTRY(IDBInitialize)
    COM_INTERFACE_ENTRY(IDBProperties)
    COM_INTERFACE_ENTRY(IPersist)
    COM_INTERFACE_ENTRY(IInternalConnection)
  END_COM_MAP()
};

// ---------------------------------------------------------------------
// The consumer.

class CPersonAccessor {
public:
  LONG m_nID;
  TCHAR m_szName[32];
  CHAR m_szScore[16];  // the score as text
  DBSTATUS m_dwScoreStatus;
  DBLENGTH m_cbScore;
  WCHAR m_szCity[16];
  DBLENGTH m_cbCity;

  BEGIN_COLUMN_MAP(CPersonAccessor)
    COLUMN_ENTRY(1, m_nID)
    COLUMN_ENTRY(2, m_szName)
    COLUMN_ENTRY_LENGTH_STATUS(3, m_szScore, m_cbScore, m_dwScoreStatus)
    COLUMN_ENTRY_LENGTH(4, m_szCity, m_cbCity)
  END_COLUMN_MAP()
};

// What changes rows: ID, name and score.
class CPersonChange {
public:
  LONG m_nID;
  WCHAR m_szName[32];
  double m_dScore;
  DBSTATUS m_dwScoreStatus;

  BEGIN_COLUMN_MAP(CPersonChange)
    COLUMN_ENTRY(1, m_nID)
    COLUMN_ENTRY(2, m_szName)
    COLUMN_ENTRY_STATUS(3, m_dScore, m_dwScoreStatus)
  END_COLUMN_MAP()
};

static bool Eq(LPCWSTR a, LPCWSTR b) { return a != NULL && b != NULL && wcscmp(a, b) == 0; }

static void TestDataSource() {
  // Before Initialize: DBINIT only.
  CComPtr<IDBInitialize> spInit;
  CHECK(SUCCEEDED(::CoCreateInstance(CLSID_PeopleProvider, NULL, CLSCTX_INPROC_SERVER,
                                     IID_IDBInitialize, (void**)&spInit)));
  if (spInit == NULL) return;
  CComQIPtr<IDBProperties> spProps(spInit);
  CHECK(spProps != NULL);
  CDBPropIDSet ids(DBPROPSET_DATASOURCEINFO);
  ids.AddPropertyID(DBPROP_PROVIDERFRIENDLYNAME);
  ULONG cSets = 0;
  DBPROPSET* pSets = NULL;
  CHECK(spProps->GetProperties(1, &ids, &cSets, &pSets) == DB_E_ERRORSOCCURRED);
  CHECK(cSets == 1 && pSets[0].rgProperties[0].dwStatus == DBPROPSTATUS_NOTSUPPORTED);
  for (ULONG i = 0; i < cSets; i++) ::CoTaskMemFree(pSets[i].rgProperties);
  ::CoTaskMemFree(pSets);

  // A data source the provider does not have: refused.
  CDBPropSet bad(DBPROPSET_DBINIT);
  bad.AddProperty(DBPROP_INIT_DATASOURCE, OLESTR("elsewhere"));
  CHECK(spProps->SetProperties(1, &bad) == DB_E_ERRORSOCCURRED);
  CHECK(bad.rgProperties[0].dwStatus == DBPROPSTATUS_BADVALUE);
  // An optional one it does not know: set aside.
  CDBPropSet opt(DBPROPSET_DBINIT);
  opt.AddProperty(DBPROP_INIT_DATASOURCE, OLESTR("People"));
  opt.AddProperty(DBPROP_INIT_LCID, (long)1033);
  opt.rgProperties[1].dwOptions = DBPROPOPTIONS_OPTIONAL;
  CHECK(spProps->SetProperties(1, &opt) == DB_S_ERRORSOCCURRED);
  CHECK(opt.rgProperties[0].dwStatus == DBPROPSTATUS_OK &&
        opt.rgProperties[1].dwStatus == DBPROPSTATUS_NOTSUPPORTED);

  // The descriptions, the chained rowset properties among them.
  DBPROPIDSET all = {NULL, 0, DBPROPSET_ROWSETALL};
  ULONG cInfo = 0;
  DBPROPINFOSET* pInfo = NULL;
  OLECHAR* pDesc = NULL;
  CHECK(spProps->GetPropertyInfo(1, &all, &cInfo, &pInfo, &pDesc) == S_OK);
  bool bFound = false;
  if (cInfo == 1)
    for (ULONG i = 0; i < pInfo[0].cPropertyInfos; i++)
      if (pInfo[0].rgPropertyInfos[i].dwPropertyID == DBPROP_CANHOLDROWS)
        bFound = Eq(pInfo[0].rgPropertyInfos[i].pwszDescription, L"CANHOLDROWS") &&
                 pInfo[0].rgPropertyInfos[i].vtType == VT_BOOL;
  CHECK(bFound);
  if (pInfo != NULL) ::CoTaskMemFree(pInfo[0].rgPropertyInfos);
  ::CoTaskMemFree(pInfo);
  ::CoTaskMemFree(pDesc);

  // After: the information, and DBINIT fixed.
  CHECK(spInit->Initialize() == S_OK);
  CHECK(spInit->Initialize() == DB_E_ALREADYINITIALIZED);
  CHECK(spProps->GetProperties(1, &ids, &cSets, &pSets) == S_OK);
  CHECK(cSets == 1 && pSets[0].rgProperties[0].vValue.vt == VT_BSTR &&
        Eq(pSets[0].rgProperties[0].vValue.bstrVal, L"People of atldbtest"));
  for (ULONG i = 0; i < cSets; i++) {
    ::VariantClear(&pSets[i].rgProperties[0].vValue);
    ::CoTaskMemFree(pSets[i].rgProperties);
  }
  ::CoTaskMemFree(pSets);
  CDBPropSet later(DBPROPSET_DBINIT);
  later.AddProperty(DBPROP_INIT_TIMEOUT, (long)5);
  CHECK(spProps->SetProperties(1, &later) == DB_E_ERRORSOCCURRED &&
        later.rgProperties[0].dwStatus == DBPROPSTATUS_NOTSETTABLE);

  // A session holds it open.
  CComQIPtr<IDBCreateSession> spCreate(spInit);
  CComPtr<IOpenRowset> spSession;
  CHECK(spCreate->CreateSession(NULL, IID_IOpenRowset, (IUnknown**)&spSession) == S_OK);
  CHECK(spInit->Uninitialize() == DB_E_OBJECTOPEN);
  CComPtr<IGetDataSource> spGet;
  CHECK(spSession != NULL && SUCCEEDED(spSession->QueryInterface(IID_IGetDataSource, (void**)&spGet)));
  CComPtr<IDBInitialize> spBack;
  CHECK(spGet != NULL && spGet->GetDataSource(IID_IDBInitialize, (IUnknown**)&spBack) == S_OK &&
        spBack.p == spInit.p);
  spBack.Release();
  spGet.Release();
  spSession.Release();
  CHECK(spInit->Uninitialize() == S_OK);
  CComPtr<IOpenRowset> spNo;
  CHECK(spCreate->CreateSession(NULL, IID_IOpenRowset, (IUnknown**)&spNo) == E_UNEXPECTED);
  CComQIPtr<IPersist> spPersist(spInit);
  CLSID clsid = GUID_NULL;
  CHECK(spPersist != NULL && spPersist->GetClassID(&clsid) == S_OK &&
        InlineIsEqualGUID(clsid, CLSID_PeopleProvider));
}

static void TestTable(CSession& session) {
  CTable<CAccessor<CPersonAccessor> > t;
  CHECK(t.Open(session, _T("People")) == S_OK);
  CHECK(t.MoveFirst() == S_OK);
  CHECK(t.m_nID == 1 && lstrcmp(t.m_szName, _T("Ada")) == 0);
  CHECK(lstrcmpA(t.m_szScore, "91.5") == 0 && t.m_dwScoreStatus == DBSTATUS_S_OK && t.m_cbScore == 4);
  CHECK(Eq(t.m_szCity, L"London") && t.m_cbCity == 12);
  int n = 1;
  while (t.MoveNext() == S_OK) n++;
  CHECK(n == 4);
  CHECK(t.MoveLast() == S_OK && t.m_nID == 4);
  CHECK(t.m_dwScoreStatus == DBSTATUS_S_ISNULL && t.m_cbCity == 0);  // no score
  CHECK(t.MovePrev() == S_OK && t.m_nID == 3 && lstrcmpA(t.m_szScore, "88.25") == 0);
  CHECK(t.MovePrev() == S_OK && t.m_nID == 2);
  CHECK(t.MoveNext() == S_OK && t.m_nID == 3);
  CHECK(t.MoveFirst() == S_OK && t.m_nID == 1);
  CHECK(t.MovePrev() == DB_S_ENDOFROWSET);

  // The same row twice: the same handle.
  CComQIPtr<IRowsetIdentity> spId(t.m_spRowset);
  CHECK(spId != NULL);
  HROW rows[2] = {NULL, NULL};
  HROW* pRows = rows;
  DBCOUNTITEM c = 0;
  t.m_spRowset->RestartPosition(DB_NULL_HCHAPTER);
  CHECK(t.m_spRowset->GetNextRows(DB_NULL_HCHAPTER, 0, 1, &c, &pRows) == S_OK && c == 1);
  t.m_spRowset->RestartPosition(DB_NULL_HCHAPTER);
  pRows = rows + 1;
  CHECK(t.m_spRowset->GetNextRows(DB_NULL_HCHAPTER, 0, 1, &c, &pRows) == S_OK && c == 1);
  CHECK(spId != NULL && spId->IsSameRow(rows[0], rows[1]) == S_OK);
  DBREFCOUNT refs[2];
  CHECK(t.m_spRowset->ReleaseRows(2, rows, NULL, refs, NULL) == S_OK && refs[0] == 1 && refs[1] == 0);
  CHECK(t.m_spRowset->ReleaseRows(1, rows, NULL, NULL, NULL) == DB_E_ERRORSOCCURRED);

  // Read only, unless asked: no changes through IRowsetChange.
  CComQIPtr<IRowsetInfo> spInfo(t.m_spRowset);
  CHECK(spInfo != NULL);
  CComPtr<ICommand> spSpec;
  CHECK(spInfo->GetSpecification(IID_ICommand, (IUnknown**)&spSpec) == E_NOINTERFACE);
  t.Close();
}

static void TestCommand(CSession& session) {
  CCommand<CDynamicAccessor> cmd;
  CHECK(cmd.Open(session, L"select * from People") == S_OK);
  CHECK(cmd.GetColumnCount() == 4);
  CHECK(Eq(cmd.GetColumnName(2), L"Name") && Eq(cmd.GetColumnName(4), L"City"));
  DBTYPE type = 0;
  CHECK(cmd.GetColumnType(3, &type) && type == DBTYPE_R8);
  CHECK(cmd.MoveFirst() == S_OK);
  LONG id = 0;
  double score = 0;
  CHECK(cmd.GetValue(1, &id) && id == 1);
  CHECK(cmd.GetValue(3, &score) && score == 91.5);
  CHECK(Eq((LPCWSTR)cmd.GetValue(2), L"Ada"));
  CHECK(lstrcmpA((LPCSTR)cmd.GetValue(4), "London") == 0);
  // The rowset's properties are the command's.
  CComQIPtr<IRowsetInfo> spInfo(cmd.m_spRowset);
  CDBPropIDSet ids(DBPROPSET_ROWSET);
  ids.AddPropertyID(DBPROP_CANHOLDROWS);
  ids.AddPropertyID(DBPROP_IRowsetLocate);
  ULONG cSets = 0;
  DBPROPSET* pSets = NULL;
  CHECK(spInfo != NULL && spInfo->GetProperties(1, &ids, &cSets, &pSets) == DB_S_ERRORSOCCURRED);
  CHECK(cSets == 1 && pSets[0].rgProperties[0].vValue.boolVal == VARIANT_TRUE &&
        pSets[0].rgProperties[1].dwStatus == DBPROPSTATUS_NOTSUPPORTED);
  ::CoTaskMemFree(pSets[0].rgProperties);
  ::CoTaskMemFree(pSets);
  CComPtr<ICommand> spSpec;
  CHECK(spInfo->GetSpecification(IID_ICommand, (IUnknown**)&spSpec) == S_OK && spSpec != NULL);
  spSpec.Release();
  cmd.Close();

  CCommand<CDynamicAccessor> bad;
  CHECK(bad.Open(session, L"select * from Nowhere") == DB_E_ERRORSINCOMMAND);

  // ICommandText and IConvertType on the command itself.
  CComQIPtr<IDBCreateCommand> spCreate(session.m_spOpenRowset);
  CComPtr<ICommandText> spText;
  CHECK(spCreate != NULL && spCreate->CreateCommand(NULL, IID_ICommandText, (IUnknown**)&spText) == S_OK);
  LPOLESTR psz = NULL;
  CHECK(spText->GetCommandText(NULL, &psz) == DB_E_NOCOMMAND);
  CHECK(spText->SetCommandText(DBGUID_SQL, L"People") == S_OK);
  CHECK(spText->GetCommandText(NULL, &psz) == S_OK && Eq(psz, L"People"));
  ::CoTaskMemFree(psz);
  CComQIPtr<IConvertType> spConv(spText);
  CHECK(spConv->CanConvert(DBTYPE_I4, DBTYPE_WSTR, DBCONVERTFLAGS_COLUMN) == S_OK);
  CHECK(spConv->CanConvert(DBTYPE_GUID, DBTYPE_I4, DBCONVERTFLAGS_COLUMN) == S_FALSE);
  CComPtr<IRowset> spRowset;
  DBROWCOUNT nRows = 0;
  CHECK(spText->Execute(NULL, IID_IRowset, NULL, &nRows, (IUnknown**)&spRowset) == S_OK && nRows == 4);
}

static void TestChanges(CSession& session) {
  g_nFlushes = 0;
  CDBPropSet props(DBPROPSET_ROWSET);
  props.AddProperty(DBPROP_IRowsetChange, true);
  props.AddProperty(DBPROP_UPDATABILITY,
                    (long)(DBPROPVAL_UP_CHANGE | DBPROPVAL_UP_INSERT | DBPROPVAL_UP_DELETE));
  CTable<CAccessor<CPersonChange> > t;
  CHECK(t.Open(session, _T("People"), &props) == S_OK);
  CHECK(t.m_spRowsetChange != NULL);
  // A change.
  CHECK(t.MoveFirst() == S_OK && t.m_nID == 1);
  lstrcpyW(t.m_szName, L"Ada Lovelace");
  t.m_dScore = 99;
  t.m_dwScoreStatus = DBSTATUS_S_OK;
  CHECK(t.SetData() == S_OK);
  CHECK(g_nFlushes == 1 && Eq(g_people[0].m_szName, L"Ada Lovelace") && g_people[0].m_dScore == 99);
  // A name too long for the row: refused, the row as it was.
  WCHAR szLong[40];
  for (int i = 0; i < 39; i++) szLong[i] = L'x';
  szLong[39] = 0;
  CComQIPtr<IAccessor> spAcc(t.m_spRowset);
  DBBINDING b = {};
  b.iOrdinal = 2;
  b.obValue = 0;
  b.dwPart = DBPART_VALUE;
  b.wType = DBTYPE_WSTR;
  b.cbMaxLen = sizeof(szLong);
  HACCESSOR h = DB_NULL_HACCESSOR;
  CHECK(spAcc->CreateAccessor(DBACCESSOR_ROWDATA, 1, &b, 0, &h, NULL) == S_OK);
  CHECK(t.m_spRowsetChange->SetData(t.m_hRow, h, szLong) == DB_E_ERRORSOCCURRED);
  spAcc->ReleaseAccessor(h, NULL);
  CHECK(Eq(g_people[0].m_szName, L"Ada Lovelace"));
  // A bad ordinal for an accessor.
  b.iOrdinal = 9;
  DBBINDSTATUS st = 0;
  CHECK(spAcc->CreateAccessor(DBACCESSOR_ROWDATA, 1, &b, 0, &h, &st) == DB_E_ERRORSOCCURRED &&
        st == DBBINDSTATUS_BADORDINAL);
  // A null score, which the provider keeps as -1.
  t.m_dwScoreStatus = DBSTATUS_S_ISNULL;
  CHECK(t.SetData() == S_OK && g_people[0].m_dScore == -1);
  // An insert, at the end.
  t.m_nID = 5;
  lstrcpyW(t.m_szName, L"Margaret");
  t.m_dScore = 95;
  t.m_dwScoreStatus = DBSTATUS_S_OK;
  CHECK(t.Insert() == S_OK);
  CHECK(g_people.size() == 5 && g_people[4].m_nID == 5 && Eq(g_people[4].m_szName, L"Margaret"));
  CHECK(g_people[4].m_szCity[0] == 0);
  // A delete: the second row.
  CHECK(t.MoveNext() == S_OK && t.m_nID == 2);
  CHECK(t.Delete() == S_OK);
  CHECK(g_people.size() == 4 && g_people[1].m_nID == 3);
  CHECK(t.GetData() == DB_E_DELETEDROW);
  CHECK(t.MoveNext() == S_OK && t.m_nID == 3);  // the next one moved up
  t.Close();

  // Without UPDATABILITY's changes, none.
  CDBPropSet ro(DBPROPSET_ROWSET);
  ro.AddProperty(DBPROP_IRowsetChange, true);
  CTable<CAccessor<CPersonChange> > t2;
  CHECK(t2.Open(session, _T("People"), &ro) == S_OK);
  CHECK(t2.MoveFirst() == S_OK);
  CHECK(t2.SetData() == DB_E_NOTSUPPORTED && t2.Delete() == DB_E_NOTSUPPORTED);
  t2.Close();

  // A rowset property the provider will not set, required: no rowset.
  CDBPropSet locate(DBPROPSET_ROWSET);
  locate.AddProperty(DBPROP_BOOKMARKS, true);
  CTable<CAccessor<CPersonChange> > t3;
  CHECK(t3.Open(session, _T("People"), &locate) == DB_E_ERRORSOCCURRED);
}

// Reads DBSCHEMA_TABLES' names.
static std::vector<CStringW> TableNames(CSession& session, LPCWSTR pszType) {
  std::vector<CStringW> names;
  CComQIPtr<IDBSchemaRowset> spSchema(session.m_spOpenRowset);
  if (spSchema == NULL) return names;
  VARIANT restr[4];
  for (int i = 0; i < 4; i++) ::VariantInit(&restr[i]);
  CComBSTR bstrType(pszType);
  if (pszType != NULL) {
    restr[3].vt = VT_BSTR;
    restr[3].bstrVal = bstrType;
  }
  CAccessorRowset<CDynamicAccessor> rs;
  if (FAILED(spSchema->GetRowset(NULL, DBSCHEMA_TABLES, 4, restr, IID_IRowset, 0, NULL,
                                 (IUnknown**)&rs.m_spRowset)))
    return names;
  if (FAILED(rs.Bind())) return names;
  for (HRESULT hr = rs.MoveFirst(); hr == S_OK; hr = rs.MoveNext())
    names.push_back((LPCWSTR)rs.GetValue(3));
  return names;
}

static void TestSchema(CSession& session) {
  CComQIPtr<IDBSchemaRowset> spSchema(session.m_spOpenRowset);
  CHECK(spSchema != NULL);
  if (spSchema == NULL) return;
  ULONG c = 0;
  GUID* pGuids = NULL;
  ULONG* pRestr = NULL;
  CHECK(spSchema->GetSchemas(&c, &pGuids, &pRestr) == S_OK && c == 1 &&
        InlineIsEqualGUID(pGuids[0], DBSCHEMA_TABLES) && pRestr[0] == 0x0c);
  ::CoTaskMemFree(pGuids);
  ::CoTaskMemFree(pRestr);
  std::vector<CStringW> all = TableNames(session, NULL);
  CHECK(all.size() == 2 && all[0] == L"People" && all[1] == L"Nobody");
  std::vector<CStringW> views = TableNames(session, L"VIEW");
  CHECK(views.size() == 1 && views[0] == L"Nobody");
  CComPtr<IRowset> sp;
  CHECK(spSchema->GetRowset(NULL, DBSCHEMA_COLUMNS, 0, NULL, IID_IRowset, 0, NULL,
                            (IUnknown**)&sp) == E_INVALIDARG);
}

int main() {
  ::CoInitialize(NULL);
  ResetPeople();
  CComPtr<IClassFactory> spCF;
  CHECK(SUCCEEDED(CPeopleSource::_ClassFactoryCreatorClass::CreateInstance(
      (void*)&CPeopleSource::_CreatorClass::CreateInstance, IID_IClassFactory, (void**)&spCF)));
  DWORD dwReg = 0;
  CHECK(SUCCEEDED(::CoRegisterClassObject(CLSID_PeopleProvider, spCF, CLSCTX_INPROC_SERVER,
                                          REGCLS_MULTIPLEUSE, &dwReg)));
  TestDataSource();
  {
    CDataSource ds;
    CDBPropSet init(DBPROPSET_DBINIT);
    init.AddProperty(DBPROP_INIT_DATASOURCE, OLESTR("People"));
    CHECK(ds.Open(CLSID_PeopleProvider, &init) == S_OK);
    CComVariant v;
    CHECK(ds.GetProperty(DBPROPSET_DATASOURCEINFO, DBPROP_DATASOURCEREADONLY, &v) == S_OK &&
          v.vt == VT_BOOL && v.boolVal == VARIANT_TRUE);
    CSession session;
    CHECK(session.Open(ds) == S_OK);
    TestTable(session);
    TestCommand(session);
    TestChanges(session);
    TestSchema(session);
  }
  ::CoRevokeClassObject(dwReg);
  spCF.Release();
  CHECK(_Module.GetLockCount() == 0);
  ::CoUninitialize();
  if (g_nFailures == 0) printf("ALL PASSED\n");
  return g_nFailures != 0;
}
