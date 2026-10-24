// LAB `bsp`: maps of the classic BSP families LOOKED AT and MEASURED. The sample fixture written
// as a map of every family (29, BSP2, 38, 46) and read back: each drawn from above with its
// lightmaps and the ceilings cut away, views from inside lit only by the lightmaps, the lightmaps
// themselves (one tile a face, and 46's pages), the leaves from above coloured by what one cluster
// can see, and 46's curved patch tessellated at three levels. With `--map=<file>` (and `--lit`)
// any map of these families is drawn the same way instead. The numbers: each file's bytes, its
// surfaces, triangles, lightmaps, leaves and clusters, and how long a read takes.
//
// ge:summary Lab: every BSP family's fixture read and drawn: cutaway, inside, lightmaps, pvs, patch
// ge:keywords lab bsp brush map family v29 bsp2 v38 v46 fixture cutaway lightmap atlas pvs leaves
// ge:keywords patch tessellation read-time
// ge:skill brush labs
// ge:test tests/brush_read_test.cpp
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "assets/model.hpp"
#include "brush/read.hpp"
#include "brush/write.hpp"
#include "core/num_text.hpp"
#include "lab/lab.hpp"
#include "lab/model_view.hpp"
#include "lab/png.hpp"
#include "lab/raster.hpp"
#include "textured_view.hpp"
#include "vfs/pak.hpp"

namespace {

namespace br = ge::brush;
namespace lab = ge::lab;
using lab::Det;
using lab::Picture;
using lab::Rgba;

constexpr Rgba kDark{22, 24, 30};
constexpr Rgba kPaper{246, 245, 240};
constexpr Rgba kInk{24, 26, 32};
constexpr int kSlots = 16, kLevels = 16;  // texture colours × light levels = the 256 materials

struct Loaded {
  std::string name;
  std::vector<uint8_t> bytes;
  br::Map map;
};

// A texture's colour, told by its name after its last '/' (so a family's path prefix does not
// change it): a palette of building materials.
const uint8_t* colour_of(const std::string& path) {
  const std::string name = path.substr(path.rfind('/') + 1);
  static const uint8_t kPalette[][3] = {
      {186, 120, 92},  {170, 168, 160}, {214, 206, 188}, {120, 132, 142},
      {196, 182, 150}, {150, 160, 120}, {200, 150, 110}, {140, 150, 170},
      {180, 180, 200}, {210, 190, 160}, {160, 110, 90},  {130, 140, 120},
      {190, 200, 210}, {170, 140, 120}, {200, 200, 180}, {150, 130, 160}};
  uint32_t h = 2166136261u;
  for (char c : name) h = (h ^ uint8_t(c)) * 16777619u;
  return kPalette[h % kSlots];
}

ge::assets::Model palette_of(const br::Map& map) {
  ge::assets::Model m;
  m.materials.resize(size_t(kSlots) * kLevels);
  for (int slot = 0; slot < kSlots; ++slot) {
    const uint8_t* c = slot < int(map.textures.size()) ? colour_of(map.textures[size_t(slot)].name)
                                                       : colour_of(std::to_string(slot));
    for (int l = 0; l < kLevels; ++l) {
      const double light = (l + 0.5) / kLevels * 1.6;
      for (int k = 0; k < 3; ++k) {
        const double v = std::min(1.0, c[k] / 255.0 * light);
        m.materials[size_t(slot) * kLevels + size_t(l)].rgb[k] = float(v * v);
      }
    }
  }
  return m;
}

// The lightmap's grey at (u, v) across lightmap `l`, nearest luxel.
double luxel(const br::Map& map, int32_t l, double u, double v) {
  if (l < 0) return 160;
  const br::Lightmap& lm = map.lightmaps[size_t(l)];
  const auto x = uint32_t(std::clamp(u * lm.width, 0.0, double(lm.width) - 1));
  const auto y = uint32_t(std::clamp(v * lm.height, 0.0, double(lm.height) - 1));
  const size_t at = lm.offset + (size_t(y) * lm.width + x) * 3;
  return (map.light_rgb[at] + map.light_rgb[at + 1] + map.light_rgb[at + 2]) / 3.0;
}

// The map's triangles in the engine's frame, each cut into pieces about `piece` units across so
// the lightmap's gradient shows, every vertex coloured by its texture and its luxel.
lab::Shape shape_of(const br::Map& map, double piece, bool patches_only = false) {
  lab::Shape s;
  for (const br::Surface& surf : map.surfaces) {
    if (patches_only && surf.kind != br::SurfaceKind::patch) continue;
    const int slot = int(surf.texture % kSlots);
    for (uint32_t t = surf.first; t + 2 < surf.first + surf.count; t += 3) {
      const br::Vertex* v[3] = {&map.vertices[map.indices[t]], &map.vertices[map.indices[t + 1]],
                                &map.vertices[map.indices[t + 2]]};
      double longest = 0;
      for (int e = 0; e < 3; ++e) {
        const float* a = v[e]->pos;
        const float* b = v[(e + 1) % 3]->pos;
        longest = std::max(longest, std::sqrt(double(a[0] - b[0]) * (a[0] - b[0]) +
                                              double(a[1] - b[1]) * (a[1] - b[1]) +
                                              double(a[2] - b[2]) * (a[2] - b[2])));
      }
      const int n = std::clamp(int(std::ceil(longest / piece)), 1, 64);
      const auto base = uint32_t(s.part.size());
      // Barycentric rows: row i has n - i + 1 points.
      for (int i = 0; i <= n; ++i) {
        for (int j = 0; j <= n - i; ++j) {
          const double w1 = double(i) / n, w2 = double(j) / n, w0 = 1 - w1 - w2;
          float pos[3], nrm[3];
          double lm[2];
          for (int k = 0; k < 3; ++k) {
            pos[k] = float(w0 * v[0]->pos[k] + w1 * v[1]->pos[k] + w2 * v[2]->pos[k]);
            nrm[k] = float(w0 * v[0]->nrm[k] + w1 * v[1]->nrm[k] + w2 * v[2]->nrm[k]);
          }
          for (int k = 0; k < 2; ++k) {
            lm[k] = w0 * v[0]->lm[k] + w1 * v[1]->lm[k] + w2 * v[2]->lm[k];
          }
          double e[3];
          br::to_engine(pos, e);
          for (int k = 0; k < 3; ++k) s.pos.push_back(float(e[k]));
          s.nrm.insert(s.nrm.end(), {nrm[0], nrm[2], nrm[1]});
          const double g = luxel(map, surf.lightmap, lm[0], lm[1]);
          const int level = std::clamp(int(g / 256.0 * kLevels), 0, kLevels - 1);
          s.part.push_back(uint8_t(slot * kLevels + level));
        }
      }
      const auto at = [&](int i, int j) {
        // Points before row i: sum over r < i of (n - r + 1).
        return base + uint32_t(i * (n + 1) - i * (i - 1) / 2 + j);
      };
      for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n - i; ++j) {
          s.idx.insert(s.idx.end(), {at(i, j), at(i + 1, j), at(i, j + 1)});
          if (j + 1 < n - i)
            s.idx.insert(s.idx.end(), {at(i + 1, j), at(i + 1, j + 1), at(i, j + 1)});
        }
      }
    }
  }
  s.top.resize(s.part.size(), 0);
  return s;
}

lab::ViewStyle style(bool wire = false) {
  lab::ViewStyle st;
  st.floor = false;
  st.baked = !wire;
  st.wire = wire;
  st.background = kDark;
  st.edge = Rgba{255, 255, 255, wire ? uint8_t(110) : uint8_t(40)};
  return st;
}

void label(Picture& p, int x, int y, const std::string& text) {
  lab::fill_rect(p, x, y, lab::text_width(text, 2) + 10, 22, Rgba{0, 0, 0, 160});
  lab::draw_text(p, x + 5, y + 4, text, Rgba{240, 240, 240}, 2);
}

std::string kb(size_t bytes) { return ge::core::num::to_fixed(double(bytes) / 1024.0, 1) + " KB"; }

// The leaves from above (x right, y up the page), each filled by what cluster `from` sees.
void draw_pvs(Picture& p, int x0, int y0, int w, int h, const br::Map& map, int32_t from,
              const std::vector<std::string>& names) {
  float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
  for (const br::Leaf& l : map.leaves) {
    if (l.solid || l.cluster < 0) continue;
    for (int k = 0; k < 2; ++k)
      lo[k] = std::min(lo[k], l.mins[k]), hi[k] = std::max(hi[k], l.maxs[k]);
  }
  const double span = std::max(hi[0] - lo[0], hi[1] - lo[1]) * 1.1;
  const double s = std::min(w, h) / span;
  const double ox = x0 + (w - (hi[0] - lo[0]) * s) / 2, oy = y0 + (h + (hi[1] - lo[1]) * s) / 2;
  const auto X = [&](double x) { return ox + (x - lo[0]) * s; };
  const auto Y = [&](double y) { return oy - (y - lo[1]) * s; };
  for (const br::Leaf& l : map.leaves) {
    if (l.solid || l.cluster < 0) continue;
    const Rgba fill = l.cluster == from                        ? Rgba{232, 150, 60}
                      : br::cluster_sees(map, from, l.cluster) ? Rgba{90, 180, 110}
                                                               : Rgba{200, 198, 190};
    lab::fill_rect(p, X(l.mins[0]), Y(l.maxs[1]), (l.maxs[0] - l.mins[0]) * s,
                   (l.maxs[1] - l.mins[1]) * s, fill);
    const double xs[2] = {X(l.mins[0]), X(l.maxs[0])}, ys[2] = {Y(l.mins[1]), Y(l.maxs[1])};
    for (int k = 0; k < 2; ++k) {
      lab::draw_line(p, xs[0], ys[k], xs[1], ys[k], kInk, 2);
      lab::draw_line(p, xs[k], ys[0], xs[k], ys[1], kInk, 2);
    }
    const std::string name = size_t(l.cluster) < names.size() ? names[size_t(l.cluster)]
                                                              : "C" + std::to_string(l.cluster);
    if (lab::text_width(name) < (l.maxs[0] - l.mins[0]) * s) {
      lab::draw_text(p, (xs[0] + xs[1]) / 2 - lab::text_width(name) / 2.0, (ys[0] + ys[1]) / 2 - 4,
                     name, kInk);
    }
  }
}

// The lightmaps as pictures: one tile a lightmap, shelved, each luxel `scale` pixels.
Picture draw_lightmaps(const br::Map& map, int width, int scale) {
  int x = 4, y = 4, shelf = 0;
  std::vector<std::pair<int, int>> at;
  for (const br::Lightmap& l : map.lightmaps) {
    const int w = int(l.width) * scale, h = int(l.height) * scale;
    if (x + w + 4 > width) x = 4, y += shelf + 4, shelf = 0;
    at.emplace_back(x, y);
    x += w + 4;
    shelf = std::max(shelf, h);
  }
  Picture p = lab::create_picture(width, y + shelf + 4, kDark);
  for (size_t i = 0; i < map.lightmaps.size(); ++i) {
    const br::Lightmap& l = map.lightmaps[i];
    for (uint32_t j = 0; j < l.height; ++j) {
      for (uint32_t k = 0; k < l.width; ++k) {
        const size_t o = l.offset + (size_t(j) * l.width + k) * 3;
        const Rgba c{map.light_rgb[o], map.light_rgb[o + 1], map.light_rgb[o + 2]};
        lab::fill_rect(p, at[i].first + int(k) * scale, at[i].second + int(j) * scale, scale, scale,
                       c);
      }
    }
  }
  return p;
}

double read_us(const std::vector<uint8_t>& bytes) {
  double best = 1e30;
  for (int i = 0; i < 12; ++i) {
    br::Map m;
    std::string e;
    const auto t0 = std::chrono::steady_clock::now();
    br::read_bsp(bytes, &m, &e);
    best = std::min(
        best,
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count());
  }
  return best;
}

// Where to stand in a map to look at it: the player's start, then up to three other starts spread
// through the entity list, each at eye height facing its `angle`.
std::vector<std::pair<std::string, br::labs::TexturedEye>> eyes_of(const br::Map& map) {
  std::vector<std::pair<std::string, br::labs::TexturedEye>> out;
  std::vector<const br::Entity*> others;
  const auto eye_at = [](const br::Entity& e) {
    br::labs::TexturedEye eye;
    double o[3] = {0, 0, 0};
    std::sscanf(e.get("origin").c_str(), "%lf %lf %lf", &o[0], &o[1], &o[2]);
    for (int k = 0; k < 3; ++k) eye.at[k] = o[k];
    eye.at[2] += 22;  // the eye over a start's origin
    eye.yaw_deg = std::atof(e.get("angle").c_str());
    return eye;
  };
  for (const br::Entity& e : map.entities) {
    const std::string& c = e.get("classname");
    if (c == "info_player_start" && out.empty()) out.push_back({"THE START", eye_at(e)});
    if (c == "info_player_deathmatch" || c == "info_player_coop") others.push_back(&e);
  }
  for (size_t i = 0; i < 3 && i < others.size(); ++i) {
    const size_t at = others.size() <= 3 ? i : i * (others.size() - 1) / 2;
    out.push_back({"START " + std::to_string(at + 1), eye_at(*others[at])});
  }
  return out;
}

// A texture's file name for the matcher: a leading '*' (a liquid) is `liq_`, a leading '+' (an
// animation frame) `anim_`, and anything a file name cannot hold an underscore.
std::string texture_file(const std::string& name) {
  std::string out;
  size_t at = 0;
  if (!name.empty() && name[0] == '*') out = "liq_", at = 1;
  if (!name.empty() && name[0] == '+') out = "anim_", at = 1;
  for (size_t i = at; i < name.size(); ++i) {
    const char c = name[i];
    out += std::isalnum(uint8_t(c)) || c == '_' || c == '-' ? char(std::tolower(uint8_t(c))) : '_';
  }
  return out + ".png";
}

// --textures-out: every texture of each 29 or BSP2 map written as a PNG through the palette, to
// `<dir>/<map>/`, with `textures.txt` naming each (the map's name, the file, the size). For the
// procedural texture matcher to read OUTSIDE the tree; nothing here enters a digest or a shot.
bool write_textures(lab::Lab& lab, const std::vector<Loaded>& maps,
                    const std::vector<uint8_t>& palette, const std::string& dir) {
  for (const Loaded& l : maps) {
    if (l.map.family != br::Family::v29 && l.map.family != br::Family::bsp2) continue;
    std::string stem = l.name.substr(l.name.rfind('/') + 1);
    stem = stem.substr(0, stem.rfind('.'));
    const std::filesystem::path out = std::filesystem::path(dir) / stem;
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    std::ofstream index(out / "textures.txt");
    if (ec || !index) {
      lab.fail("bsp: cannot write under " + out.string(),
               "next: pass --textures-out=<a writable directory outside the tree>");
      return false;
    }
    index << "# <name in the map> <file> <width>x<height> (bsp lab --textures-out)\n";
    size_t written = 0;
    for (const br::Texture& t : l.map.textures) {
      if (t.indexed.size() != size_t(t.width) * t.height || t.indexed.empty()) continue;
      Picture p = lab::create_picture(int(t.width), int(t.height), Rgba{0, 0, 0});
      for (size_t i = 0; i < t.indexed.size(); ++i) {
        for (size_t k = 0; k < 3; ++k) p.rgba[i * 4 + k] = palette[size_t(t.indexed[i]) * 3 + k];
        p.rgba[i * 4 + 3] = 255;
      }
      const std::string file = texture_file(t.name);
      if (!lab::write_png((out / file).string(), p)) {
        lab.fail("bsp: cannot write " + (out / file).string(), "next: check the directory");
        return false;
      }
      index << t.name << " " << file << " " << t.width << "x" << t.height << "\n";
      ++written;
    }
    std::printf("  textures: %zu of %s written to %s\n", written, l.name.c_str(),
                out.string().c_str());
  }
  return true;
}

void run(lab::Lab& lab) {
  const int px = int(lab.arg_num("px"));
  const std::string path = lab.arg("map");
  const std::string pak_path = lab.arg("pak");
  std::vector<uint8_t> pak_bytes, palette;
  ge::vfs::Pak pak;
  std::vector<Loaded> maps;
  std::vector<std::string> names;
  if (path.empty()) {
    const br::Fixture fx = br::sample_fixture();
    for (const br::FixtureRoom& r : fx.rooms) names.push_back(r.name);
    for (br::Family f : {br::Family::v29, br::Family::bsp2, br::Family::v38, br::Family::v46}) {
      Loaded l;
      l.name = br::family_name(f);
      std::string error;
      if (!br::write_bsp(fx, f, &l.bytes, &error)) {
        lab.fail("bsp: writing the " + l.name + " fixture: " + error,
                 "next: fix plugins/brush/fixture.cpp; skill: brush");
        return;
      }
      maps.push_back(std::move(l));
    }
  } else if (!pak_path.empty()) {
    // Maps out of a pack archive: --map names one or more paths inside it, comma separated.
    std::ifstream in(pak_path, std::ios::binary);
    pak_bytes.assign(std::istreambuf_iterator<char>(in), {});
    std::string error;
    if (pak_bytes.empty() || !ge::vfs::read_pak(pak_bytes, &pak, &error)) {
      lab.fail("bsp: cannot read the pack " + pak_path + (error.empty() ? "" : ": " + error),
               "next: pass --pak=<file.pak>, a path from the repo root; skill: vfs");
      return;
    }
    if (const ge::vfs::PakEntry* e = ge::vfs::pak_find(pak, "gfx/palette.lmp");
        e != nullptr && e->size == 768) {
      const auto b = ge::vfs::pak_file(pak_bytes, *e);
      palette.assign(b.begin(), b.end());
    }
    size_t from = 0;
    while (from <= path.size()) {
      const size_t to = std::min(path.find(',', from), path.size());
      const std::string name = path.substr(from, to - from);
      from = to + 1;
      const ge::vfs::PakEntry* e = ge::vfs::pak_find(pak, name);
      if (e == nullptr) {
        std::string what = "bsp: ";
        what += pak_path;
        what += " holds no ";
        what += name;
        lab.fail(what, "next: pass --map=<path inside the pack>, as `maps/<name>.bsp`; skill: vfs");
        return;
      }
      const auto b = ge::vfs::pak_file(pak_bytes, *e);
      Loaded l;
      l.name = name;
      l.bytes.assign(b.begin(), b.end());
      maps.push_back(std::move(l));
    }
  } else {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      lab.fail("bsp: cannot read " + path,
               "next: pass --map=<file.bsp>, a path from the repo root");
      return;
    }
    Loaded l;
    l.name = path;
    l.bytes.assign(std::istreambuf_iterator<char>(in), {});
    maps.push_back(std::move(l));
  }
  for (Loaded& l : maps) {
    std::string error;
    if (!br::read_bsp(l.bytes, &l.map, &error)) {
      lab.fail("bsp: reading " + l.name + ": " + error, "skill: brush");
      return;
    }
  }
  if (!path.empty() && !lab.arg("lit").empty()) {
    std::ifstream in(lab.arg("lit"), std::ios::binary);
    const std::vector<uint8_t> lit(std::istreambuf_iterator<char>(in), {});
    std::string error;
    if (!br::read_lit(lit, &maps[0].map, &error)) {
      lab.fail("bsp: " + lab.arg("lit") + ": " + error, "skill: brush");
      return;
    }
  }

  lab::LabTable table{"MAPS",
                      {"family", "bytes", "surfaces", "triangles", "lightmaps", "luxels", "leaves",
                       "clusters", "entities", "patches"},
                      {}};
  lab::LabTable times{"READ TIMES (measured, best of 12)", {"family", "us"}, {}};
  for (const Loaded& l : maps) {
    const br::Map& m = l.map;
    size_t luxels = 0;
    for (const br::Lightmap& x : m.lightmaps) luxels += size_t(x.width) * x.height;
    table.rows.push_back({br::family_name(m.family), std::to_string(l.bytes.size()),
                          std::to_string(m.surfaces.size()), std::to_string(m.triangles()),
                          std::to_string(m.lightmaps.size()), std::to_string(luxels),
                          std::to_string(m.leaves.size()), std::to_string(m.clusters),
                          std::to_string(m.entities.size()), std::to_string(m.patches)});
    // A map given by name is keyed by its file name too, so two maps of one family keep apart.
    std::string key = br::family_name(m.family);
    if (!path.empty()) {
      const std::string file = l.name.substr(l.name.rfind('/') + 1);
      key += "-" + file.substr(0, file.rfind('.'));
    }
    for (char& c : key) c = char(std::tolower(uint8_t(c)));
    lab.metric("bytes-" + key, double(l.bytes.size()), "count");
    lab.metric("triangles-" + key, double(m.triangles()), "count");
    const double us = read_us(l.bytes);
    lab.metric("read-us-" + key, us, "us", Det::measured);
    times.rows.push_back({key, ge::core::num::to_fixed(us, 1)});
  }
  lab.table(table);
  lab.table(times, Det::measured);
  const std::vector<std::pair<std::string, std::string>> inputs{
      {"map", path.empty() ? "the sample fixture" : path}};

  // Every family from above, ceilings cut away, lit by its own lightmaps.
  {
    const int cols = maps.size() > 1 ? 2 : 1, cw = px, ch = px * 3 / 4;
    const int rows = int((maps.size() + size_t(cols) - 1) / size_t(cols));
    Picture p = lab::create_picture(cols * cw, rows * ch, kDark);
    for (size_t i = 0; i < maps.size(); ++i) {
      const br::Map& m = maps[i].map;
      const lab::Shape s = shape_of(m, 24);
      const int x = int(i % size_t(cols)) * cw, y = int(i / size_t(cols)) * ch;
      lab::draw_shape(p, x, y, cw, ch, lab::orbit_cam(lab::shape_box(s), 30, 55, 30, 0.95, 4.0 / 3),
                      palette_of(m), s, style());
      label(p, x + 4, y + 4,
            std::string(br::family_name(m.family)) + "  " + kb(maps[i].bytes.size()) + "  " +
                std::to_string(m.triangles()) + " TRIS");
    }
    lab.shot(
        {"families", "ONE FIXTURE, EVERY FAMILY",
         "The sample fixture written by our own writers as a version 29, BSP2, 38 and 46 "
         "map and read back into the one view, drawn from above with the ceilings and the "
         "near walls cut away and lit only by each file's lightmaps: the hall and its lamp, "
         "the side room, the dark doorways, the corridor and the vault (46 adds its curved "
         "patch). Each panel says the file's size and its triangles.",
         "the four read to the same rooms and light; 46 alone carries the vault's patch", inputs},
        p);
  }

  if (path.empty()) {
    const br::Map& m = maps[3].map;
    const lab::Shape s = shape_of(m, 12);
    struct Eye {
      const char* name;
      double eye[3], at[3];
    };
    const Eye eyes[] = {{"THE HALL TO THE EAST DOOR", {2, 1.4, 4.8}, {13, 1.1, 4.8}},
                        {"DOWN THE CORRIDOR", {5.6, 1.4, 11.6}, {6.2, 1.2, 18}},
                        {"THE VAULT", {9.2, 1.4, 16.8}, {17, 5.5, 16.8}},
                        {"THE SIDE ROOM", {21, 1.4, 10.5}, {16, 1.4, 2.5}}};
    const int cw = px, ch = px * 3 / 4;
    Picture p = lab::create_picture(cw * 2, ch * 2, kDark);
    for (int i = 0; i < 4; ++i) {
      lab::ViewCam cam;
      for (int k = 0; k < 3; ++k) cam.eye[k] = eyes[i].eye[k], cam.target[k] = eyes[i].at[k];
      cam.fov_deg = 75;
      lab::draw_shape(p, (i % 2) * cw, (i / 2) * ch, cw, ch, cam, palette_of(m), s, style());
      label(p, (i % 2) * cw + 4, (i / 2) * ch + 4, eyes[i].name);
    }
    lab.shot({"inside", "STANDING IN THE 46 MAP",
              "Four eyes at head height in the version 46 fixture, every surface coloured by its "
              "texture and lit only by its lightmap: the hall toward the east door, the corridor, "
              "the vault's rippled patch overhead, and the side room looking back to its door.",
              "light falls off from each lamp; doorways are dark; walls meet without gaps", inputs},
             p);
  }

  if (const std::string dir = lab.arg("textures-out"); !dir.empty()) {
    if (palette.empty()) {
      lab.fail("bsp: --textures-out needs a palette",
               "next: pass --pak=<file.pak> holding gfx/palette.lmp and --map=<path in it>");
      return;
    }
    if (!write_textures(lab, maps, palette, dir)) return;
  }

  // With a palette (a pack's gfx/palette.lmp), each 29 or BSP2 map seen textured from its starts.
  if (!palette.empty()) {
    for (const Loaded& l : maps) {
      const br::Map& m = l.map;
      if (m.family != br::Family::v29 && m.family != br::Family::bsp2) continue;
      const auto eyes = eyes_of(m);
      if (eyes.empty()) continue;
      const int cw = px, ch = px * 5 / 8, cols = eyes.size() > 1 ? 2 : 1;
      const int rows = int((eyes.size() + size_t(cols) - 1) / size_t(cols));
      Picture p = lab::create_picture(cols * cw, rows * ch, kDark);
      for (size_t i = 0; i < eyes.size(); ++i) {
        const int x = int(i % size_t(cols)) * cw, y = int(i / size_t(cols)) * ch;
        br::labs::draw_textured(p, x, y, cw, ch, m, palette, eyes[i].second);
        label(p, x + 4, y + 4, eyes[i].first);
      }
      std::string id = l.name.substr(l.name.rfind('/') + 1);
      id = "textured-" + id.substr(0, id.rfind('.'));
      for (char& c : id) c = char(std::tolower(uint8_t(c)));
      lab.shot({id,
                "STANDING IN " + l.name,
                "The map read out of the pack and drawn with its own pictures through the pack's "
                "palette, lit by its lightmaps (a luxel of 128 is the picture as painted), the "
                "sky's two layers along the view, trigger volumes left out: from the player's "
                "start and up to three other starts, at eye height, facing each start's angle.",
                "walls, floors and ceilings meet without gaps; light pools under the lamps; "
                "no picture is smeared or offset across a face",
                {{"pak", pak_path}, {"map", l.name}}},
               p);
    }
  }

  {
    std::vector<Picture> tiles;
    std::vector<std::string> labels;
    for (const Loaded& l : maps) {
      if (path.empty() && l.map.family != br::Family::v38 && l.map.family != br::Family::v46) {
        continue;
      }
      tiles.push_back(draw_lightmaps(l.map, px, l.map.family == br::Family::v46 ? 3 : 2));
      labels.push_back(std::string(br::family_name(l.map.family)) + "  " +
                       std::to_string(l.map.lightmaps.size()) + " LIGHTMAPS");
    }
    int h = 0;
    for (const Picture& t : tiles) h = std::max(h, t.height + 30);
    Picture p = lab::create_picture(px * int(tiles.size()), h, kDark);
    for (size_t i = 0; i < tiles.size(); ++i) {
      lab::blit(p, tiles[i], int(i) * px, 30);
      label(p, int(i) * px + 4, 4, labels[i]);
    }
    lab.shot({"lightmaps", "THE LIGHTMAPS",
              "Every lightmap as stored, one luxel every 16 texels: a tile a face in 38 (29 and "
              "BSP2 hold the same in grey), packed into 128 x 128 pages in 46, where the vault's "
              "patch has a block of its own.",
              "each wall's tile is brightest toward its room's lamp; doorways stay at the ambient",
              inputs},
             p);
  }

  {
    const br::Map& m = maps.back().map;
    const int32_t first = 0, last = m.clusters - 1;
    const int cw = px, ch = px;
    Picture p = lab::create_picture(cw * 2, ch, kPaper);
    for (int i = 0; i < 2; ++i) {
      const int32_t from = i == 0 ? first : last;
      draw_pvs(p, i * cw, 0, cw, ch, m, from, names);
      const std::string who =
          size_t(from) < names.size() ? names[size_t(from)] : std::to_string(from);
      lab::draw_text(p, i * cw + 8, 8, "WHAT " + who + " SEES", kInk, 2);
    }
    lab.shot({"pvs", "THE LEAVES AND WHAT EACH SEES",
              "The map's empty leaves from above (x right, y up the page), filled by the "
              "visibility: the cluster asked about in orange, what it can see in green, what it "
              "cannot in grey. Left from the first cluster, right from the last.",
              "a cluster sees through its doorways two rooms deep and no further", inputs},
             p);
  }

  if (path.empty()) {
    const auto levels = lab.arg_nums("patch-levels");
    const int cw = px * 2 / 3, ch = px / 2;
    Picture p = lab::create_picture(cw * int(levels.size()), ch, kDark);
    for (size_t i = 0; i < levels.size(); ++i) {
      br::Map m;
      std::string error;
      br::ReadOptions o;
      o.patch_level = int(levels[i]);
      if (!br::read_bsp(maps[3].bytes, &m, &error, o)) {
        lab.fail("bsp: patch level " + std::to_string(o.patch_level) + ": " + error,
                 "next: pass --patch-levels between 1 and 32");
        return;
      }
      const lab::Shape s = shape_of(m, 1e9, true);
      lab::ViewStyle st = style(true);
      st.baked = false;
      lab::draw_shape(p, int(i) * cw, 0, cw, ch,
                      lab::orbit_cam(lab::shape_box(s), 20, -35, 30, 0.95, double(cw) / ch),
                      palette_of(m), s, st);
      size_t tris = 0;
      for (const br::Surface& x : m.surfaces)
        tris += x.kind == br::SurfaceKind::patch ? x.count / 3 : 0;
      label(p, int(i) * cw + 4, 4,
            "LEVEL " + std::to_string(o.patch_level) + "  " + std::to_string(tris) + " TRIS");
    }
    lab.shot({"patch-levels",
              "46'S CURVED PATCH, TESSELLATED",
              "The vault's patch (5 x 3 control points, two biquadratic pieces) from below with "
              "every triangle drawn, at each tessellation level asked for: the pieces share their "
              "middle edge exactly, so they meet without a crack at any level.",
              "the ripple rounds out as the level rises; no crack along the middle seam",
              {{"patch-levels", lab.arg("patch-levels")}}},
             p);
  }
}

const lab::LabSpec kSpec{
    "bsp",
    lab::LabKind::picture,
    "plugin:brush",
    "plugins/brush",
    "brush",
    "What does each classic BSP family read into, how does it look, and how fast is a read?",
    {
        {"map", "", "a map file to draw instead of the fixtures (any family)"},
        {"lit", "", "a .lit file for --map (29 and BSP2)"},
        {"pak", "",
         "a pack archive --map's paths are read from (comma separated); its "
         "gfx/palette.lmp textures the views"},
        {"textures-out", "",
         "a directory OUTSIDE the tree: each map's textures written there as PNGs (liq_ for "
         "'*', anim_ for '+') with textures.txt, for the texture matcher"},
        {"px", "480", "a panel's width, px"},
        {"patch-levels", "2,6,12", "46's patch tessellated at these levels"},
    },
    "--px=160 --patch-levels=2,4",
    run,
};
GE_LAB(kSpec);

}  // namespace
