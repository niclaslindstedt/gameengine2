// A RUN RECORDED (`ge record`, `play record`): an input log (or the pilot's run of a seed) played
// back and one of the game's views drawn at a steady frame rate, each frame with a band naming its
// time, step and view, encoded as a video by ffmpeg (H.264 MP4, VP9 WebM or a GIF) or written as
// PNG frames, and a CONTACT SHEET beside it: the run at a glance, a grid of thumbnails each
// labelled with its time and step. A frame is the game's own CPU picture (game.hpp's views), so a
// recording needs no GPU, no window and reproduces from the log like everything else; with `gpu`
// it is the game's renderer's picture of a view in one of its modes (lit, wireframe, depth,
// overlay: game.hpp § THE GPU'S PICTURES), the renderer opened once and handed every frame in
// order.
//
// THE FRAME CLOCK: frame k shows the run at step `from + round(k * speed * hz / fps)`, while that
// is at most `to`: `speed` 2 plays the run at twice its pace, 0.25 in slow motion. The video
// lasts (to - from) / hz / speed seconds.
//
// THE ENCODER is ffmpeg, an external tool (`ffmpeg -version` finds it); frames go to it raw on
// a pipe, so nothing the size of the video is held in memory. Its own output is in `log`.
//
// `mkv` is the ENGINE'S OWN FILE (engine/capture/av_export.hpp), with no ffmpeg: the frames as
// uncompressed I420 in the engine's Matroska muxer and the run's SOUND beside them (the game's
// `SoundSpec`, play/game.hpp), rendered by the synth on the run's own clock, so it plays at
// --speed=1 only. It is large (width * height * 1.5 bytes a frame) until the encoders land
// (docs/replays.md, row M23d2).
//
// ge:summary A run recorded: a view drawn at a frame rate, encoded by ffmpeg, and a contact sheet
// ge:keywords record video recording mp4 webm gif h264 vp9 ffmpeg encode frames frame-rate speed
// ge:keywords contact-sheet thumbnails timestamps resolution crf bitrate frame_steps
// ge:keywords gpu-recording record-gpu mode lit wireframe depth overlay mkv sound
// ge:skill agent-interface
// ge:test tests/play_record_test.cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lab/raster.hpp"
#include "play/game.hpp"
#include "play/inputs.hpp"

namespace ge::play {

// How the frames become a file. `format` mp4 (H.264), webm (VP9), gif, mkv (the engine's own,
// with sound: above), or png (frames in a directory, no encoder). An empty `codec` is the format's
// own; `crf` < 0 its usual quality (23 for H.264, 32 for VP9: lower is better and larger); a
// `bitrate` ("4M") replaces the crf. `preset` is H.264's speed against size (ultrafast .. veryslow;
// default medium).
struct Encoding {
  std::string format = "mp4";
  std::string codec;
  int crf = -1;
  std::string bitrate;
  std::string preset;
};

// Why `e` cannot be encoded ("" when it can).
std::string encoding_problem(const Encoding& e);
// The file extension `e` writes ("" for png frames).
std::string_view extension_of(const Encoding& e);
// The ffmpeg command line that reads raw RGBA frames of `width` x `height` at `fps` from its
// standard input and writes `file`, its own output appended to `log`.
std::string ffmpeg_command(const Encoding& e, int width, int height, double fps,
                           const std::string& file, const std::string& log);

// The step each frame shows (THE FRAME CLOCK above); empty when the range holds none.
std::vector<int64_t> frame_steps(double hz, int64_t from, int64_t to, double fps, double speed);

// A step as the time on a run's clock: "m:ss.cc".
std::string clock_text(int64_t step, double hz);

struct RecordOptions {
  std::string view;  // "" the spec's first view (the renderer's, with gpu)
  bool gpu = false;  // the renderer's pictures in place of the game's CPU views
  std::string mode;  // with gpu: "" the renderer's first mode
  int64_t from = 0;
  int64_t to = -1;  // < 0: the log's end
  double fps = 30;
  double speed = 1;
  int width = 0, height = 0;  // 0: the view's own size (each made even for the encoder)
  bool band = true;           // the time, step and view burned into each frame
  Encoding encoding;
  const SoundSpec* sound = nullptr;  // mkv: the game's sound (null: no audio track)
  std::string video;  // the file (format != png), or the frames' directory (png); "" none
  std::string log;    // the encoder's output
  std::string sheet;  // the contact sheet's PNG; "" none
  int thumbs = 24;    // the sheet's thumbnails, spread evenly over the frames
  int columns = 6;
};

struct Recording {
  std::string view;  // the lens's name: "map", or "chase/depth" with gpu
  int width = 0, height = 0;
  int64_t frames = 0;
  int64_t from = 0, to = 0;
  double seconds = 0;   // the video's length
  uint64_t digest = 0;  // the run's at `to`
  std::string encoder;  // the ffmpeg command, "" for png frames and mkv
  int64_t samples = 0;  // mkv: the sound's samples a channel (0: no audio track)
  int64_t bytes = 0;    // mkv: the file's size
};

// `log` played from its start, `opt.view` drawn at every frame step from `opt.from` to `opt.to`,
// the frames sent to the encoder (or written as PNGs) and the contact sheet written.
std::optional<Recording> record(const GameSpec& spec, const InputLog& log, const RecordOptions& opt,
                                std::string& error);

// The time, step and view burned into the foot of `frame` as a dark band of one line.
void burn_band(lab::Picture& frame, std::string_view line);

// The contact sheet: `title` over a grid of `columns` thumbnails, each with its label under it.
lab::Picture contact_sheet(std::string_view title,
                           const std::vector<std::pair<lab::Picture, std::string>>& thumbs,
                           int columns);

}  // namespace ge::play
