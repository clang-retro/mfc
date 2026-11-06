// afxpriv.h - the framework's semi-private classes and messages: print
// preview, the private WM_ messages (also in afxwin.h), and the string
// conversion macros (afxconv.h).
//
// Part of clang-retro MFC, an independent implementation of the MFC API
// written from its public documentation.
#ifndef __AFXPRIV_H__
#define __AFXPRIV_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif
#ifndef __AFXEXT_H__
#include <afxext.h>
#endif
#include <afxconv.h>
#include <afxadv.h>

// ---------------------------------------------------------------------
// Docking: the dock bars along the frame's edges, the small frames that
// hold floating bars, and the drag loop that moves bars between them.

// One saved control bar (or dock bar) in a CDockState.
class CControlBarInfo {
public:
  CControlBarInfo();
  virtual ~CControlBarInfo() {}

  UINT m_nBarID;
  BOOL m_bVisible;
  BOOL m_bFloating;
  BOOL m_bHorz;
  BOOL m_bDockBar;
  CPoint m_pointPos;   // in the dock bar, or the floating frame's on the screen
  UINT m_nMRUWidth;
  BOOL m_bDocking;     // the bar has a dock context (it can be dragged)
  UINT m_uMRUDockID;
  CRect m_rectMRUDockPos;
  DWORD m_dwMRUFloatStyle;
  CPoint m_ptMRUFloatPos;
  CPtrArray m_arrBarID;  // a dock bar's bars in order; 0 ends a row
  CControlBar* m_pBar;   // filled in while a state is applied

  virtual BOOL SaveState(LPCTSTR lpszProfileName, int nIndex);
  virtual BOOL LoadState(LPCTSTR lpszProfileName, int nIndex, CDockState* pDockState);
  virtual void Serialize(CArchive& ar, CDockState* pDockState);
};

// A strip along one edge of a frame (or the inside of a floating frame)
// that lays out the control bars docked in it, in rows.
class CDockBar : public CControlBar {
  DECLARE_DYNAMIC(CDockBar)
public:
  explicit CDockBar(BOOL bFloating = FALSE);
  virtual ~CDockBar();

  BOOL Create(CWnd* pParentWnd, DWORD dwStyle, UINT nID);

  BOOL m_bFloating;
  // The docked bars, row by row. Element 0 is NULL and a NULL ends each
  // row, so an empty dock bar holds just one NULL.
  CPtrArray m_arrBars;
  BOOL m_bLayoutQuery;
  CRect m_rectLayout;

  virtual BOOL IsDockBar() const { return TRUE; }
  int GetDockedCount() const;
  int GetDockedVisibleCount() const;
  CControlBar* GetDockedControlBar(int nPos) const;
  int FindBar(CControlBar* pBar, int nPosExclude = -1);
  void ShowAll(BOOL bShow);

  // lpRect, in screen coordinates, picks the row and the place in it.
  void DockControlBar(CControlBar* pBar, LPCRECT lpRect = NULL);
  void ReDockControlBar(CControlBar* pBar, LPCRECT lpRect = NULL);
  BOOL RemoveControlBar(CControlBar* pBar, int nPosExclude = -1, int nAddPlaceHolder = 0);

  virtual CSize CalcFixedLayout(BOOL bStretch, BOOL bHorz);
  virtual void OnUpdateCmdUI(CFrameWnd* pTarget, BOOL bDisableIfNoHndler);
  virtual void DoPaint(CDC* pDC);

  int Insert(CControlBar* pBar, CRect rect, CPoint ptMid);
  void _AttachBar(CControlBar* pBar);  // style, parent and owner for a new member
  CSize _CalcBarSize(CControlBar* pBar, BOOL bHorz);

  afx_msg LRESULT OnSizeParent(WPARAM wParam, LPARAM lParam);
  afx_msg void OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp);
  afx_msg void OnNcPaint();
  DECLARE_MESSAGE_MAP()
};

// The frame a floating control bar lives in.
class CMiniDockFrameWnd : public CMiniFrameWnd {
  DECLARE_DYNCREATE(CMiniDockFrameWnd)
public:
  CMiniDockFrameWnd();
  virtual ~CMiniDockFrameWnd();

  virtual BOOL Create(CWnd* pParent, DWORD dwBarStyle);
  virtual void RecalcLayout(BOOL bNotify = TRUE);

  CDockBar m_wndDockBar;

  CControlBar* _GetBar() const;
  virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);

  afx_msg void OnClose();
  afx_msg void OnNcLButtonDown(UINT nHitTest, CPoint point);
  afx_msg void OnNcLButtonDblClk(UINT nHitTest, CPoint point);
  afx_msg int OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message);
  afx_msg void OnSizing(UINT fwSide, LPRECT pRect);
  DECLARE_MESSAGE_MAP()
};

// Moves a control bar with the mouse: an outline follows the pointer,
// thin where the bar would dock and thick where it would float. Holding
// Ctrl keeps it from docking; Esc or the right button cancels.
class CDockContext {
public:
  explicit CDockContext(CControlBar* pBar);
  virtual ~CDockContext();

  virtual void StartDrag(CPoint pt);  // pt in screen coordinates
  void Move(CPoint pt);
  void EndDrag();
  void OnKey(int nChar, BOOL bDown);

  virtual void StartResize(int nHitTest, CPoint pt);
  virtual void ToggleDocking();

  CControlBar* m_pBar;
  CFrameWnd* m_pDockSite;
  DWORD m_dwDockStyle;      // where the bar may dock (its EnableDocking)
  DWORD m_dwOverDockStyle;  // the side under the outline now, or 0
  DWORD m_dwStyle;          // the bar's style when the drag began
  BOOL m_bFlip;
  BOOL m_bForceFrame;       // Ctrl is down
  BOOL m_bDragging;
  BOOL m_bMoved;
  CPoint m_ptLast;
  CRect m_rectLast;
  CSize m_sizeLast;
  BOOL m_bDitherLast;
  CRect m_rectDragHorz;
  CRect m_rectDragVert;
  CRect m_rectFrameDragHorz;
  CRect m_rectFrameDragVert;
  CDC* m_pDC;

  // Where the bar last docked (in its dock bar's coordinates) and
  // floated, for ToggleDocking.
  UINT m_uMRUDockID;
  CRect m_rectMRUDockPos;
  DWORD m_dwMRUFloatStyle;
  CPoint m_ptMRUFloatPos;

  void InitLoop();
  void CancelLoop();
  BOOL Track();
  void DrawFocusRect(BOOL bRemoveRect = FALSE);
  void UpdateState(BOOL* pFlag, BOOL bNewValue);
  DWORD CanDock();
  CDockBar* GetDockBar(DWORD dwOverDockStyle);
  void _RestoreLayout();
};

// ---------------------------------------------------------------------
// CPreviewDC: draws on the screen what a printer would print. The
// attribute DC is the printer's, so a view measures and lays out as it
// does when printing; the output DC is the preview window's, with every
// mapping scaled to the size of the page on the screen.

class CPreviewDC : public CDC {
  DECLARE_DYNAMIC(CPreviewDC)
public:
  CPreviewDC();
  virtual ~CPreviewDC();

  virtual void SetAttribDC(HDC hDC);
  virtual void SetOutputDC(HDC hDC);
  virtual void ReleaseOutputDC();

  // Screen pixels per printer pixel, as a fraction.
  void SetScaleRatio(int nNumerator, int nDenominator);
  // Where the printable area's top left corner is in the output DC.
  void SetTopLeftOffset(CSize sizeTop);
  // Clips output to the page.
  void ClipToPage();
  void PrinterDPtoScreenDP(LPPOINT lpPoint) const;

  virtual int SaveDC();
  virtual BOOL RestoreDC(int nSavedDC);
  virtual CFont* SelectObject(CFont* pFont);
  using CDC::SelectObject;

  virtual int SetMapMode(int nMapMode);
  virtual CPoint SetViewportOrg(int x, int y);
  virtual CPoint OffsetViewportOrg(int nWidth, int nHeight);
  virtual CSize SetViewportExt(int cx, int cy);
  virtual CSize ScaleViewportExt(int xNum, int xDenom, int yNum, int yDenom);
  virtual CSize SetWindowExt(int cx, int cy);
  virtual CSize ScaleWindowExt(int xNum, int xDenom, int yNum, int yDenom);

  // Text goes out with the printer's character widths, so lines break
  // and end where they will on paper.
  virtual BOOL TextOut(int x, int y, LPCTSTR lpszString, int nCount);
  virtual BOOL ExtTextOut(int x, int y, UINT nOptions, LPCRECT lpRect, LPCTSTR lpszString,
                          UINT nCount, LPINT lpDxWidths);
  virtual CSize TabbedTextOut(int x, int y, LPCTSTR lpszString, int nCount, int nTabPositions,
                              LPINT lpnTabStopPositions, int nTabOrigin);
  using CDC::TextOut;
  using CDC::ExtTextOut;
  using CDC::TabbedTextOut;

protected:
  void MirrorMappingMode(BOOL bCompute);
  void MirrorAttributes();
  void ComputeScaleRatios();

  int m_nScaleNum;
  int m_nScaleDen;
  CSize m_sizeTopLeft;
  int m_nSaveDCIndex;  // the output DC's save level when it was set
};

// ---------------------------------------------------------------------
// CPreviewView: the window that shows the pages during print preview.

class CPreviewView : public CScrollView {
  DECLARE_DYNCREATE(CPreviewView)
public:
  CPreviewView();
  virtual ~CPreviewView();

  virtual BOOL SetPrintView(CView* pPrintView);
  virtual void OnDisplayPageNumber(UINT nPage, UINT nPagesDisplayed);

  enum { ZOOM_OUT = 0, ZOOM_MIDDLE = 1, ZOOM_IN = 2 };

  struct PAGE_INFO {
    CRect rectScreen;      // the page on the screen, in client coordinates
    CSize sizeUnscaled;    // the page in printer pixels
    CSize sizeScaleRatio;  // screen pixels per printer pixel (cx / cy)
    CSize sizeZoomOutRatio;
  };

  CPrintPreviewState* m_pPreviewState;
  CDialogBar* m_pToolBar;
  CView* m_pOrigView;
  CView* m_pPrintView;
  CPreviewDC* m_pPreviewDC;
  CDC m_dcPrint;
  CPrintInfo* m_pPreviewInfo;
  UINT m_nZoomOutPages;
  UINT m_nZoomState;
  UINT m_nMaxPages;
  UINT m_nCurrentPage;
  UINT m_nPages;
  int m_nSecondPageOffset;
  HCURSOR m_hMagnifyCursor;
  CSize m_sizePrinterPPI;
  CPoint m_ptCenterPoint;
  PAGE_INFO* m_pPageInfo;
  PAGE_INFO m_pageInfoArray[2];
  BOOL m_bPageNumDisplayed;

  virtual void OnDraw(CDC* pDC);
  virtual void OnActivateView(BOOL bActivate, CView* pActivateView, CView* pDeactiveView);
  virtual BOOL OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo);
  virtual void OnPrepareDC(CDC* pDC, CPrintInfo* pInfo = NULL);

  void SetZoomState(UINT nNewState, UINT nPage, CPoint point);
  void SetCurrentPage(UINT nPage, BOOL bClearRatios);
  BOOL FindPageRect(CPoint& point, UINT& nPage);
  void PositionPage(UINT nPage);
  CSize CalcScaleRatio(CSize windowSize, CSize actualSize);
  CSize CalcPageDisplaySize();
  BOOL CanShowPage(UINT nPage);

  afx_msg void OnPreviewClose();
  afx_msg void OnNumPageChange();
  afx_msg void OnNextPage();
  afx_msg void OnPrevPage();
  afx_msg void OnPreviewPrint();
  afx_msg void OnZoomIn();
  afx_msg void OnZoomOut();
  afx_msg void OnUpdateNumPageChange(CCmdUI* pCmdUI);
  afx_msg void OnUpdateNextPage(CCmdUI* pCmdUI);
  afx_msg void OnUpdatePrevPage(CCmdUI* pCmdUI);
  afx_msg void OnUpdateZoomIn(CCmdUI* pCmdUI);
  afx_msg void OnUpdateZoomOut(CCmdUI* pCmdUI);
  afx_msg void OnSize(UINT nType, int cx, int cy);
  afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
  afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
  afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg BOOL OnEraseBkgnd(CDC* pDC);
  afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);

  friend class CView;
  DECLARE_MESSAGE_MAP()
};

#endif  // __AFXPRIV_H__
