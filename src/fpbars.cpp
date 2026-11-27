// fpbars.cpp - the Feature Pack's bars: CMFCToolBarImages, the toolbar
// buttons (plain, menu and combo box), CMFCToolBar, CMFCMenuBar,
// CMFCStatusBar, CCommandManager, CMFCToolTipCtrl and CMFCPopupMenu,
// and the visual manager's drawing of bars, buttons and status panes.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxtoolbar.h>
#include <afxmenubar.h>
#include <afxstatusbar.h>
#include <afxcommandmanager.h>
#include <afxtooltipctrl.h>
#include <afxpopupmenu.h>
#include <afxdockingmanager.h>
#include <afxpaneframewnd.h>
#include <afxvisualmanager.h>

UINT AFX_WM_CREATETOOLBAR = ::RegisterWindowMessage(_T("AFX_WM_CREATETOOLBAR"));
UINT AFX_WM_DELETETOOLBAR = ::RegisterWindowMessage(_T("AFX_WM_DELETETOOLBAR"));
UINT AFX_WM_CUSTOMIZETOOLBAR = ::RegisterWindowMessage(_T("AFX_WM_CUSTOMIZETOOLBAR"));
UINT AFX_WM_RESETTOOLBAR = ::RegisterWindowMessage(_T("AFX_WM_RESETTOOLBAR"));
UINT AFX_WM_SHOWREGULARMENU = ::RegisterWindowMessage(_T("AFX_WM_SHOWREGULARMENU"));
UINT AFX_WM_TOOLBARMENU = ::RegisterWindowMessage(_T("AFX_WM_TOOLBARMENU"));
UINT AFX_WM_CUSTOMIZEHELP = ::RegisterWindowMessage(_T("AFX_WM_CUSTOMIZEHELP"));
UINT AFX_WM_GETDOCUMENTCOLORS = ::RegisterWindowMessage(_T("AFX_WM_GETDOCUMENTCOLORS"));
UINT AFX_WM_ON_STATUSBAR_DBLCLK = ::RegisterWindowMessage(_T("AFX_WM_ON_STATUSBAR_DBLCLK"));

static const int kGripper = 8;     // the gripper's width across the bar
static const int kCustomize = 12;  // the customize button at the bar's end
static const int kMenuArrow = 10;  // a menu button's drop-down arrow

// The tooltip text of a command: what follows the \n in its string.
static CString _AfxCommandTip(UINT nID) {
  CString str;
  if (nID == 0 || !str.LoadString(nID)) return CString();
  int i = str.Find(_T('\n'));
  return i >= 0 ? str.Mid(i + 1) : str;
}

// The window the bars' commands go to: their frame, the main one for MDI.
static CWnd* _AfxBarCommandWnd(CWnd* pBar) {
  CBasePane* pPane = DYNAMIC_DOWNCAST(CBasePane, pBar);
  CFrameWnd* pFrame = pPane != NULL ? pPane->GetDockSiteFrameWnd() : NULL;
  if (pFrame == NULL && pBar != NULL) pFrame = pBar->GetParentFrame();
  if (pFrame != NULL && pFrame->IsKindOf(RUNTIME_CLASS(CMDIChildWnd))) {
    CFrameWnd* pTop = pFrame->GetTopLevelFrame();
    if (pTop != NULL) return pTop;
  }
  return pFrame != NULL ? (CWnd*)pFrame : AfxGetMainWnd();
}

// ---------------------------------------------------------------------
// The visual manager's bars.

void CMFCVisualManager::OnFillBarBackground(CDC* pDC, CBasePane* pBar, CRect rectClient, CRect, BOOL) {
  COLORREF clr = m_bFlat ? m_clrFace : afxGlobalData.clrBarFace;
  if (pBar != NULL && pBar->IsKindOf(RUNTIME_CLASS(CMFCMenuBar))) clr = GetMenuBarColor();
  pDC->FillSolidRect(rectClient, clr);
}

void CMFCVisualManager::OnDrawBarGripper(CDC* pDC, CRect rectGripper, BOOL bHorz, CBasePane*) {
  AFX_GLOBAL_DATA& g = afxGlobalData;
  if (m_bFlat) {
    // A column (or row) of dots.
    if (bHorz) {
      int x = rectGripper.left + rectGripper.Width() / 2 - 1;
      for (int y = rectGripper.top + 3; y + 2 <= rectGripper.bottom - 3; y += 3) {
        pDC->FillSolidRect(x + 1, y + 1, 2, 2, g.clrBtnHilite);
        pDC->FillSolidRect(x, y, 2, 2, g.clrBtnShadow);
      }
    } else {
      int y = rectGripper.top + rectGripper.Height() / 2 - 1;
      for (int x = rectGripper.left + 3; x + 2 <= rectGripper.right - 3; x += 3) {
        pDC->FillSolidRect(x + 1, y + 1, 2, 2, g.clrBtnHilite);
        pDC->FillSolidRect(x, y, 2, 2, g.clrBtnShadow);
      }
    }
    return;
  }
  // A raised bar.
  if (bHorz)
    pDC->Draw3dRect(rectGripper.left + 2, rectGripper.top + 2, 3, rectGripper.Height() - 4, g.clrBtnHilite,
                    g.clrBtnShadow);
  else
    pDC->Draw3dRect(rectGripper.left + 2, rectGripper.top + 2, rectGripper.Width() - 4, 3, g.clrBtnHilite,
                    g.clrBtnShadow);
}

void CMFCVisualManager::OnDrawSeparator(CDC* pDC, CBasePane*, CRect rect, BOOL bIsHoriz) {
  AFX_GLOBAL_DATA& g = afxGlobalData;
  if (bIsHoriz) {
    int x = rect.left + rect.Width() / 2 - 1;
    pDC->FillSolidRect(x, rect.top + 2, 1, rect.Height() - 4, g.clrBtnShadow);
    if (!m_bFlat) pDC->FillSolidRect(x + 1, rect.top + 2, 1, rect.Height() - 4, g.clrBtnHilite);
  } else {
    int y = rect.top + rect.Height() / 2 - 1;
    pDC->FillSolidRect(rect.left + 2, y, rect.Width() - 4, 1, g.clrBtnShadow);
    if (!m_bFlat) pDC->FillSolidRect(rect.left + 2, y + 1, rect.Width() - 4, 1, g.clrBtnHilite);
  }
}

void CMFCVisualManager::OnFillButtonInterior(CDC* pDC, CMFCToolBarButton* pButton, CRect rect,
                                             AFX_BUTTON_STATE state) {
  BOOL bChecked = pButton != NULL && (pButton->m_nStyle & TBBS_CHECKED) != 0;
  if (m_bFlat) {
    if (state == ButtonsIsPressed)
      pDC->FillSolidRect(rect, m_clrPressed);
    else if (state == ButtonsIsHighlighted)
      pDC->FillSolidRect(rect, m_clrHighlight);
    else if (bChecked)
      pDC->FillSolidRect(rect, AfxBlendColor(m_clrHighlight, afxGlobalData.clrWindow, 50));
    return;
  }
  if (bChecked && state != ButtonsIsHighlighted)
    pDC->FillSolidRect(rect, AfxBlendColor(afxGlobalData.clrBtnFace, afxGlobalData.clrBtnHilite, 50));
}

void CMFCVisualManager::OnDrawButtonBorder(CDC* pDC, CMFCToolBarButton* pButton, CRect rect,
                                           AFX_BUTTON_STATE state) {
  AFX_GLOBAL_DATA& g = afxGlobalData;
  BOOL bChecked = pButton != NULL && (pButton->m_nStyle & TBBS_CHECKED) != 0;
  if (m_bFlat) {
    if (state != ButtonsIsRegular || bChecked) {
      CBrush br(state == ButtonsIsPressed ? m_clrPressedBorder : m_clrHighlightBorder);
      pDC->FrameRect(rect, &br);
    }
    return;
  }
  if (state == ButtonsIsPressed || bChecked)
    pDC->Draw3dRect(rect, g.clrBtnShadow, g.clrBtnHilite);
  else if (state == ButtonsIsHighlighted)
    pDC->Draw3dRect(rect, g.clrBtnHilite, g.clrBtnShadow);
}

COLORREF CMFCVisualManager::GetToolbarButtonTextColor(CMFCToolBarButton* pButton, AFX_BUTTON_STATE) {
  if (pButton != NULL && (pButton->m_nStyle & TBBS_DISABLED)) return afxGlobalData.clrGrayedText;
  return afxGlobalData.clrBarText;
}

void CMFCVisualManager::OnHighlightMenuItem(CDC* pDC, CMFCToolBarButton*, CRect rect, COLORREF& clrText) {
  pDC->FillSolidRect(rect, afxGlobalData.clrHilite);
  clrText = afxGlobalData.clrTextHilite;
}

void CMFCVisualManager::OnDrawStatusBarPaneBorder(CDC* pDC, CMFCStatusBar*, CRect rectPane, UINT, UINT nStyle) {
  AFX_GLOBAL_DATA& g = afxGlobalData;
  if (nStyle & SBPS_NOBORDERS) return;
  if (nStyle & SBPS_POPOUT)
    pDC->Draw3dRect(rectPane, g.clrBtnHilite, g.clrBtnShadow);
  else
    pDC->Draw3dRect(rectPane, g.clrBtnShadow, m_bFlat ? g.clrBtnShadow : g.clrBtnHilite);
}

void CMFCVisualManager::OnDrawStatusBarProgress(CDC* pDC, CMFCStatusBar*, CRect rectProgress, int nProgressTotal,
                                                int nProgressCurr, COLORREF clrBar, COLORREF,
                                                COLORREF clrProgressText, BOOL bProgressText) {
  AFX_GLOBAL_DATA& g = afxGlobalData;
  CRect r = rectProgress;
  r.DeflateRect(1, 1);
  if (nProgressTotal <= 0) return;
  int nCurr = nProgressCurr < 0 ? 0 : nProgressCurr > nProgressTotal ? nProgressTotal : nProgressCurr;
  CRect rectBar = r;
  rectBar.right = r.left + (int)((long long)r.Width() * nCurr / nProgressTotal);
  pDC->FillSolidRect(rectBar, clrBar == (COLORREF)-1 ? g.clrHilite : clrBar);
  if (bProgressText) {
    CString str;
    str.Format(_T("%d%%"), (int)((long long)nCurr * 100 / nProgressTotal));
    pDC->SetBkMode(TRANSPARENT);
    pDC->SetTextColor(clrProgressText == (COLORREF)-1 ? g.clrBtnText : clrProgressText);
    pDC->DrawText(str, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

void CMFCVisualManager::OnDrawStatusBarSizeBox(CDC* pDC, CMFCStatusBar*, CRect rectSizeBox) {
  pDC->DrawFrameControl(rectSizeBox, DFC_SCROLL, DFCS_SCROLLSIZEGRIP);
}

// ---------------------------------------------------------------------
// CCommandManager

CCommandManager::CCommandManager() {}

CCommandManager::~CCommandManager() {}

CCommandManager* AFXAPI GetCmdMgr() {
  static CCommandManager s_manager;
  return &s_manager;
}

void CCommandManager::SetCmdImage(UINT uiCmd, int iImage, BOOL bUserImage) {
  if (uiCmd == 0) return;
  if (bUserImage)
    m_CommandIndexUser.SetAt(uiCmd, iImage);
  else
    m_CommandIndex.SetAt(uiCmd, iImage);
}

int CCommandManager::GetCmdImage(UINT uiCmd, BOOL bUserImage) const {
  int iImage;
  if (bUserImage) return m_CommandIndexUser.Lookup(uiCmd, iImage) ? iImage : -1;
  return m_CommandIndex.Lookup(uiCmd, iImage) ? iImage : -1;
}

void CCommandManager::ClearCmdImage(UINT uiCmd) {
  m_CommandIndex.RemoveKey(uiCmd);
  m_CommandIndexUser.RemoveKey(uiCmd);
}

void CCommandManager::ClearUserCmdImages() { m_CommandIndexUser.RemoveAll(); }

void CCommandManager::ClearAllCmdImages() {
  m_CommandIndex.RemoveAll();
  m_CommandIndexUser.RemoveAll();
  m_lstCommandsWithoutImages.RemoveAll();
}

void CCommandManager::EnableMenuItemImage(UINT uiCmd, BOOL bEnable, int nUserImage) {
  POSITION pos = m_lstCommandsWithoutImages.Find(uiCmd);
  if (bEnable) {
    if (pos != NULL) m_lstCommandsWithoutImages.RemoveAt(pos);
    if (nUserImage >= 0) SetCmdImage(uiCmd, nUserImage, TRUE);
  } else if (pos == NULL) {
    m_lstCommandsWithoutImages.AddTail(uiCmd);
  }
}

BOOL CCommandManager::IsMenuItemWithoutImage(UINT uiCmd) const {
  return m_lstCommandsWithoutImages.Find(uiCmd) != NULL;
}

// ---------------------------------------------------------------------
// CMFCToolBarImages

CMFCToolBarImages::CMFCToolBarImages()
    : m_hImageList(NULL), m_sizeImage(16, 15), m_clrTransparent((COLORREF)-1), m_nBitsPerPixel(0),
      m_nDisabledImageAlpha(127), m_nFadedImageAlpha(150) {}

CMFCToolBarImages::~CMFCToolBarImages() { Clear(); }

void CMFCToolBarImages::_Create() {
  if (m_hImageList == NULL) m_hImageList = ::ImageList_Create(m_sizeImage.cx, m_sizeImage.cy, ILC_COLOR32 | ILC_MASK, 0, 16);
}

void CMFCToolBarImages::Clear() {
  if (m_hImageList != NULL) ::ImageList_Destroy(m_hImageList);
  m_hImageList = NULL;
}

int CMFCToolBarImages::GetCount() const { return m_hImageList != NULL ? ::ImageList_GetImageCount(m_hImageList) : 0; }

void CMFCToolBarImages::SetImageSize(SIZE sizeImage, BOOL) {
  if (sizeImage.cx == m_sizeImage.cx && sizeImage.cy == m_sizeImage.cy) return;
  m_sizeImage = sizeImage;
  if (m_hImageList != NULL && GetCount() == 0) Clear();
}

COLORREF CMFCToolBarImages::SetTransparentColor(COLORREF clrTransparent) {
  COLORREF clrOld = m_clrTransparent;
  m_clrTransparent = clrTransparent;
  return clrOld;
}

BOOL CMFCToolBarImages::Load(UINT uiResID, HINSTANCE hinstRes, BOOL bAdd) {
  if (hinstRes == NULL) hinstRes = AfxFindResourceHandle(MAKEINTRESOURCE(uiResID), RT_BITMAP);
  HBITMAP hbmp = (HBITMAP)::LoadImage(hinstRes, MAKEINTRESOURCE(uiResID), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
  if (hbmp == NULL) return FALSE;
  if (!bAdd) Clear();
  int i = AddImage(hbmp, TRUE);
  ::DeleteObject(hbmp);
  return i >= 0;
}

BOOL CMFCToolBarImages::Load(LPCTSTR lpszBmpFileName, DWORD) {
  HBITMAP hbmp = (HBITMAP)::LoadImage(NULL, lpszBmpFileName, IMAGE_BITMAP, 0, 0,
                                      LR_LOADFROMFILE | LR_CREATEDIBSECTION);
  if (hbmp == NULL) return FALSE;
  Clear();
  int i = AddImage(hbmp, TRUE);
  ::DeleteObject(hbmp);
  return i >= 0;
}

int CMFCToolBarImages::AddImage(HBITMAP hbmp, BOOL bSetBitPerPixel) {
  BITMAP bm;
  if (hbmp == NULL || ::GetObject(hbmp, sizeof(bm), &bm) == 0) return -1;
  if (bSetBitPerPixel || m_nBitsPerPixel == 0) m_nBitsPerPixel = bm.bmBitsPixel;
  // A strip as tall as its images.
  if (GetCount() == 0 && bm.bmHeight != m_sizeImage.cy && bm.bmHeight > 0) {
    Clear();
    m_sizeImage.cy = bm.bmHeight;
  }
  _Create();
  if (m_hImageList == NULL) return -1;
  int nFirst = GetCount();
  // The masked add changes the bitmap, so it works on a copy.
  HBITMAP hCopy = (HBITMAP)::CopyImage(hbmp, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
  if (hCopy == NULL) return -1;
  int iResult;
  if (bm.bmBitsPixel == 32 && m_clrTransparent == (COLORREF)-1) {
    iResult = ::ImageList_Add(m_hImageList, hCopy, NULL);
  } else {
    COLORREF clr = m_clrTransparent;
    if (clr == (COLORREF)-1) {
      // Gray, unless the art uses magenta as its clear color.
      clr = RGB(192, 192, 192);
      if (bm.bmBitsPixel >= 16) {
        HDC hdc = ::CreateCompatibleDC(NULL);
        HGDIOBJ hOld = ::SelectObject(hdc, hCopy);
        for (int y = 0; y < bm.bmHeight && clr != RGB(255, 0, 255); y++)
          for (int x = 0; x < bm.bmWidth; x++)
            if (::GetPixel(hdc, x, y) == RGB(255, 0, 255)) {
              clr = RGB(255, 0, 255);
              break;
            }
        ::SelectObject(hdc, hOld);
        ::DeleteDC(hdc);
      }
    }
    iResult = ::ImageList_AddMasked(m_hImageList, hCopy, clr);
  }
  ::DeleteObject(hCopy);
  return iResult < 0 ? -1 : nFirst;
}

int CMFCToolBarImages::AddImage(const CMFCToolBarImages& imageList, int nIndex) {
  if (imageList.m_hImageList == NULL) return -1;
  HICON hIcon = ::ImageList_GetIcon(imageList.m_hImageList, nIndex, ILD_NORMAL);
  if (hIcon == NULL) return -1;
  int i = AddIcon(hIcon);
  ::DestroyIcon(hIcon);
  return i;
}

int CMFCToolBarImages::AddIcon(HICON hIcon, BOOL) {
  _Create();
  if (m_hImageList == NULL || hIcon == NULL) return -1;
  return ::ImageList_ReplaceIcon(m_hImageList, -1, hIcon);
}

BOOL CMFCToolBarImages::UpdateImage(int iImage, HBITMAP hbmp) {
  return m_hImageList != NULL && ::ImageList_Replace(m_hImageList, iImage, hbmp, NULL);
}

BOOL CMFCToolBarImages::DeleteImage(int iImage) {
  return m_hImageList != NULL && ::ImageList_Remove(m_hImageList, iImage);
}

HICON CMFCToolBarImages::ExtractIcon(int nIndex) {
  return m_hImageList != NULL ? ::ImageList_GetIcon(m_hImageList, nIndex, ILD_NORMAL) : NULL;
}

BOOL CMFCToolBarImages::Draw(CDC* pDC, int x, int y, int iImage, BOOL, BOOL bDisabled, BOOL, BOOL, BOOL, BYTE) {
  if (m_hImageList == NULL || iImage < 0 || iImage >= GetCount()) return FALSE;
  if (bDisabled) {
    // Embossed in the bar's colors.
    HICON hIcon = ::ImageList_GetIcon(m_hImageList, iImage, ILD_NORMAL);
    if (hIcon == NULL) return FALSE;
    pDC->DrawState(CPoint(x, y), m_sizeImage, hIcon, DSS_DISABLED, (HBRUSH)NULL);
    ::DestroyIcon(hIcon);
    return TRUE;
  }
  return ::ImageList_Draw(m_hImageList, iImage, pDC->GetSafeHdc(), x, y, ILD_TRANSPARENT);
}

BOOL CMFCToolBarImages::DrawEx(CDC* pDC, CRect rect, int iImageIndex, int horzAlign, int vertAlign, CRect, BYTE) {
  int x = horzAlign == 0 ? rect.left : horzAlign == 2 ? rect.right - m_sizeImage.cx
                                                       : rect.left + (rect.Width() - m_sizeImage.cx) / 2;
  int y = vertAlign == 0 ? rect.top : vertAlign == 2 ? rect.bottom - m_sizeImage.cy
                                                     : rect.top + (rect.Height() - m_sizeImage.cy) / 2;
  return Draw(pDC, x, y, iImageIndex);
}

// ---------------------------------------------------------------------
// CMFCToolBarButton

IMPLEMENT_SERIAL(CMFCToolBarButton, CObject, VERSIONABLE_SCHEMA | 1)

BOOL CMFCToolBarButton::m_bWrapText = TRUE;
CLIPFORMAT CMFCToolBarButton::m_cFormat = 0;

CMFCToolBarButton::CMFCToolBarButton()
    : m_nID(0), m_nStyle(TBBS_BUTTON), m_bText(FALSE), m_bImage(TRUE), m_bWrap(FALSE), m_bWholeText(TRUE),
      m_bTextBelow(FALSE), m_bDragFromCollection(FALSE), m_dwdItemData(0), m_iImage(-1), m_iUserImage(-1),
      m_bUserButton(FALSE), m_bLocked(FALSE), m_bVisible(TRUE), m_bDisableFill(FALSE), m_rect(0, 0, 0, 0),
      m_pWndParent(NULL) {}

CMFCToolBarButton::CMFCToolBarButton(UINT uiID, int iImage, LPCTSTR lpszText, BOOL bUserButton, BOOL bLocked)
    : CMFCToolBarButton() {
  m_nID = uiID;
  m_bUserButton = bUserButton;
  m_bLocked = bLocked;
  if (bUserButton)
    m_iUserImage = iImage;
  else
    m_iImage = iImage;
  if (lpszText != NULL) m_strText = lpszText;
  if (!bLocked && !bUserButton && uiID != 0 && iImage >= 0) GetCmdMgr()->SetCmdImage(uiID, iImage, FALSE);
}

CMFCToolBarButton::~CMFCToolBarButton() {}

void CMFCToolBarButton::CopyFrom(const CMFCToolBarButton& src) {
  m_nID = src.m_nID;
  m_nStyle = src.m_nStyle;
  m_strText = src.m_strText;
  m_bText = src.m_bText;
  m_bImage = src.m_bImage;
  m_bWrap = src.m_bWrap;
  m_bWholeText = src.m_bWholeText;
  m_bTextBelow = src.m_bTextBelow;
  m_dwdItemData = src.m_dwdItemData;
  m_iImage = src.m_iImage;
  m_iUserImage = src.m_iUserImage;
  m_bUserButton = src.m_bUserButton;
  m_bLocked = src.m_bLocked;
  m_bVisible = src.m_bVisible;
  m_bDisableFill = src.m_bDisableFill;
}

void CMFCToolBarButton::Serialize(CArchive& ar) {
  CObject::Serialize(ar);
  if (ar.IsStoring()) {
    ar << (DWORD)m_nID << (DWORD)m_nStyle << (LONG)m_iImage << (LONG)m_iUserImage << m_strText << (LONG)m_bText
       << (LONG)m_bImage << (LONG)m_bUserButton;
  } else {
    DWORD dwID, dwStyle;
    LONG iImage, iUserImage, bText, bImage, bUser;
    ar >> dwID >> dwStyle >> iImage >> iUserImage >> m_strText >> bText >> bImage >> bUser;
    m_nID = dwID;
    m_nStyle = dwStyle;
    m_iImage = (int)iImage;
    m_iUserImage = (int)iUserImage;
    m_bText = (BOOL)bText;
    m_bImage = (BOOL)bImage;
    m_bUserButton = (BOOL)bUser;
  }
}

void CMFCToolBarButton::EnableWindow(BOOL bEnable) {
  if (bEnable)
    m_nStyle &= ~TBBS_DISABLED;
  else
    m_nStyle |= TBBS_DISABLED;
}

SIZE CMFCToolBarButton::OnCalculateSize(CDC* pDC, const CSize& sizeDefault, BOOL bHorz) {
  if (IsSeparator()) return bHorz ? CSize(8, sizeDefault.cy) : CSize(sizeDefault.cx, 8);
  CSize size = sizeDefault;
  BOOL bImage = m_bImage && GetImage() >= 0;
  if (m_bText && !m_strText.IsEmpty() && bHorz) {
    CSize sizeText = pDC->GetTextExtent(m_strText);
    size.cx = bImage ? sizeDefault.cx + sizeText.cx + 4 : sizeText.cx + 12;
    if (size.cy < sizeText.cy + 6) size.cy = sizeText.cy + 6;
  }
  return size;
}

void CMFCToolBarButton::OnDraw(CDC* pDC, const CRect& rect, CMFCToolBarImages* pImages, BOOL bHorz, BOOL,
                               BOOL bHighlight, BOOL bDrawBorder, BOOL bGrayDisabledButtons) {
  if (IsSeparator()) return;
  CMFCVisualManager* pVM = CMFCVisualManager::GetInstance();
  BOOL bDisabled = (m_nStyle & TBBS_DISABLED) != 0;
  BOOL bPressed = (m_nStyle & TBBS_PRESSED) != 0;
  CMFCVisualManager::AFX_BUTTON_STATE state = bPressed ? CMFCVisualManager::ButtonsIsPressed
                                              : (bHighlight && !bDisabled) ? CMFCVisualManager::ButtonsIsHighlighted
                                                                           : CMFCVisualManager::ButtonsIsRegular;
  if (!m_bDisableFill) pVM->OnFillButtonInterior(pDC, this, rect, state);

  BOOL bImage = m_bImage && GetImage() >= 0 && pImages != NULL;
  BOOL bText = m_bText && !m_strText.IsEmpty() && bHorz;
  int nShift = (bPressed || (m_nStyle & TBBS_CHECKED)) && !pVM->IsFlat() ? 1 : 0;
  CRect rectText = rect;
  if (bImage) {
    CSize sizeImage = pImages->GetImageSize();
    int x = bText ? rect.left + 4 : rect.left + (rect.Width() - sizeImage.cx) / 2;
    int y = rect.top + (rect.Height() - sizeImage.cy) / 2;
    pImages->Draw(pDC, x + nShift, y + nShift, GetImage(), bHighlight, bDisabled && bGrayDisabledButtons);
    rectText.left = x + sizeImage.cx + 4;
  }
  if (bText) {
    pDC->SetBkMode(TRANSPARENT);
    pDC->SetTextColor(pVM->GetToolbarButtonTextColor(this, state));
    rectText.OffsetRect(nShift, nShift);
    pDC->DrawText(m_strText, rectText, DT_SINGLELINE | DT_VCENTER | (bImage ? DT_LEFT : DT_CENTER));
  }
  if (bDrawBorder && HaveHotBorder()) pVM->OnDrawButtonBorder(pDC, this, rect, state);
}

BOOL CMFCToolBarButton::OnToolHitTest(const CWnd* pWnd, TOOLINFO* pTI) {
  if (pTI == NULL || IsSeparator()) return FALSE;
  pTI->hwnd = pWnd->GetSafeHwnd();
  pTI->uId = m_nID;
  pTI->rect = m_rect;
  pTI->lpszText = LPSTR_TEXTCALLBACK;
  return TRUE;
}

// ---------------------------------------------------------------------
// CMFCToolBarMenuButton

IMPLEMENT_SERIAL(CMFCToolBarMenuButton, CMFCToolBarButton, VERSIONABLE_SCHEMA | 1)

BOOL CMFCToolBarMenuButton::m_bAlwaysCallOwnerDraw = FALSE;

// A deep copy of a menu and its submenus.
static HMENU _AfxCopyMenu(HMENU hMenu) {
  if (hMenu == NULL) return NULL;
  HMENU hCopy = ::CreatePopupMenu();
  int n = ::GetMenuItemCount(hMenu);
  for (int i = 0; i < n; i++) {
    TCHAR szText[256];
    szText[0] = 0;
    UINT uState = ::GetMenuState(hMenu, i, MF_BYPOSITION);
    ::GetMenuString(hMenu, i, szText, 256, MF_BYPOSITION);
    HMENU hSub = ::GetSubMenu(hMenu, i);
    if (hSub != NULL) uState &= 0xFF;  // a popup's high byte is its item count
    UINT uFlags = MF_BYPOSITION | (uState & (MF_SEPARATOR | MF_GRAYED | MF_DISABLED | MF_CHECKED | MF_MENUBREAK));
    if (hSub != NULL)
      ::InsertMenu(hCopy, i, uFlags | MF_POPUP | MF_STRING, (UINT_PTR)_AfxCopyMenu(hSub), szText);
    else if (uState & MF_SEPARATOR)
      ::InsertMenu(hCopy, i, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    else
      ::InsertMenu(hCopy, i, uFlags | MF_STRING, ::GetMenuItemID(hMenu, i), szText);
  }
  return hCopy;
}

CMFCToolBarMenuButton::CMFCToolBarMenuButton()
    : m_bDrawDownArrow(TRUE), m_bMenuMode(FALSE), m_bClickedOnMenu(FALSE), m_bMenuOnly(FALSE), m_hMenu(NULL),
      m_bOwnMenu(FALSE), m_bDroppedDown(FALSE), m_pWndMessage(NULL), m_nLastCommand(0) {}

CMFCToolBarMenuButton::CMFCToolBarMenuButton(const CMFCToolBarMenuButton& src) : CMFCToolBarMenuButton() {
  CopyFrom(src);
}

CMFCToolBarMenuButton::CMFCToolBarMenuButton(UINT uiID, HMENU hMenu, int iImage, LPCTSTR lpszText,
                                             BOOL bUserButton)
    : CMFCToolBarMenuButton() {
  m_nID = uiID;
  m_bUserButton = bUserButton;
  if (bUserButton)
    m_iUserImage = iImage;
  else
    m_iImage = iImage;
  if (lpszText != NULL) m_strText = lpszText;
  CreateFromMenu(hMenu);
}

CMFCToolBarMenuButton::~CMFCToolBarMenuButton() {
  if (m_bOwnMenu && m_hMenu != NULL) ::DestroyMenu(m_hMenu);
}

void CMFCToolBarMenuButton::CopyFrom(const CMFCToolBarButton& s) {
  CMFCToolBarButton::CopyFrom(s);
  const CMFCToolBarMenuButton* pSrc = DYNAMIC_DOWNCAST(CMFCToolBarMenuButton, const_cast<CMFCToolBarButton*>(&s));
  if (pSrc == NULL) return;
  if (m_bOwnMenu && m_hMenu != NULL) ::DestroyMenu(m_hMenu);
  // A menu the source owns is copied; one it borrows is borrowed too.
  m_bOwnMenu = pSrc->m_bOwnMenu;
  m_hMenu = pSrc->m_bOwnMenu ? _AfxCopyMenu(pSrc->m_hMenu) : pSrc->m_hMenu;
  m_bDrawDownArrow = pSrc->m_bDrawDownArrow;
  m_bMenuMode = pSrc->m_bMenuMode;
  m_bMenuOnly = pSrc->m_bMenuOnly;
  m_pWndMessage = pSrc->m_pWndMessage;
}

HMENU CMFCToolBarMenuButton::CreateMenu() const { return _AfxCopyMenu(m_hMenu); }

void CMFCToolBarMenuButton::CreateFromMenu(HMENU hMenu) {
  if (m_bOwnMenu && m_hMenu != NULL) ::DestroyMenu(m_hMenu);
  m_hMenu = _AfxCopyMenu(hMenu);
  m_bOwnMenu = m_hMenu != NULL;
}

BOOL CMFCToolBarMenuButton::OpenPopupMenu(CWnd* pWnd) {
  if (pWnd == NULL) pWnd = m_pWndParent;
  if (pWnd == NULL || m_hMenu == NULL) return FALSE;
  CRect rect = m_rect;
  pWnd->ClientToScreen(rect);
  CBasePane* pBar = DYNAMIC_DOWNCAST(CBasePane, pWnd);
  BOOL bHorz = pBar == NULL || pBar->IsHorizontal();
  CWnd* pTarget = m_pWndMessage != NULL ? m_pWndMessage : _AfxBarCommandWnd(pWnd);
  m_bDroppedDown = TRUE;
  m_nStyle |= TBBS_PRESSED;
  pWnd->InvalidateRect(m_rect);
  pWnd->UpdateWindow();
  // The frame owns the menu, so its items get their command UI and prompts.
  UINT nCmd = (UINT)::TrackPopupMenu(m_hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_LEFTBUTTON,
                                     bHorz ? rect.left : rect.right, bHorz ? rect.bottom : rect.top, 0,
                                     pTarget->GetSafeHwnd(), NULL);
  m_bDroppedDown = FALSE;
  m_nStyle &= ~TBBS_PRESSED;
  m_nLastCommand = nCmd;
  if (::IsWindow(pWnd->m_hWnd)) pWnd->InvalidateRect(m_rect);
  if (nCmd != 0 && pTarget->GetSafeHwnd() != NULL) pTarget->SendMessage(WM_COMMAND, nCmd);
  return TRUE;
}

BOOL CMFCToolBarMenuButton::OnClick(CWnd* pWnd, BOOL) {
  if (m_hMenu == NULL) return FALSE;
  // A button with a command of its own runs it unless the arrow is hit.
  if (m_nID != 0 && m_nID != (UINT)-1 && !m_bMenuOnly && !m_bClickedOnMenu && !m_bMenuMode) return FALSE;
  m_bClickedOnMenu = FALSE;
  return OpenPopupMenu(pWnd);
}

SIZE CMFCToolBarMenuButton::OnCalculateSize(CDC* pDC, const CSize& sizeDefault, BOOL bHorz) {
  if (m_bMenuMode) {
    CString strText = m_strText;
    strText.Remove(_T('&'));
    CSize sizeText = pDC->GetTextExtent(strText);
    if (!bHorz) return CSize(sizeDefault.cx, sizeText.cx + 12);
    return CSize(sizeText.cx + 12, sizeDefault.cy > sizeText.cy + 6 ? sizeDefault.cy : sizeText.cy + 6);
  }
  CSize size = CMFCToolBarButton::OnCalculateSize(pDC, sizeDefault, bHorz);
  if (m_bDrawDownArrow && m_hMenu != NULL) {
    if (bHorz)
      size.cx += kMenuArrow;
    else
      size.cy += kMenuArrow;
  }
  return size;
}

void CMFCToolBarMenuButton::OnDraw(CDC* pDC, const CRect& rect, CMFCToolBarImages* pImages, BOOL bHorz,
                                   BOOL bCustomizeMode, BOOL bHighlight, BOOL bDrawBorder,
                                   BOOL bGrayDisabledButtons) {
  CMFCVisualManager* pVM = CMFCVisualManager::GetInstance();
  if (m_bMenuMode) {
    // A menu bar item: its text, lit when hot, pushed in while open.
    BOOL bDisabled = (m_nStyle & TBBS_DISABLED) != 0;
    CMFCVisualManager::AFX_BUTTON_STATE state =
        m_bDroppedDown ? CMFCVisualManager::ButtonsIsPressed
        : (bHighlight && !bDisabled) ? CMFCVisualManager::ButtonsIsHighlighted
                                     : CMFCVisualManager::ButtonsIsRegular;
    pVM->OnFillButtonInterior(pDC, this, rect, state);
    if (state != CMFCVisualManager::ButtonsIsRegular) pVM->OnDrawButtonBorder(pDC, this, rect, state);
    pDC->SetBkMode(TRANSPARENT);
    pDC->SetTextColor(pVM->GetToolbarButtonTextColor(this, state));
    CRect r = rect;
    if (!bHorz) {
      CFont* pOld = pDC->SelectObject(&afxGlobalData.fontVert);
      CString strText = m_strText;
      strText.Remove(_T('&'));
      CSize sizeText = pDC->GetTextExtent(strText);
      pDC->TextOut(r.right - (r.Width() - sizeText.cy) / 2, r.top + 6, strText);
      pDC->SelectObject(pOld);
      return;
    }
    pDC->DrawText(m_strText, r, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    return;
  }
  CRect rectButton = rect;
  CRect rectArrow(0, 0, 0, 0);
  if (m_bDrawDownArrow && m_hMenu != NULL) {
    rectArrow = rect;
    if (bHorz)
      rectArrow.left = rectButton.right = rect.right - kMenuArrow;
    else
      rectArrow.top = rectButton.bottom = rect.bottom - kMenuArrow;
  }
  CMFCToolBarButton::OnDraw(pDC, rect, pImages, bHorz, bCustomizeMode, bHighlight, bDrawBorder,
                            bGrayDisabledButtons);
  if (!rectArrow.IsRectEmpty()) {
    if (m_nID != 0 && !m_bMenuOnly && bHighlight) {
      // The line between the button and its arrow.
      if (bHorz)
        pDC->FillSolidRect(rectArrow.left, rect.top + 3, 1, rect.Height() - 6, afxGlobalData.clrBtnShadow);
      else
        pDC->FillSolidRect(rect.left + 3, rectArrow.top, rect.Width() - 6, 1, afxGlobalData.clrBtnShadow);
    }
    pVM->OnDrawMenuArrow(pDC, rectArrow, (m_nStyle & TBBS_DISABLED) == 0);
  }
}

// ---------------------------------------------------------------------
// CMFCToolBarComboBoxButton

IMPLEMENT_SERIAL(CMFCToolBarComboBoxButton, CMFCToolBarButton, VERSIONABLE_SCHEMA | 1)

BOOL CMFCToolBarComboBoxButton::m_bFlat = FALSE;
BOOL CMFCToolBarComboBoxButton::m_bCenterVert = TRUE;

CMFCToolBarComboBoxButton::CMFCToolBarComboBoxButton()
    : m_dwStyle(CBS_DROPDOWNLIST), m_iWidth(150), m_iSelIndex(-1), m_nDropDownHeight(150), m_pWndCombo(NULL),
      m_bInSync(FALSE) {}

CMFCToolBarComboBoxButton::CMFCToolBarComboBoxButton(UINT uiID, int iImage, DWORD dwStyle, int iWidth)
    : CMFCToolBarComboBoxButton() {
  m_nID = uiID;
  m_iImage = iImage;
  m_dwStyle = dwStyle;
  m_iWidth = iWidth > 0 ? iWidth : 150;
}

CMFCToolBarComboBoxButton::~CMFCToolBarComboBoxButton() {
  if (m_pWndCombo != NULL) {
    if (m_pWndCombo->GetSafeHwnd() != NULL) m_pWndCombo->DestroyWindow();
    delete m_pWndCombo;
  }
}

void CMFCToolBarComboBoxButton::CopyFrom(const CMFCToolBarButton& s) {
  CMFCToolBarButton::CopyFrom(s);
  const CMFCToolBarComboBoxButton* pSrc =
      DYNAMIC_DOWNCAST(CMFCToolBarComboBoxButton, const_cast<CMFCToolBarButton*>(&s));
  if (pSrc == NULL) return;
  m_dwStyle = pSrc->m_dwStyle;
  m_iWidth = pSrc->m_iWidth;
  m_iSelIndex = pSrc->m_iSelIndex;
  m_nDropDownHeight = pSrc->m_nDropDownHeight;
  m_strEdit = pSrc->m_strEdit;
  m_lstItems.RemoveAll();
  m_lstItems.AddTail(const_cast<CStringList*>(&pSrc->m_lstItems));
  m_lstItemData.RemoveAll();
  for (POSITION pos = pSrc->m_lstItemData.GetHeadPosition(); pos != NULL;)
    m_lstItemData.AddTail(pSrc->m_lstItemData.GetNext(pos));
  _SyncCombo();
}

void CMFCToolBarComboBoxButton::_SyncCombo() {
  if (m_pWndCombo == NULL || m_pWndCombo->GetSafeHwnd() == NULL) return;
  m_bInSync = TRUE;
  m_pWndCombo->ResetContent();
  POSITION posData = m_lstItemData.GetHeadPosition();
  for (POSITION pos = m_lstItems.GetHeadPosition(); pos != NULL;) {
    int i = m_pWndCombo->AddString(m_lstItems.GetNext(pos));
    if (posData != NULL) m_pWndCombo->SetItemData(i, m_lstItemData.GetNext(posData));
  }
  m_pWndCombo->SetCurSel(m_iSelIndex);
  if (m_iSelIndex < 0 && !m_strEdit.IsEmpty()) m_pWndCombo->SetWindowText(m_strEdit);
  m_bInSync = FALSE;
}

INT_PTR CMFCToolBarComboBoxButton::AddItem(LPCTSTR lpszItem, DWORD_PTR dwData) {
  if (lpszItem == NULL) return -1;
  int i = FindItem(lpszItem);
  if (i >= 0) return i;
  m_lstItems.AddTail(lpszItem);
  m_lstItemData.AddTail(dwData);
  if (m_pWndCombo != NULL && m_pWndCombo->GetSafeHwnd() != NULL) {
    int iCombo = m_pWndCombo->AddString(lpszItem);
    m_pWndCombo->SetItemData(iCombo, dwData);
  }
  return m_lstItems.GetCount() - 1;
}

LPCTSTR CMFCToolBarComboBoxButton::GetItem(int iIndex) const {
  if (iIndex == -1) iIndex = m_iSelIndex;
  POSITION pos = iIndex >= 0 ? m_lstItems.FindIndex(iIndex) : NULL;
  return pos != NULL ? (LPCTSTR)m_lstItems.GetAt(pos) : NULL;
}

DWORD_PTR CMFCToolBarComboBoxButton::GetItemData(int iIndex) const {
  if (iIndex == -1) iIndex = m_iSelIndex;
  POSITION pos = iIndex >= 0 ? m_lstItemData.FindIndex(iIndex) : NULL;
  return pos != NULL ? m_lstItemData.GetAt(pos) : 0;
}

int CMFCToolBarComboBoxButton::FindItem(LPCTSTR lpszText) const {
  int i = 0;
  for (POSITION pos = m_lstItems.GetHeadPosition(); pos != NULL; i++)
    if (m_lstItems.GetNext(pos).CompareNoCase(lpszText) == 0) return i;
  return -1;
}

BOOL CMFCToolBarComboBoxButton::SelectItem(int iIndex, BOOL bNotify) {
  if (iIndex >= (int)m_lstItems.GetCount()) return FALSE;
  m_iSelIndex = iIndex < 0 ? -1 : iIndex;
  m_strEdit = m_iSelIndex >= 0 ? GetItem(m_iSelIndex) : _T("");
  if (m_pWndCombo != NULL && m_pWndCombo->GetSafeHwnd() != NULL) {
    m_bInSync = TRUE;
    m_pWndCombo->SetCurSel(m_iSelIndex);
    m_bInSync = FALSE;
  }
  if (bNotify && m_pWndParent != NULL) {
    CWnd* pTarget = _AfxBarCommandWnd(m_pWndParent);
    if (pTarget != NULL) pTarget->SendMessage(WM_COMMAND, MAKEWPARAM(m_nID, 0), 0);
  }
  return TRUE;
}

BOOL CMFCToolBarComboBoxButton::SelectItem(DWORD_PTR dwData) {
  int i = 0;
  for (POSITION pos = m_lstItemData.GetHeadPosition(); pos != NULL; i++)
    if (m_lstItemData.GetNext(pos) == dwData) return SelectItem(i, FALSE);
  return FALSE;
}

BOOL CMFCToolBarComboBoxButton::SelectItem(LPCTSTR lpszText) {
  int i = FindItem(lpszText);
  return i >= 0 && SelectItem(i, FALSE);
}

BOOL CMFCToolBarComboBoxButton::DeleteItem(int iIndex) {
  POSITION pos = iIndex >= 0 ? m_lstItems.FindIndex(iIndex) : NULL;
  if (pos == NULL) return FALSE;
  m_lstItems.RemoveAt(pos);
  POSITION posData = m_lstItemData.FindIndex(iIndex);
  if (posData != NULL) m_lstItemData.RemoveAt(posData);
  if (m_iSelIndex == iIndex)
    m_iSelIndex = -1;
  else if (m_iSelIndex > iIndex)
    m_iSelIndex--;
  if (m_pWndCombo != NULL && m_pWndCombo->GetSafeHwnd() != NULL) m_pWndCombo->DeleteString(iIndex);
  return TRUE;
}

void CMFCToolBarComboBoxButton::RemoveAllItems() {
  m_lstItems.RemoveAll();
  m_lstItemData.RemoveAll();
  m_iSelIndex = -1;
  m_strEdit.Empty();
  if (m_pWndCombo != NULL && m_pWndCombo->GetSafeHwnd() != NULL) m_pWndCombo->ResetContent();
}

void CMFCToolBarComboBoxButton::SetText(LPCTSTR lpszText) {
  m_strEdit = lpszText != NULL ? lpszText : _T("");
  int i = FindItem(m_strEdit);
  m_iSelIndex = i;
  if (m_pWndCombo != NULL && m_pWndCombo->GetSafeHwnd() != NULL) {
    m_bInSync = TRUE;
    if (i >= 0)
      m_pWndCombo->SetCurSel(i);
    else
      m_pWndCombo->SetWindowText(m_strEdit);
    m_bInSync = FALSE;
  }
}

BOOL CMFCToolBarComboBoxButton::IsWindowVisible() {
  return m_pWndCombo != NULL && m_pWndCombo->GetSafeHwnd() != NULL && m_pWndCombo->IsWindowVisible();
}

void CMFCToolBarComboBoxButton::EnableWindow(BOOL bEnable) {
  CMFCToolBarButton::EnableWindow(bEnable);
  if (m_pWndCombo != NULL && m_pWndCombo->GetSafeHwnd() != NULL && m_pWndCombo->IsWindowEnabled() != bEnable)
    m_pWndCombo->EnableWindow(bEnable);
}

SIZE CMFCToolBarComboBoxButton::OnCalculateSize(CDC* pDC, const CSize& sizeDefault, BOOL bHorz) {
  if (!bHorz) return CMFCToolBarButton::OnCalculateSize(pDC, sizeDefault, bHorz);
  int cy = sizeDefault.cy;
  if (m_pWndCombo != NULL && m_pWndCombo->GetSafeHwnd() != NULL) {
    CRect r;
    m_pWndCombo->GetWindowRect(r);
    if (r.Height() + 2 > cy) cy = r.Height() + 2;
  }
  return CSize(m_iWidth, cy);
}

void CMFCToolBarComboBoxButton::OnDraw(CDC* pDC, const CRect& rect, CMFCToolBarImages* pImages, BOOL bHorz,
                                       BOOL bCustomizeMode, BOOL bHighlight, BOOL bDrawBorder,
                                       BOOL bGrayDisabledButtons) {
  // Across the bar the combo box draws itself; down it, the button's image.
  if (bHorz && m_pWndCombo != NULL && m_pWndCombo->GetSafeHwnd() != NULL) return;
  CMFCToolBarButton::OnDraw(pDC, rect, pImages, bHorz, bCustomizeMode, bHighlight, bDrawBorder,
                            bGrayDisabledButtons);
}

void CMFCToolBarComboBoxButton::OnMove() {
  if (m_pWndCombo == NULL || m_pWndCombo->GetSafeHwnd() == NULL) return;
  CBasePane* pBar = DYNAMIC_DOWNCAST(CBasePane, m_pWndParent);
  BOOL bHorz = pBar == NULL || pBar->IsHorizontal();
  if (!bHorz || m_rect.IsRectEmpty() || IsHidden()) {
    m_pWndCombo->ShowWindow(SW_HIDE);
    return;
  }
  CRect rectCombo;
  m_pWndCombo->GetWindowRect(rectCombo);
  int cy = rectCombo.Height();
  int y = m_bCenterVert ? m_rect.top + (m_rect.Height() - cy) / 2 : m_rect.top;
  m_pWndCombo->SetWindowPos(NULL, m_rect.left + 1, y, m_rect.Width() - 2, cy + m_nDropDownHeight,
                            SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void CMFCToolBarComboBoxButton::OnShow(BOOL bShow) {
  if (m_pWndCombo != NULL && m_pWndCombo->GetSafeHwnd() != NULL) {
    if (bShow)
      OnMove();
    else
      m_pWndCombo->ShowWindow(SW_HIDE);
  }
}

CComboBox* CMFCToolBarComboBoxButton::CreateCombo(CWnd* pWndParent, const CRect& rect) {
  CComboBox* pCombo = new CComboBox;
  if (!pCombo->Create(m_dwStyle | WS_CHILD | WS_VSCROLL | WS_TABSTOP, rect, pWndParent, m_nID)) {
    delete pCombo;
    return NULL;
  }
  return pCombo;
}

void CMFCToolBarComboBoxButton::OnChangeParentWnd(CWnd* pWndParent) {
  CMFCToolBarButton::OnChangeParentWnd(pWndParent);
  if (m_pWndCombo != NULL) {
    if (m_pWndCombo->GetSafeHwnd() != NULL) {
      if (pWndParent != NULL && ::GetParent(m_pWndCombo->m_hWnd) == pWndParent->GetSafeHwnd()) return;
      m_pWndCombo->DestroyWindow();
    }
    delete m_pWndCombo;
    m_pWndCombo = NULL;
  }
  if (pWndParent == NULL || pWndParent->GetSafeHwnd() == NULL) return;
  m_pWndCombo = CreateCombo(pWndParent, CRect(0, 0, m_iWidth, m_nDropDownHeight));
  if (m_pWndCombo == NULL) return;
  CFont* pFont = pWndParent->GetFont();
  m_pWndCombo->SetFont(pFont != NULL ? pFont : &afxGlobalData.fontRegular);
  _SyncCombo();
  if (m_nStyle & TBBS_DISABLED) m_pWndCombo->EnableWindow(FALSE);
}

BOOL CMFCToolBarComboBoxButton::OnClick(CWnd*, BOOL) { return m_pWndCombo != NULL && m_pWndCombo->GetSafeHwnd() != NULL; }

BOOL CMFCToolBarComboBoxButton::NotifyCommand(int iNotifyCode) {
  if (m_pWndCombo == NULL || m_bInSync) return FALSE;
  switch (iNotifyCode) {
    case CBN_SELENDOK: {
      int i = m_pWndCombo->GetCurSel();
      if (i < 0) return FALSE;
      m_iSelIndex = i;
      m_pWndCombo->GetLBText(i, m_strEdit);
      // The other combo boxes of this command follow.
      CObList lst;
      CMFCToolBar::GetCommandButtons(m_nID, lst);
      for (POSITION pos = lst.GetHeadPosition(); pos != NULL;) {
        CMFCToolBarComboBoxButton* p = DYNAMIC_DOWNCAST(CMFCToolBarComboBoxButton, lst.GetNext(pos));
        if (p != NULL && p != this) p->SelectItem(i, FALSE);
      }
      return TRUE;  // the toolbar sends the command
    }
    case CBN_EDITCHANGE:
      m_pWndCombo->GetWindowText(m_strEdit);
      m_iSelIndex = FindItem(m_strEdit);
      return FALSE;
  }
  return FALSE;
}

CMFCToolBarComboBoxButton* AFXAPI CMFCToolBarComboBoxButton::GetByCmd(UINT uiCmd, BOOL bIsFocus) {
  CObList lst;
  CMFCToolBar::GetCommandButtons(uiCmd, lst);
  for (POSITION pos = lst.GetHeadPosition(); pos != NULL;) {
    CMFCToolBarComboBoxButton* p = DYNAMIC_DOWNCAST(CMFCToolBarComboBoxButton, lst.GetNext(pos));
    if (p == NULL) continue;
    if (!bIsFocus) return p;
    HWND hFocus = ::GetFocus();
    if (p->GetHwnd() != NULL && (hFocus == p->GetHwnd() || ::IsChild(p->GetHwnd(), hFocus))) return p;
  }
  return NULL;
}

BOOL AFXAPI CMFCToolBarComboBoxButton::SelectItemAll(UINT uiCmd, int iIndex) {
  CObList lst;
  CMFCToolBar::GetCommandButtons(uiCmd, lst);
  BOOL bAny = FALSE;
  for (POSITION pos = lst.GetHeadPosition(); pos != NULL;) {
    CMFCToolBarComboBoxButton* p = DYNAMIC_DOWNCAST(CMFCToolBarComboBoxButton, lst.GetNext(pos));
    if (p != NULL && p->SelectItem(iIndex, FALSE)) bAny = TRUE;
  }
  return bAny;
}

int AFXAPI CMFCToolBarComboBoxButton::GetCountAll(UINT uiCmd) {
  CMFCToolBarComboBoxButton* p = GetByCmd(uiCmd);
  return p != NULL ? (int)p->GetCount() : CB_ERR;
}

int AFXAPI CMFCToolBarComboBoxButton::GetCurSelAll(UINT uiCmd) {
  CMFCToolBarComboBoxButton* p = GetByCmd(uiCmd);
  return p != NULL ? p->GetCurSel() : CB_ERR;
}

LPCTSTR AFXAPI CMFCToolBarComboBoxButton::GetItemAll(UINT uiCmd, int iIndex) {
  CMFCToolBarComboBoxButton* p = GetByCmd(uiCmd);
  return p != NULL ? p->GetItem(iIndex) : NULL;
}

// ---------------------------------------------------------------------
// CMFCToolBar

IMPLEMENT_DYNCREATE(CMFCBaseToolBar, CPane)
IMPLEMENT_SERIAL(CMFCToolBar, CMFCBaseToolBar, VERSIONABLE_SCHEMA | 1)

CMFCToolBarImages CMFCToolBar::m_Images;
CMFCToolBarImages CMFCToolBar::m_ColdImages;
CMFCToolBarImages CMFCToolBar::m_DisabledImages;
CMFCToolBarImages CMFCToolBar::m_MenuImages;
CMFCToolBarImages CMFCToolBar::m_DisabledMenuImages;
CMFCToolBarImages* CMFCToolBar::m_pUserImages = NULL;
BOOL CMFCToolBar::m_bDisableLabelsEdit = FALSE;
BOOL CMFCToolBar::m_bDontScaleImages = FALSE;
BOOL CMFCToolBar::m_bShowTooltips = TRUE;
BOOL CMFCToolBar::m_bShowShortcutKeys = TRUE;
BOOL CMFCToolBar::m_bAltCustomization = TRUE;
BOOL CMFCToolBar::m_bLargeIcons = FALSE;
BOOL CMFCToolBar::m_bAutoGrayInactiveImages = FALSE;
BOOL CMFCToolBar::m_bExtCharTranslation = FALSE;
CSize CMFCToolBar::m_sizeButton(23, 22);
CSize CMFCToolBar::m_sizeImage(16, 15);
CSize CMFCToolBar::m_sizeMenuButton(22, 21);
CSize CMFCToolBar::m_sizeMenuImage(16, 15);
CList<UINT, UINT> CMFCToolBar::m_lstBasicCommands;
CList<UINT, UINT> CMFCToolBar::m_lstUnpermittedCommands;
COLORREF CMFCToolBar::m_clrTextHot = (COLORREF)-1;
CObList* CMFCToolBar::m_pAllToolbars = NULL;

BEGIN_MESSAGE_MAP(CMFCToolBar, CMFCBaseToolBar)
  ON_WM_CREATE()
  ON_WM_DESTROY()
  ON_WM_SIZE()
  ON_WM_MOUSEMOVE()
  ON_WM_LBUTTONDOWN()
  ON_WM_LBUTTONUP()
  ON_WM_LBUTTONDBLCLK()
  ON_WM_CANCELMODE()
  ON_WM_CONTEXTMENU()
  ON_MESSAGE(WM_MOUSELEAVE, OnMouseLeave)
  ON_MESSAGE(WM_SETFONT, OnSetFont)
  ON_MESSAGE(WM_GETFONT, OnGetFont)
  ON_NOTIFY_EX_RANGE(TTN_NEEDTEXT, 0, 0xFFFF, OnToolTipText)
END_MESSAGE_MAP()

// Command UI for a toolbar button.
class CMFCToolBarCmdUI : public CCmdUI {
public:
  CMFCToolBarButton* m_pButton;
  BOOL m_bChanged;
  CMFCToolBarCmdUI() : m_pButton(NULL), m_bChanged(FALSE) {}
  void _SetStyle(UINT nStyle) {
    if (nStyle != m_pButton->m_nStyle) {
      m_pButton->SetStyle(nStyle);
      m_bChanged = TRUE;
    }
  }
  virtual void Enable(BOOL bOn) {
    m_bEnableChanged = TRUE;
    BOOL bWas = (m_pButton->m_nStyle & TBBS_DISABLED) == 0;
    _SetStyle(bOn ? (m_pButton->m_nStyle & ~TBBS_DISABLED) : (m_pButton->m_nStyle | TBBS_DISABLED));
    if (bWas != bOn) m_pButton->EnableWindow(bOn);
  }
  virtual void SetCheck(int nCheck) {
    UINT n = m_pButton->m_nStyle & ~(TBBS_CHECKED | TBBS_INDETERMINATE);
    if (nCheck == 1) n |= TBBS_CHECKED;
    if (nCheck == 2) n |= TBBS_INDETERMINATE;
    _SetStyle(n);
  }
  virtual void SetRadio(BOOL bOn) { SetCheck(bOn ? 1 : 0); }
  virtual void SetText(LPCTSTR) {}
};

CMFCToolBar::CMFCToolBar()
    : m_iSelected(-1), m_sizeCurButtonLocked(23, 22), m_sizeCurImageLocked(16, 15), m_iHighlighted(-1),
      m_iButtonCapture(-1), m_bLocked(FALSE), m_bTextLabels(FALSE), m_bCustomizeButton(FALSE),
      m_bRouteCommandsViaFrame(TRUE), m_bUserDefined(FALSE), m_bTracking(FALSE), m_bHorzLayout(TRUE),
      m_ptDragStart(0, 0), m_iCustomizeCmd(0), m_uiOriginalResID(0), m_pToolTip(NULL), m_hFont(NULL) {}

CMFCToolBar::~CMFCToolBar() {
  RemoveAllButtons();
  if (m_pAllToolbars != NULL) {
    POSITION pos = m_pAllToolbars->Find(this);
    if (pos != NULL) m_pAllToolbars->RemoveAt(pos);
  }
  delete m_pToolTip;
}

const CObList& AFXAPI CMFCToolBar::GetAllToolbars() {
  if (m_pAllToolbars == NULL) m_pAllToolbars = new CObList;
  return *m_pAllToolbars;
}

BOOL CMFCToolBar::Create(CWnd* pParentWnd, DWORD dwStyle, UINT nID) {
  return CreateEx(pParentWnd, TBSTYLE_FLAT, dwStyle, CRect(1, 1, 1, 1), nID);
}

BOOL CMFCToolBar::CreateEx(CWnd* pParentWnd, DWORD, DWORD dwStyle, CRect rcBorders, UINT nID) {
  m_rectBorders = rcBorders;
  if (!CPane::CreateEx(0, NULL, dwStyle, CRect(0, 0, 0, 0), pParentWnd, nID, AFX_CBRS_FLOAT | AFX_CBRS_CLOSE))
    return FALSE;
  GetAllToolbars();
  if (m_pAllToolbars->Find(this) == NULL) m_pAllToolbars->AddTail(this);
  return TRUE;
}

int CMFCToolBar::OnCreate(LPCREATESTRUCT lpCreateStruct) {
  if (CMFCBaseToolBar::OnCreate(lpCreateStruct) == -1) return -1;
  if ((m_dwStyle & CBRS_TOOLTIPS) && m_bShowTooltips) {
    m_pToolTip = new CToolTipCtrl;
    if (!m_pToolTip->Create(this, TTS_ALWAYSTIP)) {
      delete m_pToolTip;
      m_pToolTip = NULL;
    } else {
      m_pToolTip->Activate(TRUE);
    }
  }
  return 0;
}

void CMFCToolBar::OnDestroy() {
  if (m_pToolTip != NULL && m_pToolTip->GetSafeHwnd() != NULL) m_pToolTip->DestroyWindow();
  delete m_pToolTip;
  m_pToolTip = NULL;
  CMFCBaseToolBar::OnDestroy();
}

void CMFCToolBar::_AddButtonsFromIDs(const UINT* lpIDArray, int nIDCount, int iFirstImage) {
  int iImage = iFirstImage;
  for (int i = 0; i < nIDCount; i++) {
    UINT nID = lpIDArray != NULL ? lpIDArray[i] : 0;
    if (nID == 0) {
      InsertSeparator();
      continue;
    }
    int iThis = iFirstImage >= 0 ? iImage++ : GetCmdMgr()->GetCmdImage(nID, FALSE);
    InsertButton(CMFCToolBarButton(nID, iThis, NULL, FALSE, m_bLocked));
  }
}

BOOL CMFCToolBar::LoadToolBar(UINT uiResID, UINT, UINT, BOOL bLocked, UINT, UINT, UINT uiHotResID) {
  LPCTSTR lpszName = MAKEINTRESOURCE(uiResID);
  HINSTANCE hInst = AfxFindResourceHandle(lpszName, RT_TOOLBAR);
  HRSRC hRsrc = ::FindResource(hInst, lpszName, RT_TOOLBAR);
  if (hRsrc == NULL) return FALSE;
  HGLOBAL hGlobal = ::LoadResource(hInst, hRsrc);
  const WORD* pData = hGlobal != NULL ? (const WORD*)::LockResource(hGlobal) : NULL;
  if (pData == NULL || pData[0] != 1) return FALSE;  // version, width, height, count, then the IDs
  CSize sizeImage(pData[1], pData[2]);
  int nCount = pData[3];
  m_bLocked = bLocked;
  CMFCToolBarImages* pImages;
  if (bLocked) {
    pImages = &m_ImagesLocked;
    m_sizeCurImageLocked = sizeImage;
    m_sizeCurButtonLocked = CSize(sizeImage.cx + 7, sizeImage.cy + 7);
    pImages->Clear();
  } else {
    pImages = &m_Images;
    if (m_Images.GetCount() == 0) SetSizes(CSize(sizeImage.cx + 7, sizeImage.cy + 7), sizeImage);
  }
  pImages->SetImageSize(sizeImage);
  int iFirst = pImages->GetCount();
  if (!pImages->Load(uiHotResID != 0 ? uiHotResID : uiResID, hInst, TRUE)) iFirst = -1;
  UINT* pIDs = new UINT[nCount + 1];
  for (int i = 0; i < nCount; i++) pIDs[i] = pData[4 + i];
  RemoveAllButtons();
  _AddButtonsFromIDs(pIDs, nCount, iFirst >= 0 ? iFirst : -1);
  delete[] pIDs;
  m_uiOriginalResID = uiResID;
  AdjustLayout();
  return TRUE;
}

BOOL CMFCToolBar::LoadToolBarEx(UINT uiToolbarResID, void*, BOOL bLocked) {
  return LoadToolBar(uiToolbarResID, 0, 0, bLocked);
}

BOOL CMFCToolBar::LoadBitmap(UINT uiResID, UINT, UINT, BOOL bLocked, UINT, UINT) {
  m_bLocked = bLocked;
  return (bLocked ? m_ImagesLocked : m_Images).Load(uiResID, NULL, !bLocked);
}

BOOL CMFCToolBar::SetButtons(const UINT* lpIDArray, int nIDCount, BOOL bRemapImages) {
  RemoveAllButtons();
  if (lpIDArray == NULL) {
    for (int i = 0; i < nIDCount; i++) InsertSeparator();
  } else if (bRemapImages) {
    _AddButtonsFromIDs(lpIDArray, nIDCount, -1);
  } else {
    _AddButtonsFromIDs(lpIDArray, nIDCount, 0);
  }
  AdjustLayout();
  return TRUE;
}

CMFCToolBarButton* CMFCToolBar::_CreateCopy(const CMFCToolBarButton& button) {
  CRuntimeClass* pClass = button.GetRuntimeClass();
  CMFCToolBarButton* pButton = DYNAMIC_DOWNCAST(CMFCToolBarButton, pClass->CreateObject());
  if (pButton == NULL) pButton = new CMFCToolBarButton;
  pButton->CopyFrom(button);
  return pButton;
}

int CMFCToolBar::InsertButton(const CMFCToolBarButton& button, INT_PTR iInsertAt) {
  CMFCToolBarButton* pButton = _CreateCopy(button);
  if (pButton->m_strText.IsEmpty() && !pButton->IsSeparator() && pButton->m_nID != 0) {
    // The label: the command's tooltip text.
    pButton->m_strText = _AfxCommandTip(pButton->m_nID);
  }
  if (m_bTextLabels) pButton->m_bText = TRUE;
  int iIndex;
  POSITION pos = iInsertAt >= 0 ? m_Buttons.FindIndex(iInsertAt) : NULL;
  if (pos == NULL) {
    m_Buttons.AddTail(pButton);
    iIndex = GetCount() - 1;
  } else {
    m_Buttons.InsertBefore(pos, pButton);
    iIndex = (int)iInsertAt;
  }
  pButton->OnChangeParentWnd(this);
  if (m_hWnd != NULL) AdjustLayout();
  return iIndex;
}

int CMFCToolBar::InsertSeparator(INT_PTR iInsertAt) {
  CMFCToolBarButton sep;
  sep.m_nStyle = TBBS_SEPARATOR;
  return InsertButton(sep, iInsertAt);
}

BOOL CMFCToolBar::RemoveButton(int iIndex) {
  POSITION pos = iIndex >= 0 ? m_Buttons.FindIndex(iIndex) : NULL;
  if (pos == NULL) return FALSE;
  CMFCToolBarButton* pButton = (CMFCToolBarButton*)m_Buttons.GetAt(pos);
  m_Buttons.RemoveAt(pos);
  delete pButton;
  if (m_iHighlighted >= GetCount()) m_iHighlighted = -1;
  m_iButtonCapture = -1;
  if (m_hWnd != NULL) AdjustLayout();
  return TRUE;
}

void CMFCToolBar::RemoveAllButtons() {
  while (!m_Buttons.IsEmpty()) delete (CMFCToolBarButton*)m_Buttons.RemoveHead();
  m_iHighlighted = m_iButtonCapture = -1;
}

int CMFCToolBar::ReplaceButton(UINT uiCmd, const CMFCToolBarButton& button, BOOL bAll) {
  int nCount = 0;
  for (POSITION pos = m_Buttons.GetHeadPosition(); pos != NULL;) {
    POSITION posThis = pos;
    CMFCToolBarButton* pOld = (CMFCToolBarButton*)m_Buttons.GetNext(pos);
    if (pOld->m_nID != uiCmd) continue;
    CMFCToolBarButton* pNew = _CreateCopy(button);
    m_Buttons.SetAt(posThis, pNew);
    delete pOld;
    pNew->OnChangeParentWnd(this);
    nCount++;
    if (!bAll) break;
  }
  if (nCount > 0 && m_hWnd != NULL) AdjustLayout();
  return nCount;
}

CMFCToolBarButton* CMFCToolBar::GetButton(int iIndex) const {
  POSITION pos = iIndex >= 0 ? m_Buttons.FindIndex(iIndex) : NULL;
  return pos != NULL ? (CMFCToolBarButton*)m_Buttons.GetAt(pos) : NULL;
}

int CMFCToolBar::CommandToIndex(UINT nIDFind, int iIndexFirst) const {
  int i = 0;
  for (POSITION pos = m_Buttons.GetHeadPosition(); pos != NULL; i++) {
    CMFCToolBarButton* p = (CMFCToolBarButton*)m_Buttons.GetNext(pos);
    if (i >= iIndexFirst && p->m_nID == nIDFind && !p->IsSeparator()) return i;
  }
  return -1;
}

UINT CMFCToolBar::GetItemID(int nIndex) const {
  CMFCToolBarButton* p = GetButton(nIndex);
  return p != NULL ? p->m_nID : 0;
}

void CMFCToolBar::GetItemRect(int nIndex, LPRECT lpRect) const {
  CMFCToolBarButton* p = GetButton(nIndex);
  if (p != NULL)
    *lpRect = p->Rect();
  else
    ::SetRectEmpty(lpRect);
}

UINT CMFCToolBar::GetButtonStyle(int nIndex) const {
  CMFCToolBarButton* p = GetButton(nIndex);
  return p != NULL ? p->m_nStyle : 0;
}

void CMFCToolBar::SetButtonStyle(int nIndex, UINT nStyle) {
  CMFCToolBarButton* p = GetButton(nIndex);
  if (p == NULL) return;
  UINT nOld = p->m_nStyle;
  p->SetStyle(nStyle);
  if (m_hWnd == NULL) return;
  if ((nOld ^ nStyle) & (TBBS_SEPARATOR | TBBS_HIDDEN))
    AdjustLayout();
  else
    InvalidateRect(p->Rect());
}

CString CMFCToolBar::GetButtonText(int nIndex) const {
  CMFCToolBarButton* p = GetButton(nIndex);
  return p != NULL ? p->m_strText : CString();
}

BOOL CMFCToolBar::SetButtonText(int nIndex, LPCTSTR lpszText) {
  CMFCToolBarButton* p = GetButton(nIndex);
  if (p == NULL) return FALSE;
  p->m_strText = lpszText != NULL ? lpszText : _T("");
  if (m_hWnd != NULL) AdjustLayout();
  return TRUE;
}

void CMFCToolBar::GetButtonInfo(int nIndex, UINT& nID, UINT& nStyle, int& iImage) const {
  CMFCToolBarButton* p = GetButton(nIndex);
  nID = p != NULL ? p->m_nID : 0;
  nStyle = p != NULL ? p->m_nStyle : 0;
  iImage = p != NULL ? p->GetImage() : -1;
}

void CMFCToolBar::SetButtonInfo(int nIndex, UINT nID, UINT nStyle, int iImage) {
  CMFCToolBarButton* p = GetButton(nIndex);
  if (p == NULL) return;
  p->m_nID = nID;
  p->SetStyle(nStyle);
  p->SetImage(iImage);
  if (m_hWnd != NULL) AdjustLayout();
}

BOOL CMFCToolBar::SetToolBarBtnText(UINT nBtnIndex, LPCTSTR szText, BOOL bShowText, BOOL bShowImage) {
  CMFCToolBarButton* p = GetButton((int)nBtnIndex);
  if (p == NULL) return FALSE;
  if (szText != NULL) p->m_strText = szText;
  p->m_bText = bShowText;
  p->m_bImage = bShowImage;
  if (m_hWnd != NULL) AdjustLayout();
  return TRUE;
}

int CMFCToolBar::HitTest(CPoint point) {
  int i = 0;
  for (POSITION pos = m_Buttons.GetHeadPosition(); pos != NULL; i++) {
    CMFCToolBarButton* p = (CMFCToolBarButton*)m_Buttons.GetNext(pos);
    if (!p->IsSeparator() && !p->IsHidden() && p->Rect().PtInRect(point)) return i;
  }
  return -1;
}

int AFXAPI CMFCToolBar::GetCommandButtons(UINT uiCmd, CObList& listButtons) {
  listButtons.RemoveAll();
  const CObList& lst = GetAllToolbars();
  for (POSITION pos = lst.GetHeadPosition(); pos != NULL;) {
    CMFCToolBar* pBar = (CMFCToolBar*)lst.GetNext(pos);
    for (POSITION posB = pBar->m_Buttons.GetHeadPosition(); posB != NULL;) {
      CMFCToolBarButton* p = (CMFCToolBarButton*)pBar->m_Buttons.GetNext(posB);
      if (p->m_nID == uiCmd && !p->IsSeparator()) listButtons.AddTail(p);
    }
  }
  return (int)listButtons.GetCount();
}

CRect CMFCToolBar::_GripperRect() const {
  if (!HasGripper() || m_pMiniFrame != NULL || m_hWnd == NULL) return CRect(0, 0, 0, 0);
  CRect r;
  GetClientRect(r);
  if (const_cast<CMFCToolBar*>(this)->IsHorizontal())
    r.right = r.left + kGripper;
  else
    r.bottom = r.top + kGripper;
  return r;
}

CRect CMFCToolBar::_CustomizeRect() const {
  if (!m_bCustomizeButton || m_hWnd == NULL) return CRect(0, 0, 0, 0);
  CRect r;
  GetClientRect(r);
  if (const_cast<CMFCToolBar*>(this)->IsHorizontal())
    r.left = r.right - kCustomize;
  else
    r.top = r.bottom - kCustomize;
  return r;
}

CSize CMFCToolBar::_Layout(BOOL bHorz, BOOL bApply) {
  CClientDC dc(m_hWnd != NULL ? this : CWnd::GetDesktopWindow());
  HFONT hFont = m_hFont != NULL ? m_hFont : (HFONT)afxGlobalData.fontRegular.GetSafeHandle();
  HGDIOBJ hOld = ::SelectObject(dc.GetSafeHdc(), hFont);
  BOOL bGripper = HasGripper() && m_pMiniFrame == NULL;
  int x = (bHorz ? m_rectBorders.left : m_rectBorders.top) + (bGripper ? kGripper : 0);
  int nDepth = 0;
  CSize sizeButton = GetButtonSize();
  // The buttons' depth across the bar: the tallest one.
  for (POSITION pos = m_Buttons.GetHeadPosition(); pos != NULL;) {
    CMFCToolBarButton* p = (CMFCToolBarButton*)m_Buttons.GetNext(pos);
    if (p->IsHidden()) continue;
    CSize s = p->OnCalculateSize(&dc, sizeButton, bHorz);
    int d = bHorz ? s.cy : s.cx;
    if (d > nDepth) nDepth = d;
  }
  if (nDepth == 0) nDepth = bHorz ? sizeButton.cy : sizeButton.cx;
  int y0 = bHorz ? m_rectBorders.top : m_rectBorders.left;
  for (POSITION pos = m_Buttons.GetHeadPosition(); pos != NULL;) {
    CMFCToolBarButton* p = (CMFCToolBarButton*)m_Buttons.GetNext(pos);
    if (p->IsHidden()) {
      if (bApply) {
        p->SetRect(CRect(0, 0, 0, 0));
        p->OnShow(FALSE);
      }
      continue;
    }
    CSize s = p->OnCalculateSize(&dc, sizeButton, bHorz);
    int cx = bHorz ? s.cx : s.cy;
    if (bApply) {
      CRect r = bHorz ? CRect(x, y0, x + cx, y0 + nDepth) : CRect(y0, x, y0 + nDepth, x + cx);
      p->SetRect(r);
      p->OnShow(TRUE);
    }
    x += cx;
  }
  x += (bHorz ? m_rectBorders.right : m_rectBorders.bottom) + (m_bCustomizeButton ? kCustomize : 0);
  int cy = nDepth + (bHorz ? m_rectBorders.top + m_rectBorders.bottom : m_rectBorders.left + m_rectBorders.right);
  ::SelectObject(dc.GetSafeHdc(), hOld);
  return bHorz ? CSize(x, cy) : CSize(cy, x);
}

void CMFCToolBar::AdjustLayout() {
  if (m_hWnd == NULL) return;
  m_bHorzLayout = IsHorizontal();
  _Layout(m_bHorzLayout, TRUE);
  _UpdateTooltips();
  Invalidate();
  if (m_pMiniFrame != NULL) m_pMiniFrame->OnPaneRecalcLayout();
}

CSize CMFCToolBar::CalcSize(BOOL bVertDock) { return _Layout(!bVertDock, FALSE); }

CSize CMFCToolBar::CalcFixedLayout(BOOL, BOOL bHorz) { return CalcSize(!bHorz); }

void CMFCToolBar::OnSize(UINT nType, int cx, int cy) {
  CMFCBaseToolBar::OnSize(nType, cx, cy);
  if (m_bHorzLayout != IsHorizontal() || cx > 0) {
    m_bHorzLayout = IsHorizontal();
    _Layout(m_bHorzLayout, TRUE);
    _UpdateTooltips();
    Invalidate();
  }
}

void CMFCToolBar::_UpdateTooltips() {
  if (m_pToolTip == NULL || m_pToolTip->GetSafeHwnd() == NULL) return;
  while (m_pToolTip->GetToolCount() > 0) {
    TOOLINFO ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    if (!::SendMessage(m_pToolTip->m_hWnd, TTM_ENUMTOOLS, 0, (LPARAM)&ti)) break;
    ::SendMessage(m_pToolTip->m_hWnd, TTM_DELTOOL, 0, (LPARAM)&ti);
  }
  for (POSITION pos = m_Buttons.GetHeadPosition(); pos != NULL;) {
    CMFCToolBarButton* p = (CMFCToolBarButton*)m_Buttons.GetNext(pos);
    if (p->IsSeparator() || p->IsHidden() || p->m_nID == 0 || p->Rect().IsRectEmpty()) continue;
    if (p->GetHwnd() != NULL) continue;  // a combo box has its own window
    CRect r = p->Rect();
    m_pToolTip->AddTool(this, LPSTR_TEXTCALLBACK, r, p->m_nID);
  }
}

BOOL CMFCToolBar::OnToolTipText(UINT, NMHDR* pNMHDR, LRESULT* pResult) {
  NMTTDISPINFO* pTTT = (NMTTDISPINFO*)pNMHDR;
  UINT nID = (UINT)pNMHDR->idFrom;
  if (pTTT->uFlags & TTF_IDISHWND) nID = ::GetDlgCtrlID((HWND)pNMHDR->idFrom);
  CMFCToolBarButton* pButton = NULL;
  for (POSITION pos = m_Buttons.GetHeadPosition(); pos != NULL;) {
    CMFCToolBarButton* p = (CMFCToolBarButton*)m_Buttons.GetNext(pos);
    if (p->m_nID == nID) {
      pButton = p;
      break;
    }
  }
  CString strTip;
  if (pButton == NULL || !OnUserToolTip(pButton, strTip)) {
    if (pButton == NULL || !pButton->OnGetCustomToolTipText(strTip)) strTip = _AfxCommandTip(nID);
    if (strTip.IsEmpty() && pButton != NULL) strTip = pButton->m_strText;
  }
  static TCHAR s_szTip[256];
  lstrcpyn(s_szTip, strTip, 256);
  pTTT->lpszText = s_szTip;
  pTTT->hinst = NULL;
  *pResult = 0;
  return TRUE;
}

BOOL CMFCToolBar::PreTranslateMessage(MSG* pMsg) {
  if (m_pToolTip != NULL && m_pToolTip->GetSafeHwnd() != NULL && pMsg->hwnd == m_hWnd &&
      pMsg->message >= WM_MOUSEFIRST && pMsg->message <= WM_MOUSELAST)
    m_pToolTip->RelayEvent(pMsg);
  if (pMsg->message == WM_KEYDOWN && (pMsg->wParam == VK_RETURN || pMsg->wParam == VK_ESCAPE)) {
    // Enter in a combo box's edit runs its command; Esc goes back to the view.
    for (POSITION pos = m_Buttons.GetHeadPosition(); pos != NULL;) {
      CMFCToolBarComboBoxButton* p = DYNAMIC_DOWNCAST(CMFCToolBarComboBoxButton, m_Buttons.GetNext(pos));
      if (p == NULL || p->GetHwnd() == NULL) continue;
      if (pMsg->hwnd != p->GetHwnd() && !::IsChild(p->GetHwnd(), pMsg->hwnd)) continue;
      if (::SendMessage(p->GetHwnd(), CB_GETDROPPEDSTATE, 0, 0)) return FALSE;
      if (pMsg->wParam == VK_RETURN) {
        p->NotifyCommand(CBN_EDITCHANGE);
        _SendCommand(p);
      }
      CWnd* pTarget = _AfxBarCommandWnd(this);
      CFrameWnd* pFrame = DYNAMIC_DOWNCAST(CFrameWnd, pTarget);
      if (pFrame != NULL && pFrame->GetActiveFrame()->GetActiveView() != NULL)
        pFrame->GetActiveFrame()->GetActiveView()->SetFocus();
      return TRUE;
    }
  }
  return CMFCBaseToolBar::PreTranslateMessage(pMsg);
}

BOOL CMFCToolBar::OnCommand(WPARAM wParam, LPARAM lParam) {
  if (lParam != 0) {
    // A combo box's notification.
    for (POSITION pos = m_Buttons.GetHeadPosition(); pos != NULL;) {
      CMFCToolBarButton* p = (CMFCToolBarButton*)m_Buttons.GetNext(pos);
      if (p->GetHwnd() == (HWND)lParam) {
        CMFCToolBarComboBoxButton* pCombo = DYNAMIC_DOWNCAST(CMFCToolBarComboBoxButton, p);
        if (pCombo != NULL && pCombo->NotifyCommand(HIWORD(wParam))) _SendCommand(p);
        return TRUE;
      }
    }
  }
  // A menu command picked here goes to the frame.
  CWnd* pTarget = _AfxBarCommandWnd(this);
  if (pTarget != NULL && lParam == 0) return (BOOL)pTarget->SendMessage(WM_COMMAND, wParam, 0) || TRUE;
  return CMFCBaseToolBar::OnCommand(wParam, lParam);
}

CFrameWnd* CMFCToolBar::_CommandTarget() const {
  if (!m_bRouteCommandsViaFrame) {
    CFrameWnd* pOwner = DYNAMIC_DOWNCAST(CFrameWnd, GetOwner());
    if (pOwner != NULL) return pOwner;
  }
  return DYNAMIC_DOWNCAST(CFrameWnd, _AfxBarCommandWnd(const_cast<CMFCToolBar*>(this)));
}

void CMFCToolBar::_SendCommand(CMFCToolBarButton* pButton) {
  if (pButton == NULL || pButton->m_nID == 0 || (pButton->m_nStyle & TBBS_DISABLED)) return;
  if (OnSendCommand(pButton)) return;
  CWnd* pTarget = m_bRouteCommandsViaFrame ? _AfxBarCommandWnd(this) : GetOwner();
  if (pTarget == NULL) return;
  pTarget->SendMessage(WM_SETMESSAGESTRING, AFX_IDS_IDLEMESSAGE);
  pTarget->SendMessage(WM_COMMAND, MAKEWPARAM(pButton->m_nID, 0), 0);
}

void CMFCToolBar::OnChangeHot(int iHot) {
  if (iHot == m_iHighlighted) return;
  CMFCToolBarButton* pOld = GetButton(m_iHighlighted);
  if (pOld != NULL) InvalidateRect(pOld->Rect());
  m_iHighlighted = iHot;
  CMFCToolBarButton* pNew = GetButton(iHot);
  if (pNew != NULL) InvalidateRect(pNew->Rect());
  // The status bar's prompt follows the mouse.
  if (m_dwStyle & CBRS_FLYBY) {
    CWnd* pTarget = _AfxBarCommandWnd(this);
    if (pTarget != NULL)
      pTarget->SendMessage(WM_SETMESSAGESTRING, pNew != NULL && pNew->m_nID != 0 ? pNew->m_nID : AFX_IDS_IDLEMESSAGE);
  }
}

void CMFCToolBar::OnMouseMove(UINT nFlags, CPoint point) {
  if (m_bTracking) {
    CPoint ptScreen = point;
    ClientToScreen(&ptScreen);
    CSize d = ptScreen - m_ptDragStart;
    if (abs(d.cx) > ::GetSystemMetrics(SM_CXDRAG) || abs(d.cy) > ::GetSystemMetrics(SM_CYDRAG)) {
      m_bTracking = FALSE;
      ReleaseCapture();
      CRect rectWnd;
      GetWindowRect(rectWnd);
      if (FloatPane(CRect(rectWnd.TopLeft() + d, CSize(0, 0)), DM_MOUSE) && m_pMiniFrame != NULL)
        ((CWnd*)m_pMiniFrame)->SendMessage(WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(ptScreen.x, ptScreen.y));
    }
    return;
  }
  int iHit = HitTest(point);
  if (m_iButtonCapture >= 0) {
    CMFCToolBarButton* p = GetButton(m_iButtonCapture);
    if (p != NULL) {
      UINT nStyle = iHit == m_iButtonCapture ? (p->m_nStyle | TBBS_PRESSED) : (p->m_nStyle & ~TBBS_PRESSED);
      if (nStyle != p->m_nStyle) {
        p->SetStyle(nStyle);
        InvalidateRect(p->Rect());
      }
    }
    return;
  }
  OnChangeHot(iHit);
  TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, m_hWnd, 0};
  ::TrackMouseEvent(&tme);
  CMFCBaseToolBar::OnMouseMove(nFlags, point);
}

LRESULT CMFCToolBar::OnMouseLeave(WPARAM, LPARAM) {
  if (m_iButtonCapture < 0) OnChangeHot(-1);
  return 0;
}

void CMFCToolBar::_ShowCustomizeMenu() {
  CMenu menu;
  if (!menu.CreatePopupMenu()) return;
  CString strText = m_strCustomizeText;
  if (strText.IsEmpty()) strText = _T("&Customize...");
  menu.AppendMenu(MF_STRING, (UINT)m_iCustomizeCmd, strText);
  CRect r = _CustomizeRect();
  ClientToScreen(r);
  CWnd* pTarget = _AfxBarCommandWnd(this);
  UINT nCmd = (UINT)menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RETURNCMD, r.left, r.bottom, pTarget);
  if (nCmd != 0 && pTarget != NULL) pTarget->SendMessage(WM_COMMAND, nCmd);
}

void CMFCToolBar::OnLButtonDown(UINT nFlags, CPoint point) {
  if (_GripperRect().PtInRect(point)) {
    if (CanFloat()) {
      m_bTracking = TRUE;
      m_ptDragStart = point;
      ClientToScreen(&m_ptDragStart);
      SetCapture();
    }
    return;
  }
  if (_CustomizeRect().PtInRect(point)) {
    _ShowCustomizeMenu();
    return;
  }
  int iHit = HitTest(point);
  CMFCToolBarButton* p = GetButton(iHit);
  if (p == NULL || (p->m_nStyle & TBBS_DISABLED)) {
    CMFCBaseToolBar::OnLButtonDown(nFlags, point);
    return;
  }
  CMFCToolBarMenuButton* pMenu = DYNAMIC_DOWNCAST(CMFCToolBarMenuButton, p);
  if (pMenu != NULL) {
    CRect r = p->Rect();
    pMenu->m_bClickedOnMenu = pMenu->m_bDrawDownArrow &&
                              (IsHorizontal() ? point.x >= r.right - kMenuArrow : point.y >= r.bottom - kMenuArrow);
  }
  if (p->OnClick(this, FALSE)) {
    if (::IsWindow(m_hWnd)) {
      OnChangeHot(-1);
      Invalidate();
    }
    return;
  }
  m_iButtonCapture = iHit;
  p->SetStyle(p->m_nStyle | TBBS_PRESSED);
  InvalidateRect(p->Rect());
  SetCapture();
}

void CMFCToolBar::OnLButtonUp(UINT nFlags, CPoint point) {
  if (m_bTracking) {
    m_bTracking = FALSE;
    ReleaseCapture();
    return;
  }
  if (m_iButtonCapture >= 0) {
    int iCapture = m_iButtonCapture;
    m_iButtonCapture = -1;
    ReleaseCapture();
    CMFCToolBarButton* p = GetButton(iCapture);
    if (p != NULL) {
      BOOL bRun = HitTest(point) == iCapture;
      p->SetStyle(p->m_nStyle & ~TBBS_PRESSED);
      InvalidateRect(p->Rect());
      if (bRun) {
        HWND hWnd = m_hWnd;
        p->OnClickUp();
        _SendCommand(p);
        if (!::IsWindow(hWnd)) return;
      }
    }
    return;
  }
  CMFCBaseToolBar::OnLButtonUp(nFlags, point);
}

void CMFCToolBar::OnLButtonDblClk(UINT nFlags, CPoint point) {
  if (_GripperRect().PtInRect(point) && CanFloat()) {
    CRect rectWnd;
    GetWindowRect(rectWnd);
    FloatPane(CRect(rectWnd.TopLeft(), CSize(0, 0)), DM_DBL_CLICK);
    return;
  }
  OnLButtonDown(nFlags, point);
}

void CMFCToolBar::OnCancelMode() {
  CMFCBaseToolBar::OnCancelMode();
  m_bTracking = FALSE;
  if (m_iButtonCapture >= 0) {
    CMFCToolBarButton* p = GetButton(m_iButtonCapture);
    if (p != NULL) p->SetStyle(p->m_nStyle & ~TBBS_PRESSED);
    m_iButtonCapture = -1;
    Invalidate();
  }
  if (::GetCapture() == m_hWnd) ReleaseCapture();
}

void CMFCToolBar::OnContextMenu(CWnd* pWnd, CPoint point) {
  CDockingManager* pManager = GetDockingManager();
  if (pManager != NULL && pManager->OnPaneContextMenu(point)) return;
  CMFCBaseToolBar::OnContextMenu(pWnd, point);
}

LRESULT CMFCToolBar::OnSetFont(WPARAM wParam, LPARAM lParam) {
  m_hFont = (HFONT)wParam;
  for (POSITION pos = m_Buttons.GetHeadPosition(); pos != NULL;) {
    CMFCToolBarButton* p = (CMFCToolBarButton*)m_Buttons.GetNext(pos);
    if (p->GetHwnd() != NULL) ::SendMessage(p->GetHwnd(), WM_SETFONT, wParam, lParam);
  }
  if (LOWORD(lParam)) AdjustLayout();
  return 0;
}

LRESULT CMFCToolBar::OnGetFont(WPARAM, LPARAM) {
  return (LRESULT)(m_hFont != NULL ? m_hFont : (HFONT)afxGlobalData.fontRegular.GetSafeHandle());
}

void CMFCToolBar::EnableTextLabels(BOOL bEnable) {
  m_bTextLabels = bEnable;
  for (POSITION pos = m_Buttons.GetHeadPosition(); pos != NULL;) ((CMFCToolBarButton*)m_Buttons.GetNext(pos))->m_bText = bEnable;
  if (m_hWnd != NULL) AdjustLayout();
}

void CMFCToolBar::EnableCustomizeButton(BOOL bEnable, int iCustomizeCmd, const CString& strCustomizeText, BOOL) {
  m_bCustomizeButton = bEnable;
  m_iCustomizeCmd = iCustomizeCmd;
  m_strCustomizeText = strCustomizeText;
  if (m_hWnd != NULL) AdjustLayout();
}

void CMFCToolBar::EnableCustomizeButton(BOOL bEnable, int iCustomizeCmd, UINT uiCustomizeTextResId,
                                        BOOL bQuickCustomize) {
  CString str;
  str.LoadString(uiCustomizeTextResId);
  EnableCustomizeButton(bEnable, iCustomizeCmd, str, bQuickCustomize);
}

void CMFCToolBar::SetLockedSizes(SIZE sizeButton, SIZE sizeImage, BOOL) {
  m_sizeCurButtonLocked = sizeButton;
  m_sizeCurImageLocked = sizeImage;
  m_ImagesLocked.SetImageSize(sizeImage);
}

void CMFCToolBar::OnUpdateCmdUI(CFrameWnd* pTarget, BOOL bDisableIfNoHndler) {
  CMFCToolBarCmdUI state;
  state.m_pOther = this;
  state.m_nIndexMax = (UINT)GetCount();
  state.m_nIndex = 0;
  for (POSITION pos = m_Buttons.GetHeadPosition(); pos != NULL; state.m_nIndex++) {
    CMFCToolBarButton* p = (CMFCToolBarButton*)m_Buttons.GetNext(pos);
    if (p->IsSeparator() || p->m_nID == 0) continue;
    CMFCToolBarMenuButton* pMenu = DYNAMIC_DOWNCAST(CMFCToolBarMenuButton, p);
    if (pMenu != NULL && pMenu->m_bMenuMode) continue;
    state.m_pButton = p;
    state.m_nID = p->m_nID;
    state.m_bChanged = FALSE;
    state.DoUpdate(pTarget, bDisableIfNoHndler && (pMenu == NULL || pMenu->m_hMenu == NULL));
    if (state.m_bChanged) InvalidateRect(p->Rect());
  }
}

void CMFCToolBar::DoPaint(CDC* pDCPaint) {
  CRect rectClient;
  GetClientRect(rectClient);
  CDC dc;
  CBitmap bmp;
  CDC* pDC = pDCPaint;
  if (dc.CreateCompatibleDC(pDCPaint) && bmp.CreateCompatibleBitmap(pDCPaint, rectClient.Width(), rectClient.Height())) {
    dc.SelectObject(&bmp);
    pDC = &dc;
  }
  CMFCVisualManager* pVM = CMFCVisualManager::GetInstance();
  pVM->OnFillBarBackground(pDC, this, rectClient, rectClient);
  BOOL bHorz = IsHorizontal();
  CRect rectGripper = _GripperRect();
  if (!rectGripper.IsRectEmpty()) pVM->OnDrawBarGripper(pDC, rectGripper, bHorz, this);
  HFONT hFont = m_hFont != NULL ? m_hFont : (HFONT)afxGlobalData.fontRegular.GetSafeHandle();
  HGDIOBJ hOldFont = ::SelectObject(pDC->GetSafeHdc(), hFont);
  int i = 0;
  for (POSITION pos = m_Buttons.GetHeadPosition(); pos != NULL; i++) {
    CMFCToolBarButton* p = (CMFCToolBarButton*)m_Buttons.GetNext(pos);
    if (p->IsHidden() || p->Rect().IsRectEmpty()) continue;
    if (p->IsSeparator())
      DrawSeparator(pDC, p->Rect(), bHorz);
    else
      DrawButton(pDC, p, _ButtonImages(), i == m_iHighlighted, TRUE);
  }
  ::SelectObject(pDC->GetSafeHdc(), hOldFont);
  CRect rectCustomize = _CustomizeRect();
  if (!rectCustomize.IsRectEmpty()) {
    CRect r = rectCustomize;
    r.top = r.bottom - 10;
    pVM->OnDrawMenuArrow(pDC, r, TRUE);
  }
  if (pDC != pDCPaint) pDCPaint->BitBlt(0, 0, rectClient.Width(), rectClient.Height(), pDC, 0, 0, SRCCOPY);
}

void CMFCToolBar::DrawSeparator(CDC* pDC, const CRect& rect, BOOL bHorz) {
  CMFCVisualManager::GetInstance()->OnDrawSeparator(pDC, this, rect, bHorz);
}

BOOL CMFCToolBar::DrawButton(CDC* pDC, CMFCToolBarButton* pButton, CMFCToolBarImages* pImages, BOOL bHighlighted,
                             BOOL bDrawDisabledImages) {
  pButton->OnDraw(pDC, pButton->Rect(), pImages, IsHorizontal(), FALSE, bHighlighted, TRUE, bDrawDisabledImages);
  return TRUE;
}

void AFXAPI CMFCToolBar::SetSizes(SIZE sizeButton, SIZE sizeImage) {
  m_sizeButton = sizeButton;
  m_sizeImage = sizeImage;
  m_Images.SetImageSize(sizeImage);
}

void AFXAPI CMFCToolBar::SetMenuSizes(SIZE sizeButton, SIZE sizeImage) {
  m_sizeMenuButton = sizeButton;
  m_sizeMenuImage = sizeImage;
}

void AFXAPI CMFCToolBar::SetBasicCommands(CList<UINT, UINT>& lstCommands) {
  m_lstBasicCommands.RemoveAll();
  for (POSITION pos = lstCommands.GetHeadPosition(); pos != NULL;) m_lstBasicCommands.AddTail(lstCommands.GetNext(pos));
}

void AFXAPI CMFCToolBar::AddBasicCommand(UINT uiCmd) {
  if (m_lstBasicCommands.Find(uiCmd) == NULL) m_lstBasicCommands.AddTail(uiCmd);
}

BOOL AFXAPI CMFCToolBar::IsBasicCommand(UINT uiCmd) { return m_lstBasicCommands.Find(uiCmd) != NULL; }

void AFXAPI CMFCToolBar::SetNonPermittedCommands(CList<UINT, UINT>& lstCommands) {
  m_lstUnpermittedCommands.RemoveAll();
  for (POSITION pos = lstCommands.GetHeadPosition(); pos != NULL;)
    m_lstUnpermittedCommands.AddTail(lstCommands.GetNext(pos));
}

void AFXAPI CMFCToolBar::ResetAllImages() {
  m_Images.Clear();
  m_ColdImages.Clear();
  m_DisabledImages.Clear();
  m_MenuImages.Clear();
  m_DisabledMenuImages.Clear();
}

void CMFCToolBar::CleanUpLockedImages() {
  m_ImagesLocked.Clear();
  m_ColdImagesLocked.Clear();
  m_DisabledImagesLocked.Clear();
}

void AFXAPI CMFCToolBar::CleanUpImages() {
  ResetAllImages();
  GetCmdMgr()->CleanUp();
}

// ---------------------------------------------------------------------
// CMFCMenuBar

IMPLEMENT_SERIAL(CMFCMenuBar, CMFCToolBar, VERSIONABLE_SCHEMA | 1)

BOOL CMFCMenuBar::m_bShowAllCommands = TRUE;
BOOL CMFCMenuBar::m_bRecentlyUsedMenus = FALSE;
BOOL CMFCMenuBar::m_bHighlightDisabledItems = FALSE;
BOOL CMFCMenuBar::m_bMenuShadows = FALSE;

CMFCMenuBar::CMFCMenuBar() : m_hMenu(NULL), m_hDefaultMenu(NULL), m_uiDefMenuResId(0) {}

CMFCMenuBar::~CMFCMenuBar() {}

BOOL CMFCMenuBar::Create(CWnd* pParentWnd, DWORD dwStyle, UINT nID) {
  return CreateEx(pParentWnd, TBSTYLE_FLAT, dwStyle, CRect(1, 1, 1, 1), nID);
}

BOOL CMFCMenuBar::CreateEx(CWnd* pParentWnd, DWORD dwCtrlStyle, DWORD dwStyle, CRect rcBorders, UINT nID) {
  if (!CMFCToolBar::CreateEx(pParentWnd, dwCtrlStyle, dwStyle & ~CBRS_TOOLTIPS, rcBorders, nID)) return FALSE;
  SetWindowText(_T("Menu Bar"));
  // The frame's menu moves here.
  CFrameWnd* pFrame = DYNAMIC_DOWNCAST(CFrameWnd, pParentWnd);
  if (pFrame != NULL && pFrame->GetSafeHwnd() != NULL) {
    HMENU hMenu = ::GetMenu(pFrame->m_hWnd);
    if (hMenu != NULL) {
      CreateFromMenu(hMenu, TRUE);
      ::SetMenu(pFrame->m_hWnd, NULL);
    }
  }
  return TRUE;
}

void CMFCMenuBar::CreateFromMenu(HMENU hMenu, BOOL bDefaultMenu, BOOL bForceUpdate) {
  if (bDefaultMenu) m_hDefaultMenu = hMenu;
  if (hMenu == m_hMenu && !bForceUpdate && GetCount() > 0) return;
  m_hMenu = hMenu;
  RemoveAllButtons();
  if (hMenu != NULL) {
    int n = ::GetMenuItemCount(hMenu);
    for (int i = 0; i < n; i++) {
      TCHAR szText[256];
      szText[0] = 0;
      ::GetMenuString(hMenu, i, szText, 256, MF_BYPOSITION);
      UINT uState = ::GetMenuState(hMenu, i, MF_BYPOSITION);
      // For a popup the high byte is its item count, not flags.
      if (uState & MF_POPUP) uState &= 0xFF;
      if ((uState & (MF_SEPARATOR | MF_BITMAP | MF_OWNERDRAW)) || szText[0] == 0) continue;
      HMENU hSub = ::GetSubMenu(hMenu, i);
      CMFCToolBarMenuButton button;
      button.m_nID = hSub != NULL ? 0 : ::GetMenuItemID(hMenu, i);
      button.m_strText = szText;
      button.m_bText = TRUE;
      button.m_bImage = FALSE;
      button.m_bMenuMode = TRUE;
      button.m_bDrawDownArrow = FALSE;
      button.m_hMenu = hSub;  // the frame's own submenu: the MDI window list stays live
      button.m_bOwnMenu = FALSE;
      if (uState & (MF_GRAYED | MF_DISABLED)) button.m_nStyle |= TBBS_DISABLED;
      InsertButton(button);
    }
  }
  if (m_hWnd != NULL) {
    AdjustLayout();
    if (m_pDockSite != NULL && m_pDockSite->GetSafeHwnd() != NULL) m_pDockSite->RecalcLayout();
  }
}

CSize CMFCMenuBar::CalcFixedLayout(BOOL bStretch, BOOL bHorz) {
  CSize size = CMFCToolBar::CalcFixedLayout(bStretch, bHorz);
  // Docked, the bar takes its whole row.
  if (bStretch && m_pMiniFrame == NULL) {
    if (bHorz)
      size.cx = 32767;
    else
      size.cy = 32767;
  }
  return size;
}

void CMFCMenuBar::OnUpdateCmdUI(CFrameWnd*, BOOL) {}

BOOL CMFCMenuBar::TranslateChar(UINT nChar) {
  TCHAR ch = (TCHAR)(DWORD_PTR)::CharUpper((LPTSTR)(DWORD_PTR)(TCHAR)nChar);
  int i = 0;
  for (POSITION pos = m_Buttons.GetHeadPosition(); pos != NULL; i++) {
    CMFCToolBarButton* p = (CMFCToolBarButton*)m_Buttons.GetNext(pos);
    int iAmp = p->m_strText.Find(_T('&'));
    if (iAmp < 0 || iAmp + 1 >= p->m_strText.GetLength()) continue;
    TCHAR chItem = (TCHAR)(DWORD_PTR)::CharUpper((LPTSTR)(DWORD_PTR)p->m_strText[iAmp + 1]);
    if (chItem == ch && !(p->m_nStyle & TBBS_DISABLED)) return OpenItem(i);
  }
  return FALSE;
}

BOOL CMFCMenuBar::OpenItem(int iItem) {
  CMFCToolBarButton* p = GetButton(iItem);
  if (p == NULL) return FALSE;
  CMFCToolBarMenuButton* pMenu = DYNAMIC_DOWNCAST(CMFCToolBarMenuButton, p);
  if (pMenu != NULL && pMenu->m_hMenu != NULL) {
    OnChangeHot(iItem);
    pMenu->OpenPopupMenu(this);
    if (::IsWindow(m_hWnd)) OnChangeHot(-1);
    return TRUE;
  }
  _SendCommand(p);
  return TRUE;
}

// ---------------------------------------------------------------------
// CMFCStatusBar

IMPLEMENT_DYNAMIC(CMFCStatusBar, CPane)

BEGIN_MESSAGE_MAP(CMFCStatusBar, CPane)
  ON_WM_CREATE()
  ON_WM_DESTROY()
  ON_WM_SIZE()
  ON_WM_NCHITTEST()
  ON_WM_NCLBUTTONDOWN()
  ON_WM_LBUTTONDBLCLK()
  ON_WM_TIMER()
  ON_MESSAGE(WM_SETTEXT, OnSetText)
  ON_MESSAGE(WM_GETTEXT, OnGetText)
  ON_MESSAGE(WM_GETTEXTLENGTH, OnGetTextLength)
  ON_MESSAGE(WM_SETFONT, OnSetFont)
  ON_MESSAGE(WM_GETFONT, OnGetFont)
END_MESSAGE_MAP()

// Command UI for a status bar pane.
class CMFCStatusBarCmdUI : public CCmdUI {
public:
  CMFCStatusBar* m_pBar;
  virtual void Enable(BOOL bOn) {
    m_bEnableChanged = TRUE;
    UINT nStyle = m_pBar->GetPaneStyle(m_nIndex);
    UINT nNew = bOn ? (nStyle & ~SBPS_DISABLED) : (nStyle | SBPS_DISABLED);
    if (nNew != nStyle) m_pBar->SetPaneStyle(m_nIndex, nNew);
  }
  virtual void SetCheck(int nCheck) {
    UINT nStyle = m_pBar->GetPaneStyle(m_nIndex);
    UINT nNew = nCheck ? (nStyle | SBPS_POPOUT) : (nStyle & ~SBPS_POPOUT);
    if (nNew != nStyle) m_pBar->SetPaneStyle(m_nIndex, nNew);
  }
  virtual void SetRadio(BOOL bOn) { SetCheck(bOn); }
  virtual void SetText(LPCTSTR lpszText) { m_pBar->SetPaneText(m_nIndex, lpszText); }
};

CMFCStatusBar::CMFCStatusBar() : m_hFont(NULL), m_cyHeight(0), m_bPaneDoubleClick(FALSE), m_pToolTip(NULL) {}

CMFCStatusBar::~CMFCStatusBar() {
  for (int i = 0; i < GetCount(); i++) _FreePane(m_arPanes[i]);
  m_arPanes.RemoveAll();
  delete m_pToolTip;
}

void CMFCStatusBar::_FreePane(CMFCStatusBarPaneInfo* pPane) {
  if (pPane == NULL) return;
  if (pPane->bOwnImages && pPane->hImages != NULL) ::ImageList_Destroy(pPane->hImages);
  delete pPane;
}

BOOL CMFCStatusBar::Create(CWnd* pParentWnd, DWORD dwStyle, UINT nID) { return CreateEx(pParentWnd, 0, dwStyle, nID); }

BOOL CMFCStatusBar::CreateEx(CWnd* pParentWnd, DWORD, DWORD dwStyle, UINT nID) {
  if ((dwStyle & CBRS_ALIGN_ANY) == 0) dwStyle |= CBRS_BOTTOM;
  return CPane::CreateEx(0, NULL, dwStyle, CRect(0, 0, 0, 0), pParentWnd, nID, 0);
}

int CMFCStatusBar::OnCreate(LPCREATESTRUCT lpCreateStruct) {
  if (CPane::OnCreate(lpCreateStruct) == -1) return -1;
  m_pToolTip = new CToolTipCtrl;
  if (!m_pToolTip->Create(this, TTS_ALWAYSTIP)) {
    delete m_pToolTip;
    m_pToolTip = NULL;
  }
  return 0;
}

void CMFCStatusBar::OnDestroy() {
  for (int i = 0; i < GetCount(); i++) KillTimer(i + 1);
  if (m_pToolTip != NULL && m_pToolTip->GetSafeHwnd() != NULL) m_pToolTip->DestroyWindow();
  delete m_pToolTip;
  m_pToolTip = NULL;
  CPane::OnDestroy();
}

BOOL CMFCStatusBar::SetIndicators(const UINT* lpIDArray, int nIDCount) {
  for (int i = 0; i < GetCount(); i++) _FreePane(m_arPanes[i]);
  m_arPanes.RemoveAll();
  CClientDC dc(m_hWnd != NULL ? (CWnd*)this : CWnd::GetDesktopWindow());
  HGDIOBJ hOld = ::SelectObject(dc.GetSafeHdc(), m_hFont != NULL ? m_hFont : afxGlobalData.fontRegular.GetSafeHandle());
  for (int i = 0; i < nIDCount; i++) {
    CMFCStatusBarPaneInfo* p = new CMFCStatusBarPaneInfo;
    p->nID = lpIDArray != NULL ? lpIDArray[i] : 0;
    p->nStyle = SBPS_NORMAL;
    p->cxText = 0;
    p->hImages = NULL;
    p->nFrameCount = p->nCurrFrame = 0;
    p->sizeImage = CSize(0, 0);
    p->clrText = p->clrBackground = (COLORREF)-1;
    p->nProgressCurr = 0;
    p->nProgressTotal = -1;
    p->bProgressText = FALSE;
    p->clrProgressBar = p->clrProgressBarDest = p->clrProgressText = (COLORREF)-1;
    p->rect.SetRectEmpty();
    p->bOwnImages = FALSE;
    if (p->nID != 0) {
      p->strText.LoadString(p->nID);
      p->cxText = dc.GetTextExtent(p->strText).cx;
    } else if (i == 0) {
      p->nStyle = SBPS_NOBORDERS | SBPS_STRETCH;
    }
    m_arPanes.Add(p);
  }
  ::SelectObject(dc.GetSafeHdc(), hOld);
  RecalcLayout();
  return TRUE;
}

CMFCStatusBarPaneInfo* CMFCStatusBar::GetPane(int nIndex) const {
  return nIndex >= 0 && nIndex < GetCount() ? m_arPanes[nIndex] : NULL;
}

int CMFCStatusBar::CommandToIndex(UINT nIDFind) const {
  for (int i = 0; i < GetCount(); i++)
    if (m_arPanes[i]->nID == nIDFind) return i;
  return -1;
}

UINT CMFCStatusBar::GetItemID(int nIndex) const {
  CMFCStatusBarPaneInfo* p = GetPane(nIndex);
  return p != NULL ? p->nID : 0;
}

void CMFCStatusBar::GetItemRect(int nIndex, LPRECT lpRect) const {
  CMFCStatusBarPaneInfo* p = GetPane(nIndex);
  if (p != NULL)
    *lpRect = p->rect;
  else
    ::SetRectEmpty(lpRect);
}

CString CMFCStatusBar::GetPaneText(int nIndex) const {
  CMFCStatusBarPaneInfo* p = GetPane(nIndex);
  return p != NULL ? p->strText : CString();
}

BOOL CMFCStatusBar::SetPaneText(int nIndex, LPCTSTR lpszNewText, BOOL bUpdate) {
  CMFCStatusBarPaneInfo* p = GetPane(nIndex);
  if (p == NULL) return FALSE;
  CString str = lpszNewText != NULL ? lpszNewText : _T("");
  if (p->strText == str) return TRUE;
  p->strText = str;
  if (bUpdate) InvalidatePaneContent(nIndex);
  return TRUE;
}

void CMFCStatusBar::GetPaneInfo(int nIndex, UINT& nID, UINT& nStyle, int& cxWidth) const {
  CMFCStatusBarPaneInfo* p = GetPane(nIndex);
  nID = p != NULL ? p->nID : 0;
  nStyle = p != NULL ? p->nStyle : 0;
  cxWidth = p != NULL ? p->cxText : 0;
}

void CMFCStatusBar::SetPaneInfo(int nIndex, UINT nID, UINT nStyle, int cxWidth) {
  CMFCStatusBarPaneInfo* p = GetPane(nIndex);
  if (p == NULL) return;
  p->nID = nID;
  p->nStyle = nStyle;
  p->cxText = cxWidth;
  RecalcLayout();
}

UINT CMFCStatusBar::GetPaneStyle(int nIndex) const {
  CMFCStatusBarPaneInfo* p = GetPane(nIndex);
  return p != NULL ? p->nStyle : 0;
}

void CMFCStatusBar::SetPaneStyle(int nIndex, UINT nStyle) {
  CMFCStatusBarPaneInfo* p = GetPane(nIndex);
  if (p == NULL || p->nStyle == nStyle) return;
  BOOL bLayout = ((p->nStyle ^ nStyle) & SBPS_STRETCH) != 0;
  p->nStyle = nStyle;
  if (bLayout)
    RecalcLayout();
  else
    InvalidatePaneContent(nIndex);
}

int CMFCStatusBar::GetPaneWidth(int nIndex) const {
  CMFCStatusBarPaneInfo* p = GetPane(nIndex);
  return p != NULL ? p->rect.Width() : 0;
}

void CMFCStatusBar::SetPaneWidth(int nIndex, int cx) {
  CMFCStatusBarPaneInfo* p = GetPane(nIndex);
  if (p == NULL) return;
  p->cxText = cx;
  RecalcLayout();
}

void CMFCStatusBar::SetPaneIcon(int nIndex, HICON hIcon, BOOL bUpdate) {
  CMFCStatusBarPaneInfo* p = GetPane(nIndex);
  if (p == NULL) return;
  if (p->bOwnImages && p->hImages != NULL) ::ImageList_Destroy(p->hImages);
  p->hImages = NULL;
  p->bOwnImages = FALSE;
  p->nFrameCount = 0;
  if (hIcon != NULL) {
    ICONINFO ii;
    CSize size(16, 16);
    if (::GetIconInfo(hIcon, &ii)) {
      BITMAP bm;
      if (ii.hbmColor != NULL && ::GetObject(ii.hbmColor, sizeof(bm), &bm)) size = CSize(bm.bmWidth, bm.bmHeight);
      if (ii.hbmColor != NULL) ::DeleteObject(ii.hbmColor);
      if (ii.hbmMask != NULL) ::DeleteObject(ii.hbmMask);
    }
    p->hImages = ::ImageList_Create(size.cx, size.cy, ILC_COLOR32 | ILC_MASK, 1, 0);
    ::ImageList_ReplaceIcon(p->hImages, -1, hIcon);
    p->bOwnImages = TRUE;
    p->sizeImage = size;
  }
  RecalcLayout();
  if (bUpdate) InvalidatePaneContent(nIndex);
}

void CMFCStatusBar::SetPaneIcon(int nIndex, HBITMAP hBmp, COLORREF clrTransparent, BOOL bUpdate) {
  CMFCStatusBarPaneInfo* p = GetPane(nIndex);
  if (p == NULL) return;
  if (p->bOwnImages && p->hImages != NULL) ::ImageList_Destroy(p->hImages);
  p->hImages = NULL;
  p->bOwnImages = FALSE;
  BITMAP bm;
  if (hBmp != NULL && ::GetObject(hBmp, sizeof(bm), &bm)) {
    p->hImages = ::ImageList_Create(bm.bmWidth, bm.bmHeight, ILC_COLOR32 | ILC_MASK, 1, 0);
    HBITMAP hCopy = (HBITMAP)::CopyImage(hBmp, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    ::ImageList_AddMasked(p->hImages, hCopy, clrTransparent);
    ::DeleteObject(hCopy);
    p->bOwnImages = TRUE;
    p->sizeImage = CSize(bm.bmWidth, bm.bmHeight);
  }
  RecalcLayout();
  if (bUpdate) InvalidatePaneContent(nIndex);
}

void CMFCStatusBar::SetPaneAnimation(int nIndex, HIMAGELIST hImageList, UINT nFrameRate, BOOL bUpdate) {
  CMFCStatusBarPaneInfo* p = GetPane(nIndex);
  if (p == NULL) return;
  if (p->bOwnImages && p->hImages != NULL) ::ImageList_Destroy(p->hImages);
  p->bOwnImages = FALSE;
  p->hImages = hImageList;
  p->nCurrFrame = 0;
  p->nFrameCount = hImageList != NULL ? ::ImageList_GetImageCount(hImageList) : 0;
  if (hImageList != NULL) {
    int cx, cy;
    ::ImageList_GetIconSize(hImageList, &cx, &cy);
    p->sizeImage = CSize(cx, cy);
  }
  if (m_hWnd != NULL) {
    KillTimer(nIndex + 1);
    if (p->nFrameCount > 1) SetTimer(nIndex + 1, nFrameRate, NULL);
  }
  RecalcLayout();
  if (bUpdate) InvalidatePaneContent(nIndex);
}

void CMFCStatusBar::SetPaneTextColor(int nIndex, COLORREF clrText, BOOL bUpdate) {
  CMFCStatusBarPaneInfo* p = GetPane(nIndex);
  if (p == NULL) return;
  p->clrText = clrText;
  if (bUpdate) InvalidatePaneContent(nIndex);
}

void CMFCStatusBar::SetPaneBackgroundColor(int nIndex, COLORREF clrBackground, BOOL bUpdate) {
  CMFCStatusBarPaneInfo* p = GetPane(nIndex);
  if (p == NULL) return;
  p->clrBackground = clrBackground;
  if (bUpdate) InvalidatePaneContent(nIndex);
}

void CMFCStatusBar::SetTipText(int nIndex, LPCTSTR pszTipText) {
  CMFCStatusBarPaneInfo* p = GetPane(nIndex);
  if (p == NULL) return;
  p->strToolTip = pszTipText != NULL ? pszTipText : _T("");
  RecalcLayout();
}

CString CMFCStatusBar::GetTipText(int nIndex) const {
  CMFCStatusBarPaneInfo* p = GetPane(nIndex);
  return p != NULL ? p->strToolTip : CString();
}

void CMFCStatusBar::EnablePaneProgressBar(int nIndex, long nTotal, BOOL bDisplayText, COLORREF clrBar,
                                          COLORREF clrBarDest, COLORREF clrProgressText) {
  CMFCStatusBarPaneInfo* p = GetPane(nIndex);
  if (p == NULL) return;
  p->nProgressTotal = nTotal;
  p->nProgressCurr = 0;
  p->bProgressText = bDisplayText;
  p->clrProgressBar = clrBar;
  p->clrProgressBarDest = clrBarDest;
  p->clrProgressText = clrProgressText;
  InvalidatePaneContent(nIndex);
}

void CMFCStatusBar::SetPaneProgress(int nIndex, long nCurr, BOOL bUpdate) {
  CMFCStatusBarPaneInfo* p = GetPane(nIndex);
  if (p == NULL || p->nProgressCurr == nCurr) return;
  p->nProgressCurr = nCurr;
  if (bUpdate) InvalidatePaneContent(nIndex);
}

long CMFCStatusBar::GetPaneProgress(int nIndex) const {
  CMFCStatusBarPaneInfo* p = GetPane(nIndex);
  return p != NULL ? p->nProgressCurr : 0;
}

void CMFCStatusBar::InvalidatePaneContent(int nIndex) {
  CMFCStatusBarPaneInfo* p = GetPane(nIndex);
  if (p != NULL && m_hWnd != NULL) InvalidateRect(p->rect, FALSE);
}

int CMFCStatusBar::HitTest(CPoint point) const {
  for (int i = 0; i < GetCount(); i++)
    if (m_arPanes[i]->rect.PtInRect(point)) return i;
  return -1;
}

BOOL CMFCStatusBar::_HasSizeBox() const {
  if (m_hWnd == NULL) return FALSE;
  CWnd* pTop = GetTopLevelParent();
  return pTop != NULL && (pTop->GetStyle() & WS_THICKFRAME) && !pTop->IsZoomed() &&
         (m_dwStyle & CBRS_ALIGN_ANY) == CBRS_ALIGN_BOTTOM;
}

CRect CMFCStatusBar::_SizeBoxRect() const {
  CRect r;
  GetClientRect(r);
  r.left = r.right - ::GetSystemMetrics(SM_CXVSCROLL);
  return r;
}

void CMFCStatusBar::RecalcLayout() {
  int cyText = afxGlobalData.GetTextHeight();
  m_cyHeight = cyText + 6;
  for (int i = 0; i < GetCount(); i++)
    if (m_arPanes[i]->sizeImage.cy + 6 > m_cyHeight) m_cyHeight = m_arPanes[i]->sizeImage.cy + 6;
  if (m_hWnd == NULL) return;
  CRect rectClient;
  GetClientRect(rectClient);
  rectClient.DeflateRect(2, 2, 2, 1);
  if (_HasSizeBox()) rectClient.right -= ::GetSystemMetrics(SM_CXVSCROLL) - 2;
  // The fixed panes at their widths; the stretching ones share the rest.
  int nFixed = 0, nStretch = 0;
  for (int i = 0; i < GetCount(); i++) {
    CMFCStatusBarPaneInfo* p = m_arPanes[i];
    int cxImage = p->hImages != NULL ? p->sizeImage.cx + (p->strText.IsEmpty() ? 0 : 3) : 0;
    if (p->nStyle & SBPS_STRETCH)
      nStretch++;
    else
      nFixed += p->cxText + cxImage + 8;
    if (i > 0) nFixed += 2;
  }
  int nRoom = rectClient.Width() - nFixed;
  if (nRoom < 0) nRoom = 0;
  int x = rectClient.left;
  for (int i = 0; i < GetCount(); i++) {
    CMFCStatusBarPaneInfo* p = m_arPanes[i];
    int cxImage = p->hImages != NULL ? p->sizeImage.cx + (p->strText.IsEmpty() ? 0 : 3) : 0;
    int cx;
    if (p->nStyle & SBPS_STRETCH) {
      cx = nRoom / nStretch;
      nRoom -= cx;
      nStretch--;
    } else {
      cx = p->cxText + cxImage + 8;
    }
    p->rect = CRect(x, rectClient.top, x + cx, rectClient.bottom);
    x += cx + 2;
  }
  // Tips for the panes that have one.
  if (m_pToolTip != NULL && m_pToolTip->GetSafeHwnd() != NULL) {
    for (int i = 0; i < GetCount(); i++) {
      m_pToolTip->DelTool(this, i + 1);
      if (!m_arPanes[i]->strToolTip.IsEmpty()) m_pToolTip->AddTool(this, m_arPanes[i]->strToolTip, m_arPanes[i]->rect, i + 1);
    }
  }
  Invalidate();
}

CSize CMFCStatusBar::CalcFixedLayout(BOOL bStretch, BOOL bHorz) {
  if (m_cyHeight == 0) RecalcLayout();
  int cx = 0;
  for (int i = 0; i < GetCount(); i++) cx += m_arPanes[i]->cxText + 10;
  if (bStretch) cx = 32767;
  return bHorz ? CSize(cx, m_cyHeight) : CSize(m_cyHeight, cx);
}

void CMFCStatusBar::OnSize(UINT nType, int cx, int cy) {
  CPane::OnSize(nType, cx, cy);
  RecalcLayout();
}

void CMFCStatusBar::OnDrawPane(CDC* pDC, CMFCStatusBarPaneInfo* pPane) {
  CMFCVisualManager* pVM = CMFCVisualManager::GetInstance();
  CRect r = pPane->rect;
  if (pPane->clrBackground != (COLORREF)-1) pDC->FillSolidRect(r, pPane->clrBackground);
  pVM->OnDrawStatusBarPaneBorder(pDC, this, r, pPane->nID, pPane->nStyle);
  CRect rectContent = r;
  rectContent.DeflateRect(3, 1);
  if (pPane->nProgressTotal > 0) {
    pVM->OnDrawStatusBarProgress(pDC, this, r, (int)pPane->nProgressTotal, (int)pPane->nProgressCurr,
                                 pPane->clrProgressBar, pPane->clrProgressBarDest, pPane->clrProgressText,
                                 pPane->bProgressText);
    return;
  }
  if (pPane->hImages != NULL) {
    int iImage = pPane->nFrameCount > 1 ? pPane->nCurrFrame : 0;
    int y = rectContent.top + (rectContent.Height() - pPane->sizeImage.cy) / 2;
    ::ImageList_Draw(pPane->hImages, iImage, pDC->GetSafeHdc(), rectContent.left, y, ILD_TRANSPARENT);
    rectContent.left += pPane->sizeImage.cx + 3;
  }
  if (!(pPane->nStyle & SBPS_DISABLED) && !pPane->strText.IsEmpty()) {
    pDC->SetBkMode(TRANSPARENT);
    pDC->SetTextColor(pPane->clrText != (COLORREF)-1 ? pPane->clrText : afxGlobalData.clrBtnText);
    pDC->DrawText(pPane->strText, rectContent, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  }
}

void CMFCStatusBar::DoPaint(CDC* pDCPaint) {
  CRect rectClient;
  GetClientRect(rectClient);
  CDC dc;
  CBitmap bmp;
  CDC* pDC = pDCPaint;
  if (dc.CreateCompatibleDC(pDCPaint) && bmp.CreateCompatibleBitmap(pDCPaint, rectClient.Width(), rectClient.Height())) {
    dc.SelectObject(&bmp);
    pDC = &dc;
  }
  CMFCVisualManager* pVM = CMFCVisualManager::GetInstance();
  pVM->OnFillBarBackground(pDC, this, rectClient, rectClient);
  HGDIOBJ hOld = ::SelectObject(pDC->GetSafeHdc(), m_hFont != NULL ? m_hFont : afxGlobalData.fontRegular.GetSafeHandle());
  for (int i = 0; i < GetCount(); i++) OnDrawPane(pDC, m_arPanes[i]);
  ::SelectObject(pDC->GetSafeHdc(), hOld);
  if (_HasSizeBox()) pVM->OnDrawStatusBarSizeBox(pDC, this, _SizeBoxRect());
  if (pDC != pDCPaint) pDCPaint->BitBlt(0, 0, rectClient.Width(), rectClient.Height(), pDC, 0, 0, SRCCOPY);
}

void CMFCStatusBar::OnUpdateCmdUI(CFrameWnd* pTarget, BOOL bDisableIfNoHndler) {
  CMFCStatusBarCmdUI state;
  state.m_pBar = this;
  state.m_pOther = this;
  state.m_nIndexMax = (UINT)GetCount();
  for (state.m_nIndex = 0; state.m_nIndex < state.m_nIndexMax; state.m_nIndex++) {
    state.m_nID = m_arPanes[state.m_nIndex]->nID;
    if (state.m_nID == 0) continue;
    state.DoUpdate(pTarget, bDisableIfNoHndler);
  }
}

LRESULT CMFCStatusBar::OnNcHitTest(CPoint point) {
  if (_HasSizeBox()) {
    CPoint pt = point;
    ScreenToClient(&pt);
    if (_SizeBoxRect().PtInRect(pt)) return HTBOTTOMRIGHT;
  }
  return CPane::OnNcHitTest(point);
}

void CMFCStatusBar::OnNcLButtonDown(UINT nHitTest, CPoint point) {
  if (nHitTest == HTBOTTOMRIGHT) {
    // The grip sizes the frame, not the bar.
    CWnd* pTop = GetTopLevelParent();
    if (pTop != NULL) pTop->SendMessage(WM_NCLBUTTONDOWN, HTBOTTOMRIGHT, MAKELPARAM(point.x, point.y));
    return;
  }
  CPane::OnNcLButtonDown(nHitTest, point);
}

void CMFCStatusBar::OnLButtonDblClk(UINT nFlags, CPoint point) {
  int i = HitTest(point);
  if (m_bPaneDoubleClick && i >= 0) {
    CWnd* pTarget = _AfxBarCommandWnd(this);
    if (pTarget != NULL) pTarget->SendMessage(AFX_WM_ON_STATUSBAR_DBLCLK, m_arPanes[i]->nID, (LPARAM)this);
    return;
  }
  CPane::OnLButtonDblClk(nFlags, point);
}

void CMFCStatusBar::OnTimer(UINT_PTR nIDEvent) {
  CMFCStatusBarPaneInfo* p = GetPane((int)nIDEvent - 1);
  if (p == NULL || p->nFrameCount <= 1) {
    CPane::OnTimer(nIDEvent);
    return;
  }
  p->nCurrFrame = (p->nCurrFrame + 1) % p->nFrameCount;
  InvalidatePaneContent((int)nIDEvent - 1);
}

LRESULT CMFCStatusBar::OnSetText(WPARAM, LPARAM lParam) {
  // The frame's message line: pane 0.
  return SetPaneText(0, (LPCTSTR)lParam) ? TRUE : FALSE;
}

LRESULT CMFCStatusBar::OnGetText(WPARAM wParam, LPARAM lParam) {
  CString str = GetPaneText(0);
  int nMax = (int)wParam;
  if (nMax <= 0 || lParam == 0) return 0;
  lstrcpyn((LPTSTR)lParam, str, nMax);
  return lstrlen((LPTSTR)lParam);
}

LRESULT CMFCStatusBar::OnGetTextLength(WPARAM, LPARAM) { return GetPaneText(0).GetLength(); }

LRESULT CMFCStatusBar::OnSetFont(WPARAM wParam, LPARAM lParam) {
  m_hFont = (HFONT)wParam;
  if (LOWORD(lParam)) RecalcLayout();
  return 0;
}

LRESULT CMFCStatusBar::OnGetFont(WPARAM, LPARAM) {
  return (LRESULT)(m_hFont != NULL ? m_hFont : (HFONT)afxGlobalData.fontRegular.GetSafeHandle());
}

// ---------------------------------------------------------------------
// CMFCToolTipCtrl

IMPLEMENT_DYNCREATE(CMFCToolTipCtrl, CToolTipCtrl)

CMFCToolTipCtrl::CMFCToolTipCtrl(CMFCToolTipInfo* pParams)
    : m_nFixedWidthRegular(0), m_nFixedWidthWithImage(0), m_ptLocation(-1, -1) {
  SetParams(pParams);
}

CMFCToolTipCtrl::~CMFCToolTipCtrl() {}

void CMFCToolTipCtrl::SetParams(CMFCToolTipInfo* pParams) {
  if (pParams != NULL)
    m_Params = *pParams;
  else
    m_Params = CMFCToolTipInfo();
}

void CMFCToolTipCtrl::SetDescription(const CString strDescription) { m_strDescription = strDescription; }

// ---------------------------------------------------------------------
// CMFCPopupMenu

IMPLEMENT_SERIAL(CMFCPopupMenu, CMiniFrameWnd, VERSIONABLE_SCHEMA | 1)

BOOL CMFCPopupMenu::m_bForceShadow = FALSE;
BOOL CMFCPopupMenu::m_bForceMenuFocus = TRUE;
BOOL CMFCPopupMenu::m_bMenuSound = TRUE;
CMFCPopupMenu::ANIMATION_TYPE CMFCPopupMenu::m_AnimationType = CMFCPopupMenu::NO_ANIMATION;
UINT CMFCPopupMenu::m_AnimationSpeed = 15;
CMFCPopupMenu* CMFCPopupMenu::m_pActivePopupMenu = NULL;

CMFCPopupMenu::CMFCPopupMenu()
    : m_hMenu(NULL), m_bRightAlign(FALSE), m_bAutoDestroy(TRUE), m_pParentBtn(NULL), m_pMessageWnd(NULL),
      m_nLastCommand(0) {}

CMFCPopupMenu::~CMFCPopupMenu() {
  if (m_pActivePopupMenu == this) m_pActivePopupMenu = NULL;
}

BOOL CMFCPopupMenu::Create(CWnd* pWndParent, int x, int y, HMENU hMenu, BOOL, BOOL bOwnMessage) {
  m_hMenu = hMenu;
  CWnd* pOwner = m_pMessageWnd;
  if (pOwner == NULL) pOwner = bOwnMessage ? pWndParent : AfxGetMainWnd();
  if (pOwner == NULL) pOwner = pWndParent;
  BOOL bOK = FALSE;
  if (hMenu != NULL && pOwner != NULL && pOwner->GetSafeHwnd() != NULL) {
    if (x == -1 && y == -1) {
      POINT pt;
      ::GetCursorPos(&pt);
      x = pt.x;
      y = pt.y;
    }
    m_pActivePopupMenu = this;
    m_nLastCommand = (UINT)::TrackPopupMenu(
        hMenu, (m_bRightAlign ? TPM_RIGHTALIGN : TPM_LEFTALIGN) | TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0,
        pOwner->m_hWnd, NULL);
    m_pActivePopupMenu = NULL;
    if (m_nLastCommand != 0) pOwner->SendMessage(WM_COMMAND, m_nLastCommand);
    bOK = TRUE;
  }
  if (m_bAutoDestroy) delete this;
  return bOK;
}
