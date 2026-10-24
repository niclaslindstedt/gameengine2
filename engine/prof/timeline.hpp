// EVERY THREAD'S TIMELINE: what each thread of the process was doing, and when, while a
// recording runs, so an agent can see how the work of a frame or a session is spread over the
// cores, where a thread waits for another and where cores sit idle (prof/load.hpp reads it).
//
//   ge::core::name_this_thread("main");            // once a thread: its lane's name
//   ge::prof::timeline_start(1 << 16);             // room for 65536 slices a lane
//   { GE_WORK("frame.prepare"); prepare(); }       // a stretch of work on this thread
//   { GE_WAIT("gpu"); wait_for_gpu(); }            // this thread blocked, its core given back
//   ge::prof::timeline_stop();
//   std::vector<ge::prof::Lane> lanes = ge::prof::timeline_lanes();
//
// A LANE is one thread: its name (core/thread_name.hpp; `thread N` when it never named itself),
// its OS id (to match the OS sampler's threads, prof/cpu_sampler.hpp) and its SLICES. A slice is
// one stretch of one ACT: `work` (the thread runs code for a purpose), `spin` (it burns its core
// waiting) or `sleep` (it is blocked and gives its core back). Slices nest by scope (`depth`):
// at any moment a thread is doing its innermost open slice, and a moment no slice covers is
// untracked (a thread the timeline never hears from is untracked all along: the OS sampler still
// sees its CPU time).
//
// The worker pool (core/parallel.hpp) is heard without a line of code: while a recording runs
// its listener writes each band as `band` work, a caller's wait for its last band as `wait for
// bands`, a worker spinning for the next call as `look for work` and asleep as `asleep`.
//
// Cost: off, a scope is one relaxed load. On, a scope reads the steady clock twice and writes one
// slice into its thread's own buffer (no lock; the room is reserved at the start, so recording
// allocates nothing after a thread's first slice). A slice past a lane's room is counted in its
// `dropped`, not kept. Start, stop and read from one thread, the one that owns the recording;
// slices that close while it restarts are dropped. Timing feeds no simulation result and no
// digest. Names are string literals kept by pointer, in the zones' registry (`zone_name`).
//
// ge:summary Every thread's timeline: work, spin and sleep slices per thread, the pool heard too
// ge:keywords prof timeline lanes threads per-thread work wait spin sleep idle utilisation
// ge:keywords ge_work ge_wait ge_spin timeline_start timeline_lanes critical-path cores load
// ge:skill perf
// ge:test tests/prof_timeline_test.cpp
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "prof/zones.hpp"

namespace ge::prof {

// What a thread does in a slice.
enum class Act : uint8_t { work, spin, sleep };

struct Slice {
  uint32_t name = 0;  // zone_name(name) spells it
  Act act = Act::work;
  uint8_t depth = 0;     // 0 for a slice opened with none open on its thread
  int64_t start_ns = 0;  // since timeline_start
  int64_t dur_ns = 0;
};

struct Lane {
  std::string name;
  uint64_t os_id = 0;         // core::this_thread_os_id() of its thread; 0 for a made lane
  bool gpu = false;           // a made lane of the GPU's passes (prof/load.hpp)
  std::vector<Slice> slices;  // in the order they closed
  int64_t dropped = 0;        // slices past the lane's room
};

// Starts a recording with room for `most_slices` slices a lane, clearing the last one, and
// listens to the worker pool (from then on: an unheard pool event is one relaxed load and a call).
void timeline_start(size_t most_slices);
// Stops it; the slices stay readable.
void timeline_stop();
bool timeline_on();
// The steady clock (now_ns) at timeline_start: a slice's start is after it.
int64_t timeline_origin_ns();
// Every lane that wrote a slice in this recording, in the order the threads first wrote.
std::vector<Lane> timeline_lanes();

namespace detail {
extern std::atomic<bool> g_timeline_on;
struct OpenSlice {
  int64_t start_ns;
  uint64_t generation;
  uint32_t name;
  Act act;
  uint8_t depth;
};
void open_slice(OpenSlice& s, uint32_t name, Act act);
void close_slice(const OpenSlice& s);
}  // namespace detail

class Activity {
 public:
  Activity(uint32_t name, Act act) : on_(detail::g_timeline_on.load(std::memory_order_relaxed)) {
    if (on_) detail::open_slice(slice_, name, act);
  }
  ~Activity() {
    if (on_) detail::close_slice(slice_);
  }
  Activity(const Activity&) = delete;
  Activity& operator=(const Activity&) = delete;

 private:
  bool on_;
  detail::OpenSlice slice_{};
};

}  // namespace ge::prof

#define GE_ACT_(name, act)                                                             \
  static const uint32_t GE_ZONE_CAT(ge_act_id_, __LINE__) = ::ge::prof::zone_id(name); \
  const ::ge::prof::Activity GE_ZONE_CAT(ge_act_, __LINE__)(GE_ZONE_CAT(ge_act_id_, __LINE__), act)
// This thread works from here to the end of the scope, on what the string literal names.
#define GE_WORK(name) GE_ACT_(name, ::ge::prof::Act::work)
// This thread is blocked (asleep in a wait, its core given back) till the end of the scope.
#define GE_WAIT(name) GE_ACT_(name, ::ge::prof::Act::sleep)
// This thread burns its core waiting (a spin loop) till the end of the scope.
#define GE_SPIN(name) GE_ACT_(name, ::ge::prof::Act::spin)
