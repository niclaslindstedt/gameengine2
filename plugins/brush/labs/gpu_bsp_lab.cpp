// LAB `gpu-bsp`: THE BSP RENDERER (plugins/brush/app/bsp_draw.hpp) LOOKED AT: a classic map drawn
// three ways from the same eye: CLASSIC (its textures, lightmaps, warped liquids and its own sky),
// V2 WITH LIGHTMAPS (our sky and clouds through the sky faces, our water, lava and slime in the
// liquids), and V2 WITH REAL-TIME LIGHTS (the light entities as point lights with their flicker
// and cube shadows, the lava's glow, the sun and the sky's light through the sky faces). Each map
// also from above, its liquids, lights and the start marked, to choose views by.
//
// With `--pak=<file>` and `--maps=<a>,<b>` the maps are read out of a pack (the palette its
// gfx/palette.lmp); without, the brush plugin's sample fixture is drawn, so CI needs no game data.
// `--view=<map>:x,y,z,heading,pitch` (engine metres, degrees) adds a view; several by `;`.
//
// ge:summary Lab: a classic map drawn classic, v2 with lightmaps and v2 with real-time lights
// ge:keywords lab bsp brush renderer classic v2 sky liquids lava slime water point-light shadow
// ge:keywords before-after overview gpu-bsp
// ge:skill brush render labs
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "brush/app/bsp_draw.hpp"
#include "brush/app/bsp_liquids.hpp"
#include "brush/app/draw_data.hpp"
#include "brush/write.hpp"
#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "gpu/device.hpp"
#include "gpu/target.hpp"
#include "lab/lab.hpp"
#include "lab/labs/liquid_scene.hpp"
#include "lab/raster.hpp"
#include "render/bloom.hpp"
#include "render/clouds.hpp"
#include "render/frame.hpp"
#include "render/liquid.hpp"
#include "render/post.hpp"
#include "render/sky.hpp"
#include "render/water.hpp"

namespace {

namespace gl = ge::lab;
namespace gr = ge::render;
namespace ba = ge::brush::app;
namespace ls = ge::lab::liquid_scene;
using gl::Det;
constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180;

struct MapState {
  ba::Scene scene;
  ba::BspDraw bsp;
  gr::Water water;
  gr::Liquids liquids;
  ba::LiquidWorld world;
  ba::LiquidRows rows;
};

struct Frame {
  ge::gpu::PipelineSet pipelines;
  gr::FrameTargets targets;
  wgpu::Buffer uniforms;
  gr::FrameUniforms staged{};
  wgpu::BindGroupLayout layout;
  wgpu::BindGroup frame, post_group;
  gr::Sky sky;
  gr::CloudPass clouds;
  gr::Bloom bloom;
  gr::Post post;
  gr::FrameRecorder rec;
  ge::gpu::Target out;
  uint32_t w = 0, h = 0;
};

struct View {
  std::string id, title, caption;
  size_t map = 0;
  double eye[3] = {0, 0, 0};
  double heading = 0, pitch = 0;
};

// The sky of the views: the sun where `--sun` puts it, a fair day's heaps of cloud.
void dress(gr::FrameView& v, double sun_el, double sun_az, double cover, double t) {
  v.sun_elevation = sun_el;
  v.sun_azimuth = sun_az;
  v.altitude_m = 600;
  v.time_s = t;
  v.fov_y = 1.25;
  v.near_m = 0.05;
  if (cover > 0) {
    v.clouds.count = 1;
    gr::CloudLayer& l = v.clouds.layers[0];
    l.base_m = 1500;
    l.top_m = 2300;
    l.cover = cover;
    l.cell_m = 1600;
    l.stretch = 1.3;
    l.dome = 1;
    l.density = 28.0 / 800;
    l.shadow_tau = 2.4;
    v.clouds.quarter = false;
    v.clouds.spread = false;
  }
}

gl::Picture draw(ge::gpu::Device& d, Frame& f, MapState& m, const gr::FrameView& v,
                 const ba::Options& o) {
  f.rec.begin_frame(d);
  gr::fill_uniforms(f.staged, v, f.w, f.h);
  d.queue.WriteBuffer(f.uniforms, 0, &f.staged, sizeof f.staged);
  gr::prepare_sky(f.sky, v);
  ba::prepare_bsp(d, m.bsp, v, o);
  const bool v2 = o.mode == ba::Mode::v2;
  for (auto& b : m.water.bodies) b.hidden = !v2;
  for (auto& b : m.liquids.bodies) b.hidden = !v2;
  ba::light_rooms(m.water, m.liquids, m.rows, o.minlight, o.brightness);
  gr::prepare_water(d, m.water, v);
  gr::prepare_liquid(d, m.liquids, v);
  wgpu::CommandEncoder e = d.device.CreateCommandEncoder();
  gr::encode_sky_tables(f.sky, f.pipelines, f.rec, e);
  ba::encode_bsp(m.bsp, f.pipelines, f.rec, e, f.targets, f.frame);
  gr::encode_sky(f.sky, f.pipelines, f.rec, e, f.targets, f.frame);
  gr::encode_clouds(f.clouds, f.pipelines, f.rec, e, f.targets, f.frame, v, f.staged);
  gr::encode_water(m.water, f.pipelines, f.rec, e, f.targets, f.frame);
  gr::encode_liquid(m.liquids, f.pipelines, f.rec, e, f.targets, f.frame);
  gr::encode_bloom(f.bloom, f.pipelines, f.rec, e, 0);
  gr::encode_post(f.post, f.pipelines, f.rec, e, f.frame, f.post_group, f.out.view);
  f.rec.end_frame(e);
  wgpu::CommandBuffer c = e.Finish();
  d.queue.Submit(1, &c);
  f.rec.after_submit(d);
  const ge::gpu::Image img = ge::gpu::read_now(d, f.out);
  return gl::Picture{static_cast<int>(img.width), static_cast<int>(img.height), img.rgba};
}

// A filled triangle on the raster (the overview's).
void fill_tri(gl::Picture& p, const double a[2], const double b[2], const double c[2],
              gl::Rgba col) {
  const int x0 = std::max(0, int(std::floor(std::min({a[0], b[0], c[0]}))));
  const int x1 = std::min(p.width - 1, int(std::ceil(std::max({a[0], b[0], c[0]}))));
  const int y0 = std::max(0, int(std::floor(std::min({a[1], b[1], c[1]}))));
  const int y1 = std::min(p.height - 1, int(std::ceil(std::max({a[1], b[1], c[1]}))));
  const double area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
  if (std::fabs(area) < 1e-9) return;
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const double px = x + 0.5, py = y + 0.5;
      const double w0 = ((b[0] - px) * (c[1] - py) - (b[1] - py) * (c[0] - px)) / area;
      const double w1 = ((c[0] - px) * (a[1] - py) - (c[1] - py) * (a[0] - px)) / area;
      const double w2 = 1 - w0 - w1;
      if (w0 >= 0 && w1 >= 0 && w2 >= 0) gl::set_pixel(p, x, y, col);
    }
  }
}

// The map from above: the floors shaded by height (the highest drawn last), the liquids in their
// colours, the sky faces blue, each light a dot, the start a ring; a grid every 10 m, labelled.
gl::Picture overview(const ba::Scene& s, int size) {
  const double span = std::max(s.maxs[0] - s.mins[0], s.maxs[2] - s.mins[2]) + 4;
  const double k = (size - 40) / span;
  const auto to = [&](double x, double z, double out[2]) {
    out[0] = 20 + (x - s.mins[0] + 2) * k;
    out[1] = size - 20 - (z - s.mins[2] + 2) * k;
  };
  gl::Picture p = gl::create_picture(size, size, {18, 20, 26});
  std::vector<size_t> tris;
  for (size_t i = 0; i + 2 < s.indices.size(); i += 3) tris.push_back(i);
  const auto height = [&](size_t i) {
    return s.vertices[s.indices[i]].pos[1] + s.vertices[s.indices[i + 1]].pos[1] +
           s.vertices[s.indices[i + 2]].pos[1];
  };
  // Floors by height, the liquids over them.
  const auto wet = [&](size_t i) {
    const uint32_t kind = s.vertices[s.indices[i]].kind;
    return kind == ba::kWater || kind == ba::kSlime || kind == ba::kLava;
  };
  std::sort(tris.begin(), tris.end(),
            [&](size_t a, size_t b) { return wet(a) != wet(b) ? wet(b) : height(a) < height(b); });
  const double ylo = s.mins[1], yhi = std::max(s.maxs[1], s.mins[1] + 1);
  for (size_t i : tris) {
    const ba::DrawVertex& a = s.vertices[s.indices[i]];
    const ba::DrawVertex& b = s.vertices[s.indices[i + 1]];
    const ba::DrawVertex& c = s.vertices[s.indices[i + 2]];
    const float ny = std::fabs(a.nrm[1]);
    if (a.kind == ba::kHidden || a.kind == ba::kSky || ny < 0.5f) continue;
    double pa[2], pb[2], pc[2];
    to(a.pos[0], a.pos[2], pa);
    to(b.pos[0], b.pos[2], pb);
    to(c.pos[0], c.pos[2], pc);
    gl::Rgba col{0, 0, 0};
    const double t = std::clamp((a.pos[1] - ylo) / (yhi - ylo), 0.0, 1.0);
    if (a.kind == ba::kWater)
      col = {60, 120, 210};
    else if (a.kind == ba::kSlime)
      col = {90, 200, 60};
    else if (a.kind == ba::kLava)
      col = {240, 90, 20};
    else if (a.kind == ba::kSky)
      col = {150, 200, 255};
    else if (a.kind == ba::kWarp)
      col = {200, 80, 220};
    else
      col = {uint8_t(60 + 150 * t), uint8_t(60 + 150 * t), uint8_t(64 + 150 * t)};
    fill_tri(p, pa, pb, pc, col);
  }
  for (double x = std::ceil(s.mins[0] / 10) * 10; x <= s.maxs[0]; x += 10) {
    double q[2];
    to(x, s.mins[2], q);
    gl::draw_text(p, q[0], size - 12, ls::fixed(x, 0), {150, 150, 160});
  }
  for (double z = std::ceil(s.mins[2] / 10) * 10; z <= s.maxs[2]; z += 10) {
    double q[2];
    to(s.mins[0], z, q);
    gl::draw_text(p, 2, q[1] - 3, ls::fixed(z, 0), {150, 150, 160});
  }
  // The sky's ceilings, outlined.
  for (size_t i = 0; i + 2 < s.indices.size(); i += 3) {
    const ba::DrawVertex* v[3] = {&s.vertices[s.indices[i]], &s.vertices[s.indices[i + 1]],
                                  &s.vertices[s.indices[i + 2]]};
    if (v[0]->kind != ba::kSky || v[0]->nrm[1] > -0.5f) continue;
    for (int e = 0; e < 3; ++e) {
      double a[2], b[2];
      to(v[e]->pos[0], v[e]->pos[2], a);
      to(v[(e + 1) % 3]->pos[0], v[(e + 1) % 3]->pos[2], b);
      gl::draw_line(p, a[0], a[1], b[0], b[1], {120, 190, 255});
    }
  }
  for (const ba::PointLight& l : s.lights) {
    double q[2];
    to(l.pos[0], l.pos[2], q);
    gl::fill_disc(p, q[0], q[1], 2, l.flame ? gl::Rgba{255, 170, 60} : gl::Rgba{255, 240, 180});
  }
  double q[2];
  to(s.start[0], s.start[2], q);
  gl::draw_ring(p, q[0], q[1], 6, {255, 60, 60});
  gl::draw_line(p, q[0], q[1], q[0] + 14 * ge::core::libm::sin(s.start_heading),
                q[1] - 14 * ge::core::libm::cos(s.start_heading), {255, 60, 60});
  return p;
}

// Whether the eye at `e` (engine metres) is in the map's open air (not in a wall, nor outside).
bool open_at(const ge::brush::Map& m, const double e[3]) {
  const float f[3] = {float(e[0] / ge::brush::kUnitM), float(e[2] / ge::brush::kUnitM),
                      float(e[1] / ge::brush::kUnitM)};
  const ge::brush::Leaf& l = m.leaves[ge::brush::leaf_at(m, f)];
  return !l.solid && l.contents != -6 && l.contents != -2;
}

// How far the eye can see along a heading at its own height before a wall, m (up to 40).
double open_run(const ge::brush::Map& m, const double e[3], double heading) {
  double d = 0;
  for (; d < 40; d += 0.4) {
    const double p[3] = {e[0] + d * ge::core::libm::sin(heading), e[1],
                         e[2] + d * ge::core::libm::cos(heading)};
    if (!open_at(m, p)) break;
  }
  return d;
}

double widest(const ge::brush::Map& m, const double e[3]) {
  double best = 0, run = -1;
  for (int i = 0; i < 16; ++i) {
    const double hd = i * kPi / 8;
    const double r = open_run(m, e, hd);
    if (r > run) run = r, best = hd;
  }
  return best;
}

// Back the eye off along the way it looks, up to `most` m, staying in the open.
void back_off(const ge::brush::Map& m, double e[3], double heading, double most) {
  for (double d = 0.4; d <= most; d += 0.4) {
    const double p[3] = {e[0] - 0.4 * ge::core::libm::sin(heading), e[1],
                         e[2] - 0.4 * ge::core::libm::cos(heading)};
    if (!open_at(m, p)) return;
    for (int k = 0; k < 3; ++k) e[k] = p[k];
  }
}

// The views a map gets by itself: from the start; over the largest pool of each liquid, from
// above its largest surface along the longest open way; and up at its largest sky.
std::vector<View> auto_views(const ba::Scene& s, size_t map, const std::string& name) {
  std::vector<View> out;
  View start;
  start.id = name + "-start";
  start.title = name + ": the start";
  start.caption = "From where the player starts, looking the way they face.";
  start.map = map;
  for (int k = 0; k < 3; ++k) start.eye[k] = s.start[k];
  start.heading = s.start_heading;
  out.push_back(start);
  bool seen[ba::kKinds] = {};
  for (const ba::LiquidPatch& p : s.liquids) {
    if (seen[p.kind]) continue;
    View v;
    // An eye over the pool with nothing between it and the surface: its largest surface's middle,
    // else a point of a grid over its box; the lowest such eye from 1.4 m up.
    bool ok = false;
    std::vector<std::pair<double, double>> at = {{p.centre[0], p.centre[1]}};
    for (int i = 0; i < 5; ++i)
      for (int j = 0; j < 5; ++j)
        at.push_back({p.box[0] + (i + 0.5) / 5 * (p.box[2] - p.box[0]),
                      p.box[1] + (j + 0.5) / 5 * (p.box[3] - p.box[1])});
    for (const auto& [x, z] : at) {
      for (double up = 1.4; up < 5 && !ok; up += 0.4) {
        bool clear = true;
        for (double y = p.level + 0.1; y <= p.level + up + 0.3 && clear; y += 0.2) {
          const double q[3] = {x, y, z};
          clear = open_at(s.map, q);
        }
        if (clear) {
          v.eye[0] = x;
          v.eye[1] = p.level + up;
          v.eye[2] = z;
          ok = true;
        }
      }
      if (ok) break;
    }
    if (!ok) continue;
    seen[p.kind] = true;
    const char* what = p.kind == ba::kLava ? "lava" : p.kind == ba::kSlime ? "slime" : "water";
    v.id = name + "-" + what;
    v.title = name + ": the " + what;
    v.caption = std::string("Over the largest pool of ") + what + ", along its longest open way.";
    v.map = map;
    v.heading = widest(s.map, v.eye);
    back_off(s.map, v.eye, v.heading, 4);
    v.pitch = -0.32;
    out.push_back(v);
  }
  // The largest sky ceiling.
  double best = 0, c[3] = {0, 0, 0};
  for (size_t i = 0; i + 2 < s.indices.size(); i += 3) {
    const ba::DrawVertex& a = s.vertices[s.indices[i]];
    const ba::DrawVertex& b = s.vertices[s.indices[i + 1]];
    const ba::DrawVertex& cc = s.vertices[s.indices[i + 2]];
    if (a.kind != ba::kSky || a.nrm[1] > -0.5f) continue;
    const double ux = b.pos[0] - a.pos[0], uz = b.pos[2] - a.pos[2];
    const double vx = cc.pos[0] - a.pos[0], vz = cc.pos[2] - a.pos[2];
    const double area = 0.5 * std::fabs(ux * vz - uz * vx);
    if (area > best) {
      best = area;
      for (int k = 0; k < 3; ++k) c[k] = (a.pos[k] + b.pos[k] + cc.pos[k]) / 3;
    }
  }
  if (best > 0) {
    View v;
    v.eye[0] = c[0];
    v.eye[2] = c[2];
    bool ok = false;
    for (double down = 4; down < 30 && !ok; down += 0.5) {
      v.eye[1] = c[1] - down;
      const double below[3] = {v.eye[0], v.eye[1] - 1.5, v.eye[2]};
      ok = open_at(s.map, v.eye) && open_at(s.map, below);
    }
    if (ok) {
      v.heading = widest(s.map, v.eye);
      back_off(s.map, v.eye, v.heading, 8);
      const double run = ge::core::hypot(c[0] - v.eye[0], c[2] - v.eye[2]);
      v.pitch = ge::core::libm::atan2(c[1] - v.eye[1], std::max(run, 1.0)) * 0.8;
      v.id = name + "-sky";
      v.title = name + ": the sky";
      v.caption = "Up at the largest of the map's sky ceilings.";
      v.map = map;
      out.push_back(v);
    }
  }
  return out;
}

bool parse_view(const std::string& spec, const std::vector<std::string>& names, View* out) {
  const size_t colon = spec.find(':');
  if (colon == std::string::npos) return false;
  const std::string map = spec.substr(0, colon);
  double x, y, z, hd, pt;
  if (std::sscanf(spec.c_str() + colon + 1, "%lf,%lf,%lf,%lf,%lf", &x, &y, &z, &hd, &pt) != 5)
    return false;
  for (size_t i = 0; i < names.size(); ++i) {
    if (names[i] != map) continue;
    out->map = i;
    out->eye[0] = x;
    out->eye[1] = y;
    out->eye[2] = z;
    out->heading = hd * kDeg;
    out->pitch = pt * kDeg;
    out->id = map + "-at-" + ls::fixed(x, 0) + "-" + ls::fixed(z, 0);
    out->title = map + " at " + ls::fixed(x, 1) + ", " + ls::fixed(y, 1) + ", " + ls::fixed(z, 1);
    out->caption = "A view picked by hand.";
    return true;
  }
  return false;
}

std::vector<std::string> split(const std::string& s, char c) {
  std::vector<std::string> out;
  size_t at = 0;
  while (at <= s.size()) {
    size_t end = s.find(c, at);
    if (end == std::string::npos) end = s.size();
    if (end > at) out.push_back(s.substr(at, end - at));
    at = end + 1;
  }
  return out;
}

void run(gl::Lab& lab) {
  const uint32_t w = static_cast<uint32_t>(lab.arg_num("width"));
  const uint32_t h = w * 9 / 16;
  const std::vector<double> sun = lab.arg_nums("sun");
  const double sun_el = (sun.size() > 0 ? sun[0] : 30) * kDeg;
  const double sun_az = (sun.size() > 1 ? sun[1] : 220) * kDeg;
  const double cover = lab.arg_num("clouds");
  const double t = lab.arg_num("time");

  // ── The maps ──
  ba::Pack pack;
  std::vector<uint8_t> palette;
  std::vector<std::string> paths, names;
  if (!lab.arg("pak").empty()) {
    std::string err;
    if (!ba::read_pack(lab.arg("pak"), &pack, &err)) {
      lab.fail(err,
               "Pass --pak=<a pack file> holding maps/*.bsp and gfx/palette.lmp. Skill: brush.");
      return;
    }
    const auto pal = ba::pack_file(pack, "gfx/palette.lmp");
    palette.assign(pal.begin(), pal.end());
    for (const std::string& m : split(lab.arg("maps"), ',')) {
      paths.push_back("maps/" + m + ".bsp");
      names.push_back(m);
    }
  } else {
    std::vector<uint8_t> bytes;
    std::string err;
    ge::brush::write_bsp(ge::brush::sample_fixture(), ge::brush::Family::v29, &bytes, &err);
    pack.bytes = bytes;
    pack.files.push_back({"maps/fixture.bsp", 0, uint32_t(bytes.size())});
    paths.push_back("maps/fixture.bsp");
    names.push_back("fixture");
  }
  if (palette.size() < 768) {
    palette.resize(768);
    for (int i = 0; i < 256; ++i)
      for (int k = 0; k < 3; ++k) palette[size_t(i * 3 + k)] = uint8_t(i);
  }
  std::vector<std::unique_ptr<MapState>> maps;
  for (size_t i = 0; i < paths.size(); ++i) {
    auto m = std::make_unique<MapState>();
    std::string err;
    if (!ba::load_scene(pack, paths[i], palette, &m->scene, &err)) {
      lab.fail(paths[i] + ": " + err, "Check the map's name in the pack (--maps). Skill: brush.");
      return;
    }
    const ba::Scene& s = m->scene;
    gl::LabTable table{names[i] + ": what the map holds", {"what", "count"}, {}};
    const char* kinds[] = {"solid", "sky", "water", "slime", "lava", "hidden", "warped"};
    for (int k = 0; k < ba::kKinds; ++k) {
      table.rows.push_back({std::string(kinds[k]) + " triangles", std::to_string(s.counts[k])});
      lab.metric(names[i] + ".tris." + kinds[k], s.counts[k], "count");
    }
    table.rows.push_back({"liquid bodies", std::to_string(s.liquids.size())});
    table.rows.push_back({"lights", std::to_string(s.lights.size())});
    lab.metric(names[i] + ".bodies", double(s.liquids.size()), "count");
    lab.metric(names[i] + ".lights", double(s.lights.size()), "count");
    lab.table(table);
    gl::LabTable bodies{
        names[i] + ": the liquids' bodies", {"kind", "level m", "box m", "area m^2"}, {}};
    for (const ba::LiquidPatch& p : s.liquids) {
      bodies.rows.push_back({p.kind == ba::kLava    ? "lava"
                             : p.kind == ba::kSlime ? "slime"
                                                    : "water",
                             ls::fixed(p.level, 2),
                             ls::fixed(p.box[0], 1) + "," + ls::fixed(p.box[1], 1) + " .. " +
                                 ls::fixed(p.box[2], 1) + "," + ls::fixed(p.box[3], 1),
                             ls::fixed(p.area, 1)});
    }
    lab.table(bodies);
    lab.shot(
        {names[i] + "-above",
         names[i] + " from above",
         "The floors shaded by height, water blue, slime green, lava orange, the sky's "
         "ceilings outlined in blue, lights dots (flames orange), the start a red ring. Metres on "
         "the edges.",
         "Where the liquids and the open sky are, to pick views by.",
         {{"map", names[i]}}},
        overview(s, 900), Det::deterministic);
    maps.push_back(std::move(m));
  }

  std::vector<View> views;
  for (size_t i = 0; i < maps.size(); ++i) {
    for (View& v : auto_views(maps[i]->scene, i, names[i])) views.push_back(v);
  }
  for (const std::string& spec : split(lab.arg("view"), ';')) {
    View v;
    if (parse_view(spec, names, &v))
      views.push_back(v);
    else
      lab.fail("--view=" + spec + " is not <map>:x,y,z,heading,pitch", "Name a map in --maps.");
  }
  std::vector<std::string> ids;
  for (const View& v : views) ids.push_back(v.id);

  auto device = ge::gpu::create_device({.label = "gpu-bsp", .validation = 1});
  if (!device) {
    if (ls::gpu_required()) {
      lab.fail("no GPU device, and GE_GPU_REQUIRE=1",
               "Install Mesa's Vulkan drivers (apt install mesa-vulkan-drivers). Skill: gpu.");
    }
    lab.note("No GPU adapter on this machine: no pictures.");
    for (const std::string& id : ids) {
      ls::placeholders(lab, {id.c_str()});
    }
    ls::checked(lab, 0, 0);
    return;
  }
  ge::gpu::Device& d = *device;
  Frame f;
  f.w = w;
  f.h = h;
  f.targets = gr::create_targets(d, "gpu-bsp", w, h);
  f.uniforms =
      gr::make_buffer(d, "gpu-bsp.uniforms", sizeof(gr::FrameUniforms), wgpu::BufferUsage::Uniform);
  f.layout = gr::frame_layout(d);
  gr::create_sky(d, f.pipelines, f.sky, f.uniforms, f.layout);
  gr::create_clouds(d, f.pipelines, f.clouds, f.layout);
  gr::BloomSettings bs;
  bs.strength = 0.05;
  gr::create_bloom_pipelines(d, f.pipelines, f.bloom, bs);
  gr::PostSettings ps;
  ps.bloom = 0.05;
  gr::create_post(d, f.pipelines, f.post, f.layout, wgpu::TextureFormat::RGBA8Unorm, ps);
  ba::LiquidRows rows;
  ba::LiquidRows::Grime& g = rows.slime_grime;
  const auto knob = [&](const char* name, double* v) {
    if (!lab.arg(name).empty()) *v = lab.arg_num(name);
  };
  knob("murk", &g.murk);
  knob("scum", &g.scum);
  knob("grime", &g.grime);
  knob("debris", &g.debris);
  knob("film", &g.film);
  knob("bubbles", &g.bubbles);
  knob("glow", &g.glow);
  for (auto& m : maps) {
    m->rows = rows;
    ba::create_bsp(d, f.pipelines, m->bsp, m->scene, f.layout);
    gr::create_water(d, f.pipelines, m->water, f.layout);
    gr::create_liquid(d, f.pipelines, m->liquids, f.layout);
    m->world = ba::liquid_world(m->scene, rows);
    ba::add_bsp_liquids(d, m->water, m->liquids, m->world, rows);
  }
  if (!f.pipelines.finish(d)) {
    lab.fail("the BSP renderer's pipelines did not build",
             "Read the gpu: lines above (plugins/brush/app/*.wgsl). Skill: render.");
    return;
  }
  ge::gpu::end_load(d);
  f.frame = gr::frame_group(d, f.layout, f.uniforms, f.sky);
  gr::bind_cloud_targets(d, f.clouds, f.targets);
  const wgpu::TextureView inputs[2] = {f.targets.hdr_view, f.targets.hdr_view};
  gr::create_bloom_targets(d, f.bloom, w, h, inputs);
  f.post_group = gr::post_group(d, f.post, f.targets.hdr_view, f.bloom.view);
  f.rec.init(d, "gpu-bsp.timer", 16);
  f.out = ge::gpu::create_target(d, "gpu-bsp.final", w, h);
  for (auto& m : maps) {
    ba::create_bsp_targets(d, m->bsp, f.targets);
    gr::create_water_targets(d, m->water, f.targets);
    gr::create_liquid_targets(d, m->liquids, f.targets);
  }

  // ── The views, three ways each, side by side ──
  struct Way {
    const char* label;
    ba::Options o;
  };
  std::vector<Way> ways(3);
  ways[0].label = "CLASSIC";
  ways[0].o.mode = ba::Mode::classic;
  ways[1].label = "V2: LIGHTMAPS, OUR SKY AND LIQUIDS";
  ways[1].o.lighting = ba::Lighting::lightmaps;
  ways[2].label = "V2: REAL-TIME LIGHTS";
  ways[2].o.strength = lab.arg_num("strength") > 0 ? lab.arg_num("strength") : 1;
  if (!lab.arg("shadowed").empty()) ways[2].o.shadowed = int(lab.arg_num("shadowed"));
  if (!lab.arg("most").empty()) ways[2].o.most = int(lab.arg_num("most"));
  if (lab.arg_num("brightness") > 0) {
    for (Way& way : ways) way.o.brightness = lab.arg_num("brightness");
  }
  if (!lab.arg("minlight").empty()) {
    for (Way& way : ways) way.o.minlight = lab.arg_num("minlight");
  }
  if (!lab.arg("bounce").empty()) {
    for (Way& way : ways) way.o.bounce = lab.arg_num("bounce");
  }
  // The ladder: v2 lit in real time with neither the floor nor the bounce, then at each floor.
  std::vector<Way> ladder;
  std::vector<std::string> labels;  // the ladder's labels, alive while the ladder is drawn
  if (!lab.arg("ladder").empty()) {
    Way off = ways[2];
    off.o.minlight = 0;
    off.o.bounce = 0;
    labels.push_back("NO FLOOR, NO BOUNCE");
    ladder.push_back(off);
    std::string list = lab.arg("ladder");
    size_t at = 0;
    while (at <= list.size()) {
      size_t end = list.find(',', at);
      if (end == std::string::npos) end = list.size();
      // A rung: "<minlight>" or "<minlight>@<brightness>".
      Way w2 = ways[2];
      const std::string item = list.substr(at, end - at);
      const size_t sep = item.find('@');
      w2.o.minlight = std::strtod(item.substr(0, sep).c_str(), nullptr);
      if (sep != std::string::npos) w2.o.brightness = std::strtod(item.c_str() + sep + 1, nullptr);
      labels.push_back("MINLIGHT " + ls::fixed(w2.o.minlight, 2) + ", BRIGHTNESS " +
                       ls::fixed(w2.o.brightness, 1));
      ladder.push_back(w2);
      at = end + 1;
    }
    for (size_t i = 0; i < ladder.size(); ++i) ladder[i].label = labels[i].c_str();
  }
  for (const View& v : views) {
    if (!lab.arg("only").empty() && v.id.find(lab.arg("only")) == std::string::npos) continue;
    gr::FrameView fv;
    fv.eye[0] = v.eye[0];
    fv.eye[1] = v.eye[1];
    fv.eye[2] = v.eye[2];
    fv.heading = v.heading;
    fv.pitch = v.pitch;
    dress(fv, sun_el, sun_az, cover, t);
    MapState& m = *maps[v.map];
    std::vector<gl::Picture> pics;
    for (const Way& way : ways) pics.push_back(draw(d, f, m, fv, way.o));
    gl::Picture sheet = gl::create_picture(int(w) * 3 + 40, int(h) + 30, {250, 250, 248});
    for (size_t i = 0; i < pics.size(); ++i) {
      const int ox = 10 + int(i) * (int(w) + 10);
      gl::draw_text(sheet, ox, 6, ways[i].label, {24, 26, 32});
      ls::paste(sheet, pics[i], ox, 20);
    }
    lab.shot({v.id,
              v.title,
              v.caption + " Left: the classic look. Middle: v2 under our sky with our liquids, "
                          "lit by the lightmaps. Right: v2 lit in real time.",
              "The sky faces show our sky and clouds; the liquids are ours; the real-time lights "
              "throw shadows and the lava lights the walls.",
              {{"eye", ls::fixed(v.eye[0], 1) + "," + ls::fixed(v.eye[1], 1) + "," +
                           ls::fixed(v.eye[2], 1)},
               {"heading", ls::fixed(v.heading / kDeg, 0)},
               {"pitch", ls::fixed(v.pitch / kDeg, 0)}}},
             sheet, Det::measured);
    if (!ladder.empty()) {
      std::vector<gl::Picture> rungs;
      for (const Way& way : ladder) rungs.push_back(draw(d, f, m, fv, way.o));
      const int n = int(rungs.size());
      gl::Picture row = gl::create_picture(int(w) * n + 10 * (n + 1), int(h) + 30, {250, 250, 248});
      for (int i = 0; i < n; ++i) {
        const int ox = 10 + i * (int(w) + 10);
        gl::draw_text(row, ox, 6, ladder[size_t(i)].label, {24, 26, 32});
        ls::paste(row, rungs[size_t(i)], ox, 20);
      }
      lab.shot({v.id + "-ladder",
                v.title + ": the floor of light",
                v.caption + " V2 lit in real time: first with neither the floor nor the bounce, "
                            "then at each floor of light, the baked light bouncing at its share.",
                "The dark corners come up while the lit walls and the lamps' pools keep their "
                "light.",
                {}},
               row, Det::measured);
    }
    // The lights a real-time frame drew and shadowed.
    lab.metric(v.id + ".lights", double(m.bsp.frame_lights.size()), "count", Det::measured);
    lab.metric(v.id + ".shadowed", double(m.bsp.shadowed), "count", Det::measured);
  }
  ls::checked(lab, int(d.errors.load()), int(d.pipelines_after_load));
}

const gl::LabSpec kSpec{
    "gpu-bsp",
    gl::LabKind::picture,
    "plugin:brush",
    "plugins/brush/app/bsp_draw.hpp",
    "brush",
    "Does a classic map drawn by the v2 BSP renderer show our sky through its sky faces, our "
    "water, lava and slime in its liquids, and real-time lights with shadows, beside its classic "
    "look?",
    {{"width", "640", "a view's width, px (16:9)"},
     {"pak", "", "a pack file holding maps/*.bsp and gfx/palette.lmp (none: the sample fixture)"},
     {"maps", "start", "the maps in the pack, comma separated, without maps/ and .bsp"},
     {"view", "", "more views: <map>:x,y,z,heading,pitch (m, degrees), separated by ;"},
     {"sun", "30,220", "the sun's elevation and azimuth, degrees"},
     {"clouds", "0.3", "the heaps' cover, 0..1 (0: a clear sky)"},
     {"time", "20", "the views' time, s (the flicker, the liquids, the clouds)"},
     {"strength", "1", "the point lights' strength against the classic's"},
     {"shadowed", "32", "the nearest lights that cast shadows"},
     {"most", "160", "the lights a frame draws at most"},
     {"only", "", "draw only the views whose id holds this"},
     {"brightness", "2.0", "a lightmap's 1 against a white face under the open day"},
     {"minlight", "0.3", "v2's soft floor of light, the lightmaps' units (r_minlight)"},
     {"bounce", "0.35", "the baked light's share added to the real-time lights (r_bounce)"},
     {"murk", "1", "the slime's murk: 0 clearish, 1 murky, 2 opaque"},
     {"scum", "0.35", "the share of the slime a scum covers"},
     {"grime", "0.8", "the slime's grime, 0..1: silt and rot mottled over it"},
     {"debris", "1.5", "bits afloat on the slime a square metre"},
     {"film", "300", "the slime's oily sheen, nm (0 none)"},
     {"bubbles", "1.5", "the slime's bubbles a square metre a second"},
     {"glow", "0.4", "the slime's own green light, a share of the murky slime's"},
     {"ladder", "",
      "rungs to draw each view at too, comma separated: <minlight> or <minlight>@<brightness>"}},
    "--width=160",
    run};
GE_LAB(kSpec);

}  // namespace
