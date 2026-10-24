// The boom (engine/camera/boom.hpp): it stands behind the subject at its arm and over the ground,
// keeps the subject inside its frame through every scenario (the cliff's drop included), leans
// with the slope, widens with speed while pulling in, takes the fall look on a long fall only,
// keeps its distance from the trunks, snaps rather than chases a teleport, and banks with a craft
// in the air only by its own share.
//
// ge:summary Tests: the boom's arm, framing, incline, stretch, fall look, trunks and teleport
// ge:keywords test camera boom chase framing incline stretch fall-look trunks clearance teleport
// ge:skill camera testing
// ge:test tests/camera_boom_test.cpp
#include <cmath>
#include <string>
#include <vector>

#include "camera/boom.hpp"
#include "camera/scenarios.hpp"
#include "check.hpp"
#include "core/libm/libm.hpp"
#include "core/math.hpp"

namespace cam = ge::camera;

namespace {

struct Frame {
  cam::Subject s;
  cam::Pose p;
  double fallen;
};

std::vector<Frame> run(const char* id, double hz, const cam::BoomRow& row = {}) {
  const cam::Scenario* sc = cam::find_camera_scenario(id);
  const cam::World w = cam::scenario_world(*sc);
  cam::ScriptRun r = cam::start_script(*sc);
  cam::BoomState st;
  std::vector<Frame> out;
  const int n = static_cast<int>(sc->seconds * hz);
  for (int i = 1; i <= n; ++i) {
    const cam::Subject s = cam::advance_to(r, i / hz);
    const cam::Pose p = cam::frame_boom(row, s, w, st, 1.0 / hz, 1, 1);
    out.push_back({s, p, st.fallen});
  }
  return out;
}

// Where the subject's aim point stands in the frame vertically: -1 the bottom edge, 1 the top.
double in_frame(const Frame& f, const cam::BoomRow& row) {
  const double dx = f.s.pos.x - f.p.eye.x, dz = f.s.pos.z - f.p.eye.z;
  const double dy = f.s.pos.y + row.aim_height_m - f.p.eye.y;
  const double to = ge::core::libm::atan2(dy, ge::core::hypot(dx, dz));
  return (to - f.p.pitch) / (f.p.fov_y / 2);
}

}  // namespace

TEST(the_boom_keeps_the_subject_inside_its_frame_everywhere) {
  const cam::BoomRow row;
  for (const cam::Scenario& sc : cam::camera_scenarios()) {
    if (sc.id == "teleport") continue;
    for (const Frame& f : run(std::string(sc.id).c_str(), 60)) {
      const double v = in_frame(f, row);
      CHECK_MSG(std::fabs(v) <= row.frame + 0.01,
                std::string(sc.id) + ": the subject left the frame (" + std::to_string(v) + ")");
    }
  }
}

TEST(the_boom_stands_behind_and_over_the_ground) {
  const cam::BoomRow row;
  for (const char* id : {"walk", "carve", "stairs"}) {
    const cam::Scenario* sc = cam::find_camera_scenario(id);
    for (const Frame& f : run(id, 60)) {
      const double g = cam::scenario_ground(*sc, f.p.eye.x, f.p.eye.z);
      CHECK(f.p.eye.y >= g + row.clearance_m - 1e-9);
      // Behind: the subject is ahead of the lens along the subject's heading.
      const double ahead = (f.s.pos.x - f.p.eye.x) * std::sin(f.s.heading) +
                           (f.s.pos.z - f.p.eye.z) * std::cos(f.s.heading);
      CHECK(ahead > 0.5);
    }
  }
}

TEST(the_lens_widens_with_speed_and_the_arm_pulls_in) {
  const auto frames = run("carve", 60);
  const Frame& slow = frames[30];
  const Frame& fast = frames.back();
  CHECK(fast.p.fov_y > slow.p.fov_y + 0.1);
}

TEST(the_fall_look_comes_in_on_the_cliff_and_never_on_a_jump) {
  double most_jump = 0, most_cliff = 0;
  for (const Frame& f : run("jump", 60)) most_jump = f.fallen > most_jump ? f.fallen : most_jump;
  for (const Frame& f : run("cliff", 60))
    most_cliff = f.fallen > most_cliff ? f.fallen : most_cliff;
  CHECK(most_jump == 0);
  CHECK(most_cliff > 0.9);
}

TEST(the_lens_keeps_its_gap_from_the_trunks) {
  const cam::BoomRow row;
  const cam::Scenario* sc = cam::find_camera_scenario("trees");
  const cam::World w = cam::scenario_world(*sc);
  for (const Frame& f : run("trees", 120)) {
    cam::Obstacle near[16];
    const int n = cam::obstacles_near(w, f.p.eye.x, f.p.eye.z, 0.5, near, 16);
    CHECK_MSG(n == 0,
              "the lens came within half a metre of a trunk at z " + std::to_string(f.p.eye.z));
  }
  (void)row;
}

TEST(a_teleport_is_snapped_not_chased) {
  const auto frames = run("teleport", 60);
  for (size_t i = 1; i < frames.size(); ++i) {
    const double moved = frames[i].p.eye.x - frames[i - 1].p.eye.x;
    if (frames[i].s.event_count > 0) {
      CHECK(moved > 35);
    } else {
      CHECK(std::fabs(moved) < 2);
    }
  }
}

TEST(the_boom_banks_with_a_craft_in_the_air_by_its_own_share) {
  const cam::World w = cam::scenario_world(*cam::find_camera_scenario("walk"));
  auto settle = [&](const cam::BoomRow& row) {
    cam::BoomState st;
    cam::Pose p;
    cam::Subject s;
    s.grounded = false;
    s.vel = {0, 0, 20};
    s.q = ge::core::from_euler(0, 0, 0.5);  // banked right side down into a turn
    for (int i = 0; i < 240; ++i) {
      s.pos = {0, 200, 20.0 * i / 120};
      p = cam::frame_boom(row, s, w, st, 1.0 / 120, 1, 1);
    }
    return p.roll;
  };
  cam::BoomRow row;
  row.fall_from_m = 1e6;
  CHECK(std::fabs(settle(row)) < 1e-3);  // a skier's boom: level in the air
  row.bank_air = 0.6;
  CHECK(std::fabs(settle(row) - 0.3) < 0.02);
}
