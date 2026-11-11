// afxcmn.h - the Windows common controls.
//
// The classes wrap the controls' messages. Features that need a newer
// comctl32.dll than Windows 95 shipped (4.70 and later) are still
// available; they simply do nothing on systems without them.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXCMN_H__
#define __AFXCMN_H__

#include "afxwin.h"
#include <commctrl.h>
#include <richedit.h>

class CToolTipCtrl;

// ---------------------------------------------------------------------
// CImageList

class CImageList : public CObject {
  DECLARE_DYNCREATE(CImageList)
public:
  HIMAGELIST m_hImageList;

  CImageList() : m_hImageList(NULL) {}
  virtual ~CImageList();

  operator HIMAGELIST() const { return m_hImageList; }
  HIMAGELIST GetSafeHandle() const { return AFX_IS_NULL_THIS() ? NULL : m_hImageList; }
  static CImageList* PASCAL FromHandle(HIMAGELIST hImageList);
  static CImageList* PASCAL FromHandlePermanent(HIMAGELIST hImageList);
  static void PASCAL DeleteTempMap();

  BOOL Create(int cx, int cy, UINT nFlags, int nInitial, int nGrow);
  BOOL Create(UINT nBitmapID, int cx, int nGrow, COLORREF crMask);
  BOOL Create(LPCTSTR lpszBitmapID, int cx, int nGrow, COLORREF crMask);
  BOOL Create(CImageList& imagelist1, int nImage1, CImageList& imagelist2, int nImage2, int dx,
              int dy);
  BOOL Create(CImageList* pImageList);
  BOOL Attach(HIMAGELIST hImageList);
  HIMAGELIST Detach();
  BOOL DeleteImageList();

  int GetImageCount() const { return ImageList_GetImageCount(m_hImageList); }
  COLORREF SetBkColor(COLORREF cr) { return ImageList_SetBkColor(m_hImageList, cr); }
  COLORREF GetBkColor() const { return ImageList_GetBkColor(m_hImageList); }
  BOOL GetImageInfo(int nImage, IMAGEINFO* pImageInfo) const {
    return ImageList_GetImageInfo(m_hImageList, nImage, pImageInfo);
  }
  int Add(CBitmap* pbmImage, CBitmap* pbmMask) {
    return ImageList_Add(m_hImageList, (HBITMAP)pbmImage->GetSafeHandle(),
                         (HBITMAP)pbmMask->GetSafeHandle());
  }
  int Add(CBitmap* pbmImage, COLORREF crMask) {
    return ImageList_AddMasked(m_hImageList, (HBITMAP)pbmImage->GetSafeHandle(), crMask);
  }
  int Add(HICON hIcon) { return ImageList_ReplaceIcon(m_hImageList, -1, hIcon); }
  BOOL Remove(int nImage) { return ImageList_Remove(m_hImageList, nImage); }
  BOOL Replace(int nImage, CBitmap* pbmImage, CBitmap* pbmMask) {
    return ImageList_Replace(m_hImageList, nImage, (HBITMAP)pbmImage->GetSafeHandle(),
                             (HBITMAP)pbmMask->GetSafeHandle());
  }
  int Replace(int nImage, HICON hIcon) { return ImageList_ReplaceIcon(m_hImageList, nImage, hIcon); }
  HICON ExtractIcon(int nImage) { return ImageList_GetIcon(m_hImageList, nImage, ILD_NORMAL); }
  BOOL Draw(CDC* pDC, int nImage, POINT pt, UINT nStyle) {
    return ImageList_Draw(m_hImageList, nImage, pDC->GetSafeHdc(), pt.x, pt.y, nStyle);
  }
  BOOL SetOverlayImage(int nImage, int nOverlay) {
    return ImageList_SetOverlayImage(m_hImageList, nImage, nOverlay);
  }
  BOOL SetDragCursorImage(int nDrag, CPoint ptHotSpot) {
    return ImageList_SetDragCursorImage(m_hImageList, nDrag, ptHotSpot.x, ptHotSpot.y);
  }
  BOOL BeginDrag(int nImage, CPoint ptHotSpot) {
    return ImageList_BeginDrag(m_hImageList, nImage, ptHotSpot.x, ptHotSpot.y);
  }
  static void PASCAL EndDrag() { ImageList_EndDrag(); }
  static BOOL PASCAL DragMove(CPoint pt) { return ImageList_DragMove(pt.x, pt.y); }
  static BOOL PASCAL DragShowNolock(BOOL bShow) { return ImageList_DragShowNolock(bShow); }
  static BOOL PASCAL DragEnter(CWnd* pWndLock, CPoint point) {
    return ImageList_DragEnter(pWndLock->GetSafeHwnd(), point.x, point.y);
  }
  static BOOL PASCAL DragLeave(CWnd* pWndLock) { return ImageList_DragLeave(pWndLock->GetSafeHwnd()); }
  BOOL Read(CArchive* pArchive);
  BOOL Write(CArchive* pArchive);
};

// ---------------------------------------------------------------------
// Bars

class CToolBarCtrl : public CWnd {
  DECLARE_DYNAMIC(CToolBarCtrl)
public:
  CToolBarCtrl() {}
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);

  BOOL EnableButton(int nID, BOOL bEnable = TRUE) {
    return (BOOL)::SendMessage(m_hWnd, TB_ENABLEBUTTON, nID, MAKELPARAM(bEnable, 0));
  }
  BOOL CheckButton(int nID, BOOL bCheck = TRUE) {
    return (BOOL)::SendMessage(m_hWnd, TB_CHECKBUTTON, nID, MAKELPARAM(bCheck, 0));
  }
  BOOL PressButton(int nID, BOOL bPress = TRUE) {
    return (BOOL)::SendMessage(m_hWnd, TB_PRESSBUTTON, nID, MAKELPARAM(bPress, 0));
  }
  BOOL HideButton(int nID, BOOL bHide = TRUE) {
    return (BOOL)::SendMessage(m_hWnd, TB_HIDEBUTTON, nID, MAKELPARAM(bHide, 0));
  }
  BOOL Indeterminate(int nID, BOOL bIndeterminate = TRUE) {
    return (BOOL)::SendMessage(m_hWnd, TB_INDETERMINATE, nID, MAKELPARAM(bIndeterminate, 0));
  }
  BOOL IsButtonEnabled(int nID) const { return (BOOL)::SendMessage(m_hWnd, TB_ISBUTTONENABLED, nID, 0); }
  BOOL IsButtonChecked(int nID) const { return (BOOL)::SendMessage(m_hWnd, TB_ISBUTTONCHECKED, nID, 0); }
  BOOL IsButtonPressed(int nID) const { return (BOOL)::SendMessage(m_hWnd, TB_ISBUTTONPRESSED, nID, 0); }
  BOOL IsButtonHidden(int nID) const { return (BOOL)::SendMessage(m_hWnd, TB_ISBUTTONHIDDEN, nID, 0); }
  BOOL IsButtonIndeterminate(int nID) const {
    return (BOOL)::SendMessage(m_hWnd, TB_ISBUTTONINDETERMINATE, nID, 0);
  }
  BOOL SetState(int nID, UINT nState) {
    return (BOOL)::SendMessage(m_hWnd, TB_SETSTATE, nID, MAKELPARAM(nState, 0));
  }
  int GetState(int nID) const { return (int)::SendMessage(m_hWnd, TB_GETSTATE, nID, 0); }
  BOOL AddButtons(int nNumButtons, LPTBBUTTON lpButtons) {
    return (BOOL)::SendMessage(m_hWnd, TB_ADDBUTTONS, nNumButtons, (LPARAM)lpButtons);
  }
  BOOL InsertButton(int nIndex, LPTBBUTTON lpButton) {
    return (BOOL)::SendMessage(m_hWnd, TB_INSERTBUTTON, nIndex, (LPARAM)lpButton);
  }
  BOOL DeleteButton(int nIndex) { return (BOOL)::SendMessage(m_hWnd, TB_DELETEBUTTON, nIndex, 0); }
  BOOL GetButton(int nIndex, LPTBBUTTON lpButton) const {
    return (BOOL)::SendMessage(m_hWnd, TB_GETBUTTON, nIndex, (LPARAM)lpButton);
  }
  int GetButtonCount() const { return (int)::SendMessage(m_hWnd, TB_BUTTONCOUNT, 0, 0); }
  UINT CommandToIndex(UINT nID) const { return (UINT)::SendMessage(m_hWnd, TB_COMMANDTOINDEX, nID, 0); }
  BOOL GetItemRect(int nIndex, LPRECT lpRect) const {
    return (BOOL)::SendMessage(m_hWnd, TB_GETITEMRECT, nIndex, (LPARAM)lpRect);
  }
  int AddBitmap(int nNumButtons, UINT nBitmapID);
  int AddBitmap(int nNumButtons, CBitmap* pBitmap);
  int AddString(UINT nStringID);
  int AddStrings(LPCTSTR lpszStrings) {
    return (int)::SendMessage(m_hWnd, TB_ADDSTRING, 0, (LPARAM)lpszStrings);
  }
  void AutoSize() { ::SendMessage(m_hWnd, TB_AUTOSIZE, 0, 0); }
  void SetButtonStructSize(int nSize) { ::SendMessage(m_hWnd, TB_BUTTONSTRUCTSIZE, nSize, 0); }
  BOOL SetButtonSize(CSize size) {
    return (BOOL)::SendMessage(m_hWnd, TB_SETBUTTONSIZE, 0, MAKELPARAM(size.cx, size.cy));
  }
  BOOL SetBitmapSize(CSize size) {
    return (BOOL)::SendMessage(m_hWnd, TB_SETBITMAPSIZE, 0, MAKELPARAM(size.cx, size.cy));
  }
  CSize GetButtonSize() const {
    DWORD dw = (DWORD)::SendMessage(m_hWnd, TB_GETBUTTONSIZE, 0, 0);
    return CSize(LOWORD(dw), HIWORD(dw));
  }
  CToolTipCtrl* GetToolTips() const;
  void SetToolTips(CToolTipCtrl* pTip);
  void SetOwner(CWnd* pWnd) { ::SendMessage(m_hWnd, TB_SETPARENT, (WPARAM)pWnd->GetSafeHwnd(), 0); }
  void SetRows(int nRows, BOOL bLarger, LPRECT lpRect) {
    ::SendMessage(m_hWnd, TB_SETROWS, MAKEWPARAM(nRows, bLarger), (LPARAM)lpRect);
  }
  int GetRows() const { return (int)::SendMessage(m_hWnd, TB_GETROWS, 0, 0); }
  BOOL SetCmdID(int nIndex, UINT nID) { return (BOOL)::SendMessage(m_hWnd, TB_SETCMDID, nIndex, nID); }
  UINT GetBitmapFlags() const { return (UINT)::SendMessage(m_hWnd, TB_GETBITMAPFLAGS, 0, 0); }
  int HitTest(LPPOINT ppt) const { return (int)::SendMessage(m_hWnd, TB_HITTEST, 0, (LPARAM)ppt); }
  CImageList* SetImageList(CImageList* pImageList);
  CImageList* SetHotImageList(CImageList* pImageList);
  CImageList* SetDisabledImageList(CImageList* pImageList);
  DWORD SetExtendedStyle(DWORD dwExStyle) {
    return (DWORD)::SendMessage(m_hWnd, TB_SETEXTENDEDSTYLE, 0, dwExStyle);
  }
  DWORD GetExtendedStyle() const { return (DWORD)::SendMessage(m_hWnd, TB_GETEXTENDEDSTYLE, 0, 0); }
  BOOL GetButtonInfo(int nID, TBBUTTONINFO* ptbbi) const {
    return (BOOL)::SendMessage(m_hWnd, TB_GETBUTTONINFO, nID, (LPARAM)ptbbi);
  }
  BOOL SetButtonInfo(int nID, TBBUTTONINFO* ptbbi) {
    return (BOOL)::SendMessage(m_hWnd, TB_SETBUTTONINFO, nID, (LPARAM)ptbbi);
  }
  void Customize() { ::SendMessage(m_hWnd, TB_CUSTOMIZE, 0, 0); }
};

class CStatusBarCtrl : public CWnd {
  DECLARE_DYNAMIC(CStatusBarCtrl)
public:
  CStatusBarCtrl() {}
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);

  BOOL SetText(LPCTSTR lpszText, int nPane, int nType) {
    return (BOOL)::SendMessage(m_hWnd, SB_SETTEXT, (nPane | nType), (LPARAM)lpszText);
  }
  CString GetText(int nPane, int* pType = NULL) const;
  int GetTextLength(int nPane, int* pType = NULL) const {
    DWORD dw = (DWORD)::SendMessage(m_hWnd, SB_GETTEXTLENGTH, nPane, 0);
    if (pType != NULL) *pType = HIWORD(dw);
    return LOWORD(dw);
  }
  BOOL SetParts(int nParts, int* pWidths) {
    return (BOOL)::SendMessage(m_hWnd, SB_SETPARTS, nParts, (LPARAM)pWidths);
  }
  int GetParts(int nParts, int* pParts) const {
    return (int)::SendMessage(m_hWnd, SB_GETPARTS, nParts, (LPARAM)pParts);
  }
  BOOL GetBorders(int* pBorders) const {
    return (BOOL)::SendMessage(m_hWnd, SB_GETBORDERS, 0, (LPARAM)pBorders);
  }
  void SetMinHeight(int nMin) { ::SendMessage(m_hWnd, SB_SETMINHEIGHT, nMin, 0); }
  BOOL SetSimple(BOOL bSimple = TRUE) { return (BOOL)::SendMessage(m_hWnd, SB_SIMPLE, bSimple, 0); }
  BOOL IsSimple() const { return (BOOL)::SendMessage(m_hWnd, SB_ISSIMPLE, 0, 0); }
  BOOL GetRect(int nPane, LPRECT lpRect) const {
    return (BOOL)::SendMessage(m_hWnd, SB_GETRECT, nPane, (LPARAM)lpRect);
  }
  BOOL SetIcon(int nPane, HICON hIcon) {
    return (BOOL)::SendMessage(m_hWnd, SB_SETICON, nPane, (LPARAM)hIcon);
  }
  void SetTipText(int nPane, LPCTSTR pszTipText) {
    ::SendMessage(m_hWnd, SB_SETTIPTEXT, nPane, (LPARAM)pszTipText);
  }
  COLORREF SetBkColor(COLORREF cr) { return (COLORREF)::SendMessage(m_hWnd, SB_SETBKCOLOR, 0, cr); }
};

// ---------------------------------------------------------------------
// Tooltips

class CToolTipCtrl : public CWnd {
  DECLARE_DYNAMIC(CToolTipCtrl)
public:
  CToolTipCtrl() {}
  virtual BOOL Create(CWnd* pParentWnd, DWORD dwStyle = 0);

  void Activate(BOOL bActivate) { ::SendMessage(m_hWnd, TTM_ACTIVATE, bActivate, 0); }
  BOOL AddTool(CWnd* pWnd, LPCTSTR lpszText = LPSTR_TEXTCALLBACK, LPCRECT lpRectTool = NULL,
               UINT_PTR nIDTool = 0);
  BOOL AddTool(CWnd* pWnd, UINT nIDText, LPCRECT lpRectTool = NULL, UINT_PTR nIDTool = 0);
  void DelTool(CWnd* pWnd, UINT_PTR nIDTool = 0);
  void UpdateTipText(LPCTSTR lpszText, CWnd* pWnd, UINT_PTR nIDTool = 0);
  void UpdateTipText(UINT nIDText, CWnd* pWnd, UINT_PTR nIDTool = 0);
  void RelayEvent(LPMSG lpMsg) { ::SendMessage(m_hWnd, TTM_RELAYEVENT, 0, (LPARAM)lpMsg); }
  void SetDelayTime(UINT nDelay) { ::SendMessage(m_hWnd, TTM_SETDELAYTIME, TTDT_AUTOMATIC, nDelay); }
  void SetDelayTime(DWORD dwDuration, int iTime) {
    ::SendMessage(m_hWnd, TTM_SETDELAYTIME, dwDuration, MAKELPARAM(iTime, 0));
  }
  int SetMaxTipWidth(int iWidth) { return (int)::SendMessage(m_hWnd, TTM_SETMAXTIPWIDTH, 0, iWidth); }
  int GetToolCount() const { return (int)::SendMessage(m_hWnd, TTM_GETTOOLCOUNT, 0, 0); }
  void Pop() { ::SendMessage(m_hWnd, TTM_POP, 0, 0); }
  void Update() { ::SendMessage(m_hWnd, TTM_UPDATE, 0, 0); }
  void FillInToolInfo(TOOLINFO& ti, CWnd* pWnd, UINT_PTR nIDTool) const;
};

// ---------------------------------------------------------------------
// Simple controls

class CProgressCtrl : public CWnd {
  DECLARE_DYNAMIC(CProgressCtrl)
public:
  CProgressCtrl() {}
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);
  void SetRange(short nLower, short nUpper) {
    ::SendMessage(m_hWnd, PBM_SETRANGE, 0, MAKELPARAM(nLower, nUpper));
  }
  void SetRange32(int nLower, int nUpper) { ::SendMessage(m_hWnd, PBM_SETRANGE32, nLower, nUpper); }
  void GetRange(int& nLower, int& nUpper) const {
    PBRANGE range;
    ::SendMessage(m_hWnd, PBM_GETRANGE, FALSE, (LPARAM)&range);
    nLower = range.iLow;
    nUpper = range.iHigh;
  }
  int GetPos() const { return (int)::SendMessage(m_hWnd, PBM_GETPOS, 0, 0); }
  int SetPos(int nPos) { return (int)(short)LOWORD(::SendMessage(m_hWnd, PBM_SETPOS, nPos, 0)); }
  int OffsetPos(int nPos) { return (int)(short)LOWORD(::SendMessage(m_hWnd, PBM_DELTAPOS, nPos, 0)); }
  int SetStep(int nStep) { return (int)(short)LOWORD(::SendMessage(m_hWnd, PBM_SETSTEP, nStep, 0)); }
  int StepIt() { return (int)(short)LOWORD(::SendMessage(m_hWnd, PBM_STEPIT, 0, 0)); }
  COLORREF SetBarColor(COLORREF clrBar) {
    return (COLORREF)::SendMessage(m_hWnd, PBM_SETBARCOLOR, 0, clrBar);
  }
  COLORREF SetBkColor(COLORREF clrNew) {
    return (COLORREF)::SendMessage(m_hWnd, PBM_SETBKCOLOR, 0, clrNew);
  }
};

class CSliderCtrl : public CWnd {
  DECLARE_DYNAMIC(CSliderCtrl)
public:
  CSliderCtrl() {}
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);
  int GetLineSize() const { return (int)::SendMessage(m_hWnd, TBM_GETLINESIZE, 0, 0); }
  int SetLineSize(int nSize) { return (int)::SendMessage(m_hWnd, TBM_SETLINESIZE, 0, nSize); }
  int GetPageSize() const { return (int)::SendMessage(m_hWnd, TBM_GETPAGESIZE, 0, 0); }
  int SetPageSize(int nSize) { return (int)::SendMessage(m_hWnd, TBM_SETPAGESIZE, 0, nSize); }
  int GetRangeMax() const { return (int)::SendMessage(m_hWnd, TBM_GETRANGEMAX, 0, 0); }
  int GetRangeMin() const { return (int)::SendMessage(m_hWnd, TBM_GETRANGEMIN, 0, 0); }
  void GetRange(int& nMin, int& nMax) const {
    nMin = GetRangeMin();
    nMax = GetRangeMax();
  }
  void SetRangeMin(int nMin, BOOL bRedraw = FALSE) {
    ::SendMessage(m_hWnd, TBM_SETRANGEMIN, bRedraw, nMin);
  }
  void SetRangeMax(int nMax, BOOL bRedraw = FALSE) {
    ::SendMessage(m_hWnd, TBM_SETRANGEMAX, bRedraw, nMax);
  }
  void SetRange(int nMin, int nMax, BOOL bRedraw = FALSE) {
    SetRangeMin(nMin, FALSE);
    SetRangeMax(nMax, bRedraw);
  }
  void GetSelection(int& nMin, int& nMax) const {
    nMin = (int)::SendMessage(m_hWnd, TBM_GETSELSTART, 0, 0);
    nMax = (int)::SendMessage(m_hWnd, TBM_GETSELEND, 0, 0);
  }
  void SetSelection(int nMin, int nMax) {
    ::SendMessage(m_hWnd, TBM_SETSELSTART, FALSE, nMin);
    ::SendMessage(m_hWnd, TBM_SETSELEND, TRUE, nMax);
  }
  void GetChannelRect(LPRECT lprc) const { ::SendMessage(m_hWnd, TBM_GETCHANNELRECT, 0, (LPARAM)lprc); }
  void GetThumbRect(LPRECT lprc) const { ::SendMessage(m_hWnd, TBM_GETTHUMBRECT, 0, (LPARAM)lprc); }
  int GetPos() const { return (int)::SendMessage(m_hWnd, TBM_GETPOS, 0, 0); }
  void SetPos(int nPos) { ::SendMessage(m_hWnd, TBM_SETPOS, TRUE, nPos); }
  UINT GetNumTics() const { return (UINT)::SendMessage(m_hWnd, TBM_GETNUMTICS, 0, 0); }
  BOOL SetTic(int nTic) { return (BOOL)::SendMessage(m_hWnd, TBM_SETTIC, 0, nTic); }
  void SetTicFreq(int nFreq) { ::SendMessage(m_hWnd, TBM_SETTICFREQ, nFreq, 0); }
  void ClearSel(BOOL bRedraw = FALSE) { ::SendMessage(m_hWnd, TBM_CLEARSEL, bRedraw, 0); }
  void ClearTics(BOOL bRedraw = FALSE) { ::SendMessage(m_hWnd, TBM_CLEARTICS, bRedraw, 0); }
  void VerifyPos() {}
};

class CSpinButtonCtrl : public CWnd {
  DECLARE_DYNAMIC(CSpinButtonCtrl)
public:
  CSpinButtonCtrl() {}
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);
  BOOL SetAccel(int nAccel, UDACCEL* pAccel) {
    return (BOOL)::SendMessage(m_hWnd, UDM_SETACCEL, nAccel, (LPARAM)pAccel);
  }
  UINT GetAccel(int nAccel, UDACCEL* pAccel) const {
    return (UINT)::SendMessage(m_hWnd, UDM_GETACCEL, nAccel, (LPARAM)pAccel);
  }
  int SetBase(int nBase) { return (int)::SendMessage(m_hWnd, UDM_SETBASE, nBase, 0); }
  UINT GetBase() const { return (UINT)::SendMessage(m_hWnd, UDM_GETBASE, 0, 0); }
  CWnd* SetBuddy(CWnd* pWndBuddy) {
    return CWnd::FromHandle((HWND)::SendMessage(m_hWnd, UDM_SETBUDDY, (WPARAM)pWndBuddy->GetSafeHwnd(), 0));
  }
  CWnd* GetBuddy() const { return CWnd::FromHandle((HWND)::SendMessage(m_hWnd, UDM_GETBUDDY, 0, 0)); }
  int SetPos(int nPos) { return (int)(short)::SendMessage(m_hWnd, UDM_SETPOS, 0, MAKELPARAM(nPos, 0)); }
  int GetPos() const { return (int)::SendMessage(m_hWnd, UDM_GETPOS, 0, 0); }
  void SetRange(short nLower, short nUpper) {
    ::SendMessage(m_hWnd, UDM_SETRANGE, 0, MAKELPARAM(nUpper, nLower));
  }
  void SetRange32(int nLower, int nUpper) { ::SendMessage(m_hWnd, UDM_SETRANGE32, nLower, nUpper); }
  DWORD GetRange() const { return (DWORD)::SendMessage(m_hWnd, UDM_GETRANGE, 0, 0); }
  void GetRange(int& lower, int& upper) const {
    DWORD dw = GetRange();
    lower = (short)HIWORD(dw);
    upper = (short)LOWORD(dw);
  }
  void GetRange32(int& lower, int& upper) const {
    ::SendMessage(m_hWnd, UDM_GETRANGE32, (WPARAM)&lower, (LPARAM)&upper);
  }
};

class CHotKeyCtrl : public CWnd {
  DECLARE_DYNAMIC(CHotKeyCtrl)
public:
  CHotKeyCtrl() {}
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);
  void SetHotKey(WORD wVirtualKeyCode, WORD wModifiers) {
    ::SendMessage(m_hWnd, HKM_SETHOTKEY, MAKEWORD(wVirtualKeyCode, wModifiers), 0);
  }
  DWORD GetHotKey() const { return (DWORD)::SendMessage(m_hWnd, HKM_GETHOTKEY, 0, 0); }
  void GetHotKey(WORD& wVirtualKeyCode, WORD& wModifiers) const {
    DWORD dw = GetHotKey();
    wVirtualKeyCode = LOBYTE(LOWORD(dw));
    wModifiers = HIBYTE(LOWORD(dw));
  }
  void SetRules(WORD wInvalidComb, WORD wModifiers) {
    ::SendMessage(m_hWnd, HKM_SETRULES, wInvalidComb, MAKELPARAM(wModifiers, 0));
  }
};

class CAnimateCtrl : public CWnd {
  DECLARE_DYNAMIC(CAnimateCtrl)
public:
  CAnimateCtrl() {}
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);
  BOOL Open(LPCTSTR lpszFileName) { return (BOOL)::SendMessage(m_hWnd, ACM_OPEN, 0, (LPARAM)lpszFileName); }
  BOOL Open(UINT nID) { return (BOOL)::SendMessage(m_hWnd, ACM_OPEN, 0, (LPARAM)MAKEINTRESOURCE(nID)); }
  BOOL Play(UINT nFrom, UINT nTo, UINT nRep) {
    return (BOOL)::SendMessage(m_hWnd, ACM_PLAY, nRep, MAKELPARAM(nFrom, nTo));
  }
  BOOL Stop() { return (BOOL)::SendMessage(m_hWnd, ACM_STOP, 0, 0); }
  BOOL Close() { return (BOOL)::SendMessage(m_hWnd, ACM_OPEN, 0, 0); }
  BOOL Seek(UINT nTo) { return Play(nTo, nTo, 1); }
};

// ---------------------------------------------------------------------
// Header, list view and tree view

class CHeaderCtrl : public CWnd {
  DECLARE_DYNAMIC(CHeaderCtrl)
public:
  CHeaderCtrl() {}
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);
  int GetItemCount() const { return (int)::SendMessage(m_hWnd, HDM_GETITEMCOUNT, 0, 0); }
  BOOL GetItem(int nPos, HDITEM* pHeaderItem) const {
    return (BOOL)::SendMessage(m_hWnd, HDM_GETITEM, nPos, (LPARAM)pHeaderItem);
  }
  BOOL SetItem(int nPos, HDITEM* pHeaderItem) {
    return (BOOL)::SendMessage(m_hWnd, HDM_SETITEM, nPos, (LPARAM)pHeaderItem);
  }
  int InsertItem(int nPos, HDITEM* phdi) {
    return (int)::SendMessage(m_hWnd, HDM_INSERTITEM, nPos, (LPARAM)phdi);
  }
  BOOL DeleteItem(int nPos) { return (BOOL)::SendMessage(m_hWnd, HDM_DELETEITEM, nPos, 0); }
  BOOL Layout(HDLAYOUT* pHeaderLayout) {
    return (BOOL)::SendMessage(m_hWnd, HDM_LAYOUT, 0, (LPARAM)pHeaderLayout);
  }
  BOOL GetItemRect(int nIndex, LPRECT lpRect) const {
    return (BOOL)::SendMessage(m_hWnd, HDM_GETITEMRECT, nIndex, (LPARAM)lpRect);
  }
  CImageList* SetImageList(CImageList* pImageList);
  CImageList* GetImageList() const;
  virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
  virtual BOOL OnChildNotify(UINT, WPARAM, LPARAM, LRESULT*);
};

class CListCtrl : public CWnd {
  DECLARE_DYNAMIC(CListCtrl)
public:
  CListCtrl() {}
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);
  virtual BOOL CreateEx(DWORD dwExStyle, DWORD dwStyle, const RECT& rect, CWnd* pParentWnd,
                        UINT nID);

  COLORREF GetBkColor() const { return (COLORREF)::SendMessage(m_hWnd, LVM_GETBKCOLOR, 0, 0); }
  BOOL SetBkColor(COLORREF cr) { return (BOOL)::SendMessage(m_hWnd, LVM_SETBKCOLOR, 0, cr); }
  COLORREF GetTextColor() const { return (COLORREF)::SendMessage(m_hWnd, LVM_GETTEXTCOLOR, 0, 0); }
  BOOL SetTextColor(COLORREF cr) { return (BOOL)::SendMessage(m_hWnd, LVM_SETTEXTCOLOR, 0, cr); }
  COLORREF GetTextBkColor() const { return (COLORREF)::SendMessage(m_hWnd, LVM_GETTEXTBKCOLOR, 0, 0); }
  BOOL SetTextBkColor(COLORREF cr) { return (BOOL)::SendMessage(m_hWnd, LVM_SETTEXTBKCOLOR, 0, cr); }
  CImageList* GetImageList(int nImageList) const;
  CImageList* SetImageList(CImageList* pImageList, int nImageListType);
  int GetItemCount() const { return (int)::SendMessage(m_hWnd, LVM_GETITEMCOUNT, 0, 0); }
  BOOL GetItem(LVITEM* pItem) const { return (BOOL)::SendMessage(m_hWnd, LVM_GETITEM, 0, (LPARAM)pItem); }
  BOOL SetItem(const LVITEM* pItem) { return (BOOL)::SendMessage(m_hWnd, LVM_SETITEM, 0, (LPARAM)pItem); }
  BOOL SetItem(int nItem, int nSubItem, UINT nMask, LPCTSTR lpszItem, int nImage, UINT nState,
               UINT nStateMask, LPARAM lParam);
  UINT GetCallbackMask() const { return (UINT)::SendMessage(m_hWnd, LVM_GETCALLBACKMASK, 0, 0); }
  BOOL SetCallbackMask(UINT nMask) { return (BOOL)::SendMessage(m_hWnd, LVM_SETCALLBACKMASK, nMask, 0); }
  int GetNextItem(int nItem, int nFlags) const {
    return (int)::SendMessage(m_hWnd, LVM_GETNEXTITEM, nItem, MAKELPARAM(nFlags, 0));
  }
  POSITION GetFirstSelectedItemPosition() const {
    return (POSITION)(DWORD_PTR)(1 + GetNextItem(-1, LVNI_SELECTED));
  }
  int GetNextSelectedItem(POSITION& pos) const {
    int nOld = (int)(DWORD_PTR)pos - 1;
    pos = (POSITION)(DWORD_PTR)(1 + GetNextItem(nOld, LVNI_SELECTED));
    return nOld;
  }
  BOOL GetItemRect(int nItem, LPRECT lpRect, UINT nCode) const {
    if (lpRect != NULL) lpRect->left = nCode;
    return (BOOL)::SendMessage(m_hWnd, LVM_GETITEMRECT, nItem, (LPARAM)lpRect);
  }
  BOOL SetItemPosition(int nItem, POINT pt) {
    return (BOOL)::SendMessage(m_hWnd, LVM_SETITEMPOSITION32, nItem, (LPARAM)&pt);
  }
  BOOL GetItemPosition(int nItem, LPPOINT lpPoint) const {
    return (BOOL)::SendMessage(m_hWnd, LVM_GETITEMPOSITION, nItem, (LPARAM)lpPoint);
  }
  int GetStringWidth(LPCTSTR lpsz) const {
    return (int)::SendMessage(m_hWnd, LVM_GETSTRINGWIDTH, 0, (LPARAM)lpsz);
  }
  CEdit* GetEditControl() const {
    return (CEdit*)CWnd::FromHandle((HWND)::SendMessage(m_hWnd, LVM_GETEDITCONTROL, 0, 0));
  }
  BOOL GetColumn(int nCol, LVCOLUMN* pColumn) const {
    return (BOOL)::SendMessage(m_hWnd, LVM_GETCOLUMN, nCol, (LPARAM)pColumn);
  }
  BOOL SetColumn(int nCol, const LVCOLUMN* pColumn) {
    return (BOOL)::SendMessage(m_hWnd, LVM_SETCOLUMN, nCol, (LPARAM)pColumn);
  }
  int GetColumnWidth(int nCol) const { return (int)::SendMessage(m_hWnd, LVM_GETCOLUMNWIDTH, nCol, 0); }
  BOOL SetColumnWidth(int nCol, int cx) {
    return (BOOL)::SendMessage(m_hWnd, LVM_SETCOLUMNWIDTH, nCol, MAKELPARAM(cx, 0));
  }
  BOOL GetViewRect(LPRECT lpRect) const {
    return (BOOL)::SendMessage(m_hWnd, LVM_GETVIEWRECT, 0, (LPARAM)lpRect);
  }
  int GetTopIndex() const { return (int)::SendMessage(m_hWnd, LVM_GETTOPINDEX, 0, 0); }
  int GetCountPerPage() const { return (int)::SendMessage(m_hWnd, LVM_GETCOUNTPERPAGE, 0, 0); }
  BOOL GetOrigin(LPPOINT lpPoint) const {
    return (BOOL)::SendMessage(m_hWnd, LVM_GETORIGIN, 0, (LPARAM)lpPoint);
  }
  BOOL SetItemState(int nItem, LVITEM* pItem) {
    return (BOOL)::SendMessage(m_hWnd, LVM_SETITEMSTATE, nItem, (LPARAM)pItem);
  }
  BOOL SetItemState(int nItem, UINT nState, UINT nMask);
  UINT GetItemState(int nItem, UINT nMask) const {
    return (UINT)::SendMessage(m_hWnd, LVM_GETITEMSTATE, nItem, nMask);
  }
  CString GetItemText(int nItem, int nSubItem) const;
  int GetItemText(int nItem, int nSubItem, LPTSTR lpszText, int nLen) const;
  BOOL SetItemText(int nItem, int nSubItem, LPCTSTR lpszText);
  void SetItemCount(int nItems) { ::SendMessage(m_hWnd, LVM_SETITEMCOUNT, nItems, 0); }
  void SetItemCountEx(int iCount, DWORD dwFlags = LVSICF_NOINVALIDATEALL) {
    ::SendMessage(m_hWnd, LVM_SETITEMCOUNT, iCount, dwFlags);
  }
  BOOL SetItemData(int nItem, DWORD_PTR dwData);
  DWORD_PTR GetItemData(int nItem) const;
  UINT GetSelectedCount() const { return (UINT)::SendMessage(m_hWnd, LVM_GETSELECTEDCOUNT, 0, 0); }
  int GetSelectionMark() const { return (int)::SendMessage(m_hWnd, LVM_GETSELECTIONMARK, 0, 0); }
  int SetSelectionMark(int iIndex) { return (int)::SendMessage(m_hWnd, LVM_SETSELECTIONMARK, 0, iIndex); }
  DWORD GetExtendedStyle() const {
    return (DWORD)::SendMessage(m_hWnd, LVM_GETEXTENDEDLISTVIEWSTYLE, 0, 0);
  }
  DWORD SetExtendedStyle(DWORD dwNewStyle) {
    return (DWORD)::SendMessage(m_hWnd, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, dwNewStyle);
  }
  BOOL GetCheck(int nItem) const {
    return ((GetItemState(nItem, LVIS_STATEIMAGEMASK) >> 12) - 1) != 0;
  }
  BOOL SetCheck(int nItem, BOOL fCheck = TRUE) {
    return SetItemState(nItem, INDEXTOSTATEIMAGEMASK(fCheck ? 2 : 1), LVIS_STATEIMAGEMASK);
  }
  CHeaderCtrl* GetHeaderCtrl() const {
    return (CHeaderCtrl*)CWnd::FromHandle((HWND)::SendMessage(m_hWnd, LVM_GETHEADER, 0, 0));
  }
  BOOL GetSubItemRect(int iItem, int iSubItem, int nArea, CRect& ref) const;
  int SubItemHitTest(LPLVHITTESTINFO pInfo) {
    return (int)::SendMessage(m_hWnd, LVM_SUBITEMHITTEST, 0, (LPARAM)pInfo);
  }

  int InsertItem(const LVITEM* pItem) {
    return (int)::SendMessage(m_hWnd, LVM_INSERTITEM, 0, (LPARAM)pItem);
  }
  int InsertItem(int nItem, LPCTSTR lpszItem);
  int InsertItem(int nItem, LPCTSTR lpszItem, int nImage);
  int InsertItem(UINT nMask, int nItem, LPCTSTR lpszItem, UINT nState, UINT nStateMask, int nImage,
                 LPARAM lParam);
  BOOL DeleteItem(int nItem) { return (BOOL)::SendMessage(m_hWnd, LVM_DELETEITEM, nItem, 0); }
  BOOL DeleteAllItems() { return (BOOL)::SendMessage(m_hWnd, LVM_DELETEALLITEMS, 0, 0); }
  int FindItem(LVFINDINFO* pFindInfo, int nStart = -1) const {
    return (int)::SendMessage(m_hWnd, LVM_FINDITEM, nStart, (LPARAM)pFindInfo);
  }
  int HitTest(LVHITTESTINFO* pHitTestInfo) const {
    return (int)::SendMessage(m_hWnd, LVM_HITTEST, 0, (LPARAM)pHitTestInfo);
  }
  int HitTest(CPoint pt, UINT* pFlags = NULL) const;
  BOOL EnsureVisible(int nItem, BOOL bPartialOK) {
    return (BOOL)::SendMessage(m_hWnd, LVM_ENSUREVISIBLE, nItem, MAKELPARAM(bPartialOK, 0));
  }
  BOOL Scroll(CSize size) { return (BOOL)::SendMessage(m_hWnd, LVM_SCROLL, size.cx, size.cy); }
  BOOL RedrawItems(int nFirst, int nLast) {
    return (BOOL)::SendMessage(m_hWnd, LVM_REDRAWITEMS, nFirst, nLast);
  }
  BOOL Arrange(UINT nCode) { return (BOOL)::SendMessage(m_hWnd, LVM_ARRANGE, nCode, 0); }
  CEdit* EditLabel(int nItem) {
    return (CEdit*)CWnd::FromHandle((HWND)::SendMessage(m_hWnd, LVM_EDITLABEL, nItem, 0));
  }
  int InsertColumn(int nCol, const LVCOLUMN* pColumn) {
    return (int)::SendMessage(m_hWnd, LVM_INSERTCOLUMN, nCol, (LPARAM)pColumn);
  }
  int InsertColumn(int nCol, LPCTSTR lpszColumnHeading, int nFormat = LVCFMT_LEFT, int nWidth = -1,
                   int nSubItem = -1);
  BOOL DeleteColumn(int nCol) { return (BOOL)::SendMessage(m_hWnd, LVM_DELETECOLUMN, nCol, 0); }
  CImageList* CreateDragImage(int nItem, LPPOINT lpPoint);
  BOOL Update(int nItem) { return (BOOL)::SendMessage(m_hWnd, LVM_UPDATE, nItem, 0); }
  BOOL SortItems(PFNLVCOMPARE pfnCompare, DWORD_PTR dwData) {
    return (BOOL)::SendMessage(m_hWnd, LVM_SORTITEMS, dwData, (LPARAM)pfnCompare);
  }
  virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
  virtual BOOL OnChildNotify(UINT, WPARAM, LPARAM, LRESULT*);
};

class CTreeCtrl : public CWnd {
  DECLARE_DYNAMIC(CTreeCtrl)
public:
  CTreeCtrl() {}
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);
  virtual BOOL CreateEx(DWORD dwExStyle, DWORD dwStyle, const RECT& rect, CWnd* pParentWnd,
                        UINT nID);

  BOOL GetItemRect(HTREEITEM hItem, LPRECT lpRect, BOOL bTextOnly) const {
    *(HTREEITEM*)lpRect = hItem;
    return (BOOL)::SendMessage(m_hWnd, TVM_GETITEMRECT, bTextOnly, (LPARAM)lpRect);
  }
  UINT GetCount() const { return (UINT)::SendMessage(m_hWnd, TVM_GETCOUNT, 0, 0); }
  UINT GetIndent() const { return (UINT)::SendMessage(m_hWnd, TVM_GETINDENT, 0, 0); }
  void SetIndent(UINT nIndent) { ::SendMessage(m_hWnd, TVM_SETINDENT, nIndent, 0); }
  CImageList* GetImageList(int nImageList) const;
  CImageList* SetImageList(CImageList* pImageList, int nImageListType);
  HTREEITEM GetNextItem(HTREEITEM hItem, UINT nCode) const {
    return (HTREEITEM)::SendMessage(m_hWnd, TVM_GETNEXTITEM, nCode, (LPARAM)hItem);
  }
  HTREEITEM GetChildItem(HTREEITEM hItem) const { return GetNextItem(hItem, TVGN_CHILD); }
  HTREEITEM GetNextSiblingItem(HTREEITEM hItem) const { return GetNextItem(hItem, TVGN_NEXT); }
  HTREEITEM GetPrevSiblingItem(HTREEITEM hItem) const { return GetNextItem(hItem, TVGN_PREVIOUS); }
  HTREEITEM GetParentItem(HTREEITEM hItem) const { return GetNextItem(hItem, TVGN_PARENT); }
  HTREEITEM GetFirstVisibleItem() const { return GetNextItem(NULL, TVGN_FIRSTVISIBLE); }
  HTREEITEM GetNextVisibleItem(HTREEITEM hItem) const { return GetNextItem(hItem, TVGN_NEXTVISIBLE); }
  HTREEITEM GetPrevVisibleItem(HTREEITEM hItem) const { return GetNextItem(hItem, TVGN_PREVIOUSVISIBLE); }
  HTREEITEM GetSelectedItem() const { return GetNextItem(NULL, TVGN_CARET); }
  HTREEITEM GetDropHilightItem() const { return GetNextItem(NULL, TVGN_DROPHILITE); }
  HTREEITEM GetRootItem() const { return GetNextItem(NULL, TVGN_ROOT); }
  BOOL GetItem(TVITEM* pItem) const { return (BOOL)::SendMessage(m_hWnd, TVM_GETITEM, 0, (LPARAM)pItem); }
  BOOL SetItem(TVITEM* pItem) { return (BOOL)::SendMessage(m_hWnd, TVM_SETITEM, 0, (LPARAM)pItem); }
  BOOL SetItem(HTREEITEM hItem, UINT nMask, LPCTSTR lpszItem, int nImage, int nSelectedImage,
               UINT nState, UINT nStateMask, LPARAM lParam);
  CString GetItemText(HTREEITEM hItem) const;
  BOOL GetItemImage(HTREEITEM hItem, int& nImage, int& nSelectedImage) const;
  UINT GetItemState(HTREEITEM hItem, UINT nStateMask) const {
    return (UINT)::SendMessage(m_hWnd, TVM_GETITEMSTATE, (WPARAM)hItem, nStateMask);
  }
  DWORD_PTR GetItemData(HTREEITEM hItem) const;
  BOOL SetItemText(HTREEITEM hItem, LPCTSTR lpszItem) {
    return SetItem(hItem, TVIF_TEXT, lpszItem, 0, 0, 0, 0, 0);
  }
  BOOL SetItemImage(HTREEITEM hItem, int nImage, int nSelectedImage) {
    return SetItem(hItem, TVIF_IMAGE | TVIF_SELECTEDIMAGE, NULL, nImage, nSelectedImage, 0, 0, 0);
  }
  BOOL SetItemState(HTREEITEM hItem, UINT nState, UINT nStateMask) {
    return SetItem(hItem, TVIF_STATE, NULL, 0, 0, nState, nStateMask, 0);
  }
  BOOL SetItemData(HTREEITEM hItem, DWORD_PTR dwData) {
    return SetItem(hItem, TVIF_PARAM, NULL, 0, 0, 0, 0, (LPARAM)dwData);
  }
  BOOL ItemHasChildren(HTREEITEM hItem) const;
  CEdit* GetEditControl() const {
    return (CEdit*)CWnd::FromHandle((HWND)::SendMessage(m_hWnd, TVM_GETEDITCONTROL, 0, 0));
  }
  UINT GetVisibleCount() const { return (UINT)::SendMessage(m_hWnd, TVM_GETVISIBLECOUNT, 0, 0); }
  BOOL GetCheck(HTREEITEM hItem) const {
    return ((GetItemState(hItem, TVIS_STATEIMAGEMASK) >> 12) - 1) != 0;
  }
  BOOL SetCheck(HTREEITEM hItem, BOOL fCheck = TRUE) {
    return SetItemState(hItem, INDEXTOSTATEIMAGEMASK(fCheck ? 2 : 1), TVIS_STATEIMAGEMASK);
  }
  COLORREF SetBkColor(COLORREF clr) { return (COLORREF)::SendMessage(m_hWnd, TVM_SETBKCOLOR, 0, clr); }
  COLORREF SetTextColor(COLORREF clr) {
    return (COLORREF)::SendMessage(m_hWnd, TVM_SETTEXTCOLOR, 0, clr);
  }

  HTREEITEM InsertItem(LPTVINSERTSTRUCT lpInsertStruct) {
    return (HTREEITEM)::SendMessage(m_hWnd, TVM_INSERTITEM, 0, (LPARAM)lpInsertStruct);
  }
  HTREEITEM InsertItem(UINT nMask, LPCTSTR lpszItem, int nImage, int nSelectedImage, UINT nState,
                       UINT nStateMask, LPARAM lParam, HTREEITEM hParent, HTREEITEM hInsertAfter);
  HTREEITEM InsertItem(LPCTSTR lpszItem, HTREEITEM hParent = TVI_ROOT,
                       HTREEITEM hInsertAfter = TVI_LAST);
  HTREEITEM InsertItem(LPCTSTR lpszItem, int nImage, int nSelectedImage,
                       HTREEITEM hParent = TVI_ROOT, HTREEITEM hInsertAfter = TVI_LAST);
  BOOL DeleteItem(HTREEITEM hItem) { return (BOOL)::SendMessage(m_hWnd, TVM_DELETEITEM, 0, (LPARAM)hItem); }
  BOOL DeleteAllItems() { return (BOOL)::SendMessage(m_hWnd, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT); }
  BOOL Expand(HTREEITEM hItem, UINT nCode) {
    return (BOOL)::SendMessage(m_hWnd, TVM_EXPAND, nCode, (LPARAM)hItem);
  }
  BOOL Select(HTREEITEM hItem, UINT nCode) {
    return (BOOL)::SendMessage(m_hWnd, TVM_SELECTITEM, nCode, (LPARAM)hItem);
  }
  BOOL SelectItem(HTREEITEM hItem) { return Select(hItem, TVGN_CARET); }
  BOOL SelectDropTarget(HTREEITEM hItem) { return Select(hItem, TVGN_DROPHILITE); }
  BOOL SelectSetFirstVisible(HTREEITEM hItem) { return Select(hItem, TVGN_FIRSTVISIBLE); }
  CEdit* EditLabel(HTREEITEM hItem) {
    return (CEdit*)CWnd::FromHandle((HWND)::SendMessage(m_hWnd, TVM_EDITLABEL, 0, (LPARAM)hItem));
  }
  HTREEITEM HitTest(CPoint pt, UINT* pFlags = NULL) const;
  HTREEITEM HitTest(TVHITTESTINFO* pHitTestInfo) const {
    return (HTREEITEM)::SendMessage(m_hWnd, TVM_HITTEST, 0, (LPARAM)pHitTestInfo);
  }
  CImageList* CreateDragImage(HTREEITEM hItem);
  BOOL SortChildren(HTREEITEM hItem) {
    return (BOOL)::SendMessage(m_hWnd, TVM_SORTCHILDREN, 0, (LPARAM)hItem);
  }
  BOOL EnsureVisible(HTREEITEM hItem) {
    return (BOOL)::SendMessage(m_hWnd, TVM_ENSUREVISIBLE, 0, (LPARAM)hItem);
  }
  BOOL SortChildrenCB(LPTVSORTCB pSort) {
    return (BOOL)::SendMessage(m_hWnd, TVM_SORTCHILDRENCB, 0, (LPARAM)pSort);
  }
};

// ---------------------------------------------------------------------
// Tab control

class CTabCtrl : public CWnd {
  DECLARE_DYNAMIC(CTabCtrl)
public:
  CTabCtrl() {}
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);
  CImageList* GetImageList() const;
  CImageList* SetImageList(CImageList* pImageList);
  int GetItemCount() const { return (int)::SendMessage(m_hWnd, TCM_GETITEMCOUNT, 0, 0); }
  BOOL GetItem(int nItem, TCITEM* pTabCtrlItem) const {
    return (BOOL)::SendMessage(m_hWnd, TCM_GETITEM, nItem, (LPARAM)pTabCtrlItem);
  }
  BOOL SetItem(int nItem, TCITEM* pTabCtrlItem) {
    return (BOOL)::SendMessage(m_hWnd, TCM_SETITEM, nItem, (LPARAM)pTabCtrlItem);
  }
  BOOL GetItemRect(int nItem, LPRECT lpRect) const {
    return (BOOL)::SendMessage(m_hWnd, TCM_GETITEMRECT, nItem, (LPARAM)lpRect);
  }
  int GetCurSel() const { return (int)::SendMessage(m_hWnd, TCM_GETCURSEL, 0, 0); }
  int SetCurSel(int nItem) { return (int)::SendMessage(m_hWnd, TCM_SETCURSEL, nItem, 0); }
  void SetCurFocus(int nItem) { ::SendMessage(m_hWnd, TCM_SETCURFOCUS, nItem, 0); }
  CSize SetItemSize(CSize size) {
    DWORD dw = (DWORD)::SendMessage(m_hWnd, TCM_SETITEMSIZE, 0, MAKELPARAM(size.cx, size.cy));
    return CSize(LOWORD(dw), HIWORD(dw));
  }
  void SetPadding(CSize size) { ::SendMessage(m_hWnd, TCM_SETPADDING, 0, MAKELPARAM(size.cx, size.cy)); }
  int GetRowCount() const { return (int)::SendMessage(m_hWnd, TCM_GETROWCOUNT, 0, 0); }
  CToolTipCtrl* GetToolTips() const;
  void SetToolTips(CToolTipCtrl* pWndTip);
  int GetCurFocus() const { return (int)::SendMessage(m_hWnd, TCM_GETCURFOCUS, 0, 0); }
  LONG InsertItem(int nItem, TCITEM* pTabCtrlItem) {
    return (LONG)::SendMessage(m_hWnd, TCM_INSERTITEM, nItem, (LPARAM)pTabCtrlItem);
  }
  LONG InsertItem(int nItem, LPCTSTR lpszItem);
  LONG InsertItem(int nItem, LPCTSTR lpszItem, int nImage);
  BOOL DeleteItem(int nItem) { return (BOOL)::SendMessage(m_hWnd, TCM_DELETEITEM, nItem, 0); }
  BOOL DeleteAllItems() { return (BOOL)::SendMessage(m_hWnd, TCM_DELETEALLITEMS, 0, 0); }
  void AdjustRect(BOOL bLarger, LPRECT lpRect) {
    ::SendMessage(m_hWnd, TCM_ADJUSTRECT, bLarger, (LPARAM)lpRect);
  }
  void RemoveImage(int nImage) { ::SendMessage(m_hWnd, TCM_REMOVEIMAGE, nImage, 0); }
  int HitTest(TCHITTESTINFO* pHitTestInfo) const {
    return (int)::SendMessage(m_hWnd, TCM_HITTEST, 0, (LPARAM)pHitTestInfo);
  }
  void DeselectAll(BOOL fExcludeFocus) { ::SendMessage(m_hWnd, TCM_DESELECTALL, fExcludeFocus, 0); }
  virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
  virtual BOOL OnChildNotify(UINT, WPARAM, LPARAM, LRESULT*);
};

// ---------------------------------------------------------------------
// Rich edit

class CRichEditCtrl : public CWnd {
  DECLARE_DYNAMIC(CRichEditCtrl)
public:
  CRichEditCtrl() {}
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);

  BOOL CanUndo() const { return (BOOL)::SendMessage(m_hWnd, EM_CANUNDO, 0, 0); }
  int GetLineCount() const { return (int)::SendMessage(m_hWnd, EM_GETLINECOUNT, 0, 0); }
  BOOL GetModify() const { return (BOOL)::SendMessage(m_hWnd, EM_GETMODIFY, 0, 0); }
  void SetModify(BOOL bModified = TRUE) { ::SendMessage(m_hWnd, EM_SETMODIFY, bModified, 0); }
  void GetRect(LPRECT lpRect) const { ::SendMessage(m_hWnd, EM_GETRECT, 0, (LPARAM)lpRect); }
  void SetRect(LPCRECT lpRect) { ::SendMessage(m_hWnd, EM_SETRECT, 0, (LPARAM)lpRect); }
  int GetLine(int nIndex, LPTSTR lpszBuffer, int nMaxLength) const;
  int GetFirstVisibleLine() const { return (int)::SendMessage(m_hWnd, EM_GETFIRSTVISIBLELINE, 0, 0); }
  void LimitText(long nChars = 0) { ::SendMessage(m_hWnd, EM_EXLIMITTEXT, 0, nChars); }
  long GetLimitText() const { return (long)::SendMessage(m_hWnd, EM_GETLIMITTEXT, 0, 0); }
  long LineFromChar(long nIndex) const { return (long)::SendMessage(m_hWnd, EM_EXLINEFROMCHAR, 0, nIndex); }
  int LineIndex(int nLine = -1) const { return (int)::SendMessage(m_hWnd, EM_LINEINDEX, nLine, 0); }
  int LineLength(int nLine = -1) const { return (int)::SendMessage(m_hWnd, EM_LINELENGTH, nLine, 0); }
  void LineScroll(int nLines, int nChars = 0) { ::SendMessage(m_hWnd, EM_LINESCROLL, nChars, nLines); }
  void GetSel(CHARRANGE& cr) const { ::SendMessage(m_hWnd, EM_EXGETSEL, 0, (LPARAM)&cr); }
  void GetSel(long& nStartChar, long& nEndChar) const {
    CHARRANGE cr;
    GetSel(cr);
    nStartChar = cr.cpMin;
    nEndChar = cr.cpMax;
  }
  void SetSel(CHARRANGE& cr) { ::SendMessage(m_hWnd, EM_EXSETSEL, 0, (LPARAM)&cr); }
  void SetSel(long nStartChar, long nEndChar) {
    CHARRANGE cr = {(LONG)nStartChar, (LONG)nEndChar};
    SetSel(cr);
  }
  CString GetSelText() const;
  long GetTextLength() const { return (long)::SendMessage(m_hWnd, WM_GETTEXTLENGTH, 0, 0); }
  WORD GetSelectionType() const { return (WORD)::SendMessage(m_hWnd, EM_SELECTIONTYPE, 0, 0); }
  COLORREF SetBackgroundColor(BOOL bSysColor, COLORREF cr) {
    return (COLORREF)::SendMessage(m_hWnd, EM_SETBKGNDCOLOR, bSysColor, cr);
  }
  DWORD GetDefaultCharFormat(CHARFORMAT& cf) const {
    cf.cbSize = sizeof(cf);
    return (DWORD)::SendMessage(m_hWnd, EM_GETCHARFORMAT, 0, (LPARAM)&cf);
  }
  DWORD GetSelectionCharFormat(CHARFORMAT& cf) const {
    cf.cbSize = sizeof(cf);
    return (DWORD)::SendMessage(m_hWnd, EM_GETCHARFORMAT, 1, (LPARAM)&cf);
  }
  BOOL SetDefaultCharFormat(CHARFORMAT& cf) {
    cf.cbSize = sizeof(cf);
    return (BOOL)::SendMessage(m_hWnd, EM_SETCHARFORMAT, 0, (LPARAM)&cf);
  }
  BOOL SetSelectionCharFormat(CHARFORMAT& cf) {
    cf.cbSize = sizeof(cf);
    return (BOOL)::SendMessage(m_hWnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
  }
  BOOL SetWordCharFormat(CHARFORMAT& cf) {
    cf.cbSize = sizeof(cf);
    return (BOOL)::SendMessage(m_hWnd, EM_SETCHARFORMAT, SCF_SELECTION | SCF_WORD, (LPARAM)&cf);
  }
  DWORD GetParaFormat(PARAFORMAT& pf) const {
    pf.cbSize = sizeof(pf);
    return (DWORD)::SendMessage(m_hWnd, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
  }
  BOOL SetParaFormat(PARAFORMAT& pf) {
    pf.cbSize = sizeof(pf);
    return (BOOL)::SendMessage(m_hWnd, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
  }
  long GetEventMask() const { return (long)::SendMessage(m_hWnd, EM_GETEVENTMASK, 0, 0); }
  DWORD SetEventMask(DWORD dwEventMask) {
    return (DWORD)::SendMessage(m_hWnd, EM_SETEVENTMASK, 0, dwEventMask);
  }
  BOOL SetReadOnly(BOOL bReadOnly = TRUE) { return (BOOL)::SendMessage(m_hWnd, EM_SETREADONLY, bReadOnly, 0); }
  BOOL SetOLECallback(void* pCallback) {
    return (BOOL)::SendMessage(m_hWnd, EM_SETOLECALLBACK, 0, (LPARAM)pCallback);
  }
  // The control's IRichEditOle, AddRef'd; the caller releases it.
  struct IRichEditOle* GetIRichEditOle() const {
    struct IRichEditOle* p = NULL;
    ::SendMessage(m_hWnd, EM_GETOLEINTERFACE, 0, (LPARAM)&p);
    return p;
  }
  void EmptyUndoBuffer() { ::SendMessage(m_hWnd, EM_EMPTYUNDOBUFFER, 0, 0); }
  void ReplaceSel(LPCTSTR lpszNewText, BOOL bCanUndo = FALSE) {
    ::SendMessage(m_hWnd, EM_REPLACESEL, bCanUndo, (LPARAM)lpszNewText);
  }
  void HideSelection(BOOL bHide, BOOL bPerm) { ::SendMessage(m_hWnd, EM_HIDESELECTION, bHide, bPerm); }
  // EM_FINDTEXTEX always takes ANSI text; the W message takes Unicode.
#ifdef UNICODE
  long FindText(DWORD dwFlags, FINDTEXTEX* pFindText) const {
    return (long)::SendMessage(m_hWnd, EM_FINDTEXTEXW, dwFlags, (LPARAM)pFindText);
  }
#else
  long FindText(DWORD dwFlags, FINDTEXTEX* pFindText) const {
    return (long)::SendMessage(m_hWnd, EM_FINDTEXTEX, dwFlags, (LPARAM)pFindText);
  }
#endif
  long GetTextRange(int nFirst, int nLast, CString& refString) const;
  long GetTextLengthEx(DWORD dwFlags = GTL_DEFAULT, UINT uCodePage = (UINT)-1) const {
    GETTEXTLENGTHEX gtl = {dwFlags, uCodePage};
    if (uCodePage == (UINT)-1) gtl.codepage = sizeof(TCHAR) == 2 ? 1200 : CP_ACP;
    return (long)::SendMessage(m_hWnd, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
  }
  UINT GetOptions() const { return (UINT)::SendMessage(m_hWnd, EM_GETOPTIONS, 0, 0); }
  void SetOptions(WORD wOp, DWORD dwFlags) { ::SendMessage(m_hWnd, EM_SETOPTIONS, wOp, dwFlags); }
  BOOL SetAutoURLDetect(BOOL bEnable = TRUE) {
    return !(BOOL)::SendMessage(m_hWnd, EM_AUTOURLDETECT, bEnable, 0);
  }
  DWORD GetDefaultCharFormat(CHARFORMAT2& cf) const {
    cf.cbSize = sizeof(cf);
    return (DWORD)::SendMessage(m_hWnd, EM_GETCHARFORMAT, 0, (LPARAM)&cf);
  }
  DWORD GetSelectionCharFormat(CHARFORMAT2& cf) const {
    cf.cbSize = sizeof(cf);
    return (DWORD)::SendMessage(m_hWnd, EM_GETCHARFORMAT, 1, (LPARAM)&cf);
  }
  BOOL SetDefaultCharFormat(CHARFORMAT2& cf) {
    cf.cbSize = sizeof(cf);
    return (BOOL)::SendMessage(m_hWnd, EM_SETCHARFORMAT, 0, (LPARAM)&cf);
  }
  BOOL SetSelectionCharFormat(CHARFORMAT2& cf) {
    cf.cbSize = sizeof(cf);
    return (BOOL)::SendMessage(m_hWnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
  }
  BOOL SetWordCharFormat(CHARFORMAT2& cf) {
    cf.cbSize = sizeof(cf);
    return (BOOL)::SendMessage(m_hWnd, EM_SETCHARFORMAT, SCF_SELECTION | SCF_WORD, (LPARAM)&cf);
  }
  DWORD GetParaFormat(PARAFORMAT2& pf) const {
    pf.cbSize = sizeof(pf);
    return (DWORD)::SendMessage(m_hWnd, EM_GETPARAFORMAT, 0, (LPARAM)&pf);
  }
  BOOL SetParaFormat(PARAFORMAT2& pf) {
    pf.cbSize = sizeof(pf);
    return (BOOL)::SendMessage(m_hWnd, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
  }
  long FormatRange(FORMATRANGE* pfr, BOOL bDisplay = TRUE) {
    return (long)::SendMessage(m_hWnd, EM_FORMATRANGE, bDisplay, (LPARAM)pfr);
  }
  long StreamIn(int nFormat, EDITSTREAM& es) {
    return (long)::SendMessage(m_hWnd, EM_STREAMIN, nFormat, (LPARAM)&es);
  }
  long StreamOut(int nFormat, EDITSTREAM& es) {
    return (long)::SendMessage(m_hWnd, EM_STREAMOUT, nFormat, (LPARAM)&es);
  }
  BOOL Undo() { return (BOOL)::SendMessage(m_hWnd, EM_UNDO, 0, 0); }
  void Clear() { ::SendMessage(m_hWnd, WM_CLEAR, 0, 0); }
  void Copy() { ::SendMessage(m_hWnd, WM_COPY, 0, 0); }
  void Cut() { ::SendMessage(m_hWnd, WM_CUT, 0, 0); }
  void Paste() { ::SendMessage(m_hWnd, WM_PASTE, 0, 0); }
  // Pastes the clipboard in one format; a metafile gives the icon to show.
  void PasteSpecial(UINT nClipFormat, DWORD dvAspect = 0, HMETAFILE hMF = 0) {
    REPASTESPECIAL reps = {dvAspect, (DWORD_PTR)hMF};
    ::SendMessage(m_hWnd, EM_PASTESPECIAL, nClipFormat, (LPARAM)&reps);
  }
  BOOL CanPaste(UINT nFormat = 0) const { return (BOOL)::SendMessage(m_hWnd, EM_CANPASTE, nFormat, 0); }
  void RequestResize() { ::SendMessage(m_hWnd, EM_REQUESTRESIZE, 0, 0); }
  CPoint GetCharPos(long lChar) const {
    POINTL pt = {0, 0};
    ::SendMessage(m_hWnd, EM_POSFROMCHAR, (WPARAM)&pt, lChar);
    return CPoint(pt.x, pt.y);
  }
  BOOL DisplayBand(LPRECT pDisplayRect) {
    return (BOOL)::SendMessage(m_hWnd, EM_DISPLAYBAND, 0, (LPARAM)pDisplayRect);
  }
  BOOL SetTargetDevice(HDC hDC, long lLineWidth) {
    return (BOOL)::SendMessage(m_hWnd, EM_SETTARGETDEVICE, (WPARAM)hDC, lLineWidth);
  }
};

// ---------------------------------------------------------------------
// Date, time, IP address, extended combo box and rebar

class COleDateTime;

class CDateTimeCtrl : public CWnd {
  DECLARE_DYNAMIC(CDateTimeCtrl)
public:
  CDateTimeCtrl() {}
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);
  DWORD GetTime(LPSYSTEMTIME pTimeDest) const {
    return (DWORD)::SendMessage(m_hWnd, DTM_GETSYSTEMTIME, 0, (LPARAM)pTimeDest);
  }
  BOOL SetTime(LPSYSTEMTIME pTimeNew = NULL) {
    return (BOOL)::SendMessage(m_hWnd, DTM_SETSYSTEMTIME, pTimeNew == NULL ? GDT_NONE : GDT_VALID,
                               (LPARAM)pTimeNew);
  }
  BOOL SetFormat(LPCTSTR pstrFormat) { return (BOOL)::SendMessage(m_hWnd, DTM_SETFORMAT, 0, (LPARAM)pstrFormat); }
  DWORD GetRange(LPSYSTEMTIME pMinRange) const {
    return (DWORD)::SendMessage(m_hWnd, DTM_GETRANGE, 0, (LPARAM)pMinRange);
  }
  BOOL SetRange(DWORD dwFlags, LPSYSTEMTIME pRange) {
    return (BOOL)::SendMessage(m_hWnd, DTM_SETRANGE, dwFlags, (LPARAM)pRange);
  }
  // With dates (afxdisp.h).
  BOOL GetTime(COleDateTime& timeDest) const;
  BOOL SetTime(const COleDateTime& timeNew);
  DWORD GetRange(COleDateTime* pMinRange, COleDateTime* pMaxRange) const;
  BOOL SetRange(const COleDateTime* pMinRange, const COleDateTime* pMaxRange);
};

class CMonthCalCtrl : public CWnd {
  DECLARE_DYNAMIC(CMonthCalCtrl)
public:
  CMonthCalCtrl() {}
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);
  BOOL GetCurSel(LPSYSTEMTIME pDateTime) const {
    return (BOOL)::SendMessage(m_hWnd, MCM_GETCURSEL, 0, (LPARAM)pDateTime);
  }
  BOOL SetCurSel(const LPSYSTEMTIME pDateTime) {
    return (BOOL)::SendMessage(m_hWnd, MCM_SETCURSEL, 0, (LPARAM)pDateTime);
  }
  BOOL GetMinReqRect(RECT* pRect) const {
    return (BOOL)::SendMessage(m_hWnd, MCM_GETMINREQRECT, 0, (LPARAM)pRect);
  }
  // With dates (afxdisp.h).
  BOOL GetCurSel(COleDateTime& refDateTime) const;
  BOOL SetCurSel(const COleDateTime& refDateTime);
  BOOL GetToday(COleDateTime& refDateTime) const;
  void SetToday(const COleDateTime& refDateTime);
};

class CIPAddressCtrl : public CWnd {
  DECLARE_DYNAMIC(CIPAddressCtrl)
public:
  CIPAddressCtrl() {}
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);
  BOOL IsBlank() const { return (BOOL)::SendMessage(m_hWnd, IPM_ISBLANK, 0, 0); }
  void ClearAddress() { ::SendMessage(m_hWnd, IPM_CLEARADDRESS, 0, 0); }
  int GetAddress(BYTE& nField0, BYTE& nField1, BYTE& nField2, BYTE& nField3) const {
    DWORD dwAddress = 0;
    int n = (int)::SendMessage(m_hWnd, IPM_GETADDRESS, 0, (LPARAM)&dwAddress);
    nField0 = (BYTE)FIRST_IPADDRESS(dwAddress);
    nField1 = (BYTE)SECOND_IPADDRESS(dwAddress);
    nField2 = (BYTE)THIRD_IPADDRESS(dwAddress);
    nField3 = (BYTE)FOURTH_IPADDRESS(dwAddress);
    return n;
  }
  int GetAddress(DWORD& dwAddress) const {
    return (int)::SendMessage(m_hWnd, IPM_GETADDRESS, 0, (LPARAM)&dwAddress);
  }
  void SetAddress(DWORD dwAddress) { ::SendMessage(m_hWnd, IPM_SETADDRESS, 0, dwAddress); }
  void SetAddress(BYTE nField0, BYTE nField1, BYTE nField2, BYTE nField3) {
    SetAddress(MAKEIPADDRESS(nField0, nField1, nField2, nField3));
  }
  void SetFieldFocus(WORD nField) { ::SendMessage(m_hWnd, IPM_SETFOCUS, nField, 0); }
  void SetFieldRange(int nField, BYTE nLower, BYTE nUpper) {
    ::SendMessage(m_hWnd, IPM_SETRANGE, nField, MAKEIPRANGE(nLower, nUpper));
  }
};

class CComboBoxEx : public CComboBox {
  DECLARE_DYNAMIC(CComboBoxEx)
public:
  CComboBoxEx() {}
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);
  int DeleteItem(int iIndex) { return (int)::SendMessage(m_hWnd, CBEM_DELETEITEM, iIndex, 0); }
  BOOL GetItem(COMBOBOXEXITEM* pCBItem) {
    return (BOOL)::SendMessage(m_hWnd, CBEM_GETITEM, 0, (LPARAM)pCBItem);
  }
  int InsertItem(const COMBOBOXEXITEM* pCBItem) {
    return (int)::SendMessage(m_hWnd, CBEM_INSERTITEM, 0, (LPARAM)pCBItem);
  }
  BOOL SetItem(const COMBOBOXEXITEM* pCBItem) {
    return (BOOL)::SendMessage(m_hWnd, CBEM_SETITEM, 0, (LPARAM)pCBItem);
  }
  CComboBox* GetComboBoxCtrl() {
    return (CComboBox*)CWnd::FromHandle((HWND)::SendMessage(m_hWnd, CBEM_GETCOMBOCONTROL, 0, 0));
  }
  CEdit* GetEditCtrl() {
    return (CEdit*)CWnd::FromHandle((HWND)::SendMessage(m_hWnd, CBEM_GETEDITCONTROL, 0, 0));
  }
  CImageList* SetImageList(CImageList* pImageList);
  CImageList* GetImageList() const;
};

class CReBarCtrl : public CWnd {
  DECLARE_DYNAMIC(CReBarCtrl)
public:
  CReBarCtrl() {}
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);
  UINT GetBandCount() const { return (UINT)::SendMessage(m_hWnd, RB_GETBANDCOUNT, 0, 0); }
  BOOL GetBandInfo(UINT uBand, REBARBANDINFO* prbbi) const {
    return (BOOL)::SendMessage(m_hWnd, RB_GETBANDINFO, uBand, (LPARAM)prbbi);
  }
  BOOL SetBandInfo(UINT uBand, REBARBANDINFO* prbbi) {
    return (BOOL)::SendMessage(m_hWnd, RB_SETBANDINFO, uBand, (LPARAM)prbbi);
  }
  BOOL InsertBand(UINT uIndex, REBARBANDINFO* prbbi) {
    return (BOOL)::SendMessage(m_hWnd, RB_INSERTBAND, uIndex, (LPARAM)prbbi);
  }
  BOOL DeleteBand(UINT uBand) { return (BOOL)::SendMessage(m_hWnd, RB_DELETEBAND, uBand, 0); }
  UINT GetBarHeight() const { return (UINT)::SendMessage(m_hWnd, RB_GETBARHEIGHT, 0, 0); }
  UINT GetRowCount() const { return (UINT)::SendMessage(m_hWnd, RB_GETROWCOUNT, 0, 0); }
  UINT GetRowHeight(UINT uRow) const {
    return (UINT)::SendMessage(m_hWnd, RB_GETROWHEIGHT, uRow, 0);
  }
  BOOL GetBarInfo(REBARINFO* prbi) const {
    return (BOOL)::SendMessage(m_hWnd, RB_GETBARINFO, 0, (LPARAM)prbi);
  }
  BOOL SetBarInfo(REBARINFO* prbi) {
    return (BOOL)::SendMessage(m_hWnd, RB_SETBARINFO, 0, (LPARAM)prbi);
  }
  COLORREF GetBkColor() const { return (COLORREF)::SendMessage(m_hWnd, RB_GETBKCOLOR, 0, 0); }
  COLORREF SetBkColor(COLORREF clr) {
    return (COLORREF)::SendMessage(m_hWnd, RB_SETBKCOLOR, 0, (LPARAM)clr);
  }
  COLORREF GetTextColor() const {
    return (COLORREF)::SendMessage(m_hWnd, RB_GETTEXTCOLOR, 0, 0);
  }
  COLORREF SetTextColor(COLORREF clr) {
    return (COLORREF)::SendMessage(m_hWnd, RB_SETTEXTCOLOR, 0, (LPARAM)clr);
  }
  // The window that gets the notifications; the previous one.
  CWnd* SetOwner(CWnd* pWnd) {
    return CWnd::FromHandle(
        (HWND)::SendMessage(m_hWnd, RB_SETPARENT, (WPARAM)pWnd->GetSafeHwnd(), 0));
  }
  CToolTipCtrl* GetToolTips() const {
    return (CToolTipCtrl*)CWnd::FromHandle((HWND)::SendMessage(m_hWnd, RB_GETTOOLTIPS, 0, 0));
  }
  void SetToolTips(CToolTipCtrl* pTip) {
    ::SendMessage(m_hWnd, RB_SETTOOLTIPS, (WPARAM)((CWnd*)pTip)->GetSafeHwnd(), 0);
  }
  int IDToIndex(UINT uBandID) const {
    return (int)::SendMessage(m_hWnd, RB_IDTOINDEX, uBandID, 0);
  }
  BOOL GetRect(UINT uBand, LPRECT prc) const {
    return (BOOL)::SendMessage(m_hWnd, RB_GETRECT, uBand, (LPARAM)prc);
  }
  int HitTest(RBHITTESTINFO* prbht) {
    return (int)::SendMessage(m_hWnd, RB_HITTEST, 0, (LPARAM)prbht);
  }
  void GetBandBorders(UINT uBand, LPRECT prc) const {
    ::SendMessage(m_hWnd, RB_GETBANDBORDERS, uBand, (LPARAM)prc);
  }
  CImageList* GetImageList() const;
  BOOL SetImageList(CImageList* pImageList);
  BOOL GetColorScheme(COLORSCHEME* lpcs) {
    lpcs->dwSize = sizeof(COLORSCHEME);
    return (BOOL)::SendMessage(m_hWnd, RB_GETCOLORSCHEME, 0, (LPARAM)lpcs);
  }
  void SetColorScheme(const COLORSCHEME* lpcs) {
    ::SendMessage(m_hWnd, RB_SETCOLORSCHEME, 0, (LPARAM)lpcs);
  }
  CPalette* GetPalette() const {
    return CPalette::FromHandle((HPALETTE)::SendMessage(m_hWnd, RB_GETPALETTE, 0, 0));
  }
  CPalette* SetPalette(HPALETTE hPal) {
    return CPalette::FromHandle((HPALETTE)::SendMessage(m_hWnd, RB_SETPALETTE, 0, (LPARAM)hPal));
  }
  void BeginDrag(UINT uBand, DWORD dwPos = (DWORD)-1) {
    ::SendMessage(m_hWnd, RB_BEGINDRAG, uBand, (LPARAM)dwPos);
  }
  void EndDrag() { ::SendMessage(m_hWnd, RB_ENDDRAG, 0, 0); }
  void DragMove(DWORD dwPos = (DWORD)-1) { ::SendMessage(m_hWnd, RB_DRAGMOVE, 0, (LPARAM)dwPos); }
  void MaximizeBand(UINT uBand) { ::SendMessage(m_hWnd, RB_MAXIMIZEBAND, uBand, 0); }
  // Back to the band's ideal width.
  void RestoreBand(UINT uBand) { ::SendMessage(m_hWnd, RB_MAXIMIZEBAND, uBand, TRUE); }
  void MinimizeBand(UINT uBand) { ::SendMessage(m_hWnd, RB_MINIMIZEBAND, uBand, 0); }
  BOOL ShowBand(UINT uBand, BOOL fShow = TRUE) {
    return (BOOL)::SendMessage(m_hWnd, RB_SHOWBAND, uBand, fShow);
  }
  BOOL MoveBand(UINT uFrom, UINT uTo) {
    return (BOOL)::SendMessage(m_hWnd, RB_MOVEBAND, uFrom, uTo);
  }
  BOOL SizeToRect(CRect& rect) {
    return (BOOL)::SendMessage(m_hWnd, RB_SIZETORECT, 0, (LPARAM)(LPRECT)rect);
  }
  void PushChevron(UINT uBand, LPARAM lAppValue) {
    ::SendMessage(m_hWnd, RB_PUSHCHEVRON, uBand, lAppValue);
  }
  IDropTarget* GetDropTarget() const;
  DWORD GetExtendedStyle() const {
    return (DWORD)::SendMessage(m_hWnd, WM_USER + 42 /* RB_GETEXTENDEDSTYLE */, 0, 0);
  }
  DWORD SetExtendedStyle(DWORD dwMask, DWORD dwStyleEx) {
    return (DWORD)::SendMessage(m_hWnd, WM_USER + 41 /* RB_SETEXTENDEDSTYLE */, dwMask, dwStyleEx);
  }
};

// ---------------------------------------------------------------------
// CLinkCtrl: the SysLink control (comctl32 6, so a program needs a
// manifest that asks for it): text with <a> links.

class CLinkCtrl : public CWnd {
  DECLARE_DYNAMIC(CLinkCtrl)
public:
  CLinkCtrl() {}
  virtual BOOL Create(LPCTSTR lpszLinkMarkup, DWORD dwStyle, const RECT& rect, CWnd* pParentWnd,
                      UINT nID);
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);
  virtual BOOL CreateEx(DWORD dwExStyle, LPCTSTR lpszLinkMarkup, DWORD dwStyle, const RECT& rect,
                        CWnd* pParentWnd, UINT nID);
  virtual BOOL CreateEx(DWORD dwExStyle, DWORD dwStyle, const RECT& rect, CWnd* pParentWnd,
                        UINT nID);

  // The height the text needs at the control's width.
  int GetIdealHeight() const { return (int)::SendMessage(m_hWnd, LM_GETIDEALHEIGHT, 0, 0); }
  BOOL SetItem(PLITEM pItem) { return (BOOL)::SendMessage(m_hWnd, LM_SETITEM, 0, (LPARAM)pItem); }
  BOOL GetItem(PLITEM pItem) const {
    return (BOOL)::SendMessage(m_hWnd, LM_GETITEM, 0, (LPARAM)pItem);
  }
  BOOL SetItemID(int iLink, LPCWSTR szID);
  BOOL SetItemUrl(int iLink, LPCWSTR szUrl);
  BOOL SetItemState(int iLink, UINT state, UINT stateMask = 0xFFFFFFFF);
  BOOL GetItemID(int iLink, CString& strID) const;
  BOOL GetItemID(int iLink, LPWSTR szID, UINT cchID) const;
  BOOL GetItemUrl(int iLink, CString& strUrl) const;
  BOOL GetItemUrl(int iLink, LPWSTR szUrl, UINT cchUrl) const;
  BOOL GetItemState(int iLink, UINT* pnState, UINT stateMask = 0xFFFFFFFF) const;
  BOOL LinkHitTest(PLHITTESTINFO phti) const {
    return (BOOL)::SendMessage(m_hWnd, LM_HITTEST, 0, (LPARAM)phti);
  }
};

// ---------------------------------------------------------------------
// CPagerCtrl: the pager (SysPager), which scrolls a child window too big
// for it.

class CPagerCtrl : public CWnd {
  DECLARE_DYNAMIC(CPagerCtrl)
public:
  CPagerCtrl() {}
  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);
  virtual BOOL CreateEx(DWORD dwExStyle, DWORD dwStyle, const RECT& rect, CWnd* pParentWnd,
                        UINT nID);

  void SetChild(HWND hwndChild) { ::SendMessage(m_hWnd, PGM_SETCHILD, 0, (LPARAM)hwndChild); }
  // Asks the parent for the child's size again (PGN_CALCSIZE).
  void RecalcSize() { ::SendMessage(m_hWnd, PGM_RECALCSIZE, 0, 0); }
  void ForwardMouse(BOOL bForward) { ::SendMessage(m_hWnd, PGM_FORWARDMOUSE, bForward, 0); }
  COLORREF GetBkColor() const { return (COLORREF)::SendMessage(m_hWnd, PGM_GETBKCOLOR, 0, 0); }
  COLORREF SetBkColor(COLORREF clr) {
    return (COLORREF)::SendMessage(m_hWnd, PGM_SETBKCOLOR, 0, (LPARAM)clr);
  }
  int GetBorder() const { return (int)::SendMessage(m_hWnd, PGM_GETBORDER, 0, 0); }
  int SetBorder(int iBorder) { return (int)::SendMessage(m_hWnd, PGM_SETBORDER, 0, iBorder); }
  int GetButtonSize() const { return (int)::SendMessage(m_hWnd, PGM_GETBUTTONSIZE, 0, 0); }
  int SetButtonSize(int iButtonSize) {
    return (int)::SendMessage(m_hWnd, PGM_SETBUTTONSIZE, 0, iButtonSize);
  }
  int GetScrollPos() const { return (int)::SendMessage(m_hWnd, PGM_GETPOS, 0, 0); }
  int SetScrollPos(int iPos) { return (int)::SendMessage(m_hWnd, PGM_SETPOS, 0, iPos); }
  // PGB_TOPORLEFT or PGB_BOTTOMORRIGHT: PGF_INVISIBLE, PGF_NORMAL, ...
  DWORD GetButtonState(int iButton) {
    return (DWORD)::SendMessage(m_hWnd, PGM_GETBUTTONSTATE, 0, iButton);
  }
  BOOL IsButtonInvisible(int iButton) { return GetButtonState(iButton) == PGF_INVISIBLE; }
  BOOL IsButtonNormal(int iButton) { return (GetButtonState(iButton) & PGF_NORMAL) != 0; }
  BOOL IsButtonGrayed(int iButton) { return (GetButtonState(iButton) & PGF_GRAYED) != 0; }
  BOOL IsButtonDepressed(int iButton) { return (GetButtonState(iButton) & PGF_DEPRESSED) != 0; }
  BOOL IsButtonHot(int iButton) { return (GetButtonState(iButton) & PGF_HOT) != 0; }
  void GetDropTarget(IDropTarget** ppDropTarget) {
    ::SendMessage(m_hWnd, PGM_GETDROPTARGET, 0, (LPARAM)ppDropTarget);
  }
};

// ---------------------------------------------------------------------
// CDragListBox: a list box whose items the user drags into a new order
// (MakeDragList). The virtual functions get the cursor in screen
// coordinates.

class CDragListBox : public CListBox {
  DECLARE_DYNAMIC(CDragListBox)
public:
  CDragListBox();
  virtual ~CDragListBox();

  int ItemFromPt(CPoint pt, BOOL bAutoScroll = TRUE) const;
  virtual BOOL BeginDrag(CPoint pt);
  virtual void CancelDrag(CPoint pt);
  // DL_STOPCURSOR, DL_COPYCURSOR or DL_MOVECURSOR.
  virtual UINT Dragging(CPoint pt);
  virtual void Dropped(int nSrcIndex, CPoint pt);
  // The insertion mark before nItem (-1 for none).
  virtual void DrawInsert(int nItem);
  void DrawSingle(int nIndex);

  // ----- Implementation
  virtual void PreSubclassWindow();
  virtual BOOL OnChildNotify(UINT, WPARAM, LPARAM, LRESULT*);
  int m_nLast;
};

#endif  // __AFXCMN_H__
