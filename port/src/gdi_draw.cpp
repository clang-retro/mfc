// gdi_draw.cpp - lines, curves and shapes, paths, and flood fills.
//
// Every shape becomes figures: lists of points in device pixel space,
// where (x, y) is the middle of pixel (x, y). A figure is filled by
// taking the pixels whose middles lie inside it (the left and top edges
// in, the right and bottom out), and stroked either one pixel wide, step
// by step as LineTo does, or, for a wide pen, as the union of a band
// around each segment with the pen's joins and end caps. Both build a
// mask first, so that a pixel is drawn once whatever the raster
// operation, and the fill leaves out the pixels the pen draws.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "gdi.h"
#include "user.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

using namespace winport;

namespace {

const double kPi = 3.14159265358979323846;

int Floor(double v) { return (int)floor(v); }
int RoundI(double v) { return (int)floor(v + 0.5); }

// ---------------------------------------------------------------------
// Masks: which pixels of a device rectangle a shape covers. 1 is the
// pen (or the brush), 2 the background color in the gaps of a styled
// line.

struct Mask {
  RECT box = {0, 0, 0, 0};
  int w = 0, h = 0;
  std::vector<BYTE> v;
  void Init(const RECT& b) {
    box = b;
    w = std::max(0, (int)(b.right - b.left));
    h = std::max(0, (int)(b.bottom - b.top));
    v.assign((size_t)w * h, 0);
  }
  bool Empty() const { return w == 0 || h == 0; }
  BYTE Get(int x, int y) const {
    if (x < box.left || y < box.top || x >= box.right || y >= box.bottom) return 0;
    return v[(size_t)(y - box.top) * w + (x - box.left)];
  }
  void Set(int x, int y, BYTE val) {
    if (x < box.left || y < box.top || x >= box.right || y >= box.bottom) return;
    BYTE& p = v[(size_t)(y - box.top) * w + (x - box.left)];
    if (val == 1 || p == 0) p = val;
  }
  void Span(int y, int x0, int x1) {  // [x0, x1)
    if (y < box.top || y >= box.bottom) return;
    if (x0 < box.left) x0 = box.left;
    if (x1 > box.right) x1 = box.right;
    if (x0 >= x1) return;
    memset(&v[(size_t)(y - box.top) * w + (x0 - box.left)], 1, (size_t)(x1 - x0));
  }
};

// Fills figures into the mask by the fill mode, treating each as closed.
void FillInto(const std::vector<Figure>& figs, int mode, Mask& m) {
  struct Edge {
    double x0, y0, x1, y1;
    int dir;
  };
  std::vector<Edge> edges;
  double top = 1e300, bottom = -1e300;
  for (const Figure& f : figs) {
    size_t n = f.pts.size();
    if (n < 2) continue;
    for (size_t i = 0; i < n; i++) {
      PtF a = f.pts[i], b = f.pts[(i + 1) % n];
      if (a.y == b.y) continue;
      Edge e;
      if (a.y < b.y) {
        e = {a.x, a.y, b.x, b.y, 1};
      } else {
        e = {b.x, b.y, a.x, a.y, -1};
      }
      edges.push_back(e);
      top = std::min(top, e.y0);
      bottom = std::max(bottom, e.y1);
    }
  }
  if (edges.empty()) return;
  int y0 = std::max((int)m.box.top, (int)ceil(top)), y1 = std::min((int)m.box.bottom, (int)ceil(bottom));
  std::vector<std::pair<double, int>> xs;
  for (int y = y0; y < y1; y++) {
    xs.clear();
    for (const Edge& e : edges)
      if (y >= e.y0 && y < e.y1) xs.push_back(std::make_pair(e.x0 + (y - e.y0) * (e.x1 - e.x0) / (e.y1 - e.y0), e.dir));
    if (xs.size() < 2) continue;
    std::sort(xs.begin(), xs.end());
    if (mode == WINDING) {
      int wind = 0;
      for (size_t i = 0; i + 1 < xs.size(); i++) {
        wind += xs[i].second;
        if (wind != 0) m.Span(y, (int)ceil(xs[i].first), (int)ceil(xs[i + 1].first));
      }
    } else {
      for (size_t i = 0; i + 1 < xs.size(); i += 2)
        m.Span(y, (int)ceil(xs[i].first), (int)ceil(xs[i + 1].first));
    }
  }
}

void FillPolygon(const std::vector<PtF>& pts, Mask& m) {
  std::vector<Figure> f(1);
  f[0].pts = pts;
  FillInto(f, WINDING, m);
}

void Disc(PtF c, double r, Mask& m) {
  int y0 = (int)ceil(c.y - r), y1 = Floor(c.y + r);
  for (int y = y0; y <= y1; y++) {
    double d = r * r - (y - c.y) * (y - c.y);
    if (d < 0) continue;
    double dx = sqrt(d);
    m.Span(y, (int)ceil(c.x - dx), Floor(c.x + dx) + 1);
  }
}

// ---------------------------------------------------------------------
// Pens

struct PenInfo {
  bool null = true;
  DWORD color = 0;
  int width = 1;  // in device pixels
  UINT kind = PS_SOLID;
  UINT endcap = PS_ENDCAP_ROUND, join = PS_JOIN_ROUND;
  double miter = 10;
  bool thin() const { return width <= 1; }
};

PenInfo PenFor(DCObj* dc) {
  PenInfo pi;
  PenObj* p = GdiAs<PenObj>(dc->s.pen, OBJ_PEN);
  if (!p) return pi;
  pi.kind = p->style & PS_STYLE_MASK;
  if (pi.kind == PS_NULL) return pi;
  pi.null = false;
  COLORREF c = p->color;
  if (dc->s.pen == GetStockObject(DC_PEN)) c = dc->s.dcPenColor;
  pi.color = ToPixel(c);
  if (IsMonoDC(dc)) pi.color = ToMono(pi.color);
  bool geometric = !p->extended || (p->style & PS_TYPE_MASK) == PS_GEOMETRIC;
  pi.width = 1;
  if (geometric && p->width > 1) pi.width = std::max(1, dc->ScaleX(p->width));
  if (p->extended && geometric) {
    pi.endcap = p->style & PS_ENDCAP_MASK;
    pi.join = p->style & PS_JOIN_MASK;
  }
  pi.miter = dc->s.miterLimit;
  // Wide pens draw solid unless they are geometric extended pens; their
  // styles are not drawn yet.
  if (!pi.thin() && pi.kind != PS_INSIDEFRAME) pi.kind = PS_SOLID;
  return pi;
}

// The on and off runs of a styled one-pixel line.
struct LineStyle {
  const int* runs = NULL;
  int count = 0;
  int index = 0, left = 0;
  bool opaque = false;
  void Reset() {
    index = 0;
    left = count ? runs[0] : 0;
  }
  // 1 to draw with the pen, 2 with the background, 0 to leave alone.
  BYTE Next() {
    if (!count) return 1;
    BYTE r = (index % 2 == 0) ? 1 : (opaque ? 2 : 0);
    if (--left <= 0) {
      index = (index + 1) % count;
      left = runs[index];
    }
    return r;
  }
};

LineStyle StyleFor(DCObj* dc, const PenInfo& pen) {
  static const int dash[] = {18, 6}, dot[] = {3, 3}, dashDot[] = {9, 6, 3, 6}, dashDotDot[] = {9, 3, 3, 3, 3, 3},
                   alt[] = {1, 1};
  LineStyle s;
  switch (pen.kind) {
    case PS_DASH: s.runs = dash; s.count = 2; break;
    case PS_DOT: s.runs = dot; s.count = 2; break;
    case PS_DASHDOT: s.runs = dashDot; s.count = 4; break;
    case PS_DASHDOTDOT: s.runs = dashDotDot; s.count = 6; break;
    case PS_ALTERNATE: s.runs = alt; s.count = 2; break;
  }
  s.opaque = dc->s.bkMode == OPAQUE;
  s.Reset();
  return s;
}

// A one-pixel line from a to b, leaving out b.
void ThinLine(int x0, int y0, int x1, int y1, LineStyle& st, Mask& m) {
  int dx = abs(x1 - x0), dy = abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  if (dx >= dy) {
    int err = dx / 2;
    for (int i = 0; i < dx; i++) {
      m.Set(x0, y0, st.Next());
      err -= dy;
      if (err < 0) {
        y0 += sy;
        err += dx;
      }
      x0 += sx;
    }
  } else {
    int err = dy / 2;
    for (int i = 0; i < dy; i++) {
      m.Set(x0, y0, st.Next());
      err -= dx;
      if (err < 0) {
        x0 += sx;
        err += dy;
      }
      y0 += sy;
    }
  }
}

void ThinFigure(const Figure& f, LineStyle st, Mask& m) {
  st.Reset();
  size_t n = f.pts.size();
  if (n == 0) return;
  std::vector<POINT> p(n);
  for (size_t i = 0; i < n; i++) p[i] = {RoundI(f.pts[i].x), RoundI(f.pts[i].y)};
  for (size_t i = 0; i + 1 < n; i++) ThinLine(p[i].x, p[i].y, p[i + 1].x, p[i + 1].y, st, m);
  if (f.closed && n > 1) ThinLine(p[n - 1].x, p[n - 1].y, p[0].x, p[0].y, st, m);
  if (n == 1 && f.closed) m.Set(p[0].x, p[0].y, 1);
}

void WideFigure(const Figure& f, const PenInfo& pen, Mask& m) {
  std::vector<PtF> p;
  for (const PtF& q : f.pts)
    if (p.empty() || fabs(p.back().x - q.x) > 1e-9 || fabs(p.back().y - q.y) > 1e-9) p.push_back(q);
  if (f.closed && p.size() > 1 && fabs(p.back().x - p[0].x) < 1e-9 && fabs(p.back().y - p[0].y) < 1e-9)
    p.pop_back();
  double r = pen.width / 2.0;
  if (p.empty()) return;
  if (p.size() == 1) {
    if (pen.endcap == PS_ENDCAP_ROUND) Disc(p[0], r, m);
    return;
  }
  size_t n = p.size();
  size_t segs = f.closed ? n : n - 1;
  auto dirOf = [&](size_t i) {
    PtF a = p[i], b = p[(i + 1) % n];
    double dx = b.x - a.x, dy = b.y - a.y, len = sqrt(dx * dx + dy * dy);
    PtF d = {dx / len, dy / len};
    return d;
  };
  for (size_t i = 0; i < segs; i++) {
    PtF a = p[i], b = p[(i + 1) % n];
    PtF d = dirOf(i);
    PtF nn = {-d.y * r, d.x * r};
    if (!f.closed && pen.endcap == PS_ENDCAP_SQUARE) {
      if (i == 0) a = {a.x - d.x * r, a.y - d.y * r};
      if (i == segs - 1) b = {b.x + d.x * r, b.y + d.y * r};
    }
    FillPolygon({{a.x + nn.x, a.y + nn.y}, {b.x + nn.x, b.y + nn.y}, {b.x - nn.x, b.y - nn.y}, {a.x - nn.x, a.y - nn.y}},
                m);
  }
  // Joins
  for (size_t i = 0; i < n; i++) {
    bool end = !f.closed && (i == 0 || i == n - 1);
    if (end) {
      if (pen.endcap == PS_ENDCAP_ROUND) Disc(p[i], r, m);
      continue;
    }
    if (pen.join == PS_JOIN_ROUND) {
      Disc(p[i], r, m);
      continue;
    }
    PtF d0 = dirOf((i + n - 1) % n), d1 = dirOf(i);
    double cross = d0.x * d1.y - d0.y * d1.x;
    if (fabs(cross) < 1e-9) continue;
    // The outer side of the turn.
    double s = cross > 0 ? -1 : 1;
    PtF n0 = {-d0.y * r * s, d0.x * r * s}, n1 = {-d1.y * r * s, d1.x * r * s};
    PtF c = p[i];
    PtF a = {c.x + n0.x, c.y + n0.y}, b = {c.x + n1.x, c.y + n1.y};
    std::vector<PtF> poly = {c, a, b};
    if (pen.join == PS_JOIN_MITER) {
      double cosA = d0.x * d1.x + d0.y * d1.y;
      double ratio = 1 / sqrt((1 - cosA) / 2 + 1e-12);  // miter length over pen width
      if (ratio <= pen.miter) {
        // Where the outer edges meet.
        double t = ((b.x - a.x) * d1.y - (b.y - a.y) * d1.x) / (d0.x * d1.y - d0.y * d1.x);
        PtF tip = {a.x + d0.x * t, a.y + d0.y * t};
        poly = {c, a, tip, b};
      }
    }
    FillPolygon(poly, m);
  }
}

void StrokeInto(DCObj* dc, const std::vector<Figure>& figs, const PenInfo& pen, Mask& m) {
  if (pen.thin()) {
    LineStyle st = StyleFor(dc, pen);
    for (const Figure& f : figs) ThinFigure(f, st, m);
  } else {
    for (const Figure& f : figs) WideFigure(f, pen, m);
  }
}

// ---------------------------------------------------------------------
// Painting masks

// Paints the mask's pixels inside the clip: 1 with the brush or color
// c1, 2 with c2, through the DC's binary raster operation. Pixels set in
// except are left alone.
void PaintMask(DCObj* dc, DrawScope& ds, const Mask& m, const BrushPaint* brush, DWORD c1, DWORD c2,
               const Mask* except) {
  if (m.Empty()) return;
  int rop = dc->s.rop2;
  bool mono = IsMonoDC(dc);
  for (const RECT& c : ds.clip.Rects()) {
    RECT r = Intersect(c, m.box);
    if (RectEmpty(r)) continue;
    for (int y = r.top; y < r.bottom; y++) {
      const BYTE* mv = &m.v[(size_t)(y - m.box.top) * m.w];
      DWORD* row = ds.px.Row(y + ds.origin.y) + ds.origin.x;
      for (int x = r.left; x < r.right; x++) {
        BYTE k = mv[x - m.box.left];
        if (!k) continue;
        if (except && except->Get(x, y)) continue;
        DWORD color;
        if (k == 2) {
          color = c2;
        } else if (brush) {
          if (!brush->Draws(x, y)) continue;
          color = brush->At(x, y);
        } else {
          color = c1;
        }
        DWORD v = Rop2(rop, row[x] & 0xFFFFFF, color);
        row[x] = mono ? ToMono(v) : v;
      }
    }
    ds.Dirty(r);
  }
}

RECT FigureBounds(const std::vector<Figure>& figs, int margin) {
  double l = 1e300, t = 1e300, r = -1e300, b = -1e300;
  for (const Figure& f : figs)
    for (const PtF& p : f.pts) {
      l = std::min(l, p.x);
      t = std::min(t, p.y);
      r = std::max(r, p.x);
      b = std::max(b, p.y);
    }
  if (l > r) return MakeRect(0, 0, 0, 0);
  const double lim = 1e8;
  l = std::max(l, -lim);
  t = std::max(t, -lim);
  r = std::min(r, lim);
  b = std::min(b, lim);
  return MakeRect(Floor(l) - margin, Floor(t) - margin, Floor(r) + margin + 2, Floor(b) + margin + 2);
}

bool Recording(DCObj* dc) { return dc->pathState == DCObj::kPathOpen; }

void AddToPath(DCObj* dc, const std::vector<Figure>& figs) {
  for (const Figure& f : figs) dc->path.push_back(f);
  dc->pathFigureOpen = !figs.empty() && !figs.back().closed;
}

// Draws figures: filled with the DC's brush and fill mode when fill,
// outlined with its pen when stroke. In a path, records them instead.
BOOL DrawFigures(DCObj* dc, const std::vector<Figure>& figs, bool fill, bool stroke, int fillMode = -1) {
  if (Recording(dc)) {
    AddToPath(dc, figs);
    return TRUE;
  }
  DrawScope ds(dc);
  if (!ds.ok) return TRUE;
  PenInfo pen = PenFor(dc);
  BrushPaint brush;
  if (fill) brush = PaintFor(dc, (HBRUSH)dc->s.brush);
  bool doStroke = stroke && !pen.null, doFill = fill && !brush.null;
  if (!doStroke && !doFill) return TRUE;
  RECT box = Intersect(FigureBounds(figs, pen.width + 1), ds.clip.Bounds());
  if (RectEmpty(box)) return TRUE;
  Mask sm, fm;
  if (doStroke) {
    sm.Init(box);
    StrokeInto(dc, figs, pen, sm);
  }
  if (doFill) {
    fm.Init(box);
    FillInto(figs, fillMode < 0 ? dc->s.polyFill : fillMode, fm);
    PaintMask(dc, ds, fm, &brush, 0, 0, doStroke ? &sm : NULL);
  }
  if (doStroke) PaintMask(dc, ds, sm, NULL, pen.color, ToPixel(dc->s.bkColor), NULL);
  return TRUE;
}

// ---------------------------------------------------------------------
// Building figures

// Whether logical boxes map to device boxes (no rotation or shear).
bool AxisAligned(DCObj* dc) {
  return dc->s.graphicsMode != GM_ADVANCED || (dc->s.xform.eM12 == 0 && dc->s.xform.eM21 == 0);
}

PtF Dev(DCObj* dc, double x, double y) { return dc->ToDeviceF(x, y); }

// A shape's frame: a box in some space, and how to bring its points to
// device space.
struct Frame {
  DCObj* dc;
  bool device;  // the box is in device pixels already
  double l, t, r, b;
  PtF Map(PtF p) const { return device ? p : Dev(dc, p.x, p.y); }
  double cx() const { return (l + r) / 2; }
  double cy() const { return (t + b) / 2; }
  double rx() const { return (r - l) / 2; }
  double ry() const { return (b - t) / 2; }
};

// The frame of a shape's bounding box: in device space, the right and
// bottom edges are left out, and an inside-frame pen keeps within it.
Frame BoxFrame(DCObj* dc, int l, int t, int r, int b, const PenInfo* pen) {
  Frame f;
  f.dc = dc;
  f.device = AxisAligned(dc);
  if (f.device) {
    PtF a = Dev(dc, l, t), c = Dev(dc, r, b);
    double x0 = RoundI(std::min(a.x, c.x)), x1 = RoundI(std::max(a.x, c.x));
    double y0 = RoundI(std::min(a.y, c.y)), y1 = RoundI(std::max(a.y, c.y));
    if (dc->s.graphicsMode == GM_COMPATIBLE || true) {
      x1 -= 1;
      y1 -= 1;
    }
    f.l = x0;
    f.t = y0;
    f.r = std::max(x0, x1);
    f.b = std::max(y0, y1);
  } else {
    f.l = std::min(l, r);
    f.r = std::max(l, r);
    f.t = std::min(t, b);
    f.b = std::max(t, b);
  }
  if (pen && !pen->null && pen->kind == PS_INSIDEFRAME && pen->width > 1 && f.device) {
    double in = (pen->width - 1) / 2.0;
    double w = f.r - f.l, h = f.b - f.t;
    double ix = std::min(in, w / 2), iy = std::min(in, h / 2);
    f.l += ix;
    f.r -= ix;
    f.t += iy;
    f.b -= iy;
  }
  return f;
}

int Steps(double rx, double ry, double sweep) {
  double per = (fabs(rx) + fabs(ry)) * fabs(sweep) / 2;  // about the arc's length
  int n = (int)ceil(per / 1.5);
  return std::max(4, std::min(n, 4000));
}

// Points on an ellipse from angle a0 through sweep (radians,
// counterclockwise as seen on the screen when sweep > 0), including both ends.
void ArcPoints(const Frame& f, double a0, double sweep, std::vector<PtF>* out) {
  double cx = f.cx(), cy = f.cy(), rx = f.rx(), ry = f.ry();
  int n = Steps(rx, ry, sweep);
  for (int i = 0; i <= n; i++) {
    double a = a0 + sweep * i / n;
    // In a logical frame y may grow upward; map handles that.
    double sy = f.device ? -1 : (f.dc->s.viewportExt.cy * (long long)f.dc->s.windowExt.cy < 0 ? 1 : -1);
    out->push_back(f.Map({cx + rx * cos(a), cy + sy * ry * sin(a)}));
  }
}

// The angle on the frame's ellipse where the ray from its middle toward
// the point (in the frame's space) meets it.
double RayAngle(const Frame& f, double px, double py) {
  double dx = px - f.cx(), dy = py - f.cy();
  bool up = !f.device && (f.dc->s.viewportExt.cy * (long long)f.dc->s.windowExt.cy < 0);
  double a = atan2(up ? dy : -dy, dx);
  double rx = f.rx(), ry = f.ry();
  if (rx <= 0 || ry <= 0) return a;
  return atan2(rx * sin(a), ry * cos(a));
}

PtF FramePoint(DCObj* dc, const Frame& f, int x, int y) {
  if (f.device) return Dev(dc, x, y);
  PtF p = {(double)x, (double)y};
  return p;
}

// An arc's start angle and sweep from its radial points.
void ArcAngles(DCObj* dc, const Frame& f, int xs, int ys, int xe, int ye, double* a0, double* sweep) {
  PtF s = FramePoint(dc, f, xs, ys), e = FramePoint(dc, f, xe, ye);
  double as = RayAngle(f, s.x, s.y), ae = RayAngle(f, e.x, e.y);
  double sw = ae - as;
  if (dc->s.arcDirection == AD_CLOCKWISE) {
    while (sw >= 0) sw -= 2 * kPi;
  } else {
    while (sw <= 0) sw += 2 * kPi;
  }
  if (fabs(ae - as) < 1e-9) sw = dc->s.arcDirection == AD_CLOCKWISE ? -2 * kPi : 2 * kPi;
  *a0 = as;
  *sweep = sw;
}

Figure EllipseFigure(const Frame& f) {
  Figure fig;
  ArcPoints(f, 0, 2 * kPi, &fig.pts);
  fig.pts.pop_back();
  fig.closed = true;
  return fig;
}

Figure RectFigure(const Frame& f) {
  Figure fig;
  fig.pts = {f.Map({f.l, f.t}), f.Map({f.r, f.t}), f.Map({f.r, f.b}), f.Map({f.l, f.b})};
  fig.closed = true;
  return fig;
}

// Flattens a cubic Bezier from p0, adding the points after p0.
void Bezier(PtF p0, PtF p1, PtF p2, PtF p3, std::vector<PtF>* out) {
  double len = hypot(p1.x - p0.x, p1.y - p0.y) + hypot(p2.x - p1.x, p2.y - p1.y) + hypot(p3.x - p2.x, p3.y - p2.y);
  int n = std::max(2, std::min(1000, (int)ceil(len / 2)));
  for (int i = 1; i <= n; i++) {
    double t = (double)i / n, u = 1 - t;
    double a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
    out->push_back({a * p0.x + b * p1.x + c * p2.x + d * p3.x, a * p0.y + b * p1.y + c * p2.y + d * p3.y});
  }
}

PtF DevPoint(DCObj* dc, POINT p) { return Dev(dc, p.x, p.y); }

POINT LogicalOf(DCObj* dc, PtF p) { return dc->ToLogical(RoundI(p.x), RoundI(p.y)); }

// Lines drawn from the current position: in a path they extend the open
// figure.
BOOL LinesFromCurrent(DCObj* dc, const std::vector<PtF>& pts) {
  if (pts.empty()) return TRUE;
  PtF cur = DevPoint(dc, dc->s.current);
  if (Recording(dc)) {
    if (!dc->pathFigureOpen || dc->path.empty()) {
      Figure f;
      f.pts.push_back(cur);
      dc->path.push_back(f);
      dc->pathFigureOpen = true;
    }
    for (const PtF& p : pts) dc->path.back().pts.push_back(p);
    return TRUE;
  }
  std::vector<Figure> figs(1);
  figs[0].pts.push_back(cur);
  figs[0].pts.insert(figs[0].pts.end(), pts.begin(), pts.end());
  return DrawFigures(dc, figs, false, true);
}

}  // namespace

// ---------------------------------------------------------------------
// Lines

BOOL WINAPI MoveToEx(HDC hdc, int x, int y, LPPOINT old) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return FALSE;
  if (old) *old = dc->s.current;
  dc->s.current = {x, y};
  if (Recording(dc)) {
    Figure f;
    f.pts.push_back(Dev(dc, x, y));
    if (dc->pathFigureOpen && !dc->path.empty() && dc->path.back().pts.size() == 1)
      dc->path.back() = f;
    else
      dc->path.push_back(f);
    dc->pathFigureOpen = true;
  }
  return TRUE;
}

BOOL WINAPI GetCurrentPositionEx(HDC hdc, LPPOINT p) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !p) return FALSE;
  *p = dc->s.current;
  return TRUE;
}

BOOL WINAPI LineTo(HDC hdc, int x, int y) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return FALSE;
  BOOL ok = LinesFromCurrent(dc, {Dev(dc, x, y)});
  dc->s.current = {x, y};
  return ok;
}

BOOL WINAPI Polyline(HDC hdc, const POINT* pts, int n) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !pts || n < 2) return FALSE;
  std::vector<Figure> f(1);
  for (int i = 0; i < n; i++) f[0].pts.push_back(DevPoint(dc, pts[i]));
  return DrawFigures(dc, f, false, true);
}

BOOL WINAPI PolylineTo(HDC hdc, const POINT* pts, DWORD n) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !pts) return FALSE;
  if (!n) return TRUE;
  std::vector<PtF> p;
  for (DWORD i = 0; i < n; i++) p.push_back(DevPoint(dc, pts[i]));
  BOOL ok = LinesFromCurrent(dc, p);
  dc->s.current = pts[n - 1];
  return ok;
}

BOOL WINAPI PolyPolyline(HDC hdc, const POINT* pts, const DWORD* counts, DWORD n) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !pts || !counts) return FALSE;
  std::vector<Figure> figs;
  for (DWORD i = 0; i < n; i++) {
    if (counts[i] < 2) return FALSE;
    Figure f;
    for (DWORD j = 0; j < counts[i]; j++) f.pts.push_back(DevPoint(dc, *pts++));
    figs.push_back(f);
  }
  return DrawFigures(dc, figs, false, true);
}

BOOL WINAPI Polygon(HDC hdc, const POINT* pts, int n) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !pts || n < 2) return FALSE;
  std::vector<Figure> f(1);
  for (int i = 0; i < n; i++) f[0].pts.push_back(DevPoint(dc, pts[i]));
  f[0].closed = true;
  return DrawFigures(dc, f, true, true);
}

BOOL WINAPI PolyPolygon(HDC hdc, const POINT* pts, const INT* counts, int n) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !pts || !counts || n <= 0) return FALSE;
  std::vector<Figure> figs;
  for (int i = 0; i < n; i++) {
    if (counts[i] < 2) return FALSE;
    Figure f;
    for (int j = 0; j < counts[i]; j++) f.pts.push_back(DevPoint(dc, *pts++));
    f.closed = true;
    figs.push_back(f);
  }
  return DrawFigures(dc, figs, true, true);
}

BOOL WINAPI PolyBezier(HDC hdc, const POINT* pts, DWORD n) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !pts || n < 4 || (n - 1) % 3) return FALSE;
  std::vector<Figure> f(1);
  PtF p0 = DevPoint(dc, pts[0]);
  f[0].pts.push_back(p0);
  for (DWORD i = 1; i + 2 < n; i += 3) {
    PtF p3 = DevPoint(dc, pts[i + 2]);
    Bezier(p0, DevPoint(dc, pts[i]), DevPoint(dc, pts[i + 1]), p3, &f[0].pts);
    p0 = p3;
  }
  return DrawFigures(dc, f, false, true);
}

BOOL WINAPI PolyBezierTo(HDC hdc, const POINT* pts, DWORD n) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !pts || n % 3) return FALSE;
  if (!n) return TRUE;
  std::vector<PtF> out;
  PtF p0 = DevPoint(dc, dc->s.current);
  for (DWORD i = 0; i + 2 < n; i += 3) {
    PtF p3 = DevPoint(dc, pts[i + 2]);
    Bezier(p0, DevPoint(dc, pts[i]), DevPoint(dc, pts[i + 1]), p3, &out);
    p0 = p3;
  }
  BOOL ok = LinesFromCurrent(dc, out);
  dc->s.current = pts[n - 1];
  return ok;
}

BOOL WINAPI PolyDraw(HDC hdc, const POINT* pts, const BYTE* types, int n) {
  DCObj* dc = DCOf(hdc);
  if (!dc || !pts || !types) return FALSE;
  // Check the types first, as Windows draws nothing for a bad list.
  for (int i = 0; i < n; i++) {
    BYTE t = types[i] & ~PT_CLOSEFIGURE;
    if (t == PT_BEZIERTO) {
      if (i + 2 >= n || (types[i + 1] & ~PT_CLOSEFIGURE) != PT_BEZIERTO ||
          (types[i + 2] & ~PT_CLOSEFIGURE) != PT_BEZIERTO)
        return FALSE;
      i += 2;
    } else if (t != PT_MOVETO && t != PT_LINETO) {
      return FALSE;
    }
  }
  std::vector<Figure> figs;
  bool recording = Recording(dc);
  PtF cur = DevPoint(dc, dc->s.current);
  POINT start = dc->s.current;
  auto open = [&]() -> Figure& {
    if (recording) {
      if (!dc->pathFigureOpen || dc->path.empty()) {
        Figure f;
        f.pts.push_back(cur);
        dc->path.push_back(f);
        dc->pathFigureOpen = true;
      }
      return dc->path.back();
    }
    if (figs.empty() || figs.back().closed) {
      Figure f;
      f.pts.push_back(cur);
      figs.push_back(f);
    }
    return figs.back();
  };
  for (int i = 0; i < n; i++) {
    BYTE t = types[i] & ~PT_CLOSEFIGURE;
    bool close = (types[i] & PT_CLOSEFIGURE) != 0;
    if (t == PT_MOVETO) {
      cur = DevPoint(dc, pts[i]);
      start = pts[i];
      if (recording) {
        Figure f;
        f.pts.push_back(cur);
        dc->path.push_back(f);
        dc->pathFigureOpen = true;
      } else {
        Figure f;
        f.pts.push_back(cur);
        figs.push_back(f);
      }
      dc->s.current = pts[i];
      continue;
    }
    Figure& f = open();
    if (t == PT_LINETO) {
      cur = DevPoint(dc, pts[i]);
      f.pts.push_back(cur);
      dc->s.current = pts[i];
    } else {
      PtF p3 = DevPoint(dc, pts[i + 2]);
      Bezier(cur, DevPoint(dc, pts[i]), DevPoint(dc, pts[i + 1]), p3, &f.pts);
      cur = p3;
      close = (types[i + 2] & PT_CLOSEFIGURE) != 0;
      dc->s.current = pts[i + 2];
      i += 2;
    }
    if (close) {
      f.closed = true;
      if (recording) dc->pathFigureOpen = false;
      cur = f.pts[0];
      dc->s.current = start;
    }
  }
  if (recording) return TRUE;
  return DrawFigures(dc, figs, false, true);
}

// ---------------------------------------------------------------------
// Shapes

BOOL WINAPI Rectangle(HDC hdc, int l, int t, int r, int b) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return FALSE;
  PenInfo pen = PenFor(dc);
  Frame f = BoxFrame(dc, l, t, r, b, &pen);
  return DrawFigures(dc, {RectFigure(f)}, true, true, ALTERNATE);
}

BOOL WINAPI Ellipse(HDC hdc, int l, int t, int r, int b) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return FALSE;
  PenInfo pen = PenFor(dc);
  Frame f = BoxFrame(dc, l, t, r, b, &pen);
  return DrawFigures(dc, {EllipseFigure(f)}, true, true, ALTERNATE);
}

BOOL WINAPI RoundRect(HDC hdc, int l, int t, int r, int b, int ew, int eh) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return FALSE;
  PenInfo pen = PenFor(dc);
  Frame f = BoxFrame(dc, l, t, r, b, &pen);
  double rx, ry;
  if (f.device) {
    rx = (dc->ScaleX(ew) - 1) / 2.0;
    ry = (dc->ScaleY(eh) - 1) / 2.0;
  } else {
    rx = abs(ew) / 2.0;
    ry = abs(eh) / 2.0;
  }
  rx = std::max(0.0, std::min(rx, f.rx()));
  ry = std::max(0.0, std::min(ry, f.ry()));
  if (rx < 0.5 || ry < 0.5) return DrawFigures(dc, {RectFigure(f)}, true, true, ALTERNATE);
  Figure fig;
  fig.closed = true;
  struct Corner {
    double cx, cy, a0;
  } corners[4] = {{f.r - rx, f.t + ry, 0}, {f.l + rx, f.t + ry, kPi / 2}, {f.l + rx, f.b - ry, kPi},
                  {f.r - rx, f.b - ry, 3 * kPi / 2}};
  for (const Corner& c : corners) {
    Frame cf = f;
    cf.l = c.cx - rx;
    cf.r = c.cx + rx;
    cf.t = c.cy - ry;
    cf.b = c.cy + ry;
    ArcPoints(cf, c.a0, kPi / 2, &fig.pts);
  }
  return DrawFigures(dc, {fig}, true, true, ALTERNATE);
}

namespace {

enum ArcKind { kArc, kArcTo, kChord, kPie };

BOOL ArcCore(HDC hdc, ArcKind kind, int l, int t, int r, int b, int xs, int ys, int xe, int ye) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return FALSE;
  PenInfo pen = PenFor(dc);
  Frame f = BoxFrame(dc, l, t, r, b, kind == kArc || kind == kArcTo ? NULL : &pen);
  double a0, sweep;
  ArcAngles(dc, f, xs, ys, xe, ye, &a0, &sweep);
  Figure fig;
  ArcPoints(f, a0, sweep, &fig.pts);
  switch (kind) {
    case kArc: return DrawFigures(dc, {fig}, false, true);
    case kArcTo: {
      BOOL ok = LinesFromCurrent(dc, fig.pts);
      dc->s.current = LogicalOf(dc, fig.pts.back());
      return ok;
    }
    case kChord:
      fig.closed = true;
      return DrawFigures(dc, {fig}, true, true, ALTERNATE);
    case kPie:
      fig.pts.push_back(f.Map({f.cx(), f.cy()}));
      fig.closed = true;
      return DrawFigures(dc, {fig}, true, true, ALTERNATE);
  }
  return FALSE;
}

}  // namespace

BOOL WINAPI Arc(HDC hdc, int l, int t, int r, int b, int xs, int ys, int xe, int ye) {
  return ArcCore(hdc, kArc, l, t, r, b, xs, ys, xe, ye);
}
BOOL WINAPI ArcTo(HDC hdc, int l, int t, int r, int b, int xs, int ys, int xe, int ye) {
  return ArcCore(hdc, kArcTo, l, t, r, b, xs, ys, xe, ye);
}
BOOL WINAPI Chord(HDC hdc, int l, int t, int r, int b, int xs, int ys, int xe, int ye) {
  return ArcCore(hdc, kChord, l, t, r, b, xs, ys, xe, ye);
}
BOOL WINAPI Pie(HDC hdc, int l, int t, int r, int b, int xs, int ys, int xe, int ye) {
  return ArcCore(hdc, kPie, l, t, r, b, xs, ys, xe, ye);
}

BOOL WINAPI AngleArc(HDC hdc, int x, int y, DWORD radius, FLOAT start, FLOAT sweep) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return FALSE;
  Frame f;
  f.dc = dc;
  f.device = false;
  f.l = x - (double)radius;
  f.r = x + (double)radius;
  f.t = y - (double)radius;
  f.b = y + (double)radius;
  std::vector<PtF> pts;
  // The angles count counterclockwise with y up in logical space.
  int n = Steps(radius, radius, sweep * kPi / 180);
  for (int i = 0; i <= n; i++) {
    double a = (start + sweep * i / (double)n) * kPi / 180;
    pts.push_back(Dev(dc, x + radius * cos(a), y - radius * sin(a)));
  }
  BOOL ok = LinesFromCurrent(dc, pts);
  double a = (start + sweep) * kPi / 180;
  dc->s.current = {RoundI(x + radius * cos(a)), RoundI(y - radius * sin(a))};
  return ok;
}

BOOL WINAPI LineDDA(int x0, int y0, int x1, int y1, LINEDDAPROC proc, LPARAM data) {
  if (!proc) return FALSE;
  int dx = abs(x1 - x0), dy = abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int n = std::max(dx, dy), err = n / 2;
  for (int i = 0; i < n; i++) {
    proc(x0, y0, data);
    if (dx >= dy) {
      x0 += sx;
      err -= dy;
      if (err < 0) {
        y0 += sy;
        err += dx;
      }
    } else {
      y0 += sy;
      err -= dx;
      if (err < 0) {
        x0 += sx;
        err += dy;
      }
    }
  }
  return TRUE;
}

// ---------------------------------------------------------------------
// Flood fills

BOOL WINAPI ExtFloodFill(HDC hdc, int x, int y, COLORREF color, UINT type) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return FALSE;
  POINT p = dc->ToDevice(x, y);
  DrawScope ds(dc);
  if (!ds.ok || !ds.clip.Contains(p.x, p.y)) return FALSE;
  BrushPaint brush = PaintFor(dc, (HBRUSH)dc->s.brush);
  DWORD target = ToPixel(color) & 0xFFFFFF;
  if (IsMonoDC(dc)) target = ToMono(target);
  RECT box = ds.clip.Bounds();
  auto inside = [&](int px, int py) {
    if (!ds.clip.Contains(px, py)) return false;
    DWORD v = ds.At(px, py) & 0xFFFFFF;
    return type == FLOODFILLSURFACE ? v == target : v != target;
  };
  if (!inside(p.x, p.y)) return FALSE;
  Mask m;
  m.Init(box);
  std::vector<POINT> stack;
  stack.push_back(p);
  while (!stack.empty()) {
    POINT q = stack.back();
    stack.pop_back();
    if (m.Get(q.x, q.y) || !inside(q.x, q.y)) continue;
    int l = q.x, r = q.x;
    while (l - 1 >= box.left && !m.Get(l - 1, q.y) && inside(l - 1, q.y)) l--;
    while (r + 1 < box.right && !m.Get(r + 1, q.y) && inside(r + 1, q.y)) r++;
    m.Span(q.y, l, r + 1);
    for (int dy = -1; dy <= 1; dy += 2) {
      int ny = q.y + dy;
      if (ny < box.top || ny >= box.bottom) continue;
      bool run = false;
      for (int i = l; i <= r; i++) {
        bool in = !m.Get(i, ny) && inside(i, ny);
        if (in && !run) stack.push_back({i, ny});
        run = in;
      }
    }
  }
  if (!brush.null) PaintMask(dc, ds, m, &brush, 0, 0, NULL);
  return TRUE;
}

BOOL WINAPI FloodFill(HDC hdc, int x, int y, COLORREF color) {
  return ExtFloodFill(hdc, x, y, color, FLOODFILLBORDER);
}

// ---------------------------------------------------------------------
// Paths

namespace {

DCObj* PathDC(HDC hdc, bool needClosed) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return NULL;
  if (needClosed && dc->pathState != DCObj::kPathClosed) {
    SetLastError(ERROR_CAN_NOT_COMPLETE);
    return NULL;
  }
  return dc;
}

void DropPath(DCObj* dc) {
  dc->path.clear();
  dc->pathState = DCObj::kNoPath;
  dc->pathFigureOpen = false;
}

Region MaskRegion(const Mask& m) {
  Region rgn;
  for (int y = 0; y < m.h; y++) {
    const BYTE* row = &m.v[(size_t)y * m.w];
    int x = 0;
    while (x < m.w) {
      while (x < m.w && !row[x]) x++;
      int s = x;
      while (x < m.w && row[x]) x++;
      if (x > s)
        rgn.AppendDisjoint(MakeRect(m.box.left + s, m.box.top + y, m.box.left + x, m.box.top + y + 1));
    }
  }
  rgn.Normalize();
  return rgn;
}

Region PathRegion(DCObj* dc) {
  std::vector<Figure> figs = dc->path;
  for (Figure& f : figs) f.closed = true;
  Mask m;
  m.Init(FigureBounds(figs, 1));
  FillInto(figs, dc->s.polyFill, m);
  return MaskRegion(m);
}

}  // namespace

BOOL WINAPI BeginPath(HDC hdc) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return FALSE;
  dc->path.clear();
  dc->pathState = DCObj::kPathOpen;
  dc->pathFigureOpen = false;
  return TRUE;
}

BOOL WINAPI EndPath(HDC hdc) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return FALSE;
  if (dc->pathState != DCObj::kPathOpen) {
    SetLastError(ERROR_CAN_NOT_COMPLETE);
    return FALSE;
  }
  dc->pathState = DCObj::kPathClosed;
  dc->pathFigureOpen = false;
  return TRUE;
}

BOOL WINAPI AbortPath(HDC hdc) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return FALSE;
  DropPath(dc);
  return TRUE;
}

BOOL WINAPI CloseFigure(HDC hdc) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return FALSE;
  if (dc->pathState != DCObj::kPathOpen) {
    SetLastError(ERROR_CAN_NOT_COMPLETE);
    return FALSE;
  }
  if (dc->pathFigureOpen && !dc->path.empty()) {
    dc->path.back().closed = true;
    dc->pathFigureOpen = false;
    dc->s.current = LogicalOf(dc, dc->path.back().pts[0]);
  }
  return TRUE;
}

BOOL WINAPI FillPath(HDC hdc) {
  DCObj* dc = PathDC(hdc, true);
  if (!dc) return FALSE;
  std::vector<Figure> figs = dc->path;
  for (Figure& f : figs) f.closed = true;
  DropPath(dc);
  return DrawFigures(dc, figs, true, false);
}

BOOL WINAPI StrokePath(HDC hdc) {
  DCObj* dc = PathDC(hdc, true);
  if (!dc) return FALSE;
  std::vector<Figure> figs = dc->path;
  DropPath(dc);
  return DrawFigures(dc, figs, false, true);
}

BOOL WINAPI StrokeAndFillPath(HDC hdc) {
  DCObj* dc = PathDC(hdc, true);
  if (!dc) return FALSE;
  std::vector<Figure> fill = dc->path, stroke = dc->path;
  for (Figure& f : fill) f.closed = true;
  DropPath(dc);
  // The fill takes the closed figures, the pen the figures as drawn.
  DrawFigures(dc, fill, true, false);
  return DrawFigures(dc, stroke, false, true);
}

BOOL WINAPI FlattenPath(HDC hdc) {
  // Curves are flattened as they are added.
  return PathDC(hdc, true) != NULL;
}

BOOL WINAPI WidenPath(HDC hdc) {
  DCObj* dc = PathDC(hdc, true);
  if (!dc) return FALSE;
  PenInfo pen = PenFor(dc);
  if (pen.null) return TRUE;
  Mask m;
  m.Init(FigureBounds(dc->path, pen.width + 1));
  StrokeInto(dc, dc->path, pen, m);
  for (BYTE& v : m.v) v = v ? 1 : 0;
  Region rgn = MaskRegion(m);
  dc->path.clear();
  // Each piece of the outline becomes a closed figure around its pixels.
  for (const RECT& r : rgn.Rects()) {
    Figure f;
    f.pts = {{r.left - 0.5, r.top - 0.5}, {r.right - 0.5, r.top - 0.5}, {r.right - 0.5, r.bottom - 0.5},
             {r.left - 0.5, r.bottom - 0.5}};
    f.closed = true;
    dc->path.push_back(f);
  }
  return TRUE;
}

int WINAPI GetPath(HDC hdc, LPPOINT pts, LPBYTE types, int size) {
  DCObj* dc = PathDC(hdc, true);
  if (!dc) return -1;
  int n = 0;
  for (const Figure& f : dc->path) n += (int)f.pts.size();
  if (size == 0) return n;
  if (size < n || !pts || !types) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return -1;
  }
  int i = 0;
  for (const Figure& f : dc->path) {
    for (size_t j = 0; j < f.pts.size(); j++, i++) {
      pts[i] = LogicalOf(dc, f.pts[j]);
      types[i] = j == 0 ? PT_MOVETO : PT_LINETO;
    }
    if (f.closed && !f.pts.empty()) types[i - 1] |= PT_CLOSEFIGURE;
  }
  return n;
}

HRGN WINAPI PathToRegion(HDC hdc) {
  DCObj* dc = PathDC(hdc, true);
  if (!dc) return NULL;
  Region rgn = PathRegion(dc);
  DropPath(dc);
  HRGN h = CreateRectRgn(0, 0, 0, 0);
  if (RegionObj* r = GdiAs<RegionObj>(h, OBJ_REGION)) r->rgn = rgn;
  return h;
}

BOOL WINAPI SelectClipPath(HDC hdc, int mode) {
  HRGN h = PathToRegion(hdc);
  if (!h) return FALSE;
  int r = ExtSelectClipRgn(hdc, h, mode);
  DeleteObject(h);
  return r != ERROR;
}

// ---------------------------------------------------------------------
// Odds and ends

namespace winport {

void FillFiguresDevice(DCObj* dc, const std::vector<Figure>& figs, DWORD color) {
  DrawScope ds(dc);
  if (!ds.ok) return;
  RECT box = Intersect(FigureBounds(figs, 1), ds.clip.Bounds());
  if (RectEmpty(box)) return;
  Mask m;
  m.Init(box);
  FillInto(figs, WINDING, m);
  int rop = dc->s.rop2;
  dc->s.rop2 = R2_COPYPEN;
  PaintMask(dc, ds, m, NULL, color, color, NULL);
  dc->s.rop2 = rop;
}

}  // namespace winport

BOOL WINAPI GdiComment(HDC hdc, UINT, const BYTE*) { return DCOf(hdc) != NULL; }

int WINAPI Escape(HDC hdc, int code, int, LPCSTR in, LPVOID) {
  DCObj* dc = DCOf(hdc);
  if (!dc) return 0;
  if (code == QUERYESCSUPPORT) return 0;
  return 0;
}

int WINAPI ExtEscape(HDC hdc, int, int, LPCSTR, int, LPSTR) { return DCOf(hdc) ? 0 : -1; }

BOOL WINAPI GetAspectRatioFilterEx(HDC hdc, LPSIZE s) {
  if (!DCOf(hdc) || !s) return FALSE;
  s->cx = s->cy = 0;
  return TRUE;
}

int WINAPI EnumObjects(HDC hdc, int type, GOBJENUMPROC proc, LPARAM data) {
  if (!DCOf(hdc) || !proc) return 0;
  static const COLORREF colors[] = {RGB(0, 0, 0),       RGB(255, 255, 255), RGB(255, 0, 0),   RGB(0, 255, 0),
                                    RGB(0, 0, 255),     RGB(255, 255, 0),   RGB(255, 0, 255), RGB(0, 255, 255),
                                    RGB(128, 128, 128), RGB(192, 192, 192)};
  int r = 1;
  for (COLORREF c : colors) {
    if (type == OBJ_PEN) {
      for (int style = PS_SOLID; style <= PS_DASHDOTDOT; style++) {
        LOGPEN lp = {(UINT)style, {0, 0}, c};
        if (!(r = proc(&lp, data))) return 0;
      }
    } else if (type == OBJ_BRUSH) {
      LOGBRUSH lb = {BS_SOLID, c, 0};
      if (!(r = proc(&lb, data))) return 0;
    } else {
      return 0;
    }
  }
  return r;
}
