// afxtoolbarimages.h - CMFCToolBarImages, a strip of same-sized images
// loaded from bitmap resources (gray, or magenta for 24-bit art, as the
// clear color; 32-bit art keeps its alpha), drawn normal, highlighted,
// disabled (embossed) or faded.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXTOOLBARIMAGES_H__
#define __AFXTOOLBARIMAGES_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif
#include <afxcmn.h>

// What a run of Draw calls shares; MFC code declares one and passes it to
// PrepareDrawImage and EndDrawImage.
class CAfxDrawState {
public:
  CAfxDrawState() : hbmMono(NULL), hbmMonoOld(NULL), hbmOldGlyphs(NULL), iImageWidth(0), iImageHeight(0) {}
  HBITMAP hbmMono, hbmMonoOld, hbmOldGlyphs;
  int iImageWidth, iImageHeight;
};

class CMFCToolBarImages : public CObject {
public:
  CMFCToolBarImages();
  virtual ~CMFCToolBarImages();

  BOOL Load(UINT uiResID, HINSTANCE hinstRes = NULL, BOOL bAdd = FALSE);
  BOOL Load(LPCTSTR lpszBmpFileName, DWORD nMaxFileSize = 819200);
  int AddImage(HBITMAP hbmp, BOOL bSetBitPerPixel = FALSE);
  int AddImage(const CMFCToolBarImages& imageList, int nIndex);
  int AddIcon(HICON hIcon, BOOL bAlphaBlend = FALSE);
  BOOL UpdateImage(int iImage, HBITMAP hbmp);
  BOOL DeleteImage(int iImage);
  void Clear();
  BOOL IsValid() const { return m_hImageList != NULL; }
  int GetCount() const;
  void SetImageSize(SIZE sizeImage, BOOL bUpdateCount = FALSE);
  CSize GetImageSize(BOOL /*bDest*/ = FALSE) const { return m_sizeImage; }
  COLORREF SetTransparentColor(COLORREF clrTransparent);
  COLORREF GetTransparentColor() const { return m_clrTransparent; }
  int GetBitsPerPixel() const { return m_nBitsPerPixel; }
  HIMAGELIST GetImageList() const { return m_hImageList; }
  HICON ExtractIcon(int nIndex);
  BOOL IsReadOnly() const { return FALSE; }
  void SetDisabledImageAlpha(BYTE nValue) { m_nDisabledImageAlpha = nValue; }
  void SetFadedImageAlpha(BYTE nValue) { m_nFadedImageAlpha = nValue; }

  BOOL Draw(CDC* pDC, int x, int y, int iImage, BOOL bHilite = FALSE, BOOL bDisabled = FALSE,
            BOOL bIndeterminate = FALSE, BOOL bShadow = FALSE, BOOL bInactive = FALSE, BYTE alphaSrc = 255);
  BOOL DrawEx(CDC* pDC, CRect rect, int iImageIndex, int horzAlign = 1, int vertAlign = 1,
              CRect rectSrc = CRect(0, 0, 0, 0), BYTE alphaSrc = 255);
  // Before and after a run of Draw calls (kept for MFC's sake).
  BOOL PrepareDrawImage(CAfxDrawState& ds, CSize sizeImageDest = CSize(0, 0), BOOL /*bFadeInactive*/ = FALSE) {
    ds.iImageWidth = sizeImageDest.cx != 0 ? sizeImageDest.cx : m_sizeImage.cx;
    ds.iImageHeight = sizeImageDest.cy != 0 ? sizeImageDest.cy : m_sizeImage.cy;
    return m_hImageList != NULL;
  }
  void EndDrawImage(CAfxDrawState& /*ds*/) {}

protected:
  void _Create();

  HIMAGELIST m_hImageList;
  CSize m_sizeImage;
  COLORREF m_clrTransparent;
  int m_nBitsPerPixel;
  BYTE m_nDisabledImageAlpha, m_nFadedImageAlpha;
};

#endif  // __AFXTOOLBARIMAGES_H__
