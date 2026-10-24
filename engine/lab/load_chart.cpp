// The load charts' bodies (load_chart.hpp). A lane is drawn by pixel column: each column takes
// the act (or the work's name) that covered most of it, its opacity the share it covered, and
// neighbouring columns alike are one rectangle, so a window of a million slices draws in a few
// thousand rectangles and a single short slice still shows.
//
// ge:summary The load charts' bodies: lanes by pixel column, the fixed-scale heatmap, the bars
// ge:keywords chart load lanes columns coverage heatmap bars frames series spans
// ge:skill perf debug labs
// ge:test tests/lab_load_chart_test.cpp
#include "lab/load_chart.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>

#include "lab/chart_kit.hpp"

namespace ge::lab {

using namespace kit;  // the builders' shared kit (chart_kit.hpp)

namespace {

constexpr Rgba kOther{150, 150, 150};  // work past the six named colours
constexpr Rgba kSpin{196, 140, 30};
constexpr Rgba kSleep{196, 196, 192};
constexpr Rgba kUnknown{222, 120, 120};
constexpr Rgba kFrameEdge{200, 198, 192};
constexpr size_t kNamed = 6;  // work names given a colour of their own

// Work names, most work first over every lane: the colour key.
std::vector<std::string> work_names(const prof::Load& load) {
  std::map<std::string, int64_t> total;
  for (const prof::LaneLoad& l : load.lanes) {
    for (const auto& [name, ns] : l.work_by_name) total[name] += ns;
  }
  std::vector<std::pair<std::string, int64_t>> v(total.begin(), total.end());
  std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
    return a.second != b.second ? a.second > b.second : a.first < b.first;
  });
  std::vector<std::string> out;
  out.reserve(v.size());
  for (const auto& [name, ns] : v) out.push_back(name);
  return out;
}

size_t slot_of(const std::vector<std::string>& names, std::string_view name) {
  for (size_t i = 0; i < names.size() && i < kNamed; i++) {
    if (names[i] == name) return i;
  }
  return kNamed;
}

Rgba slot_colour(size_t slot) { return slot < kNamed ? series_colour(slot) : kOther; }

uint8_t opacity(double share) {
  // Steps of a sixteenth, so neighbouring columns of nearly the same cover merge.
  const double q = core::num::round(std::clamp(share, 0.0, 1.0) * 16) / 16;
  return static_cast<uint8_t>(core::num::round(70 + 185 * q));
}

// One row of columns: a category each (or -1, empty) and its opacity, merged into rectangles.
struct Columns {
  std::vector<int> cat;
  std::vector<uint8_t> alpha;
};

Columns columns_of(const std::vector<std::vector<int64_t>>& cover, int64_t col_ns) {
  Columns c;
  c.cat.assign(cover.size(), -1);
  c.alpha.assign(cover.size(), 0);
  for (size_t x = 0; x < cover.size(); x++) {
    int64_t sum = 0, best = 0;
    for (size_t k = 0; k < cover[x].size(); k++) {
      sum += cover[x][k];
      if (cover[x][k] > best) {
        best = cover[x][k];
        c.cat[x] = static_cast<int>(k);
      }
    }
    if (sum > 0) c.alpha[x] = opacity(static_cast<double>(sum) / static_cast<double>(col_ns));
  }
  return c;
}

void add_cover(std::vector<std::vector<int64_t>>& cover, size_t cat, int64_t t0, int64_t t1,
               int64_t from, int64_t col_ns) {
  const int64_t n = static_cast<int64_t>(cover.size());
  for (int64_t x = std::max<int64_t>(0, (t0 - from) / col_ns); x < n; x++) {
    const int64_t c0 = from + x * col_ns;
    if (c0 >= t1) break;
    const int64_t o = std::min(t1, c0 + col_ns) - std::max(t0, c0);
    if (o > 0) cover[static_cast<size_t>(x)][cat] += o;
  }
}

std::string at_ms(int64_t ns) { return short_num(static_cast<double>(ns) / 1e6); }

}  // namespace

Chart lanes_chart(const ChartFrame& frame, const std::vector<prof::Lane>& lanes,
                  const prof::Load& load, int64_t from_ns, int64_t to_ns,
                  const std::vector<prof::FrameWindow>& frames) {
  Chart c;
  c.title = frame.title;
  c.plot.width = frame.width;
  if (to_ns <= from_ns) to_ns = from_ns + 1;
  const std::vector<std::string> names = work_names(load);
  std::vector<std::string> legend;
  for (size_t i = 0; i < names.size() && i < kNamed; i++) legend.push_back(names[i]);
  if (names.size() > kNamed) legend.push_back("other work");
  if (legend.size() == 1) legend.push_back("other work");  // the head draws a key of two or more
  constexpr double kRow = 20, kPath = 9;
  int label_w = 0;
  for (const prof::Lane& l : lanes) label_w = std::max(label_w, text_width(l.name));
  label_w = std::max(label_w, text_width("critical path"));
  const size_t legend_rows = 1 + legend.size() / 6;
  const double head_guess =
      58 + (frame.subtitle.empty() ? 0 : 14) + 14.0 * static_cast<double>(legend_rows) + 14;
  const double rows_h = kRow * static_cast<double>(lanes.size()) + (load.main >= 0 ? kPath + 6 : 0);
  c.plot.height = static_cast<int>(head_guess + rows_h + 60);
  double top = draw_head(c.plot, frame, legend);
  c.plot.text(16, top - 4,
              "BARS: WORK. HALF HEIGHT AMBER: SPINNING. THIN GREY: ASLEEP. BLANK: UNTRACKED.",
              kInk2);
  top += 14;
  Area a{16.0 + label_w + 10, top, frame.width - 16.0, top + rows_h};
  a.x0 = 0;
  a.x1 = static_cast<double>(to_ns - from_ns) / 1e6;
  const size_t ncols = static_cast<size_t>(std::max(1.0, a.r - a.l));
  const int64_t col_ns = std::max<int64_t>(
      1, (to_ns - from_ns + static_cast<int64_t>(ncols) - 1) / static_cast<int64_t>(ncols));
  const double col_px = (a.r - a.l) / static_cast<double>(ncols);
  // The frames' edges first, under everything.
  for (const prof::FrameWindow& f : frames) {
    if (f.start_ns < from_ns || f.start_ns > to_ns) continue;
    const double x = a.px(static_cast<double>(f.start_ns - from_ns) / 1e6);
    c.plot.line(x, a.t, x, a.b, kFrameEdge);
  }
  // Each lane's dominant colour: what a wait on it is drawn in on the critical path.
  std::vector<Rgba> lane_colour(lanes.size(), kOther);
  for (size_t li = 0; li < lanes.size() && li < load.lanes.size(); li++) {
    if (!load.lanes[li].work_by_name.empty()) {
      lane_colour[li] = slot_colour(slot_of(names, load.lanes[li].work_by_name[0].first));
    }
  }
  double y = a.t;
  for (size_t li = 0; li < lanes.size(); li++) {
    const prof::Lane& lane = lanes[li];
    c.plot.text(16, y + kRow / 2 - 3, lane.name, kInk);
    c.plot.line(a.l, y + kRow - 0.5, a.r, y + kRow - 0.5, kGrid);
    // Categories: the named work slots and `other` (0..kNamed), then spin and sleep.
    std::vector<std::vector<int64_t>> cover(ncols, std::vector<int64_t>(kNamed + 3, 0));
    for (const prof::Segment& s : prof::flatten(lane, from_ns, to_ns)) {
      const size_t cat = s.act == prof::Act::work   ? slot_of(names, prof::zone_name(s.name))
                         : s.act == prof::Act::spin ? kNamed + 1
                                                    : kNamed + 2;
      add_cover(cover, cat, s.t0, s.t1, from_ns, col_ns);
    }
    const Columns cols = columns_of(cover, col_ns);
    for (size_t x = 0; x < ncols;) {
      size_t e = x + 1;
      while (e < ncols && cols.cat[e] == cols.cat[x] && cols.alpha[e] == cols.alpha[x]) e++;
      if (cols.cat[x] >= 0) {
        const size_t cat = static_cast<size_t>(cols.cat[x]);
        const double x0 = a.l + col_px * static_cast<double>(x);
        const double w = col_px * static_cast<double>(e - x);
        const int64_t t0 = static_cast<int64_t>(x) * col_ns, t1 = static_cast<int64_t>(e) * col_ns;
        const std::string when = " from " + at_ms(t0) + " to " + at_ms(t1) + " ms";
        if (cat <= kNamed) {
          const std::string what = cat < kNamed && cat < names.size() ? names[cat] : "other work";
          c.plot.rect(x0, y + 2, w, kRow - 5, with_alpha(slot_colour(cat), cols.alpha[x]),
                      std::string(lane.name).append(": ").append(what).append(when));
        } else if (cat == kNamed + 1) {
          c.plot.rect(x0, y + kRow / 2 - 1, w, kRow / 2 - 2, with_alpha(kSpin, cols.alpha[x]),
                      lane.name + ": spinning" + when);
        } else {
          c.plot.rect(x0, y + kRow / 2 - 2, w, 3, kSleep, lane.name + ": asleep" + when);
        }
      }
      x = e;
    }
    y += kRow;
    if (static_cast<int>(li) == load.main) {
      // The critical path: the main lane's own work, then its waits by what ended them.
      c.plot.text(16, y + 1, "critical path", kInk2);
      std::vector<std::vector<int64_t>> path(ncols, std::vector<int64_t>(lanes.size() + 2, 0));
      for (const prof::PathPiece& p : load.path) {
        const size_t cat = p.on == prof::kOwnWork       ? 0
                           : p.on == prof::kUnknownWait ? 1
                                                        : 2 + static_cast<size_t>(p.on);
        add_cover(path, cat, std::max(p.t0, from_ns), std::min(p.t1, to_ns), from_ns, col_ns);
      }
      const Columns pc = columns_of(path, col_ns);
      for (size_t x = 0; x < ncols;) {
        size_t e = x + 1;
        while (e < ncols && pc.cat[e] == pc.cat[x]) e++;
        if (pc.cat[x] >= 0) {
          const size_t cat = static_cast<size_t>(pc.cat[x]);
          const Rgba col = cat == 0 ? kInk2 : cat == 1 ? kUnknown : lane_colour[cat - 2];
          const std::string what = cat == 0   ? "the main lane's own work"
                                   : cat == 1 ? "waiting on nothing marked"
                                              : "waiting on " + lanes[cat - 2].name;
          c.plot.rect(a.l + col_px * static_cast<double>(x), y + 1,
                      col_px * static_cast<double>(e - x), kPath - 2, col,
                      "critical path: " + what);
        }
        x = e;
      }
      y += kPath + 6;
    }
  }
  const Ticks xt = nice_ticks(a.x0, a.x1, std::max(2, static_cast<int>((a.r - a.l) / 90)));
  for (const double v : xt.at) {
    const double x = a.px(v);
    c.plot.line(x, a.b, x, a.b + 4, kAxis);
    const std::string s = tick_label(v, xt.decimals);
    c.plot.text(x - text_width(s) / 2.0, a.b + 7, s, kInk2);
  }
  c.plot.line(a.l, a.b, a.r, a.b, kAxis);
  const std::string xl = frame.x_label.empty() ? "t (ms)" : frame.x_label;
  c.plot.text((a.l + a.r) / 2 - text_width(xl) / 2.0, a.b + 20, xl, kInk2);
  for (const prof::LaneLoad& l : load.lanes) {
    Summary s;
    s.n = 1;
    s.min = s.p50 = s.p95 = s.p99 = s.max = l.work_share * 100;
    c.summaries.emplace_back(l.name + " work (% of the run)", s);
  }
  return c;
}

Chart busy_chart(const ChartFrame& frame, const prof::Load& load) {
  Chart c;
  c.title = frame.title;
  c.plot.width = frame.width;
  std::vector<std::string> rows;
  std::vector<const double*> data;
  for (size_t i = 0; i < load.lanes.size(); i++) {
    rows.push_back("lane " + load.lanes[i].name);
    data.push_back(load.lane_bins.data() + i * load.bins);
  }
  for (size_t i = 0; i < load.os_threads.size() && i < 12; i++) {
    const prof::OsThreadLoad& t = load.os_threads[i];
    rows.push_back("os " + (t.name.empty() ? std::to_string(t.os_id) : t.name) +
                   (t.lane < 0 ? " *" : ""));
    data.push_back(load.os_bins.data() + i * load.bins);
  }
  const size_t ncores = load.bins ? load.core_bins.size() / load.bins : 0;
  for (size_t i = 0; i < ncores; i++) {
    rows.push_back(load.whole_machine ? "the machine" : "core " + std::to_string(i));
    data.push_back(load.core_bins.data() + i * load.bins);
  }
  const double head = 58 + (frame.subtitle.empty() ? 0 : 14);
  const double row_h = 14;
  c.plot.height =
      static_cast<int>(head + std::max(150.0, row_h * static_cast<double>(rows.size()) + 48));
  draw_head(c.plot, frame, {});
  int label_w = 0;
  for (const std::string& r : rows) label_w = std::max(label_w, text_width(r));
  Area a{16.0 + label_w + 10, head, frame.width - 90.0,
         head + row_h * static_cast<double>(rows.size())};
  a.x0 = 0;
  a.x1 = static_cast<double>(load.to_ns - load.from_ns) / 1e6;
  const auto shade = [](double v) {
    const double t = std::clamp(v, 0.0, 1.0);
    const auto mix = [t](double lo, double hi) {
      return static_cast<uint8_t>(core::num::round(lo + (hi - lo) * t));
    };
    return Rgba{mix(244, 10), mix(246, 52), mix(250, 120)};
  };
  const size_t cols_px = static_cast<size_t>(std::max(1.0, a.r - a.l));
  const size_t step = std::max<size_t>(1, (load.bins + cols_px - 1) / cols_px);
  const size_t cells = load.bins ? (load.bins + step - 1) / step : 0;
  const double cell_w = cells ? (a.r - a.l) / static_cast<double>(cells) : 0;
  for (size_t r = 0; r < rows.size(); r++) {
    const double y = a.t + row_h * static_cast<double>(r);
    c.plot.text(16, y + row_h / 2 - 3, rows[r], kInk);
    std::vector<double> all(data[r], data[r] + load.bins);
    c.summaries.emplace_back(rows[r] + " (share)", summarize(all));
    for (size_t k = 0, cell = 0; k < load.bins; k += step, cell++) {
      // A column of several bins is drawn at their mean: the share it covered.
      double sum = 0;
      size_t n = 0;
      for (size_t j = k; j < std::min(load.bins, k + step); j++, n++) sum += data[r][j];
      const double v = n ? sum / static_cast<double>(n) : 0;
      c.plot.rect(a.l + cell_w * static_cast<double>(cell), y, cell_w + 0.5, row_h - 1, shade(v),
                  rows[r] + ": " + short_num(v * 100) + "%");
    }
  }
  const Ticks xt = nice_ticks(a.x0, a.x1, std::max(2, static_cast<int>((a.r - a.l) / 90)));
  for (const double v : xt.at) {
    const double x = a.px(v);
    c.plot.line(x, a.b, x, a.b + 4, kAxis);
    const std::string s = tick_label(v, xt.decimals);
    c.plot.text(x - text_width(s) / 2.0, a.b + 7, s, kInk2);
  }
  const std::string xl = frame.x_label.empty() ? "t (ms)" : frame.x_label;
  c.plot.text((a.l + a.r) / 2 - text_width(xl) / 2.0, a.b + 20, xl, kInk2);
  for (int i = 0; i < 40; i++) {
    c.plot.rect(a.r + 16, a.t + i * 3.0, 14, 3, shade((39 - i) / 39.0));
  }
  c.plot.text(a.r + 34, a.t, "100%", kInk2);
  c.plot.text(a.r + 34, a.t + 112, "0%", kInk2);
  c.plot.text(a.r + 16, a.t + 126, "* UNMARKED", kInk2);
  return c;
}

Chart working_chart(const ChartFrame& frame, const prof::Load& load) {
  Chart c;
  c.title = frame.title;
  c.plot.width = frame.width;
  c.plot.height = frame.height;
  Area a{16, draw_head(c.plot, frame, {}) + 14, frame.width - 16.0, frame.height - 44.0};
  const size_t n = std::max(load.time_at_working.size(), load.hardware_threads + 1);
  const double window = static_cast<double>(std::max<int64_t>(1, load.to_ns - load.from_ns));
  a.x0 = -0.5;
  a.x1 = static_cast<double>(n) - 0.5;
  a.y0 = 0;
  a.y1 = 100;
  ChartFrame f = frame;
  if (f.y_label.empty()) f.y_label = "share of the run (%)";
  draw_axes(c.plot, a, f, false);
  std::vector<double> shares;
  for (size_t k = 0; k < n; k++) {
    const double share = k < load.time_at_working.size()
                             ? 100.0 * static_cast<double>(load.time_at_working[k]) / window
                             : 0;
    shares.push_back(share);
    const double x0 = a.px(static_cast<double>(k) - 0.4), x1 = a.px(static_cast<double>(k) + 0.4);
    const Rgba col = k == 0 ? kSleep : series_colour(0);
    c.plot.rect(x0, a.py(share), x1 - x0, a.py(0) - a.py(share), col,
                std::to_string(k) + " threads working: " + short_num(share) + "% of the run");
    const std::string s = std::to_string(k);
    c.plot.text((x0 + x1) / 2 - text_width(s) / 2.0, a.b + 7, s, kInk2);
    if (share >= 1) {
      const std::string v = short_num(share) + "%";
      c.plot.text((x0 + x1) / 2 - text_width(v) / 2.0, a.py(share) - 11, v, kInk2);
    }
  }
  const double hx = a.px(static_cast<double>(load.hardware_threads) + 0.5);
  for (int i = 0; a.t + i * 10.0 < a.b; i++) {
    c.plot.line(hx, a.t + i * 10.0, hx, std::min(a.t + i * 10.0 + 6, a.b), kRing);
  }
  const std::string hw =
      "THE MACHINE: " + std::to_string(load.hardware_threads) + " HARDWARE THREADS";
  c.plot.text(std::min(hx + 6, a.r - text_width(hw)), a.t + 2, hw, kRing);
  const double mx = a.px(load.mean_working);
  c.plot.line(mx, a.t, mx, a.b, kInk2, 2);
  const std::string mean = "MEAN " + short_num(load.mean_working);
  c.plot.text(std::min(mx + 6, a.r - text_width(mean)), a.t + 16, mean, kInk2);
  const std::string xl = "threads working at once";
  c.plot.text((a.l + a.r) / 2 - text_width(xl) / 2.0, a.b + 20, xl, kInk2);
  c.summaries.emplace_back("time at k threads working (%)", summarize(shares));
  return c;
}

Chart frames_chart(const ChartFrame& frame, const prof::Load& load) {
  Chart c;
  c.title = frame.title;
  c.plot.width = frame.width;
  c.plot.height = frame.height;
  // The parts of a frame, bottom up: the main lane's own work, its waits grouped by what the
  // lane waited on mostly does (every pool worker's `band` is one group), waits on nothing
  // marked, and the main lane idle.
  const std::vector<std::string> names = work_names(load);
  struct Part {
    std::string label;
    Rgba colour;
    std::vector<double> ms;
  };
  std::vector<Part> parts;
  std::vector<int> part_of(load.lanes.size(), -1);
  parts.push_back({"main's own work", kInk2, {}});
  for (size_t l = 0; l < load.lanes.size(); l++) {
    bool waited = false;
    for (const prof::FrameLoad& f : load.frames) waited = waited || f.waits_on[l] > 0;
    if (!waited) continue;
    const std::string what = load.lanes[l].gpu ? std::string("the gpu")
                             : load.lanes[l].work_by_name.empty()
                                 ? load.lanes[l].name
                                 : load.lanes[l].work_by_name[0].first;
    for (size_t p = 0; p < parts.size(); p++) {
      if (parts[p].label == "waits on " + what) part_of[l] = static_cast<int>(p);
    }
    if (part_of[l] < 0) {
      const Rgba col = load.lanes[l].work_by_name.empty()
                           ? kOther
                           : slot_colour(slot_of(names, load.lanes[l].work_by_name[0].first));
      part_of[l] = static_cast<int>(parts.size());
      parts.push_back({"waits on " + what, col, {}});
    }
  }
  const size_t unknown = parts.size();
  parts.push_back({"waits on nothing marked", kUnknown, {}});
  const size_t idle = parts.size();
  parts.push_back({"main idle (present, vsync)", kSleep, {}});
  double top_ms = 0;
  std::vector<double> ideal;
  for (const prof::FrameLoad& f : load.frames) {
    for (Part& p : parts) p.ms.push_back(0);
    parts[0].ms.back() = static_cast<double>(f.main_work_ns) / 1e6;
    for (size_t l = 0; l < f.waits_on.size(); l++) {
      if (part_of[l] >= 0)
        parts[static_cast<size_t>(part_of[l])].ms.back() +=
            static_cast<double>(f.waits_on[l]) / 1e6;
    }
    parts[unknown].ms.back() = static_cast<double>(f.wait_unknown_ns) / 1e6;
    parts[idle].ms.back() = static_cast<double>(f.main_idle_ns) / 1e6;
    top_ms = std::max(top_ms, static_cast<double>(f.dur_ns) / 1e6);
    ideal.push_back(f.ideal_ns / 1e6);
  }
  double y = draw_head(c.plot, frame, {});
  double x = 16;
  for (const Part& p : parts) {
    const int w = text_width(p.label) + 28;
    if (x + w > frame.width - 16 && x > 16) {
      x = 16;
      y += 14;
    }
    c.plot.rect(x, y, 10, 8, p.colour);
    c.plot.text(x + 14, y, p.label, kInk2);
    x += w;
  }
  const std::string ideal_key = "BLACK TICK: THE IDEAL (THE FRAME'S WORK SPREAD OVER EVERY CORE)";
  c.plot.text(16, y + 14, ideal_key, kInk2);
  Area a{16, y + 44, frame.width - 16.0, frame.height - 44.0};
  const size_t n = load.frames.size();
  a.x0 = -0.5;
  a.x1 = static_cast<double>(std::max<size_t>(n, 1)) - 0.5;
  a.y0 = 0;
  a.y1 = top_ms > 0 ? top_ms * 1.05 : 1;
  ChartFrame f = frame;
  if (f.y_label.empty()) f.y_label = "ms";
  if (f.x_label.empty()) f.x_label = "frame";
  draw_axes(c.plot, a, f, true);
  // At most a bar a pixel column: frames past that are drawn one in `step`.
  const size_t cols = static_cast<size_t>(std::max(1.0, a.r - a.l));
  const size_t step = std::max<size_t>(1, (n + cols - 1) / cols);
  const double half = std::max(0.5, 0.4 * (a.px(1) - a.px(0)) * static_cast<double>(step));
  for (size_t i = 0; i < n; i += step) {
    const double cx = a.px(static_cast<double>(i));
    double base = 0;
    for (const Part& p : parts) {
      const double v = p.ms[i];
      if (v <= 0) continue;
      c.plot.rect(cx - half, a.py(base + v), 2 * half, a.py(base) - a.py(base + v), p.colour,
                  "frame " + std::to_string(i) + ": " + p.label + " " + short_num(v) + " ms");
      base += v;
    }
    c.plot.line(cx - half, a.py(ideal[i]), cx + half, a.py(ideal[i]), kInk, 2);
  }
  for (const Part& p : parts) c.summaries.emplace_back(p.label + " (ms)", summarize(p.ms));
  c.summaries.emplace_back("ideal (ms)", summarize(ideal));
  return c;
}

Series load_series(const prof::Load& load) {
  std::vector<std::string> cols = {"t_ms"};
  for (const prof::LaneLoad& l : load.lanes) cols.push_back("lane." + l.name);
  for (const prof::OsThreadLoad& t : load.os_threads) {
    cols.push_back("os." + (t.name.empty() ? std::string("thread") : t.name) + "#" +
                   std::to_string(t.os_id));
  }
  const size_t ncores = load.bins ? load.core_bins.size() / load.bins : 0;
  for (size_t i = 0; i < ncores; i++) cols.push_back("core." + std::to_string(i));
  Series s = make_series(cols);
  s.set("title", "load: each lane's work share, each OS thread's and core's CPU share, a bin");
  s.set("x", "t_ms");
  s.set("bin_ms", core::num::to_string(static_cast<double>(load.bin_ns) / 1e6));
  s.set("hardware_threads", std::to_string(load.hardware_threads));
  for (size_t b = 0; b < load.bins; b++) {
    std::vector<double> row = {static_cast<double>(static_cast<int64_t>(b) * load.bin_ns) / 1e6};
    for (size_t i = 0; i < load.lanes.size(); i++) row.push_back(load.lane_bins[i * load.bins + b]);
    for (size_t i = 0; i < load.os_threads.size(); i++) {
      row.push_back(load.os_bins[i * load.bins + b]);
    }
    for (size_t i = 0; i < ncores; i++) row.push_back(load.core_bins[i * load.bins + b]);
    add_row(s, row);
  }
  return s;
}

Series lanes_spans(const std::vector<prof::Lane>& lanes) {
  Series s;
  s.columns = {Column{"name", true, {}, {}},      Column{"depth", false, {}, {}},
               Column{"start_ns", false, {}, {}}, Column{"dur_ns", false, {}, {}},
               Column{"act", true, {}, {}},       Column{"nest", false, {}, {}}};
  s.set("title", "every thread's timeline: a slice a row, its lane the depth");
  s.set("lanes", std::to_string(lanes.size()));
  for (size_t i = 0; i < lanes.size(); i++) {
    const std::string k = "lane." + std::to_string(i);
    s.set(k, lanes[i].name);
    if (lanes[i].gpu) s.set(k + ".gpu", "1");
    if (lanes[i].os_id) s.set(k + ".os_id", std::to_string(lanes[i].os_id));
    for (const prof::Slice& sl : lanes[i].slices) {
      s.columns[0].cell.emplace_back(prof::zone_name(sl.name));
      s.columns[1].num.push_back(static_cast<double>(i));
      s.columns[2].num.push_back(static_cast<double>(sl.start_ns));
      s.columns[3].num.push_back(static_cast<double>(sl.dur_ns));
      s.columns[4].cell.emplace_back(sl.act == prof::Act::work   ? "work"
                                     : sl.act == prof::Act::spin ? "spin"
                                                                 : "sleep");
      s.columns[5].num.push_back(sl.depth);
    }
  }
  return s;
}

bool lanes_from_spans(const Series& s, std::vector<prof::Lane>& out, std::string& error) {
  out.clear();
  const Column* name = s.find("name");
  const Column* lane = s.find("depth");
  const Column* start = s.find("start_ns");
  const Column* dur = s.find("dur_ns");
  const Column* act = s.find("act");
  const Column* nest = s.find("nest");
  if (!s.is_spans() || !act || !act->text || !nest || nest->text || s.get("lanes").empty()) {
    error = "not a lanes file: it needs `# lanes:`, name, depth, start_ns, dur_ns, act and nest";
    return false;
  }
  const size_t n = static_cast<size_t>(std::max(0.0, std::strtod(s.get("lanes").c_str(), nullptr)));
  out.resize(n);
  for (size_t i = 0; i < n; i++) {
    const std::string k = "lane." + std::to_string(i);
    out[i].name = s.get(k);
    out[i].gpu = s.get(k + ".gpu") == "1";
    out[i].os_id = std::strtoull(s.get(k + ".os_id").c_str(), nullptr, 10);
  }
  for (size_t r = 0; r < s.rows(); r++) {
    const double l = lane->num[r];
    if (!(l >= 0) || l >= static_cast<double>(n)) continue;
    prof::Slice sl;
    sl.name = prof::zone_id_of(name->cell[r]);
    sl.act = act->cell[r] == "spin"    ? prof::Act::spin
             : act->cell[r] == "sleep" ? prof::Act::sleep
                                       : prof::Act::work;
    sl.depth = static_cast<uint8_t>(std::clamp(nest->num[r], 0.0, 255.0));
    sl.start_ns = static_cast<int64_t>(start->num[r]);
    sl.dur_ns = static_cast<int64_t>(dur->num[r]);
    out[static_cast<size_t>(l)].slices.push_back(sl);
  }
  return true;
}

namespace {

void shoot(Lab& lab, const LoadReport& r, const std::string& id, const Chart& chart,
           const std::string& caption, const std::string& look_for) {
  const std::string svg = to_svg(chart.plot);
  lab.attach(r.prefix + id + ".svg", std::vector<uint8_t>(svg.begin(), svg.end()), Det::measured);
  lab.shot({r.prefix + id, chart.title, caption, look_for, r.inputs}, to_picture(chart.plot),
           Det::measured);
}

std::vector<uint8_t> bytes_of(const std::string& s) { return {s.begin(), s.end()}; }

}  // namespace

void report_load(Lab& lab, const LoadReport& r, const std::vector<prof::Lane>& lanes,
                 const prof::Load& load, const std::vector<prof::FrameWindow>& frames) {
  const std::string hw = std::to_string(load.hardware_threads);
  const int64_t to = r.zoom_to_ns < 0 ? load.to_ns : r.zoom_to_ns;
  const int64_t from = r.zoom_to_ns < 0 ? load.from_ns : r.zoom_from_ns;
  ChartFrame lf{"EVERY THREAD, " + (r.zoom_what.empty() ? "THE WHOLE RUN" : r.zoom_what),
                r.subject,
                "t (ms)",
                "",
                1280,
                420};
  shoot(lab, r, "lanes", lanes_chart(lf, lanes, load, from, to, frames),
        "One row a thread: full bars are work coloured by what it is, half-height amber is a "
        "thread spinning, the thin grey line a thread asleep, blank untracked. Under the main "
        "lane its critical path: dark is its own work, a colour is a wait on the lane of that "
        "colour. Faint vertical lines are frame edges.",
        "the cores at work where the run has work to spread; long blank or grey rows are cores "
        "the run leaves idle");
  ChartFrame bf{"BUSY SHARE OVER THE RUN", r.subject, "t (ms)", "", 1280, 420};
  shoot(lab, r, "busy", busy_chart(bf, load),
        "Each lane's work share, each OS thread's CPU share (* the timeline never hears it) and "
        "each core's busy share (every process on the machine), a column a " +
            short_num(static_cast<double>(load.bin_ns) / 1e6) + " ms bin, white 0% to dark 100%.",
        "dark rows where the run works; pale core rows are idle cores; a dark unmarked thread is "
        "work the timeline does not see");
  ChartFrame wf{"THREADS WORKING AT ONCE", r.subject, "", "", 960, 360};
  shoot(lab, r, "working", working_chart(wf, load),
        "The share of the run during which exactly k marked threads worked at once. The dashed "
        "red line is the machine's " +
            hw + " hardware threads, the solid line the mean.",
        "weight near the red line when the run is spread well; weight at 0 and 1 is a serial run");
  if (r.per_frame) {
    ChartFrame ff{"EACH FRAME'S CRITICAL PATH", r.subject, "frame", "ms", 1280, 420};
    shoot(lab, r, "frames", frames_chart(ff, load),
          "A bar a frame, its height the frame's length, stacked bottom up: the main lane's own "
          "work (dark), its waits coloured by what the lane it waited on works at (the GPU's "
          "when there is a GPU lane), waits nothing marked ended (red) and the main lane idle "
          "(grey: a present, a vsync). The black tick is the ideal: the frame's work spread "
          "over every hardware thread.",
          "the tallest part is what bounds the frame; a tick far below the top of the coloured "
          "parts is time more threads could win back");
  }
  lab.attach(r.prefix + "load.tsv", bytes_of(write_series(load_series(load))), Det::measured);
  lab.attach(r.prefix + "lanes.tsv", bytes_of(write_series(lanes_spans(lanes))), Det::measured);
  const std::string k = r.prefix;
  lab.metric(k + "hardware-threads", static_cast<double>(load.hardware_threads), "count",
             Det::measured);
  lab.metric(k + "efficiency", load.efficiency * 100, "%", Det::measured);
  lab.metric(k + "mean-working", load.mean_working, "threads", Det::measured);
  if (load.os_efficiency >= 0) {
    lab.metric(k + "os-efficiency", load.os_efficiency * 100, "%", Det::measured);
  }
  for (const prof::LaneLoad& l : load.lanes) {
    std::string name = l.name;
    for (char& ch : name) ch = ch == ' ' ? '-' : ch;
    lab.metric(std::string(k).append("lane.").append(name).append(".work"), l.work_share * 100, "%",
               Det::measured);
  }
  if (!load.frames.empty()) {
    std::vector<double> dur, own;
    for (const prof::FrameLoad& f : load.frames) {
      dur.push_back(static_cast<double>(f.dur_ns) / 1e6);
      own.push_back(static_cast<double>(f.main_work_ns) / 1e6);
    }
    lab.metric(k + "frame-ms.p50", summarize(dur).p50, "ms", Det::measured);
    lab.metric(k + "main-work-ms.p50", summarize(own).p50, "ms", Det::measured);
  }
  lab.note(prof::load_text(load));
}

}  // namespace ge::lab
