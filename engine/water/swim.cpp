// Swimming: definitions.
//
// ge:summary Swimming: the immersion level, the two regimes, cold and breath: definitions
// ge:keywords water swim immersion friction accelerate idle-sink column buoyancy soak cold breath
// ge:keywords viscous stokes-drag yield liquid-motion
// ge:skill water
// ge:test tests/water_swim_test.cpp
#include "water/swim.hpp"

#include <cmath>

#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "core/num.hpp"
#include "water/spectrum.hpp"

namespace ge::water {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kHead = 0.12;         // m over the eyes to the top of the head
constexpr double kStroke = 1.2;        // s a stroke flat out
constexpr double kRecoverBreath = 6;   // a breath caught back this many times faster than held
constexpr double kShockDrain = 3;      // the breath held runs out this much faster in the shock
constexpr double kShockStroke = 0.5;   // the stroke's share in the shock
constexpr double kFailedStroke = 0.1;  // the stroke's share after swim failure
constexpr double kSpentStroke = 0.3;   // the stroke's share with no stamina left
constexpr double kFresh = 1000;        // kg/m^3: the water a garment soaks up

double length(const SwimInput& in) { return std::sqrt(in.x * in.x + in.y * in.y + in.z * in.z); }

bool wet_at(const World& world, double x, double y, double z, double t) {
  const Sample s = sample(world, x, y, z, t);
  return s.body >= 0 && s.depth > 0;
}

void kinematic(const SwimSpec& spec, const SwimScale& motion, const SwimInput& in, SwimState& st,
               double dt) {
  // The wish: the input's direction at the swim's share of the run speed, or the slow sink; both
  // scaled by the liquid's motion (1 in water).
  const double effort = core::num::min(1.0, length(in));
  double wx = 0, wy = -spec.idle_sink * motion.sink, wz = 0;
  if (effort > 1e-9) {
    const double k = spec.run_speed * spec.wish_scale * motion.speed / length(in) * effort;
    wx = in.x * k;
    wy = in.y * k;
    wz = in.z * k;
  }
  const double wish = std::sqrt(wx * wx + wy * wy + wz * wz);
  // The friction, by the depth of the water.
  double speed = std::sqrt(st.v.x * st.v.x + st.v.y * st.v.y + st.v.z * st.v.z);
  if (speed > 0) {
    const double kept =
        core::num::max(0.0, speed - dt * speed * spec.friction * motion.friction * st.level) /
        speed;
    st.v.x *= kept;
    st.v.y *= kept;
    st.v.z *= kept;
    speed *= kept;
  }
  // The acceleration toward the wish, up to it.
  if (wish > 0) {
    const double add = wish - speed;
    if (add > 0) {
      const double step = core::num::min(add, spec.accelerate * wish * dt);
      st.v.x += step * wx / wish;
      st.v.y += step * wy / wish;
      st.v.z += step * wz / wish;
    }
  }
  st.p.x += st.v.x * dt;
  st.p.y += st.v.y * dt;
  st.p.z += st.v.z * dt;
}

void body(const World& world, const SwimSpec& spec, const SwimInput& in, SwimState& st, double t,
          double dt, SwimEvents* ev) {
  const double s = spec.scale > 0 ? spec.scale : 1;
  const double fx = st.p.x / s, fy = st.p.y / s, fz = st.p.z / s;
  const Sample w = sample(world, fx, fy + spec.waist, fz, t);
  const double rho = w.body >= 0 ? w.density : kFresh;
  const double temp = w.temperature;
  st.in_water += dt;
  // The cold: the shock first, then the limbs cooling toward swim failure.
  const bool cold = temp < spec.cold_from;
  st.shock = cold && st.in_water < spec.shock_time;
  const double effort = core::num::min(1.0, length(in));
  if (cold) st.cold = core::num::min(1.0, st.cold + dt / failure_time(temp));
  st.failed = st.cold >= 1;
  const Budget b = budget(spec, st.in_water);
  const double mass = b.mass;
  double volume = b.volume;
  const double area = b.area;
  st.carried = b.carried;
  // The lungs: full while a breath is held, emptied once it is gone, between at the surface.
  const bool under = st.level >= 3;
  if (under) {
    volume += st.breath > 0 ? spec.lungs : spec.residual;
  } else {
    volume += 0.5 * (spec.lungs + spec.residual);
  }
  // The breath.
  if (under) {
    st.breath -= dt * (1 + effort) * (st.shock ? kShockDrain : 1);
    if (st.breath <= 0) {
      st.breath = 0;
      st.drowning = true;
    }
  } else {
    st.breath = core::num::min(spec.breath, st.breath + dt * kRecoverBreath);
    st.drowning = false;
  }
  // The stamina.
  if (effort > 0) {
    st.stamina = core::num::max(0.0, st.stamina - dt * effort);
  } else {
    st.stamina = core::num::min(spec.stamina, st.stamina + dt * spec.recover);
  }
  // The forces, on a vertical column of his height by its submerged share.
  const double height = spec.eyes + kHead;
  const double share = w.body >= 0 ? core::clamp((w.surface - fy) / height, 0, 1) : 0;
  const double vx = st.v.x / s, vy = st.v.y / s, vz = st.v.z / s;
  double ax = 0, ay = rho * kGravity * volume * share - mass * kGravity, az = 0;
  const double rx = vx - w.vx * share, ry = vy - w.vy * share, rz = vz - w.vz * share;
  const double rs = std::sqrt(rx * rx + ry * ry + rz * rz);
  const double q = 0.5 * rho * spec.drag * area * rs * share;
  ax -= q * rx;
  ay -= q * ry;
  az -= q * rz;
  if (effort > 0) {
    double k = spec.thrust * effort * spec.area / area;  // soaked clothes drag the stroke down
    if (st.shock) k *= kShockStroke;
    if (st.failed) k *= kFailedStroke;
    if (st.stamina <= 0) k *= kSpentStroke;
    const double n = length(in);
    ax += k * in.x / n;
    ay += k * in.y / n;
    az += k * in.z / n;
    st.stroke_phase += dt * effort / kStroke;
    if (st.stroke_phase >= 1) {
      st.stroke_phase -= 1;
      if (ev) ev->stroke = true;
    }
  }
  double nvx = vx + ax / mass * dt, nvy = vy + ay / mass * dt, nvz = vz + az / mass * dt;
  // A viscous liquid's Stokes drag on his equivalent sphere, taken implicitly (a thick liquid's
  // is stiff), and a Bingham liquid's yield gripping his wetted skin, which takes up to its
  // force's worth of his speed through the water a step and never turns him round.
  const double ux = w.vx * share, uy = w.vy * share, uz = w.vz * share;
  const double stokes = 6 * kPi * w.viscosity * std::sqrt(area / kPi) * share;
  if (w.body >= 0 && stokes > 0) {
    const double keep = 1 / (1 + stokes / mass * dt);
    nvx = ux + (nvx - ux) * keep;
    nvy = uy + (nvy - uy) * keep;
    nvz = uz + (nvz - uz) * keep;
  }
  if (w.body >= 0 && w.yield > 0) {
    const double gx = nvx - ux, gy = nvy - uy, gz = nvz - uz;
    const double g = std::sqrt(gx * gx + gy * gy + gz * gz);
    if (g > 0) {
      const double kept = core::num::max(0.0, g - w.yield * spec.skin * share / mass * dt) / g;
      nvx = ux + gx * kept;
      nvy = uy + gy * kept;
      nvz = uz + gz * kept;
    }
  }
  st.v = {nvx * s, nvy * s, nvz * s};
  st.p.x += st.v.x * dt;
  st.p.y += st.v.y * dt;
  st.p.z += st.v.z * dt;
}

}  // namespace

Budget budget(const SwimSpec& spec, double in_water) {
  Budget b;
  b.mass = spec.mass;
  b.volume = spec.volume;
  b.area = spec.area;
  for (int i = 0; i < spec.garments && i < kMaxGarments; ++i) {
    const Garment& g = spec.gear[i];
    // The water a garment soaks up is the water's own volume: it weighs nothing in the water.
    const double soaked = g.capacity * (1 - core::libm::exp(-g.soak * in_water));
    const double air = g.air * core::libm::exp(-g.escape * in_water);
    b.carried += soaked;
    b.air += air;
    b.mass += g.mass + soaked;
    b.volume += (g.density > 0 ? g.mass / g.density : 0) + soaked / kFresh + air;
    if (g.capacity > 0) b.area += g.drag * soaked / g.capacity;
  }
  return b;
}

int immersion(const World& world, const SwimSpec& spec, const core::Vec3& p, double t) {
  const double s = spec.scale > 0 ? spec.scale : 1;
  const double x = p.x / s, y = p.y / s, z = p.z / s;
  if (!wet_at(world, x, y + 0.01, z, t)) return 0;
  if (!wet_at(world, x, y + spec.waist, z, t)) return 1;
  if (!wet_at(world, x, y + spec.eyes, z, t)) return 2;
  return 3;
}

double failure_time(double temperature) {
  // About ten minutes of real swimming in water at 0 degrees, half an hour at 5, an hour at 10.
  const double t = core::num::max(0.0, temperature);
  return 600 * (1 + 0.8 * t + 0.02 * t * t);
}

void step_swimmer(const World& world, const SwimSpec& spec, const SwimInput& input, SwimState& st,
                  double t, double dt, Ledge ledge, SwimEvents* events) {
  if (st.breath < 0) st.breath = spec.breath;
  if (st.stamina < 0) st.stamina = spec.stamina;
  const int was = st.level;
  st.level = immersion(world, spec, st.p, t);
  if (events) {
    *events = {};
    events->entered = was == 0 && st.level > 0;
    events->left = was > 0 && st.level == 0;
    events->gasp = was == 3 && st.level < 3;
    if (events->entered || events->left) {
      const double sc = spec.scale > 0 ? spec.scale : 1;
      events->splash_speed = std::sqrt(st.v.x * st.v.x + st.v.y * st.v.y + st.v.z * st.v.z) / sc;
    }
  }
  // The water jump, once begun, is the game's gravity's until it ends.
  if (st.jumping > 0) {
    st.jumping -= dt;
    st.v.y -= spec.gravity * dt;
    st.p.x += st.v.x * dt;
    st.p.y += st.v.y * dt;
    st.p.z += st.v.z * dt;
    return;
  }
  if (st.level == 2 && input.jump && spec.jump_speed > 0 && ledge.found) {
    const double reach = spec.jump_speed * spec.jump_speed / (2 * spec.gravity);
    if (ledge.found(ledge.context, st.p, st.heading, reach)) {
      st.v = {core::libm::sin(st.heading) * spec.jump_push, spec.jump_speed,
              core::libm::cos(st.heading) * spec.jump_push};
      st.jumping = spec.jump_time;
      return;
    }
  }
  if (spec.regime == Regime::kinematic) {
    if (st.level >= 2) {
      // The liquid at his waist sets the motion's scales.
      const double sc = spec.scale > 0 ? spec.scale : 1;
      const Sample w = sample(world, st.p.x / sc, st.p.y / sc + spec.waist, st.p.z / sc, t);
      const SwimScale motion =
          w.body >= 0 ? world.bodies[static_cast<size_t>(w.body)].stuff.liquid.swim : SwimScale{};
      kinematic(spec, motion, input, st, dt);
    }
    return;
  }
  if (st.level >= 1) body(world, spec, input, st, t, dt, events);
}

SwimSpec default_swimmer() {
  SwimSpec s;
  s.regime = Regime::body;
  // A swimsuit: next to nothing, soaked at once.
  s.gear[0] = {.mass = 0.15, .density = 1150, .capacity = 0.1, .soak = 0.5};
  s.garments = 1;
  return s;
}

}  // namespace ge::water
