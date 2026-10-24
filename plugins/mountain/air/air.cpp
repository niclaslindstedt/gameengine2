// The wind's gusts and veer, the fall's squalls, and the visual range through it.
//
// ge:summary The wind and the falling snow at a moment: definitions
// ge:keywords air wind gust veer fall squall visibility phase hash field_of
// ge:skill mountain
// ge:test tests/mountain_air_test.cpp
#include "mountain/air/air.hpp"

#include <algorithm>
#include <cstdint>

#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "core/num.hpp"
#include "mountain/mapgen/weather.hpp"

namespace ge::mountain::air {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kTau = 2 * kPi;

struct Swell {
  double period;  // s
  double share;   // of the mean
};

constexpr Swell kGusts[] = {{6.3, 0.14}, {17.9, 0.2}, {53, 0.16}};
constexpr double kVeer = 0.22;      // rad either way
constexpr double kVeerPeriod = 41;  // s
constexpr Swell kSqualls[] = {{37, 0.35}, {113, 0.4}, {347, 0.25}};
constexpr double kBlown = 0.3;         // a storm's gust's share lifted off the ground
constexpr double kRangeFull = 45;      // m, at a fall of 1
constexpr double kRangeClear = 20000;  // m, at none

// A phase in [0, 2 pi) hashed off the map's seed and a salt.
double phase(const mapgen::Level& level, uint32_t salt, uint32_t mul1, uint32_t mul2, int s1,
             int s2) {
  uint32_t h = (core::num::to_uint32(level.seed) ^ salt) * mul1;
  h ^= h >> s1;
  h *= mul2;
  h ^= h >> s2;
  return static_cast<double>(h) / 4294967296.0 * kTau;
}

double wind_phase(const mapgen::Level& level, uint32_t salt) {
  return phase(level, salt, 0x9e3779b1u, 0x85ebca77u, 15, 13);
}

double fall_phase(const mapgen::Level& level, uint32_t salt) {
  return phase(level, salt, 0x85ebca6bu, 0xc2b2ae35u, 13, 16);
}

// How deep each snowing sky breathes about its mean, as a share of it.
double breath(mapgen::WeatherKind kind) {
  switch (kind) {
    case mapgen::WeatherKind::flurries:
      return 0.9;
    case mapgen::WeatherKind::storm:
      return 0.35;
    default:
      return 0.3;
  }
}

}  // namespace

double downhill_from(double from) {
  // From `from` the air moves toward `from + pi`, whose +z part is -cos(from): up the mountain
  // while cos(from) > 0.
  return core::libm::cos(from) > 0 ? kPi - from : from;
}

wind::Field field_of(const mapgen::Level& level) {
  wind::Field f;
  f.mean = level.weather.wind;
  f.from = level.weather.wind_from;
  f.gusts = 3;
  for (uint32_t i = 0; i < 3; ++i) {
    f.gust[i] = {kGusts[i].period, kGusts[i].share, wind_phase(level, i + 1)};
  }
  f.veer = {kVeerPeriod, kVeer, wind_phase(level, 9)};
  f.fold = downhill_from;
  f.roughness = wind::kRoughness::snow;
  return f;
}

Wind wind_at(const mapgen::Level& level, double t) {
  const wind::Air a = wind::open_at(field_of(level), t);
  Wind out;
  out.x = a.x;
  out.z = a.z;
  out.speed = a.speed;
  out.gust = a.gust;
  return out;
}

Fall fall_at(const mapgen::Level& level, double t) {
  const mapgen::Weather& w = level.weather;
  Fall out;
  if (!mapgen::snows(w.kind) || w.snowfall <= 0) return out;
  double g = 0;
  for (uint32_t i = 0; i < 3; ++i) {
    g += kSqualls[i].share *
         core::libm::sin(kTau * t / kSqualls[i].period + fall_phase(level, 0x5au + i));
  }
  double fall = w.snowfall * (1 + breath(w.kind) * g);
  if (w.kind == mapgen::WeatherKind::storm) fall += kBlown * (wind_at(level, t).gust - 0.5);
  out.fall = core::clamp(fall, 0.0, 1.0);
  out.visibility = visibility_in(out.fall);
  return out;
}

double visibility_in(double fall) {
  return fall > 0 ? core::num::min(kRangeClear, kRangeFull / (fall * fall)) : kRangeClear;
}

}  // namespace ge::mountain::air
