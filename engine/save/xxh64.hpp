// THE SAVE FILE'S CHECKSUM: a 64-bit hash of a run of bytes by the XXH64 algorithm (its public
// specification), at several gigabytes a second on one core. Every chunk of a save, its directory
// and its header carry one (file.hpp), so a byte flipped by a disk, a cut-off copy or a hand
// edit is found on load before any field is read. Not a cryptographic hash: it detects damage,
// it does not resist forgery. Held to the algorithm's own published values by
// tests/save_xxh64_test.cpp.
//
// ge:summary XXH64: the 64-bit checksum on every save chunk, directory and header
// ge:keywords save checksum hash xxh64 xxhash crc integrity damage corrupt verify
// ge:skill save
// ge:test tests/save_xxh64_test.cpp
#pragma once

#include <cstddef>
#include <cstdint>

namespace ge::save {

// The hash of `n` bytes at `data` with `seed`.
uint64_t xxh64(const void* data, size_t n, uint64_t seed = 0);

}  // namespace ge::save
