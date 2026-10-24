// A run exported with its sound (engine/capture/av_export.hpp through `record --format=mkv`):
// the sync fixture (sync_fixture.hpp) flashes and clicks on the same steps, and in the file each
// click sounds on its step's sample and the first bright picture after it is less than a frame
// later; the sound runs exactly as long as the pictures; with no sound the file has one track;
// the speed is refused with sound; the step and frame clocks round as stated.
//
// ge:summary Tests: an export's sound and pictures in sync to a frame, its tracks and lengths
// ge:keywords test export av-export sync flash click lag mkv record sound frame-clock
// ge:skill replay testing
// ge:test tests/capture_av_export_test.cpp
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "audio/voice.hpp"
#include "capture/av_export.hpp"
#include "capture/mkv_read.hpp"
#include "capture/sync_fixture.hpp"
#include "check.hpp"
#include "play/record.hpp"

namespace gc = ge::capture;
namespace gp = ge::play;
namespace fs = std::filesystem;

namespace {

constexpr int64_t kSteps = 600;  // 5 s at 120 Hz: four clicks, at 37, 187, 337 and 487
constexpr double kFps = 30;

std::vector<uint8_t> read_all(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string exported(bool sound, double speed, std::string& error) {
  const fs::path dir = fs::temp_directory_path() / "ge_capture_av_export_test";
  fs::create_directories(dir);
  gp::RecordOptions o;
  o.view = "flash";
  o.fps = kFps;
  o.speed = speed;
  o.band = false;
  o.thumbs = 0;
  o.encoding.format = "mkv";
  o.sound = sound ? &gc::kSyncSound : nullptr;
  o.video = (dir / (sound ? "sync.mkv" : "mute.mkv")).string();
  return gp::record(gc::kSyncGame, gc::sync_log(kSteps), o, error) ? o.video : "";
}

}  // namespace

TEST(the_step_and_frame_clocks_round_as_stated) {
  CHECK(gc::step_sample(37, 0, 120, 48000) == 14800);
  CHECK(gc::step_sample(40, 3, 120, 48000) == 14800);
  CHECK(gc::frame_sample(10, 30, 48000) == 16000);
  CHECK(gc::frame_sample(1, 29.97, 48000) == 1602);
  CHECK(gc::frame_ms(1, 30) == 33 && gc::frame_ms(2, 30) == 67);
}

TEST(each_click_sounds_on_its_step_and_its_flash_shows_within_a_frame) {
  std::string error;
  const std::string path = exported(true, 1, error);
  CHECK_MSG(!path.empty(), error.c_str());
  if (path.empty()) return;
  const std::vector<uint8_t> bytes = read_all(path);
  const auto f = gc::read_mkv(bytes, error);
  CHECK_MSG(f.has_value(), error.c_str());
  if (!f) return;
  CHECK(f->tracks.size() == 2);
  // The pictures (each lit or dark by its first luma byte) and the sound, in order.
  std::vector<std::pair<int64_t, bool>> pictures;
  std::vector<float> sound;
  for (const gc::ReadBlock& b : f->blocks) {
    if (b.track == 1) {
      pictures.emplace_back(b.ms, bytes[b.offset] > 128);
    } else {
      const size_t n = b.size / sizeof(float);
      const size_t at = sound.size();
      sound.resize(at + n);
      std::memcpy(sound.data() + at, bytes.data() + b.offset, b.size);
    }
  }
  CHECK(pictures.size() == 151);  // steps 0..600, a picture every 4
  CHECK(static_cast<int64_t>(sound.size() / 2) == gc::frame_sample(151, kFps, ge::audio::kRate));
  int clicks = 0;
  size_t from = 0;
  for (int64_t step = 1; step <= kSteps; step++) {
    if (!gc::sync_clicks_at(step, 150, 37)) continue;
    clicks++;
    const int64_t due = gc::step_sample(step, 0, 120, ge::audio::kRate);
    // The click's first sound: on its step's sample (the tone starts at the block's start),
    // never before. Silence between clicks is exactly 0.
    size_t loud = from;
    while (loud < sound.size() / 2 && std::fabs(sound[2 * loud]) < 1e-6f) loud++;
    from = loud + static_cast<size_t>(ge::audio::kRate / 10);  // past this click
    CHECK(static_cast<int64_t>(loud) >= due);
    CHECK(static_cast<int64_t>(loud) - due < 4);
    const double click_ms = 1000.0 * static_cast<double>(loud) / ge::audio::kRate;
    // The first bright picture after it: later, by less than a frame.
    int64_t lit_ms = -1;
    for (const auto& [ms, lit] : pictures) {
      if (lit && static_cast<double>(ms) + 1 >= click_ms) {
        lit_ms = ms;
        break;
      }
    }
    CHECK(lit_ms >= 0);
    CHECK(static_cast<double>(lit_ms) - click_ms > -1.0);
    CHECK(static_cast<double>(lit_ms) - click_ms < 1000.0 / kFps);
  }
  CHECK(clicks == 4);
}

TEST(with_no_sound_the_file_has_one_track_and_speed_is_refused_with_sound) {
  std::string error;
  const std::string mute = exported(false, 2, error);
  CHECK_MSG(!mute.empty(), error.c_str());
  const auto f = gc::read_mkv(read_all(mute), error);
  CHECK(f.has_value() && f->tracks.size() == 1 && f->tracks[0].type == 1);
  CHECK(exported(true, 2, error).empty());
  CHECK(error.find("plays at the run's pace") != std::string::npos);
}
