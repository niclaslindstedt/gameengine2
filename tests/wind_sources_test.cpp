// engine/wind/sources.hpp: a jet blows along its axis, holds its speed through its core and decays
// as 6.2 diameters over the distance past it, and nothing blows behind its nozzle; a whirl turns
// clockwise from above at its rim speed, solid inside and 1 / r outside, continuous at the rim,
// with an updraft in its core; a blast is still before its moment and outside its front, and dies
// with the square of the distance and with time; sources add onto a field's wind.
//
// ge:summary Test: local wind sources: jet decay, whirl rim and core, blast front, the sum
// ge:keywords test wind source jet whirl blast rankine downwash
// ge:skill water testing
#include <cmath>
#include <vector>

#include "check.hpp"
#include "wind/sources.hpp"

namespace {

namespace gw = ge::wind;

gw::Source down_jet() {
  gw::Source s;
  s.kind = gw::SourceKind::jet;
  s.y = 20;
  s.radius = 1;
  s.strength = 12;
  return s;
}

}  // namespace

TEST(a_jet_blows_along_its_axis_and_decays_past_its_core) {
  const gw::Source s = down_jet();
  const gw::Air near = gw::source_at(s, 0, 15, 0, 0);
  CHECK(std::fabs(near.y + 12) < 1e-9 && near.x == 0 && near.z == 0);
  // Past 6.2 diameters (12.4 m) the centreline falls as their share of the distance.
  CHECK(std::fabs(gw::source_at(s, 0, 20 - 24.8, 0, 0).y + 6) < 1e-9);
  // Off the axis it is weaker; behind the nozzle there is nothing.
  CHECK(std::fabs(gw::source_at(s, 1.5, 15, 0, 0).y) < 12 * 0.5);
  CHECK(gw::source_at(s, 0, 25, 0, 0).speed == 0);
}

TEST(a_whirl_turns_clockwise_from_above_and_is_continuous_at_its_rim) {
  gw::Source s;
  s.kind = gw::SourceKind::whirl;
  s.radius = 4;
  s.strength = 10;
  // East of the centre (+x), clockwise from above moves toward -z.
  const gw::Air east = gw::source_at(s, 4, 0, 0, 0);
  CHECK(std::fabs(east.z + 10) < 1e-9 && std::fabs(east.x) < 1e-9);
  const double in =
      std::hypot(gw::source_at(s, 3.999, 50, 0, 0).x, gw::source_at(s, 3.999, 50, 0, 0).z);
  const double out =
      std::hypot(gw::source_at(s, 4.001, 50, 0, 0).x, gw::source_at(s, 4.001, 50, 0, 0).z);
  CHECK(std::fabs(in - out) < 0.01);
  CHECK(std::fabs(std::hypot(gw::source_at(s, 8, 0, 0, 0).x, gw::source_at(s, 8, 0, 0, 0).z) - 5) <
        1e-9);
  CHECK(gw::source_at(s, 0.5, 1, 0, 0).y > 4);       // the core rises
  CHECK(gw::source_at(s, 0.5, 400, 0, 0).y < 1e-3);  // and fades with height
}

TEST(a_blast_waits_for_its_moment_and_its_front) {
  gw::Source s;
  s.kind = gw::SourceKind::blast;
  s.radius = 2;
  s.strength = 30;
  s.t0 = 1;
  CHECK(gw::source_at(s, 10, 0, 0, 0.5).speed == 0);   // before its moment
  CHECK(gw::source_at(s, 100, 0, 0, 1.1).speed == 0);  // beyond the front (34 m)
  const gw::Air a = gw::source_at(s, 10, 0, 0, 1.1);
  CHECK(a.x > 0 && std::fabs(a.y) < 1e-12);
  CHECK(std::fabs(a.x - 30 * 0.04 * std::exp(-0.2)) < 1e-9);
  CHECK(gw::source_at(s, 10, 0, 0, 3).speed < a.speed * 0.03);
}

TEST(sources_add_onto_the_field) {
  gw::Field f;
  f.mean = 4;
  f.from = 3.141592653589793;  // toward +z
  const gw::Source jets[] = {down_jet()};
  const gw::Air a = gw::wind_at(f, {0, 15, 0, 10}, 0, nullptr, jets);
  CHECK(std::fabs(a.z - 4) < 1e-9 && std::fabs(a.y + 12) < 1e-9);
  CHECK(std::fabs(a.speed - std::sqrt(16.0 + 144.0)) < 1e-9);
}
