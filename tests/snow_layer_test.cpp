// The snow layer (engine/snow/layer.hpp, snapshot.hpp): an untouched layer answers its cover bit
// for bit and keeps no tile; a tile's unpressed samples still read the cover; a layer is found by
// its region; the snapshot comes back sample for sample into a layer of the same spec and refuses
// another's.
//
// ge:summary Tests: the snow layer's cover, tiles, sample query and snapshot
// ge:keywords test snow layer cover sample untouched tile snapshot save load bits
// ge:skill snow testing
// ge:test tests/snow_layer_test.cpp
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "check.hpp"
#include "core/prng.hpp"
#include "snow/layer.hpp"
#include "snow/press.hpp"
#include "snow/reference.hpp"
#include "snow/snapshot.hpp"

namespace sn = ge::snow;

namespace {

// A sloped ground and a cover whose depth and packed share vary over the plan.
struct Scene {
  ge::core::Heightfield ground, depth, packed;
  sn::LayerSpec spec;
};

Scene make_scene() {
  Scene s;
  const sn::Region r{-20, -20, 20, 20};
  s.ground = sn::bake_ground(r, 0.5, [](double x, double z) { return 100 - 0.3 * z + 0.05 * x; });
  s.depth = sn::bake_ground(r, 2, [](double x, double z) { return 0.4 + 0.01 * (x + z); });
  s.packed = sn::bake_ground(r, 2, [](double x, double) { return x < 0 ? 1.0 : 0.0; });
  s.spec.region = r;
  s.spec.ground.field = &s.ground;
  s.spec.cover.depth_grid = &s.depth;
  s.spec.cover.packed_grid = &s.packed;
  return s;
}

void walk(sn::Layer& layer) {
  for (int i = 0; i < 20; i++) {
    sn::press(layer,
              sn::place(sn::reference_contact(2).imprint, -3 + 0.3 * (i % 2), -6 + 0.75 * i, 0),
              i * 0.5);
  }
  for (int i = 0; i < 30; i++) {
    sn::press(layer, sn::place(sn::reference_contact(3).imprint, 5, -6 + 0.2 * i, 0),
              20 + i * 0.05);
  }
}

}  // namespace

TEST(an_untouched_layer_samples_its_cover_bit_for_bit_and_keeps_no_tile) {
  Scene s = make_scene();
  sn::SnowWorld world;
  const sn::Layer& layer = sn::add_layer(world, s.spec);
  ge::core::Rng rng = ge::core::make_rng(7);
  for (int i = 0; i < 2000; i++) {
    const double x = ge::core::range(rng, -20, 20), z = ge::core::range(rng, -20, 20);
    const sn::Column c = sn::sample(layer, x, z);
    const sn::CoverSample cov = sn::cover_at(s.spec.cover, x, z);
    CHECK_BITS(c.depth, ge::core::sample_field(s.depth, x, z));
    CHECK_BITS(c.packed, ge::core::clamp(ge::core::sample_field(s.packed, x, z), 0, 1));
    CHECK_BITS(c.density, cov.density);
    CHECK_BITS(c.ground, ge::core::sample_field(s.ground, x, z));
    CHECK_BITS(c.surface, c.ground + c.depth);
    CHECK(c.drop == 0 && !c.touched);
  }
  CHECK(layer.tiles.empty() && sn::mass_units(layer) == 0);
}

TEST(a_tile_s_unpressed_samples_still_read_the_cover) {
  Scene s = make_scene();
  sn::SnowWorld world;
  sn::Layer& layer = sn::add_layer(world, s.spec);
  sn::press(layer, sn::place(sn::reference_contact(2).imprint, 1, 1, 0), 0);
  CHECK(layer.tiles.size() == 1);
  // 2 m off the print, inside the same 6.4 m tile.
  const sn::Column c = sn::sample(layer, 3.05, 1.05);
  CHECK(!c.touched);
  CHECK_BITS(c.depth, ge::core::sample_field(s.depth, 3.05, 1.05));
}

TEST(the_density_from_packed_share_and_back_agree_for_every_kind) {
  for (int k = 0; k < static_cast<int>(sn::Kind::count); k++) {
    const auto kind = static_cast<sn::Kind>(k);
    for (int n = 0; n <= 8; n++) {
      const double p = n * 0.125;
      const double rho = sn::density_from_packed(kind, p);
      if (sn::kind_row(kind).packed > sn::kind_row(kind).loose) {
        CHECK(std::abs(sn::packed_from_density(kind, rho) - p) < 1e-12);
      }
      CHECK(rho <= sn::kind_row(kind).cap);
    }
  }
}

TEST(a_layer_is_found_by_its_region) {
  sn::SnowWorld world;
  sn::LayerSpec a, b;
  a.region = {0, 0, 10, 10};
  b.region = {20, 0, 30, 10};
  b.cover.depth = 0.7;
  sn::add_layer(world, a);
  sn::add_layer(world, b);
  sn::Column c;
  CHECK(sn::sample(world, 25, 5, c) && c.depth == 0.7);
  CHECK(sn::sample(world, 5, 5, c) && c.depth == 0.3);
  CHECK(!sn::sample(world, 15, 5, c));
}

TEST(a_snapshot_loads_back_sample_for_sample_and_refuses_another_layer) {
  Scene s = make_scene();
  sn::SnowWorld world;
  sn::Layer& layer = sn::add_layer(world, s.spec);
  walk(layer);
  const std::vector<uint8_t> bytes = sn::save(layer);
  sn::SnowWorld other;
  sn::Layer& copy = sn::add_layer(other, s.spec);
  std::string error;
  CHECK_MSG(sn::load(copy, bytes, error), error);
  CHECK(copy.tiles.size() == layer.tiles.size() && copy.mass_in == layer.mass_in);
  CHECK(sn::save(copy) == bytes);
  for (int j = 0; j < 271; j++) {
    for (int i = 0; i < 293; i++) {
      const double z = -8 + j * 0.037, x = -5 + i * 0.041;
      const sn::Column a = sn::sample(layer, x, z), b = sn::sample(copy, x, z);
      CHECK_BITS(a.surface, b.surface);
      CHECK_BITS(a.density, b.density);
    }
  }
  sn::SnapshotInfo info;
  CHECK(sn::read_info(bytes, info, error) && info.tiles == layer.tiles.size());
  sn::LayerSpec wrong = s.spec;
  wrong.cell = 0.2;
  sn::Layer& elsewhere = sn::add_layer(other, wrong);
  CHECK(!sn::load(elsewhere, bytes, error) && error.find("another layer") != std::string::npos);
  std::vector<uint8_t> cut(bytes.begin(), bytes.end() - 3);
  CHECK(!sn::read_info(cut, info, error) && error.find("cut short") != std::string::npos);
  sn::SnowWorld bare;
  CHECK_MSG(sn::load_bare(bare, bytes, error), error);
  CHECK(sn::mass_units(*bare.layers[0]) == sn::mass_units(layer));
}
