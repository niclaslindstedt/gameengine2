// THE ENGINE'S OWN PROFILER: named timing zones a game, a plugin or the renderer opens around the
// work it wants costed, recorded only while something asked for a recording. It answers "where does
// the CPU time of a step or a frame go" without an external profiler, on every platform, headless:
//
//   void step(GameState& s) {
//     GE_ZONE("step");
//     { GE_ZONE("skier"); step_skier(...); }   // nested: "skier" inside "step"
//   }
//
// Off (the default), a zone is one relaxed load and a branch: leave the zones in. On (`start`),
// each zone writes one `Span`: its name, its depth, when it opened and how long it ran, and how
// many heap allocations happened inside it (when the executable counts them, memory.hpp). `totals`
// folds a recording into one row a zone name: calls, inclusive and exclusive time, exclusive
// allocations. `ge profile`, `ge bench` and `ge mem` read those rows (engine/play/bench.hpp), and
// the `profile` lab times the renderer's prepares with the same zones.
//
// Rules: a zone's name is a string literal (it is kept by pointer); zones nest by scope and never
// cross a return to the caller's caller; the recording is one thread's (the simulation's), so a
// zone opened on another thread is not recorded. Timing reads the wall clock and feeds no
// simulation result and no digest.
//
// ge:summary Named timing zones (GE_ZONE): spans with depth, time and allocations, folded per name
// ge:keywords prof profile profiler zone ge_zone span timing cpu-ms hotspot exclusive inclusive
// ge:keywords recording totals flame timeline
// ge:skill perf
// ge:test tests/prof_zones_test.cpp
#pragma once

#include <atomic>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace ge::prof {

struct Span {
  uint32_t name = 0;     // zone_name(name) spells it
  uint32_t depth = 0;    // 0 for a zone opened with no zone open
  int64_t start_ns = 0;  // since `start`
  int64_t dur_ns = -1;   // -1 while the zone is still open
  int64_t allocs = 0;    // heap allocations inside it, its children's included
};

// One row a zone name, over a recording.
struct ZoneTotal {
  uint32_t name = 0;
  int64_t calls = 0;
  int64_t incl_ns = 0;  // its own time and its children's
  int64_t excl_ns = 0;  // its own time alone
  int64_t excl_allocs = 0;
};

// Starts a recording with room for `most_spans` spans (zones past them are counted in `dropped`
// and not kept), clearing the last one. The room is reserved here, so recording allocates nothing.
void start(size_t most_spans);
void stop();
// The recording's spans in the order the zones opened.
std::span<const Span> spans();
size_t dropped();
// Empties the recording and keeps it running (a bench clears it between steps it has read).
void clear();
std::string_view zone_name(uint32_t id);
// The id of a zone name (registered on first use). GE_ZONE calls it once a site.
uint32_t zone_id(const char* name);
// The id of a name made at run time (read from a file): the name is copied and kept for the life
// of the process, so a reader that interns a file's names keeps one copy a name.
uint32_t zone_id_of(std::string_view name);

// Folds `spans` into one row a zone name, in the order the names first appear.
void totals(std::span<const Span> spans, std::vector<ZoneTotal>& out);

// Nanoseconds on the steady clock (a bench's own step timing reads the same clock).
int64_t now_ns();

namespace detail {
extern std::atomic<bool> g_on;
size_t open(uint32_t id);
void close(size_t span);
inline constexpr size_t kNotRecorded = ~size_t{0};
}  // namespace detail

class Zone {
 public:
  explicit Zone(uint32_t id)
      : span_(detail::g_on.load(std::memory_order_relaxed) ? detail::open(id)
                                                           : detail::kNotRecorded) {}
  ~Zone() {
    if (span_ != detail::kNotRecorded) detail::close(span_);
  }
  Zone(const Zone&) = delete;
  Zone& operator=(const Zone&) = delete;

 private:
  size_t span_;
};

}  // namespace ge::prof

#define GE_ZONE_CAT2(a, b) a##b
#define GE_ZONE_CAT(a, b) GE_ZONE_CAT2(a, b)
// A zone from here to the end of the enclosing scope, named by a string literal.
#define GE_ZONE(name)                                                                   \
  static const uint32_t GE_ZONE_CAT(ge_zone_id_, __LINE__) = ::ge::prof::zone_id(name); \
  const ::ge::prof::Zone GE_ZONE_CAT(ge_zone_, __LINE__)(GE_ZONE_CAT(ge_zone_id_, __LINE__))
