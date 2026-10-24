// A PACK ARCHIVE: the classic uncompressed container a game's content ships in. The file is a
// 12-byte header (the magic `PACK`, the directory's offset, its length in bytes), the files' bytes
// stored whole, and a directory of 64-byte entries (a path of up to 56 bytes, zero padded, then
// the file's offset and size). Paths use '/' and are matched as stored, case and all.
//
// `read_pak` checks the whole directory before it hands anything back: the magic, the directory
// inside the archive and a whole number of entries, every path terminated, and every file inside
// the archive, so a damaged or hostile archive is refused with the entry and the offset rather
// than read out of range. A file's bytes are then a view into the archive (`pak_file`), never a
// copy. `write_pak` makes an archive from named files, for tests and fixtures.
//
// This is the first slice of the virtual file system (docs/goals.md M18a): search paths, zip
// archives and directories over the same lookup come with the rest of that row.
//
// ge:summary Reads and writes the classic pack archive: a checked directory, files as views
// ge:keywords vfs pak pack archive container directory content files read write fixture refuse
// ge:skill vfs
// ge:test tests/vfs_pak_test.cpp
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ge::vfs {

struct PakEntry {
  std::string path;
  uint32_t offset = 0, size = 0;  // bytes into the archive
};

struct Pak {
  std::vector<PakEntry> entries;  // in the directory's order
};

// The directory of the archive in `bytes`; false with the reason in `*error`.
bool read_pak(std::span<const uint8_t> bytes, Pak* out, std::string* error);
// The entry stored under `path`, or nullptr. The first wins when a path is stored twice.
const PakEntry* pak_find(const Pak& pak, std::string_view path);
// A file's bytes as a view into the archive `bytes` the directory was read from.
std::span<const uint8_t> pak_file(std::span<const uint8_t> bytes, const PakEntry& entry);
// An archive of `files` (path, bytes) in the given order; false when a path is empty or longer
// than 55 bytes, or the archive would pass 4 GB.
bool write_pak(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files,
               std::vector<uint8_t>* out, std::string* error);

}  // namespace ge::vfs
