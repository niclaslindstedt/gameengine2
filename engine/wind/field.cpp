// The wind's field: the open air, the profile, the shelter and the sum at a point.
//
// ge:summary The wind as a pure function: definitions
// ge:keywords wind gust veer log-law shelter bilinear phase hash
// ge:skill water determinism
// ge:test tests/wind_field_test.cpp
#include "wind/field.hpp"

#include <cmath>

#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "core/num.hpp"
#include "wind/sources.hpp"

namespace ge::wind {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kTau = 2 * kPi;

// The default gusts: periods spread over the turbulence's energetic band, the longer ones carrying
// more (the spectrum's energy rises toward its peak at about a minute here).
struct Default {
  double period;  // s
  double weight;  // relative share
};
constexpr Default kDefaults[] = {{5.3, 0.55}, {9.7, 0.75}, {17.9, 1.0}, {31.3, 1.0}, {59.0, 0.9}};
constexpr double kVeerPeriod = 47;          // s
constexpr double kVeerPerIntensity = 1.13;  // rad of swing per unit of intensity: sigma 0.8 I

}  // namespace

double phase_of(uint32_t seed, uint32_t salt) {
  uint32_t h = (seed ^ (salt * 0x9e3779b1u)) * 0x85ebca77u;
  h ^= h >> 15;
  h *= 0xc2b2ae3du;
  h ^= h >> 13;
  return static_cast<double>(h) / 4294967296.0 * kTau;
}

double turbulence_for(double roughness, double height) {
  if (height <= roughness * 1.0001) return 0;
  return 1 / core::libm::log(height / roughness);
}

void lay_gusts(Field& field, uint32_t seed, double intensity) {
  double sum2 = 0;
  for (const Default& d : kDefaults) sum2 += d.weight * d.weight;
  // Independent sines of amplitudes s_i have a standard deviation sqrt(sum s_i^2 / 2).
  const double scale = intensity * std::sqrt(2 / sum2);
  field.gusts = 0;
  for (const Default& d : kDefaults) {
    const auto i = static_cast<uint32_t>(field.gusts);
    field.gust[field.gusts++] = {d.period, d.weight * scale, phase_of(seed, i + 1)};
  }
  field.veer = {kVeerPeriod, kVeerPerIntensity * intensity, phase_of(seed, 9)};
}

double profile_at(double roughness, double height) {
  if (height <= roughness) return 0;
  return core::libm::log(height / roughness) / core::libm::log(10 / roughness);
}

Air open_at(const Field& field, double t) {
  // This order is the bits' contract: a world's wind that moved onto this module kept its golden.
  double g = 0, total = 0;
  for (int i = 0; i < field.gusts; ++i) {
    const Swell& s = field.gust[i];
    g += s.share * core::libm::sin(kTau * t / s.period + s.phase);
    total += s.share;
  }
  Air out;
  const double speed = core::num::max(0.0, field.mean * (1 + g));
  const double veer =
      field.veer.period > 0
          ? field.veer.share * core::libm::sin(kTau * t / field.veer.period + field.veer.phase)
          : 0.0;
  const double from = field.from + veer;
  const double toward = (field.fold ? field.fold(from) : from) + kPi;
  out.x = core::libm::sin(toward) * speed;
  out.z = core::libm::cos(toward) * speed;
  out.speed = speed;
  out.gust = total > 0 ? 0.5 + 0.5 * g / total : 0.5;
  return out;
}

ShelterAt shelter_at(const Shelter& shelter, double x, double z) {
  ShelterAt out;
  if (shelter.nx <= 0 || shelter.nz <= 0) return out;
  const double fx = core::clamp((x - shelter.x0) / shelter.cell, 0, shelter.nx - 1);
  const double fz = core::clamp((z - shelter.z0) / shelter.cell, 0, shelter.nz - 1);
  const int i0 = static_cast<int>(fx), k0 = static_cast<int>(fz);
  const int i1 = i0 + 1 < shelter.nx ? i0 + 1 : i0, k1 = k0 + 1 < shelter.nz ? k0 + 1 : k0;
  const double u = fx - i0, v = fz - k0;
  const auto at = [&](const std::vector<float>& g) {
    const auto cell = [&](int i, int k) { return static_cast<double>(g[k * shelter.nx + i]); };
    return core::lerp(core::lerp(cell(i0, k0), cell(i1, k0), u),
                      core::lerp(cell(i0, k1), cell(i1, k1), u), v);
  };
  out.keep = at(shelter.keep);
  if (!shelter.turn.empty()) out.turn = at(shelter.turn);
  return out;
}

Air wind_at(const Field& field, const Where& where, double t, const Shelter* shelter,
            std::span<const Source> sources) {
  const Air open = open_at(field, t);
  double k = profile_at(field.roughness, where.above);
  double x = open.x, z = open.z;
  if (shelter) {
    const ShelterAt s = shelter_at(*shelter, where.x, where.z);
    k *= s.keep;
    if (s.turn != 0) {
      // Turned clockwise from above: a heading h becomes h + turn.
      const double c = core::libm::cos(s.turn), sn = core::libm::sin(s.turn);
      const double rx = x * c + z * sn, rz = z * c - x * sn;
      x = rx;
      z = rz;
    }
  }
  Air out;
  out.x = x * k;
  out.z = z * k;
  out.gust = open.gust;
  for (const Source& src : sources) {
    const Air a = source_at(src, where.x, where.y, where.z, t);
    out.x += a.x;
    out.y += a.y;
    out.z += a.z;
  }
  out.speed = std::sqrt(out.x * out.x + out.y * out.y + out.z * out.z);
  return out;
}

}  // namespace ge::wind
