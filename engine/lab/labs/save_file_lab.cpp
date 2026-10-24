// LAB `save-file`: the save file (engine/save/file.hpp) over a run-shaped state of every kind of
// field (a world's scalars and its stream, racers each with a name, a pose, a path of plain
// points, gates, an optional lift, notes): does it round-trip to the byte, is every cut and every
// flipped byte of a small file refused, do an older chunk read with its defaults and a newer one
// refuse naming both versions, and how fast are files written, opened (every checksum checked),
// read and listed (`peek`)? The state is drawn off a seeded stream, so the file's bytes and the
// refusals are deterministic; the speeds are measured.
//
// ge:summary Lab: the save file's round trip, refusals, versions and speeds on a run-shaped state
// ge:keywords lab save save-file round-trip refuse truncate flip version speed throughput
// ge:keywords write read open peek checksum bytes
// ge:skill save labs
// ge:test tests/save_file_test.cpp
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "core/prng.hpp"
#include "lab/lab.hpp"
#include "save/file.hpp"
#include "save/xxh64.hpp"

namespace sample {

struct Point {
  double x = 0, y = 0, z = 0, t = 0;
};
template <class Io>
void describe(Io& io, Point& p) {
  io(p.x);
  io(p.y);
  io(p.z);
  io(p.t);
}

struct Racer {
  std::string name;
  std::array<double, 3> pos{}, vel{};
  std::array<double, 4> turn{};
  std::vector<Point> path;
  std::vector<int32_t> gates;
  std::optional<int32_t> lift;
  bool down = false;
  std::vector<std::string> notes;
  double best = 0;  // from chunk version 2
};
template <class Io>
void describe(Io& io, Racer& r) {
  io(r.name);
  io(r.pos);
  io(r.vel);
  io(r.turn);
  io(r.path);
  io(r.gates);
  io(r.lift);
  io(r.down);
  io(r.notes);
  io.since(2, r.best, -1.0);
}

struct World {
  ge::core::Rng rng{1};
  double t = 0;
  int64_t steps = 0;
  std::vector<Racer> racers;
};
template <class Io>
void describe(Io& io, World& w) {
  io(w.rng);
  io(w.t);
  io(w.steps);
}

}  // namespace sample

GE_SAVE_PLAIN(sample::Point);

namespace {

using ge::lab::Det;
namespace sv = ge::save;

constexpr uint32_t kWorld = sv::tag("WRLD");
constexpr uint32_t kRacer = sv::tag("RACR");
constexpr uint16_t kRacerVersion = 2;

std::string fmt(double v, int digits) {
  char b[32];
  std::snprintf(b, sizeof b, "%.*f", digits, v);
  return b;
}
std::string hex(uint64_t v) {
  char b[24];
  std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
  return b;
}

sample::World make_world(int racers, int points, uint32_t seed) {
  auto rng = ge::core::make_rng(seed);
  sample::World w;
  w.rng = ge::core::make_rng(seed * 7 + 1);
  w.steps = int64_t{120} * 600;
  w.t = static_cast<double>(w.steps) / 120.0;
  for (int i = 0; i < racers; i++) {
    sample::Racer r;
    r.name = "racer " + std::to_string(i);
    for (double& v : r.pos) v = ge::core::range(rng, -1000, 1000);
    for (double& v : r.vel) v = ge::core::range(rng, -30, 30);
    r.turn = {1, 0, 0, 0};
    r.path.resize(static_cast<size_t>(points));
    for (int k = 0; k < points; k++)
      r.path[k] = {ge::core::next(rng), ge::core::next(rng), ge::core::next(rng), k / 120.0};
    for (int g = 0; g < 40; g++) r.gates.push_back(ge::core::int_in(rng, 0, 1 << 20));
    if (i % 3 == 0) r.lift = i;
    r.down = i % 2 == 1;
    r.notes = {"start", i % 5 ? "clean" : "a fall at the third gate"};
    r.best = ge::core::range(rng, 50, 90);
    w.racers.push_back(std::move(r));
  }
  return w;
}

sv::Meta make_meta(const sample::World& w) {
  sv::Meta m;
  m.game = "sample";
  m.game_version = 1;
  m.title = "A sample run";
  m.place = "the third gate";
  m.play_s = w.t;
  m.saved_unix = 1792750000;
  m.setup = "game sample\nseed 38\n";
  return m;
}

void write_file(std::vector<uint8_t>& out, const sample::World& w, uint16_t racer_version) {
  sv::FileWriter f(out, make_meta(w));
  f.put(kWorld, 0, 1, w);
  for (size_t i = 0; i < w.racers.size(); i++)
    f.put(kRacer, static_cast<uint32_t>(i), racer_version, w.racers[i]);
  f.finish();
}

// The file read back into a world; false with `problem`.
bool read_file(const std::vector<uint8_t>& bytes, sample::World& w, std::string& problem) {
  sv::FileReader r;
  if (!r.open(bytes, "sample.save", "sample")) {
    problem = r.problem();
    return false;
  }
  if (!r.get(kWorld, 0, 1, w)) {
    problem = r.problem();
    return false;
  }
  const auto racers = r.all(kRacer);
  w.racers.resize(racers.size());
  for (size_t i = 0; i < racers.size(); i++)
    if (!r.get(kRacer, static_cast<uint32_t>(i), kRacerVersion, w.racers[i])) {
      problem = r.problem();
      return false;
    }
  return true;
}

template <class F>
double best_ms(int reps, F&& f) {
  double best = 1e300;
  for (int k = 0; k < reps; k++) {
    const auto t0 = std::chrono::steady_clock::now();
    f();
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    best = std::min(best, ms);
  }
  return best;
}

void run(ge::lab::Lab& lab) {
  const int racers = static_cast<int>(lab.arg_num("racers"));
  const int points = static_cast<int>(lab.arg_num("points"));
  const int reps = std::max(1, static_cast<int>(lab.arg_num("reps")));
  const auto seed = static_cast<uint32_t>(lab.arg_num("seed"));

  // --- the round trip, at size ---
  const sample::World world = make_world(racers, points, seed);
  std::vector<uint8_t> bytes;
  write_file(bytes, world, kRacerVersion);
  sample::World back;
  std::string problem;
  const bool read = read_file(bytes, back, problem);
  std::vector<uint8_t> again;
  if (read) write_file(again, back, kRacerVersion);
  const bool identical = read && again == bytes;
  lab.metric("file.bytes", static_cast<double>(bytes.size()), "B");
  lab.metric("file.chunks", static_cast<double>(world.racers.size() + 2), "count");
  lab.metric("file.round-trip-identical", identical ? 1 : 0, "bool");
  if (!identical)
    lab.fail("the file read back does not write the same bytes" +
                 (read ? std::string() : ": " + problem),
             "Run tests/save_io_test.cpp and tests/save_file_test.cpp. Skill: save.");

  ge::lab::LabTable sizes{
      "the file at " + std::to_string(racers) + " racers of " + std::to_string(points) + " points",
      {"part", "chunks", "bytes", "share"},
      {}};
  {
    sv::FileReader r;
    r.open(bytes);
    uint64_t racer_bytes = 0;
    for (const sv::ChunkInfo* c : r.all(kRacer)) racer_bytes += c->stored;
    const double total = static_cast<double>(bytes.size());
    const auto share = [&](uint64_t n) {
      return fmt(100 * static_cast<double>(n) / total, 2) + "%";
    };
    const uint64_t meta = r.find(sv::kMetaTag)->stored, wrld = r.find(kWorld)->stored;
    const uint64_t frame = bytes.size() - meta - wrld - racer_bytes;
    sizes.rows.push_back({"META", "1", std::to_string(meta), share(meta)});
    sizes.rows.push_back({"WRLD", "1", std::to_string(wrld), share(wrld)});
    sizes.rows.push_back(
        {"RACR", std::to_string(racers), std::to_string(racer_bytes), share(racer_bytes)});
    sizes.rows.push_back({"header, padding, directory", "", std::to_string(frame), share(frame)});
    sizes.rows.push_back({"the file's xxh64", "", hex(sv::xxh64(bytes.data(), bytes.size())), ""});
  }
  lab.table(std::move(sizes));

  // --- the speeds (measured) ---
  const double mb = static_cast<double>(bytes.size()) / 1e6;
  std::vector<uint8_t> scratch;
  scratch.reserve(bytes.size());
  const double write_ms = best_ms(reps, [&] { write_file(scratch, world, kRacerVersion); });
  const double open_ms = best_ms(reps, [&] {
    sv::FileReader r;
    r.open(bytes);
  });
  sample::World into;
  const double read_ms = best_ms(reps, [&] { read_file(bytes, into, problem); });
  sv::Header h;
  sv::Meta m;
  const double peek_ms = best_ms(reps * 10, [&] {
    sv::peek(std::span(bytes).first(std::min(bytes.size(), sv::kPeekBytes)), h, m, problem);
  });
  lab.metric("speed.write-ms", write_ms, "ms", Det::measured);
  lab.metric("speed.write-mb-s", mb / (write_ms / 1e3), "MB/s", Det::measured);
  lab.metric("speed.open-ms", open_ms, "ms", Det::measured);
  lab.metric("speed.open-mb-s", mb / (open_ms / 1e3), "MB/s", Det::measured);
  lab.metric("speed.read-ms", read_ms, "ms", Det::measured);
  lab.metric("speed.read-mb-s", mb / (read_ms / 1e3), "MB/s", Det::measured);
  lab.metric("speed.peek-us", peek_ms * 1e3, "us", Det::measured);
  lab.note("Speeds are the best of " + std::to_string(reps) +
           " runs; time them on PRESET=release (a debug build times the sanitizers). `read` is "
           "open plus every chunk read into the state.");

  // --- the refusals, on a small file: every cut and every flipped byte ---
  const sample::World small_world = make_world(2, 6, seed);
  std::vector<uint8_t> small;
  write_file(small, small_world, kRacerVersion);
  int cuts_refused = 0, flips_refused = 0;
  for (size_t cut = 0; cut < small.size(); cut++) {
    sv::FileReader r;
    cuts_refused += !r.open(std::span(small).first(cut));
  }
  for (size_t at = 0; at < small.size(); at++) {
    std::vector<uint8_t> bad = small;
    bad[at] ^= static_cast<uint8_t>(1u << (at % 8));
    sv::FileReader r;
    flips_refused += !r.open(bad);
  }
  lab.metric("refuse.file-bytes", static_cast<double>(small.size()), "B");
  lab.metric("refuse.cuts-accepted", static_cast<double>(small.size() - cuts_refused), "count");
  lab.metric("refuse.flips-accepted", static_cast<double>(small.size() - flips_refused), "count");
  if (cuts_refused != int(small.size()) || flips_refused != int(small.size()))
    lab.fail("a damaged file opened: " + std::to_string(small.size() - cuts_refused) +
                 " cuts and " + std::to_string(small.size() - flips_refused) +
                 " flipped bytes were accepted",
             "Every byte must be under a checksum or checked as zero: "
             "tests/save_file_test.cpp. Skill: save.");

  // --- versions, and what each refusal says ---
  ge::lab::LabTable says{"what a refusal says", {"case", "opens", "reads", "message"}, {}};
  auto row = [&](const std::string& what, bool opens, bool reads, const std::string& msg) {
    says.rows.push_back({what, opens ? "yes" : "no", reads ? "yes" : "no", msg});
  };
  {
    // An older chunk: version 1 had no best, so it reads -1.
    std::vector<uint8_t> v1;
    sv::FileWriter f(v1, make_meta(small_world));
    f.put(kWorld, 0, 1, small_world);
    sample::Racer r = small_world.racers[0];
    sv::Writer& w = f.chunk(kRacer, 0, 1);
    w(r.name), w(r.pos), w(r.vel), w(r.turn), w(r.path), w(r.gates), w(r.lift), w(r.down);
    w(r.notes);
    f.finish();
    sample::World got;
    const bool ok = read_file(v1, got, problem);
    const bool defaulted = ok && got.racers.size() == 1 && got.racers[0].best == -1.0;
    row("racer chunk version 1 (no best)", ok, defaulted, defaulted ? "best reads -1" : problem);
    if (!defaulted) lab.fail("an older chunk did not read with its default", "Skill: save.");
  }
  {
    std::vector<uint8_t> v3;
    write_file(v3, small_world, 3);
    sample::World got;
    const bool ok = read_file(v3, got, problem);
    row("racer chunk version 3 (newer)", true, ok, ok ? "" : problem);
    if (ok || problem.find("version 3; this build reads up to version 2") == std::string::npos)
      lab.fail("a newer chunk was not refused naming both versions", "Skill: save.");
  }
  {
    std::vector<uint8_t> cut(small.begin(), small.end() - 9);
    sv::FileReader r;
    row("cut 9 bytes short", r.open(cut, "sample.save"), false, r.problem());
    std::vector<uint8_t> flip = small;
    flip[small.size() / 2] ^= 4;
    row("one byte flipped mid-file", r.open(flip, "sample.save"), false, r.problem());
    std::vector<uint8_t> newer = small;
    newer[8] = 9;  // the format word, the header checksum fixed up: a newer build's file
    const uint64_t sum = sv::xxh64(newer.data(), 56);
    std::memcpy(newer.data() + 56, &sum, 8);
    row("format 9", r.open(newer, "sample.save"), false, r.problem());
    row("another game's save", r.open(small, "sample.save", "other"), false, r.problem());
  }
  lab.table(std::move(says));
}

const ge::lab::LabSpec kSpec{
    "save-file",
    ge::lab::LabKind::sim,
    "engine",
    "engine/save/file.hpp, engine/save/io.hpp",
    "save",
    "Does a save file round-trip to the byte, refuse every damage, read old chunk versions, and "
    "how fast is it written, opened and read?",
    {
        {"racers", "64", "racers in the sample state (each one chunk)"},
        {"points", "2000", "points in each racer's path (32 bytes each)"},
        {"reps", "5", "timing repetitions; the best is kept"},
        {"seed", "38", "the state's stream"},
    },
    "--racers=8 --points=200 --reps=1",
    run,
};
GE_LAB(kSpec);

}  // namespace
