// ctlcore.cpp - COleControl: the control's window, drawing, stock
// properties and events, ambient properties, and in-place activation.
// Its OLE interfaces are in ctlintf.cpp and its persistence in
// ctlprop.cpp.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxctl.h>
#include "ctlimpl.h"

IMPLEMENT_DYNAMIC(COleControl, CWnd)

// ---------------------------------------------------------------------
// Maps

BEGIN_MESSAGE_MAP(COleControl, CWnd)
  ON_WM_PAINT()
  ON_WM_ERASEBKGND()
  ON_WM_LBUTTONDOWN()
  ON_WM_LBUTTONUP()
  ON_WM_LBUTTONDBLCLK()
  ON_WM_RBUTTONDOWN()
  ON_WM_RBUTTONUP()
  ON_WM_RBUTTONDBLCLK()
  ON_WM_MBUTTONDOWN()
  ON_WM_MBUTTONUP()
  ON_WM_MBUTTONDBLCLK()
  ON_WM_MOUSEMOVE()
  ON_WM_KEYDOWN()
  ON_WM_KEYUP()
  ON_WM_CHAR()
  ON_WM_SYSKEYDOWN()
  ON_WM_SYSKEYUP()
  ON_WM_SETFOCUS()
  ON_WM_KILLFOCUS()
  ON_WM_GETDLGCODE()
  ON_WM_MOUSEACTIVATE()
  ON_MESSAGE(WM_SETTEXT, OnSetText)
END_MESSAGE_MAP()

// COleControl's own event map is empty; a control's lists its events.
const AFX_EVENTMAP* COleControl::GetEventMap() const { return GetThisEventMap(); }
const AFX_EVENTMAP* PASCAL COleControl::GetThisEventMap() {
  static const AFX_EVENTMAP_ENTRY entries[] = {{NULL, DISPID_UNKNOWN, NULL}};
  static const AFX_EVENTMAP eventMap = {NULL, &entries[0]};
  return &eventMap;
}

BEGIN_CONNECTION_MAP(COleControl, CWnd)
  CONNECTION_PART(COleControl, IID_IPropertyNotifySink, PropConnPt)
END_CONNECTION_MAP()

BEGIN_INTERFACE_MAP(COleControl, CWnd)
  INTERFACE_PART(COleControl, IID_IOleObject, OleObject)
  INTERFACE_PART(COleControl, IID_IOleInPlaceObject, OleInPlaceObject)
  INTERFACE_PART(COleControl, IID_IOleInPlaceObjectWindowless, OleInPlaceObject)
  INTERFACE_PART(COleControl, IID_IOleWindow, OleInPlaceObject)
  INTERFACE_PART(COleControl, IID_IOleInPlaceActiveObject, OleInPlaceActiveObject)
  INTERFACE_PART(COleControl, IID_IOleControl, OleControl)
  INTERFACE_PART(COleControl, IID_IViewObject, ViewObject)
  INTERFACE_PART(COleControl, IID_IViewObject2, ViewObject)
  INTERFACE_PART(COleControl, IID_IViewObjectEx, ViewObject)
  INTERFACE_PART(COleControl, IID_IPersistStreamInit, PersistStreamInit)
  INTERFACE_PART(COleControl, IID_IPersistStorage, PersistStorage)
  INTERFACE_PART(COleControl, IID_IPersistPropertyBag, PersistPropertyBag)
  INTERFACE_PART(COleControl, IID_IPersist, PersistStreamInit)
  INTERFACE_PART(COleControl, IID_IProvideClassInfo, ProvideClassInfo)
  INTERFACE_PART(COleControl, IID_IProvideClassInfo2, ProvideClassInfo)
  INTERFACE_PART(COleControl, IID_ISpecifyPropertyPages, SpecifyPropertyPages)
  INTERFACE_PART(COleControl, IID_IPerPropertyBrowsing, PerPropertyBrowsing)
  INTERFACE_PART(COleControl, IID_IQuickActivate, QuickActivate)
  INTERFACE_PART(COleControl, IID_IConnectionPointContainer, ConnPtContainer)
END_INTERFACE_MAP()

// ---------------------------------------------------------------------
// Construction

COleControl::COleControl()
    : m_clrBackColor(0x80000000 | COLOR_WINDOW),
      m_clrForeColor(0x80000000 | COLOR_WINDOWTEXT),
      m_sAppearance(0),
      m_sBorderStyle(0),
      m_bEnabled(TRUE),
      m_lReadyState(READYSTATE_COMPLETE),
      m_font(&m_xFontNotification),
      m_cxExtent(0),
      m_cyExtent(0),
      m_bModified(FALSE),
      m_bInitialized(FALSE),
      m_bInPlaceActive(FALSE),
      m_bUIActive(FALSE),
      m_bInPlaceSiteEx(FALSE),
      m_bInPlaceSiteWndless(FALSE),
      m_bOptimizedDraw(FALSE),
      m_pInPlaceSiteWndless(NULL),
      m_nFreezeEvents(0),
      m_bPendingUIActivation(FALSE),
      m_rcPos(0, 0, 0, 0),
      m_rcClip(0, 0, 0, 0),
      m_piidPrimary(NULL),
      m_piidEvents(NULL),
      m_pClientSite(NULL),
      m_pControlSite(NULL),
      m_pInPlaceSite(NULL),
      m_pInPlaceFrame(NULL),
      m_pInPlaceDoc(NULL),
      m_pOleAdviseHolder(NULL),
      m_pAdviseSink(NULL),
      m_dwAdviseFlags(0),
      m_pReflect(NULL) {
  memset(&m_frameInfo, 0, sizeof(m_frameInfo));
  m_frameInfo.cb = sizeof(m_frameInfo);
  EnableAutomation();
  EnableConnections();
  m_xEventConnPt.m_pContainer = this;
  // A new control is 100 by 50 pixels until it says otherwise.
  SetInitialSize(100, 50);
  AfxOleLockApp();
}

COleControl::~COleControl() {
  DestroyControlWindow();
  m_font.ReleaseFont();
  if (m_pAdviseSink != NULL) m_pAdviseSink->Release();
  if (m_pOleAdviseHolder != NULL) m_pOleAdviseHolder->Release();
  m_ambientDispDriver.ReleaseDispatch();
  if (m_pControlSite != NULL) m_pControlSite->Release();
  if (m_pClientSite != NULL) m_pClientSite->Release();
  AfxOleUnlockApp();
}

void COleControl::OnFinalRelease() {
  // The window goes before the object.
  InPlaceDeactivate();
  DestroyControlWindow();
  CCmdTarget::OnFinalRelease();
}

void COleControl::InitializeIIDs(const IID* piidPrimary, const IID* piidEvents) {
  m_piidPrimary = piidPrimary;
  m_piidEvents = piidEvents;
}

BOOL COleControl::GetDispatchIID(IID* pIID) {
  if (m_piidPrimary == NULL) return FALSE;
  *pIID = *m_piidPrimary;
  return TRUE;
}

LPCLSID COleControl::GetPropPageIDs(ULONG& cPropPages) {
  cPropPages = 0;
  return NULL;
}

// ---------------------------------------------------------------------
// The events connection point, whose interface is the control's own.

REFIID COleControl::XEventConnPt::GetIID() {
  COleControl* pCtl = (COleControl*)m_pContainer;
  return pCtl != NULL && pCtl->m_piidEvents != NULL ? *pCtl->m_piidEvents : IID_IDispatch;
}

void COleControl::XEventConnPt::OnAdvise(BOOL) {}

BOOL COleControl::GetExtraConnectionPoints(CPtrArray* pConnPoints) {
  if (m_piidEvents == NULL) return FALSE;
  pConnPoints->Add((LPCONNECTIONPOINT)&m_xEventConnPt.m_xConnPt);
  return TRUE;
}

// ---------------------------------------------------------------------
// The window

namespace {

// The window between the container and a subclassed Windows control: the
// control's notifications to its parent come back to it as OCM_ messages.
class CReflectorWnd : public CWnd {
public:
  COleControl* m_pCtl;
  explicit CReflectorWnd(COleControl* pCtl) : m_pCtl(pCtl) {}
  BOOL Create(const CRect& rect, HWND hWndParent) {
    LPCTSTR lpszClass = AfxRegisterWndClass(0);
    return CreateEx(0, lpszClass, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                    rect, CWnd::FromHandle(hWndParent), 0);
  }
  virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
      case WM_COMMAND:
      case WM_NOTIFY:
      case WM_CTLCOLORMSGBOX:
      case WM_CTLCOLOREDIT:
      case WM_CTLCOLORLISTBOX:
      case WM_CTLCOLORBTN:
      case WM_CTLCOLORDLG:
      case WM_CTLCOLORSCROLLBAR:
      case WM_CTLCOLORSTATIC:
      case WM_DRAWITEM:
      case WM_MEASUREITEM:
      case WM_DELETEITEM:
      case WM_VKEYTOITEM:
      case WM_CHARTOITEM:
      case WM_COMPAREITEM:
      case WM_HSCROLL:
      case WM_VSCROLL:
      case WM_PARENTNOTIFY:
        if (m_pCtl != NULL && m_pCtl->m_hWnd != NULL)
          return m_pCtl->SendMessage(OCM__BASE + message, wParam, lParam);
        break;
      case WM_SIZE:
        if (m_pCtl != NULL && m_pCtl->m_hWnd != NULL)
          m_pCtl->SetWindowPos(NULL, 0, 0, LOWORD(lParam), HIWORD(lParam),
                               SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
        break;
      case WM_SETFOCUS:
        if (m_pCtl != NULL && m_pCtl->m_hWnd != NULL) {
          ::SetFocus(m_pCtl->m_hWnd);
          return 0;
        }
        break;
    }
    return CWnd::WindowProc(message, wParam, lParam);
  }
  virtual void PostNcDestroy() { delete this; }
};

LPCTSTR ControlClassName() {
  return AfxRegisterWndClass(CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW, ::LoadCursor(NULL, IDC_ARROW));
}

}  // namespace

BOOL COleControl::PreCreateWindow(CREATESTRUCT& cs) {
  if (cs.lpszClass == NULL) cs.lpszClass = ControlClassName();
  return TRUE;
}

// A control subclasses a Windows control when its PreCreateWindow names
// a window class of its own.
BOOL COleControl::IsSubclassedControl() {
  CREATESTRUCT cs;
  memset(&cs, 0, sizeof(cs));
  if (!PreCreateWindow(cs) || cs.lpszClass == NULL) return FALSE;
  LPCTSTR lpszDefault = ControlClassName();
  if (IS_INTRESOURCE(cs.lpszClass) || IS_INTRESOURCE(lpszDefault)) return cs.lpszClass != lpszDefault;
  return lstrcmpi(cs.lpszClass, lpszDefault) != 0;
}

CWnd* COleControl::GetOuterWindow() const {
  return m_pReflect != NULL ? m_pReflect : (CWnd*)this;
}

BOOL COleControl::CreateControlWindow(HWND hWndParent, const CRect& rcPos, LPCRECT prcClipped) {
  if (m_hWnd != NULL) {
    CWnd* pOuter = GetOuterWindow();
    if (::GetParent(pOuter->m_hWnd) != hWndParent) ::SetParent(pOuter->m_hWnd, hWndParent);
    pOuter->MoveWindow(rcPos.left, rcPos.top, rcPos.Width(), rcPos.Height());
    pOuter->ShowWindow(SW_SHOWNA);
    return TRUE;
  }
  (void)prcClipped;
  DWORD dwStyle = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS;
  DWORD dwExStyle = 0;
  if (m_sBorderStyle != 0) {
    if (m_sAppearance != 0)
      dwExStyle |= WS_EX_CLIENTEDGE;
    else
      dwStyle |= WS_BORDER;
  }
  if (!m_bEnabled) dwStyle |= WS_DISABLED;
  HWND hWndOwner = hWndParent;
  CRect rect(rcPos);
  if (IsSubclassedControl()) {
    CReflectorWnd* pReflect = new CReflectorWnd(this);
    if (!pReflect->Create(rcPos, hWndParent)) {
      delete pReflect;
      return FALSE;
    }
    m_pReflect = pReflect;
    hWndOwner = pReflect->m_hWnd;
    rect.OffsetRect(-rect.left, -rect.top);
  }
  if (!CreateEx(dwExStyle, NULL, m_strText, dwStyle, rect, CWnd::FromHandle(hWndOwner), 0)) {
    if (m_pReflect != NULL) {
      m_pReflect->DestroyWindow();
      m_pReflect = NULL;
    }
    return FALSE;
  }
  // A subclassed control shows the stock font.
  if (m_pReflect != NULL && m_font.m_pFont != NULL)
    SendMessage(WM_SETFONT, (WPARAM)m_font.GetFontHandle(), FALSE);
  return TRUE;
}

void COleControl::DestroyControlWindow() {
  if (m_hWnd != NULL && ::IsWindow(m_hWnd)) DestroyWindow();
  if (m_pReflect != NULL) {
    CWnd* pReflect = m_pReflect;
    m_pReflect = NULL;
    ((CReflectorWnd*)pReflect)->m_pCtl = NULL;
    if (pReflect->m_hWnd != NULL)
      pReflect->DestroyWindow();  // deletes itself
    else
      delete pReflect;
  }
}

// ---------------------------------------------------------------------
// Drawing

void COleControl::OnDraw(CDC* pDC, const CRect& rcBounds, const CRect&) {
  if (IsSubclassedControl()) DoSuperclassPaint(pDC, rcBounds);
}

void COleControl::OnDrawMetafile(CDC* pDC, const CRect& rcBounds) {
  OnDraw(pDC, rcBounds, rcBounds);
}

// The Windows control draws itself into the DC.
void COleControl::DoSuperclassPaint(CDC* pDC, const CRect& rcBounds) {
  if (m_hWnd == NULL) return;
  CPoint ptOld = pDC->SetViewportOrg(pDC->GetViewportOrg() + rcBounds.TopLeft());
  CWnd::DefWindowProc(WM_PAINT, (WPARAM)pDC->m_hDC, 0);
  pDC->SetViewportOrg(ptOld);
}

void COleControl::OnPaint() {
  CPaintDC dc(this);
  CRect rcClient;
  GetClientRect(&rcClient);
  if (IsSubclassedControl()) {
    // The control paints on the DC from BeginPaint.
    CWnd::DefWindowProc(WM_PAINT, (WPARAM)dc.m_hDC, 0);
    return;
  }
  if (GetControlFlags() & clipPaintDC) dc.IntersectClipRect(rcClient);
  OnDraw(&dc, rcClient, CRect(dc.m_ps.rcPaint));
}

BOOL COleControl::OnEraseBkgnd(CDC* pDC) {
  if (IsSubclassedControl()) return CWnd::OnEraseBkgnd(pDC);
  return TRUE;  // OnDraw paints the background
}

void COleControl::InvalidateControl(LPCRECT lpRect, BOOL bErase) {
  if (m_bInPlaceSiteWndless && m_bInPlaceActive) {
    // The rectangle is the control's; the container's is offset.
    CRect rect;
    if (lpRect != NULL) {
      rect = *lpRect;
      rect.OffsetRect(m_rcPos.TopLeft());
    }
    m_pInPlaceSiteWndless->InvalidateRect(lpRect != NULL ? &rect : NULL, bErase);
  } else if (m_hWnd != NULL && m_bInPlaceActive)
    InvalidateRect(lpRect, bErase);
  else
    SendAdvise(OBJECTCODE_VIEWCHANGED);
  if (m_bInPlaceActive) SendAdvise(OBJECTCODE_DATACHANGED);
}

void COleControl::Refresh() {
  InvalidateControl();
  if (m_hWnd != NULL) UpdateWindow();
}

void COleControl::DoClick() { OnClick(LEFT_BUTTON); }

void COleControl::OnClick(USHORT) { FireClick(); }

// The view advise sink and the OLE advise holder hear of changes.
void COleControl::SendAdvise(UINT uCode) {
  switch (uCode) {
    case OBJECTCODE_VIEWCHANGED:
      if (m_pAdviseSink != NULL) m_pAdviseSink->OnViewChange(DVASPECT_CONTENT, -1);
      break;
    case OBJECTCODE_SAVED:
      if (m_pOleAdviseHolder != NULL) m_pOleAdviseHolder->SendOnSave();
      break;
    case OBJECTCODE_CLOSED:
      if (m_pOleAdviseHolder != NULL) m_pOleAdviseHolder->SendOnClose();
      break;
    case OBJECTCODE_DATACHANGED:
      break;
  }
}

// ---------------------------------------------------------------------
// Size

void COleControl::SetInitialSize(int cx, int cy) {
  SIZEL sizeHm;
  _AfxCtlPixelsToHimetric(cx, cy, &sizeHm);
  m_cxExtent = sizeHm.cx;
  m_cyExtent = sizeHm.cy;
}

void COleControl::GetControlSize(int* pcx, int* pcy) {
  if (m_bInPlaceActive) {
    *pcx = m_rcPos.Width();
    *pcy = m_rcPos.Height();
    return;
  }
  SIZE size;
  _AfxCtlHimetricToPixels(m_cxExtent, m_cyExtent, &size);
  *pcx = size.cx;
  *pcy = size.cy;
}

BOOL COleControl::SetControlSize(int cx, int cy) {
  SIZEL sizeHm;
  _AfxCtlPixelsToHimetric(cx, cy, &sizeHm);
  if (!OnSetExtent(&sizeHm)) return FALSE;
  m_cxExtent = sizeHm.cx;
  m_cyExtent = sizeHm.cy;
  if (m_bInPlaceActive && m_pInPlaceSite != NULL) {
    CRect rect(m_rcPos.TopLeft(), CSize(cx, cy));
    m_pInPlaceSite->OnPosRectChange(&rect);
  } else if (m_pClientSite != NULL) {
    m_pClientSite->RequestNewObjectLayout();
  }
  SetModifiedFlag();
  return TRUE;
}

BOOL COleControl::GetRectInContainer(LPRECT lpRect) {
  if (!m_bInPlaceActive) return FALSE;
  *lpRect = m_rcPos;
  return TRUE;
}

BOOL COleControl::SetRectInContainer(LPCRECT lpRect) {
  if (!m_bInPlaceActive || m_pInPlaceSite == NULL) return FALSE;
  return SUCCEEDED(m_pInPlaceSite->OnPosRectChange(lpRect));
}

BOOL COleControl::OnSetExtent(LPSIZEL) { return TRUE; }

BOOL COleControl::OnSetObjectRects(LPCRECT lprcPosRect, LPCRECT lprcClipRect) {
  m_rcPos = *lprcPosRect;
  if (lprcClipRect != NULL) m_rcClip = *lprcClipRect;
  CWnd* pOuter = GetOuterWindow();
  if (pOuter->m_hWnd != NULL)
    pOuter->SetWindowPos(NULL, m_rcPos.left, m_rcPos.top, m_rcPos.Width(), m_rcPos.Height(),
                         SWP_NOZORDER | SWP_NOACTIVATE);
  SIZEL sizeHm;
  _AfxCtlPixelsToHimetric(m_rcPos.Width(), m_rcPos.Height(), &sizeHm);
  m_cxExtent = sizeHm.cx;
  m_cyExtent = sizeHm.cy;
  return TRUE;
}

// ---------------------------------------------------------------------
// Stock properties

OLE_COLOR PASCAL COleControl::TranslateColor(OLE_COLOR clrColor, HPALETTE hpal) {
  COLORREF cr = RGB(0, 0, 0);
  ::OleTranslateColor(clrColor, hpal, &cr);
  return cr;
}

// The pattern of every stock property's set function.
#define STOCK_SET(dispid, member, value, onChanged) \
  do {                                               \
    if (!BoundPropertyRequestEdit(dispid)) {         \
      SetNotPermitted();                             \
      return;                                        \
    }                                                \
    member = value;                                  \
    SetModifiedFlag();                               \
    onChanged();                                     \
    BoundPropertyChanged(dispid);                    \
  } while (0)

OLE_COLOR COleControl::GetBackColor() { return m_clrBackColor; }
void COleControl::SetBackColor(OLE_COLOR clr) {
  STOCK_SET(DISPID_BACKCOLOR, m_clrBackColor, clr, OnBackColorChanged);
}
OLE_COLOR COleControl::GetForeColor() { return m_clrForeColor; }
void COleControl::SetForeColor(OLE_COLOR clr) {
  STOCK_SET(DISPID_FORECOLOR, m_clrForeColor, clr, OnForeColorChanged);
}
short COleControl::GetAppearance() { return m_sAppearance; }
void COleControl::SetAppearance(short s) {
  if (s != 0 && s != 1) ThrowError(CTL_E_INVALIDPROPERTYVALUE);
  STOCK_SET(DISPID_APPEARANCE, m_sAppearance, s, OnAppearanceChanged);
}
short COleControl::GetBorderStyle() { return m_sBorderStyle; }
void COleControl::SetBorderStyle(short s) {
  if (s != 0 && s != 1) ThrowError(CTL_E_INVALIDPROPERTYVALUE);
  STOCK_SET(DISPID_BORDERSTYLE, m_sBorderStyle, s, OnBorderStyleChanged);
}
BOOL COleControl::GetEnabled() { return m_bEnabled; }
void COleControl::SetEnabled(BOOL b) {
  STOCK_SET(DISPID_ENABLED, m_bEnabled, b != FALSE, OnEnabledChanged);
}

LPFONTDISP COleControl::GetFont() {
  if (m_font.m_pFont == NULL) m_font.InitializeFont(NULL, AmbientFont());
  return m_font.GetFontDispatch();
}

void COleControl::SetFont(LPFONTDISP pFontDisp) {
  if (!BoundPropertyRequestEdit(DISPID_FONT)) {
    SetNotPermitted();
    return;
  }
  m_font.InitializeFont(NULL, pFontDisp);
  SetModifiedFlag();
  OnFontChanged();
  BoundPropertyChanged(DISPID_FONT);
}

CFontHolder& COleControl::InternalGetFont() {
  if (m_font.m_pFont == NULL) {
    LPFONTDISP pAmbient = AmbientFont();
    m_font.InitializeFont(NULL, pAmbient);
    if (pAmbient != NULL) pAmbient->Release();
  }
  return m_font;
}

CFont* COleControl::SelectFontObject(CDC* pDC, CFontHolder& fontHolder) {
  return fontHolder.Select(pDC, 0, 0);
}

CFont* COleControl::SelectStockFont(CDC* pDC) { return SelectFontObject(pDC, InternalGetFont()); }

void COleControl::GetFontTextMetrics(LPTEXTMETRIC lptm, CFontHolder& fontHolder) {
  fontHolder.QueryTextMetrics(lptm);
}

void COleControl::GetStockTextMetrics(LPTEXTMETRIC lptm) {
  GetFontTextMetrics(lptm, InternalGetFont());
}

OLE_HANDLE COleControl::GetHwnd() { return (OLE_HANDLE)(UINT_PTR)m_hWnd; }

BSTR COleControl::GetText() { return InternalGetText().AllocSysString(); }

const CString& COleControl::InternalGetText() {
  // A subclassed edit control keeps the text the user types.
  if (m_hWnd != NULL && IsSubclassedControl()) {
    int n = (int)CWnd::DefWindowProc(WM_GETTEXTLENGTH, 0, 0);
    CWnd::DefWindowProc(WM_GETTEXT, (WPARAM)(n + 1), (LPARAM)m_strText.GetBuffer(n + 1));
    m_strText.ReleaseBuffer();
  }
  return m_strText;
}

void COleControl::SetText(LPCTSTR pszText) {
  if (!BoundPropertyRequestEdit(DISPID_CAPTION) || !BoundPropertyRequestEdit(DISPID_TEXT)) {
    SetNotPermitted();
    return;
  }
  m_strText = pszText != NULL ? pszText : _T("");
  if (m_hWnd != NULL) CWnd::DefWindowProc(WM_SETTEXT, 0, (LPARAM)(LPCTSTR)m_strText);
  SetModifiedFlag();
  OnTextChanged();
  BoundPropertyChanged(DISPID_CAPTION);
  BoundPropertyChanged(DISPID_TEXT);
}

// Text set on the window from outside.
LRESULT COleControl::OnSetText(WPARAM wParam, LPARAM lParam) {
  LRESULT lResult = Default();
  m_strText = lParam != 0 ? (LPCTSTR)lParam : _T("");
  (void)wParam;
  SetModifiedFlag();
  OnTextChanged();
  BoundPropertyChanged(DISPID_CAPTION);
  BoundPropertyChanged(DISPID_TEXT);
  return lResult;
}

long COleControl::GetReadyState() { return m_lReadyState; }

void COleControl::InternalSetReadyState(long lNewState) {
  if (m_lReadyState == lNewState) return;
  m_lReadyState = lNewState;
  FireReadyStateChange();
  BoundPropertyChanged(DISPID_READYSTATE);
}

void COleControl::OnBackColorChanged() { InvalidateControl(); }
void COleControl::OnForeColorChanged() { InvalidateControl(); }
void COleControl::OnTextChanged() { InvalidateControl(); }

void COleControl::OnAppearanceChanged() { OnBorderStyleChanged(); }

void COleControl::OnBorderStyleChanged() {
  if (m_hWnd == NULL) {
    InvalidateControl();
    return;
  }
  DWORD dwStyle = 0, dwExStyle = 0;
  if (m_sBorderStyle != 0) {
    if (m_sAppearance != 0)
      dwExStyle = WS_EX_CLIENTEDGE;
    else
      dwStyle = WS_BORDER;
  }
  CWnd::ModifyStyle(m_hWnd, WS_BORDER, dwStyle, SWP_FRAMECHANGED);
  CWnd::ModifyStyleEx(m_hWnd, WS_EX_CLIENTEDGE, dwExStyle, SWP_FRAMECHANGED);
  InvalidateControl();
}

void COleControl::OnEnabledChanged() {
  if (m_hWnd != NULL) ::EnableWindow(m_hWnd, m_bEnabled);
  InvalidateControl();
}

void COleControl::OnFontChanged() {
  if (m_hWnd != NULL && IsSubclassedControl() && m_font.m_pFont != NULL)
    SendMessage(WM_SETFONT, (WPARAM)m_font.GetFontHandle(), TRUE);
  InvalidateControl();
}

// The stock properties a control has: those in its dispatch map.
DWORD COleControl::GetStockPropMask() const {
  DWORD dwMask = 0;
  for (const AFX_DISPMAP* pMap = ((COleControl*)this)->GetDispatchMap(); pMap != NULL;
       pMap = pMap->pfnGetBaseMap != NULL ? pMap->pfnGetBaseMap() : NULL) {
    for (const AFX_DISPMAP_ENTRY* p = pMap->lpEntries; p->nKind != _AFX_DISP_END;
         p++) {
      switch (p->lDispID) {
        case DISPID_BACKCOLOR: dwMask |= STOCKPROP_BACKCOLOR; break;
        case DISPID_CAPTION: dwMask |= STOCKPROP_CAPTION; break;
        case DISPID_FONT: dwMask |= STOCKPROP_FONT; break;
        case DISPID_FORECOLOR: dwMask |= STOCKPROP_FORECOLOR; break;
        case DISPID_TEXT: dwMask |= STOCKPROP_TEXT; break;
        case DISPID_BORDERSTYLE: dwMask |= STOCKPROP_BORDERSTYLE; break;
        case DISPID_ENABLED: dwMask |= STOCKPROP_ENABLED; break;
        case DISPID_APPEARANCE: dwMask |= STOCKPROP_APPEARANCE; break;
        case DISPID_READYSTATE: dwMask |= STOCKPROP_READYSTATE; break;
      }
    }
  }
  return dwMask;
}

// ---------------------------------------------------------------------
// Property changes

void COleControl::SetModifiedFlag(BOOL bModified) { m_bModified = bModified; }
BOOL COleControl::IsModified() { return m_bModified; }

LPDISPATCH COleControl::GetExtendedControl() {
  LPDISPATCH pDisp = NULL;
  if (m_pControlSite == NULL || FAILED(m_pControlSite->GetExtendedControl(&pDisp))) return NULL;
  return pDisp;
}

void COleControl::BoundPropertyChanged(DISPID dispid) {
  POSITION pos = m_xPropConnPt.GetStartPosition();
  while (pos != NULL) {
    LPPROPERTYNOTIFYSINK pSink = (LPPROPERTYNOTIFYSINK)m_xPropConnPt.GetNextConnection(pos);
    if (pSink != NULL) pSink->OnChanged(dispid);
  }
}

BOOL COleControl::BoundPropertyRequestEdit(DISPID dispid) {
  POSITION pos = m_xPropConnPt.GetStartPosition();
  while (pos != NULL) {
    LPPROPERTYNOTIFYSINK pSink = (LPPROPERTYNOTIFYSINK)m_xPropConnPt.GetNextConnection(pos);
    if (pSink != NULL && pSink->OnRequestEdit(dispid) == S_FALSE) return FALSE;
  }
  return TRUE;
}

void COleControl::SetNotPermitted() { ThrowError(CTL_E_SETNOTPERMITTED); }
void COleControl::SetNotSupported() { ThrowError(CTL_E_SETNOTSUPPORTED); }
void COleControl::GetNotSupported() { ThrowError(CTL_E_GETNOTSUPPORTED); }

void COleControl::ThrowError(SCODE sc, UINT nDescriptionID, UINT nHelpID) {
  CString str;
  if (nDescriptionID != 0) str.LoadString(nDescriptionID);
  ThrowError(sc, str.IsEmpty() ? NULL : (LPCTSTR)str, nHelpID == (UINT)-1 ? 0 : nHelpID);
}

void COleControl::ThrowError(SCODE sc, LPCTSTR pszDescription, UINT nHelpID) {
  COleDispatchException* pException =
      new COleDispatchException(pszDescription, nHelpID, 0);
  pException->m_scError = sc;
  throw pException;
}

// ---------------------------------------------------------------------
// Events

void AFX_CDECL COleControl::FireEvent(DISPID dispid, const BYTE* pbParams, ...) {
  va_list argList;
  va_start(argList, pbParams);
  FireEventV(dispid, pbParams, argList);
  va_end(argList);
}

void COleControl::FireEventV(DISPID dispid, const BYTE* pbParams, va_list argList) {
  if (AreEventsFrozen()) return;
  // A handler may release the control.
  ExternalAddRef();
  const CPtrArray* pConnections = m_xEventConnPt.GetConnections();
  // The sinks as they are now; one may disconnect while it is called.
  CPtrArray sinks;
  if (pConnections != NULL) sinks.Copy(*pConnections);
  for (INT_PTR i = 0; i < sinks.GetSize(); i++) {
    LPDISPATCH pDisp = (LPDISPATCH)sinks[i];
    if (pDisp == NULL) continue;
    pDisp->AddRef();
    COleDispatchDriver driver(pDisp, TRUE);
    va_list args;
    va_copy(args, argList);
    try {
      driver.InvokeHelperV(dispid, DISPATCH_METHOD, VT_EMPTY, NULL, pbParams, args);
    } catch (CException* e) {
      e->Delete();
    }
    va_end(args);
  }
  ExternalRelease();
}

void COleControl::FireClick() { FireEvent(DISPID_CLICK, EVENT_PARAM(VTS_NONE)); }
void COleControl::FireDblClick() { FireEvent(DISPID_DBLCLICK, EVENT_PARAM(VTS_NONE)); }

void COleControl::FireKeyDown(USHORT* pnChar, short nShiftState) {
  FireEvent(DISPID_KEYDOWN, EVENT_PARAM(VTS_PI2 VTS_I2), pnChar, nShiftState);
}
void COleControl::FireKeyPress(USHORT* pnChar) {
  FireEvent(DISPID_KEYPRESS, EVENT_PARAM(VTS_PI2), pnChar);
}
void COleControl::FireKeyUp(USHORT* pnChar, short nShiftState) {
  FireEvent(DISPID_KEYUP, EVENT_PARAM(VTS_PI2 VTS_I2), pnChar, nShiftState);
}

#define MOUSE_PARAMS VTS_I2 VTS_I2 VTS_XPOS_PIXELS VTS_YPOS_PIXELS
void COleControl::FireMouseDown(short nButton, short nShiftState, OLE_XPOS_PIXELS x,
                                OLE_YPOS_PIXELS y) {
  FireEvent(DISPID_MOUSEDOWN, EVENT_PARAM(MOUSE_PARAMS), nButton, nShiftState, x, y);
}
void COleControl::FireMouseMove(short nButton, short nShiftState, OLE_XPOS_PIXELS x,
                                OLE_YPOS_PIXELS y) {
  FireEvent(DISPID_MOUSEMOVE, EVENT_PARAM(MOUSE_PARAMS), nButton, nShiftState, x, y);
}
void COleControl::FireMouseUp(short nButton, short nShiftState, OLE_XPOS_PIXELS x,
                              OLE_YPOS_PIXELS y) {
  FireEvent(DISPID_MOUSEUP, EVENT_PARAM(MOUSE_PARAMS), nButton, nShiftState, x, y);
}

void COleControl::FireError(SCODE scode, LPCTSTR lpszDescription, UINT nHelpID) {
  CString strDescription(lpszDescription != NULL ? lpszDescription : _T(""));
  BSTR bstrDescription = strDescription.AllocSysString();
  BOOL bCancelDisplay = FALSE;
  CString strSource(AfxGetAppName() != NULL ? AfxGetAppName() : _T(""));
  FireEvent(DISPID_ERROREVENT,
            EVENT_PARAM(VTS_I2 VTS_PBSTR VTS_SCODE VTS_BSTR VTS_BSTR VTS_I4 VTS_PBOOL),
            (short)SCODE_CODE(scode), &bstrDescription, scode, (LPCTSTR)strSource, _T(""),
            (long)nHelpID, &bCancelDisplay);
  ::SysFreeString(bstrDescription);
}

void COleControl::FireReadyStateChange() {
  FireEvent(DISPID_READYSTATECHANGE, EVENT_PARAM(VTS_I4), m_lReadyState);
}

void COleControl::OnFreezeEvents(BOOL) {}

DWORD COleControl::GetStockEventMask() const {
  DWORD dwMask = 0;
  for (const AFX_EVENTMAP* pMap = GetEventMap(); pMap != NULL;
       pMap = pMap->pfnGetBaseMap != NULL ? pMap->pfnGetBaseMap() : NULL) {
    for (const AFX_EVENTMAP_ENTRY* p = pMap->lpEntries; p->lpszName != NULL; p++) {
      switch (p->dispid) {
        case DISPID_CLICK: dwMask |= STOCKEVENT_CLICK; break;
        case DISPID_DBLCLICK: dwMask |= STOCKEVENT_DBLCLICK; break;
        case DISPID_KEYDOWN: dwMask |= STOCKEVENT_KEYDOWN; break;
        case DISPID_KEYPRESS: dwMask |= STOCKEVENT_KEYPRESS; break;
        case DISPID_KEYUP: dwMask |= STOCKEVENT_KEYUP; break;
        case DISPID_MOUSEDOWN: dwMask |= STOCKEVENT_MOUSEDOWN; break;
        case DISPID_MOUSEMOVE: dwMask |= STOCKEVENT_MOUSEMOVE; break;
        case DISPID_MOUSEUP: dwMask |= STOCKEVENT_MOUSEUP; break;
        case DISPID_ERROREVENT: dwMask |= STOCKEVENT_ERROR; break;
        case DISPID_READYSTATECHANGE: dwMask |= STOCKEVENT_READYSTATECHANGE; break;
      }
    }
  }
  return dwMask;
}

// ---------------------------------------------------------------------
// Mouse and keyboard: the stock events of the event map

static short ShiftState() {
  short s = 0;
  if (::GetKeyState(VK_SHIFT) < 0) s |= SHIFT_MASK;
  if (::GetKeyState(VK_CONTROL) < 0) s |= CTRL_MASK;
  if (::GetKeyState(VK_MENU) < 0) s |= ALT_MASK;
  return s;
}

static short ButtonState(UINT nFlags) {
  short s = 0;
  if (nFlags & MK_LBUTTON) s |= LEFT_BUTTON;
  if (nFlags & MK_RBUTTON) s |= RIGHT_BUTTON;
  if (nFlags & MK_MBUTTON) s |= MIDDLE_BUTTON;
  return s;
}

void COleControl::ButtonDown(USHORT iButton, UINT, CPoint point) {
  if (GetStockEventMask() & STOCKEVENT_MOUSEDOWN)
    FireMouseDown((short)iButton, ShiftState(), point.x, point.y);
  if (GetCapture() != this) SetCapture();
}

void COleControl::ButtonUp(USHORT iButton, UINT nFlags, CPoint point) {
  if (GetStockEventMask() & STOCKEVENT_MOUSEUP)
    FireMouseUp((short)iButton, ShiftState(), point.x, point.y);
  if (ButtonState(nFlags) == 0 && GetCapture() == this) ReleaseCapture();
  CRect rcClient;
  GetClientRect(&rcClient);
  if (rcClient.PtInRect(point) && (GetStockEventMask() & STOCKEVENT_CLICK)) OnClick(iButton);
}

void COleControl::ButtonDblClk(USHORT iButton, UINT nFlags, CPoint point) {
  ButtonDown(iButton, nFlags, point);
  if (GetStockEventMask() & STOCKEVENT_DBLCLICK) FireDblClick();
}

void COleControl::OnLButtonDown(UINT nFlags, CPoint point) {
  ButtonDown(LEFT_BUTTON, nFlags, point);
  Default();
}
void COleControl::OnLButtonUp(UINT nFlags, CPoint point) {
  Default();
  ButtonUp(LEFT_BUTTON, nFlags, point);
}
void COleControl::OnLButtonDblClk(UINT nFlags, CPoint point) {
  ButtonDblClk(LEFT_BUTTON, nFlags, point);
  Default();
}
void COleControl::OnRButtonDown(UINT nFlags, CPoint point) {
  ButtonDown(RIGHT_BUTTON, nFlags, point);
  Default();
}
void COleControl::OnRButtonUp(UINT nFlags, CPoint point) {
  Default();
  ButtonUp(RIGHT_BUTTON, nFlags, point);
}
void COleControl::OnRButtonDblClk(UINT nFlags, CPoint point) {
  ButtonDblClk(RIGHT_BUTTON, nFlags, point);
  Default();
}
void COleControl::OnMButtonDown(UINT nFlags, CPoint point) {
  ButtonDown(MIDDLE_BUTTON, nFlags, point);
  Default();
}
void COleControl::OnMButtonUp(UINT nFlags, CPoint point) {
  Default();
  ButtonUp(MIDDLE_BUTTON, nFlags, point);
}
void COleControl::OnMButtonDblClk(UINT nFlags, CPoint point) {
  ButtonDblClk(MIDDLE_BUTTON, nFlags, point);
  Default();
}

void COleControl::OnMouseMove(UINT nFlags, CPoint point) {
  if (GetStockEventMask() & STOCKEVENT_MOUSEMOVE)
    FireMouseMove(ButtonState(nFlags), ShiftState(), point.x, point.y);
  Default();
}

// A handler that sets the key to 0 keeps it from the control.
void COleControl::KeyDown(USHORT nChar) {
  USHORT n = nChar;
  if (GetStockEventMask() & STOCKEVENT_KEYDOWN) FireKeyDown(&n, ShiftState());
  if (n != 0) Default();
}

void COleControl::KeyUp(USHORT nChar) {
  USHORT n = nChar;
  if (GetStockEventMask() & STOCKEVENT_KEYUP) FireKeyUp(&n, ShiftState());
  if (n != 0) Default();
}

void COleControl::OnKeyDown(UINT nChar, UINT, UINT) { KeyDown((USHORT)nChar); }
void COleControl::OnKeyUp(UINT nChar, UINT, UINT) { KeyUp((USHORT)nChar); }
void COleControl::OnSysKeyDown(UINT nChar, UINT, UINT) { KeyDown((USHORT)nChar); }
void COleControl::OnSysKeyUp(UINT nChar, UINT, UINT) { KeyUp((USHORT)nChar); }

void COleControl::OnChar(UINT nChar, UINT, UINT) {
  USHORT n = (USHORT)nChar;
  if (GetStockEventMask() & STOCKEVENT_KEYPRESS) FireKeyPress(&n);
  if (n == 0) return;
  if (n != nChar) {
    // The handler changed the character.
    CWnd::DefWindowProc(WM_CHAR, n, 0);
    return;
  }
  Default();
}

void COleControl::OnSetFocus(CWnd* pOldWnd) {
  if (m_bInPlaceActive && !m_bUIActive) OnActivateInPlace(TRUE, NULL);
  if (m_pControlSite != NULL) m_pControlSite->OnFocus(TRUE);
  CWnd::OnSetFocus(pOldWnd);
  Default();
}

void COleControl::OnKillFocus(CWnd* pNewWnd) {
  if (m_pControlSite != NULL) m_pControlSite->OnFocus(FALSE);
  CWnd::OnKillFocus(pNewWnd);
  Default();
}

UINT COleControl::OnGetDlgCode() {
  if (IsSubclassedControl()) return (UINT)Default();
  return DLGC_WANTARROWS | DLGC_WANTCHARS;
}

int COleControl::OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message) {
  if (m_bInPlaceActive && !m_bUIActive) OnActivateInPlace(TRUE, NULL);
  return CWnd::OnMouseActivate(pDesktopWnd, nHitTest, message);
}

BOOL COleControl::PreTranslateMessage(MSG* pMsg) { return CWnd::PreTranslateMessage(pMsg); }

// ---------------------------------------------------------------------
// The container's ambient properties

void COleControl::OnSetClientSite() {}

BOOL COleControl::GetAmbientProperty(DISPID dispid, VARTYPE vtProp, void* pvProp) {
  if (m_ambientDispDriver.m_lpDispatch == NULL) return FALSE;
  try {
    m_ambientDispDriver.GetProperty(dispid, vtProp, pvProp);
    return TRUE;
  } catch (CException* e) {
    e->Delete();
  }
  return FALSE;
}

OLE_COLOR COleControl::AmbientBackColor() {
  long l;
  if (!GetAmbientProperty(DISPID_AMBIENT_BACKCOLOR, VT_I4, &l)) l = (long)::GetSysColor(COLOR_WINDOW);
  return (OLE_COLOR)l;
}

OLE_COLOR COleControl::AmbientForeColor() {
  long l;
  if (!GetAmbientProperty(DISPID_AMBIENT_FORECOLOR, VT_I4, &l))
    l = (long)::GetSysColor(COLOR_WINDOWTEXT);
  return (OLE_COLOR)l;
}

CString COleControl::AmbientDisplayName() {
  CString str;
  if (!GetAmbientProperty(DISPID_AMBIENT_DISPLAYNAME, VT_BSTR, &str)) str.Empty();
  return str;
}

LPFONTDISP COleControl::AmbientFont() {
  LPDISPATCH pDisp = NULL;
  if (!GetAmbientProperty(DISPID_AMBIENT_FONT, VT_DISPATCH, &pDisp) || pDisp == NULL) return NULL;
  LPFONTDISP pFontDisp = NULL;
  if (FAILED(pDisp->QueryInterface(IID_IFontDisp, (void**)&pFontDisp))) pFontDisp = NULL;
  pDisp->Release();
  return pFontDisp;
}

LCID COleControl::AmbientLocaleID() {
  long l;
  if (!GetAmbientProperty(DISPID_AMBIENT_LOCALEID, VT_I4, &l)) l = (long)::GetThreadLocale();
  return (LCID)l;
}

short COleControl::AmbientAppearance() {
  short s;
  if (!GetAmbientProperty(DISPID_AMBIENT_APPEARANCE, VT_I2, &s)) s = 0;
  return s;
}

short COleControl::AmbientTextAlign() {
  short s;
  if (!GetAmbientProperty(DISPID_AMBIENT_TEXTALIGN, VT_I2, &s)) s = 0;
  return s;
}

#define AMBIENT_BOOL(name, dispid, def)                          \
  BOOL COleControl::name() {                                     \
    BOOL b;                                                      \
    if (!GetAmbientProperty(dispid, VT_BOOL, &b)) b = def;       \
    return b;                                                    \
  }
AMBIENT_BOOL(AmbientUserMode, DISPID_AMBIENT_USERMODE, TRUE)
AMBIENT_BOOL(AmbientUIDead, DISPID_AMBIENT_UIDEAD, FALSE)
AMBIENT_BOOL(AmbientShowGrabHandles, DISPID_AMBIENT_SHOWGRABHANDLES, TRUE)
AMBIENT_BOOL(AmbientShowHatching, DISPID_AMBIENT_SHOWHATCHING, TRUE)

CString COleControl::AmbientScaleUnits() {
  CString str;
  if (!GetAmbientProperty(DISPID_AMBIENT_SCALEUNITS, VT_BSTR, &str)) str.Empty();
  return str;
}

void COleControl::OnAmbientPropertyChange(DISPID) {}

BOOL COleControl::LockInPlaceActive(BOOL bLock) {
  return m_pControlSite != NULL && SUCCEEDED(m_pControlSite->LockInPlaceActive(bLock));
}

BOOL COleControl::TransformCoords(POINTL* lpptlHimetric, POINTF* lpptfContainer, DWORD flags) {
  if (m_pControlSite != NULL &&
      SUCCEEDED(m_pControlSite->TransformCoords(lpptlHimetric, lpptfContainer, flags)))
    return TRUE;
  // Without the container's help: HIMETRIC and pixels.
  if (flags & XFORMCOORDS_HIMETRICTOCONTAINER) {
    SIZE size;
    _AfxCtlHimetricToPixels(lpptlHimetric->x, lpptlHimetric->y, &size);
    lpptfContainer->x = (float)size.cx;
    lpptfContainer->y = (float)size.cy;
  } else {
    SIZEL sizeHm;
    _AfxCtlPixelsToHimetric((int)lpptfContainer->x, (int)lpptfContainer->y, &sizeHm);
    lpptlHimetric->x = sizeHm.cx;
    lpptlHimetric->y = sizeHm.cy;
  }
  return TRUE;
}

void COleControl::PreModalDialog(HWND) {
  if (m_pInPlaceFrame != NULL) m_pInPlaceFrame->EnableModeless(FALSE);
}

void COleControl::PostModalDialog(HWND) {
  if (m_pInPlaceFrame != NULL) m_pInPlaceFrame->EnableModeless(TRUE);
}

// ---------------------------------------------------------------------
// Activation

HRESULT COleControl::OnActivateInPlace(BOOL bUIActivate, LPMSG) {
  if (m_bInPlaceActive && (!bUIActivate || m_bUIActive)) return S_OK;
  if (m_pClientSite == NULL) return E_UNEXPECTED;
  if (!m_bInPlaceActive) {
    BOOL bRedraw = TRUE;
    LPOLEINPLACESITE pSite = NULL;
    if (FAILED(m_pClientSite->QueryInterface(IID_IOleInPlaceSite, (void**)&pSite)))
      return E_NOINTERFACE;
    if (pSite->CanInPlaceActivate() != S_OK) {
      pSite->Release();
      return E_FAIL;
    }
    m_pInPlaceSite = pSite;
    // Windowless, when the control and the container both can.
    m_bInPlaceSiteWndless = FALSE;
    m_bInPlaceSiteEx = FALSE;
    if (GetControlFlags() & windowlessActivate) {
      LPOLEINPLACESITEWINDOWLESS pWndless = NULL;
      if (SUCCEEDED(pSite->QueryInterface(IID_IOleInPlaceSiteWindowless, (void**)&pWndless))) {
        if (pWndless->CanWindowlessActivate() == S_OK) {
          m_pInPlaceSiteWndless = pWndless;
          m_bInPlaceSiteWndless = TRUE;
        } else {
          pWndless->Release();
        }
      }
    }
    LPOLEINPLACESITEEX pSiteEx = NULL;
    if (SUCCEEDED(pSite->QueryInterface(IID_IOleInPlaceSiteEx, (void**)&pSiteEx))) {
      m_bInPlaceSiteEx = TRUE;
      BOOL bNoRedraw = FALSE;
      pSiteEx->OnInPlaceActivateEx(&bNoRedraw, m_bInPlaceSiteWndless ? ACTIVATE_WINDOWLESS : 0);
      pSiteEx->Release();
      // The container keeps what it drew of the inactive control, which
      // looks the same (noFlickerActivate), when it says so.
      bRedraw = !(bNoRedraw && (GetControlFlags() & noFlickerActivate));
    } else {
      m_pInPlaceSite->OnInPlaceActivate();
    }
    HWND hWndParent = NULL;
    m_pInPlaceSite->GetWindow(&hWndParent);
    memset(&m_frameInfo, 0, sizeof(m_frameInfo));
    m_frameInfo.cb = sizeof(m_frameInfo);
    RECT rcPos = {0, 0, 0, 0}, rcClip = {0, 0, 0, 0};
    if (FAILED(m_pInPlaceSite->GetWindowContext(&m_pInPlaceFrame, &m_pInPlaceDoc, &rcPos,
                                                &rcClip, &m_frameInfo))) {
      m_pInPlaceFrame = NULL;
      m_pInPlaceDoc = NULL;
    }
    m_rcPos = rcPos;
    m_rcClip = rcClip;
    if (m_bInPlaceSiteWndless) {
      if (bRedraw) m_pInPlaceSiteWndless->InvalidateRect(&m_rcPos, FALSE);
      // No window: the container's is the control's.
      SIZEL sizeHm;
      _AfxCtlPixelsToHimetric(m_rcPos.Width(), m_rcPos.Height(), &sizeHm);
      m_cxExtent = sizeHm.cx;
      m_cyExtent = sizeHm.cy;
    } else if (!CreateControlWindow(hWndParent, m_rcPos, &m_rcClip)) {
      if (m_pInPlaceFrame != NULL) m_pInPlaceFrame->Release();
      if (m_pInPlaceDoc != NULL) m_pInPlaceDoc->Release();
      m_pInPlaceFrame = NULL;
      m_pInPlaceDoc = NULL;
      m_pInPlaceSite->OnInPlaceDeactivate();
      m_pInPlaceSite->Release();
      m_pInPlaceSite = NULL;
      m_bInPlaceSiteEx = FALSE;
      return E_FAIL;
    }
    m_bInPlaceActive = TRUE;
  }
  if (bUIActivate && !m_bUIActive) {
    m_bUIActive = TRUE;
    m_pInPlaceSite->OnUIActivate();
    LPOLEINPLACEACTIVEOBJECT pActive = &m_xOleInPlaceActiveObject;
    if (m_pInPlaceFrame != NULL) m_pInPlaceFrame->SetActiveObject(pActive, NULL);
    if (m_pInPlaceDoc != NULL) m_pInPlaceDoc->SetActiveObject(pActive, NULL);
    // Controls have no menus or tools of their own.
    if (m_pInPlaceFrame != NULL) m_pInPlaceFrame->SetMenu(NULL, NULL, NULL);
    HWND hWndFocus = ::GetFocus();
    if (m_bInPlaceSiteWndless)
      m_pInPlaceSiteWndless->SetFocus(TRUE);
    else if (m_hWnd != NULL && hWndFocus != m_hWnd && !::IsChild(m_hWnd, hWndFocus))
      ::SetFocus(m_hWnd);
  }
  return S_OK;
}

void COleControl::UIDeactivate() {
  if (!m_bUIActive) return;
  m_bUIActive = FALSE;
  if (m_pInPlaceFrame != NULL) m_pInPlaceFrame->SetActiveObject(NULL, NULL);
  if (m_pInPlaceDoc != NULL) m_pInPlaceDoc->SetActiveObject(NULL, NULL);
  if (m_pInPlaceSite != NULL) m_pInPlaceSite->OnUIDeactivate(FALSE);
}

void COleControl::InPlaceDeactivate() {
  if (!m_bInPlaceActive) return;
  UIDeactivate();
  m_bInPlaceActive = FALSE;
  // The window stays hidden while the control is not in place.
  CWnd* pOuter = GetOuterWindow();
  if (pOuter->m_hWnd != NULL) {
    pOuter->ShowWindow(SW_HIDE);
    DestroyControlWindow();
  }
  if (m_pInPlaceFrame != NULL) m_pInPlaceFrame->Release();
  if (m_pInPlaceDoc != NULL) m_pInPlaceDoc->Release();
  m_pInPlaceFrame = NULL;
  m_pInPlaceDoc = NULL;
  if (m_pInPlaceSite != NULL) {
    LPOLEINPLACESITE pSite = m_pInPlaceSite;
    m_pInPlaceSite = NULL;
    if (m_bInPlaceSiteWndless) {
      // The mouse and the keyboard go back to the container.
      if (m_pInPlaceSiteWndless->GetCapture() == S_OK) m_pInPlaceSiteWndless->SetCapture(FALSE);
      if (m_pInPlaceSiteWndless->GetFocus() == S_OK) m_pInPlaceSiteWndless->SetFocus(FALSE);
      m_pInPlaceSiteWndless->Release();
      m_pInPlaceSiteWndless = NULL;
      m_bInPlaceSiteWndless = FALSE;
    }
    LPOLEINPLACESITEEX pSiteEx = NULL;
    if (m_bInPlaceSiteEx &&
        SUCCEEDED(pSite->QueryInterface(IID_IOleInPlaceSiteEx, (void**)&pSiteEx))) {
      // The container need not draw the control again when it looks the
      // same inactive (noFlickerActivate).
      pSiteEx->OnInPlaceDeactivateEx((GetControlFlags() & noFlickerActivate) != 0);
      pSiteEx->Release();
    } else {
      pSite->OnInPlaceDeactivate();
    }
    m_bInPlaceSiteEx = FALSE;
    pSite->Release();
  }
}

// ---------------------------------------------------------------------
// Windowless activation

DWORD COleControl::GetControlFlags() { return fastBeginPaint | clipPaintDC; }

// ---------------------------------------------------------------------
// The mouse while inactive (IPointerInactive, with pointerInactive)

DWORD COleControl::GetActivationPolicy() { return 0; }
void COleControl::OnInactiveMouseMove(LPCRECT, long, long, DWORD) {}
BOOL COleControl::OnInactiveSetCursor(LPCRECT, long, long, DWORD, BOOL) { return FALSE; }

// ---------------------------------------------------------------------
// The view (IViewObjectEx)

DWORD COleControl::GetViewStatus() { return VIEWSTATUS_OPAQUE; }

BOOL COleControl::OnQueryHitPoint(DWORD dwAspect, LPCRECT pRectBounds, POINT ptlLoc, LONG,
                                  DWORD* pHitResult) {
  if (dwAspect != DVASPECT_CONTENT) return FALSE;
  *pHitResult = ::PtInRect(pRectBounds, ptlLoc) ? HITRESULT_HIT : HITRESULT_OUTSIDE;
  return TRUE;
}

BOOL COleControl::OnQueryHitRect(DWORD dwAspect, LPCRECT pRectBounds, LPCRECT prcLoc, LONG,
                                 DWORD* pHitResult) {
  if (dwAspect != DVASPECT_CONTENT) return FALSE;
  RECT rc;
  *pHitResult = ::IntersectRect(&rc, pRectBounds, prcLoc) ? HITRESULT_HIT : HITRESULT_OUTSIDE;
  return TRUE;
}

BOOL COleControl::OnGetNaturalExtent(DWORD, LONG, DVTARGETDEVICE*, HDC, DVEXTENTINFO*, LPSIZEL) {
  return FALSE;
}

IDropTarget* COleControl::GetWindowlessDropTarget() { return NULL; }

BOOL COleControl::OnWindowlessMessage(UINT msg, WPARAM wParam, LPARAM lParam,
                                      LRESULT* plResult) {
  // The mouse's position, in the control's own coordinates.
  if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && msg != WM_MOUSEWHEEL)
    lParam = MAKELPARAM((short)LOWORD(lParam) - m_rcPos.left, (short)HIWORD(lParam) - m_rcPos.top);
  AFX_THREAD_STATE* pState = AfxGetThreadState();
  MSG msgOld = pState->m_lastSentMsg;
  pState->m_lastSentMsg.hwnd = NULL;
  pState->m_lastSentMsg.message = msg;
  pState->m_lastSentMsg.wParam = wParam;
  pState->m_lastSentMsg.lParam = lParam;
  LRESULT lResult = 0;
  if (!OnWndMsg(msg, wParam, lParam, &lResult)) lResult = DefWindowProc(msg, wParam, lParam);
  pState->m_lastSentMsg = msgOld;
  *plResult = lResult;
  return TRUE;
}

LRESULT COleControl::DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam) {
  if (m_hWnd == NULL && m_bInPlaceSiteWndless) {
    // The container's own handling, except for the focus, which is the
    // control's and not the window's.
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS) return 0;
    LRESULT lResult = 0;
    if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST && message != WM_MOUSEWHEEL)
      lParam = MAKELPARAM((short)LOWORD(lParam) + m_rcPos.left,
                          (short)HIWORD(lParam) + m_rcPos.top);
    m_pInPlaceSiteWndless->OnDefWindowMessage(message, wParam, lParam, &lResult);
    return lResult;
  }
  return CWnd::DefWindowProc(message, wParam, lParam);
}

CWnd* COleControl::SetCapture() {
  if (m_bInPlaceSiteWndless) {
    CWnd* pOld = GetCapture();
    m_pInPlaceSiteWndless->SetCapture(TRUE);
    return pOld;
  }
  return CWnd::SetCapture();
}

BOOL COleControl::ReleaseCapture() {
  if (m_bInPlaceSiteWndless) return SUCCEEDED(m_pInPlaceSiteWndless->SetCapture(FALSE));
  return ::ReleaseCapture();
}

CWnd* COleControl::GetCapture() {
  if (m_bInPlaceSiteWndless) return m_pInPlaceSiteWndless->GetCapture() == S_OK ? this : NULL;
  return CWnd::GetCapture();
}

CWnd* COleControl::SetFocus() {
  if (m_bInPlaceSiteWndless) {
    CWnd* pOld = GetFocus();
    m_pInPlaceSiteWndless->SetFocus(TRUE);
    return pOld;
  }
  return CWnd::SetFocus();
}

CWnd* COleControl::GetFocus() {
  if (m_bInPlaceSiteWndless) return m_pInPlaceSiteWndless->GetFocus() == S_OK ? this : NULL;
  return CWnd::GetFocus();
}

// Windowless, the DC is the container's, with the control's top left at
// the origin.
CDC* COleControl::GetDC(LPCRECT lprcRect, DWORD dwFlags) {
  if (!m_bInPlaceSiteWndless) return CWnd::GetDC();
  CRect rect;
  if (lprcRect != NULL) {
    rect = *lprcRect;
    rect.OffsetRect(m_rcPos.TopLeft());
  }
  HDC hDC = NULL;
  if (FAILED(m_pInPlaceSiteWndless->GetDC(lprcRect != NULL ? &rect : NULL, dwFlags, &hDC)) ||
      hDC == NULL)
    return NULL;
  ::OffsetViewportOrgEx(hDC, m_rcPos.left, m_rcPos.top, NULL);
  return CDC::FromHandle(hDC);
}

int COleControl::ReleaseDC(CDC* pDC) {
  if (pDC == NULL) return 0;
  if (!m_bInPlaceSiteWndless) return CWnd::ReleaseDC(pDC);
  return SUCCEEDED(m_pInPlaceSiteWndless->ReleaseDC(pDC->m_hDC)) ? 1 : 0;
}

void COleControl::GetClientRect(LPRECT lpRect) const {
  if (m_bInPlaceSiteWndless || m_hWnd == NULL) {
    ::SetRect(lpRect, 0, 0, m_rcPos.Width(), m_rcPos.Height());
    return;
  }
  CWnd::GetClientRect(lpRect);
}

void COleControl::InvalidateRgn(CRgn* pRgn, BOOL bErase) {
  if (!m_bInPlaceSiteWndless) {
    if (m_hWnd != NULL) CWnd::InvalidateRgn(pRgn, bErase);
    return;
  }
  if (pRgn == NULL) {
    m_pInPlaceSiteWndless->InvalidateRgn(NULL, bErase);
    return;
  }
  // The region in the container's coordinates.
  HRGN hRgn = ::CreateRectRgn(0, 0, 0, 0);
  ::CombineRgn(hRgn, (HRGN)pRgn->GetSafeHandle(), NULL, RGN_COPY);
  ::OffsetRgn(hRgn, m_rcPos.left, m_rcPos.top);
  m_pInPlaceSiteWndless->InvalidateRgn(hRgn, bErase);
  ::DeleteObject(hRgn);
}

void COleControl::ScrollWindow(int xAmount, int yAmount, LPCRECT lpRect, LPCRECT lpClipRect) {
  if (!m_bInPlaceSiteWndless) {
    if (m_hWnd != NULL) CWnd::ScrollWindow(xAmount, yAmount, lpRect, lpClipRect);
    return;
  }
  CRect rcScroll(lpRect != NULL ? *lpRect : CRect(0, 0, m_rcPos.Width(), m_rcPos.Height()));
  rcScroll.OffsetRect(m_rcPos.TopLeft());
  CRect rcClip;
  if (lpClipRect != NULL) {
    rcClip = *lpClipRect;
    rcClip.OffsetRect(m_rcPos.TopLeft());
  }
  m_pInPlaceSiteWndless->ScrollRect(xAmount, yAmount, &rcScroll,
                                    lpClipRect != NULL ? &rcClip : NULL);
}

void COleControl::ClientToParent(LPCRECT lprcBounds, LPPOINT pPoint) const {
  pPoint->x += lprcBounds->left;
  pPoint->y += lprcBounds->top;
}

void COleControl::ParentToClient(LPCRECT lprcBounds, LPPOINT pPoint, BOOL) const {
  pPoint->x -= lprcBounds->left;
  pPoint->y -= lprcBounds->top;
}

BOOL COleControl::OnEdit(LPMSG lpMsg, HWND, LPCRECT) {
  return SUCCEEDED(OnActivateInPlace(TRUE, lpMsg));
}

// The property pages, in the system's property frame.
BOOL COleControl::OnProperties(LPMSG, HWND hWndParent, LPCRECT) {
  // A container that shows property frames itself does it.
  if (m_pControlSite != NULL && m_pControlSite->ShowPropertyFrame() == S_OK) return TRUE;
  ULONG cPages = 0;
  LPCLSID pPages = GetPropPageIDs(cPages);
  if (cPages == 0 || pPages == NULL) return FALSE;
  LPUNKNOWN pUnk = GetIDispatch(FALSE);
  CString strName = AmbientDisplayName();
  if (strName.IsEmpty()) {
    UINT nID = GetUserTypeNameID();
    if (nID != 0) strName.LoadString(nID);
  }
  CStringW strNameW(strName);
  // The stock pages, where the system has none.
  CDWordArray cookies;
  cookies.SetSize(cPages);
  _AfxRegisterStockPropPages(pPages, cPages, cookies.GetData());
  PreModalDialog(hWndParent);
  HRESULT hr = ::OleCreatePropertyFrame(hWndParent, 0, 0, strNameW, 1, &pUnk, cPages, pPages,
                                        AmbientLocaleID(), 0, NULL);
  PostModalDialog(hWndParent);
  _AfxRevokeStockPropPages(cookies.GetData(), cPages);
  return SUCCEEDED(hr);
}

// Custom verbs are numbered from 1, in the order of the maps from the
// class's own to its bases'.
const AFX_MSGMAP_ENTRY* COleControl::FindVerbEntry(LONG iVerb) {
  LONG nCustom = 0;
  for (const AFX_MSGMAP* pMap = GetMessageMap(); pMap != NULL;
       pMap = pMap->pfnGetBaseMap != NULL ? pMap->pfnGetBaseMap() : NULL) {
    for (const AFX_MSGMAP_ENTRY* p = pMap->lpEntries; p->nSig != AfxSig_end; p++) {
      if (p->nMessage != _AFX_OLEVERB_MSG || p->nSig != AfxSig_thunk) continue;
      if (p->nCode == _AFX_OLEVERB_CUSTOM) {
        if (++nCustom == iVerb) return p;
      } else if (p->nCode == _AFX_OLEVERB_STOCK && (LONG)p->nID == iVerb) {
        return p;
      }
    }
  }
  return NULL;
}

BOOL COleControl::OnDoVerb(LONG iVerb, LPMSG lpMsg, HWND hWndParent, LPCRECT lpRect) {
  const AFX_MSGMAP_ENTRY* pEntry = FindVerbEntry(iVerb);
  if (pEntry != NULL) {
    AFX_OLEVERB_PARAMS params = {lpMsg, hWndParent, lpRect};
    return pEntry->pThunk(this, _AFX_OLEVERB_MSG, (WPARAM)iVerb, (LPARAM)&params) != 0;
  }
  switch (iVerb) {
    case OLEIVERB_PRIMARY:
    case OLEIVERB_SHOW:
    case OLEIVERB_OPEN:
    case OLEIVERB_UIACTIVATE:
      return OnEdit(lpMsg, hWndParent, lpRect);
    case OLEIVERB_INPLACEACTIVATE:
      return SUCCEEDED(OnActivateInPlace(FALSE, lpMsg));
    case OLEIVERB_HIDE:
      UIDeactivate();
      if (GetOuterWindow()->m_hWnd != NULL) GetOuterWindow()->ShowWindow(SW_HIDE);
      return TRUE;
    case OLEIVERB_PROPERTIES:
      return OnProperties(lpMsg, hWndParent, lpRect);
  }
  return FALSE;
}

void COleControl::OnClose(DWORD dwSaveOption) {
  if (m_bModified && m_pClientSite != NULL &&
      (dwSaveOption == OLECLOSE_SAVEIFDIRTY || dwSaveOption == OLECLOSE_PROMPTSAVE))
    m_pClientSite->SaveObject();
  InPlaceDeactivate();
  SendAdvise(OBJECTCODE_CLOSED);
}

void COleControl::OnGetControlInfo(LPCONTROLINFO pControlInfo) {
  pControlInfo->hAccel = NULL;
  pControlInfo->cAccel = 0;
  pControlInfo->dwFlags = 0;
}

void COleControl::OnMnemonic(LPMSG) {}

// ---------------------------------------------------------------------
// Pixels and HIMETRIC

void AFXAPI _AfxCtlPixelsToHimetric(int cx, int cy, SIZEL* pSizeL) {
  HDC hdc = ::GetDC(NULL);
  int nX = ::GetDeviceCaps(hdc, LOGPIXELSX);
  int nY = ::GetDeviceCaps(hdc, LOGPIXELSY);
  ::ReleaseDC(NULL, hdc);
  pSizeL->cx = ::MulDiv(cx, 2540, nX);
  pSizeL->cy = ::MulDiv(cy, 2540, nY);
}

void AFXAPI _AfxCtlHimetricToPixels(long cx, long cy, SIZE* pSize) {
  HDC hdc = ::GetDC(NULL);
  int nX = ::GetDeviceCaps(hdc, LOGPIXELSX);
  int nY = ::GetDeviceCaps(hdc, LOGPIXELSY);
  ::ReleaseDC(NULL, hdc);
  pSize->cx = ::MulDiv(cx, nX, 2540);
  pSize->cy = ::MulDiv(cy, nY, 2540);
}
