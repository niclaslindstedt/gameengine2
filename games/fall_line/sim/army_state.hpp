// THE ARMY ON FOOT AS A RUN HOLDS IT (`GameState::army`): every soldier out, what he is doing,
// and the counters that send more. Its own header so state.hpp can carry it without the rules
// that move it (army.hpp).
//
// A run whose hunt never sent a soldier leaves it inactive, hashes none of it and moves no digest.
//
// ge:summary The army as a run holds it: ArmyState, Soldier, ArmySled, ArmyHeli, ArmyPara, modes
// ge:keywords army army-state soldier soldier-mode soldier-from squad wave lift-squad troops
// ge:keywords army-change army-sled army-sled-mode army-sled-change army-heli army-heli-mode
// ge:keywords army-heli-change land-task army-para army-para-mode army-para-change
// ge:skill sim
// ge:test tests/fall_line_army_test.cpp
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "ai/steer.hpp"
#include "paramotor/air.hpp"
#include "paramotor/para.hpp"
#include "skier/skier.hpp"
#include "snowmobile/sled.hpp"

namespace fall_line::sim {

// Where a soldier came from: a squad up the lifts, a ground wave up from the valley, off one of
// the army's snowmobiles, or put down by one of its helicopters.
enum class SoldierFrom : uint8_t { lift, wave, sled, heli };

// What he is doing:
// - `walk` up the lane to the lift's load line; `wait` there for a carrier; `ride` it up;
// - `climb` with his wave up the way from the valley;
// - `chase` after the man (or to where he was last seen);
// - `hold` with him in sight and in range, where the army shoots to kill;
// - `arrest` kneeling over him, where it does not;
// - `out` off a landed helicopter: run out to his side and knelt facing out until it has lifted;
// - `rope` sliding down a hovering helicopter's fast rope;
// - `board` skiing to a helicopter come to pick his squad up;
// - `gone` sent home or aboard: taken off the map at the end of the step.
enum class SoldierMode : uint8_t {
  walk,
  wait,
  ride,
  climb,
  chase,
  hold,
  arrest,
  out,
  rope,
  board,
  gone
};

// What an army event says (`EventKind::army`: its `gate` the change, its `value` how many men): a
// lift's squad put down, a wave formed up, soldiers let off at a lift's top, the arrest, men sent
// home.
enum class ArmyChange : uint8_t { squad, wave, off, arrest, home };

struct Soldier {
  int32_t id = 0;  // the army's own, in the order sent; his pursuer id is `army::kPursuerBase` + it
  SoldierFrom from = SoldierFrom::lift;
  SoldierMode mode = SoldierMode::walk;
  int32_t squad = 0;           // the lift squad's, the wave's, or the machine's id
  double t = 0;                // s in this mode
  double x = 0, y = 0, z = 0;  // his feet, world frame, m
  double heading = 0;          // rad
  double speed = 0;            // m/s along `heading`
  bool climbing = false;       // working his way: skating, the herringbone, the traverse
  bool sees = false;           // whether he saw the man at his last look
  double tx = 0, tz = 0;       // where he is going, when told: his place in the lift's line
  // On a lift: which, the carrier that has him, and his seat on it.
  int32_t lift = -1;
  int32_t carrier = -1;
  int32_t seat = 0;
  // With his wave: the way he climbs (`GameState::ways`), the arc he climbs to next, the arc he
  // leaves the column at, and his file, m right of the way's line.
  int32_t way = -1;
  double s = 0;
  double s_end = 0;
  double lane = 0;
  // THE WATCH ON HIS PROGRESS (army_troop.hpp, ai/steer.hpp): where he was at the window's start
  // and when, how many windows in a row he barely moved, the side he steps off to and until when.
  ge::ai::Progress progress;
  // HIS FIRE (gunfire.hpp): the bursts fired this hold, and the clock before which he does not
  // stop to fire again.
  int32_t bursts = 0;
  double rest = 0;
};

// What one of the army's snowmobiles is doing (army_sled.hpp):
// - `due` called, its crew mounting up in the village;
// - `drive` after him, or to where he was last seen;
// - `halt` stopped side-on to him, the gunner on him;
// - `leave` the hunt over, driving home;
// - `empty` left standing, its crew off it after him on foot;
// - `gone` home or let go: taken off the map at the end of the step.
enum class ArmySledMode : uint8_t { due, drive, halt, leave, empty, gone };

// What an army snowmobile's event says (`EventKind::army_sled`: its `gate` the change, its `value`
// the machine's id): called, out of the village, halted, its crew off it to take him in, dug out,
// stood back on its belt, home.
enum class ArmySledChange : uint8_t { called, out, halt, dismount, dug, righted, gone };

struct ArmySled {
  int32_t id =
      0;  // the army's own, in the order called; its pursuer id `army_sled::kPursuerBase` + it
  ArmySledMode mode = ArmySledMode::due;
  double due = 0;  // the clock it leaves the village at, s
  double t = 0;    // s in this mode
  ge::snowmobile::SledState body;
  double home_x = 0, home_z = 0;  // where it set off from, m
  double way = 0;                 // the heading it steers, rad
  double pace = 0;                // the pace it is driven at, m/s
  bool blocked = false;           // he is up a face it cannot climb: it waits
  int32_t on_way = -1;            // the way it climbs up to him (`GameState::ways`), or -1
  double way_end = 0;             // ...to the arc nearest him, m
  double next = 0;                // the clock before which it does not halt again, s
  double saw = -1e9;              // the clock its crew last saw him, s
  // NOT GETTING STUCK: where it was at the window's start and when, how many windows in a row it
  // barely moved, the side it swings off to and until when (ai/steer.hpp), and how often it was
  // dug out.
  ge::ai::Progress progress;
  int32_t dug = 0;
  int32_t bursts = 0;  // the gunner's bursts this halt (gunfire.hpp)
};

// What one of the army's helicopters is doing (army_heli.hpp):
// - `due` called, not yet on the map (it comes in at `ArmyHeli::due`);
// - `hunt` after him: round him, off his tail, or combing where he was last seen;
// - `land` flying in to a landing's spot and down onto it (or to the hover over it);
// - `set` on the snow, its doors open; `rope` hovering, the men sliding down;
// - `rise` climbing straight back off it to the hunt;
// - `leave` flying out over the map's edge, the hunt over;
// - `gone` out over the edge (or taken by him): dropped at the end of the step;
// - `fall` rammed out of the sky, spinning down; `wreck` down on the snow (army_steal.hpp).
enum class ArmyHeliMode : uint8_t { due, hunt, land, set, rope, rise, leave, gone, fall, wreck };

// What an army helicopter's event says (`EventKind::army_heli`: its `gate` the change, its
// `value` the machine's id): called, in over the edge, on the snow or at the rope's hover, back
// off it, turned for home, gone; taken by him off the snow, rammed, down on the snow.
enum class ArmyHeliChange : uint8_t {
  called,
  arrived,
  landed,
  lifted,
  leaving,
  gone,
  stolen,
  rammed,
  down
};

// A LANDING asked of a machine: to put men DOWN near him (`drop`) or PICK UP a squad left behind,
// at a spot on the snow (m, the skid datum's place), set down on it facing `heading` (rad) or
// (`rope`) hovered over it while the men come down a fast rope. `count` men to put down, `put`
// out so far, the clock the last of them was clear (-1 not yet); `done` once it is over, and
// the machine lifts off.
struct LandTask {
  bool drop = true;
  double x = 0, y = 0, z = 0;
  double heading = 0;
  bool rope = false;
  int32_t count = 0;
  int32_t put = 0;
  double clear = -1;
  bool done = false;
};

struct ArmyHeli {
  int32_t id = 0;  // the army's own, in the order called; pursuer `army_heli::kPursuerBase` + it
  ArmyHeliMode mode = ArmyHeliMode::due;
  double due = 0;                 // the clock it comes in at, s
  double t = 0;                   // s in this mode
  double bearing = 0;             // from the map's middle, the way it comes in on, rad
  double x = 0, y = 0, z = 0;     // the skid datum, world frame, m
  double vx = 0, vy = 0, vz = 0;  // m/s
  double ax = 0, az = 0;          // the acceleration it pulls, m/s^2 (smoothed: the bank follows)
  double heading = 0, pitch = 0, roll = 0;  // rad, the engine's sign conventions
  double yaw_rate = 0;                      // rad/s
  bool on = false;     // it has reached the hunt: within the search's circle of where he was seen
  int32_t dir = 1;     // the way round its circle: 1 clockwise from above (the right door inward)
  double doors = 0;    // how far the sliding doors stand open, 0..1
  int32_t aboard = 0;  // men aboard to put down
  // Each door gun's lay (the right door's, then the left's): its swing off square out of its door,
  // forward positive, and its lift, rad (army_door.hpp). Not hashed: what it does shows in the
  // gunfire's.
  std::array<double, 2> lay_yaw{};
  std::array<double, 2> lay_pitch{};
  bool tasked = false;
  LandTask task;
};

// What an army paramotor is doing (army_para.hpp):
// - `due` called, coming in at its clock;
// - `hunt` over him: circling where he is seen, searching where he was last seen;
// - `leave` flying out over the map's edge, the hunt over;
// - `down` its wing brought down onto the snow or into a crown, the soldier falling or lying;
// - `gone` out over the edge or lain long enough: dropped at the end of the step.
enum class ArmyParaMode : uint8_t { due, hunt, leave, down, gone };

// What an army paramotor's event says (`EventKind::army_para`: its `gate` the change, its `value`
// the rig's id): called, in over the edge, turned for home, gone, brought down.
enum class ArmyParaChange : uint8_t { called, arrived, leaving, gone, down };

// AN ARMY PARAMOTOR: a rifleman in the harness (`pilot`, a skier's state hung under the wing, the
// free ride's own physics) and his rig (`wing`).
struct ArmyPara {
  int32_t id = 0;  // the army's own, in the order called; pursuer `army_para::kPursuerBase` + it
  ArmyParaMode mode = ArmyParaMode::due;
  double due = 0;      // the clock it comes in at, s
  double t = 0;        // s in this mode
  double bearing = 0;  // from the map's middle, the way it comes in on and leaves by, rad
  int32_t dir = 1;     // the way round its circle: 1 clockwise from above
  double next = 0;     // the clock before which he fires no burst, s
  bool low = false;    // off the circle and down the fall line until back over `kFloorM`
  ge::skier::SkierState pilot;
  ge::paramotor::ParaState wing;
};

struct ArmyState {
  bool active = false;  // a soldier was sent this run: only then is any of it hashed
  // THE SOLDIERS OUT, reserved for `army::kMostSoldiers` at create, so a step never allocates.
  std::vector<Soldier> soldiers;
  int32_t next_id = 0;
  // THE LIFTS' SQUADS: whether one is called, the clock it is due at, and how many were sent.
  bool lift_called = false;
  double lift_due = 0;
  int32_t lift_squads = 0;
  // THE GROUND WAVES: how many were sent this run (each one's number, and the row it is read at),
  // the stars they are being counted at and how many of those have gone, and their men sent.
  int32_t waves = 0;
  int32_t wave_star = 0;
  int32_t wave_at = 0;
  int32_t marched = 0;
  int32_t arrested_by = -1;  // the soldier who took him, or -1
  // THE SNOWMOBILES OUT, reserved for `army_sled::kMostSleds` at create; the next one's id, how
  // many were called this run (each one's place beside the spot), how many a wave keeps out on
  // top of the roster's, and where they set off from (beside the free ride's snowmobile spot).
  std::vector<ArmySled> sleds;
  int32_t next_sled = 0;
  int32_t sleds_called = 0;
  int32_t wave_sleds = 0;
  double pad_x = 0, pad_z = 0, pad_heading = 0;
  // THE HELICOPTERS OUT, reserved for `army_heli::kMostHelis` at create; the next one's id, how
  // many were called this run (each one's bearing either side of the first's), the bearing from
  // the map's middle the army comes in from (toward him when the first is called), and the men
  // they put down and picked up again.
  std::vector<ArmyHeli> helis;
  int32_t next_heli = 0;
  int32_t helis_called = 0;
  double from = 0;
  int32_t dropped = 0;
  int32_t picked = 0;
  // The machines he took off the snow and brought down this run (army_steal.hpp).
  int32_t stolen = 0;
  int32_t downed = 0;
  // Whether the hunt sends its helicopters. A lab or a test staging the army's other arms on
  // their own turns it off; the game never does.
  bool send_helis = true;
  // THE PARAMOTORS OUT, reserved for `army_para::kMostParas` at create; the next one's id, how many
  // were called this run (each one's bearing either side of the army's), how many were brought
  // down by the snow or a crown (a fault the labs hold at nought), and the eddies' field their
  // wings fly in (made off the map's seed when the first is called).
  std::vector<ArmyPara> paras;
  int32_t next_para = 0;
  int32_t paras_called = 0;
  int32_t paras_lost = 0;
  ge::paramotor::ParaAirField para_field{};
  // THE SOLDIERS BY PLACE, built each step before they move (ai/steer.hpp): who keeps apart from
  // whom. Scratch: never hashed.
  ge::ai::Neighbours near;
  // Whether its men fire (gunfire.hpp). A lab or a test staging the army's moves without the
  // gunfire turns it off; the game never does.
  bool fire = true;
  // Whether the paramotors' eddies' field (`para_field`) has been made.
  bool para_air = false;
  // Whether the hunt sends its paramotors. A lab or a test staging the army's other arms on their
  // own turns it off; the game never does.
  bool send_paras = true;
};

}  // namespace fall_line::sim
