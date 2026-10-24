// Bodies of water and the one query: definitions.
//
// ge:summary Bodies of water and the one sample query: definitions
// ge:keywords water world lake ocean channel volume sample nearest-segment parabolic current hull
// ge:skill water
// ge:test tests/water_world_test.cpp
#include "water/world.hpp"

#include <cmath>
#include <utility>

#include "core/libm/libm.hpp"
#include "core/math.hpp"
#include "core/num.hpp"
#include "core/output.hpp"

namespace ge::water {

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kFar = 1e12;         // m: an ocean's plan
constexpr double kOceanDepth = 1e4;   // m: an ocean with no bed
constexpr double kBreakDepth = 0.78;  // waves held under this share of the depth
// A parabolic section's flow: speed c (1 - r^2) over depth D (1 - r^2) carries c D W 8 / 15, so
// the mid-stream speed that carries Q is 15 Q / (8 W D), 1.25 times the section's mean.
constexpr double kFlowShare = 8.0 / 15.0;

uint32_t seed_for(const World& world, size_t index) {
  return world.seed ^ (static_cast<uint32_t>(index + 1) * 0x9e3779b1u);
}

// The waves laid for a body: its sea grown over `fetch` by the world's mean wind.
Waves waves_for(const World& world, size_t index, double fetch, double depth) {
  const double toward = world.wind.from + kPi;
  Sea sea = grow(world.wind.mean, fetch, toward);
  sea.depth = depth;
  Lay how = world.lay;
  how.seed = seed_for(world, index);
  return lay(sea, how);
}

double laid_hs(const Waves& waves) { return 4 * std::sqrt(waves.m0); }

// The amplitude scale that holds a sea's height under the depth's share.
double shallow_scale(const Waves& waves, double scale, double depth) {
  const double hs = laid_hs(waves) * scale;
  if (hs <= 0) return scale;
  const double most = kBreakDepth * core::num::max(0.0, depth);
  return hs > most ? scale * most / hs : scale;
}

double bed_or(const Bed& bed, double x, double z, double fallback) {
  return bed.height ? bed.height(bed.context, x, z) : fallback;
}

// The surface's waves written into a sample at (x, z) over water `water_depth` m deep.
void put_surface(const World& world, const Body& body, double level, double x, double y, double z,
                 double t, double scale, double water_depth, Sample& out) {
  const Surface s = surface_at(body.waves, x, z, t, scale);
  out.surface = level + s.height;
  const double n = std::sqrt(s.dx * s.dx + 1 + s.dz * s.dz);
  out.nx = -s.dx / n;
  out.ny = 1 / n;
  out.nz = -s.dz / n;
  out.vx = s.u;
  out.vy = s.v;
  out.vz = s.w;
  out.depth = out.surface - y;
  out.density = body.stuff.density;
  out.temperature = body.stuff.temperature;
  out.viscosity = body.stuff.liquid.viscosity;
  out.yield = body.stuff.liquid.yield;
  if (body.drift[0] != 0 || body.drift[1] != 0) {
    out.vx += body.drift[0];
    out.vz += body.drift[1];
  }
  out.breaking = breaking_at(s, water_depth, world.wind.mean);
}

bool sample_lake(const World& world, const Body& body, double x, double y, double z, double t,
                 Sample& out) {
  if (x < body.box[0] || x > body.box[2] || z < body.box[1] || z > body.box[3]) return false;
  if (!inside(body.shore, x, z)) return false;
  const double bed = bed_or(body.bed, x, z, body.level - body.depth);
  const double d = body.level - bed;
  if (d <= 0) return false;
  // A point's sea grows as the root of its own fetch (the growth law's Hs ~ sqrt(F)).
  double scale = 0;
  if (body.fetch_ref > 0) scale = std::sqrt(fetch_at(body.fetch, x, z) / body.fetch_ref);
  scale = shallow_scale(body.waves, scale, d);
  out.bed = bed;
  put_surface(world, body, body.level, x, y, z, t, scale, d, out);
  return true;
}

bool sample_ocean(const World& world, const Body& body, double x, double y, double z, double t,
                  Sample& out) {
  const double bed = bed_or(body.bed, x, z, body.level - kOceanDepth);
  const double d = body.level - bed;
  if (d <= 0) return false;
  out.bed = bed;
  put_surface(world, body, body.level, x, y, z, t, shallow_scale(body.waves, 1, d), d, out);
  return true;
}

bool sample_channel(const World& world, const Body& body, double x, double y, double z, double t,
                    Sample& out) {
  if (x < body.box[0] || x > body.box[2] || z < body.box[1] || z > body.box[3]) return false;
  const std::vector<ChannelPoint>& c = body.channel;
  double best = 1e300, best_u = 0;
  size_t best_i = 0;
  for (size_t i = 0; i + 1 < c.size(); ++i) {
    const double ex = c[i + 1].x - c[i].x, ez = c[i + 1].z - c[i].z;
    const double len2 = ex * ex + ez * ez;
    const double u =
        len2 > 0 ? core::clamp(((x - c[i].x) * ex + (z - c[i].z) * ez) / len2, 0, 1) : 0;
    const double px = c[i].x + ex * u - x, pz = c[i].z + ez * u - z;
    const double d2 = px * px + pz * pz;
    if (d2 < best) {
      best = d2;
      best_i = i;
      best_u = u;
    }
  }
  const ChannelPoint& a = c[best_i];
  const ChannelPoint& b = c[best_i + 1];
  const double half = 0.5 * core::lerp(a.width, b.width, best_u);
  const double off = std::sqrt(best);
  if (half <= 0 || off > half) return false;
  const double r = off / half;
  const double across = 1 - r * r;
  const double level = core::lerp(a.level, b.level, best_u);
  const double mid = core::lerp(a.depth, b.depth, best_u);
  const double bed = bed_or(body.bed, x, z, level - mid * across);
  const double d = level - bed;
  if (d <= 0) return false;
  out.bed = bed;
  put_surface(world, body, level, x, y, z, t, shallow_scale(body.waves, 1, d), d, out);
  // The current: the discharge carried by the parabolic section, fastest mid-stream, down the
  // centreline, and falling with the surface.
  const double ex = b.x - a.x, ez = b.z - a.z;
  const double len = std::sqrt(ex * ex + ez * ez);
  const double carry = kFlowShare * 2 * half * mid;
  if (len > 0 && carry > 0) {
    const double speed = body.discharge / carry * across;
    out.vx += speed * ex / len;
    out.vz += speed * ez / len;
    out.vy += speed * (b.level - a.level) / len;
  }
  return true;
}

// The top of a hull over (x, z): the lowest of its upward-facing planes there.
double hull_top(const Hull& hull, double x, double z) {
  double top = 1e300;
  for (const Plane& p : hull.planes)
    if (p.ny > 0) top = core::num::min(top, (p.d - p.nx * x - p.nz * z) / p.ny);
  return top;
}

bool in_hull(const Hull& hull, double x, double y, double z) {
  for (const Plane& p : hull.planes)
    if (p.nx * x + p.ny * y + p.nz * z > p.d) return false;
  return true;
}

bool sample_volume(const World& world, const Body& body, double x, double y, double z, double t,
                   Sample& out) {
  for (const Hull& hull : body.hulls) {
    const double top = hull_top(hull, x, z);
    if (top >= 1e300) continue;
    // In the hull's column: inside just under its top.
    if (!in_hull(hull, x, top - 1e-6, z)) continue;
    if (y < top && !in_hull(hull, x, y, z)) continue;  // under the hull's floor
    out.surface = top;
    out.nx = out.nz = 0;
    out.ny = 1;
    out.vx = out.vy = out.vz = 0;
    out.depth = top - y;
    out.bed = top;
    for (const Plane& p : hull.planes)
      if (p.ny < 0) out.bed = core::num::max(out.bed == top ? -1e300 : out.bed, p.d / p.ny);
    out.density = body.stuff.density;
    out.temperature = body.stuff.temperature;
    out.viscosity = body.stuff.liquid.viscosity;
    out.yield = body.stuff.liquid.yield;
    // Waves laid on it by hand (lay_waves): the surface theirs, held under the depth's share.
    if (body.waves.count > 0) {
      const double bed = out.bed;
      put_surface(world, body, top, x, y, z, t,
                  shallow_scale(body.waves, 1, core::num::max(0.0, top - bed)), top - bed, out);
      out.bed = bed;
    }
    return true;
  }
  return false;
}

bool sample_flow(const World& world, const Body& body, double x, double y, double z, Sample& out) {
  const Flow& flow = world.flows[static_cast<size_t>(body.flow)];
  const FlowSample s = flow_at(flow, x, z);
  if (!s.inside || s.depth <= 0) return false;
  // The surface's slope across a cell, for the normal.
  const double d = flow.spec.cell;
  const FlowSample e = flow_at(flow, x + d, z), w = flow_at(flow, x - d, z);
  const FlowSample n = flow_at(flow, x, z + d), so = flow_at(flow, x, z - d);
  const double dx = e.depth > 0 && w.depth > 0 ? (e.surface - w.surface) / (2 * d) : 0;
  const double dz = n.depth > 0 && so.depth > 0 ? (n.surface - so.surface) / (2 * d) : 0;
  const double len = std::sqrt(dx * dx + 1 + dz * dz);
  out.surface = s.surface;
  out.nx = -dx / len;
  out.ny = 1 / len;
  out.nz = -dz / len;
  out.vx = s.vx;
  out.vy = 0;
  out.vz = s.vz;
  out.depth = s.surface - y;
  out.bed = s.bed;
  out.density = body.stuff.density;
  out.temperature = body.stuff.temperature;
  out.viscosity = body.stuff.liquid.viscosity;
  out.yield = body.stuff.liquid.yield;
  return true;
}

void box_of(Body& body, double x0, double z0, double x1, double z1) {
  body.box[0] = x0;
  body.box[1] = z0;
  body.box[2] = x1;
  body.box[3] = z1;
}

}  // namespace

namespace {

// A viscous liquid's waves cut by its viscosity, and its drift downwind over `depth` m (a thin
// liquid's untouched: no drift is added to its samples, so water keeps its bits).
void thicken(const World& world, Body& body, double depth) {
  const Liquid& liquid = body.stuff.liquid;
  if (liquid.viscosity <= 0) return;
  damp_waves(body.waves, liquid, body.stuff.density, world.wind.mean);
  const double toward = world.wind.from + kPi;
  const double d = drift(liquid, world.wind.mean, depth);
  body.drift[0] = d * core::libm::sin(toward);
  body.drift[1] = d * core::libm::cos(toward);
}

// The body's stuff and the optics its look gives.
void dress(Body& body, Stuff stuff) {
  const Constituents* named = stuff.water ? &*stuff.water : find_look(stuff.look);
  if (!named) {
    core::output::warn("water: the look \"" + stuff.look +
                       "\" is not a named look, so the body is drawn as nordic-clear. next: name "
                       "one of water::looks() or set Stuff::water (skill: water)");
    named = find_look("nordic-clear");
  }
  body.optics = optics(*named);
  body.stuff = std::move(stuff);
}

}  // namespace

int add_lake(World& world, std::span<const Point> shore, double level, Stuff stuff, Bed bed,
             double depth) {
  Body body;
  body.kind = Kind::lake;
  dress(body, std::move(stuff));
  body.level = level;
  body.depth = depth;
  body.bed = bed;
  body.shore.assign(shore.begin(), shore.end());
  if (!shore.empty()) {
    double x0 = shore[0].x, x1 = x0, z0 = shore[0].z, z1 = z0;
    for (const Point& p : shore) {
      x0 = core::num::min(x0, p.x);
      x1 = core::num::max(x1, p.x);
      z0 = core::num::min(z0, p.z);
      z1 = core::num::max(z1, p.z);
    }
    box_of(body, x0, z0, x1, z1);
  }
  body.fetch = bake_fetch(shore, world.wind.from + kPi);
  body.fetch_ref = body.fetch.longest;
  body.waves = waves_for(world, world.bodies.size(), body.fetch_ref, depth);
  thicken(world, body, depth);
  world.bodies.push_back(std::move(body));
  return static_cast<int>(world.bodies.size()) - 1;
}

int add_ocean(World& world, double level, Stuff stuff, Bed bed, double fetch) {
  Body body;
  body.kind = Kind::ocean;
  dress(body, std::move(stuff));
  body.level = level;
  body.depth = kOceanDepth;
  body.bed = bed;
  box_of(body, -kFar, -kFar, kFar, kFar);
  body.fetch_ref = fetch;
  body.waves = waves_for(world, world.bodies.size(), fetch, kOceanDepth);
  thicken(world, body, kOceanDepth);
  world.bodies.push_back(std::move(body));
  return static_cast<int>(world.bodies.size()) - 1;
}

int add_river(World& world, std::span<const ChannelPoint> centreline, double discharge, Stuff stuff,
              Bed bed) {
  Body body;
  body.kind = Kind::channel;
  dress(body, std::move(stuff));
  body.bed = bed;
  body.discharge = discharge;
  body.channel.assign(centreline.begin(), centreline.end());
  double widest = 0, deepest = 0;
  if (!centreline.empty()) {
    double x0 = 1e300, z0 = 1e300, x1 = -1e300, z1 = -1e300;
    for (const ChannelPoint& p : centreline) {
      const double h = 0.5 * p.width;
      x0 = core::num::min(x0, p.x - h);
      x1 = core::num::max(x1, p.x + h);
      z0 = core::num::min(z0, p.z - h);
      z1 = core::num::max(z1, p.z + h);
      widest = core::num::max(widest, p.width);
      deepest = core::num::max(deepest, p.depth);
    }
    box_of(body, x0, z0, x1, z1);
  }
  if (centreline.size() < 2) box_of(body, 1, 1, 0, 0);  // nothing to sample
  // A ripple of its own: the wind's sea over the channel's width.
  body.fetch_ref = widest;
  body.depth = deepest;
  body.waves = waves_for(world, world.bodies.size(), widest, deepest);
  thicken(world, body, deepest);
  world.bodies.push_back(std::move(body));
  return static_cast<int>(world.bodies.size()) - 1;
}

int add_volume(World& world, std::span<const Hull> hulls, Stuff stuff) {
  Body body;
  body.kind = Kind::volume;
  dress(body, std::move(stuff));
  body.hulls.assign(hulls.begin(), hulls.end());
  world.bodies.push_back(std::move(body));
  return static_cast<int>(world.bodies.size()) - 1;
}

int add_flow(World& world, Flow flow, Stuff stuff) {
  Body body;
  body.kind = Kind::flow;
  dress(body, std::move(stuff));
  box_of(body, flow.spec.x0, flow.spec.z0, flow.spec.x0 + flow.spec.nx * flow.spec.cell,
         flow.spec.z0 + flow.spec.nz * flow.spec.cell);
  body.flow = static_cast<int>(world.flows.size());
  world.flows.push_back(std::move(flow));
  world.bodies.push_back(std::move(body));
  return static_cast<int>(world.bodies.size()) - 1;
}

void lay_waves(World& world, int index, double fetch) {
  Body& body = world.bodies[static_cast<size_t>(index)];
  double depth = body.depth;
  if (body.kind == Kind::volume) {
    // The deepest of its hulls: its top less its floor, under the middle of its plan.
    depth = 0;
    for (const Hull& hull : body.hulls) {
      double top = 1e300, floor = -1e300;
      for (const Plane& p : hull.planes) {
        if (p.ny > 0 && p.nx == 0 && p.nz == 0) top = core::num::min(top, p.d / p.ny);
        if (p.ny < 0 && p.nx == 0 && p.nz == 0) floor = core::num::max(floor, p.d / p.ny);
      }
      if (top < 1e300 && floor > -1e300) depth = core::num::max(depth, top - floor);
    }
  }
  body.fetch_ref = fetch;
  body.waves = waves_for(world, static_cast<size_t>(index), fetch, core::num::max(depth, 0.01));
  if (body.stuff.liquid.viscosity > 0) {
    thicken(world, body, depth);
  } else {
    const double toward = world.wind.from + kPi;
    const double d = kDriftShare * world.wind.mean;
    body.drift[0] = d * core::libm::sin(toward);
    body.drift[1] = d * core::libm::cos(toward);
  }
}

void step_flows(World& world, double dt) {
  for (Flow& flow : world.flows) step_flow(flow, dt);
}

Sample sample(const World& world, double x, double y, double z, double t) {
  // The body the point is under the surface of wins; else the nearest surface below the point.
  Sample best;
  best.depth = -1e300;
  for (size_t i = 0; i < world.bodies.size(); ++i) {
    const Body& body = world.bodies[i];
    Sample s;
    bool hit = false;
    switch (body.kind) {
      case Kind::lake:
        hit = sample_lake(world, body, x, y, z, t, s);
        break;
      case Kind::ocean:
        hit = sample_ocean(world, body, x, y, z, t, s);
        break;
      case Kind::channel:
        hit = sample_channel(world, body, x, y, z, t, s);
        break;
      case Kind::volume:
        hit = sample_volume(world, body, x, y, z, t, s);
        break;
      case Kind::flow:
        hit = sample_flow(world, body, x, y, z, s);
        break;
    }
    if (!hit) continue;
    s.body = static_cast<int>(i);
    if (s.depth >= 0) return s;
    if (s.depth > best.depth) best = s;
  }
  if (best.body < 0) return Sample{};
  return best;
}

void sample(const World& world, std::span<const double> xyz, double t, std::span<Sample> out) {
  const size_t n = xyz.size() / 3 < out.size() ? xyz.size() / 3 : out.size();
  for (size_t i = 0; i < n; ++i)
    out[i] = sample(world, xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2], t);
}

}  // namespace ge::water
