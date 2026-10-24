# Agent guidance for gameengine2

This file is the single source of truth for AI coding agents in this repo. `CLAUDE.md`,
`GEMINI.md`, `.cursorrules` and `.github/copilot-instructions.md` are symlinks to it,
and `.claude/skills` and `.gemini/skills` are symlinks to `.agents/skills`.
`tools/check-agents.sh` (CI's `agents` job) checks these links.

**This file is the ROUTER, not the manual.** It says what the project is, the rules
every task follows, where things live, and which skill owns each subject. The
procedures live in `.agents/skills/<name>/SKILL.md`. Load the skill that owns your
task's subject before your first edit.

**Only agents write code here.** Optimise everything (the code, its layout, the docs and
the output it prints) for an agent that arrives with no context, has to find the one
place an answer lives, and has to change it safely.

## What this is

A C++20 game engine: fast, deterministic to the bit, and built for agents. Its first game
is **Fall Line**, a downhill skiing game, which stays in the tree as the demo game
(`games/fall_line/`). **It is not a ski engine**: generic code is in
`engine/`, what a genre or a world shares (racing, the generated ski mountain, the skier on snow) is in
`plugins/<name>/` for any game of that kind to link, and only Fall Line's own is in
`games/fall_line/` ([`docs/architecture.md`](docs/architecture.md) § Plugins). **The goal spec and the roadmap are
[`docs/goals.md`](docs/goals.md).** Every task serves one of its milestone rows and
updates that row's status file (`docs/goals/<row>.md`) in its PR.

| Doc | What it holds |
| --- | --- |
| [`docs/goals.md`](docs/goals.md) | The long-term goals (G1–G9), the non-goals and the milestone roadmap |
| [`docs/architecture.md`](docs/architecture.md) | The layers and their dependency direction, the frame, the code conventions, diagnostics, the determinism contract and the build |
| [`docs/labs.md`](docs/labs.md) | The labs: the contract every subsystem, plugin and game plugs into, the kinds (sim, scenario, picture, drive, bench), captions, digests, and the labs each milestone lands |
| [`docs/debugging.md`](docs/debugging.md) | Debugging a run: the log channels and the log spec, log dumps, series traces and spans files, the outlier rule, the charts and `ge graph` |
| `docs/index.md` | GENERATED from the code's `ge:` tags by `make index` (and `make agents-check`) in every checkout, never committed: every file's one-line summary, owning skill and test; keyword to files |
| [`docs/rendering.md`](docs/rendering.md) | The GPU backend (Dawn), the frame's pass table, every rendering technique and why, budgets, lab hooks, and what was considered and not taken |
| [`docs/perf.md`](docs/perf.md) | The performance ledger: every speed-up, before and after, and how it was measured |
| [`docs/input.md`](docs/input.md) | Input devices: the layers, the model, actions and bindings, raw mouse, wheels and force feedback, gamepads, touch, players, recording, the labs and the decisions |
| [`docs/camera.md`](docs/camera.md) | The camera: the pose, the followers, the subject, the rigs (first person, boom, mounted, orbit, planted, free), the modifiers, the world's queries, the transitions, the stack, the cvars, the labs and the research |
| [`docs/movement.md`](docs/movement.md) | Character movement for any genre: the knobs and presets, the step's model (friction, the acceleration rule and strafe jumping, jumps, steps, slopes, crouch, slide), the world's trace, cvars, what a camera reads, water, the labs |
| [`docs/networking.md`](docs/networking.md) | Multiplayer: what each kind of game asks of the network, the layers of `engine/net`, the decisions, the wire format, the simulated network, the tools, and Fall Line's pilot (M17) |
| [`docs/publishing.md`](docs/publishing.md) | What a release is (GitHub Releases + GitHub Packages), how a game consumes it, changeset fragments, cutting a release, the credentials |
| [`docs/replays.md`](docs/replays.md) | Replays, recording and broadcasting: a run kept as its inputs, keyframes and the seek, the replay file, the director, video with sound, streaming to a video service, spectating by inputs, Discord presence (M23) |
| [`docs/shooters-edge.md`](docs/shooters-edge.md) | The second game, Shooter's Edge (M18, after Fall Line): the compatibility contract, the clean room, the homes, the protocols, the labs and the `ge` commands it needs |
| [`docs/water.md`](docs/water.md) | Water and wind (M24): the bodies and the one query, the waves the wind grows, floating, swimming tuned per game, flowing water, the colour from what is in the water, the surface and the view under it, the sound, and the labs |
| [`docs/liquids.md`](docs/liquids.md) | Liquids other than water (M30): viscosity, yield and harm on a body's stuff, the wind and the swimmer in them, lava and slime drawn, and the labs |

**The working area.** `porting/` holds everything that only matters while modules are
being carried over from their reference sources: the porting guide (`porting/README.md`),
the research behind it, the porting map (`porting/map.md`: which reference module each
engine file reproduces), the golden-table generators (`porting/parity/`) and their make
targets (`porting/porting.mk`). It is a WORKING area, not documentation: it is deleted when
porting ends, and nothing outside it may depend on it except `make golden`.

## Rules for every task

- **A CLEAN SLATE: NO LEGACY REFERENCES.** The engine and its games stand on their own; they
  are not a continuation of the code they were carried over from. Outside `porting/`,
  nothing (code, comments, `ge:` tags, docs, skills, lessons, this file, the Makefile,
  CMake, CI, commit messages and PR titles) names the reference repositories or kit, the
  reference language or its files and identifiers, or treats the reference as the
  authority, and no file is described by where it came from; nothing justifies a choice by
  what a reference runtime does. Say what the code does and why in
  the engine's own terms: "rounds halves toward +inf", "held bit-exact by
  `tests/golden/core_num.txt`". The link from an engine file to its reference is a row in
  `porting/map.md`; porting procedure and research go elsewhere in `porting/`. `make
  agents-check` scans every tracked file outside `porting/`, the `port` skill and
  `tests/golden/` and fails on a reference name (`tools/agents-index.mjs`, `LEGACY`).
- **C++ ONLY, AND NO INHERITED ARCHITECTURE.** The engine and every game on it are written in
  C++. The only other languages allowed are the thinnest platform glue a store demands
  (Swift or Objective-C for Apple's app shell, Java or Kotlin for Android's), kept as small as
  the platform allows, the small Node scripts under `tools/` that check the
  repository's structure and ship in no game, and Blender Python asset builders: offline
  sources of a model, run headless by `ge asset` (docs/goals.md M8g), whose output (a .glb)
  is committed and loaded by the engine in C++; they never run in a game, CI never needs
  Blender, and no other scripting enters the tree. The one runtime exception is
  Shooter's Edge's QC VM (`plugins/progs`, `docs/shooters-edge.md`), which executes existing mods'
  bytecode for compatibility; the tree's own QC is limited to that game's test mod. Nothing about the reference sources'
  architecture (their layers, module split, file layout, state shape, names or data
  formats) is assumed to carry over: each design decision is made here, for this engine,
  and recorded in `docs/architecture.md`. Where a carried-over module keeps a behaviour (a
  golden pins its bits), that is a decision about the result, not about the structure
  around it.
- **WRITE ONLY TO THIS REPOSITORY.** The reference sources (`porting/README.md` names
  them) are read-only: clone them beside this checkout, read them and run them, but never
  commit, push, open issues or comment there. If a reference has a bug, the engine
  reproduces it and you tell the user.
- **THIS REPOSITORY IS PUBLIC.** Nothing sensitive goes in the tree, in workflow files or
  in repository variables: no tokens, keys, passwords, e-mail addresses, account or
  device ids, or paths under a home directory. Credentials live only in **GitHub
  Actions secrets** and are referenced as `${{ secrets.NAME }}`. Locally they come from
  the environment or a gitignored `.env`. Workflows request the narrowest
  `permissions:` they need, map a secret into `env:` (never interpolate it into a
  `run:` script) and never print one. Actions **variables** and workflow logs are public
  too. An agent never handles a credential's value: a maintainer enters secrets in the
  repository settings. Releases need only the built-in `GITHUB_TOKEN`
  ([`docs/publishing.md`](docs/publishing.md) lists every credential). Write every
  comment, doc and commit message for a stranger reading it.
- **ENGINE, PLUGIN OR GAME** (G8). Before the first edit, decide each file's home with the
  `plugins` skill: any genre would use it → `engine/`; any game of one genre or world →
  `plugins/<name>/`; only this game → `games/<game>/`. Dependencies run game → plugin →
  engine; `make agents-check` holds them.
- **ONLY WHAT THE GAME NEEDS** (G4). Before you build a component, name the Fall Line
  feature it serves. A general-purpose feature with no user is out of scope.
- **FAST, AND PROVEN FAST** (G2). Hot paths do not allocate, branch on virtuals or chase
  pointers. A performance claim carries a before-and-after measurement in the PR
  (`perf` skill).
- **DETERMINISTIC ON ITS OWN TERMS** (G1). The simulation produces the same IEEE 754
  doubles for the same seed and inputs on every compiler and OS, so replays, ghosts and
  tests reproduce everywhere; the golden tables in `tests/golden/` are the engine's own
  record of those bits. Matching a reference implementation's bits is NOT a requirement: a
  deliberate change of behaviour rewrites the affected goldens in the PR that makes it, and
  says why. No `<cmath>` transcendentals in simulation code, no FMA contraction and no
  fast-math. Randomness only from `core::Rng`. Every simulation module lands with a golden
  test (`determinism` and `testing` skills).
- **LABS, NOT GUESSES** (G9). Every subsystem, plugin and game has labs (`docs/labs.md`).
  A change in a lab's subject runs it before and after (`--tag=before`, then
  `--compare=before`) and puts both in the PR. A milestone row is done only when the labs
  in its **Labs it lands** cell run in `make labs-smoke` (`labs` skill).
- **AGENT-ACTIONABLE OUTPUT** (G6). Every error, assert and log line says what failed,
  where, and the next action to take (`docs/architecture.md` § Diagnostics).
- **GREEN, THEN MERGE** (G7). CI must pass on every row. A PR you open is yours to drive
  to green, and you merge it (squash) once it is green. Never skip or weaken a test to
  get there.
- **WHEN A TASK IS DONE, NAME THE NEXT STEP.** Update the roadmap row's status file
  (`docs/goals/<row>.md`), then name the next open milestone row as the follow-up.
- **SHOW THE WORK IN PICTURES.** Every progress report and every finished PR comes with
  pictures: a lab's screenshots, before and after side by side, a chart of the numbers, a
  diagram of a design, or a table of measurements drawn as an image. Nearly all work can be
  shown this way; the labs exist to make that cheap (`labs` skill). Put the pictures on one
  page linked from the report and the PR, never as loose attachments.
- **SAY WHAT `ge` LACKED.** `ge` is the agent's whole interface (G6), so it grows from what
  agents miss. When a task would have been easier with a `ge` command or flag that does not
  exist (to build, debug, inspect, measure or optimise), say so in the PR description under
  **ge wishlist** and to whoever directs the work: the command, what it would answer, and the
  steps it would have saved. Check `ge help` and the open rows in `docs/goals.md` first, and
  add a wish that recurs to `docs/goals/ge-backlog.md`, one line under its group.
- **SKILLS LEARN.** Read each loaded skill's lessons before the first edit, and before the
  commit record what the session learned, fix what the skill got wrong, and promote what
  is always true (the `skill-reflection` skill). Lessons ship in the same PR as the work.
- **THE MAIN ENVIRONMENT IS LINUX WITH NO GPU.** Every test, lab, scenario and screenshot
  harness runs headless in a GPU-less Linux container, rendering in software. The
  secondary environment is an Apple M3 Max on macOS (Metal), used only for what the
  container cannot do (GPU timings, Metal or real-GPU-only bugs, interactive play;
  `docs/rendering.md` § Where it is developed). When a task needs the Mac, say so to the
  user and open a GitHub issue labelled `needs-mac` with the exact commands, what to look
  at and what to paste back. Never do that because the Mac would only be faster.
- **`make precheck` BEFORE EVERY PUSH.** It runs, in about 40 seconds, the gates that cost
  threads the most CI rounds (fmt-check, agents-check, libm-check, lint on the changed files,
  `cli_diagnostics_test`, the tests the change reaches and the smoke digests of the labs it
  reaches, `ge digest --affected`). A game repo's is `ge precheck`.
  Run it in the background when it is long (a tool's background mode re-invokes you when it
  exits); never poll it in a loop.
- **AFTER PUSHING A PR, CHECK IT EVERY 15 MINUTES UNTIL IT IS MERGED.** Each check-in looks at
  the checks AND the merge state, since a branch left behind main is where threads get stuck:
  `build/<preset>/ge ci <pr>` answers both in one command. A conflict: merge `origin/main`,
  resolve it, `make precheck`, push. A red check: fix it and push. `main` merges only through
  GitHub's merge queue: right after each push, arm squash auto-merge on the PR (`gh pr merge
  <pr> --auto --squash`), which puts it in the queue once its pull-request checks pass; the
  queue then runs the whole matrix on the tree that lands and squashes it, or ejects it with
  the failing row named (fix, push, it re-queues). A direct merge is refused. The check-ins
  still catch conflicts, which auto-merge waits on forever. Schedule the check-ins; never sleep in a loop.
- **Every work session ends with the `commit` skill.**

## Build and test

```sh
make build        # cmake --preset debug && cmake --build (PRESET=release for -O2, no sanitizers)
make test         # build + ctest;  make test T=cli_ builds and runs only the tests the regex names
make fmt          # clang-format in place;  make fmt-check is what CI runs
make precheck     # BEFORE EVERY PUSH: fmt, agents, libm, fragments, build, lint, the tests and the labs' digests the change reaches (~40 s, more when it reaches labs)
make lint         # clang-tidy's bug-finding checks, in parallel (BASE=origin/main: only what the change reaches)
make labs         # list the labs;  make lab LAB=level ARGS="--seeds=38" runs one
make lab LAB=synth # every voice the synth can play, metered, pictured and written as WAV
make lab LAB=audition # Fall Line's bank, the skier's beds and the bot's run, metered and written as WAV
make lab LAB=trace ARGS="--seed=38 --log=course=debug" # one run step by step: trace.tsv, charts, log.txt
make lab LAB=anim ARGS="--scenario=turns" # the skier's figure frame by frame: filmstrips, onion skins, joint faults
make lab LAB=world ARGS=--diff=before # the last run's pictures against the kept run's, per distance band
make sim SEEDS=38 # the bot down a seed: the run report table and its line on the map (the sim lab)
make sim ARGS=--rivals=3  # the same with a field of four: the field table, every line drawn
make play         # an agent at the controls: the play protocol on stdio (engine/play/session.hpp)
build/debug/play why runs/fall_line-38.inputs --step=1200 --around=6   # why the bot did it (ge why)
build/release/play bench --seeds=38 --rivals=3   # Fall Line's step timed per zone (ge bench's engine half)
make labs-smoke   # every lab at its smoke flags, twice, digests against tests/labs_digests.txt
make agents-check # symlinks, skills ↔ this router, the 1000-line cap, ge: tags (writing docs/index.md), lesson fragments
make index        # write docs/index.md from every file's ge: tags (gitignored: never committed)
make find Q="noise sample_field"  # files, skills, lessons and the porting map matching any word
make lessons ARGS="port --scope=engine/core"   # read a skill's lessons, filtered (no ARGS: the inventory)
make ge ARGS="new ../snow-racer"  # the ge command line (ge help): scaffold a game repo, then grow it with ge alone
make ge ARGS="asset build skier"  # a model rebuilt in headless Blender off the game's data (GE_BLENDER: a blender, or a python with bpy)
make golden       # regenerate the golden tables (working area: porting/porting.mk, porting/README.md)
make golden-libm  # the golden tables that reach a transcendental (same place)
make libm-check   # no <cmath> transcendental in simulation code
make release-check # the changeset fragments valid + tools/release.py's tests; prints the next version
make shaders-check # every embedded WGSL shader compiled by Tint offline (needs GE_GPU)
```

**A cloud session starts the build itself.** `.claude/settings.json`'s SessionStart hook runs
`tools/session-start.sh`, which installs CI's Linux packages the container lacks and runs `make
build` (the debug preset, reusing `build/debug/`) in the background while you read. Before you
rely on the build, `tools/session-start.sh --wait` blocks until it is done and exits with its
result; the output is `build/session-start.log`, the one-line state `build/session-start.status`.

CI (`.github/workflows/ci.yml`) is tiered, because the org's runners are a small shared pool. A pull
request runs the gates (fmt-check, agents-check, libm-check, release-check, the changeset rule),
lint and shaders-check, and ONE build-and-test row (Linux x64, GCC debug+ASan); the merge queue
runs the whole matrix on the tree that lands: Linux x64 (GCC debug+ASan, GCC release, Clang
release), Linux arm64 (GCC), macOS (Apple Clang), Windows (MSVC), and `packaging.yml` (the
installed package building a consumer on Linux, macOS and Windows, and `ge new` growing a repo).
A change to prose alone (docs, skills, lessons, root Markdown, fragments) runs the gates only;
the label `full-ci` runs everything on a pull request. The ruleset requires one check,
`required`, which passes when every job passed or was skipped by those rules. A golden failure
on one row points at that row's compiler or libm. The Linux x64 rows install Mesa's lavapipe and
run the tests under a virtual X display with `GE_GPU_REQUIRE=1`, so the GPU and window tests run
there and a skip fails; elsewhere they run when the runner has an adapter and skip (CTest's exit
77) when not. A shipped change carries a `.changes/unreleased/` fragment (or the `no-changelog`
label). Releases are dispatched, never hand-made (`release` skill,
[`docs/publishing.md`](docs/publishing.md)).

## Where things live

Each row names a directory and what it is for, never its files: a file's entry is its own
`ge:summary` and keywords, read with `make find Q="<words>"` or the generated `docs/index.md`
(`make index`). A pull request edits a row only when it adds a directory or changes what one is
for, so parallel pull requests do not collide here; `make agents-check` holds each row to 240
characters.

| Path | What | Skill |
| --- | --- | --- |
| `engine/core/` | The deterministic pool: the PRNG, the math, the vendored libm, number semantics and text, noise, heightfields, quaternions, the sun and moon, the log channels, parallel bakes | `determinism`, `debug` |
| `engine/loop/` | The fixed-step run clock between the display's frames and the 120 Hz step | `sim` |
| `engine/lab/` | The lab contract and runner (`ge::lab`): metrics, tables, captioned shots, digests, reports, the CPU raster and PNG, picture diffs, series, stats, charts, pose tracks; `labs/` the engine's own labs | `labs`, `debug` |
| `engine/play/` | The play contract (`ge::play`): a game's spec and run, the recorded session and its protocol, input logs, shots, recordings, the bench, and the `play` executable's command line | `agent-interface` |
| `engine/prof/` | The profiler (`ge::prof`): zones, allocation counts and the counting allocator, the peak resident set; feeds no digest | `perf` |
| `engine/ai/` | The bots' layer (`ge::ai`): the decision record `ge why` reads, the hands that look human, paths, thinking on a schedule, sight and memory, steering, choosing | `bots` |
| `engine/wind/` | The wind (`ge::wind`): a pure function of place and time over a field (mean, gusts, veer, the log-law profile, shelter, local sources); a world's wind is one field of it | `water` |
| `engine/water/` | The water (`ge::water`): bodies and the one `sample` query, the sea a wind grows over a fetch, floating, swimming, the colour from what is in the water, flowing water and its tiers | `water` |
| `engine/cli/` | The `ge` command line for game repos (`cmake/Cli.cmake`): the command catalog, the answer shape and findings, and one file per command group | `agent-interface` |
| `consumer-skills/` | The skills a game repo gets from `ge new` and `ge skills sync`, compiled into `ge`; written for a game repo, never for this one | `agent-interface` |
| `scaffold/` | The game repo `ge new` writes, as `.in` files with placeholders, compiled into `ge` | `agent-interface` |
| `templates/` | What `ge add` writes (a system, a test, a lab of each kind) and the starting games of `ge new --game=<name>`, compiled into `ge` | `agent-interface` |
| `*/assets/<name>/` | Built models (`ge asset`): the manifest, the Blender builder, the committed .glb and its data | `assets` |
| `*/labs/` | Labs beside their subject, compiled into the one `labs` executable | `labs` |
| `engine/console/` | The console (`ge::console`, `docs/console.md`): cvars and their tiers, the console language, aliases, binds, cfg files, the drop-down console's model | `console` |
| `engine/replay/` | Replays (`ge::replay`, `docs/replays.md`): a run watched again off its input log, the keyframes a seek starts from (the game's `Run::clone`), the player's seek in a step budget, the transport | `replay` |
| `engine/net/` | Multiplayer networking (`ge::net`, `docs/networking.md`): addresses, the wire, UDP and TCP sockets, links, the simulated network, sessions, LAN discovery, replication; the only place socket headers are included | `net` |
| `engine/move/` | Character movement (`ge::move`, `docs/movement.md`): the knobs and presets, the step over a trace callback, the box world, the strafer, the scenarios | `move` |
| `engine/input/` | Input devices (`ge::input`, `docs/input.md`): the device model, events, state, dead zones, actions and bindings, rebinding, wheels' roles, touch, players, feedback, the event log, simulated devices | `input` |
| `engine/camera/` | The camera (`ge::camera`, `docs/camera.md`): poses, followers, the rigs (first person, boom, mounted, orbit, planted, free), shake, transitions, the stack, the scripted subjects | `camera` |
| `engine/platform/` | SDL3 (`GE_PLATFORM`): the window, input, the frame clock, the audio device; the only place SDL is included | `platform` |
| `engine/gpu/` | The native GPU device over Dawn (`GE_GPU`): adapters and tiers, pipelines and the manifest, surfaces, targets, timers; the only place webgpu.h is included | `gpu` |
| `engine/render/` | Generic rendering mechanisms (`docs/rendering.md`): the frame and its pass table, sky, terrain, shadows, Hi-Z, TAA-U, bloom, post, meshes, instances, skinning; WGSL beside each | `render` |
| `engine/audio/` | The synth (`ge::audio`): the voice vocabulary, the DSP graph, sounds and racks, the meter, WAV; deterministic, no platform | `audio` |
| `engine/assets/` | Authored models: the glTF loader and writer, the `Model`, posing and CPU skinning; no GPU | `assets` |
| `engine/ui/` | The 2D layer (`ge::ui`): paints, the draw list, images, fonts, the CPU raster, backdrops, menus as a model and a view; no GPU | `ui` |
| `plugins/` | Plugins, one directory each, built as `ge_<name>` (`cmake/Plugins.cmake`, checked by `tools/check-plugins.mjs`) | `plugins` |
| `plugins/racing/` | The racing primitives (`ge::racing`): the control tape, the record book's policy, the standings | `racing` |
| `plugins/mountain/mapgen/` | The world generator (`ge::mountain::mapgen`): the piste and the ski area by stage, the rule books, the real faces and hints, the digest; `picture/` the map on the CPU | `mountain` |
| `plugins/mountain/lifts/` | The lifts run (`ge::mountain::lifts`): every carrier round its loop as a pure function of the clock | `mountain` |
| `plugins/mountain/air/` | The air at run time (`ge::mountain::air`): the wind's gusts and veer, the fall's squalls and the visual range, as pure functions of the map and the clock | `mountain` |
| `plugins/mountain/app/` | The mountain as drawn (`ge_mountain_app`, with the GPU): the snow's material, the trail map, the trees, the weather's look | `mountain`, `render` |
| `plugins/mountain/analysis/` | The rule book re-checked on a finished map (`ge::mountain::analysis`) | `mountain` |
| `plugins/skier/` | The skier on the snow (`ge::skier`): the step, the snow, the limits, the skis and tuning, the trees, the body and its injuries, the hand-built maps, the ride scenarios | `skier` |
| `plugins/skier/app/` | The skier as seen and heard (`ge_skier_app`): the rig and figure, the wind and snow beds, the tracks | `skier`, `audio` |
| `plugins/snowmobile/` | The snowmobile on the snow (`ge::snowmobile`): the spec, the step, the body, the drive, the bot's hands, the scenarios | `snowmobile` |
| `plugins/helicopter/` | The helicopter in the air and on the snow (`ge::helicopter`): the spec, the step, the bot's hands, the downwash, the scenarios | `helicopter` |
| `plugins/paramotor/` | The powered speed wing a skier flies (`ge::paramotor`): the spec, the wing on its lines, the engine, the acro, the bot's hands, the scenarios | `paramotor` |
| `games/fall_line/sim/` | Fall Line's own simulation (`fall_line_sim`): the state, the step, the course, the free ride and lifts, the bot and the field, the headless run, the hunt, the play contract | `sim` |
| `games/fall_line/cfg/` | Fall Line's cvars (`fall_line_cfg`): every setting a cfg may change, bound to the Settings its app reads | `console` |
| `games/fall_line/sound/` | Fall Line's sound (`fall_line_sound`, no GPU): the bank, the route, the listener, the mix | `audio`, `sim` |
| `games/fall_line/camera/` | Fall Line's camera (`fall_line_camera`, no GPU): the ladder of engine rigs and the camera key, the skier as the subject, the trunks and posts it answers | `camera` |
| `games/fall_line/labs/` | Fall Line's labs, the headless ones and the GPU's | `sim`, `labs`, `audio` |
| `games/fall_line/app/` | Fall Line's presentation (`fall_line_app`): the pass table, the look, the scene, the views, the `fall_line` window; then the HUD | `platform`, `gpu`, `render`, `ui`, `audio` |
| `tests/` | `<topic>_test.cpp`, one executable each; `check.hpp` (the harness), `golden.hpp`, `golden/` | `testing` |
| `porting/` | The WORKING AREA (above): the porting guide, the porting map, the golden-table generators. Deleted when porting ends | `port` |
| `tools/` | The repository's checks and scripts: `check-agents.sh`, `agents-index.mjs` (the `ge:` tags, `docs/index.md`, `make find`), `skill-lessons.mjs`, `precheck.sh`, `lint.sh` | `write-code` |
| `CMakeLists.txt`, `CMakePresets.json`, `cmake/`, `Makefile`, `.github/workflows/` | The build and CI | `build` |

### Finding things

Agents find code by grep, so the tree is built to be grepped:

- **Every source file ends its leading comment with a `ge:` tag block**: `ge:summary` (one
  line), `ge:keywords` (lower-case concepts in the engine's words, kebab or snake case),
  `ge:skill`, and `ge:test` where it applies. The format is the `write-code` skill's §
  Discovery tags. `make agents-check` fails an untagged file, an unknown skill, a missing
  test path, a keyword with a capital, and the retired tag that named a file's reference.
- **`make find Q="word word"`** searches every file's tags, every skill's description,
  every lesson's title, scope and concepts, and the porting map's rows at once (so a
  reference identifier still finds the engine file while porting lasts). `grep -rn "ge:keywords.*<word>"` is the
  same search for files alone. A search that misses is fixed by adding the word you tried to
  the right file's `ge:keywords` (`skill-reflection`, question 6).
- **`docs/index.md`** is generated from the tags by `make index` (and by `make agents-check`) in
  your own checkout. It is gitignored, so it is never committed and never conflicts: run
  `make index` to read it, or `make find` to search without it.
- **One name, everywhere**: a module `engine/loop/run_clock.hpp` has its test
  `tests/loop_run_clock_test.cpp` and its golden `tests/golden/loop_run_clock.txt`; a plugin's
  test is `tests/<plugin>_<file>_test.cpp`. Names are `snake_case`.

### Stated once

- **The FP flags**: `ge_fp` in the root `CMakeLists.txt`, linked by `ge_core` (so by every
  target) and published with it as `ge::fp`. **The instruction-set baseline** (AVX2 on
  x86-64): `ge_isa` beside it, published as `ge::isa`. `ge_flags` is the tree's own warnings and
  sanitizers, never published.
- **The plugin contract** (homes, layout, naming, dependencies, registration):
  `docs/architecture.md` § Plugins; built by `cmake/Plugins.cmake`, checked by
  `tools/check-plugins.mjs`.
- **World conventions**: the plan is x/z with y up; heading 0 = +z,
  clockwise from above; pitch tips-up positive; roll right-side-down positive; `steer`
  and `edge` positive to the right. Stated in `engine/core/math.hpp`'s header.
- **The PRNG**: `engine/core/prng.hpp`. Nothing else makes random numbers.
- **The transcendentals**: `engine/core/libm/libm.hpp` says which vendored upstream each
  function is (fdlibm, and glibc's pow). Nothing in simulation code calls `<cmath>`'s.
- **Number semantics** (rounding, `to_int32`/`to_uint32` wrap, `imul`, NaN-aware
  `max`/`min`, `sign`, `mod`): `engine/core/num.hpp`; numbers as text (`to_fixed`,
  `to_string`): `engine/core/num_text.hpp`.
- **The test harness**: `tests/check.hpp`. Bit comparison is `CHECK_BITS`.
- **The audio rate and format**: `kRate` (48 kHz) in `engine/audio/voice.hpp`, interleaved stereo
  float; the device converts to the hardware. The faders are the synth's buses (`bus_gain`).

## Commit and PR conventions

- Conventional commits (`feat(core): …`, `fix(render): …`, `docs: …`, `ci: …`), scoped by
  directory. PRs are squash-merged, and the PR title is itself a conventional subject.
- A PR states which milestone row it serves and includes its golden or perf evidence.

## Skills

Skills live in `.agents/skills/`, each one a `SKILL.md` with `name` and `description`
front matter and a `.lessons/` directory (`skill-reflection`). `.agents/skills/README.md`
lists them by group, and `tools/check-agents.sh` checks that this table, that README
and the directories agree.

| Skill | Load it when |
| --- | --- |
| `start-work` | Before any task: a clean tree, synced with `origin/main`, the milestone row named |
| `write-code` | On every code change: file shape, naming, comments, the edit loop, the line cap |
| `commit` | To end every session: the gates, the commit, the push, the PR, driving it to green, merging |
| `skill-reflection` | At the start and end of a session: read and record a skill's lessons |
| `determinism` | Anything in `engine/core`, any floating-point or RNG question, a golden failure |
| `port` | Carrying a module over from its reference source (the working area's skill: `porting/`) |
| `testing` | Writing a test, a golden table or a golden generator |
| `build` | CMake, presets, dependencies (`FetchContent`), compiler flags, CI workflows |
| `release` | A changeset fragment, `cmake/Packaging.cmake`, a public target, cutting or dry-running a release |
| `plugins` | Deciding where code lives (engine, plugin or game), adding a plugin, moving code between homes |
| `racing` | `plugins/racing`: the control tape, the record book's policy, the standings |
| `mountain` | `plugins/mountain`: the generated ski mountain (mapgen, analysis, `level_digest`) |
| `skier` | `plugins/skier`: the skier on the snow (`step_skier`, the snow, the limits), the body and its injuries, the hand-built maps, the ride scenarios |
| `snowmobile` | `plugins/snowmobile`: the snowmobile on the snow (`step_sled`, the body, the drive), what throws its rider, the bot's hands on it, the sled scenarios |
| `helicopter` | `plugins/helicopter`: the helicopter (`step_heli`, the rotor, the disc, the landing, the strikes), the downwash, the bot's hands on it, the heli scenarios |
| `paramotor` | `plugins/paramotor`: the powered speed wing a skier flies (`step_para`, the wing on its lines, the engine, the fold, the cut-away), the air it flies in, the bot's hands on it, the para scenarios |
| `sim` | `games/fall_line/sim`: `create_game`, the step, the bot, the headless sim CLI, the run clock |
| `platform` | `engine/platform`: SDL3, the window, input, the gamepad, the audio device, files |
| `move` | `engine/move`: a moving character's knobs, presets and step (ground and air acceleration, friction, jumps, strafe jumping, steps, slopes, crouch, slide, flight), the trace a world answers, the box world, the strafer, the scenarios, the `move-*` labs; the M28 rows |
| `input` | `engine/input`: keyboards, raw mice, gamepads, joysticks, wheels and pedals, touch; actions, contexts, the bindings file, rebinding, the device database, players and hot-plug, rumble and force feedback, the event log, the simulated backend; `play/devices`; the M19 rows |
| `camera` | `engine/camera`: the pose and its roll, the followers, the subject and the world's queries, the rigs (first person with bob, roll and kick; the boom; the mounted head; orbit, planted, free), shake and comfort, cut, blend and inertialize, the camera stack, the scripted subjects and the camera labs; the M26 rows |
| `gpu` | `engine/gpu`: the native GPU device over Dawn (Dawn's `webgpu.h`), feature tiers, buffers, pipelines and the manifest, WGSL and `make shaders-check` |
| `render` | A draw pass, the pass table, GPU-driven instances, shadows, sky, TAA-U, post, lab captures, extraction from `GameState` |
| `assets` | `engine/assets`: an authored model (glTF), posing and skinning, the `models` lab, exporting from Blender |
| `audio` | The synth, a voice, a bed, the audio thread |
| `ui` | The 2D layer, text, fonts, backdrops, menus, the HUD, a card, the dev UI |
| `labs` | Writing, running or reading a lab: a measurement, a captioned screenshot, a scenario, a drive, a bench; the before-and-after a PR owes |
| `debug` | Finding out why a run behaves as it does: log channels and `--log` dumps, series traces and spans, the outlier rule, the charts (PNG + SVG), `ge graph` |
| `perf` | Any optimisation, a benchmark, a profile, a cost claim |
| `agent-interface` | Diagnostics wording, consumer skills, the `ge` CLI (M12) |
| `water` | Water and wind: `engine/wind` (the field, gusts, the log-law profile, shelter, local sources), `engine/water` and its passes as M24's rows land (bodies, waves, floating, swimming, colour, flow), their labs |
| `bots` | Anything a bot decides: `engine/ai`, the decision record every bot writes, `Run::trace`, `ge why`, making a bot more human, paths and thinking on a schedule (M16) |
| `console` | `engine/console`: a setting (cvar) added or named, its tier, cfg files and the console language, aliases, scripting, binds, the drop-down console, `ge cvar` and `ge cfg`, the `console` lab (M21) |
| `replay` | `engine/replay`: watching a run again: a game's `Run::clone`, the keyframes and their thinning, the player's seek in a step budget, the transport; later the replay file, the instant replay, the director, spectating by inputs and the video export (M23) |
| `net` | `engine/net`: multiplayer networking: sockets, the simulated network and its conditions, captures, the wire format, connections and channels, nodes up to 32, sessions (lobby, teams, chat, kick, reconnect), LAN discovery, NAT traversal (STUN, the rendezvous, hole punching, the relay, a port opened by PCP, NAT-PMP or UPnP), fake traffic, the `net-link`, `net-session` and `net-reach` labs; the M17 plan |
