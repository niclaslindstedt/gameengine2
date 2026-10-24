// engine/grass/cover.hpp: a cover grows its density, fades with its share, thins by rank as a
// strict subset, keeps every blade on its triangle, grows the same blades whatever order its
// triangles come in, takes a mesh's floors and not its walls, and keeps its bits in
// tests/golden/grass_cover.txt (rewrite it with `GE_GOLDEN_WRITE=1 build/debug/tests/
// grass_cover_test` for a deliberate move, with the `grass` lab before and after in the PR).
//
// ge:summary Test: grass covers and the blade scatter: density, share, rank, order, mesh; golden
// ge:keywords test grass cover scatter density share rank golden add_mesh cover_heightfield
// ge:skill grass testing
#include <algorithm>
#include <cmath>
#include <tuple>
#include <vector>

#include "check.hpp"
#include "core/heightfield.hpp"
#include "grass/cover.hpp"
#include "grass_golden.hpp"

namespace {

namespace gg = ge::grass;
using ge::test::grass_preset;

// A 20 m square of gentle slope at 1 m cells, `share` everywhere.
gg::Cover field_cover(const char* kind, float share) {
  gg::Cover cover;
  cover.kinds.push_back(grass_preset(kind));
  ge::core::Heightfield f = ge::core::create_heightfield(0, 0, 1, 41, 41);
  for (int r = 0; r < 41; ++r) {
    for (int c = 0; c < 41; ++c)
      f.data[static_cast<size_t>(r) * 41 + c] =
          0.1f * static_cast<float>(c) + 0.05f * static_cast<float>(r);
  }
  gg::cover_heightfield(cover, f, {5, 5, 25, 25}, 1.0,
                        [&](double, double) { return gg::CoverAt{0, share}; });
  return cover;
}

auto key(const gg::Blade& b) { return std::tuple(b.root[0], b.root[1], b.root[2], b.seed); }

}  // namespace

TEST(a_cover_grows_its_density) {
  const gg::Cover c = field_cover("meadow", 1);
  CHECK(c.tris.size() == 800);
  float area = 0;
  for (const auto& t : c.tris) area += gg::tri_area(t);
  const auto blades = gg::scatter_all(c);
  const double per_m2 = static_cast<double>(blades.size()) / area;
  CHECK_MSG(std::fabs(per_m2 / 400 - 1) < 0.02, "meadow density " + std::to_string(per_m2));
}

TEST(a_cover_fades_with_its_share) {
  const auto full = gg::scatter_all(field_cover("meadow", 1)).size();
  const auto half = gg::scatter_all(field_cover("meadow", 0.5f)).size();
  const double r = static_cast<double>(half) / static_cast<double>(full);
  CHECK_MSG(std::fabs(r - 0.5) < 0.03, "half share kept " + std::to_string(r));
}

TEST(thinning_by_rank_keeps_a_subset) {
  const gg::Cover c = field_cover("long", 1);
  std::vector<gg::Blade> all, quarter, half;
  for (size_t i = 0; i < c.tris.size(); ++i) {
    gg::scatter(c, i, all);
    gg::scatter(c, i, quarter, 0.25f);
    gg::scatter(c, i, half, 0.5f);
  }
  const double q = static_cast<double>(quarter.size()) / static_cast<double>(all.size());
  CHECK_MSG(std::fabs(q - 0.25) < 0.03, "quarter kept " + std::to_string(q));
  std::vector<std::tuple<float, float, float, uint32_t>> hk;
  hk.reserve(half.size());
  for (const auto& b : half) hk.push_back(key(b));
  std::sort(hk.begin(), hk.end());
  for (const auto& b : quarter) CHECK(std::binary_search(hk.begin(), hk.end(), key(b)));
}

TEST(every_blade_stands_on_its_triangle) {
  const gg::Cover c = field_cover("tussock", 1);
  const gg::Kind& k = c.kinds[0];
  for (size_t i = 0; i < c.tris.size(); i += 37) {
    std::vector<gg::Blade> bs;
    gg::scatter(c, i, bs);
    const gg::Tri& t = c.tris[i];
    for (const auto& b : bs) {
      // Inside the triangle's plan box and on its plane (the slope is planar per cell).
      CHECK(b.root[0] >= std::min({t.a[0], t.b[0], t.c[0]}) - 1e-4f);
      CHECK(b.root[0] <= std::max({t.a[0], t.b[0], t.c[0]}) + 1e-4f);
      CHECK(std::fabs(b.root[1] - (0.1f * b.root[0] + 0.05f * b.root[2])) < 1e-3f);
      CHECK(b.height >= k.height * (1 - k.height_jitter) * 0.75f - 1e-5f);
      CHECK(b.height <= k.height * (1 + k.height_jitter) * 1.25f + 1e-5f);
      CHECK(gg::kind_of(b) == 0);
      const float lean = std::sqrt(b.lean[0] * b.lean[0] + b.lean[1] * b.lean[1]);
      CHECK(lean <= k.lean + k.lean_jitter + 1e-5f);
    }
  }
}

TEST(the_same_ground_grows_the_same_blades_in_any_order) {
  gg::Cover a = field_cover("meadow", 1);
  gg::Cover b = a;
  std::reverse(b.tris.begin(), b.tris.end());
  auto ka = gg::scatter_all(a), kb = gg::scatter_all(b);
  CHECK(ka.size() == kb.size());
  std::vector<std::tuple<float, float, float, uint32_t>> sa, sb;
  for (const auto& x : ka) sa.push_back(key(x));
  for (const auto& x : kb) sb.push_back(key(x));
  std::sort(sa.begin(), sa.end());
  std::sort(sb.begin(), sb.end());
  CHECK(sa == sb);
  b.seed = 7;
  CHECK(gg::scatter_all(b).front().seed != gg::scatter_all(a).front().seed);
}

TEST(a_mesh_grows_on_its_floors_not_its_walls) {
  // A floor quad (2 triangles, wound either way) and a wall quad.
  const float p[] = {0, 0, 0, 4, 0, 0, 4, 0, 4, 0, 0, 4,   // floor
                     0, 0, 0, 4, 0, 0, 4, 3, 0, 0, 3, 0};  // wall at z = 0
  const uint32_t idx[] = {0, 1, 2, 0, 3, 2, 4, 5, 6, 4, 6, 7};
  gg::Cover c;
  c.kinds.push_back(grass_preset("lawn"));
  gg::add_mesh(c, p, idx, 0);
  CHECK(c.tris.size() >= 8);  // split to fit a triangle's budget
  float area = 0;
  for (const auto& t : c.tris) area += gg::tri_area(t);
  CHECK(std::fabs(area - 16) < 1e-3f);
  const auto blades = gg::scatter_all(c);
  CHECK(std::fabs(static_cast<double>(blades.size()) / (16 * 900) - 1) < 0.03);
  for (const auto& b : blades) CHECK(b.root[1] == 0);
}

TEST(the_scatter_keeps_its_bits) {
  const gg::Cover c = field_cover("tussock", 0.8f);
  std::vector<gg::Blade> bs;
  for (size_t i = 0; i < c.tris.size(); i += 97) gg::scatter(c, i, bs);
  std::vector<std::vector<uint64_t>> rows;
  for (size_t i = 0; i < bs.size(); i += 41) {
    const auto& b = bs[i];
    rows.push_back({ge::test::fbits(b.root[0]), ge::test::fbits(b.root[1]),
                    ge::test::fbits(b.root[2]), b.seed, ge::test::fbits(b.height),
                    ge::test::fbits(b.width), ge::test::fbits(b.lean[0]),
                    ge::test::fbits(b.lean[1])});
  }
  rows.push_back({bs.size()});
  ge::test::hold_grass_golden("tests/golden/grass_cover.txt", rows);
}
