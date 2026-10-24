// engine/texgen/noise.hpp: every kind tiles (periodic at u = 1 and v = 1), stays in 0..1, and
// holds its bits against tests/golden/texgen_noise.txt.
// Row: kind period octaves warp(bits) u(bits) v(bits) | value(bits). A deliberate change
// rewrites the table: `GE_GOLDEN_WRITE=1 build/<preset>/tests/texgen_noise_test`, and the PR
// says why.
//
// ge:summary Golden test: texture noise kinds tile, stay in range and hold their bits
// ge:keywords test golden texgen noise tileable periodic range
// ge:skill testing textures
#include "texgen/noise.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "golden.hpp"

namespace tg = ge::texgen;

namespace {

constexpr const char* kGolden = "tests/golden/texgen_noise.txt";

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

std::string hex(uint64_t v) {
  char buf[17];
  std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
  return buf;
}

}  // namespace

TEST(every_kind_is_periodic_and_in_range) {
  for (int k = 0; k < tg::kNoiseKindCount; ++k) {
    for (int octaves : {1, 4}) {
      const tg::NoiseSpec spec{static_cast<tg::NoiseKind>(k), 3, octaves, 0.5, 1, 0, 11};
      for (int t = 0; t < 50; ++t) {
        const double w = (t + 0.37) / 50;
        const double a = tg::sample(spec, 0, w), b = tg::sample(spec, 1, w);
        const double c = tg::sample(spec, w, 0), d = tg::sample(spec, w, 1);
        CHECK_MSG(a == b && c == d, std::string(tg::noise_kind_name(spec.kind)) +
                                        " is not periodic at the tile's edge; a lattice index is "
                                        "hashed unwrapped (wrap_cell, engine/texgen/noise.cpp)");
        CHECK(a >= 0 && a <= 1 && c >= 0 && c <= 1);
      }
    }
  }
}

TEST(names_round_trip) {
  for (int k = 0; k < tg::kNoiseKindCount; ++k) {
    tg::NoiseKind out{};
    CHECK(tg::find_noise_kind(tg::noise_kind_name(static_cast<tg::NoiseKind>(k)), out));
    CHECK(static_cast<int>(out) == k);
  }
  tg::NoiseKind out{};
  CHECK(!tg::find_noise_kind("simplex", out));
}

TEST(noise_matches_golden_bit_for_bit) {
  std::string got;
  for (int k = 0; k < tg::kNoiseKindCount; ++k) {
    for (int c = 0; c < 3; ++c) {
      const int period = 2 + 3 * c, octaves = 1 + 2 * c;
      const double warp = c == 2 ? 0.4 : 0;
      const tg::NoiseSpec spec{static_cast<tg::NoiseKind>(k), period, octaves, 0.5, 1, warp,
                               static_cast<uint32_t>(100 + k)};
      for (int t = 0; t < 12; ++t) {
        const double u = tg::hash01(t, 1, 5), v = tg::hash01(t, 2, 5);
        got += hex(k) + " " + hex(period) + " " + hex(octaves) + " " + hex(ge::test::bits(warp)) +
               " " + hex(ge::test::bits(u)) + " " + hex(ge::test::bits(v)) + " " +
               hex(ge::test::bits(tg::sample(spec, u, v))) + "\n";
      }
    }
  }
  if (env_set("GE_GOLDEN_WRITE")) {
    std::ofstream(ge::test::root_path(kGolden)) << got;
    return;
  }
  std::string want;
  for (const auto& row : ge::test::golden_words(kGolden)) {
    std::string line;
    for (const auto& w : row) line += (line.empty() ? "" : " ") + w;
    want += line + "\n";
  }
  CHECK_MSG(got == want, std::string(kGolden) +
                             " moved: a noise kind changed its bits. If that was meant, rerun with "
                             "GE_GOLDEN_WRITE=1 and say why in the PR");
}
