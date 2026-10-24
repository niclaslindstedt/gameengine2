---
name: perf
description: "Use for any optimisation, benchmark, profile or performance claim in gameengine2. The budgets, how to measure CPU and GPU cost, the proofs a speed-up owes (same bits, before/after), and the ledger."
---

# Performance

The engine exists to be fast (G2). Speed is proven by measurement.

## Budgets

- **Step**: well under 1 ms of one core for a race step at 120 Hz (the player plus
  three rivals).
- **Level build**: well under a second a seed on one core (`generate_level`, measured
  per seed), and never slower than the last release.
- **Frame**: `docs/rendering.md` § Measurement: the top of the picture ladder at 1440p
  in 8.3 ms of GPU on the reference desktop (M3 Max), the middle of it in 12 ms of GPU
  on a recent phone or tablet (thermal headroom under 60 Hz), and the renderer's CPU
  half under 1 ms.
  Timestamp queries per pass give the GPU numbers.
- **GPU claims** carry per-pass GPU ms, draws and instances before and after, from the
  frame's stats on a real GPU. The development container renders in software, so its GPU
  numbers say nothing about speed: a GPU timing claim is measured on the Mac through a
  `needs-mac` issue (`docs/rendering.md` § Where it is developed).

## Measuring

- **A game's step**: `ge bench`, `ge profile`, `ge mem` and `ge leaks` (`engine/cli/perf.hpp`
  over the play executable's `play bench`, `engine/play/bench.hpp`): the pilot plays a seed once,
  its log is replayed `--repeats` times with every zone recording, and a step's time is its
  median over the repeats. `--tag=before`, then `--compare=before`, for the PR. The release
  preset is their default; a debug build's sanitizers multiply every time.
- **Zones** (`engine/prof/zones.hpp`): `GE_ZONE("name")` at the top of a scope, one per system
  in `step` (Fall Line: `bot`, `skier`, `course`, `meet`, `tape`, `hash`), per heavy part of a
  run's open (`mapgen`, `mapgen.attempt`, `mapgen.analyze`) and per part of a frame's prepare
  (`frame.sky`, `frame.terrain`, `frame.figures`, `frame.instances`, `frame.shadows`). Never in
  an inner loop: a bench keeps 24 entries a step. Off, a zone is one relaxed atomic load.
- **Allocations**: an executable that links `ge::prof_alloc` (the play executable does, through
  `ge::play`) counts every `operator new`; `ge mem` names the zone a step allocates in after the
  first second. `ge leaks` runs the pilot under the debug build's LeakSanitizer (or valgrind).
- **The frame**: the game's `profile` lab: GPU and CPU ms per pass, the prepare's zones and the
  simulation's between frames, and `frames.tsv` a frame (`ge profile --render`, `make lab
  LAB=profile`).
- **How the run uses the machine** (does most of the CPU sit idle, what holds a frame up): `ge
  load --seeds=38` (`engine/play/load.hpp`) replays the pilot's run paced at the game's rate with
  every thread's timeline (`engine/prof/timeline.hpp`: `GE_WORK`, `GE_WAIT`, `GE_SPIN`; the
  worker pool's bands and waits are heard on their own) and the OS sampler beside it
  (`prof/cpu_sampler.hpp`: each thread's CPU time, each core's busy share); `--render` adds the
  game's `gpu-load` lab, the frame loop with a `gpu` lane. `prof/load.hpp` folds them into the
  efficiency (work over hardware threads x time), the threads working at once, each frame's
  critical path (its own work, then each wait laid on the lane that ended it) and the ideal (the
  work spread perfectly); the findings say what bounds the median frame. Name every thread the
  engine starts (`core::name_this_thread`) and mark its work, or it shows only as an unmarked
  OS thread. On a software adapter the GPU's work is the rasterizer's CPU threads: real GPU
  times need a real adapter (`docs/perf.md` § The load monitor).
- An engine module's own cost: a `bench` lab (below), median of N runs (N ≥ 20), on an idle
  machine, back to back before and after.
- In the GPU-less container (no `perf` there), callgrind ranks where the instructions go:
  `valgrind --tool=callgrind build/release/labs mapgen-cost --seeds=38 --reps=1 --version=10`,
  then `callgrind_annotate --inclusive=yes`. Its counts rank; wall time from the bench lab
  proves.
- While porting, `porting/subsystems.md` § perf says how to time the reference on the
  same machine.

## The proofs a speed-up owes

1. **Same bits.** Every golden test is still green. An optimisation that changes a
   digest is a bug, not a trade-off.
2. **Before and after**, same machine, same build type, with the numbers in the PR.
3. **No allocation added** to a step or a frame: `ge mem` (or `play bench`'s
   `allocs.steps_allocating`) is 0 after the warm-up, or the PR says which zone still
   allocates and why.

## Levers, in the order to try them

1. Algorithmic: a spatial hash instead of a scan, bake once instead of per step
   (the level's compile step), early-out on the nearest ring first (the heightfield's
   block offsets).
2. Memory: SoA for hot arrays, contiguous storage, no pointer chasing, reserve up
   front.
3. Parallelism: only where the result does not depend on scheduling (per-pass
   `prepare`, independent rows of a bake: `core::parallel_bands`, on a persistent pool, so a
   call costs microseconds; a grid that bakes in under about 5 µs on one thread still loses,
   so pass `workers = 1` there, and `make lab LAB=parallel-cost` says where the line is).
   Never inside `step`.
4. SIMD: only with operations that are exact (add, mul, sqrt, compares, with no FMA). The
   x86 build already targets AVX2 (`ge_isa`, the `build` skill), so the compiler vectorises
   plain loops first; intrinsics only where a profile still points after that.

Hand-written assembly is not used: intrinsics give the same instructions, inline, in one
source for the agent to read, and assembly would need an x86 and an ARM copy.

The ledger is `docs/perf.md`: add a row for every speed-up (what changed, before and after,
how measured).

## Labs

A cost claim is a `bench` lab run before and after: `make lab LAB=<bench> PRESET=release
ARGS=--tag=before`, then `--compare=before`. Wall-clock metrics are `Det::measured` (compared,
never hashed), and the digest still holds the work's OUTPUT, so a speed-up that changed what
was built shows as a digest move. `mapgen-cost` is the worked example.
