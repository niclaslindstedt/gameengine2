// LAB `load`: does the load monitor see a frame's shape of work as it is? A synthetic frame loop
// of known parts runs on real threads: the main thread simulates, bakes rows on the worker pool
// (core::parallel_bands, its last band twice the others so the caller waits for it), hands a
// render thread its work and waits for it, encodes, then sleeps out the frame (a present). Every
// thread's timeline (prof/timeline.hpp) and the OS sampler (prof/cpu_sampler.hpp) record it, the
// load analysis (prof/load.hpp) folds it, and the load charts (lab/load_chart.hpp) draw it: the
// lanes round a few frames, the busy heatmap, the threads at once and each frame's critical path.
//
// What a correct run shows: the main lane's critical path laid on the pool (the slow band) and on
// the render thread, the main lane idle for the present, the pool's workers working only in the
// bake and spinning or asleep the rest, and the machine mostly idle. Every number is measured
// (the digest holds the shots' and files' names and the frame count alone, so it is the same on
// every machine).
//
// ge:summary Lab: the load monitor on a synthetic frame loop (pool bake, render thread, present)
// ge:keywords lab load prof timeline cpu-sampler threads utilisation critical-path pool render
// ge:keywords frame-loop idle cores busy lanes
// ge:skill perf labs
// ge:test tests/lab_runner_test.cpp
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "core/parallel.hpp"
#include "core/thread_name.hpp"
#include "lab/lab.hpp"
#include "lab/load_chart.hpp"
#include "prof/cpu_sampler.hpp"
#include "prof/load.hpp"
#include "prof/timeline.hpp"

namespace {

namespace gl = ge::lab;
namespace pf = ge::prof;
using ge::lab::Det;

// Burns the calling thread's core for `ns` (the work a part stands for).
void busy_for(int64_t ns) {
  const int64_t end = pf::now_ns() + ns;
  volatile double x = 1;
  while (pf::now_ns() < end) {
    for (int i = 0; i < 200; i++) x = x * 1.0000001 + 1e-9;
  }
}

// A thread that does a part of the frame when the main thread asks and says when it is done.
class Helper {
 public:
  explicit Helper(int64_t work_ns) : work_ns_(work_ns), thread_([this] { loop(); }) {}
  ~Helper() {
    {
      const std::lock_guard<std::mutex> g(m_);
      stop_ = true;
    }
    cv_.notify_all();
    thread_.join();
  }
  Helper(const Helper&) = delete;
  Helper& operator=(const Helper&) = delete;

  void ask() {
    {
      const std::lock_guard<std::mutex> g(m_);
      asked_++;
    }
    cv_.notify_all();
  }
  void wait() {
    GE_WAIT("wait for render");
    std::unique_lock<std::mutex> lock(m_);
    cv_.wait(lock, [&] { return done_ == asked_; });
  }

 private:
  void loop() {
    ge::core::name_this_thread("render");
    for (;;) {
      {
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait(lock, [&] { return stop_ || asked_ > done_; });
        if (stop_) return;
      }
      {
        GE_WORK("render");
        busy_for(work_ns_);
      }
      {
        const std::lock_guard<std::mutex> g(m_);
        done_++;
      }
      cv_.notify_all();
    }
  }
  int64_t work_ns_;
  std::mutex m_;
  std::condition_variable cv_;
  int64_t asked_ = 0, done_ = 0;
  bool stop_ = false;
  std::thread thread_;
};

void run(gl::Lab& lab) {
  const int frames = std::max(1, static_cast<int>(lab.arg_num("frames")));
  const auto us = [&](const char* flag) {
    return static_cast<int64_t>(std::max(0.0, lab.arg_num(flag)) * 1000);
  };
  const int64_t period = static_cast<int64_t>(std::max(1.0, lab.arg_num("period-ms")) * 1e6);
  const int64_t sim = us("sim-us"), bake = us("bake-us"), render = us("render-us");
  const int64_t encode = us("encode-us");
  const size_t workers = ge::core::default_workers();
  lab.metric("frames", frames, "count");

  ge::core::name_this_thread("main", false);  // the process keeps its OS name
  Helper helper(render);
  pf::CpuSampler sampler;
  pf::timeline_start(size_t{1} << 16);
  sampler.start(static_cast<int64_t>(lab.arg_num("sample-us")), pf::timeline_origin_ns());
  std::vector<pf::FrameWindow> windows;
  for (int f = 0; f < frames; f++) {
    const int64_t start = pf::now_ns();
    {
      GE_WORK("simulate");
      busy_for(sim);
    }
    {
      GE_WORK("bake");
      // workers bands; the last one twice the others, so the caller waits on it.
      const int64_t band = bake / static_cast<int64_t>(workers + 1);
      ge::core::parallel_bands(workers, workers, [&](size_t b, size_t) {
        busy_for(b + 1 == workers ? 2 * band : band);
      });
    }
    helper.ask();
    helper.wait();
    {
      GE_WORK("encode");
      busy_for(encode);
    }
    // The present: the rest of the frame untracked, as a vsync is.
    const int64_t left = start + period - pf::now_ns();
    if (left > 0) std::this_thread::sleep_for(std::chrono::nanoseconds(left));
    const int64_t origin = pf::timeline_origin_ns();
    windows.push_back({start - origin, pf::now_ns() - origin});
  }
  pf::timeline_stop();
  sampler.stop();
  const std::vector<pf::Lane> lanes = pf::timeline_lanes();
  pf::LoadOptions o;
  o.from_ns = windows.front().start_ns;
  o.to_ns = windows.back().end_ns;
  o.bin_ns = static_cast<int64_t>(std::max(0.1, lab.arg_num("bin-ms")) * 1e6);
  o.sampler_os_id = sampler.os_id();
  const pf::Load load = pf::analyze_load(lanes, sampler.samples(), windows, o);

  gl::LoadReport r;
  r.subject = std::to_string(frames) + " synthetic frames of " + lab.arg("period-ms") +
              " ms: simulate " + lab.arg("sim-us") + " us, bake " + lab.arg("bake-us") +
              " us on the pool, render " + lab.arg("render-us") + " us on a thread, encode " +
              lab.arg("encode-us") + " us";
  r.inputs = {{"frames", lab.arg("frames")},       {"period-ms", lab.arg("period-ms")},
              {"sim-us", lab.arg("sim-us")},       {"bake-us", lab.arg("bake-us")},
              {"render-us", lab.arg("render-us")}, {"encode-us", lab.arg("encode-us")},
              {"workers", std::to_string(workers)}};
  const size_t zoom = std::min<size_t>(windows.size(), 3);
  r.zoom_from_ns = windows[windows.size() - zoom].start_ns;
  r.zoom_to_ns = windows.back().end_ns;
  r.zoom_what = "THE LAST " + std::to_string(zoom) + " FRAMES";
  gl::report_load(lab, r, lanes, load, windows);
  if (load.main < 0) {
    lab.fail("the timeline has no `main` lane",
             "The lab names its thread `main` before it records: read engine/prof/timeline.hpp "
             "and run tests/prof_timeline_test.cpp (skill: perf).");
  }
#ifndef NDEBUG
  lab.note(
      "a DEBUG build: the parts' lengths are wall clock and hold, but the pool's spin and "
      "wake times are a sanitized build's. Build PRESET=release to measure.");
#endif
}

const ge::lab::LabSpec kSpec{
    "load",
    ge::lab::LabKind::bench,
    "engine",
    "engine/prof/load.hpp",
    "perf",
    "Does the load monitor see a frame's known shape of work on real threads: the pool's and the "
    "render thread's work, the main thread's waits on them, and the idle cores?",
    {
        {"frames", "30", "frames run"},
        {"period-ms", "16.667", "a frame's length (the present sleeps out the rest)"},
        {"sim-us", "2000", "the main thread's simulation, a frame"},
        {"bake-us", "4000", "the pool's bake, a frame (split over the workers, the last band 2x)"},
        {"render-us", "3000", "the render thread's part, a frame (the main thread waits on it)"},
        {"encode-us", "1000", "the main thread's encoding after the render"},
        {"bin-ms", "5", "the busy chart's bin"},
        {"sample-us", "2000", "the OS sampler's period"},
    },
    "--frames=4 --period-ms=8 --sim-us=500 --bake-us=1000 --render-us=500 --encode-us=200",
    run,
};
GE_LAB(kSpec);

}  // namespace
