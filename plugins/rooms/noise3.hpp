// SEEDED 3D VALUE NOISE for growing surfaces: a lattice hash on integers (no float in the hash, so
// no rounding to agree on), trilinear with the Hermite fade, summed in octaves. Floats, exact
// operations only (no transcendental, no fused multiply-add: the build's FP flags), so a room grows
// to the same bits on every machine.
//
// ge:summary Seeded 3D value noise and fbm on an integer lattice hash, float, bit-deterministic
// ge:keywords noise noise3 value-noise fbm octaves lattice hash3 3d seed procedural relief
// ge:skill rooms determinism
// ge:test tests/rooms_build_test.cpp
#pragma once

#include <cmath>
#include <cstdint>

namespace ge::rooms {

// A mixed 32-bit word from a lattice point and a seed.
inline uint32_t hash3(int32_t x, int32_t y, int32_t z, uint32_t seed) {
  uint32_t h = seed * 0x9e3779b9u + 0x7f4a7c15u;
  h ^= static_cast<uint32_t>(x) * 0x8da6b343u;
  h ^= static_cast<uint32_t>(y) * 0xd8163841u;
  h ^= static_cast<uint32_t>(z) * 0xcb1ab31fu;
  h ^= h >> 15;
  h *= 0x2c1b3c6du;
  h ^= h >> 12;
  h *= 0x297a2d39u;
  h ^= h >> 15;
  return h;
}

// Two seeds into one (a room's seed and a surface's index).
inline uint32_t mix_seed(uint32_t a, uint32_t b) { return hash3(int32_t(b), 0x51ed, 0x2709, a); }

inline float lattice3(int32_t x, int32_t y, int32_t z, uint32_t seed) {
  return static_cast<float>(hash3(x, y, z, seed) >> 8) * (1.0f / 16777216.0f);
}

// Value noise in [0, 1) at a point in lattice units.
inline float value_noise3(float x, float y, float z, uint32_t seed) {
  const float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
  const auto ix = static_cast<int32_t>(fx), iy = static_cast<int32_t>(fy),
             iz = static_cast<int32_t>(fz);
  float tx = x - fx, ty = y - fy, tz = z - fz;
  tx = tx * tx * (3.0f - 2.0f * tx);
  ty = ty * ty * (3.0f - 2.0f * ty);
  tz = tz * tz * (3.0f - 2.0f * tz);
  const float c000 = lattice3(ix, iy, iz, seed), c100 = lattice3(ix + 1, iy, iz, seed);
  const float c010 = lattice3(ix, iy + 1, iz, seed), c110 = lattice3(ix + 1, iy + 1, iz, seed);
  const float c001 = lattice3(ix, iy, iz + 1, seed), c101 = lattice3(ix + 1, iy, iz + 1, seed);
  const float c011 = lattice3(ix, iy + 1, iz + 1, seed);
  const float c111 = lattice3(ix + 1, iy + 1, iz + 1, seed);
  const float x00 = c000 + (c100 - c000) * tx, x10 = c010 + (c110 - c010) * tx;
  const float x01 = c001 + (c101 - c001) * tx, x11 = c011 + (c111 - c011) * tx;
  const float y0 = x00 + (x10 - x00) * ty, y1 = x01 + (x11 - x01) * ty;
  return y0 + (y1 - y0) * tz;
}

// `octaves` of value noise, each twice the frequency and half the weight of the last, in [0, 1).
// The point is in units of the largest feature.
inline float fbm3(float x, float y, float z, uint32_t seed, int octaves) {
  float sum = 0, weight = 0.5f, total = 0;
  for (int o = 0; o < octaves; ++o) {
    sum += value_noise3(x, y, z, seed + uint32_t(o) * 0x632be5abu) * weight;
    total += weight;
    weight *= 0.5f;
    x *= 2.0f;
    y *= 2.0f;
    z *= 2.0f;
  }
  return sum / total;
}

}  // namespace ge::rooms
