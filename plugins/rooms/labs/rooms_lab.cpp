// LAB `rooms`: a room level LOOKED AT and MEASURED. The plan from above (every room's footprint,
// walls in their materials, openings, solids, names and seeds), the whole level grown and cut
// away from above (the walls nearest the eye and every ceiling drop out, so each room is seen from
// inside), every room on its own, every relief the format grows on a test room under a raking
// light, a cave grown from four seeds side by side, views from inside, and the cave's mouth wired.
// The numbers: the level's bytes on disk against the triangles it grows, each room's triangles
// and build time, and the whole level built on one thread and on every worker, held to the same
// bits. CPU pictures, in the digest; times measured.
//
// ge:summary Lab: a room level's plan, cutaway, rooms, reliefs, cave seeds, bytes and build times
// ge:keywords lab rooms level plan cutaway dollhouse relief seeds cave inside wireframe bytes
// ge:keywords triangles build-time parallel gelv
// ge:skill rooms labs
// ge:test tests/rooms_build_test.cpp
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "assets/model.hpp"
#include "assets/pose.hpp"
#include "core/num_text.hpp"
#include "core/parallel.hpp"
#include "lab/lab.hpp"
#include "lab/model_view.hpp"
#include "lab/raster.hpp"
#include "move/spec.hpp"
#include "rooms/build.hpp"
#include "rooms/clip.hpp"
#include "rooms/codec.hpp"
#include "rooms/relief.hpp"
#include "rooms/text.hpp"
#include "rooms/walk.hpp"

namespace {

namespace rm = ge::rooms;
namespace lab = ge::lab;
using lab::Det;
using lab::Picture;
using lab::Rgba;

constexpr Rgba kPaper{246, 245, 240};
constexpr Rgba kInk{24, 26, 32};
constexpr Rgba kGrid{222, 220, 212};
constexpr Rgba kOpening{40, 170, 90};
constexpr Rgba kDark{22, 24, 30};
constexpr Rgba kTrail{214, 48, 40};
constexpr Rgba kMissed{230, 140, 20};

std::string fixed(double v, int d) { return ge::core::num::to_fixed(v, d); }

double ms_since(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// The level's materials as a model's, sRGB squared toward linear (no transcendental).
ge::assets::Model model_of(const rm::Level& level) {
  ge::assets::Model m;
  for (const rm::Material& mat : level.materials) {
    ge::assets::Material out;
    out.name = mat.name;
    for (int k = 0; k < 3; ++k) {
      const float c = float(mat.rgb[k]) / 255.0f;
      out.rgb[k] = c * c;
    }
    m.materials.push_back(out);
  }
  return m;
}

void add_mesh(lab::Shape& s, const rm::Mesh& m) {
  const auto base = uint32_t(s.part.size());
  s.pos.insert(s.pos.end(), m.pos.begin(), m.pos.end());
  s.nrm.insert(s.nrm.end(), m.nrm.begin(), m.nrm.end());
  s.part.insert(s.part.end(), m.mat.begin(), m.mat.end());
  s.top.resize(s.part.size(), 0);
  for (uint32_t i : m.idx) s.idx.push_back(base + i);
}

lab::ViewStyle style(bool wire = false) {
  lab::ViewStyle st;
  st.floor = false;
  st.wire = wire;
  st.background = kDark;
  st.edge = Rgba{255, 255, 255, 60};
  return st;
}

// A panel's label in its top-left corner, on a dark band.
void label(Picture& p, int x, int y, const std::string& text) {
  lab::fill_rect(p, x, y, lab::text_width(text, 2) + 10, 22, Rgba{0, 0, 0, 160});
  lab::draw_text(p, x + 5, y + 4, text, Rgba{240, 240, 240}, 2);
}

ge::assets::Box box_of(const lab::Shape& s) { return lab::shape_box(s); }

// The plan from above: +x right, +z up the page; with a walk, its tour's points (rings) and the
// feet's trail (a red line) over it.
Picture draw_plan(const rm::Level& level, int px, const rm::Tour* tour = nullptr,
                  const rm::Walk* walk = nullptr) {
  double lo[2] = {1e30, 1e30}, hi[2] = {-1e30, -1e30};
  for (const rm::Room& r : level.rooms) {
    for (size_t i = 0; i < r.corners.size(); ++i) {
      double c[2];
      rm::corner_m(r, i, c);
      for (int k = 0; k < 2; ++k) lo[k] = std::min(lo[k], c[k]), hi[k] = std::max(hi[k], c[k]);
    }
  }
  const double pad = 2, span = std::max(hi[0] - lo[0], hi[1] - lo[1]) + 2 * pad;
  const double s = px / span;
  const int w = int((hi[0] - lo[0] + 2 * pad) * s), h = int((hi[1] - lo[1] + 2 * pad) * s);
  Picture p = lab::create_picture(w, h, kPaper);
  const auto X = [&](double x) { return (x - lo[0] + pad) * s; };
  const auto Y = [&](double z) { return h - (z - lo[1] + pad) * s; };
  for (int gx = int(std::floor(lo[0] - pad)); gx <= int(hi[0] + pad); ++gx) {
    lab::draw_line(p, X(gx), 0, X(gx), h, kGrid);
  }
  for (int gz = int(std::floor(lo[1] - pad)); gz <= int(hi[1] + pad); ++gz) {
    lab::draw_line(p, 0, Y(gz), w, Y(gz), kGrid);
  }
  // Floors: a dot every 12 px in the floor's colour, so the rooms read as filled.
  for (const rm::Room& r : level.rooms) {
    const uint8_t* c = level.materials[r.floor].rgb;
    const Rgba fill{c[0], c[1], c[2], 120};
    std::vector<float> fx, fz;
    for (size_t i = 0; i < r.corners.size(); ++i) {
      double q[2];
      rm::corner_m(r, i, q);
      fx.push_back(float(q[0]));
      fz.push_back(float(q[1]));
    }
    for (int yy = 0; yy < h; yy += 3) {
      for (int xx = 0; xx < w; xx += 3) {
        const double x = xx / s + lo[0] - pad, z = (h - yy) / s + lo[1] - pad;
        bool in = false;
        for (size_t i = 0, j = fx.size() - 1; i < fx.size(); j = i++) {
          if ((fz[i] > z) != (fz[j] > z) &&
              x < (fx[j] - fx[i]) * (z - fz[i]) / (fz[j] - fz[i]) + fx[i]) {
            in = !in;
          }
        }
        if (in) lab::fill_rect(p, xx, yy, 3, 3, fill);
      }
    }
  }
  for (const rm::Solid& o : level.solids) {
    const rm::Room& r = level.rooms[o.room];
    const double x0 = rm::metres(int64_t(r.origin[0]) + o.min[0]);
    const double x1 = rm::metres(int64_t(r.origin[0]) + o.max[0]);
    const double z0 = rm::metres(int64_t(r.origin[2]) + o.min[2]);
    const double z1 = rm::metres(int64_t(r.origin[2]) + o.max[2]);
    const uint8_t* c = level.materials[o.material].rgb;
    lab::fill_rect(p, X(x0), Y(z1), (x1 - x0) * s, (z1 - z0) * s, Rgba{c[0], c[1], c[2], 230});
  }
  for (const rm::Room& r : level.rooms) {
    const double thick = r.shape == rm::Shape::cave ? 2 : 4;
    for (size_t i = 0; i < r.corners.size(); ++i) {
      double a[2], b[2];
      rm::corner_m(r, i, a);
      rm::corner_m(r, (i + 1) % r.corners.size(), b);
      const uint8_t* c = level.materials[r.corners[i].wall].rgb;
      lab::draw_line(p, X(a[0]), Y(a[1]), X(b[0]), Y(b[1]), Rgba{c[0], c[1], c[2]}, thick);
    }
  }
  for (const rm::Portal& o : level.portals) {
    const rm::Room& r = level.rooms[o.room];
    double a[2], b[2];
    rm::corner_m(r, o.wall, a);
    rm::corner_m(r, (o.wall + 1u) % r.corners.size(), b);
    const double len = rm::wall_length_m(r, o.wall);
    const double t0 = rm::metres(o.offset) / len, t1 = rm::metres(o.offset + o.width) / len;
    lab::draw_line(p, X(a[0] + (b[0] - a[0]) * t0), Y(a[1] + (b[1] - a[1]) * t0),
                   X(a[0] + (b[0] - a[0]) * t1), Y(a[1] + (b[1] - a[1]) * t1),
                   o.sill > 0 ? Rgba{70, 140, 220} : kOpening, 6);
  }
  for (const rm::Room& r : level.rooms) {
    double cx = 0, cz = 0;
    for (size_t i = 0; i < r.corners.size(); ++i) {
      double c[2];
      rm::corner_m(r, i, c);
      cx += c[0] / double(r.corners.size());
      cz += c[1] / double(r.corners.size());
    }
    const std::string a = r.name, b = "SEED " + std::to_string(r.seed);
    lab::draw_text(p, X(cx) - lab::text_width(a, 2) / 2.0, Y(cz) - 12, a, kInk, 2);
    lab::draw_text(p, X(cx) - lab::text_width(b) / 2.0, Y(cz) + 6, b, kInk);
  }
  if (walk) {
    for (size_t i = 1; i < walk->trail.size(); ++i) {
      const auto& a = walk->trail[i - 1];
      const auto& b = walk->trail[i];
      lab::draw_line(p, X(a.x), Y(a.z), X(b.x), Y(b.z), kTrail, 3);
    }
  }
  if (tour) {
    for (size_t i = 0; i < tour->points.size(); ++i) {
      const auto& q = tour->points[i].at;
      const bool got = walk && i < walk->reached;
      lab::fill_disc(p, X(q.x), Y(q.z), 4, kPaper);
      lab::draw_ring(p, X(q.x), Y(q.z), 5, got ? kInk : kMissed);
    }
  }
  return p;
}

// One test room for a relief: 4 x 4 m, 3 m high, every surface of that relief.
rm::Level relief_room(rm::Relief kind, uint32_t seed) {
  rm::Level level;
  rm::Material m;
  m.name = std::string(rm::relief_name(kind));
  m.rgb[0] = 170, m.rgb[1] = 164, m.rgb[2] = 152;
  m.relief = kind;
  // Depth and size the way a level would write them for that surface.
  const uint8_t depth[rm::kReliefKinds] = {0, 10, 18, 8, 3, 2, 2, 4, 4};
  const uint8_t scale[rm::kReliefKinds] = {8, 6, 10, 4, 4, 3, 3, 2, 6};
  m.relief_cm = depth[size_t(kind)];
  m.scale_dm = scale[size_t(kind)];
  level.materials.push_back(m);
  rm::Room r;
  r.name = "test";
  r.seed = seed;
  r.ceil_y = int16_t(rm::units_of(3.0));
  const int16_t e = int16_t(rm::units_of(4.0));
  r.corners = {{0, 0, 0}, {e, 0, 0}, {e, e, 0}, {0, e, 0}};
  level.rooms.push_back(r);
  std::string err;
  rm::link(level, &err);
  return level;
}

void run(lab::Lab& lab) {
  const std::string path = lab.arg("level");
  std::ifstream in(path);
  if (!in) {
    lab.fail("rooms: cannot read " + path,
             "next: pass --level=<file.gelv.txt>, a path from the repo root");
    return;
  }
  std::stringstream text;
  text << in.rdbuf();
  rm::Level level;
  std::string error;
  if (!rm::parse(text.str(), &level, &error)) {
    lab.fail("rooms: " + path + " " + error,
             "next: fix the line it names; see plugins/rooms/text.hpp");
    return;
  }
  const int px = int(lab.arg_num("px"));
  const auto bytes = rm::encode(level);

  // Build once to count, then time each room and the level on one thread and on every worker.
  const size_t workers = ge::core::default_workers();
  auto t0 = std::chrono::steady_clock::now();
  const auto meshes = rm::build_level(level, 1);
  const double one_ms = ms_since(t0);
  t0 = std::chrono::steady_clock::now();
  const auto again = rm::build_level(level, workers);
  const double all_ms = ms_since(t0);
  size_t tris = 0, verts = 0;
  lab::LabTable rooms{"ROOMS", {"room", "shape", "seed", "walls", "triangles", "vertices"}, {}};
  lab::LabTable times{"BUILD TIMES (measured)", {"room", "ms"}, {}};
  for (size_t r = 0; r < meshes.size(); ++r) {
    const rm::Room& room = level.rooms[r];
    if (rm::mesh_digest(meshes[r]) != rm::mesh_digest(again[r])) {
      lab.fail("rooms: room '" + room.name + "' grew different bits on " + std::to_string(workers) +
                   " workers than on one",
               "next: find the shared state in plugins/rooms/build.cpp; skill: determinism");
    }
    tris += meshes[r].triangles();
    verts += meshes[r].vertices();
    std::string walls;
    for (const rm::Corner& c : room.corners) {
      const std::string& n = level.materials[c.wall].name;
      if (walls.find(n) == std::string::npos) walls += (walls.empty() ? "" : "+") + n;
    }
    rooms.rows.push_back({room.name, room.shape == rm::Shape::cave ? "cave" : "prism",
                          std::to_string(room.seed), walls, std::to_string(meshes[r].triangles()),
                          std::to_string(meshes[r].vertices())});
    t0 = std::chrono::steady_clock::now();
    const rm::Mesh m = rm::build_room(level, r);
    times.rows.push_back({room.name, fixed(ms_since(t0), 2)});
  }
  lab.metric("bytes", double(bytes.size()), "count");
  lab.metric("text-bytes", double(text.str().size()), "count");
  lab.metric("rooms", double(level.rooms.size()), "count");
  lab.metric("triangles", double(tris), "count");
  lab.metric("vertices", double(verts), "count");
  lab.metric("triangles-per-byte", double(tris) / double(bytes.size()), "count");
  lab.metric("build-ms-one-thread", one_ms, "ms", Det::measured);
  lab.metric("build-ms-workers", all_ms, "ms", Det::measured);
  lab.metric("workers", double(workers), "count", Det::measured);
  lab.table(rooms);
  lab.table(times, Det::measured);

  const auto model = model_of(level);
  lab::Shape all;
  for (const rm::Mesh& m : meshes) add_mesh(all, m);
  const std::vector<std::pair<std::string, std::string>> inputs{{"level", path}};

  lab.shot(
      {"plan", "THE LEVEL FROM ABOVE",
       "Every room's footprint (floor colour dotted), its walls in their materials (thin: a "
       "cave), solids filled, doorways green and windows blue; each room named with its seed.",
       "rooms meet wall to wall with no gaps or overlaps; every doorway on a shared wall", inputs},
      draw_plan(level, px * 2));

  {
    const int w = px * 2, h = px * 4 / 3;
    Picture p = lab::create_picture(w, h, kDark);
    lab::draw_shape(p, 0, 0, w, h, lab::orbit_cam(box_of(all), 25, 58, 30, 0.95, double(w) / h),
                    model, all, style());
    lab.shot(
        {"cutaway", "THE LEVEL GROWN, CUT AWAY",
         "Every room grown from its record and seed, seen from above with the walls facing "
         "away and the ceilings dropped, so each room shows from inside: brick and plaster "
         "flat, the gallery's stone rough, two caves, boards, sheet metal, tiles, fieldstone.",
         "walls meet floors without gaps; caves join their doorways inside a timber frame", inputs},
        p);
  }

  {
    const int cols = 4, cw = px, ch = px * 3 / 4;
    const int rows_n = int((level.rooms.size() + cols - 1) / cols);
    Picture p = lab::create_picture(cols * cw, rows_n * ch, kDark);
    for (size_t r = 0; r < meshes.size(); ++r) {
      lab::Shape one;
      add_mesh(one, meshes[r]);
      const int x = int(r % cols) * cw, y = int(r / cols) * ch;
      lab::draw_shape(p, x, y, cw, ch, lab::orbit_cam(box_of(one), 35, 50, 30, 0.95, 4.0 / 3),
                      model, one, style());
      label(p, x + 4, y + 4, level.rooms[r].name);
    }
    lab.shot({"rooms", "EVERY ROOM ON ITS OWN",
              "Each room grown alone and cut away from the same corner: its walls, floor, frames "
              "and solids.",
              "the relief reads on the walls it should and nowhere else", inputs},
             p);
  }

  {
    const int cols = 4, cw = px, ch = px * 3 / 4;
    const int kinds = rm::kReliefKinds - 1;
    Picture p = lab::create_picture(cols * cw, ((kinds + cols - 1) / cols) * ch, kDark);
    for (int k = 1; k <= kinds; ++k) {
      const rm::Level t = relief_room(rm::Relief(k), 1234);
      const rm::Mesh m = rm::build_room(t, 0);
      lab::Shape one;
      add_mesh(one, m);
      lab::ViewCam cam;
      cam.eye[0] = 2.0, cam.eye[1] = 1.6, cam.eye[2] = 0.6;
      cam.target[0] = 3.2, cam.target[1] = 1.2, cam.target[2] = 4.0;
      cam.fov_deg = 70;
      const int x = ((k - 1) % cols) * cw, y = ((k - 1) / cols) * ch;
      // A raking light across the far wall (its normal is -z), from the right and above.
      lab::ViewStyle st = style();
      st.key[0] = 0.85, st.key[1] = 0.45, st.key[2] = -0.25;
      lab::draw_shape(p, x, y, cw, ch, cam, model_of(t), one, st);
      const rm::Material& mt = t.materials[0];
      label(p, x + 4, y + 4,
            std::string(rm::relief_name(mt.relief)) + " " + std::to_string(mt.relief_cm) + "CM " +
                std::to_string(m.triangles()) + " TRIS");
    }
    lab.shot({"reliefs",
              "EVERY RELIEF A SURFACE GROWS",
              "A 4 x 4 x 3 m room with every surface of one relief, from inside its corner, lit "
              "from the upper right: rock, crag, fieldstone, ashlar, tiles, planks, corrugated "
              "sheet, dented plate. The number is the relief's depth and the room's triangles.",
              "each reads as its material; the relief fades to flat where walls, floor and "
              "ceiling meet",
              {{"seed", "1234"}}},
             p);
  }

  {
    const int cave = rm::room_index(level, "cave");
    const auto seeds = lab.arg_nums("seeds");
    if (cave >= 0 && !seeds.empty()) {
      const int cw = px, ch = px * 3 / 4;
      Picture p = lab::create_picture(cw * int(seeds.size()), ch, kDark);
      rm::Level copy = level;
      for (size_t i = 0; i < seeds.size(); ++i) {
        copy.rooms[size_t(cave)].seed = uint32_t(seeds[i]);
        lab::Shape one;
        add_mesh(one, rm::build_room(copy, size_t(cave)));
        lab::draw_shape(p, int(i) * cw, 0, cw, ch,
                        lab::orbit_cam(box_of(one), 20, 32, 30, 0.95, 4.0 / 3), model, one,
                        style());
        label(p, int(i) * cw + 4, 4, "SEED " + std::to_string(uint32_t(seeds[i])));
      }
      lab.shot({"cave-seeds",
                "ONE CAVE, FOUR SEEDS",
                "The level's cave grown from each seed: the same bytes on disk (its footprint, "
                "heights and seed), a different rock every time. Seen from the far side over the "
                "cut-away near wall, its mouth into the hall on the far wall (the frame is the "
                "hall's). A thin dark crease is the cutaway looking past an overhang into the rock "
                "behind it, not a hole: from inside the rock is whole (`inside`).",
                "the rock differs seed to seed; the floor stays walkable; the mouth stays open",
                {{"seeds", lab.arg("seeds")}}},
               p);
    }
  }

  {
    struct Eye {
      const char* name;
      double eye[3], at[3];
    };
    const Eye eyes[] = {{"HALL TO THE CAVE", {5, 1.7, 1}, {6, 1.5, 9}},
                        {"IN THE CAVE", {5.5, 1.7, 15.5}, {6, 1.2, 8}},
                        {"WORKSHOP TO THE BOILER", {2, 1.7, -4}, {9, 1.4, -4.5}},
                        {"THE CELLAR", {1, 1.6, -9}, {7, 1.0, -13}}};
    const int cw = px, ch = px * 3 / 4;
    Picture p = lab::create_picture(cw * 2, ch * 2, kDark);
    for (int i = 0; i < 4; ++i) {
      lab::ViewCam cam;
      for (int k = 0; k < 3; ++k) cam.eye[k] = eyes[i].eye[k], cam.target[k] = eyes[i].at[k];
      cam.fov_deg = 75;
      lab::draw_shape(p, (i % 2) * cw, (i / 2) * ch, cw, ch, cam, model, all, style());
      label(p, (i % 2) * cw + 4, (i / 2) * ch + 4, eyes[i].name);
    }
    lab.shot({"inside", "STANDING IN THE LEVEL",
              "Four eyes at head height: the hall's brick and pillars toward the cave's framed "
              "mouth, the cave looking back at it, the workshop's boards toward the boiler room's "
              "sheet metal, the fieldstone cellar.",
              "no holes where rooms meet; doorways line up from both sides", inputs},
             p);
  }

  {
    const int cave = rm::room_index(level, "cave");
    if (cave >= 0) {
      lab::Shape one;
      add_mesh(one, meshes[size_t(cave)]);
      const int w = px * 2, h = px * 4 / 3;
      Picture p = lab::create_picture(w, h, kDark);
      lab::ViewCam cam;
      cam.eye[0] = 6.5, cam.eye[1] = 2.2, cam.eye[2] = 12.5;
      cam.target[0] = 6, cam.target[1] = 1.3, cam.target[2] = 8;
      cam.fov_deg = 60;
      lab::draw_shape(p, 0, 0, w, h, cam, model, one, style(true));
      lab.shot(
          {"cave-mouth-wire", "THE CAVE'S MOUTH, WIRED",
           "The cave's mouth from inside with every triangle's edges: the surface-nets rock "
           "at its cell size, the tunnel cut to the doorway, the cap across it removed and the "
           "timber frame over the rock's edge.",
           "even triangles; no cap left in the doorway; the frame covers the ragged rim", inputs},
          p);
    }
  }

  // The walk: the level as the mover meets it, a tour of every room, and the walker down it.
  {
    t0 = std::chrono::steady_clock::now();
    const ge::move::MeshWorld clip = rm::clip_world(level, workers);
    const double clip_ms = ms_since(t0);
    const ge::move::World world = ge::move::as_world(clip);
    const ge::move::MoveSpec spec = *ge::move::find_preset("walker");
    const auto start = lab.arg_nums("start");
    const ge::move::Vec3 from{start.size() > 0 ? start[0] : 0, start.size() > 1 ? start[1] : 0,
                              start.size() > 2 ? start[2] : 0};
    t0 = std::chrono::steady_clock::now();
    const rm::Tour tour = rm::tour(level, world, spec, from);
    const double tour_ms = ms_since(t0);
    if (!tour.error.empty()) lab.fail("rooms: the tour stops short: " + tour.error, "skill: rooms");
    rm::WalkOptions options;
    options.sprint = true;
    t0 = std::chrono::steady_clock::now();
    const rm::Walk walk = rm::walk(level, world, spec, tour, options);
    const double walk_ms = ms_since(t0);
    if (!walk.error.empty()) lab.fail("rooms: the walk stops short: " + walk.error, "skill: rooms");
    double metres_walked = 0;
    for (size_t i = 1; i < walk.trail.size(); ++i) {
      const double dx = walk.trail[i].x - walk.trail[i - 1].x;
      const double dz = walk.trail[i].z - walk.trail[i - 1].z;
      metres_walked += std::sqrt(dx * dx + dz * dz);
    }
    lab.metric("clip-triangles", double(clip.tri.size()) / 3, "count");
    lab.metric("tour-points", double(tour.points.size()), "count");
    lab.metric("walk-rooms", double(walk.rooms.size()), "count");
    lab.metric("walk-steps", double(walk.steps), "count");
    lab.metric("walk-m", metres_walked, "m");
    lab.metric("walk-under-floor-m", walk.lowest_below_floor, "m");
    lab.metric("clip-build-ms", clip_ms, "ms", Det::measured);
    lab.metric("tour-ms", tour_ms, "ms", Det::measured);
    lab.metric("walk-ms", walk_ms, "ms", Det::measured);
    lab.metric("walk-us-per-step", walk.steps ? walk_ms * 1000 / double(walk.steps) : 0, "us",
               Det::measured);
    const std::vector<std::pair<std::string, std::string>> walk_inputs{{"level", path},
                                                                       {"start", lab.arg("start")}};
    lab.shot({"walk", "A WALKER THROUGH EVERY ROOM",
              "The tour (rings: black reached, orange not) from the hall through every room by "
              "its doorways and back, routed round the pillars, down the crypt's steps and over "
              "the caves' rock; the red line is where the walker's feet went, sprinting, on the "
              "mover's own collision with the level (flat prisms, grown caves).",
              "the red line passes every doorway and ends where it began; no ring is orange",
              walk_inputs},
             draw_plan(level, px * 2, &tour, &walk));

    // Four rooms as the walker comes into them: its eyes at the doorway, looking at the farthest
    // point it walks to in the room before it turns back or on.
    {
      const char* names[4] = {"cave", "crypt", "grotto", "boiler"};
      const int cw = px, ch = px * 3 / 4;
      Picture p = lab::create_picture(cw * 2, ch * 2, kDark);
      for (int i = 0; i < 4; ++i) {
        const int r = rm::room_index(level, names[i]);
        size_t first = tour.points.size();
        for (size_t k = 0; k < tour.points.size(); ++k) {
          if (tour.points[k].room == r) {
            first = k;
            break;
          }
        }
        if (first == tour.points.size()) continue;
        const auto& a = tour.points[first].at;
        auto far = a;
        double far_d = -1;
        for (size_t k = first; k < tour.points.size() && tour.points[k].room == r; ++k) {
          const auto& q = tour.points[k].at;
          const double d = (q.x - a.x) * (q.x - a.x) + (q.z - a.z) * (q.z - a.z);
          if (d > far_d) far = q, far_d = d;
        }
        lab::ViewCam cam;
        cam.eye[0] = a.x, cam.eye[1] = a.y + spec.eye_height, cam.eye[2] = a.z;
        cam.target[0] = far.x, cam.target[1] = far.y + 1.0, cam.target[2] = far.z;
        cam.fov_deg = 75;
        lab::draw_shape(p, (i % 2) * cw, (i / 2) * ch, cw, ch, cam, model, all, style());
        label(p, (i % 2) * cw + 4, (i / 2) * ch + 4, std::string("INTO THE ") + names[i]);
      }
      lab.shot({"walk-eyes", "THE WALK THROUGH ITS EYES",
                "Four rooms as the walker comes in, from its eyes (1.62 m) at the doorway toward "
                "the room's middle: the cave and the grotto it climbs into over their rock, the "
                "crypt down its steps, the boiler room round its tank.",
                "the eye is inside the room, never in a wall or under a floor", walk_inputs},
               p);
    }
  }
}

const lab::LabSpec kSpec{
    "rooms",
    lab::LabKind::picture,
    "plugin:rooms",
    "plugins/rooms",
    "rooms",
    "What does a room level grow into from its few bytes and seeds, and how fast?",
    {
        {"level", "plugins/rooms/levels/halls.gelv.txt", "the level, as text (.gelv.txt)"},
        {"px", "480", "a panel's width, px"},
        {"seeds", "88123,1,2,3", "the cave's seeds, side by side"},
        {"start", "5,0,4", "where the walk starts, world m (x, y, z), on a room's floor"},
    },
    "--px=120 --seeds=88123,1",
    run,
};
GE_LAB(kSpec);

}  // namespace
