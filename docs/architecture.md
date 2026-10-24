# Architecture

The layout gameengine2 builds toward, and the decisions behind it (§ Decisions).
`docs/goals.md` says in which order. Where a part is not built yet, this
page is the plan it is built to. Change this page in the same PR that changes the plan.

## The layers

```
games/fall_line/app      the demo game's shell: cards, HUD, its own look, the bank
        │
games/fall_line/sim      the game itself, headless: create_game, the step's order, the modes,
        │                the bot (M5; deterministic, no platform, no GPU)
        ▼
plugins/<name>           reusable by any game of one genre or world (§ Plugins):
        │                racing (tape, records, standings), mountain (mapgen, analysis),
        │                skier (the skier on the snow: step_skier, snow, limits, the pilot),
        │                snowmobile (the machine on the snow: step_sled, the drive, the pilot),
        │                helicopter (the machine in the air: step_heli, the wash, the pilot),
        │                paramotor (the speed wing a skier flies: step_para, the air, the pilot),
        │                rooms (the engine's own level format: rooms grown from their seeds),
        │                brush (the classic BSP families read into one view, fixture writers)
        ▼
engine/render  engine/audio  engine/ui     generic draw passes, the synth, the 2D layer
        │             │          │
engine/gpu  engine/assets  engine/platform webgpu.h wrapper; authored models (glTF); SDL3 window,
        │             │          │         input, audio device, files
        │             │          │
        ▼             ▼
engine/core  engine/loop                   the deterministic pool; the run clock
```

One direction of dependency, held by `tools/check-agents.sh` once each layer exists:

- **`engine/core`**: the deterministic pool: prng, math, libm, num (the number semantics
  helpers), noise, heightfield, quat, solar and output. It depends on nothing but the C++
  standard library headers that do no I/O and
  no floating-point transcendentals: no `<cmath>` trig, exp or pow; `sqrt`, `fabs` and
  `floor` are allowed.
- **`engine/loop`**: the fixed-step run clock. Depends on nothing.
- **`plugins/<name>`**: a mechanism one genre or world shares (§ Plugins). Its simulation
  half may include only the engine targets and plugins its `ge_add_plugin` call declares.
- **`games/fall_line/sim`**: Fall Line's own simulation. It may
  include only `engine/core`, `engine/loop`, the plugins it links and itself. It has no platform, no GPU and no wall
  clock.
- **`engine/platform`**: SDL3 behind a narrow C++ API: the window and its native handles,
  input events and the frame's monotonic clock (built, M6), then the gamepad and rumble,
  touch, the audio device and storage paths as their milestones need them. It is the only
  directory that includes SDL headers, and it hands the GPU a plain `NativeWindow` struct.
- **`engine/gpu`**: the native GPU device over Dawn (built, M6): the device and its feature
  tiers, error lines, shader modules from embedded WGSL, the pipeline manifest and its
  creation counter, the surface, offscreen targets and readback, and per-pass timestamps.
  It uses Dawn's `webgpu_cpp.h` handles (already reference-counted RAII) directly rather
  than wrapping them again, so every call reads as Dawn's API spells it. It is the
  only directory that includes `webgpu.h`.
- **`engine/render`**: generic rendering mechanisms (the pass table and named targets,
  GPU-driven instance families, the heightfield clipmap, shadows, the sky tables and
  clouds, TAA-U, post, compute particles, skinning), built as `ge_render` with the GPU. A
  pass owns its GPU buffers and exposes `prepare(view)` and `encode(pass)`; the frame core
  (the view and camera, the pass table, the sky, the terrain, post) is built (M7a).
  **`docs/rendering.md` is the plan** for this layer and `engine/gpu`.
- **`engine/assets`**: authored models (built, M8e1): the glTF 2.0 loader over cgltf (the only
  file that includes it), the `Model` it fills in the world's frame (skinned meshes, every node
  a joint, materials, clips baked at 60 Hz), posing into a bone palette and CPU skinning. No
  GPU and no platform; `engine/render`'s skinning pass reads its palette. `docs/rendering.md` §
  Authored models is the design.
- **`engine/audio`**: the synth (built, M9a): the vocabulary (`Tone`, `Noise`, `LayerSpec`,
  `LayerTarget`), a fixed-capacity DSP graph (polyBLEP oscillators, coloured noise pools,
  state-variable filters, envelopes, soft saturation, four buses, an echo and a soft-knee
  limiter), sounds as data (`SoundDef`, `play_def`), racks of steered layers, the meter and WAV
  bytes. It renders 48 kHz stereo float with no platform: the game thread's calls cross to the
  render thread through a lock-free ring, and the render is deterministic to the bit (core/libm,
  core::Rng), so a buffer is a golden and a lab digest. `engine/platform`'s audio device pulls
  it on SDL's audio thread and converts to the hardware's rate.
- **`engine/net`**: multiplayer networking (built from M17a; `docs/networking.md` is the design):
  addresses, the wire, the OS's UDP sockets (`udp_socket.cpp`, the only file that includes
  socket headers), the simulated network and its conditions, pcap captures, the transport, the
  packet format, connections with their channels, and nodes of up to 32 peers. It depends on
  `engine/core` only and reads no clock: the caller passes the time and the seeds, so a session
  over the simulated network is deterministic and every net lab is a determinism test.
- **`engine/console`**: the console and cfg files (M21; `docs/console.md` is the design): cvars
  in three tiers (the player's, the server's, code's), bound to the settings structs the
  subsystems already read so no subsystem includes the console; the command buffer, aliases,
  scripting, binds, the cfg startup order and `cfg_save`; and the drop-down console's model,
  which holds no pixels. It depends on `engine/core` only and reads no clock: a frame is one
  `run`.
- **`engine/input`**: the input layer (`ge::input`, M19; `docs/input.md` is the design): one
  32-byte event for every device, the state a frame reads, dead zones and ramps, action maps in
  contexts, the bindings file and rebinding, the device database, touch controls, local players,
  rumble and force feedback as pure functions, a simulated backend and the event log. It depends
  on `engine/core` only; the OS's devices reach it through `engine/platform`'s backend.
- **`engine/ui`**: the immediate-mode 2D layer (M22): paints (colours, linear and radial
  gradients), a draw list of rounded boxes (a paint, an image, a border, a feather) and text runs
  under clips, fonts over stb_truetype with a built-in family compiled in, a CPU raster (the labs'
  pictures and the GPU pass's reference), backdrops (`Background`: a colour, a gradient, a drifting
  picture, the live scene, scrims and vignettes) and menus as a model (pages, items, focus, events)
  with a responsive view. It depends on `engine/core` only and holds no GPU: the draw list is data,
  and the 2D pass in `engine/render` (M22b) draws the same commands. A menu never calls the game;
  it hands back events, so the game's state stays the game's and a menu replays from its inputs.
- **`engine/move`**: character movement (`ge::move`, M28; `docs/movement.md` is the design): the
  knobs of a moving body and their names, preset rows (an arena shooter's, a platformer's, a
  walker's), one step over a swept-box trace callback the game answers with its own world, a box
  world for labs and prototypes, the strafe jumper's best angle and the scenarios. It depends on
  `engine/core` only and is a pure function of its inputs, so a run replays from its commands.
- **`engine/vfs`**: content files (`ge::vfs`, M18a): the pack archive's directory checked whole
  before anything is read, and each file handed out as a view into the archive. Search paths, zip
  archives and directories come with the rest of M18a. It depends on `engine/core` only.
- **`engine/ui`**: the immediate-mode 2D layer (rects, images, SDF text), plus Dear
  ImGui behind `GE_DEV_UI`.
- **`games/fall_line/app`**: Fall Line's presentation, the `fall_line_app` library (which
  the window and the game's GPU labs link): its pass table over `engine/render`, the
  `FrameView` it extracts (from named views now, from `GameState` once the run lands),
  Fall Line's own looks, then its HUD and cards, and its sound beds.
- **`engine/play`**: the play contract (`ge::play`): what a game fills in so it can be played,
  replayed and inspected without a screen (its axes, its options, a `Run`: step, over, digest,
  observe, state, report, pilot, trace), and everything the engine does with it once for every
  game:
  the session that snaps, records and steps every input, the line protocol, the plain-text
  input log, replay, the state at a step, the divergence search, the bench (the pilot's run
  replayed and timed step by step and zone by zone, with its allocations: `ge bench`,
  `ge profile`, `ge mem`), a log replayed with the decision trace on (`ge why`), and the `play`
  executable's main. It depends on `engine/core`, `engine/ai` and `engine/prof`; a game's
  `play` target links it and `ge` runs it.
- **`engine/replay`**: replays (`ge::replay`, `docs/replays.md`): a run watched again off its
  input log from any moment. The keyframes are the game's own `Run::clone`s, kept every few
  seconds and thinned with age; the player seeks by cloning the nearest one and stepping it on
  in a budget of steps a frame; the transport is a video player's controls as pure state. It
  depends on `engine/play`; a game that clones its run gets seeking for free.
- **`engine/ai`**: the bots' layer (`ge::ai`): the DECISION RECORD every bot writes where it
  chooses (`why.hpp`: the facts it read, the options it weighed with their scores and the named
  terms of each, the choice and its margin over the next best), held to one rule: off is a null
  pointer and recording decides nothing, so a run is the same traced and untraced and `ge why`
  rebuilds the records by replaying a log instead of keeping them. The pieces bots share (the
  hands, thinking on a schedule, sight and memory, paths, steering, choosing) land here, each
  with its first user (`docs/goals.md` § M16). It depends on `engine/core` alone and is
  simulation code: deterministic, no platform, no `<cmath>` transcendental.
- **`engine/wind`**: the wind (`ge::wind`) as a pure function of place and time over a field
  built once (the mean, gusts and veer, the log-law profile, shelter, a world's rule on
  bearings, local sources from the game's state); a world's wind is one field of it (the
  mountain's, `mountain::air::field_of`). It depends on `engine/core` alone and is simulation
  code (`docs/water.md` § Wind).
- **`engine/water`**: the water (`ge::water`): bodies laid in one call each (a lake, an ocean, a
  river, a volume of hulls) and the one `sample` query everything reads, the sea a wind grows
  over each point's fetch and the components the simulation sums. It depends on `engine/core`
  and `engine/wind` and is simulation code (`docs/water.md`).
- **`engine/prof`**: the profiler (`ge::prof`): timing zones (`GE_ZONE("name")`, one relaxed
  load when no recording runs; a span with its depth, time and allocations when one does), their
  totals (a zone's own time and its children's), the counting `operator new` an executable
  opts into (`ge::prof_alloc`: every allocation and its bytes, still through `malloc` so the
  sanitizers see each block) and the peak resident set. It reads the steady clock and feeds
  nothing back, so no digest depends on it; any layer may open a zone.
- **`engine/lab`**: the lab contract and the `labs` runner (`docs/labs.md`): how any
  subsystem, plugin or game declares a lab, the CPU raster and PNG writer its pictures are
  drawn with, and the report, contact sheet and digest a run leaves. Labs themselves sit in
  a `labs/` directory beside their subject (`engine/lab/labs/`, `plugins/<name>/labs/`,
  `games/<name>/labs/`) and are compiled into one `labs` executable. A lab may include
  anything, and nothing includes a lab. Lab code keeps the simulation's numeric rules (no
  `<cmath>` transcendental, `core::Rng` only), because its digest is checked on every CI row.
- **`tools/`**: the structure checks and the index. The headless `sim` CLI is Fall Line's
  `sim` lab (`make sim`), and level-map pictures are the `level` lab's. Tools may include anything, and nothing includes a tool.
- **`porting/`**: the WORKING area, deleted when porting ends (`AGENTS.md` § The working
  area): the porting guide and map, and the generators of the golden tables. Nothing
  outside it includes or names anything in it but the `make golden` targets.
- **`tests/`**: one executable per topic.

A new game would sit beside `games/fall_line` and link the plugins it shares with it.
Fall Line's needs decide WHAT is built (G4); § Plugins decides WHERE it goes (G8).

## Plugins

The engine is genre-agnostic. Every piece of code has one of three homes, decided before its
first line is written (the `plugins` skill has the questions):

| Home | Holds | Words it may use |
| --- | --- | --- |
| `engine/<module>/` | What a game of any genre would use unchanged | The mechanism's only (vehicle, checkpoint, track) |
| `plugins/<name>/` | What any game of one genre or world would use unchanged | Its world's (a ski plugin says skier, piste, gate), never one game's |
| `games/<game>/` | What makes this game this game: modes, rules, the order of the step, the bot, pinned maps, strings, the HUD, its look | Its own |

**The contract**, stated here once:

- **Layout.**
  ```
  plugins/<name>/CMakeLists.txt          ge_add_plugin(<name> ENGINE core PLUGINS racing)
  plugins/<name>/[<module>/]*.{hpp,cpp} the simulation half, a directory per module
  plugins/<name>/app/                    the presentation half, built as ge_<name>_app when the
                                         call names its APP list (ge_add_plugin(skier ... APP audio))
  plugins/<name>/labs/                   its labs, never compiled into ge_<name>
  .agents/skills/<name>/                 its skill and lessons
  tests/<name>_<topic>_test.cpp          its tests; goldens tests/golden/<name>_<topic>.txt
  ```
- **Naming.** A plugin's name is one lower-case word, and it is the directory, the namespace
  (`ge::<name>`), the target (`ge_<name>`), the skill and the test prefix. It never equals an
  `engine/` or `games/` directory. Headers are included as `"<name>/<file>.hpp"` or, when the plugin
  has more than one module, `"<name>/<module>/<file>.hpp"`.
- **Build.** `cmake/Plugins.cmake` defines `ge_add_plugin`; the root `CMakeLists.txt` adds every
  `plugins/*/CMakeLists.txt` it finds, so there is no list to register in. Tests link every
  plugin. The plugin's targets carry the same FP flags as the core, through `ge_core`.
- **Dependencies.** A plugin's `ENGINE` and `PLUGINS` lists are its whole dependency contract,
  and its `APP` list names the engine targets only its `app/` half (and its labs) may reach
  (`audio`, `render`): `ge_<name>_app` links `ge_<name>` plus those, and the simulation target
  never sees them. An `app/` with sources and no `APP` list stops the configure.
  Games depend on plugins, plugins on the engine and on the plugins they declare, with no
  cycle; the engine never includes a plugin. `tools/check-plugins.mjs` (`make agents-check`)
  holds the includes to the lists; the compiler holds the rest, since a target only sees the
  include roots it links.
- **Registration is the build.** No run-time registry, no static-initializer
  self-registration, no virtual plugin interface: they cost a pointer chase on hot paths,
  static libraries drop unreferenced self-registering objects, and their order is
  unspecified, which breaks determinism. A plugin exposes plain functions and POD data; the
  game links `ge_<name>` and calls it from its own `create_game` and `step`, in the order it
  states once.
- **Skills and tags.** Each plugin owns the skill named after it. Every file in the plugin
  names that skill first in its `ge:skill` line, so `make find` and `docs/index.md` route to
  it. When the `ge` CLI (M12) syncs consumer skills, a consumer receives the skill of each
  plugin it links.
- **Labs.** A plugin's labs live in `plugins/<name>/labs/` and register through the engine's
  lab contract, the same way the engine's and a game's do; a lab that needs a game (the bot,
  the modes) belongs to the game.
- **Determinism.** A plugin's simulation half is simulation code: `make libm-check` holds it,
  and every rule in § Determinism contract applies.

## The frame

```
platform.poll()  →  input snapshot (the engine's signs: steer +right, etc.)
run_clock.frame(dt) → n steps
for n:  sim::step(state, input)                      // 120 Hz, deterministic, no allocation
extract(state, alpha) → FrameView                    // plain data, interpolated
render: prepare passes (CPU, may be parallel) → encode the pass table (fixed order) → submit → present
audio: beds read the state once a frame and steer their layers; events fire one-shots;
       the device thread pulls blocks through the synth's ring
ui:    HUD + cards drawn last
```

The simulation never sees the frame rate, and the renderer never writes `GameState`.

## Data and code conventions (for agents and for speed)

- **Plain data.** State is POD structs with fixed-capacity arrays or `std::vector`
  reserved once. There is no inheritance in engine or game code, no virtual dispatch on
  hot paths, and no hidden global state. Module state, when unavoidable (the output
  sink), lives in one `.cpp` and is named in its header.
- **No allocation in a step or a frame.** Arenas and reserved vectors are filled at
  create time. A step that allocates is a bug, and the `perf` skill says how to prove
  it does not.
- **SoA where a loop is hot.** Trees, particles and spatial-hash cells use structure of
  arrays. Everything else uses the natural struct. Change a layout only with a
  measurement.
- **One subject per file, under 1000 lines** (`agents:allow-large-file: <reason>` for
  honest exceptions). A file opens with a comment that states what it owns, in the
  engine's own terms (never where it came from: `AGENTS.md` § A clean slate).
- **Every number has a unit in its name or its comment** (`speed_mps`, `cell_m`), and
  every tuning number lives in its `defs/` table.
- **Stated once.** Each answer (the sign conventions, what the snow does, which
  generator built a map) is written in one function, and `AGENTS.md` lists where.
  A second copy is a bug.
- **One name for a module everywhere.** `plugins/mountain/mapgen/forest.{hpp,cpp}`, its
  test `tests/mountain_forest_test.cpp`, its golden `tests/golden/mountain_forest.txt`.
  Names are `snake_case`, so `create_game` is a search key for the function, its callers
  and its test.
- **Headers declare, sources define.** Small hot math is `inline` in headers. A system's
  header shows its whole API with comments, so it can be read without the `.cpp`.
- **Errors**: engine code returns status values or asserts; there are no exceptions on
  paths a frame runs.

## Diagnostics (agent-actionable output)

Every assert, error and warning the engine prints is a prompt for an agent: it names
**what** failed, **where** (file:line and the game-level context, such as seed, step
and system), and **the next action** (the test to run, the skill to load, or the doc
to read). For example:

```
ge: golden: core_math_test row 1834: hypot3 got 0x41fc7f55b85cfc98 want 0x41fc7f55b85cfc97.
    A last-bit difference means an FP contraction or a libm call crept in.
    Check -ffp-contract=off for this target; skill: determinism.
```

`engine/core/output` (`status`, `info`, `warn`, `error`, `header`, `debug`) is the one
channel every such line goes through; the host attaches the sink that prints it, and core
itself does no I/O. Tests print in the same shape through `tests/check.hpp`.

The convention is held by a test: `engine/cli/diagnostic.hpp` finds every `output::error` and
`output::warn`, `GE_ASSERT`, lab `fail` and CMake `message(FATAL_ERROR|WARNING ...)` and requires
its text to say what failed in three words or more and to carry a next-action marker (`next:`,
`skill:`, `make `, `run `, `rerun`, `see `, `docs/`). `tests/cli_diagnostics_test.cpp` holds the
whole tree to it, and `ge doctor` holds a game repo to it (the `agent-interface` skill).

## Determinism contract

- Only `core/libm` provides transcendentals to simulation code. `<cmath>`'s `sin`, `exp`,
  `pow` and the rest are forbidden in `engine/core`, `engine/loop`, `plugins/*` (bar a plugin's `app/`) and
  `games/*/sim`; `tools/check-libm.sh` (`make libm-check`, CI's `agents` job) fails a call.
- FP flags: `-ffp-contract=off -fno-fast-math` (GCC: `-fexcess-precision=standard`;
  MSVC: `/fp:strict`), set once on `ge_fp` in the root `CMakeLists.txt`
  (linked by `ge_core` and published with it as `ge::fp`, so a game built against a
  release gets them too).
- The instruction-set baseline: `ge_isa` (x86-64-v3, so AVX2, on x86-64; the default on
  ARM64), linked by `ge_core` and published as `ge::isa`. It moves no bits, because
  contraction stays off.
- Randomness only through `core::Rng` streams owned by the state. No clock in `sim/`.
- Threads only in a bake whose every output is a pure function of its index
  (`core::parallel_bands`, `engine/core/parallel.hpp`): the same bits on any number of
  workers, held by `tests/core_parallel_test.cpp` and every map digest. Never in a step.
- Iteration order is explicit. No `unordered_map` iteration feeds a result. Sorting
  uses `std::stable_sort`: equal keys keep their order, and that order is part of the
  result.
- Every lab's digest (`docs/labs.md` § Digests) is checked twice in one process and against
  `tests/labs_digests.txt` on every CI row (`labs_smoke`), so a lab is a determinism test too.
- Determinism is proven by the golden tables in `tests/golden/`, not by eye. They are the
  engine's own: a deliberate change of behaviour rewrites them in the PR that makes it.

## Decisions

| Part | Decision |
| --- | --- |
| Language | C++20, no exceptions or RTTI in engine code paths (they may stay enabled for tools) |
| Game-object model | POD state structs and explicit arrays, systems as free functions in a written order; no generic ECS, because the simulation is already a fixed set of systems over flat arrays |
| Determinism | The engine's own `core/libm` (vendored fdlibm and glibc's pow) and `core/num`, so every platform gives the same bits; FP flags in CMake; golden tests |
| Loop | A fixed 120 Hz accumulator (`engine/loop/run_clock.hpp`), interpolated rendering |
| Platform | SDL3, statically linked: one dependency for the window, input, gamepad, audio device and files on every desktop and phone |
| GPU | Dawn, native on every target (Metal, Vulkan, D3D12), through its `webgpu.h` header; no web build (`docs/rendering.md` § The backend) |
| Shaders | WGSL, embedded at build |
| Audio | Our own DSP graph on SDL3 audio: every sound is synthesized, so no middleware |
| UI | Our own immediate-mode 2D layer + SDF text; Dear ImGui for dev tools |
| Assets | glTF 2.0 through cgltf (`engine/assets`) for the few authored models, made by Blender Python builders run headless offline (`ge asset`, M8g) and committed as .glb; everything else generated in code or baked to embedded blobs |
| Built venues on a map | A venue whose features are finer than the 2 m grid (a ski jump's edge, its landing radius) answers `Level::ground_at` and `normal_at` off an ANALYTIC profile inside its width (`Level::venue`, one null check on the hot path), with the grid cut beneath it so a reader of the raw grid never sees ground above the surface |
| Real-world data | Baked offline by a lab off free public datasets that need no credentials (the 30 m elevation model's public bucket), into GENERATED C++ sources committed in the tree (a real face's heights: a base64 string of second-differenced varints, about 21 KB a face), so a build reads no file and fetches nothing; the bake is re-runnable and reproduces the tree to the bit |
| Tests | Our own header harness, CTest, golden tables |
| Build/CI | CMake + Ninja + presets; GitHub Actions on Linux x64/arm64 (GCC, Clang), macOS, Windows |

## Build

CMake ≥ 3.25 + Ninja, `CMakePresets.json` (`debug` with ASan/UBSan, `release`).
Dependencies are pinned to a tag and a SHA-256, and each one is added in the milestone that
needs it: SDL3 through `FetchContent` from its release archive (`cmake/SDL.cmake`), Dawn as its
prebuilt GitHub release (`cmake/Dawn.cmake`; Tint comes in the same archive), later cgltf
and Dear ImGui. Downloads land in `.cache/deps/` (gitignored, shared by every preset,
cached by CI). `GE_PLATFORM` and `GE_GPU` switch the platform layer and the GPU off; the
simulation, the plugins and their tests never need either, and `GE_GPU` defaults off where
Dawn publishes no archive (Linux arm64). `Makefile` names the everyday commands. CI (`.github/workflows/ci.yml`) runs format,
agents-check, lint and the test matrix, with ctest in parallel, ccache on the Linux and macOS
rows and, on a pull request, lint over only what the change reaches (the `build` skill § CI).
