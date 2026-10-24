// engine/prof/timeline.hpp: every thread's slices land on its own lane, nested by scope; a
// recording off records nothing; past its room a lane drops and counts; a restart clears; the
// pool's bands and waits are heard without a line of code.
//
// ge:summary Test: the timeline's lanes, nesting, room, restart and the pool's slices
// ge:keywords test prof timeline lanes slices nesting depth dropped restart pool ge_work
// ge:skill testing perf
#include "prof/timeline.hpp"

#include <chrono>
#include <string>
#include <thread>

#include "check.hpp"
#include "core/parallel.hpp"
#include "core/thread_name.hpp"

namespace {

namespace pf = ge::prof;

const pf::Lane* lane_named(const std::vector<pf::Lane>& lanes, const std::string& name) {
  for (const pf::Lane& l : lanes) {
    if (l.name == name) return &l;
  }
  return nullptr;
}

std::string name_of(const pf::Slice& s) { return std::string(pf::zone_name(s.name)); }

// The runner's order is not the file's: each test names the calling thread itself.
void name_main() { ge::core::name_this_thread("main", false); }

}  // namespace

TEST(timeline_nests_slices_by_scope) {
  name_main();
  pf::timeline_start(64);
  {
    GE_WORK("outer");
    {
      GE_WAIT("inner wait");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    { GE_SPIN("inner spin"); }
  }
  pf::timeline_stop();
  const auto lanes = pf::timeline_lanes();
  const pf::Lane* main = lane_named(lanes, "main");
  CHECK(main != nullptr);
  if (!main) return;
  CHECK(main->os_id == ge::core::this_thread_os_id());
  CHECK(main->slices.size() == 3);
  // In the order they closed: the inner two, then the outer.
  CHECK(name_of(main->slices[0]) == "inner wait");
  CHECK(main->slices[0].act == pf::Act::sleep);
  CHECK(main->slices[0].depth == 1);
  CHECK(main->slices[0].dur_ns >= 1000000);
  CHECK(name_of(main->slices[1]) == "inner spin");
  CHECK(main->slices[1].act == pf::Act::spin);
  CHECK(name_of(main->slices[2]) == "outer");
  CHECK(main->slices[2].depth == 0);
  CHECK(main->slices[2].start_ns <= main->slices[0].start_ns);
  CHECK(main->slices[2].dur_ns >= main->slices[0].dur_ns);
}

TEST(timeline_off_records_nothing_and_a_restart_clears) {
  name_main();
  pf::timeline_start(64);
  { GE_WORK("first"); }
  pf::timeline_stop();
  { GE_WORK("while off"); }
  const auto before = pf::timeline_lanes();
  const pf::Lane* first = lane_named(before, "main");
  CHECK(first && first->slices.size() == 1);
  pf::timeline_start(64);
  { GE_WORK("second"); }
  pf::timeline_stop();
  const auto after = pf::timeline_lanes();
  const pf::Lane* main = lane_named(after, "main");
  CHECK(main && main->slices.size() == 1 && name_of(main->slices[0]) == "second");
}

TEST(timeline_drops_past_its_room_and_counts) {
  name_main();
  pf::timeline_start(4);
  for (int i = 0; i < 10; i++) {
    GE_WORK("many");
  }
  pf::timeline_stop();
  const auto lanes = pf::timeline_lanes();
  const pf::Lane* main = lane_named(lanes, "main");
  CHECK(main && main->slices.size() == 4 && main->dropped == 6);
}

TEST(timeline_keeps_a_lane_a_thread) {
  name_main();
  pf::timeline_start(64);
  std::thread t([] {
    ge::core::name_this_thread("worker under test");
    GE_WORK("its work");
  });
  t.join();
  { GE_WORK("mine"); }
  pf::timeline_stop();
  const auto lanes = pf::timeline_lanes();
  const pf::Lane* w = lane_named(lanes, "worker under test");
  CHECK(w && w->slices.size() == 1 && name_of(w->slices[0]) == "its work");
  CHECK(w && w->os_id != ge::core::this_thread_os_id());
}

TEST(timeline_hears_the_pool) {
  if (ge::core::default_workers() < 2) return;  // no pool on one hardware thread
  name_main();
  pf::timeline_start(4096);
  for (int k = 0; k < 4; k++) {
    GE_WORK("bake");
    ge::core::parallel_bands(
        8, 8, [](size_t, size_t) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); });
  }
  pf::timeline_stop();
  const auto lanes = pf::timeline_lanes();
  int bands = 0;
  bool worker_banded = false;
  for (const pf::Lane& l : lanes) {
    for (const pf::Slice& s : l.slices) {
      if (name_of(s) != "band") continue;
      bands++;
      if (l.name.rfind("pool ", 0) == 0) worker_banded = true;
      // The caller's bands sit inside its `bake`.
      if (l.name == "main") CHECK(s.depth == 1);
    }
  }
  CHECK(bands == 4 * 8);  // a band a slice of the count, whoever ran it
  CHECK(worker_banded);
}
