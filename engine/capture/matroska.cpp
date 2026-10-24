// The Matroska / WebM muxer (matroska.hpp).
//
// ge:summary matroska's definitions: the header, the clusters, the cues and the patches on close
// ge:keywords matroska ebml-header segment info tracks cluster simpleblock cues seek-head void
// ge:skill replay
// ge:test tests/capture_matroska_test.cpp
#include "capture/matroska.hpp"

#include <bit>
#include <cmath>
#include <cstring>

namespace ge::capture {
namespace {

// Element IDs (the Matroska specification's).
constexpr uint32_t kEbml = 0x1A45DFA3, kEbmlVersion = 0x4286, kEbmlReadVersion = 0x42F7,
                   kEbmlMaxIdLength = 0x42F2, kEbmlMaxSizeLength = 0x42F3, kDocType = 0x4282,
                   kDocTypeVersion = 0x4287, kDocTypeReadVersion = 0x4285;
constexpr uint32_t kSegment = 0x18538067, kSeekHead = 0x114D9B74, kSeek = 0x4DBB, kSeekId = 0x53AB,
                   kSeekPosition = 0x53AC, kVoid = 0xEC;
constexpr uint32_t kInfo = 0x1549A966, kTimestampScale = 0x2AD7B1, kDuration = 0x4489,
                   kMuxingApp = 0x4D80, kWritingApp = 0x5741;
constexpr uint32_t kTracks = 0x1654AE6B, kTrackEntry = 0xAE, kTrackNumber = 0xD7,
                   kTrackUid = 0x73C5, kTrackType = 0x83, kFlagLacing = 0x9C, kCodecId = 0x86,
                   kCodecPrivate = 0x63A2, kDefaultDuration = 0x23E383, kVideo = 0xE0,
                   kPixelWidth = 0xB0, kPixelHeight = 0xBA, kColourSpace = 0x2EB524, kAudio = 0xE1,
                   kSamplingFrequency = 0xB5, kChannels = 0x9F, kBitDepth = 0x6264;
constexpr uint32_t kCluster = 0x1F43B675, kTimestamp = 0xE7, kSimpleBlock = 0xA3;
constexpr uint32_t kCues = 0x1C53BB6B, kCuePoint = 0xBB, kCueTime = 0xB3, kCueTrackPositions = 0xB7,
                   kCueTrack = 0xF7, kCueClusterPosition = 0xF1;

constexpr int64_t kSeekHeadRoom = 128;      // the Void reserved at the segment's start
constexpr int64_t kMostRelativeMs = 32767;  // a block's timestamp is an int16 from its cluster's

// A 64-bit seek (a long is 32 bits on Windows, and an export can pass 2 GB).
bool seek_to(std::FILE* f, int64_t at, int whence) {
#ifdef _WIN32
  return _fseeki64(f, at, whence) == 0;
#else
  return fseeko(f, static_cast<off_t>(at), whence) == 0;
#endif
}

bool webm_codec(std::string_view c) {
  return c == "V_VP8" || c == "V_VP9" || c == "V_AV1" || c == "A_OPUS" || c == "A_VORBIS";
}

void seek_entry(Ebml& e, uint32_t id, int64_t position) {
  const size_t at = e.open(kSeek);
  std::vector<uint8_t> raw;
  for (int i = 3; i >= 0; i--) raw.push_back(static_cast<uint8_t>(id >> (8 * i)));
  e.bin(kSeekId, raw);
  // A fixed 8-byte position, so the seek head's size is known before the positions are.
  e.id(kSeekPosition);
  put_vint(e.bytes, 8);
  for (int i = 7; i >= 0; i--) e.bytes.push_back(static_cast<uint8_t>(position >> (8 * i)));
  e.close(at);
}

}  // namespace

std::string_view doc_type_of(const MatroskaOptions& o) {
  const bool v = !o.video || webm_codec(o.video->codec);
  const bool a = !o.audio || webm_codec(o.audio->codec);
  return v && a ? "webm" : "matroska";
}

std::string options_problem(const MatroskaOptions& o) {
  if (!o.video && !o.audio) return "no track: a video, an audio or both";
  if (o.video) {
    if (o.video->width <= 0 || o.video->height <= 0) return "the video has no size";
    if (!(o.video->fps > 0)) return "the video's frame rate is not over 0";
    if (o.video->codec == "V_UNCOMPRESSED" && o.video->colour_space.size() != 4) {
      return "V_UNCOMPRESSED needs a four-character colour space (I420)";
    }
  }
  if (o.audio && (o.audio->rate <= 0 || o.audio->channels <= 0 || o.audio->bits <= 0)) {
    return "the audio needs a rate, channels and a bit depth over 0";
  }
  if (o.cluster_ms <= 0 || o.cluster_ms > kMostRelativeMs) {
    return "cluster_ms must be in 1.." + std::to_string(kMostRelativeMs);
  }
  return "";
}

MatroskaWriter::~MatroskaWriter() {
  if (file_) std::fclose(file_);
}

bool MatroskaWriter::emit(std::span<const uint8_t> b, std::string& error) {
  if (file_) {
    if (std::fwrite(b.data(), 1, b.size(), file_) != b.size()) {
      error = "the disk took " + std::to_string(written_) + " bytes and no more";
      return false;
    }
  } else {
    memory_.insert(memory_.end(), b.begin(), b.end());
  }
  written_ += static_cast<int64_t>(b.size());
  return true;
}

bool MatroskaWriter::patch(int64_t at, std::span<const uint8_t> b, std::string& error) {
  if (!file_) {
    std::memcpy(memory_.data() + at, b.data(), b.size());
    return true;
  }
  if (!seek_to(file_, at, SEEK_SET) || std::fwrite(b.data(), 1, b.size(), file_) != b.size() ||
      !seek_to(file_, 0, SEEK_END)) {
    error = "cannot patch the file at byte " + std::to_string(at);
    return false;
  }
  return true;
}

bool MatroskaWriter::open(const std::string& path, const MatroskaOptions& o, std::string& error) {
  if (const std::string why = options_problem(o); !why.empty()) {
    error = why;
    return false;
  }
  opt_ = o;
  if (!path.empty()) {
    file_ = std::fopen(path.c_str(), "wb+");
    if (!file_) {
      error = "cannot write " + path;
      return false;
    }
  }
  Ebml h;
  const size_t ebml = h.open(kEbml);
  h.uint(kEbmlVersion, 1);
  h.uint(kEbmlReadVersion, 1);
  h.uint(kEbmlMaxIdLength, 4);
  h.uint(kEbmlMaxSizeLength, 8);
  h.text(kDocType, doc_type_of(o));
  h.uint(kDocTypeVersion, 4);
  h.uint(kDocTypeReadVersion, 2);
  h.close(ebml);
  h.id(kSegment);
  segment_size_at_ = static_cast<int64_t>(h.bytes.size());
  put_vint(h.bytes, kUnknownSize, 8);
  segment_body_ = static_cast<int64_t>(h.bytes.size());
  seek_head_at_ = segment_body_;
  h.id(kVoid);
  put_vint(h.bytes, kSeekHeadRoom - 9, 8);
  h.bytes.resize(h.bytes.size() + static_cast<size_t>(kSeekHeadRoom - 9), 0);

  info_at_ = static_cast<int64_t>(h.bytes.size()) - segment_body_;
  const size_t info = h.open(kInfo);
  h.uint(kTimestampScale, 1'000'000);  // a timestamp counts milliseconds
  h.real(kDuration, 0);
  duration_at_ = static_cast<int64_t>(h.bytes.size()) - 8;
  h.text(kMuxingApp, o.app);
  h.text(kWritingApp, o.app);
  h.close(info);

  tracks_at_ = static_cast<int64_t>(h.bytes.size()) - segment_body_;
  const size_t tracks = h.open(kTracks);
  uint64_t number = 1;
  if (o.video) {
    const VideoTrack& v = *o.video;
    const size_t t = h.open(kTrackEntry);
    h.uint(kTrackNumber, number);
    h.uint(kTrackUid, number);
    h.uint(kTrackType, 1);
    h.uint(kFlagLacing, 0);
    h.text(kCodecId, v.codec);
    if (!v.codec_private.empty()) h.bin(kCodecPrivate, v.codec_private);
    h.uint(kDefaultDuration, static_cast<uint64_t>(std::llround(1e9 / v.fps)));
    const size_t video = h.open(kVideo);
    h.uint(kPixelWidth, static_cast<uint64_t>(v.width));
    h.uint(kPixelHeight, static_cast<uint64_t>(v.height));
    if (v.codec == "V_UNCOMPRESSED") {
      h.bin(kColourSpace, std::span(reinterpret_cast<const uint8_t*>(v.colour_space.data()), 4));
    }
    h.close(video);
    h.close(t);
    number++;
  }
  if (o.audio) {
    const AudioTrack& a = *o.audio;
    const size_t t = h.open(kTrackEntry);
    h.uint(kTrackNumber, number);
    h.uint(kTrackUid, number);
    h.uint(kTrackType, 2);
    h.uint(kFlagLacing, 0);
    h.text(kCodecId, a.codec);
    if (!a.codec_private.empty()) h.bin(kCodecPrivate, a.codec_private);
    const size_t audio = h.open(kAudio);
    h.real(kSamplingFrequency, static_cast<double>(a.rate));
    h.uint(kChannels, static_cast<uint64_t>(a.channels));
    h.uint(kBitDepth, static_cast<uint64_t>(a.bits));
    h.close(audio);
    h.close(t);
  }
  h.close(tracks);
  open_ = emit(h.bytes, error);
  return open_;
}

bool MatroskaWriter::flush_cluster(std::string& error) {
  if (cluster_ms_ < 0) return true;
  cluster_.close(cluster_size_at_);
  cues_.push_back({cluster_ms_, written_ - segment_body_});
  const bool ok = emit(cluster_.bytes, error);
  cluster_.bytes.clear();
  cluster_ms_ = -1;
  return ok;
}

bool MatroskaWriter::block(int track, int64_t ms, bool key, std::span<const uint8_t> data,
                           std::string& error) {
  if (!open_) {
    error = "the muxer is not open";
    return false;
  }
  if (ms < 0 || ms < last_ms_[track]) {
    error = "track " + std::to_string(track) + "'s block at " + std::to_string(ms) +
            " ms is before its last at " + std::to_string(last_ms_[track]) + " ms";
    return false;
  }
  last_ms_[track] = ms;
  const bool video = opt_.video && track == 1;
  const bool cut_here = !opt_.video || (video && key);
  const bool due = cluster_ms_ < 0 || ms - cluster_ms_ >= opt_.cluster_ms ||
                   cluster_.bytes.size() >= opt_.cluster_bytes;
  if (cluster_ms_ >= 0 && ms - cluster_ms_ > kMostRelativeMs) {
    // Past what a block's int16 timestamp reaches: a cluster is cut wherever it stands.
    if (!flush_cluster(error)) return false;
  } else if (due && cut_here && !flush_cluster(error)) {
    return false;
  }
  if (cluster_ms_ < 0) {
    cluster_size_at_ = cluster_.open(kCluster);
    cluster_.uint(kTimestamp, static_cast<uint64_t>(ms));
    cluster_ms_ = ms;
  }
  const int64_t rel = ms - cluster_ms_;
  cluster_.id(kSimpleBlock);
  put_vint(cluster_.bytes, data.size() + 4);
  put_vint(cluster_.bytes, static_cast<uint64_t>(track));
  cluster_.bytes.push_back(static_cast<uint8_t>((rel >> 8) & 0xFF));
  cluster_.bytes.push_back(static_cast<uint8_t>(rel & 0xFF));
  cluster_.bytes.push_back(key || !video ? 0x80 : 0x00);
  cluster_.bytes.insert(cluster_.bytes.end(), data.begin(), data.end());
  return true;
}

bool MatroskaWriter::video(int64_t ms, bool key, std::span<const uint8_t> data,
                           std::string& error) {
  if (!opt_.video) {
    error = "the muxer has no video track";
    return false;
  }
  return block(1, ms, key, data, error);
}

bool MatroskaWriter::audio(int64_t ms, std::span<const uint8_t> data, std::string& error) {
  if (!opt_.audio) {
    error = "the muxer has no audio track";
    return false;
  }
  return block(opt_.video ? 2 : 1, ms, true, data, error);
}

bool MatroskaWriter::close(int64_t end_ms, std::string& error) {
  if (!open_) {
    error = "the muxer is not open";
    return false;
  }
  open_ = false;
  if (!flush_cluster(error)) return false;
  const int64_t cues_at = written_ - segment_body_;
  Ebml c;
  const size_t cues = c.open(kCues);
  for (const Cue& q : cues_) {
    const size_t p = c.open(kCuePoint);
    c.uint(kCueTime, static_cast<uint64_t>(q.ms));
    const size_t tp = c.open(kCueTrackPositions);
    c.uint(kCueTrack, 1);
    c.uint(kCueClusterPosition, static_cast<uint64_t>(q.position));
    c.close(tp);
    c.close(p);
  }
  c.close(cues);
  if (!emit(c.bytes, error)) return false;

  // The seek head over the reserved Void, and a smaller Void after it to fill the room.
  Ebml s;
  const size_t head = s.open(kSeekHead);
  seek_entry(s, kInfo, info_at_);
  seek_entry(s, kTracks, tracks_at_);
  seek_entry(s, kCues, cues_at);
  s.close(head);
  const int64_t left = kSeekHeadRoom - static_cast<int64_t>(s.bytes.size());
  s.id(kVoid);
  put_vint(s.bytes, static_cast<uint64_t>(left - 2), 1);
  s.bytes.resize(static_cast<size_t>(kSeekHeadRoom), 0);
  if (!patch(seek_head_at_, s.bytes, error)) return false;

  uint8_t duration[8];
  const uint64_t bits = std::bit_cast<uint64_t>(static_cast<double>(end_ms));
  for (int i = 0; i < 8; i++) duration[i] = static_cast<uint8_t>(bits >> (8 * (7 - i)));
  if (!patch(duration_at_, duration, error)) return false;

  std::vector<uint8_t> size;
  put_vint(size, static_cast<uint64_t>(written_ - segment_body_), 8);
  if (!patch(segment_size_at_, size, error)) return false;
  if (file_) {
    const bool ok = std::fclose(file_) == 0;
    file_ = nullptr;
    if (!ok) {
      error = "the file did not close (a full disk?)";
      return false;
    }
  }
  return true;
}

}  // namespace ge::capture
