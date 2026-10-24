// The snow contact laws (contact.hpp).
//
// ge:summary The contact laws' implementation: Bekker, Rankine, Mohr–Coulomb, Janosi, glide
// ge:keywords snow contact bekker rankine passive-pressure mohr-coulomb janosi glide planing
// ge:skill snow
// ge:test tests/snow_contact_test.cpp
#include "snow/contact.hpp"

#include <cmath>

#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "snow/press.hpp"

namespace ge::snow {

namespace {

constexpr double kGravity = 9.81;  // m/s2
constexpr double kDegree = core::kPi / 180;
// Glide's temperature curve: the friction rises by these shares a (10 °C)² away from −5 °C,
// faster towards the melt than into the cold.
constexpr double kGlideBest = -5;   // °C
constexpr double kGlideCold = 0.4;  // a (10 °C)² colder
constexpr double kGlideWarm = 2.0;  // a (10 °C)² warmer

double tan_deg(double deg) { return core::libm::tan(deg * kDegree); }

}  // namespace

double bearing_width(double half_width, double half_length) {
  const double w = half_length > 0 && half_length < half_width ? half_length : half_width;
  return 2 * w;
}

double bekker_modulus(const ContactRow& row, double b) {
  if (b <= 0) return 0;
  return (row.kc / b + row.kphi) * 1000;
}

double bekker_pressure(const ContactRow& row, double b, double z) {
  if (z <= 0) return 0;
  return bekker_modulus(row, b) * core::libm::pow(z, row.n);
}

double bekker_sink(const ContactRow& row, double b, double pa) {
  const double k = bekker_modulus(row, b);
  if (pa <= 0 || k <= 0) return 0;
  return core::libm::pow(pa / k, 1 / row.n);
}

double sink_cap(double depth, double density, double pa, double cap) {
  if (depth <= 0) return 0;
  const double reach = reach_density(pa, cap);
  if (reach <= density) return 0;
  return depth * (1 - density / reach);
}

double column_sink(Kind kind, double depth, double density, double b, double pa) {
  const double z = bekker_sink(contact_row(kind), b, pa);
  const double most = sink_cap(depth, density, pa, kind_row(kind).cap);
  return z < most ? z : most;
}

double compaction_resistance(const ContactRow& row, double b, double z) {
  if (z <= 0) return 0;
  return b * bekker_modulus(row, b) * core::libm::pow(z, row.n + 1) / (row.n + 1);
}

double bulldozing_resistance(const ContactRow& row, double density, double b, double z, double v) {
  if (z <= 0 || b <= 0) return 0;
  const double kp = tan_deg(45 + row.phi / 2);
  const double passive = kp * kp;
  const double gamma = density * kGravity;  // N/m3
  const double earth = b * (2 * row.cohesion * z * kp + 0.5 * gamma * z * z * passive);
  const double swept = density * b * z * v * v;  // the momentum given to what it sweeps
  return earth + swept;
}

double shear_strength(const ContactRow& row, double pa) {
  return row.cohesion + (pa > 0 ? pa : 0) * tan_deg(row.phi);
}

double shear_at(const ContactRow& row, double pa, double j) {
  if (j <= 0) return 0;
  return shear_strength(row, pa) * (1 - core::libm::exp(-j / row.shear_k));
}

double tread_traction(const ContactRow& row, double area, double pa, double slip, double length) {
  if (slip <= 0 || length <= 0 || area <= 0) return 0;
  const double il = slip * length;  // the slip at the contact's trailing end
  const double k = row.shear_k;
  return area * shear_strength(row, pa) * (1 - k / il * (1 - core::libm::exp(-il / k)));
}

double glide_friction(Kind kind, double temp_c) {
  const double d = (temp_c - kGlideBest) / 10;
  const double rise = d < 0 ? kGlideCold * d * d : kGlideWarm * d * d;
  return contact_row(kind).glide * (1 + rise);
}

double tyre_friction(Kind kind, double temp_c) {
  const double mu = contact_row(kind).tyre;
  if (kind != Kind::ice) return mu;
  // Ice under a tyre grips half as well at the melt, where a film of water lubricates it.
  const double wet = core::clamp((temp_c + 5) / 5, 0, 1);
  return mu * (1 - 0.5 * wet);
}

double planing_sink(double static_sink, double v, double v_plane) {
  if (v_plane <= 0) return static_sink;
  const double r = v / v_plane;
  return static_sink * core::libm::exp(-r * r);
}

}  // namespace ge::snow
