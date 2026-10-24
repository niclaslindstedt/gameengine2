// Fall Line's camera (games/fall_line/camera): the ladder's six rungs in order, the camera key's
// walk, the hand-overs (inertialized between booms, blended to and from a worn lens); the skier
// as a subject (his frame rolled by his lean, a landing and a reset read between frames, the
// helmet's eye folded down by the tuck); the mountain's answers (the trunks round a point, a
// post across the arm); and over the first half minute of the bot's run on seed 38, every rung
// kept out of every trunk and over the snow; a worn lens on a skier thrown into a trunk kept out of
// its bark; the point a rung frames on him; the machines' ladders, what he is riding, and the
// hand-over as he boards the snowmobile (a move round him, never a jump, landing on the machine's
// rung).
//
// ge:summary Tests for Fall Line's camera ladder, subject, world and the lenses over a run
// ge:keywords test fall-line camera ladder rung camera-key hand-over subject landing teleport
// ge:keywords trunks posts clearance machine sled heli para ride hand-over
// ge:skill camera testing
#include <cmath>
#include <memory>

#include "check.hpp"
#include "fall_line/camera/machines.hpp"
#include "fall_line/camera/rungs.hpp"
#include "fall_line/camera/subject.hpp"
#include "fall_line/camera/world.hpp"
#include "fall_line/sim/bot.hpp"
#include "fall_line/sim/free.hpp"
#include "fall_line/sim/para_ride.hpp"
#include "fall_line/sim/simulate.hpp"
#include "fall_line/sim/sled_outing.hpp"
#include "mountain/mapgen/generate.hpp"
#include "skier/trees.hpp"

namespace {

namespace fc = fall_line::camera;
namespace cam = ge::camera;

std::shared_ptr<const ge::mountain::mapgen::Level> map38() {
  static const auto level = [] {
    ge::mountain::mapgen::GenerateOptions opts;
    opts.version = 1;  // the app's map (fall_line/app/views.hpp kMapVersion)
    return std::make_shared<const ge::mountain::mapgen::Level>(
        ge::mountain::mapgen::generate_level(38, opts));
  }();
  return level;
}

// The free ride's map, its machines in the ski area.
std::shared_ptr<const ge::mountain::mapgen::Level> free38() {
  static const auto level =
      std::make_shared<const ge::mountain::mapgen::Level>(fall_line::sim::free_map(38, 10));
  return level;
}

// The furthest a point is inside any trunk near it (positive: inside), m.
double inside_trunk(const ge::skier::World& w, const cam::Vec3& p) {
  double worst = -1e9;
  ge::skier::for_each_tree_near(*w.trees, p.x, p.z, [&](int32_t i) {
    const auto& t = w.level->trees[static_cast<size_t>(i)];
    if (p.y < t.y || p.y > t.y + t.height) return;
    const double d = t.radius - std::sqrt((p.x - t.x) * (p.x - t.x) + (p.z - t.z) * (p.z - t.z));
    worst = d > worst ? d : worst;
  });
  return worst;
}

}  // namespace

TEST(the_ladder_has_six_rungs_in_order) {
  const auto ladder = fc::fall_line_ladder();
  CHECK(ladder.size() == static_cast<size_t>(fc::kRungs));
  for (int i = 0; i < fc::kRungs; ++i) {
    const auto r = static_cast<fc::Rung>(i);
    CHECK(ladder[static_cast<size_t>(i)].name == fc::rung_name(r));
    fc::Rung back{};
    CHECK(fc::rung_named(fc::rung_name(r), back) && back == r);
  }
  CHECK(ladder[0].kind == cam::RigKind::mounted && ladder[1].kind == cam::RigKind::mounted);
  CHECK(ladder[2].kind == cam::RigKind::boom && ladder[4].kind == cam::RigKind::boom);
  CHECK(ladder[5].kind == cam::RigKind::orbit);
  fc::Rung none{};
  CHECK(!fc::rung_named("eyes", none));
}

TEST(the_camera_key_walks_the_run_rungs_and_wraps) {
  fc::Rung r = fc::Rung::tips;
  const fc::Rung walk[] = {fc::Rung::helmet, fc::Rung::chase, fc::Rung::far, fc::Rung::high,
                           fc::Rung::tips};
  for (const fc::Rung want : walk) {
    r = fc::next_rung(r);
    CHECK(r == want);
  }
  CHECK(fc::next_rung(fc::Rung::orbit) == fc::Rung::chase);
}

TEST(booms_hand_over_inertialized_and_worn_lenses_blended) {
  CHECK(fc::hand_over(fc::Rung::chase, fc::Rung::far).edit == cam::Edit::inertialize);
  CHECK(fc::hand_over(fc::Rung::high, fc::Rung::chase).edit == cam::Edit::inertialize);
  CHECK(fc::hand_over(fc::Rung::chase, fc::Rung::helmet).edit == cam::Edit::blend);
  CHECK(fc::hand_over(fc::Rung::tips, fc::Rung::chase).edit == cam::Edit::blend);
  CHECK(fc::hand_over(fc::Rung::chase, fc::Rung::orbit).seconds >
        fc::hand_over(fc::Rung::chase, fc::Rung::far).seconds);
  cam::Camera c = fc::make_run_camera(fc::Rung::chase, 1);
  CHECK(fc::rung_of(c) == fc::Rung::chase);
  fc::change_rung(c, fc::Rung::far);
  CHECK(fc::rung_of(c) == fc::Rung::far);
}

TEST(the_subject_reads_a_landing_and_a_reset_between_frames) {
  fall_line::sim::Driver run(map38(), 0);
  const ge::skier::World world = run.state().world();
  ge::skier::SkierState s = run.state().racers[0].skier;
  fc::SubjectMemory m;
  cam::Subject sub = fc::skier_subject(s, world, m);
  CHECK(sub.event_count == 0 && sub.grounded);
  CHECK(std::fabs(sub.heading - s.heading) < 1e-12);
  // In the air, sinking at 7 m/s; then on the snow: a landing at that speed.
  s.on_snow = false;
  s.vy = -7;
  sub = fc::skier_subject(s, world, m);
  CHECK(sub.event_count == 0 && !sub.grounded);
  s.on_snow = true;
  sub = fc::skier_subject(s, world, m);
  CHECK(sub.event_count == 1 && sub.events[0].kind == cam::Moment::landing);
  CHECK(std::fabs(sub.events[0].size - 7) < 1e-12);
  // Put 40 m away between two frames: a teleport.
  s.x += 40;
  sub = fc::skier_subject(s, world, m);
  CHECK(sub.event_count == 1 && sub.events[0].kind == cam::Moment::teleport);
}

TEST(the_tuck_folds_the_helmets_eye_down) {
  fall_line::sim::Driver run(map38(), 0);
  const ge::skier::World world = run.state().world();
  ge::skier::SkierState s = run.state().racers[0].skier;
  cam::Camera c = fc::make_run_camera(fc::Rung::helmet, 1);
  s.tuck = 0;
  fc::fit_worn(c, s, world);
  const double tall = c.rigs[static_cast<int>(fc::Rung::helmet)].mounted.eye_body.y;
  s.tuck = 1;
  fc::fit_worn(c, s, world);
  const double folded = c.rigs[static_cast<int>(fc::Rung::helmet)].mounted.eye_body.y;
  CHECK(tall > 1.4 && tall < 1.9);
  CHECK(folded < tall - 0.25);
}

TEST(the_world_finds_the_trunks_and_a_post_across_the_arm) {
  const auto level = map38();
  fall_line::sim::Driver run(level, 0);
  const ge::skier::World sw = run.state().world();
  const cam::World w = fc::camera_world(sw);
  const auto& t = level->trees[level->trees.size() / 2];
  CHECK(std::fabs(cam::ground_at(w, t.x, t.z) - level->ground_at(t.x, t.z)) < 1e-12);
  cam::Obstacle near[16];
  const int n = cam::obstacles_near(w, t.x + 1, t.z, 3, near, 16);
  bool found = false;
  for (int i = 0; i < n; ++i) found = found || (near[i].x == t.x && near[i].z == t.z);
  CHECK(found);
  // A line across a gate's left post, 2 m either side of it, is cut half way.
  const auto& g = level->checkpoints[3];
  const double px = g.x - std::cos(g.heading) * g.width / 2;
  const double pz = g.z + std::sin(g.heading) * g.width / 2;
  const cam::Vec3 a{px - 2 * std::sin(g.heading), g.y + 1, pz - 2 * std::cos(g.heading)};
  const cam::Vec3 b{px + 2 * std::sin(g.heading), g.y + 1, pz + 2 * std::cos(g.heading)};
  const double share = cam::clear_share(w, a, b);
  CHECK(share > 0.35 && share < 0.5);
  // Over the post's top it is open.
  const cam::Vec3 a2{a.x, g.y + 4, a.z}, b2{b.x, g.y + 4, b.z};
  CHECK(cam::clear_share(w, a2, b2) == 1.0);
}

TEST(every_rung_stays_out_of_the_trunks_and_over_the_snow) {
  const auto level = map38();
  fall_line::sim::Driver run(level, 0);
  const ge::skier::World sw = run.state().world();
  const cam::World w = fc::camera_world(sw);
  fc::RunCamera cams[fc::kRungs];
  for (int i = 0; i < fc::kRungs; ++i) cams[i] = fc::make_skier_camera(static_cast<fc::Rung>(i), 7);
  double worst_trunk = -1e9, least_over = 1e9;
  while (run.state().t < 30 && !run.over()) {
    for (int k = 0; k < 2; ++k) run.step(fall_line::sim::bot_controls(run.state(), 0, run.plan()));
    const auto& skier = run.state().racers[0].skier;
    for (int i = 0; i < fc::kRungs; ++i) {
      const cam::Pose p = fc::frame_camera(cams[i], skier, sw, w, 1.0 / 60);
      const double in = inside_trunk(sw, p.eye);
      worst_trunk = in > worst_trunk ? in : worst_trunk;
      const double over = p.eye.y - level->ground_at(p.eye.x, p.eye.z);
      least_over = over < least_over ? over : least_over;
    }
  }
  CHECK(worst_trunk < 0);
  CHECK(least_over > 0.1);
}

TEST(a_worn_lens_on_a_skier_in_a_trunk_stays_out_of_its_bark) {
  const auto level = map38();
  fall_line::sim::Driver run(level, 0);
  const ge::skier::World sw = run.state().world();
  const cam::World w = fc::camera_world(sw);
  const auto& t = level->trees[level->trees.size() / 2];
  // Stood with his boots half way into the trunk, facing its axis: his head is in the bark.
  const ge::skier::SkierState s = ge::skier::stand_skier(sw, t.x - t.radius * 0.5, t.z, 1.5708, 0);
  for (const fc::Rung r : {fc::Rung::helmet, fc::Rung::tips}) {
    fc::RunCamera rc = fc::make_skier_camera(r, 7);
    const cam::Pose p = fc::frame_camera(rc, s, sw, w, 1.0 / 60);
    CHECK(inside_trunk(sw, p.eye) < -0.1);
  }
}

TEST(a_rung_frames_his_middle_and_lower_when_he_is_down) {
  ge::skier::SkierState s;
  s.x = 3;
  s.y = 100;
  s.z = -2;
  const cam::Vec3 up = fc::framed_point(s);
  CHECK(up.x == 3 && up.z == -2 && up.y > 100.8 && up.y < 101.2);
  s.down = true;
  CHECK(fc::framed_point(s).y < up.y - 0.5);
}

TEST(each_machine_has_four_named_rungs_that_hand_over_to_the_skis_and_back) {
  for (const fc::Ride r : {fc::Ride::sled, fc::Ride::heli, fc::Ride::para}) {
    const auto ladder = fc::machine_ladder(r);
    CHECK(ladder.size() == static_cast<size_t>(fc::kMachineRungs));
    CHECK(ladder[0].kind == cam::RigKind::mounted);
    for (int i = 0; i < fc::kMachineRungs; ++i) {
      CHECK(ladder[static_cast<size_t>(i)].name == fc::lens_name(r, i));
      int back = -1;
      CHECK(fc::lens_named(r, fc::lens_name(r, i), back) && back == i);
      CHECK(fc::machine_rung_of(fc::skis_rung_of(i)) == i);
      if (i > 0) CHECK(ladder[static_cast<size_t>(i)].kind == cam::RigKind::boom);
    }
  }
  CHECK(fc::machine_ladder(fc::Ride::skis).empty());
  CHECK(fc::machine_rung_of(fc::Rung::tips) == 0);
  CHECK(fc::machine_rung_of(fc::Rung::orbit) == 1);
}

TEST(the_ride_is_what_he_is_on_and_set_lens_names_its_rungs) {
  const fall_line::sim::GameState free = fall_line::sim::create_free_ride(free38(), 0);
  CHECK(fc::ride_of(free) == fc::Ride::skis);
  const fall_line::sim::GameState para = fall_line::sim::create_para_ride(free38());
  CHECK(fc::ride_of(para) == fc::Ride::para);
  fc::RideCamera rc = fc::make_ride_camera(fc::Rung::chase, 1);
  CHECK(fc::set_lens(rc, para, "wing") && fc::active_lens(rc) == "wing");
  CHECK(fc::set_lens(rc, para, "helmet") && fc::active_lens(rc) == "pilot");  // its counterpart
  CHECK(!fc::set_lens(rc, para, "seat"));
  CHECK(!fc::set_lens(rc, free, "wing"));
  // Framed on the summit under the wing: over the snow, behind and over him on the chase.
  CHECK(fc::set_lens(rc, para, "chase"));
  const ge::skier::World sw = para.world();
  const cam::Pose p = fc::frame_ride(rc, para, sw, fc::camera_world(sw), 1.0 / 60);
  const auto& s = para.racers[0].skier;
  CHECK(p.eye.y > s.y + 0.5);
  CHECK(std::hypot(p.eye.x - s.x, p.eye.z - s.z) > 3);
}

TEST(boarding_the_snowmobile_hands_the_picture_over_without_a_jump) {
  fall_line::sim::GameState state = fall_line::sim::create_free_ride(free38(), 0);
  fall_line::sim::stand_by_sled(state, 0);
  const fall_line::sim::SledSpot spot{state.sled.body.x, state.sled.body.z,
                                      state.sled.body.heading};
  const int32_t climb = fall_line::sim::sled_climb_run(*state.level, spot);
  CHECK(climb >= 0);
  if (climb < 0) return;
  const fall_line::sim::SledOutingPilot pilot(state, climb);
  fall_line::sim::Driver run(std::move(state), 0, 1);
  const ge::skier::World sw = run.state().world();
  const cam::World w = fc::camera_world(sw);
  fc::RideCamera rc = fc::make_ride_camera(fc::Rung::chase, 7);
  double boarded = -1, worst = 0, nearest = 1e9;
  cam::Vec3 last{}, last_him{};
  bool handed = false;
  last = fc::frame_ride(rc, run.state(), sw, w, 1.0 / 60).eye;  // on his skis before he boards
  CHECK(rc.ride == fc::Ride::skis);
  const auto& first = run.state().racers[0].skier;
  last_him = {first.x, first.y, first.z};
  while (run.state().t < 60 && !run.over()) {
    for (int k = 0; k < 2; ++k) run.step(pilot(run.state(), 0));
    const auto& s = run.state().racers[0].skier;
    const cam::Pose p = fc::frame_ride(rc, run.state(), sw, w, 1.0 / 60);
    if (fc::handing_over(rc)) {
      handed = true;
      if (boarded < 0) boarded = run.state().t;
      // The lens's step past his own: his feet are put on the boards in one step, the lens is not.
      const double eye = std::hypot(p.eye.x - last.x, p.eye.y - last.y, p.eye.z - last.z);
      const double him = std::hypot(s.x - last_him.x, s.y - last_him.y, s.z - last_him.z);
      worst = std::max(worst, eye - him);
      nearest = std::min(nearest, std::hypot(p.eye.x - s.x, p.eye.z - s.z));
    }
    last = p.eye;
    last_him = {s.x, s.y, s.z};
    if (boarded >= 0 && run.state().t > boarded + 1.5) break;
  }
  CHECK(handed);
  CHECK(rc.ride == fc::Ride::sled);
  CHECK(!fc::handing_over(rc));
  CHECK(fc::active_lens(rc) == "chase");
  CHECK_MSG(worst < 1.0, "the hand-over stepped the lens " + std::to_string(worst) + " m");
  // Swung round him, never through him.
  CHECK_MSG(nearest > 3, "the chase came " + std::to_string(nearest) + " m from him");
}
