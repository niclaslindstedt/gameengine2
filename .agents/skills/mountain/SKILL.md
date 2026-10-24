---
name: mountain
description: "Use for plugins/mountain in gameengine2: the generated ski mountain any game on one links. The world generator (mapgen: generate_level, the terrain, the piste, kickers, cliffs, the forest, the day and the weather, the gates, versions, level_digest), the rule book every finished map is held to (analysis: analyze_level), and the mountain as drawn (app: the snow's material, its corduroy, relief, wrap, glitter and glare)."
---

# The mountain plugin

`plugins/mountain/` (`ge_mountain`, namespace `ge::mountain`) turns a seed into a clean ski
mountain. It depends on the engine's core only (`ge_add_plugin(mountain ENGINE core)`) and on no
plugin: a game on a generated snow mountain links it whatever it rides down it, so it never
learns about the rider. It speaks the ski world's words (piste, gate, kicker, tree line), never
one game's: which maps a game races, its modes and its strings stay in the game.

- `mountain/mapgen/` (`ge::mountain::mapgen`): `generate_level(seed, opts)` returns the `Level`
  (`types.hpp`): the heightfield, the open piste (a point every 2 m), the checkpoints (start
  gate to finish), the spawn and grid, the trees, the kickers, the sun and the weather. Each
  attempt draws off `sub_seed(seed, attempt)` in a fixed order and is accepted only if
  `analysis::analyze_level` finds it clean. `versions.hpp` says which generator built a map;
  `digest.hpp` is `level_digest`.
- `mountain/analysis/` (`ge::mountain::analysis`): the rule book (`mapgen/rules.hpp`, the
  R-rules) re-checked on the finished map, reading only what the `Level` publishes.
- A SKI AREA RACED AS A MAP (version 8): the build (`resort_build.hpp`) runs its stages, then
  `resort_level.hpp` moves the grids and woods out of the attempt ONCE into a base map
  (`resort_base`) and stands each course up on it in place (`set_course`: the course's line, its
  slalom gates from `course_gates.hpp`, its own day from `course_day.hpp`, the kickers and drops
  it skis named by its arc). `generate.cpp` holds the area to `analysis::analyze_resort` on the
  first course's map, each course to `analysis::analyze_course` (one that fails is not offered),
  and races `choose_course`'s pick (`GenerateOptions::course`, `grade`, else `deal_grade`).
  `analyze_level` answers both on a map with a `resort`; `analysis/shared.hpp` holds the checks
  every map shares (the start line, the day, the weather).
- A RACE COURSE SET OVER A BUILT MAP (R31–, the rule book's numbers in
  `discipline_rules.hpp`): `course_prep.hpp` prepares the hill for any discipline (the piste's
  kickers levelled, the start drop, groomed, combed, the woods cut), and each discipline sets its
  course over it as a new map whose `Level::race` names it and keeps the map it was set over
  (`RaceCourse::base`, so a second run is set over the same mountain). `slalom.hpp` is the slalom
  (R31); `speed_events.hpp` the downhill (R32), the super-G (R33) and the giant slalom (R36),
  their racing line, gate spacing and speed trap from `speed_course.hpp` (the crests shaved round
  by `course_prep`); `speed_ski.hpp` the speed-skiing track (R34), a track of its own searched
  and cut straight down the face, which REPLACES the map's `track`; `ski_cross.hpp` the ski cross
  (R35), a course BUILT on a stretch of the piste (its own weaving line, berms banked into the
  ground, rollers, jumps and step-downs built onto its profile), which replaces it too. `analysis/race.hpp` holds a course to its discipline's rule; `analyze_level` answers it
  on a map with a `race`.
- THE TERRAIN PARK (R20, `rules::trick`), only on a map asked for with
  `GenerateOptions::tricks`: `park.hpp` plans the kickers down a line (`plan_park`: the three
  sizes in turn, clear of the line's kickers, drops, drifts and gates) and stamps them into the
  finished ground (`stamp_park`). On the single piste the park is planned inside the attempt,
  before the drifts, and an attempt whose piste carries none is refused; on a ski area
  `set_parked_course` (`resort_level.hpp`) plans it down a course first and sets the course's
  gates round it (`course_gates` with the park as zones), and an area none of whose courses
  carries one is refused unless a course was asked for. `analysis/park.cpp` (`check_park`)
  holds the finished park; a map built without tricks moves by nothing.
- THE SKI ROUTES (R42, `resort_rules::route`), from v9 on: `ski_routes.hpp` (`lay_ski_routes`)
  finds the ORANGE lines on a built ski area, two cheapest-path searches over a grid of squares
  (from every chair's and gondola's top, and back from every run) joined through one square
  steeper than a black; v9 lays the shortest line, v10 the WILD one (a step's cost by the woods,
  the edges and the cliff faces it crosses, drops over crags, the line threaded between the
  trunks). `route_features.hpp` then fells the trunks on a wild line and cuts its jumps
  (`O1…`, `Kicker::route`) and their gaps into the ground. `generate.cpp` (`lay_routes`) lays
  them on the attempt's base map BEFORE the area is analysed, so `analysis/routes.cpp`
  (`check_routes`) and every other rule hold the map with them; the jumps ride on
  `ResortAttempt::route_kickers` into every course set. A map from v8 or before carries none,
  and the digest reads routes only where there are some.
- A FREESTYLE VENUE BUILT ON A MAP (R37 onward, the rule book's numbers in `venue_rules.hpp`):
  `straight_venue.hpp` searches a straight line of its own down the face where a profile designed
  off the horizontal cuts and fills least (`find_line`) and grades the ground to it (`grade_venue`:
  eased out, groomed, the woods and the arena cleared, lowered under a raised stretch). THE SKI
  JUMPING HILL (R43, `ski_jump.hpp`): `ski_jump_profile` builds each hill (`JumpHill::large`,
  `normal`) element by element from the construction norm's formulas over its row's free choices,
  the same on every map; `set_ski_jump` sets it over a map as a new map whose `Level::ski_jump`
  names it and keeps the map under it (`SkiJumpHill::base`). The hill is an ANALYTIC surface
  (`Level::venue`): `ground_at` and `normal_at` answer off its profile inside its width (only the
  deck up the raised in-run), and the grid under it is cut to it, never above it. Any course or
  hill already on the map is set over the map under it (`original_of`).
- THE REAL FACES (R25, M4d): 114 stretches of real ski mountains, each 4×4 km from its summit
  ridge to its valley floor, turned so the fall line runs down the map and laid 1:1.
  `real_face_crops.hpp` is the table (each face's id, region, place, middle and bearing);
  `real_face.hpp` decodes a face's kept grid (126×126 samples 32 m apart, on 0.5 m steps over its
  floor, second-differenced, zig-zag varints, base64) and samples it bicubically
  (`face_height`), and turns a map point to the globe and back. The grids are GENERATED into
  `real_faces/` (a file a face and `index.cpp`) by the `real-faces` lab's bake
  (`--bake --fetch --write`, from the repository's root): the free Copernicus 30 m elevation
  model read off its public bucket with `curl` into the gitignored `.cache/dem/`, through the
  labs' GeoTIFF reader (`labs/dem_tiles.hpp`) and inflater (`engine/lab/inflate.hpp`). Never
  edit a face's file by hand; a crop moved is a face baked again and its golden row rewritten.
  A face is BUILT ON by `GenerateOptions::face` (its region the map's, `massif::real::attempts`
  attempts): `plan_massif` reads it (`read_face`: the summit ridge down to its brow, the peak,
  shoulder and valley on the real sides, the floor and the face's own profile) and every draw is
  still made, so the stream runs on as on a dealt massif; the height is `face_lift`, the real one
  over the floor fading onto the profile on the valley floor. The rule book bends where a real
  mountain is not a dealt one: every bend is a number in `massif::real::least`, read where the
  dealt rule is (`grade_face`, `ramp_fit`, `ChainMost`, `place_start`'s `face`,
  `StationGround::level`, the drags' and access's `slack`, the analysis's `level.face`). A new
  bend is a number there and a branch beside the dealt rule's, never a second code path.
- THE REAL FACES' HINTS (R25, M4d3): the real ski area on each face, off OpenStreetMap (ODbL;
  the notice is in every generated file). `real_hints.hpp` holds a face's real lifts (chair,
  gondola, drag; their ends on a 2 m grain), its pistes (signed colour, bends top first thinned to
  6 m, the width of the piste's area at each) and its woods by height (eight bands of cover over
  the face's heights and a tree line, or none where the map draws no forest), packed as zig-zag
  varints in base64 (`packed.hpp`, shared with the faces' grids). They are GENERATED into
  `real_hints/` by the `real-hints` lab's bake (`--bake --fetch --write`): the map API read with
  `curl` (`labs/fetch.hpp`) into the gitignored `.cache/osm/`, a polite wait between calls, a
  relation's missing members fetched whole (`labs/osm_data.hpp`). The generator LEANS on them on
  the first `massif::real::hinted` attempts (`GenerateOptions::hints`, on by default):
  `real_lean.hpp` moves the stations and lifts onto the real ones within reach, lays a piste
  slot on the real piste leaving nearest its start (its colour, its line through `follow`, its
  width through `width_at`) or steers it through a real piste's bends (`via`), and bills a run its
  real piste's colour within `least::billed` of what it measures (the analysis holds the same).
  The face's woods scale R14's curve by the real cover at each height (`resort_courses.cpp`), and
  their line is the map's tree line. Every lean is a pure function of the hints and draws nothing,
  so the stream runs on as without them; the walk's last `via::last` tries drop the steering.

## The lifts run (`mountain/lifts`, M13a2)

`plugins/mountain/lifts/lift_loop.hpp` (`ge::mountain::lifts`, built into `ge_mountain`) is how
any game on the ski area finds a lift's carriers: `plan_lift` once a lift (`lift_plans` once a
map), then `carrier_at(plan, k, t)` and `carrier_place`, a pure function of the clock that is
never stepped and never stored, so a run, a replay and a picture agree. A detachable (a chair,
a gondola) slows its carriers through its terminals on a clock tabled every `kClockStep` m of
loop, so they are spaced evenly in TIME and bunch in the stations. The load zone's middle, the
gauge and where a rider is let go are the generator's (`lift_look`, `let_go_of` in
`mapgen/lift_line.hpp`), read and never restated. `tests/mountain_lift_loop_test.cpp` holds it on
three hand-built lifts; the game's ride over it is the `sim` skill's (`lift-ride` lab).

## The village's streets (`mountain/village`, M13g)

`plugins/mountain/village/` (`ge::mountain::village`, built into `ge_mountain`) lays a ski area's
village on the valley floor below the hub: `plan_streets(level, pads)` is a pure function of the
map off hashes of its seed (`pick`), never a stream, so no map's digest moves for it. The plan is
a loop (the main street along the hub's valley edge, the back street behind it, cross streets at
both ends and between), one or two roads out off the map's edge, the square below the main lift
and the day car park with its aisles; candidates are tried round the village's point and at three
spans, and the first whose loop, square and road clear everything stands. Every number, from the
cross-sections to the clearances, is `street_rules.hpp`'s. A street keeps off the runs, the lifts'
lines, stations and queue lanes, the hub, the tunnels, the gates, the start, the finish, the
kickers and the `pads` the game names (the machines' spots belong to the game, which passes
them). `bake_ground` paints the plan onto a metre grid (`Ground`: road, street, felled, garden)
and `felled_trees` reads which of the map's trees go. The caller keeps the plan; nothing stores it
on the `Level`. `tests/mountain_village_test.cpp` holds the loop, the clearances, the graph, the
ground and the reference seeds' golden; the `village` lab draws it.

## The mountain as drawn (`mountain/app`, M8)

`plugins/mountain/app/` (`ge_mountain_app`, namespace `ge::mountain::app`, built only with the
GPU: `APP render gpu`) is what any game on a snow mountain draws the mountain with. Nothing in
it moves the ground the simulation skis on; every term is light.

- THE SNOW'S MATERIAL (`snow.hpp`, `snow.wgsl`): the terrain's `fs_main` over the engine's
  `render/wgsl/terrain_draw.wgsl` (`TerrainSource::shader` and `material`, the `render` skill).
  The game keeps its colours (the albedo it hands the terrain); the material is what the snow
  does to the light. Its ground map (`bake_snow_ground`, baked once a map): the packed share, the
  rock share (`rock_share` of the slope, which the game's albedo reads too, so the two agree),
  and the piste's direction as the DOUBLED angle (a comb line has no front and back), faded to
  nought over 4 m inside the piste's edge. The shader's terms, each with a gain in `SnowLook`
  (1 the tuned look, 0 none of it): the relief (swells and grain on the loose snow, a broken face
  on rock), the corduroy (14 cm a rib, worn in patches) and the lanes (5.2 m a swath) across the
  piste's way, the wrap's blue terminator, the glare (forward scatter, harder on the piste) and
  the glitter (one crystal at most a cell, a point, written to the reactive mask).
- THE TRAIL MAP (`trails.hpp`, `trails.wgsl`, M8b): every track laid this run. A coarse RGBA8 map
  over the whole map and a fine window round the camera; each texel the groove's depth and the
  berm's height as roots of their channels' full scale, and the pressed floor. A stamp is a
  capsule (the prints are `skier/app/tracks.hpp`'s, the `skier` skill) drawn as an instanced quad
  with MAX blending, so stamps never race and a groove ridden twice keeps the deeper. The window
  moves when the camera leaves its middle half and is REDRAWN from the stamps' history (filed in
  32 m buckets by their centres), never copied: a track laid out of the window comes back sharp.
  The game converts its prints to `TrailStamp`s and hands them over (`add_trail_stamps`); each
  frame `prepare_trails` (the window, the uploads; an overflow past `capacity` drawn in command
  buffers of its own) and `encode_trails` (two passes that always open: an empty compute pass
  when there is nothing to stamp). The snow's material reads both maps (`create_snow`'s `trails`;
  without one it binds a clear texel): the slope of berm less depth across the larger of a texel
  and the pixel's footprint, the trough's shade, the pressed floor smooth, and no berm inside a
  groove. Judge it with the `trails` lab (the outside groove deeper through a turn, read back from
  the map; a racer's track still there when another comes down it) and the `world` lab's moments.
- EVERY TERM FADES BY THE PIXEL'S FOOTPRINT (`foot`, m a pixel, from `fwidth` taken once at the
  top of `fs_main`, in uniform control flow), never by distance alone: a term whose wavelength a
  pixel spans a quarter of is aliasing, at any resolution and field of view.
- To judge it: the `snow` lab (the close views, each PLAIN with every gain at nought beside
  SNOW, at 10, 12 and 14 h) and the `world` lab's `corduroy`, `powder` and `glitter` views. A
  close view finds a spot in the sun past the mountain and the trees (`views.cpp`'s `snow_spot`);
  judge the material only where the sun is on the snow, because in shade only the relief and
  the sky's light are left.

## Order of work (docs/goals.md M4a to M4d)

1. Bottom-up: types, query (nearest track point), the steps, the compile, then generate.
2. Tests: `tests/mountain_generate_test.cpp` against `tests/golden/mountain_generate.txt`
   (`make golden-libm`): `level_digest`, every grid and every section of the level over a
   spread of seeds, including seeds whose first attempts are refused.
3. M4b adds the ski area, M4c the ski routes, M4d the real faces. Each lands here.

## Rules

- The order of draws inside an attempt is fixed. It is part of every digest.
- A generator change that moves what a seed builds is a new version row (`versions.hpp`);
  never a regenerated golden.
- The generator and the analysis read one rule book and share nothing else.
- A game's lists of which seeds it races belong to the game; their digests are this plugin's
  tests.

## Labs

`plugins/mountain/labs/`: `level` (the map from above, captioned, with its gate table),
`analyze` (the rule book over seeds, `--refusals` for why attempts were thrown away; both take
`--version=1,8`, the single piste and a ski area's race map) and
`mapgen-cost` (`PRESET=release`), and for the ski area `resort` (the area from above and its
courses, with its lifts, runs and courses as tables and the attempts tried) and `junctions`
(where the groomed snow steps where runs meet, the worst drawn as relief), and `village` (the
village's streets, the ground under them and the trees felled, over seeds). A change to mapgen or
analysis runs `level` and `analyze` before and after (`--tag=before`, then `--compare=before`);
a change to the ski area's build runs `resort`, `junctions` and `village` the same way, and a change to a
discipline runs its lab (`slalom`: both runs straightened, the map from above, the gate table;
`speed-events`: the downhill, super-G and both giant slalom runs straightened, with their
jumps, nets and speed trap; `speed-ski`: the track's profile from the side and the map from
above; `ski-cross`: the course close up, its profile with the mean fall taken out, the map). A
change to the terrain park runs `park` (the park close up, its profile, the map) at
`--version=1,8`; a change to the ski routes runs `routes` (the area with its routes, each route
close up and from the side, the routes' table) at `--version=9,10`; a change to a venue or the ski
jumping hill runs `skijump-hills` (each hill's earth moved, tower and arena as a table, its
profile at true scale from the side, the map) at `--version=1,9`; a change to the real faces' crops,
their codec or their bake runs `real-faces` (the table of every face and the sheet; with `--bake`,
the samples each bake moves against the tree's); a change to the hints' codec or their bake runs
`real-hints` (every face's lifts, pistes and woods as a table, a face named alone drawn over its
heights; with `--bake`, each face's bytes against the tree's); a change to the face's massif or to any rule a
real face bends or to how it leans on its hints runs `real-maps` over every face (`--faces=all`,
about ten minutes on one core; split the faces over processes) before and after, and no face may
stop building; its `off real m` column (and `--hints=0` beside it) says how near the runs lie to
the real pistes. The `labs` skill owns how. A change to the snow's material runs `snow` (and `world --views=corduroy,powder,glitter
--hour=12`) before and after, and `profile --snow=0` beside `profile` for its cost.
