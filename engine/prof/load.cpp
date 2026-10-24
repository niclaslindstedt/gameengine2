// The load analysis' body (load.hpp): flattening a lane, the shares, the concurrency sweep, the
// samples' differences, the bins, each frame's critical path and the findings.
//
// ge:summary The load analysis' body: flatten, shares, concurrency sweep, bins, frames, findings
// ge:keywords prof load flatten sweep concurrency bins critical-path waits findings median
// ge:skill perf
// ge:test tests/prof_load_test.cpp
#include "prof/load.hpp"

#include <algorithm>
#include <map>
#include <thread>

#include "core/num_text.hpp"

namespace ge::prof {

namespace {

std::string ms(int64_t ns, int places = 2) {
  return core::num::to_fixed(static_cast<double>(ns) / 1e6, places);
}
std::string pct(double share) { return core::num::to_fixed(share * 100, 1) + "%"; }

int64_t overlap(int64_t a0, int64_t a1, int64_t b0, int64_t b1) {
  return std::max<int64_t>(0, std::min(a1, b1) - std::max(a0, b0));
}

// Adds `rate` (a share) over [t0, t1) into the bins of `row`, each bin's share of its length.
void spread(std::vector<double>& bins, size_t row, size_t nbins, int64_t from, int64_t bin_ns,
            int64_t t0, int64_t t1, double rate) {
  if (t1 <= t0 || nbins == 0) return;
  const int64_t first = std::max<int64_t>(0, (t0 - from) / bin_ns);
  for (int64_t b = first; b < static_cast<int64_t>(nbins); b++) {
    const int64_t b0 = from + b * bin_ns;
    if (b0 >= t1) break;
    const int64_t o = overlap(t0, t1, b0, b0 + bin_ns);
    bins[row * nbins + static_cast<size_t>(b)] +=
        rate * static_cast<double>(o) / static_cast<double>(bin_ns);
  }
}

int64_t median(std::vector<int64_t> v) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

// The CPU time of thread `id` in sample `s` (0 when it had not started).
int64_t cpu_of(const CpuSample& s, uint64_t id) {
  for (const ThreadTime& t : s.threads) {
    if (t.os_id == id) return t.cpu_ns;
  }
  return 0;
}

}  // namespace

std::vector<Segment> flatten(const Lane& lane, int64_t from_ns, int64_t to_ns) {
  std::vector<Slice> s(lane.slices);
  std::sort(s.begin(), s.end(), [](const Slice& a, const Slice& b) {
    return a.start_ns != b.start_ns ? a.start_ns < b.start_ns : a.depth < b.depth;
  });
  std::vector<Segment> out;
  auto emit = [&](const Slice& x, int64_t t0, int64_t t1) {
    t0 = std::max(t0, from_ns);
    t1 = std::min(t1, to_ns);
    if (t1 <= t0) return;
    if (!out.empty() && out.back().t1 == t0 && out.back().act == x.act &&
        out.back().name == x.name) {
      out.back().t1 = t1;
    } else {
      out.push_back(Segment{t0, t1, x.act, x.name});
    }
  };
  struct Open {
    Slice s;
    int64_t end;
  };
  std::vector<Open> stack;
  int64_t cur = 0;
  for (const Slice& x : s) {
    while (!stack.empty() && stack.back().end <= x.start_ns) {
      emit(stack.back().s, cur, stack.back().end);
      cur = std::max(cur, stack.back().end);
      stack.pop_back();
    }
    if (!stack.empty()) emit(stack.back().s, cur, x.start_ns);
    cur = x.start_ns;
    // A child never outlives its parent (the clock read twice can put them a tick apart).
    const int64_t end =
        stack.empty() ? x.start_ns + x.dur_ns : std::min(x.start_ns + x.dur_ns, stack.back().end);
    stack.push_back(Open{x, end});
  }
  while (!stack.empty()) {
    emit(stack.back().s, cur, stack.back().end);
    cur = std::max(cur, stack.back().end);
    stack.pop_back();
  }
  return out;
}

Load analyze_load(const std::vector<Lane>& lanes, const std::vector<CpuSample>& samples,
                  const std::vector<FrameWindow>& frames, const LoadOptions& o) {
  Load L;
  L.hardware_threads = o.hardware_threads
                           ? o.hardware_threads
                           : std::max<size_t>(1, std::thread::hardware_concurrency());
  L.from_ns = o.from_ns;
  L.to_ns = o.to_ns;
  if (L.to_ns < 0) {
    for (const Lane& l : lanes) {
      for (const Slice& s : l.slices) L.to_ns = std::max(L.to_ns, s.start_ns + s.dur_ns);
    }
    if (!samples.empty()) L.to_ns = std::max(L.to_ns, samples.back().t_ns);
  }
  if (L.to_ns <= L.from_ns) L.to_ns = L.from_ns + 1;
  const int64_t window = L.to_ns - L.from_ns;
  L.bin_ns = std::max<int64_t>(1, o.bin_ns);
  L.bins = static_cast<size_t>((window + L.bin_ns - 1) / L.bin_ns);

  // The lanes: flattened, summed, binned.
  std::vector<std::vector<Segment>> segs;
  std::vector<std::vector<Segment>> work;  // each lane's work segments alone, in time order
  L.lane_bins.assign(lanes.size() * L.bins, 0);
  for (size_t i = 0; i < lanes.size(); i++) {
    const Lane& lane = lanes[i];
    segs.push_back(flatten(lane, L.from_ns, L.to_ns));
    work.emplace_back();
    LaneLoad ll;
    ll.name = lane.name;
    ll.os_id = lane.os_id;
    ll.gpu = lane.gpu;
    ll.dropped = lane.dropped;
    std::map<uint32_t, int64_t> by_name;
    for (const Segment& s : segs.back()) {
      const int64_t d = s.t1 - s.t0;
      if (s.act == Act::work) {
        ll.work_ns += d;
        by_name[s.name] += d;
        work.back().push_back(s);
        spread(L.lane_bins, i, L.bins, L.from_ns, L.bin_ns, s.t0, s.t1, 1.0);
      } else if (s.act == Act::spin) {
        ll.spin_ns += d;
      } else {
        ll.sleep_ns += d;
      }
    }
    ll.untracked_ns = std::max<int64_t>(0, window - ll.work_ns - ll.spin_ns - ll.sleep_ns);
    ll.work_share = static_cast<double>(ll.work_ns) / static_cast<double>(window);
    for (const auto& [name, ns] : by_name) ll.work_by_name.emplace_back(zone_name(name), ns);
    std::sort(ll.work_by_name.begin(), ll.work_by_name.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (lane.name == o.main && !lane.gpu) L.main = static_cast<int>(i);
    if (lane.gpu && L.gpu < 0) L.gpu = static_cast<int>(i);
    L.lanes.push_back(std::move(ll));
  }

  // Concurrency: a sweep over every CPU lane's work.
  std::vector<std::pair<int64_t, int>> edges;
  size_t cpu_lanes = 0;
  int64_t cpu_work = 0;
  for (size_t i = 0; i < lanes.size(); i++) {
    if (lanes[i].gpu) continue;
    cpu_lanes++;
    cpu_work += L.lanes[i].work_ns;
    for (const Segment& s : work[i]) {
      edges.emplace_back(s.t0, +1);
      edges.emplace_back(s.t1, -1);
    }
  }
  std::sort(edges.begin(), edges.end());
  L.time_at_working.assign(cpu_lanes + 1, 0);
  int k = 0;
  int64_t t = L.from_ns;
  for (const auto& [at, d] : edges) {
    if (at > t)
      L.time_at_working[static_cast<size_t>(std::clamp<int>(k, 0, static_cast<int>(cpu_lanes)))] +=
          at - t;
    t = std::max(t, at);
    k += d;
  }
  if (L.to_ns > t) L.time_at_working[0] += L.to_ns - t;
  L.mean_working = static_cast<double>(cpu_work) / static_cast<double>(window);
  L.efficiency = L.mean_working / static_cast<double>(L.hardware_threads);

  // The OS's threads and cores: the samples bracketing the window, and every pair between.
  if (samples.size() >= 2) {
    size_t a = 0, b = samples.size() - 1;
    for (size_t i = 0; i < samples.size(); i++) {
      if (samples[i].t_ns <= L.from_ns) a = i;
    }
    for (size_t i = samples.size(); i-- > 0;) {
      if (samples[i].t_ns >= L.to_ns) b = i;
    }
    if (b <= a) b = std::min(samples.size() - 1, a + 1);
    const double span =
        static_cast<double>(std::max<int64_t>(1, samples[b].t_ns - samples[a].t_ns));
    int64_t total = 0;
    for (const ThreadTime& th : samples[b].threads) {
      if (th.os_id == o.sampler_os_id && o.sampler_os_id != 0) continue;
      OsThreadLoad ot;
      ot.os_id = th.os_id;
      ot.name = th.name;
      ot.cpu_ns = std::max<int64_t>(0, th.cpu_ns - cpu_of(samples[a], th.os_id));
      // At most a core: the OS counts in scheduler ticks, so a short window can read over one.
      ot.share = std::min(1.0, static_cast<double>(ot.cpu_ns) / span);
      total += ot.cpu_ns;
      for (size_t i = 0; i < lanes.size(); i++) {
        if (lanes[i].os_id != 0 && lanes[i].os_id == th.os_id) ot.lane = static_cast<int>(i);
      }
      L.os_threads.push_back(std::move(ot));
    }
    std::sort(L.os_threads.begin(), L.os_threads.end(),
              [](const OsThreadLoad& x, const OsThreadLoad& y) { return x.cpu_ns > y.cpu_ns; });
    for (const OsThreadLoad& ot : L.os_threads) {
      if (ot.lane >= 0) L.lanes[static_cast<size_t>(ot.lane)].cpu_share = ot.share;
    }
    L.os_efficiency = static_cast<double>(total) / (span * static_cast<double>(L.hardware_threads));
    const size_t ncores = std::min(samples[a].cores.size(), samples[b].cores.size());
    L.whole_machine = cores_are_whole_machine();
    for (size_t c = 0; c < ncores; c++) {
      const int64_t dt = samples[b].cores[c].total - samples[a].cores[c].total;
      const int64_t db = samples[b].cores[c].busy - samples[a].cores[c].busy;
      L.cores_busy.push_back(dt > 0 ? static_cast<double>(db) / static_cast<double>(dt) : 0);
    }
    // The bins, pair by pair.
    L.os_bins.assign(L.os_threads.size() * L.bins, 0);
    L.core_bins.assign(ncores * L.bins, 0);
    for (size_t i = a; i < b; i++) {
      const CpuSample& p = samples[i];
      const CpuSample& q = samples[i + 1];
      const int64_t dt = q.t_ns - p.t_ns;
      if (dt <= 0) continue;
      for (size_t r = 0; r < L.os_threads.size(); r++) {
        const uint64_t id = L.os_threads[r].os_id;
        const double rate =
            static_cast<double>(cpu_of(q, id) - cpu_of(p, id)) / static_cast<double>(dt);
        spread(L.os_bins, r, L.bins, L.from_ns, L.bin_ns, p.t_ns, q.t_ns, std::max(0.0, rate));
      }
      for (size_t c = 0; c < ncores && c < p.cores.size() && c < q.cores.size(); c++) {
        const int64_t tt = q.cores[c].total - p.cores[c].total;
        const double share = tt > 0 ? static_cast<double>(q.cores[c].busy - p.cores[c].busy) /
                                          static_cast<double>(tt)
                                    : 0;
        spread(L.core_bins, c, L.bins, L.from_ns, L.bin_ns, p.t_ns, q.t_ns, share);
      }
    }
  }

  // The frames' critical path.
  auto work_in = [&](size_t lane, int64_t t0, int64_t t1) {
    int64_t sum = 0;
    const std::vector<Segment>& w = work[lane];
    auto it = std::lower_bound(w.begin(), w.end(), t0,
                               [](const Segment& s, int64_t v) { return s.t1 <= v; });
    for (; it != w.end() && it->t0 < t1; ++it) sum += overlap(it->t0, it->t1, t0, t1);
    return sum;
  };
  // The CPU lane (not `self`) whose work ended last inside [a, b + wake], or -1.
  auto waker = [&](size_t self, int64_t a, int64_t b) {
    int best = -1;
    int64_t best_end = 0;
    for (size_t i = 0; i < lanes.size(); i++) {
      if (i == self || lanes[i].gpu) continue;
      const std::vector<Segment>& w = work[i];
      auto it = std::upper_bound(w.begin(), w.end(), b + o.wake_ns,
                                 [](int64_t v, const Segment& s) { return v < s.t1; });
      if (it == w.begin()) continue;
      --it;
      if (it->t1 >= a && (best < 0 || it->t1 > best_end)) {
        best = static_cast<int>(i);
        best_end = it->t1;
      }
    }
    return best;
  };
  // The main lane's critical path over the window.
  static const std::string gpu_prefix = kGpuLane;
  if (L.main >= 0) {
    const size_t m = static_cast<size_t>(L.main);
    for (const Segment& s : segs[m]) {
      PathPiece p{s.t0, s.t1, kOwnWork};
      if (s.act != Act::work) {
        const std::string_view name = zone_name(s.name);
        if (name.substr(0, gpu_prefix.size()) == gpu_prefix && L.gpu >= 0) {
          p.on = L.gpu;
        } else {
          const int w = waker(m, s.t0, s.t1);
          p.on = w >= 0 ? w : kUnknownWait;
        }
      }
      L.path.push_back(p);
    }
  }
  for (const FrameWindow& f : frames) {
    FrameLoad fl;
    fl.start_ns = f.start_ns;
    fl.dur_ns = std::max<int64_t>(0, f.end_ns - f.start_ns);
    fl.waits_on.assign(lanes.size(), 0);
    for (size_t i = 0; i < lanes.size(); i++) {
      if (!lanes[i].gpu) fl.work_ns += work_in(i, f.start_ns, f.end_ns);
    }
    if (L.gpu >= 0) fl.gpu_ns = work_in(static_cast<size_t>(L.gpu), f.start_ns, f.end_ns);
    if (L.main >= 0) {
      int64_t covered = 0;
      for (const PathPiece& p : L.path) {
        const int64_t d = overlap(p.t0, p.t1, f.start_ns, f.end_ns);
        if (d == 0) continue;
        covered += d;
        if (p.on == kOwnWork) {
          fl.main_work_ns += d;
        } else if (p.on == kUnknownWait) {
          fl.wait_unknown_ns += d;
        } else {
          fl.waits_on[static_cast<size_t>(p.on)] += d;
        }
      }
      fl.main_idle_ns = std::max<int64_t>(0, fl.dur_ns - covered);
    }
    fl.ideal_ns =
        std::max({static_cast<double>(fl.main_work_ns),
                  static_cast<double>(fl.work_ns) / static_cast<double>(L.hardware_threads),
                  static_cast<double>(std::max<int64_t>(fl.gpu_ns, 0))});
    L.frames.push_back(std::move(fl));
  }

  // The findings.
  const std::string hw = std::to_string(L.hardware_threads);
  if (L.efficiency < 0.5) {
    std::string what = "the marked threads worked " + pct(L.efficiency) + " of the " + hw +
                       " hardware threads' time (" + core::num::to_fixed(L.mean_working, 2) +
                       " threads at once on average)";
    if (L.os_efficiency >= 0 && L.os_efficiency > L.efficiency + 0.1) {
      what += "; the process as a whole used " + pct(L.os_efficiency) +
              " (the rest on threads the timeline never hears)";
    }
    if (L.os_efficiency < 0 || L.os_efficiency < 0.5) what += ": most of the CPU sits idle";
    L.findings.push_back({what,
                          "put independent rows on core::parallel_bands, or overlap independent "
                          "stages on threads (skill: perf)"});
  }
  for (const LaneLoad& ll : L.lanes) {
    if (ll.gpu) continue;
    const double spin = static_cast<double>(ll.spin_ns) / static_cast<double>(window);
    if (spin > 0.05) {
      L.findings.push_back({"lane " + ll.name + " spun " + ms(ll.spin_ns) + " ms (" + pct(spin) +
                                " of the window) waiting: its core burned for nothing",
                            "shorten the spin before it sleeps, or give it work (skill: perf)"});
    }
    if (ll.dropped > 0) {
      L.findings.push_back({"lane " + ll.name + " dropped " + std::to_string(ll.dropped) +
                                " slices past its room: its shares are short",
                            "pass a larger room to timeline_start, or mark coarser work"});
    }
  }
  {
    double unmarked = 0;
    std::string who;
    int named = 0;
    for (const OsThreadLoad& ot : L.os_threads) {
      if (ot.lane >= 0 || ot.share <= 0.02) continue;
      unmarked += ot.share;
      if (named++ < 4) who += (who.empty() ? "" : ", ") + ot.name + " " + pct(ot.share);
    }
    if (unmarked > 0.05) {
      L.findings.push_back(
          {"threads the timeline never hears used " + core::num::to_fixed(unmarked, 2) +
               " cores (" + who + (named > 4 ? ", +" + std::to_string(named - 4) + " more" : "") +
               ")",
           "mark the engine's own with core::name_this_thread and GE_WORK; a driver's or a "
           "library's (one with the process's name, a software rasterizer's) is work the GPU or "
           "the library does"});
    }
  }
  if (!L.frames.empty() && L.main >= 0) {
    std::vector<int64_t> dur, own, idle, unknown, ideal;
    std::vector<std::vector<int64_t>> on(lanes.size());
    for (const FrameLoad& f : L.frames) {
      dur.push_back(f.dur_ns);
      own.push_back(f.main_work_ns);
      idle.push_back(f.main_idle_ns);
      unknown.push_back(f.wait_unknown_ns);
      ideal.push_back(static_cast<int64_t>(f.ideal_ns));
      for (size_t i = 0; i < lanes.size(); i++) on[i].push_back(f.waits_on[i]);
    }
    std::string bound = "the main lane's own work";
    int64_t most = median(own);
    for (size_t i = 0; i < lanes.size(); i++) {
      if (median(on[i]) > most) {
        most = median(on[i]);
        bound = "waiting on " + lanes[i].name;
      }
    }
    if (median(idle) > most) {
      most = median(idle);
      bound = "the main lane idle (outside every slice: a present, a vsync)";
    }
    const int64_t path = median(dur) - median(idle);
    L.findings.push_back({"the median frame (" + ms(median(dur)) + " ms) is bound by " + bound +
                              " (" + ms(most) + " ms); the main lane works " + ms(median(own)) +
                              " ms of it",
                          "look at the lanes chart round the slowest frames"});
    if (path > 0 && static_cast<double>(median(ideal)) < 0.7 * static_cast<double>(path)) {
      L.findings.push_back(
          {"with its CPU work spread over the " + hw +
               " hardware threads and the GPU's alongside, the median frame would take " +
               ms(median(ideal)) + " ms against the " + ms(path) + " ms its critical path takes",
           "overlap what the main lane waits for with its own work (the next frame's prepare "
           "while the GPU draws this one), or move work off it onto threads (skill: perf)"});
    }
  }
  return L;
}

std::string load_text(const Load& L) {
  const int64_t window = L.to_ns - L.from_ns;
  std::string out = "load over " + ms(window, 1) + " ms on " + std::to_string(L.hardware_threads) +
                    " hardware threads: marked work " + pct(L.efficiency) + " of the machine (" +
                    core::num::to_fixed(L.mean_working, 2) + " threads at once)";
  if (L.os_efficiency >= 0) out += ", the process's CPU " + pct(L.os_efficiency);
  size_t name_w = 6;
  for (const LaneLoad& ll : L.lanes) name_w = std::max(name_w, ll.name.size() + (ll.gpu ? 6 : 0));
  std::string head = "  lane";
  head.resize(name_w + 3, ' ');
  out += "\n" + head + "work     spin     sleep    untracked  cpu (OS)  top work\n";
  for (const LaneLoad& ll : L.lanes) {
    std::string row = "  " + ll.name + (ll.gpu ? " (gpu)" : "");
    row.resize(name_w + 3, ' ');
    const auto col = [&](int64_t ns) {
      std::string c = pct(static_cast<double>(ns) / static_cast<double>(window));
      c.resize(std::max<size_t>(c.size(), 9), ' ');
      return c;
    };
    row += col(ll.work_ns) + col(ll.spin_ns) + col(ll.sleep_ns);
    std::string u = col(ll.untracked_ns);
    u.resize(std::max<size_t>(u.size(), 11), ' ');
    row += u;
    std::string cpu = ll.cpu_share >= 0 ? pct(ll.cpu_share) : "-";
    cpu.resize(std::max<size_t>(cpu.size(), 10), ' ');
    row += cpu;
    for (size_t i = 0; i < ll.work_by_name.size() && i < 3; i++) {
      row += (i ? ", " : "") + ll.work_by_name[i].first + " " + ms(ll.work_by_name[i].second, 1) +
             " ms";
    }
    out += row + "\n";
  }
  if (!L.os_threads.empty()) {
    out += "  OS threads by CPU:";
    size_t shown = 0;
    for (size_t i = 0; i < L.os_threads.size() && shown < 8; i++) {
      const OsThreadLoad& t = L.os_threads[i];
      if (t.share < 0.0005) break;  // most first: the rest used no CPU to speak of
      shown++;
      out += (shown > 1 ? ", " : " ") + (t.name.empty() ? std::to_string(t.os_id) : t.name) + " " +
             pct(t.share) + (t.lane < 0 ? " (unmarked)" : "");
    }
    if (L.os_threads.size() > shown) {
      out += ", +" + std::to_string(L.os_threads.size() - shown) + " more";
    }
    out += "\n";
  }
  if (!L.cores_busy.empty()) {
    out += L.whole_machine ? "  the machine busy:" : "  cores busy (every process):";
    for (const double c : L.cores_busy) out += " " + pct(c);
    out += "\n";
  }
  out += "  time with k marked threads working:";
  for (size_t k = 0; k < L.time_at_working.size(); k++) {
    out += " " + std::to_string(k) + ":" +
           pct(static_cast<double>(L.time_at_working[k]) / static_cast<double>(window));
  }
  out += "\n";
  if (!L.frames.empty()) {
    std::vector<int64_t> dur, own, idle, ideal, gpu;
    for (const FrameLoad& f : L.frames) {
      dur.push_back(f.dur_ns);
      own.push_back(f.main_work_ns);
      idle.push_back(f.main_idle_ns);
      ideal.push_back(static_cast<int64_t>(f.ideal_ns));
      if (f.gpu_ns >= 0) gpu.push_back(f.gpu_ns);
    }
    out += "  " + std::to_string(L.frames.size()) + " frames, medians: " + ms(median(dur)) +
           " ms a frame, main works " + ms(median(own)) + " ms, idle " + ms(median(idle)) + " ms";
    for (size_t i = 0; i < L.lanes.size(); i++) {
      std::vector<int64_t> on;
      on.reserve(L.frames.size());
      for (const FrameLoad& f : L.frames) on.push_back(f.waits_on[i]);
      if (median(on) > 0) out += ", waits on " + L.lanes[i].name + " " + ms(median(on)) + " ms";
    }
    if (!gpu.empty()) out += ", gpu busy " + ms(median(gpu)) + " ms";
    out += ", ideal " + ms(median(ideal)) + " ms\n";
  }
  if (L.findings.empty()) out += "  no findings\n";
  for (const LoadFinding& f : L.findings)
    out += "  finding: " + f.what + "; next: " + f.next + "\n";
  return out;
}

}  // namespace ge::prof
