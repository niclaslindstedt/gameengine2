// The timing zones' recording (zones.hpp).
//
// ge:summary The zones' recording: span storage, the open stack, the name registry, the fold
// ge:keywords prof zone span recording totals exclusive inclusive registry
// ge:skill perf
// ge:test tests/prof_zones_test.cpp
#include "prof/zones.hpp"

#include <chrono>
#include <deque>
#include <mutex>
#include <string>

#include "prof/memory.hpp"

namespace ge::prof {

namespace {

struct Recording {
  std::vector<Span> spans;
  std::vector<size_t> open;  // the open zones' spans, innermost last
  size_t most = 0;
  size_t dropped = 0;
  int64_t origin_ns = 0;
};

Recording& rec() {
  static Recording r;
  return r;
}

struct Names {
  std::mutex lock;
  std::vector<const char*> names;
  std::deque<std::string> copies;  // the names zone_id_of made: a deque never moves them
};

Names& names() {
  static Names n;
  return n;
}

}  // namespace

namespace detail {

std::atomic<bool> g_on{false};

size_t open(uint32_t id) {
  Recording& r = rec();
  if (r.spans.size() >= r.most) {
    r.dropped++;
    return kNotRecorded;
  }
  Span s;
  s.name = id;
  s.depth = static_cast<uint32_t>(r.open.size());
  s.allocs = allocations().count;
  s.start_ns = now_ns() - r.origin_ns;
  r.spans.push_back(s);
  r.open.push_back(r.spans.size() - 1);
  return r.spans.size() - 1;
}

void close(size_t span) {
  Recording& r = rec();
  // A clear() between the open and the close dropped the span: nothing to finish.
  if (span >= r.spans.size() || r.open.empty() || r.open.back() != span) return;
  Span& s = r.spans[span];
  s.dur_ns = now_ns() - r.origin_ns - s.start_ns;
  s.allocs = allocations().count - s.allocs;
  r.open.pop_back();
}

}  // namespace detail

int64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void start(size_t most_spans) {
  Recording& r = rec();
  r.spans.clear();
  r.spans.reserve(most_spans);
  r.open.clear();
  r.open.reserve(64);
  r.most = most_spans;
  r.dropped = 0;
  r.origin_ns = now_ns();
  detail::g_on.store(true, std::memory_order_relaxed);
}

void stop() { detail::g_on.store(false, std::memory_order_relaxed); }

std::span<const Span> spans() { return rec().spans; }

size_t dropped() { return rec().dropped; }

void clear() {
  Recording& r = rec();
  r.spans.clear();
  r.open.clear();
  r.dropped = 0;
}

std::string_view zone_name(uint32_t id) {
  Names& n = names();
  const std::lock_guard<std::mutex> guard(n.lock);
  return id < n.names.size() ? std::string_view(n.names[id]) : std::string_view("?");
}

uint32_t zone_id(const char* name) {
  Names& n = names();
  const std::lock_guard<std::mutex> guard(n.lock);
  for (size_t i = 0; i < n.names.size(); i++) {
    if (std::string_view(n.names[i]) == name) return static_cast<uint32_t>(i);
  }
  n.names.push_back(name);
  return static_cast<uint32_t>(n.names.size() - 1);
}

uint32_t zone_id_of(std::string_view name) {
  Names& n = names();
  const std::lock_guard<std::mutex> guard(n.lock);
  for (size_t i = 0; i < n.names.size(); i++) {
    if (std::string_view(n.names[i]) == name) return static_cast<uint32_t>(i);
  }
  n.copies.emplace_back(name);
  n.names.push_back(n.copies.back().c_str());
  return static_cast<uint32_t>(n.names.size() - 1);
}

void totals(std::span<const Span> spans, std::vector<ZoneTotal>& out) {
  out.clear();
  std::vector<size_t> row_of;  // zone id -> row in `out`, or npos
  // Children's time and allocations are taken off their parent: a stack of open spans.
  std::vector<size_t> stack;
  std::vector<int64_t> child_ns(spans.size(), 0), child_allocs(spans.size(), 0);
  for (size_t i = 0; i < spans.size(); i++) {
    const Span& s = spans[i];
    while (!stack.empty() && spans[stack.back()].depth >= s.depth) stack.pop_back();
    if (!stack.empty() && s.dur_ns >= 0) {
      child_ns[stack.back()] += s.dur_ns;
      child_allocs[stack.back()] += s.allocs;
    }
    stack.push_back(i);
  }
  for (size_t i = 0; i < spans.size(); i++) {
    const Span& s = spans[i];
    if (s.dur_ns < 0) continue;  // still open when the recording was read
    if (s.name >= row_of.size()) row_of.resize(s.name + 1, ~size_t{0});
    if (row_of[s.name] == ~size_t{0}) {
      row_of[s.name] = out.size();
      out.push_back(ZoneTotal{s.name, 0, 0, 0, 0});
    }
    ZoneTotal& t = out[row_of[s.name]];
    t.calls++;
    t.incl_ns += s.dur_ns;
    t.excl_ns += s.dur_ns - child_ns[i];
    t.excl_allocs += s.allocs - child_allocs[i];
  }
}

}  // namespace ge::prof
