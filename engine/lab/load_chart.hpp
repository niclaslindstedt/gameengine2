// THE LOAD CHARTS: how a run spread its work over the machine (prof/load.hpp), drawn so an agent
// can see at a glance whether the cores work or sit idle, and why a frame takes as long as it does.
//
//   lanes_chart     one row a thread over a window: work as full bars coloured by what it is,
//                   spinning as half-height amber, asleep as a thin grey line, untracked blank;
//                   under the main lane its critical path (its own work dark, its waits in the
//                   colour of what it waited on); frame edges as faint lines
//   busy_chart      a heatmap over the whole run on a fixed 0..100% scale: each lane's work
//                   share, each OS thread's CPU share (* unmarked: the timeline never hears it)
//                   and each core's busy share, a column a bin
//   working_chart   how long exactly k threads worked at once, k = 0 up, with the machine's
//                   hardware threads marked: the run's parallelism as one picture
//   frames_chart    per frame: its length, the main lane's own work, each wait by what it waited
//                   on, the GPU's busy time and the ideal (the work spread perfectly)
//
// And two files for `ge graph` and for reading: `load_series` (the bins: t_ms, then a column a
// lane, an OS thread and a core) and `lanes_spans` (every slice as a SPANS row, its lane as the
// depth, with `lane` and `act` columns and a `# lane.<n>: <name>` line a lane), which
// `lanes_from_spans` reads back.
//
// `report_load` is what a lab writes for a load in one call: the four charts as captioned shots
// (and their SVG), both files attached, the measured metrics (the machine's share, each lane's,
// the frames' medians) and the analysis' text and findings as notes. Everything it writes is
// `Det::measured` but the shots' and files' names, so a load lab's digest is the same on every
// machine.
//
// ge:summary The load charts: thread lanes, busy heatmap, threads at once, per-frame bound
// ge:keywords chart load lanes timeline threads busy heatmap utilisation concurrency frames
// ge:keywords critical-path lanes_chart busy_chart working_chart frames_chart load_series
// ge:keywords lanes_spans lanes_from_spans
// ge:skill perf debug labs
// ge:test tests/lab_load_chart_test.cpp
#pragma once

#include <cstdint>
#include <vector>

#include "lab/chart.hpp"
#include "lab/lab.hpp"
#include "lab/series.hpp"
#include "prof/load.hpp"

namespace ge::lab {

Chart lanes_chart(const ChartFrame& frame, const std::vector<prof::Lane>& lanes,
                  const prof::Load& load, int64_t from_ns, int64_t to_ns,
                  const std::vector<prof::FrameWindow>& frames);
Chart busy_chart(const ChartFrame& frame, const prof::Load& load);
Chart working_chart(const ChartFrame& frame, const prof::Load& load);
Chart frames_chart(const ChartFrame& frame, const prof::Load& load);

Series load_series(const prof::Load& load);
Series lanes_spans(const std::vector<prof::Lane>& lanes);
// The lanes a lanes_spans file holds; false (with `error`) when it is not one.
bool lanes_from_spans(const Series& s, std::vector<prof::Lane>& out, std::string& error);

struct LoadReport {
  std::string prefix;   // the shots' and files' stem: "<prefix>lanes", "<prefix>load.tsv"
  std::string subject;  // the run, one line: each chart's subtitle
  std::vector<std::pair<std::string, std::string>> inputs;  // on every shot
  int64_t zoom_from_ns = 0, zoom_to_ns = -1;  // the lanes chart's window (-1: the whole run)
  std::string zoom_what;                      // what that window is ("frames 40 to 44")
  bool per_frame = true;  // draw the frames chart (even with no frames, so the shots are the same)
};

void report_load(Lab& lab, const LoadReport& report, const std::vector<prof::Lane>& lanes,
                 const prof::Load& load, const std::vector<prof::FrameWindow>& frames);

}  // namespace ge::lab
