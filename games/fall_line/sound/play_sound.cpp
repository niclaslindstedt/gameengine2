// Fall Line's sound on the play contract (play_sound.hpp).
//
// ge:summary play_sound's definitions: the Sound over route.hpp, opened on a Fall Line run
// ge:keywords sound open step frame state_of
// ge:skill audio sim
// ge:test tests/fall_line_sound_test.cpp
#include "fall_line/sound/play_sound.hpp"

#include <memory>

#include "fall_line/sim/play.hpp"
#include "fall_line/sound/mix.hpp"
#include "fall_line/sound/route.hpp"

namespace fall_line::sound {
namespace {

class PlaySound final : public ge::play::Sound {
 public:
  explicit PlaySound(ge::audio::Synth& synth) : synth_(synth), sound_(make_run_sound()) {}
  ~PlaySound() override { run_sound_silence(sound_, synth_); }
  PlaySound(const PlaySound&) = delete;
  PlaySound& operator=(const PlaySound&) = delete;

  void step(const ge::play::Run& run) override {
    if (const sim::GameState* s = sim::state_of(run)) run_sound_step(sound_, synth_, *s);
  }
  void frame(const ge::play::Run& run, double dt) override {
    if (const sim::GameState* s = sim::state_of(run)) {
      run_sound_frame(sound_, synth_, *s, static_cast<float>(dt));
    }
  }

 private:
  ge::audio::Synth& synth_;
  RunSound sound_;
};

std::unique_ptr<ge::play::Sound> open_sound(ge::audio::Synth& synth, std::string& error) {
  (void)error;
  apply_mix(synth, {}, 0);
  return std::make_unique<PlaySound>(synth);
}

}  // namespace

const ge::play::SoundSpec kSound{open_sound};

}  // namespace fall_line::sound
