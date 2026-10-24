// engine/water/swim.hpp: the immersion level reads three heights; the kinematic regime settles at
// the speed its numbers give (the wish where friction allows it, accelerate / (friction * level)
// of it where not), sinks slowly with no input and ignores gravity; the water jump throws a
// swimmer over a lip the game reports; the body regime floats a swimmer who swims up and sinks one
// who has run out of breath; cold water shocks him (the breath held runs out faster) and brings
// swim failure on at `failure_time`; soaked clothes are carried out of the water; the events fire
// where they should; and two runs keep their bits in tests/golden/water_swim.txt.
//
// ge:summary Test: swimming: immersion, kinematic speeds, idle sink, water jump, body, cold, breath
// ge:keywords test water swim immersion kinematic idle-sink water-jump body breath cold-shock
// ge:keywords swim-failure soak golden
// ge:skill water testing
#include <cmath>
#include <vector>

#include "check.hpp"
#include "core/libm/libm.hpp"
#include "water/swim.hpp"
#include "water_golden.hpp"

namespace {

namespace gw = ge::water;
constexpr double kDt = 1.0 / 120;
constexpr double kUnits = 32;  // a game's units a metre for the kinematic regime

gw::World pool(double temperature) {
  gw::World world;
  gw::add_ocean(world, 0, {.look = "pool", .density = 1000, .temperature = temperature});
  return world;
}

// The classic numbers, in the game's units.
gw::SwimSpec classic() {
  gw::SwimSpec s;
  s.regime = gw::Regime::kinematic;
  s.scale = kUnits;
  s.waist = 0.75;
  s.eyes = 1.4;
  s.run_speed = 320;
  s.wish_scale = 0.7;
  s.accelerate = 10;
  s.friction = 4;
  s.idle_sink = 60;
  s.jump_speed = 225;
  s.jump_push = 50;
  s.jump_time = 2;
  s.gravity = 800;
  return s;
}

bool always(const void*, const ge::core::Vec3&, double, double) { return true; }

}  // namespace

TEST(the_immersion_level_reads_three_heights) {
  const gw::World world = pool(20);
  const gw::SwimSpec s = gw::default_swimmer();
  CHECK(gw::immersion(world, s, {0, 0.5, 0}, 0) == 0);
  CHECK(gw::immersion(world, s, {0, -0.5, 0}, 0) == 1);
  CHECK(gw::immersion(world, s, {0, -1.2, 0}, 0) == 2);
  CHECK(gw::immersion(world, s, {0, -2, 0}, 0) == 3);
  // The kinematic spec's heights are in the game's units.
  CHECK(gw::immersion(world, classic(), {0, -1.0 * kUnits, 0}, 0) == 2);
}

TEST(the_kinematic_regime_meets_its_numbers) {
  const gw::World world = pool(20);
  const gw::SwimSpec s = classic();
  // No input, eyes under: the slow sink, held at accelerate / (friction * 3) of it.
  gw::SwimState st;
  st.p = {0, -5 * kUnits, 0};
  for (int i = 0; i < 240; ++i) gw::step_swimmer(world, s, {}, st, i * kDt, kDt);
  const double sink = 60 * std::fmin(1.0, 10.0 / (4 * 3));
  CHECK(std::fabs(st.v.y + sink) < 0.5);
  CHECK(std::fabs(st.v.x) < 1e-9 && std::fabs(st.v.z) < 1e-9);
  // Flat out ahead: the same share of the swim's wish, gravity nowhere.
  gw::SwimState go;
  go.p = {0, -5 * kUnits, 0};
  for (int i = 0; i < 240; ++i) gw::step_swimmer(world, s, {0, 0, 1}, go, i * kDt, kDt);
  const double top = 320 * 0.7 * std::fmin(1.0, 10.0 / (4 * 3));
  CHECK(std::fabs(go.v.z - top) < 1);
  CHECK(std::fabs(go.v.y) < 1e-9);
  // Half the input, half the wish.
  gw::SwimState half;
  half.p = go.p = {0, -5 * kUnits, 0};
  for (int i = 0; i < 240; ++i) gw::step_swimmer(world, s, {0, 0, 0.5}, half, i * kDt, kDt);
  CHECK(std::fabs(half.v.z - top / 2) < 1);
  // Out of the water the game's own model has him: nothing changes.
  gw::SwimState dry;
  dry.p = {0, 10, 0};
  dry.v = {3, -2, 1};
  gw::step_swimmer(world, s, {0, 0, 1}, dry, 0, kDt);
  CHECK(dry.v.x == 3 && dry.v.y == -2 && dry.v.z == 1 && dry.level == 0);
}

TEST(the_water_jump_throws_him_over_the_lip) {
  const gw::World world = pool(20);
  const gw::SwimSpec s = classic();
  gw::SwimState st;
  st.p = {0, -1.0 * kUnits, 0};  // at the waist
  st.heading = 0;
  gw::SwimInput in{0, 0, 1, true};
  gw::step_swimmer(world, s, in, st, 0, kDt, {always, nullptr});
  CHECK(st.v.y == 225 && st.v.z == 50 && st.jumping == 2);
  double top = st.p.y;
  for (int i = 1; i < 60; ++i) {
    gw::step_swimmer(world, s, in, st, i * kDt, kDt, {always, nullptr});
    top = std::fmax(top, st.p.y);
  }
  CHECK(top > -1.0 * kUnits + 30);  // the feet up past the waist's lip
  // With no lip the jump does nothing.
  gw::SwimState no;
  no.p = {0, -1.0 * kUnits, 0};
  gw::step_swimmer(world, s, in, no, 0, kDt);
  CHECK(no.jumping == 0);
}

TEST(the_body_regime_floats_a_swimmer_who_swims_and_sinks_one_out_of_breath) {
  const gw::World world = pool(22);
  const gw::SwimSpec s = gw::default_swimmer();
  gw::SwimState up;
  up.p = {0, -0.8, 0};
  int under = 0;
  for (int i = 0; i < 120 * 120; ++i) {
    gw::step_swimmer(world, s, {0, 1, 0}, up, i * kDt, kDt);
    if (i > 120 * 30 && up.level == 3) ++under;  // once the plunge has settled
  }
  CHECK(under == 0);
  CHECK(!up.drowning);
  gw::SwimState still;
  still.p = {0, -0.8, 0};
  for (int i = 0; i < 120 * 180; ++i) gw::step_swimmer(world, s, {}, still, i * kDt, kDt);
  CHECK(still.drowning && still.breath == 0);
  CHECK(still.p.y < -10);
}

TEST(cold_water_shocks_and_brings_on_swim_failure) {
  // The breath held runs out faster in the shock.
  const auto held = [](double temperature) {
    const gw::World world = pool(temperature);
    gw::SwimState st;
    st.p = {0, -3, 0};
    int i = 0;
    while (!st.drowning && i < 120 * 300) {
      gw::step_swimmer(world, gw::default_swimmer(), {}, st, i * kDt, kDt);
      ++i;
    }
    return i * kDt;
  };
  CHECK(held(2) < 0.5 * held(22));
  CHECK(gw::failure_time(0) == 600);
  CHECK(gw::failure_time(10) > gw::failure_time(5));
  // Swimming in 2 degree water, the limbs give out at the failure time and not before.
  const gw::World world = pool(2);
  gw::SwimState st;
  st.p = {0, -1.2, 0};
  const int fail = static_cast<int>(gw::failure_time(2) * 120);
  for (int i = 0; i < fail - 120; ++i)
    gw::step_swimmer(world, gw::default_swimmer(), {0, 1, 0}, st, i * kDt, kDt);
  CHECK(!st.failed);
  for (int i = fail - 120; i < fail + 120; ++i)
    gw::step_swimmer(world, gw::default_swimmer(), {0, 1, 0}, st, i * kDt, kDt);
  CHECK(st.failed);
  // Warm water never cools him to failure.
  const gw::World warm = pool(24);
  gw::SwimState w;
  w.p = {0, -1.2, 0};
  for (int i = 0; i < 120 * 600; ++i)
    gw::step_swimmer(warm, gw::default_swimmer(), {0, 1, 0}, w, i * kDt, kDt);
  CHECK(w.cold == 0 && !w.failed && !w.shock);
}

TEST(the_events_fire_where_they_should) {
  const gw::World world = pool(20);
  gw::SwimState st;
  st.p = {0, 0.5, 0};
  st.v = {0, -4, 0};
  gw::SwimEvents ev;
  gw::step_swimmer(world, gw::default_swimmer(), {}, st, 0, kDt, {}, &ev);
  CHECK(!ev.entered);
  st.p.y = -0.3;
  gw::step_swimmer(world, gw::default_swimmer(), {}, st, kDt, kDt, {}, &ev);
  CHECK(ev.entered && std::fabs(ev.splash_speed - 4) < 0.1);
  int strokes = 0;
  for (int i = 0; i < 120 * 12; ++i) {
    gw::step_swimmer(world, gw::default_swimmer(), {0, 1, 0}, st, i * kDt, kDt, {}, &ev);
    if (ev.stroke) ++strokes;
  }
  CHECK(strokes >= 9 && strokes <= 10);  // one every 1.2 s flat out
}

TEST(every_number_keeps_its_golden) {
  std::vector<std::vector<uint64_t>> got;
  const gw::World world = pool(4);
  gw::SwimState k;
  k.p = {0, -3 * kUnits, 0};
  gw::SwimState b;
  b.p = {0, -0.8, 0};
  for (int i = 0; i < 120 * 60; ++i) {
    const double turn = i * kDt * 0.3;
    gw::step_swimmer(world, classic(), {ge::core::libm::sin(turn), 0.2, ge::core::libm::cos(turn)},
                     k, i * kDt, kDt);
    gw::step_swimmer(world, gw::default_swimmer(), {0, i % 600 < 300 ? 1.0 : 0.0, 0}, b, i * kDt,
                     kDt);
    if (i % 600 == 0) {
      got.push_back({ge::test::bits(k.p.x), ge::test::bits(k.p.y), ge::test::bits(k.p.z),
                     ge::test::bits(b.p.y), ge::test::bits(b.v.y), ge::test::bits(b.breath),
                     ge::test::bits(b.cold), ge::test::bits(b.stamina)});
    }
  }
  ge::test::hold_golden("tests/golden/water_swim.txt",
                        "every 5 s: kinematic p.x p.y p.z; body p.y v.y breath cold stamina "
                        "(water_swim_test)",
                        got);
}
