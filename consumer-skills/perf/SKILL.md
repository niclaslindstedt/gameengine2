---
name: perf
description: "Use to make this game faster or leaner, or to prove a change did not make it slower: ge bench (each step's time against its budget, --tag and --compare for a before and after), ge profile (where the step's time goes, zone by zone; --render for the frame's passes), ge mem (what the run allocates, and a hot path that still does), ge leaks (what is never freed), ge load (how the run spreads over the cores and the GPU, and what holds a frame up), the GE_ZONE timing zones that feed them, and the series files ge graph draws."
---

# Performance

Measure before you optimise and prove it after. Every number here comes from the game's own
pilot playing a seed (`ge sim`'s run), replayed with the clock running, so a measurement is
repeatable and needs no window. A claim about speed in a PR carries its `ge bench --compare`
answer.

## The commands

```sh
ge bench [--seeds=1,2] [--repeats=5] [--steps=N]   # each step's time: median, p95, p99, worst
ge bench --tag=before                              # keep it as runs/bench@before.json
ge bench --compare=before                          # every number beside it; 10% slower warns
ge profile [--seeds=1] [--render]                  # where the time goes, zone by zone
ge mem [--seeds=1]                                 # what the run allocates, and where
ge leaks [--seeds=1] [--valgrind]                  # what is never freed
ge load [--seeds=1] [--render]                     # how the run uses the cores and the GPU
```

The game's options go after `--` (`ge bench -- --rivals=3`), as for `ge sim`. bench, profile and
mem build the **release** preset (a sanitizer's time is not the game's); leaks builds the
**debug** one, whose sanitizers find leaks on Linux (elsewhere it needs valgrind).

What each answers:

| Command | Reads | Acts on |
| --- | --- | --- |
| `ge bench` | the step's median, p95, p99 and worst against its budget (1/hz), steps a second, the run's open | a warning beside a tagged run: a step 10% slower |
| `ge profile` | each zone's calls a step, its own time and its share, the time in no zone, the five slowest steps and the zone that dominated each, the zones the open ran | the biggest share first; a step with much time in no zone needs more zones |
| `ge mem` | allocations opening the run, over its steps, and the steps after the first second that still allocate; the peak resident set | a warning naming the zone a hot path allocates in |
| `ge leaks` | every block never freed | one error a leak, at its first frame in this repo |
| `ge load` | the run paced at its rate: every thread's work, spin and sleep, each OS thread's and core's busy share, the threads working at once, each frame's critical path and its ideal; `--render` adds the game's `gpu-load` lab (the frame loop and a `gpu` lane) | notes: most of the CPU idle, a thread spinning, busy threads nothing marks, what bounds the median frame |

Each bench writes two files under `runs/` for `ge graph` and for reading:
`<game>-<seed>.bench.tsv` (one row a step: its time, its allocations, each zone's time) and
`<game>-<seed>.spans.tsv` (every zone round the slowest step: a timeline of who called what).
`ge load` writes `<game>-<seed>.lanes.tsv` (every thread's slices: `ge graph` draws its lanes)
and its charts beside it. A thread the game starts names itself (`ge::core::name_this_thread`)
and marks its work with `GE_WORK("name")`, `GE_WAIT` and `GE_SPIN` (`prof/timeline.hpp`), or
it shows only as an unmarked OS thread.

## Timing zones

A zone is one line at the top of a scope: `GE_ZONE("bot");` (`prof/zones.hpp`, target
`ge::prof`). It records its time and its allocations only while a bench runs; otherwise it costs
one load. Zones nest, and a zone's **exclusive** time is its own, without the zones inside it.

- **Zone systems, not loops.** One zone round each system the step calls (the physics, the
  rules, the bots, the hash), and one round each heavy part of the open (the map build). A bench
  keeps 24 zone entries a step; more are dropped, and `ge profile` says so.
- **Name the system, dotted for a part of one**: `mapgen`, `mapgen.attempt`. The same name is
  the same zone wherever it is opened.
- **Zones change no result.** They read the clock and feed nothing back, so a run benched is the
  run played: every repeat must reach the pilot's digest, and a bench that does not is an error.

## The loop

1. `ge bench --tag=before` on the code as it is, then `ge profile` to see where the time goes.
2. Change the biggest share first. A step that allocates after the first second is the cheapest
   win: reserve the storage once, or reuse it (`ge mem` names the zone).
3. `ge bench --compare=before`. Judge the median and p95 (the worst step is noisy); a change
   inside the clock's noise is no change.
4. `ge leaks` before the commit when the change owns memory.
5. Put the before and after in the PR, and the `ge profile` table when the change moved a zone.

The frame (the GPU and the renderer's CPU, pass by pass) is the game's `profile` lab, which
`ge profile --render` runs beside the step's zones (skill: `labs`).
