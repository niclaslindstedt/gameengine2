// A run exported with its sound (av_export.hpp).
//
// ge:summary av_export's definitions: the step and frame samples, the synth rendered between
// ge:keywords export sample render pending emit pcm frame
// ge:skill replay
// ge:test tests/capture_av_export_test.cpp
#include "capture/av_export.hpp"

#include <cmath>
#include <cstring>

#include "audio/voice.hpp"
#include "capture/yuv.hpp"

namespace ge::capture {

int64_t step_sample(int64_t at, int64_t from, double hz, int rate) {
  return std::llround(static_cast<double>(at - from) * rate / hz);
}

int64_t frame_sample(int64_t k, double fps, int rate) {
  return std::llround(static_cast<double>(k) * rate / fps);
}

int64_t frame_ms(int64_t k, double fps) {
  return std::llround(static_cast<double>(k) * 1000.0 / fps);
}

bool AvExport::open(const AvExportOptions& o, std::string& error) {
  if (o.width <= 0 || o.height <= 0 || o.width % 2 != 0 || o.height % 2 != 0) {
    error = "the pictures' size " + std::to_string(o.width) + "x" + std::to_string(o.height) +
            " is not even on both sides";
    return false;
  }
  if (!(o.fps > 0) || !(o.hz > 0)) {
    error = "the frame rate and the step rate must be over 0";
    return false;
  }
  opt_ = o;
  MatroskaOptions m;
  VideoTrack v;
  v.width = o.width;
  v.height = o.height;
  v.fps = o.fps;
  m.video = v;
  if (o.sound) {
    synth_ = std::make_unique<audio::Synth>(o.seed);
    sound_ = o.sound->open(*synth_, error);
    if (!sound_) return false;
    AudioTrack a;
    a.rate = audio::kRate;
    m.audio = a;
  }
  doc_type_ = std::string(doc_type_of(m));
  return mux_.open(o.file, m, error);
}

void AvExport::render_to(int64_t sample) {
  if (!synth_ || sample <= rendered_) return;
  const auto n = static_cast<size_t>(sample - rendered_);
  const size_t at = pending_.size();
  pending_.resize(at + 2 * n);
  synth_->render(pending_.data() + at, static_cast<int>(n));
  rendered_ = sample;
}

void AvExport::stepped(const play::Run& run, int64_t at) {
  if (!sound_) return;
  render_to(step_sample(at, opt_.from, opt_.hz, audio::kRate));
  sound_->step(run);
}

bool AvExport::emit_sound(int64_t to, std::string& error) {
  if (!synth_ || to <= emitted_) return true;
  render_to(to);
  const auto n = static_cast<size_t>(to - emitted_);
  pcm_.resize(2 * n * sizeof(float));
  // Little-endian float, as A_PCM/FLOAT/IEEE is (every platform the engine runs on is).
  std::memcpy(pcm_.data(), pending_.data(), pcm_.size());
  pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(2 * n));
  const int64_t ms = std::llround(static_cast<double>(emitted_) * 1000.0 / audio::kRate);
  emitted_ = to;
  return mux_.audio(ms, pcm_, error);
}

bool AvExport::frame(const play::Run& run, const lab::Picture& p, std::string& error) {
  if (p.width != opt_.width || p.height != opt_.height) {
    error = "picture " + std::to_string(frames_) + " is " + std::to_string(p.width) + "x" +
            std::to_string(p.height) + ", not the export's " + std::to_string(opt_.width) + "x" +
            std::to_string(opt_.height);
    return false;
  }
  // The last picture's sound, which runs up to this one's time.
  if (!emit_sound(frame_sample(frames_, opt_.fps, audio::kRate), error)) return false;
  to_i420(p, yuv_);
  if (!mux_.video(frame_ms(frames_, opt_.fps), true, yuv_, error)) return false;
  frames_++;
  if (sound_) sound_->frame(run, 1.0 / opt_.fps);
  return true;
}

bool AvExport::close(std::string& error) {
  if (!emit_sound(frame_sample(frames_, opt_.fps, audio::kRate), error)) return false;
  return mux_.close(frame_ms(frames_, opt_.fps), error);
}

}  // namespace ge::capture
