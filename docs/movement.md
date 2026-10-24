# Character movement

How a body that walks, runs, jumps, falls, climbs steps, crouches, slides and steers in the air
is moved, for any game: a first-person shooter's player, a platformer's hero, a third-person
walker, a spectator's free camera. `engine/move` is the mover; a game hands it a row of knobs,
a command a step and a way to ask its world where a box stops, and gets back the body moved and
what happened. Roadmap rows M28 to M28d (`docs/goals.md`); the skill is `move`.

## What each kind of game asks of it

| Game | What it needs from movement |
| --- | --- |
| An arena shooter (Shooter's Edge, M18) | The classic feel to the unit: ground friction and acceleration, an air whose small cap rewards turning (strafe jumping and bunny hopping), jumps pressed afresh, stairs taken without a jump, ramps steep enough to slide (surfing), crouching under things and crouch-jumping onto them; the step on the command's milliseconds so a server and a predicting client move the same body |
| A modern shooter | Sprint, a slide out of a sprint, a jump forgiven a little late off a ledge and a little early before landing, a ground snap that keeps a runner on ramps and down stairs |
| A platformer | Full steering in the air, a jump as high as the button is held, a heavier fall than rise, double jumps |
| A third-person game | A person's walk and run, a modest jump, almost no steering once off the ground |
| Any game | A spectator's free flight with collision, and a developer's noclip |

None of these is a different system. Each is a ROW of the same knobs (`MoveSpec`), and
`engine/move/presets.cpp` holds a starting row for each: `classic_arena`, `strafe_arena`,
`modern_shooter`, `platformer`, `walker`.

## Where it lives (G8)

| Home | What |
| --- | --- |
| `engine/move/` | The knobs (`spec.hpp`) and their names (`knobs.hpp`), the presets, the step (`step.hpp`), the world's one question (`world.hpp`), a box world for labs and prototypes (`box_world.hpp`), the strafe jumper's hands (`strafe.hpp`), the scenarios (`scenarios.hpp`). Depends on `engine/core` only |
| `plugins/shooter/` (M18e) | The classic row in the classic map's units, the brush world's clip-hull trace answering `World::trace`, the server's other movetypes (toss, bounce, push) |
| `engine/water/swim` (M24d) | Swimming: a body in the water is handed to the swimmer, and back (§ Water) |
| `engine/camera` (M26) | Reads the body (§ What a camera reads); never feeds it |
| A game | Its row, its world's trace, the command from its input actions, the knobs bound to its console |

It is an engine module, not a plugin, because nothing in it belongs to one genre: the shooter's
feel is a row of numbers, and the same step moves a platformer's hero.

## The model

All in the row's own length unit L (a metre, or a map unit; `MoveSpec::unit` says how many make
a metre). Each step, in this order (`step_move`):

1. **The crouch.** Held: the hull drops to `crouch_height` (on the ground the feet stay; in the
   air with `crouch_jump` the feet are drawn up and the head stays, which is what lets a
   crouch-jump clear a ledge). Let go: the standing hull is taken back only where it fits
   (under a ceiling it stays crouched). The eyes travel between `eye_height` and `crouch_eye`
   over `crouch_time`.
2. **The slide.** Crouching on the ground faster than `slide_min_speed` starts a slide: friction
   `slide_friction` instead of `friction`, steering `slide_steer` of the ground's acceleration,
   and once every `slide_cooldown` s a `slide_boost`. It ends slow, standing or airborne.
3. **The jump.** A press is an edge of the button; it waits `jump_buffer` s to be used; with
   `jump_hold` a held button counts too (an automatic bunny hop). On the ground, or within
   `coyote_time` s of walking off it, the vertical speed becomes `jump_speed` (added to a rising
   speed when `jump_additive`, so a ramp's lift is kept). In the air a fresh press spends one of
   `air_jumps`. Letting go while still rising multiplies the vertical speed by `jump_cut`.
4. **The wish.** The stick turned by the view's yaw (`forward` along the heading, `side` to the
   right, held to length 1), times `max_speed` and the shares in force (`back_scale`,
   `side_scale`, `walk_scale`, `sprint_scale`, `crouch_scale` on the ground).
5. **On the ground**: the vertical speed is dropped, then **friction** takes
   `max(speed, stop_speed) * friction * dt` off the speed (times `edge_friction` when nothing
   is under a point `edge_probe` ahead within a step's drop), then **acceleration** adds
   `accelerate * wish * dt` along the wish, never past `wish` along it, then the move (6).
   **In the air**: the same acceleration rule with `air_accelerate`, the speed along the wish
   held to `air_speed_cap` (0: the wish itself), `air_stop_accelerate` while wishing against the
   velocity, `air_strafe_accelerate` and `air_strafe_cap` while only sideways is held; with only
   forward held `air_control` turns the velocity toward the wish keeping its speed; and none of
   it lifts the plan speed past `air_max_speed` (a row that steers in the air without strafe
   jumping: the platformer's, the modern shooter's). Gravity
   (`gravity`, times `fall_gravity_scale` falling, held to `terminal_speed`) is applied by halves
   either side of the move, so a jump's apex is `jump_speed^2 / 2g` at any step length.
6. **The move.** The hull is swept along the velocity; each surface hit takes the part of the
   velocity into it and the rest carries on, up to four surfaces a step (two at once leave the
   crease between them; a third stops it). On the ground a move blocked by a wall is tried again
   lifted by `step_height`, carried and set down, and kept when it got farther on walkable ground:
   that is climbing a step.
7. **The ground.** A surface within `contact` under the feet whose normal's up part is at least
   `walk_normal` is ground (anything steeper is a slope slid down, a surf ramp); a body that was
   standing follows a drop of up to `ground_snap` (down stairs, over a ramp's crest); rising
   faster than `unground_speed` is never grounded. Landing drops the speed into the ground.

**THE ACCELERATION RULE is why strafe jumping exists.** It adds speed along the wish only until
the speed ALONG the wish reaches the cap. With a small air cap, a wish pointed far enough off the
velocity is always under the cap, so it always takes its full `accel * wish * dt`, and that
addition, mostly across the velocity, lengthens it. The best angle (`best_wish_angle`) is where
the addition just meets the cap, `cos(theta) = (cap - accel * wish * dt) / speed`; a player who
keeps turning the view to hold it gains speed every step in the air, and jumping again at once
keeps ground friction from taking it back. Holding the view straight gains nothing: the speed
along the wish is already past the cap. `move-strafe` charts all of it.

## The world's one question

The mover asks the world only "where does this box, swept from here to there, first touch
something solid, and which way does that surface face?" (`World::trace`, a function pointer and
a context: no virtual, nothing allocated). Its contract (`world.hpp`): the box about the feet,
the fraction travelled, the end point pulled back by a small skin, the surface's normal, and
whether the box started inside something. A brush map's clip hull, a heightfield, a game's own
colliders all answer it. `box_world.hpp` answers it for boxes, ramps and stairs (each a convex
brush of planes, swept by pushing each plane out by the box's reach along its normal), which is
what the labs and tests stand on and what a game prototypes on before its own world exists.
`mesh_world.hpp` answers it for a level that is triangles (a room level's walls, a cave's grown
rock): each triangle met by the swept separating-axis test against the box (the box's three axes,
the triangle's normal and the nine crosses of its edges with the box's axes), so a box slides
over an edge where the triangle really ends, never along the edge's extension; the triangles
bucketed once in a uniform grid, a trace visiting each one once in the first covered cell it lies
in, with nothing allocated. A triangle is solid on both sides and has no inside.

**Keep the skin far under a step's creep.** A body stopped by a riser climbs it only when the
lifted try of one step carries it more than the skin past where it stopped, and from rest that is
`accelerate * wish * dt^2`: about 3 mm for the walker at 120 Hz. A world whose skin is larger
than that (the box world's 1/32 L is 3 cm when L is a metre) leaves a walker stuck at the foot of
a 10 cm sill; the room level's world keeps a millimetre.

## Knobs as cvars

Every knob has a row in `knobs.cpp` (its bare name, unit, range and help) and nowhere else. The
labs take `--set=name=value,...` through `apply_knobs`, `knob_text` prints a row's difference
from a preset, and `number_field` / `flag_field` / `count_field` hand the console the field to
bind (`engine/console`, M21: a bound cvar writes straight into the struct, which the step reads
with no lookup). A game binds them under its own owner prefix, the server's rules usually
(`sv_gravity`, `sv_maxspeed`, `sv_airaccelerate`, `sv_friction`, `sv_stopspeed`,
`sv_jumpspeed`, `sv_stepheight`, ...), so a cfg file retunes the feel without a rebuild and a
server sends its row to every client in the server info (M28b).

## What a camera reads

The camera (M26) reads the body and never writes it: `MoveState::grounded`, the plan speed
(`flat_length(vel)`) for the bob, the vertical speed for the landing dip, `crouch` (0..1) and
`eye_height(spec, crouch)` for the view height, and the step's `MoveEvents`: `kLanded` with its
`impact`, `kJumped`, `kSteppedUp` / `kSteppedDown` with `stepped` (the height the view smooths
over), `kSlideStart`. A teleport is the game's to report.

## Water

Swimming is not a mode of the mover. When the game's water query says the body is in deep
enough (M24d's immersion levels), it hands the body to the swimmer (`engine/water/swim`), whose
kinematic regime is the classic first-person swim; when the swimmer climbs out, the body comes
back with its position and velocity and `MoveState::grounded` found on the next step.

## Determinism and cost

A step is a pure function of the row, the state, the command, `dt` and the world: no clock, no
random draw, no allocation, every transcendental from `core/libm` (the yaw's sine and cosine, the
strafer's arc cosine). So a run replays to the bit from its commands, and every scenario's digest
is held golden (`tests/move_scenarios_test.cpp`). A step costs a handful of traces (the move,
at most four bumps, a step-up's three, the ground probe, the edge probe), so its cost is the
world's trace.

## The labs

| Lab | Question |
| --- | --- |
| `move-strafe` | How much speed does each row let a strafe jumper gain, at which angle, at which step rate? The classic row's speed over a run (a run, straight hops, the strafe jumper two ways), every row strafe jumping, the gain of one step of air against the wish's angle with the best angle marked, the paths from above, and the end speed by step rate |
| `move-trace` | What does each scenario do? The feet and the top of the head from the side with the world's solids (or from above when the path turns), take-offs and landings marked; every row's running jump; a table of every run's numbers and digest. `--preset` and `--set` retune a run |

## Decisions

- **One model, rows of numbers.** The shooter's, the platformer's and the walker's feel differ
  in numbers, not in code paths, so a game tunes by cvars and labs, and a new kind of game is a
  new row.
- **A box hull, not a capsule.** Boxes sweep exactly against brushes, step cleanly onto stairs
  and are what an arena shooter's maps are built for. A capsule is a later row when a game
  needs one.
- **Gravity by halves.** A jump's height does not depend on the step's length, so a 60 Hz client
  and a 250 Hz client jump equally high. Strafe-jump gain still depends on it, as it is gained a
  step at a time; the game fixes its step rate (`move-strafe`'s rate chart).
- **The trace is a function pointer**, not an interface with virtuals: one indirect call per
  sweep, and any world can answer it.

## Later

M28b binds the knobs to the console; M28c brings moving ground (platforms, lifts), knock-back,
jump pads, pushers and ladders; M28d prediction and reconciliation over the network. A capsule
hull and a heightfield trace wait for a game that needs them.
