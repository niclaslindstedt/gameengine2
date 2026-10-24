// press (engine/snow/press.hpp): every reference contact on every snow keeps the mass to the unit;
// a boot packs powder to the density its pressure reaches and the surface drops by what the mass
// conservation says; a sink is pressed to exactly; a slide throws a berm holding what it cut; a
// second pass presses nothing more; no pressure packs past the kind's cap; a press refused by the
// budget is counted; and a track of every contact over three snows keeps its golden
// (tests/golden/snow_press.txt; rewrite with GE_GOLDEN_WRITE=1 when a move is meant, and put the
// snow-press lab's tables before and after in the PR).
//
// ge:summary Tests: press keeps mass, packs by pressure, throws berms, holds its golden
// ge:keywords test snow press imprint mass berm compaction sink cap budget golden
// ge:skill snow testing
// ge:test tests/snow_press_test.cpp
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "golden.hpp"
#include "snow/contact.hpp"
#include "snow/layer.hpp"
#include "snow/press.hpp"
#include "snow/reference.hpp"
#include "snow/snapshot.hpp"

namespace sn = ge::snow;

namespace {

constexpr const char* kGolden = "tests/golden/snow_press.txt";

sn::LayerSpec flat(double depth, double packed, sn::Kind kind = sn::Kind::powder) {
  sn::LayerSpec s;
  s.region = {-10, -10, 10, 10};
  s.cover.depth = depth;
  s.cover.packed = packed;
  s.cover.kind = kind;
  return s;
}

// A contact's track: `n` presses along +z from z = -4.
void track(sn::Layer& layer, int contact, int n, double x = 0) {
  const sn::ReferenceContact rc = sn::reference_contact(contact);
  const double step = rc.step > 0 ? rc.step : 0.5;
  for (int i = 0; i < n; i++)
    sn::press(layer, sn::place(rc.imprint, x, -4 + step * i, 0), i / 120.0);
}

uint64_t fnv(const std::vector<uint8_t>& bytes) {
  uint64_t h = 1469598103934665603ull;
  for (const uint8_t b : bytes) {
    h ^= b;
    h *= 1099511628211ull;
  }
  return h;
}

std::string hex(uint64_t v) {
  static const char* d = "0123456789abcdef";
  std::string s(16, '0');
  for (int i = 15; i >= 0; i--, v >>= 4) s[i] = d[v & 15];
  return s;
}

}  // namespace

TEST(every_reference_contact_on_every_snow_keeps_the_mass_to_the_unit) {
  const sn::LayerSpec snows[] = {flat(0.6, 0), flat(0.25, 0.5, sn::Kind::settled),
                                 flat(0.15, 1, sn::Kind::groomed)};
  for (const sn::LayerSpec& spec : snows) {
    for (int k = 0; k < sn::kReferenceCount; k++) {
      sn::SnowWorld world;
      sn::Layer& layer = sn::add_layer(world, spec);
      track(layer, k, 24);
      CHECK_MSG(sn::mass_units(layer) + layer.spilled == layer.mass_in,
                std::string(sn::reference_contact(k).name) + " lost or made snow");
      CHECK(layer.spilled == 0);
      // Every contact presses fresh powder; the light ones leave packed snow as it was.
      if (spec.cover.packed == 0) CHECK(!layer.records.empty());
    }
  }
}

TEST(a_boot_post_holes_powder_to_the_density_its_pressure_reaches) {
  sn::SnowWorld world;
  sn::Layer& layer = sn::add_layer(world, flat(0.5, 0));
  const sn::Imprint boot = sn::reference_contact(2).imprint;
  const sn::Pressed p = sn::press(layer, boot, 0);
  CHECK(p.pressure > 25e3 && p.pressure < 35e3);  // 785 N on a 30 x 11 cm sole
  // Bekker's sink for the sole's 11 cm width passes what 50 cm of 70 kg/m3 can compact: the boot
  // post-holes until the snow under it is packed to what its pressure reaches.
  const double z = sn::column_sink(sn::Kind::powder, 0.5, 70, 0.11, p.pressure);
  CHECK(sn::bekker_sink(sn::contact_row(sn::Kind::powder), 0.11, p.pressure) > 0.5);
  CHECK(z == sn::sink_cap(0.5, 70, p.pressure, 600));
  CHECK(z > 0.38 && z < 0.45);
  const sn::Column c = sn::sample(layer, 0, 0);
  CHECK(c.touched);
  CHECK(std::abs(c.depth - (0.5 - z)) < 0.002);
  // The mass stays (a boot throws none): the print's floor is as much denser as it is lower.
  CHECK(std::abs(c.density - 0.5 * 70 / (0.5 - z)) < 3);
  CHECK(std::abs(p.sink - z) < 0.002);
}

TEST(a_sink_is_pressed_to_exactly) {
  sn::SnowWorld world;
  sn::Layer& layer = sn::add_layer(world, flat(0.4, 0));
  sn::Imprint imp;
  imp.shape = sn::disc(0, 0, 0.5);
  imp.sink = 0.15;
  imp.side_push = 0;
  imp.wall = 10;
  sn::press(layer, imp, 0);
  const sn::Column c = sn::sample(layer, 0, 0);
  CHECK(std::abs(c.depth - 0.25) < 0.0011 && std::abs(c.drop - 0.15) < 0.0011);
  CHECK(std::abs(c.density - 0.4 * 70 / 0.25) < 0.5);
}

TEST(a_slide_throws_a_berm_beside_its_groove_holding_what_it_cut) {
  sn::SnowWorld world;
  sn::Layer& layer = sn::add_layer(world, flat(0.4, 0));
  sn::Imprint imp;
  imp.shape = sn::capsule(0, -2, 0, 2, 0.2);
  imp.sink = 0.2;
  imp.motion = sn::Motion::plough;
  const sn::Pressed p = sn::press(layer, imp, 0);
  CHECK(p.thrown > 0 && p.spilled == 0);
  // Four fifths of what was cut: 70 kg/m3 to an axis 20 cm deep across a 4.4 x 0.4 m capsule, its
  // walls easing, about 15 kg.
  CHECK(p.thrown > 10 && p.thrown < 20);
  // The groove's floor is low, the berm just outside it high, and the far snow untouched.
  CHECK(sn::sample(layer, 0, 0).drop > 0.19);
  CHECK(sn::sample(layer, 0.25, 0).drop < -0.01);
  CHECK(sn::sample(layer, 1.0, 0).drop == 0);
  CHECK(sn::mass_units(layer) == layer.mass_in);
}

TEST(a_second_pass_presses_nothing_more) {
  for (int k = 0; k < sn::kReferenceCount; k++) {
    sn::SnowWorld world;
    sn::Layer& layer = sn::add_layer(world, flat(0.5, 0));
    const sn::Imprint imp = sn::reference_contact(k).imprint;
    sn::press(layer, imp, 0);
    const std::vector<uint8_t> once = sn::save(layer);
    const sn::Pressed again = sn::press(layer, imp, 1);
    CHECK_MSG(again.samples == 0 && sn::save(layer) == once,
              std::string(sn::reference_contact(k).name) + " pressed more on its second pass");
  }
}

TEST(no_pressure_packs_past_the_kind_s_cap) {
  sn::SnowWorld world;
  sn::Layer& layer = sn::add_layer(world, flat(0.5, 0));
  sn::Imprint imp;
  imp.shape = sn::disc(0, 0, 0.1);
  imp.load = 1e7;
  sn::press(layer, imp, 0);
  CHECK(sn::sample(layer, 0, 0).density <= sn::kind_row(sn::Kind::powder).cap + 1);
  CHECK(sn::reach_density(1e9, 600) == 600 && sn::reach_density(0, 600) == 0);
}

TEST(a_press_past_the_budget_is_refused_and_counted) {
  sn::LayerSpec spec = flat(0.5, 0);
  spec.max_tiles = 1;
  sn::SnowWorld world;
  sn::Layer& layer = sn::add_layer(world, spec);
  sn::press(layer, sn::place(sn::reference_contact(2).imprint, -8, -8, 0), 0);
  const sn::Pressed p = sn::press(layer, sn::place(sn::reference_contact(2).imprint, 8, 8, 0), 0);
  CHECK(layer.tiles.size() == 1 && p.refused > 0 && p.samples == 0);
  CHECK(sn::mass_units(layer) == layer.mass_in);
}

TEST(the_shapes_measure_their_outlines) {
  CHECK(std::abs(sn::shape_area(sn::disc(0, 0, 1)) - ge::core::kPi) < 1e-12);
  CHECK(sn::shape_area(sn::box(0, 0, 0.3, 2, 1)) == 8);
  CHECK(sn::shape_u(sn::box(0, 0, 0, 2, 1), 0, 2) == 1);  // its front, along +z
  CHECK(sn::shape_u(sn::box(0, 0, 0, 2, 1), 1, 0) == 1);  // its side
  CHECK(sn::shape_u(sn::capsule(0, 0, 0, 4, 0.5), 0.5, 2) == 1);
  CHECK(std::abs(sn::shape_u(sn::ellipse(0, 0, ge::core::kPi / 2, 2, 1), 2, 0) - 1) < 1e-12);
}

namespace {

bool env_set(const char* name) {
#ifdef _MSC_VER
  char* v = nullptr;
  size_t n = 0;
  const bool set = _dupenv_s(&v, &n, name) == 0 && v != nullptr;
  std::free(v);
  return set;
#else
  return std::getenv(name) != nullptr;
#endif
}

}  // namespace

TEST(a_track_of_every_contact_over_three_snows_keeps_its_golden) {
  const bool write = env_set("GE_GOLDEN_WRITE");
  const sn::LayerSpec snows[] = {flat(0.6, 0), flat(0.25, 0.5, sn::Kind::settled),
                                 flat(0.15, 1, sn::Kind::groomed)};
  std::vector<std::string> got;
  for (int s = 0; s < 3; s++) {
    sn::SnowWorld world;
    sn::Layer& layer = sn::add_layer(world, snows[s]);
    for (int k = 0; k < sn::kReferenceCount; k++) track(layer, k, 30, -7.5 + 3 * k);
    const sn::Column c = sn::sample(layer, -7.5, 0);
    got.push_back(std::to_string(s) + " " + std::to_string(layer.tiles.size()) + " " +
                  std::to_string(layer.records.size()) + " " + hex(ge::test::bits(c.surface)) +
                  " " + hex(ge::test::bits(c.density)) + " " + hex(fnv(sn::save(layer))));
  }
  if (write) {
    std::ofstream out(ge::test::root_path(kGolden));
    out << "# snow tiles records surface-bits density-bits snapshot-fnv (snow_press_test: every "
           "reference contact's track over powder, settled and groomed snow)\n";
    for (const auto& line : got) out << line << "\n";
    return;
  }
  const auto rows = ge::test::golden_words(kGolden);
  CHECK_MSG(rows.size() == got.size(),
            std::string("no golden rows: write ") + kGolden + " with GE_GOLDEN_WRITE=1");
  for (size_t i = 0; i < rows.size() && i < got.size(); i++) {
    std::string want;
    for (const auto& w : rows[i]) want += (want.empty() ? "" : " ") + w;
    CHECK_MSG(got[i] == want, "press moved:\n  got  " + got[i] + "\n  want " + want +
                                  "\nIf the move is meant, rewrite " + kGolden +
                                  " with GE_GOLDEN_WRITE=1 and put the snow-press lab's tables "
                                  "before and after in the PR.");
  }
}
