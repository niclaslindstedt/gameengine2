// The save file's bytes: the header and directory laid out and checked, and the refusals.
//
// ge:summary The save file's layout written and checked: header, padding, directory, refusals
// ge:keywords save file header directory entry padding checksum refuse peek layout
// ge:skill save
// ge:test tests/save_file_test.cpp
#include "save/file.hpp"

#include <cstdio>
#include <cstring>

#include "save/xxh64.hpp"

namespace ge::save {
namespace {

constexpr char kMagic[8] = {'G', 'E', 'S', 'A', 'V', 'E', '\r', '\n'};
constexpr std::string_view kDamaged =
    "the file is damaged: load another save (a slot's .bak, or an older autosave)";

template <class T>
void put_at(uint8_t* p, T v) {
  std::memcpy(p, &v, sizeof v);
}
template <class T>
T get_at(const uint8_t* p) {
  T v;
  std::memcpy(&v, p, sizeof v);
  return v;
}

size_t pad8(size_t n) { return (n + 7) & ~size_t{7}; }

std::string hex(uint64_t v) {
  char b[24];
  std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
  return b;
}

std::string chunk_name(uint32_t t, uint32_t instance) {
  return "chunk " + tag_name(t) + "#" + std::to_string(instance);
}

void write_header(uint8_t* p, const Header& h) {
  std::memcpy(p, kMagic, 8);
  put_at<uint16_t>(p + 8, h.format);
  put_at<uint16_t>(p + 10, static_cast<uint16_t>(h.kind));
  put_at<uint32_t>(p + 12, h.chunks);
  put_at<uint64_t>(p + 16, h.directory);
  put_at<uint64_t>(p + 24, h.size);
  put_at<uint64_t>(p + 32, h.game_hash);
  put_at<uint32_t>(p + 40, h.game_version);
  put_at<uint32_t>(p + 44, h.meta_size);
  put_at<uint64_t>(p + 48, h.meta_checksum);
  put_at<uint64_t>(p + 56, xxh64(p, 56));
}

// The header at `p` (64 bytes) into `h`; "" or what is wrong with it.
std::string read_header(const uint8_t* p, Header& h) {
  if (std::memcmp(p, kMagic, 6) != 0) return "not a save file (no GESAVE magic at byte 0)";
  if (std::memcmp(p + 6, kMagic + 6, 2) != 0)
    return "the magic's line ending was rewritten (copied as text?)";
  if (get_at<uint64_t>(p + 56) != xxh64(p, 56))
    return "the header's checksum " + hex(xxh64(p, 56)) + " is not the " +
           hex(get_at<uint64_t>(p + 56)) + " it was written with";
  h.format = get_at<uint16_t>(p + 8);
  h.kind = static_cast<Kind>(get_at<uint16_t>(p + 10));
  h.chunks = get_at<uint32_t>(p + 12);
  h.directory = get_at<uint64_t>(p + 16);
  h.size = get_at<uint64_t>(p + 24);
  h.game_hash = get_at<uint64_t>(p + 32);
  h.game_version = get_at<uint32_t>(p + 40);
  h.meta_size = get_at<uint32_t>(p + 44);
  h.meta_checksum = get_at<uint64_t>(p + 48);
  if (h.format > kFormat)
    return "format " + std::to_string(h.format) + " is newer than this build's " +
           std::to_string(kFormat) + "; load it with a newer build of the game";
  if (h.format == 0) return "format 0 is not a format";
  return "";
}

bool zeros(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; i++)
    if (p[i]) return false;
  return true;
}

}  // namespace

std::string tag_name(uint32_t t) {
  std::string s(4, ' ');
  for (int i = 0; i < 4; i++) {
    const auto c = static_cast<char>((t >> (8 * i)) & 0xff);
    if (c < 0x20 || c > 0x7e) return "0x" + hex(t).substr(8);
    s[i] = c;
  }
  return s;
}

// --- writing ---

FileWriter::FileWriter(std::vector<uint8_t>& out, const Meta& meta)
    : out_(&out), writer_(out, kMetaVersion) {
  out.clear();
  out.resize(kHeaderBytes);
  header_.format = kFormat;
  header_.kind = meta.kind;
  header_.game_hash = xxh64(meta.game.data(), meta.game.size());
  header_.game_version = meta.game_version;
  chunk(kMetaTag, 0, kMetaVersion)(meta);
}

Writer& FileWriter::chunk(uint32_t t, uint32_t instance, uint16_t version) {
  close_chunk();
  out_->resize(pad8(out_->size()));
  ChunkInfo c;
  c.tag = t;
  c.instance = instance;
  c.version = version;
  c.offset = out_->size();
  chunks_.push_back(c);
  writer_ = Writer(*out_, version);
  open_ = true;
  return writer_;
}

void FileWriter::close_chunk() {
  if (!open_) return;
  ChunkInfo& c = chunks_.back();
  c.stored = c.raw = out_->size() - c.offset;
  c.checksum = xxh64(out_->data() + c.offset, c.stored);
  open_ = false;
}

bool FileWriter::finish(std::string* problem) {
  close_chunk();
  for (size_t i = 0; i < chunks_.size(); i++)
    for (size_t j = 0; j < i; j++)
      if (chunks_[i].tag == chunks_[j].tag && chunks_[i].instance == chunks_[j].instance) {
        if (problem)
          *problem = chunk_name(chunks_[i].tag, chunks_[i].instance) +
                     " was written twice: give each chunk of a kind its own instance";
        return false;
      }
  out_->resize(pad8(out_->size()));
  header_.chunks = static_cast<uint32_t>(chunks_.size());
  header_.directory = out_->size();
  const size_t dir_bytes = chunks_.size() * kEntryBytes;
  out_->resize(out_->size() + dir_bytes + 8);
  uint8_t* d = out_->data() + header_.directory;
  for (const ChunkInfo& c : chunks_) {
    put_at<uint32_t>(d, c.tag);
    put_at<uint32_t>(d + 4, c.instance);
    put_at<uint16_t>(d + 8, c.version);
    put_at<uint8_t>(d + 10, static_cast<uint8_t>(c.codec));
    put_at<uint8_t>(d + 11, 0);
    put_at<uint32_t>(d + 12, 0);
    put_at<uint64_t>(d + 16, c.offset);
    put_at<uint64_t>(d + 24, c.stored);
    put_at<uint64_t>(d + 32, c.raw);
    put_at<uint64_t>(d + 40, c.checksum);
    d += kEntryBytes;
  }
  put_at<uint64_t>(d, xxh64(out_->data() + header_.directory, dir_bytes));
  header_.size = out_->size();
  header_.meta_size = static_cast<uint32_t>(chunks_[0].stored);
  header_.meta_checksum = chunks_[0].checksum;
  write_header(out_->data(), header_);
  return true;
}

// --- reading ---

bool FileReader::refuse(const std::string& what, std::string_view next) {
  problem_ = name_ + ": " + what;
  if (!next.empty()) problem_ += "; " + std::string(next);
  return false;
}

bool FileReader::open(std::span<const uint8_t> bytes, std::string_view name,
                      std::string_view game) {
  bytes_ = bytes;
  name_ = name;
  chunks_.clear();
  meta_ = Meta{};
  header_ = Header{};
  problem_.clear();
  const size_t n = bytes.size();
  if (n < kHeaderBytes)
    return refuse(std::to_string(n) + " bytes is shorter than a save's 64-byte header", kDamaged);
  const std::string bad = read_header(bytes.data(), header_);
  if (!bad.empty()) return refuse(bad, header_.format > kFormat ? "" : kDamaged);
  if (header_.size != n)
    return refuse("the header says " + std::to_string(header_.size) + " bytes, the file has " +
                      std::to_string(n) + (n < header_.size ? " (cut off)" : ""),
                  kDamaged);
  const uint64_t dir = header_.directory;
  const uint64_t dir_bytes = uint64_t{header_.chunks} * kEntryBytes;
  if (header_.chunks == 0 || dir < kHeaderBytes || dir % 8 || dir > n || n - dir != dir_bytes + 8)
    return refuse("the directory (" + std::to_string(header_.chunks) + " chunks at byte " +
                      std::to_string(dir) + ") does not end the file",
                  kDamaged);
  const uint8_t* d = bytes.data() + dir;
  if (get_at<uint64_t>(d + dir_bytes) != xxh64(d, dir_bytes))
    return refuse("the directory's checksum does not match", kDamaged);
  chunks_.reserve(header_.chunks);
  uint64_t expect = kHeaderBytes;  // where the next chunk may start: chunks are in file order
  for (uint32_t i = 0; i < header_.chunks; i++, d += kEntryBytes) {
    ChunkInfo c;
    c.tag = get_at<uint32_t>(d);
    c.instance = get_at<uint32_t>(d + 4);
    c.version = get_at<uint16_t>(d + 8);
    c.codec = static_cast<Codec>(get_at<uint8_t>(d + 10));
    c.offset = get_at<uint64_t>(d + 16);
    c.stored = get_at<uint64_t>(d + 24);
    c.raw = get_at<uint64_t>(d + 32);
    c.checksum = get_at<uint64_t>(d + 40);
    const std::string what = chunk_name(c.tag, c.instance) + " (entry " + std::to_string(i) + ")";
    if (get_at<uint8_t>(d + 11) || get_at<uint32_t>(d + 12))
      return refuse(what + ": its reserved bytes are not zero", kDamaged);
    if (c.codec != Codec::stored)
      return refuse(what + ": codec " + std::to_string(static_cast<int>(c.codec)) +
                        " is not one this build reads",
                    "load it with a newer build of the game");
    if (c.raw != c.stored) return refuse(what + ": a stored chunk's sizes differ", kDamaged);
    if (c.offset != pad8(expect) || c.offset > dir || c.stored > dir - c.offset)
      return refuse(what + ": at byte " + std::to_string(c.offset) + " with " +
                        std::to_string(c.stored) +
                        " bytes, it is not where the chunk before it "
                        "ends",
                    kDamaged);
    if (!zeros(bytes.data() + expect, c.offset - expect))
      return refuse(what + ": the padding before it is not zero", kDamaged);
    const uint64_t sum = xxh64(bytes.data() + c.offset, c.stored);
    if (sum != c.checksum)
      return refuse(what + " at byte " + std::to_string(c.offset) + ": checksum " + hex(sum) +
                        ", written as " + hex(c.checksum),
                    kDamaged);
    if (i == 0 && (c.tag != kMetaTag || c.offset != kHeaderBytes || c.stored != header_.meta_size ||
                   c.checksum != header_.meta_checksum))
      return refuse("the first chunk is not the meta the header names", kDamaged);
    expect = c.offset + c.stored;
    chunks_.push_back(c);
  }
  if (!zeros(bytes.data() + expect, dir - expect))
    return refuse("the padding before the directory is not zero", kDamaged);
  if (!get(kMetaTag, 0, kMetaVersion, meta_)) return false;
  if (xxh64(meta_.game.data(), meta_.game.size()) != header_.game_hash ||
      meta_.kind != header_.kind || meta_.game_version != header_.game_version)
    return refuse("the meta does not agree with the header", kDamaged);
  if (!game.empty() && meta_.game != game)
    return refuse("a save of the game \"" + meta_.game + "\", not of \"" + std::string(game) + "\"",
                  "open it with that game");
  return true;
}

const ChunkInfo* FileReader::find(uint32_t t, uint32_t instance) const {
  for (const ChunkInfo& c : chunks_)
    if (c.tag == t && c.instance == instance) return &c;
  return nullptr;
}

std::vector<const ChunkInfo*> FileReader::all(uint32_t t) const {
  std::vector<const ChunkInfo*> out;
  for (const ChunkInfo& c : chunks_)
    if (c.tag == t) out.push_back(&c);
  return out;
}

bool FileReader::usable(const ChunkInfo* c, uint32_t t, uint32_t instance, uint16_t known) {
  if (!c)
    return refuse("has no " + chunk_name(t, instance),
                  "a game that wrote no such chunk: load it without that part, or check the "
                  "tag the save was written with (ge save inspect)");
  if (c->version > known)
    return refuse(chunk_name(t, instance) + " is version " + std::to_string(c->version) +
                      "; this build reads up to version " + std::to_string(known),
                  "load it with a newer build of the game");
  return true;
}

bool FileReader::finished(const ChunkInfo& c, const Reader& r) {
  if (!r.ok())
    return refuse(chunk_name(c.tag, c.instance) + " (version " + std::to_string(c.version) + ") " +
                      r.problem(),
                  "its describe reads differently from the one that wrote it: give the change "
                  "a chunk version and read it with since/until");
  if (!r.at_end())
    return refuse(chunk_name(c.tag, c.instance) + " (version " + std::to_string(c.version) +
                      ") has " + std::to_string(r.left()) + " bytes left after its fields",
                  "its describe reads fewer fields than the one that wrote it: give the change a "
                  "chunk version and read it with since/until");
  return true;
}

bool peek(std::span<const uint8_t> prefix, Header& header, Meta& meta, std::string& problem,
          size_t* needed) {
  if (needed) *needed = kHeaderBytes;
  if (prefix.size() < kHeaderBytes) {
    problem = "the first " + std::to_string(prefix.size()) + " bytes hold no whole header";
    return false;
  }
  problem = read_header(prefix.data(), header);
  if (!problem.empty()) return false;
  const size_t want = kHeaderBytes + header.meta_size;
  if (needed) *needed = want;
  if (prefix.size() < want) {
    problem = "the meta needs the first " + std::to_string(want) + " bytes";
    return false;
  }
  if (xxh64(prefix.data() + kHeaderBytes, header.meta_size) != header.meta_checksum) {
    problem = "the meta's checksum does not match";
    return false;
  }
  Reader r(prefix.data() + kHeaderBytes, header.meta_size, kMetaVersion);
  r(meta);
  if (!r.ok() || !r.at_end()) {
    problem = "the meta does not read: " + (r.ok() ? std::string("bytes left over") : r.problem());
    return false;
  }
  return true;
}

}  // namespace ge::save
