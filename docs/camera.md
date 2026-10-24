# The camera

Where a picture is taken from, and how it moves: a skier's chase boom and helmet lens, a
racer's bumper and trackside cameras, a shooter's eyes with their bob, roll and kick, a
broadcast director's cuts, and every hand-over between them. This page is the design. The
procedures are the `camera` skill's, and the milestone rows are [`goals.md`](goals.md)
M26a–M26f.

The engine is genre-agnostic, so the camera is too. Every game of the kinds the engine serves
asks the same few things of a camera, in different proportions:

| Kind of game | What its camera has to do |
| --- | --- |
| A skier or a snowboarder | A sprung boom behind the rider that leans with the mountain, keeps the rider in frame through a cliff drop, pushes off trunks and conveys speed; worn lenses at the tips and the helmet that pitch and roll with the body |
| A racer (car, sled, craft) | A chase boom with weight, a bumper, a hood and a cockpit lens with a head on a neck, trackside cameras a director cuts between |
| A first-person shooter | The eyes at a view height, stepped up stairs smoothly, bobbed by the stride, rolled by a strafe, kicked by a shot or a hit, dipped by a landing, zoomed; the angles under the mouse with no lag at all |
| A flyer (a wing, a canopy, a plane) | A follow lens in three dimensions that banks with the flyer, a lens that looks down a long fall at the landing |
| Any game | Cuts, blends and flights between views; a shake with a shape; a replay's lenses; a developer's free camera; comfort settings |

All of them are built from the same parts: a POSE, FOLLOWERS with weight, RIGS that turn a
subject into a pose, MODIFIERS added on top, and TRANSITIONS between rigs. A game names the
rigs it uses and writes their numbers as rows.

## The layers

```
games/<game>          its ladder (which rigs, in which order, their rows), its director (when to
        │             cut), what its subject is, its cvars' defaults and units
plugins/<world>       a world's own readings for a rig, when a world has them (none yet: the
        │             mountain's lean is the ground callback below, read by any boom)
engine/camera         the pose, the followers, the subject, the rigs, the modifiers, the world
        │             queries (ground, line clear, obstacles), the transitions, the camera
        │             stack, the cvars (console/), and the labs. Pure C++: no GPU, no platform
        ▼
engine/render         FrameView: the eye, heading, pitch, ROLL, fov, near. The renderer reads
                      the pose and nothing else of the camera
```

`engine/camera` depends on `core` only (and `console` for its cvar binding, in one file). It
reads no simulation state: a game fills a `Subject` from its own state each frame, so the same
boom follows a skier, a sled or a car without a word of any of them.

## The frame

The camera runs once per DISPLAY frame, after the simulation's steps and before extraction:

1. The game fills the `Subject` from the INTERPOLATED state between the last two steps (the run
   clock's alpha), so a 144 Hz display over a 120 Hz step sees smooth motion, and a first-person
   game takes the view angles straight from this frame's input (the mouse is never interpolated
   or smoothed: a look that lags the hand is the one thing a shooter cannot forgive).
2. `Camera::update(subject, world, dt)` runs the active rig, keeps the ladder's other rigs warm,
   runs the transition, then adds the modifiers, and returns a `Pose`.
3. `to_frame_view(pose, view)` writes the eye, heading, pitch, roll, fov and near into the
   frame's `FrameView`.

**The camera never feeds the simulation.** A shooter's view angles are the player's input and
go to the simulation as part of the step's command; the bob, the roll, the kick and the shake
are presentation alone, so two players with different `cl_bob` play the same game and a replay
plays back the same. **The camera is deterministic for a given sequence of `dt`s**: it uses
`core::libm`, `core::Rng` and nothing else, so a lab that feeds it a fixed frame rate has a
digest on every CI row, and `make libm-check` scans `engine/camera`.

**No allocation in a frame.** A camera's state is fixed-size and made at create time.

## The pose

```cpp
struct Pose {
  double eye[3];        // world metres
  double yaw, pitch;    // the world's conventions (core/math.hpp): yaw 0 = +z, clockwise from above;
                        // pitch positive up
  double roll;          // right side down positive, rad
  double fov_y;         // vertical field of view, rad
  double near_m;
};
```

A pose is blended component by component: the eye linearly (in the subject's frame during a
transition, below), the angles the short way round, the fov by its tangent (so a blend between
a 50° and a 90° lens changes the picture's scale evenly). The renderer's `FrameView` gains a
`roll` (0 by default, and the matrices skip the roll entirely at 0, so no picture moves).

**The field of view** is stored as VERTICAL internally. A player's setting is HORIZONTAL at a 4:3
reference aspect (`cl_fov 90`), converted once: wider screens see more at the sides and the same
height (Hor+), which is what every first-person player expects of the number.

## The subject

What a rig reads of the thing it follows, filled by the game each frame:

| Field | What |
| --- | --- |
| `pos[3]`, `vel[3]` | World metres and metres a second (interpolated) |
| `q` | Body to world (`core/quat`): x right, y up, z forward |
| `heading` | The way the body faces, rad; `travel` is read off `vel` |
| `look_yaw`, `look_pitch` | The player's view angles, where the player aims them (first person, a free look on a boom) |
| `grounded` | Whether it stands on something; the moment it leaves and lands are read off its changes |
| `ground_speed`, `fall_speed` | For the bob and the landing dip (derived from `vel` when the game leaves them) |
| `crouch` | 0 standing .. 1 crouched |
| `surface` | 0..1, how rough the ground is under it (a groomer's chatter, gravel): the tremor's share |
| `events` | This frame's moments with a size: a landing, a hit (and its direction), a shot fired, a teleport |

A teleport (a respawn, a reset, a lift's top) is an event, never a jump the followers chase:
the camera SNAPS its followers and starts the arriving rig fresh.

## Followers: weight without lag where it hurts

Every reading a rig chases goes through a FOLLOWER, never a raw copy and never a bare
exponential ease:

- **The second-order follower** (`f`, `zeta`, `r`): a mass on a damped spring placed by its two
  poles, `f` the natural frequency in Hz, `zeta` the damping ratio (under 1 overshoots and
  breathes, 1 critically damped, over 1 creeps), and `r` the response (0 starts from rest, 1
  meets a step at once, 2 tracks a steady ramp with no lag, over 1 anticipates). A first-order
  ease moves fastest the instant the target jumps, which reads as a lens with no weight; a
  second-order one has to be accelerated into a move and settles out of it, which is what a
  heavy head on a crane looks like. Each frame is solved exactly (the closed form of the
  under-, critically and over-damped cases, the target held over the frame), so a long frame on
  a slow device draws the path a short one would have, never a ringing or exploding one; the
  `camera-follow` lab holds it, and a semi-implicit step with a clamped mass (the cheaper
  choice) strayed by a fifth of a step at 30 Hz there. Scalar, angle (the short way round, free to wind) and vector forms.
- **The critically damped spring by half-life**: one number, the time to halve the distance, for
  the readings that need no character (the arm letting back out after a pull-in).
- **The dead band**: a follower may ignore its target within a band and chase only what leaves it
  (the boom's look keeps a skier anywhere inside a soft middle of the frame, the way a composer's
  dead zone does), entered and left smoothly so the band's edge is never felt.

**Frame-rate independence is a measured property**, not a hope: the `camera-follow` lab runs every
follower at 30, 60, 120, 144 and 240 Hz and with long frames mixed in, and holds the paths to one
another within a bound.

## The rigs

A rig is a ROW of numbers and a function `frame(row, subject, world, state, dt) → Pose`. The
difference between two cameras is the row. The kinds:

### First person (`FirstPerson`)

The eyes of a body that walks, runs and jumps. Every number is a row a game sets and a cvar a
player may change:

- **View height** over the body's origin, eased between standing and crouched.
- **Step smoothing**: a body stepping up a stair rises instantly in the simulation, the eyes
  follow it over a fraction of a second, so a staircase is a ramp to the eye and not a judder.
- **The bob**: the stride as a cycle in time (`bobcycle` s, the share of it that rises,
  `bobup`), its height in proportion to the ground speed (`bob`), clamped; zero in the air. A
  weapon's sway reads the same cycle (a game draws its weapon off the bob it is handed).
- **The roll**: a strafe tips the horizon (`rollangle` at `rollspeed`, in proportion to the
  sideways speed, capped) — and a hit from the side rolls it away from the blow.
- **The kick** (the view punch): a shot or a hit hands a pitch and roll impulse to a stiff
  follower that springs back (`kicktime`, `kickpitch`, `kickroll`); the angles under the mouse
  are never touched, only the picture.
- **The landing dip**: a landing's fall speed pushes the eyes down on a spring and lets them
  back up; a long fall dips more and slower, capped.
- **The zoom**: the fov eased to a zoomed one and back, the look's sensitivity scaled with it by
  the game.
- **Idle sway**: a slow breathing drift, off unless a game turns it on.

The classic first-person look is a set of rows: the stride, the roll and the kick at the
numbers a classic config sets, in that game's units (below).

### Boom (`Boom`): the chase camera

A lens on an arm that stands behind the subject on a yaw that FOLLOWS the way it travels, at a
height that is sprung rather than bolted:

- **The yaw** follows the heading, with `slip` of the travel direction blended in (a skid shows
  its slide), on a second-order follower.
- **The height** follows the subject's on the ground and softer in the air (`lift`, `lift_air`),
  which is what makes a flight hang; the lag is let run only to `lag_max`, eased into rather than
  hit, so a subject falling off a cliff never falls out of the frame.
- **The incline**: the arm rises up the slope behind by a share of the ground's pitch read along
  its bearing from just behind to where the subject will be in `ahead` seconds, so the lens tips
  before a face drops away and a steep slope reads steep (a lens that aims level shows half a
  picture of sky; one that tips all the way reads every slope as flat).
- **The composition**: the look pitches to stand the subject at `place` of the half-fov below the
  middle, the same place on a gentle slope and a steep one, and never lets it out of `frame` of
  the half-fov (the look tilts to follow a drop).
- **The sense of speed**: the fov widens with speed while the arm pulls in along its own line by
  `hold` of what keeps the subject its size, so the world rushes out past the edges and the
  subject stays put (widening the field is the one cue measured to raise the speed a player
  reads; running the arm out with speed shrinks the subject and reads slow); the SURGE lets the
  arm out under acceleration and in under braking; the TREMOR buzzes the lens past a brisk pace on
  rough ground and goes quiet in the air.
- **The bank**: the horizon takes a small share of the subject's roll on the ground (`bank`), and
  its own share in the air (`bank_air`, none for a skier's boom): a lens chasing a craft through
  a turn flies it banked with the craft.
- **The fall look**: once a flight's arc says the subject has a long way to fall, the arm climbs
  over its back and the look tips down to hold both the subject and the landing.
- **Clearance**: never closer than `clearance` to the ground under the lens; the arm is pulled in
  AT ONCE to the first thing the line from the subject's head to the lens runs into, and let back
  out slowly; and pushed sideways off point obstacles (trunks, posts) inside a soft radius, the
  way a magnet answers its like pole, so a lens passing a tree gives it a metre and no more
  instead of jolting at the subject.

### Mounted (`Mounted`): a lens worn by the body

A point in the body's own frame (the tips of the skis, a helmet, a bumper, a hood, a cockpit
seat) looking along the body's forward axis, pitching and rolling with it by `roll_share`.
Bolted rigidly it is a still photograph on smooth ground, so a HEAD ON A NECK carries it: the
eye is pushed off upright by the body's acceleration (read by differencing VELOCITIES, never
positions, so a step rate that is not a multiple of the display's cannot shake it), through a
stiff, well damped follower bounded by the neck's arc; a hit throws the head with a directional
impulse. Under that, a fine GRAIN of the surface as motion.

### Orbit (`Orbit`), planted (`Planted`) and free (`Free`)

- **Orbit**: round the subject at a radius, a height and a rate; a menu's drone.
- **Planted**: a lens standing still in the world (a trackside camera, a TV gantry, an impact
  camera waiting for the subject), its aim on a follower that pans to hold the subject and a zoom
  that holds its size in frame; given up when the line to the subject closes.
- **Free**: the developer's flying camera, under the input's actions, through anything.

## Modifiers

Added to the rig's pose in a fixed order, each scaled by the player's COMFORT settings:

1. **Kicks** (impulses on stiff followers: a shot, a hit, a landing on a boom).
2. **Shake by trauma**: a source adds trauma (0..1, capped), trauma decays linearly, the shake
   is trauma SQUARED (a small knock barely shows, a big one is violent) times a smooth noise per
   axis (incommensurate oscillators or value noise under 8 Hz, never a fresh random offset a
   frame, which is not a rougher shot but a broken one and aliases into a lurch at 30 Hz). A
   shake's angles move the picture more than its offsets; an outside rig takes shakes from the
   world (an explosion, an avalanche) but not from the subject running into things, which the
   subject's own body shows.
3. **Comfort**: `cl_motion` (0..1) scales the bob, the roll, the tremor and every shake at once;
   `cl_shake` scales the shakes alone; the fov is the player's own. Every motion a player did not
   command can be turned down to nothing, as accessibility guidance asks.

## The world

A rig asks the world three questions, all callbacks the game supplies (no collision system is
assumed):

| Query | Answers | Used by |
| --- | --- | --- |
| `ground(x, z)` | the surface height under a point, m | every boom's clearance and incline, the orbit |
| `clear(from, to)` | the share of the segment that is open before it hits something solid (0..1), and whether a point is inside something | a boom's pull-in, a planted lens's line of sight, a flight's floor |
| `obstacles(x, z, reach, out)` | the point obstacles (position, radius, height span) near a point | a boom's push-off |

A shooter answers `clear` with a trace through its map's hull; Fall Line with its trees, posts
and arches; a game without one leaves it empty.

## Transitions

A change of view is an EDIT, and the three edits say different things:

- **Cut**: the next frame is the new view. Right for a broadcast's cut to a corner camera (the
  approach is the shot), a respawn, a replay's angle; wrong between two views of the same subject
  a few metres apart, where all a cut tells a player is that the picture was replaced. The
  arriving rig starts fresh (its followers snap).
- **Blend**: for `duration` both rigs are framed, and the pose is blended from the old to the
  new on an ease (smootherstep). The eye's blend is held in the SUBJECT's frame (how far ahead,
  to the side and up of it, and where it looks relative to its heading), rebuilt around the
  subject every frame, so at 140 km/h a lens is not left standing in a field while the subject
  drives out of the frame; it lands ON the arriving rig's pose, not near it; a floor under the
  path keeps it out of the ground between a bumper and a boom over a brow, faded in and out.
- **Inertialize**: only the NEW rig is framed. At the switch the offset between the old pose and
  the new (and its velocity) is recorded, and decays to zero on a quintic over `duration` that
  starts with the offset's own velocity and ends at rest, on each axis of the subject's frame
  and each angle on its own. Unlike the animation version, which keeps only the velocity along
  the offset and drops one moving away, every component of the velocity is kept (one moving
  away carries the offset out and back): the `camera-transitions` lab measured the dropped part
  as a kick of up to 10 m/s at the switch. Continuous in position and velocity,
  half the cost of a blend, and it re-bases cleanly when a second change arrives mid-way. The
  default for a change of rung on the same subject.

Every rig in a ladder is kept WARM: its followers keep swinging behind the scenes, so a switch to
it starts from where it would be, not from a stale frame (the cost is a few followers a rig).
Which edit a change gets is the game's (its ladder says, per pair of rungs); `cl_camblend` scales
every duration, and 0 makes every change a cut.

## The camera stack

`Camera` holds the ladder's rigs and their states, the active rung, the transition in flight and
the modifiers, all fixed-size:

```cpp
Camera cam = make_camera(ladder);          // the game's rigs, by rung
cam.set_rung(rung, Edit::inertialize);     // or Edit::cut, Edit::blend
cam.event(Moment::landing, 0.6);           // trauma, a kick, a dip
Pose p = cam.update(subject, world, dt);   // once a display frame
to_frame_view(p, frame_view);
```

A director (a broadcast, a replay's film camera, an impact camera) is a game's function that
calls `set_rung` with a planted rig's row and an edit; the stack has no notion of a TV.

### Fall Line's ladder (`games/fall_line/camera`)

Fall Line's run is seen through six rungs, each a row of an engine rig, in the order its camera
key (C) walks them: `tips` and `helmet` (mounted), `chase`, `far` and `high` (booms, the reference
ladder's rows) and `orbit` (the menu's drone, off the key). Per frame, `frame_camera` does the
game's part around `update`:

- **The subject** is the skier: the skis' base under the boots, the figure's frame (the snow's
  normal on the snow, the flight's pitch in the air) rolled by his lean, the packed share as the
  surface (the boom's tremor is harder on the groomer). A landing and a reset are read off two
  frames' states, so a frame that took several steps misses neither.
- **The worn eyes** are set off the figure each frame: the helmet's in front of the head (the tuck
  folds it down), the tips' a hand over the snow ahead of the boots, raised by the skis' sink.
- **The world** answers the ground, the trunks (`obstacles`: a boom is pushed off them) and the
  gates' posts (`clear`: a boom pulls in for them).
- **The hand-overs**: inertialized between two booms, blended in the skier's frame to and from a
  worn lens (0.6 s; 1 s to and from the orbit). While a lens passes through his head its near
  plane is pushed past it, and no lens goes under 0.15 m over the snow or nearer than 0.12 m to
  a trunk's bark (a wiped-out skier is thrown into the tree he met, and a worn lens with him).
- **The woods seen through** (M26g): the frame is handed the point the rung frames on him
  (`framed_point`: his middle, lower when he is down), and the forest's draw keeps only a
  dithered share of every tree whose crown holds the lens or crosses the line to that point
  (`docs/rendering.md` § Seeing through). A view of the mountain hands none.

The `camera-world` lab follows a run on every rung at once and pictures them at the run's
moments and through the densest wood, and measures how much of him each outside lens sees.

### The machines' lenses (`games/fall_line/camera/machines.hpp`)

On the free ride's machines the run is seen through a ladder of the machine's own, four rungs a
machine, walked by the same key:

| Machine | Worn | Chase | Third | Fourth |
| --- | --- | --- | --- | --- |
| snowmobile | `seat`: his eyes on the boards, the bars' roll, the belt's buzz | `chase`: low and close, its bank through a turn | `far` | `high` |
| helicopter | `seat`: his eyes on the skid, looking down past it, rolled with the airframe | `chase`: well back and over the cabin, banked with it in the air | `far` | `down`: the long fall under it |
| paramotor | `pilot`: his eyes in the harness, rolled with the wing | `chase`: behind him in the air, banked with the wing | `wing`: from up by the canopy, looking down on him | `down` |

- **The subjects**: the snowmobile's CoG and its own frame; the helicopter's skid datum and its
  fuselage's heading, pitch and roll; the paramotor's pilot (his skis' base, hung under the wing)
  and the wing's heading and bank. The worn eye is the figure's helmet eye carried into the
  machine's frame every frame. Landings and resets are read as the skier's are.
- **The booms in the air** keep the fall look off (it would be on for the whole flight) except on
  `down`, where it is the point, and bank with the craft (`bank_air`).
- **The hand-over** (`RideCamera`): as he boards a machine or leaves it, the rung follows him
  (worn to worn, chase to chase, the third and fourth to theirs), the arriving camera is snapped
  onto its subject and the picture blends from the leaving camera's live pose to the arriving
  one's over 1 s on a smootherstep, the eye swung round him at a blended reach (two arms on
  either side of him would otherwise cross through him) and his head clipped all the way. A change back before the hand-over is done runs it backwards
  from where it is. Every lens is settled as the skier's are (the head, the snow, the bark).
- The machines themselves are not drawn in the game's frame yet: the pictures show him.

The `camera-machines` lab takes each machine's outing with the bot and pictures every rung at its
moments and every hand-over as a strip.

## Settings and cvars

Every number a player may change is a cvar (`engine/console`, `docs/console.md`), bound to the
`CameraSettings` struct the camera reads each frame, so the camera never looks a name up. A game
registers them with its own defaults (`CvarSpec::fallback`) and its WORLD UNIT: a game whose
world is in inches reads `cl_rollspeed 200` as 200 of its units a second, converted once by the
binding; every row inside the engine is in metres, seconds and radians.

| Cvar | Unit | What |
| --- | --- | --- |
| `cl_fov` | deg | the first-person lens, horizontal at 4:3 (0: the rig's own) |
| `cl_viewheight` | world units | the eyes over the body, first person (0: the rig's own) |
| `cl_bob`, `cl_bobcycle`, `cl_bobup` | share, s, share | the stride's height per unit of speed, its period, the rising share |
| `cl_rollangle`, `cl_rollspeed` | deg, world units/s | the strafe roll and the sideways speed that reaches it |
| `v_kicktime`, `v_kickpitch`, `v_kickroll` | s, deg, deg | the view punch |
| `v_idlescale` | deg | the idle sway at its most (0 off) |
| `cl_landdip` | share | the landing dip |
| `cl_camera` | name | the rung (`chase`, `helmet`, `eyes`, …: the game's ladder), registered only when a game asks for it |
| `cl_chasedist`, `cl_chaseheight` | world units | the arm of every boom named `chase` (0: the rig's own) |
| `cl_fovspeed` | share | how much the fov widens with speed on a boom |
| `cl_camblend` | share | every transition's length; 0 cuts |
| `cl_motion` | 0..1 | comfort: the bob, the roll, the tremor and the shakes at once |
| `cl_shake` | 0..1 | the shakes alone |

They live in `engine/camera/cvars.hpp`. `add_camera_cvars` registers them on a console with a
game's `CameraDefaults` (its world unit, `unit_m`, and its own fallbacks, in the cvars' units) and
binds them to a `CameraCvars`; `apply_camera_cvars`, once a frame, writes them over the ladder
the camera was made from: the settings struct, every first-person row (in metres and radians)
and the boom named `chase`. A length or a lens of 0 leaves the row its own, so a ladder stands as
it was cut until a player turns it. The `landdip` scales the row's own dip. `cl_camera` changes the
rung by the game's edit when it moves, and a name off the ladder is left with `false`.

Fall Line registers them at the engine's defaults (a world in metres) and names its view with its
own `cl_view` (a rung or a still view) instead of `cl_camera`; the window applies them to the
skis' ladder and every machine's before each frame (`apply_ride_cvars`). `ge cvar ls cl_` lists
them. `camera-rigs --motion=0` runs every rig through every scenario at comfort 0 and fails a
horizon that rolls; `camera-shake` holds the shakes still at comfort 0.

## The labs

| Lab | Question | Writes |
| --- | --- | --- |
| `camera-follow` | Does every follower give the same path at every frame rate, and what are its step and ramp responses? | the responses charted at 30–240 Hz and with long frames; the worst gap between rates |
| `camera-rigs` | What does each rig do through each scripted subject (walk, run, strafe, jump, long fall, stairs, crouch, a skid turn, a cliff drop, a teleport)? | eye, angles and fov over time as charts; the subject's path and the eye's from the side and from above; the subject's place in the frame; jerk; ground and obstacle clearance; `gl::find_teleports` on the eye |
| `camera-transitions` | Is every edit continuous where it should be and instant where it should be? | each edit between each pair of rigs while the subject moves fast: the eye's speed and the look's rate across the switch, the overshoot, the floor; filmstrips |
| `camera-shake` | Does a shake have the shape and the size it should, and does comfort take it away? | trauma envelopes, the per-axis motion and its frequency content, at comfort 1, 0.5 and 0 |
| `camera-world` (Fall Line, GPU) | What does each rung of Fall Line's ladder SHOW at the named moments of a run, and does any lens stand inside a trunk or the snow? | each rung's picture over seed 38 at the start, the hardest carve, the biggest jump, its landing, the closest pass to the trees and a straight run through the densest wood; the camera key's hand-overs as strips; every lens's least height over the snow and least gap to bark over the whole run; in the woods the trees each lens sees through, and on every sheet the share of him each outside lens sees (`--see-through=0`: the trees drawn whole, the before) |
| `camera-machines` (Fall Line, GPU) | What does each rung of the snowmobile's, the helicopter's and the paramotor's ladders show at the moments of an outing, is the picture handed over on and off without a jump, and does any lens stand inside a trunk or the snow? | each machine's four rungs at four moments of the bot's outing on seed 38 (the board, the climb, the air and the hop off; the lift-off, the cruise, the bank and the drop; the launch, the turn, the height and the landing); every hand-over on and off as a strip on the worn rung and the chase; every lens's least height over the snow, least gap to bark and largest step through a hand-over past his own |
| `se-view` (Shooter's Edge) | Does the first-person view bob, roll and kick as the classic look does? | the eye and the angles through a scripted run, against the formulas at the classic configs' values |

## Research

What this design takes from published work on game cameras, restated as the decisions above:

- Second-order followers placed by frequency, damping and response, and critically damped
  springs by half-life, for weight that stays stable at any frame rate: Ryan Juckett, *Damped
  Springs*; Daniel Holden's spring writing on theorangeduck.com; Unreal's `FCriticalDamper`.
- Composition by dead zone and soft zone, damping per axis, and the look-ahead's smoothing: Unity
  Cinemachine's composers.
- Cameras as a STACK of rigs evaluated in isolation and blended (camera modes), beside an
  additive persistent stack (modifiers): Unreal's Gameplay Cameras blend stacks.
- Inertialization (only the new pose evaluated; the old offset decays on a quintic):
  David Bollo, *Inertialization: High-Performance Animation Transitions in Gears of War*, GDC 2018;
  Holden's *Dead Blending*.
- Trauma-driven shake (trauma squared, linear decay, smooth noise, angles over offsets): Squirrel
  Eiserloh, *Math for Game Programmers: Juicing Your Cameras With Math*, GDC 2016.
- The common mistakes (a cut between two views of the same subject, uncommanded motion, a lens
  inside a wall, lag on the look): John Nesky, *50 Camera Mistakes*, GDC 2014.
- The sense of speed: a wider field of view raises the speed players perceive, strong motion blur
  lowers it, and a higher camera at speed raises it (Holm, Skovhus and Kraus, 2017, on
  third-person racing cameras).
- Comfort: let a player turn down every motion they did not command, offer a field-of-view
  setting, avoid repetitive bobbing outside play (Xbox Accessibility Guideline 117).
- Collision: pull in at once and let out slowly, cast a fat line (or the near plane) rather than a
  thin ray so a wall is cleared before it fills half the frame.
