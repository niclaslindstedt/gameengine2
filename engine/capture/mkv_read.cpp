// A reader of the engine's own Matroska files (mkv_read.hpp).
//
// ge:summary mkv_read's definitions: the element walk, IDs and VINTs read back
// ge:keywords matroska read vint element walk
// ge:skill replay
// ge:test tests/capture_matroska_test.cpp
#include "capture/mkv_read.hpp"

#include <bit>

namespace ge::capture {
namespace {

struct Cursor {
  std::span<const uint8_t> b;
  size_t at = 0;
  std::string* error;

  bool fail(const std::string& why) {
    *error = why + " at byte " + std::to_string(at) +
             "; next: compare the file with ffprobe -show_packets (skill: replay)";
    return false;
  }
  // An ID: its length from the first byte's leading zeros, its marker kept.
  bool id(uint32_t& out) {
    if (at >= b.size()) return fail("an ID past the end");
    const int n = std::countl_zero(static_cast<uint8_t>(b[at])) + 1;
    if (n > 4 || at + static_cast<size_t>(n) > b.size()) return fail("a bad ID");
    out = 0;
    for (int i = 0; i < n; i++) out = (out << 8) | b[at++];
    return true;
  }
  // A VINT with its marker cleared; `unknown` when every value bit is set.
  bool vint(uint64_t& out, bool* unknown = nullptr) {
    if (at >= b.size()) return fail("a size past the end");
    const int n = std::countl_zero(static_cast<uint8_t>(b[at])) + 1;
    if (n > 8 || at + static_cast<size_t>(n) > b.size()) return fail("a bad size");
    out = b[at++] & (0xFFu >> n);
    for (int i = 1; i < n; i++) out = (out << 8) | b[at++];
    if (unknown) *unknown = out == (uint64_t{1} << (7 * n)) - 1;
    return true;
  }
};

uint64_t be(std::span<const uint8_t> b) {
  uint64_t v = 0;
  for (const uint8_t c : b) v = (v << 8) | c;
  return v;
}

double real(std::span<const uint8_t> b) {
  if (b.size() == 4) return std::bit_cast<float>(static_cast<uint32_t>(be(b)));
  return std::bit_cast<double>(be(b));
}

std::string text(std::span<const uint8_t> b) { return {b.begin(), b.end()}; }

// The masters the walk goes into; every other element is a leaf it reads or skips.
bool master(uint32_t id) {
  switch (id) {
    case 0x1A45DFA3:  // EBML
    case 0x18538067:  // Segment
    case 0x114D9B74:  // SeekHead
    case 0x4DBB:      // Seek
    case 0x1549A966:  // Info
    case 0x1654AE6B:  // Tracks
    case 0xAE:        // TrackEntry
    case 0xE0:        // Video
    case 0xE1:        // Audio
    case 0x1F43B675:  // Cluster
    case 0x1C53BB6B:  // Cues
    case 0xBB:        // CuePoint
    case 0xB7:        // CueTrackPositions
      return true;
    default:
      return false;
  }
}

}  // namespace

std::optional<ReadFile> read_mkv(std::span<const uint8_t> bytes, std::string& error) {
  ReadFile f;
  Cursor c{bytes, 0, &error};
  size_t segment_body = 0;
  int64_t cluster_ms = 0;
  // The ends of the masters the walk is inside, innermost last (SIZE_MAX: unknown).
  std::vector<std::pair<uint32_t, size_t>> open;
  while (c.at < bytes.size()) {
    while (!open.empty() && c.at >= open.back().second) open.pop_back();
    const size_t start = c.at;
    uint32_t id = 0;
    uint64_t size = 0;
    bool unknown = false;
    if (!c.id(id) || !c.vint(size, &unknown)) return std::nullopt;
    if (master(id)) {
      if (id == 0x18538067) {
        segment_body = c.at;
        f.segment_size_known = !unknown;
      }
      if (id == 0x1F43B675) {
        // A cluster at the segment's level ends whatever cluster was open before it.
        while (!open.empty() && open.back().first == 0x1F43B675) open.pop_back();
        f.clusters.push_back(static_cast<int64_t>(start - segment_body));
      }
      if (id == 0xAE) f.tracks.emplace_back();
      open.emplace_back(id, unknown ? SIZE_MAX : c.at + size);
      continue;
    }
    if (c.at + size > bytes.size()) {
      c.at = start;
      c.fail("element " + std::to_string(id) +
             "'s body runs past the end (a file cut short); next: read it up to its last whole "
             "cluster (skill: replay)");
      return std::nullopt;
    }
    const std::span<const uint8_t> body = bytes.subspan(c.at, size);
    const uint32_t parent = open.empty() ? 0 : open.back().first;
    ReadTrack* t = f.tracks.empty() ? nullptr : &f.tracks.back();
    switch (id) {
      case 0x4282:
        f.doc_type = text(body);
        break;
      case 0x2AD7B1:
        f.timestamp_scale = be(body);
        break;
      case 0x4489:
        f.duration = real(body);
        break;
      case 0x53AC:
        f.seek_positions.push_back(static_cast<int64_t>(be(body)));
        break;
      case 0xE7:
        cluster_ms = static_cast<int64_t>(be(body));
        break;
      case 0xF1:
        f.cue_positions.push_back(static_cast<int64_t>(be(body)));
        break;
      case 0xA3: {
        Cursor b{body, 0, &error};
        uint64_t track = 0;
        if (!b.vint(track) || b.at + 3 > body.size()) return std::nullopt;
        const auto rel = static_cast<int16_t>((body[b.at] << 8) | body[b.at + 1]);
        ReadBlock k;
        k.track = track;
        k.ms = cluster_ms + rel;
        k.key = (body[b.at + 2] & 0x80) != 0;
        k.offset = c.at + b.at + 3;
        k.size = size - b.at - 3;
        f.blocks.push_back(k);
        break;
      }
      default:
        if (t && (parent == 0xAE || parent == 0xE0 || parent == 0xE1)) {
          switch (id) {
            case 0xD7:
              t->number = be(body);
              break;
            case 0x83:
              t->type = be(body);
              break;
            case 0x86:
              t->codec = text(body);
              break;
            case 0x23E383:
              t->default_duration_ns = be(body);
              break;
            case 0xB0:
              t->width = be(body);
              break;
            case 0xBA:
              t->height = be(body);
              break;
            case 0x2EB524:
              t->colour_space = text(body);
              break;
            case 0xB5:
              t->rate = real(body);
              break;
            case 0x9F:
              t->channels = be(body);
              break;
            case 0x6264:
              t->bits = be(body);
              break;
            default:
              break;
          }
        }
        break;
    }
    c.at += size;
  }
  return f;
}

}  // namespace ge::capture
