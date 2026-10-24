// ge bench, profile, mem and leaks (perf.hpp).
//
// ge:summary The perf commands: play bench's answer read as a bench, a profile, a memory report
// ge:keywords cli ge bench profile mem leaks findings compare regression lsan valgrind
// ge:skill agent-interface perf
// ge:test tests/cli_perf_test.cpp
#include "cli/perf.hpp"

#include <regex>

#include "cli/json.hpp"
#include "cli/play.hpp"
#include "core/num_text.hpp"

namespace ge::cli {

namespace {

// A step that got this much slower than the tagged run is a warning, once the change is past the
// clock's noise: half a unit (a microsecond of a step, a millisecond of the open).
constexpr double kSlower = 0.10;
constexpr double kNoise = 0.5;

std::string q(const std::string& s) { return " \"" + s + "\""; }

double num(const play::FlatJson& m, const std::string& key) {
  const auto it = m.find(key);
  return it == m.end() ? 0 : it->second.number;
}

std::string fixed(double v, int places) { return core::num::to_fixed(v, places); }

std::string pct(double share) { return fixed(share * 100, 1) + "%"; }

// The bench writes its files under the repo's runs/, wherever the play executable runs.
bool bench_answer(const BuildOptions& o, const PerfOptions& p, play::FlatJson& m, std::string& line,
                  Outcome& out) {
  std::string args = " bench" + q("--out=" + (o.repo / "runs").string());
  if (!p.seeds.empty()) args += q("--seeds=" + p.seeds);
  if (!p.repeats.empty()) args += q("--repeats=" + p.repeats);
  if (!p.steps.empty()) args += q("--steps=" + p.steps);
  for (const std::string& w : p.options) args += q(w);
  return play_command(o, args, m, line, out);
}

int runs_of(const play::FlatJson& m) {
  int n = 0;
  while (m.contains("runs." + std::to_string(n) + ".seed")) n++;
  return n;
}

std::string run_key(int i, const std::string& k) { return "runs." + std::to_string(i) + "." + k; }

// The run of `before` with the same seed, or -1.
int same_seed(const play::FlatJson& before, const std::string& seed) {
  for (int i = 0; i < runs_of(before); i++) {
    if (text_of(before, run_key(i, "seed")) == seed) return i;
  }
  return -1;
}

std::string rel(const BuildOptions& o, const std::string& path) {
  return rel_path(o.repo, fs::path(path));
}

void check_counting(const play::FlatJson& m, Outcome& out) {
  if (text_of(m, "counting") != "true") {
    out.findings.push_back(
        {Severity::warn, "this play executable does not count allocations", "the play target",
         "link it to ge::play (which links ge::prof_alloc) and call play_main (skill: play-test)"});
  }
}

void check_digest(const play::FlatJson& m, int i, Outcome& out) {
  if (text_of(m, run_key(i, "same_digest")) != "true") {
    out.findings.push_back(
        {Severity::error,
         "seed " + text_of(m, run_key(i, "seed")) +
             ": the benched replays did not all reach the pilot's digest, so the run read "
             "something that is not in its inputs",
         "seed " + text_of(m, run_key(i, "seed")),
         "ge sim, then ge replay its log, then ge diverge two logs (skill: play-test)"});
  }
}

}  // namespace

Outcome bench_outcome(const BuildOptions& o, const play::FlatJson& m, const std::string& line,
                      const play::FlatJson* before, const std::string& tag) {
  Outcome out;
  out.result = line;
  std::string text;
  for (int i = 0; i < runs_of(m); i++) {
    const std::string seed = text_of(m, run_key(i, "seed"));
    const double median = num(m, run_key(i, "step_us.median"));
    text += "  seed " + seed + ": " + text_of(m, run_key(i, "steps")) + " steps, median " +
            fixed(median, 2) + " us (" + pct(num(m, run_key(i, "budget_share"))) + " of the " +
            fixed(num(m, run_key(i, "budget_us")), 0) + " us step), p95 " +
            fixed(num(m, run_key(i, "step_us.p95")), 2) + ", p99 " +
            fixed(num(m, run_key(i, "step_us.p99")), 2) + ", worst " +
            fixed(num(m, run_key(i, "step_us.max")), 2) + "; " +
            fixed(num(m, run_key(i, "steps_per_s")), 0) + " steps/s; open " +
            fixed(num(m, run_key(i, "open_ms")), 2) + " ms\n";
    std::string zones;
    for (int z = 0; z < 5 && m.contains(run_key(i, "zones." + std::to_string(z) + ".name")); z++) {
      const std::string k = "zones." + std::to_string(z) + ".";
      zones += (zones.empty() ? "" : ", ") + text_of(m, run_key(i, k + "name")) + " " +
               pct(num(m, run_key(i, k + "share")));
    }
    if (!zones.empty()) {
      text += "    zones: " + zones + ", untracked " + pct(num(m, run_key(i, "untracked_share"))) +
              "\n";
    }
    text += "    series " + rel(o, text_of(m, run_key(i, "series"))) + "\n";
    check_digest(m, i, out);
    if (!before) continue;
    const int j = same_seed(*before, seed);
    if (j < 0) {
      std::string what = "seed " + seed;
      what += " is not in the run tagged ";
      what += tag;
      out.findings.push_back(
          {Severity::note, what, "runs/bench@" + tag + ".json", "bench the same seeds"});
      continue;
    }
    for (const char* key : {"step_us.median", "step_us.p95", "step_us.max", "open_ms"}) {
      const double was = num(*before, run_key(j, key));
      const double now = num(m, run_key(i, key));
      const double change = was > 0 ? now / was - 1 : 0;
      text += "    " + std::string(key) + ": " + fixed(now, 2) + " vs " + fixed(was, 2) + " (" +
              (change >= 0 ? "+" : "") + pct(change) + ")\n";
      if (change > kSlower && now - was > kNoise && std::string(key) != "step_us.max") {
        std::string what = "seed " + seed;
        what += ": ";
        what += key;
        what += " is " + pct(change);
        what += " slower than the run tagged " + tag;
        what += " (" + fixed(was, 2);
        what += " -> " + fixed(now, 2);
        what += ")";
        out.findings.push_back(
            {Severity::warn, what, "seed " + seed,
             "ge profile --seed=" + seed + " to see which zone grew (skill: perf)"});
      }
    }
  }
  out.text = text;
  out.summary = before ? "the step's cost beside the run tagged " + tag
                       : "the step's cost above, each step timed over every repeat";
  out.next = before ? "ge profile for where the time goes"
                    : "ge bench --tag=before, change the code, then ge bench --compare=before";
  return out;
}

Outcome profile_outcome(const BuildOptions& o, const play::FlatJson& m, const std::string& line) {
  Outcome out;
  out.result = line;
  std::string text;
  for (int i = 0; i < runs_of(m); i++) {
    const std::string seed = text_of(m, run_key(i, "seed"));
    text += "  seed " + seed + ", " + text_of(m, run_key(i, "steps")) + " steps, median step " +
            fixed(num(m, run_key(i, "step_us.median")), 2) + " us:\n";
    text += "    zone                  calls/step   us/step    share   allocs\n";
    for (int z = 0; m.contains(run_key(i, "zones." + std::to_string(z) + ".name")); z++) {
      const std::string k = "zones." + std::to_string(z) + ".";
      std::string name = text_of(m, run_key(i, k + "name"));
      name.resize(std::max<size_t>(name.size(), 22), ' ');
      std::string calls = fixed(num(m, run_key(i, k + "calls_per_step")), 2);
      calls.resize(std::max<size_t>(calls.size(), 13), ' ');
      std::string us = fixed(num(m, run_key(i, k + "excl_us_per_step")), 3);
      us.resize(std::max<size_t>(us.size(), 11), ' ');
      std::string share = pct(num(m, run_key(i, k + "share")));
      share.resize(std::max<size_t>(share.size(), 8), ' ');
      text += "    ";
      text += name;
      text += calls;
      text += us;
      text += share;
      text += text_of(m, run_key(i, k + "allocs")) + "\n";
    }
    const double untracked = num(m, run_key(i, "untracked_share"));
    text += "    (in no zone)                                  " + pct(untracked) + "\n";
    std::string slow;
    for (int s = 0; m.contains(run_key(i, "slowest." + std::to_string(s) + ".step")); s++) {
      const std::string k = "slowest." + std::to_string(s) + ".";
      const std::string zone = text_of(m, run_key(i, k + "zone"));
      slow += (slow.empty() ? "" : ", ") + text_of(m, run_key(i, k + "step")) + " (" +
              fixed(num(m, run_key(i, k + "us")), 1) + " us" + (zone.empty() ? "" : ", " + zone) +
              ")";
    }
    text += "    slowest steps: " + slow + "\n";
    for (int z = 0; m.contains(run_key(i, "open_zones." + std::to_string(z) + ".name")); z++) {
      const std::string k = "open_zones." + std::to_string(z) + ".";
      text += "    open: " + text_of(m, run_key(i, k + "name")) + " " +
              fixed(num(m, run_key(i, k + "excl_ms")), 2) + " ms\n";
    }
    text +=
        "    timeline round the slowest step: " + rel(o, text_of(m, run_key(i, "spans"))) + "\n";
    check_digest(m, i, out);
    if (!m.contains(run_key(i, "zones.0.name"))) {
      out.findings.push_back(
          {Severity::note, "seed " + seed + ": the step opens no timing zone", "the game's step",
           "wrap each system in GE_ZONE(\"name\") (prof/zones.hpp), then ge profile again"});
    } else if (untracked > 0.3) {
      out.findings.push_back(
          {Severity::note, "seed " + seed + ": " + pct(untracked) + " of the step is in no zone",
           "the game's step",
           "add GE_ZONE round the systems the step calls outside the zones above"});
    }
    if (num(m, run_key(i, "dropped_spans")) > 0) {
      out.findings.push_back({Severity::warn,
                              "seed " + seed + ": " + text_of(m, run_key(i, "dropped_spans")) +
                                  " zone entries did not fit the recording (24 a step)",
                              "the game's step", "zone systems, not their inner loops"});
    }
  }
  out.text = text;
  out.summary = "where the step's time goes, zone by zone (exclusive time: a zone's own)";
  out.next = "ge graph on the series file, or ge bench --tag=before before a change";
  return out;
}

Outcome mem_outcome(const BuildOptions& /*o*/, const play::FlatJson& m, const std::string& line) {
  Outcome out;
  out.result = line;
  check_counting(m, out);
  std::string text;
  for (int i = 0; i < runs_of(m); i++) {
    const std::string seed = text_of(m, run_key(i, "seed"));
    const std::string steps = text_of(m, run_key(i, "steps"));
    const std::string allocating = text_of(m, run_key(i, "allocs.steps_allocating"));
    const std::string zone = text_of(m, run_key(i, "allocs.zone"));
    text += "  seed " + seed;
    text += ": opening it allocated " + text_of(m, run_key(i, "open_allocs"));
    text += " times; its " + steps;
    text += " steps " + text_of(m, run_key(i, "allocs.total"));
    text += " times; after the first " + text_of(m, run_key(i, "allocs.warmup_steps"));
    text += " steps, " + allocating;
    text += " steps allocate\n";
    if (allocating != "0") {
      std::string what = "seed " + seed;
      what += ": " + allocating;
      what += " steps after the first second allocate (first at step ";
      what += text_of(m, run_key(i, "allocs.first_allocating"));
      what += ")";
      if (!zone.empty()) what += ", most in zone " + zone;
      out.findings.push_back({Severity::warn, what,
                              zone.empty() ? "the game's step" : "zone " + zone,
                              "reserve or reuse that storage so the step allocates nothing "
                              "(skill: perf); ge mem again"});
    }
  }
  const double rss = num(m, "peak_rss_bytes");
  text += "  peak resident set " + fixed(rss / (1024.0 * 1024.0), 1) +
          " MB (the bench's own recording is in it: 40 bytes a zone entry)\n";
  out.text = text;
  out.summary = "what the run allocates, and where after it has warmed up";
  out.next = "ge leaks for what is never freed";
  return out;
}

std::vector<Finding> leak_findings(const BuildOptions& o, const std::string& report) {
  std::vector<Finding> found;
  std::error_code ec;
  const std::string root = fs::weakly_canonical(o.repo, ec).generic_string() + "/";
  const std::string given = fs::absolute(o.repo, ec).generic_string() + "/";
  auto in_repo = [&](const std::string& file) -> std::string {
    std::string f = fs::path(file).generic_string();
    for (const std::string& r : {root, given}) {
      if (f.rfind(r, 0) == 0) {
        f.erase(0, r.size());
        if (f.rfind("build", 0) == 0 || f.rfind("_deps", 0) == 0) return "";
        return f;
      }
    }
    return "";
  };
  // LeakSanitizer: a block's head line, then its frames `#N 0x.. in fn file:line:col`.
  // valgrind: `N bytes in M blocks are definitely lost`, then `at|by 0x..: fn (file:line)`.
  static const std::regex kLsanHead(R"((Direct|Indirect) leak of (\d+) byte\(s\) in (\d+) object)");
  static const std::regex kLsanFrame(
      R"(#\d+ 0x[0-9a-f]+ in (.+?) (/[^ :]+|[A-Za-z]:[^ :]+):(\d+))");
  static const std::regex kVgHead(
      R"(([\d,]+) bytes in ([\d,]+) blocks are (definitely|possibly) lost)");
  static const std::regex kVgFrame(
      R"((?:at|by) 0x[0-9A-F]+: (.+?) \(((?:[A-Za-z]:)?[^():]+):(\d+)\))");
  std::string what, where, fn;
  bool open = false;
  auto flush = [&] {
    if (!open) return;
    found.push_back({Severity::error, what + (fn.empty() ? "" : ", allocated in " + fn),
                     where.empty() ? "outside the repo (the engine or a library)" : where,
                     "free it where its owner dies, or hold it in a value or a unique_ptr "
                     "(skill: perf); ge leaks again"});
    open = false;
  };
  for (const auto view : split_lines(report)) {
    const std::string l(view);
    std::smatch s;
    if (std::regex_search(l, s, kLsanHead)) {
      flush();
      what = s[1].str() + " leak of " + s[2].str() + " bytes in " + s[3].str() + " block(s)";
      where.clear();
      fn.clear();
      open = true;
    } else if (std::regex_search(l, s, kVgHead)) {
      flush();
      what = s[1].str() + " bytes in " + s[2].str() + " block(s) " + s[3].str() + " lost";
      where.clear();
      fn.clear();
      open = true;
    } else if (open && where.empty() &&
               (std::regex_search(l, s, kLsanFrame) || std::regex_search(l, s, kVgFrame))) {
      const std::string file = in_repo(s[2].str());
      if (!file.empty()) {
        where = file + ":" + s[3].str();
        fn = s[1].str();
      }
    } else if (l.find("SUMMARY:") != std::string::npos ||
               l.find("LEAK SUMMARY") != std::string::npos) {
      flush();
    }
  }
  flush();
  return found;
}

Outcome run_bench(const BuildOptions& o, const PerfOptions& p) {
  Outcome out;
  play::FlatJson m;
  std::string line;
  if (!bench_answer(o, p, m, line, out)) return out;
  play::FlatJson before;
  bool have_before = false;
  if (!p.compare.empty()) {
    const fs::path file = o.repo / "runs" / ("bench@" + p.compare + ".json");
    const auto text = read_text(file);
    std::string why;
    if (!text || !play::read_flat(*text, before, why, true)) {
      out.findings.push_back({Severity::error, "no bench is tagged " + p.compare,
                              rel_path(o.repo, file),
                              "ge bench --tag=" + p.compare + " first, on the code before"});
      return out;
    }
    have_before = true;
  }
  Outcome b = bench_outcome(o, m, line, have_before ? &before : nullptr, p.compare);
  if (!p.tag.empty()) {
    const fs::path file = o.repo / "runs" / ("bench@" + p.tag + ".json");
    std::string why;
    if (write_text(file, line + "\n", why)) {
      b.written.push_back(rel_path(o.repo, file));
    } else {
      b.findings.push_back({Severity::error, "cannot keep the tagged run: " + why,
                            rel_path(o.repo, file), "check the directory"});
    }
  }
  return b;
}

Outcome run_profile(const BuildOptions& o, const PerfOptions& p) {
  Outcome out;
  play::FlatJson m;
  std::string line;
  if (!bench_answer(o, p, m, line, out)) return out;
  Outcome prof = profile_outcome(o, m, line);
  if (!p.render) return prof;
  // The frame: the game's `profile` lab (CPU and GPU ms per render pass).
  LabOptions lab;
  lab.id = "profile";
  Outcome frame = run_lab(o, lab);
  if (exit_of(frame) != 0 || frame.result.find("\"report\":null") != std::string::npos) {
    prof.findings.push_back(
        {Severity::note, "the game has no `profile` lab, or it did not run", "labs/",
         "ge add lab profile --kind=bench and time the frame's passes in it (skill: labs)"});
    for (Finding& f : frame.findings) prof.findings.push_back(std::move(f));
    return prof;
  }
  prof.text += "  the frame (the profile lab):\n" + frame.text;
  prof.result = json_obj({{"sim", line}, {"frame", frame.result}});
  return prof;
}

Outcome run_mem(const BuildOptions& o, const PerfOptions& p) {
  Outcome out;
  play::FlatJson m;
  std::string line;
  if (!bench_answer(o, p, m, line, out)) return out;
  return mem_outcome(o, m, line);
}

Outcome run_leaks(const BuildOptions& o, const PerfOptions& p) {
  Outcome out;
  if (!build_into(o, out)) return out;
  const auto cache = read_text(o.repo / "build" / o.preset / "CMakeCache.txt").value_or("");
  const bool sanitized = cache.find("SANITIZE:BOOL=ON") != std::string::npos;
  const bool valgrind = p.valgrind || !sanitized;
  std::string args = " sim --out=\"" + (o.repo / "build" / o.preset / "ge-leaks").string() + "\"";
  if (!p.seeds.empty()) args += q("--seeds=" + p.seeds);
  for (const std::string& w : p.options) args += q(w);
  std::string cmd;
#if defined(_WIN32) || defined(__APPLE__)
  if (!valgrind || run_quiet("valgrind --version") != 0) {
    out.findings.push_back(
        {Severity::note, "this platform's sanitizers do not find leaks, and valgrind is not here",
         "this machine",
         "run ge leaks on Linux (the debug preset), where LeakSanitizer runs (skill: perf)"});
    return out;
  }
#endif
  if (valgrind) {
#ifndef _WIN32
    if (run_quiet("valgrind --version") != 0) {
      out.findings.push_back(
          {Severity::error, "the " + o.preset + " build has no sanitizer, and valgrind is not here",
           "build/" + o.preset, "ge leaks --preset=debug (its sanitizers find leaks)"});
      return out;
    }
#endif
    cmd = "valgrind --leak-check=full --fullpath-after= --error-exitcode=0 \"" +
          play_exe(o).string() + "\"" + args;
  } else {
    cmd = "ASAN_OPTIONS=detect_leaks=1 \"" + play_exe(o).string() + "\"" + args;
  }
  const fs::path log = build_log(o, "ge-leaks");
  std::error_code ec;
  fs::remove(log, ec);
  const Logged r = run_to_log(cmd, log);
  out.findings = leak_findings(o, r.output);
  if (out.findings.empty() && r.code != 0) {
    out.findings.push_back(
        {Severity::error,
         "the run under the leak checker failed (exit " + std::to_string(r.code) + ")",
         rel_path(o.repo, log), "read the log"});
  }
  out.text = "  the pilot's run" + std::string(p.seeds.empty() ? "" : " of seeds " + p.seeds) +
             " under " + (valgrind ? "valgrind's memcheck" : "LeakSanitizer") +
             "; the report: " + rel_path(o.repo, log) + "\n";
  out.summary = out.findings.empty() ? "no leaks: every block the run allocated was freed"
                                     : "the leaks above, each at the first frame in the repo";
  out.next =
      out.findings.empty() ? "ge mem for what the steps allocate" : "fix the first, then ge leaks";
  return out;
}

Outcome load_outcome(const BuildOptions& o, const play::FlatJson& m, const std::string& line) {
  Outcome out;
  out.result = line;
  std::string text;
  for (int i = 0; i < runs_of(m); i++) {
    const std::string seed = text_of(m, run_key(i, "seed"));
    text += "  seed " + seed + ": the open " + fixed(num(m, run_key(i, "open_ms")), 1) + " ms, " +
            text_of(m, run_key(i, "steps")) + " steps " + fixed(num(m, run_key(i, "steps_ms")), 1) +
            " ms" + (text_of(m, "paced") == "true" ? " paced" : " back to back") + "\n";
    for (const char* phase : {"open", "run"}) {
      const std::string k = std::string(phase) + ".";
      std::string body = text_of(m, run_key(i, k + "text"));
      std::string indented;
      for (const auto view : split_lines(body)) {
        if (!view.empty()) indented += "    " + std::string(view) + "\n";
      }
      text += "  " + std::string(phase == std::string("open") ? "the open" : "the steps") + ":\n" +
              indented;
      for (int f = 0; m.contains(run_key(i, k + "findings." + std::to_string(f) + ".what")); f++) {
        const std::string fk = k + "findings." + std::to_string(f) + ".";
        out.findings.push_back({Severity::note,
                                "seed " + seed + ", " +
                                    (phase == std::string("open") ? "the open" : "the steps") +
                                    ": " + text_of(m, run_key(i, fk + "what")),
                                "seed " + seed, text_of(m, run_key(i, fk + "next"))});
      }
    }
    for (int f = 0; m.contains(run_key(i, "files." + std::to_string(f))); f++) {
      out.written.push_back(rel(o, text_of(m, run_key(i, "files." + std::to_string(f)))));
    }
    if (text_of(m, run_key(i, "same_digest")) != "true") check_digest(m, i, out);
  }
  out.text = text;
  out.summary =
      "how the run spreads over the machine: each thread's work, the cores, the "
      "critical path";
  out.next = "open the -lanes.png charts, or ge graph runs/<game>-<seed>.lanes.tsv";
  return out;
}

Outcome run_load(const BuildOptions& o, const LoadOptions& l) {
  Outcome out;
  std::string args = " load" + q("--out=" + (o.repo / "runs").string());
  if (!l.seeds.empty()) args += q("--seeds=" + l.seeds);
  if (!l.steps.empty()) args += q("--steps=" + l.steps);
  if (!l.paced.empty()) args += q("--paced=" + l.paced);
  if (!l.bin_ms.empty()) args += q("--bin-ms=" + l.bin_ms);
  for (const std::string& w : l.options) args += q(w);
  play::FlatJson m;
  std::string line;
  if (!play_command(o, args, m, line, out)) return out;
  Outcome load = load_outcome(o, m, line);
  if (!l.render) return load;
  // The frame loop: the game's `gpu-load` lab (its threads and the GPU while it draws).
  LabOptions lab;
  lab.id = "gpu-load";
  Outcome frame = run_lab(o, lab);
  if (exit_of(frame) != 0 || frame.result.find("\"report\":null") != std::string::npos) {
    load.findings.push_back(
        {Severity::note, "the game has no `gpu-load` lab, or it did not run", "labs/",
         "ge add lab gpu-load --kind=bench and record its frame loop with prof/timeline.hpp "
         "(skill: perf)"});
    for (Finding& f : frame.findings) load.findings.push_back(std::move(f));
    return load;
  }
  load.text += "  the frame loop (the gpu-load lab):\n" + frame.text;
  load.result = json_obj({{"sim", line}, {"frame", frame.result}});
  return load;
}

}  // namespace ge::cli
