// THE FREE RIDE'S HELICOPTER: a light helicopter (`plugins/helicopter`) waiting on its pad on the
// valley floor, the player's way up the mountain to anywhere the lifts never reach.
//
// - THE PAD (`heli_pad`): a level patch of open snow as near the village as one lies, the
//   rotor's sweep and a margin (`kPadRadius`) clear of every trunk, every lift's line and bottom
//   station, the wind tunnels' lanes and the snowmobile's spot, faced to the summit. A map with no
//   ski area puts it beside the piste's foot. A pure function of the map.
// - TAKEN (`Controls::machine`, the machine press): a skier on his skis, off any lift and any
//   machine, within `kBoardReach` m of the parked machine's seat and no faster than
//   `kBoardFastest` m/s is sat on its right skid (a `heli` event, `board`). The rotor spools up.
// - FLOWN BY HIM (`heli_controls`): the lever the collective, `Controls::lean` the cyclic fore and
//   aft (forward is down the stick, -1), the steer the cyclic side to side and the pedal the
//   pedals, through the edge's hand (`heli_turned_back`). Nothing else between him and the rotor.
//   He is held on the seat (`heli_seat`), facing out over the skid and moving with it; his skis,
//   the run under them and the run's own resets wait. His reset starts the ride again from the pad.
// - THE DROP (the machine press again): pushed off the skid with the machine's way and a little
//   out and up (`kDropOut`, `kDropUp`), or, landed, stepped off onto the snow. In the air the
//   machine's pilot takes it after a beat (`kHomeBeat`) and flies it home to its pad at
//   `kHomeClear` m over the snow; landed, he shuts down where it stands.
// - HOME: set down within `kHomeNear` m of its pad, the pilot shuts down (`home`).
// - AN ARMY MACHINE TAKEN (`take_heli`, from `army_steal.hpp`): the free ride's helicopter becomes
//   the set-down army machine he took at its pilot's door, stood where it stood with its rotor
//   turning
//   (`FreeHeli::army`), and he is boarded on it; nobody flies it home. `crash_heli` wrecks the one
//   he is flying with its way, as a ram does.
// - THE CRASH (helicopter/heli.hpp's judgement: a hard landing, the rotor's tips, the airframe, a
//   crown): a `heli` event, `crash`, the machine down where it struck; the skier on its skid thrown
//   off by the blast (`kBlastOut`, `kBlastUp`; a `wipeout`). Once he is stood up (the run's down
//   reset) or the wreck has burned `kWreck` s, the ride starts again from the pad, a fresh machine
//   spooled up with him sat on it (`restart`). Dropped off before it went down, a fresh machine
//   waits on the pad instead.
// - LEFT ON ITS PAD with the rotor still the machine is not stepped (its numbers do not move)
//   until someone takes it.
//
// Only the player (racer 0) rides it. Pure over the map and the state: draws nothing, and a step
// allocates nothing.
//
// ge:summary The free ride's helicopter: the pad, boarding, flown, the drop, home, the wreck
// ge:keywords heli helicopter free-ride heli_pad place_heli step_heli_ride board drop home
// ge:keywords crash wreck restart machine-press seat heli_controls flown-home pad
// ge:skill sim helicopter
// ge:test tests/fall_line_heli_test.cpp
#pragma once

#include <cstdint>
#include <vector>

#include "core/math.hpp"
#include "fall_line/sim/state.hpp"
#include "helicopter/heli.hpp"
#include "mountain/lifts/lift_loop.hpp"
#include "mountain/mapgen/types.hpp"
#include "skier/trees.hpp"

namespace fall_line::sim {

namespace heli_ride {
// The pad's search: its grid, m, and how far out from the village, m.
inline constexpr double kStep = 8;
inline constexpr double kSearch = 420;
// The room kept clear round the pad, m (the rotor's sweep and a margin), and the steepest the
// snow under it may lean (rise over run).
inline constexpr double kPadRadius = 14;
inline constexpr double kPadSlope = 0.08;
// The gaps kept from a lift's line, its bottom station and a tunnel's lane, m.
inline constexpr double kLineGap = 16;
inline constexpr double kStationGap = 34;
inline constexpr double kTunnelGap = 14;
// The seat on the right skid, body frame off the skid datum, m: across, and over the datum where
// the seated skier's body stands.
inline constexpr double kSeatUp = 0.9;
// BOARDING: within this of the seat, m, no faster than this, m/s (a skier's cruise).
inline constexpr double kBoardReach = 6;
inline constexpr double kBoardFastest = 12;
// THE DROP: the push he leaves the skid with, out and up, m/s.
inline constexpr double kDropOut = 1.6;
inline constexpr double kDropUp = 0.6;
// FLOWN HOME: the beat the controls stay where he left them, s, the height kept over the snow,
// m, and how near the pad a landing is home, m.
inline constexpr double kHomeBeat = 1;
inline constexpr double kHomeClear = 70;
inline constexpr double kHomeNear = 15;
// THE CRASH: the blast's push off the skid, out and up, m/s, and how long the wreck burns before
// the ride starts again, s.
inline constexpr double kBlastOut = 15;
inline constexpr double kBlastUp = 10;
inline constexpr double kWreck = 7;
}  // namespace heli_ride

// Where the machine stands: the skid datum's place on the snow and the way it faces.
struct HeliPad {
  double x = 0, z = 0;
  double heading = 0;
};

// THE GROUND'S STEEPEST LEAN over a circle of radius `r` round (x, z), rise over run.
double pad_lean(const ge::mountain::mapgen::Level& level, double x, double z, double r);

// WHETHER A MACHINE MAY STAND at (x, z): `room` m clear of every trunk, and that and the gaps
// clear of every lift's line and bottom station and every tunnel's lane.
bool pad_clear(const ge::mountain::mapgen::Level& level,
               const std::vector<ge::mountain::lifts::LiftPlan>& lifts,
               const ge::skier::TreeIndex& trees, double x, double z, double room);

// THE PAD, kept clear of the circle `avoid_r` m round (avoid_x, avoid_z) as well (the snowmobile's
// spot; a radius of 0 avoids nothing).
HeliPad heli_pad(const ge::mountain::mapgen::Level& level,
                 const std::vector<ge::mountain::lifts::LiftPlan>& lifts,
                 const ge::skier::TreeIndex& trees, double avoid_x, double avoid_z, double avoid_r);

// The free ride's machine parked on its pad, its rotor still (create time, after the snowmobile).
void place_heli(GameState& state);

// WHAT HIS HANDS DO, off his own controls: the lever, the stick and the pedals.
ge::helicopter::HeliControls heli_controls(const Controls& c);

// The machine's world: the map, its trees, no wind.
ge::helicopter::HeliWorld heli_world(const GameState& state);

// The seat on its right skid, in the world.
ge::core::Vec3 heli_seat(const ge::helicopter::HeliState& h);

// THE MACHINE'S STEP, before racer `i`'s skis: flown, flown home, parked or a wreck, and the skier
// taken on, held on its skid or let go. A press it spends (a board or a drop) is cleared from `c`,
// so nothing else this step reads it again. True while he is sat on it: the step is the
// machine's, and his skis, his run and the run's resets wait.
bool step_heli_ride(GameState& state, int32_t i, Controls& c);

// Whether the machine press sits racer `i` on the skid now.
bool heli_within(const GameState& state, int32_t i);

// Whether racer `i` is sat on the machine's skid.
inline bool on_heli(const GameState& state, int32_t i) {
  return i == 0 && state.heli.present && state.heli.rider;
}

// THE RUN'S RESET while the machine has him: on its skid, or lying where its wreck threw him, the
// ride started again from the pad with him sat on a fresh machine. False when it has nothing of
// his (the free ride's own reset is the caller's).
bool restart_heli(GameState& state, int32_t i);

// A MACHINE TAKEN FROM THE ARMY (army_steal.hpp): the free ride's helicopter made the one standing
// at (x, z) facing `heading`, its rotor at `spool` (0..1), with racer `i` sat on it. It has no
// pilot of its own (`FreeHeli::army`); once wrecked, or he is set back on the pad, the free
// ride's own machine waits on its pad again.
void take_heli(GameState& state, int32_t i, double x, double z, double heading, double spool);

// RAMMED (army_steal.hpp): the free ride's helicopter crashes where it is, as into the snow.
void crash_heli(GameState& state, int32_t i);

const char* heli_phase_name(HeliPhase p);

}  // namespace fall_line::sim
