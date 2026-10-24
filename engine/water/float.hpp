// FLOATING (docs/water.md § Floating): what the water does to a rigid body through a few PROBES
// fixed in the body's frame, so a sled, a hull, a log, a crate and a soldier all float through the
// same code. The water is read through the one query (`sample`), so a body floats on the waves
// the swimmer, the sound and the picture see.
//
// For each probe at a step (`float_push`):
// - THE SUBMERGED SHARE off the depth of its centre under the surface: a sphere's by its cap's
//   volume, a slab's (a vertical prism: a hull's section, a crate's quarter) linearly.
// - BUOYANCY: rho g V share, straight up at the probe's centre.
// - DRAG against the water's own velocity (the waves' orbital motion, a river's current):
//   linear (`Probe::linear`, N s/m when fully under) and quadratic (1/2 rho Cd A |v| v), by the
//   share.
// - THE ENTRY SLAM: a probe entering the water downward meets 1/2 rho Cs A v^2, fading out as it
//   reaches half wet, so a body dropped in stops harder than its buoyancy alone would stop it.
//   Cs = 1 puts the slam's impulse near the momentum of the water a half-wet sphere sets moving
//   (its added mass); the peak's own coefficient (pi for a sphere's first touch) would stop a light
//   body dead at the surface over a whole step.
// - ADDED MASS: the water a body drags with it, Ca rho V share, returned for the caller to add to
//   its mass in its translational accelerations. It changes as the body wets, so a caller that
//   wants no energy from nowhere takes the Lagrangian's term too, a = (F - 1/2 (dma/dt) v) /
//   (m + ma), as `step_floater` does.
// - PLANING (`Probe::plate`): a flat plate facing down in the body's frame lifts by
//   1/2 rho v^2 A pi sin(trim) while wet (a flat plate's planing lift in its simplest form) with a
//   skin friction 1/2 rho v^2 A Cf along the flow, so a fast sled or a skier at speed skims a pond
//   and slows into it.
// - A SPLASH event where a probe wets or leaves the water (where, how fast, how big), which the
//   picture's particles and ripples and the sound read.
// The function returns the force and the torque about the body's centre; the caller integrates
// its own body, or hands a `Floater` (a mass, an inertia and its motion) to `step_floater`, which
// does both for the bodies that are only ever floating things (a crate, a log, a buoy).
//
// ge:summary Floating: probes on a rigid body, buoyancy, drag, slam, added mass, planing, splashes
// ge:keywords water float floating buoyancy probe drag slam added-mass planing plate skim splash
// ge:keywords float_push step_floater floater rigid-body draught heave splash_ring
// ge:skill water
// ge:test tests/water_float_test.cpp
#pragma once

#include <cstdint>
#include <span>

#include "core/quat.hpp"
#include "water/world.hpp"

namespace ge::water {

inline constexpr int kMaxProbes = 32;
inline constexpr int kMaxSplashes = 8;
inline constexpr double kSlam = 1.0;    // the entry slam's coefficient
inline constexpr double kSkin = 0.006;  // a planing plate's skin friction

enum class Shape : uint8_t { sphere, slab };

struct Probe {
  Shape shape = Shape::sphere;
  double x = 0, y = 0, z = 0;  // the centre in the body's frame, m
  double radius = 0.5;         // a sphere's radius; a slab's half height, m
  double volume = 0;           // m^3 (0: a sphere's 4/3 pi r^3)
  double area = 0;             // the drag's and the slam's reference, m^2 (0: pi r^2)
  double drag = 1;             // the quadratic drag's coefficient
  double linear = 0;           // the linear drag, N s/m fully under
  double added = 0.5;          // the added-mass coefficient
  double plate = 0;            // a planing plate facing down in the body's frame, m^2 (0: none)
};

// Where a body is and how it moves: its centre, its orientation (body to world), its velocity and
// its angular velocity in the WORLD frame.
struct Motion {
  core::Vec3 p{0, 0, 0};
  core::Quat q{0, 0, 0, 1};
  core::Vec3 v{0, 0, 0};
  core::Vec3 w{0, 0, 0};
};

// What a body's probes remember between steps: each one's submerged share.
struct FloatState {
  double share[kMaxProbes]{};
  double added_mass = 0;  // the last step's, kg (`step_floater` keeps it)
};

struct Splash {
  double x = 0, y = 0, z = 0;  // where, m
  double speed = 0;            // the probe's speed through the surface, m/s
  double size = 0;             // the probe's radius, m
  bool entering = true;        // into the water, or out of it
};

struct Push {
  core::Vec3 force{0, 0, 0};   // N, world frame
  core::Vec3 torque{0, 0, 0};  // N m about the body's centre, world frame
  double submerged = 0;        // m^3 under the surface
  double added_mass = 0;       // kg
  int wet = 0;                 // probes touching the water
  int splashes = 0;
  Splash splash[kMaxSplashes];
};

// The submerged share of a probe whose centre is `depth` m under the surface.
double share_of(const Probe& probe, double depth);

// What the water does to a body's probes at `t` (at most kMaxProbes are read).
Push float_push(const World& world, std::span<const Probe> probes, const Motion& motion, double t,
                FloatState& state);

// A body that is only ever a floating thing.
struct Floater {
  double mass = 100;               // kg
  core::Vec3 inertia{10, 10, 10};  // kg m^2 about the body's own axes
  Motion motion;
  FloatState state;
};

// A splash's ring on `body` (wake.hpp): the water a probe of its size throws aside going through
// the surface at its speed (half a sphere's volume, times its speed over a metre a second, times
// `strength`), set off at `t`.
void splash_ring(Wakes& wakes, int body, const Splash& splash, double t, double strength = 1);

// One step of `dt` s of a floater under gravity and the water; returns the push it felt.
Push step_floater(const World& world, std::span<const Probe> probes, Floater& body, double t,
                  double dt);

}  // namespace ge::water
