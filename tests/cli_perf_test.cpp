// How `ge bench`, `ge profile`, `ge mem` and `ge leaks` read what they measured (engine/cli/
// perf.hpp): a bench answer becomes a line a seed and, beside a tagged run, a warning on a step
// that got slower past the noise; a profile lists every zone and notes a step with no zone; mem
// warns on a hot path that allocates and names its zone; and a leak report, LeakSanitizer's or
// valgrind's, becomes one finding a leak at its first frame inside the repo; and a load answer
// becomes each phase's shares and its findings as notes, with every chart it wrote listed. Running
// them for real on a scaffolded repo is the packaging workflow's `scaffold` job.
//
// ge:summary Tests: the perf commands' readings of a bench answer and of a leak checker's report
// ge:keywords test cli ge bench profile mem leaks load compare regression lsan valgrind findings
// ge:skill agent-interface perf testing
// ge:test tests/cli_perf_test.cpp
#include <string>

#include "check.hpp"
#include "cli/perf.hpp"
#include "play/load.hpp"
#include "prof/zones.hpp"

using namespace ge::cli;

namespace {

BuildOptions repo() {
  BuildOptions o;
  o.repo = fs::temp_directory_path() / "ge_cli_perf_test_repo";
  return o;
}

bool has(const std::string& s, const std::string& part) {
  return s.find(part) != std::string::npos;
}

// A `play bench` answer with one run; `median` and `allocating` vary it.
std::string answer(double median, int allocating, bool zones = true) {
  const std::string runs = (repo().repo / "runs").generic_string();
  return std::string("{\"game\":\"g\",\"hz\":120,\"repeats\":5,\"counting\":true,") +
         "\"peak_rss_bytes\":10485760,\"runs\":[{\"seed\":38,\"steps\":3600," +
         "\"digest\":\"00000000000000ff\",\"same_digest\":true,\"open_ms\":12.5," +
         "\"open_allocs\":40,\"open_zones\":[{\"name\":\"mapgen\",\"calls\":1,\"excl_ms\":11," +
         "\"incl_ms\":12}],\"step_us\":{\"median\":" + std::to_string(median) +
         ",\"mean\":31,\"p95\":40,\"p99\":50,\"max\":90,\"total_ms\":110},\"budget_us\":8333.333," +
         "\"budget_share\":0.004,\"steps_per_s\":30000,\"zones\":" +
         (zones ? "[{\"name\":\"bot\",\"calls_per_step\":3,\"excl_us_per_step\":12," +
                      std::string("\"incl_us_per_step\":12,\"share\":0.4,\"allocs\":0},") +
                      "{\"name\":\"tape\",\"calls_per_step\":1,\"excl_us_per_step\":1," +
                      "\"incl_us_per_step\":1,\"share\":0.02,\"allocs\":17}]"
                : std::string("[]")) +
         ",\"untracked_share\":" + (zones ? "0.09" : "1") +
         ",\"dropped_spans\":0,\"slowest\":[{\"step\":255,\"us\":90,\"zone\":\"bot\"}]," +
         "\"allocs\":{\"total\":57,\"warmup_steps\":120,\"steps_allocating\":" +
         std::to_string(allocating) + ",\"first_allocating\":255,\"zone\":\"tape\"}," +
         "\"series\":\"" + runs + "/g-38.bench.tsv\",\"spans\":\"" + runs + "/g-38.spans.tsv\"}]}";
}

ge::play::FlatJson flat(const std::string& text) {
  ge::play::FlatJson m;
  std::string error;
  CHECK_MSG(ge::play::read_flat(text, m, error, true), error);
  return m;
}

}  // namespace

TEST(a_bench_says_each_seeds_step_cost_and_where_its_series_is) {
  const std::string line = answer(30, 0);
  const Outcome o = bench_outcome(repo(), flat(line), line, nullptr, "");
  CHECK(exit_of(o) == 0 && o.result == line && o.findings.empty());
  CHECK_MSG(has(o.text, "seed 38: 3600 steps, median 30.00 us (0.4% of the 8333 us step)"), o.text);
  CHECK(has(o.text, "zones: bot 40.0%, tape 2.0%, untracked 9.0%"));
  CHECK(has(o.text, "series runs/g-38.bench.tsv"));
  CHECK(has(o.next, "--tag=before"));
}

TEST(a_bench_beside_a_tagged_run_warns_on_a_step_that_got_slower) {
  const std::string was = answer(30, 0);
  const std::string now = answer(36, 0);
  const auto before = flat(was);
  const Outcome slower = bench_outcome(repo(), flat(now), now, &before, "before");
  CHECK_MSG(has(slower.text, "step_us.median: 36.00 vs 30.00 (+20.0%)"), slower.text);
  CHECK(slower.findings.size() == 1 && slower.findings[0].severity == Severity::warn);
  CHECK(has(slower.findings[0].what, "step_us.median is 20.0% slower than the run tagged before"));
  // Within the clock's noise, or within 10%, it is no warning.
  const std::string near = answer(30.3, 0);
  CHECK(bench_outcome(repo(), flat(near), near, &before, "before").findings.empty());
}

TEST(a_profile_lists_every_zone_and_notes_a_step_that_opens_none) {
  const std::string line = answer(30, 0);
  const Outcome o = profile_outcome(repo(), flat(line), line);
  CHECK_MSG(has(o.text, "bot") && has(o.text, "tape") && has(o.text, "40.0%"), o.text);
  CHECK(has(o.text, "slowest steps: 255 (90.0 us, bot)"));
  CHECK(has(o.text, "open: mapgen 11.00 ms"));
  CHECK(has(o.text, "timeline round the slowest step: runs/g-38.spans.tsv"));
  CHECK(o.findings.empty());
  const std::string bare = answer(30, 0, false);
  const Outcome none = profile_outcome(repo(), flat(bare), bare);
  CHECK(none.findings.size() == 1 && has(none.findings[0].what, "opens no timing zone") &&
        has(none.findings[0].next, "GE_ZONE"));
}

TEST(mem_warns_on_a_hot_path_that_allocates_and_names_its_zone) {
  const std::string clean = answer(30, 0);
  const Outcome ok = mem_outcome(repo(), flat(clean), clean);
  CHECK_MSG(ok.findings.empty() && has(ok.text, "after the first 120 steps, 0 steps allocate"),
            ok.text);
  CHECK(has(ok.text, "peak resident set 10.0 MB"));
  const std::string hot = answer(30, 17);
  const Outcome bad = mem_outcome(repo(), flat(hot), hot);
  CHECK(bad.findings.size() == 1 && bad.findings[0].severity == Severity::warn);
  CHECK_MSG(has(bad.findings[0].what,
                "17 steps after the first second allocate (first at step "
                "255), most in zone tape"),
            bad.findings[0].what);
  CHECK(bad.findings[0].where == "zone tape");
}

TEST(a_leak_report_becomes_a_finding_at_its_first_frame_in_the_repo) {
  const std::string root = fs::absolute(repo().repo).generic_string();
  const std::string lsan =
      "==1==ERROR: LeakSanitizer: detected memory leaks\n\n"
      "Direct leak of 28 byte(s) in 1 object(s) allocated from:\n"
      "    #0 0x7f37 in malloc ../../src/libsanitizer/asan/asan_malloc_linux.cpp:69\n"
      "    #1 0x5597 in counted /opt/engine/engine/prof/alloc_hook.cpp:29\n"
      "    #2 0x5598 in open " +
      root + "/play/game.cpp:60\n    #3 0x5599 in main " + root +
      "/play/main.cpp:5\n\n"
      "Indirect leak of 8 byte(s) in 2 object(s) allocated from:\n"
      "    #0 0x7f37 in malloc ../../src/libsanitizer/asan/asan_malloc_linux.cpp:69\n"
      "    #1 0x5600 in make " +
      root +
      "/build/debug/_deps/x.cpp:3\n\n"
      "SUMMARY: AddressSanitizer: 36 byte(s) leaked in 3 allocation(s).\n";
  const auto found = leak_findings(repo(), lsan);
  CHECK_MSG(found.size() == 2, std::to_string(found.size()));
  CHECK(found[0].severity == Severity::error && found[0].where == "play/game.cpp:60");
  CHECK_MSG(has(found[0].what, "Direct leak of 28 bytes in 1 block(s), allocated in open"),
            found[0].what);
  CHECK(found[1].where == "outside the repo (the engine or a library)");
  const std::string valgrind =
      "==9== 40 bytes in 1 blocks are definitely lost in loss record 1 of 1\n"
      "==9==    at 0x4846: operator new(unsigned long) (vg_replace_malloc.c:483)\n"
      "==9==    by 0x1092: open (" +
      root + "/play/game.cpp:61)\n==9== \n==9== LEAK SUMMARY:\n";
  const auto vg = leak_findings(repo(), valgrind);
  CHECK(vg.size() == 1 && vg[0].where == "play/game.cpp:61");
  CHECK(has(vg[0].what, "40 bytes in 1 block(s) definitely lost, allocated in open"));
  CHECK(leak_findings(repo(), "no leaks here\n").empty());
}

namespace {

// A `play load` answer whose steps' analysis is a real one: main simulates, then waits on the
// pool, on two hardware threads.
std::string load_answer() {
  namespace pf = ge::prof;
  pf::Lane main{"main", 1, false, {}, 0};
  main.slices = {{pf::zone_id_of("step"), pf::Act::work, 0, 0, 2'000'000},
                 {pf::zone_id_of("wait for bands"), pf::Act::sleep, 0, 2'000'000, 2'000'000}};
  pf::Lane pool{"pool 1", 2, false, {}, 0};
  pool.slices = {{pf::zone_id_of("band"), pf::Act::work, 0, 2'000'000, 1'900'000}};
  pf::LoadOptions lo;
  lo.to_ns = 8'000'000;
  lo.hardware_threads = 2;
  const pf::Load steps = pf::analyze_load({main, pool}, {}, {{0, 8'000'000}}, lo);
  const pf::Load open = pf::analyze_load({}, {}, {}, lo);
  const std::string runs = (repo().repo / "runs").generic_string();
  return "{\"game\":\"g\",\"hz\":120,\"paced\":true,\"runs\":[{\"seed\":38,\"steps\":1,"
         "\"digest\":\"00000000000000ff\",\"same_digest\":true,\"open_ms\":12.5,"
         "\"steps_ms\":8,\"open\":" +
         ge::play::load_json(open) + ",\"run\":" + ge::play::load_json(steps) + ",\"files\":[\"" +
         runs + "/g-38.lanes.tsv\",\"" + runs + "/g-38.load-steps-lanes.png\"]}]}";
}

}  // namespace

TEST(a_load_says_each_phase_and_turns_its_findings_into_notes) {
  const std::string line = load_answer();
  const Outcome o = load_outcome(repo(), flat(line), line);
  CHECK_MSG(has(o.text, "seed 38: the open 12.5 ms, 1 steps 8.0 ms paced"), o.text);
  CHECK(has(o.text, "the steps:") && has(o.text, "pool 1"));
  CHECK(o.written.size() == 2 && has(o.written[0], "runs/g-38.lanes.tsv"));
  bool idle = false, bound = false;
  for (const Finding& f : o.findings) {
    CHECK(f.severity == Severity::note);
    idle = idle || has(f.what, "seed 38, the steps: the marked threads worked");
    bound = bound || has(f.what, "is bound by the main lane idle");
  }
  std::string all;
  for (const Finding& f : o.findings) all += f.what + "\n";
  CHECK_MSG(idle && bound, all);
  CHECK(exit_of(o) == 0);
  CHECK(has(o.next, "lanes"));
}
