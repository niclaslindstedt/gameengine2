// plugins/skier/swim.hpp: the dressed skier goes through the ice of 1 degree water and does
// nothing: once the plunge has settled his jacket's air holds his eyes out for the first minute,
// his clothes soak several kilograms within two, his eyes go under within four, and he is metres
// down by eight; swimming up flat out keeps his eyes out at least a minute longer.
//
// ge:summary Test: the dressed skier in cold water floats on his jacket's air, soaks and sinks
// ge:keywords test skier swim dressed soak sink cold water preset
// ge:skill skier water testing
#include "check.hpp"
#include "skier/swim.hpp"

namespace {

namespace gw = ge::water;
constexpr double kDt = 1.0 / 120;

gw::World icy() {
  gw::World world;
  gw::add_ocean(world, 0, {.look = "glacial", .density = 1000, .temperature = 1});
  return world;
}

// The first moment the eyes are under once the plunge has settled (30 s), s (or -1), running
// `seconds` with input `up`.
double eyes_under(const gw::SwimSpec& spec, double up, double seconds, gw::SwimState& st) {
  const gw::World world = icy();
  st.p = {0, -0.8, 0};
  double first = -1;
  for (int i = 0; i < static_cast<int>(seconds * 120); ++i) {
    gw::step_swimmer(world, spec, {0, up, 0}, st, i * kDt, kDt);
    if (first < 0 && i >= 120 * 30 && st.level == 3) first = i * kDt;
  }
  return first;
}

}  // namespace

TEST(the_dressed_skier_floats_soaks_and_sinks) {
  const gw::SwimSpec spec = ge::skier::dressed_skier();
  gw::SwimState two;
  eyes_under(spec, 0, 120, two);
  CHECK(two.carried > 5);
  gw::SwimState st;
  const double under = eyes_under(spec, 0, 480, st);
  CHECK(under > 60 && under < 240);
  CHECK(st.p.y < -5);
  CHECK(st.drowning);
}

TEST(swimming_up_holds_his_eyes_out_longer) {
  gw::SwimState still, swims;
  const double lying = eyes_under(ge::skier::dressed_skier(), 0, 600, still);
  const double swimming = eyes_under(ge::skier::dressed_skier(), 1, 600, swims);
  CHECK(lying > 0);
  CHECK(swimming < 0 || swimming > lying + 60);
}
