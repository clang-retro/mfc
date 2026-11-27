// afxeditbrowsectrl.h - CMFCEditBrowseCtrl, an edit control with a browse
// button at its right edge (in the non-client area): it chooses a file
// (CFileDialog), a folder, or calls OnBrowse for the application's own.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXEDITBROWSECTRL_H__
#define __AFXEDITBROWSECTRL_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif
#include <afxvisualmanager.h>

class CMFCEditBrowseCtrl : public CEdit {
  DECLARE_DYNAMIC(CMFCEditBrowseCtrl)
public:
  enum BrowseMode { BrowseMode_None, BrowseMode_Default, BrowseMode_File, BrowseMode_Folder };

  CMFCEditBrowseCtrl();
  virtual ~CMFCEditBrowseCtrl();

  void EnableBrowseButton(BOOL bEnable = TRUE, LPCTSTR szLabel = _T("..."));
  void EnableFileBrowseButton(LPCTSTR lpszDefExt = NULL, LPCTSTR lpszFilter = NULL,
                              DWORD dwFlags = OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT);
  void EnableFolderBrowseButton();
  BrowseMode GetMode() const { return m_Mode; }

  void SetBrowseButtonImage(HICON hIcon, BOOL bAutoDestroy = TRUE);
  void SetBrowseButtonImage(HBITMAP hBitmap, BOOL bAutoDestroy = TRUE);
  void SetBrowseButtonImage(UINT uiBmpResId);

  // The browse button's rectangle, in window coordinates.
  CRect GetBrowseButtonRect() const;

  virtual void OnBrowse();
  virtual void OnDrawBrowseButton(CDC* pDC, CRect rect, BOOL bIsButtonPressed, BOOL bIsButtonHot);
  virtual void OnChangeLayout();
  virtual void OnAfterUpdate() {}
  virtual BOOL OnIllegalFileName(CString& strFileName);

protected:
  void _FreeImage();
  void _SetText(const CString& str);
  BOOL _InButton(CPoint ptScreen) const;

  afx_msg void OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp);
  afx_msg void OnNcPaint();
  afx_msg LRESULT OnNcHitTest(CPoint point);
  afx_msg void OnNcLButtonDown(UINT nHitTest, CPoint point);
  afx_msg void OnNcMouseMove(UINT nHitTest, CPoint point);
  afx_msg LRESULT OnNcMouseLeave(WPARAM, LPARAM);
  afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
  afx_msg void OnMouseMove(UINT nFlags, CPoint point);
  afx_msg void OnCancelMode();
  afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
  DECLARE_MESSAGE_MAP()

  BrowseMode m_Mode;
  CString m_strLabel, m_strDefFileExt, m_strFileFilter;
  DWORD m_dwFileDialogFlags;
  int m_nBrowseButtonWidth;
  BOOL m_bIsButtonPressed, m_bIsButtonHighlighted, m_bIsButtonCaptured, m_bDefaultImage;
  HICON m_hIcon;
  HBITMAP m_hBitmap;
  BOOL m_bAutoDestroyImage;
  CSize m_sizeImage;
};

#endif  // __AFXEDITBROWSECTRL_H__
