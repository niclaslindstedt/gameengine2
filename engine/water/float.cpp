// Floating: definitions.
//
// ge:summary Floating: probes, buoyancy, drag, slam, added mass, planing: definitions
// ge:keywords water float probe share cap buoyancy drag slam planing semi-implicit
// ge:skill water
// ge:test tests/water_float_test.cpp
#include "water/float.hpp"

#include <cmath>

#include "core/math.hpp"
#include "water/spectrum.hpp"

namespace ge::water {

namespace {

constexpr double kPi = 3.141592653589793;

core::Vec3 cross(const core::Vec3& a, const core::Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double volume_of(const Probe& p) {
  return p.volume > 0 ? p.volume : 4.0 / 3.0 * kPi * p.radius * p.radius * p.radius;
}

double area_of(const Probe& p) { return p.area > 0 ? p.area : kPi * p.radius * p.radius; }

}  // namespace

double share_of(const Probe& probe, double depth) {
  const double r = probe.radius;
  if (r <= 0) return depth > 0 ? 1 : 0;
  if (probe.shape == Shape::slab) return core::clamp((depth + r) / (2 * r), 0, 1);
  const double h = core::clamp(depth + r, 0, 2 * r);  // the cap's height
  return h * h * (3 * r - h) / (4 * r * r * r);
}

Push float_push(const World& world, std::span<const Probe> probes, const Motion& m, double t,
                FloatState& state) {
  Push out;
  const size_t n = probes.size() < kMaxProbes ? probes.size() : kMaxProbes;
  for (size_t i = 0; i < n; ++i) {
    const Probe& pr = probes[i];
    const core::Vec3 arm = core::rotate(m.q, {pr.x, pr.y, pr.z});
    const core::Vec3 at{m.p.x + arm.x, m.p.y + arm.y, m.p.z + arm.z};
    const Sample s = sample(world, at.x, at.y, at.z, t);
    const double share = s.body < 0 ? 0 : share_of(pr, s.depth);
    const double was = state.share[i];
    state.share[i] = share;
    // The probe's velocity through the water.
    const core::Vec3 spin = cross(m.w, arm);
    const core::Vec3 rel{m.v.x + spin.x - s.vx, m.v.y + spin.y - s.vy, m.v.z + spin.z - s.vz};
    const double speed = std::sqrt(rel.x * rel.x + rel.y * rel.y + rel.z * rel.z);
    if ((was == 0) != (share == 0) && out.splashes < kMaxSplashes) {
      out.splash[out.splashes++] = {at.x, at.y, at.z, speed, pr.radius, share > 0};
    }
    if (share <= 0) continue;
    ++out.wet;
    const double rho = s.density;
    const double vol = volume_of(pr);
    const double area = area_of(pr);
    core::Vec3 f{0, rho * kGravity * vol * share, 0};
    out.submerged += vol * share;
    out.added_mass += pr.added * rho * vol * share;
    // Drag, linear and quadratic, by the share.
    const double q = share * (pr.linear + 0.5 * rho * pr.drag * area * speed);
    f.x -= q * rel.x;
    f.y -= q * rel.y;
    f.z -= q * rel.z;
    // The entry slam, fading out by half wet.
    if (rel.y < 0 && share < 0.5) {
      f.y += 0.5 * rho * kSlam * area * rel.y * rel.y * (1 - 2 * share);
    }
    // A planing plate: lift through the plate by its trim to the flow, friction along the flow.
    if (pr.plate > 0 && speed > 0) {
      const core::Vec3 down = core::rotate(m.q, {0, -1, 0});
      const double into = rel.x * down.x + rel.y * down.y + rel.z * down.z;  // toward the water
      const double wet = core::clamp(4 * share, 0, 1);
      const double dyn = 0.5 * rho * speed * speed * pr.plate * wet;
      if (into > 0) {
        const double lift = dyn * kPi * into / speed;
        f.x -= lift * down.x;
        f.y -= lift * down.y;
        f.z -= lift * down.z;
      }
      const double skin = dyn * kSkin / speed;
      f.x -= skin * rel.x;
      f.y -= skin * rel.y;
      f.z -= skin * rel.z;
    }
    out.force.x += f.x;
    out.force.y += f.y;
    out.force.z += f.z;
    const core::Vec3 tq = cross(arm, f);
    out.torque.x += tq.x;
    out.torque.y += tq.y;
    out.torque.z += tq.z;
  }
  return out;
}

Push step_floater(const World& world, std::span<const Probe> probes, Floater& body, double t,
                  double dt) {
  const Push push = float_push(world, probes, body.motion, t, body.state);
  Motion& m = body.motion;
  // Semi-implicit Euler: the velocities first, then the pose off the new velocities. The added
  // mass's change takes its Lagrangian's half share of v d(ma)/dt, so wetting and drying neither
  // make nor lose energy.
  const double moving = body.mass + push.added_mass;
  const double change = 0.5 * (push.added_mass - body.state.added_mass) / dt;
  body.state.added_mass = push.added_mass;
  m.v.x += (push.force.x - change * m.v.x) / moving * dt;
  m.v.y += (push.force.y - body.mass * kGravity - change * m.v.y) / moving * dt;
  m.v.z += (push.force.z - change * m.v.z) / moving * dt;
  const core::Vec3 tb = core::unrotate(m.q, push.torque);
  core::Vec3 wb = core::unrotate(m.q, m.w);
  wb.x += tb.x / body.inertia.x * dt;
  wb.y += tb.y / body.inertia.y * dt;
  wb.z += tb.z / body.inertia.z * dt;
  m.p.x += m.v.x * dt;
  m.p.y += m.v.y * dt;
  m.p.z += m.v.z * dt;
  m.q = core::integrate(m.q, wb.x, wb.y, wb.z, dt);
  m.w = core::rotate(m.q, wb);
  return push;
}

}  // namespace ge::water
