// engine/render/water_stream.hpp: a river's ribbon is a row of vertices bank to bank down its
// centreline, on the surface `sample` reports and with its current; a flow region over a ledge
// draws its wet cells and a curtain for every sheet, from the lip down to where it lands; the
// spray is thrown in proportion to the discharge and the same for the same seed; a splash out of
// the water throws none.
//
// ge:summary Tests for moving water's meshes and spray: the ribbon, the grid, the curtain, births
// ge:keywords test render water river ribbon flow grid waterfall curtain spray splash
// ge:skill render water testing
#include <cmath>
#include <cstring>
#include <vector>

#include "check.hpp"
#include "core/prng.hpp"
#include "render/water_stream.hpp"
#include "water/flow.hpp"
#include "water/world.hpp"

namespace {

namespace r = ge::render;
namespace gw = ge::water;

// A flow region with water held behind a 6 m ledge at x = 12, run until it pours over.
gw::World ledge() {
  gw::World world;
  gw::Flow f = gw::make_flow({.nx = 64, .nz = 8, .cell = 0.5});
  gw::set_bed(f, 0, 0, 12, 4, 6);
  gw::fill_to(f, 0, 0, 8, 4, 7);
  gw::add_flow(world, std::move(f));
  for (int i = 0; i < 240; ++i) gw::step_flows(world, 1.0 / 120);
  return world;
}

}  // namespace

TEST(a_rivers_ribbon_lies_on_its_surface) {
  gw::World world;
  const std::vector<gw::ChannelPoint> line{{0, 0, 10, 8, 1.5}, {0, 40, 9, 8, 1.5}};
  gw::add_river(world, line, 6);
  r::StreamMesh mesh;
  r::build_water_streams(world, 3, mesh, 1.0);
  // 40 rows past the first, nine across, two triangles a quad.
  CHECK(mesh.vertices.size() == size_t{41} * 9);
  CHECK(mesh.indices.size() == size_t{40} * 8 * 6);
  for (const r::StreamVertex& v : mesh.vertices) {
    CHECK(v.tag == 0);
    const gw::Sample s = gw::sample(world, v.p[0], v.p[1], v.p[2], 3);
    CHECK(s.body == 0);
    CHECK(std::fabs(v.p[1] - s.surface) < 1e-4);
    CHECK(v.v[1] > 0);  // running down the line, +z
  }
  for (const uint32_t i : mesh.indices) CHECK(i < mesh.vertices.size());
}

TEST(a_ledges_sheets_are_drawn_from_the_lip_to_the_landing) {
  const gw::World world = ledge();
  const gw::Flow& f = world.flows[0];
  CHECK(!f.falls.empty());
  r::StreamMesh mesh;
  r::build_water_streams(world, 2, mesh);
  int grid = 0, curtain = 0;
  float top = -1e9f, bottom = 1e9f;
  for (const r::StreamVertex& v : mesh.vertices) {
    if (v.tag == 1) {
      ++curtain;
      top = std::fmax(top, v.p[1]);
      bottom = std::fmin(bottom, v.p[1]);
    } else {
      CHECK(v.tag == 0);
      ++grid;
    }
  }
  CHECK(grid > 0);
  CHECK(curtain > 0 && curtain % 2 == 0);
  double lip = -1e9, land = 1e9;
  for (const gw::Fall& fall : f.falls) {
    lip = std::fmax(lip, fall.y);
    land = std::fmin(land, fall.land_y);
  }
  CHECK_MSG(std::fabs(top - lip) < 1e-3 && std::fabs(bottom - land) < 0.05,
            "the curtains run " + std::to_string(top) + " to " + std::to_string(bottom) +
                " m, the sheets " + std::to_string(lip) + " to " + std::to_string(land));
  for (const uint32_t i : mesh.indices) CHECK(i < mesh.vertices.size());
}

TEST(the_spray_follows_the_discharge_and_the_seed) {
  const gw::World world = ledge();
  const gw::Flow& f = world.flows[0];
  double q = 0;
  for (const gw::Fall& fall : f.falls) q += fall.discharge;
  ge::core::Rng a = ge::core::make_rng(5), b = ge::core::make_rng(5);
  std::vector<r::Particle> pa, pb;
  const double dt = 1.0 / 30;
  for (int i = 0; i < 300; ++i) {
    r::waterfall_spray(f, dt, a, pa);
    r::waterfall_spray(f, dt, b, pb);
  }
  const double want = 60 * q * dt * 300;
  CHECK_MSG(std::fabs(static_cast<double>(pa.size()) - want) < 0.1 * want + 10,
            std::to_string(pa.size()) + " drops for " + std::to_string(want) + " expected");
  CHECK(pa.size() == pb.size());
  CHECK(std::memcmp(pa.data(), pb.data(), pa.size() * sizeof(r::Particle)) == 0);
  for (const r::Particle& p : pa) CHECK(p.v[1] > 0 && p.life > 0);

  std::vector<r::Particle> splash;
  ge::core::Rng c = ge::core::make_rng(1);
  r::splash_spray({.speed = 6, .size = 0.5, .entering = false}, c, splash);
  CHECK(splash.empty());
  r::splash_spray({.speed = 6, .size = 0.5}, c, splash);
  CHECK(splash.size() == 80);
}
