// user_paint.cpp - painting: update regions, WM_PAINT, window device
// contexts, and scrolling a window's contents.
//
// A window's update region is in client coordinates. GetMessage makes a
// WM_PAINT for a visible window whose region is not empty; BeginPaint
// hands over the region as the clipping of a new DC, after WM_NCPAINT and
// WM_ERASEBKGND when they are due.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "gdi.h"
#include "user.h"

using namespace winport;

namespace winport {

namespace {

RECT ClientBox(Window* w) {
  return MakeRect(0, 0, w->client.right - w->client.left, w->client.bottom - w->client.top);
}

bool NeedsPaint(Window* w) { return !w->update.Empty() || w->ncPaint || w->internalPaint; }

Window* FindToPaint(Window* w, DWORD tid) {
  if (!w->Visible()) return NULL;
  if (w->tid == tid && NeedsPaint(w)) return w;
  for (Window* c : w->children)
    if (Window* f = FindToPaint(c, tid)) return f;
  return NULL;
}

}  // namespace

void InvalidateWindow(Window* w, const Region* rgn, UINT flags) {
  UserGuard g(UserLock());
  if (!w || !::winport::GetWindow(w->hwnd)) return;
  RECT box = ClientBox(w);
  if (flags & RDW_INVALIDATE) {
    Region add = rgn ? *rgn : Region(box);
    add.Intersect(box);
    w->update.Union(add);
    if (flags & RDW_ERASE) w->erase = true;
    if (flags & RDW_FRAME) w->ncPaint = true;
  }
  if (flags & RDW_INTERNALPAINT) w->internalPaint = true;
  if (flags & RDW_VALIDATE) {
    if (rgn)
      w->update.Subtract(*rgn);
    else
      w->update.Clear();
    if (flags & RDW_NOFRAME) w->ncPaint = false;
    if (flags & RDW_NOERASE) w->erase = false;
    if (w->update.Empty()) w->erase = false;
  }
  if (flags & RDW_NOINTERNALPAINT) w->internalPaint = false;
  bool children = (flags & RDW_ALLCHILDREN) || (!(flags & RDW_NOCHILDREN) && !(w->style & WS_CLIPCHILDREN));
  if (children) {
    for (Window* c : w->children) {
      if (!c->Visible()) continue;
      // The part over the child, moved into the child's client area.
      RECT cr = c->rect;
      Region part = rgn ? *rgn : Region(box);
      part.Intersect(cr);
      if (part.Empty()) continue;
      part.Offset(-(cr.left + c->client.left), -(cr.top + c->client.top));
      InvalidateWindow(c, &part, flags);
    }
  }
  if ((flags & (RDW_INVALIDATE | RDW_INTERNALPAINT)) && w->Visible()) WakeQueue(QueueFor(w->tid));
}

Window* WindowToPaint(DWORD tid, HWND filter) {
  UserGuard g(UserLock());
  if (filter) {
    Window* f = ::winport::GetWindow(filter);
    return f && ReallyVisible(f) ? FindToPaint(f, tid) : NULL;
  }
  for (Window* t : TopLevels())
    if (Window* f = FindToPaint(t, tid)) return f;
  return NULL;
}

void SurfaceChanged(Window* top, const RECT& r) {
  if (top->native) GetBackend()->Present(top, r);
}

HDC NewWindowDC(Window* w, bool client, const Region* clip) {
  DCObj* dc = new DCObj(kWindowDC);
  dc->hwnd = w->hwnd;
  dc->client = client;
  if (clip) {
    dc->extraClip = *clip;
    dc->hasExtraClip = true;
  }
  HDC h = (HDC)NewGdiHandle(dc);
  InitDC(dc);
  return h;
}

void FreeWindowDC(HDC h) {
  if (DCObj* dc = DCOf(h)) FreeDC(dc);
}

}  // namespace winport

namespace {

// Sends WM_NCPAINT and WM_ERASEBKGND when due; returns whether the
// background still needs erasing.
bool PaintFrameAndBackground(Window* w, HDC dc, bool erase) {
  HWND h = w->hwnd;
  bool nc;
  {
    UserGuard g(UserLock());
    nc = w->ncPaint;
    w->ncPaint = false;
  }
  if (nc && (w->style & (WS_BORDER | WS_DLGFRAME | WS_THICKFRAME | WS_VSCROLL | WS_HSCROLL) ||
             (w->exStyle & (WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE)) ||
             (!w->Child() && w->menu))) {
    SendTo(w, WM_NCPAINT, 1, 0);
  }
  if (!GetWindow(h)) return false;
  if (!erase) return false;
  return SendTo(w, WM_ERASEBKGND, (WPARAM)dc, 0) == 0;
}

void PaintNow(Window* w, bool children) {
  HWND h = w->hwnd;
  if (!ReallyVisible(w)) return;
  // A parent without WS_CLIPCHILDREN draws over its children, so one with
  // painting due goes first, and paints this window after itself.
  Window* first = NULL;
  {
    UserGuard g(UserLock());
    for (Window* c = w; c->parent && !(c->parent->style & WS_CLIPCHILDREN); c = c->parent)
      if (!c->parent->update.Empty()) first = c->parent;
  }
  if (first) {
    PaintNow(first, true);
    if (!GetWindow(h)) return;
  }
  bool needs;
  {
    UserGuard g(UserLock());
    needs = !w->update.Empty() || w->ncPaint || w->internalPaint;
  }
  if (needs) {
    if (w->update.Empty() && !w->internalPaint) {
      // Only the frame needs drawing.
      HDC dc = GetDC(h);
      PaintFrameAndBackground(w, dc, false);
      ReleaseDC(h, dc);
    } else {
      SendTo(w, WM_PAINT, 0, 0);
      // A window that ignores WM_PAINT is not asked again here.
      if (GetWindow(h)) {
        UserGuard g(UserLock());
        w->internalPaint = false;
      }
    }
  }
  if (!children || !GetWindow(h)) return;
  std::vector<HWND> kids;
  {
    UserGuard g(UserLock());
    for (Window* c : w->children) kids.push_back(c->hwnd);
  }
  for (HWND k : kids)
    if (Window* c = GetWindow(k)) PaintNow(c, true);
}

// Regions given in screen coordinates, in the DC's device coordinates.
Region FromScreen(Window* w, bool client, HRGN h) {
  RegionObj* r = GdiAs<RegionObj>(h, OBJ_REGION);
  if (!r) return Region();
  Region out = r->rgn;
  RECT o = client ? ScreenClient(w) : ScreenRect(w);
  out.Offset(-o.left, -o.top);
  return out;
}

}  // namespace

extern "C" {

// ---------------------------------------------------------------------
// Device contexts

HDC WINAPI GetDCEx(HWND h, HRGN rgn, DWORD flags) {
  Window* w = h ? GetWindowOrFail(h) : GetWindow(GetDesktopWindow());
  if (!w) return NULL;
  bool client = !(flags & DCX_WINDOW);
  HDC hdc;
  DCObj* dc;
  bool own = w->cls && (w->cls->wc.style & (CS_OWNDC | CS_CLASSDC)) && client && !(flags & DCX_CACHE);
  if (own) {
    if (!w->ownDC || !DCOf(w->ownDC)) {
      w->ownDC = NewWindowDC(w, true, NULL);
      DCOf(w->ownDC)->owned = true;
    }
    hdc = w->ownDC;
    dc = DCOf(hdc);
    dc->hasExtraClip = false;
    dc->extraClip.Clear();
  } else {
    hdc = NewWindowDC(w, client, NULL);
    dc = DCOf(hdc);
  }
  if (rgn && (flags & (DCX_INTERSECTRGN | DCX_EXCLUDERGN))) {
    UserGuard g(UserLock());
    Region r = FromScreen(w, client, rgn);
    if (flags & DCX_INTERSECTRGN) {
      dc->extraClip = r;
    } else {
      dc->extraClip.Set(dc->DeviceBounds());
      dc->extraClip.Subtract(r);
    }
    dc->hasExtraClip = true;
  }
  return hdc;
}

HDC WINAPI GetDC(HWND h) { return GetDCEx(h, NULL, 0); }
HDC WINAPI GetWindowDC(HWND h) { return GetDCEx(h, NULL, DCX_WINDOW | DCX_CACHE); }

int WINAPI ReleaseDC(HWND, HDC hdc) {
  DCObj* dc = DCOf(hdc);
  if (!dc || dc->kind != kWindowDC) return 0;
  if (dc->owned) {
    dc->hasExtraClip = false;
    dc->extraClip.Clear();
    return 1;
  }
  FreeDC(dc);
  return 1;
}

// ---------------------------------------------------------------------
// Painting

HDC WINAPI BeginPaint(HWND h, LPPAINTSTRUCT ps) {
  Window* w = GetWindowOrFail(h);
  if (!w || !ps) return NULL;
  HideCaret(h);
  Region upd;
  bool erase;
  {
    UserGuard g(UserLock());
    upd = w->update;
    erase = w->erase;
    w->update.Clear();
    w->erase = false;
    w->internalPaint = false;
  }
  HDC hdc = GetDCEx(h, NULL, 0);
  DCObj* dc = DCOf(hdc);
  dc->extraClip = upd;
  dc->hasExtraClip = true;
  memset(ps, 0, sizeof *ps);
  ps->hdc = hdc;
  ps->rcPaint = upd.Bounds();
  ps->fErase = PaintFrameAndBackground(w, hdc, erase);
  return hdc;
}

BOOL WINAPI EndPaint(HWND h, const PAINTSTRUCT* ps) {
  if (!ps) return FALSE;
  ReleaseDC(h, ps->hdc);
  ShowCaret(h);
  return TRUE;
}

// ---------------------------------------------------------------------
// Update regions

BOOL WINAPI InvalidateRect(HWND h, const RECT* r, BOOL erase) {
  if (!h) {
    // Every window.
    std::vector<HWND> all;
    {
      UserGuard g(UserLock());
      for (Window* t : TopLevels()) all.push_back(t->hwnd);
    }
    for (HWND t : all) InvalidateRect(t, NULL, erase);
    return TRUE;
  }
  Window* w = GetWindowOrFail(h);
  if (!w) return FALSE;
  Region rgn;
  if (r) rgn.Set(*r);
  InvalidateWindow(w, r ? &rgn : NULL, RDW_INVALIDATE | (erase ? RDW_ERASE : 0));
  return TRUE;
}

BOOL WINAPI InvalidateRgn(HWND h, HRGN hr, BOOL erase) {
  Window* w = GetWindowOrFail(h);
  if (!w) return FALSE;
  RegionObj* r = hr ? GdiAs<RegionObj>(hr, OBJ_REGION) : NULL;
  InvalidateWindow(w, r ? &r->rgn : NULL, RDW_INVALIDATE | (erase ? RDW_ERASE : 0));
  return TRUE;
}

BOOL WINAPI ValidateRect(HWND h, const RECT* r) {
  if (!h) return InvalidateRect(NULL, NULL, TRUE);
  Window* w = GetWindowOrFail(h);
  if (!w) return FALSE;
  Region rgn;
  if (r) rgn.Set(*r);
  InvalidateWindow(w, r ? &rgn : NULL, RDW_VALIDATE | RDW_NOCHILDREN);
  return TRUE;
}

BOOL WINAPI ValidateRgn(HWND h, HRGN hr) {
  Window* w = GetWindowOrFail(h);
  if (!w) return FALSE;
  RegionObj* r = hr ? GdiAs<RegionObj>(hr, OBJ_REGION) : NULL;
  InvalidateWindow(w, r ? &r->rgn : NULL, RDW_VALIDATE | RDW_NOCHILDREN);
  return TRUE;
}

BOOL WINAPI GetUpdateRect(HWND h, LPRECT out, BOOL erase) {
  Window* w = GetWindowOrFail(h);
  if (!w) return FALSE;
  RECT b;
  bool due;
  {
    UserGuard g(UserLock());
    b = w->update.Bounds();
    due = erase && w->erase && !w->update.Empty();
  }
  if (due) {
    HDC dc = GetDC(h);
    {
      UserGuard g(UserLock());
      w->erase = false;
    }
    PaintFrameAndBackground(w, dc, true);
    ReleaseDC(h, dc);
  }
  if (out) {
    // In logical units of a CS_OWNDC window's DC.
    if (w->ownDC && DCOf(w->ownDC)) {
      DCObj* dc = DCOf(w->ownDC);
      POINT a = dc->ToLogical(b.left, b.top), c = dc->ToLogical(b.right, b.bottom);
      b = MakeRect(a.x, a.y, c.x, c.y);
    }
    *out = b;
  }
  return !RectEmpty(b);
}

int WINAPI GetUpdateRgn(HWND h, HRGN hr, BOOL erase) {
  Window* w = GetWindowOrFail(h);
  RegionObj* r = GdiAs<RegionObj>(hr, OBJ_REGION);
  if (!w || !r) return ERROR;
  GetUpdateRect(h, NULL, erase);
  UserGuard g(UserLock());
  r->rgn = w->update;
  return r->rgn.Complexity();
}

int WINAPI ExcludeUpdateRgn(HDC hdc, HWND h) {
  DCObj* dc = DCOf(hdc);
  Window* w = GetWindowOrFail(h);
  if (!dc || !w) return ERROR;
  UserGuard g(UserLock());
  if (!dc->s.hasClip) {
    dc->s.clip.Set(dc->DeviceBounds());
    dc->s.hasClip = true;
  }
  dc->s.clip.Subtract(w->update);
  return dc->s.clip.Complexity();
}

BOOL WINAPI UpdateWindow(HWND h) {
  Window* w = GetWindowOrFail(h);
  if (!w) return FALSE;
  PaintNow(w, true);
  return TRUE;
}

BOOL WINAPI RedrawWindow(HWND h, const RECT* r, HRGN hr, UINT flags) {
  if (!h) {
    std::vector<HWND> all;
    {
      UserGuard g(UserLock());
      for (Window* t : TopLevels()) all.push_back(t->hwnd);
    }
    for (HWND t : all) RedrawWindow(t, r, hr, flags);
    return TRUE;
  }
  Window* w = GetWindowOrFail(h);
  if (!w) return FALSE;
  Region rgn;
  const Region* p = NULL;
  if (hr) {
    if (RegionObj* ro = GdiAs<RegionObj>(hr, OBJ_REGION)) {
      rgn = ro->rgn;
      p = &rgn;
    }
  } else if (r) {
    rgn.Set(*r);
    p = &rgn;
  }
  if (flags & (RDW_INVALIDATE | RDW_VALIDATE | RDW_INTERNALPAINT | RDW_NOINTERNALPAINT))
    InvalidateWindow(w, p, flags);
  if (flags & RDW_UPDATENOW) {
    PaintNow(w, (flags & RDW_ALLCHILDREN) || !(flags & RDW_NOCHILDREN));
  } else if (flags & RDW_ERASENOW) {
    bool erase;
    {
      UserGuard g(UserLock());
      erase = w->erase;
      w->erase = false;
    }
    HDC dc = GetDC(h);
    PaintFrameAndBackground(w, dc, erase);
    ReleaseDC(h, dc);
  }
  return TRUE;
}

BOOL WINAPI LockWindowUpdate(HWND) { return TRUE; }

// ---------------------------------------------------------------------
// Scrolling

static int ScrollCore(HDC hdc, Window* w, int dx, int dy, const RECT* scroll, const RECT* clip, Region* uncovered) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return ERROR;
  RECT box = dc->DeviceBounds();
  RECT src = scroll ? dc->ToDevice(*scroll) : box;
  RECT cl = clip ? dc->ToDevice(*clip) : box;
  int ddx = dc->ScaleX(dx) * (dx < 0 ? -1 : 1), ddy = dc->ScaleY(dy) * (dy < 0 ? -1 : 1);
  // What can be copied: the visible part of the source, moved.
  Region vis = dc->Visible();
  Region from(src);
  from.Intersect(vis);
  Region to = from;
  to.Offset(ddx, ddy);
  Region area(Intersect(src, cl));
  to.Intersect(area);
  to.Intersect(vis);
  {
    DrawScope ds(dc);
    if (ds.ok) {
      // Copy the rows in an order that never reads a moved pixel.
      Region target = to;
      target.Intersect(ds.clip);
      std::vector<DWORD> copy;
      for (const RECT& r : target.Rects()) {
        int w2 = r.right - r.left;
        copy.resize((size_t)w2 * (r.bottom - r.top));
        for (int y = r.top; y < r.bottom; y++)
          memcpy(&copy[(size_t)(y - r.top) * w2], &ds.At(r.left - ddx, y - ddy), w2 * sizeof(DWORD));
        for (int y = r.top; y < r.bottom; y++)
          memcpy(&ds.At(r.left, y), &copy[(size_t)(y - r.top) * w2], w2 * sizeof(DWORD));
        ds.Dirty(r);
      }
    }
  }
  if (uncovered) {
    *uncovered = area;
    uncovered->Subtract(to);
  }
  (void)w;
  return to.Empty() ? NULLREGION : SIMPLEREGION;
}

int WINAPI ScrollWindowEx(HWND h, int dx, int dy, const RECT* scroll, const RECT* clip, HRGN hrgnUpdate,
                          LPRECT prcUpdate, UINT flags) {
  Window* w = GetWindowOrFail(h);
  if (!w) return ERROR;
  HDC hdc = GetDCEx(h, NULL, DCX_CACHE);
  Region uncovered;
  int result = ScrollCore(hdc, w, dx, dy, scroll, clip, &uncovered);
  ReleaseDC(h, hdc);
  RECT box = MakeRect(0, 0, w->client.right - w->client.left, w->client.bottom - w->client.top);
  RECT area = Intersect(scroll ? *scroll : box, clip ? *clip : box);
  {
    // The waiting update region moves with the contents.
    UserGuard g(UserLock());
    Region inside = w->update;
    inside.Intersect(area);
    if (!inside.Empty()) {
      w->update.Subtract(area);
      inside.Offset(dx, dy);
      inside.Intersect(area);
      w->update.Union(inside);
    }
  }
  if (flags & SW_SCROLLCHILDREN) {
    std::vector<HWND> kids;
    {
      UserGuard g(UserLock());
      for (Window* c : w->children) {
        if (!scroll || Overlaps(c->rect, *scroll)) kids.push_back(c->hwnd);
      }
    }
    for (HWND k : kids) {
      Window* c = GetWindow(k);
      if (!c) continue;
      SetWindowPos(k, NULL, c->rect.left + dx, c->rect.top + dy, 0, 0,
                   SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
    }
  }
  if (flags & SW_INVALIDATE)
    InvalidateWindow(w, &uncovered, RDW_INVALIDATE | ((flags & SW_ERASE) ? RDW_ERASE : 0) | RDW_ALLCHILDREN);
  if (hrgnUpdate)
    if (RegionObj* r = GdiAs<RegionObj>(hrgnUpdate, OBJ_REGION)) r->rgn = uncovered;
  if (prcUpdate) *prcUpdate = uncovered.Bounds();
  return result == ERROR ? ERROR : uncovered.Complexity();
}

BOOL WINAPI ScrollWindow(HWND h, int dx, int dy, const RECT* scroll, const RECT* clip) {
  UINT flags = SW_INVALIDATE | SW_ERASE;
  if (!scroll) flags |= SW_SCROLLCHILDREN;
  return ScrollWindowEx(h, dx, dy, scroll, clip, NULL, NULL, flags) != ERROR;
}

BOOL WINAPI ScrollDC(HDC hdc, int dx, int dy, const RECT* scroll, const RECT* clip, HRGN hrgnUpdate,
                     LPRECT prcUpdate) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return FALSE;
  Region uncovered;
  if (ScrollCore(hdc, NULL, dx, dy, scroll, clip, &uncovered) == ERROR) return FALSE;
  if (hrgnUpdate)
    if (RegionObj* r = GdiAs<RegionObj>(hrgnUpdate, OBJ_REGION)) r->rgn = uncovered;
  if (prcUpdate) {
    RECT b = uncovered.Bounds();
    POINT a = dc->ToLogical(b.left, b.top), c = dc->ToLogical(b.right, b.bottom);
    *prcUpdate = MakeRect(a.x, a.y, c.x, c.y);
  }
  return TRUE;
}

}  // extern "C"
