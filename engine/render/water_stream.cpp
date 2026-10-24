// Moving water drawn: the channels' ribbons, the flow regions' grids, the sheets' curtains, the
// mist and the spray, and the stream draw's uploads.
//
// ge:summary Moving water's meshes, spray births and uploads
// ge:keywords render water river channel flow waterfall curtain foam spray mist mesh upload
// ge:skill render water
// ge:test tests/render_water_test.cpp
#include "render/water_stream.hpp"

#include <algorithm>
#include <cmath>

#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "render/water.hpp"

namespace ge::render {

namespace {

namespace lm = core::libm;
constexpr double kPi = 3.14159265358979323846;
constexpr int kAcross = 9;      // a ribbon's vertices bank to bank
constexpr int kCurtain = 12;    // a curtain's segments from the lip to the landing
constexpr double kWet = 0.005;  // m: a flow cell shallower than this is not drawn

StreamVertex vertex(double x, double y, double z, double foam, double vx, double vz, double along,
                    int tag) {
  return StreamVertex{{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)},
                      static_cast<float>(foam),
                      {static_cast<float>(vx), static_cast<float>(vz)},
                      static_cast<float>(along),
                      static_cast<float>(tag)};
}

void quad(StreamMesh& m, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
}

// A channel's ribbon: rows every `step` m down its centreline, kAcross vertices bank to bank,
// each where `sample` puts the surface and with its current.
void channel(const water::World& world, int index, double t, double step, StreamMesh& m) {
  const water::Body& body = world.bodies[static_cast<size_t>(index)];
  const std::vector<water::ChannelPoint>& c = body.channel;
  if (c.size() < 2) return;
  uint32_t prev = 0;
  bool first = true;
  for (size_t i = 0; i + 1 < c.size(); ++i) {
    const double ex = c[i + 1].x - c[i].x, ez = c[i + 1].z - c[i].z;
    const double len = std::sqrt(ex * ex + ez * ez);
    if (len <= 0) continue;
    const int rows = std::max(1, static_cast<int>(std::ceil(len / step)));
    // The across direction, the mean of this segment's and its neighbours' so a bend has no
    // gap or fold.
    for (int r = first ? 0 : 1; r <= rows; ++r) {
      const double u = static_cast<double>(r) / rows;
      double tx = ex / len, tz = ez / len;
      const size_t j = r == 0 && i > 0 ? i - 1 : (r == rows && i + 2 < c.size() ? i + 1 : i);
      if (j != i) {
        const double fx = c[j + 1].x - c[j].x, fz = c[j + 1].z - c[j].z;
        const double fl = std::sqrt(fx * fx + fz * fz);
        if (fl > 0) {
          tx += fx / fl;
          tz += fz / fl;
          const double tl = std::sqrt(tx * tx + tz * tz);
          tx /= tl;
          tz /= tl;
        }
      }
      const double cx = c[i].x + ex * u, cz = c[i].z + ez * u;
      const double half = 0.5 * core::lerp(c[i].width, c[i + 1].width, u) * 0.98;
      const double level = core::lerp(c[i].level, c[i + 1].level, u);
      const uint32_t row = static_cast<uint32_t>(m.vertices.size());
      for (int a = 0; a < kAcross; ++a) {
        const double o = half * (2.0 * a / (kAcross - 1) - 1);
        const double x = cx + tz * o, z = cz - tx * o;
        const water::Sample s = water::sample(world, x, level, z, t);
        const bool ours = s.body == index;
        const double y = ours ? s.surface : level;
        const double vx = ours ? s.vx : 0, vz = ours ? s.vz : 0;
        const double depth = ours ? s.surface - s.bed : 0;
        const double speed = std::sqrt(vx * vx + vz * vz);
        const double foam = 0.55 * core::smoothstep(2.5, 5.0, speed) *
                            (1 - core::smoothstep(0.2, 1.5, depth) * 0.5);
        m.vertices.push_back(vertex(x, y, z, foam, vx, vz, depth, index * 2));
      }
      if (!first || r > 0) {
        for (int a = 0; a + 1 < kAcross; ++a) quad(m, prev + a, prev + a + 1, row + a, row + a + 1);
      }
      prev = row;
    }
    first = false;
  }
}

// A flow region's wet cells, a vertex at each cell's centre and two triangles between four wet
// ones; foam where the water is supercritical and round where a sheet lands.
void flow_grid(const water::World& world, int index, StreamMesh& m) {
  const water::Body& body = world.bodies[static_cast<size_t>(index)];
  const water::Flow& f = world.flows[static_cast<size_t>(body.flow)];
  const water::FlowGrid& g = f.grids[f.tier];
  const double area = g.cell * g.cell;
  std::vector<uint32_t> at(static_cast<size_t>(g.nx) * g.nz, UINT32_MAX);
  for (int k = 0; k < g.nz; ++k) {
    for (int i = 0; i < g.nx; ++i) {
      const size_t c = static_cast<size_t>(k) * g.nx + i;
      const double h = static_cast<double>(g.vol[c]) * water::kQuantum / area;
      if (h < kWet) continue;
      const double x = f.spec.x0 + (i + 0.5) * g.cell, z = f.spec.z0 + (k + 0.5) * g.cell;
      const double vx = g.qx[c] / h, vz = g.qz[c] / h;
      const double speed = std::sqrt(vx * vx + vz * vz);
      double foam = 0.8 * core::smoothstep(0.9, 1.6, speed / std::sqrt(f.spec.gravity * h));
      for (const water::Fall& fall : f.falls) {
        const double d = core::hypot(x - fall.land_x, z - fall.land_z);
        foam += 1 - core::smoothstep(0.5 * g.cell, 4 * g.cell, d);
      }
      at[c] = static_cast<uint32_t>(m.vertices.size());
      m.vertices.push_back(vertex(x, g.bed[c] + h, z, std::min(foam, 1.0), vx, vz, h, index * 2));
    }
  }
  for (int k = 0; k + 1 < g.nz; ++k) {
    for (int i = 0; i + 1 < g.nx; ++i) {
      const size_t c = static_cast<size_t>(k) * g.nx + i;
      const uint32_t a = at[c], b = at[c + 1], d = at[c + g.nx], e = at[c + g.nx + 1];
      if (a == UINT32_MAX || b == UINT32_MAX || d == UINT32_MAX || e == UINT32_MAX) continue;
      quad(m, a, b, d, e);
    }
  }
  // The curtains: each sheet's path from its lip to its landing, as wide as the lip.
  for (const water::Fall& fall : f.falls) {
    const double v = std::sqrt(fall.vx * fall.vx + fall.vz * fall.vz);
    if (v <= 0 || fall.time <= 0) continue;
    const double ax = -fall.vz / v, az = fall.vx / v;
    const double half = 0.55 * g.cell;
    const double lip = ax * fall.x + az * fall.z;  // the across coordinate, world-continuous
    const uint32_t base = static_cast<uint32_t>(m.vertices.size());
    double along = 0, px = fall.x, py = fall.y, pz = fall.z;
    for (int s = 0; s <= kCurtain; ++s) {
      const double tau = fall.time * s / kCurtain;
      const double x = fall.x + fall.vx * tau, z = fall.z + fall.vz * tau;
      const double y = fall.y - 0.5 * f.spec.gravity * tau * tau;
      along += core::hypot3(x - px, y - py, z - pz);
      px = x;
      py = y;
      pz = z;
      const double speed = std::sqrt(v * v + f.spec.gravity * tau * f.spec.gravity * tau);
      for (const double side : {-1.0, 1.0}) {
        m.vertices.push_back(vertex(x + ax * half * side, y, z + az * half * side,
                                    lip + half * side, speed, 0, along, index * 2 + 1));
      }
      if (s > 0) {
        const uint32_t r = base + 2 * static_cast<uint32_t>(s);
        quad(m, r - 2, r - 1, r, r + 1);
      }
    }
  }
}

Particle drop(double x, double y, double z, double vx, double vy, double vz, double life,
              double size, double drag, double fall, double opacity) {
  Particle p{};
  p.p[0] = static_cast<float>(x);
  p.p[1] = static_cast<float>(y);
  p.p[2] = static_cast<float>(z);
  p.v[0] = static_cast<float>(vx);
  p.v[1] = static_cast<float>(vy);
  p.v[2] = static_cast<float>(vz);
  p.life = static_cast<float>(life);
  p.size = static_cast<float>(size);
  p.drag = static_cast<float>(drag);
  p.fall = static_cast<float>(fall);
  p.streak = 0.04f;
  p.albedo = 0.95f;
  p.opacity = static_cast<float>(opacity);
  return p;
}

}  // namespace

void build_water_streams(const water::World& world, double t, StreamMesh& out, double step) {
  out.vertices.clear();
  out.indices.clear();
  const int n = std::min(static_cast<int>(world.bodies.size()), kStreamBodies);
  for (int i = 0; i < n; ++i) {
    const water::Body& body = world.bodies[static_cast<size_t>(i)];
    if (body.kind == water::Kind::channel) channel(world, i, t, step, out);
    if (body.kind == water::Kind::flow) flow_grid(world, i, out);
  }
}

void waterfall_spray(const water::Flow& flow, double dt, core::Rng& rng,
                     std::vector<Particle>& out) {
  const double cell = flow.grids[flow.tier].cell;
  for (const water::Fall& fall : flow.falls) {
    // About 60 drops for every cubic metre landing, at least the chance of one.
    const double want = 60 * fall.discharge * dt;
    int n = static_cast<int>(want);
    if (core::chance(rng, want - n)) ++n;
    const double impact = std::sqrt(2 * flow.spec.gravity * std::max(fall.y - fall.land_y, 0.0));
    for (int i = 0; i < n; ++i) {
      const double a = core::range(rng, 0, 2 * kPi);
      const double out_speed = core::range(rng, 0.1, 0.35) * impact;
      const bool mist = core::chance(rng, 0.6);
      const double x = fall.land_x + core::range(rng, -0.5, 0.5) * cell;
      const double z = fall.land_z + core::range(rng, -0.5, 0.5) * cell;
      out.push_back(drop(x, fall.land_y + 0.1, z, out_speed * lm::cos(a),
                         core::range(rng, 0.15, 0.45) * impact, out_speed * lm::sin(a),
                         core::range(rng, mist ? 2.0 : 0.8, mist ? 4.0 : 1.6), mist ? 0.5 : 0.08,
                         mist ? 1.5 : 0.3, mist ? 0.08 : 1.0, mist ? 0.18 : 0.7));
    }
  }
}

void splash_spray(const water::Splash& splash, core::Rng& rng, std::vector<Particle>& out) {
  if (!splash.entering || splash.speed <= 0) return;
  const int n = static_cast<int>(core::clamp(splash.speed * splash.size * 40, 4, 80));
  for (int i = 0; i < n; ++i) {
    const double a = core::range(rng, 0, 2 * kPi);
    const double up = core::range(rng, 0.3, 0.8) * splash.speed;
    const double side = core::range(rng, 0.1, 0.4) * splash.speed;
    const double r = core::range(rng, 0.6, 1.0) * splash.size;
    out.push_back(drop(splash.x + r * lm::cos(a), splash.y, splash.z + r * lm::sin(a),
                       side * lm::cos(a), up, side * lm::sin(a), core::range(rng, 0.7, 1.4), 0.06,
                       0.3, 1.0, 0.8));
  }
}

void prepare_water_streams(gpu::Device& device, Water& water, const StreamMesh& mesh,
                           const water::World& world) {
  Water::Streams& s = water.streams;
  s.count = static_cast<uint32_t>(mesh.indices.size());
  if (s.count == 0) return;
  const uint64_t vbytes = mesh.vertices.size() * sizeof(StreamVertex);
  const uint64_t ibytes = mesh.indices.size() * sizeof(uint32_t);
  bool regroup = false;
  if (vbytes > s.vertex_bytes) {
    s.vertex_bytes = std::max<uint64_t>(vbytes * 3 / 2, 4096);
    s.vertices =
        make_buffer(device, "water.stream.vertices", s.vertex_bytes, wgpu::BufferUsage::Storage);
    regroup = true;
  }
  if (ibytes > s.index_bytes) {
    s.index_bytes = std::max<uint64_t>(ibytes * 3 / 2, 4096);
    s.indices =
        make_buffer(device, "water.stream.indices", s.index_bytes, wgpu::BufferUsage::Index);
  }
  device.queue.WriteBuffer(s.vertices, 0, mesh.vertices.data(), vbytes);
  device.queue.WriteBuffer(s.indices, 0, mesh.indices.data(), ibytes);
  // Each body's optics, four rows a body (water_stream.wgsl's StreamBody).
  std::vector<float> optics(size_t{kStreamBodies} * 16, 0.0f);
  const int n = std::min(static_cast<int>(world.bodies.size()), kStreamBodies);
  for (int i = 0; i < n; ++i) {
    const water::Optics& o = world.bodies[static_cast<size_t>(i)].optics;
    const water::Rgb rows[4] = {o.c, o.kd, o.deep, o.emission};
    for (int r = 0; r < 4; ++r) {
      float* at = &optics[static_cast<size_t>(i) * 16 + static_cast<size_t>(r) * 4];
      at[0] = static_cast<float>(rows[r].r);
      at[1] = static_cast<float>(rows[r].g);
      at[2] = static_cast<float>(rows[r].b);
    }
  }
  device.queue.WriteBuffer(s.optics, 0, optics.data(), optics.size() * sizeof(float));
  if ((regroup || !s.group) && water.scene_colour_view) {
    s.group = make_group(device, "water.stream", s.layout,
                         {s.optics, s.vertices, water.scene_colour_view, water.scene_depth_view});
  }
}

}  // namespace ge::render
