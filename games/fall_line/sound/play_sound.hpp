// FALL LINE'S SOUND ON THE PLAY CONTRACT (play/game.hpp § THE RUN'S SOUND): the run's sound
// (route.hpp: the events of every step as one-shots, the ride bed steered once a frame, heard from
// the chase seat) over the synth an export renders, with the faders at their defaults
// (mix.hpp). The play executable registers it with `GE_SOUND(fall_line::sound::kSound)`, so
// `ge record --format=mkv` writes a run with its sound; the capture labs pass it themselves.
//
// ge:summary Fall Line's sound on the play contract: route.hpp over an export's synth
// ge:keywords sound sound-spec play export record mkv ge_sound route bed
// ge:skill audio sim
// ge:test tests/fall_line_sound_test.cpp
#pragma once

#include "play/game.hpp"

namespace fall_line::sound {

extern const ge::play::SoundSpec kSound;

}  // namespace fall_line::sound
