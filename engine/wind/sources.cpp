// Local wind sources: the jet, the whirl and the blast as closed forms.
//
// ge:summary Local wind sources: definitions
// ge:keywords wind source jet whirl blast rankine gaussian
// ge:skill water
// ge:test tests/wind_sources_test.cpp
#include "wind/sources.hpp"

#include <cmath>

#include "core/libm/libm.hpp"
#include "core/num.hpp"

namespace ge::wind {

namespace {

constexpr double kJetCore = 6.2;     // diameters of potential core before the centreline decays
constexpr double kJetSpread = 0.1;   // the half-width's growth per metre downstream
constexpr double kWhirlRise = 0.5;   // the core's updraft as a share of the rim speed
constexpr double kWhirlHeight = 10;  // radii over which the updraft fades with height

Air jet(const Source& s, double x, double y, double z) {
  Air out;
  const double dx = x - s.x, dy = y - s.y, dz = z - s.z;
  const double along = dx * s.ax + dy * s.ay + dz * s.az;
  if (along <= 0) return out;
  const double px = dx - along * s.ax, py = dy - along * s.ay, pz = dz - along * s.az;
  const double off2 = px * px + py * py + pz * pz;
  const double centre = s.strength * core::num::min(1.0, kJetCore * 2 * s.radius / along);
  const double half = s.radius + kJetSpread * along;
  const double u = centre * core::libm::exp(-off2 / (half * half));
  out.x = u * s.ax;
  out.y = u * s.ay;
  out.z = u * s.az;
  return out;
}

Air whirl(const Source& s, double x, double y, double z) {
  Air out;
  const double dx = x - s.x, dz = z - s.z;
  const double r = std::sqrt(dx * dx + dz * dz);
  if (r <= 0) return out;
  const double v = r < s.radius ? s.strength * r / s.radius : s.strength * s.radius / r;
  // Clockwise from above: the heading of the radius plus a quarter turn, (dz, -dx) / r.
  out.x = v * dz / r;
  out.z = -v * dx / r;
  const double h = y - s.y;
  if (h >= 0) {
    const double inner = core::libm::exp(-(r * r) / (s.radius * s.radius));
    out.y = kWhirlRise * s.strength * inner * core::libm::exp(-h / (kWhirlHeight * s.radius));
  }
  return out;
}

Air blast(const Source& s, double x, double y, double z, double t) {
  Air out;
  const double age = t - s.t0;
  if (age < 0) return out;
  const double dx = x - s.x, dy = y - s.y, dz = z - s.z;
  const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (d <= 0 || d > kSound * age) return out;
  const double r = core::num::max(d, s.radius);
  const double u =
      s.strength * (s.radius / r) * (s.radius / r) * core::libm::exp(-age / kBlastLife);
  out.x = u * dx / d;
  out.y = u * dy / d;
  out.z = u * dz / d;
  return out;
}

}  // namespace

Air source_at(const Source& source, double x, double y, double z, double t) {
  Air out;
  switch (source.kind) {
    case SourceKind::jet:
      out = jet(source, x, y, z);
      break;
    case SourceKind::whirl:
      out = whirl(source, x, y, z);
      break;
    case SourceKind::blast:
      out = blast(source, x, y, z, t);
      break;
  }
  out.speed = std::sqrt(out.x * out.x + out.y * out.y + out.z * out.z);
  out.gust = 0.5;
  return out;
}

}  // namespace ge::wind
