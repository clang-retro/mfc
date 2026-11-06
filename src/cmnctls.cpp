// cmnctls.cpp - the out-of-line members of the common control classes.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxcmn.h>
#include <objidl.h>

IMPLEMENT_DYNCREATE(CImageList, CObject)
IMPLEMENT_DYNAMIC(CToolBarCtrl, CWnd)
IMPLEMENT_DYNAMIC(CStatusBarCtrl, CWnd)
IMPLEMENT_DYNAMIC(CToolTipCtrl, CWnd)
IMPLEMENT_DYNAMIC(CProgressCtrl, CWnd)
IMPLEMENT_DYNAMIC(CSliderCtrl, CWnd)
IMPLEMENT_DYNAMIC(CSpinButtonCtrl, CWnd)
IMPLEMENT_DYNAMIC(CHotKeyCtrl, CWnd)
IMPLEMENT_DYNAMIC(CAnimateCtrl, CWnd)
IMPLEMENT_DYNAMIC(CHeaderCtrl, CWnd)
IMPLEMENT_DYNAMIC(CListCtrl, CWnd)
IMPLEMENT_DYNAMIC(CTreeCtrl, CWnd)
IMPLEMENT_DYNAMIC(CTabCtrl, CWnd)
IMPLEMENT_DYNAMIC(CRichEditCtrl, CWnd)
IMPLEMENT_DYNAMIC(CDateTimeCtrl, CWnd)
IMPLEMENT_DYNAMIC(CMonthCalCtrl, CWnd)
IMPLEMENT_DYNAMIC(CIPAddressCtrl, CWnd)
IMPLEMENT_DYNAMIC(CComboBoxEx, CComboBox)
IMPLEMENT_DYNAMIC(CReBarCtrl, CWnd)

// Registers the control class (through InitCommonControlsEx) and creates
// the window.
static BOOL _AfxCreateCommCtl(CWnd* pWnd, LONG fReg, LPCTSTR lpszClass, DWORD dwExStyle,
                              DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID) {
  if (!AfxDeferRegisterClass(fReg)) {
    // An old comctl32 may not know the newer ICC_ flag; the class may
    // still exist, so try anyway.
    AfxDeferRegisterClass(AFX_WNDCOMMCTLS_REG);
  }
  return pWnd->CWnd::CreateEx(dwExStyle, lpszClass, NULL, dwStyle, rect, pParentWnd, nID);
}

#define AFX_COMMCTL_CREATE(cls, reg, wndclass)                                       \
  BOOL cls::Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID) {   \
    return _AfxCreateCommCtl(this, reg, wndclass, 0, dwStyle, rect, pParentWnd, nID); \
  }

AFX_COMMCTL_CREATE(CToolBarCtrl, AFX_WNDCOMMCTL_BAR_REG, TOOLBARCLASSNAME)
AFX_COMMCTL_CREATE(CStatusBarCtrl, AFX_WNDCOMMCTL_BAR_REG, STATUSCLASSNAME)
AFX_COMMCTL_CREATE(CProgressCtrl, AFX_WNDCOMMCTL_PROGRESS_REG, PROGRESS_CLASS)
AFX_COMMCTL_CREATE(CSliderCtrl, AFX_WNDCOMMCTL_BAR_REG, TRACKBAR_CLASS)
AFX_COMMCTL_CREATE(CSpinButtonCtrl, AFX_WNDCOMMCTL_UPDOWN_REG, UPDOWN_CLASS)
AFX_COMMCTL_CREATE(CHotKeyCtrl, AFX_WNDCOMMCTL_HOTKEY_REG, HOTKEY_CLASS)
AFX_COMMCTL_CREATE(CAnimateCtrl, AFX_WNDCOMMCTL_ANIMATE_REG, ANIMATE_CLASS)
AFX_COMMCTL_CREATE(CHeaderCtrl, AFX_WNDCOMMCTL_LISTVIEW_REG, WC_HEADER)
AFX_COMMCTL_CREATE(CListCtrl, AFX_WNDCOMMCTL_LISTVIEW_REG, WC_LISTVIEW)
AFX_COMMCTL_CREATE(CTreeCtrl, AFX_WNDCOMMCTL_TREEVIEW_REG, WC_TREEVIEW)
AFX_COMMCTL_CREATE(CTabCtrl, AFX_WNDCOMMCTL_TAB_REG, WC_TABCONTROL)
AFX_COMMCTL_CREATE(CDateTimeCtrl, AFX_WNDCOMMCTL_DATE_REG, DATETIMEPICK_CLASS)
AFX_COMMCTL_CREATE(CMonthCalCtrl, AFX_WNDCOMMCTL_DATE_REG, MONTHCAL_CLASS)
AFX_COMMCTL_CREATE(CIPAddressCtrl, AFX_WNDCOMMCTL_INTERNET_REG, WC_IPADDRESS)
AFX_COMMCTL_CREATE(CComboBoxEx, AFX_WNDCOMMCTL_USEREX_REG, WC_COMBOBOXEX)
AFX_COMMCTL_CREATE(CReBarCtrl, AFX_WNDCOMMCTL_COOL_REG, REBARCLASSNAME)

BOOL CListCtrl::CreateEx(DWORD dwExStyle, DWORD dwStyle, const RECT& rect, CWnd* pParentWnd,
                         UINT nID) {
  return _AfxCreateCommCtl(this, AFX_WNDCOMMCTL_LISTVIEW_REG, WC_LISTVIEW, dwExStyle, dwStyle,
                           rect, pParentWnd, nID);
}

BOOL CTreeCtrl::CreateEx(DWORD dwExStyle, DWORD dwStyle, const RECT& rect, CWnd* pParentWnd,
                         UINT nID) {
  return _AfxCreateCommCtl(this, AFX_WNDCOMMCTL_TREEVIEW_REG, WC_TREEVIEW, dwExStyle, dwStyle,
                           rect, pParentWnd, nID);
}

BOOL CRichEditCtrl::Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID) {
  // Prefer rich edit 2.0 and fall back to the 1.0 control Windows 95 has.
  if (AfxInitRichEdit2())
    return CWnd::CreateEx(0, RICHEDIT_CLASS, NULL, dwStyle, rect, pParentWnd, nID);
  if (!AfxInitRichEdit()) return FALSE;
  return CWnd::CreateEx(0, _T("RICHEDIT"), NULL, dwStyle, rect, pParentWnd, nID);
}

// ---------------------------------------------------------------------
// CImageList

static CHandleMap* afxMapHIMAGELIST(BOOL bCreate = FALSE) {
  AFX_THREAD_STATE* pState = AfxGetThreadState();
  if (pState->m_pmapHIMAGELIST == NULL && bCreate)
    pState->m_pmapHIMAGELIST =
        new CHandleMap(RUNTIME_CLASS(CImageList), offsetof(CImageList, m_hImageList));
  return pState->m_pmapHIMAGELIST;
}

CImageList::~CImageList() { DeleteImageList(); }

CImageList* PASCAL CImageList::FromHandle(HIMAGELIST hImageList) {
  return (CImageList*)afxMapHIMAGELIST(TRUE)->FromHandle(hImageList);
}

CImageList* PASCAL CImageList::FromHandlePermanent(HIMAGELIST hImageList) {
  CHandleMap* pMap = afxMapHIMAGELIST();
  return pMap != NULL && hImageList != NULL ? (CImageList*)pMap->LookupPermanent(hImageList)
                                            : NULL;
}

void PASCAL CImageList::DeleteTempMap() {
  CHandleMap* pMap = afxMapHIMAGELIST();
  if (pMap != NULL) pMap->DeleteTemp();
}

BOOL CImageList::Attach(HIMAGELIST hImageList) {
  if (hImageList == NULL) return FALSE;
  m_hImageList = hImageList;
  afxMapHIMAGELIST(TRUE)->SetPermanent(hImageList, this);
  return TRUE;
}

HIMAGELIST CImageList::Detach() {
  HIMAGELIST h = m_hImageList;
  if (h != NULL) {
    CHandleMap* pMap = afxMapHIMAGELIST();
    if (pMap != NULL && pMap->LookupPermanent(h) == this) pMap->RemoveHandle(h);
  }
  m_hImageList = NULL;
  return h;
}

BOOL CImageList::DeleteImageList() {
  if (m_hImageList == NULL) return FALSE;
  return ImageList_Destroy(Detach());
}

BOOL CImageList::Create(int cx, int cy, UINT nFlags, int nInitial, int nGrow) {
  AfxDeferRegisterClass(AFX_WNDCOMMCTLS_REG);
  return Attach(ImageList_Create(cx, cy, nFlags, nInitial, nGrow));
}

BOOL CImageList::Create(UINT nBitmapID, int cx, int nGrow, COLORREF crMask) {
  return Create(MAKEINTRESOURCE(nBitmapID), cx, nGrow, crMask);
}

BOOL CImageList::Create(LPCTSTR lpszBitmapID, int cx, int nGrow, COLORREF crMask) {
  AfxDeferRegisterClass(AFX_WNDCOMMCTLS_REG);
  HINSTANCE hInst = AfxFindResourceHandle(lpszBitmapID, RT_BITMAP);
  return Attach(ImageList_LoadImage(hInst, lpszBitmapID, cx, nGrow, crMask, IMAGE_BITMAP,
                                    LR_CREATEDIBSECTION));
}

BOOL CImageList::Create(CImageList& imagelist1, int nImage1, CImageList& imagelist2, int nImage2,
                        int dx, int dy) {
  return Attach(ImageList_Merge(imagelist1.m_hImageList, nImage1, imagelist2.m_hImageList,
                                nImage2, dx, dy));
}

BOOL CImageList::Create(CImageList* pImageList) {
  if (pImageList == NULL) return FALSE;
  return Attach(ImageList_Duplicate(pImageList->m_hImageList));
}

// A minimal IStream over a CArchive, for ImageList_Read and _Write.
namespace {
class CArchiveStream : public IStream {
public:
  explicit CArchiveStream(CArchive* pArchive) : m_pArchive(pArchive) {}

  // comctl32 only reads or writes through the stream; answering every
  // interface request keeps the uuid library out of the link.
  STDMETHOD(QueryInterface)(REFIID, void** ppv) override {
    *ppv = this;
    return S_OK;
  }
  STDMETHOD_(ULONG, AddRef)() override { return 1; }
  STDMETHOD_(ULONG, Release)() override { return 1; }
  STDMETHOD(Read)(void* pv, ULONG cb, ULONG* pcbRead) override {
    ULONG n = 0;
    try {
      n = m_pArchive->Read(pv, cb);
    } catch (CException* e) {
      e->Delete();
      return E_FAIL;
    }
    if (pcbRead != NULL) *pcbRead = n;
    return n == cb ? S_OK : S_FALSE;
  }
  STDMETHOD(Write)(const void* pv, ULONG cb, ULONG* pcbWritten) override {
    try {
      m_pArchive->Write(pv, cb);
    } catch (CException* e) {
      e->Delete();
      return E_FAIL;
    }
    if (pcbWritten != NULL) *pcbWritten = cb;
    return S_OK;
  }
  STDMETHOD(Seek)(LARGE_INTEGER, DWORD, ULARGE_INTEGER*) override { return E_NOTIMPL; }
  STDMETHOD(SetSize)(ULARGE_INTEGER) override { return E_NOTIMPL; }
  STDMETHOD(CopyTo)(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override {
    return E_NOTIMPL;
  }
  STDMETHOD(Commit)(DWORD) override { return S_OK; }
  STDMETHOD(Revert)() override { return E_NOTIMPL; }
  STDMETHOD(LockRegion)(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
  STDMETHOD(UnlockRegion)(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
  STDMETHOD(Stat)(STATSTG*, DWORD) override { return E_NOTIMPL; }
  STDMETHOD(Clone)(IStream**) override { return E_NOTIMPL; }

private:
  CArchive* m_pArchive;
};
}  // namespace

BOOL CImageList::Read(CArchive* pArchive) {
  CArchiveStream stream(pArchive);
  return Attach(ImageList_Read(&stream));
}

BOOL CImageList::Write(CArchive* pArchive) {
  CArchiveStream stream(pArchive);
  return ImageList_Write(m_hImageList, &stream);
}

// ---------------------------------------------------------------------
// CToolBarCtrl

int CToolBarCtrl::AddBitmap(int nNumButtons, UINT nBitmapID) {
  TBADDBITMAP tbab;
  tbab.hInst = AfxFindResourceHandle(MAKEINTRESOURCE(nBitmapID), RT_BITMAP);
  tbab.nID = nBitmapID;
  return (int)::SendMessage(m_hWnd, TB_ADDBITMAP, nNumButtons, (LPARAM)&tbab);
}

int CToolBarCtrl::AddBitmap(int nNumButtons, CBitmap* pBitmap) {
  TBADDBITMAP tbab;
  tbab.hInst = NULL;
  tbab.nID = (UINT_PTR)pBitmap->GetSafeHandle();
  return (int)::SendMessage(m_hWnd, TB_ADDBITMAP, nNumButtons, (LPARAM)&tbab);
}

int CToolBarCtrl::AddString(UINT nStringID) {
  // The control's own string loading needs the list double-null
  // terminated, so load the string here.
  CString str;
  if (!str.LoadString(nStringID)) return -1;
  int nLen = str.GetLength();
  LPTSTR p = str.GetBuffer(nLen + 1);
  p[nLen + 1] = 0;
  int n = (int)::SendMessage(m_hWnd, TB_ADDSTRING, 0, (LPARAM)p);
  str.ReleaseBuffer(nLen);
  return n;
}

CToolTipCtrl* CToolBarCtrl::GetToolTips() const {
  return (CToolTipCtrl*)CWnd::FromHandle((HWND)::SendMessage(m_hWnd, TB_GETTOOLTIPS, 0, 0));
}

void CToolBarCtrl::SetToolTips(CToolTipCtrl* pTip) {
  ::SendMessage(m_hWnd, TB_SETTOOLTIPS, (WPARAM)pTip->GetSafeHwnd(), 0);
}

CImageList* CToolBarCtrl::SetImageList(CImageList* pImageList) {
  return CImageList::FromHandle((HIMAGELIST)::SendMessage(
      m_hWnd, TB_SETIMAGELIST, 0, (LPARAM)pImageList->GetSafeHandle()));
}

CImageList* CToolBarCtrl::SetHotImageList(CImageList* pImageList) {
  return CImageList::FromHandle((HIMAGELIST)::SendMessage(
      m_hWnd, TB_SETHOTIMAGELIST, 0, (LPARAM)pImageList->GetSafeHandle()));
}

CImageList* CToolBarCtrl::SetDisabledImageList(CImageList* pImageList) {
  return CImageList::FromHandle((HIMAGELIST)::SendMessage(
      m_hWnd, TB_SETDISABLEDIMAGELIST, 0, (LPARAM)pImageList->GetSafeHandle()));
}

// ---------------------------------------------------------------------
// CStatusBarCtrl

CString CStatusBarCtrl::GetText(int nPane, int* pType) const {
  CString str;
  DWORD dw = (DWORD)::SendMessage(m_hWnd, SB_GETTEXTLENGTH, nPane, 0);
  int nLen = LOWORD(dw);
  if (pType != NULL) *pType = HIWORD(dw);
  if (nLen > 0) {
    ::SendMessage(m_hWnd, SB_GETTEXT, nPane, (LPARAM)str.GetBuffer(nLen + 1));
    str.ReleaseBuffer(nLen);
  }
  return str;
}

// ---------------------------------------------------------------------
// CToolTipCtrl

BOOL CToolTipCtrl::Create(CWnd* pParentWnd, DWORD dwStyle) {
  if (!AfxDeferRegisterClass(AFX_WNDCOMMCTL_BAR_REG)) AfxDeferRegisterClass(AFX_WNDCOMMCTLS_REG);
  BOOL bResult = CWnd::CreateEx(0, TOOLTIPS_CLASS, NULL, WS_POPUP | dwStyle, CW_USEDEFAULT,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                pParentWnd->GetSafeHwnd(), NULL, NULL);
  if (bResult) SetOwner(pParentWnd);
  return bResult;
}

void CToolTipCtrl::FillInToolInfo(TOOLINFO& ti, CWnd* pWnd, UINT_PTR nIDTool) const {
  memset(&ti, 0, sizeof(ti));
  // The version 1 size is what the Windows 95 comctl32 accepts.
  ti.cbSize = TTTOOLINFO_V1_SIZE;
  if (nIDTool == 0) {
    ti.hwnd = ::GetParent(pWnd->GetSafeHwnd());
    ti.uFlags = TTF_IDISHWND;
    ti.uId = (UINT_PTR)pWnd->GetSafeHwnd();
  } else {
    ti.hwnd = pWnd->GetSafeHwnd();
    ti.uFlags = 0;
    ti.uId = nIDTool;
  }
}

BOOL CToolTipCtrl::AddTool(CWnd* pWnd, LPCTSTR lpszText, LPCRECT lpRectTool, UINT_PTR nIDTool) {
  TOOLINFO ti;
  FillInToolInfo(ti, pWnd, nIDTool);
  if (lpRectTool != NULL)
    ti.rect = *lpRectTool;
  else if (nIDTool != 0)
    ::GetClientRect(pWnd->GetSafeHwnd(), &ti.rect);
  ti.uFlags |= TTF_SUBCLASS;
  ti.hinst = AfxGetResourceHandle();
  ti.lpszText = (LPTSTR)lpszText;
  return (BOOL)::SendMessage(m_hWnd, TTM_ADDTOOL, 0, (LPARAM)&ti);
}

BOOL CToolTipCtrl::AddTool(CWnd* pWnd, UINT nIDText, LPCRECT lpRectTool, UINT_PTR nIDTool) {
  return AddTool(pWnd, MAKEINTRESOURCE(nIDText), lpRectTool, nIDTool);
}

void CToolTipCtrl::DelTool(CWnd* pWnd, UINT_PTR nIDTool) {
  TOOLINFO ti;
  FillInToolInfo(ti, pWnd, nIDTool);
  ::SendMessage(m_hWnd, TTM_DELTOOL, 0, (LPARAM)&ti);
}

void CToolTipCtrl::UpdateTipText(LPCTSTR lpszText, CWnd* pWnd, UINT_PTR nIDTool) {
  TOOLINFO ti;
  FillInToolInfo(ti, pWnd, nIDTool);
  ti.hinst = AfxGetResourceHandle();
  ti.lpszText = (LPTSTR)lpszText;
  ::SendMessage(m_hWnd, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
}

void CToolTipCtrl::UpdateTipText(UINT nIDText, CWnd* pWnd, UINT_PTR nIDTool) {
  UpdateTipText(MAKEINTRESOURCE(nIDText), pWnd, nIDTool);
}

// ---------------------------------------------------------------------
// Owner-drawn controls send WM_DRAWITEM to the parent, which reflects it.

#define AFX_REFLECT_DRAWITEM(cls)                                                         \
  void cls::DrawItem(LPDRAWITEMSTRUCT) {}                                                 \
  BOOL cls::OnChildNotify(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult) { \
    if (message != WM_DRAWITEM) return CWnd::OnChildNotify(message, wParam, lParam, pResult); \
    DrawItem((LPDRAWITEMSTRUCT)lParam);                                                   \
    if (pResult != NULL) *pResult = TRUE;                                                 \
    return TRUE;                                                                          \
  }

AFX_REFLECT_DRAWITEM(CHeaderCtrl)
AFX_REFLECT_DRAWITEM(CListCtrl)
AFX_REFLECT_DRAWITEM(CTabCtrl)

// ---------------------------------------------------------------------
// CHeaderCtrl

CImageList* CHeaderCtrl::SetImageList(CImageList* pImageList) {
  return CImageList::FromHandle(
      (HIMAGELIST)::SendMessage(m_hWnd, HDM_SETIMAGELIST, 0, (LPARAM)pImageList->GetSafeHandle()));
}

CImageList* CHeaderCtrl::GetImageList() const {
  return CImageList::FromHandle((HIMAGELIST)::SendMessage(m_hWnd, HDM_GETIMAGELIST, 0, 0));
}

// ---------------------------------------------------------------------
// CListCtrl

CImageList* CListCtrl::GetImageList(int nImageList) const {
  return CImageList::FromHandle((HIMAGELIST)::SendMessage(m_hWnd, LVM_GETIMAGELIST, nImageList, 0));
}

CImageList* CListCtrl::SetImageList(CImageList* pImageList, int nImageListType) {
  return CImageList::FromHandle((HIMAGELIST)::SendMessage(
      m_hWnd, LVM_SETIMAGELIST, nImageListType, (LPARAM)pImageList->GetSafeHandle()));
}

BOOL CListCtrl::SetItem(int nItem, int nSubItem, UINT nMask, LPCTSTR lpszItem, int nImage,
                        UINT nState, UINT nStateMask, LPARAM lParam) {
  LVITEM lvi;
  memset(&lvi, 0, sizeof(lvi));
  lvi.mask = nMask;
  lvi.iItem = nItem;
  lvi.iSubItem = nSubItem;
  lvi.stateMask = nStateMask;
  lvi.state = nState;
  lvi.pszText = (LPTSTR)lpszItem;
  lvi.iImage = nImage;
  lvi.lParam = lParam;
  return (BOOL)::SendMessage(m_hWnd, LVM_SETITEM, 0, (LPARAM)&lvi);
}

BOOL CListCtrl::SetItemState(int nItem, UINT nState, UINT nMask) {
  LVITEM lvi;
  memset(&lvi, 0, sizeof(lvi));
  lvi.stateMask = nMask;
  lvi.state = nState;
  return (BOOL)::SendMessage(m_hWnd, LVM_SETITEMSTATE, nItem, (LPARAM)&lvi);
}

int CListCtrl::GetItemText(int nItem, int nSubItem, LPTSTR lpszText, int nLen) const {
  LVITEM lvi;
  memset(&lvi, 0, sizeof(lvi));
  lvi.iSubItem = nSubItem;
  lvi.cchTextMax = nLen;
  lvi.pszText = lpszText;
  return (int)::SendMessage(m_hWnd, LVM_GETITEMTEXT, nItem, (LPARAM)&lvi);
}

CString CListCtrl::GetItemText(int nItem, int nSubItem) const {
  // LVM_GETITEMTEXT does not report the full length, so grow the buffer
  // until the text fits.
  CString str;
  int nLen = 128;
  int nRes;
  do {
    nLen *= 2;
    LVITEM lvi;
    memset(&lvi, 0, sizeof(lvi));
    lvi.iSubItem = nSubItem;
    lvi.cchTextMax = nLen;
    lvi.pszText = str.GetBuffer(nLen);
    nRes = (int)::SendMessage(m_hWnd, LVM_GETITEMTEXT, nItem, (LPARAM)&lvi);
    str.ReleaseBuffer(nRes);
  } while (nRes >= nLen - 1);
  return str;
}

BOOL CListCtrl::SetItemText(int nItem, int nSubItem, LPCTSTR lpszText) {
  LVITEM lvi;
  memset(&lvi, 0, sizeof(lvi));
  lvi.iSubItem = nSubItem;
  lvi.pszText = (LPTSTR)lpszText;
  return (BOOL)::SendMessage(m_hWnd, LVM_SETITEMTEXT, nItem, (LPARAM)&lvi);
}

BOOL CListCtrl::SetItemData(int nItem, DWORD_PTR dwData) {
  return SetItem(nItem, 0, LVIF_PARAM, NULL, 0, 0, 0, (LPARAM)dwData);
}

DWORD_PTR CListCtrl::GetItemData(int nItem) const {
  LVITEM lvi;
  memset(&lvi, 0, sizeof(lvi));
  lvi.iItem = nItem;
  lvi.mask = LVIF_PARAM;
  ::SendMessage(m_hWnd, LVM_GETITEM, 0, (LPARAM)&lvi);
  return (DWORD_PTR)lvi.lParam;
}

BOOL CListCtrl::GetSubItemRect(int iItem, int iSubItem, int nArea, CRect& ref) const {
  RECT rect;
  rect.top = iSubItem;
  rect.left = nArea;
  BOOL bRet = (BOOL)::SendMessage(m_hWnd, LVM_GETSUBITEMRECT, iItem, (LPARAM)&rect);
  if (bRet) ref = rect;
  return bRet;
}

int CListCtrl::InsertItem(UINT nMask, int nItem, LPCTSTR lpszItem, UINT nState, UINT nStateMask,
                          int nImage, LPARAM lParam) {
  LVITEM lvi;
  memset(&lvi, 0, sizeof(lvi));
  lvi.mask = nMask;
  lvi.iItem = nItem;
  lvi.state = nState;
  lvi.stateMask = nStateMask;
  lvi.pszText = (LPTSTR)lpszItem;
  lvi.iImage = nImage;
  lvi.lParam = lParam;
  return (int)::SendMessage(m_hWnd, LVM_INSERTITEM, 0, (LPARAM)&lvi);
}

int CListCtrl::InsertItem(int nItem, LPCTSTR lpszItem) {
  return InsertItem(LVIF_TEXT, nItem, lpszItem, 0, 0, 0, 0);
}

int CListCtrl::InsertItem(int nItem, LPCTSTR lpszItem, int nImage) {
  return InsertItem(LVIF_TEXT | LVIF_IMAGE, nItem, lpszItem, 0, 0, nImage, 0);
}

int CListCtrl::HitTest(CPoint pt, UINT* pFlags) const {
  LVHITTESTINFO hti;
  memset(&hti, 0, sizeof(hti));
  hti.pt = pt;
  int nRes = (int)::SendMessage(m_hWnd, LVM_HITTEST, 0, (LPARAM)&hti);
  if (pFlags != NULL) *pFlags = hti.flags;
  return nRes;
}

int CListCtrl::InsertColumn(int nCol, LPCTSTR lpszColumnHeading, int nFormat, int nWidth,
                            int nSubItem) {
  LVCOLUMN column;
  memset(&column, 0, sizeof(column));
  column.mask = LVCF_TEXT | LVCF_FMT;
  column.pszText = (LPTSTR)lpszColumnHeading;
  column.fmt = nFormat;
  if (nWidth != -1) {
    column.mask |= LVCF_WIDTH;
    column.cx = nWidth;
  }
  if (nSubItem != -1) {
    column.mask |= LVCF_SUBITEM;
    column.iSubItem = nSubItem;
  }
  return (int)::SendMessage(m_hWnd, LVM_INSERTCOLUMN, nCol, (LPARAM)&column);
}

CImageList* CListCtrl::CreateDragImage(int nItem, LPPOINT lpPoint) {
  HIMAGELIST hImageList =
      (HIMAGELIST)::SendMessage(m_hWnd, LVM_CREATEDRAGIMAGE, nItem, (LPARAM)lpPoint);
  if (hImageList == NULL) return NULL;
  CImageList* pImageList = new CImageList;
  pImageList->Attach(hImageList);
  return pImageList;
}

// ---------------------------------------------------------------------
// CTreeCtrl

CImageList* CTreeCtrl::GetImageList(int nImageList) const {
  return CImageList::FromHandle((HIMAGELIST)::SendMessage(m_hWnd, TVM_GETIMAGELIST, nImageList, 0));
}

CImageList* CTreeCtrl::SetImageList(CImageList* pImageList, int nImageListType) {
  return CImageList::FromHandle((HIMAGELIST)::SendMessage(
      m_hWnd, TVM_SETIMAGELIST, nImageListType, (LPARAM)pImageList->GetSafeHandle()));
}

BOOL CTreeCtrl::SetItem(HTREEITEM hItem, UINT nMask, LPCTSTR lpszItem, int nImage,
                        int nSelectedImage, UINT nState, UINT nStateMask, LPARAM lParam) {
  TVITEM item;
  memset(&item, 0, sizeof(item));
  item.hItem = hItem;
  item.mask = nMask;
  item.pszText = (LPTSTR)lpszItem;
  item.iImage = nImage;
  item.iSelectedImage = nSelectedImage;
  item.state = nState;
  item.stateMask = nStateMask;
  item.lParam = lParam;
  return (BOOL)::SendMessage(m_hWnd, TVM_SETITEM, 0, (LPARAM)&item);
}

CString CTreeCtrl::GetItemText(HTREEITEM hItem) const {
  CString str;
  int nLen = 128;
  int nRes;
  do {
    nLen *= 2;
    TVITEM item;
    memset(&item, 0, sizeof(item));
    item.hItem = hItem;
    item.mask = TVIF_TEXT;
    item.pszText = str.GetBuffer(nLen);
    item.cchTextMax = nLen;
    ::SendMessage(m_hWnd, TVM_GETITEM, 0, (LPARAM)&item);
    nRes = lstrlen(item.pszText);
    if (item.pszText != str.GetBuffer(0)) {
      // The control may hand back its own buffer.
      CString strCopy(item.pszText);
      str.ReleaseBuffer(0);
      return strCopy;
    }
    str.ReleaseBuffer(nRes);
  } while (nRes >= nLen - 1);
  return str;
}

BOOL CTreeCtrl::GetItemImage(HTREEITEM hItem, int& nImage, int& nSelectedImage) const {
  TVITEM item;
  memset(&item, 0, sizeof(item));
  item.hItem = hItem;
  item.mask = TVIF_IMAGE | TVIF_SELECTEDIMAGE;
  BOOL bRes = (BOOL)::SendMessage(m_hWnd, TVM_GETITEM, 0, (LPARAM)&item);
  if (bRes) {
    nImage = item.iImage;
    nSelectedImage = item.iSelectedImage;
  }
  return bRes;
}

DWORD_PTR CTreeCtrl::GetItemData(HTREEITEM hItem) const {
  TVITEM item;
  memset(&item, 0, sizeof(item));
  item.hItem = hItem;
  item.mask = TVIF_PARAM;
  ::SendMessage(m_hWnd, TVM_GETITEM, 0, (LPARAM)&item);
  return (DWORD_PTR)item.lParam;
}

BOOL CTreeCtrl::ItemHasChildren(HTREEITEM hItem) const {
  TVITEM item;
  memset(&item, 0, sizeof(item));
  item.hItem = hItem;
  item.mask = TVIF_CHILDREN;
  ::SendMessage(m_hWnd, TVM_GETITEM, 0, (LPARAM)&item);
  return item.cChildren;
}

HTREEITEM CTreeCtrl::InsertItem(UINT nMask, LPCTSTR lpszItem, int nImage, int nSelectedImage,
                                UINT nState, UINT nStateMask, LPARAM lParam, HTREEITEM hParent,
                                HTREEITEM hInsertAfter) {
  TVINSERTSTRUCT tvis;
  memset(&tvis, 0, sizeof(tvis));
  tvis.hParent = hParent;
  tvis.hInsertAfter = hInsertAfter;
  tvis.item.mask = nMask;
  tvis.item.pszText = (LPTSTR)lpszItem;
  tvis.item.iImage = nImage;
  tvis.item.iSelectedImage = nSelectedImage;
  tvis.item.state = nState;
  tvis.item.stateMask = nStateMask;
  tvis.item.lParam = lParam;
  return (HTREEITEM)::SendMessage(m_hWnd, TVM_INSERTITEM, 0, (LPARAM)&tvis);
}

HTREEITEM CTreeCtrl::InsertItem(LPCTSTR lpszItem, HTREEITEM hParent, HTREEITEM hInsertAfter) {
  return InsertItem(TVIF_TEXT, lpszItem, 0, 0, 0, 0, 0, hParent, hInsertAfter);
}

HTREEITEM CTreeCtrl::InsertItem(LPCTSTR lpszItem, int nImage, int nSelectedImage,
                                HTREEITEM hParent, HTREEITEM hInsertAfter) {
  return InsertItem(TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE, lpszItem, nImage, nSelectedImage,
                    0, 0, 0, hParent, hInsertAfter);
}

HTREEITEM CTreeCtrl::HitTest(CPoint pt, UINT* pFlags) const {
  TVHITTESTINFO hti;
  memset(&hti, 0, sizeof(hti));
  hti.pt = pt;
  HTREEITEM h = (HTREEITEM)::SendMessage(m_hWnd, TVM_HITTEST, 0, (LPARAM)&hti);
  if (pFlags != NULL) *pFlags = hti.flags;
  return h;
}

CImageList* CTreeCtrl::CreateDragImage(HTREEITEM hItem) {
  HIMAGELIST hImageList = (HIMAGELIST)::SendMessage(m_hWnd, TVM_CREATEDRAGIMAGE, 0, (LPARAM)hItem);
  if (hImageList == NULL) return NULL;
  CImageList* pImageList = new CImageList;
  pImageList->Attach(hImageList);
  return pImageList;
}

// ---------------------------------------------------------------------
// CTabCtrl

CImageList* CTabCtrl::GetImageList() const {
  return CImageList::FromHandle((HIMAGELIST)::SendMessage(m_hWnd, TCM_GETIMAGELIST, 0, 0));
}

CImageList* CTabCtrl::SetImageList(CImageList* pImageList) {
  return CImageList::FromHandle(
      (HIMAGELIST)::SendMessage(m_hWnd, TCM_SETIMAGELIST, 0, (LPARAM)pImageList->GetSafeHandle()));
}

CToolTipCtrl* CTabCtrl::GetToolTips() const {
  return (CToolTipCtrl*)CWnd::FromHandle((HWND)::SendMessage(m_hWnd, TCM_GETTOOLTIPS, 0, 0));
}

void CTabCtrl::SetToolTips(CToolTipCtrl* pWndTip) {
  ::SendMessage(m_hWnd, TCM_SETTOOLTIPS, (WPARAM)pWndTip->GetSafeHwnd(), 0);
}

LONG CTabCtrl::InsertItem(int nItem, LPCTSTR lpszItem) {
  TCITEM item;
  memset(&item, 0, sizeof(item));
  item.mask = TCIF_TEXT;
  item.pszText = (LPTSTR)lpszItem;
  return (LONG)::SendMessage(m_hWnd, TCM_INSERTITEM, nItem, (LPARAM)&item);
}

LONG CTabCtrl::InsertItem(int nItem, LPCTSTR lpszItem, int nImage) {
  TCITEM item;
  memset(&item, 0, sizeof(item));
  item.mask = TCIF_TEXT | TCIF_IMAGE;
  item.pszText = (LPTSTR)lpszItem;
  item.iImage = nImage;
  return (LONG)::SendMessage(m_hWnd, TCM_INSERTITEM, nItem, (LPARAM)&item);
}

// ---------------------------------------------------------------------
// CRichEditCtrl

int CRichEditCtrl::GetLine(int nIndex, LPTSTR lpszBuffer, int nMaxLength) const {
  // EM_GETLINE takes the buffer size in its first WORD.
  if (nMaxLength < (int)(sizeof(WORD) / sizeof(TCHAR))) return 0;
  *(LPWORD)lpszBuffer = (WORD)nMaxLength;
  return (int)::SendMessage(m_hWnd, EM_GETLINE, nIndex, (LPARAM)lpszBuffer);
}

CString CRichEditCtrl::GetSelText() const {
  CHARRANGE cr;
  ::SendMessage(m_hWnd, EM_EXGETSEL, 0, (LPARAM)&cr);
  CString str;
  int nLen = cr.cpMax - cr.cpMin;
  if (nLen <= 0) return str;
  LPTSTR p = str.GetBuffer(nLen + 1);
  nLen = (int)::SendMessage(m_hWnd, EM_GETSELTEXT, 0, (LPARAM)p);
  str.ReleaseBuffer(nLen);
  return str;
}

long CRichEditCtrl::GetTextRange(int nFirst, int nLast, CString& refString) const {
  TEXTRANGE tr;
  tr.chrg.cpMin = nFirst;
  tr.chrg.cpMax = nLast;
  int nLen = nLast - nFirst;
  if (nLast < 0 || nLen < 0) nLen = (int)::SendMessage(m_hWnd, WM_GETTEXTLENGTH, 0, 0) - nFirst;
  if (nLen <= 0) {
    refString.Empty();
    return 0;
  }
  tr.lpstrText = refString.GetBuffer(nLen + 1);
  long n = (long)::SendMessage(m_hWnd, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
  refString.ReleaseBuffer(n);
  return n;
}

// ---------------------------------------------------------------------
// CComboBoxEx

CImageList* CComboBoxEx::SetImageList(CImageList* pImageList) {
  return CImageList::FromHandle((HIMAGELIST)::SendMessage(m_hWnd, CBEM_SETIMAGELIST, 0,
                                                          (LPARAM)pImageList->GetSafeHandle()));
}

CImageList* CComboBoxEx::GetImageList() const {
  return CImageList::FromHandle((HIMAGELIST)::SendMessage(m_hWnd, CBEM_GETIMAGELIST, 0, 0));
}
