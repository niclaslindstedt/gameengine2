// The `.gelv` writer and reader: a byte cursor each way, the FNV-1a trailer, and every refusal
// naming its byte offset.
//
// ge:summary Writes and reads the .gelv bytes: little-endian fields, inline strings, FNV trailer
// ge:keywords encode decode writer reader cursor little-endian fnv trailer refuse offset
// ge:skill rooms
// ge:test tests/rooms_level_test.cpp
#include "rooms/codec.hpp"

#include <cstdio>
#include <cstring>

namespace ge::rooms {

namespace {

uint32_t fnv1a(std::span<const uint8_t> b) {
  uint32_t h = 0x811c9dc5u;
  for (uint8_t c : b) {
    h ^= c;
    h *= 0x01000193u;
  }
  return h;
}

struct Writer {
  std::vector<uint8_t> out;
  void u8(uint8_t v) { out.push_back(v); }
  void u16(uint16_t v) {
    u8(uint8_t(v));
    u8(uint8_t(v >> 8));
  }
  void u32(uint32_t v) {
    u16(uint16_t(v));
    u16(uint16_t(v >> 16));
  }
  void i16(int16_t v) { u16(static_cast<uint16_t>(v)); }
  void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
  void f32(float v) {
    uint32_t u = 0;
    std::memcpy(&u, &v, sizeof u);
    u32(u);
  }
  void str(const std::string& s) {
    u8(uint8_t(s.size()));
    out.insert(out.end(), s.begin(), s.begin() + std::min<size_t>(s.size(), 255));
  }
};

struct Reader {
  std::span<const uint8_t> b;
  size_t at = 0;
  bool bad = false;
  size_t bad_at = 0;
  bool need(size_t n) {
    if (bad || at + n > b.size()) {
      if (!bad) bad_at = at;
      bad = true;
      return false;
    }
    return true;
  }
  uint8_t u8() { return need(1) ? b[at++] : 0; }
  uint16_t u16() {
    const uint16_t lo = u8();
    return uint16_t(lo | uint16_t(u8()) << 8);
  }
  uint32_t u32() {
    const uint32_t lo = u16();
    return lo | uint32_t(u16()) << 16;
  }
  int16_t i16() { return static_cast<int16_t>(u16()); }
  int32_t i32() { return static_cast<int32_t>(u32()); }
  float f32() {
    const uint32_t u = u32();
    float v = 0;
    std::memcpy(&v, &u, sizeof v);
    return v;
  }
  std::string str() {
    const size_t n = u8();
    if (!need(n)) return {};
    std::string s(reinterpret_cast<const char*>(b.data() + at), n);
    at += n;
    return s;
  }
};

// Whether the level needs version 2: anything version 1 cannot hold.
bool needs_v2(const Level& level) {
  if (!level.projections.empty() || !level.faces.empty() || !level.brushes.empty() ||
      !level.groups.empty() || !level.entities.empty()) {
    return true;
  }
  for (const Material& m : level.materials) {
    if (m.tex_w != 0 || m.tex_h != 0 || !m.surface.empty()) return true;
  }
  return false;
}

}  // namespace

std::vector<uint8_t> encode(const Level& level) {
  Writer w;
  const bool v2 = needs_v2(level);
  for (char c : {'G', 'E', 'L', 'V'}) w.u8(uint8_t(c));
  w.u16(v2 ? 2 : 1);
  w.u16(0);
  w.u16(uint16_t(level.materials.size()));
  w.u16(uint16_t(level.rooms.size()));
  w.u16(uint16_t(level.portals.size()));
  w.u16(uint16_t(level.solids.size()));
  if (v2) {
    w.u16(uint16_t(level.projections.size()));
    w.u32(uint32_t(level.faces.size()));
    w.u32(uint32_t(level.brushes.size()));
    w.u16(uint16_t(level.groups.size()));
    w.u32(uint32_t(level.entities.size()));
  }
  for (const Material& m : level.materials) {
    w.str(m.name);
    w.str(m.texture);
    for (uint8_t c : m.rgb) w.u8(c);
    w.u8(uint8_t(m.relief));
    w.u8(m.relief_cm);
    w.u8(m.scale_dm);
    if (v2) {
      w.u16(m.tex_w);
      w.u16(m.tex_h);
      w.str(m.surface);
    }
  }
  for (const Room& r : level.rooms) {
    w.str(r.name);
    for (int32_t o : r.origin) w.i32(o);
    w.u32(r.seed);
    w.u8(uint8_t(r.shape));
    w.u8(r.floor);
    w.u8(r.ceiling);
    w.i16(r.floor_y);
    w.i16(r.ceil_y);
    w.u8(r.cave_dm);
    w.u8(r.cave_scale_dm);
    w.u8(r.cave_cell_cm);
    w.u8(uint8_t(r.corners.size()));
    for (const Corner& c : r.corners) {
      w.i16(c.x);
      w.i16(c.z);
      w.u8(c.wall);
    }
  }
  for (const Portal& p : level.portals) {
    w.u16(p.room);
    w.u8(p.wall);
    w.u16(p.to);
    w.u8(p.trim);
    w.i16(p.offset);
    w.i16(p.width);
    w.i16(p.sill);
    w.i16(p.head);
  }
  for (const Solid& s : level.solids) {
    w.u16(s.room);
    w.u8(s.material);
    for (int16_t v : s.min) w.i16(v);
    for (int16_t v : s.max) w.i16(v);
  }
  if (v2) {
    for (const Projection& p : level.projections) {
      for (float v : p.s) w.f32(v);
      for (float v : p.t) w.f32(v);
    }
    for (const Face& f : level.faces) {
      for (float v : f.n) w.f32(v);
      w.f32(f.d);
      w.u8(f.material);
      w.u16(f.projection);
      w.u8(uint8_t((f.hidden ? 1 : 0) | (f.smooth ? 2 : 0)));
    }
    for (const Brush& b : level.brushes) {
      w.u16(b.group);
      w.u8(uint8_t(b.contents));
      w.u8(b.faces);
    }
    for (const Group& g : level.groups) w.str(g.name);
    for (const Entity& e : level.entities) {
      w.str(e.classname);
      w.u16(e.group);
      w.u8(uint8_t(e.keys.size()));
      for (const auto& [k, v] : e.keys) {
        w.str(k);
        w.str(v);
      }
    }
  }
  w.u32(fnv1a(w.out));
  return w.out;
}

bool decode(std::span<const uint8_t> bytes, Level* out, std::string* error) {
  auto fail = [&](size_t at, const char* what, const char* next) {
    if (error) {
      char b[400];
      std::snprintf(b, sizeof b, "gelv: %s at byte %zu of %zu; %s", what, at, bytes.size(), next);
      *error = b;
    }
    return false;
  };
  if (bytes.size() < 20 || bytes[0] != 'G' || bytes[1] != 'E' || bytes[2] != 'L' ||
      bytes[3] != 'V') {
    return fail(0, "not a room level (no GELV magic, or shorter than a header)",
                "check the file is a .gelv written by rooms::encode");
  }
  const size_t body = bytes.size() - 4;
  const uint32_t want = uint32_t(bytes[body]) | uint32_t(bytes[body + 1]) << 8 |
                        uint32_t(bytes[body + 2]) << 16 | uint32_t(bytes[body + 3]) << 24;
  if (fnv1a(bytes.first(body)) != want) {
    return fail(body, "the checksum does not match", "the file is damaged; write it again");
  }
  Reader r{bytes.first(body)};
  r.at = 4;
  const uint16_t version = r.u16();
  if (version < 1 || version > kFormatVersion) {
    return fail(4, "an unknown format version",
                "this build reads versions 1 and 2; rebuild the file");
  }
  const bool v2 = version == 2;
  r.u16();  // flags, none yet
  Level level;
  level.materials.resize(r.u16());
  level.rooms.resize(r.u16());
  level.portals.resize(r.u16());
  level.solids.resize(r.u16());
  size_t n_faces = 0, n_brushes = 0, n_entities = 0;
  if (v2) {
    level.projections.resize(r.u16());
    n_faces = r.u32();
    n_brushes = r.u32();
    level.groups.resize(r.u16());
    n_entities = r.u32();
    // Each record is at least this many bytes, so a count the file cannot hold is refused before
    // anything is made for it.
    const size_t left = body - std::min(body, r.at);
    if (n_faces > left / 20 || n_brushes > left / 4 || n_entities > left / 4) {
      return fail(r.at, "a count larger than the file can hold", "the file is damaged");
    }
    level.faces.resize(n_faces);
    level.brushes.resize(n_brushes);
    level.entities.resize(n_entities);
  }
  for (Material& m : level.materials) {
    m.name = r.str();
    m.texture = r.str();
    for (uint8_t& c : m.rgb) c = r.u8();
    m.relief = Relief(r.u8());
    m.relief_cm = r.u8();
    m.scale_dm = r.u8();
    if (v2) {
      m.tex_w = r.u16();
      m.tex_h = r.u16();
      m.surface = r.str();
    }
    if (uint8_t(m.relief) > uint8_t(Relief::dents)) {
      return fail(r.at - 3, "an unknown relief", "the file is newer than this build");
    }
  }
  for (Room& room : level.rooms) {
    if (r.bad) break;
    room.name = r.str();
    for (int32_t& o : room.origin) o = r.i32();
    room.seed = r.u32();
    room.shape = Shape(r.u8());
    if (uint8_t(room.shape) > uint8_t(Shape::cave)) {
      return fail(r.at - 1, "an unknown room shape", "the file is newer than this build");
    }
    room.floor = r.u8();
    room.ceiling = r.u8();
    room.floor_y = r.i16();
    room.ceil_y = r.i16();
    room.cave_dm = r.u8();
    room.cave_scale_dm = r.u8();
    room.cave_cell_cm = r.u8();
    room.corners.resize(r.u8());
    for (Corner& c : room.corners) {
      c.x = r.i16();
      c.z = r.i16();
      c.wall = r.u8();
    }
    if (r.bad) break;
  }
  for (Portal& p : level.portals) {
    p.room = r.u16();
    p.wall = r.u8();
    p.to = r.u16();
    p.trim = r.u8();
    p.offset = r.i16();
    p.width = r.i16();
    p.sill = r.i16();
    p.head = r.i16();
  }
  for (Solid& s : level.solids) {
    s.room = r.u16();
    s.material = r.u8();
    for (int16_t& v : s.min) v = r.i16();
    for (int16_t& v : s.max) v = r.i16();
  }
  for (Projection& p : level.projections) {
    for (float& v : p.s) v = r.f32();
    for (float& v : p.t) v = r.f32();
  }
  for (Face& f : level.faces) {
    for (float& v : f.n) v = r.f32();
    f.d = r.f32();
    f.material = r.u8();
    f.projection = r.u16();
    const uint8_t flags = r.u8();
    if (flags > 3) {
      return fail(r.at - 1, "a face's flags hold an unknown bit (1 hidden, 2 smooth)",
                  "the file is newer than this build");
    }
    f.hidden = (flags & 1) != 0;
    f.smooth = (flags & 2) != 0;
    if (r.bad) break;
  }
  uint32_t first = 0;
  for (Brush& b : level.brushes) {
    b.group = r.u16();
    b.contents = Contents(r.u8());
    b.faces = r.u8();
    b.first_face = first;
    first += b.faces;
    if (uint8_t(b.contents) > uint8_t(Contents::trigger)) {
      return fail(r.at - 2, "unknown brush contents", "the file is newer than this build");
    }
  }
  for (Group& g : level.groups) g.name = r.str();
  for (Entity& e : level.entities) {
    e.classname = r.str();
    e.group = r.u16();
    e.keys.resize(r.u8());
    for (auto& [k, v] : e.keys) {
      k = r.str();
      v = r.str();
    }
    if (r.bad) break;
  }
  if (r.bad) {
    return fail(r.bad_at, "the file ends inside a record", "the file is cut short; write it again");
  }
  if (r.at != body) {
    return fail(r.at, "bytes after the last record", "the counts in the header are wrong");
  }
  std::string why;
  if (!link(level, &why)) {
    if (error) *error = "gelv: the level is not sound: " + why;
    return false;
  }
  *out = std::move(level);
  return true;
}

}  // namespace ge::rooms
