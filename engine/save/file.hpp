// THE SAVE FILE (docs/saves.md § The file): what a save is on disk, and how it is written and
// opened. Every number little-endian; every part checksummed (xxh64.hpp), so every byte of a
// file is covered and a damaged file is refused before a field is read.
//
//   header     64 bytes at 0: the magic "GESAVE\r\n", the format's version, the file's kind,
//              the chunk count, the directory's offset, the file's size, the game's name hash
//              and save version, the meta chunk's size and checksum, the header's checksum
//   meta       the first chunk, at 64: what a menu lists (Meta), readable from the file's first
//              64 + meta_size bytes with `peek`, checked by the header's copy of its checksum
//   chunks     the state: each a TAG (four letters), an instance and its own version, at an
//              8-byte boundary with zero padding between (so the padding is checked too)
//   directory  at the end: one 48-byte entry a chunk (tag, instance, version, codec, offset,
//              stored and raw size, checksum), then the directory's own checksum
//
// THE WRITER builds a file in one buffer the caller keeps (so a game that saves every few
// minutes reuses its capacity): the meta at once, then chunk after chunk, each appended by a
// `Writer` (io.hpp), then `finish` closes it. THE READER opens a file's bytes, checks the header,
// the directory and every chunk's checksum, and reads a chunk into a described value with `get`.
// Every refusal is one line naming the file, the part and the next action.
//
// ge:summary The save file: header, meta first, tagged versioned chunks, directory, checksums
// ge:keywords save file format header magic chunk tag instance directory meta peek checksum
// ge:keywords file-writer file-reader open get find refuse damaged truncated version kind
// ge:keywords autosave slot character partial game-version
// ge:skill save
// ge:test tests/save_file_test.cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "save/io.hpp"

namespace ge::save {

// The format this build writes and the newest it reads.
inline constexpr uint16_t kFormat = 1;
inline constexpr size_t kHeaderBytes = 64;
inline constexpr size_t kEntryBytes = 48;

// What a file holds. A game may number kinds of its own from 100.
enum class Kind : uint16_t {  // NOLINT(performance-enum-size): the header's 16-bit field
  run = 1,                    // a whole run: what `Run::save` writes
  character = 2,              // part of a run carried to another (docs/saves.md § Part of a run)
};

// A chunk's tag from its four letters: tag("PLYR").
constexpr uint32_t tag(const char (&s)[5]) {
  return static_cast<uint32_t>(static_cast<uint8_t>(s[0])) |
         static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 8 |
         static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 16 |
         static_cast<uint32_t>(static_cast<uint8_t>(s[3])) << 24;
}
// "PLYR", or "0x…" when a letter is not printable.
std::string tag_name(uint32_t tag);

inline constexpr uint32_t kMetaTag = tag("META");
inline constexpr uint16_t kMetaVersion = 1;

// What a menu lists about a save, written first so a listing reads only the file's front.
struct Meta {
  std::string game;           // the game's name (its GameSpec's): a save of another is refused
  uint32_t game_version = 0;  // the game's save version when written
  Kind kind = Kind::run;
  std::string title;       // the line a menu shows ("Seed 38, the red run")
  std::string place;       // where in the run ("the gondola's top")
  double play_s = 0;       // seconds played
  int64_t saved_unix = 0;  // when it was saved, seconds since 1970 (the caller's clock)
  std::string setup;       // what reopens the run: the input log's header lines
};

template <class Io>
void describe(Io& io, Meta& m) {
  io(m.game);
  io(m.game_version);
  io(m.kind);
  io(m.title);
  io(m.place);
  io(m.play_s);
  io(m.saved_unix);
  io(m.setup);
}

// How a chunk's bytes are stored. Only `stored` is written so far; the field keeps room for a
// compressor without changing the format.
enum class Codec : uint8_t { stored = 0 };

struct Header {
  uint16_t format = 0;
  Kind kind = Kind::run;
  uint32_t chunks = 0;
  uint64_t directory = 0;  // the directory's offset
  uint64_t size = 0;       // the whole file's bytes
  uint64_t game_hash = 0;  // xxh64 of the game's name
  uint32_t game_version = 0;
  uint32_t meta_size = 0;
  uint64_t meta_checksum = 0;
};

struct ChunkInfo {
  uint32_t tag = 0;
  uint32_t instance = 0;
  uint16_t version = 0;
  Codec codec = Codec::stored;
  uint64_t offset = 0;
  uint64_t stored = 0;  // bytes in the file
  uint64_t raw = 0;     // bytes once decoded
  uint64_t checksum = 0;
};

class FileWriter {
 public:
  // Starts a file in `out` (cleared; its capacity kept) with `meta` as its first chunk.
  FileWriter(std::vector<uint8_t>& out, const Meta& meta);

  // A new chunk: write its fields through the returned Writer until the next `chunk` or
  // `finish`. A tag and instance written twice is refused by `finish`.
  Writer& chunk(uint32_t tag, uint32_t instance, uint16_t version);
  // A whole chunk of one described value.
  template <class T>
  void put(uint32_t tag, uint32_t instance, uint16_t version, const T& v) {
    chunk(tag, instance, version)(v);
  }
  // Closes the last chunk and writes the directory and the header: the file is `out`. False,
  // with `problem` saying why, when a chunk was written twice.
  bool finish(std::string* problem = nullptr);

 private:
  void close_chunk();
  std::vector<uint8_t>* out_;
  std::vector<ChunkInfo> chunks_;
  Writer writer_;
  Header header_;
  bool open_ = false;
};

class FileReader {
 public:
  // Opens `bytes` (which must outlive the reader), named `name` in every refusal: the header,
  // the directory, the padding and every chunk's checksum are checked. `game`, when not empty,
  // must be the game that wrote it. False with `problem()` saying why.
  bool open(std::span<const uint8_t> bytes, std::string_view name = "save",
            std::string_view game = {});

  const Header& header() const { return header_; }
  const Meta& meta() const { return meta_; }
  std::span<const ChunkInfo> chunks() const { return chunks_; }
  // The chunk `tag` `instance`, or null.
  const ChunkInfo* find(uint32_t tag, uint32_t instance = 0) const;
  // Every instance of `tag`, in the order written.
  std::vector<const ChunkInfo*> all(uint32_t tag) const;

  // Reads chunk `tag` `instance` into `v`. False, with `problem()` saying why, when it is missing,
  // newer than `known` (the newest version this build's describe reads), its bytes do not read
  // or some are left over.
  template <class T>
  bool get(uint32_t tag, uint32_t instance, uint16_t known, T& v) {
    const ChunkInfo* c = find(tag, instance);
    if (!usable(c, tag, instance, known)) return false;
    Reader r(bytes_.data() + c->offset, c->raw, c->version);
    r(v);
    return finished(*c, r);
  }
  // A Reader over chunk `c`'s bytes, for a describe that walks one chunk by hand.
  Reader reader(const ChunkInfo& c) const {
    return Reader(bytes_.data() + c.offset, c.raw, c.version);
  }
  const std::string& problem() const { return problem_; }

 private:
  bool refuse(const std::string& what, std::string_view next);
  bool usable(const ChunkInfo* c, uint32_t tag, uint32_t instance, uint16_t known);
  bool finished(const ChunkInfo& c, const Reader& r);
  std::span<const uint8_t> bytes_;
  std::string name_;
  Header header_;
  Meta meta_;
  std::vector<ChunkInfo> chunks_;
  std::string problem_;
};

// The header and the meta from a file's first `prefix` bytes (64 + the header's `meta_size` are
// enough; `kPeekBytes` is a size that holds any usual meta), checked by their checksums: what a
// list of saves reads without opening the rest. False with `problem` saying why (`needed` the
// bytes it needs when `prefix` was too short).
inline constexpr size_t kPeekBytes = 4096;
bool peek(std::span<const uint8_t> prefix, Header& header, Meta& meta, std::string& problem,
          size_t* needed = nullptr);

}  // namespace ge::save
