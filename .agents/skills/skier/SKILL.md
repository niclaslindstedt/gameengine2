---
name: skier
description: "Use for plugins/skier in gameengine2: the skier on the snow any skiing game links. The snow under a pair of skis (sink, base friction, plough, grip, brake), what a skier can do (limits: edge, carve curvature, corner and terminal speed), one step of the skier (step_skier: the contact impulse, the carve and the pivot, the tuck and the push, the jump, flight, landing, trunks, wipeout), XX---

# The skier plugin

`plugins/skier/` (`ge_skier`, namespace `ge::skier`) is a skier and his pair of skis on a
generated (or hand-built) mountain's snow. It depends on the core and on `mountain`, whose
`Level` it skis (`ge_add_plugin(skier ENGINE core PLUGINS mountain)`); it never learns which
game rides it, so the modes, the course's rules, the bot and the field stay in the game.

| File | What |
| --- | --- |
| `skier/skier.hpp/.cpp` | THE MODEL, its order stated once in the header: `SkierState`, `SkierInput`, `SkierEvents`, `World`, `step_skier` (its trunks met by the body's circle and two along the skis ahead: `meet_trees`), `stand_skier`, `skier_hash` |
| `skier/snow.hpp` | The snow under the skis by its packed share: `sink_target`, `base_friction`, `powder_drag`, `side_grip`, `brake_grip` |
| `skier/limits.hpp` | What a skier can do: `edge_most`, `carve_curvature`, `edge_for_curvature`, `corner_speed`, `carve_speed`, `terminal_speed`, `top_speed` |
| `skier/skis.hpp` | `SkiSpec` and `kReferenceSkis`, the pair every shared number was tuned on |
| `skier/tuning.hpp` | Every shared number, with its unit |
| `skier/trees.hpp/.cpp` | The trunks on a grid once a map: `build_tree_index`, `tree_overlap` |
| `skier/ragdoll.hpp/.cpp` | THE THROWN BODY: thirteen Verlet points (`Point`, `point_mass`, `point_radius`), `throw_body`, `throw_of` (by an `Over` row), `step_ragdoll` (the muscles, the air, gravity, the joints and the bones, the snow, the trunks; each point's `impact` on the snow and fresh `struck` on a trunk this step, at the way it came into the step with, and `struck_by` the trunk), `ragdoll_hash`. `step_skier` throws it the step he goes down (`over_of` the cause's row) and steps it while he is down. Its test: `tests/skier_ragdoll_test.cpp` |
| `skier/synthetic.hpp/.cpp` | The hand-built maps: `synthetic_slope()`, `flat_strip(pitch, packed)` |
| `skier/scenarios.hpp/.cpp` | The ride scenarios (one table for the tests and the labs) and `run_scenario`, which steps the body after every step |
| `skier/anatomy.hpp/.cpp` | THE BODY'S CATALOG (M13e1): the twenty parts, the bones and the organs, the mechanisms, the 98 injuries of the skis and the 32 penetrating wounds of a round (M15b) with their AIS and even-chance doses, and every number of the body (`injury::`), each with its research |
| `skier/body.hpp/.cpp` | THE BODY: `blow_of`, `risk_of`, `Doses` and `judge_body` (the ladders read worst first off a hash of the step, never the Rng), `step_body` (a trunk on the skis, a landing, a landing across, and thrown, every point of him on the snow at its give by the packed share or on a trunk: `ragdoll_blows`; a folded landing's trunk load `Owed` until his trunk meets the snow), `severity_of` (the ISS), `dies_of`, death, `mend_body`, `body_hash`. Its test: `tests/skier_body_test.cpp` |
| `skier/swim.hpp` | THE DRESSED SKIER IN THE WATER (M24d): `dressed_skier()`, the engine's swimmer (`water::SwimSpec`, the `water` skill) filled with his jacket, trousers, boots, helmet, gloves and base layers, each garment's mass, density, soak and trapped air. Its test: `tests/skier_swim_test.cpp`; its lab: `skier-swim` |
| `skier/pilot.hpp/.cpp` | THE SKIER'S PILOT every bot on skis flies (M16c): `lay_pilot_plan` (the curvature along the game's line and an offset line, the speed off `carve_speed` eased to every lip, carried back so each slowing is braked for) and `skier_pilot` (pure pursuit of a `PilotRider`'s lane, further by late hands' lead; the brake over the plan at his pace, the tuck under it), its `line` and `speed` decisions on an `ai::Why` (the `bots` skill). Its test: `tests/skier_pilot_test.cpp` on the slope |

Its app half, `plugins/skier/app/` (`ge_skier_app`, no GPU), is the skier as seen and heard:
`rig.hpp` (the skeleton the skier model is built on), `rig_pose.hpp` (the POSER: `RigPoser` places every rig joint from a `SkierState` each frame, the lean through two lags so its rate never steps, each ski rolled about its widest edge line so it never cuts the snow, the legs and arms by two-bone IK with the pelvis lowered by a smooth maximum of the legs, the pole tips trailing at a fixed height; `bind_rig` and `rig_locals` turn a pose into the model's locals), `figure.hpp` (the placeholder figure), the beds (`wind_bed.hpp`, `snow_bed.hpp`, `ride_bed.hpp`,
the `audio` skill) and `tracks.hpp`: the PRINTS a skier's skis lay each frame, two capsules a
stance apart from the pen (where they last touched) to where they touch now; the outside ski of a
turn deeper by `ski_load`, a carve narrower, a skid as wide as the ski's footprint across the
travel and shallower with a berm, a scuff on the piste and a hand's furrow in loose snow; none in
the air, none for a reset, one wide gouge for a body down. The game hands them to its trail map
(`mountain/app/trails.hpp`, the `mountain` skill); the `trails` lab measures the two together.

## The model in one paragraph

A body of `SkiSpec::mass`, integrated semi-implicitly at the caller's step. On the snow a
VELOCITY-LEVEL CONTACT takes the velocity into the snow out, and that normal impulse is the
currency every friction is paid from: across the skis the edge's grip, along them the base,
the brake and the powder. So a sideways load can never exceed what the snow holds, and a skid
costs speed because the slide it takes out is energy lost. The skis yaw at the carve's rate
(the speed times `tan(edge) / sidecut`) held to the grip, plus the pivot, plus their tracking
of their own way. A flight is the ground curving away faster than gravity follows; a landing
is the support met again, its load `1 + v_n^2 / (2 stroke g)`. There is no mode.

## Rules

- **State each answer once.** A pilot (`pilot.hpp`, or a game's own) plans on `limits.hpp`, never on a number
  of its own; the physics reads the same functions. A number that shapes the feel goes in
  `tuning.hpp` with its unit; a pair's own goes in `SkiSpec`.
- **Deterministic.** `core/libm` for every transcendental, no allocation in `step_skier`, no
  state outside `SkierState`. `skier_hash` is the trajectory's digest.
- **Test on the hand-built maps.** A scenario never reads the generator, so the plugin's tests
  pass with it deleted. A new situation is a row in `scenarios.cpp`'s table: it is a test (its
  golden row and, if it has one right answer, an expectation in
  `tests/skier_scenarios_test.cpp`) and a lab run at once.
- **The body is judged, never drawn from the Rng.** Its risk draws hash the step, the part and the
  seed, so stepping it moves no other digest; a doses source (a trunk, a landing, a round)
  fills `Doses` from what a step already measured and calls `judge_body`, never re-measuring.
  A catalog or body change rewrites `tests/golden/skier_body.txt` and runs the `injuries` lab
  before and after.
- **A model change moves the golden on purpose**: run `make lab LAB=ride ARGS=--tag=before`
  first, change, run `--compare=before`, rewrite `tests/golden/skier_scenarios.txt` with
  `GE_GOLDEN_WRITE=1 build/debug/tests/skier_scenarios_test`, and put the compare table and
  the pictures that moved in the PR. A game's `sim` digests move with it (the `sim` skill).

## Labs

`ride` (scenario): every scenario run and measured (top speed against the terminal, distance,
the hardest sideways load and the tightest path, flights and landings, the stop, the wipeout),
with one picture each: the line from above coloured by what he was doing, and his speed. Its
`thrown` table is every wipeout's thrown body: the tumble, how far and how high it went, and
when it lay still; a body that never lies still for a second fails the lab.
`make lab LAB=ride ARGS=--scenario=kicker` runs one. The `labs` skill owns how to read it.

`injuries` (scenario): every scenario's body (its injuries by part, the ISS, when he was
mortally hurt and when he died) and the body's curves over 400 draws: a trunk skied into
head-on (the physics run once, the body thrown on into it, every draw judged off the same steps)
and across his whole line (13 lines from 0.6 m left of it to 0.6 m right, averaged), met beside him by closing speed, a landing by its load, as the share hurt, the mean ISS and the
share killed; pictures: every scenario's parts shaded by their worst AIS, and the curves.

`anim` (scenario): the skier ridden through a scenario and recorded every step as a pose track:
`--figure=model` (the default) the poser's rig (`app/rig_pose.hpp`), with the run baked onto the
skier model as a clip (`skier-run.glb`, for the `models` lab), `--figure=pieces` the placeholder
figure (`app/figure.hpp`) in its own frame (`figure_frame`, `figure_joint_positions`): filmstrips,
onion skins, joint angles, a jolt heatmap and the faults it shows (the `debug` skill reads it).
A change to how the figure is posed runs it before and after.

## Still to come

What the game does not ask for yet stays out (G4): the catalog of pairs, the wind, crust and
ice, the poles and the legs as stations, and the tricks.
Each lands with the milestone row that needs it.
