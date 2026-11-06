// dockstat.cpp - saving and restoring where a frame's control bars are:
// CDockState, CControlBarInfo, and CFrameWnd's GetDockState,
// SetDockState, SaveBarState and LoadBarState.
//
// In the profile, a state is a summary section, "<name>-Summary", with
// the number of bars, and a section per bar, "<name>-Bar<n>". A dock bar's
// section lists the bars in it, in order, with 0 between rows.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxext.h>
#include <afxpriv.h>

IMPLEMENT_SERIAL(CDockState, CObject, 1)

static const TCHAR szSummary[] = _T("%s-Summary");
static const TCHAR szBarSection[] = _T("%s-Bar%d");

// ---------------------------------------------------------------------
// CControlBarInfo

CControlBarInfo::CControlBarInfo() {
  m_nBarID = 0;
  m_bVisible = m_bFloating = m_bHorz = m_bDockBar = FALSE;
  m_pointPos = CPoint(-1, -1);
  m_nMRUWidth = 32767;
  m_bDocking = FALSE;
  m_uMRUDockID = 0;
  m_rectMRUDockPos.SetRectEmpty();
  m_dwMRUFloatStyle = 0;
  m_ptMRUFloatPos = CPoint(-1, -1);
  m_pBar = NULL;
}

static void _WriteInt(CWinApp* pApp, LPCTSTR lpszSection, LPCTSTR lpszEntry, int n) {
  pApp->WriteProfileInt(lpszSection, lpszEntry, n);
}

// Negative numbers (positions on a monitor left of the main one) do not
// survive GetProfileInt from an .ini file, so positions are written as
// strings.
static void _WriteSigned(CWinApp* pApp, LPCTSTR lpszSection, LPCTSTR lpszEntry, int n) {
  CString str;
  str.Format(_T("%d"), n);
  pApp->WriteProfileString(lpszSection, lpszEntry, str);
}

static int _ReadSigned(CWinApp* pApp, LPCTSTR lpszSection, LPCTSTR lpszEntry, int nDefault) {
  CString str = pApp->GetProfileString(lpszSection, lpszEntry, NULL);
  if (str.IsEmpty()) return nDefault;
  return _ttoi(str);
}

BOOL CControlBarInfo::SaveState(LPCTSTR lpszProfileName, int nIndex) {
  CWinApp* pApp = AfxGetApp();
  CString strSection;
  strSection.Format(szBarSection, lpszProfileName, nIndex);
  // Start from an empty section, so nothing from an older layout stays.
  pApp->WriteProfileString(strSection, NULL, NULL);

  _WriteInt(pApp, strSection, _T("BarID"), (int)m_nBarID);
  _WriteInt(pApp, strSection, _T("Visible"), m_bVisible);
  _WriteInt(pApp, strSection, _T("Horz"), m_bHorz);
  _WriteInt(pApp, strSection, _T("Floating"), m_bFloating);
  _WriteSigned(pApp, strSection, _T("XPos"), m_pointPos.x);
  _WriteSigned(pApp, strSection, _T("YPos"), m_pointPos.y);
  _WriteInt(pApp, strSection, _T("MRUWidth"), (int)m_nMRUWidth);
  if (m_bDocking) {
    _WriteInt(pApp, strSection, _T("Docking"), 1);
    _WriteInt(pApp, strSection, _T("MRUDockID"), (int)m_uMRUDockID);
    _WriteSigned(pApp, strSection, _T("MRUDockLeftPos"), m_rectMRUDockPos.left);
    _WriteSigned(pApp, strSection, _T("MRUDockTopPos"), m_rectMRUDockPos.top);
    _WriteSigned(pApp, strSection, _T("MRUDockRightPos"), m_rectMRUDockPos.right);
    _WriteSigned(pApp, strSection, _T("MRUDockBottomPos"), m_rectMRUDockPos.bottom);
    _WriteInt(pApp, strSection, _T("MRUFloatStyle"), (int)m_dwMRUFloatStyle);
    _WriteSigned(pApp, strSection, _T("MRUFloatXPos"), m_ptMRUFloatPos.x);
    _WriteSigned(pApp, strSection, _T("MRUFloatYPos"), m_ptMRUFloatPos.y);
  }
  if (m_bDockBar) {
    _WriteInt(pApp, strSection, _T("Bars"), (int)m_arrBarID.GetSize());
    for (int i = 0; i < m_arrBarID.GetSize(); i++) {
      CString strEntry;
      strEntry.Format(_T("Bar#%d"), i);
      _WriteInt(pApp, strSection, strEntry, (int)(UINT_PTR)m_arrBarID[i]);
    }
  }
  return TRUE;
}

BOOL CControlBarInfo::LoadState(LPCTSTR lpszProfileName, int nIndex, CDockState* pDockState) {
  CWinApp* pApp = AfxGetApp();
  CString strSection;
  strSection.Format(szBarSection, lpszProfileName, nIndex);

  m_nBarID = pApp->GetProfileInt(strSection, _T("BarID"), 0);
  m_bVisible = pApp->GetProfileInt(strSection, _T("Visible"), TRUE);
  m_bHorz = pApp->GetProfileInt(strSection, _T("Horz"), TRUE);
  m_bFloating = pApp->GetProfileInt(strSection, _T("Floating"), FALSE);
  m_pointPos.x = _ReadSigned(pApp, strSection, _T("XPos"), -1);
  m_pointPos.y = _ReadSigned(pApp, strSection, _T("YPos"), -1);
  if (pDockState != NULL && m_bFloating) pDockState->ScalePoint(m_pointPos);
  m_nMRUWidth = pApp->GetProfileInt(strSection, _T("MRUWidth"), 32767);
  m_bDocking = pApp->GetProfileInt(strSection, _T("Docking"), 0);
  if (m_bDocking) {
    m_uMRUDockID = pApp->GetProfileInt(strSection, _T("MRUDockID"), 0);
    m_rectMRUDockPos.left = _ReadSigned(pApp, strSection, _T("MRUDockLeftPos"), 0);
    m_rectMRUDockPos.top = _ReadSigned(pApp, strSection, _T("MRUDockTopPos"), 0);
    m_rectMRUDockPos.right = _ReadSigned(pApp, strSection, _T("MRUDockRightPos"), 0);
    m_rectMRUDockPos.bottom = _ReadSigned(pApp, strSection, _T("MRUDockBottomPos"), 0);
    m_dwMRUFloatStyle = pApp->GetProfileInt(strSection, _T("MRUFloatStyle"), 0);
    m_ptMRUFloatPos.x = _ReadSigned(pApp, strSection, _T("MRUFloatXPos"), -1);
    m_ptMRUFloatPos.y = _ReadSigned(pApp, strSection, _T("MRUFloatYPos"), -1);
    if (pDockState != NULL) pDockState->ScalePoint(m_ptMRUFloatPos);
  }
  int nBars = (int)pApp->GetProfileInt(strSection, _T("Bars"), 0);
  m_bDockBar = nBars > 0 || (m_nBarID >= AFX_IDW_DOCKBAR_TOP && m_nBarID <= AFX_IDW_DOCKBAR_FLOAT);
  m_arrBarID.RemoveAll();
  for (int i = 0; i < nBars; i++) {
    CString strEntry;
    strEntry.Format(_T("Bar#%d"), i);
    m_arrBarID.Add((void*)(UINT_PTR)pApp->GetProfileInt(strSection, strEntry, 0));
  }
  return m_nBarID != 0;
}

void CControlBarInfo::Serialize(CArchive& ar, CDockState* pDockState) {
  if (ar.IsStoring()) {
    ar << (DWORD)m_nBarID;
    ar << (DWORD)m_bVisible << (DWORD)m_bFloating << (DWORD)m_bHorz << (DWORD)m_bDockBar;
    ar << (LONG)m_pointPos.x << (LONG)m_pointPos.y;
    ar << (DWORD)m_nMRUWidth << (DWORD)m_bDocking;
    if (m_bDocking) {
      ar << (DWORD)m_uMRUDockID;
      ar << (LONG)m_rectMRUDockPos.left << (LONG)m_rectMRUDockPos.top;
      ar << (LONG)m_rectMRUDockPos.right << (LONG)m_rectMRUDockPos.bottom;
      ar << m_dwMRUFloatStyle << (LONG)m_ptMRUFloatPos.x << (LONG)m_ptMRUFloatPos.y;
    }
    ar << (WORD)m_arrBarID.GetSize();
    for (int i = 0; i < m_arrBarID.GetSize(); i++) ar << (DWORD)(UINT_PTR)m_arrBarID[i];
  } else {
    DWORD dw;
    LONG l1, l2, l3, l4;
    ar >> dw;
    m_nBarID = dw;
    ar >> dw;
    m_bVisible = dw;
    ar >> dw;
    m_bFloating = dw;
    ar >> dw;
    m_bHorz = dw;
    ar >> dw;
    m_bDockBar = dw;
    ar >> l1 >> l2;
    m_pointPos = CPoint(l1, l2);
    if (pDockState != NULL && m_bFloating) pDockState->ScalePoint(m_pointPos);
    ar >> dw;
    m_nMRUWidth = dw;
    ar >> dw;
    m_bDocking = dw;
    if (m_bDocking) {
      ar >> dw;
      m_uMRUDockID = dw;
      ar >> l1 >> l2 >> l3 >> l4;
      m_rectMRUDockPos.SetRect(l1, l2, l3, l4);
      ar >> m_dwMRUFloatStyle >> l1 >> l2;
      m_ptMRUFloatPos = CPoint(l1, l2);
      if (pDockState != NULL) pDockState->ScalePoint(m_ptMRUFloatPos);
    }
    WORD w;
    ar >> w;
    m_arrBarID.RemoveAll();
    for (int i = 0; i < w; i++) {
      ar >> dw;
      m_arrBarID.Add((void*)(UINT_PTR)dw);
    }
  }
}

// ---------------------------------------------------------------------
// CDockState

CDockState::CDockState() {
  m_dwVersion = 2;
  m_bScaling = FALSE;
  m_rectDevice.SetRect(0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN));
  m_rectClip = m_rectDevice;
}

CDockState::~CDockState() { Clear(); }

void CDockState::Clear() {
  for (int i = 0; i < m_arrBarInfo.GetSize(); i++) delete (CControlBarInfo*)m_arrBarInfo[i];
  m_arrBarInfo.RemoveAll();
}

CSize CDockState::GetScreenSize() { return m_rectDevice.Size(); }

void CDockState::SetScreenSize(CSize& size) {
  m_rectDevice.SetRect(0, 0, size.cx, size.cy);
  CSize sizeNow(::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN));
  m_bScaling = size != sizeNow && size.cx > 0 && size.cy > 0;
  m_rectClip.SetRect(0, 0, sizeNow.cx, sizeNow.cy);
}

// From the screen the state was saved on to this one, and kept on it.
void CDockState::ScalePoint(CPoint& pt) {
  if (m_bScaling) {
    CSize sizeNow = m_rectClip.Size();
    pt.x = MulDiv(pt.x, sizeNow.cx, m_rectDevice.Width());
    pt.y = MulDiv(pt.y, sizeNow.cy, m_rectDevice.Height());
  }
  if (pt.x > m_rectClip.right - 16) pt.x = m_rectClip.right - 16;
  if (pt.y > m_rectClip.bottom - 16) pt.y = m_rectClip.bottom - 16;
}

void CDockState::ScaleRectPos(CRect& rect) {
  CPoint pt = rect.TopLeft();
  ScalePoint(pt);
  rect.OffsetRect(pt - rect.TopLeft());
}

void CDockState::SaveState(LPCTSTR lpszProfileName) {
  CWinApp* pApp = AfxGetApp();
  CString strSection;
  strSection.Format(szSummary, lpszProfileName);
  pApp->WriteProfileInt(strSection, _T("Bars"), (int)m_arrBarInfo.GetSize());
  CSize size = GetScreenSize();
  pApp->WriteProfileInt(strSection, _T("ScreenCX"), size.cx);
  pApp->WriteProfileInt(strSection, _T("ScreenCY"), size.cy);
  for (int i = 0; i < m_arrBarInfo.GetSize(); i++)
    ((CControlBarInfo*)m_arrBarInfo[i])->SaveState(lpszProfileName, i);
}

void CDockState::LoadState(LPCTSTR lpszProfileName) {
  CWinApp* pApp = AfxGetApp();
  CString strSection;
  strSection.Format(szSummary, lpszProfileName);
  Clear();
  int nBars = (int)pApp->GetProfileInt(strSection, _T("Bars"), 0);
  CSize size;
  size.cx = (int)pApp->GetProfileInt(strSection, _T("ScreenCX"), ::GetSystemMetrics(SM_CXSCREEN));
  size.cy = (int)pApp->GetProfileInt(strSection, _T("ScreenCY"), ::GetSystemMetrics(SM_CYSCREEN));
  SetScreenSize(size);
  for (int i = 0; i < nBars; i++) {
    CControlBarInfo* pInfo = new CControlBarInfo;
    if (pInfo->LoadState(lpszProfileName, i, this))
      m_arrBarInfo.Add(pInfo);
    else
      delete pInfo;
  }
}

void CDockState::Serialize(CArchive& ar) {
  if (ar.IsStoring()) {
    ar << (WORD)0xFFFF << m_dwVersion;  // a marker, then the version
    CSize size = GetScreenSize();
    ar << (LONG)size.cx << (LONG)size.cy;
    ar << (WORD)m_arrBarInfo.GetSize();
    for (int i = 0; i < m_arrBarInfo.GetSize(); i++)
      ((CControlBarInfo*)m_arrBarInfo[i])->Serialize(ar, this);
  } else {
    Clear();
    WORD w;
    ar >> w;
    if (w == 0xFFFF) {
      ar >> m_dwVersion;
      LONG cx, cy;
      ar >> cx >> cy;
      CSize size(cx, cy);
      SetScreenSize(size);
      ar >> w;
    } else {
      m_dwVersion = 1;
    }
    for (int i = 0; i < w; i++) {
      CControlBarInfo* pInfo = new CControlBarInfo;
      pInfo->Serialize(ar, this);
      m_arrBarInfo.Add(pInfo);
    }
  }
}

// ---------------------------------------------------------------------
// A bar's part of the state

void CControlBar::GetBarInfo(CControlBarInfo* pInfo) {
  pInfo->m_pBar = this;
  pInfo->m_nBarID = (UINT)GetDlgCtrlID();
  pInfo->m_bVisible = (GetStyle() & WS_VISIBLE) != 0;
  pInfo->m_bFloating = IsFloating();
  pInfo->m_bHorz = IsHorzBar();
  pInfo->m_bDockBar = IsDockBar();
  pInfo->m_nMRUWidth = m_nMRUWidth;

  CRect rect;
  GetWindowRect(&rect);
  if (IsDockBar() && ((CDockBar*)this)->m_bFloating) {
    // A floating dock bar: its frame's place on the screen.
    CWnd* pFrame = GetParent();
    pFrame->GetWindowRect(&rect);
    pInfo->m_bFloating = TRUE;
    pInfo->m_bVisible = (pFrame->GetStyle() & WS_VISIBLE) != 0;
  } else {
    CWnd* pParent = GetParent();
    if (pParent != NULL) pParent->ScreenToClient(&rect);
  }
  pInfo->m_pointPos = rect.TopLeft();

  pInfo->m_bDocking = m_pDockContext != NULL;
  if (m_pDockContext != NULL) {
    pInfo->m_uMRUDockID = m_pDockContext->m_uMRUDockID;
    pInfo->m_rectMRUDockPos = m_pDockContext->m_rectMRUDockPos;
    pInfo->m_dwMRUFloatStyle = m_pDockContext->m_dwMRUFloatStyle;
    pInfo->m_ptMRUFloatPos = m_pDockContext->m_ptMRUFloatPos;
  }

  pInfo->m_arrBarID.RemoveAll();
  if (IsDockBar()) {
    CDockBar* pDock = (CDockBar*)this;
    for (int i = 1; i < pDock->m_arrBars.GetSize(); i++) {
      CControlBar* pBar = pDock->GetDockedControlBar(i);
      if (pDock->m_arrBars[i] == NULL)
        pInfo->m_arrBarID.Add(NULL);
      else if (pBar != NULL)
        pInfo->m_arrBarID.Add((void*)(UINT_PTR)pBar->GetDlgCtrlID());
    }
  }
}

void CControlBar::SetBarInfo(CControlBarInfo* pInfo, CFrameWnd* pFrameWnd) {
  (void)pFrameWnd;
  m_nMRUWidth = pInfo->m_nMRUWidth;
  if (m_pDockContext != NULL && pInfo->m_bDocking) {
    m_pDockContext->m_uMRUDockID = pInfo->m_uMRUDockID;
    m_pDockContext->m_rectMRUDockPos = pInfo->m_rectMRUDockPos;
    m_pDockContext->m_dwMRUFloatStyle = pInfo->m_dwMRUFloatStyle;
    m_pDockContext->m_ptMRUFloatPos = pInfo->m_ptMRUFloatPos;
  }
  if (!IsDockBar()) {
    BOOL bVisible = (GetStyle() & WS_VISIBLE) != 0;
    if (bVisible != pInfo->m_bVisible) ShowWindow(pInfo->m_bVisible ? SW_SHOWNA : SW_HIDE);
    if (pInfo->m_pointPos.x != -1 || pInfo->m_pointPos.y != -1)
      SetWindowPos(NULL, pInfo->m_pointPos.x, pInfo->m_pointPos.y, 0, 0,
                   SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
}

// ---------------------------------------------------------------------
// The frame's part

void CFrameWnd::GetDockState(CDockState& state) const {
  state.Clear();
  POSITION pos = m_listControlBars.GetHeadPosition();
  while (pos != NULL) {
    CControlBar* pBar = (CControlBar*)m_listControlBars.GetNext(pos);
    if (pBar->m_hWnd == NULL) continue;
    // Skip floating frames on their way out.
    if (pBar->IsDockBar() && ((CDockBar*)pBar)->m_bFloating &&
        ((CDockBar*)pBar)->GetDockedCount() == 0)
      continue;
    CControlBarInfo* pInfo = new CControlBarInfo;
    pBar->GetBarInfo(pInfo);
    state.m_arrBarInfo.Add(pInfo);
  }
}

void CFrameWnd::SetDockState(const CDockState& state) {
  // Find the bars, and take every bar the state places out of the dock
  // bar it is in now.
  for (int i = 0; i < state.m_arrBarInfo.GetSize(); i++) {
    CControlBarInfo* pInfo = (CControlBarInfo*)state.m_arrBarInfo[i];
    pInfo->m_pBar = pInfo->m_bDockBar ? NULL : GetControlBar(pInfo->m_nBarID);
  }
  for (int i = 0; i < state.m_arrBarInfo.GetSize(); i++) {
    CControlBarInfo* pInfo = (CControlBarInfo*)state.m_arrBarInfo[i];
    if (!pInfo->m_bDockBar) continue;
    for (int j = 0; j < pInfo->m_arrBarID.GetSize(); j++) {
      UINT nID = (UINT)(UINT_PTR)pInfo->m_arrBarID[j];
      if (nID == 0) continue;
      CControlBar* pBar = GetControlBar(nID);
      if (pBar != NULL && !pBar->IsDockBar() && pBar->m_pDockBar != NULL)
        pBar->m_pDockBar->RemoveControlBar(pBar);
    }
  }

  // Rebuild each dock bar, making the floating ones.
  for (int i = 0; i < state.m_arrBarInfo.GetSize(); i++) {
    CControlBarInfo* pInfo = (CControlBarInfo*)state.m_arrBarInfo[i];
    if (!pInfo->m_bDockBar) continue;
    CDockBar* pDockBar = NULL;
    CMiniDockFrameWnd* pFloat = NULL;
    if (pInfo->m_bFloating) {
      // The floating frame's style follows its first bar.
      DWORD dwStyle = pInfo->m_bHorz ? CBRS_ALIGN_TOP : CBRS_ALIGN_LEFT;
      for (int j = 0; j < pInfo->m_arrBarID.GetSize(); j++) {
        CControlBar* pBar = GetControlBar((UINT)(UINT_PTR)pInfo->m_arrBarID[j]);
        if (pBar != NULL && !pBar->IsDockBar()) {
          dwStyle |= pBar->m_dwStyle & CBRS_SIZE_DYNAMIC;
          break;
        }
      }
      pFloat = CreateFloatingFrame(dwStyle);
      pFloat->SetWindowPos(NULL, pInfo->m_pointPos.x, pInfo->m_pointPos.y, 0, 0,
                           SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
      pDockBar = &pFloat->m_wndDockBar;
    } else {
      CControlBar* p = GetControlBar(pInfo->m_nBarID);
      if (p == NULL || !p->IsDockBar()) continue;
      pDockBar = (CDockBar*)p;
    }

    BOOL bRowOpen = FALSE;
    for (int j = 0; j < pInfo->m_arrBarID.GetSize(); j++) {
      UINT nID = (UINT)(UINT_PTR)pInfo->m_arrBarID[j];
      if (nID == 0) {
        if (bRowOpen) pDockBar->m_arrBars.Add(NULL);
        bRowOpen = FALSE;
        continue;
      }
      CControlBar* pBar = GetControlBar(nID);
      if (pBar == NULL || pBar->IsDockBar()) continue;
      if (pBar->m_pDockBar != NULL) pBar->m_pDockBar->RemoveControlBar(pBar);
      if (pDockBar->m_arrBars[pDockBar->m_arrBars.GetSize() - 1] == NULL && bRowOpen)
        pDockBar->m_arrBars.Add(NULL);
      pDockBar->m_arrBars.Add(pBar);
      bRowOpen = TRUE;
      pBar->m_pDockSite = this;
      pDockBar->_AttachBar(pBar);
    }
    if (bRowOpen) pDockBar->m_arrBars.Add(NULL);
    if (pFloat != NULL && pDockBar->GetDockedCount() == 0) pFloat->DestroyWindow();
  }

  // Each bar's own settings: shown or not, its place, its sizes.
  for (int i = 0; i < state.m_arrBarInfo.GetSize(); i++) {
    CControlBarInfo* pInfo = (CControlBarInfo*)state.m_arrBarInfo[i];
    if (pInfo->m_pBar != NULL) pInfo->m_pBar->SetBarInfo(pInfo, this);
  }

  RecalcLayout();
  POSITION pos = m_listControlBars.GetHeadPosition();
  while (pos != NULL) {
    CControlBar* pBar = (CControlBar*)m_listControlBars.GetNext(pos);
    if (pBar->IsDockBar() && ((CDockBar*)pBar)->m_bFloating && pBar->m_hWnd != NULL) {
      CWnd* pFrame = pBar->GetParent();
      if (pFrame != NULL && pFrame->IsFrameWnd()) ((CFrameWnd*)pFrame)->RecalcLayout();
    }
  }
}

void CFrameWnd::SaveBarState(LPCTSTR lpszProfileName) const {
  CDockState state;
  GetDockState(state);
  state.SaveState(lpszProfileName);
}

void CFrameWnd::LoadBarState(LPCTSTR lpszProfileName) {
  CDockState state;
  state.LoadState(lpszProfileName);
  if (state.m_arrBarInfo.GetSize() > 0) SetDockState(state);
}
