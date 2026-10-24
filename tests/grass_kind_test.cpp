// engine/grass/kind.hpp: every preset is sound and round-trips through its recipe; a recipe's
// overrides land on their fields; every fault names itself; the far colour lies between the
// blade's root and tip.
//
// ge:summary Test: kinds of grass: presets, recipes parsed and printed, faults, the far colour
// ge:keywords test grass kind preset recipe parse print far-colour
// ge:skill grass testing
#include <algorithm>
#include <string>

#include "check.hpp"
#include "grass/kind.hpp"
#include "grass_golden.hpp"

namespace gg = ge::grass;
using ge::test::grass_preset;

TEST(every_preset_is_sound_and_round_trips) {
  CHECK(gg::presets().size() == 6);
  for (const gg::Kind& k : gg::presets()) {
    CHECK(k.height > 0 && k.width > 0 && k.density > 0 && k.give > 0 && k.sway > 0);
    CHECK(gg::print_kind(k) == k.name);
    const auto back = gg::parse_kind(gg::print_kind(k));
    CHECK(back && back->height == k.height && back->density == k.density);
  }
  CHECK(grass_preset("long").height > grass_preset("meadow").height);
  CHECK(grass_preset("lawn").height < grass_preset("meadow").height);
}

TEST(a_recipe_overrides_its_preset) {
  std::string error;
  const auto k =
      gg::parse_kind("long height=1.2 density=80 width=0.01 tip=0.3,0.25,0.1 dry=0.5", &error);
  CHECK_MSG(k.has_value(), error);
  if (!k) return;
  CHECK(k->name == "long");
  CHECK(k->height == 1.2f && k->density == 80.0f && k->width == 0.01f && k->dry == 0.5f);
  CHECK(k->tip[0] == 0.3f && k->tip[1] == 0.25f && k->tip[2] == 0.1f);
  CHECK(k->sway == grass_preset("long").sway);
  CHECK(gg::print_kind(*k) == "long height=1.2 width=0.01 density=80 dry=0.5 tip=0.3,0.25,0.1");
  const auto again = gg::parse_kind(gg::print_kind(*k));
  CHECK(again.has_value());
  if (!again) return;
  CHECK(gg::print_kind(*again) == gg::print_kind(*k));
}

TEST(every_fault_names_itself) {
  std::string e;
  CHECK(!gg::parse_kind("", &e) && e.find("preset") != std::string::npos);
  CHECK(!gg::parse_kind("clover", &e) && e.find("unknown grass preset 'clover'") == 0);
  CHECK(!gg::parse_kind("meadow tall", &e) && e.find("no '='") != std::string::npos);
  CHECK(!gg::parse_kind("meadow colour=1", &e) && e.find("unknown grass field 'colour'") == 0);
  CHECK(!gg::parse_kind("meadow height=x", &e) && e.find("write a number") != std::string::npos);
  CHECK(!gg::parse_kind("meadow tip=1,2", &e) && e.find("r,g,b") != std::string::npos);
  CHECK(!gg::parse_kind("meadow height=0", &e) && e.find("above 0") != std::string::npos);
}

TEST(the_far_colour_lies_between_the_shaded_root_and_the_tip) {
  for (const gg::Kind& k : gg::presets()) {
    const auto c = gg::far_colour(k);
    for (size_t i = 0; i < 3; ++i) {
      const float lo = std::min(std::min(k.root[i], k.tip[i]), k.dry_tip[i]) * 0.68f;
      const float hi = std::max(std::max(k.root[i], k.tip[i]), k.dry_tip[i]);
      CHECK(c[i] >= lo && c[i] <= hi);
    }
  }
}
