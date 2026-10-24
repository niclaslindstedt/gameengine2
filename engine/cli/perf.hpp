// WHAT A GAME COSTS, THROUGH ge: four commands over the game's `play` executable, so an agent
// measures before it optimises and proves the change after, headless, on any machine:
//
//   ge bench [--seeds=A,B] [--repeats=N] [--steps=N] [--tag=T] [--compare=T] [-- options]
//       each step's time (median, p95, p99, worst) against its budget (1/hz), steps a second, the
//       open; --tag keeps the answer as runs/bench@T.json, --compare sets every number beside it
//       and warns on a step that got 10% slower
//   ge profile [--seed=S] [--steps=N] [--render] [-- options]
//       where the step's time goes: one row a timing zone (GE_ZONE) with its calls, exclusive
//       time and share, the time in no zone, the slowest steps and the zone that dominated each;
//       the spans round the slowest step as a timeline file. --render also runs the game's
//       `profile` lab (the frame: CPU and GPU ms per render pass) and adds its table
//   ge mem [--seeds=A,B] [--steps=N] [-- options]
//       what the run allocates: opening it, every step, and the steps after the first second that
//       still allocate (a hot path that allocates is a warning naming its zone); the peak
//       resident set
//   ge load [--seeds=A,B] [--steps=N] [--paced=0|1] [--render] [-- options]
//       how well the run uses the machine (engine/play/load.hpp): the pilot's run replayed with
//       every thread's timeline and the OS sampler, its open and its steps each folded into
//       each thread's work, spin and sleep shares, every OS thread's and core's CPU share, the
//       threads working at once and each step's critical path, with findings (idle cores,
//       threads that spin, unmarked threads, what bounds a step) and charts under runs/;
//       --render also runs the game's `gpu-load` lab (the frame loop's threads and the GPU)
//   ge leaks [--seeds=A,B] [--valgrind] [-- options]
//       the pilot's run under the leak checker (the debug preset's sanitizers on Linux; valgrind's
//       memcheck with --valgrind or where no sanitizer is built): each leak a finding at the
//       first frame in the repo
//
// bench, profile and mem build the release preset unless --preset says otherwise (a debug
// build's sanitizers make every time several times longer); leaks builds the debug one. All but
// leaks run `play bench` (engine/play/bench.hpp) and write its series and spans files under
// runs/ for `ge graph`.
//
// ge:summary ge bench, profile, mem, load and leaks: a game's step cost, zones, allocations, load
// ge:keywords cli ge bench profile mem leaks benchmark hotspot allocations rss leak lsan valgrind
// ge:keywords tag compare regression render-passes load utilisation threads cores idle
// ge:skill agent-interface perf
// ge:test tests/cli_perf_test.cpp
#pragma once

#include <string>
#include <vector>

#include "cli/build.hpp"
#include "cli/outcome.hpp"
#include "play/json_read.hpp"

namespace ge::cli {

struct PerfOptions {
  std::string seeds;    // --seeds (or --seed), "" for the play executable's default
  std::string repeats;  // --repeats, ""
  std::string steps;    // --steps, ""
  std::string tag, compare;
  bool render = false;
  bool valgrind = false;
  std::vector<std::string> options;  // the game's, after --
};

Outcome run_bench(const BuildOptions& o, const PerfOptions& p);
Outcome run_profile(const BuildOptions& o, const PerfOptions& p);
Outcome run_mem(const BuildOptions& o, const PerfOptions& p);
Outcome run_leaks(const BuildOptions& o, const PerfOptions& p);

struct LoadOptions {
  std::string seeds, steps, paced, bin_ms;  // "" for the play executable's defaults
  bool render = false;
  std::vector<std::string> options;  // the game's, after --
};
Outcome run_load(const BuildOptions& o, const LoadOptions& l);
// The reading of a `play load` answer (exposed for the tests).
Outcome load_outcome(const BuildOptions& o, const play::FlatJson& m, const std::string& line);

// The readings of a `play bench` answer (exposed for the tests). `before` is a tagged answer's
// flat map or null.
Outcome bench_outcome(const BuildOptions& o, const play::FlatJson& m, const std::string& line,
                      const play::FlatJson* before, const std::string& tag);
Outcome profile_outcome(const BuildOptions& o, const play::FlatJson& m, const std::string& line);
Outcome mem_outcome(const BuildOptions& o, const play::FlatJson& m, const std::string& line);
// A leak checker's report (LeakSanitizer's or valgrind's) as findings, each at the first frame
// inside `o.repo` (the build directories and the engine's own files excepted).
std::vector<Finding> leak_findings(const BuildOptions& o, const std::string& report);

}  // namespace ge::cli
