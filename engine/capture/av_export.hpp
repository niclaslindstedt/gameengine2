// A RUN EXPORTED WITH ITS SOUND (docs/replays.md § Video with sound): the pictures a recording
// draws (play/record.hpp, `--format=mkv`) written into the engine's own Matroska file
// (capture/matroska.hpp) beside the run's sound, rendered offline in lockstep by the synth.
//
// THE TWO CLOCKS ARE ONE. Picture k is shown at k / fps seconds and shows the run after the step
// the frame clock picks for it (play/record.hpp's `frame_steps`); a step's events sound at the
// sample its end falls on, (at - from) * rate / hz rounded, where `at` is the steps taken. So the
// export renders the synth up to each step's sample before it hands the step's events to the
// game's sound (`stepped`), and a click and a flash on the same step land within a frame of each
// other however slowly the export runs: nothing is timed by a wall clock. Each picture's sound
// (the samples from its time to the next picture's) is written just before the next picture, so
// the file interleaves the tracks by time.
//
// THE TRACKS: the video as uncompressed I420 (capture/yuv.hpp) and the sound as 32-bit float
// stereo at the synth's rate, so the file is `matroska`, not `webm`, until the encoders (row
// M23d2) take their place. The game's sound comes from its `SoundSpec` (play/game.hpp); with none
// the file has no audio track.
//
// ge:summary A run exported with its sound: pictures as I420, the synth rendered on the step clock
// ge:keywords export av-export mkv matroska sound audio sync lockstep click flash frame-clock
// ge:keywords sample step-sample offline-render synth i420 pcm
// ge:skill replay
// ge:test tests/capture_av_export_test.cpp
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "audio/synth.hpp"
#include "capture/matroska.hpp"
#include "lab/raster.hpp"
#include "play/game.hpp"

namespace ge::capture {

struct AvExportOptions {
  std::string file;           // "" keeps the file in memory (`bytes()`)
  int width = 0, height = 0;  // the pictures' size, both even
  double fps = 30;
  double hz = 120;                         // the run's steps a second
  int64_t from = 0;                        // the step picture 0 shows
  const play::SoundSpec* sound = nullptr;  // null: no audio track
  uint32_t seed = 1;                       // the synth's noise
};

// The sample step `at`'s events sound at: (at - from) * rate / hz, rounded.
int64_t step_sample(int64_t at, int64_t from, double hz, int rate);
// The sample picture `k` starts at: k * rate / fps, rounded.
int64_t frame_sample(int64_t k, double fps, int rate);
// The millisecond picture `k` is shown at: k * 1000 / fps, rounded.
int64_t frame_ms(int64_t k, double fps);

class AvExport {
 public:
  bool open(const AvExportOptions& o, std::string& error);
  // After each step the run takes once open: `at` is the steps it has taken.
  void stepped(const play::Run& run, int64_t at);
  // The next picture (`p` at the open size) of `run` as it stands.
  bool frame(const play::Run& run, const lab::Picture& p, std::string& error);
  // The last picture's sound, and the file closed.
  bool close(std::string& error);

  bool has_sound() const { return sound_ != nullptr; }
  int64_t frames() const { return frames_; }
  int64_t samples() const { return emitted_; }
  int64_t bytes() const { return mux_.written(); }
  const std::vector<uint8_t>& memory() const { return mux_.bytes(); }
  std::string_view doc_type() const { return doc_type_; }

 private:
  void render_to(int64_t sample);
  bool emit_sound(int64_t to, std::string& error);

  AvExportOptions opt_;
  MatroskaWriter mux_;
  std::string doc_type_;
  std::unique_ptr<audio::Synth> synth_;
  std::unique_ptr<play::Sound> sound_;
  std::vector<float> pending_;  // rendered, not yet written: samples from `emitted_` on
  int64_t rendered_ = 0;        // samples rendered
  int64_t emitted_ = 0;         // samples written
  int64_t frames_ = 0;
  std::vector<uint8_t> yuv_;
  std::vector<uint8_t> pcm_;
};

}  // namespace ge::capture
