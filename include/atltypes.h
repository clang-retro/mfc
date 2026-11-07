// atltypes.h - CSize, CPoint and CRect.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLTYPES_H__
#define __ATLTYPES_H__

#include <windows.h>

class CSize;
class CPoint;
class CRect;

class CSize : public tagSIZE {
public:
  CSize() { cx = 0; cy = 0; }
  CSize(int initCX, int initCY) { cx = initCX; cy = initCY; }
  CSize(SIZE initSize) { cx = initSize.cx; cy = initSize.cy; }
  CSize(POINT initPt) { cx = initPt.x; cy = initPt.y; }
  CSize(DWORD dwSize) { cx = (short)LOWORD(dwSize); cy = (short)HIWORD(dwSize); }

  BOOL operator==(SIZE size) const { return cx == size.cx && cy == size.cy; }
  BOOL operator!=(SIZE size) const { return cx != size.cx || cy != size.cy; }
  void operator+=(SIZE size) { cx += size.cx; cy += size.cy; }
  void operator-=(SIZE size) { cx -= size.cx; cy -= size.cy; }
  void SetSize(int CX, int CY) { cx = CX; cy = CY; }

  CSize operator+(SIZE size) const { return CSize(cx + size.cx, cy + size.cy); }
  CSize operator-(SIZE size) const { return CSize(cx - size.cx, cy - size.cy); }
  CSize operator-() const { return CSize(-cx, -cy); }
  CPoint operator+(POINT point) const;
  CPoint operator-(POINT point) const;
  CRect operator+(const RECT* lpRect) const;
  CRect operator-(const RECT* lpRect) const;
};

class CPoint : public tagPOINT {
public:
  CPoint() { x = 0; y = 0; }
  CPoint(int initX, int initY) { x = initX; y = initY; }
  CPoint(POINT initPt) { x = initPt.x; y = initPt.y; }
  CPoint(SIZE initSize) { x = initSize.cx; y = initSize.cy; }
  CPoint(LPARAM dwPoint) { x = (short)LOWORD(dwPoint); y = (short)HIWORD(dwPoint); }

  void Offset(int xOffset, int yOffset) { x += xOffset; y += yOffset; }
  void Offset(POINT point) { x += point.x; y += point.y; }
  void Offset(SIZE size) { x += size.cx; y += size.cy; }
  void SetPoint(int X, int Y) { x = X; y = Y; }

  BOOL operator==(POINT point) const { return x == point.x && y == point.y; }
  BOOL operator!=(POINT point) const { return x != point.x || y != point.y; }
  void operator+=(SIZE size) { x += size.cx; y += size.cy; }
  void operator-=(SIZE size) { x -= size.cx; y -= size.cy; }
  void operator+=(POINT point) { x += point.x; y += point.y; }
  void operator-=(POINT point) { x -= point.x; y -= point.y; }

  CPoint operator+(SIZE size) const { return CPoint(x + size.cx, y + size.cy); }
  CPoint operator-(SIZE size) const { return CPoint(x - size.cx, y - size.cy); }
  CPoint operator-() const { return CPoint(-x, -y); }
  CPoint operator+(POINT point) const { return CPoint(x + point.x, y + point.y); }
  CSize operator-(POINT point) const { return CSize(x - point.x, y - point.y); }
  CRect operator+(const RECT* lpRect) const;
  CRect operator-(const RECT* lpRect) const;
};

class CRect : public tagRECT {
public:
  CRect() { left = top = right = bottom = 0; }
  CRect(int l, int t, int r, int b) { left = l; top = t; right = r; bottom = b; }
  CRect(const RECT& srcRect) { ::CopyRect(this, &srcRect); }
  CRect(LPCRECT lpSrcRect) { ::CopyRect(this, lpSrcRect); }
  CRect(POINT point, SIZE size) {
    right = (left = point.x) + size.cx;
    bottom = (top = point.y) + size.cy;
  }
  CRect(POINT topLeft, POINT bottomRight) {
    left = topLeft.x; top = topLeft.y; right = bottomRight.x; bottom = bottomRight.y;
  }

  int Width() const { return right - left; }
  int Height() const { return bottom - top; }
  CSize Size() const { return CSize(right - left, bottom - top); }
  CPoint& TopLeft() { return *((CPoint*)this); }
  CPoint& BottomRight() { return *((CPoint*)this + 1); }
  const CPoint& TopLeft() const { return *((const CPoint*)this); }
  const CPoint& BottomRight() const { return *((const CPoint*)this + 1); }
  CPoint CenterPoint() const { return CPoint((left + right) / 2, (top + bottom) / 2); }
  void SwapLeftRight() { SwapLeftRight(this); }
  static void SwapLeftRight(LPRECT lpRect) {
    LONG temp = lpRect->left; lpRect->left = lpRect->right; lpRect->right = temp;
  }

  operator LPRECT() { return this; }
  operator LPCRECT() const { return this; }

  BOOL IsRectEmpty() const { return ::IsRectEmpty(this); }
  BOOL IsRectNull() const { return left == 0 && right == 0 && top == 0 && bottom == 0; }
  BOOL PtInRect(POINT point) const { return ::PtInRect(this, point); }

  void SetRect(int x1, int y1, int x2, int y2) { ::SetRect(this, x1, y1, x2, y2); }
  void SetRect(POINT topLeft, POINT bottomRight) {
    ::SetRect(this, topLeft.x, topLeft.y, bottomRight.x, bottomRight.y);
  }
  void SetRectEmpty() { ::SetRectEmpty(this); }
  void CopyRect(LPCRECT lpSrcRect) { ::CopyRect(this, lpSrcRect); }
  BOOL EqualRect(LPCRECT lpRect) const { return ::EqualRect(this, lpRect); }

  void InflateRect(int x, int y) { ::InflateRect(this, x, y); }
  void InflateRect(SIZE size) { ::InflateRect(this, size.cx, size.cy); }
  void InflateRect(LPCRECT lpRect) {
    left -= lpRect->left; top -= lpRect->top; right += lpRect->right; bottom += lpRect->bottom;
  }
  void InflateRect(int l, int t, int r, int b) { left -= l; top -= t; right += r; bottom += b; }
  void DeflateRect(int x, int y) { ::InflateRect(this, -x, -y); }
  void DeflateRect(SIZE size) { ::InflateRect(this, -size.cx, -size.cy); }
  void DeflateRect(LPCRECT lpRect) {
    left += lpRect->left; top += lpRect->top; right -= lpRect->right; bottom -= lpRect->bottom;
  }
  void DeflateRect(int l, int t, int r, int b) { left += l; top += t; right -= r; bottom -= b; }

  void OffsetRect(int x, int y) { ::OffsetRect(this, x, y); }
  void OffsetRect(SIZE size) { ::OffsetRect(this, size.cx, size.cy); }
  void OffsetRect(POINT point) { ::OffsetRect(this, point.x, point.y); }
  void NormalizeRect() {
    int nTemp;
    if (left > right) { nTemp = left; left = right; right = nTemp; }
    if (top > bottom) { nTemp = top; top = bottom; bottom = nTemp; }
  }
  void MoveToY(int y) { bottom = Height() + y; top = y; }
  void MoveToX(int x) { right = Width() + x; left = x; }
  void MoveToXY(int x, int y) { MoveToX(x); MoveToY(y); }
  void MoveToXY(POINT point) { MoveToX(point.x); MoveToY(point.y); }

  BOOL IntersectRect(LPCRECT lpRect1, LPCRECT lpRect2) {
    return ::IntersectRect(this, lpRect1, lpRect2);
  }
  BOOL UnionRect(LPCRECT lpRect1, LPCRECT lpRect2) { return ::UnionRect(this, lpRect1, lpRect2); }
  BOOL SubtractRect(LPCRECT lpRectSrc1, LPCRECT lpRectSrc2) {
    return ::SubtractRect(this, lpRectSrc1, lpRectSrc2);
  }

  void operator=(const RECT& srcRect) { ::CopyRect(this, &srcRect); }
  BOOL operator==(const RECT& rect) const { return ::EqualRect(this, &rect); }
  BOOL operator!=(const RECT& rect) const { return !::EqualRect(this, &rect); }
  void operator+=(POINT point) { ::OffsetRect(this, point.x, point.y); }
  void operator+=(SIZE size) { ::OffsetRect(this, size.cx, size.cy); }
  void operator+=(LPCRECT lpRect) { InflateRect(lpRect); }
  void operator-=(POINT point) { ::OffsetRect(this, -point.x, -point.y); }
  void operator-=(SIZE size) { ::OffsetRect(this, -size.cx, -size.cy); }
  void operator-=(LPCRECT lpRect) { DeflateRect(lpRect); }
  void operator&=(const RECT& rect) { ::IntersectRect(this, this, &rect); }
  void operator|=(const RECT& rect) { ::UnionRect(this, this, &rect); }

  CRect operator+(POINT pt) const { CRect r(*this); ::OffsetRect(&r, pt.x, pt.y); return r; }
  CRect operator-(POINT pt) const { CRect r(*this); ::OffsetRect(&r, -pt.x, -pt.y); return r; }
  CRect operator+(LPCRECT lpRect) const { CRect r(this); r.InflateRect(lpRect); return r; }
  CRect operator+(SIZE size) const { CRect r(*this); ::OffsetRect(&r, size.cx, size.cy); return r; }
  CRect operator-(SIZE size) const { CRect r(*this); ::OffsetRect(&r, -size.cx, -size.cy); return r; }
  CRect operator-(LPCRECT lpRect) const { CRect r(this); r.DeflateRect(lpRect); return r; }
  CRect operator&(const RECT& rect2) const { CRect r; ::IntersectRect(&r, this, &rect2); return r; }
  CRect operator|(const RECT& rect2) const { CRect r; ::UnionRect(&r, this, &rect2); return r; }
  CRect MulDiv(int nMultiplier, int nDivisor) const {
    return CRect(::MulDiv(left, nMultiplier, nDivisor), ::MulDiv(top, nMultiplier, nDivisor),
                 ::MulDiv(right, nMultiplier, nDivisor), ::MulDiv(bottom, nMultiplier, nDivisor));
  }
};

inline CPoint CSize::operator+(POINT point) const { return CPoint(cx + point.x, cy + point.y); }
inline CPoint CSize::operator-(POINT point) const { return CPoint(cx - point.x, cy - point.y); }
inline CRect CSize::operator+(const RECT* lpRect) const { return CRect(lpRect) + *this; }
inline CRect CSize::operator-(const RECT* lpRect) const { return CRect(lpRect) - *this; }
inline CRect CPoint::operator+(const RECT* lpRect) const { return CRect(lpRect) + *this; }
inline CRect CPoint::operator-(const RECT* lpRect) const { return CRect(lpRect) - *this; }

#endif // __ATLTYPES_H__
