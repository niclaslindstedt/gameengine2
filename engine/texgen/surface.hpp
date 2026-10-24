// THE SURFACE a recipe's layers paint: one float plane a property, `size` x `size`, row-major,
// y down (v = 0 is the tile's top edge, which is "up" on a wall). The planes are the colour
// (sRGB, 0..1), the height (0 deep .. 1 proud), the roughness, the metalness and the glow
// (sRGB colour times its brightness). Layers read and write them in order; bake.hpp turns the
// finished surface into the maps a renderer samples.
//
// The helpers every layer shares sit here too: the texel loop over workers, a wrapped blur, and
// the two readings of the height that make a texture "settle": the cavity (how far a texel sits
// below its neighbourhood: where grime, moss and glow pool) and the convexity (how far above:
// where edges wear), and the up-facing share (where dust and snow lie on a wall).
//
// ge:summary The surface layers paint (colour, height, roughness, metal, glow) and its helpers
// ge:keywords surface plane albedo height roughness metalness glow emissive blur cavity
// ge:keywords convexity up-facing for_texels texgen
// ge:skill textures
// ge:test tests/texgen_bake_test.cpp
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/parallel.hpp"

namespace ge::texgen {

enum Plane : uint8_t { kR, kG, kB, kHeight, kRough, kMetal, kGlowR, kGlowG, kGlowB, kPlaneCount };

struct Surface {
  int size = 0;
  std::array<std::vector<float>, kPlaneCount> plane;

  size_t at(int x, int y) const { return static_cast<size_t>(y) * size + x; }
  // A texel's index with x and y wrapped into the tile.
  size_t wrapped(int x, int y) const {
    x = ((x % size) + size) % size;
    y = ((y % size) + size) % size;
    return at(x, y);
  }
};

// A grey mid-height surface: colour 0.5, height 0.5, roughness 0.8, no metal, no glow.
Surface make_surface(int size);

// fn(x, y, u, v) for every texel, rows split over the worker pool. (u, v) is the texel's centre
// in tile coordinates. fn may write only its own texel.
template <class Fn>
void for_texels(int size, Fn&& fn) {
  const double inv = 1.0 / size;
  core::parallel_bands(static_cast<size_t>(size), [&](size_t begin, size_t end) {
    for (int y = static_cast<int>(begin); y < static_cast<int>(end); ++y) {
      const double v = (y + 0.5) * inv;
      for (int x = 0; x < size; ++x) fn(x, y, (x + 0.5) * inv, v);
    }
  });
}

// `in` blurred by a wrapped box of `radius` texels, twice (a tent), into a new plane.
std::vector<float> blur(const std::vector<float>& in, int size, int radius);

// blur(height) - height: positive in pits and grooves, negative on crests, about +-0.2.
std::vector<float> relief(const Surface& s, int radius);

// How much each texel faces up the tile (toward v = 0): the height rising as y grows, scaled so
// a 45 degree bevel at bump 1 reads about 1. Clamped to 0..1.
std::vector<float> up_facing(const Surface& s);

inline double clamp01(double x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }
inline double smoothstep(double a, double b, double x) {
  const double t = clamp01((x - a) / (b - a));
  return t * t * (3 - 2 * t);
}

}  // namespace ge::texgen
