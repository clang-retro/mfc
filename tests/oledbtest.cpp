// oledbtest.cpp - the OLE DB consumer templates (atldbcli.h) and
// COleDBRecordView (afxoledb.h), against a small OLE DB provider of the
// test's own, registered with CoRegisterClassObject: a People table (ID,
// Name, Score) that SELECT, DELETE and UPDATE commands read and change.
//   - CDataSource (by class ID, by ProgID, with a property set; a refused
//     password) and CSession, with a transaction aborted and one committed;
//   - CTable and CCommand with CAccessor (column maps, accessor maps, length
//     and status entries, parameter maps, DEFINE_COMMAND), CDynamicAccessor,
//     CDynamicStringAccessor, CManualAccessor and CNoAccessor/CNoRowset;
//   - moving both ways, changing, inserting and deleting rows;
//   - CDBErrorInfo reading the provider's error records;
//   - a COleDBRecordView in a frame, moved with the record commands, writing
//     the form back to the rows.
// At the end every provider object, row handle and accessor is released.
#include <afxwin.h>
#include <afxext.h>
#include <afxoledb.h>
#include <string>
#include <vector>
#include "oledbtest.h"
#include "testutil.h"

// ---------------------------------------------------------------------
// The provider's table

struct MockRow {
  LONG id;
  std::wstring name;
  double score;
  bool scoreNull;
  bool deleted;
};
static std::vector<MockRow> g_rows;
static std::vector<MockRow> g_saved;  // the table as a transaction began
static bool g_bInTransaction;
static std::vector<std::wstring> g_commands;  // what Execute ran
static LONG g_nObjects;                       // live provider objects
static LONG g_nHeldRows;                      // row handles not released
static LONG g_nAccessors;                     // accessors not released

static void ResetTable() {
  g_rows.clear();
  MockRow a = {1, L"Ada", 36.5, false, false};
  MockRow b = {2, L"Brian", 0, true, false};
  MockRow c = {3, L"Carol", 29, false, false};
  g_rows.push_back(a);
  g_rows.push_back(b);
  g_rows.push_back(c);
}

// {6B3F2A10-58C4-4E0B-9A51-3E7C0D1B2A44}
static const CLSID CLSID_MockProvider = {
    0x6b3f2a10, 0x58c4, 0x4e0b, {0x9a, 0x51, 0x3e, 0x7c, 0x0d, 0x1b, 0x2a, 0x44}};

struct MockColumn {
  const wchar_t* pszName;
  DBTYPE wType;
  DBLENGTH cbSize;
  DBCOLUMNFLAGS dwFlags;
};
static const MockColumn g_columns[] = {
    {L"ID", DBTYPE_I4, 4, DBCOLUMNFLAGS_WRITE | DBCOLUMNFLAGS_ISFIXEDLENGTH},
    {L"Name", DBTYPE_WSTR, 32, DBCOLUMNFLAGS_WRITE},
    {L"Score", DBTYPE_R8, 8,
     DBCOLUMNFLAGS_WRITE | DBCOLUMNFLAGS_ISFIXEDLENGTH | DBCOLUMNFLAGS_ISNULLABLE |
         DBCOLUMNFLAGS_MAYBENULL},
};
static const DBORDINAL kColumns = 3;

// A cell's value: null, a number or text.
struct MockValue {
  enum Kind { kNull, kInt, kReal, kText } kind;
  LONG i;
  double d;
  std::wstring s;
  MockValue() : kind(kNull), i(0), d(0) {}
  std::wstring Text() const {
    WCHAR sz[64];
    if (kind == kInt) {
      wsprintfW(sz, L"%ld", (long)i);
      return sz;
    }
    if (kind == kReal) {
      char szA[64];
      sprintf(szA, "%g", d);
      MultiByteToWideChar(CP_ACP, 0, szA, -1, sz, 64);
      return sz;
    }
    return s;
  }
  double Real() const { return kind == kInt ? i : kind == kReal ? d : _wtof(s.c_str()); }
  LONG Int() const { return kind == kInt ? i : kind == kReal ? (LONG)d : _wtol(s.c_str()); }
};

static MockValue GetCell(const MockRow& row, DBORDINAL nColumn) {
  MockValue v;
  switch (nColumn) {
    case 1: v.kind = MockValue::kInt; v.i = row.id; break;
    case 2: v.kind = MockValue::kText; v.s = row.name; break;
    case 3:
      if (!row.scoreNull) {
        v.kind = MockValue::kReal;
        v.d = row.score;
      }
      break;
  }
  return v;
}

static void SetCell(MockRow& row, DBORDINAL nColumn, const MockValue& v) {
  switch (nColumn) {
    case 1: row.id = v.Int(); break;
    case 2: row.name = v.Text(); break;
    case 3:
      row.scoreNull = v.kind == MockValue::kNull;
      row.score = row.scoreNull ? 0 : v.Real();
      break;
  }
}

// A value into a consumer's buffer as one binding says; its status.
static DBSTATUS PutValue(const MockValue& v, const DBBINDING& b, BYTE* pData) {
  DBSTATUS status = DBSTATUS_S_OK;
  DBLENGTH cb = 0;
  BYTE* p = pData + b.obValue;
  bool bValue = (b.dwPart & DBPART_VALUE) != 0;
  if (v.kind == MockValue::kNull) {
    status = DBSTATUS_S_ISNULL;
  } else {
    switch (b.wType) {
      case DBTYPE_I4: cb = 4; if (bValue) *(LONG*)p = v.Int(); break;
      case DBTYPE_I2: cb = 2; if (bValue) *(SHORT*)p = (SHORT)v.Int(); break;
      case DBTYPE_R8: cb = 8; if (bValue) *(double*)p = v.Real(); break;
      case DBTYPE_R4: cb = 4; if (bValue) *(float*)p = (float)v.Real(); break;
      case DBTYPE_WSTR: {
        std::wstring s = v.Text();
        cb = s.size() * sizeof(WCHAR);
        if (bValue && b.cbMaxLen >= sizeof(WCHAR)) {
          size_t n = s.size();
          if ((n + 1) * sizeof(WCHAR) > b.cbMaxLen) {
            n = b.cbMaxLen / sizeof(WCHAR) - 1;
            status = DBSTATUS_S_TRUNCATED;
          }
          memcpy(p, s.c_str(), n * sizeof(WCHAR));
          ((WCHAR*)p)[n] = 0;
        }
        break;
      }
      case DBTYPE_STR: {
        std::wstring s = v.Text();
        char sz[512];
        int n = WideCharToMultiByte(CP_ACP, 0, s.c_str(), -1, sz, sizeof(sz), NULL, NULL) - 1;
        if (n < 0) n = 0;
        cb = n;
        if (bValue && b.cbMaxLen >= 1) {
          if ((DBLENGTH)n + 1 > b.cbMaxLen) {
            n = (int)b.cbMaxLen - 1;
            status = DBSTATUS_S_TRUNCATED;
          }
          memcpy(p, sz, n);
          p[n] = 0;
        }
        break;
      }
      case DBTYPE_BSTR:
        cb = sizeof(BSTR);
        if (bValue) *(BSTR*)p = SysAllocString(v.Text().c_str());
        break;
      default:
        status = DBSTATUS_E_CANTCONVERTVALUE;
        break;
    }
  }
  if (b.dwPart & DBPART_LENGTH) *(DBLENGTH*)(pData + b.obLength) = cb;
  if (b.dwPart & DBPART_STATUS) *(DBSTATUS*)(pData + b.obStatus) = status;
  // A null nobody can be told about is an error.
  if (status == DBSTATUS_S_ISNULL && !(b.dwPart & DBPART_STATUS)) return DBSTATUS_E_UNAVAILABLE;
  return status;
}

// A value from a consumer's buffer.
static bool GetValue(const DBBINDING& b, const BYTE* pData, MockValue* pv) {
  if ((b.dwPart & DBPART_STATUS) && *(const DBSTATUS*)(pData + b.obStatus) == DBSTATUS_S_ISNULL) {
    pv->kind = MockValue::kNull;
    return true;
  }
  if (!(b.dwPart & DBPART_VALUE)) return false;
  const BYTE* p = pData + b.obValue;
  switch (b.wType) {
    case DBTYPE_I4: pv->kind = MockValue::kInt; pv->i = *(const LONG*)p; return true;
    case DBTYPE_I2: pv->kind = MockValue::kInt; pv->i = *(const SHORT*)p; return true;
    case DBTYPE_R8: pv->kind = MockValue::kReal; pv->d = *(const double*)p; return true;
    case DBTYPE_R4: pv->kind = MockValue::kReal; pv->d = *(const float*)p; return true;
    case DBTYPE_WSTR: {
      size_t nMax = b.cbMaxLen / sizeof(WCHAR), n = 0;
      if (b.dwPart & DBPART_LENGTH) nMax = *(const DBLENGTH*)(pData + b.obLength) / sizeof(WCHAR);
      while (n < nMax && ((const WCHAR*)p)[n] != 0) n++;
      pv->kind = MockValue::kText;
      pv->s.assign((const WCHAR*)p, n);
      return true;
    }
    case DBTYPE_STR: {
      size_t nMax = b.cbMaxLen, n = 0;
      if (b.dwPart & DBPART_LENGTH) nMax = *(const DBLENGTH*)(pData + b.obLength);
      while (n < nMax && p[n] != 0) n++;
      std::string s((const char*)p, n);
      WCHAR sz[512];
      MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, sz, 512);
      pv->kind = MockValue::kText;
      pv->s = sz;
      return true;
    }
    default:
      return false;
  }
}

// ---------------------------------------------------------------------
// The provider's objects. Each counts its references; g_nObjects counts
// the objects.

#define MOCK_REFCOUNT                                               \
  LONG m_nRef;                                                      \
  STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_nRef); } \
  STDMETHODIMP_(ULONG) Release() {                                  \
    LONG n = InterlockedDecrement(&m_nRef);                         \
    if (n == 0) delete this;                                        \
    return n;                                                       \
  }

// Error records, as an OLE DB provider gives them.
class CMockError : public IErrorInfo, public IErrorRecords {
public:
  MOCK_REFCOUNT
  struct Record {
    HRESULT hr;
    std::wstring desc;
  };
  std::vector<Record> m_records;
  CMockError() : m_nRef(1) { g_nObjects++; }
  virtual ~CMockError() { g_nObjects--; }
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) {
    if (riid == IID_IUnknown || riid == IID_IErrorInfo)
      *ppv = static_cast<IErrorInfo*>(this);
    else if (riid == IID_IErrorRecords)
      *ppv = static_cast<IErrorRecords*>(this);
    else
      return *ppv = NULL, E_NOINTERFACE;
    AddRef();
    return S_OK;
  }
  // IErrorInfo: the first record.
  STDMETHODIMP GetGUID(GUID* pguid) { *pguid = IID_ICommand; return S_OK; }
  STDMETHODIMP GetSource(BSTR* p) { *p = SysAllocString(L"MockProvider"); return S_OK; }
  STDMETHODIMP GetDescription(BSTR* p) {
    *p = SysAllocString(m_records.empty() ? L"" : m_records[0].desc.c_str());
    return S_OK;
  }
  STDMETHODIMP GetHelpFile(BSTR* p) { *p = NULL; return S_OK; }
  STDMETHODIMP GetHelpContext(DWORD* p) { *p = 0; return S_OK; }
  // IErrorRecords
  STDMETHODIMP AddErrorRecord(ERRORINFO*, DWORD, DISPPARAMS*, IUnknown*, DWORD) { return E_NOTIMPL; }
  STDMETHODIMP GetBasicErrorInfo(ULONG n, ERRORINFO* p) {
    if (n >= m_records.size()) return DB_E_BADRECORDNUM;
    memset(p, 0, sizeof(*p));
    p->hrError = m_records[n].hr;
    p->clsid = CLSID_MockProvider;
    p->iid = IID_ICommand;
    p->dwMinor = n + 1;
    return S_OK;
  }
  STDMETHODIMP GetCustomErrorObject(ULONG, REFIID, IUnknown** pp) { *pp = NULL; return S_OK; }
  STDMETHODIMP GetErrorInfo(ULONG n, LCID, IErrorInfo** pp) {
    *pp = NULL;
    if (n >= m_records.size()) return DB_E_BADRECORDNUM;
    ICreateErrorInfo* pCreate = NULL;
    HRESULT hr = CreateErrorInfo(&pCreate);
    if (FAILED(hr)) return hr;
    pCreate->SetDescription((LPOLESTR)m_records[n].desc.c_str());
    pCreate->SetSource((LPOLESTR)L"MockProvider");
    pCreate->SetGUID(IID_ICommand);
    hr = pCreate->QueryInterface(IID_IErrorInfo, (void**)pp);
    pCreate->Release();
    return hr;
  }
  STDMETHODIMP GetErrorParameters(ULONG, DISPPARAMS* p) { memset(p, 0, sizeof(*p)); return S_OK; }
  STDMETHODIMP GetRecordCount(ULONG* p) { *p = (ULONG)m_records.size(); return S_OK; }
};

static HRESULT RaiseError(HRESULT hr, const std::wstring& strDesc) {
  CMockError* p = new CMockError;
  CMockError::Record r = {hr, strDesc};
  p->m_records.push_back(r);
  SetErrorInfo(0, static_cast<IErrorInfo*>(p));
  p->Release();
  return hr;
}

// Accessors, for the command (parameters) and the rowset (row data). The
// owner gives IUnknown.
struct MockAccessor {
  DBACCESSORFLAGS dwFlags;
  std::vector<DBBINDING> bindings;
  DBREFCOUNT nRef;
};

class CMockAccessors : public IAccessor {
public:
  std::vector<MockAccessor*> m_list;
  virtual ~CMockAccessors() {
    for (size_t i = 0; i < m_list.size(); i++) {
      if (m_list[i] != NULL) {
        delete m_list[i];
        g_nAccessors--;
      }
    }
  }
  MockAccessor* Find(HACCESSOR h) {
    for (size_t i = 0; i < m_list.size(); i++)
      if (m_list[i] != NULL && (HACCESSOR)m_list[i] == h) return m_list[i];
    return NULL;
  }
  STDMETHODIMP AddRefAccessor(HACCESSOR h, DBREFCOUNT* pc) {
    MockAccessor* p = Find(h);
    if (p == NULL) return DB_E_BADACCESSORHANDLE;
    p->nRef++;
    if (pc != NULL) *pc = p->nRef;
    return S_OK;
  }
  STDMETHODIMP CreateAccessor(DBACCESSORFLAGS dwFlags, DBCOUNTITEM c, const DBBINDING rg[],
                              DBLENGTH, HACCESSOR* ph, DBBINDSTATUS rgStatus[]) {
    if (ph == NULL) return E_INVALIDARG;
    *ph = NULL;
    bool bOK = true;
    for (DBCOUNTITEM i = 0; i < c; i++) {
      bool bGood = rg[i].iOrdinal >= 1;
      if (!(dwFlags & DBACCESSOR_PARAMETERDATA)) bGood = bGood && rg[i].iOrdinal <= kColumns;
      if (rgStatus != NULL) rgStatus[i] = bGood ? DBBINDSTATUS_OK : DBBINDSTATUS_BADORDINAL;
      bOK = bOK && bGood;
    }
    if (!bOK) return DB_E_ERRORSOCCURRED;
    MockAccessor* p = new MockAccessor;
    p->dwFlags = dwFlags;
    p->bindings.assign(rg, rg + c);
    p->nRef = 1;
    m_list.push_back(p);
    g_nAccessors++;
    *ph = (HACCESSOR)p;
    return S_OK;
  }
  STDMETHODIMP GetBindings(HACCESSOR h, DBACCESSORFLAGS* pdwFlags, DBCOUNTITEM* pc,
                           DBBINDING** prg) {
    MockAccessor* p = Find(h);
    if (p == NULL) return DB_E_BADACCESSORHANDLE;
    *pdwFlags = p->dwFlags;
    *pc = p->bindings.size();
    *prg = (DBBINDING*)CoTaskMemAlloc(p->bindings.size() * sizeof(DBBINDING) + 1);
    if (!p->bindings.empty()) memcpy(*prg, &p->bindings[0], p->bindings.size() * sizeof(DBBINDING));
    return S_OK;
  }
  STDMETHODIMP ReleaseAccessor(HACCESSOR h, DBREFCOUNT* pc) {
    for (size_t i = 0; i < m_list.size(); i++) {
      if (m_list[i] != NULL && (HACCESSOR)m_list[i] == h) {
        DBREFCOUNT n = --m_list[i]->nRef;
        if (pc != NULL) *pc = n;
        if (n == 0) {
          delete m_list[i];
          m_list[i] = NULL;
          g_nAccessors--;
        }
        return S_OK;
      }
    }
    return DB_E_BADACCESSORHANDLE;
  }
};

// Whether a property set asks for IRowsetChange.
static bool WantsChange(ULONG cSets, const DBPROPSET* pSets) {
  for (ULONG i = 0; i < cSets; i++) {
    if (pSets[i].guidPropertySet != DBPROPSET_ROWSET) continue;
    for (ULONG j = 0; j < pSets[i].cProperties; j++) {
      const DBPROP& prop = pSets[i].rgProperties[j];
      if (prop.dwPropertyID == DBPROP_IRowsetChange && prop.vValue.vt == VT_BOOL &&
          prop.vValue.boolVal == VARIANT_TRUE)
        return true;
    }
  }
  return false;
}

// A rowset over some of the table's rows. A row handle is the row's
// place in m_rows plus one.
class CMockRowset : public IRowsetChange, public IRowset, public IColumnsInfo,
                    public CMockAccessors {
public:
  MOCK_REFCOUNT
  std::vector<size_t> m_rows;  // places in g_rows
  LONG m_nPos;                 // the next fetch position
  bool m_bAtStart;
  bool m_bChange;
  LONG m_nHeld;

  CMockRowset(bool bChange)
      : m_nRef(1), m_nPos(0), m_bAtStart(true), m_bChange(bChange), m_nHeld(0) {
    g_nObjects++;
  }
  virtual ~CMockRowset() {
    g_nHeldRows -= m_nHeld;  // the rowset going releases them
    g_nObjects--;
  }
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) {
    if (riid == IID_IUnknown || riid == IID_IRowset)
      *ppv = static_cast<IRowset*>(this);
    else if (riid == IID_IAccessor)
      *ppv = static_cast<IAccessor*>(this);
    else if (riid == IID_IColumnsInfo)
      *ppv = static_cast<IColumnsInfo*>(this);
    else if (riid == IID_IRowsetChange && m_bChange)
      *ppv = static_cast<IRowsetChange*>(this);
    else
      return *ppv = NULL, E_NOINTERFACE;
    AddRef();
    return S_OK;
  }

  MockRow* Row(HROW h) {
    size_t n = (size_t)h;
    if (n == 0 || n > m_rows.size()) return NULL;
    return &g_rows[m_rows[n - 1]];
  }

  // IRowset
  STDMETHODIMP AddRefRows(DBCOUNTITEM c, const HROW rg[], DBREFCOUNT rgRef[], DBROWSTATUS rgSt[]) {
    for (DBCOUNTITEM i = 0; i < c; i++) {
      m_nHeld++;
      g_nHeldRows++;
      if (rgRef != NULL) rgRef[i] = 1;
      if (rgSt != NULL) rgSt[i] = DBROWSTATUS_S_OK;
    }
    return S_OK;
  }
  STDMETHODIMP GetData(HROW h, HACCESSOR hAccessor, void* pData) {
    MockRow* pRow = Row(h);
    if (pRow == NULL) return DB_E_BADROWHANDLE;
    if (pRow->deleted) return DB_E_DELETEDROW;
    MockAccessor* pAcc = Find(hAccessor);
    if (pAcc == NULL) return DB_E_BADACCESSORHANDLE;
    int nErrors = 0;
    for (size_t i = 0; i < pAcc->bindings.size(); i++) {
      const DBBINDING& b = pAcc->bindings[i];
      DBSTATUS st = PutValue(GetCell(*pRow, b.iOrdinal), b, (BYTE*)pData);
      if (st != DBSTATUS_S_OK && st != DBSTATUS_S_ISNULL && st != DBSTATUS_S_TRUNCATED) nErrors++;
    }
    if (nErrors == 0) return S_OK;
    return nErrors == (int)pAcc->bindings.size() ? DB_E_ERRORSOCCURRED : DB_S_ERRORSOCCURRED;
  }
  STDMETHODIMP GetNextRows(HCHAPTER, DBROWOFFSET lOffset, DBROWCOUNT cRows,
                           DBCOUNTITEM* pcObtained, HROW** prghRows) {
    *pcObtained = 0;
    LONG n = (LONG)m_rows.size();
    LONG p = m_nPos;
    // From the start, going back counts from the end.
    if (m_bAtStart && (lOffset < 0 || cRows < 0)) p = n;
    m_bAtStart = false;
    LONG start = p + (LONG)lOffset;
    if (start < 0 || start > n) {
      m_nPos = start < 0 ? 0 : n;
      return DB_S_ENDOFROWSET;
    }
    LONG want = cRows < 0 ? -(LONG)cRows : (LONG)cRows;
    std::vector<HROW> got;
    if (cRows > 0) {
      for (LONG i = start; i < n && (LONG)got.size() < want; i++) got.push_back((HROW)(i + 1));
      m_nPos = start + (LONG)got.size();
    } else {
      for (LONG i = start - 1; i >= 0 && (LONG)got.size() < want; i--) got.push_back((HROW)(i + 1));
      m_nPos = start - (LONG)got.size();
    }
    if (!got.empty()) {
      if (*prghRows == NULL) *prghRows = (HROW*)CoTaskMemAlloc(got.size() * sizeof(HROW));
      memcpy(*prghRows, &got[0], got.size() * sizeof(HROW));
      m_nHeld += (LONG)got.size();
      g_nHeldRows += (LONG)got.size();
    }
    *pcObtained = got.size();
    return (LONG)got.size() < want ? DB_S_ENDOFROWSET : S_OK;
  }
  STDMETHODIMP ReleaseRows(DBCOUNTITEM c, const HROW rg[], DBROWOPTIONS[], DBREFCOUNT rgRef[],
                           DBROWSTATUS rgSt[]) {
    for (DBCOUNTITEM i = 0; i < c; i++) {
      if (Row(rg[i]) == NULL) continue;
      m_nHeld--;
      g_nHeldRows--;
      if (rgRef != NULL) rgRef[i] = 0;
      if (rgSt != NULL) rgSt[i] = DBROWSTATUS_S_OK;
    }
    return S_OK;
  }
  STDMETHODIMP RestartPosition(HCHAPTER) {
    m_nPos = 0;
    m_bAtStart = true;
    return S_OK;
  }

  // IRowsetChange
  STDMETHODIMP DeleteRows(HCHAPTER, DBCOUNTITEM c, const HROW rg[], DBROWSTATUS rgSt[]) {
    int nErrors = 0;
    for (DBCOUNTITEM i = 0; i < c; i++) {
      MockRow* pRow = Row(rg[i]);
      DBROWSTATUS st = DBROWSTATUS_S_OK;
      if (pRow == NULL)
        st = DBROWSTATUS_E_INVALID;
      else if (pRow->deleted)
        st = DBROWSTATUS_E_DELETED;
      else
        pRow->deleted = true;
      if (st != DBROWSTATUS_S_OK) nErrors++;
      if (rgSt != NULL) rgSt[i] = st;
    }
    return nErrors == 0 ? S_OK : nErrors == (int)c ? DB_E_ERRORSOCCURRED : DB_S_ERRORSOCCURRED;
  }
  HRESULT Apply(MockRow* pRow, HACCESSOR hAccessor, void* pData) {
    MockAccessor* pAcc = Find(hAccessor);
    if (pAcc == NULL) return DB_E_BADACCESSORHANDLE;
    for (size_t i = 0; i < pAcc->bindings.size(); i++) {
      MockValue v;
      if (GetValue(pAcc->bindings[i], (const BYTE*)pData, &v))
        SetCell(*pRow, pAcc->bindings[i].iOrdinal, v);
    }
    return S_OK;
  }
  STDMETHODIMP SetData(HROW h, HACCESSOR hAccessor, void* pData) {
    MockRow* pRow = Row(h);
    if (pRow == NULL) return DB_E_BADROWHANDLE;
    if (pRow->deleted) return DB_E_DELETEDROW;
    return Apply(pRow, hAccessor, pData);
  }
  STDMETHODIMP InsertRow(HCHAPTER, HACCESSOR hAccessor, void* pData, HROW* phRow) {
    MockRow row = {0, L"", 0, true, false};
    HRESULT hr = Apply(&row, hAccessor, pData);
    if (FAILED(hr)) return hr;
    g_rows.push_back(row);
    m_rows.push_back(g_rows.size() - 1);
    if (phRow != NULL) {
      *phRow = (HROW)m_rows.size();
      m_nHeld++;
      g_nHeldRows++;
    }
    return S_OK;
  }

  // IColumnsInfo
  STDMETHODIMP GetColumnInfo(DBORDINAL* pc, DBCOLUMNINFO** prg, OLECHAR** ppStrings) {
    *pc = kColumns;
    *prg = (DBCOLUMNINFO*)CoTaskMemAlloc(kColumns * sizeof(DBCOLUMNINFO));
    size_t cch = 0;
    for (DBORDINAL i = 0; i < kColumns; i++) cch += wcslen(g_columns[i].pszName) + 1;
    *ppStrings = (OLECHAR*)CoTaskMemAlloc(cch * sizeof(OLECHAR));
    OLECHAR* psz = *ppStrings;
    for (DBORDINAL i = 0; i < kColumns; i++) {
      DBCOLUMNINFO& info = (*prg)[i];
      memset(&info, 0, sizeof(info));
      wcscpy(psz, g_columns[i].pszName);
      info.pwszName = psz;
      psz += wcslen(psz) + 1;
      info.iOrdinal = i + 1;
      info.dwFlags = g_columns[i].dwFlags;
      info.ulColumnSize = g_columns[i].cbSize;
      info.wType = g_columns[i].wType;
      info.bPrecision = (BYTE)~0;
      info.bScale = (BYTE)~0;
      info.columnid.eKind = DBKIND_NAME;
      info.columnid.uName.pwszName = info.pwszName;
    }
    return S_OK;
  }
  STDMETHODIMP MapColumnIDs(DBORDINAL, const DBID[], DBORDINAL[]) { return E_NOTIMPL; }
};

static HRESULT MakeRowset(const std::vector<size_t>& rows, bool bChange, REFIID riid,
                          IUnknown** ppRowset) {
  CMockRowset* p = new CMockRowset(bChange);
  p->m_rows = rows;
  HRESULT hr = p->QueryInterface(riid, (void**)ppRowset);
  p->Release();
  return hr;
}

static std::vector<size_t> LiveRows(LONG nMinID = LONG_MIN) {
  std::vector<size_t> rows;
  for (size_t i = 0; i < g_rows.size(); i++)
    if (!g_rows[i].deleted && g_rows[i].id > nMinID) rows.push_back(i);
  return rows;
}

// A command: SELECT * FROM People [WHERE ID > ?], DELETE FROM People
// WHERE ID = ?, UPDATE People SET Score = 0. Anything else is an error,
// with an error record.
class CMockCommand : public ICommandText, public ICommandProperties, public ISupportErrorInfo,
                     public CMockAccessors {
public:
  MOCK_REFCOUNT
  std::wstring m_strText;
  bool m_bChange;
  CMockCommand() : m_nRef(1), m_bChange(false) { g_nObjects++; }
  virtual ~CMockCommand() { g_nObjects--; }
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) {
    if (riid == IID_IUnknown || riid == IID_ICommand || riid == IID_ICommandText)
      *ppv = static_cast<ICommandText*>(this);
    else if (riid == IID_ICommandProperties)
      *ppv = static_cast<ICommandProperties*>(this);
    else if (riid == IID_IAccessor)
      *ppv = static_cast<IAccessor*>(this);
    else if (riid == IID_ISupportErrorInfo)
      *ppv = static_cast<ISupportErrorInfo*>(this);
    else
      return *ppv = NULL, E_NOINTERFACE;
    AddRef();
    return S_OK;
  }
  STDMETHODIMP InterfaceSupportsErrorInfo(REFIID riid) {
    return riid == IID_ICommand || riid == IID_ICommandText ? S_OK : S_FALSE;
  }

  // The first parameter, as a number.
  bool Param(DBPARAMS* pParams, LONG* pn) {
    if (pParams == NULL || pParams->pData == NULL) return false;
    MockAccessor* pAcc = Find(pParams->hAccessor);
    if (pAcc == NULL) return false;
    for (size_t i = 0; i < pAcc->bindings.size(); i++) {
      MockValue v;
      if (pAcc->bindings[i].iOrdinal == 1 &&
          GetValue(pAcc->bindings[i], (const BYTE*)pParams->pData, &v)) {
        *pn = v.Int();
        return true;
      }
    }
    return false;
  }

  STDMETHODIMP Cancel() { return S_OK; }
  STDMETHODIMP Execute(IUnknown* pOuter, REFIID riid, DBPARAMS* pParams, DBROWCOUNT* pcAffected,
                       IUnknown** ppRowset) {
    if (pOuter != NULL) return DB_E_NOAGGREGATION;
    if (ppRowset != NULL) *ppRowset = NULL;
    if (pcAffected != NULL) *pcAffected = DB_COUNTUNAVAILABLE;
    g_commands.push_back(m_strText);
    const std::wstring& s = m_strText;
    LONG n = 0;
    if (s == L"SELECT * FROM People" || s == L"SELECT * FROM People WHERE ID > ?") {
      LONG nMin = LONG_MIN;
      if (s.find(L'?') != std::wstring::npos && !Param(pParams, &nMin))
        return RaiseError(DB_E_PARAMNOTOPTIONAL, L"No value for the parameter");
      if (ppRowset == NULL || riid == IID_NULL) return S_OK;
      return MakeRowset(LiveRows(nMin), m_bChange, riid, ppRowset);
    }
    if (s == L"DELETE FROM People WHERE ID = ?") {
      if (!Param(pParams, &n)) return RaiseError(DB_E_PARAMNOTOPTIONAL, L"No value for the parameter");
      DBROWCOUNT c = 0;
      for (size_t i = 0; i < g_rows.size(); i++) {
        if (!g_rows[i].deleted && g_rows[i].id == n) {
          g_rows[i].deleted = true;
          c++;
        }
      }
      if (pcAffected != NULL) *pcAffected = c;
      return S_OK;
    }
    if (s == L"UPDATE People SET Score = 0") {
      DBROWCOUNT c = 0;
      for (size_t i = 0; i < g_rows.size(); i++) {
        if (!g_rows[i].deleted) {
          g_rows[i].score = 0;
          g_rows[i].scoreNull = false;
          c++;
        }
      }
      if (pcAffected != NULL) *pcAffected = c;
      return S_OK;
    }
    return RaiseError(DB_E_ERRORSINCOMMAND, L"Syntax error in '" + s + L"'");
  }
  STDMETHODIMP GetDBSession(REFIID, IUnknown** pp) { *pp = NULL; return S_FALSE; }
  STDMETHODIMP GetCommandText(GUID* pguid, LPOLESTR* pp) {
    if (pguid != NULL) *pguid = DBGUID_DEFAULT;
    *pp = (LPOLESTR)CoTaskMemAlloc((m_strText.size() + 1) * sizeof(OLECHAR));
    wcscpy(*pp, m_strText.c_str());
    return S_OK;
  }
  STDMETHODIMP SetCommandText(REFGUID, LPCOLESTR psz) {
    m_strText = psz != NULL ? psz : L"";
    return S_OK;
  }
  // ICommandProperties: only IRowsetChange matters.
  STDMETHODIMP GetProperties(const ULONG, const DBPROPIDSET[], ULONG* pc, DBPROPSET** pp) {
    *pc = 0;
    *pp = NULL;
    return S_OK;
  }
  STDMETHODIMP SetProperties(ULONG cSets, DBPROPSET rgSets[]) {
    m_bChange = WantsChange(cSets, rgSets);
    return S_OK;
  }
};

// The session: tables, commands and transactions.
class CMockSession : public IOpenRowset, public IDBCreateCommand, public ITransactionLocal {
public:
  MOCK_REFCOUNT
  CMockSession() : m_nRef(1) { g_nObjects++; }
  virtual ~CMockSession() { g_nObjects--; }
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) {
    if (riid == IID_IUnknown || riid == IID_IOpenRowset)
      *ppv = static_cast<IOpenRowset*>(this);
    else if (riid == IID_IDBCreateCommand)
      *ppv = static_cast<IDBCreateCommand*>(this);
    else if (riid == IID_ITransactionLocal || riid == IID_ITransaction)
      *ppv = static_cast<ITransactionLocal*>(this);
    else
      return *ppv = NULL, E_NOINTERFACE;
    AddRef();
    return S_OK;
  }
  STDMETHODIMP OpenRowset(IUnknown* pOuter, DBID* pTableID, DBID*, REFIID riid, ULONG cSets,
                          DBPROPSET rgSets[], IUnknown** ppRowset) {
    if (pOuter != NULL) return DB_E_NOAGGREGATION;
    if (ppRowset != NULL) *ppRowset = NULL;
    if (pTableID == NULL || pTableID->eKind != DBKIND_NAME || pTableID->uName.pwszName == NULL ||
        _wcsicmp(pTableID->uName.pwszName, L"People") != 0)
      return DB_E_NOTABLE;
    return MakeRowset(LiveRows(), WantsChange(cSets, rgSets), riid, ppRowset);
  }
  STDMETHODIMP CreateCommand(IUnknown* pOuter, REFIID riid, IUnknown** ppCommand) {
    if (pOuter != NULL) return DB_E_NOAGGREGATION;
    CMockCommand* p = new CMockCommand;
    HRESULT hr = p->QueryInterface(riid, (void**)ppCommand);
    p->Release();
    return hr;
  }
  // The table as it was is put back on Abort.
  STDMETHODIMP StartTransaction(ISOLEVEL, ULONG, ITransactionOptions*, ULONG* pulLevel) {
    if (g_bInTransaction) return XACT_E_XTIONEXISTS;
    g_saved = g_rows;
    g_bInTransaction = true;
    if (pulLevel != NULL) *pulLevel = 1;
    return S_OK;
  }
  STDMETHODIMP GetOptionsObject(ITransactionOptions** pp) { *pp = NULL; return E_NOTIMPL; }
  STDMETHODIMP Commit(BOOL, DWORD, DWORD) {
    if (!g_bInTransaction) return XACT_E_NOTRANSACTION;
    g_bInTransaction = false;
    return S_OK;
  }
  STDMETHODIMP Abort(BOID*, BOOL, BOOL) {
    if (!g_bInTransaction) return XACT_E_NOTRANSACTION;
    g_rows = g_saved;
    g_bInTransaction = false;
    return S_OK;
  }
  STDMETHODIMP GetTransactionInfo(XACTTRANSINFO* p) {
    if (!g_bInTransaction) return XACT_E_NOTRANSACTION;
    memset(p, 0, sizeof(*p));
    p->isoLevel = ISOLATIONLEVEL_READCOMMITTED;
    return S_OK;
  }
};

// The data source: the DBINIT properties, a password check, sessions.
class CMockDataSource : public IDBInitialize, public IDBProperties, public IDBCreateSession {
public:
  MOCK_REFCOUNT
  std::vector<DBPROPID> m_ids;
  std::vector<std::wstring> m_values;
  bool m_bInit;
  CMockDataSource() : m_nRef(1), m_bInit(false) { g_nObjects++; }
  virtual ~CMockDataSource() { g_nObjects--; }
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) {
    if (riid == IID_IUnknown || riid == IID_IDBInitialize)
      *ppv = static_cast<IDBInitialize*>(this);
    else if (riid == IID_IDBProperties)
      *ppv = static_cast<IDBProperties*>(this);
    else if (riid == IID_IDBCreateSession)
      *ppv = static_cast<IDBCreateSession*>(this);
    else
      return *ppv = NULL, E_NOINTERFACE;
    AddRef();
    return S_OK;
  }
  const std::wstring* Prop(DBPROPID id) const {
    for (size_t i = 0; i < m_ids.size(); i++)
      if (m_ids[i] == id) return &m_values[i];
    return NULL;
  }
  STDMETHODIMP Initialize() {
    const std::wstring* pPassword = Prop(DBPROP_AUTH_PASSWORD);
    if (pPassword != NULL && *pPassword != L"secret") return DB_SEC_E_AUTH_FAILED;
    m_bInit = true;
    return S_OK;
  }
  STDMETHODIMP Uninitialize() {
    m_bInit = false;
    return S_OK;
  }
  STDMETHODIMP GetProperties(ULONG cSets, const DBPROPIDSET rgSets[], ULONG* pc, DBPROPSET** pp) {
    *pc = 0;
    *pp = (DBPROPSET*)CoTaskMemAlloc(sizeof(DBPROPSET) * (cSets > 0 ? cSets : 1));
    for (ULONG i = 0; i < cSets; i++) {
      DBPROPSET& set = (*pp)[i];
      set.guidPropertySet = rgSets[i].guidPropertySet;
      set.cProperties = rgSets[i].cPropertyIDs;
      set.rgProperties = (DBPROP*)CoTaskMemAlloc(sizeof(DBPROP) * (set.cProperties + 1));
      for (ULONG j = 0; j < set.cProperties; j++) {
        DBPROP& prop = set.rgProperties[j];
        memset(&prop, 0, sizeof(prop));
        prop.dwPropertyID = rgSets[i].rgPropertyIDs[j];
        VariantInit(&prop.vValue);
        const std::wstring* p = Prop(prop.dwPropertyID);
        if (p != NULL) {
          prop.vValue.vt = VT_BSTR;
          prop.vValue.bstrVal = SysAllocString(p->c_str());
        } else {
          prop.dwStatus = DBPROPSTATUS_NOTSET;
        }
      }
      (*pc)++;
    }
    return S_OK;
  }
  STDMETHODIMP GetPropertyInfo(ULONG, const DBPROPIDSET[], ULONG* pc, DBPROPINFOSET** pp,
                               OLECHAR** ppDesc) {
    *pc = 0;
    *pp = NULL;
    if (ppDesc != NULL) *ppDesc = NULL;
    return E_NOTIMPL;
  }
  STDMETHODIMP SetProperties(ULONG cSets, DBPROPSET rgSets[]) {
    for (ULONG i = 0; i < cSets; i++) {
      for (ULONG j = 0; j < rgSets[i].cProperties; j++) {
        const DBPROP& prop = rgSets[i].rgProperties[j];
        VARIANT v;
        VariantInit(&v);
        if (FAILED(VariantChangeType(&v, const_cast<VARIANT*>(&prop.vValue), 0, VT_BSTR))) continue;
        m_ids.push_back(prop.dwPropertyID);
        m_values.push_back(v.bstrVal != NULL ? v.bstrVal : L"");
        VariantClear(&v);
      }
    }
    return S_OK;
  }
  STDMETHODIMP CreateSession(IUnknown* pOuter, REFIID riid, IUnknown** pp) {
    *pp = NULL;
    if (!m_bInit) return E_UNEXPECTED;
    if (pOuter != NULL) return DB_E_NOAGGREGATION;
    CMockSession* p = new CMockSession;
    HRESULT hr = p->QueryInterface(riid, (void**)pp);
    p->Release();
    return hr;
  }
};

class CMockFactory : public IClassFactory {
public:
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) {
    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
      *ppv = this;
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() { return 2; }
  STDMETHODIMP_(ULONG) Release() { return 1; }
  STDMETHODIMP CreateInstance(IUnknown* pOuter, REFIID riid, void** ppv) {
    *ppv = NULL;
    if (pOuter != NULL) return CLASS_E_NOAGGREGATION;
    CMockDataSource* p = new CMockDataSource;
    HRESULT hr = p->QueryInterface(riid, ppv);
    p->Release();
    return hr;
  }
  STDMETHODIMP LockServer(BOOL) { return S_OK; }
};
static CMockFactory g_factory;

// ---------------------------------------------------------------------
// The consumer's classes

class CPeople {
public:
  LONG m_ID;
  WCHAR m_Name[32];
  double m_Score;
  CPeople() { ClearRecord(); }
  void ClearRecord() {
    m_ID = 0;
    m_Name[0] = 0;
    m_Score = 0;
  }
  BEGIN_COLUMN_MAP(CPeople)
    COLUMN_ENTRY(1, m_ID)
    COLUMN_ENTRY(2, m_Name)
    COLUMN_ENTRY(3, m_Score)
  END_COLUMN_MAP()
  DEFINE_COMMAND_EX(CPeople, L"SELECT * FROM People")
};

// With a length and a status for the nullable column.
class CPeopleStatus {
public:
  LONG m_ID;
  char m_Name[4];  // short: "Brian" is cut
  DBLENGTH m_nNameLength;
  DBSTATUS m_nNameStatus;
  double m_Score;
  DBLENGTH m_nScoreLength;
  DBSTATUS m_nScoreStatus;
  BEGIN_COLUMN_MAP(CPeopleStatus)
    COLUMN_ENTRY(1, m_ID)
    COLUMN_ENTRY_LENGTH_STATUS(2, m_Name, m_nNameLength, m_nNameStatus)
    COLUMN_ENTRY_LENGTH_STATUS(3, m_Score, m_nScoreLength, m_nScoreStatus)
  END_COLUMN_MAP()
  DEFINE_COMMAND(CPeopleStatus, _T("SELECT * FROM People"))
};

// Two accessors: the ID read with each move, the name when asked for.
class CPeopleSplit {
public:
  LONG m_ID;
  WCHAR m_Name[32];
  BEGIN_ACCESSOR_MAP(CPeopleSplit, 2)
    BEGIN_ACCESSOR(0, true)
      COLUMN_ENTRY(1, m_ID)
    END_ACCESSOR()
    BEGIN_ACCESSOR(1, false)
      COLUMN_ENTRY(2, m_Name)
    END_ACCESSOR()
  END_ACCESSOR_MAP()
};

// A parameter for the WHERE clause.
class CPeopleAbove {
public:
  LONG m_ID;
  WCHAR m_Name[32];
  LONG m_nMin;
  BEGIN_COLUMN_MAP(CPeopleAbove)
    COLUMN_ENTRY(1, m_ID)
    COLUMN_ENTRY(2, m_Name)
  END_COLUMN_MAP()
  BEGIN_PARAM_MAP(CPeopleAbove)
    SET_PARAM_TYPE(DBPARAMIO_INPUT)
    COLUMN_ENTRY(1, m_nMin)
  END_PARAM_MAP()
};

static bool Eq(const WCHAR* a, const WCHAR* b) { return a != NULL && wcscmp(a, b) == 0; }

static void TestDataSource() {
  CDataSource ds;
  // A wrong password is refused, and the source stays closed.
  HRESULT hr = ds.Open(CLSID_MockProvider, _T("people.db"), _T("ada"), _T("wrong"));
  CHECK(hr == DB_SEC_E_AUTH_FAILED);
  CHECK(ds.m_spInit == NULL);
  hr = ds.Open(CLSID_MockProvider, _T("people.db"), _T("ada"), _T("secret"));
  CHECK(hr == S_OK);
  CComVariant var;
  hr = ds.GetProperty(DBPROPSET_DBINIT, DBPROP_INIT_DATASOURCE, &var);
  CHECK(hr == S_OK && var.vt == VT_BSTR && Eq(var.bstrVal, L"people.db"));
  var.Clear();
  CHECK(ds.GetProperty(DBPROPSET_DBINIT, DBPROP_AUTH_USERID, &var) == S_OK &&
        var.vt == VT_BSTR && Eq(var.bstrVal, L"ada"));
  CSession session;
  CHECK(session.Open(ds) == S_OK);
  session.Close();
  ds.Close();
  CHECK(ds.m_spInit == NULL);

  // By ProgID, with a property set of the program's own.
  CRegKey key;
  CString strCLSID;
  {
    LPOLESTR psz = NULL;
    StringFromCLSID(CLSID_MockProvider, &psz);
    strCLSID = psz;
    CoTaskMemFree(psz);
  }
  if (key.Create(HKEY_CLASSES_ROOT, _T("MockProvider.1\\CLSID")) ==
      ERROR_SUCCESS) {
    key.SetStringValue(NULL, strCLSID);
    key.Close();
    CDBPropSet set(DBPROPSET_DBINIT);
    CHECK(set.AddProperty(DBPROP_INIT_DATASOURCE, OLESTR("people.db")));
    CHECK(set.AddProperty(DBPROP_INIT_TIMEOUT, (long)5));
    CDBPropSet copy = set;  // copies keep their values
    CHECK(copy.cProperties == 2);
    hr = ds.Open(_T("MockProvider.1"), &copy);
    TestLog("open by ProgID: %08lx", (unsigned long)hr);
    CHECK(hr == S_OK);
    var.Clear();
    CHECK(ds.GetProperty(DBPROPSET_DBINIT, DBPROP_INIT_TIMEOUT, &var) == S_OK &&
          var.vt == VT_BSTR && Eq(var.bstrVal, L"5"));
    ds.Close();
    RegDeleteKey(HKEY_CLASSES_ROOT, _T("MockProvider.1\\CLSID"));
    RegDeleteKey(HKEY_CLASSES_ROOT, _T("MockProvider.1"));
  }
  // A class nobody registered.
  CHECK(FAILED(ds.Open(_T("NoSuch.Provider.1"))));
}

static void TestTable(CSession& session) {
  CTable<CAccessor<CPeople> > table;
  CHECK(table.Open(session, _T("Nope")) == DB_E_NOTABLE);
  HRESULT hr = table.Open(session, _T("People"));
  CHECK(hr == S_OK);
  // No IRowsetChange asked for: none, and no changes.
  CHECK(table.m_spRowsetChange == NULL);
  CHECK(table.MoveFirst() == S_OK);
  CHECK(table.m_ID == 1 && Eq(table.m_Name, L"Ada") && table.m_Score == 36.5);
  CHECK(table.SetData() == E_UNEXPECTED);
  // Brian's score is null, and this map has no status for it: the
  // other columns are read.
  hr = table.MoveNext();
  CHECK(hr == DB_S_ERRORSOCCURRED && table.m_ID == 2);
  CHECK(table.MoveNext() == S_OK);
  CHECK(table.m_ID == 3 && Eq(table.m_Name, L"Carol"));
  CHECK(table.MoveNext() == DB_S_ENDOFROWSET);
  CHECK(table.m_hRow == NULL);
  CHECK(table.MoveLast() == S_OK && table.m_ID == 3);
  CHECK(table.MovePrev() == DB_S_ERRORSOCCURRED);  // Brian again
  CHECK(table.MovePrev() == S_OK && table.m_ID == 1);
  CHECK(table.MovePrev() == DB_S_ENDOFROWSET);
  CHECK(table.MoveFirst() == S_OK && table.m_ID == 1);
  // Skipping one.
  CHECK(table.MoveNext(1, true) == S_OK && table.m_ID == 3);
  table.Close();
  CHECK(table.m_spRowset == NULL);

  // The column information.
  CTable<CNoAccessor> plain;
  CHECK(plain.Open(session, _T("People")) == S_OK);
  DBORDINAL nColumns = 0;
  DBCOLUMNINFO* pInfo = NULL;
  OLECHAR* pStrings = NULL;
  CHECK(plain.GetColumnInfo(&nColumns, &pInfo, &pStrings) == S_OK);
  CHECK(nColumns == 3 && pInfo != NULL && Eq(pInfo[1].pwszName, L"Name") &&
        pInfo[2].wType == DBTYPE_R8);
  CoTaskMemFree(pInfo);
  CoTaskMemFree(pStrings);
}

static void TestChanges(CSession& session) {
  CDBPropSet props(DBPROPSET_ROWSET);
  props.AddProperty(DBPROP_IRowsetChange, true);
  props.AddProperty(DBPROP_UPDATABILITY,
                    (long)(DBPROPVAL_UP_CHANGE | DBPROPVAL_UP_INSERT | DBPROPVAL_UP_DELETE));
  CTable<CAccessor<CPeople> > table;
  CHECK(table.Open(session, _T("People"), &props) == S_OK);
  CHECK(table.m_spRowsetChange != NULL);
  CHECK(table.MoveFirst() == S_OK);
  table.m_Score = 40;
  wcscpy(table.m_Name, L"Ada L.");
  CHECK(table.SetData() == S_OK);
  CHECK(g_rows[0].score == 40 && g_rows[0].name == L"Ada L.");

  // A new row, its handle kept.
  table.ClearRecord();
  table.m_ID = 4;
  wcscpy(table.m_Name, L"Dora");
  table.m_Score = 12.25;
  CHECK(table.Insert(0, true) == S_OK);
  CHECK(table.m_hRow != NULL);
  CHECK(g_rows.size() == 4 && g_rows[3].name == L"Dora" && !g_rows[3].scoreNull);
  table.ClearRecord();
  CHECK(table.GetData() == S_OK && table.m_ID == 4 && table.m_Score == 12.25);
  // And one more, without.
  table.m_ID = 5;
  wcscpy(table.m_Name, L"Eve");
  CHECK(table.Insert() == S_OK);
  CHECK(g_rows.size() == 5 && g_rows[4].id == 5);

  // Deleted: the last row.
  CHECK(table.MoveLast() == S_OK && table.m_ID == 5);
  CHECK(table.Delete() == S_OK);
  CHECK(g_rows[4].deleted);
  CHECK(table.Delete() == DB_E_ERRORSOCCURRED);  // already
  CHECK(table.GetData() == DB_E_DELETEDROW);
  // Deferred updates are not offered.
  CHECK(table.Update() == E_NOINTERFACE && table.Undo() == E_NOINTERFACE);
  table.Close();
  CHECK(table.Update() == E_UNEXPECTED);
}

static void TestCommands(CSession& session) {
  // DEFINE_COMMAND's text when no text is given.
  CCommand<CAccessor<CPeople> > cmd;
  size_t nRun = g_commands.size();
  CHECK(cmd.Open(session) == S_OK);
  CHECK(g_commands.size() == nRun + 1 && g_commands.back() == L"SELECT * FROM People");
  int nRows = 0, nNullErrors = 0;
  for (;;) {
    HRESULT hr = cmd.MoveNext();
    if (hr == DB_S_ENDOFROWSET) break;
    if (hr == DB_S_ERRORSOCCURRED) nNullErrors++;
    nRows++;
    if (nRows > 10) break;
  }
  CHECK(nRows == 4);  // Ada, Brian, Carol, Dora
  CHECK(nNullErrors == 1);
  cmd.Close();

  // Lengths and statuses.
  CCommand<CAccessor<CPeopleStatus> > status;
  CHECK(status.Open(session, (LPCWSTR)NULL) == S_OK);
  CHECK(status.MoveNext() == S_OK);
  CHECK(status.m_ID == 1 && status.m_nScoreStatus == DBSTATUS_S_OK && status.m_Score == 40);
  CHECK(status.m_nNameStatus == DBSTATUS_S_TRUNCATED && strcmp(status.m_Name, "Ada") == 0 &&
        status.m_nNameLength == 6);
  CHECK(status.MoveNext() == S_OK);
  CHECK(status.m_ID == 2 && status.m_nScoreStatus == DBSTATUS_S_ISNULL);

  // Two accessors.
  CCommand<CAccessor<CPeopleSplit> > split;
  CHECK(split.Open(session, "SELECT * FROM People") == S_OK);
  CHECK(split.GetNumAccessors() == 2 && split.IsAutoAccessor(0) && !split.IsAutoAccessor(1));
  split.m_Name[0] = 0;
  CHECK(split.MoveNext() == S_OK && split.m_ID == 1 && split.m_Name[0] == 0);
  CHECK(split.GetData(1) == S_OK && Eq(split.m_Name, L"Ada L."));

  // A parameter, and the command run again with another value.
  CCommand<CAccessor<CPeopleAbove> > above;
  above.m_nMin = 1;
  CHECK(above.Open(session, L"SELECT * FROM People WHERE ID > ?") == S_OK);
  CHECK(above.MoveNext() == S_OK && above.m_ID == 2 && Eq(above.m_Name, L"Brian"));
  above.m_nMin = 3;
  CHECK(above.Open() == S_OK);
  CHECK(above.MoveNext() == S_OK && above.m_ID == 4);
  CHECK(above.MoveNext() == DB_S_ENDOFROWSET);
}

static void TestDynamic(CSession& session) {
  CCommand<CDynamicAccessor> dyn;
  CHECK(dyn.Open(session, L"SELECT * FROM People") == S_OK);
  CHECK(dyn.GetColumnCount() == 3 && !dyn.HasBookmark());
  CHECK(Eq(dyn.GetColumnName(2), L"Name"));
  DBTYPE wType = 0;
  CHECK(dyn.GetColumnType(3, &wType) && wType == DBTYPE_R8);
  DBORDINAL nOrdinal = 0;
  CHECK(dyn.GetOrdinal(_T("score"), &nOrdinal) && nOrdinal == 3);
  CHECK(dyn.MoveNext() == S_OK);
  LONG nID = 0;
  CHECK(dyn.GetValue(1, &nID) && nID == 1);
  CHECK(Eq((WCHAR*)dyn.GetValue(2), L"Ada L."));
  double d = 0;
  CHECK(dyn.GetValue(_T("Score"), &d) && d == 40);
  DBLENGTH nLength = 0;
  CHECK(dyn.GetLength(2, &nLength) && nLength == 12);
  CHECK(dyn.MoveNext() == S_OK);  // the status takes Brian's null
  DBSTATUS st = 0;
  CHECK(dyn.GetStatus(3, &st) && st == DBSTATUS_S_ISNULL);
  dyn.Close();

  CCommand<CDynamicStringAccessorW> strW;
  CHECK(strW.Open(session, L"SELECT * FROM People") == S_OK);
  CHECK(strW.MoveFirst() == S_OK);
  CHECK(Eq(strW.GetString(1), L"1") && Eq(strW.GetString(3), L"40"));
  CHECK(strW.MoveLast() == S_OK);
  CHECK(Eq(strW.GetString(_T("Score")), L"12.25"));

  CCommand<CDynamicStringAccessor> str;
  CHECK(str.Open(session, _T("SELECT * FROM People")) == S_OK);
  CHECK(str.MoveNext() == S_OK);
  CHECK(lstrcmp(str.GetString(2), _T("Ada L.")) == 0);
  // A string goes back into the buffer.
  CHECK(str.SetString(2, _T("Ada")) == S_OK && lstrcmp(str.GetString(2), _T("Ada")) == 0);
}

static void TestManual(CSession& session) {
  // Row data and a parameter, bound by hand.
  struct {
    LONG nID;
    WCHAR szName[16];
    DBLENGTH nLength;
    DBSTATUS nStatus;
  } row;
  struct {
    LONG nMin;
  } param;
  CCommand<CManualAccessor> cmd;
  CHECK(cmd.CreateAccessor(2, &row, sizeof(row)) == S_OK);
  cmd.AddBindEntry(1, DBTYPE_I4, sizeof(row.nID), &row.nID);
  cmd.AddBindEntry(2, DBTYPE_WSTR, sizeof(row.szName), row.szName, &row.nLength, &row.nStatus);
  CHECK(cmd.CreateParameterAccessor(1, &param, sizeof(param)) == S_OK);
  cmd.AddParameterEntry(1, DBTYPE_I4, sizeof(param.nMin), &param.nMin);
  param.nMin = 2;
  CHECK(cmd.Open(session, L"SELECT * FROM People WHERE ID > ?") == S_OK);
  CHECK(cmd.MoveNext() == S_OK && row.nID == 3 && Eq(row.szName, L"Carol") && row.nLength == 10 &&
        row.nStatus == DBSTATUS_S_OK);
  cmd.Close();

  // No rows: a DELETE and the rows it took.
  CCommand<CManualAccessor, CNoRowset> del;
  struct {
    LONG nID;
  } key;
  CHECK(del.CreateParameterAccessor(1, &key, sizeof(key)) == S_OK);
  del.AddParameterEntry(1, DBTYPE_I4, sizeof(key.nID), &key.nID);
  key.nID = 4;
  DBROWCOUNT nAffected = 0;
  CHECK(del.Open(session, L"DELETE FROM People WHERE ID = ?", NULL, &nAffected) == S_OK);
  CHECK(nAffected == 1 && g_rows[3].deleted);
  key.nID = 42;
  CHECK(del.Open(NULL, &nAffected) == S_OK && nAffected == 0);
}

static void TestTransactions(CSession& session) {
  CCommand<CNoAccessor, CNoRowset> cmd;
  DBROWCOUNT nAffected = 0;
  // Aborted: the scores come back.
  CHECK(session.StartTransaction() == S_OK);
  XACTTRANSINFO info;
  CHECK(session.GetTransactionInfo(&info) == S_OK);
  CHECK(cmd.Open(session, L"UPDATE People SET Score = 0", NULL, &nAffected) == S_OK);
  CHECK(nAffected == 3 && g_rows[2].score == 0 && !g_rows[1].scoreNull);
  CHECK(session.Abort() == S_OK);
  CHECK(g_rows[2].score == 29 && g_rows[1].scoreNull);
  CHECK(session.Abort() == XACT_E_NOTRANSACTION);
  // Committed: they stay.
  CHECK(session.StartTransaction() == S_OK);
  CHECK(cmd.Open(session, L"UPDATE People SET Score = 0", NULL, &nAffected) == S_OK);
  CHECK(session.Commit() == S_OK);
  CHECK(g_rows[2].score == 0 && g_rows[0].score == 0);
}

static void TestErrors(CSession& session) {
  CCommand<CNoAccessor, CNoRowset> cmd;
  HRESULT hr = cmd.Open(session, L"FROB People");
  CHECK(hr == DB_E_ERRORSINCOMMAND);
  CDBErrorInfo errors;
  ULONG nRecords = 0;
  hr = errors.GetErrorRecords(cmd.m_spCommand, IID_ICommand, &nRecords);
  CHECK(hr == S_OK && nRecords == 1);
  CComBSTR bstrDesc, bstrSource;
  GUID guid = GUID_NULL;
  CHECK(errors.GetAllErrorInfo(0, GetUserDefaultLCID(), &bstrDesc, &bstrSource, &guid) == S_OK);
  CHECK(bstrDesc != NULL && wcsstr(bstrDesc, L"FROB") != NULL);
  CHECK(bstrSource != NULL && Eq(bstrSource, L"MockProvider") && guid == IID_ICommand);
  ERRORINFO basic;
  CHECK(errors.GetBasicErrorInfo(0, &basic) == S_OK && basic.hrError == DB_E_ERRORSINCOMMAND &&
        basic.clsid == CLSID_MockProvider);
  CHECK(errors.GetBasicErrorInfo(1, &basic) == DB_E_BADRECORDNUM);
  TestLog("error record: %ls", (const WCHAR*)bstrDesc);
  // Read once: nothing is left.
  CHECK(FAILED(errors.GetErrorRecords(&nRecords)) && nRecords == 0);
  // A parameter left out.
  CCommand<CNoAccessor, CNoRowset> del;
  CHECK(del.Open(session, L"DELETE FROM People WHERE ID = ?") == DB_E_PARAMNOTOPTIONAL);
  AtlTraceErrorRecords(DB_E_PARAMNOTOPTIONAL);
}

static void TestConsumer() {
  DWORD dwCookie = 0;
  HRESULT hr = CoRegisterClassObject(CLSID_MockProvider, &g_factory, CLSCTX_INPROC_SERVER,
                                     REGCLS_MULTIPLEUSE, &dwCookie);
  CHECK(hr == S_OK);
  ResetTable();
  TestDataSource();
  TestLog("data sources: %d failure(s) so far", g_nFailures);
  {
    CDataSource ds;
    CSession session;
    CHECK(ds.Open(CLSID_MockProvider, _T("people.db")) == S_OK);
    CHECK(session.Open(ds) == S_OK);
    TestTable(session);
    TestLog("tables: %d failure(s) so far", g_nFailures);
    TestChanges(session);
    TestLog("changes: %d failure(s) so far", g_nFailures);
    TestCommands(session);
    TestLog("commands: %d failure(s) so far", g_nFailures);
    TestDynamic(session);
    TestLog("dynamic accessors: %d failure(s) so far", g_nFailures);
    TestManual(session);
    TestLog("manual accessors: %d failure(s) so far", g_nFailures);
    TestTransactions(session);
    TestErrors(session);
    TestLog("transactions, errors: %d failure(s) so far", g_nFailures);
  }
  // Everything given back.
  TestLog("left: %ld objects, %ld rows, %ld accessors", (long)g_nObjects, (long)g_nHeldRows,
          (long)g_nAccessors);
  CHECK(g_nObjects == 0);
  CHECK(g_nHeldRows == 0);
  CHECK(g_nAccessors == 0);
  // The record view starts again from the first table.
  ResetTable();
  g_rows[1].score = 31;
  g_rows[1].scoreNull = false;
}

// ---------------------------------------------------------------------
// COleDBRecordView

// The form's row: TCHAR text, as AppWizard writes it.
class CPeopleRow {
public:
  LONG m_ID;
  TCHAR m_szName[32];
  double m_Score;
  BEGIN_COLUMN_MAP(CPeopleRow)
    COLUMN_ENTRY(1, m_ID)
    COLUMN_ENTRY(2, m_szName)
    COLUMN_ENTRY(3, m_Score)
  END_COLUMN_MAP()
};

class CPeopleSet : public CTable<CAccessor<CPeopleRow> > {
public:
  CDataSource m_ds;
  CSession m_session;
  HRESULT OpenAll() {
    HRESULT hr = m_ds.Open(CLSID_MockProvider, _T("people.db"));
    if (FAILED(hr)) return hr;
    hr = m_session.Open(m_ds);
    if (FAILED(hr)) return hr;
    CDBPropSet props(DBPROPSET_ROWSET);
    props.AddProperty(DBPROP_IRowsetChange, true);
    props.AddProperty(DBPROP_UPDATABILITY, (long)DBPROPVAL_UP_CHANGE);
    return Open(m_session, _T("People"), &props);
  }
  void CloseAll() {
    Close();
    m_session.Close();
    m_ds.Close();
  }
};

class COleDbDoc : public CDocument {
  DECLARE_DYNCREATE(COleDbDoc)
public:
  CPeopleSet m_set;
  virtual void DeleteContents() { m_set.CloseAll(); }
};
IMPLEMENT_DYNCREATE(COleDbDoc, CDocument)

class CPeopleView : public COleDBRecordView {
  DECLARE_DYNCREATE(CPeopleView)
public:
  CPeopleSet* m_pSet;
  CPeopleView() : COleDBRecordView(IDD_ROW), m_pSet(NULL) {}
  virtual CRowset<>* OnGetRowset() { return m_pSet; }
  virtual void DoDataExchange(CDataExchange* pDX) {
    COleDBRecordView::DoDataExchange(pDX);
    DDX_Text(pDX, IDC_ID, m_pSet->m_ID);
    DDX_Text(pDX, IDC_NAME, m_pSet->m_szName, 32);
    DDX_Text(pDX, IDC_SCORE, m_pSet->m_Score);
  }
  virtual void OnInitialUpdate() {
    m_pSet = &((COleDbDoc*)GetDocument())->m_set;
    HRESULT hr = m_pSet->OpenAll();
    CHECK(hr == S_OK);
    if (SUCCEEDED(hr)) CHECK(m_pSet->MoveFirst() == S_OK);
    COleDBRecordView::OnInitialUpdate();
  }
  BOOL OnFirst() const { return m_bOnFirstRow; }
  BOOL OnLast() const { return m_bOnLastRow; }
};
IMPLEMENT_DYNCREATE(CPeopleView, COleDBRecordView)

class CTestCmdUI : public CCmdUI {
public:
  BOOL m_bEnabled;
  CTestCmdUI() : m_bEnabled(-1) {}
  virtual void Enable(BOOL bOn) { m_bEnabled = bOn; }
};

class CMainFrame : public CFrameWnd {
  DECLARE_DYNCREATE(CMainFrame)
public:
  int m_nStep;
  CMainFrame() : m_nStep(0) {}

  afx_msg int OnCreate(LPCREATESTRUCT lpcs) {
    if (CFrameWnd::OnCreate(lpcs) == -1) return -1;
    SetTimer(1, 250, NULL);
    return 0;
  }
  CPeopleView* View() { return (CPeopleView*)GetActiveView(); }
  CString Text(int nID) {
    CString str;
    View()->GetDlgItemText(nID, str);
    return str;
  }
  void SetText(int nID, LPCTSTR psz) { View()->SetDlgItemText(nID, psz); }
  BOOL Enabled(UINT nID) {
    CTestCmdUI ui;
    ui.m_nID = nID;
    ui.DoUpdate(View(), FALSE);
    return ui.m_bEnabled == TRUE;
  }
  void Command(UINT nID) { SendMessage(WM_COMMAND, nID); }

  afx_msg void OnTimer(UINT_PTR nIDEvent) {
    if (nIDEvent != 1) {
      CFrameWnd::OnTimer(nIDEvent);
      return;
    }
    CPeopleView* pView = View();
    switch (m_nStep++) {
      case 0:
        CHECK(pView->IsKindOf(RUNTIME_CLASS(COleDBRecordView)));
        CHECK(pView->m_pSet != NULL && pView->m_pSet->m_spRowsetChange != NULL);
        // The first row is on the form.
        CHECK(Text(IDC_ID) == _T("1"));
        CHECK(Text(IDC_NAME) == _T("Ada"));
        CHECK(Text(IDC_SCORE) == _T("36.5"));
        CHECK(!Enabled(ID_RECORD_FIRST) && !Enabled(ID_RECORD_PREV));
        CHECK(Enabled(ID_RECORD_NEXT) && Enabled(ID_RECORD_LAST));
        TestSaveWindowBitmap(m_hWnd, _T("oledbtest.bmp"));
        // An edit goes to the row as the view moves on.
        SetText(IDC_NAME, _T("Ada L."));
        Command(ID_RECORD_NEXT);
        CHECK(g_rows[0].name == L"Ada L.");
        CHECK(Text(IDC_ID) == _T("2") && Text(IDC_NAME) == _T("Brian"));
        CHECK(Text(IDC_SCORE) == _T("31"));
        CHECK(Enabled(ID_RECORD_FIRST) && Enabled(ID_RECORD_PREV));
        break;
      case 1:
        Command(ID_RECORD_NEXT);
        CHECK(Text(IDC_NAME) == _T("Carol"));
        // Past the end it stays on the last row.
        Command(ID_RECORD_NEXT);
        CHECK(Text(IDC_NAME) == _T("Carol"));
        CHECK(pView->OnLast());
        CHECK(!Enabled(ID_RECORD_NEXT) && !Enabled(ID_RECORD_LAST));
        CHECK(Enabled(ID_RECORD_PREV));
        SetText(IDC_SCORE, _T("30"));
        Command(ID_RECORD_FIRST);
        CHECK(g_rows[2].score == 30);
        CHECK(Text(IDC_NAME) == _T("Ada L."));
        CHECK(pView->OnFirst());
        break;
      case 2:
        Command(ID_RECORD_LAST);
        CHECK(Text(IDC_SCORE) == _T("30"));
        CHECK(pView->OnLast() && !pView->OnFirst());
        Command(ID_RECORD_PREV);
        CHECK(Text(IDC_NAME) == _T("Brian"));
        Command(ID_RECORD_PREV);
        Command(ID_RECORD_PREV);  // before the first: back on it
        CHECK(Text(IDC_NAME) == _T("Ada L."));
        CHECK(pView->OnFirst());
        CHECK(!Enabled(ID_RECORD_PREV) && Enabled(ID_RECORD_NEXT));
        TestSaveWindowBitmap(m_hWnd, _T("oledbtest-end.bmp"));
        break;
      default:
        KillTimer(1);
        PostMessage(WM_CLOSE);
        break;
    }
  }
  DECLARE_MESSAGE_MAP()
};
IMPLEMENT_DYNCREATE(CMainFrame, CFrameWnd)
BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
  ON_WM_CREATE()
  ON_WM_TIMER()
END_MESSAGE_MAP()

class COleDbApp : public CWinApp {
public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("oledbtest.log"));
    CoInitialize(NULL);
    TestConsumer();
    CSingleDocTemplate* pTemplate =
        new CSingleDocTemplate(IDR_MAINFRAME, RUNTIME_CLASS(COleDbDoc), RUNTIME_CLASS(CMainFrame),
                               RUNTIME_CLASS(CPeopleView));
    AddDocTemplate(pTemplate);
    CCommandLineInfo cmdInfo;
    ParseCommandLine(cmdInfo);
    if (!ProcessShellCommand(cmdInfo)) {
      TestLog("ProcessShellCommand failed");
      g_nFailures++;
      return FALSE;
    }
    m_pMainWnd->ShowWindow(SW_SHOW);
    m_pMainWnd->UpdateWindow();
    return TRUE;
  }
  virtual int ExitInstance() {
    CWinApp::ExitInstance();
    TestLog("after the view: %ld objects, %ld rows, %ld accessors", (long)g_nObjects,
            (long)g_nHeldRows, (long)g_nAccessors);
    CHECK(g_nObjects == 0 && g_nHeldRows == 0 && g_nAccessors == 0);
    CoUninitialize();
    return TestFinish();
  }
};

COleDbApp theApp;
