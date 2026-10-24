// THE STATE DESCRIBED ONCE (docs/saves.md § Describing the state once). A type's fields are
// listed by ONE function template, found by argument-dependent lookup beside the type:
//
//   template <class Io>
//   void describe(Io& io, Racer& r) {
//     io(r.skier);               // a described type, a number, an enum, a string, a vector,
//     io(r.gates);               //   an array, an optional or a pair: each walked by its kind
//     io.since(3, r.best, 0.0);  // added in the chunk's version 3: older chunks read 0.0
//     io.until(5, r.old_lap);    // dropped in version 5: read from older chunks, never written
//   }
//
// and both directions walk it: the WRITER appends the fields' bytes to a buffer, the READER fills
// the fields from bytes, so a field saved is a field loaded and the two can never drift. A
// change too big for `since` and `until` branches on `io.version()`, the version of the chunk
// being read (the version being written, on the writer).
//
// THE BYTES: little-endian and fixed-width (a double is its IEEE 754 bits, a bool one byte, a
// length a u32), so a save written on one platform reads bit for bit on every other. Numbers,
// enums and arrays and vectors of them are copied in BULK with one memcpy; so are types a game
// marks `GE_SAVE_PLAIN(T)`: a trivially copyable struct whose described fields are its whole
// memory, in order, with no padding (`plain_problem<T>()` checks it, and the tests call it for
// every plain type). Everything else is walked field by field. Nothing is named or reflected.
//
// THE READER never reads past its bytes: a read past the end, a length longer than what is left
// or a bool that is not 0 or 1 sets its PROBLEM (the first one wins, with the byte it happened
// at) and reads zeros after, so a describe needs no checks of its own and a damaged chunk can
// never run outside its buffer. A describe may refuse a value itself with `io.fail(what)`.
//
// ge:summary The state described once: describe(io, T&) walked by the save Writer and Reader
// ge:keywords save load serialize deserialize describe io writer reader walk field version
// ge:keywords since until plain bulk memcpy ge_save_plain plain_problem little-endian
// ge:keywords vector string optional array pair enum bool
// ge:skill save
// ge:test tests/save_io_test.cpp
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/prng.hpp"

namespace ge::save {

static_assert(std::endian::native == std::endian::little,
              "saves copy numbers in bulk as little-endian bytes");

// Opt-in: T's memory IS its described fields, so a vector of them is one memcpy.
template <class T>
struct Plain : std::false_type {};

template <class T>
inline constexpr bool kBulk =
    (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) || std::is_enum_v<T> || Plain<T>::value;

class Writer;
class Reader;

namespace detail {
template <class T>
struct IsVector : std::false_type {};
template <class T, class A>
struct IsVector<std::vector<T, A>> : std::true_type {};
template <class T>
struct IsArray : std::false_type {};
template <class T, size_t N>
struct IsArray<std::array<T, N>> : std::true_type {};
template <class T>
struct IsOptional : std::false_type {};
template <class T>
struct IsOptional<std::optional<T>> : std::true_type {};
template <class T>
struct IsPair : std::false_type {};
template <class A, class B>
struct IsPair<std::pair<A, B>> : std::true_type {};

// The fewest bytes one T can take, so a length can be checked against what is left before
// anything is allocated (1 for a bool, an optional or a described type: an empty one is not
// counted).
template <class T>
constexpr size_t min_bytes() {
  if constexpr (kBulk<T>)
    return sizeof(T);
  else if constexpr (std::is_same_v<T, std::string> || IsVector<T>::value)
    return 4;
  else if constexpr (IsArray<T>::value)
    return std::tuple_size_v<T> * min_bytes<typename T::value_type>();
  else if constexpr (IsPair<T>::value)
    return min_bytes<typename T::first_type>() + min_bytes<typename T::second_type>();
  else
    return 1;
}

template <class Io, class T>
void walk(Io& io, T& v);
}  // namespace detail

// Appends described values' bytes to `out` (never cleared: a file writer appends chunk after
// chunk to one buffer).
class Writer {
 public:
  static constexpr bool kReading = false;
  Writer(std::vector<uint8_t>& out, uint16_t version) : out_(&out), version_(version) {}

  template <class T>
  void operator()(const T& v) {
    detail::walk(*this, const_cast<T&>(v));
  }
  template <class T, class F>
  void since(uint16_t version, const T& field, const F& fallback) {
    (void)version;
    (void)fallback;
    (*this)(field);
  }
  template <class T>
  void until(uint16_t version, const T& field) {
    (void)version;
    (void)field;
  }
  uint16_t version() const { return version_; }
  void fail(const std::string& what) { (void)what; }

  void raw(const void* bytes, size_t n) {
    if (!n) return;
    const size_t at = out_->size();
    out_->resize(at + n);
    std::memcpy(out_->data() + at, bytes, n);
  }
  void length(uint32_t& n, size_t min_item) {
    (void)min_item;
    raw(&n, 4);
  }
  void boolean(bool& b) {
    const uint8_t v = b ? 1 : 0;
    raw(&v, 1);
  }

 private:
  std::vector<uint8_t>* out_;
  uint16_t version_;
};

// Fills described values from `bytes` written at chunk `version`.
class Reader {
 public:
  static constexpr bool kReading = true;
  Reader(const uint8_t* bytes, size_t n, uint16_t version)
      : at_(bytes), begin_(bytes), end_(bytes + n), version_(version) {}

  template <class T>
  void operator()(T& v) {
    detail::walk(*this, v);
  }
  // Read when the chunk is at least `version`; else the field takes `fallback`.
  template <class T, class F>
  void since(uint16_t version, T& field, const F& fallback) {
    if (version_ >= version)
      (*this)(field);
    else
      field = fallback;
  }
  // Read (into `field`, often a local the describe then converts) only from chunks older than
  // `version`, the version that dropped it.
  template <class T>
  void until(uint16_t version, T& field) {
    if (version_ < version) (*this)(field);
  }
  uint16_t version() const { return version_; }
  // Refuse the chunk: `what` names the value and why ("a racer index 9 of 4 racers").
  void fail(const std::string& what);

  bool ok() const { return problem_.empty(); }
  // Every byte read: a chunk with bytes left over was written by a different describe.
  bool at_end() const { return at_ == end_; }
  size_t offset() const { return static_cast<size_t>(at_ - begin_); }
  size_t left() const { return static_cast<size_t>(end_ - at_); }
  const std::string& problem() const { return problem_; }

  void raw(void* dst, size_t n) {
    if (n > left() || !ok()) {
      if (ok()) overrun(n);
      if (n) std::memset(dst, 0, n);
      at_ = end_;
      return;
    }
    if (n) std::memcpy(dst, at_, n);
    at_ += n;
  }
  void length(uint32_t& n, size_t min_item);
  void boolean(bool& b);

 private:
  void overrun(size_t n);
  const uint8_t* at_;
  const uint8_t* begin_;
  const uint8_t* end_;
  uint16_t version_;
  std::string problem_;
};

namespace detail {

template <class Io, class T>
void walk(Io& io, T& v) {
  if constexpr (kBulk<T>) {
    io.raw(&v, sizeof(T));
  } else if constexpr (std::is_same_v<T, bool>) {
    io.boolean(v);
  } else if constexpr (std::is_same_v<T, std::string>) {
    uint32_t n = static_cast<uint32_t>(v.size());
    io.length(n, 1);
    if constexpr (Io::kReading) v.resize(n);
    io.raw(v.data(), n);
  } else if constexpr (IsVector<T>::value) {
    using E = typename T::value_type;
    uint32_t n = static_cast<uint32_t>(v.size());
    io.length(n, min_bytes<E>());
    if constexpr (std::is_same_v<E, bool>) {  // std::vector<bool> packs bits: one at a time
      if constexpr (Io::kReading) v.assign(n, false);
      for (uint32_t i = 0; i < n; i++) {
        bool b = v[i];
        io.boolean(b);
        if constexpr (Io::kReading) v[i] = b;
      }
    } else {
      if constexpr (Io::kReading) v.resize(n);
      if constexpr (kBulk<E>)
        io.raw(v.data(), sizeof(E) * n);
      else
        for (E& e : v) walk(io, e);
    }
  } else if constexpr (IsArray<T>::value) {
    using E = typename T::value_type;
    if constexpr (kBulk<E>)
      io.raw(v.data(), sizeof(E) * v.size());
    else
      for (E& e : v) walk(io, e);
  } else if constexpr (IsOptional<T>::value) {
    bool has = v.has_value();
    io.boolean(has);
    if constexpr (Io::kReading) {
      if (has)
        v.emplace();
      else
        v.reset();
    }
    if (has) walk(io, *v);
  } else if constexpr (IsPair<T>::value) {
    walk(io, v.first);
    walk(io, v.second);
  } else {
    describe(io, v);  // found beside T by argument-dependent lookup
  }
}

}  // namespace detail

// Why T cannot be GE_SAVE_PLAIN ("" when it can): not trivially copyable, or its described
// fields are not all of its bytes (padding, or a field left out of its describe).
template <class T>
std::string plain_problem() {
  if constexpr (!std::is_trivially_copyable_v<T>) {
    return "not trivially copyable: describe it field by field instead of GE_SAVE_PLAIN";
  } else {
    std::vector<uint8_t> out;
    Writer w(out, 0xffff);
    T v{};
    describe(w, v);
    if (out.size() != sizeof(T))
      return "its described fields take " + std::to_string(out.size()) + " bytes of its " +
             std::to_string(sizeof(T)) +
             ": padding or a field missing from describe; reorder "
             "the fields or describe it field by field instead of GE_SAVE_PLAIN";
    return "";
  }
}

// `v`'s bytes appended to `out`, at `version`.
template <class T>
void write(std::vector<uint8_t>& out, const T& v, uint16_t version) {
  Writer w(out, version);
  w(v);
}

}  // namespace ge::save

namespace ge::core {
// The seeded stream's state: one word, copied in bulk.
template <class Io>
void describe(Io& io, Rng& r) {
  io(r.a);
}
}  // namespace ge::core

// Mark T (a struct whose memory is exactly its described fields) as copied in bulk. At global
// scope, after T's describe.
#define GE_SAVE_PLAIN(T) \
  template <>            \
  struct ge::save::Plain<T> : std::true_type {}

GE_SAVE_PLAIN(ge::core::Rng);
