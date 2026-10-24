// The timeline's recording (timeline.hpp): one buffer a thread, written by that thread alone,
// the registry of lanes, and the pool's listener.
//
// A buffer's slots are reserved at the start and published by its count (a release store the
// reader's acquire load pairs with), so a writer takes no lock. A restart bumps the generation:
// a slice opened under the old one is dropped when it closes. A restart that needs more room than
// a lane has swaps in a bigger array and keeps the old one alive to the end of the process, so a
// writer that read the old pointer an instant before writes into memory that is still there.
//
// ge:summary The timeline's recording: per-thread slice buffers, the lane registry, the listener
// ge:keywords prof timeline lane buffer registry generation pool-listener slice
// ge:skill perf
// ge:test tests/prof_timeline_test.cpp
#include "prof/timeline.hpp"

#include <memory>
#include <mutex>

#include "core/parallel.hpp"
#include "core/thread_name.hpp"

namespace ge::prof {

namespace {

struct LaneBuf {
  std::string name;
  uint64_t os_id = 0;
  std::atomic<Slice*> slots{nullptr};
  std::atomic<size_t> room{0};   // the array's size, which only grows
  std::atomic<size_t> limit{0};  // this recording's room, at most `room`
  std::atomic<size_t> count{0};
  std::atomic<int64_t> dropped{0};
};

struct Registry {
  std::mutex lock;
  std::vector<std::unique_ptr<LaneBuf>> lanes;
  std::vector<std::unique_ptr<Slice[]>> arrays;  // every array ever handed out, kept alive
  size_t room = 0;
  int64_t origin_ns = 0;
};

Registry& registry() {
  static Registry* r = new Registry;  // never destroyed: threads may write while the process ends
  return *r;
}

std::atomic<uint64_t> g_generation{0};

thread_local LaneBuf* t_lane = nullptr;
thread_local uint8_t t_depth = 0;

void give_room(Registry& r, LaneBuf& l) {
  if (l.room.load() >= r.room) {
    l.limit.store(r.room);
    return;
  }
  r.arrays.push_back(std::make_unique<Slice[]>(r.room));
  l.slots.store(r.arrays.back().get());
  l.room.store(r.room);
  l.limit.store(r.room);
}

LaneBuf& this_lane() {
  if (t_lane) return *t_lane;
  Registry& r = registry();
  const std::lock_guard<std::mutex> guard(r.lock);
  auto lane = std::make_unique<LaneBuf>();
  const std::string& named = core::this_thread_name();
  lane->name = named.empty() ? "thread " + std::to_string(r.lanes.size() + 1) : named;
  lane->os_id = core::this_thread_os_id();
  give_room(r, *lane);
  t_lane = lane.get();
  r.lanes.push_back(std::move(lane));
  return *t_lane;
}

void put(const Slice& s, uint64_t generation) {
  if (g_generation.load(std::memory_order_acquire) != generation) return;
  LaneBuf& l = this_lane();
  const size_t i = l.count.load(std::memory_order_relaxed);
  if (i >= l.limit.load(std::memory_order_relaxed)) {
    l.dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  l.slots.load(std::memory_order_acquire)[i] = s;
  l.count.store(i + 1, std::memory_order_release);
}

// The pool's events on this thread: the band it runs, the wait it is in.
struct PoolState {
  detail::OpenSlice band{};
  detail::OpenSlice wait{};
  bool in_band = false;
  bool in_wait = false;
};

thread_local PoolState t_pool;

uint32_t name_of(bool idle, Act act) {
  static const uint32_t caller = zone_id("wait for bands");
  static const uint32_t look = zone_id("look for work");
  static const uint32_t asleep = zone_id("asleep");
  return idle ? (act == Act::spin ? look : asleep) : caller;
}

void hear_pool(core::PoolEvent e, bool idle) {
  PoolState& p = t_pool;
  // A begin heard while the recording is off opens nothing; an end always closes what is open
  // (its slice is dropped by the generation when it is not this recording's).
  const bool on = detail::g_timeline_on.load(std::memory_order_relaxed);
  if (!on && e != core::PoolEvent::band_end && e != core::PoolEvent::wait_end) {
    if (p.in_wait) detail::close_slice(p.wait);  // a spin that sleeps after the stop
    p.in_wait = false;
    return;
  }
  static const uint32_t band = zone_id("band");
  switch (e) {
    case core::PoolEvent::band_begin:
      detail::open_slice(p.band, band, Act::work);
      p.in_band = true;
      break;
    case core::PoolEvent::band_end:
      if (p.in_band) detail::close_slice(p.band);
      p.in_band = false;
      break;
    case core::PoolEvent::spin_begin:
    case core::PoolEvent::sleep_begin: {
      if (p.in_wait) detail::close_slice(p.wait);  // a spin that gave up and sleeps
      const Act act = e == core::PoolEvent::spin_begin ? Act::spin : Act::sleep;
      detail::open_slice(p.wait, name_of(idle, act), act);
      p.in_wait = true;
      break;
    }
    case core::PoolEvent::wait_end:
      if (p.in_wait) detail::close_slice(p.wait);
      p.in_wait = false;
      break;
  }
}

}  // namespace

namespace detail {

std::atomic<bool> g_timeline_on{false};

void open_slice(OpenSlice& s, uint32_t name, Act act) {
  s.start_ns = now_ns();
  s.generation = g_generation.load(std::memory_order_acquire);
  s.name = name;
  s.act = act;
  s.depth = t_depth++;
}

void close_slice(const OpenSlice& s) {
  t_depth = s.depth;
  const int64_t end = now_ns();
  put(Slice{s.name, s.act, s.depth, s.start_ns, end - s.start_ns}, s.generation);
}

}  // namespace detail

void timeline_start(size_t most_slices) {
  Registry& r = registry();
  {
    const std::lock_guard<std::mutex> guard(r.lock);
    detail::g_timeline_on.store(false);
    g_generation.fetch_add(1);
    r.room = most_slices;
    r.origin_ns = now_ns();
    for (auto& lane : r.lanes) {
      give_room(r, *lane);
      lane->count.store(0);
      lane->dropped.store(0);
    }
  }
  core::set_pool_listener(hear_pool);
  detail::g_timeline_on.store(true);
}

// The listener stays once installed: an end the pool sends after the stop still closes its
// slice, so a thread's nesting is right when the next recording starts.
void timeline_stop() { detail::g_timeline_on.store(false); }

bool timeline_on() { return detail::g_timeline_on.load(std::memory_order_relaxed); }

int64_t timeline_origin_ns() {
  Registry& r = registry();
  const std::lock_guard<std::mutex> guard(r.lock);
  return r.origin_ns;
}

std::vector<Lane> timeline_lanes() {
  Registry& r = registry();
  const std::lock_guard<std::mutex> guard(r.lock);
  std::vector<Lane> out;
  for (const auto& l : r.lanes) {
    const size_t n = l->count.load(std::memory_order_acquire);
    if (n == 0 && l->dropped.load() == 0) continue;
    Lane lane;
    lane.name = l->name;
    lane.os_id = l->os_id;
    lane.dropped = l->dropped.load();
    const Slice* slots = l->slots.load(std::memory_order_acquire);
    lane.slices.reserve(n);
    for (size_t i = 0; i < n; i++) {
      Slice s = slots[i];
      s.start_ns -= r.origin_ns;
      // A slice that began before the recording (a scope open across the start) is not its.
      if (s.start_ns >= 0) lane.slices.push_back(s);
    }
    out.push_back(std::move(lane));
  }
  return out;
}

}  // namespace ge::prof
