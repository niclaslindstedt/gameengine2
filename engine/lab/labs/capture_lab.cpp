// LAB `capture`: IS AN EXPORT'S SOUND IN SYNC WITH ITS PICTURES? (engine/capture; docs/replays.md
// § Video with sound). The sync fixture (capture/sync_fixture.hpp) flashes and clicks on the same
// steps; it is exported through `record --format=mkv` at each frame rate of `--fps`, and the
// engine's reader takes the file apart: every click's first sound against its step's sample (it
// must be on it), and the first bright picture after it (it must follow by less than a frame).
// When ffmpeg is installed it decodes the same file: every picture and every sample must come out
// (its numbers are measured, since a machine may not have it).
//
// The cost of the uncompressed path is measured too: a 1280x720 picture to I420 and into the
// muxer, a frame, and the bytes a minute of it takes (what the encoders, row M23d2, will cut).
//
//   make lab LAB=capture ARGS="--seconds=30 --fps=24,25,30,60"
//
// ge:summary Lab: an export's sound against its pictures, a click and a flash on one step
// ge:keywords lab capture export sync flash click lag mkv ffmpeg decode i420 cost
// ge:skill replay labs
// ge:test tests/capture_av_export_test.cpp
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "audio/voice.hpp"
#include "capture/av_export.hpp"
#include "capture/matroska.hpp"
#include "capture/mkv_read.hpp"
#include "capture/sync_fixture.hpp"
#include "capture/yuv.hpp"
#include "core/num_text.hpp"
#include "lab/lab.hpp"
#include "lab/raster.hpp"
#include "play/record.hpp"

namespace {

namespace gc = ge::capture;
namespace gl = ge::lab;
namespace gp = ge::play;
namespace num = ge::core::num;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

constexpr int64_t kPeriod = 150, kFirst = 37;

struct Click {
  int64_t step = 0;
  double due_ms = 0;    // its step's sample, as time
  double sound_ms = 0;  // its first sound in the file
  double lit_ms = -1;   // the first bright picture at or after it
};

struct Take {
  double fps = 0;
  int64_t frames = 0, samples = 0, bytes = 0;
  std::vector<Click> clicks;
  std::vector<std::pair<int64_t, bool>> pictures;  // ms, lit
  std::vector<float> left;                         // the left channel
  bool ffmpeg_decodes = false;
  int64_t ffmpeg_frames = -1;
  std::string error;
};

std::vector<uint8_t> read_all(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The shell's sink for a command's output.
#ifdef _WIN32
constexpr const char* kQuiet = " > NUL 2>&1";
#else
constexpr const char* kQuiet = " > /dev/null 2>&1";
#endif

bool tool_found(const char* tool) {
  return std::system((std::string(tool) + " -version" + kQuiet).c_str()) == 0;
}

// ffprobe's count of the video's frames in `path`, or -1.
int64_t probe_frames(const std::string& path) {
  const std::string cmd =
      "ffprobe -v error -count_frames -select_streams v:0 -show_entries "
      "stream=nb_read_frames -of csv=p=0 \"" +
      path + "\"";
#ifdef _WIN32
  std::FILE* p = _popen(cmd.c_str(), "r");
#else
  std::FILE* p = popen(cmd.c_str(), "r");  // NOLINT(cert-env33-c): the decoder is the point
#endif
  if (!p) return -1;
  char line[64] = {};
  const bool got = std::fgets(line, sizeof line, p) != nullptr;
#ifdef _WIN32
  _pclose(p);
#else
  pclose(p);
#endif
  return got ? std::atoll(line) : -1;
}

Take take(double fps, int64_t steps, const fs::path& dir, bool ffmpeg) {
  Take t;
  t.fps = fps;
  gp::RecordOptions o;
  o.view = "flash";
  o.fps = fps;
  o.band = false;
  o.thumbs = 0;
  o.encoding.format = "mkv";
  o.sound = &gc::kSyncSound;
  o.video = (dir / ("sync-" + num::to_string(fps) + "fps.mkv")).string();
  const auto rec = gp::record(gc::kSyncGame, gc::sync_log(steps, kPeriod, kFirst), o, t.error);
  if (!rec) return t;
  const std::vector<uint8_t> bytes = read_all(o.video);
  const auto f = gc::read_mkv(bytes, t.error);
  if (!f) return t;
  t.frames = rec->frames;
  t.samples = rec->samples;
  t.bytes = rec->bytes;
  for (const gc::ReadBlock& b : f->blocks) {
    if (b.track == 1) {
      t.pictures.emplace_back(b.ms, bytes[b.offset] > 128);
    } else {
      const size_t n = b.size / (2 * sizeof(float));
      for (size_t i = 0; i < n; i++) {
        float l = 0;
        std::memcpy(&l, bytes.data() + b.offset + 2 * i * sizeof(float), sizeof(float));
        t.left.push_back(l);
      }
    }
  }
  constexpr double kRate = ge::audio::kRate;
  size_t from = 0;
  for (int64_t s = 1; s <= steps; s++) {
    if (!gc::sync_clicks_at(s, kPeriod, kFirst)) continue;
    Click c;
    c.step = s;
    c.due_ms = 1000.0 *
               static_cast<double>(gc::step_sample(s, 0, gc::kSyncGame.hz, ge::audio::kRate)) /
               kRate;
    size_t i = from;
    while (i < t.left.size() && std::fabs(t.left[i]) < 1e-6f) i++;
    if (i >= t.left.size()) break;  // the last click past the last picture's sound
    c.sound_ms = 1000.0 * static_cast<double>(i) / kRate;
    from = i + static_cast<size_t>(kRate / 10);
    for (const auto& [ms, lit] : t.pictures) {
      if (lit && static_cast<double>(ms) + 1 >= c.sound_ms) {
        c.lit_ms = static_cast<double>(ms);
        break;
      }
    }
    t.clicks.push_back(c);
  }
  if (ffmpeg) {
    t.ffmpeg_decodes =
        std::system(("ffmpeg -v error -i \"" + o.video + "\" -f null -" + kQuiet).c_str()) == 0;
    t.ffmpeg_frames = probe_frames(o.video);
  }
  return t;
}

// Each frame rate's first click, 120 ms either side: the pictures as boxes on the time line
// (white when lit), the sound's wave under them, the click's step as a red line.
gl::Picture strips(const std::vector<Take>& takes) {
  constexpr int kW = 960, kRow = 92, kHead = 24, kLeft = 90;
  constexpr double kSpan = 240;  // ms across
  gl::Picture p = gl::create_picture(kW, kHead + kRow * static_cast<int>(takes.size()) + 20,
                                     {250, 250, 252, 255});
  const gl::Rgba ink{30, 34, 40, 255}, faint{150, 156, 166, 255}, red{205, 40, 40, 255};
  gl::draw_text(p, 8, 8, "EACH FRAME RATE'S FIRST CLICK: PICTURES ABOVE, SOUND BELOW", ink);
  const double px = (kW - kLeft - 16) / kSpan;
  for (size_t r = 0; r < takes.size(); r++) {
    const Take& t = takes[r];
    if (t.clicks.empty()) continue;
    const Click& c = t.clicks.front();
    const double t0 = c.due_ms - kSpan / 2;
    const int y0 = kHead + kRow * static_cast<int>(r);
    gl::draw_text(p, 8, y0 + 18, num::to_string(t.fps) + " FPS", ink, 2);
    const auto x_of = [&](double ms) { return kLeft + (ms - t0) * px; };
    const double frame_ms = 1000.0 / t.fps;
    for (const auto& [ms, lit] : t.pictures) {
      const double m = static_cast<double>(ms);
      if (m + frame_ms < t0 || m > t0 + kSpan) continue;
      const double x = std::max<double>(kLeft, x_of(m));
      const double w = std::min<double>(x_of(m + frame_ms), kW - 16) - x;
      gl::fill_rect(p, x, y0 + 4, w - 1, 30,
                    lit ? gl::Rgba{255, 255, 255, 255} : gl::Rgba{44, 48, 56, 255});
      gl::draw_line(p, x, y0 + 4, x, y0 + 34, faint);
    }
    gl::draw_line(p, kLeft, y0 + 34, kW - 16, y0 + 34, faint);
    const double mid = y0 + 60;
    for (int x = kLeft; x < kW - 16; x++) {
      const double ms0 = t0 + (x - kLeft) / px, ms1 = t0 + (x + 1 - kLeft) / px;
      float lo = 0, hi = 0;
      for (auto i = static_cast<int64_t>(ms0 * ge::audio::kRate / 1000.0);
           i < static_cast<int64_t>(ms1 * ge::audio::kRate / 1000.0); i++) {
        if (i < 0 || i >= static_cast<int64_t>(t.left.size())) continue;
        lo = std::min(lo, t.left[static_cast<size_t>(i)]);
        hi = std::max(hi, t.left[static_cast<size_t>(i)]);
      }
      gl::draw_line(p, x, mid - hi * 40, x, mid - lo * 40, ink);
    }
    gl::draw_line(p, x_of(c.due_ms), y0 + 2, x_of(c.due_ms), y0 + kRow - 6, red);
    gl::draw_text(p, x_of(c.due_ms) + 4, y0 + kRow - 16,
                  "step " + std::to_string(c.step) + ", picture +" +
                      num::to_fixed(c.lit_ms - c.sound_ms, 1) + " ms",
                  red);
  }
  const int yb = kHead + kRow * static_cast<int>(takes.size()) + 4;
  gl::draw_text(p, kLeft, yb, "-120 ms", faint);
  gl::draw_text(p, kW / 2.0 + 30, yb, "the click's step", faint);
  gl::draw_text(p, kW - 70, yb, "+120 ms", faint);
  return p;
}

void run(gl::Lab& lab) {
  const double seconds = lab.arg_num("seconds");
  const std::vector<double> rates = lab.arg_nums("fps");
  const auto steps = static_cast<int64_t>(std::llround(seconds * gc::kSyncGame.hz));
  const fs::path dir = fs::temp_directory_path() / "ge-capture-lab";
  std::error_code ec;
  fs::create_directories(dir, ec);
  const bool ffmpeg = tool_found("ffmpeg") && tool_found("ffprobe");
  lab.note(ffmpeg ? "ffmpeg found: each file is decoded by it too"
                  : "ffmpeg not found: the files are read by the engine's reader alone");

  std::vector<Take> takes;
  gl::LabTable table{
      "Every frame rate: pictures, samples, MB, clicks, worst lag (picture after "
      "click), sound off its step",
      {"fps", "frames", "samples", "MB", "clicks", "worst-lag-ms", "sound-off-ms"},
      {}};
  double worst_off = 0;
  int64_t late = 0, early = 0, unshown = 0, mismatched = 0;
  for (const double fps : rates) {
    Take t = take(fps, steps, dir, ffmpeg);
    if (!t.error.empty()) {
      lab.fail("the export at " + num::to_string(fps) + " fps failed: " + t.error,
               "Run tests/capture_av_export_test.cpp (skill: replay).");
      continue;
    }
    double worst = 0, off = 0;
    for (const Click& c : t.clicks) {
      off = std::max(off, std::fabs(c.sound_ms - c.due_ms));
      if (c.lit_ms < 0) {
        unshown++;
        continue;
      }
      const double lag = c.lit_ms - c.sound_ms;
      worst = std::max(worst, lag);
      if (lag >= 1000.0 / fps) late++;
      if (lag <= -1) early++;
    }
    worst_off = std::max(worst_off, off);
    const int64_t expected = gc::frame_sample(t.frames, fps, ge::audio::kRate);
    if (t.samples != expected) mismatched++;
    table.rows.push_back({num::to_string(fps), std::to_string(t.frames), std::to_string(t.samples),
                          num::to_fixed(static_cast<double>(t.bytes) / 1e6, 2),
                          std::to_string(t.clicks.size()), num::to_fixed(worst, 2),
                          num::to_fixed(off, 3)});
    const std::string key = "fps" + num::to_string(fps);
    lab.metric(key + ".frames", static_cast<double>(t.frames), "count");
    lab.metric(key + ".worst-lag-ms", worst, "ms");
    if (ffmpeg) {
      lab.metric(key + ".ffmpeg-decodes", t.ffmpeg_decodes ? 1 : 0, "bool", gl::Det::measured);
      lab.metric(key + ".ffmpeg-frames", static_cast<double>(t.ffmpeg_frames), "count",
                 gl::Det::measured);
      if (!t.ffmpeg_decodes || t.ffmpeg_frames != t.frames) {
        lab.fail("ffmpeg read " + std::to_string(t.ffmpeg_frames) + " of " +
                     std::to_string(t.frames) + " pictures at " + num::to_string(fps) + " fps",
                 "Read the file with tests/capture_matroska_test.cpp's checks; compare the "
                 "muxer's elements with ffprobe -show_packets (skill: replay).");
      }
    }
    takes.push_back(std::move(t));
  }
  lab.table(std::move(table));
  lab.metric("late", static_cast<double>(late), "count");
  lab.metric("early", static_cast<double>(early), "count");
  lab.metric("unshown", static_cast<double>(unshown), "count");
  lab.metric("sound-off-ms", worst_off, "ms");
  lab.metric("length-mismatches", static_cast<double>(mismatched), "count");
  if (late || early || unshown) {
    lab.fail(std::to_string(late) + " flashes shown a frame or more after their click, " +
                 std::to_string(early) + " before it, " + std::to_string(unshown) + " never",
             "Read av_export.cpp's stepped and frame: the synth is rendered to a step's sample "
             "before its events (skill: replay).");
  }
  if (worst_off > 0.1) {
    lab.fail("a click sounded " + num::to_fixed(worst_off, 3) + " ms off its step's sample",
             "The step's sound must start on the block that starts on its sample: read "
             "av_export.cpp's render_to (skill: replay).");
  }
  if (mismatched) {
    lab.fail("the sound did not run exactly as long as the pictures",
             "Read av_export.cpp's close (skill: replay).");
  }

  // The uncompressed path's cost at 1280x720: the conversion and the muxer, a frame.
  {
    const gl::Picture frame = gl::create_picture(1280, 720, {90, 140, 200, 255});
    gc::MatroskaOptions mo;
    gc::VideoTrack v;
    v.width = 1280;
    v.height = 720;
    mo.video = v;
    gc::MatroskaWriter w;
    std::string error;
    std::vector<uint8_t> yuv;
    w.open("", mo, error);
    constexpr int kFrames = 30;
    const auto t0 = Clock::now();
    for (int k = 0; k < kFrames; k++) {
      gc::to_i420(frame, yuv);
      w.video(gc::frame_ms(k, 30), true, yuv, error);
    }
    const double ms =
        std::chrono::duration<double, std::milli>(Clock::now() - t0).count() / kFrames;
    lab.metric("720p.frame-ms", ms, "ms", gl::Det::measured);
    lab.metric("720p.mb-per-minute", static_cast<double>(gc::i420_bytes(1280, 720)) * 30 * 60 / 1e6,
               "MB");
  }

  lab.shot({"sync",
            "A CLICK AND A FLASH ON ONE STEP, AT " + std::to_string(rates.size()) + " FRAME RATES",
            "The sync fixture exported at each frame rate and read back from the file: each row "
            "is 240 ms round its first click, the pictures as boxes along the time line (white "
            "when the flash shows), the sound's wave under them, the click's step as the red "
            "line, and how long after the click the first white picture comes.",
            "The wave starting on the red line in every row, and the first white box starting "
            "on it or after it by less than one box's width.",
            {{"seconds", num::to_string(seconds)}, {"fps", lab.arg("fps")}}},
           strips(takes));
}

const gl::LabSpec kSpec{
    "capture",
    gl::LabKind::scenario,
    "engine",
    "engine/capture (av_export, matroska, mkv_read, sync_fixture), engine/play/record.cpp",
    "replay",
    "Does an export's sound land on its steps, and does a flash on a click's step show less than "
    "a frame after it, at every frame rate?",
    {{"seconds", "30", "how long each export runs"},
     {"fps", "24,25,30,60", "the frame rates exported"}},
    "--seconds=4 --fps=24,30",
    run,
};
GE_LAB(kSpec);

}  // namespace
