// THE STATE OF A RUN: the map, the mode (a race down the course, or the free ride over the ski
// area's runs), the field of racers on it (each a skier, his progress down the course and his
// free ride), the run's clock and this step's events. `create_game` builds it (game.hpp) and
// `step` is the only thing that writes it during a run; a pilot, a lab or a renderer reads it.
//
// Every buffer a step writes is sized at create time: `racers` never grows, `events` is
// reserved for `kEventsReserve` a step and a free ride's `skied` for every run, so a step
// allocates nothing.
//
// ge:summary The state of a run: GameState, Racer, Progress, Controls, the events
// ge:keywords game-state state racer progress controls events event-kind gate missed finish
// ge:keywords reset wipeout landing takeoff field clock wanted mode free-ride onto arrived skied
// ge:keywords lift-ride lift-phase boarded unloaded sled free-sled sled-mode sled-phase machine
// ge:keywords army ways heli free-heli heli-mode heli-phase lever pedal para free-para
// ge:keywords para-phase paramotor crowd bump
// ge:skill sim
// ge:test tests/fall_line_sim_test.cpp
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "helicopter/heli.hpp"
#include "mountain/lifts/lift_loop.hpp"
#include "mountain/mapgen/types.hpp"
#include "paramotor/air.hpp"
#include "paramotor/para.hpp"
#include "skier/body.hpp"
#include "skier/skier.hpp"
#include "skier/skis.hpp"
#include "skier/trees.hpp"
#include "snowmobile/sled.hpp"

#include "ai/why.hpp"
#include "fall_line/sim/army_state.hpp"
#include "fall_line/sim/crowd_state.hpp"
#include "fall_line/sim/gunfire_state.hpp"
#include "fall_line/sim/wanted_state.hpp"

namespace fall_line::sim {

// THE CLOCK every run is stepped on: 120 steps a second (the edge and the landing's contact
// want it, and every pilot decides on every step).
inline constexpr double kHz = 120;
inline constexpr double kDt = 1 / kHz;

// What a racer is asked this step: the skis' controls, the reset (stand me back on the piste
// past the last gate I took), his weight fore and aft (-1 forward .. 1 back: on the free ride's
// snowmobile, the climber's stance; on its helicopter, the cyclic fore and aft; under the
// paramotor's wing, the speed bar's opposite: forward is the bar pushed; nothing on skis),
// THE MACHINE PRESS (on the free ride: take the snowmobile or the helicopter stood beside it, or
// get off it; under the paramotor's wing: let the rig go; sled_ride.hpp, heli_ride.hpp,
// para_ride.hpp), and the helicopter's own two: the collective
// `lever` (0 down .. 1 up) and the `pedal` (-1 left .. 1 right). On the helicopter the steer is
// the cyclic side to side.
struct Controls {
  ge::skier::SkierInput ski;
  bool reset = false;
  double lean = 0;
  bool machine = false;
  double lever = 0;
  double pedal = 0;
};

enum class EventKind : uint8_t {
  gate,       // a gate taken (`gate` its index; the start gate is 0)
  missed,     // the owed gate skied past outside its width
  finish,     // the finish line crossed (`value` the run's time, s)
  takeoff,    // a flight began (it passed the skier's least flight)
  landing,    // back on the snow (`value` the landing's load, g)
  wipeout,    // went down (`gate` the skier's Wipeout as a number)
  glance,     // brushed a trunk and kept going
  reset,      // stood back on the piste (`gate` the gate owed)
  shoulder,   // two racers met (`racer` and `gate` the two, `value` the closing speed)
  wanted,     // the wanted level moved (`gate` the stars after, `value` its WantedChange)
  onto,       // a free ride came onto a run (`gate` the run's index in the ski area)
  arrived,    // a free ride reached a run's foot (`gate` the run, `value` the clock, s)
  boarded,    // a free ride came into a lift's load zone (`gate` the lift's index)
  unloaded,   // a lift let him go at its top (`gate` the lift, `value` the ride's time, s)
  sled,       // the snowmobile (`gate` its SledPhase, `value` its speed, m/s; sled_ride.hpp)
  army,       // the army on foot moved (`gate` its ArmyChange, `value` how many men)
  heli,       // the helicopter (`gate` its HeliPhase, `value` its speed, m/s; heli_ride.hpp)
  para,       // the paramotor (`gate` its ParaPhase, `value` his speed, m/s; para_ride.hpp)
  army_sled,  // an army snowmobile (`gate` its ArmySledChange, `value` its id; army_sled.hpp)
  bump,       // a racer met one of the crowd (`gate` the amateur's id, `value` the closing speed)
  army_heli,  // an army helicopter (`gate` its ArmyHeliChange, `value` its id; army_heli.hpp)
  gunfire,    // a round fired, into him, into the snow or a trunk, or past him (gunfire.hpp)
  hurt,       // injuries taken (`gate` how many this step, `value` the body's severity score after)
  died,       // the body gave out (`value` its severity score); his run is over
  army_para,  // an army paramotor (`gate` its ArmyParaChange, `value` its id; army_para.hpp)
};

const char* event_name(EventKind kind);

struct GameEvent {
  EventKind kind;
  int32_t racer;
  int32_t gate;
  double value;
};

inline constexpr size_t kEventsReserve = 64;

// How far down the course a racer is.
struct Progress {
  int32_t next = 0;     // the gate owed: 0 the start gate ... the last the finish line
  bool missed = false;  // the owed gate was skied past; nothing counts until it is taken
  bool finished = false;
  double finish_time = 0;  // s, the clock at the line, to the share of the step it was crossed
  int32_t resets = 0;
  int32_t wipeouts = 0;
  int32_t missed_gates = 0;  // gates skied past (each counted once)
  double stuck = 0;          // s going nowhere on his skis
};

// How a racer rides when the bot has his controls (field.hpp deals it): the share of the bot's
// speed plan he dares, and how far right of the piste's centre he holds his line, m.
struct Rider {
  double pace = 1;
  double lane = 0;
};

// What a run is: a RACE down the course's gates to the finish, or the FREE RIDE over the ski
// area's runs (free.hpp), with no gate and no finish line.
enum class Mode : uint8_t { race, free };

// Where a free ride has been (free.hpp): the run under his skis, the last one he was on and how
// far down it, every run skied (in the order first skied), the metres skied, and the foot.
// Where a lift has him (lift_ride.hpp): not at all, waiting in its load zone for a carrier, or
// carried up the line on one.
enum class LiftPhase : uint8_t { none, waiting, carried };
struct OnLift {
  LiftPhase phase = LiftPhase::none;
  int32_t lift = -1;     // the lift's index in the ski area
  int32_t carrier = -1;  // the carrier he is on, once carried
  double since = 0;      // s, the clock when he was taken from the load zone
  int32_t ridden = 0;    // lifts ridden to the top this run
};

struct FreeRide {
  int32_t start = 0;           // the run he was stood at the head of
  int32_t run = -1;            // the run under his skis, or -1 off every run
  int32_t last = -1;           // the last run he was on, or -1 before any
  double last_s = 0;           // m down `last` he was last on it
  std::vector<int32_t> skied;  // reserved for every run at create
  double distance = 0;         // m skied over the plan, on the snow
  bool arrived = false;        // at a run's foot (since the last lift let him go)
  double arrived_at = 0;       // s, the clock when he got there
  OnLift lift;                 // the lift that has him, and the lifts ridden
};

// THE FREE RIDE'S SNOWMOBILE (sled_ride.hpp): the machine's body (snowmobile/sled.hpp), what it
// is doing, whether the player is on its boards and whether he was thrown off it, the seconds in
// this mode, and how many times it has been taken.
enum class SledMode : uint8_t { parked, ridden, down };
// What a `sled` event says: taken, hopped off, the rider thrown, stood back on its belt.
enum class SledPhase : uint8_t { board, hop, crash, right };
struct FreeSled {
  bool present = false;  // every free ride on a map has one; a race none
  ge::snowmobile::SledState body;
  SledMode mode = SledMode::parked;
  bool rider = false;
  bool thrown = false;
  double t = 0;         // s in this mode
  int32_t taken = 0;    // times boarded this run
  double still = 0;     // s at rest with nobody on it
  bool asleep = false;  // at rest on its parking brake, not stepped until something wakes it
};

// THE FREE RIDE'S HELICOPTER (heli_ride.hpp): the machine's body (helicopter/heli.hpp), its pad,
// what it is doing, whether the player is sat on its skid, whether he was aboard when it crashed,
// the seconds in this mode, the controls it was last flown on and how many times it was taken.
enum class HeliMode : uint8_t { parked, flown, home, wreck };
// What a `heli` event says: sat on the skid, dropped off it, crashed, set down home on its pad,
// the ride started again from the pad.
enum class HeliPhase : uint8_t { board, drop, crash, home, restart };
struct FreeHeli {
  bool present = false;  // every free ride on a map has one; a race none
  ge::helicopter::HeliState body;
  double pad_x = 0, pad_z = 0, pad_heading = 0;
  HeliMode mode = HeliMode::parked;
  bool rider = false;
  bool aboard = false;                 // the wreck's: he was on its skid when it came down
  double t = 0;                        // s in this mode
  ge::helicopter::HeliControls flown;  // the controls of the last step flown
  int32_t taken = 0;                   // times boarded this run
  bool army = false;  // the army's machine he took (army_steal.hpp): no pilot to fly it home
};

// THE FREE RIDE'S PARAMOTOR (para_ride.hpp): a free ride begun under it on the summit. The rig's
// body (paramotor/para.hpp), the air's eddies dealt off the map's seed, where the ride begins
// and how many times it has.
// What a `para` event says: the wing let fly, off the snow, touched down, a fold, the rig come off
// him (let go, the wing down on the snow, in a crown, or he went down under it), the ride begun
// again on the summit, a loop flown, and a fall into his own wing (`paramotor/acro.hpp`).
enum class ParaPhase : uint8_t {
  launch,
  take_off,
  touch,
  fold,
  drop,
  ground,
  crown,
  rider,
  restart,
  loop,
  tangle
};
struct FreePara {
  bool present = false;  // a free ride begun under it (create_para_ride); none otherwise
  ge::paramotor::ParaState body;
  ge::paramotor::ParaAirField field;
  double start_x = 0, start_z = 0, start_heading = 0;
  int32_t starts = 0;  // times the ride was begun on the summit
};

struct Racer {
  ge::skier::SkierState skier;
  Progress progress;
  Rider rider;
  FreeRide free;
  // THE BODY (skier/body.hpp): what his skis' blows and the hunt's rounds have done to him,
  // stepped after his skis under the INJURIES switch (gunfire.hpp judges the rounds); mended at
  // a reset. Dead (`body.mortal >= 0`), his run is over: he lies where he fell.
  ge::skier::BodyState body;
};

struct GameState {
  std::shared_ptr<const ge::mountain::mapgen::Level> level;
  Mode mode = Mode::race;
  // A free ride's runs as lines the skier is found on (free.hpp's `run_tracks`), each index built
  // at create; none in a race.
  std::shared_ptr<const std::vector<ge::mountain::mapgen::Track>> runs;
  // A free ride's lifts, planned at create (lift_loop.hpp); none in a race.
  std::shared_ptr<const std::vector<ge::mountain::lifts::LiftPlan>> lifts;
  ge::skier::TreeIndex trees;
  ge::skier::SkiSpec spec;
  std::vector<Racer> racers;      // the player's first; sized at create
  std::vector<GameEvent> events;  // this step's, cleared at its start
  // The ways a ground wave climbs (army.hpp's `army_ways`), built at create.
  std::shared_ptr<const std::vector<ge::mountain::mapgen::Track>> ways;
  WantedState wanted;    // the hunt for the player (wanted.hpp); inactive until raised
  ArmyState army;        // the army on foot after him (army.hpp); inactive until sent
  GunfireState gunfire;  // the army's guns and their rounds (gunfire.hpp); inactive until armed
  FreeSled sled;         // the free ride's snowmobile (sled_ride.hpp); none in a race
  FreeHeli heli;         // the free ride's helicopter (heli_ride.hpp); none in a race
  FreePara para;         // the free ride's paramotor (para_ride.hpp); only a ride begun under it
  CrowdState crowd;      // the amateurs on the ski area (crowd.hpp); inactive until dealt
  // THE INJURIES SWITCH: every racer's body stepped after his skis, blows hurting and killing.
  // Off, the skis' blows never touch the bodies and no digest reads them.
  bool injuries = false;
  double t = 0;  // s since the start
  int64_t steps = 0;
  // WHERE THE GAME'S OWN AGENTS RECORD WHY (ai/why.hpp): the army's machines write their decisions
  // here while a trace is on (`ge why`); null in a normal run. Observed, never hashed, and nothing
  // decides by it.
  ge::ai::Why* why = nullptr;

  ge::skier::World world() const { return ge::skier::World{level.get(), &trees}; }
};

}  // namespace fall_line::sim
