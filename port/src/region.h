// region.h - areas made of rectangles, for update regions, clipping and
// GDI regions. A Region keeps rectangles that do not overlap.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_REGION_H
#define _WINPORT_REGION_H

#include <windows.h>

#include <vector>

namespace winport {

inline bool RectEmpty(const RECT& r) { return r.right <= r.left || r.bottom <= r.top; }
inline RECT MakeRect(LONG l, LONG t, LONG r, LONG b) {
  RECT rc = {l, t, r, b};
  return rc;
}
inline RECT Intersect(const RECT& a, const RECT& b) {
  RECT r = {a.left > b.left ? a.left : b.left, a.top > b.top ? a.top : b.top,
            a.right < b.right ? a.right : b.right, a.bottom < b.bottom ? a.bottom : b.bottom};
  if (RectEmpty(r)) r = MakeRect(0, 0, 0, 0);
  return r;
}
inline bool Overlaps(const RECT& a, const RECT& b) { return !RectEmpty(Intersect(a, b)); }
inline RECT Offset(RECT r, int dx, int dy) {
  r.left += dx;
  r.right += dx;
  r.top += dy;
  r.bottom += dy;
  return r;
}

class Region {
public:
  Region() {}
  explicit Region(const RECT& r) {
    if (!RectEmpty(r)) rects_.push_back(r);
  }
  bool Empty() const { return rects_.empty(); }
  const std::vector<RECT>& Rects() const { return rects_; }
  void Clear() { rects_.clear(); }
  void Set(const RECT& r) {
    rects_.clear();
    if (!RectEmpty(r)) rects_.push_back(r);
  }
  RECT Bounds() const {
    if (rects_.empty()) return MakeRect(0, 0, 0, 0);
    RECT b = rects_[0];
    for (const RECT& r : rects_) {
      if (r.left < b.left) b.left = r.left;
      if (r.top < b.top) b.top = r.top;
      if (r.right > b.right) b.right = r.right;
      if (r.bottom > b.bottom) b.bottom = r.bottom;
    }
    return b;
  }
  bool Contains(LONG x, LONG y) const {
    for (const RECT& r : rects_)
      if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return true;
    return false;
  }
  bool Touches(const RECT& rc) const {
    for (const RECT& r : rects_)
      if (Overlaps(r, rc)) return true;
    return false;
  }
  void Offset(int dx, int dy) {
    for (RECT& r : rects_) r = winport::Offset(r, dx, dy);
  }
  void Union(const RECT& rc) {
    if (RectEmpty(rc)) return;
    Region add(rc);
    add.Subtract(*this);
    rects_.insert(rects_.end(), add.rects_.begin(), add.rects_.end());
    Coalesce();
  }
  void Union(const Region& o) {
    for (const RECT& r : o.rects_) Union(r);
  }
  void Subtract(const RECT& cut) {
    if (RectEmpty(cut)) return;
    std::vector<RECT> out;
    for (const RECT& r : rects_) {
      if (!Overlaps(r, cut)) {
        out.push_back(r);
        continue;
      }
      // Up to four pieces: above, below, left and right of the cut.
      if (r.top < cut.top) out.push_back(MakeRect(r.left, r.top, r.right, cut.top));
      if (r.bottom > cut.bottom) out.push_back(MakeRect(r.left, cut.bottom, r.right, r.bottom));
      LONG t = r.top > cut.top ? r.top : cut.top, b = r.bottom < cut.bottom ? r.bottom : cut.bottom;
      if (r.left < cut.left) out.push_back(MakeRect(r.left, t, cut.left, b));
      if (r.right > cut.right) out.push_back(MakeRect(cut.right, t, r.right, b));
    }
    rects_.swap(out);
  }
  void Subtract(const Region& o) {
    for (const RECT& r : o.rects_) Subtract(r);
  }
  void Intersect(const RECT& rc) {
    std::vector<RECT> out;
    for (const RECT& r : rects_) {
      RECT i = winport::Intersect(r, rc);
      if (!RectEmpty(i)) out.push_back(i);
    }
    rects_.swap(out);
  }
  void Intersect(const Region& o) {
    std::vector<RECT> out;
    for (const RECT& a : rects_)
      for (const RECT& b : o.rects_) {
        RECT i = winport::Intersect(a, b);
        if (!RectEmpty(i)) out.push_back(i);
      }
    rects_.swap(out);
  }
  void Xor(const Region& o) {
    Region a = *this, b = o;
    a.Subtract(o);
    b.Subtract(*this);
    rects_ = a.rects_;
    Union(b);
  }
  // Adds a rectangle known not to overlap the region, for building one
  // row by row; Normalize joins the pieces afterwards.
  void AppendDisjoint(const RECT& r) {
    if (!RectEmpty(r)) rects_.push_back(r);
  }
  void Normalize() { Coalesce(); }
  // NULLREGION, SIMPLEREGION or COMPLEXREGION.
  int Complexity() const { return rects_.empty() ? 1 : rects_.size() == 1 ? 2 : 3; }
  bool Equals(const Region& o) const {
    Region a = *this, b = o;
    a.Subtract(o);
    b.Subtract(*this);
    return a.Empty() && b.Empty();
  }

private:
  // Joins rectangles that share a whole edge, so that repeated unions do
  // not fragment the region.
  void Coalesce() {
    bool merged = true;
    while (merged) {
      merged = false;
      for (size_t i = 0; i < rects_.size() && !merged; i++)
        for (size_t j = i + 1; j < rects_.size() && !merged; j++) {
          RECT& a = rects_[i];
          const RECT& b = rects_[j];
          if (a.left == b.left && a.right == b.right && (a.bottom == b.top || b.bottom == a.top)) {
            a.top = a.top < b.top ? a.top : b.top;
            a.bottom = a.bottom > b.bottom ? a.bottom : b.bottom;
            merged = true;
          } else if (a.top == b.top && a.bottom == b.bottom && (a.right == b.left || b.right == a.left)) {
            a.left = a.left < b.left ? a.left : b.left;
            a.right = a.right > b.right ? a.right : b.right;
            merged = true;
          }
          if (merged) rects_.erase(rects_.begin() + j);
        }
    }
  }
  std::vector<RECT> rects_;
};

}  // namespace winport

#endif
