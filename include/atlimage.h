// atlimage.h - CImage: a bitmap (a DIB section when the class makes it)
// that loads and saves BMP, GIF, JPEG, PNG and TIFF files through GDI+,
// and draws itself with BitBlt, StretchBlt, AlphaBlend and TransparentBlt.
//
// GDI+ starts the first time an image is loaded or saved and stops when
// the program ends (or at ReleaseGDIPlus). As with Visual C++, an image
// loaded from a file with an alpha channel keeps it unpremultiplied
// (32-bit ARGB), and Draw blends it with AlphaBlend.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLIMAGE_H__
#define __ATLIMAGE_H__

#include <atlbase.h>
#include <atlstr.h>
#include <atlsimpcoll.h>
#include <shlwapi.h>
#include <gdiplus.h>

#ifdef _WIN32
// GDI+ itself, AlphaBlend and TransparentBlt (msimg32), and the path
// functions; written directly, since clang ignores #pragma comment(lib)
// for MinGW targets.
__asm__(".section .drectve,\"yn\"\n"
        ".ascii \" -defaultlib:gdiplus -defaultlib:msimg32 -defaultlib:shlwapi\"\n"
        ".text");
#endif

namespace ATL {

// GDI+ for every image in the program, started on first use.
class CInitGDIPlus {
public:
  CInitGDIPlus() : m_token(0), m_bStarted(false) { ::InitializeCriticalSection(&m_cs); }
  ~CInitGDIPlus() {
    ReleaseGDIPlus();
    ::DeleteCriticalSection(&m_cs);
  }
  bool Init() {
    ::EnterCriticalSection(&m_cs);
    if (!m_bStarted) {
      Gdiplus::GdiplusStartupInput input;
      m_bStarted = Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok;
    }
    bool b = m_bStarted;
    ::LeaveCriticalSection(&m_cs);
    return b;
  }
  void ReleaseGDIPlus() {
    ::EnterCriticalSection(&m_cs);
    if (m_bStarted) {
      Gdiplus::GdiplusShutdown(m_token);
      m_bStarted = false;
    }
    ::LeaveCriticalSection(&m_cs);
  }

private:
  ULONG_PTR m_token;
  bool m_bStarted;
  CRITICAL_SECTION m_cs;
};

inline CInitGDIPlus& _AtlGdiPlus() {
  static CInitGDIPlus s;
  return s;
}

class CImage {
public:
  enum DIBOrientation { DIBOR_DEFAULT, DIBOR_BOTTOMUP, DIBOR_TOPDOWN };
  static const DWORD createAlphaChannel = 0x01;
  static const DWORD excludeGIF = 0x01;
  static const DWORD excludeBMP = 0x02;
  static const DWORD excludeEMF = 0x04;
  static const DWORD excludeWMF = 0x08;
  static const DWORD excludeJPEG = 0x10;
  static const DWORD excludePNG = 0x20;
  static const DWORD excludeTIFF = 0x40;
  static const DWORD excludeIcon = 0x80;
  static const DWORD excludeOther = 0x80000000;
  static const DWORD excludeDefaultLoad = 0;
  static const DWORD excludeDefaultSave = excludeIcon | excludeEMF | excludeWMF;
  static const DWORD excludeValid = 0x800000ff;

  CImage() { Clear(); }
  ~CImage() { Destroy(); }
  CImage(const CImage&) = delete;
  CImage& operator=(const CImage&) = delete;

  operator HBITMAP() const { return m_hBitmap; }

  // ---- making, taking and freeing the bitmap

  BOOL Create(int nWidth, int nHeight, int nBPP, DWORD dwFlags = 0) {
    return CreateEx(nWidth, nHeight, nBPP, BI_RGB, NULL, dwFlags);
  }
  BOOL CreateEx(int nWidth, int nHeight, int nBPP, DWORD eCompression,
                const DWORD* pdwBitfields = NULL, DWORD dwFlags = 0) {
    if (nWidth <= 0 || nHeight == 0) return FALSE;
    if (eCompression == BI_BITFIELDS && (nBPP != 16 && nBPP != 32)) return FALSE;
    if (eCompression != BI_RGB && eCompression != BI_BITFIELDS) return FALSE;
    if (nBPP != 1 && nBPP != 4 && nBPP != 8 && nBPP != 16 && nBPP != 24 && nBPP != 32)
      return FALSE;
    struct {
      BITMAPINFOHEADER h;
      RGBQUAD colors[256];
    } bmi;
    ::ZeroMemory(&bmi, sizeof bmi);
    bmi.h.biSize = sizeof(BITMAPINFOHEADER);
    bmi.h.biWidth = nWidth;
    bmi.h.biHeight = nHeight;  // negative: top-down
    bmi.h.biPlanes = 1;
    bmi.h.biBitCount = (WORD)nBPP;
    bmi.h.biCompression = eCompression;
    if (eCompression == BI_BITFIELDS) {
      DWORD* masks = (DWORD*)bmi.colors;
      if (pdwBitfields != NULL) {
        masks[0] = pdwBitfields[0];
        masks[1] = pdwBitfields[1];
        masks[2] = pdwBitfields[2];
      } else if (nBPP == 16) {
        masks[0] = 0xF800, masks[1] = 0x07E0, masks[2] = 0x001F;  // 5-6-5
      } else {
        masks[0] = 0xFF0000, masks[1] = 0x00FF00, masks[2] = 0x0000FF;
      }
    } else if (nBPP <= 8) {
      _DefaultColorTable(nBPP, bmi.colors);
      bmi.h.biClrUsed = 1u << nBPP;
    }
    void* pBits = NULL;
    HBITMAP hbm = ::CreateDIBSection(NULL, (BITMAPINFO*)&bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (hbm == NULL) return FALSE;
    Attach(hbm, nHeight < 0 ? DIBOR_TOPDOWN : DIBOR_BOTTOMUP);
    if ((dwFlags & createAlphaChannel) && nBPP == 32) m_bHasAlphaChannel = true;
    return TRUE;
  }

  void Attach(HBITMAP hBitmap, DIBOrientation eOrientation = DIBOR_DEFAULT) {
    ATLASSERT(m_hBitmap == NULL);
    m_hBitmap = hBitmap;
    _UpdateBitmapInfo(eOrientation);
  }
  HBITMAP Detach() {
    HBITMAP h = m_hBitmap;
    ATLASSERT(m_nDCRefCount == 0);
    Clear();
    return h;
  }
  void Destroy() {
    if (m_hBitmap == NULL) return;
    while (m_nDCRefCount > 0) ReleaseDC();
    ::DeleteObject(Detach());
  }

  // ---- what it is

  bool IsNull() const { return m_hBitmap == NULL; }
  bool IsDIBSection() const { return m_bIsDIBSection; }
  bool IsIndexed() const { return m_nBPP <= 8; }
  int GetWidth() const { return m_nWidth; }
  int GetHeight() const { return m_nHeight; }
  int GetBPP() const { return m_nBPP; }
  int GetPitch() const { return m_nPitch; }
  void* GetBits() { return m_pBits; }
  const void* GetBits() const { return m_pBits; }
  void* GetPixelAddress(int x, int y) {
    return (BYTE*)m_pBits + y * m_nPitch + (x * m_nBPP) / 8;
  }
  const void* GetPixelAddress(int x, int y) const {
    return (const BYTE*)m_pBits + y * m_nPitch + (x * m_nBPP) / 8;
  }
  int GetMaxColorTableEntries() const { return IsIndexed() ? 1 << m_nBPP : 0; }
  static BOOL IsTransparencySupported() { return TRUE; }
  void SetHasAlphaChannel(bool bHasAlphaChannel) { m_bHasAlphaChannel = bHasAlphaChannel; }

  void GetColorTable(UINT iFirstColor, UINT nColors, RGBQUAD* prgbColors) const {
    HDC hdc = GetDC();
    ::GetDIBColorTable(hdc, iFirstColor, nColors, prgbColors);
    ReleaseDC();
  }
  void SetColorTable(UINT iFirstColor, UINT nColors, const RGBQUAD* prgbColors) {
    HDC hdc = GetDC();
    ::SetDIBColorTable(hdc, iFirstColor, nColors, prgbColors);
    ReleaseDC();
  }

  // The transparent color: an index into the color table of an indexed
  // image, and an RGB value (a COLORREF) for any other; -1 for none.
  LONG GetTransparentColor() const { return m_iTransparentColor; }
  LONG SetTransparentColor(LONG iTransparentColor) {
    LONG old = m_iTransparentColor;
    m_iTransparentColor = iTransparentColor;
    return old;
  }

  // ---- pixels

  COLORREF GetPixel(int x, int y) const {
    HDC hdc = GetDC();
    COLORREF c = ::GetPixel(hdc, x, y);
    ReleaseDC();
    return c;
  }
  void SetPixel(int x, int y, COLORREF color) {
    HDC hdc = GetDC();
    ::SetPixelV(hdc, x, y, color);
    ReleaseDC();
  }
  void SetPixelRGB(int x, int y, BYTE r, BYTE g, BYTE b) { SetPixel(x, y, RGB(r, g, b)); }
  void SetPixelIndexed(int x, int y, int iIndex) {
    ATLASSERT(IsIndexed() && m_bIsDIBSection);
    ::GdiFlush();
    BYTE* p = (BYTE*)GetPixelAddress(x, y);
    switch (m_nBPP) {
    case 8: *p = (BYTE)iIndex; break;
    case 4: {
      int shift = (x & 1) ? 0 : 4;
      *p = (BYTE)((*p & ~(0x0F << shift)) | ((iIndex & 0x0F) << shift));
      break;
    }
    case 1: {
      int bit = 7 - (x & 7);
      *p = (BYTE)((*p & ~(1 << bit)) | ((iIndex & 1) << bit));
      break;
    }
    }
  }

  // ---- the device context: one per image, counted

  HDC GetDC() const {
    if (m_hBitmap == NULL) return NULL;
    if (m_nDCRefCount++ == 0) {
      m_hDC = ::CreateCompatibleDC(NULL);
      m_hOldBitmap = (HBITMAP)::SelectObject(m_hDC, m_hBitmap);
    }
    return m_hDC;
  }
  void ReleaseDC() const {
    ATLASSERT(m_nDCRefCount > 0);
    if (m_nDCRefCount <= 0 || --m_nDCRefCount > 0) return;
    ::SelectObject(m_hDC, m_hOldBitmap);
    ::DeleteDC(m_hDC);
    m_hDC = NULL;
    m_hOldBitmap = NULL;
  }

  // ---- drawing

  BOOL BitBlt(HDC hDestDC, int xDest, int yDest, DWORD dwROP = SRCCOPY) const {
    return BitBlt(hDestDC, xDest, yDest, m_nWidth, m_nHeight, 0, 0, dwROP);
  }
  BOOL BitBlt(HDC hDestDC, const POINT& pointDest, DWORD dwROP = SRCCOPY) const {
    return BitBlt(hDestDC, pointDest.x, pointDest.y, dwROP);
  }
  BOOL BitBlt(HDC hDestDC, int xDest, int yDest, int nDestWidth, int nDestHeight, int xSrc,
              int ySrc, DWORD dwROP = SRCCOPY) const {
    HDC hdc = GetDC();
    BOOL b = ::BitBlt(hDestDC, xDest, yDest, nDestWidth, nDestHeight, hdc, xSrc, ySrc, dwROP);
    ReleaseDC();
    return b;
  }
  BOOL BitBlt(HDC hDestDC, const RECT& rectDest, const POINT& pointSrc,
              DWORD dwROP = SRCCOPY) const {
    return BitBlt(hDestDC, rectDest.left, rectDest.top, rectDest.right - rectDest.left,
                  rectDest.bottom - rectDest.top, pointSrc.x, pointSrc.y, dwROP);
  }

  BOOL StretchBlt(HDC hDestDC, int xDest, int yDest, int nDestWidth, int nDestHeight,
                  DWORD dwROP = SRCCOPY) const {
    return StretchBlt(hDestDC, xDest, yDest, nDestWidth, nDestHeight, 0, 0, m_nWidth, m_nHeight,
                      dwROP);
  }
  BOOL StretchBlt(HDC hDestDC, const RECT& rectDest, DWORD dwROP = SRCCOPY) const {
    return StretchBlt(hDestDC, rectDest.left, rectDest.top, rectDest.right - rectDest.left,
                      rectDest.bottom - rectDest.top, dwROP);
  }
  BOOL StretchBlt(HDC hDestDC, int xDest, int yDest, int nDestWidth, int nDestHeight, int xSrc,
                  int ySrc, int nSrcWidth, int nSrcHeight, DWORD dwROP = SRCCOPY) const {
    HDC hdc = GetDC();
    int oldMode = ::SetStretchBltMode(hDestDC, COLORONCOLOR);
    BOOL b = ::StretchBlt(hDestDC, xDest, yDest, nDestWidth, nDestHeight, hdc, xSrc, ySrc,
                          nSrcWidth, nSrcHeight, dwROP);
    ::SetStretchBltMode(hDestDC, oldMode);
    ReleaseDC();
    return b;
  }
  BOOL StretchBlt(HDC hDestDC, const RECT& rectDest, const RECT& rectSrc,
                  DWORD dwROP = SRCCOPY) const {
    return StretchBlt(hDestDC, rectDest.left, rectDest.top, rectDest.right - rectDest.left,
                      rectDest.bottom - rectDest.top, rectSrc.left, rectSrc.top,
                      rectSrc.right - rectSrc.left, rectSrc.bottom - rectSrc.top, dwROP);
  }

  BOOL AlphaBlend(HDC hDestDC, int xDest, int yDest, BYTE bSrcAlpha = 0xff,
                  BYTE bBlendOp = AC_SRC_OVER) const {
    return AlphaBlend(hDestDC, xDest, yDest, m_nWidth, m_nHeight, 0, 0, m_nWidth, m_nHeight,
                      bSrcAlpha, bBlendOp);
  }
  BOOL AlphaBlend(HDC hDestDC, const POINT& pointDest, BYTE bSrcAlpha = 0xff,
                  BYTE bBlendOp = AC_SRC_OVER) const {
    return AlphaBlend(hDestDC, pointDest.x, pointDest.y, bSrcAlpha, bBlendOp);
  }
  BOOL AlphaBlend(HDC hDestDC, int xDest, int yDest, int nDestWidth, int nDestHeight, int xSrc,
                  int ySrc, int nSrcWidth, int nSrcHeight, BYTE bSrcAlpha = 0xff,
                  BYTE bBlendOp = AC_SRC_OVER) const {
    BLENDFUNCTION bf;
    bf.BlendOp = bBlendOp;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = bSrcAlpha;
    bf.AlphaFormat = m_bHasAlphaChannel ? AC_SRC_ALPHA : 0;
    HDC hdc = GetDC();
    BOOL b = ::AlphaBlend(hDestDC, xDest, yDest, nDestWidth, nDestHeight, hdc, xSrc, ySrc,
                          nSrcWidth, nSrcHeight, bf);
    ReleaseDC();
    return b;
  }
  BOOL AlphaBlend(HDC hDestDC, const RECT& rectDest, const RECT& rectSrc, BYTE bSrcAlpha = 0xff,
                  BYTE bBlendOp = AC_SRC_OVER) const {
    return AlphaBlend(hDestDC, rectDest.left, rectDest.top, rectDest.right - rectDest.left,
                      rectDest.bottom - rectDest.top, rectSrc.left, rectSrc.top,
                      rectSrc.right - rectSrc.left, rectSrc.bottom - rectSrc.top, bSrcAlpha,
                      bBlendOp);
  }

  BOOL TransparentBlt(HDC hDestDC, int xDest, int yDest, int nDestWidth, int nDestHeight,
                      UINT crTransparent = CLR_INVALID) const {
    return TransparentBlt(hDestDC, xDest, yDest, nDestWidth, nDestHeight, 0, 0, m_nWidth,
                          m_nHeight, crTransparent);
  }
  BOOL TransparentBlt(HDC hDestDC, const RECT& rectDest, UINT crTransparent = CLR_INVALID) const {
    return TransparentBlt(hDestDC, rectDest.left, rectDest.top, rectDest.right - rectDest.left,
                          rectDest.bottom - rectDest.top, crTransparent);
  }
  BOOL TransparentBlt(HDC hDestDC, int xDest, int yDest, int nDestWidth, int nDestHeight,
                      int xSrc, int ySrc, int nSrcWidth, int nSrcHeight,
                      UINT crTransparent = CLR_INVALID) const {
    if (crTransparent == CLR_INVALID) crTransparent = _GetTransparentRGB();
    HDC hdc = GetDC();
    BOOL b = ::TransparentBlt(hDestDC, xDest, yDest, nDestWidth, nDestHeight, hdc, xSrc, ySrc,
                              nSrcWidth, nSrcHeight, crTransparent);
    ReleaseDC();
    return b;
  }
  BOOL TransparentBlt(HDC hDestDC, const RECT& rectDest, const RECT& rectSrc,
                      UINT crTransparent = CLR_INVALID) const {
    return TransparentBlt(hDestDC, rectDest.left, rectDest.top, rectDest.right - rectDest.left,
                          rectDest.bottom - rectDest.top, rectSrc.left, rectSrc.top,
                          rectSrc.right - rectSrc.left, rectSrc.bottom - rectSrc.top,
                          crTransparent);
  }

  BOOL MaskBlt(HDC hDestDC, int xDest, int yDest, int nDestWidth, int nDestHeight, int xSrc,
               int ySrc, HBITMAP hbmMask, int xMask, int yMask,
               DWORD dwROP = SRCCOPY) const {
    HDC hdc = GetDC();
    BOOL b = ::MaskBlt(hDestDC, xDest, yDest, nDestWidth, nDestHeight, hdc, xSrc, ySrc, hbmMask,
                       xMask, yMask, dwROP);
    ReleaseDC();
    return b;
  }
  BOOL MaskBlt(HDC hDestDC, int xDest, int yDest, HBITMAP hbmMask,
               DWORD dwROP = SRCCOPY) const {
    return MaskBlt(hDestDC, xDest, yDest, m_nWidth, m_nHeight, 0, 0, hbmMask, 0, 0, dwROP);
  }
  BOOL PlgBlt(HDC hDestDC, const POINT* pPoints, HBITMAP hbmMask = NULL) const {
    return PlgBlt(hDestDC, pPoints, 0, 0, m_nWidth, m_nHeight, hbmMask, 0, 0);
  }
  BOOL PlgBlt(HDC hDestDC, const POINT* pPoints, int xSrc, int ySrc, int nSrcWidth,
              int nSrcHeight, HBITMAP hbmMask = NULL, int xMask = 0, int yMask = 0) const {
    HDC hdc = GetDC();
    BOOL b = ::PlgBlt(hDestDC, pPoints, hdc, xSrc, ySrc, nSrcWidth, nSrcHeight, hbmMask, xMask,
                      yMask);
    ReleaseDC();
    return b;
  }

  // Draw: AlphaBlend for an image with an alpha channel, TransparentBlt
  // for one with a transparent color, StretchBlt otherwise.
  BOOL Draw(HDC hDestDC, int xDest, int yDest, int nDestWidth, int nDestHeight, int xSrc,
            int ySrc, int nSrcWidth, int nSrcHeight) const {
    if (m_bHasAlphaChannel)
      return AlphaBlend(hDestDC, xDest, yDest, nDestWidth, nDestHeight, xSrc, ySrc, nSrcWidth,
                        nSrcHeight);
    if (m_iTransparentColor != -1)
      return TransparentBlt(hDestDC, xDest, yDest, nDestWidth, nDestHeight, xSrc, ySrc,
                            nSrcWidth, nSrcHeight);
    return StretchBlt(hDestDC, xDest, yDest, nDestWidth, nDestHeight, xSrc, ySrc, nSrcWidth,
                      nSrcHeight);
  }
  BOOL Draw(HDC hDestDC, const RECT& rectDest, const RECT& rectSrc) const {
    return Draw(hDestDC, rectDest.left, rectDest.top, rectDest.right - rectDest.left,
                rectDest.bottom - rectDest.top, rectSrc.left, rectSrc.top,
                rectSrc.right - rectSrc.left, rectSrc.bottom - rectSrc.top);
  }
  BOOL Draw(HDC hDestDC, int xDest, int yDest) const {
    return Draw(hDestDC, xDest, yDest, m_nWidth, m_nHeight, 0, 0, m_nWidth, m_nHeight);
  }
  BOOL Draw(HDC hDestDC, const POINT& pointDest) const {
    return Draw(hDestDC, pointDest.x, pointDest.y);
  }
  BOOL Draw(HDC hDestDC, int xDest, int yDest, int nDestWidth, int nDestHeight) const {
    return Draw(hDestDC, xDest, yDest, nDestWidth, nDestHeight, 0, 0, m_nWidth, m_nHeight);
  }
  BOOL Draw(HDC hDestDC, const RECT& rectDest) const {
    return Draw(hDestDC, rectDest.left, rectDest.top, rectDest.right - rectDest.left,
                rectDest.bottom - rectDest.top);
  }

  // ---- files and resources

  HRESULT Load(LPCTSTR pszFileName) {
    if (!_AtlGdiPlus().Init()) return E_FAIL;
    Gdiplus::GpBitmap* pBitmap = NULL;
    CStringW name(pszFileName);
    if (Gdiplus::DllExports::GdipCreateBitmapFromFile(name, &pBitmap) != Gdiplus::Ok)
      return E_FAIL;
    HRESULT hr = _CreateFromGdiplus(pBitmap);
    Gdiplus::DllExports::GdipDisposeImage(pBitmap);
    return hr;
  }
  HRESULT Load(IStream* pStream) {
    if (pStream == NULL) return E_POINTER;
    if (!_AtlGdiPlus().Init()) return E_FAIL;
    Gdiplus::GpBitmap* pBitmap = NULL;
    if (Gdiplus::DllExports::GdipCreateBitmapFromStream(pStream, &pBitmap) != Gdiplus::Ok)
      return E_FAIL;
    HRESULT hr = _CreateFromGdiplus(pBitmap);
    Gdiplus::DllExports::GdipDisposeImage(pBitmap);
    return hr;
  }
  void LoadFromResource(HINSTANCE hInstance, LPCTSTR pszResourceName) {
    HBITMAP hbm = (HBITMAP)::LoadImage(hInstance, pszResourceName, IMAGE_BITMAP, 0, 0,
                                       LR_CREATEDIBSECTION);
    if (hbm != NULL) Attach(hbm);
  }
  void LoadFromResource(HINSTANCE hInstance, UINT nIDResource) {
    LoadFromResource(hInstance, MAKEINTRESOURCE(nIDResource));
  }

  // Save: in the format guidFileType names (Gdiplus::ImageFormatPNG and
  // the others), or for GUID_NULL the one the file name's extension says.
  HRESULT Save(LPCTSTR pszFileName, REFGUID guidFileType = GUID_NULL) const {
    if (m_hBitmap == NULL) return E_FAIL;
    if (!_AtlGdiPlus().Init()) return E_FAIL;
    CLSID clsid;
    bool found = guidFileType == GUID_NULL ? _EncoderForFile(pszFileName, clsid)
                                           : _EncoderForFormat(guidFileType, clsid);
    if (!found) return E_FAIL;
    Gdiplus::GpBitmap* pBitmap = _MakeGdiplusBitmap();
    if (pBitmap == NULL) return E_FAIL;
    CStringW name(pszFileName);
    Gdiplus::Status st = Gdiplus::DllExports::GdipSaveImageToFile(pBitmap, name, &clsid, NULL);
    Gdiplus::DllExports::GdipDisposeImage(pBitmap);
    return st == Gdiplus::Ok ? S_OK : E_FAIL;
  }
  HRESULT Save(IStream* pStream, REFGUID guidFileType) const {
    if (pStream == NULL) return E_POINTER;
    if (m_hBitmap == NULL) return E_FAIL;
    if (!_AtlGdiPlus().Init()) return E_FAIL;
    CLSID clsid;
    if (!_EncoderForFormat(guidFileType, clsid)) return E_FAIL;
    Gdiplus::GpBitmap* pBitmap = _MakeGdiplusBitmap();
    if (pBitmap == NULL) return E_FAIL;
    Gdiplus::Status st =
        Gdiplus::DllExports::GdipSaveImageToStream(pBitmap, pStream, &clsid, NULL);
    Gdiplus::DllExports::GdipDisposeImage(pBitmap);
    return st == Gdiplus::Ok ? S_OK : E_FAIL;
  }

  // Filter strings for the file dialogs: "PNG (*.PNG)|*.PNG|" for each
  // format GDI+ reads (or writes), after an "all image files" entry when
  // pszAllFilesDescription is given, with the formats' GUIDs in the same
  // order (GUID_NULL for the "all" entry).
  static HRESULT GetImporterFilterString(CString& strImporters,
                                         CSimpleArray<GUID>& aguidFileTypes,
                                         LPCTSTR pszAllFilesDescription = NULL,
                                         DWORD dwExclude = excludeDefaultLoad,
                                         TCHAR chSeparator = TEXT('|')) {
    return _FilterString(false, strImporters, aguidFileTypes, pszAllFilesDescription,
                         dwExclude, chSeparator);
  }
  static HRESULT GetExporterFilterString(CString& strExporters,
                                         CSimpleArray<GUID>& aguidFileTypes,
                                         LPCTSTR pszAllFilesDescription = NULL,
                                         DWORD dwExclude = excludeDefaultSave,
                                         TCHAR chSeparator = TEXT('|')) {
    return _FilterString(true, strExporters, aguidFileTypes, pszAllFilesDescription, dwExclude,
                         chSeparator);
  }

  static void ReleaseGDIPlus() { _AtlGdiPlus().ReleaseGDIPlus(); }

private:
  HBITMAP m_hBitmap;
  void* m_pBits;
  int m_nWidth;
  int m_nHeight;
  int m_nPitch;
  int m_nBPP;
  bool m_bIsDIBSection;
  bool m_bHasAlphaChannel;
  LONG m_iTransparentColor;
  mutable HDC m_hDC;
  mutable HBITMAP m_hOldBitmap;
  mutable int m_nDCRefCount;

  void Clear() {
    m_hBitmap = NULL;
    m_pBits = NULL;
    m_nWidth = m_nHeight = m_nPitch = m_nBPP = 0;
    m_bIsDIBSection = false;
    m_bHasAlphaChannel = false;
    m_iTransparentColor = -1;
    m_hDC = NULL;
    m_hOldBitmap = NULL;
    m_nDCRefCount = 0;
  }

  void _UpdateBitmapInfo(DIBOrientation eOrientation) {
    DIBSECTION ds;
    if (::GetObject(m_hBitmap, sizeof ds, &ds) == sizeof ds) {
      m_bIsDIBSection = true;
      m_nWidth = ds.dsBmih.biWidth;
      m_nHeight = ds.dsBmih.biHeight < 0 ? -ds.dsBmih.biHeight : ds.dsBmih.biHeight;
      m_nBPP = ds.dsBmih.biBitCount;
      int pitch = ((m_nWidth * m_nBPP + 31) / 32) * 4;
      bool bottomUp = eOrientation == DIBOR_DEFAULT ? ds.dsBmih.biHeight > 0
                                                    : eOrientation == DIBOR_BOTTOMUP;
      if (bottomUp) {
        // GetBits is the top row as displayed, and the rows go back.
        m_pBits = (BYTE*)ds.dsBm.bmBits + pitch * (m_nHeight - 1);
        m_nPitch = -pitch;
      } else {
        m_pBits = ds.dsBm.bmBits;
        m_nPitch = pitch;
      }
    } else {
      BITMAP bm;
      ::GetObject(m_hBitmap, sizeof bm, &bm);
      m_bIsDIBSection = false;
      m_nWidth = bm.bmWidth;
      m_nHeight = bm.bmHeight;
      m_nBPP = bm.bmBitsPixel * bm.bmPlanes;
      m_nPitch = bm.bmWidthBytes;
      m_pBits = NULL;
    }
  }

  static void _DefaultColorTable(int nBPP, RGBQUAD* p) {
    if (nBPP == 1) {
      p[1].rgbRed = p[1].rgbGreen = p[1].rgbBlue = 0xFF;
      return;
    }
    // The 16 VGA colors, and for 256 the halftone palette's.
    static const BYTE vga[16][3] = {
        {0, 0, 0},       {0x80, 0, 0},       {0, 0x80, 0},       {0x80, 0x80, 0},
        {0, 0, 0x80},    {0x80, 0, 0x80},    {0, 0x80, 0x80},    {0xC0, 0xC0, 0xC0},
        {0x80, 0x80, 0x80}, {0xFF, 0, 0},    {0, 0xFF, 0},       {0xFF, 0xFF, 0},
        {0, 0, 0xFF},    {0xFF, 0, 0xFF},    {0, 0xFF, 0xFF},    {0xFF, 0xFF, 0xFF}};
    if (nBPP == 4) {
      for (int i = 0; i < 16; i++)
        p[i].rgbRed = vga[i][0], p[i].rgbGreen = vga[i][1], p[i].rgbBlue = vga[i][2];
      return;
    }
    HDC hdc = ::GetDC(NULL);
    HPALETTE hpal = ::CreateHalftonePalette(hdc);
    ::ReleaseDC(NULL, hdc);
    PALETTEENTRY pe[256];
    UINT n = hpal != NULL ? ::GetPaletteEntries(hpal, 0, 256, pe) : 0;
    if (hpal != NULL) ::DeleteObject(hpal);
    for (UINT i = 0; i < 256; i++) {
      if (i < n)
        p[i].rgbRed = pe[i].peRed, p[i].rgbGreen = pe[i].peGreen, p[i].rgbBlue = pe[i].peBlue;
      else
        p[i].rgbRed = p[i].rgbGreen = p[i].rgbBlue = (BYTE)i;
    }
  }

  COLORREF _GetTransparentRGB() const {
    if (m_iTransparentColor == -1) return CLR_INVALID;
    if (!IsIndexed()) return (COLORREF)m_iTransparentColor;
    RGBQUAD q = {};
    GetColorTable((UINT)m_iTransparentColor, 1, &q);
    return RGB(q.rgbRed, q.rgbGreen, q.rgbBlue);
  }

  // A DIB section like the GDI+ bitmap, and its pixels copied in.
  HRESULT _CreateFromGdiplus(Gdiplus::GpBitmap* pBitmap) {
    using namespace Gdiplus;
    UINT w = 0, h = 0;
    PixelFormat fmt = 0;
    DllExports::GdipGetImageWidth(pBitmap, &w);
    DllExports::GdipGetImageHeight(pBitmap, &h);
    DllExports::GdipGetImagePixelFormat(pBitmap, &fmt);
    int nBPP;
    DWORD flags = 0, compression = BI_RGB;
    PixelFormat lockFmt;
    switch (fmt) {
    case PixelFormat1bppIndexed: nBPP = 1, lockFmt = fmt; break;
    case PixelFormat4bppIndexed: nBPP = 4, lockFmt = fmt; break;
    case PixelFormat8bppIndexed: nBPP = 8, lockFmt = fmt; break;
    case PixelFormat16bppRGB555: nBPP = 16, lockFmt = fmt; break;
    case PixelFormat16bppRGB565: nBPP = 16, lockFmt = fmt, compression = BI_BITFIELDS; break;
    case PixelFormat24bppRGB: nBPP = 24, lockFmt = fmt; break;
    case PixelFormat32bppRGB: nBPP = 32, lockFmt = fmt; break;
    default:
      if (fmt & PixelFormatAlpha) {
        nBPP = 32, lockFmt = PixelFormat32bppARGB, flags = createAlphaChannel;
      } else {
        nBPP = 24, lockFmt = PixelFormat24bppRGB;
      }
      break;
    }
    Destroy();
    if (!CreateEx((int)w, (int)h, nBPP, compression, NULL, flags)) return E_FAIL;
    if (nBPP <= 8) {
      INT size = 0;
      DllExports::GdipGetImagePaletteSize(pBitmap, &size);
      if (size > 0) {
        ColorPalette* pal = (ColorPalette*)::malloc(size);
        if (pal != NULL && DllExports::GdipGetImagePalette(pBitmap, pal, size) == Ok) {
          RGBQUAD q[256];
          UINT n = pal->Count < 256 ? pal->Count : 256;
          for (UINT i = 0; i < n; i++) {
            ARGB c = pal->Entries[i];
            q[i].rgbRed = (BYTE)(c >> 16), q[i].rgbGreen = (BYTE)(c >> 8);
            q[i].rgbBlue = (BYTE)c, q[i].rgbReserved = 0;
          }
          SetColorTable(0, n, q);
        }
        ::free(pal);
      }
    }
    BitmapData data;
    data.Width = w;
    data.Height = h;
    data.Stride = m_nPitch;
    data.PixelFormat = lockFmt;
    data.Scan0 = m_pBits;
    data.Reserved = 0;
    Rect rc(0, 0, (INT)w, (INT)h);
    ::GdiFlush();
    if (DllExports::GdipBitmapLockBits(pBitmap, &rc, ImageLockModeRead | ImageLockModeUserInputBuf,
                                       lockFmt, &data) != Ok) {
      Destroy();
      return E_FAIL;
    }
    DllExports::GdipBitmapUnlockBits(pBitmap, &data);
    return S_OK;
  }

  // A GDI+ bitmap over this image's pixels (a copy for one that is not a
  // DIB section).
  Gdiplus::GpBitmap* _MakeGdiplusBitmap() const {
    using namespace Gdiplus;
    GpBitmap* pBitmap = NULL;
    if (!m_bIsDIBSection) {
      DllExports::GdipCreateBitmapFromHBITMAP(m_hBitmap, NULL, &pBitmap);
      return pBitmap;
    }
    PixelFormat fmt;
    switch (m_nBPP) {
    case 1: fmt = PixelFormat1bppIndexed; break;
    case 4: fmt = PixelFormat4bppIndexed; break;
    case 8: fmt = PixelFormat8bppIndexed; break;
    case 16: {
      DIBSECTION ds;
      ::GetObject(m_hBitmap, sizeof ds, &ds);
      fmt = ds.dsBmih.biCompression == BI_BITFIELDS && ds.dsBitfields[1] == 0x07E0
                ? PixelFormat16bppRGB565
                : PixelFormat16bppRGB555;
      break;
    }
    case 24: fmt = PixelFormat24bppRGB; break;
    default: fmt = m_bHasAlphaChannel ? PixelFormat32bppARGB : PixelFormat32bppRGB; break;
    }
    ::GdiFlush();
    if (DllExports::GdipCreateBitmapFromScan0(m_nWidth, m_nHeight, m_nPitch, fmt,
                                              (BYTE*)m_pBits, &pBitmap) != Ok)
      return NULL;
    if (m_nBPP <= 8) {
      int n = 1 << m_nBPP;
      ColorPalette* pal = (ColorPalette*)::malloc(sizeof(ColorPalette) + n * sizeof(ARGB));
      if (pal != NULL) {
        RGBQUAD q[256];
        GetColorTable(0, n, q);
        pal->Flags = 0;
        pal->Count = n;
        for (int i = 0; i < n; i++)
          pal->Entries[i] = 0xFF000000u | ((ARGB)q[i].rgbRed << 16) |
                            ((ARGB)q[i].rgbGreen << 8) | q[i].rgbBlue;
        DllExports::GdipSetImagePalette(pBitmap, pal);
        ::free(pal);
      }
    }
    return pBitmap;
  }

  // GDI+'s encoders (bSave) or decoders, in an array the caller frees.
  static Gdiplus::ImageCodecInfo* _Codecs(bool bSave, UINT& n) {
    using namespace Gdiplus;
    UINT size = 0;
    n = 0;
    if (!_AtlGdiPlus().Init()) return NULL;
    if ((bSave ? DllExports::GdipGetImageEncodersSize(&n, &size)
               : DllExports::GdipGetImageDecodersSize(&n, &size)) != Ok ||
        size == 0)
      return NULL;
    ImageCodecInfo* p = (ImageCodecInfo*)::malloc(size);
    if (p == NULL) return NULL;
    if ((bSave ? DllExports::GdipGetImageEncoders(n, size, p)
               : DllExports::GdipGetImageDecoders(n, size, p)) != Ok) {
      ::free(p);
      n = 0;
      return NULL;
    }
    return p;
  }
  static bool _EncoderForFormat(REFGUID guid, CLSID& clsid) {
    UINT n;
    Gdiplus::ImageCodecInfo* p = _Codecs(true, n);
    bool found = false;
    for (UINT i = 0; i < n && !found; i++)
      if (p[i].FormatID == guid) clsid = p[i].Clsid, found = true;
    ::free(p);
    return found;
  }
  static bool _EncoderForFile(LPCTSTR pszFileName, CLSID& clsid) {
    CStringW ext(::PathFindExtension(pszFileName));
    if (ext.IsEmpty()) return false;
    UINT n;
    Gdiplus::ImageCodecInfo* p = _Codecs(true, n);
    bool found = false;
    for (UINT i = 0; i < n && !found; i++) {
      // FilenameExtension: "*.JPG;*.JPEG;*.JPE;*.JFIF"
      CStringW exts(p[i].FilenameExtension);
      int pos = 0;
      for (CStringW tok = exts.Tokenize(L";", pos); pos >= 0; tok = exts.Tokenize(L";", pos)) {
        tok.Trim();
        if (tok.Left(1) == L"*") tok = tok.Mid(1);
        if (tok.CompareNoCase(ext) == 0) {
          clsid = p[i].Clsid;
          found = true;
          break;
        }
      }
    }
    ::free(p);
    return found;
  }
  static DWORD _ExcludeFlag(REFGUID guid) {
    using namespace Gdiplus;
    if (guid == ImageFormatGIF) return excludeGIF;
    if (guid == ImageFormatBMP) return excludeBMP;
    if (guid == ImageFormatEMF) return excludeEMF;
    if (guid == ImageFormatWMF) return excludeWMF;
    if (guid == ImageFormatJPEG) return excludeJPEG;
    if (guid == ImageFormatPNG) return excludePNG;
    if (guid == ImageFormatTIFF) return excludeTIFF;
    if (guid == ImageFormatIcon) return excludeIcon;
    return excludeOther;
  }
  static HRESULT _FilterString(bool bSave, CString& str, CSimpleArray<GUID>& guids,
                               LPCTSTR pszAll, DWORD dwExclude, TCHAR sep) {
    UINT n;
    Gdiplus::ImageCodecInfo* p = _Codecs(bSave, n);
    if (p == NULL) return E_FAIL;
    str.Empty();
    CString all;
    CString each;
    CSimpleArray<GUID> found;
    for (UINT i = 0; i < n; i++) {
      if (dwExclude & _ExcludeFlag(p[i].FormatID)) continue;
      CString desc(p[i].FormatDescription), exts(p[i].FilenameExtension);
      each += desc + TEXT(" (") + exts + TEXT(")") + sep + exts + sep;
      if (!all.IsEmpty()) all += TEXT(';');
      all += exts;
      found.Add(p[i].FormatID);
    }
    ::free(p);
    if (pszAll != NULL) {
      // The "all" entry comes first in the filter, so its GUID does too.
      str = CString(pszAll) + TEXT(" (") + all + TEXT(")") + sep + all + sep;
      GUID none = GUID_NULL;
      guids.Add(none);
    }
    for (int i = 0; i < found.GetSize(); i++) guids.Add(found[i]);
    str += each;
    str += sep;
    return S_OK;
  }
};

}  // namespace ATL

#endif  // __ATLIMAGE_H__
