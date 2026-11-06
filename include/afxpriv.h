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
