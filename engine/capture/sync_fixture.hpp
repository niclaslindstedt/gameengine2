// THE EXPORT'S SYNC FIXTURE: a run that does nothing but FLASH and CLICK on the same steps, so an
// export of it shows whether its sound and its pictures agree (docs/replays.md § Video with
// sound). Every `period` steps (the option `period`, 150 by default: 1.25 s at 120 Hz), starting
// at `first` (37: off every frame grid, so the frame clock has to round), the run's step makes a
// click (a short square tone from its `SoundSpec`) and its picture (the view `flash`) turns white
// for the next `hz / 20` steps, so the first frame on or after the click shows it at any frame
// rate of 20 or more. Between flashes the picture is black and the run is silent.
//
// An export holds them in sync when each click's first loud sample and the first bright frame
// after it are less than one frame apart, the click first: `tests/capture_av_export_test.cpp`
// reads its own file back, the `capture` lab has ffmpeg decode it.
//
// ge:summary The export's sync fixture: a run that flashes and clicks on the same steps
// ge:keywords sync fixture flash click export lag av-sync test-game period
// ge:skill replay
// ge:test tests/capture_av_export_test.cpp
#pragma once

#include <cstdint>

#include "play/game.hpp"
#include "play/inputs.hpp"

namespace ge::capture {

extern const play::GameSpec kSyncGame;
extern const play::SoundSpec kSyncSound;

// The steps the fixture clicks on, below `steps`.
bool sync_clicks_at(int64_t step, int64_t period, int64_t first);

// A log of `steps` idle steps of the fixture, its `period` and `first` given.
play::InputLog sync_log(int64_t steps, int64_t period = 150, int64_t first = 37);

}  // namespace ge::capture
