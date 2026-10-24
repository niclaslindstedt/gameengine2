// A READER OF THE ENGINE'S OWN MATROSKA FILES, for the tests and the labs that check what the muxer
// wrote (capture/matroska.hpp): the doc type, the tracks, every block with its track, its time and
// its bytes, the cues, the seek head's targets and the duration. It walks the elements the muxer
// writes and skips any other; it is not a general demuxer (no lacing, no block groups, no
// encryption), and a player never needs it.
//
// ge:summary A reader of the engine's own Matroska files: tracks, blocks, cues, the seek head
// ge:keywords matroska mkv read parse demux blocks tracks cues seek-head duration test
// ge:skill replay
// ge:test tests/capture_matroska_test.cpp
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ge::capture {

struct ReadTrack {
  uint64_t number = 0;
  uint64_t type = 0;  // 1 video, 2 audio
  std::string codec;
  uint64_t width = 0, height = 0;
  std::string colour_space;
  double rate = 0;
  uint64_t channels = 0, bits = 0;
  uint64_t default_duration_ns = 0;
};

struct ReadBlock {
  uint64_t track = 0;
  int64_t ms = 0;  // the cluster's timestamp plus the block's
  bool key = false;
  size_t offset = 0, size = 0;  // the block's data in the file's bytes
};

struct ReadFile {
  std::string doc_type;
  uint64_t timestamp_scale = 0;
  double duration = 0;
  std::vector<ReadTrack> tracks;
  std::vector<ReadBlock> blocks;
  std::vector<int64_t> clusters;  // each cluster's offset from the segment's body
  std::vector<int64_t> cue_positions;
  std::vector<int64_t> seek_positions;  // the seek head's targets, from the segment's body
  bool segment_size_known = false;
};

// `bytes` read, or nothing with `error` naming the byte where it went wrong.
std::optional<ReadFile> read_mkv(std::span<const uint8_t> bytes, std::string& error);

}  // namespace ge::capture
