# Labs

A lab runs the real code on one question and writes back what it found: numbers, tables
and captioned pictures. Labs are how this engine is tuned and checked. An agent measures
and LOOKS instead of guessing, and a PR shows its evidence as a lab's output before and
after the change. Labs are built once, into the engine, so that every engine subsystem,
every plugin and every game plugs into one contract.

Two rules follow from it:

1. **Every subsystem owes labs.** A module is not done when its tests pass. It is
   done when an agent can run a lab that shows what it does. Each milestone row in
   [`goals.md`](goals.md) names the labs its exit needs (the **Labs** column).
2. **A change in a lab's subject runs that lab before and after.** Keep both outputs
   (`--tag=before`, then `--compare=before`) and put them in the PR.

The procedures (how to write a lab, how to read one, how to caption a picture) are the
`labs` skill's. This page is the design.

## The contract

A lab is a `LabSpec` and a `run` function (`engine/lab/lab.hpp`):

| Field | What |
| --- | --- |
| `id` | The name on the command line (`make lab LAB=level`): the subject's noun, kebab-case. |
| `kind` | `sim`, `scenario`, `picture`, `drive` or `bench` (below). |
| `owner` | `engine`, `plugin:<name>` or `game:<name>`. Whose subject it is. |
| `subject` | The files or the system it measures. |
| `skill` | The skill that says how to read its output. |
| `question` | The one question it answers, as a sentence. It heads every report. |
| `flags` | Its own flags with defaults and help. Anything else typed is an error. |
| `smoke` | The flags CI runs it with. Seconds, not minutes. |

`run(Lab&)` reads its flags (`arg`, `arg_num`, `arg_nums`, `arg_bool`) and reports through
five sinks: `metric` (a keyed number with a unit), `table`, `note`, `shot` (a picture and
its words) and `fail` (what failed and the next action, in the
[diagnostics](architecture.md#diagnostics-agent-actionable-output) shape).

### The kinds

| Kind | What it does | Today |
| --- | --- | --- |
| `sim` | Headless runs of the real code over seeds or a sweep. Numbers and tables. | `analyze`, `run-clock` |
| `scenario` | One staged situation, such as a skier placed at a moment on a synthetic slope with a scripted input, measured exactly. | `ride` |
| `picture` | A captioned picture of a subject. It is drawn on the CPU raster now, and from M7 it is a GPU frame read back. | `level`, `noise` |
| `drive` | A pilot plays the game, either the bot or an agent over stdio. The run is the report, and the control tape is the replay. | `sim`, `play` |
| `bench` | What something costs. Its wall-clock metrics are `measured`, which means they are printed and compared but never put in the digest. A game's step needs no lab of its own: `ge bench`, `ge profile` and `ge mem` time the pilot's run through the play contract, zone by zone (`engine/play/bench.hpp`). | `mapgen-cost` |

### Where labs live, and how they register

A lab is a file in a `labs/` directory beside its subject:

```
engine/lab/            the framework: contract, runner, report, raster, PNG
engine/lab/labs/       the engine's own labs (<dir>_<name>_lab.cpp: core_noise_lab.cpp)
plugins/<name>/labs/   a plugin's labs, owner "plugin:<name>" (plugins/mountain/labs/)
games/<name>/labs/     a game's labs, owner "game:<name>"
```

Each lab file ends with `GE_LAB(kSpec);`, which registers the lab from a static
initializer. CMake globs every `.cpp` in those `labs/` directories into ONE executable,
`labs`. Nothing is registered by hand, and a plugin or a game adds a lab by adding a file.
Labs are compiled into the executable and never into a static library, because the linker
drops unreferenced objects from a library and the registration with them. A helper that
several of one module's labs share sits in the same directory (`plugins/mountain/labs/level_map.hpp`).

A lab may include anything (it is tooling), and nothing includes a lab.

## Where labs run

**The primary development environment is a Linux container with no GPU**, and every lab
must run and be verifiable there: the CPU raster today, and from M6 GPU pictures through a
software adapter (a CPU Vulkan driver under Dawn), so a screenshot lab works on
CI and in an agent's container alike. A lab that renders reads its frame back to a
`Picture`, and the agent opens the PNG and the contact sheet. No lab may need a window, a
display server or a person: the one lab that measures a window (`frame-pace`) runs under a
virtual display (`xvfb-run -a`) in the container, and without a display it notes that and
passes, with the same digest.

Some things can only be measured on the secondary machine, an Apple-silicon Mac: real GPU
timings, Metal-specific behaviour and the feel of the frame pacing on a real display. A lab
that needs it says so in its `question`, keeps a smoke mode that still runs on Linux (at the
least its CPU half), and its hardware runs follow `AGENTS.md`'s rule for work that needs the
Mac. That rule applies only when Linux cannot answer the question, never because the Mac
would answer it faster.

## Running labs

```sh
make labs                                     # the list: id, kind, owner, skill, question
make lab LAB=level ARGS="--seeds=38,7"        # one lab; ARGS are its flags and the runner's
make lab LAB=level ARGS=--help                # its flags, with defaults
make lab LAB=level ARGS=--tag=before          # keep this run as previews/level@before/
make lab LAB=level ARGS=--compare=before      # every metric beside the kept run's, every picture diffed
make lab LAB=world ARGS=--diff=before         # run nothing: the last run's pictures against the kept run's
make labs-smoke                               # every lab at its smoke flags, twice, digests checked
make labs-digests                             # rewrite tests/labs_digests.txt (a reviewed change)
make ge ARGS="digest level --write"           # rewrite one lab's row only; no --write: just check it
build/debug/labs --json                       # the list as one JSON array (what `ge lab` reads)
make lab LAB=trace ARGS="--log=course=debug"  # any lab: the log spec's lines in previews/<id>/log.txt
```

`--log=<spec>` switches on log channels for the run (`docs/debugging.md`): every line kept goes to
`previews/<id>/log.txt` as `[step] level channel: message`, the debug and trace lines only
there, and a note in the report counts them by channel and level. The log is `measured`: it
never moves a digest.

In a game repo, `ge lab` is the way in: `ge lab` lists, `ge lab <id> --tag=before` and
`--compare=before` run one, and with `--json` the lab's whole `report.json` is the answer's
`result`. `ge lab diff <id>` diffs the pictures (below), every band under its floor a finding.

### The sweep: one constant or one flag at several values

`ge lab sweep <id> --set=<name>=<a>,<b>,... [--in=<file>] [--metrics=<word,...>] -- <lab flags>`
runs a lab with one constant of the code at each value, every metric side by side
(`engine/cli/sweep.hpp`). The constant is a `constexpr` defined on one line (a spec's or a
tuning table's, `inline constexpr double kFlight = 0.6;`), named bare (`kFlight`) or qualified
(`launch::kFlight`, which keeps only files that open those namespaces); `--in` names its file
when the name is in several. For each value the initializer is rewritten in place, only the
`labs` target is rebuilt (the files that include it recompile) and the lab runs with
`--tag=sweep-<name>-<value>`, so every run's pictures stay for `ge lab diff`. The file is put
back after the last run and the labs rebuilt on what it holds; a sweep killed half way leaves
its text under `build/<preset>/ge-sweep/backup.txt`, and the next sweep puts it back first.
Three values of a paramotor constant over one scenario take about 20 s in debug.

```sh
ge lab sweep para --set=launch::kFlight=0.4,0.6,0.9 --metrics=launch -- --scenario=launch
```

`--flag=<flag>=<a>,<b>,...` sweeps one of the lab's own flags instead of a constant: the labs are
built once and the lab runs with `--<flag>=<value>` at each value, tagged
`sweep-<flag>-<value>`, the same table side by side. Nothing is rewritten, so it is the way to
take a picture lab over the hours of a day or a sim over its step rate. `--set` and `--flag`
together are refused: one thing at a time.

```sh
ge lab sweep para --flag=hz=60,120 --metrics=launch -- --scenario=launch
```

### The picture diff: did the look move, and how near the player?

`--compare=<tag>` diffs every picture of the run against the same picture of the kept run, and
`--diff=<tag>` does it for the last run without running anything (`engine/lab/diff.hpp`). Each
shot is cropped out of its caption band and measured over the whole picture and, when the lab
handed a **distance map** with the shot (`Lab::shot`'s last argument; a GPU lab reads it off the
frame's depth with `render::read_distance`), over every **distance band**: 0-25 m, 25-100 m,
100-400 m, 400 m+ and the sky by default (`--diff-bands=25,100,400`). For each it gives the
PSNR, the SSIM of the luma, the mean and largest channel change and the share of pixels changed
by more than 8 levels, and over the whole picture a near-weighted mean (each pixel weighted by
1 / (1 + distance / 25 m)).

Each band is held to a **floor**, its PSNR in dB: 40, 36, 32 and 28 farther out, 28 for the sky
(`--diff-psnr=40,36,32,28,28`), so a change on the far ridge may be coarser than one at the
skis before it fails. A band under 64 pixels is reported and not judged; a picture with no
distance map is held to the nearest floor. `--diff` exits 1 when a band is under its floor;
`--compare` only prints it. Under `previews/<id>/diff/` it writes `diff.json` and a sheet a shot:

| Panel | Shows |
| --- | --- |
| BEFORE, AFTER | the two pictures |
| CHANGE | the after dimmed, every pixel changed by more than 8 levels coloured blue to yellow to red by how much, the bands' edges as contour lines |
| DISTANCE BANDS | each band in its own colour over the picture, its edges drawn and its name (in metres) at its middle |

with every band's numbers and verdict in the caption band under them. Two runs of the same code
on the same machine give the same bits (PSNR 100 everywhere), so any number under 100 is the
change. The `world` lab hands a distance map with every view; a GPU lab whose pictures guard a
look should do the same.

The binary is `build/<preset>/labs` (`PRESET=release` for anything timed). An unknown flag
exits 2, a failed lab exits 1, and a clean run exits 0, because a measurement that ignores
a mistyped flag reports a confident wrong number.

## What a lab writes

The terminal gets the report: the question, the inputs in force, the metrics, the tables, the
notes, where the pictures went, any failures and the digest. Under `previews/<id>/` (or
`previews/<id>@<tag>/`, gitignored) the next agent and the PR get files:

| File | For |
| --- | --- |
| `report.json` | Everything above, machine-readable. |
| `metrics.tsv` | `key value unit det`, one metric a line. `--compare` reads it back. |
| `index.md` | **The contact sheet**: every picture with its title, caption, what to look for and its inputs. An agent reads this first, then opens the pictures it needs. |
| `<shot>.png` | The picture with its **caption band** burned in under it. |
| `<shot>.json` | The shot's words beside the picture, with the picture's own size (the band left out). |
| `<shot>.distance` | How far away each pixel is, when the lab handed a distance map: `GEDIST1\n`, the width and height as little-endian u32, then the metres as f32 (infinity for the sky). Never in the digest. |
| `diff/` | With `--compare` or `--diff`: a sheet a shot and `diff.json` (above). |
| any attached file | What `lab.attach(name, bytes)` wrote, listed at the foot of the contact sheet: a rendered sound's `<shot>.wav`, for a reader with speakers to hear exactly what was metered. Its bytes are in the digest unless it is `measured`. |

### Sounds

A lab shows a rendered sound with `ge::lab::sound_shot(lab, shot, stereo)` (`engine/lab/sound.hpp`):
it meters the buffer (`engine/audio/meter.hpp`), writes `<id>.loudest-db`, `.peak-db`,
`.centroid-hz`, `.onset-s` and `.length-s`, draws one picture (the waveform zoomed to its peak,
the 10 ms level envelope from -80 to 0 dBFS, the ten octave bands) and attaches the WAV. A session
that cannot listen reads the sound by those numbers and that picture. The synth renders the same
bytes on every platform, so a sound's metrics and its WAV are in the digest like a raster's pixels.

### Captions

A picture without words is a picture nobody can review. So every `shot` carries a `Shot`:

- `title`: one line ("SEED 38 FROM ABOVE");
- `caption`: what the picture shows, including the legend (which colour is what, which way
  is up);
- `look_for`: what a reviewer checks it for ("gates alternate red and blue down the fall
  line"). This is the picture's acceptance test, written down;
- `inputs`: the seed, the version and the flags it was made under.

The words are burned into a band under the image, so a picture pasted alone into a PR still
says what it is. They are also in the sidecar and the contact sheet. The words are written
for a reader who sees only that picture.

## Digests: every lab is a determinism test

A lab's **digest** is an FNV-1a 64 over what it wrote, in order: each deterministic metric's
key and bits, every table cell, and every picture's id and pixels (the caption band
included). `measured` metrics are left out, and so are the pixels of a `measured` picture
(`lab.shot(shot, picture, Det::measured)`): a GPU frame is rasterised by each adapter in
its own way, so its id is hashed and its pixels are not, and its sidecar and contact sheet
say so. A lab that holds a GPU frame to an image does it itself, on the pinned adapter, and
fails when it is off. Three checks use it:

- **`--repeat=N`** runs the lab N times in one process and fails when the digests differ.
  That catches state kept between runs and unseeded draws.
- **`--check-digests=tests/labs_digests.txt`** compares each lab's smoke-run digest with
  the committed baseline. CTest's `labs_smoke_1`..`_8` (one slice of the labs each,
  `--shard=I/N`) run it on every CI row, so a lab whose
  output differs between compilers fails on the row that differs.
- A lab that has to move (its drawing changed, or the code it runs changed on purpose)
  rewrites its row of the baseline with `ge digest <id> --write` (`make ge ARGS=...` here; the
  runner's `--write-digests` on one lab or a shard keeps the other rows), and the PR says why.
  A game repo keeps the same baseline once `ge digest --write` writes it, and its
  `labs_smoke` case checks it from then on.
- The baseline merges with git's union driver (`.gitattributes`): two branches that add or move
  different labs' rows merge cleanly. Two branches that move the same lab's row leave two rows
  for it, and `--check-digests` fails naming the lab until `ge digest <id> --write` keeps one.
- After a change, `ge digest --diff` runs every lab at its smoke flags and prints only the labs
  whose digest is off the baseline (or not in it), writing nothing: the list of rows the PR has
  to explain or fix, in one command (about 14 minutes for every lab in the debug build; name a
  lab to run only it).

That is why lab code follows the simulation's rules even though it is tooling: no `<cmath>`
transcendental (`make libm-check` scans `engine/lab`; use `core/libm`), integer pixels from
`num::round`, and nothing random but `core::Rng`.

## Pictures before and after the renderer

**Today** a picture is drawn on the CPU raster (`engine/lab/raster.hpp`: boxes, lines,
discs and a 5x7 font) and written by the PNG
writer (`engine/lab/png.hpp`). That is enough for maps from above, plots, profiles and
contact sheets, and it runs anywhere, CI included.

**From M6/M7** the renderer adds a capture path, and the API above stays the same:

```cpp
// render (M7): draw a named view of a GameState offscreen and read it back.
ge::lab::Picture capture(const FrameView& view, const CaptureSpec& spec);   // planned
lab.shot({"chase-12", "CHASE AT 12 S", "...", "...", inputs}, capture(view, spec));
```

- `capture` renders into an offscreen texture on a headless device (the GPU smoke test M6
  sets up), reads it back to RGBA8 and returns a `Picture`. It gets the same caption band,
  sidecar and contact sheet.
- **Named views** (`spawn`, `powder`, `lookback`, `track`, `jump`, `landing`, `vista`,
  `chase-<s>`, `orbit`) are a table in the game's labs. A view
  is a camera rig and a moment, so the same view is the same picture on every run.
- **Side by side with a reference.** A picture lab can put its frame beside a reference
  picture of the same view (`--reference=DIR`, built with M7), with both captions. The pair
  is M7's and M8's exit evidence.
- GPU pictures are not bit-identical across drivers, so a GPU shot is hashed only when
  the lab says it is stable (a software adapter on CI). Otherwise its pixels are left out
  of the digest, while its metrics still count.

## Scenario labs

A scenario stages ONE exact situation instead of skiing into it: a hand-built map (a
synthetic slope with a kicker, rollers and a lone tree, or a flat drag strip of packed snow
or powder: `plugins/skier/synthetic.hpp`), a moment to stand the skier at (`stand_skier`:
position, heading, speed, height over the snow), a scripted input as a function of run time and
state, a length in seconds, and the numbers that say how it went (`lab.metric`).

A scenario lab is a table of these (one `LabSpec`, with `--scenario=<id>` choosing a row), so
`make lab LAB=ride ARGS=--scenario=kicker` stages the kicker taken at a known speed. The same
rows (`plugins/skier/scenarios.cpp`) back the physics tests
(`tests/skier_scenarios_test.cpp`), so a scenario is written once and is both a test and a lab.

**Step by step, `--trace`.** Every scenario lab (`ride`, `sled`, `heli`, `para`) takes `--trace`:
each scenario it runs also writes its every step as a series file, `<id>.trace.tsv` (the state,
the controls and the events: `ge graph` reads it, and two runs' files chart on one graph), and
draws it as captioned charts (`<id>.<chart>.png` and `.svg`: the height, the speeds, the angles,
the controls), its events marked as grey lines and its flights shaded, spikes ringed. The
runner hands each step to the lab through an optional callback (`run_para_scenario(sc, hz,
each)`), so the plugin needs nothing of the lab kit; the lab writes and draws through
`engine/lab/series_charts.hpp`, the same helper the `trace` lab draws a seed's run with. It is
the scenario's before and after at the scale of a step: `--trace --tag=before`, change the code,
`--trace --compare=before`, then `ge graph` both files.

Under `--trace` each scenario also reports its **teleports** (`engine/lab/teleport.hpp`): every
step where the actor moved farther than its speed carries it over the step (with a slack of 1.5
and 5 cm), as `<id>.teleports` and a note a jump naming its step, its time and the two distances.
A lab that follows anything that moves gets the same check from `find_teleports` and
`report_teleports`, which can fail the lab when its subject must never jump.

```sh
make lab LAB=para ARGS="--scenario=launch --trace"   # previews/para/launch.trace.tsv and its charts
```

## Drive labs: the bot, the rivals and agents at the controls (M5)

A **pilot** is anything that turns what a skier can see into a `SkierInput`, one step at a
time. The engine has one loop for every pilot:

```
state = create_game(opts)
for each step:  input = pilot(state)  →  tape.record(input)  →  step(state, input)
report: the RunReport (finish, time, gates, crashes, digest) + the control tape
```

- **The bot** (`games/fall_line/sim/bot.hpp`) is a pilot. The `sim` drive lab (`make sim`)
  rides it over seeds and prints the run report table (finish, time, gates, crashes, digest)
  with its line drawn on each map; `simulate.hpp` is the one loop every pilot drives, and
  `tests/fall_line_sim_test.cpp` holds its digests golden.
- **Rivals** are bots with profiles (`Rider`: a pace off a stream of the map's seed, a lane off
  the grid slot), on the same grid, through the same `step`; `make sim ARGS=--rivals=3` prints
  the field table (`games/fall_line/sim/field.hpp`).
- **Every drive is recorded on the control tape** (`plugins/racing/tape.hpp`, the format a
  ghost and a replay use). A drive's seed, options and tape reproduce it exactly, so any
  run (a bot's, an agent's, a player's) becomes a repro or a scenario.
- **An agent at the controls**: every game is played the same way, through the engine's
  play contract (`engine/play/game.hpp`). A game names its axes (the whole input: Fall Line's
  `steer`, `tuck`, `brake`, `jump`, `reset`) and options (`rivals`, `version`) and opens a
  `Run` (step, over, digest, observe, state, report, pilot). The engine's session
  (`engine/play/session.hpp`) snaps every input onto its axis's grid, records it on the INPUT
  LOG (`engine/play/inputs.hpp`: plain text, one line per run of held steps) and steps the run,
  so a run played by an agent, by the game's pilot or from a log is written down the same way
  and replays to the same digest. The game's `play` executable (`build/<preset>/play`) carries
  it, and `ge` drives it:

  | Command | What |
  | --- | --- |
  | `ge play --seed=38 -- --rivals=3` | the line protocol on the terminal (below) |
  | `ge sim --seeds=38,7` | the game's pilot over each seed: report, digest, `runs/<game>-<seed>.inputs` |
  | `ge sim --human` | the same, with how human each run looks (`engine/play/human.hpp`): each axis's saturation, jumps, reversals, hold and lag, each decision's changes of mind, a warning for every one outside a person's band (skill: bots) |
  | `ge replay <log>` | the log played back; a digest it misses is a determinism break |
  | `ge state <log> --step=N` | everything that decides the next step, after N steps |
  | `ge diverge <a> <b>` | the first input and the first step two runs part, and the fields that differ |
  | `ge why <log> --step=N [--around=K] [--flips] [--timeline]` | why a bot did it: the log replayed with the decision trace on (`engine/play/why.hpp`), agent N's decisions round the step (what it saw, the options it weighed with their scores and terms, the choice and its margin); on a log a person or an agent played, what the pilot would have done instead and the first step it would have played otherwise; `--flips` every change of mind; `--timeline` a series file for `ge graph` (skill: bots) |
  | `ge shot <log> --step=N --view=V` | a still of the run at step N: the game's view drawn on the CPU raster, framed with its caption, the seed, the options, the step and the digest (`runs/<game>-<seed>-<view>-<step>.png`); without a log, the pilot's run of `--seed`. Fall Line's views: `near` (the piste 160 m round the player, the field on it) and `map` (the whole map) |
| `ge shot --gpu [--view=V] [--mode=M] [--size=WxH]` | the same still drawn by the game's GPU renderer (a play executable built with the GPU registers one: `RendererSpec`, `GE_RENDERER`), in one of its modes. Fall Line's views: `chase` (the default), `high`, and the named views `start`, `piste`, `lookback`, `vista`, `aerial`; its modes: `lit` (the game's picture), `wireframe` (the clipmap's tiles banded by distance, the trees and figures orange, hidden lines hidden), `depth` (near white, 4 km black on a log scale) and `overlay` (the lit picture with the piste's line, the gates where the ground shows them and each racer's velocity, number and speed) |
| `ge record --gpu [--view=V] [--mode=M]` | the run recorded through the GPU renderer: one renderer for the whole recording, the first frame a cut and every frame after drawn on from the last, so the tracks, the spray and TAA's history carry; the files are named by the view and the mode (`runs/<game>-<seed>-chase-overlay.mp4`) |

  The protocol is stated once in `engine/play/session.hpp`. The session opens with `hello`
  (the game, the seed and options, the step rate, the axes, the ops); then:

  | Agent sends | Engine answers |
  | --- | --- |
  | `{"op":"observe"}` | the step and the clock, and the game's observation. Fall Line's: where he is, his heading, speed and edge, on the snow or in the air, down or not; the gate owed, its bearing off his heading (+ right) and its distance; where he stands on the piste (`piste_lateral`, `piste_width`, `piste_heading`, `piste_bend` over the next 30 m); finished and the finish time; his place in the field; his events since the last answer |
  | `{"op":"act","input":{"<axis>":v,...},"steps":n}` | the observation after n steps (1 to 1200) with that input held; an absent axis is 0, a `press` axis (`reset`) is taken on the first step |
  | `{"op":"pilot","steps":n}` | the game's own pilot plays n steps, then the observation: an agent hands over and takes back |
  | `{"op":"why"}` | what the pilot would do now and why, without stepping: the input it would send and its decision records (`engine/ai/why.hpp`), so an agent can ask before it hands over |
  | `{"op":"state"}` | the game's whole state now, every field to the bit |
  | `{"op":"inputs"}` | the input log so far, as its text: the run as a replay |
  | `{"op":"end"}` | the run's report, steps and digest; the session ends |

  A line the session cannot use answers `{"op":"error","error":...,"next":...}`: what was wrong
  and what to send instead. An agent that sends the bot's moves rides the bot's run to the bit,
  the pilot's run IS the bot's, and an agent's log played back is its run again
  (`tests/fall_line_play_test.cpp`). The `play` lab runs the same session: `--stdio` for the
  protocol, `--script=<file>` to play a file's lines, and with neither the bot plays through
  the protocol's text and its log is replayed to the digest. Two logs one input apart
  `ge diverge` at exactly that step, naming the fields that differ (the skier's yaw rate first).
  The protocol carries only what a player could know, so an agent's playtest is a player's run.

## The labs

| Lab | Kind | Owner | Subject | Lands |
| --- | --- | --- | --- | --- |
| `noise` | picture | engine | `engine/core/noise.hpp` | M2 (now) |
| `moon` | picture | engine | The real moon (`engine/core/moon.hpp`, `engine/render/moon.hpp`, the shadow light of `engine/render/night.hpp`), drawn on the CPU: the albedo map, a month of phases as the eye at 46 N sees them each at its highest, the moon's height over a year beside the noon sun's, each full moon's path across the sky, and the eclipse of 3 March 2026 from 20 N 155 W (seas no darker than the highlands by 1.4 times, a winter full moon not 20 degrees over a summer's or an eclipse not dark fails); `--bake=<png> --write` regenerates `engine/render/moon_data.cpp` off the mosaic (`engine/lab/labs/gpu_moon_lab.cpp`) | M8f6 (now) |
| `stars` | picture | engine | The real stars (`engine/render/stars.hpp`): the generated table decoded and the cube map the sky pass reads cut from it; the count brighter than each magnitude, the stars no cell holds, the brightest missing, and the whole sky charted by right ascension and declination, each star by its magnitude and colour (fewer than 4,000 or more than 6,000 to the sixth magnitude, over 30 dropped or a fourth-magnitude star missing fails); `--bake=<catalog> --write` regenerates `engine/render/star_data.cpp` off the catalogue's text (`engine/lab/labs/gpu_stars_lab.cpp`) | M8f5 (now) |
| `menu` | picture | engine | `engine/ui` (a front door over every backdrop kind, both pages at 1280x720, 390x844 and 844x390, the highlight's glide, a page's fade and a picture's drift, drawn by the CPU raster; any layout problem, or a highlight slower than 15 frames to settle, fails) | M22a (now) |
| `charts` | picture | engine | `engine/lab/chart.hpp`, `plot.hpp`, `stats.hpp`, `series.hpp` (every chart form over seeded data with outliers planted at known places, each as a PNG and an SVG; a series file written and read back) | M14a (now) |
| `wind` | picture | engine | `engine/wind/field.hpp`, `sources.hpp` (the gusts over water, grass and forest with the intensity asked and measured and the 3 s gust factor; the log-law profile over six grounds; a sheltered field's plan 2 m up with a downwash and a whirl, as arrows over the speed; the sources' speed by distance) | M24a (now) |
| `waves` | picture | engine | `engine/water/spectrum.hpp`, `waves.hpp` (the growth laws' height and period by fetch for four winds; the spectrum of a lake's chop, an open sea and a shelf swell with the components laid off each and how they fan round the wind; a long series at one point measured against the laid Hs and mean period, more than 8 % off fails; each sea from above lit off its slopes and its profile along the wind) | M24b (now) |
| `fetch` | picture | engine | `engine/water/fetch.hpp`, `world.hpp` (an irregular lake's effective fetch for a wind from each quarter; the height one wind grows at every point; two patches of the surface the one query gives, windward and lee; the fetch, height, period and measured roughness along the long axis, a lee no rougher than the windward shore fails) | M24b (now) |
| `float` | picture | engine | `engine/water/float.hpp` (ten boxes of 100 to 950 kg/m3 settled and drawn at their draughts, more than 5 mm off fails; three boxes heaving with the period measured against 2 pi sqrt((m + ma) / (rho g A)), more than 3 % off fails; an undamped box's energy over a minute, any gain over 1 % fails; balls dropped from three heights with their splash, force and plunge; a planing sled run onto a pond at four speeds; nine buoys riding a sea) | M24c (now) |
| `swim` | picture | engine | `engine/water/swim.hpp` (a swimmer in a swimsuit treading water and lying still in warm and 2 degree water, his eyes against the surface; a first-person preset's kinematic speeds from rest against the formula, more than 1 unit/s off fails; the water jump's arc from the waist; the cold shock's breath and the swim-failure time by the water's temperature) | M24d (now) |
| `water-colour` | picture | engine | `engine/water/optics.hpp` (every named look from above over its own bed at six depths, the shore shelving to 20 m, and from 2 m down: the veil and white posts fading into it, a white wall receding with the visibility marked; the deep reflectance spectra; a brown lake's absorption by its parts; each look's visibility against its field band, out of it fails) | M24e (now) |
| `flow` | picture | engine | `engine/water/flow.hpp` (a dam break against Ritter's solution at 5, 10 and 20 s, more than 0.02 m rms off fails; a broken dam flooding a valley from above; a breach's outflow against the weir law, a power out of 1.3 to 1.7 fails; a drain against Torricelli's law; a river over a cliff from the side with the sheet's arc; the books of every run to the quantum and one thread against eight, any difference fails) | M24f (now) |
| `water-lod` | picture | engine | `engine/water/flow.hpp` (one flood run at each of the three tiers side by side, its water and its depth off the finest; the flood changed tier six times, its water to the quantum (a quantum lost fails) and the surface's jump; the tier an actor gives a region walking out and back across both edges) | M24f (now) |
| `water-cost` | bench | engine | `engine/water/flow.hpp` (a flow region's step timed by size, tier and one or eight threads; a sample of a developed sea) | M24f (now) |
| `gpu-water` | picture | engine | `engine/render/water.hpp` (a lake in a bowl of hills in a light air and a fresh wind; every named look from the shore; the open sea in a light, a fresh and a gale wind; the sea at sunset; a fly-out from 2 m to 2 km; the simulation's crest drawn and read back against `surface_at`, more than 3 cm off fails; any GPU error or pipeline made after the load fails; a view whose water changes under 5 % of the picture fails) | M24g (now) |
| `gpu-underwater` | picture | engine | `engine/render/underwater.hpp` (from 6 m down in a sandy bowl with white markers 2 to 32 m ahead: every named look with the eyes open, each captioned with its visibility; the four presets; straight up from 5 m down, Snell's window measured off the picture, more than 3 degrees from 48.6 fails; a dive from 1 m over the surface to 3 m under; any GPU error or pipeline made after the load fails) | M24h (now) |
| `liquids` | picture | engine | `engine/water/liquid.hpp`, `harm.hpp`, `liquids.hpp` (the share of a gale's input each named liquid keeps by wavelength and the drift it drags; the swim each gives; the damage at each immersion over time, lava's burns and the slimes' ticks; the radiant heat over a lava pool by height) | M30a (now) |
| `gpu-lava` | picture | engine | `engine/render/liquid.hpp` (a lava pool in a quarry by day, at dusk and at night; the crust's cover swept; the plates close up by day and night; the flow at four moments; both lavas; the glow table; a view with no lava in it, a GPU error or a pipeline made after the load fails) | M30b (now) |
| `gpu-slime` | picture | engine | `engine/render/liquid.hpp` (the five slimes from the rim and close up; the toxic slime by day, at dusk and at night; the gel seen into; one gale over three slimes; a view with no slime in it, a GPU error or a pipeline made after the load fails) | M30c (now) |
| `run-clock` | sim | engine | `engine/loop/run_clock.hpp` | M2 (now) |
| `parallel-cost` | bench | engine | `engine/core/parallel.hpp` (a five-point stencil baked by `parallel_bands` over square grids of each `--sides` on each `--workers` count: the median microseconds a call and the speed-up over the first count, measured, charted by grid side; an empty band a worker, the dispatch's floor; each grid's output hash, which must be the same on every worker count, is the digest) | M4e (now) |
| `level` | picture | plugin:mountain | `plugins/mountain/mapgen` (`--version=1,8`: the single piste, a ski area's race map) | M4a, M4b4a (now) |
| `analyze` | sim | plugin:mountain | `plugins/mountain/analysis` (a race map's course and area at `--version=8`) | M4a, M4b4a (now) |
| `mapgen-cost` | bench | plugin:mountain | `plugins/mountain/mapgen` | M4a (now) |
| `slalom` | picture | plugin:mountain | `plugins/mountain/mapgen/slalom.hpp` (a slalom set over a map, both runs, straightened and from above) | M4b4b (now) |
| `speed-events` | picture | plugin:mountain | `plugins/mountain/mapgen/speed_events.hpp` (the downhill, super-G and giant slalom set over a map, straightened and from above) | M4b4c (now) |
| `speed-ski` | picture | plugin:mountain | `plugins/mountain/mapgen/speed_ski.hpp` (a speed-skiing track cut down a map's face, its profile from the side and the map from above) | M4b4d (now) |
| `ski-cross` | picture | plugin:mountain | `plugins/mountain/mapgen/ski_cross.hpp` (a ski-cross course built over a map: close up, its profile with the mean fall taken out, the map from above) | M4b4e (now) |
| `park` | picture | plugin:mountain | `plugins/mountain/mapgen/park.hpp` (the terrain park on a tricks map: close up, its profile with the mean fall taken out, the map from above; `--version=1,8`) | M4b5 (now) |
| `resort` | picture | plugin:mountain | `plugins/mountain/mapgen/resort_build.hpp` (the ski area, its courses) | M4b3d (now) |
| `junctions` | picture | plugin:mountain | `plugins/mountain/mapgen/network_stamp.cpp` (where runs meet) | M4b3d (now) |
| `routes` | picture | plugin:mountain | `plugins/mountain/mapgen/ski_routes.hpp`, `route_features.hpp` (the ski routes on a ski area: the area with every route in orange, each route close up with its drops and jumps, each from the side with its pitch against a black's ceiling and a route's; `--version=9,10`) | M4c1 |
| `skijump-hills` | picture | plugin:mountain | `plugins/mountain/mapgen/ski_jump.hpp`, `straight_venue.hpp` (the ski jumping hill set over a map: the tower, the raised in-run, the dig, the landing hill's fill, cut and rms, the arena, the drop and the design gate as a table, least earth first; each hill's profile at true scale from the side and the map from above; `--hill=large,normal`, `--version=1,9`) | M4c2 (now) |
| `real-faces` | picture | plugin:mountain | `plugins/mountain/mapgen/real_face.hpp`, `real_face_crops.hpp`, the generated `real_faces/` (every face decoded and measured: its floor, top, drop and the share of it falling down the map, as a table; a sheet of every face shaded; a face alone with its mean profile; `--bake` reads each face off the 30 m elevation model beside the tree's, `--fetch` downloads the tiles, `--write` writes the faces and the index) | M4d1 |
| `real-hints` | picture | plugin:mountain | `plugins/mountain/mapgen/real_hints.hpp`, `packed.hpp`, the generated `real_hints/` (every face's real ski area decoded and counted: its chairs, gondolas and drags, its pistes by colour and their length, its tree line and its cover by band, as a table; a face named alone, or up to four, drawn over its shaded heights; `--bake` reads each face off the open map data (`--fetch` downloads it into `.cache/osm/`) beside the tree's, `--write` writes the faces and the index) | M4d3 |
| `real-maps` | picture | plugin:mountain | `plugins/mountain/mapgen/massif.hpp` (a face's massif), every rule a real face bends (`massif::real`), `generate.hpp`'s `face` (a ski area built on every face named: whether a clean map stands, its attempt, its pistes, lifts, courses, vertical, the vertical its lifts ride and its trees, as a table; the build's seconds, measured; a face named alone, or two, drawn from above, the real ski area's pistes and lifts faintly over it, and with its courses; how far its pistes lie from the real ones, `off real m`; `--hints=0` builds without leaning on them) | M4d2, M4d3 |
| `sim` | drive | game:fall_line | `games/fall_line/sim` (the step, the course, the bot, the field) | M5b–M5c (now) |
| `free` | drive | game:fall_line | `games/fall_line/sim/free.hpp` (the free ride: the bot down every piste of each seed's ski area from its head to a foot; the chain, the foot, the time, the runs found under him, the metres, the digest; the ski area with every run and the bot's lines; `--outings`: the bot down a piste, up the lift at its foot and down another run, with the wait, the ride and his line, white on the lift; `--sled`: the bot's outing on the snowmobile from its spot up a run and down it on skis, the metres climbed, the time on it, his line yellow on the machine; `--heli`: the bot's outing on the helicopter from its pad to the head of the highest run, dropped there and down it on skis, the metres climbed, the time on it, his line red on the machine; `--para`: the bot's outing under the paramotor off the highest run's head, landed on a gentle stretch of that run and down it on skis, the stretch, the time hung, the landing's load, his line blue while he hangs) | M13a1, M13a2, M13b2, M13c2, M13d2 (now) |
| `lift-ride` | drive | game:fall_line | `plugins/mountain/lifts/lift_loop.hpp` and `games/fall_line/sim/lift_ride.hpp` (every lift of each seed ridden from its load zone to its top: the carriers, the rope's speed, the headway, the wait, the ride, the slowest and fastest he was carried, the biggest carried step, the let-go's distance from its point; the ski area with every lift's carriers where the clock puts them) | M13a2 (now) |
| `sled` | scenario | plugin:snowmobile | `plugins/snowmobile` (`step_sled`, the body, the drive, the pilot: the top speed, the sink and the float, the climb and the high-mark, full lock's circle, the carve's roll, the brake and the parking brake, the drop, the cliff, the trunk, the bot up the slope and round a trunk; the line from above, the speed and the belt's sink) | M13b1 (now) |
| `heli` | scenario | plugin:helicopter | `plugins/helicopter` (`step_heli`, the rotor, the pilot: the spool, the lift-off and the torque's turn, the climb at full lever, the hover's drift, the cruise, the landing, the crash on a dropped lever, a steep face and a crown, the disc over the top, the height kept over a climbing face, the fuselage hung toward a rider; the line from above, the height and the speed) | M13c1 (now) |
| `para` | scenario | plugin:paramotor | `plugins/paramotor` (`step_para`, the wing, the air, the pilot: the launch off a face, the glide's speed and sink, the climb on full throttle, the banked turn, the stall, the bot down the fall line at its height and onto a spot, ridge lift up a face, the lee's folds, a crown and the drop; the acro: the dive, the swing-up, the loop, the tangle and its cut-away, with the loops, the top load, the tangle's time and the top sink; the line from above, the height, the speed and the folds) | M13d1, M13d3 (now) |
| `ride` | scenario | plugin:skier | `plugins/skier` (`step_skier`, the snow, the limits) | M5a (now) |
| `injuries` | scenario | plugin:skier | `plugins/skier/body.hpp` (every ride scenario's injuries by part, its ISS, when he was mortally hurt and died; a trunk skied into head-on (the body thrown on into it), across his whole line, and met beside him by closing speed and a landing by its load over 400 draws: the share hurt, the mean ISS, the share killed, the commonest injury; every scenario's parts by their worst AIS, and the curves) | M13e1 (now) |
| `play` | drive | game:fall_line | `games/fall_line/sim/play` on `engine/play` (an agent over stdio, the bot through the protocol, its input log replayed) | M5c, M12b (now) |
| `trace` | drive | game:fall_line | the player's skier step by step down one seed with the bot: `trace.tsv` (a series file), the speed, the sideways load and the edge and lean charted against the clock with gates, flights and spikes; `--log=` for the same run's events (docs/debugging.md) | M14a (now) |
| `why` | drive | game:fall_line | `games/fall_line/sim/bot`'s decisions (`engine/ai/why`): the pilot and a field of three down one seed with every bot traced and the same run untraced beside it (the digests must agree), per racer the share of steps it braked, tucked and glided, its changes of mind a minute and its close calls; the player's speed against the plan's cap with every change of mind marked, and the margin of each speed call; `why-0.tsv` for `ge graph` | M16a (now) |
| `replay` | sim | game:fall_line | `engine/replay` (`keep`, `player`) over Fall Line's run clone (`games/fall_line/sim/play.cpp`): the pilot's race on one seed with a field, its log played by the replay's player and sought to every keyframe, a step either side of each and a spread back and forth, each seek's digest against the run played straight there (`mismatches` must be 0); the keyframes kept, a clone's bytes and time, the worst seek in steps and ms; the timeline of keyframes and seeks (`docs/replays.md`) | M23a (now) |
| `replay-cut` | sim | game:fall_line | `engine/replay/director.hpp` over Fall Line's moments (`games/fall_line/sim/moments.hpp`): the pilot's race through the live recorder, its moments filed as marks (a flight at its take-off), cut by the director; every flight in the cut and opening before its lip, the spacing held; the reel played at `--fps` off the live keeper, every landed frame checked against the live race (`mismatches` 0), the frames a cut holds counted; the cut and every mark's fate as tables, the timeline, a `near` still at each shot's mark | M23c (now) |
| `replay-file` | sim | game:fall_line | `engine/replay` (`file`, `live`, `index`): the pilot's race through the live recorder, the player's moments filed as marks; an instant replay of the last `--back` seconds every `--every`, every step checked against the live run and the live run resumed to the same digest; the race written as a replay file, read back to the same text, refused one game version on (both versions named), listed by the index, clipped round its heaviest mark and the clip played in to out; the file attached (`race.ge-replay`) and the timeline of marks, replays and clip | M23b (now) |
| `humanness` | drive | game:fall_line | `games/fall_line/sim/hands` on `engine/ai/hands` and `engine/ai/human` (each hand profile, `none` and `human`, over seeds 38, 7 and 101 with a field of four: every racer's axes (saturation, jumps, reversals, hold, lag) and decisions (changes of mind) against a person's bands, every value out of band listed with the assumption it breaks, whether each racer finished, its time and resets; the player's steer meant against pressed, charted; `player-<profile>.tsv` for `ge graph`) | M16b (now) |
| `path` | sim | game:fall_line | `engine/ai/path` and the outing's graph search (`games/fall_line/sim/outing`): one seed's walk on foot across its ski area from the first lift's bottom to the last lift's top on 8 m cells (each step its length and 6 m more a metre climbed or dropped, barred over a rise of 0.7), by Dijkstra and by A* (held to one cost, the cells each expanded beside it), string-pulled to its fewest clear legs and drawn on the map; every run's outing (the lift and the next run the graph search chose) | M16d (now) |
| `sight` | sim | game:fall_line | `engine/ai/sight` under the wanted level's sight line (`games/fall_line/sim/wanted`): one seed's sheds from its first lift's top, a soldier's on the snow and a helicopter crew's 60 m over it, a point every 10 m asked whether he sees a man there and why not (out of range, behind the ground, through the woods; heard within reach), each answer held to `sees_quarry`, the shares as a table and each shed drawn on the map | M16e (now) |
| `steer` | sim | game:fall_line | `engine/ai/steer` under the army's drive and troop (`games/fall_line/sim/army_drive`, `army_troop`): an army snowmobile's fan of headings at one seed's first lift's bottom wanting its last lift's top, each heading walked out, costed and drawn (the one taken in yellow) and tabled; 96 soldiers packed in 40 m, each one's push apart over the step's grid held to the full pass's to the bit, with the visits each made | M16e (now) |
| `wanted` | scenario | game:fall_line | `games/fall_line/sim/wanted` (the wanted level on seed 38 against scripted pursuers: the hunt table of the stars reached and when, the share seen, the searches, how it ended; each scenario's line by the stars; the sight sheds of a soldier at the start gate and a helicopter over the middle: open, hidden by the ground, by the woods, out of range) | M15a (now) |
| `army` | scenario | game:fall_line | `games/fall_line/sim/army` (the army on foot on seed 38, the man stood still while the stars are pinned: `valley`, `slope` and `arrest`; the army table of the lifted men sent and let off, the waves' men, the most out, how many hold him, the nearest, the men caught and when he was arrested; every soldier's path over the map) | M15c (now) |
| `army-sleds` | scenario | game:fall_line | `games/fall_line/sim/army_sled` (the army's snowmobiles on seed 38, the man stood still while the stars are pinned: `open`, `run`, `face` and `arrest`; the sleds table of the machines called, the most out, the short halts, the times dug out and righted, the nearest, when they came in range, the crews off, the arrest and whose; every machine's path over the map) | M15d1 (now) |
| `army-flight` | scenario | game:fall_line | `games/fall_line/sim/army_heli` (the army's helicopters on seed 38, the stars pinned and the army's other arms on: `open`, `rope`, `leapfrog`, `hide` and `fast`; the helicopters table of the machines called, when the first came in, the circle flown round him, the landings and fast ropes, the men out and picked up, the men holding him, when the stars were lost, the machines gone home and the share of echelon; every machine's path, where it set down and where its men went) | M15e1 (now) |
| `gunfire` | scenario | game:fall_line | `games/fall_line/sim/gunfire` (the army's guns: the range table of each gun's rounds into him by range, rules of fire and a still or crossing man, with the energy a hit carried and the part struck most; the hunt table of a seed 38 hunt's rounds by the soldiers, the snowmobiles' gunners and the door gunners, the hits, his injury severity score and how the hunt ended, where the army shoots to kill and where it does not; the doors table of the door gunners' rounds at each rule of fire, and a picture each of the machines round him and every door round from its muzzle, coloured by its door) | M15b, M15e2 (now) |
| `crowd` | scenario | game:fall_line | `games/fall_line/sim/crowd.hpp` and `crowd_lift.hpp` (the free ride's crowd on `--seed`, `--count` amateurs over `--limit` seconds, the player stood still: the runs table (colour, length, where each merges, the lift at its foot, the lifts it passes), the crowd table (how many ski, stop, are down, queue, ride, skate and are up a lift unseen), the kinds table (their mean speed, falls and stops a minute, the runs skied), the lifts table (the ride's time, the first carried up, the let off, the still queued); a step's time, measured; fails on a place that is not a number, a lift idle past its ride, anyone stuck 60 s or a jump over 1 m in a step; every amateur's line over the map) | M13f (now) |
| `anim` | scenario | plugin:skier | `engine/lab/anim.hpp` over the skier's figure (`plugins/skier/app/figure.hpp`) ridden through a ride scenario: filmstrips from the side, behind and above, onion skins with the hands' and tips' paths, the joints' angles, a jolt heatmap, `motion.tsv`, the faults found (pop, snap, angle snap, stretch, clip), and a copy with a pop, a stretch and a clip planted, each of which must be found | M14b (now) |
| `gpu-capture` | picture | engine | `engine/gpu` (the headless capture) | M6 (now) |
| `frame-pace` | bench | engine | the platform's frame loop | M6 (now) |
| `world` | picture | game:fall_line | Fall Line's frame from named views of a seed, TAA-U on or off at a render scale, the shimmer, the overdraw probe, a run's moments from the chase camera with the field (`--moments`, `--rivals`), the racers' tracks laid over the run before each moment (`--trails=0`: none), the cull check (every view redrawn with occlusion culling off, `cull-faults` must be 0), a reach (`--reach`, m: the ground and its woods drawn only so far, misted into the sky from half of it), the `wood` view (the trees' shadows on sunlit snow), the game's own cvars set first (`--cfg="sv_weather storm; sv_hour 17"`: the sky's `sv_` cvars and `r_clouds`) and the sun's cascades over the whole run (`cascade-redraws.near` and `.far`, deterministic, with no device) (`games/fall_line/labs/gpu_world_lab.cpp`) | M7a, M7d1, M7d2, M21b (now) |
| `record` | picture | game:fall_line | The play contract's GPU pictures (`games/fall_line/app/play_render.hpp`, what `ge shot --gpu` and `ge record --gpu` draw): the bot's run of `--seed` to `--at` seconds drawn from `--view` in every mode (lit, wireframe, depth, overlay) by one renderer, then `--frames` more pictures a recording apart; each mode's picture size held, the share each lights measured (`games/fall_line/labs/gpu_record_lab.cpp`) | M12d (now) |
| `taa` | picture | game:fall_line | TAA-U on a moving camera against the converged still: ghosting, aliasing, stills repeat; `--scenario` chases a ride scenario's skinned skier instead (`games/fall_line/labs/gpu_taa_lab.cpp`) | M7c (now) |
| `crowd-look` | picture | game:fall_line | The free ride's crowd (`games/fall_line/sim/crowd.hpp`) drawn in Fall Line's frame as one palette family (`engine/render/skinning.hpp`, `games/fall_line/app/scene.hpp`'s `crowd_instance`): `--count` amateurs on `--seed` run `--at` seconds, under the sun at `--hour`, then the busiest lift's QUEUE, the BUSY stretch of a run, a GROUP from ahead of its leader and one DOWN, each filmed `--frames` frames with the crowd and again without it (the nearest amateurs casting into the figures' atlas): the share of the picture the crowd changes, the frame's CPU ms, the instances' and the figures' shadows' GPU ms, each side (`games/fall_line/labs/gpu_crowd_lab.cpp`) | M8e2 (now) |
| `shadows` | picture | game:fall_line | the horizon maps over a day from above, and the bake counter of a sun sweep (`games/fall_line/labs/gpu_shadows_lab.cpp`) | M7b (now) |
| `profile` | bench | game:fall_line | Fall Line's frame over a stretch of the bot's run from the chase camera: every pass's GPU ms (median, p95, worst), the CPU ms encoding it, its draws and dispatches; the renderer's CPU half against `--cpu-budget`; where a frame's CPU goes by part (the prepare's timing zones, each pass's encoding, the simulation's steps between frames by their zones); `frames.tsv`, one row a frame (`games/fall_line/labs/gpu_profile_lab.cpp`; `ge profile --render` runs a game's) | M7d3, M12b (now) |
| `ladder` | bench | game:fall_line | Fall Line's quality ladder (`games/fall_line/app/ladder.hpp` over `engine/render/ladder.hpp`): every row cheapest first from a stop that costs and is worth nothing, the presets' cost and worth, and the automatic preset over a sweep of budgets on the table's machine and one twice as slow (a fit over its budget, or worth more on a smaller one, fails); `--price` times every stop on the adapter (each row lowered alone from the top, under a flurry, from `--view` (the start raised 15 degrees into the sky by default, `chase`, or `--view=vista --lift=0 --fall=0` for the distance row), in rounds) and prints the lines for the price table, and shoots the view at every preset; `--spread=0` prices the cloud's quarter march taken whole every frame (`games/fall_line/labs/gpu_ladder_lab.cpp`) | M8f4 (now) |
| `snow` | picture | game:fall_line | The snow's material up close (`plugins/mountain/app/snow.wgsl` in Fall Line's frame): the `corduroy`, `powder` and `glitter` views at each `--hours` hour, each PLAIN (the material's gains at nought: the albedo lit) and SNOW, with the ground's mean luma, its detail and its glints beside each (`games/fall_line/labs/gpu_snow_lab.cpp`) | M8a (now) |
| `trails` | picture | game:fall_line | The tracks (`plugins/skier/app/tracks.hpp`'s prints in `plugins/mountain/app/trails.hpp`'s maps, read by the snow's material) over a bot's run with `--rivals`: the TURN (the deepest edge) and the STRAIGHT (the flattest) looked down on, each groove's depth and width read back from the fine map across the skier's line beside the CPU's prints (the outside groove must be the deeper through the turn), and the RIVAL place (a racer within 4 m of a line another laid a second or more before), whose track must be in the fine map (`games/fall_line/labs/gpu_trails_lab.cpp`) | M8b (now) |
| `models` | picture | engine | `engine/assets` (an authored model at rest from behind and from the right, shaded with its edges and skeleton; each clip as a filmstrip, an onion skin of every frame's bones, and its joints' angles from rest and angular acceleration graphed over time; its counts, its weights' check and each clip's largest angle and acceleration; `--model=<file.glb>` for any model, the test model through a .glb by default) | M8e1 (now) |
| `model-views` | picture | engine | `engine/lab/model_sheets.hpp` over any model (`--model=<glb>`, `--asset=<name>`, the test model by default): all the way round in perspective, orthographic from four sides against a ruler, every edge and the skeleton, close-ups of its small parts (`--detail`), every level of detail (`<name>.lod<N>`) side by side and drawn 96 to 12 px tall, every clip as a strip; with `--refs=<file>` the reference sheet (`engine/lab/model_compare.hpp`: photographs, cropped, beside the model posed and turned to match; outside the digest) | M8e2 (now) |
| `skier-swim` | picture | plugin:skier | `plugins/skier/swim.hpp` (the dressed skier and a swimmer through the ice of 1 degree water, lying still and swimming up: the eyes against the surface, when they go under for good, when the breath runs out, the water carried; the skier's budget as his clothes soak: the trapped air, the soak and what he weighs in the water) | M24d (now) |
| `skier-views` | picture | plugin:skier | the skier model as the game poses it (`plugins/skier/app/rig_pose.hpp`): the stances (stand, carving each way, the tuck, the air, down) from four sides, the carve all the way round and close up, and the reference sheet with `--refs` (poses: a stance and `field:value` changes) | M8e2 (now) |
| `forest` | picture | plugin:mountain | The trees as drawn (`plugins/mountain/app/trees.hpp`): every form's four variants at full, mid and far on one sheet, standing as an instance stretches them and lit from behind the shoulder, every kind in its colours on another; the triangles a mesh against each LOD's budget (450, 160, 60; over one fails), the share of its faces carrying snow (`plugins/mountain/labs/gpu_forest_lab.cpp`) | M8c (now) |
| `spray` | picture | game:fall_line | What the skis throw (`plugins/skier/app/spray.hpp`), flown and drawn by the engine's particles (`engine/render/particles.hpp`) in Fall Line's frame: four ride scenarios (`carve-hard`, `hockey-stop`, `kicker` a fifth of a second after landing, `powder-turns` in a 6 m/s crosswind, its cloud drifting off) drawn frame by frame from their start, each from the chase camera and from the side; the grains the arithmetic threw (none fails) and the live particles at the shot (`games/fall_line/labs/gpu_spray_lab.cpp`) | M8d1 (now) |
| `night` | picture | game:fall_line | The night in the picture (`engine/render/night.hpp` over `plugins/mountain/app/night_look.hpp`): a seed's map at a dusk falling (3, 8 and 14 degrees under), the minute either side of the moon taking the key light, under a full, a quarter and a new moon, the full and the quarter moon's discs through a long lens, the moon by day, close-ups of a day moon, a twilight crescent, a full moon and an eclipse (drawn under a clear sky), a moonless sky's stars and the real stars' highest figure (the hunter, the plough, the southern cross or the summer triangle) through a 40-degree lens; the date, the hour, the sun's and the moon's heights, the moon's light, the snow's and the sky's luma and the stars drawn, and the twilight's light off the sky tables against `twilight_lux` (a dusk that does not fall, a handover that jumps over 20 %, moons out of order, no stars, the twilight off by half or a close-up's disc no brighter than its sky fails) (`games/fall_line/labs/gpu_night_lab.cpp`) | M8f2, M8f5, M8f6 (now) |
| `snowfall` | picture | game:fall_line | The falling snow (`engine/render/snowfall.hpp`) on the map's wind (`plugins/mountain/air/air.hpp`) in Fall Line's frame: a seed's map under a flurry, a steady snow and a storm in turn, from a named view, drawn frame by frame up to the shot; the fall, the visual range, the wind and the flakes drawn at the shot (a snowing sky that draws none fails), the passes' GPU ms (`games/fall_line/labs/gpu_snowfall_lab.cpp`) | M8d2 (now) |
| `lake` | picture | game:fall_line | The water in Fall Line's own frame (`engine/render/water.hpp` in `games/fall_line/app/frame.cpp`'s table): a tarn dug into a seed's lower slopes and filled (`water::add_lake`, the dug ground its bed), from its shore, from 45 m over it, into the sun, in 12 m/s, at 16:30, from 2 m under it and looking up from 2.5 m down (`--look` the water, a clear tarn by default); each drawn with and without the water, a view whose water changes too little of the picture fails, as does any GPU error or pipeline made after the load (`games/fall_line/labs/gpu_lake_lab.cpp`) | M24g, M24h (now), M24l |
| `weather` | picture | game:fall_line | The mountain's skies in the picture (`plugins/mountain/app/weather_look.hpp` and `cloud_field.hpp` over `render/view.hpp`'s `Weather` and `Clouds`, `wgsl/frame.wgsl` and `render/clouds.hpp`): a seed's map under each of the eight weather kinds in turn, from a named view (`--lift` raises it to the sky); the look's numbers (the sun's pass, the deck's cover, the visual range, the deck hidden, the cloud's layers, the share of the sky the lowest covers and of the sun their shadows leave the map), the picture's luma and contrast, whole and in its upper half, the cloud passes' GPU ms (each pass and the whole, the median of the frames past the first two), and the share of the picture the quarter-resolution cloud, spread over the frames (`--spread=0`: whole every frame; `--turn` swings the view round, degrees a second), misses every pixel's march by more than 8 levels (an overcast no flatter than the clear day fails, a fog's far half no flatter than the clear day's, a layer's measured cover more than 0.1 off its dealt one, and over 1 % missed by the quarter) (`games/fall_line/labs/gpu_weather_lab.cpp`) | M8f1, M8f3, M8f4 (now) |
| `skis`, `skier` | picture | game:fall_line | the world drawn | M8e–M8e2 |
| `synth` | sim | engine | `engine/audio` (every voice the synth can play, metered, pictured and written as WAV; the render's cost) | M9a (now) |
| `move-strafe` | sim | engine | `engine/move` (the acceleration rule, `strafe.hpp`): the classic row's speed over a run (a run, straight hops, the strafe jumper two ways), every preset row strafe jumping, one step of air's gain against the wish angle with the best angle marked, the paths from above, the end speed by step rate | M28a (now) |
| `move-trace` | scenario | engine | `engine/move/scenarios.hpp`: every scenario's feet and head from the side with the world's solids (or from above), take-offs and landings marked; every row's running jump; every run's numbers and digest | M28a (now) |
| `input-shape` | picture | engine | `engine/input/shape.hpp` (every dead zone along a diagonal and near a cardinal, the response curves with an anti-dead zone, the key ramps over time, charted) | M19a (now) |
| `input-ffb` | scenario | engine | `engine/input/feedback.hpp` on the simulated wheel (`engine/input/sim.hpp`): a spring returning the wheel and how fast it settles, a sine felt as a ring, a constant pull, a road's rumble | M19a (now) |
| `input-session` | scenario | engine | `engine/input` end to end on simulated devices: a skier's, a shooter's and a racer's bindings over a keyboard, a raw mouse, a pad, a wheel and touch; the event log replayed to the same actions, players joining, a rebind and its conflict | M19a (now) |
| `camera-follow` | scenario | engine | `engine/camera/follow.hpp`: every follower and spring the rigs are built on, handed a quick rise and a ramp at 30, 60, 144 and 240 Hz and at 60 Hz with 100 ms hitches, charted one line a rate; the overshoot, the settle and the stray from the 240 Hz line (a stray past the case's bound fails) | M26a (now) |
| `camera-rigs` | scenario | engine | `engine/camera/` through every scripted subject (`engine/camera/scenarios.hpp`: walk, strafe, jump, stairs, crouch, fire, cliff, carve, skid, trees, teleport) on every rung of the reference ladder: what four lenses see at four moments (sketched on the CPU raster), the eye over the ground, the horizon's roll, the subject's place in the frame and the fov charted; each rig's least clearance, worst acceleration, stray and unexplained jumps (a jump or a stray past a boom's frame fails) | M26a (now) |
| `camera-world` | picture | game:fall_line | `games/fall_line/camera` over `engine/camera`: the bot's run on seed 38 followed on every rung of Fall Line's ladder at once (and a straight run through the densest wood), each rung pictured at the start, the hardest carve, the biggest jump, its landing and the closest pass to the trees, the camera key's hand-overs as strips; every lens's least height over the snow and least gap to bark (a lens inside a trunk or under the snow fails); the trees each lens sees through in the woods and the share of him each outside lens sees (`--see-through=0`: the trees whole). GPU | M26c, M26g (now) |
| `camera-transitions` | scenario | engine | `engine/camera/transition.hpp` and the stack's `set_rung`: cut, blend and inertialize between pairs of rungs mid-carve (and a retarget half way): strips of what the lens sees through the change, the eye's distance and speed in the subject's frame charted beside the arriving rig's; the kick in the eye's velocity at the switch, the peaks and when it lands (a smooth edit that kicks the eye or lands late fails) | M26a (now) |
| `camera-shake` | scenario | engine | `engine/camera/shake.hpp`: an explosion's and a landing's trauma at comfort 1, 0.5 and 0, and the full blow at 30, 60 and 240 Hz, charted; peaks, the calm, the wave's frequency (a shake past its row, outliving its trauma, faster than its top frequency, moving at comfort 0 or changing with the rate fails) | M26a (now) |
| `audition` | sim | game:fall_line | Fall Line's bank, the skier's beds at seven moments and the bot's whole run, metered, pictured and written as WAV | M9b (now) |
| `screenshots` | picture | game:fall_line | the HUD and the cards at 1280x720, 390x844, 844x390 | M10 |
| `console` | picture | engine | `engine/console/view.hpp` (the drop-down console drawn on the CPU from its model: down, sliding and closed with the notify lines, every kind of line coloured; the lines by kind and the slide's frames) (`engine/lab/labs/console_view_lab.cpp`) | M21a (now) |
| `cli-loop` | bench | engine | `engine/cli` (the `ge` commands of an agent's loop run on a scaffolded repo: each answer's lines, bytes and time, failed over its line budget; the catalog's shape is the digest) | M12a (now) |
