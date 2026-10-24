// THE PARAMOTOR: a powered speed wing on a skier's back, flown by hand on four controls with
// nothing between the hands and the wing. The pilot is the skier himself (`skier::SkierState`):
// on the snow his skis are the skier's in every respect (the edge, the skid, the sink), and in the
// air he hangs in the harness under the wing. Every number is `spec.hpp`'s.
//
// THE RIDE: READY, the wing held inflated over him (`ready_para`) until he has skied off fast
// enough for the air to fly it; FLOWN, the wing on its lines over him, in the air or skiing under
// it on the snow (speed riding); TANGLED, fallen into his own wing out of a swing he threw
// (`acro.hpp`), falling in the bundle until he cuts it away or meets the snow; DROPPED, the canopy
// and the motor let go (`drop_para`, or cut away: the wing brought down onto the snow or into a
// crown, or the skier wiped out) and falling free of him, lying where they come down.
//
// THE STEP (`step_para`), after the skier's own step (`skier::step_skier` keeps the snow, his
// drag and the landing; in the air the caller hands it no input, `para_hangs`):
// 1. THE HANDS: the controls follow them over `spec::kHands` s; near the map's edge the toggles
//    are turned toward its middle.
// 2. THE ENGINE: the rpm after the throttle; the thrust, its square falling with the air through
//    the propeller, along his skis on the snow, along the wing's way aloft, tilted with his lean.
// 3. THE WING, two bodies on an inextensible line: a point mass flying by a polar, its angle of
//    attack the air through it against the plane square to its lines plus the trim, so its pitch
//    is the pendulum's own and nothing sets it (a surge, a dive after a stall and the swing under
//    a hard turn all fall out of it); the turn a side force toward the toggle pulled, righted by
//    the canopy's arc; the swing damped against the air; the lines pulled tight, pulling only.
// 4. THE AIR (`air.hpp`): the wind, ridge lift and the lee, the eddies; read at both tips, a FOLD
//    when one goes under its angle, refilling by itself and faster pumped.
// 5. THE PILOT HUNG: turned to the wing's heading and banked under it aloft; stood up near the
//    snow with his skis turned to his way to land square.
// 6. THE WING BROUGHT DOWN onto the snow or into a crown: cut away.
// 7. THE ACRO (`acro.hpp`): the bar forward pitches the wing into a dive, back at a dive's speed
//    the rears bend the pair's path up and round in a LOOP; a wing met by the air from behind
//    collapses whole, and lines gone slack at the top of a swing tangle him in it.
//
// Nothing here draws a random number or allocates; `para_hash` is the trajectory's digest.
//
// ge:summary The paramotor: ParaState, ParaControls, ParaEvents, step_para, ready_para, drop_para
// ge:keywords paramotor para wing canopy lines polar stall surge brakes toggles speed-bar
// ge:keywords throttle engine thrust launch takeoff landing speed-riding fold collapse crown
// ge:keywords drop cut-away step_para ready_para drop_para para_hangs para_hash pilot-mass
// ge:keywords acro dive swing-up loop tumble slack tangle tangled aloft_para aloft in-flight
// ge:skill paramotor
// ge:test tests/paramotor_scenarios_test.cpp
#pragma once

#include <cstdint>

#include "core/quat.hpp"
#include "mountain/mapgen/types.hpp"
#include "paramotor/air.hpp"
#include "skier/skier.hpp"
#include "skier/skis.hpp"
#include "skier/trees.hpp"

namespace ge::paramotor {

// THE CONTROLS as the pilot works them, each clamped where it is read:
// - `throttle` the hand throttle, 0..1.
// - `brake` both brakes, 0..1: slower, a flare near the snow, a stall held down.
// - `steer` the toggles' difference and his weight in the harness, -1..1, right positive.
// - `bar` the risers, -1..1: forward (positive) the accelerator, back the trimmers let out.
struct ParaControls {
  double throttle = 0;
  double brake = 0;
  double steer = 0;
  double bar = 0;
};

// What the rig is doing (the header's THE RIDE).
enum class ParaMode : uint8_t { ready, flown, dropped, tangled };

const char* para_mode_name(ParaMode m);

// Why the rig came off him: let go by the press, the wing brought down onto the snow or into a
// crown, or the skier wiped out under it.
enum class ParaCut : uint8_t { none, drop, ground, crown, rider };

const char* para_cut_name(ParaCut c);

// A piece of the rig falling free once released: where, how fast, and whether it lies on the
// snow.
struct ParaPiece {
  double x = 0, y = 0, z = 0;
  double vx = 0, vy = 0, vz = 0;
  double heading = 0;
  bool down = false;
};

struct ParaState {
  ParaMode mode = ParaMode::ready;
  double x = 0, y = 0, z = 0;     // the canopy's centre, world, m
  double vx = 0, vy = 0, vz = 0;  // m/s
  double heading = 0;             // into the air it flies through, rad
  double bank = 0;                // its lines tilted across its way, right side down positive
  double pitch = 0;               // off the lines' plane, rad (what it is drawn by)
  double airspeed = 0;            // the air through it, m/s
  double alpha = 0;               // its angle of attack, rad
  bool stalled = false;
  double fold = 0;          // how much of the wing is collapsed, 0..1
  double fold_side = 0;     // -1 the left tip, 1 the right, 0 a frontal
  double wind = 0;          // the mean wind's speed at the wing, m/s
  double lift = 0;          // the air's rise off the slopes there, m/s
  double rough = 0;         // the eddies' sigma there, m/s
  double tension = 0;       // the pull on the lines, N (0 slack)
  core::Vec3 way{0, 0, 1};  // the way its leading edge points, square to the lines, unit
  ParaControls controls;    // as they stand after the hands' lag
  // THE ACRO (`acro.hpp`): the lines' turn about the pitch axis, unwrapped, rad (forward over
  // him positive), and the whole turns of it flown (LOOPS); the s the lines have hung slack with
  // the wing low; how hard the rears bite, 0..1; the pull he hangs on, g; and where the rears held
  // the wing last step (`hold_at` the mode's clock then, s; -1 none).
  double swing = 0;
  int32_t loops = 0;
  double slack = 0;
  double rears = 0;
  double g = 1;
  core::Vec3 hold{0, 0, 0};
  double hold_at = -1;
  double rpm = 0;       // the engine's
  double thrust = 0;    // N, this step
  double prop = 0;      // the propeller's turn, rad, wrapped
  bool flying = false;  // the skis off the snow under the wing past `launch::kFlight` s
  double agl = 0;       // the skis' height over the snow, m
  double climb = 0;     // m/s
  double t = 0;         // s in this mode
  double air_time = 0;  // s off the snow this flight
  int32_t flights = 0;  // the take-offs flown
  ParaCut cut = ParaCut::none;
  ParaPiece canopy;  // once dropped: the canopy, falling free
  ParaPiece motor;   // ...and the motor
};

// What one step did, for the game to turn into its events and the labs to count.
struct ParaEvents {
  bool launched = false;        // the wing let fly off the snow
  bool took_off = false;        // the skis off the snow and the pilot hung in the harness
  bool touched = false;         // the skis back on the snow under the flying wing
  bool folded = false;          // a fold began
  bool looped = false;          // the lines swung a whole turn round him
  bool tangled = false;         // he fell into his wing
  ParaCut cut = ParaCut::none;  // the rig came off this step
};

// What the wing flies over and in: the map's ground and trees, the trees' index, the eddies'
// field (`make_air_field`, once a map), the wind 10 m over open snow, and the caller's clock, s
// (the eddies are carried on it).
struct ParaWorld {
  const mountain::mapgen::Level* level;
  const skier::TreeIndex* trees;
  const ParaAirField* field;
  ParaWind wind;
  double now = 0;
};

// The mass hung on the lines, kg: the skier, his kit and the motor on his back.
double pilot_mass(const skier::SkiSpec& spec);

// A rig ready on the skier: the wing held inflated over him, a little back of plumb, the engine
// at idle.
ParaState ready_para(const skier::SkierState& s);

// A rig ALREADY FLYING over a pilot `s` put in the air: the wing plumb over his harness on its
// lines, both of them on `heading` at `speed`, m/s, the engine at `throttle`. What a game brings a
// paramotor onto the map with in flight (a pilot met aloft, not launched off the snow); `s` is
// given the same velocity and taken off the snow.
ParaState aloft_para(skier::SkierState& s, double heading, double speed, double throttle);

// Whether the skier hangs in the harness (or falls tangled in the wing) this step: his skis do
// nothing and the caller hands `step_skier` no input.
inline bool para_hangs(const ParaState& p) {
  return (p.mode == ParaMode::flown && p.flying) || p.mode == ParaMode::tangled;
}

// Whether the rig is on him.
inline bool para_rigged(const ParaState& p) { return p.mode != ParaMode::dropped; }

// ONE STEP of `dt` s on `in`, after the skier's own. A dropped rig's pieces fall and lie.
// Allocates nothing.
void step_para(ParaState& p, skier::SkierState& s, const ParaControls& in, const ParaWorld& world,
               const skier::SkiSpec& spec, double dt, ParaEvents& events);

// THE RELEASE: the canopy and the motor let go, falling free of him (`cut` says why).
void drop_para(ParaState& p, const skier::SkierState& s, ParaCut cut, ParaEvents& events);

// FNV-1a 64 over every number of the state, folded into `h`: the trajectory's digest.
uint64_t para_hash(const ParaState& p, uint64_t h);

}  // namespace ge::paramotor
