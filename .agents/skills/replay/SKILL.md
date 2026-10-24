---
name: replay
description: "Use for engine/replay in gameengine2 and anything that watches a run again: a game's Run::clone (the keyframe), the keeper and its thinning policy, the replay player's seek in a step budget, the transport (play, pause, speeds, skip, frame step), the replay file with its marks, clip and game version, the replay index, the live recorder and the instant replay, the director and its highlight reel; later spectating by inputs and the video export (docs/replays.md, rows M23a-M23h)."
---

# Replays

`engine/replay/` (`ge_replay`, namespace `ge::replay`) watches a run again from any moment. The
design is `docs/replays.md`; the rows are M23a–M23h in `docs/goals.md`. The one idea: a run is
its setup and its inputs (the input log, `engine/play/inputs.hpp`), so a replay is that log
stepped again through the game's own `Run`, never a recording of positions or pixels.

| File | What |
| --- | --- |
| `replay/keep.hpp/.cpp` | The keyframes: `Run::clone`s taken every `every_s`, thinned by age band on fixed multiples; the nearest copy at or before a step |
| `replay/player.hpp/.cpp` | The player: the log opened from its setup and stepped off its keyframes to any step, in a budget of steps a frame |
| `replay/transport.hpp/.cpp` | The transport: play, pause (the end pauses), four speeds, skip, frame step, seek; pure state over the step count |
| `replay/file.hpp/.cpp` | The replay file (`*.ge-replay`): the input log with marks, a clip, the game's version, the date; its text, `plays_on`, save and load |
| `replay/index.hpp/.cpp` | The index: a directory's replay files as list rows for a menu's browser, a broken file a problem line |
| `replay/director.hpp/.cpp` | The director: marks ranked and spaced into a cut of shots by the game's `ShotRule`s; the reel's clock over the shots at their rates |
| `replay/live.hpp/.cpp` | The live run: a session that keeps keyframes and marks; the instant replay lent the live keeper and handed back |
| `capture/ebml.hpp/.cpp`, `capture/matroska.hpp/.cpp` | The muxer: EBML elements; a Matroska / WebM file of a video and an audio track, clusters cut at keyframes, the seek head, cues and duration patched on close |
| `capture/mkv_read.hpp/.cpp` | The reader the tests and labs take a file apart with: tracks, blocks, clusters, cues, seek head |
| `capture/yuv.hpp/.cpp` | RGBA to I420 and back, BT.709 limited range in integers |
| `capture/av_export.hpp/.cpp` | The A/V clock: pictures on the frame clock, the synth rendered to each step's sample before the run's sound hears it, the file interleaved by time |
| `capture/sync_fixture.hpp/.cpp` | A game that flashes and clicks on the same steps, with its sound |
| `play/game.hpp`'s `Sound`, `SoundSpec`, `GE_SOUND` | The run's sound on the play contract; Fall Line's is `games/fall_line/sound/play_sound.hpp` |

`tests/replay_walker.hpp` is the toy game the replay tests share (a walker on its own seeded
stream, its log and the straight digest).

## Rules

- **A seek lands on the straight run.** A run sought to step `s` has the digest and the state of
  the run played straight to `s`. Any change to the keeper, the player or a game's `clone` runs
  `make lab LAB=replay` before and after: `mismatches` must stay 0
  (`tests/replay_player_test.cpp` on a toy game, `tests/fall_line_play_test.cpp` on Fall Line).
- **A game's clone copies everything a step reads or writes**, its streams' positions, its
  pilot's hands and its digest included, and shares only what no step writes (the map, the runs'
  lines). A part the clone misses shows up as a mismatch at the first seek that starts from a
  keyframe. The clone's trace is off (`Run::trace(nullptr)`).
- **A keyframe is never stepped.** The player clones the keyframe it starts from, so the same
  keyframe serves the next seek.
- **The kept set is a rule, not a history.** `keeps(k, step, newest)` decides it from the policy
  and the newest step alone; spacings are whole multiples of the band before them
  (`policy_problem` says which is not), so a copy dropped is never wanted again.
- **The transport reads no clock.** The app hands `tick` the frame's seconds; a test hands it
  numbers. A seek's stepping is paid out with `Player::advance(budget)`, never in one frame.
- **What a keyframe costs is measured, not guessed**: the `replay` lab's `clone-bytes` (the
  counting allocator), `clone-ms` and `worst-seek-steps`. A game whose clone grows past a few
  hundred kilobytes shares more of what its steps never write.
- **The instant replay lends the keeper, never copies it.** `Live::watch` moves the live
  keeper into a `Player` (which starts from its copy at step 0, so no run is opened again) and
  `resume` moves it back; the live run is never stepped by the replay. The `replay-file` lab
  checks every step of every instant replay and the live run's digest after it.
- **A change that moves what a log plays to raises `GameSpec::version`.** The file carries it and
  `plays_on` refuses another, naming both; the `replay-file` lab's `refused` holds the wording.
- **The director is the engine's, the moments are the game's.** `plan_cut` knows no kind: a
  game hands it marks and a `ShotRule` a kind (lead, hold, rate, scale, lens). Fall Line's are in
  `games/fall_line/sim/moments.hpp`; a change there or in `plan_cut` runs `make lab
  LAB=replay-cut` before and after: every flight cut and opening before its lip, no spacing fault,
  `mismatches` 0. A reel is played off the live keeper (`Live::watch`), never a fresh player that
  would step the whole race again at its first cut.
- **A run's sound lands on its step's sample.** A step's sound starts at
  `round((step - from) * rate / hz)`: the export renders the synth up to that sample before
  `Sound::step` hears the step, and each picture's sound is muxed before the next picture. Any
  change to `av_export`, `record`'s mkv path or a game's `Sound` runs `make lab LAB=capture`
  before and after: `late`, `early` and `unshown` 0, `sound-off-ms` under 0.1; Fall Line's runs
  `make lab LAB=capture-run` too (the sound never silence, every picture and sample in the file).
- **The muxer's bytes are pinned.** `tests/capture_matroska_test.cpp` pins one small file's hash;
  a deliberate change of the layout rewrites it and says why in the PR. The file must still read
  when cut at a cluster's end (a crash), and ffmpeg must decode what the labs write.
- **The export is offline and steady.** It renders the synth on demand, never through the audio
  device, so the file's sound is the run's whatever the machine's speed; it plays at `--speed=1`
  only.
