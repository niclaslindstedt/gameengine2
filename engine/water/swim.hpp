// SWIMMING (docs/water.md § Swimming): one swimmer, two regimes, tuned per game by one struct
// (`SwimSpec`). The water is read through the one query (`sample`), so a swimmer is in the same
// water as a float, the sound and the picture.
//
// THE IMMERSION LEVEL (`immersion`), 0 to 3, from the water at three heights of the swimmer: 1
// when the feet are in, 2 at the waist, 3 when the eyes are under. It drives the friction, the
// control and the breath, and a game reads it for its own (the view's tint, the sound's muffle).
//
// THE KINEMATIC REGIME (`Regime::kinematic`, the classic first-person feel): velocity-driven,
// with no buoyancy and no drag curves; the numbers are the feel and they are the game's, in the
// game's own units of length (the engine reads the water in metres; `SwimSpec::scale` is the
// game's units per metre). At level 2 or more: the wish is the input's direction (the view's
// pitch included) times the run speed times `wish_scale`; with no input it is straight down at
// `idle_sink` (the slow sink); a friction takes `speed * friction * level * dt` off; an
// acceleration of `accelerate * wish speed * dt` is added toward the wish, up to it; gravity is
// not applied. At level 1 or less the game's own ground and air model has the swimmer, and the
// step leaves his velocity alone.
//
// THE BODY REGIME (`Regime::body`, physical): forces on the swimmer as a vertical column of his
// own height, by its submerged share. Gravity on his mass and his gear's; buoyancy on his body's
// volume, his lungs' air and his gear's (each garment's own material, and the air it traps,
// which escapes over minutes); quadratic drag on his frontal area, raised as his clothes soak;
// and the stroke's thrust toward the wish, cut by soaked clothes, by the cold shock, by his limbs
// cooling and by his stamina. Water soaked into a garment is the water's own density, so it
// weighs nothing in the water and everything out of it (`SwimState::carried`). A person with a
// breath held floats just barely; a dressed skier floats on his jacket's air at first, then his
// suit lets it go, his boots are denser than the water, and he sinks slowly unless he swims.
// - THE COLD (in water under `cold_from` degrees): a cold shock for `shock_time` s (the gasp: the
//   breath held a fraction as long, the stroke half as strong), then the limbs cool toward swim
//   failure in `failure_time(T)` (about ten minutes of swimming at 0 degrees, longer as the water
//   warms), after which the stroke is a tenth of itself. What any stage costs is the game's.
// - THE BREATH: held while the eyes are under, running out faster with effort and in the shock,
//   and caught again at the surface; `SwimState::drowning` says it has run out. The lungs are
//   full while it is held and at their residual once it is gone.
//
// IN A LIQUID (liquid.hpp): the kinematic regime's wish speed, friction and idle sink are scaled
// by the liquid's `SwimScale` at his waist; the body regime adds a viscous liquid's Stokes drag on
// his equivalent sphere (taken implicitly) and a Bingham liquid's yield on his wetted skin, so he
// wades through slime and is held fast in tar; a liquid denser than he is floats him high.
//
// THE WATER JUMP, both regimes: at level 2, holding jump, facing a lip the game's collision finds
// within reach (`Ledge`, a callback, since the engine does not know the world's solids), the
// swimmer is thrown up at `jump_speed` and forward at `jump_push` for `jump_time`, the game's
// gravity acting, so he comes out over the lip.
//
// Every step reports a splash where the swimmer's feet enter or leave the water and a stroke and
// a breath for the sound (`SwimEvents`).
//
// ge:summary Swimming: the immersion level, the kinematic and the body regimes, cold, breath, jump
// ge:keywords water swim swimmer immersion water-level kinematic body buoyancy clothes garment
// ge:keywords soak trapped-air cold-shock swim-failure breath drowning stroke water-jump ledge
// ge:keywords step_swimmer swim_spec default_swimmer
// ge:skill water
// ge:test tests/water_swim_test.cpp
#pragma once

#include <cstdint>

#include "core/quat.hpp"
#include "water/world.hpp"

namespace ge::water {

inline constexpr int kMaxGarments = 8;

enum class Regime : uint8_t { kinematic, body };

// One piece of what a swimmer wears (the body regime).
struct Garment {
  double mass = 0;      // dry, kg
  double density = 0;   // its material's, kg/m^3 (0: no volume of its own)
  double capacity = 0;  // the water it soaks up, kg
  double soak = 0;      // 1/s: soaked as 1 - exp(-soak t)
  double air = 0;       // trapped air when it goes in, m^3
  double escape = 0;    // 1/s: the air let go as exp(-escape t)
  double drag = 0;      // the frontal area it adds fully soaked, m^2
};

struct SwimSpec {
  Regime regime = Regime::body;
  double waist = 0.95;  // m over the feet
  double eyes = 1.62;   // m over the feet
  // The kinematic regime, in the game's units (`scale` of them a metre).
  double scale = 1;         // the game's units per metre
  double run_speed = 0;     // the game's top running speed
  double wish_scale = 0.7;  // the swim's share of it
  double accelerate = 10;   // 1/s
  double friction = 4;      // 1/s per immersion level
  double idle_sink = 0;     // the slow sink with no input
  // The body regime, SI.
  double mass = 75;          // kg, the body
  double volume = 0.0712;    // m^3, the body less its lungs' air
  double lungs = 0.0045;     // m^3 of air with a breath held
  double residual = 0.0012;  // m^3 of air left when it is gone
  double area = 0.12;        // m^2 frontal, swimming
  double skin = 1.8;         // m^2 of skin a liquid's yield grips, all of him in
  double drag = 1.0;         // the drag coefficient
  double thrust = 90;        // N, the stroke flat out
  double stamina = 600;      // s of flat-out effort a full stamina holds
  double recover = 0.2;      // the stamina's recovery at rest, a share of its drain
  double breath = 60;        // s a breath held lasts at rest
  double cold_from = 15;     // degrees C: colder water shocks and cools
  double shock_time = 60;    // s
  Garment gear[kMaxGarments];
  int garments = 0;
  // The water jump, both regimes (the game's units in the kinematic one).
  double jump_speed = 0;
  double jump_push = 0;
  double jump_time = 0;      // s
  double gravity = 9.80665;  // the game's, for the jump (its units / s^2)
};

// What the swimmer is asked to do: a wish direction in the world (unit or shorter: its length is
// the effort, 0..1), and the jump.
struct SwimInput {
  double x = 0, y = 0, z = 0;
  bool jump = false;
};

// A lip within reach: the game answers whether, from the swimmer's feet at `p` facing `heading`,
// there is a ledge he can climb onto within `reach` (the game's units) over his eyes.
struct Ledge {
  bool (*found)(const void* context, const core::Vec3& p, double heading, double reach) = nullptr;
  const void* context = nullptr;
};

struct SwimState {
  core::Vec3 p{0, 0, 0};  // the feet, in the game's units (metres in the body regime)
  core::Vec3 v{0, 0, 0};
  double heading = 0;       // where he faces (the jump's direction), rad
  int level = 0;            // 0..3
  double in_water = 0;      // s since he went in (the soak, the air, the shock run off it)
  double breath = -1;       // s of breath held left (-1: full, set on the first step)
  double stamina = -1;      // s of effort left (-1: full)
  double cold = 0;          // 0..1, the limbs' cooling toward swim failure
  double jumping = 0;       // s of the water jump left
  double stroke_phase = 0;  // 0..1 through a stroke
  bool shock = false;
  bool failed = false;    // swim failure
  bool drowning = false;  // the breath has run out
  double carried = 0;     // kg of water his clothes would bring out with him
};

struct SwimEvents {
  bool entered = false, left = false;  // the feet through the surface
  double splash_speed = 0;
  bool stroke = false;
  bool gasp = false;  // the eyes back out after a held breath
};

// What the body regime's swimmer and his clothes weigh and displace after `in_water` s (the lungs
// left out: they are the breath's), and his frontal area with the soak.
struct Budget {
  double mass = 0;     // kg, soaked water included
  double volume = 0;   // m^3, the trapped air and the soaked water included
  double air = 0;      // m^3 of it still trapped
  double carried = 0;  // kg of water soaked up
  double area = 0;     // m^2
};
Budget budget(const SwimSpec& spec, double in_water);

// The level at the swimmer's feet `p` (the game's units).
int immersion(const World& world, const SwimSpec& spec, const core::Vec3& p, double t);

// Swim-failure time at `temperature` degrees C, s.
double failure_time(double temperature);

// One step of `dt` s.
void step_swimmer(const World& world, const SwimSpec& spec, const SwimInput& input,
                  SwimState& state, double t, double dt, Ledge ledge = {},
                  SwimEvents* events = nullptr);

// A person in a swimsuit in temperate water.
SwimSpec default_swimmer();

}  // namespace ge::water
