// viewform.cpp - CCtrlView (views that are controls) and CFormView
// (views made from a dialog template).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxext.h>

// ---------------------------------------------------------------------
// CCtrlView

IMPLEMENT_DYNAMIC(CCtrlView, CView)

BEGIN_MESSAGE_MAP(CCtrlView, CView)
  ON_WM_PAINT()
END_MESSAGE_MAP()

CCtrlView::CCtrlView(LPCTSTR lpszClass, DWORD dwStyle)
    : m_strClass(lpszClass), m_dwDefaultStyle(dwStyle) {}

BOOL CCtrlView::PreCreateWindow(CREATESTRUCT& cs) {
  cs.lpszClass = m_strClass;
  // Make sure the control's class exists (the common controls register
  // theirs on demand).
  WNDCLASS wndcls;
  if (!::GetClassInfo(NULL, cs.lpszClass, &wndcls)) AfxDeferRegisterClass(AFX_WNDCOMMCTLS_REG);
  cs.style |= m_dwDefaultStyle;
  return CView::PreCreateWindow(cs);
}

void CCtrlView::OnDraw(CDC*) {}

// The control paints itself.
void CCtrlView::OnPaint() { Default(); }

// ---------------------------------------------------------------------
// CFormView

IMPLEMENT_DYNAMIC(CFormView, CScrollView)

BEGIN_MESSAGE_MAP(CFormView, CScrollView)
  ON_WM_CREATE()
  ON_WM_SETFOCUS()
END_MESSAGE_MAP()

CFormView::CFormView(LPCTSTR lpszTemplateName)
    : m_lpszTemplateName(lpszTemplateName), m_pCreateContext(NULL), m_hWndFocus(NULL) {}

CFormView::CFormView(UINT nIDTemplate)
    : m_lpszTemplateName(MAKEINTRESOURCE(nIDTemplate)), m_pCreateContext(NULL),
      m_hWndFocus(NULL) {}

BOOL CFormView::Create(LPCTSTR lpszClassName, LPCTSTR lpszWindowName, DWORD dwRequestedStyle,
                       const RECT& rect, CWnd* pParentWnd, UINT nID, CCreateContext* pContext) {
  (void)lpszClassName;
  (void)lpszWindowName;
  // The window comes from the dialog template; OnCreate picks up the
  // creation context from here.
  m_pCreateContext = pContext;
  BOOL bResult = CreateDlg(m_lpszTemplateName, pParentWnd);
  m_pCreateContext = NULL;
  if (!bResult) return FALSE;

  ExecuteDlgInit(m_lpszTemplateName);
  SetDlgCtrlID(nID);

  // The form scrolls when the view is smaller than the template.
  CRect rectTemplate;
  GetWindowRect(&rectTemplate);
  SetScrollSizes(MM_TEXT, rectTemplate.Size());

  // A requested border becomes the sunken client edge, as for other views.
  if (dwRequestedStyle & WS_BORDER) {
    ModifyStyle(WS_BORDER, 0);
    ModifyStyleEx(0, WS_EX_CLIENTEDGE);
  }
  SetWindowPos(NULL, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
               SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  if (dwRequestedStyle & WS_VISIBLE) ShowWindow(SW_SHOWNORMAL);
  return TRUE;
}

int CFormView::OnCreate(LPCREATESTRUCT lpcs) {
  // A dialog's creation parameters are not the context, so pass it on.
  lpcs->lpCreateParams = m_pCreateContext;
  return CScrollView::OnCreate(lpcs);
}

void CFormView::OnDraw(CDC*) {}

void CFormView::OnInitialUpdate() {
  if (!UpdateData(FALSE)) TRACE(_T("UpdateData failed during form view initial update.\n"));
  CScrollView::OnInitialUpdate();
}

void CFormView::OnSetFocus(CWnd*) {
  if (!::IsWindow(m_hWndFocus) || !::IsChild(m_hWnd, m_hWndFocus)) {
    // Start at the first control that takes the focus.
    m_hWndFocus = NULL;
    HWND hWnd = ::GetNextDlgTabItem(m_hWnd, NULL, FALSE);
    if (hWnd != NULL) m_hWndFocus = hWnd;
  }
  if (m_hWndFocus != NULL) {
    ::SetFocus(m_hWndFocus);
    return;
  }
  Default();
}

BOOL CFormView::SaveFocusControl() {
  HWND hWndFocus = ::GetFocus();
  if (hWndFocus != NULL && ::IsChild(m_hWnd, hWndFocus)) {
    m_hWndFocus = hWndFocus;
    return TRUE;
  }
  return FALSE;
}

void CFormView::OnActivateView(BOOL bActivate, CView* pActivateView, CView* pDeactiveView) {
  if (SaveFocusControl()) return;  // the focus stays on our control
  CScrollView::OnActivateView(bActivate, pActivateView, pDeactiveView);
}

void CFormView::OnActivateFrame(UINT nState, CFrameWnd* pFrameWnd) {
  if (nState == WA_INACTIVE) SaveFocusControl();
  CScrollView::OnActivateFrame(nState, pFrameWnd);
}

BOOL CFormView::PreTranslateMessage(MSG* pMsg) {
  if (CScrollView::PreTranslateMessage(pMsg)) return TRUE;
  // The frames get the keys first, for their accelerators.
  CFrameWnd* pFrame = GetTopLevelFrame();
  if (pFrame != NULL && pFrame->m_bHelpMode) return FALSE;
  pFrame = GetParentFrame();
  while (pFrame != NULL) {
    if (pFrame->PreTranslateMessage(pMsg)) return TRUE;
    pFrame = pFrame->GetParentFrame();
  }
  // Then the dialog keys: tab, arrows, the default button, mnemonics.
  if ((pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST) &&
      (pMsg->hwnd == m_hWnd || ::IsChild(m_hWnd, pMsg->hwnd)))
    return ::IsDialogMessage(m_hWnd, pMsg);
  return FALSE;
}
