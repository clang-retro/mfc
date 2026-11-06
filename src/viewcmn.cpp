// viewcmn.cpp - CListView and CTreeView, plus the OLE start-up calls from
// afxdisp.h.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxcview.h>
#include <afxdisp.h>

IMPLEMENT_DYNCREATE(CListView, CCtrlView)
IMPLEMENT_DYNCREATE(CTreeView, CCtrlView)

BEGIN_MESSAGE_MAP(CListView, CCtrlView)
  ON_WM_NCDESTROY()
END_MESSAGE_MAP()

BEGIN_MESSAGE_MAP(CTreeView, CCtrlView)
  ON_WM_DESTROY()
END_MESSAGE_MAP()

BOOL CListView::PreCreateWindow(CREATESTRUCT& cs) {
  if (!AfxDeferRegisterClass(AFX_WNDCOMMCTL_LISTVIEW_REG)) AfxDeferRegisterClass(AFX_WNDCOMMCTLS_REG);
  return CCtrlView::PreCreateWindow(cs);
}

void CListView::DrawItem(LPDRAWITEMSTRUCT) {}

BOOL CListView::OnChildNotify(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult) {
  if (message != WM_DRAWITEM) return CCtrlView::OnChildNotify(message, wParam, lParam, pResult);
  DrawItem((LPDRAWITEMSTRUCT)lParam);
  if (pResult != NULL) *pResult = TRUE;
  return TRUE;
}

void CListView::OnNcDestroy() {
  // The image lists belong to the program unless LVS_SHAREIMAGELISTS is
  // off, in which case the control destroys them; forget our wrappers.
  CCtrlView::OnNcDestroy();
}

BOOL CTreeView::PreCreateWindow(CREATESTRUCT& cs) {
  if (!AfxDeferRegisterClass(AFX_WNDCOMMCTL_TREEVIEW_REG)) AfxDeferRegisterClass(AFX_WNDCOMMCTLS_REG);
  return CCtrlView::PreCreateWindow(cs);
}

void CTreeView::OnDestroy() { CCtrlView::OnDestroy(); }

static BOOL g_bOleInit = FALSE;

BOOL AFXAPI AfxOleInit() {
  if (g_bOleInit) return TRUE;
  if (FAILED(::OleInitialize(NULL))) return FALSE;
  g_bOleInit = TRUE;
  return TRUE;
}

void AFXAPI AfxOleTerm(BOOL bJustRevoke) {
  if (bJustRevoke || !g_bOleInit) return;
  ::OleUninitialize();
  g_bOleInit = FALSE;
}

// Control containment is not implemented; programs that ask for it start
// up and get no ActiveX controls.
void AFXAPI AfxEnableControlContainer(COccManager*) {}
