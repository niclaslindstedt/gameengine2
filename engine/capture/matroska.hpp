// THE ENGINE'S MATROSKA AND WEBM MUXER (docs/replays.md § Video with sound): a video track and an
// audio track written as blocks in time order, in clusters, with the cues a player seeks by. WebM
// is Matroska with a narrower list of codecs; the file says `webm` when every track's codec is one
// WebM allows (VP8, VP9, AV1, Opus, Vorbis) and `matroska` otherwise (the uncompressed I420 and
// float PCM the export writes until an encoder is built, row M23d2).
//
// THE LAYOUT: the EBML header, then the SEGMENT, written with an unknown size and patched on
// close: a seek head (reserved as a Void, filled in on close with where the info, the tracks and
// the cues are), the info (the timestamp scale, 1 ms, and the duration, patched on close), the
// tracks, the clusters, and the cues at the end. A CLUSTER is held in memory while it is built and
// written whole when the next one starts: a new one starts at a video keyframe once the last has
// run `cluster_ms` or grown to `cluster_bytes` (with no video, by time alone). So a file cut short
// by a crash or a full disk still plays to its last whole cluster: its segment's size and its
// seek head are only ever patched, never needed.
//
// TIMESTAMPS are milliseconds from the start, handed in rising order per track; the caller
// interleaves the tracks by time (capture/av_export.hpp hands each frame's sound before the next
// frame's picture).
//
// ge:summary The Matroska / WebM muxer: tracks, clusters, cues, a seek head patched on close
// ge:keywords matroska mkv webm muxer mux container cluster cues seek-head simpleblock segment
// ge:keywords v_uncompressed i420 a_pcm float doc-type track video audio keyframe
// ge:skill replay
// ge:test tests/capture_matroska_test.cpp
#pragma once

#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "capture/ebml.hpp"

namespace ge::capture {

struct VideoTrack {
  std::string codec = "V_UNCOMPRESSED";
  int width = 0, height = 0;
  std::string colour_space = "I420";  // V_UNCOMPRESSED's pixel layout as a four-character code
  double fps = 30;                    // written as the track's default frame duration
  std::vector<uint8_t> codec_private;
};

struct AudioTrack {
  std::string codec = "A_PCM/FLOAT/IEEE";
  int rate = 48000;
  int channels = 2;
  int bits = 32;
  std::vector<uint8_t> codec_private;
};

struct MatroskaOptions {
  std::optional<VideoTrack> video;
  std::optional<AudioTrack> audio;
  std::string app = "ge";  // the muxing and writing app's name in the info
  int64_t cluster_ms = 1000;
  size_t cluster_bytes = size_t{16} << 20;
};

// "webm" when every track's codec is one WebM allows, else "matroska".
std::string_view doc_type_of(const MatroskaOptions& o);
// Why `o` cannot be written ("" when it can).
std::string options_problem(const MatroskaOptions& o);

class MatroskaWriter {
 public:
  MatroskaWriter() = default;
  MatroskaWriter(const MatroskaWriter&) = delete;
  MatroskaWriter& operator=(const MatroskaWriter&) = delete;
  ~MatroskaWriter();

  // Start a file at `path`, or with an empty path keep every byte in memory (`bytes()`).
  bool open(const std::string& path, const MatroskaOptions& o, std::string& error);
  // A block at `ms`. The tracks are numbered video 1, audio 2 (audio 1 with no video).
  bool video(int64_t ms, bool key, std::span<const uint8_t> data, std::string& error);
  bool audio(int64_t ms, std::span<const uint8_t> data, std::string& error);
  // The last cluster written, the cues, and the seek head, the duration and the segment's size
  // patched. `end_ms` is when the last block ends (the duration).
  bool close(int64_t end_ms, std::string& error);

  const std::vector<uint8_t>& bytes() const { return memory_; }
  int64_t written() const { return written_; }
  int64_t clusters() const { return static_cast<int64_t>(cues_.size()); }

 private:
  struct Cue {
    int64_t ms;
    int64_t position;  // the cluster's offset from the segment's body
  };
  bool emit(std::span<const uint8_t> b, std::string& error);
  bool patch(int64_t at, std::span<const uint8_t> b, std::string& error);
  bool block(int track, int64_t ms, bool key, std::span<const uint8_t> data, std::string& error);
  bool flush_cluster(std::string& error);

  MatroskaOptions opt_;
  std::FILE* file_ = nullptr;
  bool open_ = false;
  std::vector<uint8_t> memory_;
  int64_t written_ = 0;
  int64_t segment_size_at_ = 0;          // where the segment's 8-byte size is
  int64_t segment_body_ = 0;             // where the segment's body starts
  int64_t seek_head_at_ = 0;             // the reserved Void the seek head is written over
  int64_t duration_at_ = 0;              // the duration's 8 bytes
  int64_t info_at_ = 0, tracks_at_ = 0;  // offsets from the segment's body
  Ebml cluster_;
  size_t cluster_size_at_ = 0;
  int64_t cluster_ms_ = -1;  // < 0: no cluster open
  std::vector<Cue> cues_;
  int64_t last_ms_[3] = {-1, -1, -1};
};

}  // namespace ge::capture
