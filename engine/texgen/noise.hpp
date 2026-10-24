// TILEABLE NOISE for textures. Every function here is periodic over the unit square: (u, v) in
// [0, 1) are tile coordinates, `period` is how many lattice cells span the tile, and a lattice
// index is wrapped modulo the period before it is hashed, so the noise at u = 1 is the noise at
// u = 0 and a texture built from it repeats without a seam at any size. Octaves double the
// period, which keeps it an integer and keeps them tiling.
//
// The kinds: value (bilinear-quintic lattice values), perlin (gradient noise), cells (the
// distance to the nearest feature point, F1), edges (F2 - F1: thin lines where two cells meet,
// cracks and flagstone joints), voronoi (one flat value a cell, flagstones and scales), ridged
// (folded octaves, sharp crests: rock), turbulence (folded without the crest flip: smoke, rust),
// white (a value a texel). Each returns 0..1.
//
// Bits: integer hashing, no transcendental, plain IEEE arithmetic, so the same inputs give the
// same doubles on every compiler (tests/texgen_noise_test.cpp, tests/golden/texgen_noise.txt).
//
// ge:summary Tileable noise for textures: value, perlin, cells, edges, voronoi, ridged, fbm
// ge:keywords noise tileable seamless periodic value-noise perlin gradient-noise worley
// ge:keywords cellular voronoi edges ridged turbulence fbm octaves domain-warp white-noise hash
// ge:keywords texgen noise_kind sample hash_u32
// ge:skill textures determinism
// ge:test tests/texgen_noise_test.cpp
#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

namespace ge::texgen {

// A 32-bit integer hash of a lattice point and a seed (three rounds of xor-shift-multiply).
inline uint32_t hash_u32(uint32_t x, uint32_t y, uint32_t seed) {
  uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ seed * 0xcb1ab31fu;
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  h ^= h >> 16;
  return h;
}

// The hash as a double in [0, 1): its top 24 bits.
inline double hash01(uint32_t x, uint32_t y, uint32_t seed) {
  return static_cast<double>(hash_u32(x, y, seed) >> 8) * (1.0 / 16777216.0);
}

// A lattice index wrapped into [0, period).
inline uint32_t wrap_cell(double i, int period) {
  const double p = period;
  return static_cast<uint32_t>(i - std::floor(i / p) * p);
}

// Quintic fade: zero first and second derivative at the cell's edges, so no grid shows.
inline double fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

enum class NoiseKind : uint8_t {
  value,
  perlin,
  cells,
  edges,
  voronoi,
  ridged,
  turbulence,
  white,
  count,
};
inline constexpr int kNoiseKindCount = static_cast<int>(NoiseKind::count);

// "value", "perlin", ...; nullptr past the end.
const char* noise_kind_name(NoiseKind kind);
// The kind a name spells; false when no kind does.
bool find_noise_kind(std::string_view name, NoiseKind& out);

// One noise field: what `sample` reads.
struct NoiseSpec {
  NoiseKind kind = NoiseKind::value;
  int period = 8;     // lattice cells across the tile, the first octave (1..4096)
  int octaves = 1;    // 1..10; each doubles the period
  double gain = 0.5;  // each octave's weight against the one before
  double jitter = 1;  // cells, edges, voronoi: how far a feature point strays in its cell, 0..1
  double warp = 0;    // domain warp: the tile coordinates pushed by a value fBm, in tile units
  uint32_t seed = 0;
};

// One octave of `kind` at a period, 0..1. ridged and turbulence are octave rules and read as
// perlin here.
double octave(NoiseKind kind, double u, double v, int period, double jitter, uint32_t seed);

// The field at tile coordinates (u, v): its octaves summed and normalised to 0..1, after the
// warp. A pure function of its arguments.
double sample(const NoiseSpec& spec, double u, double v);

}  // namespace ge::texgen
