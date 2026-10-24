// The pen's arithmetic: flattening a design under a style (arcs as superellipses, stencil
// breaks, slab serifs, the proportions, the slant and the hand's wobble) and the distance
// raster that writes a cell.
//
// ge:summary Flattens a design under a style and rasterizes it by signed distance
// ge:keywords pen flatten rasterize signed-distance capsule box hull serif stencil superellipse
// ge:keywords render_glyph coverage noise rough wobble dither
// ge:skill ui
// ge:test tests/charset_charset_test.cpp
#include "charset/pen.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include "charset/charset.hpp"
#include "core/libm/libm.hpp"
#include "core/noise.hpp"
#include "core/num.hpp"

namespace ge::charset {
namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

double len(Pt a, Pt b) { return std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y)); }

// A point of a superellipse of exponent n (2 is the ellipse) at `deg`.
Pt super_point(const Piece& p, double deg, double n) {
  const double a = deg * kDegToRad;
  double c = core::libm::cos(a);
  double s = core::libm::sin(a);
  if (n != 2) {
    const double e = 2.0 / n;
    c = (c < 0 ? -1.0 : 1.0) * core::libm::pow(std::fabs(c), e);
    s = (s < 0 ? -1.0 : 1.0) * core::libm::pow(std::fabs(s), e);
  }
  return {p.cx + p.rx * c, p.cy + p.ry * s};
}

// An arc's points after its first: every multiple of `step` strictly inside the sweep, then
// its end, so a faceted O is symmetric whatever angle its arc starts at.
void flatten_arc(const Piece& p, double step, double n, std::vector<Pt>& out) {
  const double dir = p.a1 >= p.a0 ? 1.0 : -1.0;
  double k = dir > 0 ? std::floor(p.a0 / step) + 1 : std::ceil(p.a0 / step) - 1;
  for (;; k += dir) {
    const double a = k * step;
    if (dir > 0 ? a >= p.a1 - 1e-9 : a <= p.a1 + 1e-9) break;
    out.push_back(super_point(p, a, n));
  }
  out.push_back(super_point(p, p.a1, n));
}

// Squared distance from `q` to the segment a-b.
double seg_dist(Pt q, Pt a, Pt b) {
  const double dx = b.x - a.x, dy = b.y - a.y;
  const double l2 = dx * dx + dy * dy;
  double t = l2 > 0 ? ((q.x - a.x) * dx + (q.y - a.y) * dy) / l2 : 0;
  t = std::clamp(t, 0.0, 1.0);
  const double ex = q.x - (a.x + t * dx), ey = q.y - (a.y + t * dy);
  return std::sqrt(ex * ex + ey * ey);
}

double dist_to_line(Pt q, const std::vector<Pt>& line) {
  double best = 1e9;
  if (line.size() == 1) return len(q, line[0]);
  for (size_t i = 1; i < line.size(); ++i) best = std::min(best, seg_dist(q, line[i - 1], line[i]));
  return best;
}

// Whether `q` touches any line but `self`.
bool touches_other(Pt q, const std::vector<std::vector<Pt>>& lines, size_t self, double within) {
  for (size_t j = 0; j < lines.size(); ++j) {
    if (j != self && dist_to_line(q, lines[j]) < within) return true;
  }
  return false;
}

// `line` cut back by `by` units at its start (front) or end.
void shorten(std::vector<Pt>& line, double by, bool front) {
  if (front) std::reverse(line.begin(), line.end());
  while (line.size() >= 2 && by > 0) {
    Pt& end = line.back();
    const Pt prev = line[line.size() - 2];
    const double l = len(prev, end);
    if (l > by) {
      end = {end.x + (prev.x - end.x) * by / l, end.y + (prev.y - end.y) * by / l};
      by = 0;
    } else {
      by -= l;
      line.pop_back();
    }
  }
  if (front) std::reverse(line.begin(), line.end());
}

bool on_line_y(double y, std::initializer_list<double> ys) {
  for (double v : ys) {
    if (std::fabs(y - v) < 3) return true;
  }
  return false;
}

// Slab serifs on free stem ends: a horizontal slab across a near-vertical end on the baseline,
// the x-height, the cap height, an ascender or a descender; a short drop inward off a free end
// of a top or bottom bar.
void add_serifs(std::vector<std::vector<Pt>>& lines, std::vector<Pt>& dots, const Style& st) {
  const size_t n = lines.size();
  for (size_t i = 0; i < n; ++i) {
    if (lines[i].size() < 2) continue;
    for (int end = 0; end < 2; ++end) {
      const std::vector<Pt>& l = lines[i];
      const Pt p = end ? l.back() : l.front();
      const Pt q = end ? l[l.size() - 2] : l[1];
      if (len(l.front(), l.back()) < 1) continue;  // a closed path has no free end
      if (touches_other(p, lines, i, st.weight * 1.5)) continue;
      const double dx = std::fabs(q.x - p.x), dy = std::fabs(q.y - p.y);
      if (dx + dy < 16) continue;
      if (dx <= 0.65 * dy && on_line_y(p.y, {0, 74, 100, 108, -32})) {
        if (st.lozenges) {
          dots.push_back(p);
        } else {
          lines.push_back({{p.x - st.serif, p.y}, {p.x + st.serif, p.y}});
        }
      } else if (!st.lozenges && dy <= 0.2 * dx && (p.y >= 70 || p.y <= 4)) {
        const double drop = p.y >= 70 ? -st.serif * 0.9 : st.serif * 0.9;
        lines.push_back({p, {p.x, p.y + drop}});
      }
    }
  }
}

// The codes drawn through the cell's edges, which no transform may move off them.
bool tiles(uint8_t code) { return (code >= 13 && code <= 15) || (code >= 17 && code <= 25); }

struct Transform {
  const Style& st;
  uint8_t code;
  bool lower;
  Pt operator()(Pt p) const {
    if (tiles(code)) return p;
    double y = p.y;
    if (lower) {
      y = y <= 74 ? y * st.x_height / 74 : st.x_height + (y - 74) * (st.cap + 8 - st.x_height) / 34;
    } else {
      y = y * st.cap / 100;
    }
    double x = 50 + (p.x - 50) * st.width;
    if (st.jitter > 0) {
      // The glyph lifted and tilted about its middle, each by its own hash.
      const double lift = (core::hash2(code, 1, st.seed) - 0.5) * 2 * st.jitter;
      const double tilt = (core::hash2(code, 2, st.seed) - 0.5) * 0.2 * st.jitter * kDegToRad;
      const double c = core::libm::cos(tilt), s = core::libm::sin(tilt);
      const double rx = x - 50, ry = y - 40;
      x = 50 + rx * c - ry * s;
      y = 40 + rx * s + ry * c + lift;
    }
    if (st.wobble > 0) {
      const double s = st.seed * 131.0 + code * 7.0;
      x += st.wobble * (core::value_noise(p.x, p.y, 38, s) - 0.5) * 2;
      y += st.wobble * (core::value_noise(p.x, p.y, 38, s + 1) - 0.5) * 2;
    }
    x += st.slant * y;
    return {x, y};
  }
};

// A glyph too wide for its cell (an M or a W, a hand's wobble, a slant's lean) is narrowed,
// and one that leans out of it is moved back, so no ink comes within a pixel of the cell's
// side at the smallest atlas: a filtered sample never reads the neighbour.
void fit(Strokes& s, const Style& st) {
  constexpr double kMargin = kCellW / 16.0 + 0.25;  // a pixel at 256 px, and a little
  const double grow = st.pen == PenShape::square ? 1.42
                      : st.pen == PenShape::nib  ? 1 + st.nib_thin
                                                 : 1;
  const double reach = st.weight * std::max(1.0, st.dot) * grow + st.rough;
  double lo = 1e9, hi = -1e9;
  for (const auto& line : s.lines) {
    for (Pt p : line) lo = std::min(lo, p.x - reach), hi = std::max(hi, p.x + reach);
  }
  for (Pt p : s.dots) lo = std::min(lo, p.x - reach), hi = std::max(hi, p.x + reach);
  for (const auto& poly : s.fills) {
    for (Pt p : poly) lo = std::min(lo, p.x), hi = std::max(hi, p.x);
  }
  if (lo >= kMargin && hi <= kCellW - kMargin) return;
  double k = 1, shift = 0;
  if (hi - lo > kCellW - 2 * kMargin) {
    k = (kCellW - 2 * kMargin) / (hi - lo);
    shift = kCellW / 2 - (lo + hi) / 2;
  } else {
    shift = lo < kMargin ? kMargin - lo : kCellW - kMargin - hi;
  }
  const double mid = (lo + hi) / 2;
  const auto move = [&](Pt& p) { p.x = mid + shift + (p.x - mid) * k; };
  if (k < 1) {
    // The pen does not narrow with the skeleton, so the skeleton narrows more.
    const double inner = (hi - lo) - 2 * reach;
    const double room = kCellW - 2 * kMargin - 2 * reach;
    k = inner > 0 && room > 0 ? room / inner : k;
  }
  for (auto& line : s.lines) {
    for (Pt& p : line) move(p);
  }
  for (Pt& p : s.dots) move(p);
  for (auto& poly : s.fills) {
    for (Pt& p : poly) move(p);
  }
}

}  // namespace

Strokes flatten(const Design& design, const Style& st, uint8_t code) {
  Strokes out;
  out.dither = design.dither;
  const double n = st.squareness;
  std::vector<bool> ellipse;
  for (const Path& path : design.paths) {
    if (path.ellipse && st.stencil) {
      // A stencilled bowl breaks at its top and bottom.
      const Piece& p = path.pieces[0];
      const double g = (st.stencil_gap + st.weight) / std::max(p.rx, 1.0) / kDegToRad;
      for (double a : {90.0, 270.0}) {
        Piece half = p;
        half.a0 = a + g;
        half.a1 = a + 180 - g;
        std::vector<Pt> pts{super_point(half, half.a0, n)};
        flatten_arc(half, st.arc_step, n, pts);
        out.lines.push_back(std::move(pts));
        ellipse.push_back(true);
      }
      continue;
    }
    std::vector<Pt> pts{path.start};
    for (const Piece& piece : path.pieces) {
      if (piece.arc) {
        if (path.ellipse) pts[0] = super_point(piece, piece.a0, n);
        flatten_arc(piece, st.arc_step, n, pts);
      } else {
        pts.push_back(piece.to);
      }
    }
    out.lines.push_back(std::move(pts));
    ellipse.push_back(path.ellipse);
  }
  if (st.stencil && !tiles(code)) {
    // Every free end that runs into another stroke stops short of it.
    const std::vector<std::vector<Pt>> before = out.lines;
    const double cut = st.stencil_gap + 2 * st.weight;
    for (size_t i = 0; i < out.lines.size(); ++i) {
      if (ellipse[i] || before[i].size() < 2) continue;
      const bool head = touches_other(before[i].front(), before, i, st.weight);
      const bool tail = touches_other(before[i].back(), before, i, st.weight);
      if (head) shorten(out.lines[i], cut, true);
      if (tail) shorten(out.lines[i], cut, false);
    }
  }
  const bool alnum =
      (code >= '0' && code <= '9') || (code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z');
  std::vector<Pt> feet;
  if ((st.serifs || st.lozenges) && alnum) add_serifs(out.lines, feet, st);

  const bool lower = code >= 'a' && code <= 'z';
  const Transform tf{st, code, lower};
  for (auto& line : out.lines) {
    for (Pt& p : line) p = tf(p);
  }
  for (Pt p : design.dots) out.dots.push_back(tf(p));
  for (Pt p : feet) out.dots.push_back(tf(p));
  for (const auto& poly : design.fills) {
    std::vector<Pt> f;
    f.reserve(poly.size());
    for (Pt p : poly) f.push_back(tf(p));
    out.fills.push_back(std::move(f));
  }
  for (const auto& e : design.discs) {
    std::vector<Pt> f;
    for (int k = 0; k < 32; ++k) {
      const double a = k * (360.0 / 32) * kDegToRad;
      f.push_back(tf({e[0] + e[2] * core::libm::cos(a), e[1] + e[3] * core::libm::sin(a)}));
    }
    out.fills.push_back(std::move(f));
  }
  if (!tiles(code)) fit(out, st);
  return out;
}

namespace {

// A convex polygon as its edges' outward lines: the distance to it is the largest of the
// distances past them (exact inside and along an edge, a little short beyond a corner).
struct Convex {
  std::vector<double> nx, ny, c;  // n . p - c > 0 outside
  double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
};

Convex convex_of(std::vector<Pt> pts) {
  // Andrew's monotone chain: the hull, counter-clockwise.
  std::sort(pts.begin(), pts.end(),
            [](Pt a, Pt b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
  auto cross = [](Pt o, Pt a, Pt b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
  };
  std::vector<Pt> h(pts.size() * 2);
  size_t k = 0;
  for (size_t i = 0; i < pts.size(); ++i) {
    while (k >= 2 && cross(h[k - 2], h[k - 1], pts[i]) <= 0) --k;
    h[k++] = pts[i];
  }
  for (size_t i = pts.size() - 1, t = k + 1; i-- > 0;) {
    while (k >= t && cross(h[k - 2], h[k - 1], pts[i]) <= 0) --k;
    h[k++] = pts[i];
  }
  h.resize(k > 1 ? k - 1 : k);
  Convex cv;
  for (size_t i = 0; i < h.size(); ++i) {
    const Pt a = h[i], b = h[(i + 1) % h.size()];
    const double ex = b.x - a.x, ey = b.y - a.y;
    const double l = std::sqrt(ex * ex + ey * ey);
    if (l <= 0) continue;
    const double nx = ey / l, ny = -ex / l;  // outward for a counter-clockwise hull
    cv.nx.push_back(nx);
    cv.ny.push_back(ny);
    cv.c.push_back(nx * a.x + ny * a.y);
    cv.x0 = std::min(cv.x0, a.x);
    cv.y0 = std::min(cv.y0, a.y);
    cv.x1 = std::max(cv.x1, a.x);
    cv.y1 = std::max(cv.y1, a.y);
  }
  return cv;
}

double convex_dist(const Convex& cv, double x, double y) {
  double d = -1e9;
  for (size_t i = 0; i < cv.c.size(); ++i) d = std::max(d, cv.nx[i] * x + cv.ny[i] * y - cv.c[i]);
  return d;
}

struct Cell {
  int w, h;
  double upp;  // design units per pixel
  std::vector<double> dist;
  // The pixel box covering design box [x0, x1] x [y0, y1], grown by `pad` units.
  void box(double x0, double y0, double x1, double y1, double pad, int& px0, int& py0, int& px1,
           int& py1) const {
    px0 = std::max(0, static_cast<int>(std::floor((x0 - pad) / upp)));
    px1 = std::min(w - 1, static_cast<int>(std::ceil((x1 + pad) / upp)));
    py0 = std::max(0, static_cast<int>(std::floor((kCellTop - y1 - pad) / upp)));
    py1 = std::min(h - 1, static_cast<int>(std::ceil((kCellTop - y0 + pad) / upp)));
  }
  double x_of(int px) const { return (px + 0.5) * upp; }
  double y_of(int py) const { return kCellTop - (py + 0.5) * upp; }
  template <class Fn>
  void paint(double x0, double y0, double x1, double y1, double pad, Fn&& fn) {
    int px0, py0, px1, py1;
    box(x0, y0, x1, y1, pad, px0, py0, px1, py1);
    for (int py = py0; py <= py1; ++py) {
      const double y = y_of(py);
      for (int px = px0; px <= px1; ++px) {
        double& d = dist[static_cast<size_t>(py) * w + px];
        d = std::min(d, fn(x_of(px), y));
      }
    }
  }
};

void paint_segment(Cell& cell, const Style& st, Pt a, Pt b, double pad) {
  const double w = st.weight;
  const double bx0 = std::min(a.x, b.x), bx1 = std::max(a.x, b.x);
  const double by0 = std::min(a.y, b.y), by1 = std::max(a.y, b.y);
  switch (st.pen) {
    case PenShape::round:
      cell.paint(bx0, by0, bx1, by1, w + pad,
                 [&](double x, double y) { return seg_dist({x, y}, a, b) - w; });
      break;
    case PenShape::square: {
      const double l = len(a, b);
      const double ux = l > 0 ? (b.x - a.x) / l : 1, uy = l > 0 ? (b.y - a.y) / l : 0;
      const double mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2;
      const double hl = l / 2 + w;
      cell.paint(bx0, by0, bx1, by1, w * 1.5 + pad, [&](double x, double y) {
        const double lx = std::fabs((x - mx) * ux + (y - my) * uy) - hl;
        const double ly = std::fabs(-(x - mx) * uy + (y - my) * ux) - w;
        const double ox = std::max(lx, 0.0), oy = std::max(ly, 0.0);
        return std::sqrt(ox * ox + oy * oy) + std::min(std::max(lx, ly), 0.0);
      });
      break;
    }
    case PenShape::nib: {
      const double t = st.nib_angle * kDegToRad;
      const double nx = core::libm::cos(t) * w, ny = core::libm::sin(t) * w;
      const double tx = -ny * st.nib_thin, ty = nx * st.nib_thin;
      std::vector<Pt> pts;
      for (Pt p : {a, b}) {
        pts.push_back({p.x + nx + tx, p.y + ny + ty});
        pts.push_back({p.x + nx - tx, p.y + ny - ty});
        pts.push_back({p.x - nx + tx, p.y - ny + ty});
        pts.push_back({p.x - nx - tx, p.y - ny - ty});
      }
      const Convex cv = convex_of(pts);
      cell.paint(cv.x0, cv.y0, cv.x1, cv.y1, pad,
                 [&](double x, double y) { return convex_dist(cv, x, y); });
      break;
    }
  }
}

void paint_dot(Cell& cell, const Style& st, Pt p, double pad) {
  const double r = st.weight * st.dot;
  if (st.pen == PenShape::round) {
    cell.paint(p.x, p.y, p.x, p.y, r + pad, [&](double x, double y) { return len({x, y}, p) - r; });
    return;
  }
  // A square pen makes a square; a nib a lozenge on its angle.
  const double t = (st.pen == PenShape::nib ? st.nib_angle : 0) * kDegToRad;
  const double c = core::libm::cos(t) * r, s = core::libm::sin(t) * r;
  const Convex cv =
      convex_of({{p.x + c, p.y + s}, {p.x - s, p.y + c}, {p.x - c, p.y - s}, {p.x + s, p.y - c}});
  const Convex sq =
      convex_of({{p.x - r, p.y - r}, {p.x + r, p.y - r}, {p.x + r, p.y + r}, {p.x - r, p.y + r}});
  const Convex& use = st.pen == PenShape::nib ? cv : sq;
  cell.paint(use.x0, use.y0, use.x1, use.y1, pad,
             [&](double x, double y) { return convex_dist(use, x, y); });
}

}  // namespace

void render_glyph(uint8_t code, const Style& st, int w, int h, uint8_t* out, int stride) {
  render_design(design_of(code, st.variant), code, st, w, h, out, stride);
}

void render_design(const Design& design, uint8_t code, const Style& style, int w, int h,
                   uint8_t* out, int stride) {
  // The box lines and slider pieces tile edge to edge, so every font draws them with a plain
  // square pen (a broad nib at its mean width): a nib's or a rough edge's ends would not meet
  // the next cell's.
  Style st = style;
  if (tiles(code)) {
    if (st.pen == PenShape::nib) st.weight *= (1 + st.nib_thin) / 2;
    st.pen = PenShape::square;
    st.rough = 0;
  }
  Cell cell{w, h, kCellW / w, {}};
  cell.dist.assign(static_cast<size_t>(w) * h, 1e9);
  const Strokes s = flatten(design, st, code);
  const double pad = cell.upp * 1.5 + st.rough;
  for (const auto& line : s.lines) {
    if (line.size() == 1) paint_segment(cell, st, line[0], line[0], pad);
    for (size_t i = 1; i < line.size(); ++i) paint_segment(cell, st, line[i - 1], line[i], pad);
  }
  for (Pt p : s.dots) paint_dot(cell, st, p, pad);
  for (const auto& poly : s.fills) {
    const Convex cv = convex_of(poly);
    cell.paint(cv.x0, cv.y0, cv.x1, cv.y1, pad,
               [&](double x, double y) { return convex_dist(cv, x, y); });
  }
  const double seed = st.seed * 977.0;
  for (int py = 0; py < h; ++py) {
    for (int px = 0; px < w; ++px) {
      double d = cell.dist[static_cast<size_t>(py) * w + px];
      if (s.dither) {
        // The shades are pixel patterns of the cell itself, so they tile at every size.
        const bool on = s.dither == 2 ? ((px + py) & 1) == 0 : (px & 1) == 0 && (py & 1) == 0;
        out[static_cast<size_t>(py) * stride + px] = on ? 255 : 0;
        continue;
      }
      if (d > 1e8) {
        out[static_cast<size_t>(py) * stride + px] = 0;
        continue;
      }
      if (st.rough > 0) {
        const double x = cell.x_of(px), y = cell.y_of(py);
        const double nz = 0.65 * core::value_noise(x, y, st.rough_scale, seed) +
                          0.35 * core::value_noise(x, y, st.rough_scale * 0.43, seed + 1);
        d += st.rough * (nz - 0.5) * 2;
      }
      const double cov = std::clamp(0.5 - d / cell.upp, 0.0, 1.0);
      out[static_cast<size_t>(py) * stride + px] =
          static_cast<uint8_t>(core::num::round(cov * 255));
    }
  }
}

}  // namespace ge::charset
