# Liquids: lava, slime and the rest

Water (`docs/water.md`, M24) is one liquid of many. A shooter's maps have pools of lava and
slime, and other games will want tar, oil, mud and stranger stuff. This page covers how the
engine generalises water into **liquid types** rather than forking it. A liquid is water's
`Stuff` with a `Liquid` added: its viscosity, its yield stress, its surface tension, what it does
to a swimmer and what it does to a body. Every system that reads a body of water reads those
numbers:

- The wind's waves are damped by the viscosity.
- The surface drifts only once the wind beats the yield.
- A swimmer strokes more slowly in a thicker liquid, floats on a denser one, and is held still
  by a strong yield.
- A body takes damage by the liquid's heat or its poison.

The look is a second, separate choice: lava drawn as a black body, slime as a turbid volume.

The roadmap is `docs/goals.md` § M30 (one status file a row, `docs/goals/M30<x>.md`). The skill is
`water`.

## What the games ask of it

- **A shooter's lava and slime** (M18, Shooter's Edge). These are pools a player falls into, swims
  through, and is hurt by on a clock. The damage per tick depends on how deep he is, and the
  classic numbers are a row a game sets (M30g). The pools must read at a glance: lava glowing,
  slime green and poisonous.
- **Any game's tunable liquids.** Viscosity, density and yield are knobs. They change how the
  wind moves the surface, how a player moves in the liquid and how he is hurt. All of it follows
  from the numbers, so a game tunes a liquid by its stuff, not by a dozen separate switches.

## Homes (G8)

Everything here is generic and lives in `engine/`. The named rows are data any game can pick.
Only a game's own tuning (Shooter's Edge's ticks, M30g) lives in its plugin.

| Subject | Home | Lab | Test |
| --- | --- | --- | --- |
| A liquid's numbers, the wind on it (keep, drift), the swim it gives, its ripples | `engine/water/liquid.hpp` | `liquids` | `tests/water_liquid_test.cpp` (golden) |
| Harm: ticks on a clock, burns by thermal dose, radiant heat | `engine/water/harm.hpp` | `liquids` | `tests/water_harm_test.cpp` (golden) |
| A black body's glow: luminance and colour by temperature | `engine/water/glow.hpp` | `gpu-lava` | `tests/water_glow_test.cpp` |
| The named liquids (water, sea, two lavas, five slimes) | `engine/water/liquids.hpp` | `liquids` | `tests/water_liquids_test.cpp` |
| The body's liquid in the query, waves damped, drift, `lay_waves` for a volume | `engine/water/world.hpp` | `liquids` | `tests/water_world_test.cpp` (golden) |
| The swimmer in a liquid (both regimes) | `engine/water/swim.hpp` | `swim`, `liquids` | `tests/water_liquids_test.cpp` |
| Lava and slime drawn, the looks, the eye's key | `engine/render/liquid.hpp`, `liquid.wgsl` | `gpu-lava`, `gpu-slime` | `tests/render_liquid_test.cpp` |

## A liquid (`engine/water/liquid.hpp`, M30a)

```cpp
struct Liquid {
  double viscosity = 0;    // Pa s (0: thin, the viscous terms dropped, water's bits kept)
  double yield = 0;        // Pa, the Bingham yield stress
  double tension = 0.072;  // N/m
  SwimScale swim;          // the kinematic regime's scales (swim_for derives them)
  Harm harm;               // what it does to a body
};
```

The `Stuff` (`world.hpp`) carries it next to the density, the temperature and the look. A
**Bingham liquid** is the simplest model with a yield: below its yield stress it does not flow
at all, and above it it flows with its viscosity. That covers lava's crust and toothpaste-like
gels, which a pure viscosity cannot.

**Viscosity 0 means "thin"**: the viscous terms are skipped, not computed with a tiny number. So
water's goldens (waves, the query, the swim) keep their bits, and a game that never names a
liquid pays nothing for this.

### The wind on a liquid

The wind feeds a wave of wavenumber k and angular frequency ω at a growth rate (Plant 1982):

    β = 0.04 (u*/c)² ω · (ρ_water / ρ)    u* = √Cd · U,  Cd = 1.3e-3,  c = ω/k

The liquid's viscosity drains the wave's energy at `4 ν k²` (Lamb), with ν = μ/ρ. A wave keeps
the share `1 − loss/β` of what the wind would give it, clamped to 0..1 (`wind_keep`). Short waves
die first. Water keeps everything a picture shows. A runny mucus (0.05 Pa s) keeps its ripples
past half a metre. The toxic slime (2 Pa s) keeps only swells longer than its pit. Lava keeps
nothing. `damp_waves` applies the share to a body's laid components when it is made; `ripples`
lays a short band of capillary-gravity waves (ω² = gk + (σ/ρ)k³) for the render.

Experiments on wind over viscous liquids (Paquier, Moisy and Rabaud 2015, 2016) show the wind
speed needed to raise waves rising with the viscosity. Below that speed the surface only shows
slow, wrinkled undulations. This growth-against-loss model keeps that trend in a closed form a
game can afford.

**Drift.** The wind's stress is τ = ρ_air Cd U². Water drifts at 3 % of the 10 m wind. A viscous
liquid drifts as a sheared layer of depth h, at `(τ − τy) h / μ` capped at that 3 %, and not at
all while τ ≤ τy (`drift`). A 10 m/s breeze is 0.16 Pa: the toxic slime's 0.2 Pa yield holds it
still, while a 22 m/s gale moves it slowly. Lava and the gel never move.

### Swimming in a liquid

Both regimes of `swim.hpp` read the liquid at the swimmer's waist:

- **The kinematic regime** (a first-person game's) scales its wish speed, friction and idle sink
  by the liquid's `SwimScale`. `swim_for` derives the scales from a default swimmer's stroke:
  90 N of thrust against a quadratic drag (A = 0.12 m², Cd = 1) plus the Stokes drag 6πμr, and
  the yield's grip (τy over 1.8 m² of skin) taken off the thrust.
  - The speed share is that stroke's speed in the liquid over its speed in water.
  - Friction scales by its inverse.
  - The sink scales by the body's buoyancy, (1013 − ρ)/13, clamped to ±1. A negative sink means
    he rises: a swimmer floats high on lava and on the denser slimes.
- **The body regime** adds an implicit Stokes drag on the body's velocity, plus the yield's grip:
  a velocity the yield's force can stop within the step is stopped. A body in tar stays where it
  fell.

A game can overwrite `swim` after `liquid()` gives it the row, when its own feel matters more
than the numbers.

### Harm (`engine/water/harm.hpp`)

- **Ticks** (`HarmKind::chemical`): `tick[level − 1]` health every `interval` s, at immersion
  levels 1 (feet), 2 (waist) and 3 (all in). The clock keeps running out of the liquid, so leaving
  and re-entering does not reset it. This is the classic shooter model, and a game sets its own
  numbers.
- **Burns** (`HarmKind::heat`): the skin is wetted on a share of the body by level (0.15, 0.5,
  1). The heat flux into it is `h (T − 35 °C)`, plus any `outside` flux. Damage follows the
  **thermal dose** D = Σ q^(4/3) dt, with q in kW/m² (Eisenberg's probit for thermal radiation).
  The doses that mark the grades are:

  | Grade | Dose |
  | --- | --- |
  | Pain | 92 |
  | First-degree burn | 105 |
  | Second-degree burn | 290 |
  | Third-degree burn | 1000 |
  | Lethal (half of those exposed) | 2376 |

  The lethal dose is one life (100 health), so `per_dose` = 100/2376. Lava kills a body all in
  within a second, and a body feet first in a few seconds.
- **Radiant heat** (`radiant_flux`): a hot disc of radius R at temperature T heats a face h above
  its middle by εσT⁴ · R²/(R² + h²). Over open melt at 1150 °C that is about 200 kW/m², pain in a
  tenth of a second. Over a 700 °C crust it is under a quarter of that. A game decides whether to
  pass it in as `outside`, since it is lethal at a distance a game may not want.

## The named liquids (`engine/water/liquids.hpp`)

| Row | ρ kg/m³ | μ Pa s | τy Pa | °C | Harm | What it is |
| --- | --- | --- | --- | --- | --- | --- |
| `water` | 1000 | 0.00114 | 0 | 15 | none | fresh water |
| `sea` | 1025 | 0.00122 | 0 | 15 | none | sea water |
| `lava` | 2600 | 400 | 100 | 1150 | heat | molten basalt, a pāhoehoe flow's melt |
| `lava-crusted` | 2450 | 20000 | 2000 | 1100 | heat | basalt near its solidus under a thick crust |
| `slime` | 1150 | 2 | 0.2 | 25 | 4, 8, 12 a s | the toxic green slime |
| `slime-gel` | 1030 | 30 | 20 | 15 | 2, 4, 6 a s | a clear jelly |
| `slime-mucus` | 1010 | 0.05 | 0.01 | 20 | none | a pale runny mucus, like egg white |
| `slime-sludge` | 1250 | 5 | 3 | 20 | 3, 6, 9 a s | an olive sludge under scum |
| `slime-tar` | 1100 | 3000 | 50 | 30 | none | a black tar |

Basaltic lava's viscosity runs from 10² to 10⁴ Pa s depending on its temperature and crystals,
with yield stresses from tens to thousands of Pa. Its bulk density is 2.4–2.8 t/m³. The two rows
sit inside those bands. The slimes' numbers are ordinary non-Newtonian fluids (gels, mucus,
sludges, bitumen) at room temperature. `liquid(name)` returns a row's `Stuff`; an unknown name
logs a warning naming the rows and gives water.

## Drawn (`engine/render/liquid.hpp`, M30b–M30c)

A liquid body is a grid of 96 × 96 quads over its box at its level, drawn after the opaque scene
over a copy of the scene's colour and depth. The pass reads the copy, so the pool is seen through
and feathered at its shore. It does not touch the water's passes. The look is a `LiquidLook`
picked by name (`find_liquid_look`): `lava` and `lava-crusted`, and the five slimes.

### Lava

The surface's temperature is worked out per pixel, and its light is read from a 32-entry **glow
table** (600–1700 K). The table is computed on the CPU by `water/glow.hpp`: Planck's law weighed
by the CIE colour-matching functions (Wyman, Sloan and Shirley's multi-lobe fit), giving cd/m²
and linear sRGB. 1150 °C is about 2900 cd/m² of deep orange at ε = 0.9.

- **The crust** is a warped Voronoi tiling of plates with sub-cracks. Each plate has an age
  (hashed, running on at `age_rate`) that cools its skin from `crust_hot` to `crust_cold` and
  widens its cracks. The melt's heat shows in a crack and falls off as exp(−d/w) from it.
- **Open melt** shows where a slow noise passes `cover`. It carries swirls, threads and hot
  streaks.
- **Relief**: plates stand over cracks, and ropy folds (pāhoehoe) run across the flow, with
  knobs. That relief gives the normal.
- **Shading**: the crust and the melt are glasses of their roughness (GGX) under the sun and the
  sky, and the glow is added on top.
- **The flow** carries everything on a two-phase flow map. Each phase's pattern is drawn back
  along the flow for its share of the period and the two are crossfaded, so the pattern never
  stretches.
- **Filtering**: a crack narrower than the pixel's footprint is widened to it, with its heat
  spread over the width, so distant crust does not sparkle.

**Exposure.** Lava is thousands of cd/m² in a night of 0.001. The frame has to adapt to the
scene's emitters, not only to the sky. `FrameView::adapt_cd` is the luminance the eye adapts to
from the scene's own lights. It is passed to the frame's white point and to the night's
pre-exposure (keeping fp16 from overflowing), and `adapt_for(look, key)` returns it for a look.
The neutral tone curve whitens a saturated red's shoulder, so a view of lava keys it at 1.5×
the melt. That is how a photograph exposed for the lava holds it orange.

### Slime

Each slime is a turbid volume:

- **Its own colour** is the Kubelka–Munk reflectance of a thick layer, R = 1 + K/S − √((K/S)² +
  2K/S), from its absorption K and scattering S per band.
- **What lies behind it** is seen through exp(−(K + S)d), where d is the distance through the
  liquid read off the scene's depth. So the gel shows the stones on its bed, greener the deeper
  they lie, while the toxic slime is opaque.
- **Marbling**: the stuff's strength varies in slow swirls carried on the flow.
- **Ripples** come from `water::ripples`, cut by its viscosity.
- **Bubbles** swell in cells, at the look's rate and size. Each is a dome lighter in its middle
  and darker at its rim, with a sharp highlight, and bursts into a ring that runs out and dies.
- **Scum** lies in matte patches that drift with the flow.
- **An oily film** lies in patches. Its thin-film interference (n = 1.45, a few hundred nm)
  tints the reflection.
- **Glow**: the slime can carry its own light, which pulses slowly and is brighter in the
  bubbles.

| Look | Reads as |
| --- | --- |
| `slime` | an opaque toxic green, bubbling, glowing faintly (60 cd/m²) |
| `slime-gel` | clear green-cyan glass; the bed shows through |
| `slime-mucus` | pale milky yellow-green, glossy, small bubbles |
| `slime-sludge` | olive-brown under grey-brown scum, an oily sheen between, bubbling |
| `slime-tar` | black glass with an oil's sheen in patches, a rare slow bubble |

## Labs (G9)

| Lab | Kind | Row | What it shows |
| --- | --- | --- | --- |
| `liquids` | picture | M30a | The share of a gale's input each liquid keeps by wavelength, the drift, the swim each gives (stroke speed, the scales, metres in 10 s), the damage over time at each immersion (lava's burns, the slimes' ticks), the radiant heat over a lava pool by height |
| `gpu-lava` | picture | M30b | A lava pool in a quarry by day, at dusk and at night; the crust's cover from 15 to 97 %; the plates close up by day and night; the flow at four moments; both lavas; the glow table |
| `gpu-slime` | picture | M30c | The five slimes from the rim and close up; the toxic slime glowing by day, at dusk and at night; the gel seen into; one gale over three slimes |

## Sources

The numbers above come from:

- the wind's growth rate (Plant 1982) and viscous damping (Lamb, Hydrodynamics);
- wind waves on viscous liquids (Paquier, Moisy and Rabaud 2015, 2016);
- the Bingham model;
- basaltic lava's rheology and densities (Shaw 1969; Pinkerton and Sparks 1978; the
  field measurements of active pāhoehoe flows) and its surface temperatures;
- the thermal dose and its probits (Eisenberg et al. 1975; the burn thresholds compiled in
  O'Sullivan and Jagger 2004);
- Planck's law and the analytic CIE fit (Wyman, Sloan and Shirley 2013);
- Kubelka–Munk's two-flux theory;
- thin-film interference (any optics text).

## Not taken

- **A full non-Newtonian flow solver.** A pool is drawn and swum in, not poured. Lava that flows
  down a slope, cools and stops is M30d, on the flow grid. It is not a second solver.
- **Simulating the crust.** The plates and cracks are drawn, not stepped. A game cannot break the
  crust yet, and a hole punched in it is a later row.
- **Texture art.** Every look is procedural, so a game tunes it with numbers and nothing is
  painted.

## Not built yet

These are open rows in `docs/goals.md` § M30:

- viscous flow and lava that cools (M30d);
- heat shimmer, the glow lighting the walls, and reflections in the slime (M30e);
- the sound (M30f);
- Shooter's Edge's own liquids (M30g).
