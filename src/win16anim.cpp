// win16anim.cpp - the animation window for 16-bit Windows, which has no
// comctl32.dll.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
//
// The animation control plays the first video stream of an AVI clip, as
// comctl32.dll does: from an "AVI" resource of a module or from a file.
// The frames are uncompressed (BI_RGB, 1, 4, 8 or 24 bits) or BI_RLE8.
// The control reads each frame from the file when it shows it, so a
// clip can be larger than a segment. One frame must fit in a segment.
//
// 16-bit Windows has no threads. A timer moves the clip to its next
// frame, as ACS_TIMER does on Win32. The parent gets ACN_START and
// ACN_STOP in WM_COMMAND.
#include "stdafx.h"

#ifdef _AFX_WIN16

#include <afxtempl.h>

#include "afximpl.h"
#include "win16ctl.h"

using namespace afx16;

namespace {

const UINT kFrameTimer = 1;
const DWORD kKeyFrame = 0x10;  // AVIIF_KEYFRAME of an idx1 entry

/// Gives a four-character code from its characters.
inline DWORD Fcc(char a, char b, char c, char d) {
  return (DWORD)(BYTE)a | ((DWORD)(BYTE)b << 8) | ((DWORD)(BYTE)c << 16) | ((DWORD)(BYTE)d << 24);
}

/// Reads a little-endian DWORD from bytes.
inline DWORD RdDword(const BYTE FAR* p) {
  return (DWORD)p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24);
}

// The data of an animation control.
struct W16Anim {
  HWND hwnd;
  HFILE hFile;         // the clip, or HFILE_ERROR
  LONG lBase;          // the position of the clip in the file
  LONG lSize;          // the length of the clip
  BITMAPINFO FAR* pbmi;  // the format of the frames, with the colors
  UINT cbBmi;
  int cx, cy;          // the size of a frame
  UINT cbStride;       // the bytes of one row of a frame
  BYTE FAR* pBits;     // the frame that shows
  UINT cbBits;
  BOOL bRle;           // BI_RLE8
  CDWordArray offsets, sizes, flags;  // the data of each frame in the clip
  int nFrames;
  int iDecoded;        // the frame in pBits, or -1
  DWORD dwMsPerFrame;
  BOOL bPlaying;
  int iFrom, iTo, iCur;
  int nRepeat;         // -1 for always
  int iTransparent;    // the color index of ACS_TRANSPARENT, or -1
  /// Starts with no clip.
  W16Anim()
      : hwnd(NULL), hFile(HFILE_ERROR), lBase(0), lSize(0), pbmi(NULL), cbBmi(0), cx(0), cy(0),
        cbStride(0), pBits(NULL), cbBits(0), bRle(FALSE), nFrames(0), iDecoded(-1),
        dwMsPerFrame(100), bPlaying(FALSE), iFrom(0), iTo(0), iCur(0), nRepeat(0),
        iTransparent(-1) {}
};

/// Sends WM_COMMAND with a notification code to the parent of a control,
/// packed as 16-bit Windows packs it: the ID in wParam, the window in
/// the low word and the code in the high word of lParam.
void AnSendCommand(HWND hWnd, UINT nCode) {
  ::SendMessage(::GetParent(hWnd), WM_COMMAND, (WPARAM)::GetDlgCtrlID(hWnd),
                MAKELPARAM((WORD)(UINT)hWnd, nCode));
}

/// Reads bytes of the clip from a position in it. Gives TRUE when all
/// the bytes are there.
BOOL AnRead(W16Anim* a, LONG lPos, void FAR* pv, UINT cb) {
  if (lPos < 0 || lPos + (LONG)cb > a->lSize) return FALSE;
  if (::_llseek(a->hFile, a->lBase + lPos, 0) == HFILE_ERROR) return FALSE;
  return ::_lread(a->hFile, pv, cb) == cb;
}

/// Stops the clip. The parent gets ACN_STOP when the clip played.
void AnStop(W16Anim* a) {
  if (!a->bPlaying) return;
  ::KillTimer(a->hwnd, kFrameTimer);
  a->bPlaying = FALSE;
  AnSendCommand(a->hwnd, ACN_STOP);
}

/// Closes the clip and frees its data.
void AnClose(W16Anim* a) {
  AnStop(a);
  if (a->hFile != HFILE_ERROR) ::_lclose(a->hFile);
  a->hFile = HFILE_ERROR;
  delete[] (BYTE FAR*)a->pbmi;
  delete[] a->pBits;
  a->pbmi = NULL;
  a->pBits = NULL;
  a->offsets.RemoveAll();
  a->sizes.RemoveAll();
  a->flags.RemoveAll();
  a->nFrames = 0;
  a->iDecoded = -1;
  a->iTransparent = -1;
  a->cx = a->cy = 0;
}

/// Reads the stream format chunk (strf) of a video stream: a
/// BITMAPINFOHEADER and its colors. Gives FALSE for a format that the
/// control cannot show.
BOOL AnReadFormat(W16Anim* a, LONG lPos, DWORD cb) {
  if (cb < sizeof(BITMAPINFOHEADER) || cb > sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD))
    return FALSE;
  UINT cbAll = sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD);
  BYTE FAR* p = new BYTE[cbAll];
  _fmemset(p, 0, cbAll);
  if (!AnRead(a, lPos, p, (UINT)cb)) {
    delete[] p;
    return FALSE;
  }
  BITMAPINFOHEADER FAR* pbih = (BITMAPINFOHEADER FAR*)p;
  WORD nBits = pbih->biBitCount;
  DWORD dwComp = pbih->biCompression;
  BOOL bOk = (dwComp == BI_RGB && (nBits == 1 || nBits == 4 || nBits == 8 || nBits == 24)) ||
             (dwComp == BI_RLE8 && nBits == 8);
  long cxBits = pbih->biWidth;
  long cyBits = pbih->biHeight < 0 ? -pbih->biHeight : pbih->biHeight;
  if (!bOk || cxBits <= 0 || cyBits <= 0 || pbih->biHeight < 0) {
    delete[] p;
    return FALSE;
  }
  DWORD cbStride = (((DWORD)cxBits * nBits + 31) / 32) * 4;
  DWORD cbBits = cbStride * (DWORD)cyBits;
  if (cbBits > 0xFFF0UL) {  // one frame must fit in a segment
    delete[] p;
    return FALSE;
  }
  pbih->biSize = sizeof(BITMAPINFOHEADER);
  pbih->biCompression = BI_RGB;  // the control draws the frames that it has decoded
  pbih->biSizeImage = 0;
  a->pbmi = (BITMAPINFO FAR*)p;
  a->cbBmi = cbAll;
  a->cx = (int)cxBits;
  a->cy = (int)cyBits;
  a->cbStride = (UINT)cbStride;
  a->cbBits = (UINT)cbBits;
  a->bRle = dwComp == BI_RLE8;
  a->pBits = new BYTE[a->cbBits];
  _fmemset(a->pBits, 0, a->cbBits);
  return TRUE;
}

/// Reads the header list (hdrl) of a clip: the main header and the first
/// video stream. Gives the number of that stream in *pnStream.
BOOL AnReadHeaders(W16Anim* a, LONG lPos, LONG lEnd, int* pnStream) {
  int nStream = 0;
  BOOL bFound = FALSE;
  while (lPos + 8 <= lEnd) {
    BYTE hdr[12];
    if (!AnRead(a, lPos, hdr, 8)) return FALSE;
    DWORD fcc = RdDword(hdr), cb = RdDword(hdr + 4);
    LONG lData = lPos + 8;
    if (fcc == Fcc('a', 'v', 'i', 'h') && cb >= 16) {
      BYTE avih[4];
      if (AnRead(a, lData, avih, 4)) {
        DWORD us = RdDword(avih);
        if (us > 0) a->dwMsPerFrame = (us + 500) / 1000;
      }
    } else if (fcc == Fcc('L', 'I', 'S', 'T') && cb >= 4 && !bFound) {
      if (!AnRead(a, lData, hdr + 8, 4)) return FALSE;
      if (RdDword(hdr + 8) == Fcc('s', 't', 'r', 'l')) {
        // A stream list: strh and strf.
        LONG lSub = lData + 4, lSubEnd = lData + (LONG)cb;
        BOOL bVideo = FALSE;
        while (lSub + 8 <= lSubEnd) {
          BYTE sub[8];
          if (!AnRead(a, lSub, sub, 8)) return FALSE;
          DWORD fccSub = RdDword(sub), cbSub = RdDword(sub + 4);
          if (fccSub == Fcc('s', 't', 'r', 'h') && cbSub >= 28) {
            BYTE strh[28];
            if (!AnRead(a, lSub + 8, strh, 28)) return FALSE;
            bVideo = RdDword(strh) == Fcc('v', 'i', 'd', 's');
            DWORD dwScale = RdDword(strh + 20), dwRate = RdDword(strh + 24);
            if (bVideo && dwScale > 0 && dwRate > 0)
              a->dwMsPerFrame = (dwScale * 1000UL + dwRate / 2) / dwRate;
          } else if (fccSub == Fcc('s', 't', 'r', 'f') && bVideo) {
            if (!AnReadFormat(a, lSub + 8, cbSub)) return FALSE;
            bFound = TRUE;
            *pnStream = nStream;
          }
          lSub += 8 + (LONG)((cbSub + 1) & ~1UL);
        }
        if (!bFound) nStream++;
      }
    }
    lPos = lData + (LONG)((cb + 1) & ~1UL);
  }
  return bFound;
}

/// Gives TRUE when a chunk ID is a frame of a stream: "NNdb" or "NNdc".
BOOL AnIsFrame(DWORD fcc, int nStream) {
  char c0 = (char)('0' + nStream / 10), c1 = (char)('0' + nStream % 10);
  return fcc == Fcc(c0, c1, 'd', 'b') || fcc == Fcc(c0, c1, 'd', 'c');
}

/// Finds the frames of a stream in the movie list (movi), and in its
/// 'rec ' lists.
void AnReadMovie(W16Anim* a, LONG lPos, LONG lEnd, int nStream) {
  while (lPos + 8 <= lEnd) {
    BYTE hdr[12];
    if (!AnRead(a, lPos, hdr, 8)) return;
    DWORD fcc = RdDword(hdr), cb = RdDword(hdr + 4);
    if (fcc == Fcc('L', 'I', 'S', 'T') && cb >= 4) {
      AnReadMovie(a, lPos + 12, lPos + 8 + (LONG)cb, nStream);
    } else if (AnIsFrame(fcc, nStream)) {
      a->offsets.Add((DWORD)(lPos + 8));
      a->sizes.Add(cb);
      a->flags.Add(a->offsets.GetSize() == 1 ? kKeyFrame : 0);
    }
    lPos += 8 + (LONG)((cb + 1) & ~1UL);
  }
}

/// Reads the key frame flags of the index (idx1). The entries of the
/// stream are in the sequence of the frames.
void AnReadIndex(W16Anim* a, LONG lPos, DWORD cb, int nStream) {
  int iFrame = 0;
  for (DWORD k = 0; k + 16 <= cb && iFrame < (int)a->offsets.GetSize(); k += 16) {
    BYTE e[16];
    if (!AnRead(a, lPos + (LONG)k, e, 16)) return;
    if (!AnIsFrame(RdDword(e), nStream)) continue;
    if (RdDword(e + 4) & kKeyFrame) a->flags[iFrame] |= kKeyFrame;
    iFrame++;
  }
}

/// Reads the structure of an AVI clip: the headers, the frames and the
/// index. Gives FALSE when it is not a clip that the control can show.
BOOL AnParse(W16Anim* a) {
  BYTE hdr[12];
  if (!AnRead(a, 0, hdr, 12)) return FALSE;
  if (RdDword(hdr) != Fcc('R', 'I', 'F', 'F') || RdDword(hdr + 8) != Fcc('A', 'V', 'I', ' '))
    return FALSE;
  LONG lEnd = (LONG)MinL(8 + (LONG)RdDword(hdr + 4), a->lSize);
  LONG lPos = 12;
  int nStream = -1;
  LONG lMovi = -1;
  while (lPos + 8 <= lEnd) {
    if (!AnRead(a, lPos, hdr, 8)) return FALSE;
    DWORD fcc = RdDword(hdr), cb = RdDword(hdr + 4);
    LONG lData = lPos + 8;
    if (fcc == Fcc('L', 'I', 'S', 'T') && cb >= 4) {
      if (!AnRead(a, lData, hdr + 8, 4)) return FALSE;
      DWORD fccList = RdDword(hdr + 8);
      if (fccList == Fcc('h', 'd', 'r', 'l')) {
        if (!AnReadHeaders(a, lData + 4, lData + (LONG)cb, &nStream)) return FALSE;
      } else if (fccList == Fcc('m', 'o', 'v', 'i') && nStream >= 0) {
        lMovi = lData;
        AnReadMovie(a, lData + 4, lData + (LONG)cb, nStream);
      }
    } else if (fcc == Fcc('i', 'd', 'x', '1') && nStream >= 0 && lMovi >= 0) {
      AnReadIndex(a, lData, cb, nStream);
    }
    lPos = lData + (LONG)((cb + 1) & ~1UL);
  }
  a->nFrames = (int)a->offsets.GetSize();
  return nStream >= 0 && a->nFrames > 0;
}

/// Decodes BI_RLE8 data into the frame. The runs and the copies of
/// pixels change the frame; the pixels that a delta skips stay as they
/// were.
void AnDecodeRle8(W16Anim* a, const BYTE FAR* p, UINT cb) {
  int x = 0, y = 0;
  UINT i = 0;
  while (i + 1 < cb && y < a->cy) {
    BYTE n = p[i], c = p[i + 1];
    i += 2;
    if (n > 0) {
      for (int k = 0; k < n && x < a->cx; k++, x++) a->pBits[(UINT)y * a->cbStride + x] = c;
      continue;
    }
    if (c == 0) {  // the end of a line
      x = 0;
      y++;
    } else if (c == 1) {  // the end of the frame
      return;
    } else if (c == 2) {  // a delta
      if (i + 1 >= cb) return;
      x += p[i];
      y += p[i + 1];
      i += 2;
    } else {  // c pixels as they are, to a word boundary
      for (int k = 0; k < c && i < cb; k++, i++)
        if (x < a->cx) a->pBits[(UINT)y * a->cbStride + x++] = p[i];
      if (c & 1) i++;
    }
  }
}

/// Decodes one frame into the frame bits. An empty chunk does not change
/// the frame.
BOOL AnDecodeOne(W16Anim* a, int i) {
  DWORD cb = a->sizes[i];
  if (cb == 0) return TRUE;
  if (cb > 0xFFF0UL) return FALSE;
  BYTE FAR* p = new BYTE[(UINT)cb];
  BOOL bOk = AnRead(a, (LONG)a->offsets[i], p, (UINT)cb);
  if (bOk) {
    if (a->bRle)
      AnDecodeRle8(a, p, (UINT)cb);
    else
      _fmemcpy(a->pBits, p, (UINT)MinL((long)cb, (long)a->cbBits));
  }
  delete[] p;
  return bOk;
}

/// Makes a frame the frame that shows. A frame that changes the frame
/// before it needs the frames from the last key frame.
BOOL AnDecode(W16Anim* a, int i) {
  if (i < 0 || i >= a->nFrames) return FALSE;
  if (i == a->iDecoded) return TRUE;
  int iStart;
  if (!a->bRle) {
    iStart = i;
  } else {
    iStart = i;
    while (iStart > 0 && !(a->flags[iStart] & kKeyFrame)) {
      if (iStart == a->iDecoded + 1 && a->iDecoded >= 0) break;
      iStart--;
    }
    if (iStart == 0 || (a->flags[iStart] & kKeyFrame)) _fmemset(a->pBits, 0, a->cbBits);
  }
  for (int k = iStart; k <= i; k++)
    if (!AnDecodeOne(a, k)) {
      a->iDecoded = -1;
      return FALSE;
    }
  a->iDecoded = i;
  // ACS_TRANSPARENT: the color of the first pixel of the first frame
  // (the top-left pixel) shows the background.
  if (a->iTransparent < 0 && i == 0 && a->pbmi->bmiHeader.biBitCount <= 8 &&
      (StyleOf(a->hwnd) & ACS_TRANSPARENT)) {
    const BYTE FAR* pTop = a->pBits + (UINT)(a->cy - 1) * a->cbStride;
    int nBits = a->pbmi->bmiHeader.biBitCount;
    a->iTransparent = nBits == 8 ? pTop[0] : (nBits == 4 ? pTop[0] >> 4 : pTop[0] >> 7);
  }
  return TRUE;
}

/// Gives the position of the frame in the window: the top-left corner,
/// or the middle with ACS_CENTER.
POINT AnOrigin(W16Anim* a) {
  POINT pt = {0, 0};
  if (StyleOf(a->hwnd) & ACS_CENTER) {
    RECT rc;
    ::GetClientRect(a->hwnd, &rc);
    pt.x = (rc.right - a->cx) / 2;
    pt.y = (rc.bottom - a->cy) / 2;
  }
  return pt;
}

/// Draws the control: the background of the parent, and the frame that
/// shows. With ACS_TRANSPARENT, the transparent color is the color of
/// the background brush.
void AnPaint(W16Anim* a, HDC hdc) {
  RECT rc;
  ::GetClientRect(a->hwnd, &rc);
  HBRUSH hbr = ParentBrush(a->hwnd, hdc);
  POINT pt = AnOrigin(a);
  BOOL bFrame = a->pbmi != NULL && a->iDecoded >= 0;
  int nSaved = ::SaveDC(hdc);
  if (bFrame) ::ExcludeClipRect(hdc, pt.x, pt.y, pt.x + a->cx, pt.y + a->cy);
  ::FillRect(hdc, &rc, hbr);
  ::RestoreDC(hdc, nSaved);
  if (!bFrame) return;
  RGBQUAD rgbOld = {0, 0, 0, 0};
  BOOL bSwap = a->iTransparent >= 0;
  if (bSwap) {
    LOGBRUSH lb;
    COLORREF clr = ::GetSysColor(COLOR_WINDOW);
    if (::GetObject(hbr, sizeof(lb), &lb) == sizeof(lb) && lb.lbStyle == BS_SOLID)
      clr = lb.lbColor;
    RGBQUAD FAR& q = a->pbmi->bmiColors[a->iTransparent];
    rgbOld = q;
    q.rgbRed = GetRValue(clr);
    q.rgbGreen = GetGValue(clr);
    q.rgbBlue = GetBValue(clr);
  }
  ::SetDIBitsToDevice(hdc, pt.x, pt.y, a->cx, a->cy, 0, 0, 0, (UINT)a->cy, a->pBits, a->pbmi,
                      DIB_RGB_COLORS);
  if (bSwap) a->pbmi->bmiColors[a->iTransparent] = rgbOld;
}

/// Shows a frame now, without the background.
void AnShow(W16Anim* a, int i) {
  if (!AnDecode(a, i)) return;
  a->iCur = i;
  HDC hdc = ::GetDC(a->hwnd);
  AnPaint(a, hdc);
  ::ReleaseDC(a->hwnd, hdc);
}

/// Opens a clip: from an "AVI" resource of a module, or else from a
/// file. Without ACS_CENTER, the window gets the size of a frame. With
/// ACS_AUTOPLAY, the clip plays again and again.
BOOL AnOpen(W16Anim* a, HINSTANCE hInst, LPCSTR lpszName) {
  AnClose(a);
  if (lpszName == NULL) {
    ::InvalidateRect(a->hwnd, NULL, TRUE);
    return TRUE;
  }
  if (hInst == NULL) hInst = (HINSTANCE)::GetWindowWord(a->hwnd, GWW_HINSTANCE);
  HRSRC hRsrc = ::FindResource(hInst, lpszName, "AVI");
  if (hRsrc != NULL) {
    a->hFile = (HFILE)::AccessResource(hInst, hRsrc);
    if (a->hFile != HFILE_ERROR) {
      a->lBase = ::_llseek(a->hFile, 0, 1);
      a->lSize = (LONG)::SizeofResource(hInst, hRsrc);
    }
  } else if (HIWORD((DWORD)lpszName) != 0) {
    a->hFile = ::_lopen(lpszName, READ);
    if (a->hFile != HFILE_ERROR) {
      a->lBase = 0;
      a->lSize = ::_llseek(a->hFile, 0, 2);
    }
  }
  if (a->hFile == HFILE_ERROR || !AnParse(a)) {
    AnClose(a);
    return FALSE;
  }
  AnDecode(a, 0);
  a->iCur = 0;
  if (!(StyleOf(a->hwnd) & ACS_CENTER)) {
    // The window gets the size of a frame, with its border.
    RECT rcWnd, rcClient;
    ::GetWindowRect(a->hwnd, &rcWnd);
    ::GetClientRect(a->hwnd, &rcClient);
    int cxBorder = (rcWnd.right - rcWnd.left) - rcClient.right;
    int cyBorder = (rcWnd.bottom - rcWnd.top) - rcClient.bottom;
    ::SetWindowPos(a->hwnd, NULL, 0, 0, a->cx + cxBorder, a->cy + cyBorder,
                   SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
  }
  ::InvalidateRect(a->hwnd, NULL, TRUE);
  if (StyleOf(a->hwnd) & ACS_AUTOPLAY) {
    a->iFrom = 0;
    a->iTo = a->nFrames - 1;
    a->nRepeat = -1;
    a->bPlaying = TRUE;
    ::SetTimer(a->hwnd, kFrameTimer, (UINT)MaxL(a->dwMsPerFrame, 1), NULL);
    AnSendCommand(a->hwnd, ACN_START);
  }
  return TRUE;
}

/// Plays frames iFrom to iTo (0xFFFF for the last frame) nRepeat times
/// (-1 for always). When iFrom is iTo, the control only shows that
/// frame. Gives FALSE for frames that are not in the clip.
BOOL AnPlay(W16Anim* a, int nRepeat, UINT uFrom, UINT uTo) {
  if (a->pbmi == NULL) return FALSE;
  int iFrom = (int)uFrom;
  int iTo = uTo == 0xFFFF ? a->nFrames - 1 : (int)uTo;
  if (iFrom < 0 || iFrom >= a->nFrames || iTo >= a->nFrames || iTo < iFrom) return FALSE;
  ::KillTimer(a->hwnd, kFrameTimer);
  BOOL bWasPlaying = a->bPlaying;
  a->bPlaying = FALSE;
  AnShow(a, iFrom);
  if (iFrom == iTo || nRepeat == 0) {
    if (bWasPlaying) AnSendCommand(a->hwnd, ACN_STOP);
    return TRUE;
  }
  a->iFrom = iFrom;
  a->iTo = iTo;
  a->nRepeat = nRepeat;
  a->bPlaying = TRUE;
  ::SetTimer(a->hwnd, kFrameTimer, (UINT)MaxL(a->dwMsPerFrame, 1), NULL);
  if (!bWasPlaying) AnSendCommand(a->hwnd, ACN_START);
  return TRUE;
}

/// Moves the clip to its next frame. After the last frame, the clip
/// plays again, or stops when it played all its times.
void AnTick(W16Anim* a) {
  if (!a->bPlaying) return;
  int i = a->iCur + 1;
  if (i > a->iTo) {
    if (a->nRepeat > 0 && --a->nRepeat == 0) {
      AnStop(a);
      return;
    }
    i = a->iFrom;
  }
  AnShow(a, i);
}

/// Does the messages of an animation window.
LRESULT CALLBACK W16AnimProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
  W16Anim* a = StateOf<W16Anim>(hWnd);
  if (nMsg == WM_NCCREATE) {
    AttachState<W16Anim>(hWnd);
    return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  }
  if (a == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
  switch (nMsg) {
    case WM_CREATE: {
      // The window text of a dialog template can name the clip.
      LPCREATESTRUCT pcs = (LPCREATESTRUCT)lParam;
      if (pcs != NULL && pcs->lpszName != NULL &&
          (HIWORD((DWORD)pcs->lpszName) == 0 || pcs->lpszName[0] != '\0'))
        AnOpen(a, pcs->hInstance, pcs->lpszName);
      return 0;
    }
    case WM_DESTROY:
      AnClose(a);
      break;
    case WM_NCDESTROY:
      AnClose(a);
      DetachState(hWnd, a);
      break;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = ::BeginPaint(hWnd, &ps);
      AnPaint(a, hdc);
      ::EndPaint(hWnd, &ps);
      return 0;
    }
    case WM_SIZE:
      ::InvalidateRect(hWnd, NULL, TRUE);
      return 0;
    case WM_TIMER:
      if (wParam == kFrameTimer) AnTick(a);
      return 0;
    case ACM_OPENA:
      return AnOpen(a, (HINSTANCE)wParam, (LPCSTR)lParam);
    case ACM_OPENW:
      return FALSE;  // the window does its messages in ANSI only
    case ACM_PLAY:
      return AnPlay(a, (int)wParam, LOWORD(lParam), HIWORD(lParam));
    case ACM_STOP:
      AnStop(a);
      return TRUE;
    case ACM_ISPLAYING:
      return a->bPlaying;
  }
  return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

}  // namespace

/// Registers the animation class of 16-bit Windows, with the name of the
/// library and the name of comctl32.dll. Gives TRUE when the class is
/// there.
BOOL AFXAPI _AfxW16RegisterAnimateClass() {
  return RegisterCtlClasses(ANIMATE_CLASS, "SysAnimate32", W16AnimProc);
}

#endif  // _AFX_WIN16
