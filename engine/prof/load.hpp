// HOW WELL A RUN USES THE MACHINE: the timeline's lanes (timeline.hpp) and the OS sampler's
// readings (cpu_sampler.hpp) folded into one answer an agent can act on. Does most of the CPU sit
// idle, which thread holds every frame up, does a frame wait on the pool or the GPU, and how much
// faster could the frame be if its work were spread over every core?
//
//   Load load = analyze_load(timeline_lanes(), sampler.samples(), frames, options);
//   std::puts(load_text(load).c_str());   // the shares, the frames' bound, the findings
//
// PER LANE: its work, spin, sleep and untracked time over the window (the innermost slice at
// each moment counts), its share of the window working, and its CPU share as the OS saw it when
// the sampler matched its thread. PER OS THREAD: every thread of the process with its CPU share,
// whether the timeline hears it, so a busy thread the code never marks (a driver's, a software
// rasterizer's) still shows. PER CORE: each core's busy share (every process on the machine).
//
// CONCURRENCY: how long exactly k CPU lanes were working at once, for every k; the mean number
// working; and the EFFICIENCY, the lanes' work over (hardware threads x window): 100% is every
// core working all the time. The OS's efficiency is the same over the process's CPU time (spins
// and unmarked threads included).
//
// THE FRAMES (when the caller hands their windows): per frame, every lane's work in it, the main
// lane's own work, its idle (untracked: a present, a vsync) and its waits, each laid on the lane
// that ended it. That is the frame's CRITICAL PATH: the main lane's work, then what it waited on.
// A wait slice named `gpu...` is the GPU's (the lane marked `gpu`); any other wait is the CPU lane
// whose work ended last inside it (give or take the 0.2 ms a wake takes), or `unknown`. The frame's
// IDEAL is max(the main lane's own work, every CPU lane's work / hardware threads, the GPU's busy
// time): what the frame would cost with its CPU work perfectly spread and the GPU's beside it.
//
// A LANE MADE BY THE CALLER (os_id 0) can stand for what is not a CPU thread: a lane with `gpu`
// set is the GPU's passes, counted as the GPU's busy share and each frame's GPU time, never in the
// CPU's concurrency.
//
// Everything here reads the lanes and samples given and nothing else; the same inputs give the same
// answer (the numbers are measured, so a run's answer is never in a digest).
//
// ge:summary The load analysis: per-thread and per-core utilisation, concurrency, critical path
// ge:keywords prof load utilisation utilization idle cores threads concurrency efficiency
// ge:keywords critical-path bound frame-bound waits gpu-bound parallelism headroom amdahl
// ge:keywords analyze_load load_text load_findings flatten busy-share
// ge:skill perf
// ge:test tests/prof_load_test.cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "prof/cpu_sampler.hpp"
#include "prof/timeline.hpp"

namespace ge::prof {

// The GPU's lane is a made lane (os_id 0) with this name; the main lane's GPU waits name it too.
inline constexpr const char* kGpuLane = "gpu";

struct LoadOptions {
  int64_t from_ns = 0;
  int64_t to_ns = -1;           // -1: the last slice's or sample's end
  std::string main = "main";    // the lane whose waits the critical path follows
  int64_t bin_ns = 10'000'000;  // the utilisation-over-time bins
  size_t hardware_threads = 0;  // 0: the machine's
  uint64_t sampler_os_id = 0;   // the sampler's own thread, left out of the OS threads
  int64_t wake_ns = 200'000;    // how late a woken thread may start after its waker's end
};

struct FrameWindow {
  int64_t start_ns = 0, end_ns = 0;
};

// A lane flattened: at each moment, the innermost slice.
struct Segment {
  int64_t t0 = 0, t1 = 0;
  Act act = Act::work;
  uint32_t name = 0;
};

struct LaneLoad {
  std::string name;
  uint64_t os_id = 0;
  bool gpu = false;
  int64_t work_ns = 0, spin_ns = 0, sleep_ns = 0, untracked_ns = 0;
  double work_share = 0;  // of the window
  double cpu_share = -1;  // the OS's CPU time for its thread over the window; -1 unmatched
  int64_t dropped = 0;
  std::vector<std::pair<std::string, int64_t>> work_by_name;  // most first
};

struct OsThreadLoad {
  uint64_t os_id = 0;
  std::string name;
  int64_t cpu_ns = 0;  // over the window
  double share = 0;    // of one core over the window
  int lane = -1;       // the timeline lane on this thread, -1 when the timeline never hears it
};

struct FrameLoad {
  int64_t start_ns = 0, dur_ns = 0;
  int64_t work_ns = 0;            // every CPU lane's work in it
  int64_t main_work_ns = 0;       // the main lane's own
  int64_t main_idle_ns = 0;       // the main lane untracked
  std::vector<int64_t> waits_on;  // a lane each: the main lane's waits laid on it
  int64_t wait_unknown_ns = 0;
  int64_t gpu_ns = -1;  // the GPU lane's work in it; -1 with no GPU lane
  double ideal_ns = 0;  // max(main work, work / hardware threads, gpu busy)
};

// A stretch of the main lane's critical path: its own work, or a wait laid on what ended it.
struct PathPiece {
  int64_t t0 = 0, t1 = 0;
  int on = -1;  // kOwnWork, kUnknownWait, or the lane waited on
};
inline constexpr int kOwnWork = -1;
inline constexpr int kUnknownWait = -2;

struct LoadFinding {
  std::string what;  // what was found, with its numbers
  std::string next;  // what to do about it
};

struct Load {
  int64_t from_ns = 0, to_ns = 0;
  size_t hardware_threads = 1;
  int main = -1;  // the main lane's index, -1 when absent
  int gpu = -1;   // the GPU lane's index, -1 when absent
  std::vector<LaneLoad> lanes;
  std::vector<OsThreadLoad> os_threads;  // most CPU first
  std::vector<double> cores_busy;        // a core each, over the window
  bool whole_machine = false;            // cores_busy is one row for the machine
  std::vector<int64_t> time_at_working;  // [k]: time exactly k CPU lanes worked at once
  double mean_working = 0;
  double efficiency = 0;      // CPU lanes' work / (hardware threads x window)
  double os_efficiency = -1;  // the process's CPU time / (hardware threads x window); -1 no samples
  int64_t bin_ns = 0;
  size_t bins = 0;
  std::vector<double> lane_bins;  // lanes x bins, row-major: the work share of each bin
  std::vector<double> os_bins;    // os_threads x bins: CPU share of one core
  std::vector<double> core_bins;  // cores x bins: busy share
  std::vector<PathPiece> path;    // the main lane's critical path over the window, in time order
  std::vector<FrameLoad> frames;
  std::vector<LoadFinding> findings;
};

// The lane's segments over [from, to): the innermost slice at each moment, untracked left out.
std::vector<Segment> flatten(const Lane& lane, int64_t from_ns, int64_t to_ns);

Load analyze_load(const std::vector<Lane>& lanes, const std::vector<CpuSample>& samples,
                  const std::vector<FrameWindow>& frames, const LoadOptions& options);

// The answer as an agent reads it: the window, the lanes' shares, the busiest OS threads, the
// cores, the concurrency, the frames' critical path (median over the frames) and the findings.
std::string load_text(const Load& load);

}  // namespace ge::prof
