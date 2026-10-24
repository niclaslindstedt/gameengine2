// engine/grass/sway.hpp: a blade keeps its length in any wind; it bends downwind more the harder
// the wind blows, the stiffer kinds less; in a calm it stands at its rest lean and only breathes;
// the wind grid carries a gust downwind as a front and snaps to whole cells; and the shapes keep
// their bits in tests/golden/grass_sway.txt (rewrite with GE_GOLDEN_WRITE=1 for a deliberate
// move).
//
// ge:summary Test: a blade in the wind: length kept, bend by speed and give, calm, gust fronts
// ge:keywords test grass sway bend length wind-grid gust front golden blade_shape
// ge:skill grass testing
#include <cmath>
#include <vector>

#include "check.hpp"
#include "grass/sway.hpp"
#include "grass_golden.hpp"
#include "wind/field.hpp"

namespace {

namespace gg = ge::grass;
using ge::test::grass_preset;

gg::Blade upright(float height) {
  gg::Blade b{};
  b.root[0] = 3;
  b.root[1] = 1;
  b.root[2] = -2;
  b.seed = 0x123456u;
  b.height = height;
  b.width = 0.005f;
  b.lean[0] = 0.2f;
  b.lean[1] = 0.0f;
  return b;
}

float arc_length(const gg::Shape& s) {
  float len = 0, prev[3], p[3];
  gg::bezier_point(s, 0, prev);
  for (int i = 1; i <= 200; ++i) {
    gg::bezier_point(s, static_cast<float>(i) / 200, p);
    len += std::sqrt((p[0] - prev[0]) * (p[0] - prev[0]) + (p[1] - prev[1]) * (p[1] - prev[1]) +
                     (p[2] - prev[2]) * (p[2] - prev[2]));
    for (int k = 0; k < 3; ++k) prev[k] = p[k];
  }
  return len;
}

}  // namespace

TEST(a_blade_keeps_its_length_in_any_wind) {
  const gg::Kind k = grass_preset("long");
  const gg::Blade b = upright(0.9f);
  for (const float w : {0.0f, 1.0f, 3.0f, 6.0f, 12.0f, 25.0f}) {
    for (const float t : {0.0f, 0.37f, 1.9f}) {
      const float air[2] = {w, 0};
      const float len = arc_length(gg::blade_shape(k, b, air, t));
      CHECK_MSG(std::fabs(len / 0.9f - 1) < 0.04f,
                "length " + std::to_string(len) + " at " + std::to_string(w) + " m/s");
    }
  }
}

TEST(a_blade_bends_downwind_by_the_speed_and_its_give) {
  const gg::Blade b = upright(0.5f);
  const gg::Kind soft = grass_preset("long"), stiff = grass_preset("lawn");
  float last = 1e9f;
  for (const float w : {0.0f, 2.0f, 4.0f, 8.0f, 16.0f}) {
    // Averaged over a sway period, so the sway's phase does not decide.
    float tip_y = 0, tip_z = 0;
    for (int i = 0; i < 64; ++i) {
      const float air[2] = {0, w};
      const gg::Shape s = gg::blade_shape(soft, b, air, static_cast<float>(i) * 0.05f);
      tip_y += s.v2[1] - s.v0[1];
      tip_z += s.v2[2] - s.v0[2];
    }
    CHECK(tip_y < last);
    last = tip_y;
    if (w > 0) CHECK(tip_z > 0);
  }
  CHECK(gg::bend_of(stiff, 8) < gg::bend_of(soft, 8));
  CHECK(std::fabs(gg::bend_of(soft, soft.give) - 0.5f) < 1e-6f);
  CHECK(gg::bend_of(soft, 0) == 0);
}

TEST(in_a_calm_a_blade_stands_at_its_rest_lean) {
  const gg::Kind k = grass_preset("meadow");
  const gg::Blade b = upright(0.4f);
  const float calm[2] = {0, 0};
  for (const float t : {0.0f, 0.5f, 1.3f}) {
    const gg::Shape s = gg::blade_shape(k, b, calm, t);
    // The rest lean is 0.2 rad toward +x; the breathing adds at most 0.04 rad.
    const float off = std::atan2(s.v2[0] - s.v0[0], s.v2[1] - s.v0[1]);
    CHECK_MSG(off > 0.05f && off < 0.35f, "calm lean " + std::to_string(off));
    CHECK(std::fabs(s.v2[2] - s.v0[2]) < 0.02f);
  }
}

TEST(the_wind_grid_carries_gusts_downwind_and_snaps) {
  ge::wind::Field f;
  f.mean = 6;
  f.from = 0;  // from +z: blows toward -z
  ge::wind::lay_gusts(f, 3, 0.3);
  gg::GrassAir air;
  air.field = &f;
  gg::WindGrid a, b;
  gg::fill_wind_grid(a, air, 0, 0, 62, 32, 10);
  gg::fill_wind_grid(b, air, 0.4, -0.3, 62, 32, 10);
  CHECK(a.x0 == b.x0 && a.z0 == b.z0 && a.air == b.air);
  CHECK(a.mean[1] < 0 && std::fabs(a.mean[0]) < 1e-6f);
  // A node 12 m downwind at t + 2 s meets what the upwind node met at t.
  gg::WindGrid later;
  gg::fill_wind_grid(later, air, 0, 0, 62, 32, 12);
  const size_t up = (size_t{20} * 32 + 10) * 2,
               down = (size_t{14} * 32 + 10) * 2;  // 6 cells of 2 m
  CHECK(std::fabs(later.air[down + 1] - a.air[up + 1]) < 1e-4f);
  CHECK(a.air[up + 1] < 0);
}

TEST(the_shapes_keep_their_bits) {
  std::vector<std::vector<uint64_t>> rows;
  const gg::Blade b = upright(0.6f);
  for (const gg::Kind& k : gg::presets()) {
    for (const float w : {0.0f, 5.0f, 15.0f}) {
      const float air[2] = {w * 0.6f, -w * 0.8f};
      const gg::Shape s = gg::blade_shape(k, b, air, 2.25f);
      std::vector<uint64_t> row;
      for (const float* v : {s.v0, s.v1, s.v2}) {
        for (int i = 0; i < 3; ++i) row.push_back(ge::test::fbits(v[i]));
      }
      rows.push_back(row);
    }
  }
  ge::test::hold_grass_golden("tests/golden/grass_sway.txt", rows);
}
