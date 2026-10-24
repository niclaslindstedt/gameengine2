// A run recorded (record.hpp).
//
// ge:summary record's definitions: the frame clock, the encoder's command line, the band, the sheet
// ge:keywords record ffmpeg popen pipe rawvideo rgba frames contact-sheet band thumbnails
// ge:skill agent-interface
// ge:test tests/play_record_test.cpp
#include "play/record.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "capture/av_export.hpp"
#include "core/num_text.hpp"
#include "lab/png.hpp"
#include "play/session.hpp"

namespace ge::play {

namespace {

namespace fs = std::filesystem;

constexpr int64_t kMostFrames = 1'000'000;  // a guard against a frame rate typed wrong
constexpr lab::Rgba kSheetBack{24, 24, 28, 255};
constexpr lab::Rgba kTitle{240, 240, 240, 255};
constexpr lab::Rgba kLabel{190, 200, 215, 255};
constexpr lab::Rgba kBandBack{0, 0, 0, 170};
constexpr lab::Rgba kBandText{255, 255, 255, 255};

// A word ffmpeg's command line takes as is: letters, digits and ._- (a codec, a preset, a rate).
bool plain_word(std::string_view w) {
  return std::all_of(w.begin(), w.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
           c == '_' || c == '-';
  });
}

std::string quoted(const std::string& s) { return "\"" + s + "\""; }

bool ffmpeg_found() {
#ifdef _WIN32
  return std::system("ffmpeg -version > NUL 2>&1") == 0;
#else
  return std::system("ffmpeg -version > /dev/null 2>&1") == 0;
#endif
}

std::FILE* open_pipe(const std::string& command) {
#ifdef _WIN32
  return _popen(command.c_str(), "wb");
#else
  return popen(command.c_str(), "w");  // NOLINT(cert-env33-c): the encoder is the point
#endif
}

int close_pipe(std::FILE* p) {
#ifdef _WIN32
  return _pclose(p);
#else
  return pclose(p);
#endif
}

// `p` grown to even sides (H.264 and VP9 in 4:2:0 need them), the new edge black.
lab::Picture even(lab::Picture p) {
  if (p.width % 2 == 0 && p.height % 2 == 0) return p;
  lab::Picture out =
      lab::create_picture(p.width + (p.width % 2), p.height + (p.height % 2), {0, 0, 0, 255});
  lab::blit(out, p, 0, 0);
  return out;
}

}  // namespace

std::string encoding_problem(const Encoding& e) {
  if (e.format != "mp4" && e.format != "webm" && e.format != "gif" && e.format != "png" &&
      e.format != "mkv") {
    return "no format `" + e.format + "`; the formats are mp4, webm, gif, mkv and png";
  }
  if (e.crf > 63) return "--crf " + std::to_string(e.crf) + " is over 63";
  for (const std::string* w : {&e.codec, &e.bitrate, &e.preset}) {
    if (!plain_word(*w)) return "`" + *w + "` is not a plain word (letters, digits and ._-)";
  }
  return "";
}

std::string_view extension_of(const Encoding& e) {
  if (e.format == "png") return "";
  return e.format;
}

std::string ffmpeg_command(const Encoding& e, int width, int height, double fps,
                           const std::string& file, const std::string& log) {
  std::string c = "ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgba -s " +
                  std::to_string(width) + "x" + std::to_string(height) + " -r " +
                  core::num::to_string(fps) + " -i -";
  if (e.format == "mp4") {
    c += " -c:v " + (e.codec.empty() ? std::string("libx264") : e.codec);
    c += " -preset " + (e.preset.empty() ? std::string("medium") : e.preset);
    c += " -pix_fmt yuv420p";
    c += e.bitrate.empty() ? " -crf " + std::to_string(e.crf < 0 ? 23 : e.crf)
                           : " -b:v " + e.bitrate;
    c += " -movflags +faststart";
  } else if (e.format == "webm") {
    c += " -c:v " + (e.codec.empty() ? std::string("libvpx-vp9") : e.codec);
    c += " -row-mt 1 -deadline good -cpu-used 4 -pix_fmt yuv420p";
    c += e.bitrate.empty() ? " -crf " + std::to_string(e.crf < 0 ? 32 : e.crf) + " -b:v 0"
                           : " -b:v " + e.bitrate;
  } else if (e.format == "gif") {
    // One palette made from the whole clip, then every frame mapped onto it.
    c += " -filter_complex \"[0:v]split[a][b];[a]palettegen[p];[b][p]paletteuse\"";
  }
  return c + " " + quoted(file) + " 2>> " + quoted(log);
}

std::vector<int64_t> frame_steps(double hz, int64_t from, int64_t to, double fps, double speed) {
  std::vector<int64_t> out;
  if (!(hz > 0) || !(fps > 0) || !(speed > 0) || from < 0 || to < from) return out;
  const double per_frame = speed * hz / fps;  // steps a frame
  for (int64_t k = 0; k < kMostFrames; k++) {
    const int64_t s = from + std::llround(static_cast<double>(k) * per_frame);
    if (s > to) break;
    out.push_back(s);
  }
  return out;
}

std::string clock_text(int64_t step, double hz) {
  const int64_t cs = hz > 0 ? std::llround(static_cast<double>(step) * 100.0 / hz) : 0;
  const int64_t m = cs / 6000, s = cs / 100 % 60, c = cs % 100;
  return std::to_string(m) + ":" + (s < 10 ? "0" : "") + std::to_string(s) + "." +
         (c < 10 ? "0" : "") + std::to_string(c);
}

void burn_band(lab::Picture& frame, std::string_view line) {
  const int scale = frame.width >= 960 ? 2 : 1;
  const int h = lab::kGlyphH * scale + 6;
  const size_t cols = static_cast<size_t>(std::max(0, (frame.width - 8) / (lab::kGlyphW * scale)));
  lab::fill_rect(frame, 0, frame.height - h, frame.width, h, kBandBack);
  lab::draw_text(frame, 4, frame.height - h + 3, line.substr(0, cols), kBandText, scale);
}

lab::Picture contact_sheet(std::string_view title,
                           const std::vector<std::pair<lab::Picture, std::string>>& thumbs,
                           int columns) {
  constexpr int kGap = 8;
  const int head = 2 * lab::kGlyphH + 2 * kGap;
  const int label = lab::kGlyphH + 6;
  int tw = 1, th = 1;
  for (const auto& [p, _] : thumbs) {
    tw = std::max(tw, p.width);
    th = std::max(th, p.height);
  }
  const int n = static_cast<int>(thumbs.size());
  const int cols = std::max(1, std::min(columns, n));
  const int rows = (n + cols - 1) / cols;
  const int width = std::max(kGap + cols * (tw + kGap), lab::text_width(title, 2) + 2 * kGap);
  lab::Picture out = lab::create_picture(width, head + rows * (th + label + kGap), kSheetBack);
  lab::draw_text(out, kGap, kGap, title, kTitle, 2);
  const size_t cells = static_cast<size_t>(std::max(1, tw / lab::kGlyphW));
  for (int i = 0; i < n; i++) {
    const int x = kGap + (i % cols) * (tw + kGap);
    const int y = head + (i / cols) * (th + label + kGap);
    lab::blit(out, thumbs[static_cast<size_t>(i)].first, x, y);
    lab::draw_text(out, x, y + th + 3,
                   std::string_view(thumbs[static_cast<size_t>(i)].second).substr(0, cells),
                   kLabel);
  }
  return out;
}

std::optional<Recording> record(const GameSpec& spec, const InputLog& log, const RecordOptions& opt,
                                std::string& error) {
  if (const std::string why = encoding_problem(opt.encoding); !why.empty()) {
    error = why;
    return std::nullopt;
  }
  const bool own = !opt.video.empty() && opt.encoding.format == "mkv";
  const bool video = !opt.video.empty() && opt.encoding.format != "png" && !own;
  const bool frames_dir = !opt.video.empty() && opt.encoding.format == "png";
  if (own && opt.sound && opt.speed != 1) {
    error =
        "an mkv's sound plays at the run's pace, and --speed is " + core::num::to_string(opt.speed);
    return std::nullopt;
  }
  if (video && !ffmpeg_found()) {
    error = "ffmpeg is not installed, and a " + opt.encoding.format + " needs it";
    return std::nullopt;
  }
  Lens lens;
  if (!open_lens(spec, LensAsk{opt.view, opt.gpu, opt.mode, opt.width, opt.height}, lens, error)) {
    return std::nullopt;
  }
  const int64_t to = opt.to < 0 ? log.steps() : std::min(opt.to, log.steps());
  const std::vector<int64_t> steps = frame_steps(spec.hz, opt.from, to, opt.fps, opt.speed);
  if (steps.empty()) {
    error = "no frame in steps " + std::to_string(opt.from) + ".." + std::to_string(to) +
            " (the log has " + std::to_string(log.steps()) + "; --fps and --speed over 0)";
    return std::nullopt;
  }
  Setup setup;
  if (!setup_of(spec, log, setup, error)) return std::nullopt;
  Session s(spec, setup);
  if (!s.ok()) {
    error = s.error();
    return std::nullopt;
  }

  // Which frames the sheet shows: `thumbs` of them, spread from the first to the last.
  const int64_t n = static_cast<int64_t>(steps.size());
  const int64_t picks = std::clamp<int64_t>(opt.thumbs, 0, n);
  std::vector<int64_t> pick;
  for (int64_t i = 0; i < picks; i++) pick.push_back(picks == 1 ? 0 : i * (n - 1) / (picks - 1));
  size_t next_pick = 0;
  std::vector<std::pair<lab::Picture, std::string>> thumbs;

  Recording rec;
  rec.view = lens.name();
  rec.from = steps.front();
  rec.to = steps.back();
  rec.frames = n;
  rec.seconds = static_cast<double>(n) / opt.fps;
  std::FILE* pipe = nullptr;
  std::error_code ec;
  if (frames_dir) fs::create_directories(opt.video, ec);
  capture::AvExport av;
  bool av_open = false;
  int64_t at = 0;
  for (int64_t k = 0; k < n; k++) {
    while (at < steps[static_cast<size_t>(k)]) {
      s.step(log.at(at++));
      if (av_open) av.stepped(s.run(), at);
    }
    lab::Picture raw;
    std::string caption;
    if (!shoot_lens(lens, s.run(), raw, caption, error)) break;
    if (k == 0) {
      rec.width = opt.width > 0 ? opt.width : raw.width;
      rec.height = opt.height > 0 ? opt.height : raw.height;
    }
    lab::Picture frame = raw.width == rec.width && raw.height == rec.height
                             ? std::move(raw)
                             : lab::resized(raw, rec.width, rec.height);
    frame = even(std::move(frame));
    if (k == 0) {
      rec.width = frame.width;
      rec.height = frame.height;
    }
    const std::string when = clock_text(at, spec.hz) + "  step " + std::to_string(at);
    if (next_pick < pick.size() && pick[next_pick] == k) {
      const int tw = std::min(frame.width, 240);
      const int th = std::max(1, frame.height * tw / std::max(1, frame.width));
      thumbs.emplace_back(lab::resized(frame, tw, th), when);
      next_pick++;
    }
    if (opt.band)
      burn_band(frame,
                std::string(when).append("  ").append(rec.view).append("  ").append(caption));
    if (video && !pipe) {
      rec.encoder = ffmpeg_command(opt.encoding, rec.width, rec.height, opt.fps, opt.video,
                                   opt.log.empty() ? opt.video + ".log" : opt.log);
      pipe = open_pipe(rec.encoder);
      if (!pipe) {
        error = "ffmpeg did not start: " + rec.encoder;
        return std::nullopt;
      }
    }
    if (own && !av_open) {
      capture::AvExportOptions ao;
      ao.file = opt.video;
      ao.width = frame.width;
      ao.height = frame.height;
      ao.fps = opt.fps;
      ao.hz = spec.hz;
      ao.from = at;
      ao.sound = opt.sound;
      if (!av.open(ao, error)) return std::nullopt;
      av_open = true;
    }
    if (av_open && !av.frame(s.run(), frame, error)) break;
    if (pipe && std::fwrite(frame.rgba.data(), 1, frame.rgba.size(), pipe) != frame.rgba.size()) {
      error = "ffmpeg stopped taking frames at frame " + std::to_string(k) + "; its log says why";
      break;
    }
    if (frames_dir) {
      char name[32];
      std::snprintf(name, sizeof name, "frame_%06lld.png", static_cast<long long>(k));
      if (!lab::write_png((fs::path(opt.video) / name).string(), frame)) {
        error = "cannot write a frame into " + opt.video;
        break;
      }
    }
  }
  rec.digest = s.run().digest();
  if (av_open) {
    std::string close_error;
    if (!av.close(close_error) && error.empty()) error = close_error;
    rec.samples = av.samples();
    rec.bytes = av.bytes();
  }
  if (pipe && close_pipe(pipe) != 0 && error.empty()) {
    error = "ffmpeg failed to encode " + opt.video + "; its log says why";
  }
  if (!error.empty()) return std::nullopt;
  if (!opt.sheet.empty()) {
    const std::string title = std::string(spec.name) + " " + rec.view + "  seed " +
                              core::num::to_string(setup.seed) + "  " +
                              clock_text(rec.from, spec.hz) + " to " + clock_text(rec.to, spec.hz) +
                              "  digest " + hex64(rec.digest);
    if (!lab::write_png(opt.sheet, contact_sheet(title, thumbs, opt.columns))) {
      error = "cannot write the contact sheet " + opt.sheet;
      return std::nullopt;
    }
  }
  return rec;
}

}  // namespace ge::play
