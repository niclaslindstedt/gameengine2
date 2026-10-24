// The pack archive's reader and writer (pak.hpp).
//
// ge:summary The pack archive's checked directory reader, file views and fixture writer
// ge:keywords vfs pak pack archive directory read write refuse bounds
// ge:skill vfs
// ge:test tests/vfs_pak_test.cpp
#include "vfs/pak.hpp"

#include <cstddef>

namespace ge::vfs {
namespace {

constexpr size_t kHeader = 12, kEntry = 64, kPath = 56;

uint32_t u32(std::span<const uint8_t> b, size_t at) {
  return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 |
         uint32_t(b[at + 3]) << 24;
}

void put(std::vector<uint8_t>* out, size_t at, uint32_t v) {
  for (int k = 0; k < 4; ++k) (*out)[at + size_t(k)] = uint8_t(v >> (8 * k));
}

bool refuse(std::string* error, const std::string& what) {
  if (error) {
    *error = "pak: " + what + ". The archive is damaged or not a pack: fetch it again (skill: vfs)";
  }
  return false;
}

}  // namespace

bool read_pak(std::span<const uint8_t> bytes, Pak* out, std::string* error) {
  out->entries.clear();
  if (bytes.size() < kHeader || bytes[0] != 'P' || bytes[1] != 'A' || bytes[2] != 'C' ||
      bytes[3] != 'K') {
    return refuse(error, "no PACK magic in the first 4 bytes");
  }
  const uint32_t dir = u32(bytes, 4), len = u32(bytes, 8);
  if (dir > bytes.size() || len > bytes.size() - dir) {
    return refuse(error, "the directory (" + std::to_string(len) + " bytes at " +
                             std::to_string(dir) + ") runs past the archive's " +
                             std::to_string(bytes.size()) + " bytes");
  }
  if (len % kEntry != 0) {
    return refuse(error, "the directory's " + std::to_string(len) + " bytes are not whole " +
                             std::to_string(kEntry) + "-byte entries");
  }
  out->entries.reserve(len / kEntry);
  for (size_t i = 0; i < len / kEntry; ++i) {
    const size_t at = dir + i * kEntry;
    size_t n = 0;
    while (n < kPath && bytes[at + n] != 0) ++n;
    if (n == kPath || n == 0) {
      return refuse(error, "entry " + std::to_string(i) + " at byte " + std::to_string(at) +
                               (n == 0 ? " has an empty path" : "'s path is not terminated"));
    }
    PakEntry e;
    e.path.assign(reinterpret_cast<const char*>(&bytes[at]), n);
    e.offset = u32(bytes, at + kPath);
    e.size = u32(bytes, at + kPath + 4);
    if (e.offset > bytes.size() || e.size > bytes.size() - e.offset) {
      return refuse(error, "entry " + std::to_string(i) + " (" + e.path + ") lies past the end");
    }
    out->entries.push_back(std::move(e));
  }
  return true;
}

const PakEntry* pak_find(const Pak& pak, std::string_view path) {
  for (const PakEntry& e : pak.entries) {
    if (e.path == path) return &e;
  }
  return nullptr;
}

std::span<const uint8_t> pak_file(std::span<const uint8_t> bytes, const PakEntry& entry) {
  return bytes.subspan(entry.offset, entry.size);
}

bool write_pak(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files,
               std::vector<uint8_t>* out, std::string* error) {
  uint64_t total = kHeader + uint64_t(files.size()) * kEntry;
  for (const auto& [path, data] : files) {
    if (path.empty() || path.size() >= kPath) {
      if (error) *error = "pak: the path \"" + path + "\" is empty or over 55 bytes; shorten it";
      return false;
    }
    total += data.size();
  }
  if (total > 0xffffffffu) {
    if (error) *error = "pak: the archive would pass 4 GB; split the files over two archives";
    return false;
  }
  out->assign(kHeader, 0);
  std::vector<uint32_t> offsets;
  for (const auto& f : files) {
    offsets.push_back(uint32_t(out->size()));
    out->insert(out->end(), f.second.begin(), f.second.end());
  }
  const auto dir = uint32_t(out->size());
  for (size_t i = 0; i < files.size(); ++i) {
    const size_t at = out->size();
    out->resize(at + kEntry, 0);
    for (size_t k = 0; k < files[i].first.size(); ++k) (*out)[at + k] = uint8_t(files[i].first[k]);
    put(out, at + kPath, offsets[i]);
    put(out, at + kPath + 4, uint32_t(files[i].second.size()));
  }
  (*out)[0] = 'P', (*out)[1] = 'A', (*out)[2] = 'C', (*out)[3] = 'K';
  put(out, 4, dir);
  put(out, 8, uint32_t(files.size() * kEntry));
  return true;
}

}  // namespace ge::vfs
