// occdb.cpp - data binding for hosted ActiveX controls: a control's
// property bound to a field of a data source control's current row
// (simple binding, BindDefaultProperty), or given the data source itself
// (BindProperty), for data source controls that are OLE DB data sources
// (the DataSource interface of msdatsrc.h), or older ones that make an
// ICursor (IVBDSC, ocdb.h).
//
// The data source control's site keeps a CDataSourceControl: the
// rowset of the data source's default data member, its row position and
// the bound properties. When the current row or a field changes, the
// bound properties are set from their fields; when a bound property
// changes (IPropertyNotifySink::OnChanged), its field is set. With a
// cursor, a field is read by binding its column as a VARIANT and moving
// to the current row, and set by ICursorUpdateARow (BeginUpdate,
// SetColumn, Update); the site's INotifyDBEvents tells of changes.
//
// The data source control's notifications are ON_DSCNOTIFY ones for its
// container, through the site (occsite.cpp for a cursor, here for OLE
// DB once something is bound to it).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxocc.h>
#include <stddef.h>
#include <oledb.h>
#include <msdatsrc.h>

// {7C0FFAB3-CD84-11D0-949A-00A0C91110ED}
static const IID kIID_DataSource = {
    0x7c0ffab3, 0xcd84, 0x11d0, {0x94, 0x9a, 0x00, 0xa0, 0xc9, 0x11, 0x10, 0xed}};

#define _AFX_CURSOR_IID(l) {l, 0xd188, 0x11cd, {0xad, 0x48, 0x00, 0xaa, 0x00, 0x3c, 0x9c, 0xb6}}
extern "C" const IID IID_ICursor = _AFX_CURSOR_IID(0x9f6aa700);
extern "C" const IID IID_ICursorMove = _AFX_CURSOR_IID(0xacff0690);
extern "C" const IID IID_ICursorUpdateARow = _AFX_CURSOR_IID(0xd14216a0);
extern "C" const IID IID_INotifyDBEvents = _AFX_CURSOR_IID(0xdb526cc0);
#undef _AFX_CURSOR_IID
// {1AB42240-8C70-11CE-9421-00AA0062BE57}
extern "C" const IID IID_IVBDSC = {
    0x1ab42240, 0x8c70, 0x11ce, {0x94, 0x21, 0x00, 0xaa, 0x00, 0x62, 0xbe, 0x57}};

// A column of a cursor: its ID (whose name, if it has one, is
// strIDName) and its name.
struct _AFX_CURSOR_COLUMN {
  DBCOLUMNID id;
  CStringW strIDName;
  CString strName;
};

// One bound property of a control.
struct CDataBoundProperty {
  CDataBoundProperty* m_pNext;
  COleControlSite* m_pSite;        // the bound control
  CDataSourceControl* m_pDSC;      // its data source
  DISPID m_dispid;
  VARTYPE m_vt;
  BOOL m_bComplex;   // BindProperty: the property holds the data source
  DBORDINAL m_nColumn;  // from 1; with a cursor, in m_cursorColumns
  HACCESSOR m_hAccessor;
};

// The buffer a field is read into and written from.
struct _AFX_DB_FIELD {
  VARIANT var;
  DBSTATUS status;
};

class CDataSourceControl;

// The notifications the data source control's rowset sends.
class _AfxDSCSink final : public IRowPositionChange, public IRowsetNotify {
public:
  explicit _AfxDSCSink(CDataSourceControl* pDSC) : m_cRef(1), m_pDSC(pDSC) {}
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (::IsEqualIID(iid, IID_IUnknown) || ::IsEqualIID(iid, IID_IRowPositionChange))
      *ppv = static_cast<IRowPositionChange*>(this);
    else if (::IsEqualIID(iid, IID_IRowsetNotify))
      *ppv = static_cast<IRowsetNotify*>(this);
    else {
      *ppv = NULL;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  STDMETHOD_(ULONG, AddRef)() { return (ULONG)InterlockedIncrement(&m_cRef); }
  STDMETHOD_(ULONG, Release)() {
    LONG c = InterlockedDecrement(&m_cRef);
    if (c == 0) delete this;
    return (ULONG)c;
  }
  STDMETHOD(OnRowPositionChange)(DBREASON eReason, DBEVENTPHASE ePhase, WINBOOL);
  STDMETHOD(OnFieldChange)(IRowset*, HROW, DBORDINAL, DBORDINAL[], DBREASON, DBEVENTPHASE ePhase,
                           WINBOOL);
  STDMETHOD(OnRowChange)(IRowset*, DBCOUNTITEM, const HROW[], DBREASON, DBEVENTPHASE ePhase,
                         WINBOOL);
  STDMETHOD(OnRowsetChange)(IRowset*, DBREASON, DBEVENTPHASE ePhase, WINBOOL);

  LONG m_cRef;
  CDataSourceControl* m_pDSC;  // NULL once the data source control goes
};

class CDataSourceControl {
public:
  explicit CDataSourceControl(COleControlSite* pSite)
      : m_pSite(pSite),
        m_pDataSource(NULL),
        m_pRowPos(NULL),
        m_pRowset(NULL),
        m_pAccessor(NULL),
        m_pSink(NULL),
        m_dwRowPosCookie(0),
        m_dwRowsetCookie(0),
        m_hFirstRow(DB_NULL_HROW),
        m_nColumns(0),
        m_pColumns(NULL),
        m_pStrings(NULL),
        m_bUpdating(FALSE),
        m_pCursorMove(NULL),
        m_pCursorUpdate(NULL) {}
  ~CDataSourceControl();

  BOOL Initialize();
  BOOL InitializeCursor();
  BOOL GetCursorField(CDataBoundProperty* pProp, VARIANT* pvar);
  BOOL SetCursorField(CDataBoundProperty* pProp, const VARIANT* pvar);
  IUnknown* GetCursor();  // the rowset or the cursor, without a reference
  DBORDINAL FindColumn(LPCTSTR pszName) const;
  HROW GetCurrentRow();  // with a reference to release, or DB_NULL_HROW
  void ReleaseRow(HROW hRow);
  BOOL GetField(CDataBoundProperty* pProp, VARIANT* pvar);
  BOOL SetField(CDataBoundProperty* pProp, const VARIANT* pvar);
  void UpdateControl(CDataBoundProperty* pProp);
  // Sets the bound properties from their fields; after pExcept set its
  // field, the others bound to that field.
  void UpdateControls(CDataBoundProperty* pExcept = NULL);
  void Add(CDataBoundProperty* pProp);
  void Remove(CDataBoundProperty* pProp);

  COleControlSite* m_pSite;
  DataSource* m_pDataSource;
  IRowPosition* m_pRowPos;  // NULL when the data source gives only a rowset
  IRowset* m_pRowset;
  IAccessor* m_pAccessor;
  _AfxDSCSink* m_pSink;
  DWORD m_dwRowPosCookie;
  DWORD m_dwRowsetCookie;
  HROW m_hFirstRow;  // without a row position, the current row is the first
  DBORDINAL m_nColumns;
  DBCOLUMNINFO* m_pColumns;
  OLECHAR* m_pStrings;
  CPtrList m_bound;  // the bound properties
  BOOL m_bUpdating;
  // An older data source control: its cursor, and the cursor's columns.
  ICursorMove* m_pCursorMove;
  ICursorUpdateARow* m_pCursorUpdate;  // NULL when read only
  CPtrArray m_cursorColumns;
};

static void Advise(IUnknown* pUnk, REFIID iid, IUnknown* pSink, DWORD* pdwCookie) {
  IConnectionPointContainer* pCPC = NULL;
  if (pUnk == NULL || FAILED(pUnk->QueryInterface(IID_IConnectionPointContainer, (void**)&pCPC)))
    return;
  IConnectionPoint* pCP = NULL;
  if (SUCCEEDED(pCPC->FindConnectionPoint(iid, &pCP))) {
    if (FAILED(pCP->Advise(pSink, pdwCookie))) *pdwCookie = 0;
    pCP->Release();
  }
  pCPC->Release();
}

static void Unadvise(IUnknown* pUnk, REFIID iid, DWORD dwCookie) {
  IConnectionPointContainer* pCPC = NULL;
  if (pUnk == NULL || dwCookie == 0 ||
      FAILED(pUnk->QueryInterface(IID_IConnectionPointContainer, (void**)&pCPC)))
    return;
  IConnectionPoint* pCP = NULL;
  if (SUCCEEDED(pCPC->FindConnectionPoint(iid, &pCP))) {
    pCP->Unadvise(dwCookie);
    pCP->Release();
  }
  pCPC->Release();
}

BOOL CDataSourceControl::Initialize() {
  if (m_pSite->m_pObject == NULL) return FALSE;
  if (FAILED(m_pSite->m_pObject->QueryInterface(kIID_DataSource, (void**)&m_pDataSource)))
    return InitializeCursor();
  // The default data member: a row position, or else a rowset.
  BSTR bstrDM = ::SysAllocString(L"");
  IUnknown* pUnk = NULL;
  if (SUCCEEDED(m_pDataSource->getDataMember(bstrDM, IID_IRowPosition, &pUnk)) && pUnk != NULL) {
    pUnk->QueryInterface(IID_IRowPosition, (void**)&m_pRowPos);
    pUnk->Release();
    pUnk = NULL;
    if (m_pRowPos != NULL) m_pRowPos->GetRowset(IID_IRowset, &pUnk);
  } else {
    pUnk = NULL;
    m_pDataSource->getDataMember(bstrDM, IID_IRowset, &pUnk);
  }
  ::SysFreeString(bstrDM);
  if (pUnk != NULL) {
    pUnk->QueryInterface(IID_IRowset, (void**)&m_pRowset);
    pUnk->Release();
  }
  if (m_pRowset == NULL) return FALSE;
  m_pRowset->QueryInterface(IID_IAccessor, (void**)&m_pAccessor);
  IColumnsInfo* pInfo = NULL;
  if (SUCCEEDED(m_pRowset->QueryInterface(IID_IColumnsInfo, (void**)&pInfo))) {
    if (FAILED(pInfo->GetColumnInfo(&m_nColumns, &m_pColumns, &m_pStrings))) {
      m_nColumns = 0;
      m_pColumns = NULL;
      m_pStrings = NULL;
    }
    pInfo->Release();
  }
  if (m_pAccessor == NULL || m_pColumns == NULL) return FALSE;

  m_pSink = new _AfxDSCSink(this);
  if (m_pRowPos != NULL)
    Advise(m_pRowPos, IID_IRowPositionChange, static_cast<IRowPositionChange*>(m_pSink),
           &m_dwRowPosCookie);
  Advise(m_pRowset, IID_IRowsetNotify, static_cast<IRowsetNotify*>(m_pSink), &m_dwRowsetCookie);
  if (m_pRowPos == NULL) {
    DBCOUNTITEM nGot = 0;
    HROW* phRows = &m_hFirstRow;
    if (FAILED(m_pRowset->GetNextRows(DB_NULL_HCHAPTER, 0, 1, &nGot, &phRows)) || nGot == 0)
      m_hFirstRow = DB_NULL_HROW;
  }
  return TRUE;
}

// The cursor's columns: the rows of its columns cursor, read as their
// column ID and name.
BOOL CDataSourceControl::InitializeCursor() {
  IVBDSC* pVBDSC = NULL;
  if (FAILED(m_pSite->m_pObject->QueryInterface(IID_IVBDSC, (void**)&pVBDSC))) return FALSE;
  ICursor* pCursor = NULL;
  HRESULT hr = pVBDSC->CreateCursor(&pCursor);
  pVBDSC->Release();
  if (FAILED(hr) || pCursor == NULL) return FALSE;
  hr = pCursor->QueryInterface(IID_ICursorMove, (void**)&m_pCursorMove);
  pCursor->Release();
  if (FAILED(hr) || m_pCursorMove == NULL) {
    m_pCursorMove = NULL;
    return FALSE;
  }
  if (FAILED(m_pCursorMove->QueryInterface(IID_ICursorUpdateARow, (void**)&m_pCursorUpdate)))
    m_pCursorUpdate = NULL;

  IUnknown* pUnk = NULL;
  ULONG_PTR nRows = 0;
  if (FAILED(m_pCursorMove->GetColumnsCursor(IID_ICursor, &pUnk, &nRows)) || pUnk == NULL)
    return FALSE;
  ICursor* pColumns = NULL;
  pUnk->QueryInterface(IID_ICursor, (void**)&pColumns);
  pUnk->Release();
  if (pColumns == NULL) return FALSE;

  struct MetaRow {
    DBCOLUMNID id;
    DWORD dwIDInfo;
    LPSTR pszName;
    DWORD dwNameInfo;
  };
  DBCOLUMNBINDING bindings[2];
  memset(bindings, 0, sizeof(bindings));
  bindings[0].columnID = COLUMN_COLUMNID;
  bindings[0].obData = offsetof(MetaRow, id);
  bindings[0].cbMaxLen = sizeof(DBCOLUMNID);
  bindings[0].obInfo = offsetof(MetaRow, dwIDInfo);
  bindings[0].obVarDataLen = DB_NOVALUE;
  bindings[0].dwBinding = DBBINDING_DEFAULT;
  bindings[0].dwDataType = DBTYPE_COLUMNID;
  bindings[1].columnID = COLUMN_NAME;
  bindings[1].obData = offsetof(MetaRow, pszName);
  bindings[1].cbMaxLen = DB_NOMAXLENGTH;
  bindings[1].obInfo = offsetof(MetaRow, dwNameInfo);
  bindings[1].obVarDataLen = DB_NOVALUE;
  bindings[1].dwBinding = DBBINDING_DEFAULT;
  bindings[1].dwDataType = VT_LPSTR;
  DBFETCHROWS fetch;
  memset(&fetch, 0, sizeof(fetch));
  fetch.cRowsRequested = nRows != 0 ? nRows : 1024;
  fetch.dwFlags = DBROWFETCH_CALLEEALLOCATES;
  LARGE_INTEGER zero;
  zero.QuadPart = 0;
  hr = pColumns->SetBindings(2, bindings, sizeof(MetaRow), DBCOLUMNBINDOPTS_REPLACE);
  if (SUCCEEDED(hr)) hr = pColumns->GetNextRows(zero, &fetch);
  pColumns->Release();
  if (FAILED(hr)) return FALSE;
  for (ULONG_PTR i = 0; i < fetch.cRowsReturned; i++) {
    const MetaRow& row = ((const MetaRow*)fetch.pData)[i];
    _AFX_CURSOR_COLUMN* pCol = new _AFX_CURSOR_COLUMN;
    pCol->id = row.id;
    if (row.id.dwKind != DBCOLKIND_GUID_NUMBER) {
      pCol->strIDName = row.id.lpdbsz;
      pCol->id.lpdbsz = (LPDBSTR)(LPCWSTR)pCol->strIDName;
    }
    if (row.pszName != NULL && row.dwNameInfo != DB_NULL) pCol->strName = row.pszName;
    m_cursorColumns.Add(pCol);
  }
  ::CoTaskMemFree(fetch.pData);
  ::CoTaskMemFree(fetch.pVarData);
  return TRUE;
}

CDataSourceControl::~CDataSourceControl() {
  // Its bound properties are bound to nothing now.
  while (!m_bound.IsEmpty()) {
    CDataBoundProperty* pProp = (CDataBoundProperty*)m_bound.GetHead();
    COleControlSite* pSite = pProp->m_pSite;
    CDataBoundProperty** pp = &pSite->m_pBindings;
    while (*pp != NULL && *pp != pProp) pp = &(*pp)->m_pNext;
    if (*pp != NULL) *pp = pProp->m_pNext;
    Remove(pProp);
    delete pProp;
  }
  if (m_pSink != NULL) {
    m_pSink->m_pDSC = NULL;
    if (m_pRowPos != NULL) Unadvise(m_pRowPos, IID_IRowPositionChange, m_dwRowPosCookie);
    Unadvise(m_pRowset, IID_IRowsetNotify, m_dwRowsetCookie);
    m_pSink->Release();
  }
  if (m_hFirstRow != DB_NULL_HROW && m_pRowset != NULL)
    m_pRowset->ReleaseRows(1, &m_hFirstRow, NULL, NULL, NULL);
  if (m_pColumns != NULL) ::CoTaskMemFree(m_pColumns);
  if (m_pStrings != NULL) ::CoTaskMemFree(m_pStrings);
  if (m_pAccessor != NULL) m_pAccessor->Release();
  if (m_pRowset != NULL) m_pRowset->Release();
  if (m_pRowPos != NULL) m_pRowPos->Release();
  if (m_pDataSource != NULL) m_pDataSource->Release();
  for (INT_PTR i = 0; i < m_cursorColumns.GetSize(); i++)
    delete (_AFX_CURSOR_COLUMN*)m_cursorColumns[i];
  if (m_pCursorUpdate != NULL) m_pCursorUpdate->Release();
  if (m_pCursorMove != NULL) m_pCursorMove->Release();
}

DBORDINAL CDataSourceControl::FindColumn(LPCTSTR pszName) const {
  if (pszName == NULL) return 0;
  for (INT_PTR i = 0; i < m_cursorColumns.GetSize(); i++) {
    if (((_AFX_CURSOR_COLUMN*)m_cursorColumns[i])->strName.CompareNoCase(pszName) == 0)
      return (DBORDINAL)i + 1;
  }
  CStringW strName(pszName);
  for (DBORDINAL i = 0; i < m_nColumns; i++) {
    const DBCOLUMNINFO& info = m_pColumns[i];
    if (info.pwszName != NULL && ::lstrcmpiW(info.pwszName, strName) == 0) return info.iOrdinal;
  }
  return 0;
}

HROW CDataSourceControl::GetCurrentRow() {
  if (m_pRowPos == NULL) {
    if (m_hFirstRow != DB_NULL_HROW) m_pRowset->AddRefRows(1, &m_hFirstRow, NULL, NULL);
    return m_hFirstRow;
  }
  HCHAPTER hChapter = DB_NULL_HCHAPTER;
  HROW hRow = DB_NULL_HROW;
  DBPOSITIONFLAGS dwFlags = 0;
  if (FAILED(m_pRowPos->GetRowPosition(&hChapter, &hRow, &dwFlags))) return DB_NULL_HROW;
  if (dwFlags != DBPOSITION_OK && hRow != DB_NULL_HROW) {
    ReleaseRow(hRow);
    hRow = DB_NULL_HROW;
  }
  return hRow;
}

void CDataSourceControl::ReleaseRow(HROW hRow) {
  if (hRow != DB_NULL_HROW) m_pRowset->ReleaseRows(1, &hRow, NULL, NULL, NULL);
}

static BOOL MakeAccessor(IAccessor* pAccessor, DBORDINAL nColumn, HACCESSOR* phAccessor) {
  DBBINDING binding;
  memset(&binding, 0, sizeof(binding));
  binding.iOrdinal = nColumn;
  binding.obValue = offsetof(_AFX_DB_FIELD, var);
  binding.obStatus = offsetof(_AFX_DB_FIELD, status);
  binding.dwPart = DBPART_VALUE | DBPART_STATUS;
  binding.dwMemOwner = DBMEMOWNER_CLIENTOWNED;
  binding.eParamIO = DBPARAMIO_NOTPARAM;
  binding.cbMaxLen = sizeof(VARIANT);
  binding.wType = DBTYPE_VARIANT;
  DBBINDSTATUS status = DBBINDSTATUS_OK;
  return SUCCEEDED(pAccessor->CreateAccessor(DBACCESSOR_ROWDATA, 1, &binding,
                                             sizeof(_AFX_DB_FIELD), phAccessor, &status));
}

// A cursor's field: its column bound as a VARIANT of the property's
// type, read from the current row.
BOOL CDataSourceControl::GetCursorField(CDataBoundProperty* pProp, VARIANT* pvar) {
  if (pProp->m_nColumn == 0 || pProp->m_nColumn > (DBORDINAL)m_cursorColumns.GetSize())
    return FALSE;
  _AFX_CURSOR_COLUMN* pCol = (_AFX_CURSOR_COLUMN*)m_cursorColumns[(INT_PTR)pProp->m_nColumn - 1];
  DBCOLUMNBINDING binding;
  memset(&binding, 0, sizeof(binding));
  binding.columnID = pCol->id;
  binding.obData = 0;
  binding.cbMaxLen = DB_NOMAXLENGTH;
  binding.obInfo = DB_NOVALUE;
  binding.obVarDataLen = DB_NOVALUE;
  binding.dwBinding = DBBINDING_VARIANT;
  binding.dwDataType = pProp->m_vt;
  if (FAILED(m_pCursorMove->SetBindings(1, &binding, sizeof(VARIANT), DBCOLUMNBINDOPTS_REPLACE)))
    return FALSE;
  VARIANT var;
  ::VariantInit(&var);
  DBFETCHROWS fetch;
  memset(&fetch, 0, sizeof(fetch));
  fetch.cRowsRequested = 1;
  fetch.pData = &var;
  LARGE_INTEGER zero;
  zero.QuadPart = 0;
  HRESULT hr = m_pCursorMove->Move(sizeof(DBBMK_CURRENT), (void*)&DBBMK_CURRENT, zero, &fetch);
  if (FAILED(hr) || fetch.cRowsReturned == 0) {
    ::VariantClear(&var);
    return FALSE;
  }
  *pvar = var;
  return TRUE;
}

// Set through ICursorUpdateARow, as an update of the current row unless
// one is going on already.
BOOL CDataSourceControl::SetCursorField(CDataBoundProperty* pProp, const VARIANT* pvar) {
  if (m_pCursorUpdate == NULL || pProp->m_nColumn == 0 ||
      pProp->m_nColumn > (DBORDINAL)m_cursorColumns.GetSize())
    return FALSE;
  _AFX_CURSOR_COLUMN* pCol = (_AFX_CURSOR_COLUMN*)m_cursorColumns[(INT_PTR)pProp->m_nColumn - 1];
  m_bUpdating = TRUE;
  DWORD dwMode = DBEDITMODE_NONE;
  HRESULT hr = m_pCursorUpdate->GetEditMode(&dwMode);
  BOOL bBegun = FALSE;
  if (SUCCEEDED(hr) && dwMode == DBEDITMODE_NONE) {
    hr = m_pCursorUpdate->BeginUpdate(DBROWACTION_UPDATE);
    bBegun = SUCCEEDED(hr);
  }
  if (SUCCEEDED(hr)) {
    VARIANT var;
    ::VariantInit(&var);
    ::VariantCopy(&var, const_cast<VARIANT*>(pvar));
    BOOL bNull = var.vt == VT_NULL || var.vt == VT_EMPTY;
    DBBINDPARAMS params;
    memset(&params, 0, sizeof(params));
    params.dwBinding = DBBINDING_VARIANT;
    params.dwDataType = var.vt;
    params.dwInfo = bNull ? DB_NULL : (DWORD)sizeof(VARIANT);
    params.pData = &var;
    hr = m_pCursorUpdate->SetColumn(&pCol->id, &params);
    ::VariantClear(&var);
    if (SUCCEEDED(hr) && dwMode == DBEDITMODE_NONE) hr = m_pCursorUpdate->Update(NULL, NULL, NULL);
    if (FAILED(hr) && bBegun) m_pCursorUpdate->Cancel();
  }
  m_bUpdating = FALSE;
  return SUCCEEDED(hr);
}

IUnknown* CDataSourceControl::GetCursor() {
  if (m_pRowset != NULL) return m_pRowset;
  return static_cast<ICursor*>(m_pCursorMove);
}

BOOL CDataSourceControl::GetField(CDataBoundProperty* pProp, VARIANT* pvar) {
  ::VariantInit(pvar);
  if (m_pCursorMove != NULL) return GetCursorField(pProp, pvar);
  HROW hRow = GetCurrentRow();
  if (hRow == DB_NULL_HROW) return FALSE;
  _AFX_DB_FIELD field;
  ::VariantInit(&field.var);
  field.status = DBSTATUS_S_OK;
  HRESULT hr = m_pRowset->GetData(hRow, pProp->m_hAccessor, &field);
  ReleaseRow(hRow);
  if (FAILED(hr)) return FALSE;
  if (field.status == DBSTATUS_S_ISNULL) {
    ::VariantClear(&field.var);
    pvar->vt = VT_NULL;
  } else if (field.status != DBSTATUS_S_OK && field.status != DBSTATUS_S_TRUNCATED) {
    ::VariantClear(&field.var);
    return FALSE;
  } else {
    *pvar = field.var;  // the variant's contents are ours now
  }
  return TRUE;
}

BOOL CDataSourceControl::SetField(CDataBoundProperty* pProp, const VARIANT* pvar) {
  if (m_pCursorMove != NULL) return SetCursorField(pProp, pvar);
  IRowsetChange* pChange = NULL;
  if (FAILED(m_pRowset->QueryInterface(IID_IRowsetChange, (void**)&pChange))) return FALSE;
  HROW hRow = GetCurrentRow();
  HRESULT hr = E_FAIL;
  if (hRow != DB_NULL_HROW) {
    _AFX_DB_FIELD field;
    ::VariantInit(&field.var);
    ::VariantCopy(&field.var, const_cast<VARIANT*>(pvar));
    field.status = (pvar->vt == VT_NULL || pvar->vt == VT_EMPTY) ? DBSTATUS_S_ISNULL
                                                                 : DBSTATUS_S_OK;
    m_bUpdating = TRUE;
    hr = pChange->SetData(hRow, pProp->m_hAccessor, &field);
    m_bUpdating = FALSE;
    ::VariantClear(&field.var);
    ReleaseRow(hRow);
  }
  pChange->Release();
  return SUCCEEDED(hr);
}

void CDataSourceControl::UpdateControl(CDataBoundProperty* pProp) {
  COleControlSite* pSite = pProp->m_pSite;
  if (pSite->m_pObject == NULL) return;
  COleVariant var;
  if (pProp->m_bComplex) {
    // The OLE DB data source, or the cursor.
    var.vt = VT_UNKNOWN;
    var.punkVal = m_pDataSource != NULL ? (IUnknown*)m_pDataSource : GetCursor();
    var.punkVal->AddRef();
  } else {
    GetField(pProp, &var);
    // A null field, or no current row, is the property type's empty value.
    if (pProp->m_vt != VT_VARIANT && var.vt != pProp->m_vt) {
      if (var.vt == VT_NULL) var.vt = VT_EMPTY;
      if (FAILED(::VariantChangeType(&var, &var, 0, pProp->m_vt))) return;
    }
  }
  BOOL bIgnore = pSite->m_bIgnoreNotify;
  pSite->m_bIgnoreNotify = TRUE;
  pSite->SafeSetProperty(pProp->m_dispid, VT_VARIANT, &var);
  pSite->m_bIgnoreNotify = bIgnore;
}

void CDataSourceControl::UpdateControls(CDataBoundProperty* pExcept) {
  POSITION pos = m_bound.GetHeadPosition();
  while (pos != NULL) {
    CDataBoundProperty* pProp = (CDataBoundProperty*)m_bound.GetNext(pos);
    if (pProp == pExcept || pProp->m_bComplex) continue;
    // After pExcept set its field, the properties bound to that field.
    if (pExcept != NULL && pProp->m_nColumn != pExcept->m_nColumn) continue;
    UpdateControl(pProp);
  }
}

void CDataSourceControl::Add(CDataBoundProperty* pProp) { m_bound.AddTail(pProp); }

void CDataSourceControl::Remove(CDataBoundProperty* pProp) {
  POSITION pos = m_bound.Find(pProp);
  if (pos != NULL) m_bound.RemoveAt(pos);
  if (pProp->m_hAccessor != NULL && m_pAccessor != NULL) {
    m_pAccessor->ReleaseAccessor(pProp->m_hAccessor, NULL);
    pProp->m_hAccessor = NULL;
  }
}

// ---------------------------------------------------------------------
// Notifications

// The ON_DSCNOTIFY notification for a step of an OLE DB change.
static HRESULT FireDSCNotify(CDataSourceControl* pDSC, DBEVENTPHASE ePhase, DSCREASON nReason) {
  DSCSTATE nState;
  switch (ePhase) {
    case DBEVENTPHASE_OKTODO: nState = dscOKToDo; break;
    case DBEVENTPHASE_ABOUTTODO: nState = dscAboutToDo; break;
    case DBEVENTPHASE_SYNCHAFTER: nState = dscSyncAfter; break;
    case DBEVENTPHASE_FAILEDTODO: nState = dscFailedToDo; break;
    case DBEVENTPHASE_DIDEVENT: nState = dscDidEvent; break;
    default: return S_OK;
  }
  if (pDSC == NULL || nReason == dscNoReason) return S_OK;
  HRESULT hr = _AfxFireDSCNotify(pDSC->m_pSite, nState, nReason);
  // Only a change not made yet can be refused.
  return ePhase == DBEVENTPHASE_OKTODO && hr == S_FALSE ? S_FALSE : S_OK;
}

STDMETHODIMP _AfxDSCSink::OnRowPositionChange(DBREASON, DBEVENTPHASE ePhase, WINBOOL) {
  if (FireDSCNotify(m_pDSC, ePhase, dscMove) == S_FALSE) return S_FALSE;
  if (m_pDSC != NULL && ePhase == DBEVENTPHASE_DIDEVENT) m_pDSC->UpdateControls();
  return S_OK;
}
STDMETHODIMP _AfxDSCSink::OnFieldChange(IRowset*, HROW, DBORDINAL, DBORDINAL[], DBREASON,
                                        DBEVENTPHASE ePhase, WINBOOL) {
  if (FireDSCNotify(m_pDSC, ePhase, dscModify) == S_FALSE) return S_FALSE;
  // A field this side set has already been updated in the controls.
  if (m_pDSC != NULL && !m_pDSC->m_bUpdating && ePhase == DBEVENTPHASE_DIDEVENT)
    m_pDSC->UpdateControls();
  return S_OK;
}
STDMETHODIMP _AfxDSCSink::OnRowChange(IRowset*, DBCOUNTITEM, const HROW[], DBREASON eReason,
                                      DBEVENTPHASE ePhase, WINBOOL) {
  DSCREASON nReason = dscNoReason;
  switch (eReason) {
    case DBREASON_ROW_ACTIVATE: nReason = dscMove; break;
    case DBREASON_ROW_DELETE: nReason = dscDelete; break;
    case DBREASON_ROW_INSERT: nReason = dscInsert; break;
    case DBREASON_ROW_UPDATE: nReason = dscCommit; break;
    default: break;
  }
  if (FireDSCNotify(m_pDSC, ePhase, nReason) == S_FALSE) return S_FALSE;
  if (m_pDSC != NULL && !m_pDSC->m_bUpdating && ePhase == DBEVENTPHASE_DIDEVENT)
    m_pDSC->UpdateControls();
  return S_OK;
}
STDMETHODIMP _AfxDSCSink::OnRowsetChange(IRowset*, DBREASON, DBEVENTPHASE ePhase, WINBOOL) {
  if (m_pDSC != NULL && ePhase == DBEVENTPHASE_DIDEVENT) m_pDSC->UpdateControls();
  return S_OK;
}

// ---------------------------------------------------------------------
// The site's side

static void AFXAPI BindChanged(COleControlSite* pSite, DISPID dispid) {
  for (CDataBoundProperty* pProp = pSite->m_pBindings; pProp != NULL; pProp = pProp->m_pNext) {
    if (pProp->m_dispid != dispid || pProp->m_bComplex) continue;
    COleVariant var;
    TRY { pSite->GetProperty(dispid, VT_VARIANT, &var); }
    CATCH_ALL(e) {
      e->Delete();
      continue;
    }
    END_CATCH_ALL
    if (pProp->m_pDSC->SetField(pProp, &var)) pProp->m_pDSC->UpdateControls(pProp);
  }
}

static void UnbindProperty(COleControlSite* pSite, DISPID dispid) {
  CDataBoundProperty** pp = &pSite->m_pBindings;
  while (*pp != NULL) {
    CDataBoundProperty* pProp = *pp;
    if (pProp->m_dispid == dispid) {
      *pp = pProp->m_pNext;
      pProp->m_pDSC->Remove(pProp);
      delete pProp;
    } else {
      pp = &pProp->m_pNext;
    }
  }
}

static void AFXAPI BindRelease(COleControlSite* pSite) {
  while (pSite->m_pBindings != NULL) UnbindProperty(pSite, pSite->m_pBindings->m_dispid);
  delete pSite->m_pDataSourceControl;
  pSite->m_pDataSourceControl = NULL;
}

// A change to a cursor: once its current row moved or changed, the bound
// controls are set again (but not for the change this side made).
static HRESULT AFXAPI BindDBEvent(COleControlSite* pSite, int nState, DWORD dwEventWhat, ULONG,
                                  DBNOTIFYREASON*) {
  CDataSourceControl* pDSC = pSite->m_pDataSourceControl;
  if (pDSC == NULL || pDSC->m_pCursorMove == NULL || pDSC->m_bUpdating) return S_OK;
  if (nState == dscSyncAfter &&
      (dwEventWhat & (DBEVENT_CURRENT_ROW_CHANGED | DBEVENT_CURRENT_ROW_DATA_CHANGED)))
    pDSC->UpdateControls();
  return S_OK;
}

static const _AFX_OCC_BIND_HOOKS s_bindHooks = {BindChanged, BindRelease, BindDBEvent};

static CDataSourceControl* GetDSC(CWnd* pDSCWnd) {
  if (pDSCWnd == NULL || pDSCWnd->m_pCtrlSite == NULL) return NULL;
  COleControlSite* pDSCSite = pDSCWnd->m_pCtrlSite;
  if (pDSCSite->m_pDataSourceControl == NULL) {
    _afxOccBindHooks = &s_bindHooks;
    CDataSourceControl* pDSC = new CDataSourceControl(pDSCSite);
    if (!pDSC->Initialize()) {
      delete pDSC;
      return NULL;
    }
    pDSCSite->m_pDataSourceControl = pDSC;
  }
  return pDSCSite->m_pDataSourceControl;
}

static CDataBoundProperty* NewBinding(COleControlSite* pSite, CDataSourceControl* pDSC,
                                      DISPID dispid, VARTYPE vt, BOOL bComplex) {
  CDataBoundProperty* pProp = new CDataBoundProperty;
  pProp->m_pSite = pSite;
  pProp->m_pDSC = pDSC;
  pProp->m_dispid = dispid;
  pProp->m_vt = vt;
  pProp->m_bComplex = bComplex;
  pProp->m_nColumn = 0;
  pProp->m_hAccessor = NULL;
  pProp->m_pNext = pSite->m_pBindings;
  pSite->m_pBindings = pProp;
  pDSC->Add(pProp);
  return pProp;
}

void COleControlSite::BindDefaultProperty(DISPID dwDispID, VARTYPE vtProp, LPCTSTR szFieldName,
                                          CWnd* pDSCWnd) {
  UnbindProperty(this, dwDispID);
  CDataSourceControl* pDSC = GetDSC(pDSCWnd);
  if (pDSC == NULL || m_pObject == NULL) return;
  DBORDINAL nColumn = pDSC->FindColumn(szFieldName);
  HACCESSOR hAccessor = NULL;
  if (nColumn == 0) return;
  if (pDSC->m_pAccessor != NULL && !MakeAccessor(pDSC->m_pAccessor, nColumn, &hAccessor)) return;
  CDataBoundProperty* pProp = NewBinding(this, pDSC, dwDispID, vtProp, FALSE);
  pProp->m_nColumn = nColumn;
  pProp->m_hAccessor = hAccessor;
  pDSC->UpdateControl(pProp);
}

void COleControlSite::BindProperty(DISPID dwDispId, CWnd* pWndDSC) {
  UnbindProperty(this, dwDispId);
  CDataSourceControl* pDSC = GetDSC(pWndDSC);
  if (m_pObject == NULL) return;
  if (pDSC == NULL) {
    if (pWndDSC == NULL) {
      // Unbound: the property holds no data source.
      COleVariant var;
      var.vt = VT_UNKNOWN;
      var.punkVal = NULL;
      SafeSetProperty(dwDispId, VT_VARIANT, &var);
    }
    return;
  }
  CDataBoundProperty* pProp = NewBinding(this, pDSC, dwDispId, VT_UNKNOWN, TRUE);
  pDSC->UpdateControl(pProp);
}

IUnknown* COleControlSite::GetDSCCursor() {
  if (m_pDataSourceControl == NULL) {
    CDataSourceControl* pDSC = m_pWndCtrl != NULL ? GetDSC(m_pWndCtrl) : NULL;
    if (pDSC == NULL) return NULL;
  }
  return m_pDataSourceControl->GetCursor();
}

// ---------------------------------------------------------------------
// CWnd

void CWnd::BindDefaultProperty(DISPID dwDispID, VARTYPE vtProp, LPCTSTR szFieldName,
                               CWnd* pDSCWnd) {
  if (m_pCtrlSite == NULL) AfxThrowNotSupportedException();
  m_pCtrlSite->BindDefaultProperty(dwDispID, vtProp, szFieldName, pDSCWnd);
}

void CWnd::BindProperty(DISPID dwDispId, CWnd* pWndDSC) {
  if (m_pCtrlSite == NULL) AfxThrowNotSupportedException();
  m_pCtrlSite->BindProperty(dwDispId, pWndDSC);
}

IUnknown* CWnd::GetDSCCursor() {
  if (m_pCtrlSite == NULL) AfxThrowNotSupportedException();
  return m_pCtrlSite->GetDSCCursor();
}
