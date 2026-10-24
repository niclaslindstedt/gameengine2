// engine/wind/field.hpp: the log law is 1 at 10 m and 0 at the roughness; the default gusts give
// the asked intensity over a long series and stay within their band; the veer swings the bearing
// by its share either way; a fold is applied to the veered bearing; the shelter grid keeps and
// turns the wind bilinearly; and every number keeps its bits in tests/golden/wind_field.txt.
//
// A deliberate change to the wind moves the golden: rewrite it with
// `GE_GOLDEN_WRITE=1 build/release/tests/wind_field_test`, and put the `wind` lab's table before
// and after in the PR. The mountain's wind (tests/mountain_air_test.cpp) must not move with it.
//
// ge:summary Test: the wind's field: log law, gust intensity, veer, fold, shelter; golden
// ge:keywords test wind field gust veer intensity log-law roughness shelter fold golden
// ge:skill water testing
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "golden.hpp"
#include "wind/field.hpp"

namespace {

namespace gw = ge::wind;

constexpr const char* kGolden = "tests/golden/wind_field.txt";

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

gw::Field gusty(double mean, double from, uint32_t seed, double intensity) {
  gw::Field f;
  f.mean = mean;
  f.from = from;
  gw::lay_gusts(f, seed, intensity);
  return f;
}

double mirror(double from) { return from > 1 ? 2 - from : from; }

}  // namespace

TEST(the_log_law_is_one_at_ten_metres_and_nothing_at_the_roughness) {
  for (const double z0 : {gw::kRoughness::water, gw::kRoughness::snow, gw::kRoughness::grass,
                          gw::kRoughness::forest}) {
    CHECK(std::fabs(gw::profile_at(z0, 10) - 1) < 1e-12);
    CHECK(gw::profile_at(z0, z0) == 0);
    CHECK(gw::profile_at(z0, z0 * 0.5) == 0);
    CHECK(gw::profile_at(z0, 2) < 1 && gw::profile_at(z0, 50) > 1);
  }
  // Rougher ground slows the air near it more: at 2 m a forest keeps less than open water.
  CHECK(gw::profile_at(gw::kRoughness::forest, 2) < gw::profile_at(gw::kRoughness::water, 2));
  // The intensity the log law gives: about 0.09 over the sea, 0.17 over grass, at 10 m.
  CHECK(std::fabs(gw::turbulence_for(gw::kRoughness::water) - 0.0924) < 0.001);
  CHECK(std::fabs(gw::turbulence_for(gw::kRoughness::grass) - 0.1721) < 0.001);
}

TEST(the_default_gusts_give_the_asked_intensity) {
  for (const double intensity : {0.08, 0.15, 0.25}) {
    const gw::Field f = gusty(8, 0.7, 38, intensity);
    double sum = 0, sum2 = 0, lo = 1e9, hi = 0;
    const int n = 200000;
    for (int k = 0; k < n; ++k) {
      const gw::Air a = gw::open_at(f, k * 0.1);
      sum += a.speed;
      sum2 += a.speed * a.speed;
      lo = std::fmin(lo, a.speed);
      hi = std::fmax(hi, a.speed);
      CHECK(a.gust >= 0 && a.gust <= 1);
    }
    const double mean = sum / n, sd = std::sqrt(sum2 / n - mean * mean);
    CHECK_MSG(std::fabs(mean - 8) < 0.08, "mean " + std::to_string(mean));
    CHECK_MSG(std::fabs(sd / mean - intensity) < 0.05 * intensity,
              "intensity " + std::to_string(sd / mean) + " asked " + std::to_string(intensity));
    CHECK(lo > 0 && hi < 8 * (1 + 4 * intensity));
  }
}

TEST(the_veer_swings_by_its_share_and_a_fold_is_applied_after_it) {
  gw::Field f;
  f.mean = 5;
  f.from = 0.4;
  f.veer = {30, 0.2, 0};
  double lo = 9, hi = -9;
  for (int k = 0; k < 3000; ++k) {
    const gw::Air a = gw::open_at(f, k * 0.01);
    // Blowing FROM `from` means moving toward from + pi: the bearing it comes from is atan2(-x,-z).
    const double from = std::atan2(-a.x, -a.z);
    lo = std::fmin(lo, from);
    hi = std::fmax(hi, from);
    CHECK(std::fabs(a.speed - 5) < 1e-12);
  }
  CHECK(std::fabs(lo - 0.2) < 1e-3 && std::fabs(hi - 0.6) < 1e-3);
  f.fold = mirror;
  f.from = 1.5;
  f.veer = {};
  const gw::Air a = gw::open_at(f, 0);
  CHECK(std::fabs(std::atan2(-a.x, -a.z) - 0.5) < 1e-12);
}

TEST(the_shelter_keeps_and_turns_the_wind) {
  gw::Field f;
  f.mean = 10;
  f.from = 3.141592653589793;  // from the south: blowing toward +z
  f.roughness = gw::kRoughness::grass;
  gw::Shelter s;
  s.x0 = 0;
  s.z0 = 0;
  s.cell = 10;
  s.nx = 2;
  s.nz = 1;
  s.keep = {1.0f, 0.5f};
  s.turn = {0.0f, 0.5f};
  const gw::Air open = gw::wind_at(f, {0, 0, 0, 10}, 0, &s);
  CHECK(std::fabs(open.z - 10) < 1e-9 && std::fabs(open.x) < 1e-9);
  const gw::Air lee = gw::wind_at(f, {10, 0, 0, 10}, 0, &s);
  CHECK(std::fabs(lee.speed - 5) < 1e-6);
  CHECK(std::fabs(std::atan2(lee.x, lee.z) - 0.5) < 1e-6);  // turned clockwise from above
  const gw::Air half = gw::wind_at(f, {5, 0, 0, 10}, 0, &s);
  CHECK(std::fabs(half.speed - 7.5) < 1e-6);
  // Off the grid the edge is read; under the roughness there is no wind.
  CHECK(std::fabs(gw::wind_at(f, {99, 0, -40, 10}, 0, &s).speed - 5) < 1e-6);
  CHECK(gw::wind_at(f, {0, 0, 0, 0.01}, 0, &s).speed == 0);
}

TEST(every_number_keeps_its_golden) {
  const bool write = env_set("GE_GOLDEN_WRITE");
  const auto want = ge::test::golden_rows(kGolden);
  if (!write) CHECK_MSG(!want.empty(), std::string(kGolden) + " is empty; see the header");
  std::vector<std::vector<uint64_t>> got;
  const gw::Field fields[] = {gusty(6, 1.2, 38, 0.15), gusty(15, 4.0, 7, 0.22),
                              gusty(3, 5.5, 1234, 0.09)};
  for (const gw::Field& f : fields) {
    for (const double t : {0.0, 9.25, 61.5, 487.0}) {
      const gw::Air a = gw::open_at(f, t);
      const gw::Air b = gw::wind_at(f, {3, 1, 4, 2.5}, t);
      got.push_back({ge::test::bits(a.x), ge::test::bits(a.z), ge::test::bits(a.speed),
                     ge::test::bits(a.gust), ge::test::bits(b.x), ge::test::bits(b.z)});
    }
  }
  if (write) {
    std::ofstream out(ge::test::root_path(kGolden));
    out << "# per field and moment: open x, z, speed, gust; at 2.5 m x, z (wind_field_test)\n";
    for (const auto& row : got) {
      std::string line;
      for (const uint64_t word : row) {
        char b[24];
        std::snprintf(b, sizeof b, "%llx", static_cast<unsigned long long>(word));
        line += (line.empty() ? "" : " ") + std::string(b);
      }
      out << line << "\n";
    }
    return;
  }
  CHECK_MSG(got == want, "the wind moved; if the move is meant, rewrite " + std::string(kGolden) +
                             " with GE_GOLDEN_WRITE=1 on the release build and put the wind lab's "
                             "table before and after in the PR (skill: water)");
}
