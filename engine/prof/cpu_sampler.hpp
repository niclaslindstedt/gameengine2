// THE OS'S VIEW OF THE CPU: every thread of this process and the CPU time each has used, and
// every logical core of the machine and how busy it has been, read from the OS on a background
// thread every few milliseconds. It sees what the timeline (timeline.hpp) cannot: threads that
// never write a slice (the audio device's, the GPU driver's, a software rasterizer's), the CPU a
// spinning thread burns, and the other processes on the machine (in the cores' busy share).
//
//   CpuSampler s;
//   s.start(5000, timeline_origin_ns());   // a sample every 5 ms, times on the timeline's clock
//   ... the run ...
//   s.stop();                               // s.samples(): one row a sample
//
// Each OS: Linux (and Android) read /proc/self/task/<tid>/schedstat (nanoseconds on a core;
// /proc's stat ticks where it is missing) and /proc/stat; macOS and iOS ask the kernel
// (task_threads, thread_info, host_processor_info); Windows lists its threads with a snapshot
// and GetThreadTimes (updated each scheduler tick, about 15.6 ms) and has the whole machine's
// time alone (GetSystemTimes: one row, `whole_machine`). Elsewhere `read_threads` and
// `read_cores` answer false and the sampler keeps empty rows.
//
// A thread's name is the OS's (core/thread_name.hpp gives the engine's threads `ge-...` names);
// the sampler's own thread is named `ge-sampler` and its id is `os_id()`, so a reader can leave
// it out. Timing feeds no simulation result and no digest.
//
// ge:summary The OS's view: every thread's CPU time and every core's busy share, sampled
// ge:keywords prof cpu sampler os threads cores utilisation schedstat proc-stat task_threads
// ge:keywords get-thread-times cpu-time busy idle machine read_threads read_cores cpu_sampler
// ge:skill perf
// ge:test tests/prof_cpu_sampler_test.cpp
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace ge::prof {

struct ThreadTime {
  uint64_t os_id = 0;
  std::string name;    // the OS's name for it ("" when it has none)
  int64_t cpu_ns = 0;  // CPU time used since it started, user and system
};

// A core's time in the OS's own ticks: only the ratio busy / total means anything.
struct CoreTime {
  int64_t busy = 0;
  int64_t total = 0;
};

// Every thread of this process now. False when the OS gives no answer.
bool read_threads(std::vector<ThreadTime>& out);
// Every logical core's ticks since boot (one row for the whole machine on Windows: see
// `cores_are_whole_machine`). False when the OS gives no answer.
bool read_cores(std::vector<CoreTime>& out);
// Whether read_cores answers one row for the whole machine rather than one a core.
bool cores_are_whole_machine();

struct CpuSample {
  int64_t t_ns = 0;  // on the clock start() was given an origin for
  std::vector<ThreadTime> threads;
  std::vector<CoreTime> cores;
};

class CpuSampler {
 public:
  CpuSampler() = default;
  ~CpuSampler() { stop(); }
  CpuSampler(const CpuSampler&) = delete;
  CpuSampler& operator=(const CpuSampler&) = delete;

  // Samples every `period_us` microseconds on a thread of its own until stop(), the first at
  // once; times are now_ns() - origin_ns. Clears the last run's samples.
  void start(int64_t period_us, int64_t origin_ns);
  // Takes one last sample and joins the thread.
  void stop();
  const std::vector<CpuSample>& samples() const { return samples_; }
  // The sampler's own thread (0 before the first start).
  uint64_t os_id() const { return os_id_; }

 private:
  void sample();
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> os_id_{0};
  int64_t period_us_ = 5000;
  int64_t origin_ns_ = 0;
  std::vector<CpuSample> samples_;
};

}  // namespace ge::prof
