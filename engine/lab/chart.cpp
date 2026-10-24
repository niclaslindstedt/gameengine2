// The chart builders' bodies (chart.hpp): the frame every chart shares (the title, the legend,
// the axes on round ticks), then one builder a form. A line with more points than the plot has
// pixel columns is drawn as each column's least and most (so a one-step spike still shows), while
// the spikes are found on every point.
//
// ge:summary The chart builders' bodies: the shared frame, ticks, decimation, each chart form
// ge:keywords chart ticks axis legend decimate min-max line histogram box heatmap flame spans
// ge:skill debug labs
// ge:test tests/lab_chart_test.cpp
#include "lab/chart.hpp"
#include "lab/chart_kit.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

#include "core/libm/libm.hpp"
#include "core/num.hpp"
#include "core/num_text.hpp"

namespace ge::lab {

using namespace kit;  // the builders' shared kit (chart_kit.hpp)

Chart line_chart(const LineChart& spec) {
  Chart c;
  c.title = spec.frame.title;
  c.plot.width = spec.frame.width;
  c.plot.height = spec.frame.height;
  std::vector<std::string> legend;
  Range xr, yr;
  for (const Trace& tr : spec.traces) {
    legend.push_back(tr.label);
    for (size_t i = 0; i < tr.x.size() && i < tr.y.size(); i++) {
      if (spec.log_y && !(tr.y[i] > 0)) continue;
      xr.add(tr.x[i]);
      yr.add(tr.y[i]);
    }
  }
  for (const double r : spec.rules) yr.add(r);
  xr.settle(false);
  yr.settle(!spec.log_y);
  if (spec.log_y) {
    yr.lo = std::max(yr.lo, 1e-12);
    yr.hi = std::max(yr.hi, yr.lo * 10);
  }
  Area a{0, draw_head(c.plot, spec.frame, legend), spec.frame.width - 16.0,
         spec.frame.height - 40.0};
  a.x0 = xr.lo;
  a.x1 = xr.hi;
  a.y0 = yr.lo;
  a.y1 = yr.hi;
  a.log_y = spec.log_y;
  draw_axes(c.plot, a, spec.frame);

  for (const Band& band : spec.bands) {
    const double x0 = std::clamp(a.px(band.x0), a.l, a.r);
    const double x1 = std::clamp(a.px(band.x1), a.l, a.r);
    c.plot.rect(x0, a.t, std::max(1.0, x1 - x0), a.b - a.t, kBand, band.label);
  }
  // Band labels once a label, at the first stretch wide enough to hold it.
  std::map<std::string, bool> labelled;
  for (const Band& band : spec.bands) {
    const double w = a.px(band.x1) - a.px(band.x0);
    if (band.label.empty() || labelled[band.label] || w < text_width(band.label) + 4) continue;
    labelled[band.label] = true;
    c.plot.text(a.px(band.x0) + 2, a.t + 2, band.label, kInk2);
  }
  const bool mark_labels = spec.marks.size() <= 40;
  for (size_t i = 0; i < spec.marks.size(); i++) {
    const double x = a.px(spec.marks[i].x);
    if (x < a.l || x > a.r) continue;
    c.plot.line(x, a.t, x, a.b, with_alpha(kInk2, 90));
    if (mark_labels && !spec.marks[i].label.empty()) {
      c.plot.text(x + 2, a.t + 12 + static_cast<double>(i % 3) * 10, spec.marks[i].label, kInk2);
    }
  }
  for (const double r : spec.rules) dashed(c.plot, a.l, a.r, a.py(r), kInk2);

  size_t rings = 0;
  const auto columns = static_cast<size_t>(std::max(1.0, a.r - a.l));
  for (size_t k = 0; k < spec.traces.size(); k++) {
    const Trace& tr = spec.traces[k];
    const Rgba col = series_colour(k);
    const size_t n = std::min(tr.x.size(), tr.y.size());
    c.summaries.emplace_back(tr.label, summarize(tr.y));
    if (spec.points) {
      const size_t stride = std::max<size_t>(1, n / 6000);
      for (size_t i = 0; i < n; i += stride) {
        if (!std::isfinite(tr.y[i]) || (spec.log_y && !(tr.y[i] > 0))) continue;
        c.plot.disc(a.px(tr.x[i]), a.py(tr.y[i]), 2, with_alpha(col, 200));
      }
    } else if (n > 2 * columns) {
      // Each pixel column's least and most, in order, so a spike one step wide still shows.
      std::vector<double> pts;
      auto flush = [&] {
        if (pts.size() >= 4) c.plot.polyline(pts, col, 1.5);
        pts.clear();
      };
      size_t i = 0;
      while (i < n) {
        const auto col_of = [&](size_t j) {
          return static_cast<long>((a.px(tr.x[j]) - a.l) / (a.r - a.l) *
                                   static_cast<double>(columns));
        };
        const long here = col_of(i);
        double lo = std::numeric_limits<double>::infinity(), hi = -lo;
        size_t lo_i = i, hi_i = i;
        bool any = false;
        size_t j = i;
        for (; j < n && col_of(j) == here; j++) {
          const double v = tr.y[j];
          if (!std::isfinite(v) || (spec.log_y && !(v > 0))) continue;
          any = true;
          if (v < lo) lo = v, lo_i = j;
          if (v > hi) hi = v, hi_i = j;
        }
        if (!any) {
          flush();
        } else {
          const size_t first = std::min(lo_i, hi_i), second = std::max(lo_i, hi_i);
          pts.push_back(a.px(tr.x[first]));
          pts.push_back(a.py(tr.y[first]));
          if (second != first) {
            pts.push_back(a.px(tr.x[second]));
            pts.push_back(a.py(tr.y[second]));
          }
        }
        i = std::max(j, i + 1);
      }
      flush();
    } else {
      std::vector<double> pts;
      for (size_t i = 0; i < n; i++) {
        if (!std::isfinite(tr.y[i]) || (spec.log_y && !(tr.y[i] > 0))) {
          if (pts.size() >= 4) c.plot.polyline(pts, col, 2);
          pts.clear();
          continue;
        }
        pts.push_back(a.px(tr.x[i]));
        pts.push_back(a.py(tr.y[i]));
      }
      if (pts.size() >= 4) c.plot.polyline(pts, col, 2);
      if (pts.size() == 2) c.plot.disc(pts[0], pts[1], 2, col);
    }
    if (!spec.spikes) continue;
    std::vector<double> ys(tr.y.begin(), tr.y.begin() + static_cast<long>(n));
    const Summary& sum = c.summaries.back().second;
    const auto flagged = spec.points ? outliers(ys) : spikes(ys);
    for (const size_t i : flagged) {
      if (spec.log_y && !(ys[i] > 0)) continue;
      Finding f{tr.label, spec.points ? "outlier" : "spike", tr.x[i], ys[i],
                tr.label + " reads " + short_num(ys[i]) + " at x " + short_num(tr.x[i]) +
                    (spec.points ? "; the set's p50 is "
                                 : "; out of line with its neighbours, "
                                   "the run's p50 is ") +
                    short_num(sum.p50)};
      ring_finding(c, a, std::move(f), rings);
    }
  }
  return c;
}

Chart histogram_chart(const ChartFrame& frame, const std::vector<Sample>& samples, int bins) {
  Chart c;
  c.title = frame.title;
  c.plot.width = frame.width;
  c.plot.height = frame.height;
  bins = std::max(1, bins);
  Range xr;
  std::vector<std::string> legend;
  for (const Sample& s : samples) {
    legend.push_back(s.label);
    for (const double v : s.values) xr.add(v);
  }
  xr.settle(false);
  const double width = (xr.hi - xr.lo) / bins;
  std::vector<std::vector<double>> counts(samples.size(), std::vector<double>(bins, 0));
  double most = 1;
  for (size_t k = 0; k < samples.size(); k++) {
    for (const double v : samples[k].values) {
      if (!std::isfinite(v)) continue;
      const int b = std::clamp(static_cast<int>((v - xr.lo) / width), 0, bins - 1);
      most = std::max(most, ++counts[k][static_cast<size_t>(b)]);
    }
  }
  Area a{0, draw_head(c.plot, frame, legend), frame.width - 16.0, frame.height - 40.0};
  a.x0 = xr.lo;
  a.x1 = xr.hi;
  a.y0 = 0;
  a.y1 = most * 1.05;
  ChartFrame f = frame;
  if (f.y_label.empty()) f.y_label = "count";
  draw_axes(c.plot, a, f);
  const uint8_t alpha = samples.size() > 1 ? 140 : 220;
  size_t rings = 0;
  for (size_t k = 0; k < samples.size(); k++) {
    const Rgba col = with_alpha(series_colour(k), alpha);
    for (int b = 0; b < bins; b++) {
      const double n = counts[k][static_cast<size_t>(b)];
      if (n <= 0) continue;
      const double x0 = a.px(xr.lo + b * width) + 1;
      const double x1 = a.px(xr.lo + (b + 1) * width) - 1;
      c.plot.rect(x0, a.py(n), std::max(1.0, x1 - x0), a.b - a.py(n), col,
                  samples[k].label + ": " + short_num(n) + " in [" + short_num(xr.lo + b * width) +
                      ", " + short_num(xr.lo + (b + 1) * width) + ")");
    }
    const Summary sum = summarize(samples[k].values);
    c.summaries.emplace_back(samples[k].label, sum);
    const double p50 = a.px(sum.p50), p95 = a.px(sum.p95);
    c.plot.line(p50, a.t, p50, a.b, series_colour(k), 1);
    for (int i = 0; a.t + i * 8.0 < a.b; i++) {
      const double y = a.t + i * 8.0;
      c.plot.line(p95, y, p95, std::min(y + 4, a.b), series_colour(k));
    }
    c.plot.text(p50 + 3, a.t + 2 + 10.0 * static_cast<double>(k), "P50", kInk2);
    c.plot.text(p95 + 3, a.t + 2 + 10.0 * static_cast<double>(k), "P95", kInk2);
    for (const size_t i : outliers(samples[k].values)) {
      const double v = samples[k].values[i];
      Finding fd{samples[k].label, "outlier", static_cast<double>(i), v,
                 samples[k].label + " has " + short_num(v) + " against a p50 of " +
                     short_num(sum.p50) + " and a p95 of " + short_num(sum.p95)};
      if (rings < kMostRings) {
        c.plot.ring(a.px(v), a.b - 6, 5, kRing,
                    "outlier " + samples[k].label + ": " + short_num(v));
        rings++;
      }
      c.findings.push_back(std::move(fd));
    }
  }
  return c;
}

Chart box_chart(const ChartFrame& frame, const std::vector<Sample>& samples) {
  Chart c;
  c.title = frame.title;
  constexpr double kRow = 26;
  c.plot.width = frame.width;
  const double head = 58 + (frame.subtitle.empty() ? 0 : 14);
  c.plot.height = static_cast<int>(head + kRow * static_cast<double>(samples.size()) + 40);
  draw_head(c.plot, frame, {});
  Range xr;
  int label_w = 0;
  for (const Sample& s : samples) {
    for (const double v : s.values) xr.add(v);
    label_w = std::max(label_w, text_width(s.label));
  }
  xr.settle(true);
  Area a{16.0 + label_w + 12, head, frame.width - 200.0,
         head + kRow * static_cast<double>(samples.size())};
  a.x0 = xr.lo;
  a.x1 = xr.hi;
  const Ticks xt = nice_ticks(a.x0, a.x1, std::max(2, static_cast<int>((a.r - a.l) / 90)));
  for (const double v : xt.at) {
    const double x = a.px(v);
    c.plot.line(x, a.t, x, a.b, kGrid);
    const std::string s = tick_label(v, xt.decimals);
    c.plot.text(x - text_width(s) / 2.0, a.b + 7, s, kInk2);
  }
  c.plot.line(a.l, a.b, a.r, a.b, kAxis);
  if (!frame.x_label.empty()) {
    c.plot.text((a.l + a.r) / 2 - text_width(frame.x_label) / 2.0, a.b + 20, frame.x_label, kInk2);
  }
  c.plot.text(a.r + 12, a.t - 12, "P50 / P95 / MAX", kInk2);
  size_t rings = 0;
  for (size_t k = 0; k < samples.size(); k++) {
    const Sample& s = samples[k];
    std::vector<double> v;
    for (const double x : s.values) {
      if (std::isfinite(x)) v.push_back(x);
    }
    std::sort(v.begin(), v.end());
    const Summary sum = summarize(s.values);
    c.summaries.emplace_back(s.label, sum);
    const double y = a.t + kRow * static_cast<double>(k) + kRow / 2;
    const Rgba col = series_colour(k);
    c.plot.text(16, y - 3, s.label, kInk);
    c.plot.text(a.r + 12, y - 3,
                short_num(sum.p50) + " / " + short_num(sum.p95) + " / " + short_num(sum.max),
                kInk2);
    if (v.empty()) continue;
    const double p5 = quantile_sorted(v, 0.05), p25 = quantile_sorted(v, 0.25);
    const double p75 = quantile_sorted(v, 0.75), p95 = quantile_sorted(v, 0.95);
    c.plot.line(a.px(p5), y, a.px(p25), y, kInk2);
    c.plot.line(a.px(p75), y, a.px(p95), y, kInk2);
    c.plot.line(a.px(p5), y - 4, a.px(p5), y + 4, kInk2);
    c.plot.line(a.px(p95), y - 4, a.px(p95), y + 4, kInk2);
    c.plot.rect(a.px(p25), y - 8, std::max(1.0, a.px(p75) - a.px(p25)), 16, with_alpha(col, 110),
                s.label + ": p25 " + short_num(p25) + ", p50 " + short_num(sum.p50) + ", p75 " +
                    short_num(p75));
    c.plot.line(a.px(sum.p50), y - 8, a.px(sum.p50), y + 8, kInk, 2);
    for (const size_t i : outliers(s.values)) {
      const double x = s.values[i];
      c.plot.disc(a.px(x), y, 2.5, kInk);
      if (rings < kMostRings) {
        c.plot.ring(a.px(x), y, 6, kRing, "outlier " + s.label + ": " + short_num(x));
        rings++;
      }
      c.findings.push_back(Finding{s.label, "outlier", static_cast<double>(i), x,
                                   s.label + " has " + short_num(x) + " against a p50 of " +
                                       short_num(sum.p50) + " and a p95 of " + short_num(sum.p95)});
    }
  }
  return c;
}

Chart heatmap_chart(const ChartFrame& frame, const std::vector<std::string>& rows,
                    const std::vector<double>& values, double x0, double x1) {
  Chart c;
  c.title = frame.title;
  c.plot.width = frame.width;
  const size_t nr = std::max<size_t>(1, rows.size());
  const size_t nc = values.size() / nr;
  const double head = 58 + (frame.subtitle.empty() ? 0 : 14);
  const double row_h = std::clamp(220.0 / static_cast<double>(nr), 8.0, 24.0);
  // Tall enough for the scale at the right (120 px of ramp and its labels).
  c.plot.height = static_cast<int>(head + std::max(150.0, row_h * static_cast<double>(nr) + 40));
  draw_head(c.plot, frame, {});
  int label_w = 0;
  for (const std::string& r : rows) label_w = std::max(label_w, text_width(r));
  Area a{16.0 + label_w + 10, head, frame.width - 90.0, head + row_h * static_cast<double>(nr)};
  a.x0 = x0;
  a.x1 = x1 > x0 ? x1 : x0 + 1;
  Range vr;
  for (const double v : values) vr.add(v);
  vr.settle(false);
  // One hue, light to dark.
  const auto shade = [&](double v) {
    if (!std::isfinite(v)) return Rgba{200, 200, 200};
    const double t = std::clamp((v - vr.lo) / (vr.hi - vr.lo), 0.0, 1.0);
    const auto mix = [t](double lo, double hi) {
      return static_cast<uint8_t>(core::num::round(lo + (hi - lo) * t));
    };
    return Rgba{mix(236, 10), mix(243, 52), mix(251, 120)};
  };
  // At most one cell a pixel column: a column of cells narrower than a pixel is drawn at its most.
  const size_t cols_px = static_cast<size_t>(std::max(1.0, a.r - a.l));
  const size_t step = std::max<size_t>(1, (nc + cols_px - 1) / cols_px);
  const size_t cells = (nc + step - 1) / step;
  const double cell_w = (a.r - a.l) / static_cast<double>(cells);
  size_t rings = 0;
  for (size_t r = 0; r < nr && r < rows.size(); r++) {
    const double y = a.t + row_h * static_cast<double>(r);
    c.plot.text(16, y + row_h / 2 - 3, rows[r], kInk);
    std::vector<double> row(values.begin() + static_cast<long>(r * nc),
                            values.begin() + static_cast<long>((r + 1) * nc));
    c.summaries.emplace_back(rows[r], summarize(row));
    for (size_t k = 0, cell = 0; k < nc; k += step, cell++) {
      double v = row[k];
      for (size_t j = k + 1; j < std::min(nc, k + step); j++) {
        if (std::isfinite(row[j]) && (!std::isfinite(v) || row[j] > v)) v = row[j];
      }
      c.plot.rect(a.l + cell_w * static_cast<double>(cell), y, cell_w + 0.5, row_h - 1, shade(v));
    }
    const Summary& sum = c.summaries.back().second;
    for (const size_t k : outliers(row)) {
      if (!(row[k] > sum.p50)) continue;
      const double x = x0 + (a.x1 - x0) * (static_cast<double>(k) + 0.5) / static_cast<double>(nc);
      if (rings < kMostRings) {
        c.plot.ring(a.px(x), y + row_h / 2, std::min(6.0, row_h / 2), kRing,
                    "hot cell " + rows[r] + ": " + short_num(row[k]) + " at x " + short_num(x));
        rings++;
      }
      c.findings.push_back(Finding{rows[r], "hot cell", x, row[k],
                                   rows[r] + " reads " + short_num(row[k]) + " at x " +
                                       short_num(x) + "; its row's p50 is " + short_num(sum.p50)});
    }
  }
  const Ticks xt = nice_ticks(a.x0, a.x1, std::max(2, static_cast<int>((a.r - a.l) / 90)));
  for (const double v : xt.at) {
    const double x = a.px(v);
    c.plot.line(x, a.b, x, a.b + 4, kAxis);
    const std::string s = tick_label(v, xt.decimals);
    c.plot.text(x - text_width(s) / 2.0, a.b + 7, s, kInk2);
  }
  if (!frame.x_label.empty()) {
    c.plot.text((a.l + a.r) / 2 - text_width(frame.x_label) / 2.0, a.b + 20, frame.x_label, kInk2);
  }
  // The scale: the ramp, its ends labelled.
  for (int i = 0; i < 40; i++) {
    const double v = vr.lo + (vr.hi - vr.lo) * (39 - i) / 39.0;
    c.plot.rect(a.r + 16, a.t + i * 3.0, 14, 3, shade(v));
  }
  c.plot.text(a.r + 34, a.t, short_num(vr.hi), kInk2);
  c.plot.text(a.r + 34, a.t + 112, short_num(vr.lo), kInk2);
  if (!frame.y_label.empty()) c.plot.text(a.r + 16, a.t + 126, frame.y_label, kInk2);
  return c;
}

Chart span_chart(const ChartFrame& frame, const std::vector<std::string>& names,
                 const std::vector<double>& depth, const std::vector<double>& start_ns,
                 const std::vector<double>& dur_ns) {
  Chart c;
  c.title = frame.title;
  c.plot.width = frame.width;
  const size_t n = std::min({names.size(), depth.size(), start_ns.size(), dur_ns.size()});
  double t0 = std::numeric_limits<double>::infinity(), t1 = -t0;
  int deepest = 0;
  std::vector<std::string> order;  // names by first appearance: the colour key
  std::map<std::string, std::vector<double>> by_name;
  for (size_t i = 0; i < n; i++) {
    if (!std::isfinite(start_ns[i]) || !std::isfinite(dur_ns[i])) continue;
    t0 = std::min(t0, start_ns[i]);
    t1 = std::max(t1, start_ns[i] + dur_ns[i]);
    deepest = std::max(deepest, static_cast<int>(depth[i]));
    auto& durs = by_name[names[i]];
    if (durs.empty()) order.push_back(names[i]);
    durs.push_back(dur_ns[i] / 1e6);
  }
  if (!(t1 > t0)) t0 = 0, t1 = 1;
  constexpr double kRow = 18;
  std::vector<std::string> legend(order.begin(), order.end());
  const size_t legend_rows = 1 + order.size() / 6;
  const double top_guess =
      58 + (frame.subtitle.empty() ? 0 : 14) + 14.0 * static_cast<double>(legend_rows);
  c.plot.height = static_cast<int>(top_guess + kRow * (deepest + 1) + 56);
  Area a{16, draw_head(c.plot, frame, legend), frame.width - 16.0, 0};
  a.b = a.t + kRow * (deepest + 1);
  a.x0 = 0;
  a.x1 = (t1 - t0) / 1e6;
  const Ticks xt = nice_ticks(a.x0, a.x1, std::max(2, static_cast<int>((a.r - a.l) / 90)));
  for (const double v : xt.at) {
    const double x = a.px(v);
    c.plot.line(x, a.t, x, a.b, kGrid);
    const std::string s = tick_label(v, xt.decimals);
    c.plot.text(x - text_width(s) / 2.0, a.b + 7, s, kInk2);
  }
  c.plot.line(a.l, a.b, a.r, a.b, kAxis);
  const std::string xl = frame.x_label.empty() ? "t (ms)" : frame.x_label;
  c.plot.text((a.l + a.r) / 2 - text_width(xl) / 2.0, a.b + 20, xl, kInk2);
  std::map<std::string, size_t> colour_of;
  for (size_t i = 0; i < order.size(); i++) colour_of[order[i]] = i;
  std::map<std::string, Summary> sums;
  for (const std::string& name : order) {
    sums[name] = summarize(by_name[name]);
    c.summaries.emplace_back(name + " (ms)", sums[name]);
  }
  size_t hidden = 0, rings = 0;
  for (size_t i = 0; i < n; i++) {
    if (!std::isfinite(start_ns[i]) || !std::isfinite(dur_ns[i])) continue;
    const double ms0 = (start_ns[i] - t0) / 1e6;
    const double ms = dur_ns[i] / 1e6;
    const double x = a.px(ms0);
    const double w = a.px(ms0 + ms) - x;
    const double y = a.t + kRow * depth[i];
    if (w < 0.5) {
      hidden++;
    } else {
      c.plot.rect(x, y + 1, std::max(1.0, w - 1), kRow - 2,
                  with_alpha(series_colour(colour_of[names[i]]), 210),
                  names[i] + ": " + short_num(ms) + " ms at " + short_num(ms0) + " ms");
      if (w > text_width(names[i]) + 6) c.plot.text(x + 3, y + 5, names[i], kInk);
    }
  }
  for (const std::string& name : order) {
    const std::vector<double>& durs = by_name[name];
    for (const size_t k : outliers(durs)) {
      if (!(durs[k] > sums[name].p50)) continue;
      // The k-th span of this name, found again for its place.
      size_t seen = 0;
      for (size_t i = 0; i < n; i++) {
        if (names[i] != name || !std::isfinite(dur_ns[i])) continue;
        if (seen++ != k) continue;
        const double ms0 = (start_ns[i] - t0) / 1e6;
        if (rings < kMostRings) {
          c.plot.ring(a.px(ms0 + durs[k] / 2), a.t + kRow * depth[i] + kRow / 2, 7, kRing,
                      "slow span " + name + ": " + short_num(durs[k]) + " ms");
          rings++;
        }
        c.findings.push_back(Finding{name, "slow span", ms0, durs[k],
                                     name + " took " + short_num(durs[k]) + " ms at " +
                                         short_num(ms0) + " ms; its p50 is " +
                                         short_num(sums[name].p50) + " ms"});
        break;
      }
    }
  }
  if (hidden) {
    c.plot.text(16, c.plot.height - 14.0,
                std::to_string(hidden) +
                    " spans under half a pixel wide are not drawn (chart a "
                    "shorter window, or read the box chart)",
                kInk2);
  }
  return c;
}

std::string chart_text(const Chart& chart, size_t most) {
  std::string out = "chart: " + chart.title + "\n";
  for (const auto& [label, s] : chart.summaries) {
    out += "  " + label + ": n " + std::to_string(s.n) + ", min " + short_num(s.min) + ", p50 " +
           short_num(s.p50) + ", p95 " + short_num(s.p95) + ", p99 " + short_num(s.p99) + ", max " +
           short_num(s.max) + (s.nan ? ", " + std::to_string(s.nan) + " nan" : "") + "\n";
  }
  if (chart.findings.empty()) {
    out += "  no outliers\n";
    return out;
  }
  if (most == 0) {
    out += "  " + std::to_string(chart.findings.size()) + " ringed\n";
    return out;
  }
  for (size_t i = 0; i < chart.findings.size() && i < most; i++) {
    out += "  " + chart.findings[i].kind + ": " + chart.findings[i].note + "\n";
  }
  if (chart.findings.size() > most) {
    out += "  ... and " + std::to_string(chart.findings.size() - most) + " more\n";
  }
  return out;
}

}  // namespace ge::lab
