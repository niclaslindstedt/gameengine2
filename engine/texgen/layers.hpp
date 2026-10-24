// THE LAYER KINDS a recipe builds a texture from, as one table: each kind's name, what it is
// for, its keys (type, default, range, the words it takes) and the function that paints it. The
// parser checks a recipe against this table, `ge texture kinds` prints it, and docs/textures.md
// points here rather than restating it.
//
// The kinds fall in three groups:
//   base     fill, noise, stripes                    a colour and a relief to start from
//   pattern  bricks, tiles, planks, panels           built structure: joints, bevels, rivets
//   life     cracks, scratches, dirt, dust, grime,   what time and the place do to a surface:
//            wear, rust, streaks, moss, snow, glow   each reads the height so far, so grime
//                                                    pools in the joints the bricks cut and
//                                                    dust lies on the ledges they raise
//
// ge:summary The layer kinds' table (names, keys, defaults, ranges) and the resolved arguments
// ge:keywords layer kinds schema keys defaults fill noise stripes bricks tiles planks panels
// ge:keywords cracks scratches dirt dust grime wear rust streaks moss snow glow layer_kinds
// ge:keywords find_layer_kind args texgen
// ge:skill textures
// ge:test tests/texgen_recipe_test.cpp tests/texgen_bake_test.cpp
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "texgen/recipe.hpp"
#include "texgen/surface.hpp"

namespace ge::texgen {

enum class KeyType : uint8_t { number, colour, word };

struct KeyDef {
  const char* name;
  KeyType type;
  double def = 0;          // number
  double lo = 0, hi = 1;   // number: the range a value is clamped to
  const char* text = "";   // colour: "#rrggbb"; word: the default word
  const char* words = "";  // word: the words it takes, space-separated ("" = any)
  const char* help = "";
};

// A layer's keys resolved for one bake: every key of its kind, in the table's order.
struct Args {
  std::span<const KeyDef> keys;
  std::vector<double> num;
  std::vector<Rgb> rgb;
  std::vector<std::string> word;
  uint32_t seed = 0;

  double n(std::string_view key) const;
  int i(std::string_view key) const;  // n(key) rounded
  Rgb c(std::string_view key) const;
  const std::string& w(std::string_view key) const;
  // Whether the recipe wrote this key (a colour a layer only uses when given, say).
  bool given(std::string_view key) const;
  std::vector<uint8_t> written;
};

using PaintFn = void (*)(Surface&, const Args&);

struct LayerKind {
  const char* name;
  const char* group;  // base, pattern or life
  const char* help;
  std::span<const KeyDef> keys;
  PaintFn paint;
};

std::span<const LayerKind> layer_kinds();
// The kind's index in layer_kinds(), or -1.
int find_layer_kind(std::string_view name);
const KeyDef* find_key(const LayerKind& kind, std::string_view key);

// The painters, by group (patterns.cpp, life.cpp, layers.cpp).
void paint_fill(Surface&, const Args&);
void paint_noise(Surface&, const Args&);
void paint_stripes(Surface&, const Args&);
void paint_bricks(Surface&, const Args&);
void paint_planks(Surface&, const Args&);
void paint_panels(Surface&, const Args&);
void paint_cracks(Surface&, const Args&);
void paint_scratches(Surface&, const Args&);
void paint_dirt(Surface&, const Args&);
void paint_dust(Surface&, const Args&);
void paint_grime(Surface&, const Args&);
void paint_wear(Surface&, const Args&);
void paint_rust(Surface&, const Args&);
void paint_streaks(Surface&, const Args&);
void paint_moss(Surface&, const Args&);
void paint_snow(Surface&, const Args&);
void paint_glow(Surface&, const Args&);

// The colour of a surface texel and the three planes' write, shared by the painters.
inline Rgb colour_at(const Surface& s, size_t i) {
  return {s.plane[kR][i], s.plane[kG][i], s.plane[kB][i]};
}
inline void set_colour(Surface& s, size_t i, Rgb c) {
  s.plane[kR][i] = static_cast<float>(clamp01(c.r));
  s.plane[kG][i] = static_cast<float>(clamp01(c.g));
  s.plane[kB][i] = static_cast<float>(clamp01(c.b));
}
inline Rgb mix(Rgb a, Rgb b, double t) {
  return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t)};
}
inline Rgb scaled(Rgb a, double k) { return {a.r * k, a.g * k, a.b * k}; }
// The plane moved toward `to` by `t`.
inline void pull(Surface& s, int plane, size_t i, double to, double t) {
  s.plane[plane][i] = static_cast<float>(lerp(s.plane[plane][i], to, clamp01(t)));
}

}  // namespace ge::texgen
