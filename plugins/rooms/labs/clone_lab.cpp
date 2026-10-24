// LAB `clone`: a classic map CLONED into a room level and set beside the original. The map (the
// brush plugin's fixture, or `--map` paths read out of a `--pak`) goes through the cloner with a
// texture map (`--texmap`, an agent-approved `.texmap`; without one every texture is a placeholder
// drawn in its mean colour), the clone's brushes are built by CSG and drawn from the same eyes as
// the original's textured view, and a body is stood at every start in the clone's mover world.
// The numbers: cells, brushes, faces, unseen faces, groups, entities, the level's bytes, the clone
// and build times, and how far each start stands from where the map put it.
//
// Nothing a map holds is written into the tree: the pictures land in previews/, which is not
// committed, and the clone is written out only with --write (a path outside the tree).
//
// ge:summary Lab: a classic map cloned into a room level, beside the original, starts stood on
// ge:keywords lab clone bsp texmap side-by-side original brushes csg start stand walk compare
// ge:skill rooms brush labs
// ge:test tests/rooms_clone_test.cpp
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "brush/labs/textured_view.hpp"
#include "brush/read.hpp"
#include "brush/write.hpp"
#include "core/libm/libm.hpp"
#include "core/num_text.hpp"
#include "core/parallel.hpp"
#include "lab/lab.hpp"
#include "lab/model_view.hpp"
#include "lab/raster.hpp"
#include "move/spec.hpp"
#include "move/step.hpp"
#include "rooms/brush_build.hpp"
#include "rooms/clip.hpp"
#include "rooms/clone.hpp"
#include "rooms/codec.hpp"
#include "rooms/text.hpp"
#include "vfs/pak.hpp"

namespace {

namespace rm = ge::rooms;
namespace br = ge::brush;
namespace lab = ge::lab;
using lab::Det;
using lab::Picture;
using lab::Rgba;

constexpr Rgba kDark{22, 24, 30};
constexpr double kPi = 3.14159265358979323846;

std::string fixed(double v, int d) { return ge::core::num::to_fixed(v, d); }

double ms_since(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void label(Picture& p, int x, int y, const std::string& text) {
  lab::fill_rect(p, x, y, lab::text_width(text, 2) + 10, 22, Rgba{0, 0, 0, 160});
  lab::draw_text(p, x + 5, y + 4, text, Rgba{240, 240, 240}, 2);
}

ge::assets::Model model_of(const rm::Level& level) {
  ge::assets::Model m;
  for (const rm::Material& mat : level.materials) {
    ge::assets::Material out;
    out.name = mat.name;
    for (size_t k = 0; k < 3; ++k) {
      // The mean colour as it is (no squaring toward linear): a far face of a dark map reads.
      out.rgb[k] = float(mat.rgb[k]) / 255.0f;
    }
    m.materials.push_back(out);
  }
  return m;
}

// The brush mesh's drawn faces as a shape (sky and hidden faces left out; with `cut`, every face
// looking down too, so the rooms show from above).
lab::Shape shape_of(const rm::BrushMesh& m, bool cut = false) {
  lab::Shape s;
  for (size_t t = 0; t < m.triangles(); ++t) {
    if (m.hidden[t] || m.contents[t] == rm::Contents::sky) continue;
    if (cut && m.nrm[size_t(m.idx[t * 3]) * 3 + 1] < -0.3f) continue;
    for (size_t k = 0; k < 3; ++k) {
      const size_t v = size_t(m.idx[t * 3 + k]) * 3;
      s.idx.push_back(uint32_t(s.part.size()));
      for (size_t a = 0; a < 3; ++a) s.pos.push_back(m.pos[v + a]), s.nrm.push_back(m.nrm[v + a]);
      s.part.push_back(m.material[t]);
      s.top.push_back(0);
    }
  }
  return s;
}

lab::ViewStyle style() {
  lab::ViewStyle st;
  st.floor = false;
  st.background = kDark;
  return st;
}

struct Eye {
  std::string name;
  br::labs::TexturedEye eye;  // file units
};

std::vector<Eye> eyes_of(const br::Map& map) {
  std::vector<Eye> out;
  std::vector<const br::Entity*> others;
  const auto eye_at = [](const br::Entity& e) {
    br::labs::TexturedEye eye;
    double o[3] = {0, 0, 0};
    std::sscanf(e.get("origin").c_str(), "%lf %lf %lf", &o[0], &o[1], &o[2]);
    for (size_t k = 0; k < 3; ++k) eye.at[k] = o[k];
    eye.at[2] += 22;
    eye.yaw_deg = std::atof(e.get("angle").c_str());
    eye.fov_deg = 90;
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
  if (out.empty() && !map.models.empty()) {
    // A map with no starts (the fixture): its middle, looking along +x.
    Eye e{"THE MIDDLE", {}};
    for (size_t k = 0; k < 3; ++k) {
      e.eye.at[k] = (double(map.models[0].mins[k]) + double(map.models[0].maxs[k])) / 2;
    }
    out.push_back(e);
  }
  return out;
}

lab::ViewCam cam_of(const br::labs::TexturedEye& e) {
  const float at[3] = {float(e.at[0]), float(e.at[1]), float(e.at[2])};
  double p[3];
  br::to_engine(at, p);
  const double a = e.yaw_deg * kPi / 180;
  // Along the yaw in the map's x/y, which is the engine's x/z.
  const double dx = ge::core::libm::cos(a), dz = ge::core::libm::sin(a);
  lab::ViewCam cam;
  for (size_t k = 0; k < 3; ++k) cam.eye[k] = p[k];
  cam.target[0] = p[0] + dx, cam.target[1] = p[1], cam.target[2] = p[2] + dz;
  // The textured view's 90 degrees across a panel 8 wide and 5 high, as the height's angle.
  cam.fov_deg = 64.0;
  return cam;
}

struct Loaded {
  std::string name;
  std::vector<uint8_t> bytes;
  br::Map map;
};

void run(lab::Lab& lab) {
  const int px = int(lab.arg_num("px"));
  const std::string maps_arg = lab.arg("map");
  const std::string pak_path = lab.arg("pak");
  const std::string texmap_path = lab.arg("texmap");
  const bool smooth = lab.arg("smooth") == "1";
  std::vector<uint8_t> pak_bytes, palette;
  std::vector<Loaded> maps;
  if (pak_path.empty()) {
    Loaded l;
    l.name = "fixture";
    std::string error;
    if (!br::write_bsp(br::sample_fixture(), br::Family::v29, &l.bytes, &error)) {
      lab.fail("clone: writing the fixture: " + error, "next: fix plugins/brush; skill: brush");
      return;
    }
    maps.push_back(std::move(l));
  } else {
    std::ifstream in(pak_path, std::ios::binary);
    pak_bytes.assign(std::istreambuf_iterator<char>(in), {});
    ge::vfs::Pak pak;
    std::string error;
    if (pak_bytes.empty() || !ge::vfs::read_pak(pak_bytes, &pak, &error)) {
      lab.fail("clone: cannot read the pack " + pak_path + " " + error,
               "next: pass --pak=<file.pak> (outside the tree) and --map=maps/<name>.bsp");
      return;
    }
    if (const auto* e = ge::vfs::pak_find(pak, "gfx/palette.lmp"); e != nullptr && e->size == 768) {
      const auto b = ge::vfs::pak_file(pak_bytes, *e);
      palette.assign(b.begin(), b.end());
    }
    size_t from = 0;
    while (from <= maps_arg.size()) {
      const size_t to = std::min(maps_arg.find(',', from), maps_arg.size());
      const std::string name = maps_arg.substr(from, to - from);
      from = to + 1;
      const auto* e = ge::vfs::pak_find(pak, name);
      if (e == nullptr) {
        std::string what = "clone: ";
        what += pak_path;
        what += " holds no ";
        what += name;
        lab.fail(what, "next: pass --map=maps/<name>.bsp, comma separated");
        return;
      }
      const auto b = ge::vfs::pak_file(pak_bytes, *e);
      Loaded l;
      l.name = name;
      l.bytes.assign(b.begin(), b.end());
      maps.push_back(std::move(l));
    }
  }

  lab::LabTable table{"CLONED MAPS (leaves met, brushes kept, faces, unseen: no surface on them)",
                      {"map", "cells", "brushes", "faces", "unseen", "open", "groups", "entities",
                       "bytes", "placeholders", "starts stood"},
                      {}};
  lab::LabTable times{"CLONE AND BUILD TIMES, ms (measured)", {"map", "clone", "build"}, {}};
  const size_t workers = ge::core::default_workers();
  for (Loaded& l : maps) {
    std::string error;
    if (!br::read_bsp(l.bytes, &l.map, &error)) {
      lab.fail("clone: reading " + l.name + ": " + error, "skill: brush");
      return;
    }
    std::string stem = l.name.substr(l.name.rfind('/') + 1);
    stem = stem.substr(0, stem.rfind('.'));
    rm::TextureMap tm;
    rm::CloneOptions opt;
    opt.palette = palette;
    opt.smooth = smooth;
    opt.workers = workers;
    if (!texmap_path.empty()) {
      std::string file = texmap_path;
      std::string in_dir = texmap_path;
      in_dir += '/';
      in_dir += stem;
      in_dir += "/textures.texmap";
      if (std::ifstream(in_dir)) file = in_dir;
      std::ifstream in(file);
      std::stringstream text;
      text << in.rdbuf();
      if (!in || !rm::parse_texture_map(text.str(), &tm, &error)) {
        std::string what = "clone: the texture map ";
        what += file;
        what += ' ';
        what += error;
        lab.fail(what, "next: pass --texmap=<file.texmap or the matcher's directory>");
        return;
      }
      opt.placeholders = lab.arg("placeholders") == "1";
    } else {
      opt.placeholders = true;
    }
    rm::Level level;
    rm::CloneReport rep;
    auto t0 = std::chrono::steady_clock::now();
    if (!rm::clone_map(l.map, tm, opt, &level, &rep, &error)) {
      lab.fail(l.name + ": " + error, "skill: rooms");
      return;
    }
    const double clone_ms = ms_since(t0);
    t0 = std::chrono::steady_clock::now();
    const rm::BrushMesh mesh = rm::build_brushes(level, workers);
    const double build_ms = ms_since(t0);
    const std::vector<uint8_t> bytes = rm::encode(level);
    if (const std::string out = lab.arg("write"); !out.empty()) {
      std::string base = out;
      base += '/';
      base += stem;
      std::ofstream(base + ".gelv", std::ios::binary)
          .write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
      std::ofstream(base + ".gelv.txt") << rm::print(level);
    }

    // A body stood at every start in the clone's mover world.
    const ge::move::MeshWorld w = rm::clip_world(level, workers);
    const ge::move::World world = ge::move::as_world(w);
    const ge::move::MoveSpec spec = *ge::move::find_preset("walker");
    size_t starts = 0, stood = 0;
    double worst = 0;
    for (const rm::Entity& e : level.entities) {
      if (e.classname.rfind("info_player_", 0) != 0) continue;
      double o[3] = {0, 0, 0};
      if (std::sscanf(e.get("origin").c_str(), "%lf %lf %lf", &o[0], &o[1], &o[2]) != 3) continue;
      ++starts;
      // A start's origin is 24 units over the floor: its feet drop that far.
      const ge::move::MoveState s = ge::move::stand_at(spec, world, {o[0], o[1] - 0.5, o[2]});
      const double drop = o[1] - 24 * br::kUnitM - s.pos.y;
      if (s.grounded && std::fabs(drop) < 0.1) ++stood;
      worst = std::max(worst, std::fabs(drop));
    }
    lab.metric(stem + "-brushes", double(rep.brushes), "count");
    lab.metric(stem + "-faces", double(rep.faces), "count");
    lab.metric(stem + "-unseen", double(rep.unseen), "count");
    lab.metric(stem + "-open", double(rep.open), "count");
    lab.metric(stem + "-triangles", double(mesh.triangles()), "count");
    lab.metric(stem + "-bytes", double(bytes.size()), "count");
    lab.metric(stem + "-starts-stood", double(stood), "count");
    lab.metric(stem + "-start-worst-m", worst, "m");
    table.rows.push_back({l.name, std::to_string(rep.cells), std::to_string(rep.brushes),
                          std::to_string(rep.faces), std::to_string(rep.unseen),
                          std::to_string(rep.open), std::to_string(rep.groups),
                          std::to_string(rep.entities), std::to_string(bytes.size()),
                          std::to_string(rep.placeholders.size()),
                          std::to_string(stood) + " of " + std::to_string(starts)});
    times.rows.push_back({l.name, fixed(clone_ms, 1), fixed(build_ms, 1)});

    // The original and the clone from the same eyes.
    const auto eyes = eyes_of(l.map);
    const auto model = model_of(level);
    const lab::Shape shape = shape_of(mesh);
    const int cw = px, ch = px * 5 / 8;
    const bool textured = !palette.empty();
    Picture p = lab::create_picture(cw * 2, ch * int(eyes.size()), kDark);
    for (size_t i = 0; i < eyes.size(); ++i) {
      const int y = int(i) * ch;
      if (textured) {
        br::labs::draw_textured(p, 0, y, cw, ch, l.map, palette, eyes[i].eye);
      }
      label(p, 4, y + 4, eyes[i].name + (textured ? ": THE MAP" : ""));
      lab::draw_shape(p, cw, y, cw, ch, cam_of(eyes[i].eye), model, shape, style());
      label(p, cw + 4, y + 4, "THE CLONE");
    }
    std::string id = "clone-" + stem;
    for (char& c : id) c = char(std::tolower(uint8_t(c)));
    lab.shot({id,
              "CLONED: " + l.name,
              "Left the map as it is drawn (when the pack's palette is there), right its clone: "
              "the cells of its tree as brushes, built by CSG, each face in its texture's mean "
              "colour (the procedural texture comes later), from the same eyes.",
              "the same walls, floors, steps and openings on both sides; nothing missing or "
              "doubled; no face showing through a wall",
              {{"pak", pak_path}, {"map", l.name}, {"texmap", texmap_path}}},
             p);
    {
      const lab::Shape cut = shape_of(mesh, true);
      Picture o = lab::create_picture(px * 2, px * 4 / 3, kDark);
      lab::draw_shape(o, 0, 0, px * 2, px * 4 / 3,
                      lab::orbit_cam(lab::shape_box(cut), 30, 60, 30, 0.95, 1.5), model, cut,
                      style());
      lab.shot({id + "-whole",
                "THE CLONE CUT AWAY: " + l.name,
                "The whole clone from above with every ceiling, roof and sky left off, so the "
                "rooms, corridors and liquids show from inside.",
                "the map's plan reads; no stray slabs far from it",
                {{"map", l.name}}},
               o);
    }
  }
  lab.table(table);
  lab.table(times, Det::measured);
}

const lab::LabSpec kSpec{
    "clone",
    lab::LabKind::picture,
    "plugin:rooms",
    "plugins/rooms",
    "rooms",
    "Does a classic map cloned into a room level keep its structure, and does a body stand in it?",
    {
        {"pak", "", "a pack archive (outside the tree) --map's paths are read from"},
        {"map", "", "maps inside --pak, comma separated (default: the brush plugin's fixture)"},
        {"texmap", "",
         "an approved texture map, or the matcher's directory holding <map>/textures.texmap "
         "(default: every texture a placeholder)"},
        {"placeholders", "0", "1: a texture the map does not name is drawn plain, not refused"},
        {"smooth", "0", "1: every face shaded round with its smooth neighbours"},
        {"write", "", "a directory OUTSIDE the tree the clones are written to (.gelv, .gelv.txt)"},
        {"px", "480", "a panel's width, px"},
    },
    "--px=160",
    run,
};
GE_LAB(kSpec);

}  // namespace
