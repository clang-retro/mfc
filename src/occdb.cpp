// occdb.cpp - data binding for hosted ActiveX controls: a control's
// property bound to a field of a data source control's current row
// (simple binding, BindDefaultProperty), or given the data source itself
// (BindProperty), for data source controls that are OLE DB data sources
// (the DataSource interface of msdatsrc.h).
//
// The data source control's site keeps a CDataSourceControl: the
// rowset of the data source's default data member, its row position and
// the bound properties. When the current row or a field changes, the
// bound properties are set from their fields; when a bound property
// changes (IPropertyNotifySink::OnChanged), its field is set.
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

// One bound property of a control.
struct CDataBoundProperty {
  CDataBoundProperty* m_pNext;
  COleControlSite* m_pSite;        // the bound control
  CDataSourceControl* m_pDSC;      // its data source
  DISPID m_dispid;
  VARTYPE m_vt;
  BOOL m_bComplex;   // BindProperty: the property holds the data source
  DBORDINAL m_nColumn;
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
        m_bUpdating(FALSE) {}
  ~CDataSourceControl();

  BOOL Initialize();
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
  if (m_pSite->m_pObject == NULL ||
      FAILED(m_pSite->m_pObject->QueryInterface(kIID_DataSource, (void**)&m_pDataSource)))
    return FALSE;
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
}

DBORDINAL CDataSourceControl::FindColumn(LPCTSTR pszName) const {
  if (pszName == NULL) return 0;
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

BOOL CDataSourceControl::GetField(CDataBoundProperty* pProp, VARIANT* pvar) {
  ::VariantInit(pvar);
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
    var.vt = VT_UNKNOWN;
    var.punkVal = m_pDataSource;
    m_pDataSource->AddRef();
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

STDMETHODIMP _AfxDSCSink::OnRowPositionChange(DBREASON, DBEVENTPHASE ePhase, WINBOOL) {
  if (m_pDSC != NULL && ePhase == DBEVENTPHASE_DIDEVENT) m_pDSC->UpdateControls();
  return S_OK;
}
STDMETHODIMP _AfxDSCSink::OnFieldChange(IRowset*, HROW, DBORDINAL, DBORDINAL[], DBREASON,
                                        DBEVENTPHASE ePhase, WINBOOL) {
  // A field this side set has already been updated in the controls.
  if (m_pDSC != NULL && !m_pDSC->m_bUpdating && ePhase == DBEVENTPHASE_DIDEVENT)
    m_pDSC->UpdateControls();
  return S_OK;
}
STDMETHODIMP _AfxDSCSink::OnRowChange(IRowset*, DBCOUNTITEM, const HROW[], DBREASON,
                                      DBEVENTPHASE ePhase, WINBOOL) {
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

static const _AFX_OCC_BIND_HOOKS s_bindHooks = {BindChanged, BindRelease};

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
  if (nColumn == 0 || !MakeAccessor(pDSC->m_pAccessor, nColumn, &hAccessor)) return;
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
  return m_pDataSourceControl->m_pRowset;
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
