// ctlstock.cpp - the stock property pages (colors, fonts, pictures), and
// their class objects for a property frame that cannot find the system's.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxctl.h>
#include <afxdlgs.h>
#include "ctlimpl.h"

// ---------------------------------------------------------------------
// CStockPropPage

IMPLEMENT_DYNAMIC(CStockPropPage, COlePropertyPage)

CStockPropPage::CStockPropPage(UINT idDlg, UINT idCaption)
    : COlePropertyPage(idDlg, idCaption),
      m_lcid(LOCALE_USER_DEFAULT),
      m_iPropName(-1),
      m_hTemplate(NULL),
      m_nItems(0) {}

CStockPropPage::~CStockPropPage() {
  if (m_hTemplate != NULL) ::GlobalFree(m_hTemplate);
}

// The template, a DLGTEMPLATE with its items, in the system's dialog font.

static void PutWord(CByteArray& a, WORD w) {
  a.Add((BYTE)(w & 0xFF));
  a.Add((BYTE)(w >> 8));
}
static void PutDWord(CByteArray& a, DWORD dw) {
  PutWord(a, LOWORD(dw));
  PutWord(a, HIWORD(dw));
}
static void PutString(CByteArray& a, LPCWSTR lpsz) {
  for (; lpsz != NULL && *lpsz != 0; lpsz++) PutWord(a, (WORD)*lpsz);
  PutWord(a, 0);
}

void CStockPropPage::BeginTemplate(short cx, short cy) {
  m_template.RemoveAll();
  m_nItems = 0;
  PutDWord(m_template, WS_CHILD | DS_SETFONT | DS_CONTROL | DS_3DLOOK);
  PutDWord(m_template, 0);
  PutWord(m_template, 0);  // the item count, set at the end
  PutWord(m_template, 0);
  PutWord(m_template, 0);
  PutWord(m_template, (WORD)cx);
  PutWord(m_template, (WORD)cy);
  PutWord(m_template, 0);  // no menu
  PutWord(m_template, 0);  // the dialog class
  PutWord(m_template, 0);  // no caption
  PutWord(m_template, 8);
  PutString(m_template, L"MS Shell Dlg");
}

void CStockPropPage::AddItem(DWORD dwStyle, short x, short y, short cx, short cy, WORD nID,
                             WORD wClass, LPCWSTR lpszText) {
  while (m_template.GetSize() % 4 != 0) m_template.Add(0);
  PutDWord(m_template, dwStyle | WS_CHILD | WS_VISIBLE);
  PutDWord(m_template, 0);
  PutWord(m_template, (WORD)x);
  PutWord(m_template, (WORD)y);
  PutWord(m_template, (WORD)cx);
  PutWord(m_template, (WORD)cy);
  PutWord(m_template, nID);
  PutWord(m_template, 0xFFFF);
  PutWord(m_template, wClass);
  PutString(m_template, lpszText);
  PutWord(m_template, 0);  // no creation data
  m_nItems++;
}

void CStockPropPage::EndTemplate() {
  m_template[8] = (BYTE)(m_nItems & 0xFF);
  m_template[9] = (BYTE)(m_nItems >> 8);
  if (m_hTemplate != NULL) ::GlobalFree(m_hTemplate);
  m_hTemplate = ::GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)m_template.GetSize());
  if (m_hTemplate != NULL) {
    memcpy(::GlobalLock(m_hTemplate), m_template.GetData(), (size_t)m_template.GetSize());
    ::GlobalUnlock(m_hTemplate);
  }
  m_template.RemoveAll();
  SetDialogResource(m_hTemplate);
}

enum : WORD { kButton = 0x0080, kEdit = 0x0081, kStatic = 0x0082, kCombo = 0x0085 };

// The properties of a kind: in the type information, a property whose
// type is the user-defined type guid, behind nIndirect pointers.

static BOOL TypeMatches(LPTYPEINFO pti, const TYPEDESC* pDesc, REFGUID guid, int nIndirect) {
  for (int i = 0; i < nIndirect; i++) {
    if (pDesc->vt != VT_PTR || pDesc->lptdesc == NULL) return FALSE;
    pDesc = pDesc->lptdesc;
  }
  if (pDesc->vt != VT_USERDEFINED) return FALSE;
  LPTYPEINFO pRef = NULL;
  if (FAILED(pti->GetRefTypeInfo(pDesc->hreftype, &pRef)) || pRef == NULL) return FALSE;
  BOOL bMatch = FALSE;
  TYPEATTR* pAttr = NULL;
  if (SUCCEEDED(pRef->GetTypeAttr(&pAttr)) && pAttr != NULL) {
    bMatch = IsEqualGUID(pAttr->guid, guid);
    // An alias of the type (a typedef of OLE_COLOR, say).
    if (!bMatch && pAttr->typekind == TKIND_ALIAS)
      bMatch = TypeMatches(pRef, &pAttr->tdescAlias, guid, 0);
    pRef->ReleaseTypeAttr(pAttr);
  }
  pRef->Release();
  return bMatch;
}

static void AddProp(CComboBox& combo, LPCTSTR lpszName, DISPID dispid) {
  if (combo.FindStringExact(-1, lpszName) >= 0) return;
  int i = combo.AddString(lpszName);
  if (i >= 0) combo.SetItemData(i, (DWORD_PTR)dispid);
}

static void AddFromTypeInfo(LPTYPEINFO pti, REFGUID guid, int nIndirect, CComboBox& combo) {
  TYPEATTR* pAttr = NULL;
  if (FAILED(pti->GetTypeAttr(&pAttr)) || pAttr == NULL) return;
  for (UINT i = 0; i < pAttr->cVars; i++) {
    VARDESC* pVar = NULL;
    if (FAILED(pti->GetVarDesc(i, &pVar)) || pVar == NULL) continue;
    if (TypeMatches(pti, &pVar->elemdescVar.tdesc, guid, nIndirect)) {
      BSTR bstr = NULL;
      if (SUCCEEDED(pti->GetDocumentation(pVar->memid, &bstr, NULL, NULL, NULL)) && bstr != NULL)
        AddProp(combo, CString(bstr), pVar->memid);
      ::SysFreeString(bstr);
    }
    pti->ReleaseVarDesc(pVar);
  }
  for (UINT i = 0; i < pAttr->cFuncs; i++) {
    FUNCDESC* pFunc = NULL;
    if (FAILED(pti->GetFuncDesc(i, &pFunc)) || pFunc == NULL) continue;
    if (pFunc->invkind == INVOKE_PROPERTYGET && pFunc->cParams == 0 &&
        TypeMatches(pti, &pFunc->elemdescFunc.tdesc, guid, nIndirect)) {
      BSTR bstr = NULL;
      if (SUCCEEDED(pti->GetDocumentation(pFunc->memid, &bstr, NULL, NULL, NULL)) &&
          bstr != NULL)
        AddProp(combo, CString(bstr), pFunc->memid);
      ::SysFreeString(bstr);
    }
    pti->ReleaseFuncDesc(pFunc);
  }
  pti->ReleaseTypeAttr(pAttr);
}

// Without type information: the stock names whose values are of the kind.
static BOOL ValueMatches(const VARIANT& var, REFGUID guid) {
  if (IsEqualGUID(guid, GUID_COLOR))
    return var.vt == VT_I4 || var.vt == VT_UI4 || var.vt == VT_I2 || var.vt == VT_UI2;
  if (var.vt != VT_DISPATCH && var.vt != VT_UNKNOWN) return FALSE;
  if (var.punkVal == NULL) return IsEqualGUID(guid, IID_IPictureDisp);
  LPUNKNOWN pUnk = NULL;
  REFIID iid = IsEqualGUID(guid, IID_IFontDisp) ? IID_IFont : IID_IPicture;
  if (FAILED(var.punkVal->QueryInterface(iid, (void**)&pUnk)) || pUnk == NULL) return FALSE;
  pUnk->Release();
  return TRUE;
}

void CStockPropPage::FillPropnameList(REFGUID guid, int nIndirect, CComboBox& combo) {
  combo.ResetContent();
  m_iPropName = -1;
  m_strPropName.Empty();
  ULONG nObjects = 0;
  LPDISPATCH* ppDisp = GetObjectArray(&nObjects);
  if (nObjects == 0 || ppDisp == NULL || ppDisp[0] == NULL) return;
  LPDISPATCH pDisp = ppDisp[0];
  LPTYPEINFO pti = NULL;
  UINT nInfo = 0;
  if (SUCCEEDED(pDisp->GetTypeInfoCount(&nInfo)) && nInfo != 0 &&
      SUCCEEDED(pDisp->GetTypeInfo(0, m_lcid, &pti)) && pti != NULL) {
    AddFromTypeInfo(pti, guid, nIndirect, combo);
    pti->Release();
  }
  if (combo.GetCount() == 0) {
    static LPCTSTR const kColors[] = {_T("BackColor"), _T("ForeColor"), _T("BorderColor"),
                                      _T("FillColor"), _T("MaskColor"), NULL};
    static LPCTSTR const kFonts[] = {_T("Font"), NULL};
    static LPCTSTR const kPictures[] = {_T("Picture"), _T("MouseIcon"), _T("Icon"), NULL};
    LPCTSTR const* pNames = IsEqualGUID(guid, GUID_COLOR)    ? kColors
                            : IsEqualGUID(guid, IID_IFontDisp) ? kFonts
                                                               : kPictures;
    for (; *pNames != NULL; pNames++) {
      VARIANT var;
      ::VariantInit(&var);
      CStringW strName(*pNames);
      LPOLESTR pszName = (LPOLESTR)(LPCWSTR)strName;
      DISPID dispid;
      if (SUCCEEDED(pDisp->GetIDsOfNames(IID_NULL, &pszName, 1, m_lcid, &dispid)) &&
          GetPropValue(pDisp, *pNames, &var) && ValueMatches(var, guid))
        AddProp(combo, *pNames, dispid);
      ::VariantClear(&var);
    }
  }
  if (combo.GetCount() > 0) {
    combo.SetCurSel(0);
    OnSelchangePropname(combo);
  }
}

void CStockPropPage::OnSelchangePropname(CComboBox& combo) {
  m_iPropName = combo.GetCurSel();
  if (m_iPropName >= 0)
    combo.GetLBText(m_iPropName, m_strPropName);
  else
    m_strPropName.Empty();
}

BOOL CStockPropPage::OnEditProperty(DISPID dispid, CComboBox& combo) {
  for (int i = 0; i < combo.GetCount(); i++) {
    if ((DISPID)combo.GetItemData(i) == dispid) {
      combo.SetCurSel(i);
      OnSelchangePropname(combo);
      return TRUE;
    }
  }
  return FALSE;
}

BOOL CStockPropPage::OnEditProperty(DISPID dispid) { return COlePropertyPage::OnEditProperty(dispid); }

void CStockPropPage::OnObjectsChanged() { COlePropertyPage::OnObjectsChanged(); }

static BOOL DispIDOf(LPDISPATCH pDisp, LPCTSTR lpszProp, LCID lcid, DISPID* pDispID) {
  CStringW strName(lpszProp);
  LPOLESTR pszName = (LPOLESTR)(LPCWSTR)strName;
  return SUCCEEDED(pDisp->GetIDsOfNames(IID_NULL, &pszName, 1, lcid, pDispID));
}

BOOL CStockPropPage::GetPropValue(LPDISPATCH pDisp, LPCTSTR lpszProp, VARIANT* pvar) {
  DISPID dispid;
  if (pDisp == NULL || !DispIDOf(pDisp, lpszProp, m_lcid, &dispid)) return FALSE;
  DISPPARAMS params = {NULL, NULL, 0, 0};
  return SUCCEEDED(pDisp->Invoke(dispid, IID_NULL, m_lcid, DISPATCH_PROPERTYGET, &params, pvar,
                                 NULL, NULL));
}

BOOL CStockPropPage::SetPropValue(LPCTSTR lpszProp, VARIANT* pvar, BOOL bRef) {
  ULONG nObjects = 0;
  LPDISPATCH* ppDisp = GetObjectArray(&nObjects);
  BOOL bOK = TRUE;
  for (ULONG i = 0; i < nObjects; i++) {
    DISPID dispid;
    if (ppDisp[i] == NULL || !DispIDOf(ppDisp[i], lpszProp, m_lcid, &dispid)) continue;
    DISPID dispidPut = DISPID_PROPERTYPUT;
    DISPPARAMS params = {pvar, &dispidPut, 1, 1};
    HRESULT hr = ppDisp[i]->Invoke(dispid, IID_NULL, m_lcid,
                                   bRef ? DISPATCH_PROPERTYPUTREF : DISPATCH_PROPERTYPUT, &params,
                                   NULL, NULL, NULL);
    // An object that takes the picture only by value.
    if (FAILED(hr) && bRef)
      hr = ppDisp[i]->Invoke(dispid, IID_NULL, m_lcid, DISPATCH_PROPERTYPUT, &params, NULL, NULL,
                             NULL);
    if (FAILED(hr)) bOK = FALSE;
  }
  return bOK;
}

// ---------------------------------------------------------------------
// CColorPropPage: a property's color, one of sixteen or a system color.

IMPLEMENT_DYNCREATE(CColorPropPage, CStockPropPage)

BEGIN_MESSAGE_MAP(CColorPropPage, CStockPropPage)
  ON_WM_DRAWITEM()
END_MESSAGE_MAP()

static const COLORREF kColors[16] = {
    RGB(0, 0, 0),       RGB(128, 0, 0),   RGB(0, 128, 0),   RGB(128, 128, 0),
    RGB(0, 0, 128),     RGB(128, 0, 128), RGB(0, 128, 128), RGB(192, 192, 192),
    RGB(128, 128, 128), RGB(255, 0, 0),   RGB(0, 255, 0),   RGB(255, 255, 0),
    RGB(0, 0, 255),     RGB(255, 0, 255), RGB(0, 255, 255), RGB(255, 255, 255)};

static LPCTSTR const kSysColors[] = {
    _T("Scroll Bars"),         _T("Desktop"),
    _T("Active Title Bar"),    _T("Inactive Title Bar"),
    _T("Menu Bar"),            _T("Window Background"),
    _T("Window Frame"),        _T("Menu Text"),
    _T("Window Text"),         _T("Active Title Bar Text"),
    _T("Active Border"),       _T("Inactive Border"),
    _T("Application Workspace"), _T("Highlight"),
    _T("Highlighted Text"),    _T("Button Face"),
    _T("Button Shadow"),       _T("Disabled Text"),
    _T("Button Text"),         _T("Inactive Title Bar Text"),
    _T("Button Highlight")};

CColorPropPage::CColorPropPage() : CStockPropPage(0, 0), m_color(0), m_bHaveColor(FALSE) {
  SetPageName(_T("Colors"));
  BeginTemplate(250, 110);
  AddItem(SS_LEFT, 8, 8, 100, 9, (WORD)-1, kStatic, L"Property &Name:");
  AddItem(CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP | WS_GROUP, 8, 19, 100, 80, IDC_PROPNAME,
          kCombo, NULL);
  AddItem(SS_LEFT, 8, 38, 100, 9, (WORD)-1, kStatic, L"System &Color:");
  AddItem(CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 8, 49, 100, 100, IDC_SYSCOLORS, kCombo,
          NULL);
  AddItem(SS_LEFT, 120, 8, 100, 9, (WORD)-1, kStatic, L"Color &Palette:");
  for (int i = 0; i < 16; i++)
    AddItem(BS_OWNERDRAW | (i == 0 ? WS_TABSTOP | WS_GROUP : 0), (short)(120 + (i % 4) * 26),
            (short)(19 + (i / 4) * 20), 22, 16, (WORD)(IDC_COLOR0 + i), kButton, NULL);
  EndTemplate();
}

CColorPropPage::~CColorPropPage() {}

BOOL CColorPropPage::OnInitDialog() {
  CComboBox* pSys = (CComboBox*)GetDlgItem(IDC_SYSCOLORS);
  if (pSys != NULL) {
    for (int i = 0; i < (int)_countof(kSysColors); i++) {
      int n = pSys->AddString(kSysColors[i]);
      if (n >= 0) pSys->SetItemData(n, (DWORD_PTR)i);
    }
  }
  CComboBox* pProps = (CComboBox*)GetDlgItem(IDC_PROPNAME);
  if (pProps != NULL) FillPropnameList(GUID_COLOR, 0, *pProps);
  return CStockPropPage::OnInitDialog();
}

void CColorPropPage::SetColor(OLE_COLOR color) {
  m_color = color;
  m_bHaveColor = TRUE;
  if (!m_strPropName.IsEmpty() && !m_bLoading) {
    m_pending.SetAt(m_strPropName, (void*)(UINT_PTR)color);
    SetModifiedFlag(TRUE);
  }
  CComboBox* pSys = (CComboBox*)GetDlgItem(IDC_SYSCOLORS);
  if (pSys != NULL) {
    int nSel = -1;
    if ((color & 0xFF000000) == 0x80000000) {
      for (int i = 0; i < pSys->GetCount(); i++)
        if ((OLE_COLOR)pSys->GetItemData(i) == (color & 0xFF)) nSel = i;
    }
    pSys->SetCurSel(nSel);
  }
  for (int i = 0; i < 16; i++) {
    CWnd* pButton = GetDlgItem(IDC_COLOR0 + i);
    if (pButton != NULL) pButton->Invalidate();
  }
}

// The selected property's color: a change not yet applied, or the first
// object's.
void CColorPropPage::ShowProperty() {
  void* pv = NULL;
  OLE_COLOR color = 0;
  BOOL bHave = FALSE;
  if (!m_strPropName.IsEmpty() && m_pending.Lookup(m_strPropName, pv)) {
    color = (OLE_COLOR)(UINT_PTR)pv;
    bHave = TRUE;
  } else if (!m_strPropName.IsEmpty()) {
    ULONG nObjects = 0;
    LPDISPATCH* ppDisp = GetObjectArray(&nObjects);
    VARIANT var;
    ::VariantInit(&var);
    if (nObjects != 0 && GetPropValue(ppDisp[0], m_strPropName, &var) &&
        SUCCEEDED(::VariantChangeType(&var, &var, 0, VT_I4))) {
      color = (OLE_COLOR)var.lVal;
      bHave = TRUE;
    }
    ::VariantClear(&var);
  }
  BOOL bLoading = m_bLoading;
  m_bLoading = TRUE;  // not a change
  SetColor(color);
  m_bHaveColor = bHave;
  m_bLoading = bLoading;
}

void CColorPropPage::DoDataExchange(CDataExchange* pDX) {
  if (!pDX->m_bSaveAndValidate) {
    CComboBox* pProps = (CComboBox*)GetDlgItem(IDC_PROPNAME);
    if (pProps != NULL && pProps->GetCount() == 0) FillPropnameList(GUID_COLOR, 0, *pProps);
    m_pending.RemoveAll();
    ShowProperty();
    return;
  }
  // Every changed property, on every object.
  POSITION pos = m_pending.GetStartPosition();
  while (pos != NULL) {
    CString strProp;
    void* pv;
    m_pending.GetNextAssoc(pos, strProp, pv);
    VARIANT var;
    var.vt = VT_I4;
    var.lVal = (long)(OLE_COLOR)(UINT_PTR)pv;
    if (!SetPropValue(strProp, &var)) TRACE(_T("color page: %s not set\n"), (LPCTSTR)strProp);
  }
  m_pending.RemoveAll();
}

BOOL CColorPropPage::OnEditProperty(DISPID dispid) {
  CComboBox* pProps = (CComboBox*)GetDlgItem(IDC_PROPNAME);
  if (pProps == NULL || !CStockPropPage::OnEditProperty(dispid, *pProps)) return FALSE;
  ShowProperty();
  return TRUE;
}

BOOL CColorPropPage::OnCommand(WPARAM wParam, LPARAM lParam) {
  UINT nID = LOWORD(wParam), nCode = HIWORD(wParam);
  if (nID == IDC_PROPNAME && nCode == CBN_SELCHANGE) {
    OnSelchangePropname(*(CComboBox*)GetDlgItem(IDC_PROPNAME));
    ShowProperty();
    return TRUE;  // choosing a property changes nothing
  }
  if (!m_bLoading && !m_strPropName.IsEmpty()) {
    if (nID >= IDC_COLOR0 && nID < IDC_COLOR0 + 16 && nCode == BN_CLICKED) {
      SetColor(kColors[nID - IDC_COLOR0]);
      return TRUE;
    }
    if (nID == IDC_SYSCOLORS && nCode == CBN_SELCHANGE) {
      CComboBox* pSys = (CComboBox*)GetDlgItem(IDC_SYSCOLORS);
      int nSel = pSys->GetCurSel();
      if (nSel >= 0) {
        SetColor(0x80000000 | (OLE_COLOR)pSys->GetItemData(nSel));
      }
      return TRUE;
    }
  }
  return CStockPropPage::OnCommand(wParam, lParam);
}

// A color button, and a frame around the one that is the color.
void CColorPropPage::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDis) {
  if (nIDCtl < IDC_COLOR0 || nIDCtl >= IDC_COLOR0 + 16) {
    Default();
    return;
  }
  CDC* pDC = CDC::FromHandle(lpDis->hDC);
  CRect rect(lpDis->rcItem);
  COLORREF clr = kColors[nIDCtl - IDC_COLOR0];
  BOOL bChosen = m_bHaveColor && (m_color & 0xFF000000) == 0 && clr == (COLORREF)m_color;
  pDC->FillSolidRect(rect, ::GetSysColor(COLOR_BTNFACE));
  pDC->DrawEdge(rect, (lpDis->itemState & ODS_SELECTED) ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
  rect.DeflateRect(4, 3);
  pDC->FillSolidRect(rect, clr);
  if (bChosen) {
    CRect rcFrame(lpDis->rcItem);
    rcFrame.DeflateRect(1, 1);
    pDC->DrawFocusRect(rcFrame);
    rcFrame.DeflateRect(1, 1);
    pDC->DrawFocusRect(rcFrame);
  } else if (lpDis->itemState & ODS_FOCUS) {
    CRect rcFrame(lpDis->rcItem);
    rcFrame.DeflateRect(2, 2);
    pDC->DrawFocusRect(rcFrame);
  }
}

// ---------------------------------------------------------------------
// CFontPropPage: a property's font object: name, style, size, effects.

IMPLEMENT_DYNCREATE(CFontPropPage, CStockPropPage)

static const int kSizes[] = {8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72};

CFontPropPage::CFontPropPage() : CStockPropPage(0, 0), m_bHaveFont(FALSE), m_bFilling(FALSE) {
  m_state.cySize.int64 = 0;
  m_state.bBold = m_state.bItalic = m_state.bUnderline = m_state.bStrikethrough = FALSE;
  SetPageName(_T("Fonts"));
  BeginTemplate(250, 110);
  AddItem(SS_LEFT, 8, 8, 64, 9, (WORD)-1, kStatic, L"Property &Name:");
  AddItem(CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP | WS_GROUP, 8, 19, 64, 80, IDC_PROPNAME,
          kCombo, NULL);
  AddItem(SS_LEFT, 78, 8, 72, 9, (WORD)-1, kStatic, L"&Font:");
  AddItem(CBS_DROPDOWN | CBS_SORT | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP, 78, 19, 72, 100,
          IDC_FONTNAME, kCombo, NULL);
  AddItem(SS_LEFT, 156, 8, 56, 9, (WORD)-1, kStatic, L"Font St&yle:");
  AddItem(CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 156, 19, 56, 80, IDC_STYLE, kCombo, NULL);
  AddItem(SS_LEFT, 218, 8, 28, 9, (WORD)-1, kStatic, L"&Size:");
  AddItem(CBS_DROPDOWN | WS_VSCROLL | WS_TABSTOP, 218, 19, 28, 100, IDC_FONTSIZE, kCombo, NULL);
  AddItem(BS_GROUPBOX, 8, 40, 64, 40, (WORD)-1, kButton, L"Effects");
  AddItem(BS_AUTOCHECKBOX | WS_TABSTOP, 14, 52, 54, 10, IDC_STRIKEOUT, kButton, L"Stri&keout");
  AddItem(BS_AUTOCHECKBOX | WS_TABSTOP, 14, 64, 54, 10, IDC_UNDERLINE, kButton, L"&Underline");
  AddItem(BS_GROUPBOX, 78, 40, 168, 60, (WORD)-1, kButton, L"Sample");
  AddItem(SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX, 84, 52, 156, 42, IDC_SAMPLE, kStatic,
          L"AaBbYyZz");
  EndTemplate();
}

CFontPropPage::~CFontPropPage() { ClearPending(); }

void CFontPropPage::ClearPending() {
  POSITION pos = m_pending.GetStartPosition();
  while (pos != NULL) {
    CString strProp;
    void* pv;
    m_pending.GetNextAssoc(pos, strProp, pv);
    delete (FONTSTATE*)pv;
  }
  m_pending.RemoveAll();
}

// The selected property's change, made or replaced.
static void SetPending(CMapStringToPtr& map, const CString& strProp,
                       const CFontPropPage::FONTSTATE& state) {
  void* pv = NULL;
  if (map.Lookup(strProp, pv))
    *(CFontPropPage::FONTSTATE*)pv = state;
  else
    map.SetAt(strProp, new CFontPropPage::FONTSTATE(state));
}

static int CALLBACK EnumFace(const LOGFONT* plf, const TEXTMETRIC*, DWORD, LPARAM lParam) {
  CComboBox* pCombo = (CComboBox*)lParam;
  if (plf->lfFaceName[0] != _T('@') && pCombo->FindStringExact(-1, plf->lfFaceName) < 0)
    pCombo->AddString(plf->lfFaceName);
  return 1;
}

BOOL CFontPropPage::OnInitDialog() {
  m_bFilling = TRUE;
  CComboBox* pNames = (CComboBox*)GetDlgItem(IDC_FONTNAME);
  if (pNames != NULL) {
    HDC hdc = ::GetDC(NULL);
    LOGFONT lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    ::EnumFontFamiliesEx(hdc, &lf, (FONTENUMPROC)EnumFace, (LPARAM)pNames, 0);
    ::ReleaseDC(NULL, hdc);
  }
  CComboBox* pStyle = (CComboBox*)GetDlgItem(IDC_STYLE);
  if (pStyle != NULL) {
    pStyle->AddString(_T("Regular"));
    pStyle->AddString(_T("Italic"));
    pStyle->AddString(_T("Bold"));
    pStyle->AddString(_T("Bold Italic"));
  }
  CComboBox* pSize = (CComboBox*)GetDlgItem(IDC_FONTSIZE);
  if (pSize != NULL) {
    for (int i = 0; i < (int)_countof(kSizes); i++) {
      CString str;
      str.Format(_T("%d"), kSizes[i]);
      pSize->AddString(str);
    }
  }
  CComboBox* pProps = (CComboBox*)GetDlgItem(IDC_PROPNAME);
  if (pProps != NULL) FillPropnameList(IID_IFontDisp, 1, *pProps);
  m_bFilling = FALSE;
  return CStockPropPage::OnInitDialog();
}

static BOOL ReadFont(LPFONT pFont, CFontPropPage::FONTSTATE& state) {
  BSTR bstr = NULL;
  if (FAILED(pFont->get_Name(&bstr))) return FALSE;
  state.strName = bstr;
  ::SysFreeString(bstr);
  state.cySize.int64 = 0;
  pFont->get_Size(&state.cySize);
  BOOL b = FALSE;
  state.bBold = SUCCEEDED(pFont->get_Bold(&b)) && b;
  b = FALSE;
  state.bItalic = SUCCEEDED(pFont->get_Italic(&b)) && b;
  b = FALSE;
  state.bUnderline = SUCCEEDED(pFont->get_Underline(&b)) && b;
  b = FALSE;
  state.bStrikethrough = SUCCEEDED(pFont->get_Strikethrough(&b)) && b;
  return TRUE;
}

static LPFONT FontOf(const VARIANT& var) {
  LPFONT pFont = NULL;
  if ((var.vt == VT_DISPATCH || var.vt == VT_UNKNOWN) && var.punkVal != NULL &&
      FAILED(var.punkVal->QueryInterface(IID_IFont, (void**)&pFont)))
    pFont = NULL;
  return pFont;
}

void CFontPropPage::SetFontState(const FONTSTATE& state) {
  m_state = state;
  m_bHaveFont = TRUE;
  if (!m_strPropName.IsEmpty() && !m_bLoading) {
    SetPending(m_pending, m_strPropName, m_state);
    SetModifiedFlag(TRUE);
  }
  ShowState();
}

void CFontPropPage::ShowState() {
  BOOL bFilling = m_bFilling;
  m_bFilling = TRUE;
  BOOL bLoading = m_bLoading;
  m_bLoading = TRUE;
  CComboBox* pNames = (CComboBox*)GetDlgItem(IDC_FONTNAME);
  if (pNames != NULL) {
    int i = pNames->FindStringExact(-1, m_state.strName);
    if (i >= 0)
      pNames->SetCurSel(i);
    else
      pNames->SetWindowText(m_state.strName);
  }
  CComboBox* pStyle = (CComboBox*)GetDlgItem(IDC_STYLE);
  if (pStyle != NULL)
    pStyle->SetCurSel(m_bHaveFont ? (m_state.bBold ? 2 : 0) + (m_state.bItalic ? 1 : 0) : -1);
  CComboBox* pSize = (CComboBox*)GetDlgItem(IDC_FONTSIZE);
  if (pSize != NULL) {
    CString str;
    if (m_bHaveFont) {
      // CY is in ten-thousandths of a point.
      LONGLONG n = m_state.cySize.int64;
      if (n % 10000 == 0)
        str.Format(_T("%d"), (int)(n / 10000));
      else
        str.Format(_T("%d.%02d"), (int)(n / 10000), (int)((n % 10000) / 100));
    }
    int i = pSize->FindStringExact(-1, str);
    if (i >= 0)
      pSize->SetCurSel(i);
    else
      pSize->SetWindowText(str);
  }
  CheckDlgButton(IDC_STRIKEOUT, m_state.bStrikethrough ? BST_CHECKED : BST_UNCHECKED);
  CheckDlgButton(IDC_UNDERLINE, m_state.bUnderline ? BST_CHECKED : BST_UNCHECKED);
  m_bLoading = bLoading;
  m_bFilling = bFilling;
  UpdateSample();
}

// What the controls show, into m_state.
void CFontPropPage::ReadControls() {
  CComboBox* pNames = (CComboBox*)GetDlgItem(IDC_FONTNAME);
  if (pNames != NULL) {
    int i = pNames->GetCurSel();
    if (i >= 0 && ::GetFocus() != pNames->m_hWnd)
      pNames->GetLBText(i, m_state.strName);
    else
      pNames->GetWindowText(m_state.strName);
  }
  CComboBox* pStyle = (CComboBox*)GetDlgItem(IDC_STYLE);
  int nStyle = pStyle != NULL ? pStyle->GetCurSel() : -1;
  if (nStyle >= 0) {
    m_state.bBold = (nStyle & 2) != 0;
    m_state.bItalic = (nStyle & 1) != 0;
  }
  CComboBox* pSize = (CComboBox*)GetDlgItem(IDC_FONTSIZE);
  if (pSize != NULL) {
    CString str;
    int i = pSize->GetCurSel();
    if (i >= 0)
      pSize->GetLBText(i, str);
    else
      pSize->GetWindowText(str);
    double d = _tcstod(str, NULL);
    if (d > 0) m_state.cySize.int64 = (LONGLONG)(d * 10000 + 0.5);
  }
  m_state.bStrikethrough = IsDlgButtonChecked(IDC_STRIKEOUT) == BST_CHECKED;
  m_state.bUnderline = IsDlgButtonChecked(IDC_UNDERLINE) == BST_CHECKED;
}

void CFontPropPage::UpdateSample() {
  CWnd* pSample = GetDlgItem(IDC_SAMPLE);
  if (pSample == NULL) return;
  pSample->SetFont(GetFont(), FALSE);
  m_fontSample.DeleteObject();
  if (m_bHaveFont) {
    LOGFONT lf;
    memset(&lf, 0, sizeof(lf));
    CClientDC dc(this);
    lf.lfHeight = -(LONG)(m_state.cySize.int64 * dc.GetDeviceCaps(LOGPIXELSY) / (72 * 10000));
    lf.lfWeight = m_state.bBold ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = (BYTE)m_state.bItalic;
    lf.lfUnderline = (BYTE)m_state.bUnderline;
    lf.lfStrikeOut = (BYTE)m_state.bStrikethrough;
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpyn(lf.lfFaceName, m_state.strName, LF_FACESIZE);
    if (m_fontSample.CreateFontIndirect(&lf)) pSample->SetFont(&m_fontSample, FALSE);
  }
  pSample->Invalidate();
}

void CFontPropPage::ShowProperty() {
  FONTSTATE state = m_state;
  BOOL bHave = FALSE;
  void* pv = NULL;
  if (!m_strPropName.IsEmpty() && m_pending.Lookup(m_strPropName, pv)) {
    state = *(FONTSTATE*)pv;
    bHave = TRUE;
  } else if (!m_strPropName.IsEmpty()) {
    ULONG nObjects = 0;
    LPDISPATCH* ppDisp = GetObjectArray(&nObjects);
    VARIANT var;
    ::VariantInit(&var);
    if (nObjects != 0 && GetPropValue(ppDisp[0], m_strPropName, &var)) {
      LPFONT pFont = FontOf(var);
      if (pFont != NULL) {
        bHave = ReadFont(pFont, state);
        pFont->Release();
      }
    }
    ::VariantClear(&var);
  }
  m_state = state;
  m_bHaveFont = bHave;
  ShowState();
}

void CFontPropPage::DoDataExchange(CDataExchange* pDX) {
  if (!pDX->m_bSaveAndValidate) {
    CComboBox* pProps = (CComboBox*)GetDlgItem(IDC_PROPNAME);
    if (pProps != NULL && pProps->GetCount() == 0) FillPropnameList(IID_IFontDisp, 1, *pProps);
    ClearPending();
    ShowProperty();
    return;
  }
  // Each object's font object takes the changes in place.
  ULONG nObjects = 0;
  LPDISPATCH* ppDisp = GetObjectArray(&nObjects);
  POSITION pos = m_pending.GetStartPosition();
  while (pos != NULL) {
    CString strProp;
    void* pv;
    m_pending.GetNextAssoc(pos, strProp, pv);
    const FONTSTATE& state = *(FONTSTATE*)pv;
    for (ULONG i = 0; i < nObjects; i++) {
      VARIANT var;
      ::VariantInit(&var);
      if (!GetPropValue(ppDisp[i], strProp, &var)) continue;
      LPFONT pFont = FontOf(var);
      ::VariantClear(&var);
      if (pFont == NULL) continue;
      if (!state.strName.IsEmpty()) {
        BSTR bstr = state.strName.AllocSysString();
        pFont->put_Name(bstr);
        ::SysFreeString(bstr);
      }
      if (state.cySize.int64 > 0) pFont->put_Size(state.cySize);
      pFont->put_Bold(state.bBold);
      pFont->put_Italic(state.bItalic);
      pFont->put_Underline(state.bUnderline);
      pFont->put_Strikethrough(state.bStrikethrough);
      pFont->Release();
    }
  }
  ClearPending();
}

BOOL CFontPropPage::OnEditProperty(DISPID dispid) {
  CComboBox* pProps = (CComboBox*)GetDlgItem(IDC_PROPNAME);
  if (pProps == NULL || !CStockPropPage::OnEditProperty(dispid, *pProps)) return FALSE;
  ShowProperty();
  return TRUE;
}

BOOL CFontPropPage::OnCommand(WPARAM wParam, LPARAM lParam) {
  UINT nID = LOWORD(wParam), nCode = HIWORD(wParam);
  if (nID == IDC_PROPNAME && nCode == CBN_SELCHANGE) {
    OnSelchangePropname(*(CComboBox*)GetDlgItem(IDC_PROPNAME));
    ShowProperty();
    return TRUE;
  }
  BOOL bChange = FALSE;
  switch (nID) {
    case IDC_FONTNAME:
    case IDC_FONTSIZE: bChange = nCode == CBN_SELCHANGE || nCode == CBN_EDITCHANGE; break;
    case IDC_STYLE: bChange = nCode == CBN_SELCHANGE; break;
    case IDC_STRIKEOUT:
    case IDC_UNDERLINE: bChange = nCode == BN_CLICKED; break;
  }
  if (bChange && !m_bFilling && !m_bLoading && m_bHaveFont && !m_strPropName.IsEmpty()) {
    // A selection in a drop-down list is not in its edit box yet.
    if (nCode == CBN_SELCHANGE && (nID == IDC_FONTNAME || nID == IDC_FONTSIZE)) {
      CComboBox* pCombo = (CComboBox*)GetDlgItem(nID);
      int i = pCombo->GetCurSel();
      if (i >= 0) {
        CString str;
        pCombo->GetLBText(i, str);
        if (nID == IDC_FONTNAME)
          m_state.strName = str;
        else
          m_state.cySize.int64 = (LONGLONG)(_tcstod(str, NULL) * 10000 + 0.5);
      }
      CString strName = m_state.strName;
      CY cySize = m_state.cySize;
      ReadControls();
      if (nID == IDC_FONTNAME)
        m_state.strName = strName;
      else
        m_state.cySize = cySize;
    } else {
      ReadControls();
    }
    SetPending(m_pending, m_strPropName, m_state);
    UpdateSample();
    SetModifiedFlag(TRUE);
    CStockPropPage::OnCommand(wParam, lParam);
    return TRUE;
  }
  return CStockPropPage::OnCommand(wParam, lParam);
}

// ---------------------------------------------------------------------
// CPicturePropPage: a property's picture, from a file, or none.

IMPLEMENT_DYNCREATE(CPicturePropPage, CStockPropPage)

BEGIN_MESSAGE_MAP(CPicturePropPage, CStockPropPage)
  ON_WM_DRAWITEM()
END_MESSAGE_MAP()

CPicturePropPage::CPicturePropPage() : CStockPropPage(0, 0), m_pPict(NULL), m_bHavePicture(FALSE) {
  SetPageName(_T("Pictures"));
  BeginTemplate(250, 110);
  AddItem(SS_LEFT, 8, 8, 100, 9, (WORD)-1, kStatic, L"Property &Name:");
  AddItem(CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP | WS_GROUP, 8, 19, 100, 80, IDC_PROPNAME,
          kCombo, NULL);
  AddItem(BS_PUSHBUTTON | WS_TABSTOP, 8, 40, 50, 14, IDC_BROWSE, kButton, L"&Browse...");
  AddItem(BS_PUSHBUTTON | WS_TABSTOP, 8, 58, 50, 14, IDC_CLEAR, kButton, L"&Clear");
  AddItem(SS_LEFT, 120, 8, 100, 9, (WORD)-1, kStatic, L"Preview:");
  AddItem(SS_OWNERDRAW, 120, 19, 120, 84, IDC_PICTURE, kStatic, NULL);
  EndTemplate();
}

CPicturePropPage::~CPicturePropPage() {
  ClearPending();
  if (m_pPict != NULL) m_pPict->Release();
}

void CPicturePropPage::ClearPending() {
  POSITION pos = m_pending.GetStartPosition();
  while (pos != NULL) {
    CString strProp;
    void* pv;
    m_pending.GetNextAssoc(pos, strProp, pv);
    if (pv != NULL) ((LPPICTUREDISP)pv)->Release();
  }
  m_pending.RemoveAll();
}

BOOL CPicturePropPage::OnInitDialog() {
  CComboBox* pProps = (CComboBox*)GetDlgItem(IDC_PROPNAME);
  if (pProps != NULL) FillPropnameList(IID_IPictureDisp, 1, *pProps);
  return CStockPropPage::OnInitDialog();
}

void CPicturePropPage::SetPicture(LPPICTUREDISP pPict, BOOL bChanged) {
  if (pPict != NULL) pPict->AddRef();
  if (m_pPict != NULL) m_pPict->Release();
  m_pPict = pPict;
  m_bHavePicture = TRUE;
  if (bChanged && !m_strPropName.IsEmpty()) {
    void* pv = NULL;
    if (m_pending.Lookup(m_strPropName, pv) && pv != NULL) ((LPPICTUREDISP)pv)->Release();
    if (pPict != NULL) pPict->AddRef();
    m_pending.SetAt(m_strPropName, pPict);
    SetModifiedFlag(TRUE);
  }
  CWnd* pPreview = GetDlgItem(IDC_PICTURE);
  if (pPreview != NULL) pPreview->Invalidate();
}

BOOL CPicturePropPage::LoadPictureFile(LPCTSTR lpszPath) {
  if (m_strPropName.IsEmpty()) return FALSE;
  CFile file;
  if (!file.Open(lpszPath, CFile::modeRead | CFile::shareDenyWrite)) return FALSE;
  ULONGLONG nLength = file.GetLength();
  if (nLength == 0 || nLength > 0x7FFFFFFF) return FALSE;
  HGLOBAL hGlobal = ::GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)nLength);
  if (hGlobal == NULL) return FALSE;
  UINT nRead = file.Read(::GlobalLock(hGlobal), (UINT)nLength);
  ::GlobalUnlock(hGlobal);
  file.Close();
  LPSTREAM pStm = NULL;
  if (nRead != (UINT)nLength || FAILED(::CreateStreamOnHGlobal(hGlobal, TRUE, &pStm))) {
    ::GlobalFree(hGlobal);
    return FALSE;
  }
  LPPICTUREDISP pPict = NULL;
  HRESULT hr = ::OleLoadPicture(pStm, (LONG)nLength, FALSE, IID_IPictureDisp, (void**)&pPict);
  pStm->Release();
  if (FAILED(hr) || pPict == NULL) return FALSE;
  SetPicture(pPict, TRUE);
  pPict->Release();
  return TRUE;
}

void CPicturePropPage::ClearPicture() {
  if (m_strPropName.IsEmpty()) return;
  CPictureHolder empty;
  empty.CreateEmpty();
  LPPICTUREDISP pPict = empty.GetPictureDispatch();
  SetPicture(pPict, TRUE);
  if (pPict != NULL) pPict->Release();
}

void CPicturePropPage::ShowProperty() {
  void* pv = NULL;
  LPPICTUREDISP pPict = NULL;
  BOOL bHave = FALSE;
  if (!m_strPropName.IsEmpty() && m_pending.Lookup(m_strPropName, pv)) {
    pPict = (LPPICTUREDISP)pv;
    if (pPict != NULL) pPict->AddRef();
    bHave = TRUE;
  } else if (!m_strPropName.IsEmpty()) {
    ULONG nObjects = 0;
    LPDISPATCH* ppDisp = GetObjectArray(&nObjects);
    VARIANT var;
    ::VariantInit(&var);
    if (nObjects != 0 && GetPropValue(ppDisp[0], m_strPropName, &var)) {
      bHave = TRUE;
      if ((var.vt == VT_DISPATCH || var.vt == VT_UNKNOWN) && var.punkVal != NULL &&
          FAILED(var.punkVal->QueryInterface(IID_IPictureDisp, (void**)&pPict)))
        pPict = NULL;
    }
    ::VariantClear(&var);
  }
  SetPicture(pPict, FALSE);
  m_bHavePicture = bHave;
  if (pPict != NULL) pPict->Release();
}

void CPicturePropPage::DoDataExchange(CDataExchange* pDX) {
  if (!pDX->m_bSaveAndValidate) {
    CComboBox* pProps = (CComboBox*)GetDlgItem(IDC_PROPNAME);
    if (pProps != NULL && pProps->GetCount() == 0) FillPropnameList(IID_IPictureDisp, 1, *pProps);
    ClearPending();
    ShowProperty();
    return;
  }
  POSITION pos = m_pending.GetStartPosition();
  while (pos != NULL) {
    CString strProp;
    void* pv;
    m_pending.GetNextAssoc(pos, strProp, pv);
    VARIANT var;
    var.vt = VT_DISPATCH;
    var.pdispVal = (LPDISPATCH)(LPPICTUREDISP)pv;
    SetPropValue(strProp, &var, TRUE);
  }
  ClearPending();
}

BOOL CPicturePropPage::OnEditProperty(DISPID dispid) {
  CComboBox* pProps = (CComboBox*)GetDlgItem(IDC_PROPNAME);
  if (pProps == NULL || !CStockPropPage::OnEditProperty(dispid, *pProps)) return FALSE;
  ShowProperty();
  return TRUE;
}

BOOL CPicturePropPage::OnCommand(WPARAM wParam, LPARAM lParam) {
  UINT nID = LOWORD(wParam), nCode = HIWORD(wParam);
  if (nID == IDC_PROPNAME && nCode == CBN_SELCHANGE) {
    OnSelchangePropname(*(CComboBox*)GetDlgItem(IDC_PROPNAME));
    ShowProperty();
    return TRUE;
  }
  if (nID == IDC_BROWSE && nCode == BN_CLICKED) {
    CFileDialog dlg(TRUE, NULL, NULL, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                    _T("All Picture Types|*.bmp;*.dib;*.ico;*.cur;*.wmf;*.emf;*.gif;*.jpg|")
                    _T("Bitmaps (*.bmp;*.dib)|*.bmp;*.dib|Icons (*.ico;*.cur)|*.ico;*.cur|")
                    _T("Metafiles (*.wmf;*.emf)|*.wmf;*.emf|All Files (*.*)|*.*||"),
                    this);
    if (dlg.DoModal() == IDOK && !LoadPictureFile(dlg.GetPathName()))
      AfxMessageBox(_T("The file is not a picture."), MB_ICONEXCLAMATION);
    return TRUE;
  }
  if (nID == IDC_CLEAR && nCode == BN_CLICKED) {
    ClearPicture();
    return TRUE;
  }
  return CStockPropPage::OnCommand(wParam, lParam);
}

void CPicturePropPage::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDis) {
  if (nIDCtl != IDC_PICTURE) {
    Default();
    return;
  }
  CDC* pDC = CDC::FromHandle(lpDis->hDC);
  CRect rect(lpDis->rcItem);
  pDC->FillSolidRect(rect, ::GetSysColor(COLOR_WINDOW));
  pDC->DrawEdge(rect, EDGE_SUNKEN, BF_RECT);
  rect.DeflateRect(2, 2);
  LPPICTURE pPict = NULL;
  if (m_pPict == NULL || FAILED(m_pPict->QueryInterface(IID_IPicture, (void**)&pPict))) return;
  OLE_XSIZE_HIMETRIC hmWidth = 0;
  OLE_YSIZE_HIMETRIC hmHeight = 0;
  pPict->get_Width(&hmWidth);
  pPict->get_Height(&hmHeight);
  if (hmWidth > 0 && hmHeight > 0) {
    // In proportion, centered, no larger than its own size.
    int cx = ::MulDiv(hmWidth, pDC->GetDeviceCaps(LOGPIXELSX), 2540);
    int cy = ::MulDiv(hmHeight, pDC->GetDeviceCaps(LOGPIXELSY), 2540);
    if (cx > rect.Width()) {
      cy = ::MulDiv(cy, rect.Width(), cx);
      cx = rect.Width();
    }
    if (cy > rect.Height()) {
      cx = ::MulDiv(cx, rect.Height(), cy);
      cy = rect.Height();
    }
    int x = rect.left + (rect.Width() - cx) / 2, y = rect.top + (rect.Height() - cy) / 2;
    RECT rcBounds = {x, y, x + cx, y + cy};
    pPict->Render(pDC->m_hDC, x, y, cx, cy, 0, hmHeight, hmWidth, -hmHeight, &rcBounds);
  }
  pPict->Release();
}

// ---------------------------------------------------------------------
// Class objects for the stock pages' CLSIDs, registered for this thread
// only while the program's own property frame is up, and only for the
// pages the system does not have.

namespace {
class CStockPageFactory : public IClassFactory {
public:
  explicit CStockPageFactory(CRuntimeClass* pClass) : m_pClass(pClass) {}
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (ppv == NULL) return E_POINTER;
    if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_IClassFactory)) {
      *ppv = static_cast<IClassFactory*>(this);
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  // A static object: its lifetime is the program's.
  STDMETHOD_(ULONG, AddRef)() { return 2; }
  STDMETHOD_(ULONG, Release)() { return 1; }
  STDMETHOD(CreateInstance)(LPUNKNOWN pOuter, REFIID iid, void** ppv) {
    if (ppv == NULL) return E_POINTER;
    *ppv = NULL;
    if (pOuter != NULL) return CLASS_E_NOAGGREGATION;
    CCmdTarget* pPage = (CCmdTarget*)m_pClass->CreateObject();
    if (pPage == NULL) return E_OUTOFMEMORY;
    HRESULT hr = (HRESULT)pPage->ExternalQueryInterface(&iid, ppv);
    pPage->ExternalRelease();  // the creation reference
    return hr;
  }
  STDMETHOD(LockServer)(BOOL bLock) {
    if (bLock)
      AfxOleLockApp();
    else
      AfxOleUnlockApp();
    return S_OK;
  }

private:
  CRuntimeClass* m_pClass;
};
}  // namespace

static CRuntimeClass* StockPageClass(REFCLSID clsid) {
  if (IsEqualCLSID(clsid, CLSID_CColorPropPage)) return RUNTIME_CLASS(CColorPropPage);
  if (IsEqualCLSID(clsid, CLSID_CFontPropPage)) return RUNTIME_CLASS(CFontPropPage);
  if (IsEqualCLSID(clsid, CLSID_CPicturePropPage)) return RUNTIME_CLASS(CPicturePropPage);
  return NULL;
}

int AFXAPI _AfxRegisterStockPropPages(const CLSID* pClsids, ULONG nClsids, DWORD* pCookies) {
  static CStockPageFactory s_color(RUNTIME_CLASS(CColorPropPage));
  static CStockPageFactory s_font(RUNTIME_CLASS(CFontPropPage));
  static CStockPageFactory s_picture(RUNTIME_CLASS(CPicturePropPage));
  int nRegistered = 0;
  for (ULONG i = 0; i < nClsids; i++) {
    pCookies[i] = 0;
    CRuntimeClass* pClass = StockPageClass(pClsids[i]);
    if (pClass == NULL) continue;
    BOOL bSeen = FALSE;
    for (ULONG j = 0; j < i; j++) bSeen |= IsEqualCLSID(pClsids[j], pClsids[i]);
    if (bSeen) continue;
    LPCLASSFACTORY pFactory = NULL;
    if (SUCCEEDED(::CoGetClassObject(pClsids[i], CLSCTX_INPROC_SERVER, NULL, IID_IClassFactory,
                                     (void**)&pFactory))) {
      pFactory->Release();  // the system's page
      continue;
    }
    IClassFactory* pOurs = pClass == RUNTIME_CLASS(CColorPropPage)  ? (IClassFactory*)&s_color
                           : pClass == RUNTIME_CLASS(CFontPropPage) ? (IClassFactory*)&s_font
                                                                    : (IClassFactory*)&s_picture;
    if (SUCCEEDED(::CoRegisterClassObject(pClsids[i], pOurs, CLSCTX_INPROC_SERVER,
                                          REGCLS_MULTIPLEUSE, &pCookies[i])))
      nRegistered++;
    else
      pCookies[i] = 0;
  }
  return nRegistered;
}

void AFXAPI _AfxRevokeStockPropPages(const DWORD* pCookies, ULONG nClsids) {
  for (ULONG i = 0; i < nClsids; i++)
    if (pCookies[i] != 0) ::CoRevokeClassObject(pCookies[i]);
}
