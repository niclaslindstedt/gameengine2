// engine/lab/load_chart.hpp: each load chart draws at its size from planted lanes, the lanes'
// SPANS file reads back to the same lanes, a file that is not one is refused, and report_load
// takes its four shots and attaches both files under the prefix.
//
// ge:summary Test: the load charts draw, lanes_spans round-trips, report_load's shots and files
// ge:keywords test lab load chart lanes busy working frames lanes_spans lanes_from_spans
// ge:keywords report_load
// ge:skill testing perf labs
#include "lab/load_chart.hpp"

#include <string>
#include <vector>

#include "check.hpp"
#include "prof/zones.hpp"

using namespace ge::lab;
namespace pf = ge::prof;

namespace {

constexpr int64_t kMs = 1'000'000;

pf::Slice slice(const char* name, pf::Act act, uint8_t depth, int64_t from_ms, int64_t to_ms) {
  return {pf::zone_id_of(name), act, depth, from_ms * kMs, (to_ms - from_ms) * kMs};
}

std::vector<pf::Lane> planted() {
  pf::Lane main{"main", 42, false, {}, 0};
  main.slices = {slice("sim", pf::Act::work, 1, 0, 20), slice("frame", pf::Act::work, 0, 0, 60),
                 slice("wait for bands", pf::Act::sleep, 0, 60, 80)};
  pf::Lane pool{"pool 1", 43, false, {}, 2};
  pool.slices = {slice("band", pf::Act::work, 0, 60, 79),
                 slice("look for work", pf::Act::spin, 0, 79, 90)};
  pf::Lane gpu{"gpu (software)", 0, true, {}, 0};
  gpu.slices = {slice("pass", pf::Act::work, 0, 20, 50)};
  return {main, pool, gpu};
}

pf::Load analyzed(const std::vector<pf::Lane>& lanes) {
  pf::LoadOptions o;
  o.to_ns = 100 * kMs;
  o.hardware_threads = 4;
  o.bin_ns = 10 * kMs;
  return pf::analyze_load(lanes, {}, {{0, 50 * kMs}, {50 * kMs, 100 * kMs}}, o);
}

void nothing(Lab&) {}
const LabSpec kSpec{"load-chart", LabKind::bench, "engine", "tests", "labs", "?", {}, "", nothing};

}  // namespace

TEST(load_charts_draw_at_their_size) {
  const std::vector<pf::Lane> lanes = planted();
  const pf::Load L = analyzed(lanes);
  const ChartFrame f{"T", "s", "t (ms)", "", 800, 300};
  const Chart a = lanes_chart(f, lanes, L, 0, 100 * kMs, {{0, 50 * kMs}});
  const Chart b = busy_chart(f, L);
  const Chart c = working_chart(f, L);
  const Chart d = frames_chart(f, L);
  for (const Chart* ch : {&a, &b, &c, &d}) {
    CHECK(ch->plot.width == 800);
    CHECK(ch->plot.height > 0);
    CHECK(ch->plot.ops.size() > 10);
  }
  // A load with nothing in it still draws (a lab's shots are the same on every machine).
  const pf::Load empty = pf::analyze_load({}, {}, {}, {});
  CHECK(frames_chart(f, empty).plot.width == 800);
  CHECK(lanes_chart(f, {}, empty, 0, 1, {}).plot.width == 800);
}

TEST(load_series_has_a_column_a_lane) {
  const pf::Load L = analyzed(planted());
  const Series s = load_series(L);
  CHECK(s.rows() == L.bins);
  CHECK(s.find("t_ms") != nullptr);
  CHECK(s.columns.size() >= 1 + L.lanes.size());
}

TEST(lanes_spans_read_back_to_the_same_lanes) {
  const std::vector<pf::Lane> lanes = planted();
  Series parsed;
  std::string error;
  CHECK(read_series(write_series(lanes_spans(lanes)), parsed, error));
  CHECK(parsed.is_spans());
  std::vector<pf::Lane> back;
  CHECK(lanes_from_spans(parsed, back, error));
  CHECK(back.size() == lanes.size());
  if (back.size() != lanes.size()) return;
  for (size_t i = 0; i < lanes.size(); i++) {
    CHECK(back[i].name == lanes[i].name);
    CHECK(back[i].gpu == lanes[i].gpu);
    CHECK(back[i].os_id == lanes[i].os_id);
    CHECK(back[i].slices.size() == lanes[i].slices.size());
    for (size_t k = 0; k < lanes[i].slices.size() && k < back[i].slices.size(); k++) {
      const pf::Slice& x = lanes[i].slices[k];
      const pf::Slice& y = back[i].slices[k];
      CHECK(pf::zone_name(x.name) == pf::zone_name(y.name));
      CHECK(x.act == y.act && x.depth == y.depth);
      CHECK(x.start_ns == y.start_ns && x.dur_ns == y.dur_ns);
    }
  }
  // A plain series is not a lanes file.
  Series plain = make_series({"t_s", "v"});
  add_row(plain, {0, 1});
  CHECK(!lanes_from_spans(plain, back, error));
  CHECK(!error.empty());
}

TEST(report_load_takes_four_shots_and_attaches_both_files) {
  const std::vector<pf::Lane> lanes = planted();
  const pf::Load L = analyzed(lanes);
  Lab lab(kSpec, {});
  LoadReport r;
  r.prefix = "run-";
  r.subject = "planted lanes";
  report_load(lab, r, lanes, L, {{0, 50 * kMs}, {50 * kMs, 100 * kMs}});
  CHECK(lab.failures().empty());
  CHECK(lab.shots().size() == 4);
  std::vector<std::string> ids;
  for (const Lab::Taken& t : lab.shots()) ids.push_back(t.shot.id);
  CHECK(ids.size() == 4 && ids[0] == "run-lanes" && ids[3] == "run-frames");
  bool load_tsv = false, lanes_tsv = false;
  for (const Lab::File& f : lab.files()) {
    load_tsv = load_tsv || f.name == "run-load.tsv";
    lanes_tsv = lanes_tsv || f.name == "run-lanes.tsv";
    CHECK(f.det == Det::measured);  // timing: never in a digest
  }
  CHECK(load_tsv && lanes_tsv);
  CHECK(!lab.metrics().empty());
  CHECK(!lab.notes().empty());
  // Without the frames chart: three shots.
  Lab open(kSpec, {});
  r.per_frame = false;
  report_load(open, r, lanes, L, {});
  CHECK(open.shots().size() == 3);
}
