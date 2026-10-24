---
type: Added
title: Video with sound
---

`engine/capture`: the engine's own Matroska muxer and reader, RGBA to I420, and `record --format=mkv` (`ge record`), which writes a run's pictures with its sound rendered offline in lockstep on the run's clock, with no ffmpeg; a game registers its sound with `GE_SOUND` (`play/game.hpp`'s `SoundSpec`). New labs `capture` (a click and a flash on one step, in sync at every frame rate) and `capture-run`.
