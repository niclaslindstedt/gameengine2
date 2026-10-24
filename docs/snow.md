# Snow

The plan for snow in the engine: a layer of snow a game lays over any ground in one call, which
remembers what crossed it (a ski's groove, a tyre's rut, a tread's trench, a boot's posthole, a
body's crater), answers every question the physics asks of it (how deep, how dense, how hard,
how far a load sinks, what it costs to push through, what it grips), changes with the weather
and the day (new snow, settling, tracks filling in, grooming, melt and crust), and is drawn and
heard from the same numbers.

The roadmap rows are M29a–M29l in [`goals.md`](goals.md); each row's status is
`goals/M29<x>.md`. This page is the design those rows build to. Every number in it is a
starting value: the row that builds it measures it in its lab and corrects it here.

## What the games ask of it

Snow today is one number the physics reads (the mountain's packed share, a baked grid that
never changes) and a picture the GPU keeps (the trail maps of `mountain/app/trails.hpp`, which
the physics never sees). Each game of the kind the engine is drawn for uses snow differently:

| Game | The snow it stands on | What crossing it does | What it must read back |
| --- | --- | --- | --- |
| Fall Line (skiing) | A mountain's cover: the groomed piste, the powder beside it, drifts across the piste, ground kept clear round the lifts, machine snow; a dial of depth up to a metre of fresh | Skis carve grooves and skid bands; a fall leaves a crater; through a free-ride day the piste is skied up and the powder beside it tracked out; groomers reset it; the day's fall buries it | The packed share and the sink under each ski station; in deep snow, snow that stays pressed and gives under load; a skier following a track finds it firmer |
| A snowmobile game | Open powder, packed trails, a groomed track | The tread digs a trench and leaves a packed trail a metre wide; the skis cut two lines; a stuck machine digs itself in | A trail is easier to ride than the field; a trench holds the belly; the tread's grip is the snow's shear strength |
| A rally game | A snow road worn into lines by earlier traffic, the snowbank at its edge, the field beyond | Tyres compress the snow into ruts with walls; the body ploughs in deep snow | The rut is easier to drive and grips worse (packed snow slides, loose snow bites); a rut wall steers a wheel; the cut under the wheels is not the surface's drop |
| Shooter's Edge (a snowy level) | A courtyard, a roof, a ledge of snow over brush geometry, on several floors | Players leave footprints that give their path away; a body leaves a mark; a blast throws snow | Footprints in step with the players on every client; the footstep's sound by the snow |
| Any game | `add_layer(world, region, ground, preset)` and a preset by name | One `press` per contact per step | One `sample` per question |

## Homes (G8)

Snow is generic: any genre with winter in it uses it unchanged, so the layer, its physics and
its drawing are engine modules, not a plugin. Only the tuned numbers that make one machine's
contact or one world's cover are in that plugin or game.

| Home | Holds |
| --- | --- |
| `engine/snow/` | No GPU, simulation code: the layer (region, ground, cover, the deformed tiles), the one `sample` query and its batch form, `press` and the imprint record, the contact laws, the snow kinds, time (new snow, settling, filling, grooming, melt), the tiers and the budget, the snapshot |
| `engine/render/snow*` | The snow drawn: the cover and deformation mirrored on the GPU, the shared snow light model (WGSL), the fine marks (sub-cell grooves, the trail maps generalised), the displaced near patch, snow as a sheet over any ground, snow on meshes |
| `engine/audio/` | Snow's voices: the crunch of a step or a tyre by density and temperature (the squeak of cold dry snow), the hiss of a sliding base |
| `plugins/skier/` | The ski's contact: its planing law and tuned numbers, stated through the engine's laws; the skier's prints (`skier/app/tracks.hpp` becomes a caller of `press`) |
| `plugins/snowmobile/` | The tread's and the skis' contact, the trench |
| `plugins/mountain/` | A mountain's cover: the piste's packed share (R10), the drifts (R17), the kept ground round the lifts, machine snow; the corduroy and the lanes as the mountain's addition to the engine's snow material |
| `plugins/shooter/`, `plugins/brush/` (M18) | A brush world's snow regions from its top faces; the boot's imprint and the footstep |
| `games/fall_line/` | Which cover a map gets, the free ride's day (traffic, groomers, the fall), the snow dial, the look's tuning |

## The model: a column of snow over ground

Everywhere a layer covers, the snow is a **column** standing on the ground: a depth `h` (m) and
a density `ρ` (kg/m³), so a mass `m = ρ·h` (kg/m²) per square metre. Mass is what snow keeps
when it is pressed: a load that compresses fresh snow from 80 to 400 kg/m³ leaves a fifth of
its height standing, and that drop is the footprint. Everything else is read off the column:

| Read | From | Used by |
| --- | --- | --- |
| `surface` | `ground + h` | Every contact, the drawn sheet |
| `packed` (0..1) | `(ρ − ρ_loose) / (ρ_packed − ρ_loose)` of the snow's kind, or the cover's own share where nothing has pressed it | The physics' existing blend (a skier's grip, sink and drag), the material's groomed look, the sound's hiss |
| `hardness` | The kind's strength at that density (Mohr–Coulomb cohesion and friction angle, Bekker's moduli) | The contact laws |
| `kind` | One of the named snows below | The laws' parameters, the look, the voice |
| `drop` | The cover's `h` less this column's | The drawn groove, the rut a wheel sits in |
| `wall` | The surface's rise across a cell edge | A rut wall steering a wheel, a ski held in a groove |
| `fresh` | New snow laid since the cover (the day's fall) | Buried corduroy, softened tracks |

**The named snows** (starting values, from the terramechanics and snow-science tables cited
under Sources; each row measured in `snow-contact`):

| Kind | ρ kg/m³ | Bekker n | k_c kN/m^(n+1) | k_φ kN/m^(n+2) | c kPa | φ° | Sliding μ (ski) | Tyre μ |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| powder (cold, new) | 50–100 | 1.6 | 4.4 | 197 | 1 | 20 | 0.04 | 0.40 |
| settled | 150–300 | 1.44 | 10.6 | 66 | 6 | 21 | 0.035 | 0.35 |
| wind-packed | 300–400 | 1.2 | 20 | 300 | 12 | 23 | 0.03 | 0.30 |
| groomed (piste) | 400–550 | 1.0 | 50 | 800 | 25 | 25 | 0.025 | 0.25 |
| spring (wet, corn) | 400–500 | 1.3 | 8 | 150 | 2 | 18 | 0.06 | 0.20 |
| crust over powder | 250 over 80 | (the crust breaks past its strength, then powder) | | | | | 0.03 | 0.30 |
| machine | 400–500 | 1.1 | 30 | 500 | 15 | 24 | 0.03 | 0.28 |
| ice | 830–917 | (rigid) | | | | | 0.02 | 0.10 |

### The cover: what lies there before anyone crosses it

A layer's **cover** is analytic and costs nothing to keep: per point, a depth, a density, a
packed share and a kind, from constants, from baked grids over the layer's plan, or both (a
mountain's packed share is its R10 grid, its depth the snow dial). Where nothing has been
pressed, `sample` returns the cover exactly, bit for bit: the mountain's `packed_at` read through
the layer is the same double as read from the level, which is what lets Fall Line move onto the
layer without moving a digest.

### The deformed tiles: only what changed is stored

What crossing the snow changes is kept in **tiles** over the plan, allocated on the first
imprint and never before, so a layer nobody has crossed costs no memory. A fine tile is 64 × 64
cells at the layer's cell size (10 cm by default, so 6.4 m a side); a cell keeps its column as
fixed-point integers, so it is the same bits on every machine and compact enough to mirror on
the GPU unchanged:

| Field | Type | Unit |
| --- | --- | --- |
| depth | uint16 | mm of snow over the ground (65 m at most) |
| mass | uint16 | 0.05 kg/m² of snow over the ground (3.2 t/m² at most); the density is mass over depth |
| kind | uint8 | the named snow |
| flags | uint8 | touched (changed by a press), pressed floor, berm |
| minute | uint16 | minutes since the cell last changed (settling and filling read it) |

The column keeps its MASS rather than its density so that compaction is exact: a press moves
whole units of mass from cell to cell and never rounds any away, and `mass + spilled ==
mass_in` holds to the unit on every layer (snow pushed off the region is counted as spilled).

8 bytes a cell, 32 KB a fine tile. A **coarse layer** beside the fine tiles keeps the same
fields at 16× the cell (1.6 m by default): the worn snow far from anyone who looks closely (§
Level of detail).

### `press`: a contact leaves an imprint

Every contact a game simulates calls `press` once a step with what is touching the snow:

```cpp
snow::Imprint imp{
  .shape = snow::Capsule{a, b, half_width},  // or Disc, Box, Ellipse (a boot, a tyre's patch, a body)
  .load = newtons,                           // the weight through it this step
  .motion = snow::Motion::roll,              // roll, slide, step, plough, dig
  .velocity = v,                             // over the snow, m/s
  .side_push = 0.3,                          // the share of the swept snow thrown aside as berm
};
snow::Pressed p = snow::press(layer, imp, t);  // p.sink, p.compacted, p.ploughed (kg), p.kind
```

Over every cell the shape covers, the column is compacted by the pressure the load puts on it
(the contact law of § Contact turns pressure into the density the snow reaches, capped at what
that pressure can reach); the depth falls by mass conservation; and for a ploughing or sliding
contact a share of the snow it sweeps is moved outside the shape as a berm, exactly the mass
lost from under it. A cell is changed only when the new column is lower or denser than the
old, so riding a groove twice keeps the deeper of the two, as the trail maps' max blend does
today. Each `press` appends an **imprint record** (the shape, the sink, the kind, the mass
thrown) to the layer's event list: the fine marks, the spray and the crunch read the same
record, so the picture, the sound and the physics can never disagree about where a track is.

`press` allocates nothing in steady state (tiles come from a pool the layer reserves), visits
only the cells under the shape's box, and is deterministic for a given order of calls: the
game presses in its step order.

**As built (M29a).** `engine/snow/layer.hpp` (the layer, the cover, the tiles, `sample`),
`press.hpp` (the shapes and `press`), `kinds.hpp` (the named snows), `reference.hpp` (a typical
carving and skidding ski, a boot, a tyre, a snowmobile's track and a fallen body, with their
loads) and `snapshot.hpp` (`save` / `load`, the `GESNOW1` format). The details settled there:

- The sink a press reaches is measured from the UNTOUCHED cover, never from the column as it
  stands, so a second pass of the same contact changes nothing and a deeper one only deepens.
- With no `sink` given, the sink is the contact laws' (§ Contact, M29b): Bekker's sink for the
  outline's narrower side, capped by what the column can compact before it reaches
  `reach_density` (350 kg/m³ at 10 kPa and 60 kg/m³ more for every e-fold of pressure, no
  denser than the kind's own cap).
- The thrown share is laid as a berm in a ring round the outline (0.9 of the half width out,
  never less than 1.5 cells), heaped at the edge and falling away; the units are dealt out in a
  fixed order, so the berm is the same bits everywhere.
- An outline narrower than three quarters of a cell is widened to it, with its pressure kept,
  so a carving ski's edge never slips between the samples.
- The coarse layer moved to M29d with the tiers that need it.

Measured by the `snow-field` lab: a 64 m layer crossed by a skier, a walker, a car, a snowmobile
and a fall allocates 19 tiles of 121 (608 KB), and a press costs about 11 µs in a release build (230 µs under the debug build's sanitizers).
`ge snow show <file.snow>` and `ge snow column <file.snow> --at=x,z` read a saved layer without
the game.

## Contact (`engine/snow/contact`, M29b)

The laws every machine's contact is built from, each a pure function of the column and the
contact, so a ski, a tread, a tyre and a boot are configurations, not separate models:

- **Pressure–sinkage** (Bekker): `p = (k_c / b + k_φ) · zⁿ` for a contact `b` wide, inverted
  to the sink `z` a pressure reaches, capped by what the column can compact: a column can only
  sink by `h · (1 − ρ / ρ_f(p))`, where `ρ_f(p)` is the density that pressure compacts snow to
  (about 350 kg/m³ under a boot, 450–550 under a vehicle, rising with the pressure). Over the
  base (the ground, or old hard snow) the sink stops; in a metre of fresh there is no base in
  reach, which is the deep snow Fall Line's dial already models.
- **Compaction and bulldozing resistance**: the work of pressing the snow down, `R_c = b ·
  ∫₀^z p dz`, and of pushing it ahead of a front sunk into it (the plough), growing with the
  sink and the square of the speed.
- **Shear strength and traction** (Mohr–Coulomb with Janosi–Hanamoto slip): `τ_max = c + p
  tan φ` and `τ(j) = τ_max (1 − e^(−j/K))` for a slip `j`, so a tread's lugs and a tyre's
  tread grip by the snow's strength and a spinning one digs.
- **Sliding friction** of a base over snow by kind and temperature (lowest near −5 °C, rising in
  deep cold and in wet snow), and a tyre's on packed snow and ice.
- **Planing**: the support a moving contact finds rises with speed (`exp(−(v / v_plane)²)` of
  the static sink), the ski's and the sled's law today, stated once here for any fast contact.

The skier's and the snowmobile's tuned numbers stay theirs; M29j states them through these
laws and holds every race digest while it does.

**As built (M29b).** `engine/snow/contact.hpp`: one `ContactRow` a kind (`contact_row`) and the
laws over it: `bekker_sink` / `bekker_pressure`, `sink_cap` and `column_sink` (the one `press`
reads), `compaction_resistance`, `bulldozing_resistance` (Rankine's passive pressure over the
front plus the momentum of the snow swept), `shear_strength`, `shear_at` and `tread_traction`
(Janosi–Hanamoto averaged along the contact), `glide_friction`, `tyre_friction` and
`planing_sink`. Settled snow's row is the published set for natural snow cover; every other row
is fitted to the sink a 10 kPa plate 10 cm wide reaches in it (powder 58 cm, so a standing ski
sinks 27 cm and a boot post-holes to the cap; groomed 1 cm; ice half a millimetre). Ice holds a
track by friction alone. A game with better numbers passes its own row. The `snow-contact` lab
charts all of it for every kind.

## Time (`engine/snow/time`, M29c)

The snow changes with the day, cheaply, because most of it is a function of time rather than
state:

- **New snow** is laid by the game's fall rate (Fall Line's `mountain/air` fall), a pure
  function of the map and the clock: the layer adds it on read (`fresh(t)` integrated in closed
  form), so a fall costs no stepping; a cell pressed during the fall records the fresh it was
  pressed under, and only what fell since lies in its groove.
- **Settling**: a column's density rises toward its settled value with its age, `ρ(t) = ρ_s −
  (ρ_s − ρ₀) e^(−t/τ)`, τ about a day at −5 °C, faster when warm: read off the cell's age, never
  stepped.
- **Filling**: new snow and drifting snow fill hollows first, so a groove's drop shrinks by the
  fall and the drift over it; a track laid in a storm is gone in an hour.
- **Grooming** (`groom`): a swath reset to the groomed kind and a flat surface, the snow moved
  to fill the drops (mass kept), the corduroy flag set.
- **Melt and refreeze** by the air's temperature: wet snow, corn, the crust a night leaves, ice
  where traffic polished it.
- **Wind drift** (with M24a's wind): snow lifted past the threshold friction velocity and laid
  in the lee, filling tracks and building drifts across a piste or a road.

## Level of detail: keep fine only what someone is close to

A free ride's day lays tens of kilometres of tracks; a courtyard's match lays footprints over
a few hundred square metres. The layer keeps detail where it matters and the rule that keeps it
deterministic is the water's: **the simulation's tiers are measured from the actors the game
names** (each player, each machine with a rider), never from the camera, because a camera is a
machine's and differs between a server, its clients and a replay.

| Tier | Simulation (from the nearest focus actor) | Picture (from the camera) |
| --- | --- | --- |
| **Near** (about 0–64 m) | Imprints land in fine tiles at the full cell; every contact reads them back | The displaced patch: grooves, berms and craters as geometry; the fine marks at a few cm a texel |
| **Mid** (about 64–512 m) | Imprints from actors no focus is near land in the coarse layer (the worn share and the drop, at 1.6 m); a fine tile no focus has come near for a minute is **folded** into the coarse layer (mass held) | Parallax and normals from the deformation; the coarse marks |
| **Far** | Only the coarse layer and the cover | The cover's look and the coarse map's tint |

**Folding never loses snow.** A fine tile folded keeps each coarse cell's mass to the bit, and a
coarse cell refined again spreads its mass evenly over its fine cells, so a skier coming back to
a run he tracked an hour ago finds it worn, not untouched. A **budget** caps the fine tiles
(a game's to set, 64 MB by default); past it the tile least recently near a focus is folded
first, in a deterministic order. The tiers' distances are a game's to set: a courtyard is all
near, a mountain mostly far.

## Drawn (`engine/render/snow*`, M29e–M29g)

- **The GPU mirror**: the cover as plan textures over the layer; the fine tiles uploaded when
  dirty into an atlas behind a page table over the camera's window (the cells are uploaded as
  they are stored, so the picture's surface is the physics' surface); the coarse layer as one
  texture over the whole region.
- **The snow light model** moves from `mountain/app/snow.wgsl` to an engine WGSL library any
  material includes: the micro-relief and grain, the wrap's blue terminator, the glare, the
  glitter on the reactive mask, the pressed floor smooth and glitterless, the trough darkened.
  The mountain keeps its corduroy and lanes as its own addition, and the `world` lab's views are
  held to the picture diff's floors while it moves.
- **The fine marks** (the trail maps of M8b generalised): sub-cell grooves from the imprint
  records, a coarse map and a fine window redrawn from history, so a ski's 7 cm groove reads as
  two grooves even though the simulation's cell is 10 cm.
- **Geometry**: near the camera a patch displaced by the deformation, its vertex density
  following how disturbed the snow is (untouched snow pays for no vertices), so a furrow and a
  berm have a silhouette; parallax in the mid tier; normals only beyond.
- **Snow on any ground**: where the ground is a game's terrain, the snow is the terrain's
  material and displacement; elsewhere (a road's shoulder, a brush world's yard, a roof) it is
  its own **sheet**, a heightfield over the layer's baked ground, drawn with the same
  displacement and material.
- **Snow on things**: meshes take a cover by their up-facing normal and exposure (a roof, a
  ledge, a parked car's bonnet), with the same light model.
- **What snow throws**: an imprint's ploughed mass emits spray through `render/particles`
  (the skier's spray becomes a caller); a blast throws a cloud.

## Sound (`engine/audio`, M29h)

Voices driven by the imprint records and the column: the crunch of a step or a tyre, its
brightness by density and its squeak in cold dry snow (below about −10 °C), the hiss of a
sliding base by speed and packed share, the thump of a body into powder.

## Snow in one call (M29i)

```cpp
snow::SnowWorld world;
auto& yard = snow::add_layer(world, snow::Region{x0, z0, x1, z1},
                             snow::ground_from(heightfield),        // or a callback tracing down
                             snow::preset("settled", /*depth*/ 0.3));
// each step, per contact:   snow::press(yard, imprint, t);
// any question:             snow::sample(world, x, z, t);
// each frame:               render::SnowPass reads the world; audio reads its imprint records
// save, join, replay:       snow::save(world) / snow::load(world, bytes)
```

`ge add snow` writes this into a game repo with a lab beside it; `ge snow` reads a saved layer
(`ge snow show <file.snow>`: plan maps of depth, density, drop and kind, and its tiles and
memory; `ge snow column <file.snow> --at=x,z`; `ge snow diff a.snow b.snow`), and the `snow`
consumer skill tells a downstream agent how to pick a preset, a contact and a budget.

## Determinism and the network (G1)

The cover, new snow and settling are pure functions of the layer's data and the time. The tiles
are stepped state in fixed-point integers, changed only by `press`, `groom`, `fold` and the
fill, in the game's order, so two runs of one seed leave the same snow and a replay skis its
own grooves; the arithmetic between is doubles with `core/libm`. A lockstep peer gets the same
snow for free; a server never sends snow, because a client derives it from the actors it is
sent (a shooter's footprints are cosmetic there), and a late joiner or a save game gets the
layer's snapshot (`save`/`load`, M25). Nothing the GPU adds is read back by the simulation.

**Fall Line's digests**: M29a–M29i add no reader to Fall Line, so nothing moves. M29j moves the
skier and the snowmobile onto `sample` with an untouched layer first (every digest holds, the
cover returning the level's own packed share bit for bit), and only then lets the free ride's
prints feed back (a track firmer to ski, a trench that holds): that moves the free ride's
digests on purpose, and the PR names each one; the races, which never ski a pressed layer, hold.

## Labs (G9)

| Lab | Kind | Row | What it shows |
| --- | --- | --- | --- |
| `snow-press` | scenario | M29a | Every imprint (a ski carving and skidding, a boot, a tyre, a tread, a body) on every kind: cross-sections before and after, the column under it, the berm, mass held to the bit, a groove ridden twice |
| `snow-field` | picture | M29a, M29d | A layer crossed by a skier, a sled, a car and a walker: plan maps of depth, density, drop and kind; the tiles allocated and their memory |
| `snow-contact` | scenario | M29b | Sink against load and width for every kind against the Bekker curves; compaction density; traction against slip; a ski planing up with speed |
| `snow-weather` | picture | M29c | Hours in minutes: a fall burying tracks, settling, a groomer's swath, a melt and a night's crust, a drift across a road; filmstrips and the columns over time |
| `snow-lod` | picture | M29d | The tiers round the focus actors; a tile folded and refined with its mass held; memory against time on a free-ride day |
| `snow-cost` | bench | M29d, M29e | `press` and `sample` per call, a step's snow with a field of skiers, folding; the GPU's upload and passes |
| `gpu-snow` | picture | M29e–M29g | Named views: grooves, ruts, a trench and footprints close up, displaced and not; snow as a sheet over a road and a yard; snow on a roof; the distance-band diff |
| `snow-audition` | sound | M29h | Every snow voice by kind and temperature, metered and written as WAV |
| `snow-wheels` | scenario | M29k | A wheel in a rut, out of it and across its wall; a worn road's lines; grip packed against loose |
| `free --snow` | drive | M29j | Fall Line: a free-ride day's piste skied up, tracks followed, a sled's trail |

## Sources

The numbers come from: Bekker's pressure–sinkage relation and the snow parameter tables in
Wong, Theory of Ground Vehicles (4th ed., 2008) and Terramechanics and Off-Road Vehicle
Engineering (2010); snow compaction under load (Abele and Gow, CRREL reports 1975–76;
Richmond, Shoop and Blaisdell, CRREL 1995, the compaction density and the motion resistance of
tyres in snow); Mohr–Coulomb shear and the Janosi–Hanamoto slip relation (1961); snow density
classes and settling (Fierz et al., The International Classification for Seasonal Snow on the
Ground, UNESCO 2009; the viscous compaction of a snowpack, Anderson 1976); ski–snow friction by
temperature (Colbeck, A Review of the Processes that Control Snow Friction, CRREL 1992); tyre
friction on snow and ice (winter road measurements, about 0.2 packed against 0.4 loose); wind
transport (the threshold friction velocity and the saltation flux, Pomeroy and Gray 1990);
the squeak of cold snow (snow acoustics, Johnson 1982).

## Not taken

- **A voxel or particle snow (MPM).** A material-point method makes beautiful snow in film and is
  the wrong tool for a game: its cost grows with the volume, it is hard to make bit-deterministic
  across threads, and a game's snow is a surface a metre deep, which a column per cell holds.
- **Deformation only on the GPU.** A render-target snow (the common game technique) cannot be
  read by the physics, differs between machines, and is lost when the camera leaves it. The
  picture's fine marks still live on the GPU, as detail on top of the simulation's surface,
  never instead of it.
- **One snow grid over the whole world.** A 10 cm grid over a 4 km mountain is 1.6 billion
  cells. Sparse tiles where something pressed, folded to a coarse layer when nobody is near.
- **Floats in the tiles.** Fixed-point fields are half the size, the same bits everywhere by
  construction, and upload to the GPU as they are.
