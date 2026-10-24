// engine/texgen/bake.hpp: a bake is a pure function of the recipe and the reference (the same
// bytes twice; a new seed or param moves them; the key follows), every library recipe tiles,
// the disk cache writes once and then reuses, and every library recipe holds its digest at 64 px
// against tests/golden/texgen_bake.txt (row: recipe digest). A deliberate change rewrites the
// table: `GE_GOLDEN_WRITE=1 build/<preset>/tests/texgen_bake_test`, and the PR says why.
//
// ge:summary Tests the bake: determinism, variants, tiling, the disk cache, a digest golden
// ge:keywords test texgen bake determinism digest golden tiling seam cache disk
// ge:skill testing textures
#include "texgen/bake.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "golden.hpp"
#include "texgen/library.hpp"
#include "texgen/preview.hpp"

namespace tg = ge::texgen;

namespace {

constexpr const char* kGolden = "tests/golden/texgen_bake.txt";

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

tg::Recipe library(const std::string& name, int size) {
  tg::ParseResult r = tg::load_recipe(ge::test::root_path(tg::kLibraryDir) + "/" + name + ".tex");
  r.recipe.size = size;
  return r.recipe;
}

// The colour's step across the wrapped edge over the largest step across any inner boundary.
double seam(const tg::Maps& m) {
  const int s = m.size;
  auto lum = [&](int x, int y) {
    const size_t o = (static_cast<size_t>(y) * s + x) * 4;
    return 0.3 * m.rgba[0][o] + 0.59 * m.rgba[0][o + 1] + 0.11 * m.rgba[0][o + 2];
  };
  double edge = 0, inner = 1e-9;
  for (int b = 0; b < s; ++b) {
    double sx = 0, sy = 0;
    for (int k = 0; k < s; ++k) {
      sx += std::fabs(lum((b + s - 1) % s, k) - lum(b, k));
      sy += std::fabs(lum(k, (b + s - 1) % s) - lum(k, b));
    }
    (b == 0 ? edge : inner) = std::max(b == 0 ? edge : inner, std::max(sx, sy) / s);
  }
  return edge / inner;
}

}  // namespace

TEST(a_bake_is_a_pure_function) {
  const tg::Recipe r = library("brick_wall", 64);
  const tg::TextureRef ref{"brick_wall", 5, {{"wear", 0.6}}};
  const uint64_t a = tg::maps_digest(tg::bake(r, ref).maps);
  CHECK(a == tg::maps_digest(tg::bake(r, ref).maps));
  CHECK(a != tg::maps_digest(tg::bake(r, {"brick_wall", 6, {{"wear", 0.6}}}).maps));
  CHECK(a != tg::maps_digest(tg::bake(r, {"brick_wall", 5, {{"wear", 0.9}}}).maps));
  CHECK(tg::bake_key(r, ref) == tg::bake_key(r, ref));
  CHECK(tg::bake_key(r, ref) != tg::bake_key(r, {"brick_wall", 6, {{"wear", 0.6}}}));
  CHECK(tg::bake_key(r, ref) != tg::bake_key(r, {"brick_wall", 5, {}}));
}

TEST(an_unknown_param_is_an_error_not_a_bake) {
  const tg::Recipe r = library("brick_wall", 16);
  const tg::BakeResult b = tg::bake(r, {"brick_wall", std::nullopt, {{"age", 1}}});
  CHECK(!b.ok());
}

TEST(every_library_recipe_tiles) {
  for (const std::string& f : tg::list_recipes(ge::test::root_path(tg::kLibraryDir))) {
    tg::ParseResult r = tg::load_recipe(f);
    r.recipe.size = 128;
    const double s = seam(tg::bake(r.recipe, {}).maps);
    CHECK_MSG(s <= 1.2, f + " shows a seam (" + std::to_string(s) +
                            "); run `make lab LAB=texgen-specimens` and look at LIT 2X2");
  }
}

TEST(the_disk_cache_writes_once_then_reuses) {
  const std::string dir = (std::filesystem::temp_directory_path() / "ge_texgen_test").string();
  std::filesystem::remove_all(dir);
  const tg::Recipe r = library("lava_rock", 32);
  const tg::DiskResult first = tg::bake_to_disk(r, {"lava_rock", 3, {}}, dir);
  CHECK_MSG(first.ok(), tg::format_diagnostics(first.errors));
  CHECK(!first.reused);
  CHECK(first.files.size() == 6);  // five maps (it glows) and the meta
  for (const std::string& f : first.files) CHECK(std::filesystem::exists(f));
  const tg::DiskResult second = tg::bake_to_disk(r, {"lava_rock", 3, {}}, dir);
  CHECK(second.ok() && second.reused && second.files == first.files);
  const tg::DiskResult other = tg::bake_to_disk(r, {"lava_rock", 4, {}}, dir);
  CHECK(!other.reused && other.files != first.files);
  // A texture that does not glow writes no glow map.
  const tg::DiskResult plain = tg::bake_to_disk(library("concrete", 32), {}, dir);
  CHECK(plain.ok() && plain.files.size() == 5);
  std::filesystem::remove_all(dir);
}

TEST(the_preview_draws_tiles) {
  const tg::Maps m = tg::bake(library("bathroom_tiles", 32), {}).maps;
  const ge::lab::Picture p = tg::lit_preview(m, 64, 2);
  CHECK(p.width == 64 && p.rgba.size() == size_t{64} * 64 * 4);
  // Two tiles across: the left half repeats as the right half.
  CHECK(std::equal(p.rgba.begin(), p.rgba.begin() + 128, p.rgba.begin() + 128));
}

TEST(library_digests_match_golden) {
  std::string got;
  for (const std::string& f : tg::list_recipes(ge::test::root_path(tg::kLibraryDir))) {
    tg::ParseResult r = tg::load_recipe(f);
    r.recipe.size = 64;
    got += r.recipe.name + " " + hex(tg::maps_digest(tg::bake(r.recipe, {}).maps)) + "\n";
  }
  if (env_set("GE_GOLDEN_WRITE")) {
    std::ofstream(ge::test::root_path(kGolden)) << got;
    return;
  }
  std::string want;
  for (const auto& row : ge::test::golden_words(kGolden)) want += row[0] + " " + row[1] + "\n";
  CHECK_MSG(got == want, std::string(kGolden) +
                             " moved: a library texture baked other bytes. Look at it in `make lab "
                             "LAB=texgen-specimens`; if the change was meant, rerun with "
                             "GE_GOLDEN_WRITE=1 and say why in the PR");
}
