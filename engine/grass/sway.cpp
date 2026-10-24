// The wind grid, the ruffle, the bend and the blade's Bézier. `wind_at_blade` and `blade_shape`
// are written in render/grass.wgsl's order, float for float.
//
// ge:summary Grass sway definitions: the wind grid, wind_at_blade, bend_of, blade_shape
// ge:keywords grass sway wind-grid ruffle bend bezier length-correction
// ge:skill grass
// ge:test tests/grass_sway_test.cpp
#include "grass/sway.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/libm/libm.hpp"

namespace ge::grass {

namespace {

namespace lm = core::libm;
constexpr float kTau = 6.28318530717958647f;

uint32_t as_bits(int32_t v) {
  uint32_t u;
  std::memcpy(&u, &v, sizeof u);
  return u;
}

float lattice(float ix, float iz) {
  return unit(hash(hash(as_bits(static_cast<int32_t>(ix))) ^ as_bits(static_cast<int32_t>(iz))));
}

// Value noise in [0, 1) over a unit lattice, smoothstepped.
float value_noise(float x, float z) {
  const float ix = std::floor(x), iz = std::floor(z);
  float fx = x - ix, fz = z - iz;
  fx = fx * fx * (3.0f - 2.0f * fx);
  fz = fz * fz * (3.0f - 2.0f * fz);
  const float a = lattice(ix, iz), b = lattice(ix + 1.0f, iz);
  const float c = lattice(ix, iz + 1.0f), d = lattice(ix + 1.0f, iz + 1.0f);
  const float top = a + (b - a) * fx, bottom = c + (d - c) * fx;
  return top + (bottom - top) * fz;
}

float length3(const float a[3], const float b[3]) {
  const float x = b[0] - a[0], y = b[1] - a[1], z = b[2] - a[2];
  return std::sqrt(x * x + y * y + z * z);
}

}  // namespace

float bend_of(const Kind& kind, float speed) {
  const float w2 = speed * speed;
  return w2 / (w2 + kind.give * kind.give);
}

void fill_wind_grid(WindGrid& grid, const GrassAir& air, double cx, double cz, double span, int n,
                    double t) {
  n = std::clamp(n, 2, kWindGridMax);
  const double cell = span / (n - 1);
  const double x0 = std::floor((cx - span * 0.5) / cell) * cell;
  const double z0 = std::floor((cz - span * 0.5) / cell) * cell;
  grid.x0 = static_cast<float>(x0);
  grid.z0 = static_cast<float>(z0);
  grid.cell = static_cast<float>(cell);
  grid.n = n;
  grid.air.assign(static_cast<size_t>(n) * static_cast<size_t>(n) * 2, 0.0f);
  grid.mean[0] = grid.mean[1] = 0;
  if (!air.field) return;
  const wind::Field& field = *air.field;
  // The way the mean blows TO (heading 0 = +z, clockwise from above).
  const double to_x = -lm::sin(field.from), to_z = -lm::cos(field.from);
  const double carry = std::max(field.mean, 0.5);
  const double at_height = field.mean * wind::profile_at(field.roughness, air.height);
  grid.mean[0] = static_cast<float>(to_x * at_height);
  grid.mean[1] = static_cast<float>(to_z * at_height);
  for (int k = 0; k < n; ++k) {
    for (int i = 0; i < n; ++i) {
      const double x = x0 + i * cell, z = z0 + k * cell;
      const double ground = air.ground ? core::sample_field(*air.ground, x, z) : 0.0;
      const wind::Where where{x, ground + air.height, z, air.height};
      // The gusts carried downwind: this node meets the air the upwind ground had earlier.
      const double late = t - (x * to_x + z * to_z) / carry;
      wind::Air a = wind::wind_at(field, where, late, air.shelter);
      for (const wind::Source& s : air.sources) {
        const wind::Air extra = wind::source_at(s, where.x, where.y, where.z, t);
        a.x += extra.x;
        a.z += extra.z;
      }
      const size_t c =
          (static_cast<size_t>(k) * static_cast<size_t>(n) + static_cast<size_t>(i)) * 2;
      grid.air[c] = static_cast<float>(a.x);
      grid.air[c + 1] = static_cast<float>(a.z);
    }
  }
}

void wind_at_blade(const WindGrid& grid, float x, float z, float t, float out[2]) {
  out[0] = out[1] = 0;
  if (grid.n < 2) return;
  const float top = static_cast<float>(grid.n - 1);
  const float gx = std::clamp((x - grid.x0) / grid.cell, 0.0f, top);
  const float gz = std::clamp((z - grid.z0) / grid.cell, 0.0f, top);
  const float ix = std::min(std::floor(gx), top - 1.0f), iz = std::min(std::floor(gz), top - 1.0f);
  const float fx = gx - ix, fz = gz - iz;
  const size_t n = static_cast<size_t>(grid.n);
  const size_t c00 = (static_cast<size_t>(iz) * n + static_cast<size_t>(ix)) * 2;
  const size_t c10 = c00 + 2, c01 = c00 + n * 2, c11 = c01 + 2;
  const float ruffle = 0.7f + 0.6f * value_noise((x - grid.mean[0] * t) / kRuffleCell,
                                                 (z - grid.mean[1] * t) / kRuffleCell);
  for (size_t k = 0; k < 2; ++k) {
    const float a = grid.air[c00 + k] + (grid.air[c10 + k] - grid.air[c00 + k]) * fx;
    const float b = grid.air[c01 + k] + (grid.air[c11 + k] - grid.air[c01 + k]) * fx;
    out[k] = (a + (b - a) * fz) * ruffle;
  }
}

Shape blade_shape(const Kind& kind, const Blade& blade, const float wind[2], float t) {
  const float h = blade.height;
  const uint32_t hp = hash(blade.seed ^ 0x27d4eb2fu);
  const float phase = unit(hp) * kTau;
  const float freq = kind.sway * (0.85f + 0.3f * unit(hash(hp)));
  const float w = std::sqrt(wind[0] * wind[0] + wind[1] * wind[1]);
  const float b = bend_of(kind, w);
  // The sway's axis: the wind's bearing, or the blade's own lean in a calm.
  float dx = 0, dz = 0, ox = 1, oz = 0;
  if (w > 1e-4f) {
    dx = wind[0] / w;
    dz = wind[1] / w;
    ox = dx;
    oz = dz;
  } else {
    const float l = std::sqrt(blade.lean[0] * blade.lean[0] + blade.lean[1] * blade.lean[1]);
    if (l > 1e-6f) {
      ox = blade.lean[0] / l;
      oz = blade.lean[1] / l;
    }
  }
  const float osc = static_cast<float>(lm::sin(static_cast<double>(kTau * freq * t + phase)));
  const float osc2 =
      static_cast<float>(lm::sin(static_cast<double>(kTau * freq * 1.37f * t + phase * 1.7f)));
  const float amp = 0.04f + 0.6f * b * (1.0f - b);
  const float ax = blade.lean[0] * (1.0f - b) + dx * (b * kMaxBend) + ox * (osc * amp) -
                   oz * (osc2 * amp * 0.35f);
  const float az = blade.lean[1] * (1.0f - b) + dz * (b * kMaxBend) + oz * (osc * amp) +
                   ox * (osc2 * amp * 0.35f);
  const float al = std::sqrt(ax * ax + az * az);
  const float theta = std::min(al, kMaxBend + 0.1f);
  float ux = 1, uz = 0;
  if (al > 1e-6f) {
    ux = ax / al;
    uz = az / al;
  }
  const float st = static_cast<float>(lm::sin(static_cast<double>(theta)));
  const float ct = static_cast<float>(lm::cos(static_cast<double>(theta)));

  Shape s{};
  std::memcpy(s.v0, blade.root, sizeof s.v0);
  s.v2[0] = s.v0[0] + h * ux * st;
  s.v2[1] = s.v0[1] + h * ct;
  s.v2[2] = s.v0[2] + h * uz * st;
  s.v1[0] = s.v0[0];
  s.v1[1] = s.v0[1] + h * std::max(1.0f - st, 0.05f);
  s.v1[2] = s.v0[2];
  // Keep the curve's length: the Bézier's length lies between the chord and the guide polygon.
  const float chord = length3(s.v0, s.v2);
  const float poly = length3(s.v0, s.v1) + length3(s.v1, s.v2);
  const float r = h / ((2.0f * chord + poly) / 3.0f);
  float v1[3], v2[3];
  for (int k = 0; k < 3; ++k) {
    v1[k] = s.v0[k] + r * (s.v1[k] - s.v0[k]);
    v2[k] = v1[k] + r * (s.v2[k] - s.v1[k]);
  }
  std::memcpy(s.v1, v1, sizeof v1);
  std::memcpy(s.v2, v2, sizeof v2);
  s.width = blade.width;
  return s;
}

void bezier_point(const Shape& s, float u, float out[3]) {
  const float a = (1.0f - u) * (1.0f - u), b = 2.0f * u * (1.0f - u), c = u * u;
  for (int k = 0; k < 3; ++k) out[k] = a * s.v0[k] + b * s.v1[k] + c * s.v2[k];
}

}  // namespace ge::grass
