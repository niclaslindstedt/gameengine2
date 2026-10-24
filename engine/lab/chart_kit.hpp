// THE CHART KIT: what every chart builder shares, so a chart drawn outside chart.cpp (the load
// charts, load_chart.hpp) wears the same look: the categorical colours and the two inks, round
// ticks on linear and log axes, the plot area's mapping, the title-subtitle-legend head, the axes
// and their labels, dashed rules and the red rings. Internal to engine/lab's chart builders.
//
// ge:summary The chart builders' shared kit: colours, inks, ticks, the area, the head, the axes
// ge:keywords chart kit ticks nice-ticks axis legend head area colours series-colour ring
// ge:skill debug labs
// ge:test tests/lab_chart_test.cpp
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "core/libm/libm.hpp"
#include "core/num.hpp"
#include "core/num_text.hpp"
#include "lab/chart.hpp"

namespace ge::lab::kit {

// The categorical order, light surface; red is kept for the rings.
inline constexpr Rgba kSeries[] = {{42, 120, 214},  {235, 104, 52}, {27, 175, 122}, {237, 161, 0},
                                   {232, 123, 164}, {0, 131, 0},    {98, 80, 214}};
inline constexpr int kSeriesCount = 7;
inline constexpr Rgba kInk{11, 11, 11};
inline constexpr Rgba kInk2{82, 81, 78};
inline constexpr Rgba kGrid{232, 231, 228};
inline constexpr Rgba kAxis{176, 174, 168};
inline constexpr Rgba kRing{208, 40, 40};
inline constexpr Rgba kBand{120, 118, 112, 34};
inline constexpr size_t kMostRings = 60;

inline Rgba series_colour(size_t i) {
  return kSeries[std::min<size_t>(i, static_cast<size_t>(kSeriesCount - 1))];
}

inline Rgba with_alpha(Rgba c, uint8_t a) { return Rgba{c.r, c.g, c.b, a}; }

inline double log10_of(double v) { return core::libm::log(v) / core::libm::log(10.0); }

// A number as a tick or a note prints it: as many decimals as its size needs.
inline std::string short_num(double v) {
  if (!std::isfinite(v)) return "nan";
  const double a = std::fabs(v);
  if (a >= 1000) return core::num::to_fixed(v, 0);
  if (a >= 100) return core::num::to_fixed(v, 1);
  if (a >= 1) return core::num::to_fixed(v, 2);
  if (a >= 0.01) return core::num::to_fixed(v, 3);
  if (a >= 0.0001) return core::num::to_fixed(v, 5);
  if (a == 0) return "0";
  return core::num::to_string(v);
}

struct Ticks {
  std::vector<double> at;
  int decimals = 0;
};

// Round ticks over [lo, hi]: steps of 1, 2 or 5 a power of ten, about `target` of them.
inline Ticks nice_ticks(double lo, double hi, int target) {
  Ticks t;
  const double span = hi - lo;
  if (!(span > 0)) return t;
  const double raw = span / target;
  double p = 1;
  while (p * 10 <= raw) p *= 10;
  while (p > raw) p /= 10;
  const double m = raw / p;
  const double step = (m < 1.5 ? 1 : m < 3 ? 2 : m < 7 ? 5 : 10) * p;
  for (double s = step; s < 1 - 1e-9 && t.decimals < 9; s *= 10) t.decimals++;
  const double first = std::ceil(lo / step - 1e-9) * step;
  for (int i = 0; i < 64; i++) {
    const double v = first + i * step;
    if (v > hi + step * 1e-9) break;
    t.at.push_back(std::fabs(v) < step * 1e-9 ? 0 : v);
  }
  return t;
}

// Decade ticks for a log axis over [lo, hi] (both > 0).
inline Ticks log_ticks(double lo, double hi) {
  Ticks t;
  double p = 1;
  while (p > lo) p /= 10;
  while (p * 10 <= lo) p *= 10;
  for (; p <= hi * 1.0000001 && t.at.size() < 32; p *= 10) {
    if (p >= lo * 0.9999999) t.at.push_back(p);
  }
  for (double s = t.at.empty() ? 1 : t.at.front(); s < 1 - 1e-9 && t.decimals < 9; s *= 10) {
    t.decimals++;
  }
  return t;
}

inline std::string tick_label(double v, int decimals) { return core::num::to_fixed(v, decimals); }

struct Range {
  double lo = std::numeric_limits<double>::infinity();
  double hi = -std::numeric_limits<double>::infinity();
  void add(double v) {
    if (!std::isfinite(v)) return;
    lo = std::min(lo, v);
    hi = std::max(hi, v);
  }
  bool empty() const { return !(hi >= lo); }
  // Open a degenerate or empty range so a flat line sits in the middle of the plot.
  void settle(bool pad) {
    if (empty()) {
      lo = 0;
      hi = 1;
      return;
    }
    if (hi == lo) {
      const double d = lo == 0 ? 1 : std::fabs(lo) * 0.1;
      lo -= d;
      hi += d;
      return;
    }
    if (pad) {
      const double d = (hi - lo) * 0.05;
      lo -= d;
      hi += d;
    }
  }
};

// The plot area and how values map into it.
struct Area {
  double l, t, r, b;
  double x0 = 0, x1 = 1, y0 = 0, y1 = 1;
  bool log_y = false;
  double px(double x) const { return l + (x - x0) / (x1 - x0) * (r - l); }
  double py(double y) const {
    const double v = log_y ? log10_of(y) : y;
    const double lo = log_y ? log10_of(y0) : y0;
    const double hi = log_y ? log10_of(y1) : y1;
    return b - (v - lo) / (hi - lo) * (b - t);
  }
};

// The title, the subtitle and the legend; returns the y the plot area may start at.
inline double draw_head(Plot& plot, const ChartFrame& f, const std::vector<std::string>& legend) {
  plot.text(16, 12, f.title, kInk, 2);
  double y = 34;
  if (!f.subtitle.empty()) {
    plot.text(16, y, f.subtitle, kInk2);
    y += 14;
  }
  if (legend.size() >= 2) {
    double x = 16;
    for (size_t i = 0; i < legend.size(); i++) {
      std::string label = legend[i];
      if (i == static_cast<size_t>(kSeriesCount - 1) && legend.size() > i + 1) {
        label += " (+" + std::to_string(legend.size() - i - 1) + " MORE, SAME COLOUR)";
      }
      if (i >= static_cast<size_t>(kSeriesCount)) break;
      const int w = text_width(label) + 28;
      if (x + w > f.width - 16 && x > 16) {
        x = 16;
        y += 14;
      }
      plot.rect(x, y, 10, 8, series_colour(i));
      plot.text(x + 14, y, label, kInk2);
      x += w;
    }
    y += 14;
  }
  return y + 10;
}

// Grid, axes and tick labels for `a`; the y ticks' labels decide the left edge, so the area's
// left is set here.
inline void draw_axes(Plot& plot, Area& a, const ChartFrame& f, bool x_numeric = true) {
  const Ticks yt = a.log_y ? log_ticks(a.y0, a.y1) : nice_ticks(a.y0, a.y1, 6);
  int widest = 0;
  for (const double v : yt.at) widest = std::max(widest, text_width(tick_label(v, yt.decimals)));
  a.l = 16 + widest + 8;
  if (!f.y_label.empty()) plot.text(a.l, a.t - 12, f.y_label, kInk2);
  for (const double v : yt.at) {
    const double y = a.py(v);
    plot.line(a.l, y, a.r, y, kGrid);
    const std::string s = tick_label(v, yt.decimals);
    plot.text(a.l - 6 - text_width(s), y - 3, s, kInk2);
  }
  if (x_numeric) {
    const Ticks xt = nice_ticks(a.x0, a.x1, std::max(2, static_cast<int>((a.r - a.l) / 90)));
    for (const double v : xt.at) {
      const double x = a.px(v);
      plot.line(x, a.b, x, a.b + 4, kAxis);
      const std::string s = tick_label(v, xt.decimals);
      plot.text(x - text_width(s) / 2.0, a.b + 7, s, kInk2);
    }
  }
  plot.line(a.l, a.b, a.r, a.b, kAxis);
  plot.line(a.l, a.t, a.l, a.b, kAxis);
  if (!f.x_label.empty()) {
    plot.text((a.l + a.r) / 2 - text_width(f.x_label) / 2.0, a.b + 20, f.x_label, kInk2);
  }
}

inline void dashed(Plot& plot, double x0, double x1, double y, Rgba c) {
  for (int i = 0; x0 + i * 10.0 < x1; i++) {
    const double x = x0 + i * 10.0;
    plot.line(x, y, std::min(x + 6, x1), y, c);
  }
}

inline std::string at_text(const std::string& series, double x, double y) {
  return series + ": " + short_num(y) + " at x " + short_num(x);
}

inline void ring_finding(Chart& c, const Area& a, Finding f, size_t& rings) {
  if (rings < kMostRings) {
    c.plot.ring(a.px(f.x), a.py(f.y), 6, kRing, f.kind + " " + at_text(f.series, f.x, f.y));
    rings++;
  }
  c.findings.push_back(std::move(f));
}

}  // namespace ge::lab::kit
