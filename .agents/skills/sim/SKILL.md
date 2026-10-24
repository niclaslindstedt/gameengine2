---
name: sim
description: "Use for games/fall_line/sim (Fall Line's own simulation): create_game and the 120 Hz step's order, the modes, the free ride, the course, the field, the bot, the headless sim CLI, and engine/loop's run clock. The mountain generator is the mountain skill's."
---

# The Fall Line simulation

`games/fall_line/sim/` is Fall Line's own simulation:
headless, deterministic and platform-free. It may include only `engine/core`, `engine/loop`,
the plugins it links and itself. What another game of the genre would reuse goes in a plugin
instead (the `plugins` skill): the generator is `plugins/mountain` (the `mountain` skill), the
tape and standings `plugins/racing`, and the skier on the snow `plugins/skier` (the `skier`
skill: `step_skier`, the snow, the limits a bot plans on, the ride scenarios).

## What it is

`games/fall_line/sim/` (`fall_line::sim`, target `fall_line_sim`, linked by the labs and the
tests):

| File | What |
| --- | --- |
| `state.hpp` | `GameState` (the map, the trees' index, the pair, the racers, this step's events, the clock, the INJURIES switch `injuries`), `Racer` (his skis, progress, rider, free ride and `body`, stepped by `step_body` after his skis with the switch on (off, only once the hunt's rounds hurt him mortally), mended at a reset), `Rider` (a bot's pace and lane), `Progress`, `Controls`, the events; `kHz` = 120 |
| `game.hpp/.cpp` | `race_map(seed)` (v1, the single piste), `create_game`, `create_game_on`, `step` (THE ORDER of a step, stated once in the header), `state_hash` |
| `course.hpp/.cpp` | The gates in order, a miss, the finish's clock inside its step, the reset (`reset_racer`), `bearing_to_next` |
| `free.hpp/.cpp` | THE FREE RIDE (`Mode::free`): `free_map` (the ski area), `run_tracks` (its runs as lines, each index built at create), `create_free_ride` (the player alone at a run's head), `step_free` (the metres, the run under the skis, `onto`, the foot's `arrived`), `reset_free` (down the run he was last on), `run_chain` (a run and every run it flows into, to a foot) |
| `controls.hpp` | The control tape's schema (steer, tuck, brake, flags) and `snap_controls` |
| `bot.hpp/.cpp` | `plan_bot` and `plan_free` (the line a plan is laid on, `BotPlan::track`: the course's piste, or a run's chain on the free ride; its kickers' lips, and on a course whose slalom gates stand off the line the weave through them; the speed plan is the skier's pilot's, `skier/pilot.hpp`) and `bot_controls` (the `mode` decision: a lift, the stop at the foot, a reset for a missed gate; then the skier's pilot flown as the racer's `Rider`) |
| `field.hpp/.cpp` | The rivals: `deal_field` (each rival's pace off a stream of the map's seed, his lane off his grid slot), `meet_racers` (skier against skier: the shoulder), `standings` and `place_of` (off `plugins/racing`) |
| `simulate.hpp/.cpp` | `Driver` (THE ONE STEP every pilot drives: snapped, taped, the rivals' bots, `step`, hashed), `simulate`, `simulate_on`, `simulate_free` (`SimOptions::injuries` the switch; a dead player ends the drive), `replay_tape`, the `RunReport` (the body's injuries, severity, hardest blow, time of death) and its `FieldRow`s |
| `wanted.hpp/.cpp`, `wanted_state.hpp`, `wanted_rules.hpp` | THE WANTED LEVEL: `raise_heat` (the offence hook), `step_wanted` (the look, the climb, the search running out), `sees_quarry` and `through_woods` (the sight line), `under_cover`, `arrest_quarry`, `roster_of`, `ground_wave`; the state `GameState::wanted` (inactive and unhashed until raised); every number in `wanted_rules.hpp` |
| `army.hpp/.cpp`, `army_state.hpp`, `army_rules.hpp`, `army_troop.hpp/.cpp` | THE ARMY ON FOOT: `step_army` (the lifts' squads by `lift_for`, the ground waves up `GameState::ways` from `army_ways`, the chase, the hold, the arrest, the men sent home, every soldier written into the hunt's pursuers), `go_toward` / `stop_here` / `watch_progress` (a soldier's way over the snow); the state `GameState::army` (inactive and unhashed until a hunt reaches a star); every number in `army_rules.hpp` |
| `army_heli.hpp/.cpp`, `army_heli_rules.hpp`, `army_land.hpp/.cpp` | THE ARMY'S HELICOPTERS: `step_army_helis` (called from `step_army` after the snowmobiles: the call in over the map's edge, the flight as a point with a pilot's limits under `army_floor`, the pylon turn, the echelon, the search and its clock held until they reach it, the landings asked once a second and the overwatch, a section set down or slid down a fast rope, the leapfrog's pick-up, home), `step_heli_soldier` (a man `out`, on the `rope`, or going to `board`), `land_spot` (a flat clear place ahead of him, else a rope's); each machine's `station` and `landing` decisions on `GameState::why`; every number in `army_heli_rules.hpp` |
| `army_door.hpp/.cpp` | THE DOOR GUNNERS (M15e2): `step_army_doors` (called at the end of `step_army_helis`: the roster's gunners two to each hunting machine, the inward door first; the doors slid open with him in sight; each manned gun laid onto him inside its arc at a man's pace and fired through `fire_at` once laid, its door open and he in sight and range), `gunners_of`, `doors_manned` (which the echelon reads to crab the machine side-on); every number in `army_heli_rules.hpp` |
| `gunfire.hpp/.cpp`, `gunfire_state.hpp`, `gunfire_body.hpp/.cpp`, `weapons_rules.hpp` | THE GUNFIRE (M15b): `arm_gun`, `fire_at` (a burst aimed, led and held over, the hold drawn once a burst and widened by the stars, warning fire put down off him), `step_gunfire` (the rounds at the cyclic rate, flown under drag and gravity, struck into the snow, a trunk or his body; the wounds judged on `Racer::body`), `body_points`/`body_hit` (capsules round his stance); its own stream off the map's seed; every number in `weapons_rules.hpp` |
| `army_sled.hpp/.cpp`, `army_sled_rules.hpp`, `army_drive.hpp/.cpp` | THE ARMY'S SNOWMOBILES: `step_army_sleds` (called from `step_army` after the men: the call from beside the snowmobile spot, `sled_pad`, the chase up the runs and at him, the short halt, the stand-off, the crew off to take him in, righted, dug out, waiting below a face, home), each on the free ride's own `step_sled` and `sled_pilot`; `choose_way` / `way_cost` / `tunnel_within` (the snow read ahead, for every army machine); every number in `army_sled_rules.hpp` |
| `play.hpp/.cpp` | `PlaySession`: an agent at the controls over JSON lines (`hello`, `observe`, `act`, `tape`, `end`); THE PROTOCOL is stated once in its header |

The generator is `plugins/mountain` (the `mountain` skill); a map is a pure function of its
seed. The skier on the snow is `plugins/skier` (the `skier` skill).

## Order of work

1. The skier on the snow: `plugins/skier` (M5a, done).
2. The run: the state, the course, the step, the bot, `simulate` and the `sim` lab (M5b, done).
3. The field and an agent at the controls: rivals on one grid, skier against skier, the
   standings off `plugins/racing`, and the `play` lab over stdio (M5c, done).
4. The free ride's run (M13a1, done): a run's mode, the run under the skis, the foot, the
   reset, the bot down a run's chain. The game opens PLAY on the free ride; its races and
   tricks stay behind the developer switch, kept and not grown.
5. The wanted level (M15a, done): the hunt's state and rules, scripted pursuers in the `wanted`
   lab until the army's pilots (M15c–M15e) write them.
6. The lifts (M13a2, done): the carriers off `plugins/mountain/lifts`, boarding in the load
   zone, the ride, let go at the top (`lift_ride.hpp`); the bot's outing (`outing.hpp`).
7. The army on foot (M15c, done): the troop model, the lifts' squads, the ground waves' column,
   the chase, the stand-off and the arrest; the `army` lab.
8. The army's snowmobiles (M15d1, done): the call, the drive, the short halt, the arrest; the
   `army-sleds` lab.
9. The army's helicopters (M15e1, done): the call, the pylon turn, the search, the landings and
   the fast rope, the leapfrog, home; the `army-flight` lab.
10. Gunfire and wounds (M15b, done): the guns, the bursts, the rounds' flight and strikes, the
   wounds judged on the body, death ending the hunt; the `gunfire` lab.
11. The door gunners (M15e2, done): the roster's gunners on the hunting machines' door guns, laid
   in their arcs and fired by the rules of fire; the `gunfire` lab's doors table.
12. The paramotor riflemen (M15e3, done): the free ride's wing flown by the army's hands round a
   circle over him, the rifle, home; the `army-para` lab. The hunt's server cvars (`sv_lethal`,
   `sv_armyfire`, `sv_helis`, `sv_paras`) reach a run through `apply_hunt` (`hunt_cfg.hpp`),
   once before its first step; a new switch for the hunt is a field there and a cvar beside them.
13. An army helicopter taken or brought down (M15e5, done): taken at a set-down machine's pilot's
   door (the free ride's helicopter becomes it, `FreeHeli::army`), rammed by his machine (theirs
   falls spinning and lies as a wreck); `army_steal.hpp`, the `army-flight` lab's `steal` and
   `ram`.
14. Next: the free ride's other systems, a row each (M13+), and the army rows that wait on them
   (M15d2 his answer to the machines, M15d3 the heavier machines, M15e4 the balloons).

## Rules

- **The order of systems inside `step` is stated once in `game.hpp` and is part of every
  digest.** A change to it, to the course, to the bot or to the skier moves
  `tests/golden/fall_line_sim.txt`: run `make sim` before and after (`ARGS=--tag=before`, then
  `--compare=before`), rewrite the golden with
  `GE_GOLDEN_WRITE=1 build/release/tests/fall_line_sim_test` (and `fall_line_field_test` for
  the field), and put both tables in the PR (`make sim ARGS=--rivals=3` for the field's).
- **The bot plans on the skier's limits** (`skier/limits.hpp`), never on a ceiling of its own;
  its own numbers are `bot::` constants in `bot.hpp`, each with its unit.
- **The bot records why** (`mode`, `line`, `speed`) on the `ge::ai::Why*` it is handed, and
  decides the same without it (the `bots` skill): a new choice in it records its options too.
  `make lab LAB=why` and `ge why <log>` read them; `tests/fall_line_why_test.cpp` holds them.
- **The bots press through their HANDS** (`hands.hpp`: `BotHands`, a profile per run, `none`
  by default so no digest moves; `human` a person's delay, spring, slew and wander), recording
  what they meant first (the `hands` decision). Late hands steer from further ahead
  (`bot_controls`' `lead_s`). `make lab LAB=humanness` and `ge sim --human -- --hands=human`
  measure them; `tests/fall_line_hands_test.cpp` holds them.
- **The play contract's views** are `near`, `map` and `bot` (the pilot's plan, its read stretch
  and its aim over `near`, off a decision record of its own; `ge shot --view=bot`).
- **Every pilot drives through `Driver::step`** (the bot, a tape, an agent): the player's
  controls are snapped onto the tape's grid before `step` sees them, so a tape played back
  (with the same number of rivals) is the run again. Only the player is taped: a rival's
  controls are a pure function of the run.
- **The play protocol is stated once**, in `play.hpp`'s header, and mirrored in
  `docs/labs.md` § An agent at the controls. An observation carries only what a player could
  know. A new op or field changes both and `tests/fall_line_play_test.cpp`.
- No allocation in `step`: the racers are sized at create, the events reserved, a free ride's
  `skied` reserved for every run and every run's nearest-point index built at create, and its
  lifts planned at create (`GameState::lifts`).
- **A lift that has him carries him** (`lift_ride.hpp`): his skis, the run under them and the
  run's own resets wait while he is on it, and the bot's hands are off. The carriers are the
  mountain plugin's (`plugins/mountain/lifts/lift_loop.hpp`), a pure function of the clock;
  the ride stores only which carrier has him, never where it is.
- **A race's digest reads nothing of the free ride**: `state_hash` mixes `FreeRide` only when
  the mode is `free`, and a race never steps it, so a free-ride change moves only
  `tests/golden/fall_line_free.txt` and the `free` lab. Run `make lab LAB=free ARGS=--seeds=1,2,3,4,5,6,7,8,38`
  before and after any change to the free ride, the bot or the skier, and rewrite that golden
  with `GE_GOLDEN_WRITE=1 build/release/tests/fall_line_free_test`. The test and the lab are
  pinned to generator v10, so a new generator version moves neither.
- Nothing in a step draws a random number: the field's only draws (the rivals' paces) are
  made once, in `deal_field`, off a stream of the map's seed. The renderer and the app never
  write `GameState`.
- The field is at most the grid's slots (`create_game_on` caps it), the player first.
- `engine/loop/run_clock.hpp` is the run clock: the clamp (`kMaxFrameSeconds` = 0.1),
  whole steps, and `alpha` for interpolation only.

## Labs

`sim` (drive, `games/fall_line/labs/sim_lab.cpp`): the bot over seeds (`make sim SEEDS=38`, the
reference seeds by default), the run report table and each run's line drawn on its map; a seed
not finished fails the lab. It is Fall Line's headless sim CLI; `--rivals=3` puts a field on
the grid and adds the field table (every racer's pace, lane, place and shoulders).

`free` (drive, `games/fall_line/labs/free_lab.cpp`): the bot on the free ride down every piste
of each seed's ski area (`--seeds=38` by default, `--most=N` the first N), from its head to a
foot: the runs table and the ski area with every line drawn. A piste not skied to a foot fails
the lab. `--outings=N` (1 by default) rides the bot's outing from the first N pistes with a lift
at their foot (`simulate_outing`): the outings table (the lift, the run after it, the wait, the
ride) and his line, white while a lift carries him. An outing not ridden through fails the lab;
`tests/golden/fall_line_outing.txt` holds seed 38's (`GE_GOLDEN_WRITE=1
build/release/tests/fall_line_lift_ride_test`). `--injuries=1` (the default) skis every piste
again with the INJURIES switch on (`SimOptions::injuries`): the body table (the foot or his
death, the injuries, the severity score, the hardest blow, and whether the run is the one the
switch off skied). A run he lived through that moved with the switch on fails the lab.

`lift-ride` (drive, `games/fall_line/labs/lift_ride_lab.cpp`): every lift of each seed ridden
from its load zone to its top through the real `step`: the wait, the ride, the slowest and
fastest he is carried, the biggest carried step and the let-go's distance from its point. A lift
that keeps him, a carried step over 0.5 m or a let-go off its point fails the lab. Run it, with
the `free` lab's outings, before and after a change to `lift_ride.cpp`, `outing.cpp` or the lift
loop.

`wanted` (scenario, `games/fall_line/labs/wanted_lab.cpp`): the hunt on seed 38 against scripted
pursuers (`shadow`, `posts`, `arrest`), the hunt table, each line coloured by the stars, and the
sight sheds of a soldier and a helicopter. A change to `wanted.cpp` or its numbers moves
`tests/golden/fall_line_wanted.txt` (`GE_GOLDEN_WRITE=1 build/release/tests/fall_line_wanted_test`)
and this lab's digest: put its hunt table before and after in the PR. The army joins every hunt
that reaches a star, so a change to the army moves this golden too.

`army` (scenario, `games/fall_line/labs/army_lab.cpp`): the army on foot on seed 38 (v10) after a
man stood still (`valley`, `slope`, `arrest`): the army table and every soldier's path. A man
still caught at the end, a lifted squad never let off or an arrest that never comes fails it. A
change to `army*.cpp` or its numbers moves `tests/golden/fall_line_army.txt`
(`GE_GOLDEN_WRITE=1 build/release/tests/fall_line_army_test`), `fall_line_wanted.txt` and both
labs' digests: put the army table before and after in the PR.

`army-sleds` (scenario, `games/fall_line/labs/army_sled_lab.cpp`): the army's snowmobiles on seed
38 (v10) after a man stood still (`open`, `run`, `face`, `arrest`): the sleds table and every
machine's path. A machine still caught at the end, no halt where he is within reach, or an arrest
that never comes fails it. A change to `army_sled*.cpp`, `army_drive.cpp` or their numbers moves
`tests/golden/fall_line_army_sled.txt` (`GE_GOLDEN_WRITE=1
build/release/tests/fall_line_army_sled_test`), `fall_line_army.txt`, `fall_line_wanted.txt` and
the `army` and `army-sleds` digests: put the sleds table before and after in the PR. A race's map
has no snowmobile spot, so a race sends no machines.

`army-para` (scenario, `games/fall_line/labs/army_para_lab.cpp`): the army's paramotor riflemen
on seed 38 (v10) with only the paramotors sent (`four`, `five`, `home`): the paramotors table,
every rig's path round him and every rig's height over the snow. A rig brought down by the snow
or a crown, a roster not filled, no round fired at four stars or a rig still out after `home`
fails it. A change to `army_para*.cpp`, its numbers or `plugins/paramotor` moves
`tests/golden/fall_line_army_para.txt` (`GE_GOLDEN_WRITE=1
build/release/tests/fall_line_army_para_test`), `fall_line_gunfire.txt`, `fall_line_wanted.txt`
and the `army-para` digest: put the paramotors table before and after in the PR. The rigs are the
free ride's own paramotor flown by `paramotor::para_pilot` to an aim the army sets; the wing's
physics decides what it can do, so a rig in sinking air sinks even at full throttle: hold the
height in the aim (`kHoldM`) and gentle the toggles under the floor rather than asking more of
the pilot.

`army-flight` (scenario, `games/fall_line/labs/army_flight_lab.cpp`): the army's helicopters on
seed 38 (v10) with the army's other arms on (`open`, `rope`, `leapfrog`, `hide`, `fast`): the
helicopters table and every machine's path, where it set down and where its men went. A scenario
that never sees what it stages (no section set down, no fast rope, no pick-up, no machine home,
no echelon, no machine taken or rammed, nothing down on the snow) fails it. A change to
`army_heli*.cpp`, `army_land.cpp`, `army_steal*` or their numbers moves
`tests/golden/fall_line_army_heli.txt` (`GE_GOLDEN_WRITE=1
build/release/tests/fall_line_army_heli_test`), `fall_line_army_steal.txt` and the `army-flight`
digest: put the helicopters
table before and after in the PR. The `army`, `army-sleds` and `wanted` labs and tests stage their
own arms with `ArmyState::send_helis` off, so the helicopters move none of them. A trace
(`GameState::why`, set by `Driver::set_why`) records each machine's decisions as agent
`army_heli::kAgentBase` + its id; the test holds that it decides nothing.

`gunfire` (scenario, `games/fall_line/labs/gunfire_lab.cpp`): the army's guns. The range table
fires each gun from each range at a man stood still and one crossing, under aimed and warning
fire, the gunfire stepped on its own and the man healed every step; the hunt table runs seed 38
(v10) at four stars through the real `step` where the army shoots to kill and where it does not;
the doors table runs the same hunt at one, two and three stars (`hold`, `warn`, `aim`) and counts
the door gunners' rounds. Warning fire that strikes him, aimed fire at 100 m that never does, a
lethal hunt that fires nothing, door fire under `hold` or none under `aim` fails it. A change to `gunfire*.cpp`, `weapons_rules.hpp`, the wounds' rows in
`skier/anatomy.cpp`, or the soldiers', the snowmobile gunners' or the door gunners' fire in
`army.cpp`, `army_sled.cpp` and `army_door.cpp` moves `tests/golden/fall_line_gunfire.txt`
(`GE_GOLDEN_WRITE=1 build/release/tests/fall_line_gunfire_test`), the door gunners' own
`tests/golden/fall_line_army_door.txt` (`fall_line_army_door_test`) and the `gunfire` digest: put both tables before and
after in the PR. The army's older labs and tests (`army`, `army-sleds`, `army-flight`, `wanted`)
stage their arms with `ArmyState::fire` off, so the gunfire moves none of them. A round's wounds
are judged on `Racer::body` (`judge_body`); death's course is the game step's, which steps a
mortally hurt body (with no blows when the INJURIES switch is off) like every other.

`play` (drive, `games/fall_line/labs/play_lab.cpp`): an agent at the controls.
`build/debug/labs play --quiet --stdio --seed=38 --rivals=3` puts the protocol on stdio;
`--script=<file>` plays a file's lines; with neither, the bot plays through the protocol's own
text and its tape must replay to the session's digest. `run-clock` covers the run clock, `ride`
the skier (the `skier` skill). The `labs` skill owns how.
