// THE LOAD RUN: how well a game's run uses the machine, measured on a run its own pilot played.
// `ge load` reads it (engine/cli/perf.hpp). The input log is replayed once with every thread's
// timeline recording (prof/timeline.hpp) and the OS sampler beside it (prof/cpu_sampler.hpp):
//
//   the open    spec.open: building the run (a map, its bakes on the worker pool), as `open`
//   the steps   each step as `step`; paced (the default) a step starts at its slot on the wall
//               clock, i / hz, as a game's loop runs them, so the idle between steps is real;
//               unpaced they run back to back (what the steps alone cost)
//
// Each phase is folded by the load analysis (prof/load.hpp; paced, each step's slot is a frame
// window, so the critical path and the idle are a slot's) and drawn by the load charts
// (lab/load_chart.hpp). The files, under `dir`, a seed each:
//
//   <game>-<seed>.lanes.tsv        every thread's slices (lanes_spans): `ge graph` draws its lanes
//   <game>-<seed>.load-open.tsv    the open's bins (load_series)
//   <game>-<seed>.load-steps.tsv   the steps' bins
//   <game>-<seed>.load-<chart>.png and .svg   the lanes, busy, working and (steps) frames charts
//
// The timeline names the calling thread `main`; the replay must reach the log's digest (a run
// that read something else measured something else). Timing feeds no digest.
//
// ge:summary The load run: a pilot's log replayed with every thread's timeline and the OS sampler
// ge:keywords play load utilisation threads cores timeline sampler paced steps open charts
// ge:keywords load_run ge-load idle critical-path
// ge:skill agent-interface perf
// ge:test tests/cli_perf_test.cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "play/game.hpp"
#include "play/inputs.hpp"
#include "prof/load.hpp"

namespace ge::play {

struct LoadRunOptions {
  int64_t most_steps = 1200;  // only the first this many steps of the log (-1: all)
  bool paced = true;
  int64_t sample_us = 2000;  // the OS sampler's period
  int64_t bin_ms = 10;
};

struct LoadRun {
  int64_t steps = 0;
  uint64_t digest = 0;
  bool same_digest = true;
  double open_ms = 0, steps_ms = 0;
  prof::Load open, run;
  std::vector<std::string> files;  // every file written, in order
};

// Replays `log` once as above and writes its files under `dir` (stem `<game>-<seed>`).
std::optional<LoadRun> load_run(const GameSpec& spec, const InputLog& log, const LoadRunOptions& o,
                                const std::filesystem::path& dir, std::string& error);

// One phase as JSON: its window, shares, lanes, the busiest OS threads, the cores and findings.
std::string load_json(const prof::Load& load);

}  // namespace ge::play
