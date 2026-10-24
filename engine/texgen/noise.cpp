// The noise kinds' octaves: the wrapped lattices, the gradient table, the cellular search, and
// the octave rules (fBm, ridged, turbulence) with the domain warp in front.
//
// ge:summary Tileable noise octaves: wrapped value and gradient lattices, cells, octave rules
// ge:keywords noise tileable perlin value-noise worley cellular ridged turbulence fbm warp
// ge:skill textures determinism
// ge:test tests/texgen_noise_test.cpp
#include "texgen/noise.hpp"

#include <algorithm>
#include <array>

namespace ge::texgen {
namespace {

constexpr std::array<const char*, kNoiseKindCount> kNames = {
    "value", "perlin", "cells", "edges", "voronoi", "ridged", "turbulence", "white"};

// Eight unit gradients: the axes and the diagonals.
constexpr double kS = 0.70710678118654752;
constexpr std::array<double, 16> kGrad = {1,  0,  -1,  0,  0,  1,   0,   -1,
                                          kS, kS, -kS, kS, kS, -kS, -kS, -kS};

double value_octave(double u, double v, int period, uint32_t seed) {
  const double gx = u * period, gy = v * period;
  const double ix = std::floor(gx), iy = std::floor(gy);
  const double fx = fade(gx - ix), fy = fade(gy - iy);
  const uint32_t x0 = wrap_cell(ix, period), x1 = wrap_cell(ix + 1, period);
  const uint32_t y0 = wrap_cell(iy, period), y1 = wrap_cell(iy + 1, period);
  const double a = hash01(x0, y0, seed), b = hash01(x1, y0, seed);
  const double c = hash01(x0, y1, seed), d = hash01(x1, y1, seed);
  return (a + (b - a) * fx) * (1.0 - fy) + (c + (d - c) * fx) * fy;
}

double grad_dot(uint32_t cx, uint32_t cy, uint32_t seed, double dx, double dy) {
  const size_t g = (hash_u32(cx, cy, seed) >> 13) & 7u;
  return kGrad[g * 2] * dx + kGrad[g * 2 + 1] * dy;
}

double perlin_octave(double u, double v, int period, uint32_t seed) {
  const double gx = u * period, gy = v * period;
  const double ix = std::floor(gx), iy = std::floor(gy);
  const double tx = gx - ix, ty = gy - iy;
  const double fx = fade(tx), fy = fade(ty);
  const uint32_t x0 = wrap_cell(ix, period), x1 = wrap_cell(ix + 1, period);
  const uint32_t y0 = wrap_cell(iy, period), y1 = wrap_cell(iy + 1, period);
  const double a = grad_dot(x0, y0, seed, tx, ty), b = grad_dot(x1, y0, seed, tx - 1, ty);
  const double c = grad_dot(x0, y1, seed, tx, ty - 1), d = grad_dot(x1, y1, seed, tx - 1, ty - 1);
  const double n = (a + (b - a) * fx) * (1.0 - fy) + (c + (d - c) * fx) * fy;
  // A unit-gradient lattice reaches about +-0.71; spread that over 0..1.
  return std::clamp(0.5 + n * 0.7071067811865476, 0.0, 1.0);
}

// The nearest and second-nearest feature points (one a cell, wrapped) and the nearest's cell.
struct Cellular {
  double f1, f2;
  uint32_t cx, cy;
};

Cellular cellular(double u, double v, int period, double jitter, uint32_t seed) {
  const double gx = u * period, gy = v * period;
  const double ix = std::floor(gx), iy = std::floor(gy);
  Cellular out{1e9, 1e9, 0, 0};
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      const uint32_t cx = wrap_cell(ix + dx, period), cy = wrap_cell(iy + dy, period);
      const double px = ix + dx + 0.5 + jitter * (hash01(cx, cy, seed) - 0.5);
      const double py = iy + dy + 0.5 + jitter * (hash01(cx, cy, seed ^ 0x9e3779b9u) - 0.5);
      const double d2 = (px - gx) * (px - gx) + (py - gy) * (py - gy);
      if (d2 < out.f1) {
        out.f2 = out.f1;
        out.f1 = d2;
        out.cx = cx;
        out.cy = cy;
      } else if (d2 < out.f2) {
        out.f2 = d2;
      }
    }
  }
  out.f1 = std::sqrt(out.f1);
  out.f2 = std::sqrt(out.f2);
  return out;
}

}  // namespace

const char* noise_kind_name(NoiseKind kind) {
  const auto i = static_cast<size_t>(kind);
  return i < kNames.size() ? kNames[i] : nullptr;
}

bool find_noise_kind(std::string_view name, NoiseKind& out) {
  for (size_t i = 0; i < kNames.size(); ++i) {
    if (name == kNames[i]) {
      out = static_cast<NoiseKind>(i);
      return true;
    }
  }
  return false;
}

double octave(NoiseKind kind, double u, double v, int period, double jitter, uint32_t seed) {
  switch (kind) {
    case NoiseKind::value:
      return value_octave(u, v, period, seed);
    case NoiseKind::cells:
      return std::min(1.0, cellular(u, v, period, jitter, seed).f1);
    case NoiseKind::edges: {
      const Cellular c = cellular(u, v, period, jitter, seed);
      return std::min(1.0, c.f2 - c.f1);
    }
    case NoiseKind::voronoi: {
      const Cellular c = cellular(u, v, period, jitter, seed);
      return hash01(c.cx, c.cy, seed + 77u);
    }
    case NoiseKind::white:
      return hash01(wrap_cell(std::floor(u * period), period),
                    wrap_cell(std::floor(v * period), period), seed);
    case NoiseKind::perlin:
    case NoiseKind::ridged:
    case NoiseKind::turbulence:
    case NoiseKind::count:
      break;
  }
  return perlin_octave(u, v, period, seed);
}

double sample(const NoiseSpec& spec, double u, double v) {
  if (spec.warp != 0) {
    // The push is itself tileable (a value fBm on the same lattice), so the warped field is.
    double wx = 0, wy = 0, norm = 0, amp = 1;
    for (int o = 0; o < 3; ++o) {
      const int p = std::max(1, spec.period) << o;
      wx += amp * value_octave(u, v, p, spec.seed + 1013u + o);
      wy += amp * value_octave(u, v, p, spec.seed + 2029u + o);
      norm += amp;
      amp *= 0.5;
    }
    u += spec.warp * (wx / norm - 0.5);
    v += spec.warp * (wy / norm - 0.5);
  }
  const int octaves = std::clamp(spec.octaves, 1, 10);
  double sum = 0, norm = 0, amp = 1;
  for (int o = 0; o < octaves; ++o) {
    const int p = std::clamp(spec.period, 1, 4096) << o;
    const uint32_t s = spec.seed + static_cast<uint32_t>(o) * 0x632be5abu;
    double n = octave(spec.kind, u, v, p, spec.jitter, s);
    if (spec.kind == NoiseKind::ridged) {
      n = 1.0 - std::fabs(2.0 * n - 1.0);
      n *= n;
    } else if (spec.kind == NoiseKind::turbulence) {
      n = std::fabs(2.0 * n - 1.0);
    }
    sum += amp * n;
    norm += amp;
    amp *= spec.gain;
  }
  return std::clamp(sum / norm, 0.0, 1.0);
}

}  // namespace ge::texgen
