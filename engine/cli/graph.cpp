// `ge graph`'s body (graph.hpp): read the files, choose the charts, draw them with the labs'
// chart kit, write PNG and SVG, and return the outliers as findings.
//
// ge:summary ge graph's body: read, choose the charts, write PNG and SVG, findings
// ge:keywords cli graph run_graph chart png svg lanes-file lanes_chart
// ge:skill agent-interface debug
// ge:test tests/cli_graph_test.cpp
#include "cli/graph.hpp"

#include <algorithm>
#include <set>

#include "cli/json.hpp"
#include "core/num_text.hpp"
#include "lab/chart.hpp"
#include "lab/load_chart.hpp"
#include "lab/png.hpp"
#include "lab/series.hpp"

namespace ge::cli {

namespace {

namespace gl = ge::lab;

constexpr size_t kMostColumns = 16;

struct Loaded {
  std::string path;
  std::string stem;
  gl::Series series;
};

std::string label_of(const Loaded& f, bool many) { return many ? f.stem : std::string(); }

std::string num(double v) { return core::num::to_string(v); }

std::string safe(std::string s) {
  for (char& c : s) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '_' || c == '-' || c == '.';
    if (!ok) c = '_';
  }
  return s;
}

std::string with_unit(const gl::Series& s, const std::string& column) {
  const std::string unit = s.unit(column);
  return unit.empty() ? column : column + " (" + unit + ")";
}

struct Writer {
  fs::path dir;
  Outcome& o;
  std::vector<std::string> charts;  // the result's rows

  // `sets`: a histogram's or a box chart's, whose outliers are rows of a set, not points on x.
  void write(const std::string& name, const gl::Chart& chart, bool sets = false) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path png = dir / (name + ".png");
    const fs::path svg = dir / (name + ".svg");
    std::string error;
    if (!gl::write_png(png.string(), gl::to_picture(chart.plot)) ||
        !write_text(svg, gl::to_svg(chart.plot), error)) {
      o.findings.push_back({Severity::error, "could not write " + png.string() + " or its SVG",
                            dir.string(), "pass a writable directory with --out=DIR"});
      return;
    }
    o.written.push_back(png.string());
    o.written.push_back(svg.string());
    o.text += gl::chart_text(chart, 0) + "  -> " + png.string() + "\n";
    std::vector<std::string> finds;
    for (const gl::Finding& f : chart.findings) {
      finds.push_back(json_obj({{"series", json_str(f.series)},
                                {"kind", json_str(f.kind)},
                                {"x", num(f.x)},
                                {"y", num(f.y)},
                                {"note", json_str(f.note)}}));
      if (count_of(o.findings, Severity::note) < 40) {
        o.findings.push_back(
            {Severity::note, f.kind + ": " + f.note, png.string(),
             sets ? "open the chart; it is row " + core::num::to_fixed(f.x, 0) + " of " + f.series
                  : "open the chart and read the run around x " + core::num::to_fixed(f.x, 3)});
      }
    }
    std::vector<std::string> sums;
    sums.reserve(chart.summaries.size());
    for (const auto& [label, s] : chart.summaries) {
      sums.push_back(json_obj({{"series", json_str(label)},
                               {"n", json_num(static_cast<long long>(s.n))},
                               {"min", num(s.min)},
                               {"p50", num(s.p50)},
                               {"p95", num(s.p95)},
                               {"p99", num(s.p99)},
                               {"max", num(s.max)}}));
    }
    charts.push_back(json_obj({{"title", json_str(chart.title)},
                               {"png", json_str(png.string())},
                               {"svg", json_str(svg.string())},
                               {"summaries", json_arr(sums)},
                               {"findings", json_arr(finds)}}));
  }
};

}  // namespace

Outcome run_graph(const GraphOptions& opts) {
  Outcome o;
  static const std::set<std::string> kKinds{"auto", "line", "scatter", "hist",
                                            "box",  "heat", "spans"};
  if (!kKinds.count(opts.kind)) {
    o.findings.push_back({Severity::error, "no chart kind \"" + opts.kind + "\"",
                          "the command line",
                          "rerun with --kind=auto|line|scatter|hist|box|heat|spans"});
    o.exit = 2;
    return o;
  }
  std::vector<Loaded> files;
  for (const std::string& path : opts.files) {
    const auto text = read_text(path);
    if (!text) {
      o.findings.push_back({Severity::error, "cannot read " + path, path,
                            "check the path; ge sim, ge bench and labs write series files"});
      continue;
    }
    Loaded f{path, fs::path(path).stem().string(), {}};
    std::string error;
    if (!gl::read_series(*text, f.series, error)) {
      std::string what = path;
      what += " is not a series file: ";
      what += error;
      o.findings.push_back(
          {Severity::error, what, path, "see engine/lab/series.hpp for the format (skill: debug)"});
      continue;
    }
    files.push_back(std::move(f));
  }
  if (files.empty() || count_of(o.findings, Severity::error)) return o;

  const bool many = files.size() > 1;
  std::string kind = opts.kind;
  if (kind == "auto") kind = files.front().series.is_spans() ? "spans" : "line";
  Writer w{
      opts.out.empty() ? fs::path(files.front().path).parent_path() / "graphs" : fs::path(opts.out),
      o,
      {}};
  const std::string prefix = opts.title.empty() ? "" : opts.title + ": ";
  const std::string stem = files.front().stem;

  if (kind == "spans") {
    std::vector<gl::Sample> durations;
    for (const Loaded& f : files) {
      const gl::Series& s = f.series;
      if (!s.is_spans()) {
        o.findings.push_back({Severity::error, f.path + " is not a spans file", f.path,
                              "a spans file has the columns name, depth, start_ns, dur_ns "
                              "(engine/lab/series.hpp)"});
        continue;
      }
      std::vector<prof::Lane> lanes;
      std::string why;
      if (!s.get("lanes").empty() && gl::lanes_from_spans(s, lanes, why)) {
        // A lanes file (every thread's timeline): its lanes and its threads at once.
        const prof::Load load = prof::analyze_load(lanes, {}, {}, {});
        w.write(
            safe(f.stem) + ".lanes",
            gl::lanes_chart({prefix + "EVERY THREAD OF " + f.stem, f.path, "t (ms)", "", 1280, 420},
                            lanes, load, load.from_ns, load.to_ns, {}));
        w.write(
            safe(f.stem) + ".working",
            gl::working_chart(
                {prefix + "THREADS WORKING AT ONCE, " + f.stem, f.path, "", "", 960, 360}, load));
      } else {
        const gl::Chart timeline =
            gl::span_chart({prefix + "TIMELINE OF " + f.stem, f.path, "", ""}, s.find("name")->cell,
                           s.find("depth")->num, s.find("start_ns")->num, s.find("dur_ns")->num);
        w.write(safe(f.stem) + ".timeline.spans", timeline);
      }
      std::vector<std::string> order;
      std::vector<gl::Sample> mine;
      const auto& names = s.find("name")->cell;
      const auto& dur = s.find("dur_ns")->num;
      for (size_t i = 0; i < names.size(); i++) {
        auto it = std::find(order.begin(), order.end(), names[i]);
        if (it == order.end()) {
          order.push_back(names[i]);
          mine.push_back({names[i] + (many ? " (" + f.stem + ")" : ""), {}});
          it = order.end() - 1;
        }
        mine[static_cast<size_t>(it - order.begin())].values.push_back(dur[i] / 1e6);
      }
      for (auto& m : mine) durations.push_back(std::move(m));
    }
    if (!durations.empty()) {
      w.write(safe(stem) + ".zones.box",
              gl::box_chart({prefix + "ZONE DURATIONS", many ? "every file" : files.front().path,
                             "duration (ms)", ""},
                            durations),
              true);
    }
  } else {
    const gl::Series& first = files.front().series;
    const gl::Column* x_first = opts.x.empty() ? first.x() : first.find(opts.x);
    if (!x_first || x_first->text) {
      o.findings.push_back({Severity::error, "no numeric x column \"" + opts.x + "\"",
                            files.front().path, "rerun with --x=<a numeric column>"});
      return o;
    }
    std::vector<std::string> columns = opts.columns;
    if (columns.empty()) {
      for (const gl::Column& c : first.columns) {
        if (!c.text && c.name != x_first->name) columns.push_back(c.name);
      }
    }
    if (columns.size() > kMostColumns && opts.columns.empty()) {
      o.findings.push_back({Severity::warn,
                            std::to_string(columns.size()) + " columns: charting the first " +
                                std::to_string(kMostColumns),
                            files.front().path, "name the ones you want with --columns=a,b"});
      columns.resize(kMostColumns);
    }
    const auto column_of = [&](const Loaded& f, const std::string& name) -> const gl::Column* {
      const gl::Column* c = f.series.find(name);
      return c && !c->text ? c : nullptr;
    };
    if (kind == "box") {
      std::vector<gl::Sample> rows;
      for (const std::string& col : columns) {
        for (const Loaded& f : files) {
          if (const gl::Column* c = column_of(f, col)) {
            rows.push_back({col + (many ? " (" + f.stem + ")" : ""), c->num});
          }
        }
      }
      w.write(
          safe(stem) + ".box",
          gl::box_chart({prefix + "PERCENTILES OF " + stem, files.front().path, "value", ""}, rows),
          true);
    } else if (kind == "heat") {
      std::vector<std::string> rows;
      std::vector<double> values;
      for (const std::string& col : columns) {
        if (const gl::Column* c = column_of(files.front(), col)) {
          rows.push_back(col);
          values.insert(values.end(), c->num.begin(), c->num.end());
        }
      }
      const double x0 = x_first->num.empty() ? 0 : x_first->num.front();
      const double x1 = x_first->num.empty() ? 1 : x_first->num.back();
      w.write(safe(stem) + ".heat",
              gl::heatmap_chart({prefix + "HEATMAP OF " + stem, files.front().path,
                                 with_unit(first, x_first->name), ""},
                                rows, values, x0, x1));
    } else {
      for (const std::string& col : columns) {
        const std::string label = with_unit(first, col);
        if (kind == "hist") {
          std::vector<gl::Sample> samples;
          for (const Loaded& f : files) {
            if (const gl::Column* c = column_of(f, col)) {
              samples.push_back({many ? f.stem : col, c->num});
            }
          }
          w.write(safe(stem) + "." + safe(col) + ".hist",
                  gl::histogram_chart({prefix + col, files.front().path, label, ""}, samples),
                  true);
          continue;
        }
        gl::LineChart lc;
        lc.frame = {prefix + col,
                    many ? std::to_string(files.size()) + " files overlaid" : files.front().path,
                    with_unit(first, x_first->name), label};
        lc.points = kind == "scatter";
        lc.log_y = opts.log_y;
        for (const Loaded& f : files) {
          const gl::Column* c = column_of(f, col);
          const gl::Column* xc = opts.x.empty() ? f.series.x() : f.series.find(opts.x);
          if (!c || !xc || xc->text) {
            o.findings.push_back({Severity::warn, f.path + " has no column " + col, f.path,
                                  "chart the files' shared columns with --columns=a,b"});
            continue;
          }
          lc.traces.push_back({many ? label_of(f, many) : col, xc->num, c->num});
        }
        w.write(safe(stem) + "." + safe(col) + "." + kind, gl::line_chart(lc));
      }
    }
  }
  o.result = json_obj({{"kind", json_str(kind)}, {"charts", json_arr(w.charts)}});
  o.summary = std::to_string(w.charts.size()) + " chart" + (w.charts.size() == 1 ? "" : "s") +
              " in " + w.dir.string() + ", " +
              std::to_string(count_of(o.findings, Severity::note)) + " outliers noted";
  o.next = "open the PNGs, or read the SVGs' text";
  return o;
}

}  // namespace ge::cli
