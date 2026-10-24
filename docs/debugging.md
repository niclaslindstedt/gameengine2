# Debugging

An agent finds out why a run behaves as it does by reading what the run did and looking at it.
The engine gives every run three forms, each stated once, so a game gets all three by plugging
into the engine rather than writing its own printf, dump or plot. The procedure is the `debug`
skill; this page is the design.

| Form | What | Code | How a reader gets it |
| --- | --- | --- | --- |
| The log | Lines by simulation step on named channels | `engine/core/output.hpp` | `--log=<spec>` on any lab: `previews/<id>/log.txt` |
| A trace | One row a step, frame or sample: a series file | `engine/lab/series.hpp` | a lab attaches it; `ge bench`, `ge profile`, `ge mem` write it |
| A chart | A trace drawn, its outliers ringed: PNG and SVG | `engine/lab/chart.hpp`, `plot.hpp`, `stats.hpp` | a lab's shot; `ge graph <file>...` |
| Lanes | Every thread's work, spin and sleep over time, and each frame's critical path: a SPANS file with a lane a thread | `engine/prof/timeline.hpp`, `load.hpp`, `engine/lab/load_chart.hpp` | `ge load` writes `runs/<game>-<seed>.lanes.tsv`; the `load` and `gpu-load` labs; `ge graph` |

## The log

There is one output channel (`core/output`), and every line through it carries:

- a **channel**: `engine` for the plain helpers (`status`, `info`, `warn`, `error`, `debug`,
  `trace`), or one a subsystem or a game names (`channel("course")`, registered on first use, at
  most 64 a process);
- a **level**: `error`, `warn`, `status`/`info`, `debug` (an event worth a line), `trace` (every
  step);
- the **step** it was emitted in: a game's `step` calls `set_step(state.steps)` first, and every
  line until the next call carries it (-1 outside a run).

Each channel keeps lines up to its **verbosity** (`off`, `error`, `warn`, `info`, `debug`,
`trace`), `info` unless configured. THE LOG SPEC configures them all at once:

```
skier=trace,course=debug,*=warn     # skier everything, course its events, the rest warnings up
debug                               # a bare verbosity is `*=` it
```

A spec is parsed whole: a bad item changes nothing and the answer says how to write one. A name
not yet registered is registered by the spec, so a spec can be read before the code that logs on
the channel runs.

**Cost.** A dropped line is one table read when the caller guards it with `enabled(ch, level)`
before formatting, and the guard is the rule: a step allocates nothing, and the log is no
exception (`games/fall_line/sim/game.cpp`'s `log_step`).

**The dump.** The labs runner's `--log=<spec>` writes every kept line to
`previews/<id>/log.txt` as `[step] level channel: message`, with debug and trace only in the file
(the terminal keeps the rest), and notes how many lines of each channel and level it holds. The
lines are greppable by step (`grep '^\[4983\]'`), by channel (`grep 'debug course'`) and by
racer, and the step is the one `ge state --step=N` and `ge diverge` report.

Fall Line's channels: `course` (gates taken and missed, the finish, resets), `skier` (flights,
landings, wipeouts, glances at debug; every racer's state every step at trace), `field` (racers
meeting).

## Series and spans files

Every measurement over time is one text format, so every one can be charted, diffed and grepped
the same way:

```
# title: the player's skier on seed 38, the bot at the controls
# x: t_s
# unit.speed: km/h
t_s	speed	edge	gate
0.00833	0.01	1.62	0
0.01667	0.03	3.25	0
```

- `# key: value` lines first (`title`, `x` the x column, `unit.<column>`), then a tab-separated
  header, then one row a sample. Missing cells are `nan`.
- A column whose every cell is a number is numeric; any other is text.
- Numbers are written shortest round-trip, so a file read back gives the same doubles: a trace of
  a deterministic run is itself deterministic.
- **SERIES** (wide): x first, then one numeric column per measure. `ge bench`, `ge mem` and a
  lab's trace write this; `ge profile` writes one column per timing zone.
- **SPANS** (a timeline): the columns `name depth start_ns dur_ns`, one row a zone entered, depth
  its nesting. `ge profile` writes this; it is drawn as a flame timeline.

## Outliers and spikes

One rule everywhere (`stats.hpp`), so "an outlier" means the same thing in a lab note, a
`ge graph` finding and a ring on a chart:

- an **outlier** of a set: a robust z-score over 3.5, |v - median| / (1.4826 MAD), so a few wild
  values cannot hide each other the way they inflate a standard deviation;
- a **spike** of a series over time: the same test on what is left after a rolling median of its
  neighbours is taken off, with the scale floored at 1% of the series' p1..p99 range, so a slow
  climb is not a spike and a smooth 120 Hz signal flags nothing but a real jump.

Both come strongest first, and a chart rings at most 60, so a noisy signal rings its worst.

## Charts

The chart kit draws a `Plot` (a recorded drawing) that renders to a `Picture` through the labs'
raster (a lab shot, captioned and hashed) and to SVG (every label, value and ring as text, a
tooltip on every mark that has one). The forms:

| Chart | Draws | Rings |
| --- | --- | --- |
| `line_chart` | lines (or points) against x, events as marks, phases as bands, budgets as dashed rules, a log scale | spikes (points: outliers) |
| `histogram_chart` | each sample over shared bins, overlaid, its P50 and P95 | outliers |
| `box_chart` | a box a sample: p25 to p75, the median, whiskers p5 to p95, outlier dots | outliers |
| `heatmap_chart` | rows by columns in one hue, light to dark, with its scale | hot cells |
| `span_chart` | a flame timeline: a bar a span, nested by depth | slow spans |

The look is one system: a near-white surface, recessive grid and axes, two inks of text, series
in a fixed categorical order, red kept for the rings, one y axis a chart (two measures of
different scale are two charts), a legend from two series. A line with more points than the plot
has pixel columns is drawn as each column's least and most, so a one-step spike still shows.
`chart_text` is a chart in words: a line a series (n, min, p50, p95, p99, max) and every finding.

## `ge graph`

```sh
ge graph runs/trace.tsv                                  # a line chart a column, spikes ringed
ge graph before.tsv after.tsv --columns=step_ms          # the same column of each, overlaid
ge graph trace.tsv --kind=hist|box|heat|scatter          # a distribution, percentiles, a grid
ge graph zones.tsv                                       # spans: a timeline and zone boxes
ge graph runs/fall_line-38.lanes.tsv                     # lanes: a row a thread, threads at once
```

The charts go to a `graphs/` directory beside the first file (`--out=` to choose), each a PNG and
an SVG, and the answer lists them in `written`, a summary of each series in `text` and
`result.charts`, and every ringed value as a `note` finding with where to look next.

## The labs

- `charts` (engine): every chart form over seeded data with outliers planted at known places;
  each must be ringed exactly once. The reference sheet for what a chart looks like.
- `trace` (Fall Line): the player's skier step by step down one seed with the bot, as a series
  file and three charts (speed, sideways load, edge and lean) with gates, flights and spikes.
  `--log=course=debug,skier=debug` adds the same run's events by step.

- `why` (Fall Line): every bot's DECISIONS down one seed (`engine/ai/why.hpp`): what each read,
  weighed and chose, how often it changed its mind and how close its calls were, with the
  player's as a series file. For one step of any logged run, `ge why <log> --step=N` (skill:
  `bots`) replays it and prints the decisions there; `--timeline` writes them for `ge graph`.

- `anim` (the skier plugin): the skier's figure ridden through a scenario and recorded as a pose
  track: filmstrips, onion skins, joint angles, a jolt heatmap and its faults; a copy with a pop,
  a stretch and a clip planted must have each found where it was put.

## Animation views (`engine/lab/anim.hpp`)

An animation is judged by LOOKING at it frame by frame and by MEASURING how its joints move: a
pop or a stretched bone that a still frame hides is one number out of line on a chart. Any figure
records into a POSE TRACK:

- A `Rig`: the joints' names, each joint's parent (its bone runs from the parent; -1 is a root),
  its side (left, centre, right: the colour it is drawn in) and the END joints whose paths an
  onion skin traces (hands, tips, feet).
- A frame a step: every joint in the FIGURE'S OWN FRAME (x right, y up, z forward, the ground at
  `floor`), so the track shows the animation, not the figure travelling. A figure's pieces add
  their boxes as a wireframe (`add_box_wire`).

`measure_motion` gives every joint every frame its speed, acceleration and jolt, every bone its
stretch against its median length, and every joint with a parent and a child its angle and the
angle's rate. `anim_findings` reads them for five faults:

| Fault | What it is | Flagged when |
| --- | --- | --- |
| `pop` | a joint jumps in one frame | its speed is a spike, more than 1 m/s over its neighbours |
| `snap` | a joint's acceleration jerks | its acceleration is a spike, 100 m/s^2 over its neighbours, with no pop beside it |
| `angle-snap` | a joint turns too fast for one frame | its angle's rate is a spike, 360 deg/s over its neighbours |
| `stretch` | a bone changes length | more than 5% off its median length |
| `clip` | a joint goes through the ground | more than 2 cm under the floor |

A spike is the outlier rule's (`stats.hpp`) AND past the absolute floor (`AnimLimits`), so a
smooth 120 Hz motion flags nothing. Neighbouring frames of one fault on one joint are one finding
at its worst frame (a pop at the frame the joint jumped to), with a sentence saying what, where
and against what.

The views:

| View | What it shows | What it catches |
| --- | --- | --- |
| `filmstrip` | chosen frames side by side from the side, behind or above; the wireframe grey under the bones (left blue, right orange, the spine ink); one scale for every panel; findings ringed red in their frame | a limb that changes size, a pose that reads wrong from one side only, the frame a fault is at |
| `onion_skin` | frames laid over one another, the earlier fainter, and each end joint's path over every frame between | kinks and jumps in a path, a swing that is not symmetric |
| `angle_chart` | the angle at chosen joints over time (chart.hpp, spikes ringed) | a joint that never bends, one that snaps |
| `jolt_heatmap` | every joint's jolt over time, one row a joint, on a log scale | the moments the whole figure jerks, the joints that jerk alone |
| `motion_series` | the measures as a series file: `<joint>.accel`, `.jolt`, `.angle` | anything else, through `ge graph` |

`make lab LAB=anim ARGS="--scenario=turns"` is the skier's; a new figure gets a lab of its own
that records its track the same way and plants the same three faults in a copy.
