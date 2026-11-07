// afxext.h - control bars, splitter windows, form and edit views,
// bitmap buttons and printing support.
//
// Docking is simplified: bars dock to a side of their frame and never
// float. Print preview is not supported.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#ifndef __AFXEXT_H__
#define __AFXEXT_H__

#include "afxwin.h"
#include "afxdlgs.h"
#include "afxcmn.h"

class CControlBar;
class CStatusBar;
class CToolBar;
class CDialogBar;
class CDockBar;
class CDockContext;
class CSplitterWnd;
class CFormView;
class CEditView;
class CBitmapButton;
class CPrintInfo;
class CDockState;
class CControlBarInfo;
class CMiniDockFrameWnd;

#ifndef RT_TOOLBAR
#define RT_TOOLBAR MAKEINTRESOURCE(241)
#endif

// CalcDynamicLayout modes
#define LM_STRETCH 0x01
#define LM_HORZ 0x02
#define LM_MRUWIDTH 0x04
#define LM_HORZDOCK 0x08
#define LM_VERTDOCK 0x10
#define LM_LENGTHY 0x20
#define LM_COMMIT 0x40

// ---------------------------------------------------------------------
// CControlBar

class CControlBar : public CWnd {
  DECLARE_DYNAMIC(CControlBar)
protected:
  CControlBar();

public:
  virtual ~CControlBar();

  int GetCount() const { return m_nCount; }
  void SetBorders(int cxLeft = 0, int cyTop = 0, int cxRight = 0, int cyBottom = 0);
  void SetBorders(LPCRECT lpRect) {
    SetBorders(lpRect->left, lpRect->top, lpRect->right, lpRect->bottom);
  }
  CRect GetBorders() const {
    return CRect(m_cxLeftBorder, m_cyTopBorder, m_cxRightBorder, m_cyBottomBorder);
  }
  DWORD GetBarStyle() { return m_dwStyle; }
  void SetBarStyle(DWORD dwStyle);
  void EnableDocking(DWORD dwDockStyle);
  CFrameWnd* GetDockingFrame() const;
  BOOL IsFloating() const;
  BOOL IsVisible() const;
  virtual BOOL IsDockBar() const { return FALSE; }
  void DelayShow(BOOL bShow);

  virtual CSize CalcFixedLayout(BOOL bStretch, BOOL bHorz);
  virtual CSize CalcDynamicLayout(int nLength, DWORD nMode);
  virtual void OnUpdateCmdUI(CFrameWnd* pTarget, BOOL bDisableIfNoHndler) = 0;
  virtual void DoPaint(CDC* pDC);
  virtual void DrawBorders(CDC* pDC, CRect& rect);
  virtual void DrawGripper(CDC* pDC, const CRect& rect);
  virtual void CalcInsideRect(CRect& rect, BOOL bHorz) const;
  virtual void OnBarStyleChange(DWORD dwOldStyle, DWORD dwNewStyle);
  virtual BOOL SetStatusText(INT_PTR nHit);
  virtual void ResetTimer(UINT nEvent, UINT nTime);

  // Saving and restoring the docking layout (see CDockState).
  virtual void GetBarInfo(CControlBarInfo* pInfo);
  virtual void SetBarInfo(CControlBarInfo* pInfo, CFrameWnd* pFrameWnd);

  virtual BOOL PreTranslateMessage(MSG* pMsg);
  virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
  virtual void PostNcDestroy();
  virtual LRESULT WindowProc(UINT nMsg, WPARAM wParam, LPARAM lParam);

  BOOL m_bAutoDelete;
  int m_cxLeftBorder, m_cxRightBorder;
  int m_cyTopBorder, m_cyBottomBorder;
  int m_cxDefaultGap;
  UINT m_nMRUWidth;
  int m_nCount;
  void* m_pData;  // the bar's item array (allocated by derived classes)
  UINT m_nStateFlags;
  DWORD m_dwStyle;
  DWORD m_dwDockStyle;
  CFrameWnd* m_pDockSite;
  CDockBar* m_pDockBar;
  CDockContext* m_pDockContext;
  INT_PTR m_nHitLast;  // the item last shown by the fly-by status text

  enum StateFlags { delayHide = 1, delayShow = 2, tempHide = 4, statusSet = 8 };

  BOOL AllocElements(int nElements, int cbElement);
  void CalcNcBorders(CRect& rect, BOOL bHorz) const;
  BOOL IsHorzBar() const { return (m_dwStyle & CBRS_ORIENT_HORZ) != 0 || (m_dwStyle & CBRS_ALIGN_ANY) == 0; }

  afx_msg int OnCreate(LPCREATESTRUCT lpcs);
  afx_msg void OnDestroy();
  afx_msg void OnPaint();
  afx_msg BOOL OnEraseBkgnd(CDC* pDC);
  afx_msg void OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp);
  afx_msg void OnNcPaint();
  afx_msg void OnTimer(UINT_PTR nIDEvent);
  afx_msg void OnMouseMove(UINT nFlags, CPoint point);
  afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
  afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
  afx_msg LRESULT OnNcHitTest(CPoint point);
  afx_msg LRESULT OnSizeParent(WPARAM wParam, LPARAM lParam);
  afx_msg LRESULT OnIdleUpdateCmdUI(WPARAM wParam, LPARAM lParam);
  DECLARE_MESSAGE_MAP()

  friend class CFrameWnd;
  friend class CDockBar;
  friend class CDockContext;
};

// ---------------------------------------------------------------------
// CStatusBar

#define SBPS_NORMAL 0x0000
#define SBPS_NOBORDERS SBT_NOBORDERS
#define SBPS_POPOUT SBT_POPOUT
#define SBPS_OWNERDRAW SBT_OWNERDRAW
#define SBPS_DISABLED 0x04000000
#define SBPS_STRETCH 0x08000000

struct AFX_STATUSPANE {
  UINT nID;
  UINT nStyle;
  int cxText;
  CString strText;
  UINT nFlags;  // private: which parts need updating
};

class CStatusBar : public CControlBar {
  DECLARE_DYNAMIC(CStatusBar)
public:
  CStatusBar();
  virtual ~CStatusBar();

  virtual BOOL Create(CWnd* pParentWnd, DWORD dwStyle = WS_CHILD | WS_VISIBLE | CBRS_BOTTOM,
                      UINT nID = AFX_IDW_STATUS_BAR);
  virtual BOOL CreateEx(CWnd* pParentWnd, DWORD dwCtrlStyle = 0,
                        DWORD dwStyle = WS_CHILD | WS_VISIBLE | CBRS_BOTTOM,
                        UINT nID = AFX_IDW_STATUS_BAR);
  BOOL SetIndicators(const UINT* lpIDArray, int nIDCount);

  int CommandToIndex(UINT nIDFind) const;
  UINT GetItemID(int nIndex) const;
  void GetItemRect(int nIndex, LPRECT lpRect) const;
  void GetPaneText(int nIndex, CString& rString) const;
  CString GetPaneText(int nIndex) const;
  BOOL SetPaneText(int nIndex, LPCTSTR lpszNewText, BOOL bUpdate = TRUE);
  void GetPaneInfo(int nIndex, UINT& nID, UINT& nStyle, int& cxWidth) const;
  void SetPaneInfo(int nIndex, UINT nID, UINT nStyle, int cxWidth);
  UINT GetPaneStyle(int nIndex) const;
  void SetPaneStyle(int nIndex, UINT nStyle);
  CStatusBarCtrl& GetStatusBarCtrl() const { return *(CStatusBarCtrl*)this; }

  virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
  virtual CSize CalcFixedLayout(BOOL bStretch, BOOL bHorz);
  virtual void OnUpdateCmdUI(CFrameWnd* pTarget, BOOL bDisableIfNoHndler);
  virtual BOOL OnChildNotify(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult);
  virtual void OnBarStyleChange(DWORD dwOldStyle, DWORD dwNewStyle);

  void UpdateAllPanes(BOOL bUpdateRects, BOOL bUpdateText);
  AFX_STATUSPANE* _GetPanePtr(int nIndex) const { return ((AFX_STATUSPANE*)m_pData) + nIndex; }
  HFONT m_hFont;
  int m_cxSizeBox;
  BOOL m_bHideSizeBox;

  afx_msg void OnNcPaint();
  afx_msg void OnPaint();
  afx_msg BOOL OnEraseBkgnd(CDC* pDC);
  afx_msg void OnSize(UINT nType, int cx, int cy);
  afx_msg LRESULT OnSetFont(WPARAM wParam, LPARAM lParam);
  afx_msg LRESULT OnSetText(WPARAM wParam, LPARAM lParam);
  afx_msg LRESULT OnGetText(WPARAM wParam, LPARAM lParam);
  afx_msg LRESULT OnGetTextLength(WPARAM wParam, LPARAM lParam);
  DECLARE_MESSAGE_MAP()
};

// Updates a status bar pane during idle time.
class CStatusCmdUI : public CCmdUI {
public:
  virtual void Enable(BOOL bOn);
  virtual void SetCheck(int nCheck);
  virtual void SetText(LPCTSTR lpszText);
};

// ---------------------------------------------------------------------
// CToolBar

// Button styles: the low word is the TBSTYLE_, the high word the
// TBSTATE_ flags (with the enabled bit inverted).
#define TBBS_BUTTON MAKELONG(TBSTYLE_BUTTON, 0)
#define TBBS_SEPARATOR MAKELONG(TBSTYLE_SEP, 0)
#define TBBS_CHECKBOX MAKELONG(TBSTYLE_CHECK, 0)
#define TBBS_GROUP MAKELONG(TBSTYLE_GROUP, 0)
#define TBBS_CHECKGROUP (TBBS_GROUP | TBBS_CHECKBOX)
#define TBBS_DROPDOWN MAKELONG(TBSTYLE_DROPDOWN, 0)
#define TBBS_AUTOSIZE MAKELONG(TBSTYLE_AUTOSIZE, 0)
#define TBBS_NOPREFIX MAKELONG(TBSTYLE_NOPREFIX, 0)
#define TBBS_CHECKED MAKELONG(0, TBSTATE_CHECKED)
#define TBBS_PRESSED MAKELONG(0, TBSTATE_PRESSED)
#define TBBS_DISABLED MAKELONG(0, TBSTATE_ENABLED)
#define TBBS_INDETERMINATE MAKELONG(0, TBSTATE_INDETERMINATE)
#define TBBS_HIDDEN MAKELONG(0, TBSTATE_HIDDEN)
#define TBBS_WRAPPED MAKELONG(0, TBSTATE_WRAP)
#define TBBS_ELLIPSES MAKELONG(0, TBSTATE_ELLIPSES)
#define TBBS_MARKED MAKELONG(0, TBSTATE_MARKED)

class CToolBar : public CControlBar {
  DECLARE_DYNAMIC(CToolBar)
public:
  CToolBar();
  virtual ~CToolBar();

  virtual BOOL Create(CWnd* pParentWnd, DWORD dwStyle = WS_CHILD | WS_VISIBLE | CBRS_TOP,
                      UINT nID = AFX_IDW_TOOLBAR);
  virtual BOOL CreateEx(CWnd* pParentWnd, DWORD dwCtrlStyle = TBSTYLE_FLAT,
                        DWORD dwStyle = WS_CHILD | WS_VISIBLE | CBRS_ALIGN_TOP,
                        CRect rcBorders = CRect(0, 0, 0, 0), UINT nID = AFX_IDW_TOOLBAR);

  void SetSizes(SIZE sizeButton, SIZE sizeImage);
  void SetHeight(int cyHeight);
  BOOL LoadToolBar(LPCTSTR lpszResourceName);
  BOOL LoadToolBar(UINT nIDResource) { return LoadToolBar(MAKEINTRESOURCE(nIDResource)); }
  BOOL LoadBitmap(LPCTSTR lpszResourceName);
  BOOL LoadBitmap(UINT nIDResource) { return LoadBitmap(MAKEINTRESOURCE(nIDResource)); }
  BOOL SetBitmap(HBITMAP hbmImageWell);
  BOOL SetButtons(const UINT* lpIDArray, int nIDCount);

  int CommandToIndex(UINT nIDFind) const;
  UINT GetItemID(int nIndex) const;
  virtual void GetItemRect(int nIndex, LPRECT lpRect) const;
  UINT GetButtonStyle(int nIndex) const;
  void SetButtonStyle(int nIndex, UINT nStyle);
  void GetButtonInfo(int nIndex, UINT& nID, UINT& nStyle, int& iImage) const;
  void SetButtonInfo(int nIndex, UINT nID, UINT nStyle, int iImage);
  BOOL SetButtonText(int nIndex, LPCTSTR lpszText);
  CString GetButtonText(int nIndex) const;
  void GetButtonText(int nIndex, CString& rString) const;
  CToolBarCtrl& GetToolBarCtrl() const { return *(CToolBarCtrl*)this; }

  virtual CSize CalcFixedLayout(BOOL bStretch, BOOL bHorz);
  virtual CSize CalcDynamicLayout(int nLength, DWORD nMode);
  virtual void OnUpdateCmdUI(CFrameWnd* pTarget, BOOL bDisableIfNoHndler);
  virtual void OnBarStyleChange(DWORD dwOldStyle, DWORD dwNewStyle);
  virtual INT_PTR OnToolHitTest(CPoint point, TOOLINFO* pTI) const;

  HRSRC m_hRsrcImageWell;
  HINSTANCE m_hInstImageWell;
  HBITMAP m_hbmImageWell;
  BOOL m_bDelayedButtonLayout;
  CSize m_sizeImage;
  CSize m_sizeButton;
  CMapStringToPtr* m_pStringMap;

  void _GetButton(int nIndex, TBBUTTON* pButton) const;
  void _SetButton(int nIndex, TBBUTTON* pButton);
  CSize CalcLayout(DWORD nMode, int nLength = -1);
  void Layout();
  void _SetWraps(BOOL bAll);         // one row (FALSE) or one column (TRUE)
  void _ApplyWraps(const BYTE* pWrap);
  void _WrapToWidth(int nWidth);     // rows no wider than nWidth
  CSize _CalcButtonsSize();          // the extent of the visible buttons

  afx_msg void OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp);
  afx_msg void OnPaint();
  afx_msg BOOL OnEraseBkgnd(CDC* pDC);
  DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------
// CDialogBar

class CDialogBar : public CControlBar {
  DECLARE_DYNAMIC(CDialogBar)
public:
  CDialogBar();
  virtual ~CDialogBar();

  virtual BOOL Create(CWnd* pParentWnd, LPCTSTR lpszTemplateName, UINT nStyle, UINT nID);
  virtual BOOL Create(CWnd* pParentWnd, UINT nIDTemplate, UINT nStyle, UINT nID) {
    return Create(pParentWnd, MAKEINTRESOURCE(nIDTemplate), nStyle, nID);
  }

  CSize m_sizeDefault;
  virtual CSize CalcFixedLayout(BOOL bStretch, BOOL bHorz);
  virtual void OnUpdateCmdUI(CFrameWnd* pTarget, BOOL bDisableIfNoHndler);

  friend BOOL AFXAPI _AfxCreateDialogBarIndirect(CDialogBar*, CWnd*, LPCDLGTEMPLATE, UINT, UINT);

};

// ---------------------------------------------------------------------
// CDockState: where a frame's control bars are, which are floating and
// which are shown. CFrameWnd::SaveBarState and LoadBarState keep it in
// the application's profile; Serialize keeps it in an archive.

class CDockState : public CObject {
  DECLARE_SERIAL(CDockState)
public:
  CDockState();
  virtual ~CDockState();

  CPtrArray m_arrBarInfo;  // CControlBarInfo*

  void LoadState(LPCTSTR lpszProfileName);
  void SaveState(LPCTSTR lpszProfileName);
  void Clear();
  DWORD GetVersion() { return m_dwVersion; }
  virtual void Serialize(CArchive& ar);

  // Positions saved on a screen of another size are scaled to this one.
  CSize GetScreenSize();
  void SetScreenSize(CSize& size);
  void ScalePoint(CPoint& pt);
  void ScaleRectPos(CRect& rect);

  DWORD m_dwVersion;
  BOOL m_bScaling;
  CRect m_rectDevice;
  CRect m_rectClip;
};

// ---------------------------------------------------------------------
// CSplitterWnd

#define SPLS_DYNAMIC_SPLIT 0x0001
#define SPLS_INVERT_TRACKER 0x0002

class CSplitterWnd : public CWnd {
  DECLARE_DYNAMIC(CSplitterWnd)
public:
  CSplitterWnd();
  virtual ~CSplitterWnd();

  BOOL Create(CWnd* pParentWnd, int nMaxRows, int nMaxCols, SIZE sizeMin,
              CCreateContext* pContext,
              DWORD dwStyle = WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL | SPLS_DYNAMIC_SPLIT,
              UINT nID = AFX_IDW_PANE_FIRST);
  BOOL CreateStatic(CWnd* pParentWnd, int nRows, int nCols,
                    DWORD dwStyle = WS_CHILD | WS_VISIBLE, UINT nID = AFX_IDW_PANE_FIRST);
  virtual BOOL CreateView(int row, int col, CRuntimeClass* pViewClass, SIZE sizeInit,
                          CCreateContext* pContext);

  int GetRowCount() const { return m_nRows; }
  int GetColumnCount() const { return m_nCols; }
  void GetRowInfo(int row, int& cyCur, int& cyMin) const;
  void SetRowInfo(int row, int cyIdeal, int cyMin);
  void GetColumnInfo(int col, int& cxCur, int& cxMin) const;
  void SetColumnInfo(int col, int cxIdeal, int cxMin);
  DWORD GetScrollStyle() const;
  void SetScrollStyle(DWORD dwStyle);
  CWnd* GetPane(int row, int col) const;
  BOOL IsChildPane(CWnd* pWnd, int* pRow, int* pCol);
  BOOL IsChildPane(CWnd* pWnd, int& nRow, int& nCol) { return IsChildPane(pWnd, &nRow, &nCol); }
  int IdFromRowCol(int row, int col) const { return AFX_IDW_PANE_FIRST + row * 16 + col; }
  BOOL IsTracking() const { return m_bTracking; }

  virtual void RecalcLayout();
  virtual void OnDrawSplitter(CDC* pDC, int nType, const CRect& rect);
  virtual void OnInvertTracker(const CRect& rect);
  virtual BOOL CreateScrollBarCtrl(DWORD dwStyle, UINT nID);
  virtual void DeleteView(int row, int col);
  virtual BOOL SplitRow(int cyBefore);
  virtual BOOL SplitColumn(int cxBefore);
  virtual void DeleteRow(int rowDelete);
  virtual void DeleteColumn(int colDelete);
  virtual CWnd* GetActivePane(int* pRow = NULL, int* pCol = NULL);
  virtual void SetActivePane(int row, int col, CWnd* pWnd = NULL);
  CWnd* GetActivePane(int& row, int& col) { return GetActivePane(&row, &col); }
  virtual BOOL CanActivateNext(BOOL bPrev = FALSE);
  virtual void ActivateNext(BOOL bPrev = FALSE);
  virtual BOOL DoKeyboardSplit();
  virtual BOOL DoScroll(CView* pViewFrom, UINT nScrollCode, BOOL bDoScroll = TRUE);
  virtual BOOL DoScrollBy(CView* pViewFrom, CSize sizeScroll, BOOL bDoScroll = TRUE);
  virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
  virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);

  enum ESplitType { splitBox, splitBar, splitIntersection, splitBorder };
  enum HitTestValue {
    noHit = 0,
    vSplitterBox = 1,
    hSplitterBox = 2,
    bothSplitterBox = 3,
    vSplitterBar1 = 101,
    hSplitterBar1 = 201,
    splitterIntersection1 = 301,
  };

  struct CRowColInfo {
    int nMinSize;
    int nIdealSize;
    int nCurSize;
  };

  CRuntimeClass* m_pDynamicViewClass;
  int m_nMaxRows, m_nMaxCols;
  int m_cxSplitter, m_cySplitter;
  int m_cxBorderShare, m_cyBorderShare;
  int m_cxSplitterGap, m_cySplitterGap;
  int m_cxBorder, m_cyBorder;
  int m_nRows, m_nCols;
  BOOL m_bHasHScroll, m_bHasVScroll;
  CRowColInfo* m_pColInfo;
  CRowColInfo* m_pRowInfo;
  BOOL m_bTracking, m_bTracking2;
  CPoint m_ptTrackOffset;
  CRect m_rectLimit;
  CRect m_rectTracker, m_rectTracker2;
  int m_htTrack;

  BOOL CreateCommon(CWnd* pParentWnd, SIZE sizeMin, DWORD dwStyle, UINT nID);
  int HitTest(CPoint pt) const;
  void GetInsideRect(CRect& rect) const;
  void GetHitRect(int ht, CRect& rect);
  void TrackRowSize(int y, int row);
  void TrackColumnSize(int x, int col);
  void DrawAllSplitBars(CDC* pDC, int cxInside, int cyInside);
  void StartTracking(int ht);
  void StopTracking(BOOL bAccept);
  void SetSplitCursor(int ht);
  CWnd* GetSizingParent();

  // Shared scroll bars and split boxes.
  BOOL _HasHStrip() const;
  BOOL _HasVStrip() const;
  BOOL _ShowVBox() const;
  BOOL _ShowHBox() const;
  void _SyncScrollBars();
  void _MoveTrackers(CPoint pt);
  void _SyncPanes(CView* pViewFrom, CPoint ptOld);
  void _AcceptTrack();

  afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
  afx_msg void OnMouseMove(UINT nFlags, CPoint pt);
  afx_msg void OnPaint();
  afx_msg void OnLButtonDown(UINT nFlags, CPoint pt);
  afx_msg void OnLButtonDblClk(UINT nFlags, CPoint pt);
  afx_msg void OnLButtonUp(UINT nFlags, CPoint pt);
  afx_msg void OnCancelMode();
  afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg void OnSize(UINT nType, int cx, int cy);
  afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
  afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
  afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
  afx_msg void OnCaptureChanged(CWnd* pWnd);
  DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------
// CFormView

class CFormView : public CScrollView {
  DECLARE_DYNAMIC(CFormView)
protected:
  CFormView(LPCTSTR lpszTemplateName);
  CFormView(UINT nIDTemplate);

public:
  virtual BOOL Create(LPCTSTR lpszClassName, LPCTSTR lpszWindowName, DWORD dwStyle,
                      const RECT& rect, CWnd* pParentWnd, UINT nID,
                      CCreateContext* pContext = NULL);
  virtual void OnDraw(CDC* pDC);
  virtual void OnInitialUpdate();
  virtual BOOL PreTranslateMessage(MSG* pMsg);
  virtual void OnActivateView(BOOL bActivate, CView* pActivateView, CView* pDeactiveView);
  virtual void OnActivateFrame(UINT nState, CFrameWnd* pFrameWnd);

  BOOL SaveFocusControl();

  LPCTSTR m_lpszTemplateName;
  CCreateContext* m_pCreateContext;
  HWND m_hWndFocus;

  afx_msg int OnCreate(LPCREATESTRUCT lpcs);
  afx_msg void OnSetFocus(CWnd* pOldWnd);
  DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------
// CEditView

class CEditView : public CCtrlView {
  DECLARE_DYNCREATE(CEditView)
public:
  CEditView();
  virtual ~CEditView();

  static const DWORD dwStyleDefault;

  CEdit& GetEditCtrl() const { return *(CEdit*)this; }
  void GetSelectedText(CString& strResult) const;
  LPCTSTR LockBuffer() const;
  void UnlockBuffer() const;
  UINT GetBufferLength() const;
  CFont* GetPrinterFont() const;
  void SetPrinterFont(CFont* pFont);
  void SetTabStops(int nTabStops);
  BOOL FindText(LPCTSTR lpszFind, BOOL bNext = TRUE, BOOL bCase = TRUE);
  void SerializeRaw(CArchive& ar);
  UINT PrintInsideRect(CDC* pDC, RECT& rectLayout, UINT nIndexStart, UINT nIndexStop);

  virtual void Serialize(CArchive& ar);
  virtual void OnFindNext(LPCTSTR lpszFind, BOOL bNext, BOOL bCase);
  virtual void OnReplaceSel(LPCTSTR lpszFind, BOOL bNext, BOOL bCase, LPCTSTR lpszReplace);
  virtual void OnReplaceAll(LPCTSTR lpszFind, LPCTSTR lpszReplace, BOOL bCase);
  virtual void OnTextNotFound(LPCTSTR lpszFind);
  virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
  virtual void OnPrepareDC(CDC* pDC, CPrintInfo* pInfo = NULL);
  virtual BOOL OnPreparePrinting(CPrintInfo* pInfo);
  virtual void OnBeginPrinting(CDC* pDC, CPrintInfo* pInfo);
  virtual void OnPrint(CDC* pDC, CPrintInfo* pInfo);
  virtual void OnEndPrinting(CDC* pDC, CPrintInfo* pInfo);
  virtual void CalcWindowRect(LPRECT lpClientRect, UINT nAdjustType = adjustBorder);

  int m_nTabStops;
  mutable LPTSTR m_pShadowBuffer;  // a copy of the text for LockBuffer
  mutable UINT m_nShadowSize;
  CUIntArray m_aPageStart;
  HFONT m_hPrinterFont;
  HFONT m_hMirrorFont;
  CSize m_sizeChar;  // printer character size (OnBeginPrinting)
  BOOL InitializeReplace();
  BOOL SameAsSelected(LPCTSTR lpszCompare, BOOL bCase);
  void OnEditFindReplace(BOOL bFindOnly);
  void ReadFromArchive(CArchive& ar, UINT nBytes);
  void WriteToArchive(CArchive& ar);

  afx_msg int OnCreate(LPCREATESTRUCT lpcs);
  afx_msg void OnDestroy();
  afx_msg LRESULT OnSetFont(WPARAM wParam, LPARAM lParam);
  afx_msg void OnUpdateNeedSel(CCmdUI* pCmdUI);
  afx_msg void OnUpdateNeedClip(CCmdUI* pCmdUI);
  afx_msg void OnUpdateNeedText(CCmdUI* pCmdUI);
  afx_msg void OnUpdateNeedFind(CCmdUI* pCmdUI);
  afx_msg void OnUpdateEditUndo(CCmdUI* pCmdUI);
  afx_msg BOOL OnEditChange();
  afx_msg void OnEditCut();
  afx_msg void OnEditCopy();
  afx_msg void OnEditPaste();
  afx_msg void OnEditClear();
  afx_msg void OnEditUndo();
  afx_msg void OnEditSelectAll();
  afx_msg void OnEditFind();
  afx_msg void OnEditReplace();
  afx_msg void OnEditRepeat();
  afx_msg LRESULT OnFindReplaceCmd(WPARAM wParam, LPARAM lParam);
  DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------
// CRectTracker: a rectangle drawn with a border and resize handles that
// the user can move and resize with the mouse.

class CRectTracker {
public:
  enum StyleFlags {
    solidLine = 1,
    dottedLine = 2,
    hatchedBorder = 4,
    resizeInside = 8,
    resizeOutside = 16,
    hatchInside = 32,
  };
  enum TrackerHit {
    hitNothing = -1,
    hitTopLeft = 0,
    hitTopRight = 1,
    hitBottomRight = 2,
    hitBottomLeft = 3,
    hitTop = 4,
    hitRight = 5,
    hitBottom = 6,
    hitLeft = 7,
    hitMiddle = 8,
  };

  CRectTracker();
  CRectTracker(LPCRECT lpSrcRect, UINT nStyle);
  virtual ~CRectTracker();

  UINT m_nStyle;
  CRect m_rect;
  CSize m_sizeMin;
  int m_nHandleSize;

  void Draw(CDC* pDC) const;
  void GetTrueRect(LPRECT lpTrueRect) const;
  BOOL SetCursor(CWnd* pWnd, UINT nHitTest) const;
  BOOL Track(CWnd* pWnd, CPoint point, BOOL bAllowInvert = FALSE, CWnd* pWndClipTo = NULL);
  BOOL TrackRubberBand(CWnd* pWnd, CPoint point, BOOL bAllowInvert = TRUE);
  int HitTest(CPoint point) const;
  int NormalizeHit(int nHandle) const;

  virtual void DrawTrackerRect(LPCRECT lpRect, CWnd* pWndClipTo, CDC* pDC, CWnd* pWnd);
  virtual void AdjustRect(int nHandle, LPRECT lpRect);
  virtual void OnChangedRect(const CRect& rectOld);
  virtual UINT GetHandleMask() const;

protected:
  BOOL m_bAllowInvert;
  CRect m_rectLast;
  CSize m_sizeLast;
  BOOL m_bErase;
  BOOL m_bFinalErase;

  void Construct();
  virtual int GetHandleSize(LPCRECT lpRect = NULL) const;
  BOOL TrackHandle(int nHandle, CWnd* pWnd, CPoint point, CWnd* pWndClipTo);
  void GetHandleRect(int nHandle, CRect* pHandleRect) const;
  void GetModifyPointers(int nHandle, int** ppx, int** ppy, int* px, int* py);
  int HitTestHandles(CPoint point) const;
};

// ---------------------------------------------------------------------
// CBitmapButton

class CBitmapButton : public CButton {
  DECLARE_DYNAMIC(CBitmapButton)
public:
  CBitmapButton();
  virtual ~CBitmapButton();

  BOOL LoadBitmaps(LPCTSTR lpszBitmapResource, LPCTSTR lpszBitmapResourceSel = NULL,
                   LPCTSTR lpszBitmapResourceFocus = NULL,
                   LPCTSTR lpszBitmapResourceDisabled = NULL);
  BOOL LoadBitmaps(UINT nIDBitmapResource, UINT nIDBitmapResourceSel = 0,
                   UINT nIDBitmapResourceFocus = 0, UINT nIDBitmapResourceDisabled = 0);
  BOOL AutoLoad(UINT nID, CWnd* pParent);
  void SizeToContent();

  CBitmap m_bitmap;
  CBitmap m_bitmapSel;
  CBitmap m_bitmapFocus;
  CBitmap m_bitmapDisabled;

  virtual void DrawItem(LPDRAWITEMSTRUCT lpDIS);
};

// ---------------------------------------------------------------------
// CPrintInfo: the state of a print job, passed to the view's printing
// functions.

#define PRINTFLAG_MAYBOTHERUSER 0x0001
#define PRINTFLAG_PROMPTUSER 0x0002
#define PRINTFLAG_USERSUPPLIEDPRINTER 0x0004
#define PRINTFLAG_FORCEALLPAGES 0x0008
#define PRINTFLAG_RECTPAGES 0x0010

// What a frame saves while it shows print preview (CFrameWnd::OnSetPreviewMode).
struct CPrintPreviewState : public CObject {
  UINT nIDMainPane;
  HMENU hMenu;
  DWORD dwStates;
  CView* pViewActiveOld;
  BOOL(CALLBACK* lpfnCloseProc)(CFrameWnd* pFrameWnd);
  HACCEL hAccelTable;
  CPrintPreviewState();
};

class CPrintInfo {
public:
  CPrintInfo();
  ~CPrintInfo();

  CPrintDialog* m_pPD;
  BOOL m_bDocObject;
  DWORD m_dwFlags;
  UINT m_nOffsetPage;
  BOOL m_bDirect;
  BOOL m_bPreview;
  BOOL m_bContinuePrinting;
  UINT m_nCurPage;
  UINT m_nNumPreviewPages;
  CString m_strPageDesc;
  LPVOID m_lpUserData;
  CRect m_rectDraw;
  int m_nJobNumber;

  void SetMinPage(UINT nMinPage) { m_pPD->m_pd.nMinPage = (WORD)nMinPage; }
  void SetMaxPage(UINT nMaxPage) { m_pPD->m_pd.nMaxPage = (WORD)nMaxPage; }
  UINT GetMinPage() const { return m_pPD->m_pd.nMinPage; }
  UINT GetMaxPage() const { return m_pPD->m_pd.nMaxPage; }
  UINT GetFromPage() const { return m_pPD->m_pd.nFromPage; }
  UINT GetToPage() const { return m_pPD->m_pd.nToPage; }
  UINT GetOffsetPage() const { return m_nOffsetPage; }
};

#endif  // __AFXEXT_H__
