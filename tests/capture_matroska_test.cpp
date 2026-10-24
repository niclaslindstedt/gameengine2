// The Matroska / WebM muxer (engine/capture/matroska.hpp), read back by the engine's reader
// (mkv_read.hpp): the doc type follows the codecs, the tracks are numbered and described, every
// block comes back at its time and with its bytes, clusters are cut at keyframes by time and by
// size, the cues and the seek head point where they say, the duration and the segment's size are
// patched, a file cut at a cluster's end still reads, a block out of order is refused, and the
// bytes of one small file are pinned.
//
// ge:summary Tests: the Matroska muxer read back: tracks, blocks, clusters, cues, seek head, crash
// ge:keywords test matroska mkv webm muxer read cluster cues seek-head truncated golden
// ge:skill replay testing
// ge:test tests/capture_matroska_test.cpp
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "capture/matroska.hpp"
#include "capture/mkv_read.hpp"
#include "check.hpp"
#include "play/inputs.hpp"

namespace gc = ge::capture;

namespace {

gc::MatroskaOptions small(bool audio = true) {
  gc::MatroskaOptions o;
  gc::VideoTrack v;
  v.width = 4;
  v.height = 2;
  v.fps = 30;
  o.video = v;
  if (audio) o.audio = gc::AudioTrack{};
  return o;
}

// `frames` pictures of 12 bytes (picture k filled with k) and, before each, the last one's sound
// (8 bytes filled with 100 + k), as the export interleaves them.
std::vector<uint8_t> write(const gc::MatroskaOptions& o, int frames, std::string& error) {
  gc::MatroskaWriter w;
  if (!w.open("", o, error)) return {};
  for (int k = 0; k < frames; k++) {
    const int64_t ms = k * 1000 / 30;
    if (k > 0 && o.audio) {
      const std::vector<uint8_t> pcm(8, static_cast<uint8_t>(100 + k - 1));
      if (!w.audio((k - 1) * 1000 / 30, pcm, error)) return {};
    }
    const std::vector<uint8_t> picture(12, static_cast<uint8_t>(k));
    if (!w.video(ms, true, picture, error)) return {};
  }
  if (!w.close(frames * 1000 / 30, error)) return {};
  return w.bytes();
}

uint32_t be32(const std::vector<uint8_t>& b, size_t at) {
  return (uint32_t{b[at]} << 24) | (uint32_t{b[at + 1]} << 16) | (uint32_t{b[at + 2]} << 8) |
         b[at + 3];
}

uint64_t fnv(std::span<const uint8_t> b) {
  uint64_t h = 0xcbf29ce484222325ull;
  for (const uint8_t c : b) h = (h ^ c) * 0x100000001B3ull;
  return h;
}

}  // namespace

TEST(the_doc_type_is_webm_only_when_every_codec_is_one_webm_allows) {
  gc::MatroskaOptions o = small();
  CHECK(gc::doc_type_of(o) == "matroska");
  CHECK(o.video.has_value() && o.audio.has_value());
  if (!o.video || !o.audio) return;
  o.video->codec = "V_VP9";
  CHECK(gc::doc_type_of(o) == "matroska");
  o.audio->codec = "A_OPUS";
  CHECK(gc::doc_type_of(o) == "webm");
  CHECK(gc::options_problem(gc::MatroskaOptions{}).find("no track") != std::string::npos);
}

TEST(every_block_comes_back_at_its_time_with_its_bytes) {
  std::string error;
  const std::vector<uint8_t> bytes = write(small(), 5, error);
  CHECK_MSG(error.empty(), error.c_str());
  const auto f = gc::read_mkv(bytes, error);
  CHECK_MSG(f.has_value(), error.c_str());
  if (!f) return;
  CHECK(f->doc_type == "matroska");
  CHECK(f->timestamp_scale == 1'000'000);
  CHECK(f->duration == 166.0);
  CHECK(f->segment_size_known);
  CHECK(f->tracks.size() == 2);
  CHECK(f->tracks[0].number == 1 && f->tracks[0].type == 1);
  CHECK(f->tracks[0].codec == "V_UNCOMPRESSED" && f->tracks[0].colour_space == "I420");
  CHECK(f->tracks[0].width == 4 && f->tracks[0].height == 2);
  CHECK(f->tracks[0].default_duration_ns == 33'333'333);
  CHECK(f->tracks[1].number == 2 && f->tracks[1].type == 2);
  CHECK(f->tracks[1].codec == "A_PCM/FLOAT/IEEE" && f->tracks[1].rate == 48000.0);
  CHECK(f->tracks[1].channels == 2 && f->tracks[1].bits == 32);
  CHECK(f->blocks.size() == 9);  // five pictures, four sounds between them
  int k = 0;
  for (const gc::ReadBlock& b : f->blocks) {
    if (b.track != 1) continue;
    CHECK(b.ms == k * 1000 / 30);
    CHECK(b.key && b.size == 12 && bytes[b.offset] == k && bytes[b.offset + 11] == k);
    k++;
  }
  CHECK(k == 5);
}

TEST(clusters_are_cut_at_a_keyframe_by_time_and_by_size_and_the_cues_point_at_them) {
  std::string error;
  const std::vector<uint8_t> bytes = write(small(), 91, error);  // 3 s at 30 fps
  const auto f = gc::read_mkv(bytes, error);
  CHECK_MSG(f.has_value(), error.c_str());
  if (!f) return;
  CHECK(f->clusters.size() == 4);  // a cluster a second, the last holding the 3 s picture
  CHECK(f->cue_positions == f->clusters);
  gc::MatroskaOptions tiny = small();
  tiny.cluster_bytes = 40;  // past two blocks
  const auto g = gc::read_mkv(write(tiny, 10, error), error);
  CHECK(g.has_value() && g->clusters.size() > 3);
}

TEST(the_seek_head_points_at_the_info_the_tracks_and_the_cues) {
  std::string error;
  const std::vector<uint8_t> bytes = write(small(), 5, error);
  const auto f = gc::read_mkv(bytes, error);
  CHECK(f.has_value() && f->seek_positions.size() == 3);
  if (!f || f->seek_positions.size() != 3) return;
  // The segment's body starts after its ID and its 8-byte size, after the EBML header.
  size_t body = 0;
  for (size_t i = 0; i + 4 <= bytes.size(); i++) {
    if (be32(bytes, i) == 0x18538067) {
      body = i + 12;
      break;
    }
  }
  CHECK(be32(bytes, body + static_cast<size_t>(f->seek_positions[0])) == 0x1549A966);
  CHECK(be32(bytes, body + static_cast<size_t>(f->seek_positions[1])) == 0x1654AE6B);
  CHECK(be32(bytes, body + static_cast<size_t>(f->seek_positions[2])) == 0x1C53BB6B);
}

TEST(a_file_cut_short_after_a_cluster_still_reads_up_to_it) {
  std::string error;
  const std::vector<uint8_t> bytes = write(small(), 91, error);
  const auto f = gc::read_mkv(bytes, error);
  if (!f) return;
  // Everything up to where the third cluster starts, as a crash would leave it.
  size_t body = 0;
  for (size_t i = 0; i + 4 <= bytes.size(); i++) {
    if (be32(bytes, i) == 0x18538067) {
      body = i + 12;
      break;
    }
  }
  const size_t cut = body + static_cast<size_t>(f->clusters[2]);
  std::vector<uint8_t> head(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut));
  // The segment's size as it stood before the patch: unknown.
  for (size_t i = 0; i < 8; i++) head[body - 8 + i] = i == 0 ? 0x01 : 0xFF;
  const auto g = gc::read_mkv(head, error);
  CHECK_MSG(g.has_value(), error.c_str());
  if (!g) return;
  CHECK(!g->segment_size_known);
  CHECK(g->clusters.size() == 2);
  int pictures = 0;
  for (const gc::ReadBlock& b : g->blocks) pictures += b.track == 1 ? 1 : 0;
  CHECK(pictures == 60);
}

TEST(a_block_before_its_tracks_last_is_refused_by_name) {
  gc::MatroskaWriter w;
  std::string error;
  CHECK(w.open("", small(false), error));
  const std::vector<uint8_t> picture(12, 0);
  CHECK(w.video(100, true, picture, error));
  CHECK(!w.video(50, true, picture, error));
  CHECK(error.find("before its last at 100 ms") != std::string::npos);
  CHECK(!w.audio(120, picture, error));
  CHECK(error.find("no audio track") != std::string::npos);
}

TEST(the_bytes_of_a_small_file_are_pinned) {
  std::string error;
  const std::vector<uint8_t> bytes = write(small(), 5, error);
  // Raised only by a deliberate change of the layout (say why in the PR).
  CHECK(ge::play::hex64(fnv(bytes)) == "43478c3035a03dfb");
  CHECK(bytes.size() == 575);
}
