// engine/prof/load.hpp: planted lanes give known shares, concurrency, a critical path laid on the
// pool and the GPU, a frame's waits and its ideal, the OS threads matched to their lanes, and the
// findings an agent acts on.
//
// ge:summary Test: the load analysis over planted lanes and samples
// ge:keywords test prof load utilisation concurrency efficiency critical-path frames ideal
// ge:keywords findings os-threads cores flatten
// ge:skill testing perf
#include "prof/load.hpp"

#include <string>
#include <vector>

#include "check.hpp"
#include "prof/zones.hpp"

namespace {

namespace pf = ge::prof;

constexpr int64_t kMs = 1'000'000;

pf::Slice slice(const char* name, pf::Act act, uint8_t depth, double from_ms, double to_ms) {
  const auto t0 = static_cast<int64_t>(from_ms * 1e6);
  const auto t1 = static_cast<int64_t>(to_ms * 1e6);
  return {pf::zone_id_of(name), act, depth, t0, t1 - t0};
}

// A 100 ms frame on four hardware threads: main simulates, waits for the pool's bands, draws,
// waits for the GPU, then sits untracked (a present); the pool's one worker bakes inside main's
// first wait and the GPU's passes run inside its second.
std::vector<pf::Lane> planted() {
  pf::Lane main{"main", 42, false, {}, 0};
  main.slices = {slice("sim", pf::Act::work, 0, 0, 20),
                 slice("wait for bands", pf::Act::sleep, 0, 20, 50),
                 slice("draw", pf::Act::work, 0, 50, 60), slice("gpu", pf::Act::sleep, 0, 60, 90)};
  pf::Lane pool{"pool 1", 43, false, {}, 0};
  pool.slices = {slice("band", pf::Act::work, 0, 20, 49.9),
                 slice("look for work", pf::Act::spin, 0, 49.9, 60)};
  pf::Lane gpu{pf::kGpuLane, 0, true, {}, 0};
  gpu.slices = {slice("pass", pf::Act::work, 0, 55, 89.9)};
  return {main, pool, gpu};
}

pf::LoadOptions four_threads() {
  pf::LoadOptions o;
  o.to_ns = 100 * kMs;
  o.hardware_threads = 4;
  return o;
}

bool near(double a, double b, double eps = 1e-9) { return a - b < eps && b - a < eps; }

bool has_finding(const pf::Load& L, const std::string& part) {
  for (const pf::LoadFinding& f : L.findings) {
    if (f.what.find(part) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

TEST(load_flatten_keeps_the_innermost_slice) {
  pf::Lane l{"main", 0, false, {}, 0};
  l.slices = {slice("inner", pf::Act::sleep, 1, 2, 5), slice("outer", pf::Act::work, 0, 0, 10)};
  const std::vector<pf::Segment> s = pf::flatten(l, 0, 10 * kMs);
  CHECK(s.size() == 3);
  if (s.size() != 3) return;
  CHECK(s[0].act == pf::Act::work && s[0].t0 == 0 && s[0].t1 == 2 * kMs);
  CHECK(s[1].act == pf::Act::sleep && s[1].t0 == 2 * kMs && s[1].t1 == 5 * kMs);
  CHECK(s[2].act == pf::Act::work && s[2].t0 == 5 * kMs && s[2].t1 == 10 * kMs);
  // Clipped to the window asked for.
  const std::vector<pf::Segment> c = pf::flatten(l, 3 * kMs, 4 * kMs);
  CHECK(c.size() == 1 && c[0].act == pf::Act::sleep && c[0].t1 - c[0].t0 == kMs);
}

TEST(load_shares_and_concurrency_of_planted_lanes) {
  const pf::Load L = pf::analyze_load(planted(), {}, {}, four_threads());
  CHECK(L.lanes.size() == 3);
  CHECK(L.main == 0 && L.gpu == 2);
  CHECK(L.hardware_threads == 4);
  const pf::LaneLoad& m = L.lanes[0];
  CHECK(m.work_ns == 30 * kMs && m.sleep_ns == 60 * kMs && m.untracked_ns == 10 * kMs);
  CHECK(near(m.work_share, 0.3));
  CHECK(m.work_by_name.size() == 2 && m.work_by_name[0].first == "sim");
  const pf::LaneLoad& p = L.lanes[1];
  CHECK(p.work_ns == 29'900'000 && p.spin_ns == 10'100'000);
  // The GPU's work is never the CPU's.
  CHECK(L.time_at_working.size() >= 2);
  CHECK(L.time_at_working[0] == 40'100'000);
  CHECK(L.time_at_working[1] == 59'900'000);
  CHECK(L.time_at_working.size() < 3 || L.time_at_working[2] == 0);
  CHECK(near(L.efficiency, 59.9 / 400.0, 1e-6));
  CHECK(L.os_efficiency < 0);  // no samples
  CHECK(has_finding(L, "most of the CPU sits idle"));
  CHECK(has_finding(L, "lane pool 1 spun"));
}

TEST(load_critical_path_lays_waits_on_what_ended_them) {
  const pf::Load L = pf::analyze_load(planted(), {}, {}, four_threads());
  CHECK(L.path.size() == 4);
  if (L.path.size() != 4) return;
  CHECK(L.path[0].on == pf::kOwnWork);
  CHECK(L.path[1].on == 1);  // the pool's band ended inside the wait for bands
  CHECK(L.path[2].on == pf::kOwnWork);
  CHECK(L.path[3].on == 2);  // a `gpu` wait is the GPU lane's
  // With no lane ending inside it, a wait is unknown.
  std::vector<pf::Lane> alone = planted();
  alone[1].slices.clear();
  alone[1].slices.push_back(slice("band", pf::Act::work, 0, 0, 5));
  const pf::Load U = pf::analyze_load(alone, {}, {}, four_threads());
  CHECK(U.path.size() == 4 && U.path[1].on == pf::kUnknownWait);
}

TEST(load_frame_waits_gpu_and_ideal) {
  const pf::Load L = pf::analyze_load(planted(), {}, {{0, 100 * kMs}}, four_threads());
  CHECK(L.frames.size() == 1);
  if (L.frames.empty()) return;
  const pf::FrameLoad& f = L.frames[0];
  CHECK(f.dur_ns == 100 * kMs);
  CHECK(f.main_work_ns == 30 * kMs);
  CHECK(f.main_idle_ns == 10 * kMs);
  CHECK(f.waits_on.size() == 3 && f.waits_on[1] == 30 * kMs && f.waits_on[2] == 30 * kMs);
  CHECK(f.wait_unknown_ns == 0);
  CHECK(f.work_ns == 59'900'000);
  CHECK(f.gpu_ns == 34'900'000);
  CHECK(near(f.ideal_ns, 34'900'000.0));                          // the GPU's busy time bounds it
  CHECK(has_finding(L, "is bound by the main lane's own work"));  // a tie keeps its own
  CHECK(has_finding(L, "would take"));                            // 34.9 ms against a 90 ms path
  CHECK(!pf::load_text(L).empty());
}

TEST(load_matches_os_threads_to_lanes_and_reads_the_cores) {
  std::vector<pf::CpuSample> samples(2);
  samples[0].t_ns = 0;
  samples[1].t_ns = 100 * kMs;
  samples[0].threads = {{42, "labs", 0}, {77, "llvmpipe-0", 0}, {99, "ge-sampler", 0}};
  samples[1].threads = {
      {42, "labs", 50 * kMs}, {77, "llvmpipe-0", 80 * kMs}, {99, "ge-sampler", 1 * kMs}};
  samples[0].cores = {{0, 0}, {0, 0}};
  samples[1].cores = {{50, 100}, {100, 100}};
  pf::LoadOptions o = four_threads();
  o.sampler_os_id = 99;
  const pf::Load L = pf::analyze_load(planted(), samples, {}, o);
  CHECK(L.os_threads.size() == 2);  // the sampler's own left out
  if (L.os_threads.size() != 2) return;
  CHECK(L.os_threads[0].os_id == 77 && L.os_threads[0].lane == -1);
  CHECK(near(L.os_threads[0].share, 0.8));
  CHECK(L.os_threads[1].os_id == 42 && L.os_threads[1].lane == 0);
  CHECK(near(L.lanes[0].cpu_share, 0.5));
  CHECK(L.lanes[1].cpu_share < 0);  // pool 1's thread was never sampled
  CHECK(L.cores_busy.size() == 2 && near(L.cores_busy[0], 0.5) && near(L.cores_busy[1], 1.0));
  CHECK(near(L.os_efficiency, 130.0 / 400.0));
  CHECK(has_finding(L, "threads the timeline never hears used 0.80 cores (llvmpipe-0"));
}
