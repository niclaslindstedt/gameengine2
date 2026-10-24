// The run's sound (route.hpp).
//
// ge:summary The event route and the ride bed's frame for Fall Line's run
// ge:keywords route events landing wipeout gate finish bump brush
// ge:skill audio sim
// ge:test tests/fall_line_sound_test.cpp
#include "fall_line/sound/route.hpp"

#include "audio/sound.hpp"
#include "fall_line/sound/bank.hpp"
#include "fall_line/sound/mix.hpp"
#include "skier/tuning.hpp"

namespace fall_line::sound {

namespace {

using ge::audio::PlayShape;
using sim::EventKind;

// Loose snow under a landing deeper than this packed share is the whumpf, not a thump.
constexpr double kPowderUnder = 0.4;
// A landing past this share of the legs' limit is the hard one.
constexpr double kHardShare = 0.5;

float unit(double v) { return v < 0 ? 0.0f : v > 1 ? 1.0f : static_cast<float>(v); }

void play(ge::audio::Synth& synth, std::string_view id, const Listener& ear, float gain = 1) {
  const ge::audio::SoundDef* def = ge::audio::find_sound(kBank, id);
  if (!def) return;
  PlayShape shape;
  shape.gain = gain * ear.events;
  shape.pitch = ear.muffle;
  ge::audio::play_def(synth, *def, shape, kBusEffects);
}

void landing(ge::audio::Synth& synth, const ge::skier::SkierState& skier, double g,
             const Listener& ear) {
  const double harsh = ge::skier::tuning::air::kHarshG;
  const float load = unit(g / (kHardShare * harsh));
  if (skier.packed < kPowderUnder) {
    play(synth, kLandPowder, ear, 0.6f + 0.4f * load);
  } else if (g > kHardShare * harsh) {
    play(synth, kLandHard, ear, 0.8f + 0.2f * unit((g - kHardShare * harsh) / harsh));
  } else {
    play(synth, kLandSoft, ear, 0.5f + 0.5f * load);
  }
}

void wipeout(ge::audio::Synth& synth, int32_t cause, const Listener& ear) {
  switch (static_cast<ge::skier::Wipeout>(cause)) {
    case ge::skier::Wipeout::tree:
      play(synth, kHitTree, ear);
      play(synth, kWipeoutTree, ear);
      return;
    case ge::skier::Wipeout::landing:
      play(synth, kWipeoutNose, ear);
      return;
    case ge::skier::Wipeout::across:
    case ge::skier::Wipeout::hurt:
    case ge::skier::Wipeout::none:
      play(synth, kWipeoutCatch, ear);
      return;
  }
}

}  // namespace

RunSound make_run_sound() { return {ge::skier::make_ride_bed(kBusWind, kBusEffects), Seat::chase}; }

void run_sound_step(RunSound& sound, ge::audio::Synth& synth, const sim::GameState& state) {
  if (state.racers.empty()) return;
  const Listener& ear = listener_for(sound.seat);
  const ge::skier::SkierState& me = state.racers[0].skier;
  for (const sim::GameEvent& e : state.events) {
    if (e.kind == EventKind::shoulder) {
      // Two racers met; heard when he is one of them, louder the faster they closed.
      if (e.racer == 0 || e.gate == 0) play(synth, kBump, ear, 0.5f + 0.5f * unit(e.value / 8));
      continue;
    }
    if (e.racer != 0) continue;
    switch (e.kind) {
      case EventKind::gate:
        // The start gate is the wand at the start, not a panel: the same slap, quieter.
        play(synth, kGateTaken, ear, e.gate == 0 ? 0.5f : 1);
        break;
      case EventKind::missed:
        play(synth, kGateMissed, ear);
        break;
      case EventKind::finish:
        play(synth, kFinish, ear);
        break;
      case EventKind::landing:
        landing(synth, me, e.value, ear);
        break;
      case EventKind::wipeout:
        wipeout(synth, e.gate, ear);
        break;
      case EventKind::glance:
        play(synth, kBrushTree, ear);
        break;
      case EventKind::reset:
        play(synth, kReset, ear);
        break;
      case EventKind::takeoff:  // the wind's rush coming up is the take-off
      case EventKind::shoulder:
      case EventKind::bump:
      case EventKind::wanted:  // the hunt's sounds come with its pursuers
      case EventKind::onto:    // the free ride's bookkeeping, not heard
      case EventKind::arrived:
      case EventKind::boarded:  // the lift is heard from its own bed, when it has one
      case EventKind::unloaded:
      case EventKind::sled:  // the snowmobile's sound comes with its own bed
      case EventKind::army:  // the army is heard from its men, when they have a voice
      case EventKind::heli:  // the helicopter's sound comes with its own bed
      case EventKind::para:  // the paramotor's sound comes with its own bed
      case EventKind::army_sled:
      case EventKind::army_heli:
      case EventKind::army_para:
      case EventKind::gunfire:  // the reports, the cracks and the strikes wait for their voices
      case EventKind::hurt:     // the blow is heard as the fall that dealt it
      case EventKind::died:
        break;
    }
  }
}

void run_sound_frame(RunSound& sound, ge::audio::Synth& synth, const sim::GameState& state,
                     float dt, float duck) {
  if (state.racers.empty()) return;
  const Listener& ear = listener_for(sound.seat);
  ge::skier::RideHeard heard;
  heard.wind = {ear.wind * duck, ear.tone};
  heard.snow = {ear.snow * duck};
  ge::skier::ride_bed_update(sound.bed, synth, state.racers[0].skier, state.spec, dt, heard);
}

void run_sound_silence(RunSound& sound, ge::audio::Synth& synth) {
  ge::skier::ride_bed_silence(sound.bed, synth);
}

}  // namespace fall_line::sound
