// win16imgl.cpp - the image list functions (ImageList_ of commctrl.h)
// for 16-bit Windows, which has no comctl32.dll.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
//
// An image list keeps its images side by side in one bitmap that is
// compatible with the screen, and with ILC_MASK their masks in one
// monochrome bitmap. In the mask, 1 is a transparent pixel. A
// transparent pixel of the image is black, so that a draw is the mask
// with SRCAND and then the image with SRCPAINT. The bitmaps grow by
// cGrow images. ImageList_Read and ImageList_Write fail: 16-bit Windows
// has no stream format for image lists.
#include "stdafx.h"

#ifdef _AFX_WIN16

#include <commctrl.h>

#include "afximpl.h"

// The data of an image list. A HIMAGELIST points to it.
struct W16ImageList {
  int cx, cy;          // the size of one image
  UINT flags;          // the ILC_ flags
  int nCount;          // the number of images
  int nCapacity;       // the number of images that the bitmaps hold
  int nGrow;           // the number of images the bitmaps grow by
  HBITMAP hbmImage;    // the images, nCapacity * cx by cy
  HBITMAP hbmMask;     // the masks, or NULL without ILC_MASK
  COLORREF clrBk;      // ImageList_SetBkColor, or CLR_NONE
  int aOverlay[15];    // the image of each overlay, or -1
};

/// Gives the data of an image list from its handle.
static inline W16ImageList* IL(HIMAGELIST himl) { return (W16ImageList*)himl; }

namespace {

// The data of a drag (ImageList_BeginDrag).
struct W16Drag {
  HIMAGELIST himl;     // a copy of the image to drag, or NULL
  POINT ptHotspot;     // the hot spot in the image
  HWND hwndLock;       // the window of ImageList_DragEnter
  POINT ptPos;         // where the image is, in hwndLock
  BOOL bShown;         // the image is on the screen
  HBITMAP hbmSave;     // the screen under the image
};
W16Drag g_drag = {NULL, {0, 0}, NULL, {0, 0}, FALSE, NULL};

/// Gives a monochrome 8 by 8 checker pattern brush. The caller deletes
/// it.
HBRUSH CheckerBrush() {
  static const WORD aPattern[8] = {0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA};
  HBITMAP hbm = ::CreateBitmap(8, 8, 1, 1, aPattern);
  HBRUSH hbr = ::CreatePatternBrush(hbm);
  ::DeleteObject(hbm);
  return hbr;
}

/// Makes a bitmap that is compatible with the screen.
HBITMAP CreateScreenBitmap(int cx, int cy) {
  HDC hdcScreen = ::GetDC(NULL);
  HBITMAP hbm = ::CreateCompatibleBitmap(hdcScreen, cx > 0 ? cx : 1, cy > 0 ? cy : 1);
  ::ReleaseDC(NULL, hdcScreen);
  return hbm;
}

/// Makes the bitmaps of an image list hold nCapacity images, and copies
/// the images that are there. Gives FALSE when there is no memory.
BOOL SetCapacity(HIMAGELIST himl, int nCapacity) {
  if (nCapacity < 1) nCapacity = 1;
  if ((long)nCapacity * IL(himl)->cx > 0x7FFF) return FALSE;
  HBITMAP hbmImage = CreateScreenBitmap(nCapacity * IL(himl)->cx, IL(himl)->cy);
  HBITMAP hbmMask = NULL;
  if (IL(himl)->flags & ILC_MASK) hbmMask = ::CreateBitmap(nCapacity * IL(himl)->cx, IL(himl)->cy, 1, 1, NULL);
  if (hbmImage == NULL || ((IL(himl)->flags & ILC_MASK) && hbmMask == NULL)) {
    if (hbmImage != NULL) ::DeleteObject(hbmImage);
    if (hbmMask != NULL) ::DeleteObject(hbmMask);
    return FALSE;
  }
  HDC hdcScreen = ::GetDC(NULL);
  HDC hdcDst = ::CreateCompatibleDC(hdcScreen);
  HDC hdcSrc = ::CreateCompatibleDC(hdcScreen);
  int nKeep = IL(himl)->nCount < nCapacity ? IL(himl)->nCount : nCapacity;
  HGDIOBJ hOldDst = ::SelectObject(hdcDst, hbmImage);
  ::PatBlt(hdcDst, 0, 0, nCapacity * IL(himl)->cx, IL(himl)->cy, BLACKNESS);
  if (IL(himl)->hbmImage != NULL && nKeep > 0) {
    HGDIOBJ hOldSrc = ::SelectObject(hdcSrc, IL(himl)->hbmImage);
    ::BitBlt(hdcDst, 0, 0, nKeep * IL(himl)->cx, IL(himl)->cy, hdcSrc, 0, 0, SRCCOPY);
    ::SelectObject(hdcSrc, hOldSrc);
  }
  if (hbmMask != NULL) {
    ::SelectObject(hdcDst, hbmMask);
    ::PatBlt(hdcDst, 0, 0, nCapacity * IL(himl)->cx, IL(himl)->cy, BLACKNESS);
    if (IL(himl)->hbmMask != NULL && nKeep > 0) {
      HGDIOBJ hOldSrc = ::SelectObject(hdcSrc, IL(himl)->hbmMask);
      ::BitBlt(hdcDst, 0, 0, nKeep * IL(himl)->cx, IL(himl)->cy, hdcSrc, 0, 0, SRCCOPY);
      ::SelectObject(hdcSrc, hOldSrc);
    }
  }
  ::SelectObject(hdcDst, hOldDst);
  ::DeleteDC(hdcSrc);
  ::DeleteDC(hdcDst);
  ::ReleaseDC(NULL, hdcScreen);
  if (IL(himl)->hbmImage != NULL) ::DeleteObject(IL(himl)->hbmImage);
  if (IL(himl)->hbmMask != NULL) ::DeleteObject(IL(himl)->hbmMask);
  IL(himl)->hbmImage = hbmImage;
  IL(himl)->hbmMask = hbmMask;
  IL(himl)->nCapacity = nCapacity;
  if (IL(himl)->nCount > nCapacity) IL(himl)->nCount = nCapacity;
  return TRUE;
}

/// Makes room for nMore images after the last image. Gives FALSE when
/// there is no memory.
BOOL Reserve(HIMAGELIST himl, int nMore) {
  if (IL(himl)->nCount + nMore <= IL(himl)->nCapacity) return TRUE;
  int nGrow = IL(himl)->nGrow > 0 ? IL(himl)->nGrow : 4;
  int nNew = IL(himl)->nCapacity;
  while (nNew < IL(himl)->nCount + nMore) nNew += nGrow;
  return SetCapacity(himl, nNew);
}

/// Gives TRUE when an index names an image of the list.
BOOL IsImage(HIMAGELIST himl, int i) { return himl != NULL && i >= 0 && i < IL(himl)->nCount; }

/// Copies nImages images from a bitmap (and its mask) into the slots
/// from i. Without a mask bitmap, the images are opaque.
void PutImages(HIMAGELIST himl, int i, int nImages, HBITMAP hbmImage, HBITMAP hbmMask) {
  HDC hdcScreen = ::GetDC(NULL);
  HDC hdcDst = ::CreateCompatibleDC(hdcScreen);
  HDC hdcSrc = ::CreateCompatibleDC(hdcScreen);
  int x = i * IL(himl)->cx, cx = nImages * IL(himl)->cx;
  HGDIOBJ hOldDst = ::SelectObject(hdcDst, IL(himl)->hbmImage);
  HGDIOBJ hOldSrc = ::SelectObject(hdcSrc, hbmImage);
  ::BitBlt(hdcDst, x, 0, cx, IL(himl)->cy, hdcSrc, 0, 0, SRCCOPY);
  if (IL(himl)->hbmMask != NULL) {
    ::SelectObject(hdcDst, IL(himl)->hbmMask);
    if (hbmMask != NULL) {
      ::SelectObject(hdcSrc, hbmMask);
      ::BitBlt(hdcDst, x, 0, cx, IL(himl)->cy, hdcSrc, 0, 0, SRCCOPY);
      // The transparent pixels of the image become black.
      ::SelectObject(hdcDst, IL(himl)->hbmImage);
      COLORREF clrBk = ::SetBkColor(hdcDst, RGB(255, 255, 255));
      COLORREF clrText = ::SetTextColor(hdcDst, RGB(0, 0, 0));
      ::BitBlt(hdcDst, x, 0, cx, IL(himl)->cy, hdcSrc, 0, 0, 0x00220326L);  // DSna
      ::SetBkColor(hdcDst, clrBk);
      ::SetTextColor(hdcDst, clrText);
    } else {
      ::PatBlt(hdcDst, x, 0, cx, IL(himl)->cy, BLACKNESS);
    }
  }
  ::SelectObject(hdcSrc, hOldSrc);
  ::SelectObject(hdcDst, hOldDst);
  ::DeleteDC(hdcSrc);
  ::DeleteDC(hdcDst);
  ::ReleaseDC(NULL, hdcScreen);
}

/// Gives the number of images in a bitmap: its width divided by the
/// width of an image.
int ImagesIn(HIMAGELIST himl, HBITMAP hbm) {
  BITMAP bm;
  if (::GetObject(hbm, sizeof(bm), &bm) == 0 || IL(himl)->cx <= 0) return 0;
  return bm.bmWidth / IL(himl)->cx;
}

/// Draws one image of a list in a DC: the mask and the image, the
/// background color, a part of the image (cx, cy), and the blend of the
/// ILD_SELECTED and ILD_FOCUS styles.
BOOL DrawImage(HIMAGELIST himl, int i, HDC hdc, int x, int y, int cx, int cy, COLORREF clrBk,
               COLORREF clrFg, UINT fStyle) {
  if (!IsImage(himl, i) || hdc == NULL) return FALSE;
  if (cx <= 0 || cx > IL(himl)->cx) cx = IL(himl)->cx;
  if (cy <= 0 || cy > IL(himl)->cy) cy = IL(himl)->cy;
  int xSrc = i * IL(himl)->cx;
  HDC hdcImage = ::CreateCompatibleDC(hdc);
  HDC hdcMask = IL(himl)->hbmMask != NULL ? ::CreateCompatibleDC(hdc) : NULL;
  HGDIOBJ hOldImage = ::SelectObject(hdcImage, IL(himl)->hbmImage);
  HGDIOBJ hOldMask = hdcMask != NULL ? ::SelectObject(hdcMask, IL(himl)->hbmMask) : NULL;
  COLORREF clrOldBk = ::SetBkColor(hdc, RGB(255, 255, 255));
  COLORREF clrOldText = ::SetTextColor(hdc, RGB(0, 0, 0));
  if (clrBk == CLR_DEFAULT) clrBk = IL(himl)->clrBk;
  if (fStyle & ILD_MASK) {
    if (hdcMask != NULL)
      ::BitBlt(hdc, x, y, cx, cy, hdcMask, xSrc, 0, SRCCOPY);
    else
      ::PatBlt(hdc, x, y, cx, cy, BLACKNESS);
  } else if (hdcMask == NULL) {
    ::BitBlt(hdc, x, y, cx, cy, hdcImage, xSrc, 0, SRCCOPY);
  } else {
    if (!(fStyle & ILD_TRANSPARENT) && clrBk != CLR_NONE) {
      HBRUSH hbr = ::CreateSolidBrush(clrBk);
      RECT rc = {x, y, x + cx, y + cy};
      ::FillRect(hdc, &rc, hbr);
      ::DeleteObject(hbr);
    }
    ::BitBlt(hdc, x, y, cx, cy, hdcMask, xSrc, 0, SRCAND);
    ::BitBlt(hdc, x, y, cx, cy, hdcImage, xSrc, 0, SRCPAINT);
  }
  if ((fStyle & (ILD_BLEND25 | ILD_BLEND50)) && !(fStyle & ILD_MASK)) {
    // Half of the pixels of the image get the highlight color (or
    // clrFg): a mask with a checker pattern on top of the mask of the
    // image, then the brush where that mask is 0.
    HDC hdcBlend = ::CreateCompatibleDC(hdc);
    HBITMAP hbmBlend = ::CreateBitmap(cx, cy, 1, 1, NULL);
    HGDIOBJ hOldBlend = ::SelectObject(hdcBlend, hbmBlend);
    if (hdcMask != NULL)
      ::BitBlt(hdcBlend, 0, 0, cx, cy, hdcMask, xSrc, 0, SRCCOPY);
    else
      ::PatBlt(hdcBlend, 0, 0, cx, cy, BLACKNESS);
    HBRUSH hbrChecker = CheckerBrush();
    HGDIOBJ hOldBrush = ::SelectObject(hdcBlend, hbrChecker);
    ::PatBlt(hdcBlend, 0, 0, cx, cy, 0x00FA0089L);  // DPo
    ::SelectObject(hdcBlend, hOldBrush);
    ::DeleteObject(hbrChecker);
    COLORREF clr = clrFg == CLR_DEFAULT || clrFg == CLR_NONE ? ::GetSysColor(COLOR_HIGHLIGHT)
                                                             : clrFg;
    HBRUSH hbr = ::CreateSolidBrush(clr);
    hOldBrush = ::SelectObject(hdc, hbr);
    ::BitBlt(hdc, x, y, cx, cy, hdcBlend, 0, 0, 0x00B8074AL);  // PSDPxax
    ::SelectObject(hdc, hOldBrush);
    ::DeleteObject(hbr);
    ::SelectObject(hdcBlend, hOldBlend);
    ::DeleteObject(hbmBlend);
    ::DeleteDC(hdcBlend);
  }
  ::SetBkColor(hdc, clrOldBk);
  ::SetTextColor(hdc, clrOldText);
  if (hdcMask != NULL) {
    ::SelectObject(hdcMask, hOldMask);
    ::DeleteDC(hdcMask);
  }
  ::SelectObject(hdcImage, hOldImage);
  ::DeleteDC(hdcImage);
  // The overlay image goes on top, transparent.
  int nOverlay = (int)((fStyle & ILD_OVERLAYMASK) >> 8);
  if (nOverlay >= 1 && nOverlay <= 15 && !(fStyle & ILD_MASK)) {
    int iOverlay = IL(himl)->aOverlay[nOverlay - 1];
    if (iOverlay >= 0 && iOverlay != i)
      DrawImage(himl, iOverlay, hdc, x, y, cx, cy, CLR_NONE, CLR_NONE, ILD_TRANSPARENT);
  }
  return TRUE;
}

/// Draws the drag image at its position, and keeps the screen under it.
void DragShow() {
  if (g_drag.himl == NULL || g_drag.hwndLock == NULL || g_drag.bShown) return;
  HDC hdc = ::GetDCEx(g_drag.hwndLock, NULL, DCX_WINDOW | DCX_CACHE | DCX_LOCKWINDOWUPDATE);
  if (hdc == NULL) hdc = ::GetWindowDC(g_drag.hwndLock);
  int cx = IL(g_drag.himl)->cx, cy = IL(g_drag.himl)->cy;
  int x = g_drag.ptPos.x - g_drag.ptHotspot.x, y = g_drag.ptPos.y - g_drag.ptHotspot.y;
  if (g_drag.hbmSave == NULL) g_drag.hbmSave = ::CreateCompatibleBitmap(hdc, cx, cy);
  HDC hdcSave = ::CreateCompatibleDC(hdc);
  HGDIOBJ hOld = ::SelectObject(hdcSave, g_drag.hbmSave);
  ::BitBlt(hdcSave, 0, 0, cx, cy, hdc, x, y, SRCCOPY);
  ::SelectObject(hdcSave, hOld);
  ::DeleteDC(hdcSave);
  DrawImage(g_drag.himl, 0, hdc, x, y, 0, 0, CLR_NONE, CLR_NONE, ILD_TRANSPARENT);
  ::ReleaseDC(g_drag.hwndLock, hdc);
  g_drag.bShown = TRUE;
}

/// Puts back the screen under the drag image.
void DragHide() {
  if (!g_drag.bShown || g_drag.hwndLock == NULL || g_drag.hbmSave == NULL) return;
  HDC hdc = ::GetWindowDC(g_drag.hwndLock);
  int x = g_drag.ptPos.x - g_drag.ptHotspot.x, y = g_drag.ptPos.y - g_drag.ptHotspot.y;
  HDC hdcSave = ::CreateCompatibleDC(hdc);
  HGDIOBJ hOld = ::SelectObject(hdcSave, g_drag.hbmSave);
  ::BitBlt(hdc, x, y, IL(g_drag.himl)->cx, IL(g_drag.himl)->cy, hdcSave, 0, 0, SRCCOPY);
  ::SelectObject(hdcSave, hOld);
  ::DeleteDC(hdcSave);
  ::ReleaseDC(g_drag.hwndLock, hdc);
  g_drag.bShown = FALSE;
}

}  // namespace

/// Makes an empty image list for images of cx by cy pixels, with room
/// for cInitial images, that grows by cGrow images.
HIMAGELIST WINAPI ImageList_Create(int cx, int cy, UINT flags, int cInitial, int cGrow) {
  if (cx <= 0 || cy <= 0) return NULL;
  HIMAGELIST himl = (HIMAGELIST) new W16ImageList;
  IL(himl)->cx = cx;
  IL(himl)->cy = cy;
  IL(himl)->flags = flags;
  IL(himl)->nCount = 0;
  IL(himl)->nCapacity = 0;
  IL(himl)->nGrow = cGrow;
  IL(himl)->hbmImage = NULL;
  IL(himl)->hbmMask = NULL;
  IL(himl)->clrBk = CLR_NONE;
  for (int i = 0; i < 15; i++) IL(himl)->aOverlay[i] = -1;
  if (!SetCapacity(himl, cInitial)) {
    delete IL(himl);
    return NULL;
  }
  return himl;
}

/// Deletes an image list and its bitmaps.
BOOL WINAPI ImageList_Destroy(HIMAGELIST himl) {
  if (himl == NULL) return FALSE;
  if (IL(himl)->hbmImage != NULL) ::DeleteObject(IL(himl)->hbmImage);
  if (IL(himl)->hbmMask != NULL) ::DeleteObject(IL(himl)->hbmMask);
  delete IL(himl);
  return TRUE;
}

/// Gives the number of images in a list.
int WINAPI ImageList_GetImageCount(HIMAGELIST himl) { return himl != NULL ? IL(himl)->nCount : 0; }

/// Sets the number of images. New images are black and opaque.
BOOL WINAPI ImageList_SetImageCount(HIMAGELIST himl, UINT uNewCount) {
  if (himl == NULL) return FALSE;
  if ((int)uNewCount > IL(himl)->nCapacity && !SetCapacity(himl, (int)uNewCount)) return FALSE;
  IL(himl)->nCount = (int)uNewCount;
  return TRUE;
}

/// Adds the images of a bitmap, and their mask. Gives the index of the
/// first new image, or -1.
int WINAPI ImageList_Add(HIMAGELIST himl, HBITMAP hbmImage, HBITMAP hbmMask) {
  if (himl == NULL || hbmImage == NULL) return -1;
  int nImages = ImagesIn(himl, hbmImage);
  if (nImages <= 0 || !Reserve(himl, nImages)) return -1;
  int i = IL(himl)->nCount;
  PutImages(himl, i, nImages, hbmImage, hbmMask);
  IL(himl)->nCount += nImages;
  return i;
}

/// Adds the images of a bitmap. The pixels of the color crMask are
/// transparent. Gives the index of the first new image, or -1.
int WINAPI ImageList_AddMasked(HIMAGELIST himl, HBITMAP hbmImage, COLORREF crMask) {
  if (himl == NULL || hbmImage == NULL) return -1;
  if (IL(himl)->hbmMask == NULL || crMask == CLR_NONE) return ImageList_Add(himl, hbmImage, NULL);
  BITMAP bm;
  if (::GetObject(hbmImage, sizeof(bm), &bm) == 0) return -1;
  // The mask: 1 where the image has the color crMask.
  HBITMAP hbmMask = ::CreateBitmap(bm.bmWidth, bm.bmHeight, 1, 1, NULL);
  if (hbmMask == NULL) return -1;
  HDC hdcScreen = ::GetDC(NULL);
  HDC hdcImage = ::CreateCompatibleDC(hdcScreen);
  HDC hdcMask = ::CreateCompatibleDC(hdcScreen);
  HGDIOBJ hOldImage = ::SelectObject(hdcImage, hbmImage);
  HGDIOBJ hOldMask = ::SelectObject(hdcMask, hbmMask);
  COLORREF clrOld = ::SetBkColor(hdcImage, crMask);
  ::BitBlt(hdcMask, 0, 0, bm.bmWidth, bm.bmHeight, hdcImage, 0, 0, SRCCOPY);
  ::SetBkColor(hdcImage, clrOld);
  ::SelectObject(hdcMask, hOldMask);
  ::SelectObject(hdcImage, hOldImage);
  ::DeleteDC(hdcMask);
  ::DeleteDC(hdcImage);
  ::ReleaseDC(NULL, hdcScreen);
  int i = ImageList_Add(himl, hbmImage, hbmMask);
  ::DeleteObject(hbmMask);
  return i;
}

/// Puts the image and the mask of an icon in the slot i, or adds it when
/// i is -1. The icon is drawn on black and on white: the pixels that
/// differ are transparent. Gives the index of the image, or -1.
int WINAPI ImageList_ReplaceIcon(HIMAGELIST himl, int i, HICON hicon) {
  if (himl == NULL || hicon == NULL) return -1;
  if (i == -1) {
    if (!Reserve(himl, 1)) return -1;
    i = IL(himl)->nCount++;
  } else if (!IsImage(himl, i)) {
    return -1;
  }
  int cx = IL(himl)->cx, cy = IL(himl)->cy;
  HDC hdcScreen = ::GetDC(NULL);
  HDC hdcBlack = ::CreateCompatibleDC(hdcScreen);
  HDC hdcWhite = ::CreateCompatibleDC(hdcScreen);
  HBITMAP hbmBlack = ::CreateCompatibleBitmap(hdcScreen, cx, cy);
  HBITMAP hbmWhite = ::CreateCompatibleBitmap(hdcScreen, cx, cy);
  HBITMAP hbmMask = ::CreateBitmap(cx, cy, 1, 1, NULL);
  HGDIOBJ hOldBlack = ::SelectObject(hdcBlack, hbmBlack);
  HGDIOBJ hOldWhite = ::SelectObject(hdcWhite, hbmWhite);
  ::PatBlt(hdcBlack, 0, 0, cx, cy, BLACKNESS);
  ::PatBlt(hdcWhite, 0, 0, cx, cy, WHITENESS);
  ::DrawIcon(hdcBlack, 0, 0, hicon);
  ::DrawIcon(hdcWhite, 0, 0, hicon);
  HDC hdcMask = ::CreateCompatibleDC(hdcScreen);
  HGDIOBJ hOldMask = ::SelectObject(hdcMask, hbmMask);
  ::PatBlt(hdcMask, 0, 0, cx, cy, BLACKNESS);
  for (int y = 0; y < cy; y++)
    for (int x = 0; x < cx; x++)
      if (::GetPixel(hdcBlack, x, y) != ::GetPixel(hdcWhite, x, y))
        ::SetPixel(hdcMask, x, y, RGB(255, 255, 255));
  ::SelectObject(hdcMask, hOldMask);
  ::DeleteDC(hdcMask);
  ::SelectObject(hdcWhite, hOldWhite);
  ::SelectObject(hdcBlack, hOldBlack);
  ::DeleteDC(hdcWhite);
  ::DeleteDC(hdcBlack);
  ::ReleaseDC(NULL, hdcScreen);
  PutImages(himl, i, 1, hbmBlack, hbmMask);
  ::DeleteObject(hbmBlack);
  ::DeleteObject(hbmWhite);
  ::DeleteObject(hbmMask);
  return i;
}

/// Sets the background color that a draw fills the transparent pixels
/// with (CLR_NONE: they stay transparent). Gives the old color.
COLORREF WINAPI ImageList_SetBkColor(HIMAGELIST himl, COLORREF clrBk) {
  if (himl == NULL) return CLR_NONE;
  COLORREF clrOld = IL(himl)->clrBk;
  IL(himl)->clrBk = clrBk;
  return clrOld;
}

/// Gives the background color of a list.
COLORREF WINAPI ImageList_GetBkColor(HIMAGELIST himl) {
  return himl != NULL ? IL(himl)->clrBk : CLR_NONE;
}

/// Makes an image the overlay iOverlay (1 to 15), which INDEXTOOVERLAYMASK
/// draws on top of another image.
BOOL WINAPI ImageList_SetOverlayImage(HIMAGELIST himl, int iImage, int iOverlay) {
  if (himl == NULL || iOverlay < 1 || iOverlay > 15) return FALSE;
  IL(himl)->aOverlay[iOverlay - 1] = iImage;
  return TRUE;
}

/// Draws an image in a DC at (x, y).
BOOL WINAPI ImageList_Draw(HIMAGELIST himl, int i, HDC hdcDst, int x, int y, UINT fStyle) {
  return DrawImage(himl, i, hdcDst, x, y, 0, 0, CLR_DEFAULT, CLR_DEFAULT, fStyle);
}

/// Draws a part (dx by dy, 0 for all) of an image in a DC at (x, y),
/// with a background color and a blend color.
BOOL WINAPI ImageList_DrawEx(HIMAGELIST himl, int i, HDC hdcDst, int x, int y, int dx, int dy,
                             COLORREF rgbBk, COLORREF rgbFg, UINT fStyle) {
  return DrawImage(himl, i, hdcDst, x, y, dx, dy, rgbBk, rgbFg, fStyle);
}

/// Draws an image as an IMAGELISTDRAWPARAMS tells.
BOOL WINAPI ImageList_DrawIndirect(IMAGELISTDRAWPARAMS* pimldp) {
  if (pimldp == NULL) return FALSE;
  return DrawImage(pimldp->himl, pimldp->i, pimldp->hdcDst, pimldp->x, pimldp->y, pimldp->cx,
                   pimldp->cy, pimldp->rgbBk, pimldp->rgbFg, pimldp->fStyle);
}

/// Puts the image of a bitmap, and its mask, in the slot i.
BOOL WINAPI ImageList_Replace(HIMAGELIST himl, int i, HBITMAP hbmImage, HBITMAP hbmMask) {
  if (!IsImage(himl, i) || hbmImage == NULL) return FALSE;
  PutImages(himl, i, 1, hbmImage, hbmMask);
  return TRUE;
}

/// Removes the image i, or all the images when i is -1. The images after
/// it move one place to the left.
BOOL WINAPI ImageList_Remove(HIMAGELIST himl, int i) {
  if (himl == NULL) return FALSE;
  if (i == -1) {
    IL(himl)->nCount = 0;
    for (int n = 0; n < 15; n++) IL(himl)->aOverlay[n] = -1;
    return SetCapacity(himl, IL(himl)->nGrow > 0 ? IL(himl)->nGrow : 4);
  }
  if (!IsImage(himl, i)) return FALSE;
  int nAfter = IL(himl)->nCount - i - 1;
  if (nAfter > 0) {
    HDC hdcScreen = ::GetDC(NULL);
    HDC hdc = ::CreateCompatibleDC(hdcScreen);
    HGDIOBJ hOld = ::SelectObject(hdc, IL(himl)->hbmImage);
    ::BitBlt(hdc, i * IL(himl)->cx, 0, nAfter * IL(himl)->cx, IL(himl)->cy, hdc, (i + 1) * IL(himl)->cx, 0,
             SRCCOPY);
    if (IL(himl)->hbmMask != NULL) {
      ::SelectObject(hdc, IL(himl)->hbmMask);
      ::BitBlt(hdc, i * IL(himl)->cx, 0, nAfter * IL(himl)->cx, IL(himl)->cy, hdc, (i + 1) * IL(himl)->cx, 0,
               SRCCOPY);
    }
    ::SelectObject(hdc, hOld);
    ::DeleteDC(hdc);
    ::ReleaseDC(NULL, hdcScreen);
  }
  IL(himl)->nCount--;
  for (int n = 0; n < 15; n++) {
    if (IL(himl)->aOverlay[n] == i)
      IL(himl)->aOverlay[n] = -1;
    else if (IL(himl)->aOverlay[n] > i)
      IL(himl)->aOverlay[n]--;
  }
  return TRUE;
}

/// Makes an icon from an image and its mask. The caller destroys it.
HICON WINAPI ImageList_GetIcon(HIMAGELIST himl, int i, UINT flags) {
  if (!IsImage(himl, i)) return NULL;
  int cx = IL(himl)->cx, cy = IL(himl)->cy;
  HDC hdcScreen = ::GetDC(NULL);
  int nPlanes = ::GetDeviceCaps(hdcScreen, PLANES);
  int nBits = ::GetDeviceCaps(hdcScreen, BITSPIXEL);
  HDC hdc = ::CreateCompatibleDC(hdcScreen);
  HDC hdcSrc = ::CreateCompatibleDC(hdcScreen);
  HBITMAP hbmColor = ::CreateCompatibleBitmap(hdcScreen, cx, cy);
  HBITMAP hbmMask = ::CreateBitmap(cx, cy, 1, 1, NULL);
  HGDIOBJ hOld = ::SelectObject(hdc, hbmColor);
  HGDIOBJ hOldSrc = ::SelectObject(hdcSrc, IL(himl)->hbmImage);
  ::BitBlt(hdc, 0, 0, cx, cy, hdcSrc, i * cx, 0, SRCCOPY);
  ::SelectObject(hdc, hbmMask);
  if (IL(himl)->hbmMask != NULL) {
    ::SelectObject(hdcSrc, IL(himl)->hbmMask);
    ::BitBlt(hdc, 0, 0, cx, cy, hdcSrc, i * cx, 0, SRCCOPY);
  } else {
    ::PatBlt(hdc, 0, 0, cx, cy, BLACKNESS);
  }
  ::SelectObject(hdcSrc, hOldSrc);
  ::SelectObject(hdc, hOld);
  ::DeleteDC(hdcSrc);
  ::DeleteDC(hdc);
  ::ReleaseDC(NULL, hdcScreen);
  (void)flags;
  // CreateIcon takes the bits of the AND mask (1 bit for each pixel, rows
  // of whole words) and of the XOR image in the format of the screen.
  BITMAP bmMask, bmColor;
  ::GetObject(hbmMask, sizeof(bmMask), &bmMask);
  ::GetObject(hbmColor, sizeof(bmColor), &bmColor);
  DWORD cbMask = (DWORD)bmMask.bmWidthBytes * cy;
  DWORD cbColor = (DWORD)bmColor.bmWidthBytes * cy * bmColor.bmPlanes;
  HICON hicon = NULL;
  BYTE* pMask = new BYTE[(size_t)cbMask];
  BYTE* pColor = new BYTE[(size_t)cbColor];
  if (::GetBitmapBits(hbmMask, cbMask, pMask) && ::GetBitmapBits(hbmColor, cbColor, pColor))
    hicon = ::CreateIcon(AfxGetInstanceHandle(), cx, cy, (BYTE)nPlanes, (BYTE)nBits, pMask, pColor);
  delete[] pMask;
  delete[] pColor;
  ::DeleteObject(hbmMask);
  ::DeleteObject(hbmColor);
  return hicon;
}

/// Loads a bitmap resource into a new image list. cx is the width of an
/// image (0: the height of the bitmap). The pixels of crMask are
/// transparent, unless it is CLR_NONE. Only IMAGE_BITMAP works.
HIMAGELIST WINAPI ImageList_LoadImageA(HINSTANCE hi, LPCSTR lpbmp, int cx, int cGrow,
                                       COLORREF crMask, UINT uType, UINT uFlags) {
  (void)uFlags;
  if (uType != IMAGE_BITMAP) return NULL;
  HBITMAP hbm = ::LoadBitmap(hi, lpbmp);
  if (hbm == NULL) return NULL;
  BITMAP bm;
  ::GetObject(hbm, sizeof(bm), &bm);
  if (cx <= 0) cx = bm.bmHeight;
  int nImages = cx > 0 ? bm.bmWidth / cx : 0;
  HIMAGELIST himl = ImageList_Create(cx, bm.bmHeight, crMask != CLR_NONE ? ILC_MASK : ILC_COLOR,
                                     nImages, cGrow);
  if (himl != NULL) {
    int i = crMask != CLR_NONE ? ImageList_AddMasked(himl, hbm, crMask)
                               : ImageList_Add(himl, hbm, NULL);
    if (i < 0) {
      ImageList_Destroy(himl);
      himl = NULL;
    }
  }
  ::DeleteObject(hbm);
  return himl;
}

/// Copies the image iSrc of a list into the slot iDst of a list, or
/// swaps the two images with ILCF_SWAP. The two lists must be the same.
BOOL WINAPI ImageList_Copy(HIMAGELIST himlDst, int iDst, HIMAGELIST himlSrc, int iSrc,
                           UINT uFlags) {
  if (himlDst != himlSrc || !IsImage(himlSrc, iSrc) || !IsImage(himlDst, iDst)) return FALSE;
  if (iDst == iSrc) return TRUE;
  HIMAGELIST himl = himlSrc;
  HDC hdcScreen = ::GetDC(NULL);
  HDC hdc = ::CreateCompatibleDC(hdcScreen);
  HDC hdcTemp = ::CreateCompatibleDC(hdcScreen);
  HBITMAP hbmTemp = ::CreateCompatibleBitmap(hdcScreen, IL(himl)->cx, IL(himl)->cy);
  HBITMAP hbmTempMask = ::CreateBitmap(IL(himl)->cx, IL(himl)->cy, 1, 1, NULL);
  for (int nPass = 0; nPass < 2; nPass++) {
    HBITMAP hbm = nPass == 0 ? IL(himl)->hbmImage : IL(himl)->hbmMask;
    if (hbm == NULL) continue;
    HGDIOBJ hOld = ::SelectObject(hdc, hbm);
    HGDIOBJ hOldTemp = ::SelectObject(hdcTemp, nPass == 0 ? hbmTemp : hbmTempMask);
    int cx = IL(himl)->cx, cy = IL(himl)->cy;
    ::BitBlt(hdcTemp, 0, 0, cx, cy, hdc, iDst * cx, 0, SRCCOPY);
    ::BitBlt(hdc, iDst * cx, 0, cx, cy, hdc, iSrc * cx, 0, SRCCOPY);
    if (uFlags & ILCF_SWAP) ::BitBlt(hdc, iSrc * cx, 0, cx, cy, hdcTemp, 0, 0, SRCCOPY);
    ::SelectObject(hdcTemp, hOldTemp);
    ::SelectObject(hdc, hOld);
  }
  ::DeleteObject(hbmTemp);
  ::DeleteObject(hbmTempMask);
  ::DeleteDC(hdcTemp);
  ::DeleteDC(hdc);
  ::ReleaseDC(NULL, hdcScreen);
  return TRUE;
}

/// Starts a drag: a copy of the image iTrack is the drag image, with its
/// hot spot at (dxHotspot, dyHotspot).
BOOL WINAPI ImageList_BeginDrag(HIMAGELIST himlTrack, int iTrack, int dxHotspot, int dyHotspot) {
  if (!IsImage(himlTrack, iTrack)) return FALSE;
  ImageList_EndDrag();
  HIMAGELIST himl =
      ImageList_Create(IL(himlTrack)->cx, IL(himlTrack)->cy, IL(himlTrack)->flags | ILC_MASK, 1, 1);
  if (himl == NULL) return FALSE;
  IL(himl)->nCount = 1;
  HDC hdcScreen = ::GetDC(NULL);
  HDC hdcDst = ::CreateCompatibleDC(hdcScreen);
  HDC hdcSrc = ::CreateCompatibleDC(hdcScreen);
  HGDIOBJ hOldDst = ::SelectObject(hdcDst, IL(himl)->hbmImage);
  HGDIOBJ hOldSrc = ::SelectObject(hdcSrc, IL(himlTrack)->hbmImage);
  ::BitBlt(hdcDst, 0, 0, IL(himl)->cx, IL(himl)->cy, hdcSrc, iTrack * IL(himl)->cx, 0, SRCCOPY);
  ::SelectObject(hdcDst, IL(himl)->hbmMask);
  if (IL(himlTrack)->hbmMask != NULL) {
    ::SelectObject(hdcSrc, IL(himlTrack)->hbmMask);
    ::BitBlt(hdcDst, 0, 0, IL(himl)->cx, IL(himl)->cy, hdcSrc, iTrack * IL(himl)->cx, 0, SRCCOPY);
  } else {
    ::PatBlt(hdcDst, 0, 0, IL(himl)->cx, IL(himl)->cy, BLACKNESS);
  }
  ::SelectObject(hdcSrc, hOldSrc);
  ::SelectObject(hdcDst, hOldDst);
  ::DeleteDC(hdcSrc);
  ::DeleteDC(hdcDst);
  ::ReleaseDC(NULL, hdcScreen);
  g_drag.himl = himl;
  g_drag.ptHotspot.x = dxHotspot;
  g_drag.ptHotspot.y = dyHotspot;
  return TRUE;
}

/// Ends a drag and deletes the drag image.
void WINAPI ImageList_EndDrag(void) {
  DragHide();
  if (g_drag.himl != NULL) ImageList_Destroy(g_drag.himl);
  if (g_drag.hbmSave != NULL) ::DeleteObject(g_drag.hbmSave);
  g_drag.himl = NULL;
  g_drag.hbmSave = NULL;
  g_drag.hwndLock = NULL;
}

/// Shows the drag image at (x, y) of a window, in the coordinates of its
/// window rectangle (NULL: the desktop).
BOOL WINAPI ImageList_DragEnter(HWND hwndLock, int x, int y) {
  if (g_drag.himl == NULL) return FALSE;
  DragHide();
  g_drag.hwndLock = hwndLock != NULL ? hwndLock : ::GetDesktopWindow();
  g_drag.ptPos.x = x;
  g_drag.ptPos.y = y;
  DragShow();
  return TRUE;
}

/// Hides the drag image and unlocks the window.
BOOL WINAPI ImageList_DragLeave(HWND hwndLock) {
  (void)hwndLock;
  DragHide();
  g_drag.hwndLock = NULL;
  return TRUE;
}

/// Moves the drag image to (x, y).
BOOL WINAPI ImageList_DragMove(int x, int y) {
  if (g_drag.himl == NULL) return FALSE;
  BOOL bShown = g_drag.bShown;
  DragHide();
  g_drag.ptPos.x = x;
  g_drag.ptPos.y = y;
  if (bShown) DragShow();
  return TRUE;
}

/// Does nothing: the drag image does not change with the cursor.
BOOL WINAPI ImageList_SetDragCursorImage(HIMAGELIST himlDrag, int iDrag, int dxHotspot,
                                         int dyHotspot) {
  (void)himlDrag, (void)iDrag, (void)dxHotspot, (void)dyHotspot;
  return g_drag.himl != NULL;
}

/// Shows or hides the drag image while a drag goes on.
BOOL WINAPI ImageList_DragShowNolock(BOOL fShow) {
  if (g_drag.himl == NULL) return FALSE;
  if (fShow)
    DragShow();
  else
    DragHide();
  return TRUE;
}

/// Gives the drag image, its position and its hot spot.
HIMAGELIST WINAPI ImageList_GetDragImage(POINT* ppt, POINT* pptHotspot) {
  if (ppt != NULL) *ppt = g_drag.ptPos;
  if (pptHotspot != NULL) *pptHotspot = g_drag.ptHotspot;
  return g_drag.himl;
}

/// Fails: 16-bit Windows has no stream format for image lists.
HIMAGELIST WINAPI ImageList_Read(LPSTREAM pstm) {
  (void)pstm;
  return NULL;
}

/// Fails: 16-bit Windows has no stream format for image lists.
BOOL WINAPI ImageList_Write(HIMAGELIST himl, LPSTREAM pstm) {
  (void)himl, (void)pstm;
  return FALSE;
}

/// Gives the size of an image.
BOOL WINAPI ImageList_GetIconSize(HIMAGELIST himl, int* cx, int* cy) {
  if (himl == NULL) return FALSE;
  if (cx != NULL) *cx = IL(himl)->cx;
  if (cy != NULL) *cy = IL(himl)->cy;
  return TRUE;
}

/// Sets the size of an image and removes all the images.
BOOL WINAPI ImageList_SetIconSize(HIMAGELIST himl, int cx, int cy) {
  if (himl == NULL || cx <= 0 || cy <= 0) return FALSE;
  IL(himl)->cx = cx;
  IL(himl)->cy = cy;
  IL(himl)->nCount = 0;
  if (IL(himl)->hbmImage != NULL) ::DeleteObject(IL(himl)->hbmImage);
  if (IL(himl)->hbmMask != NULL) ::DeleteObject(IL(himl)->hbmMask);
  IL(himl)->hbmImage = IL(himl)->hbmMask = NULL;
  return SetCapacity(himl, IL(himl)->nGrow > 0 ? IL(himl)->nGrow : 4);
}

/// Gives the bitmaps of a list and the rectangle of the image i in them.
BOOL WINAPI ImageList_GetImageInfo(HIMAGELIST himl, int i, IMAGEINFO* pImageInfo) {
  if (!IsImage(himl, i) || pImageInfo == NULL) return FALSE;
  pImageInfo->hbmImage = IL(himl)->hbmImage;
  pImageInfo->hbmMask = IL(himl)->hbmMask;
  ::SetRect(&pImageInfo->rcImage, i * IL(himl)->cx, 0, (i + 1) * IL(himl)->cx, IL(himl)->cy);
  return TRUE;
}

/// Makes a list with one image: the image i1 of a list, and on top of it
/// the image i2 of another list at the offset (dx, dy).
HIMAGELIST WINAPI ImageList_Merge(HIMAGELIST himl1, int i1, HIMAGELIST himl2, int i2, int dx,
                                  int dy) {
  if (!IsImage(himl1, i1) || !IsImage(himl2, i2)) return NULL;
  int xOff1 = dx < 0 ? -dx : 0, yOff1 = dy < 0 ? -dy : 0;
  int xOff2 = dx > 0 ? dx : 0, yOff2 = dy > 0 ? dy : 0;
  int cx = IL(himl1)->cx + xOff1 > IL(himl2)->cx + xOff2 ? IL(himl1)->cx + xOff1 : IL(himl2)->cx + xOff2;
  int cy = IL(himl1)->cy + yOff1 > IL(himl2)->cy + yOff2 ? IL(himl1)->cy + yOff1 : IL(himl2)->cy + yOff2;
  HIMAGELIST himl = ImageList_Create(cx, cy, ILC_MASK, 1, 1);
  if (himl == NULL) return NULL;
  IL(himl)->nCount = 1;
  HDC hdcScreen = ::GetDC(NULL);
  HDC hdc = ::CreateCompatibleDC(hdcScreen);
  // The mask: transparent where neither image has a pixel.
  HGDIOBJ hOld = ::SelectObject(hdc, IL(himl)->hbmMask);
  ::PatBlt(hdc, 0, 0, cx, cy, WHITENESS);
  DrawImage(himl1, i1, hdc, xOff1, yOff1, 0, 0, CLR_NONE, CLR_NONE, ILD_MASK);
  HDC hdcSrc = ::CreateCompatibleDC(hdcScreen);
  if (IL(himl2)->hbmMask != NULL) {
    HGDIOBJ hOldSrc = ::SelectObject(hdcSrc, IL(himl2)->hbmMask);
    ::BitBlt(hdc, xOff2, yOff2, IL(himl2)->cx, IL(himl2)->cy, hdcSrc, i2 * IL(himl2)->cx, 0, SRCAND);
    ::SelectObject(hdcSrc, hOldSrc);
  } else {
    ::PatBlt(hdc, xOff2, yOff2, IL(himl2)->cx, IL(himl2)->cy, BLACKNESS);
  }
  ::DeleteDC(hdcSrc);
  // The image: black where transparent, then the two images.
  ::SelectObject(hdc, IL(himl)->hbmImage);
  ::PatBlt(hdc, 0, 0, cx, cy, BLACKNESS);
  DrawImage(himl1, i1, hdc, xOff1, yOff1, 0, 0, CLR_NONE, CLR_NONE, ILD_TRANSPARENT);
  DrawImage(himl2, i2, hdc, xOff2, yOff2, 0, 0, CLR_NONE, CLR_NONE, ILD_TRANSPARENT);
  ::SelectObject(hdc, hOld);
  ::DeleteDC(hdc);
  ::ReleaseDC(NULL, hdcScreen);
  return himl;
}

/// Makes a copy of an image list.
HIMAGELIST WINAPI ImageList_Duplicate(HIMAGELIST himl) {
  if (himl == NULL) return NULL;
  HIMAGELIST himlNew = ImageList_Create(IL(himl)->cx, IL(himl)->cy, IL(himl)->flags, IL(himl)->nCapacity,
                                        IL(himl)->nGrow);
  if (himlNew == NULL) return NULL;
  if (IL(himl)->nCount > 0) PutImages(himlNew, 0, IL(himl)->nCount, IL(himl)->hbmImage, IL(himl)->hbmMask);
  IL(himlNew)->nCount = IL(himl)->nCount;
  IL(himlNew)->clrBk = IL(himl)->clrBk;
  for (int i = 0; i < 15; i++) IL(himlNew)->aOverlay[i] = IL(himl)->aOverlay[i];
  return himlNew;
}

#endif  // _AFX_WIN16
