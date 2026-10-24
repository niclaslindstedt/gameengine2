---
name: play-test
description: "Use to test this game by PLAYING it without a screen: the play contract a game fills (play/game.cpp: its axes, options, observe, state, report, pilot), ge play's line protocol, ge sim's pilot runs, the input log, ge replay to a digest, ge state at a step, ge diverge to find the step two runs part, ge shot for a captioned still of any step, ge record for the run as a video and a contact sheet, and ge why for why a bot did what it did and ge sim --human for how human it looks. Load it to add an axis or an option, to make a replay hold, or to chase a determinism break."
---

# Play-testing the game

The game is tested by playing it. Its `play` executable (`play/main.cpp` and `play/game.cpp`)
registers the game on the engine's **play contract**, and `ge` drives it: an agent plays over a
line protocol, the game's own pilot plays whole runs, and every run is written down as an
**input log** that replays to the same digest. Nothing here needs a window.

## The contract (`play/game.cpp`, the engine's `play/game.hpp`)

A `GameSpec` names the game, its step rate, its longest run, its **axes** (the whole input, one
number each: `signed_unit` -1..1, `lever` 0..1, `toggle` held, `press` taken once a hold) and its
**options** (what a run is set up with, as `--<name>=<value>`). `open` makes a `Run`:

| `Run::` | What it gives | Who reads it |
| --- | --- | --- |
| `step(input)` | one fixed step on one snapped value per axis | everything |
| `over()` | nothing is left to play | the protocol, `ge sim` |
| `digest()` | a hash folded over every step's state | `ge replay`, `ge diverge` |
| `observe()` | what a player sees now (JSON object), events since the last call | an agent |
| `state()` | everything that decides the next step (JSON object of scalars) | `ge state`, `ge diverge` |
| `report()` | the run's result (JSON object) | `ge sim`, `ge replay`, `end` |
| `pilot(input)` | the game's bot: fills the axes, returns true | `ge sim`, the `pilot` op |
| `shoot(view, out, caption)` | one of the spec's **views** drawn on the CPU raster, and a line saying what it shows | `ge shot` |

Rules:

- **The axes are the whole input.** Anything a run reads that is not its seed, its options or its
  axes (a clock, an unseeded random number, an uninitialised field) breaks the replay. The engine
  snaps every value onto its axis's grid before `step`, so do not snap again in the game.
- **`state()` lists every field `step` reads**, in full precision (`num::to_string`, not
  `to_fixed`): `ge diverge` can only name the fields it is shown.
- **`observe()` is for deciding, `state()` is for comparing.** Keep the observation small and in
  the player's terms (where the goal is from here, not the whole world).
- **A view is drawn from the state, never stepped.** `shoot` reads the run as it stands and draws
  on the CPU (`lab/raster.hpp`), so a still needs no GPU or window and reproduces from the log.
  Its caption says what is in the picture in the game's terms (where, how fast, what is next);
  the engine adds the seed, the options, the step and the digest under it.
- **A new axis or option** is a row in the spec's arrays. It changes the input log's `axes` line,
  so old logs no longer `fit`: say so in the commit.

## The loop

```sh
ge sim --seeds=1,2,3            # the pilot plays each seed; runs/<game>-<seed>.inputs each
ge replay runs/<game>-1.inputs  # played back: must reach the digest it recorded
ge play --seed=1                # you at the controls, one JSON line each way (below)
ge state runs/<game>-1.inputs --step=600   # the whole state after 600 steps
ge diverge runs/a.inputs runs/b.inputs     # where two runs part, and which fields differ
ge shot runs/<game>-1.inputs --step=600 --view=<v>   # a captioned still at that step, a PNG
ge shot --seed=1 --view=<v>                # the pilot's run of seed 1, at its end
ge record runs/<game>-1.inputs --view=<v> --to=30s   # the run as an MP4 and a contact sheet
ge record runs/<game>-1.inputs --gpu --mode=wireframe   # the same through the GPU renderer
```

**LOOK at a run.** `ge shot` replays the log to the step and draws the view, framed with its
caption, the seed, the options, the step and the digest; open the PNG it names. Shoot the step
`ge diverge` names on both logs to see two runs part, and a step a lab or a bench flags to see
what happened there. `ge shot` with no `--view` draws the first and lists the others.

**WATCH a run.** `ge record` plays the log back and draws the view at every frame (`--fps`, 30
by default), the run at `--speed` (2 twice as fast, 0.25 slow motion) from `--from` to `--to`
(a step, or seconds with an `s`), each frame with its time, step and view burned in. ffmpeg
encodes it: `--format=mp4` (H.264, the default), `webm` (VP9) or `gif`; `--size=WxH`, `--crf`
(lower is better and larger) or `--bitrate=4M`, `--encoder-preset` (H.264's speed against size).
`--format=png` keeps the frames as PNGs and needs no ffmpeg; `--format=mkv` is the engine's own
file with the run's SOUND (the game's `GE_SOUND`; `--sound=0` leaves it out), uncompressed and
large, at `--speed=1` only, also with no ffmpeg. Beside it the CONTACT SHEET: a grid
of `--thumbs` stills, each labelled with its time and step, the run at a glance; read it first,
then watch the stretch it points at. It builds the release preset, since every frame is drawn on
the CPU.

**WATCH it as the game draws it.** A game built with the GPU registers a renderer beside its
spec (`GE_RENDERER(spec)`: a `RendererSpec` of views, MODES and `open`, and a `Renderer` whose
`draw` pictures a `Run` as it stands). `--gpu` on `ge shot` or `ge record` draws through it, and
`--mode` picks how: the lit picture, the meshes' edges (`wireframe`), the depth, or the lit
picture with the game's marks over it (`overlay`); `ge shot --gpu` lists the views and
`play describe` the modes. One renderer draws a whole recording: the first frame is a cut and
every frame after is drawn on from the last, as the game's own window would. Without an adapter
the command says so and names the driver to install.

**The protocol** (`ge play`): the session opens with `hello` (the axes, the options, the ops).
Then send one object a line: `{"op":"observe"}`; `{"op":"act","input":{"<axis>":v},"steps":n}`
(holds the input n steps, answers the observation after them); `{"op":"pilot","steps":n}` (the
game's pilot plays n steps); `{"op":"why"}` (what the pilot would do now and its decisions, without
stepping); `{"op":"state"}`; `{"op":"inputs"}` (the log so far); `{"op":"end"}`
(the report and the digest). A bad line answers `{"op":"error","error":...,"next":...}`.
Script a session by piping lines in: `printf '{"op":"observe"}\n' | ge play --seed=1`.

**The input log** is plain text: a header (`game`, `seed`, `option`, `hz`, `axes`, `steps`,
`digest`), then one line per run of held steps, `<count> <value per axis>`. Edit it by hand to ask
"what if": copy a log, change one line, drop the `digest` line, and `ge diverge` the two.

**WHY A BOT DID IT.** A game whose Run accepts a decision trace (`Run::trace`, the engine's
`ai/why.hpp`) has every bot decision explained from a log: `ge why <log> --step=N --around=K`
replays it and prints agent 0's decisions (the game's pilot; `--agent=` the game's other bots)
round step N: what it saw, the options it weighed with their scores and terms, the one it took
and by what margin. On a log you or a person played, it is what the pilot WOULD have done, and
the summary names the first step it would have played otherwise. `--flips` lists every step it
changed its mind (`--close=x` adds the close calls); `--timeline` writes the decisions as a
series file for `ge graph`. A bot records with `if (why) why->fact(...)`: off is a null pointer,
and it must decide exactly the same traced and untraced (`ge why` replays to the log's digest).

**HOW HUMAN IT LOOKS.** `ge sim --human` adds, per run, each axis's metrics (a signed axis read
as a stick, a lever as a trigger: the share at an end, jumps, reversals a minute, how long it is
held part-way, the lag behind what the bot meant) and each decision's changes of mind a minute,
with a warning for every value outside a person's band (the engine's `ai/human.hpp`). The lag
needs the bot to record what it meant before its hands (`ai::record_intent`, the engine's
`ai/hands.hpp`, which also turns a decision into late, settling, slewed presses). A pilot that
holds hands must press on a copy in `pilot()` and commit it in `step()` only when it was asked
at that step, or `ge why` (which asks at every step) moves the run.

## Chasing a determinism break

1. `ge replay` reports a digest that differs from the recorded one, or a test's golden moved.
2. Record the same inputs twice (`ge sim` twice, or one log replayed) and `ge diverge` them: the
   `state_step` is the first step after which they part, the fields are what differs.
3. Read what `step` does to those fields at that step: the cause is an input it reads that is not
   in the log (skill: `sim-system`, determinism).
4. Fix it, rerun `ge replay` on the original log (an honest change of behaviour rewrites its
   digest: say so in the commit), then `ge check`.
