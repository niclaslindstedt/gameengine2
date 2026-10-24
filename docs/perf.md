# The performance ledger

Every speed-up lands a row here: what changed, the cost before and after, and how it was
measured. The budgets and the proofs a speed-up owes are the `perf` skill's. A row's numbers
are wall time on the machine named, so compare rows on one machine only.

## Threaded bakes

Measured with `make lab LAB=parallel-cost PRESET=release` (a five-point stencil over square
grids, the median of 15 batches a point), the median of three runs of each build alternated, in
the 4-core Linux x86-64 container. Every grid's output hash the same on every worker count, and
every level digest unchanged.

| Change | Empty call, 8 workers | 64² grid, 8 workers | 256² grid, 8 workers | 1024² grid, 8 workers | Ski area (v10) | Where |
| --- | --- | --- | --- | --- | --- | --- |
| `parallel_bands` on a persistent pool (the caller and up to 7 workers, started once, each taking the next band; a short spin between calls, then sleep) instead of a thread a band each call | 357 µs → 1.4 µs | 368 µs → 3.6 µs (1 worker: 6.4 µs) | 436 µs → 39 µs (1 worker: 99 µs) | 1269 µs → 765 µs | 1312 ms → 1232 ms (−6%, A/B three times) | `engine/core/parallel.hpp`, `parallel.cpp` |

Before, a call started and joined a thread a band: about 60 µs a thread here, so on any grid
under about 512² rows eight workers lost to one. A grid of 16² (0.4 µs on one thread) still
loses on a pool (2.6 µs: a wake-up costs more than the work), so a caller that bakes grids that
small passes `workers = 1`.

## Map generation

Measured with `make lab LAB=mapgen-cost PRESET=release ARGS="--seeds=38,7,1 --reps=5
--version=<v>"`, the mean of each seed's best of five, in the 4-core Linux x86-64 container.
Every row kept every level digest (v10: `0b310d692fbf2dee`, v1: `4c49f555e1c4e253`).

| Change | v10 (ski area) | v1 (single piste) | Where |
| --- | --- | --- | --- |
| The x86-64 baseline raised to AVX2 (x86-64-v3), the compiler vectorising on its own | about −11% | | `ge_isa`, root `CMakeLists.txt` |
| The ski routes' search keeps each step's felled metres instead of walking the line through the trees on every visit | about −8% | | `plugins/mountain/mapgen/ski_routes.cpp` |
| The mountain's rows baked in bands on threads | about −12% | most of the v1 gain | `engine/core/parallel.hpp`, `massif.cpp`, `terrain.cpp` |
| All three, together | 2906 ms → 2154 ms (−26%) | 542 ms → 277 ms (−49%) | |
| The piste's nearest-point lookup on a dense grid over the cells it touches (was a hash map), and both it and the network index walking each ring only where it crosses the occupied cells | 2156 ms → 1543 ms (−28%) | 280 ms → 257 ms (−8%) | `query.cpp`, `net_index.hpp` |
| The ski routes: a heap entry left at a dearer cost than its square was reached at is passed over, the felled metres of every downhill step worked once on threads, the two searches run side by side, the candidate lines judged on threads (only each top's best laid again), a line refused for its length before it is laid on the snow, headings worked only for a line kept | about −17% | | `ski_routes.cpp` |
| The network index's cells hold their segments as spans of a run's consecutive ones with a box, a span too far off passed over whole; a ring walks a row only across the columns it holds segments in (a column only across its rows) | about −4% | | `net_index.hpp` |
| Readings that are pure run on threads, the answers taken in their old order: a drag lift's candidate places, the woods' scan (its draws taken first, in order), a run's nearest-segment pass over the grid by bands of rows; the walk's candidate headings and their sines worked once | about −20% | about −10% | `station_clear.cpp`, `forest.cpp`, `network_stamp.cpp`, `network.cpp` |
| All of the above, together | 1548 ms → 932 ms (−40%) | 257 ms → 224 ms (−13%) | |
| Two library calls the profile hid (they run few instructions but many cycles): a remainder whose dividend is already inside the divisor returns it (`num::mod`, every angle difference), and a wrap to 32 bits within int64's range is an integer cast (`num::to_int32`, every noise hash); the walk's heading choice weighs the heading that turns least first and passes over any whose turn alone already costs more | about −8% | | `engine/core/num.hpp`, `network.cpp` |
| `hypot` divides only the smaller side (the larger over itself is exactly 1); a power-of-two grid pitch multiplies by its exact inverse | about −2% | | `engine/core/math.hpp`, `heightfield.hpp` |
| More pure per-cell passes on threads: a run's levelling of the grid, a lane's raster; the ski routes' trunks filed once a map and the kept lines threaded through the woods side by side | about −7% | | `network_stamp.cpp`, `lanes.cpp`, `ski_routes.cpp` |
| These three, together, against the last row, side by side on one machine | 948 ms → 788 ms (−17%) | 242 ms → 223 ms (−8%) | |

The lookups' ring walks visited every cell of every ring out to the search radius, most of
them empty: a query far from any run read up to 90 rings of hash-map misses. Clamping each ring
to the box the segments lie in skips those cells and reads the occupied ones in the same order,
so the segment that wins a tie is the same and no bit moves.

A reading run on threads must keep nothing between calls. The station and woods readings each
wrote their answer into one `NetHit` the closure shared, harmless on one thread; on several the
writes raced and the maps moved (the digest changed from one run to the next). Each call now
answers into its own. A thread-sanitizer build of `mountain_resort_level_test`,
`mountain_ski_routes_test` and `core_parallel_test` reports no race.

The instruction counts (callgrind) missed the two library calls: `fmod` runs a handful of
instructions that take tens of cycles, so its cost shows only in wall time. A sampling profile
(SIGPROF over the build, the samples symbolised with `addr2line`) found it, and the A/B runs of
the two builds back to back, three times over, are how the rows above were measured: the
container's timings wander by ±5% from one run to the next.

## The frame on the GPU

Measured with `make lab LAB=profile PRESET=release ARGS="--frames=30 --tag=before"`, then
`--compare=before` (seed 38, the chase camera, three rivals), the median GPU ms of each pass on
the container's software adapter (llvmpipe): proportions, not a real GPU's times. Every row is
held by the picture diff (`labs world --compare=before`, and `trails`, `snow`, `shadows`): the
bands it reports and how far each moved.

What the terrain's colour pass spends, each term taken out alone (65 ms in all): the sun's
shadow 10 ms (the figures' tiles 6 of it), the tracks 6, the ridges' horizon 6, the micro-relief
3, the haze 2.

| Change | terrain (ms) | The look | Where |
| --- | --- | --- | --- |
| The figures' shadow tiles: a point's clip in the atlas taken once and moved per tile by an offset worked on the CPU (the view is affine), and a point outside every tile's reach skips them all | 65.2 → 62.9 | every view the same to the bit | `render/shadows.cpp`, `wgsl/sun_shadow.wgsl` |
| The tracks: past a quarter of a coarse texel a pixel the groove's slope fades out, gone by a half, and the track is one tap of the coarse map (was five, or ten in the fine window's rim) | 62.9 → 62.0 | one pixel of one view a level apart; every band at or near 100 dB | `mountain/app/snow.wgsl` |
| Both | 65.2 → 62.0 (−5%) | | |

## The flow step on threads

`water::step_flow` splits its rows over `core::parallel_bands`. On the persistent pool a call
costs a wake-up, and under 64² cells (a small region, or a large one at a coarse tier) that
wake-up costs about as much as the rows it splits or more, so such a grid now runs on the caller
alone; the bits are the same either way, and 64² and up are unchanged. `water-cost`, release
build, ms a 1/60 s step with 8 workers asked (the one-thread time beside it):

| Grid as stepped | 8 asked, before | 8 asked, after | 1 thread |
| --- | --- | --- | --- |
| 32² (32² at tier 0) | 0.064 | 0.079 | 0.080 |
| 16² (32² at tier 1) | 0.0126 | 0.0100 | 0.0113 |
| 8² (32² at tier 2) | 0.0036 | 0.0017 | 0.0017 |
| 16² (64² at tier 2) | 0.0074 | 0.0050 | 0.0059 |

At the smallest grids the pool doubled the step; at 32² it was a wash within the container's
noise (±5%, and wider at these sizes). Where: `engine/water/flow.cpp`.

## The load monitor

Does the run use the machine, or does most of it sit idle? `ge load --seeds=38` answers that for
a game's run, and `ge load --render` adds its frame loop on the GPU (the game's `gpu-load` lab).
The pieces:

| Piece | What it records or works out | Where |
| --- | --- | --- |
| Thread names | Each engine thread names itself (`pool 1`, `sampler`). The OS sees `ge-pool-1`. The main thread keeps the process's name, so a thread that carries it is one a library started | `engine/core/thread_name.hpp` |
| The timeline | A lane a thread: slices of work, spin and sleep (`GE_WORK`, `GE_SPIN`, `GE_WAIT`), nested by scope, written to the thread's own buffer without a lock. The worker pool's bands and waits are recorded through its listener, so a bake needs no line of its own | `engine/prof/timeline.hpp`, `engine/core/parallel.hpp` |
| The OS sampler | Every thread's CPU time and every core's busy share, every few milliseconds, including threads the timeline never hears (the GPU driver's, a software rasterizer's) | `engine/prof/cpu_sampler.hpp` |
| The analysis | Each lane's shares, how long exactly k threads worked at once, the efficiency (work over hardware threads × time), each frame's critical path (the main lane's own work, then each wait laid on the lane that ended it), the ideal frame (its work spread over every core, beside the GPU's), and findings | `engine/prof/load.hpp` |
| The charts | The lanes, the busy heatmap, the threads at once and each frame's stacked path | `engine/lab/load_chart.hpp` |

How to read the answer: the **efficiency** is the share of the machine the marked threads
kept busy. The **OS efficiency** is the same share measured from the process's CPU time, so it
includes spins and unmarked threads. A run whose OS efficiency is far above its efficiency has
work the timeline does not see. The **frames' bound** names what the median frame spends most
of its length on. When the **ideal** is well under the critical path, more threads or overlap
could win that time back.

### The GPU's lane, and the Mac

The `gpu` lane is built from each frame's pass timestamps (`gpu/timer.hpp`'s `start_ms`) and
placed at the frame's submit time on the CPU's clock, because the engine does not yet know the
offset between the GPU's clock and the CPU's (M33b). On the container's software adapter
(llvmpipe) the lane is named `gpu (software)`: its passes run on the CPU, as the rasterizer's
unmarked threads, so their cost also shows in the CPU's numbers. That is an honest picture of
the container, not of a GPU. On a hardware adapter, run:

```sh
make lab LAB=gpu-load PRESET=release ARGS="--frames=120"
```

Try CI's macOS runner (Metal) first. Use the Mac only for what that runner cannot show.

### What it found first

These were measured with the release build on the 4-core Linux x86-64 container, using the
software adapter:

| Run | Efficiency (marked threads) | Process CPU | Threads at once | What bounds the median frame |
| --- | --- | --- | --- | --- |
| `labs load` (the synthetic loop, 30 frames at 60 Hz) | 14.9% | 15.9% | 0.60 | the main lane idle, 8.7 of 16.9 ms; waits on the render thread 3.2 ms; ideal 3.9 ms |
| `labs gpu-load`, the open (seed 38's map built) | 45.4% | 44.1% | 1.81 | 70% of the open on one thread, 25% on all four: the serial stages between the pool's bakes |
| `labs gpu-load`, 60 frames of the bot's run | 0.6% | 69.2% | 0.03 | waiting on `gpu (software)`, 336 of 340 ms; the main lane works 1.0 ms; llvmpipe's four threads use 2.8 cores |
| `ge load --seeds=38`, 1200 steps paced at 120 Hz | 0.0% | 0.2% | 0.00 | idle: a step is 13 µs of its 8.3 ms slot |

On the container, then: the simulation leaves the CPU idle, the frame is all GPU, and the map's
open is the one place the CPU's spread matters. There, about two thirds of the time runs on one
thread. `ge load`'s open is short (0.8 ms) because the play contract reuses the map its pilot
built, so the open's spread is measured by the `gpu-load` lab, which builds the map itself.
