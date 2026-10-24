// Flowing water: definitions.
//
// ge:summary Flowing water: the finite-volume step, sources, drains, waterfalls and the tiers
// ge:keywords water flow hll riemann hydrostatic-reconstruction substep cfl quanta lip sheet
// ge:keywords coarsen refine level
// ge:skill water
// ge:test tests/water_flow_test.cpp
#include "water/flow.hpp"

#include <cmath>
#include <cstddef>
#include <utility>

#include "core/libm/libm.hpp"
#include "core/parallel.hpp"

namespace ge::water {

namespace {

// m/s: the fastest water in a far region under which it has settled and may sleep.
constexpr double kSettled = 1e-3;

constexpr int kMaxSubsteps = 64;
constexpr double kDry = 1e-6;  // m: shallower than this a cell has no velocity

double depth_of(const FlowGrid& g, int c) {
  return static_cast<double>(g.vol[c]) * kQuantum / (g.cell * g.cell);
}

double surface_of(const FlowGrid& g, int c) { return g.bed[c] + depth_of(g, c); }

int cell_at(const Flow& f, const FlowGrid& g, double x, double z) {
  const double u = (x - f.spec.x0) / g.cell, w = (z - f.spec.z0) / g.cell;
  if (u < 0 || w < 0) return -1;
  const int i = static_cast<int>(u), k = static_cast<int>(w);
  if (i >= g.nx || k >= g.nz) return -1;
  return k * g.nx + i;
}

// One side of a face: depth, bed, the velocity across the face and along it.
struct Side {
  double h = 0, b = 0, un = 0, ut = 0;
};

// The flux across a face from L to R (normal pointing L to R): mass, normal and tangential
// momentum, by HLL over the hydrostatic reconstruction (so still water over any bed stays
// still); `pl` and `pr` are the pressure corrections each side takes for its bed. The face
// stands at the higher bed or at its `crest`, whichever is higher.
struct Flux {
  double mass = 0, mn = 0, mt = 0, pl = 0, pr = 0;
};

Flux hll(const Side& l, const Side& r, double g, double crest) {
  Flux out;
  double zf = l.b > r.b ? l.b : r.b;
  zf = crest > zf ? crest : zf;
  const double hl = l.h + l.b - zf > 0 ? l.h + l.b - zf : 0;
  const double hr = r.h + r.b - zf > 0 ? r.h + r.b - zf : 0;
  out.pl = 0.5 * g * (l.h * l.h - hl * hl);
  out.pr = 0.5 * g * (r.h * r.h - hr * hr);
  if (hl <= 0 && hr <= 0) return out;
  const double cl = std::sqrt(g * hl), cr = std::sqrt(g * hr);
  const double ul = hl > 0 ? l.un : 0, ur = hr > 0 ? r.un : 0;
  double sl = ul - cl < ur - cr ? ul - cl : ur - cr;
  double sr = ul + cl > ur + cr ? ul + cl : ur + cr;
  if (hl <= 0) sl = ur - 2 * cr;  // a wet front into the dry: its speed
  if (hr <= 0) sr = ul + 2 * cl;
  const double fl[3] = {hl * ul, hl * ul * ul + 0.5 * g * hl * hl, hl * ul * l.ut};
  const double fr[3] = {hr * ur, hr * ur * ur + 0.5 * g * hr * hr, hr * ur * r.ut};
  const double ql[3] = {hl, hl * ul, hl * l.ut}, qr[3] = {hr, hr * ur, hr * r.ut};
  double f[3];
  for (int j = 0; j < 3; ++j) {
    if (sl >= 0) {
      f[j] = fl[j];
    } else if (sr <= 0) {
      f[j] = fr[j];
    } else {
      f[j] = (sr * fl[j] - sl * fr[j] + sl * sr * (qr[j] - ql[j])) / (sr - sl);
    }
  }
  out.mass = f[0];
  out.mn = f[1];
  out.mt = f[2];
  return out;
}

double vel(const FlowGrid& g, const std::vector<double>& q, int c) {
  const double h = depth_of(g, c);
  return h > kDry ? q[c] / h : 0;
}

// The faces of the grid: x faces (nx + 1) a row, z faces nx a row for nz + 1 rows.
struct Faces {
  std::vector<Flux> x, z;
  std::vector<double> scale;    // a cell's share of its outflow it can give
  std::vector<int64_t> mx, mz;  // quanta across each face, + toward +x / +z
  std::vector<uint8_t> lx, lz;  // the face is a lip (its quanta go into the air)
};

bool is_lip(const Flow& f, const FlowGrid& g, int from, int to) {
  return g.bed[to] < g.bed[from] - f.spec.fall_drop &&
         surface_of(g, to) < surface_of(g, from) - f.spec.fall_drop;
}

// The fluxes across row k's x faces and the z faces under row k (between k - 1 and k).
void fluxes(const Flow& f, const FlowGrid& g, Faces& fc, int k0, int k1) {
  const double gr = f.spec.gravity;
  const int nx = g.nx, nz = g.nz;
  for (int k = k0; k < k1; ++k) {
    for (int i = 0; i <= nx; ++i) {
      const int fi = k * (nx + 1) + i;
      Side l, r;
      fc.lx[fi] = 0;
      if (i > 0) {
        const int c = k * nx + i - 1;
        l = {depth_of(g, c), g.bed[c], vel(g, g.qx, c), vel(g, g.qz, c)};
      }
      if (i < nx) {
        const int c = k * nx + i;
        r = {depth_of(g, c), g.bed[c], vel(g, g.qx, c), vel(g, g.qz, c)};
      }
      if (i == 0) l = (f.spec.open & kWest) ? Side{0, r.b, 0, 0} : Side{r.h, r.b, -r.un, r.ut};
      if (i == nx) r = (f.spec.open & kEast) ? Side{0, l.b, 0, 0} : Side{l.h, l.b, -l.un, l.ut};
      Flux fl = hll(l, r, gr, g.sx.empty() ? -INFINITY : g.sx[fi]);
      if ((i == 0 && !(f.spec.open & kWest)) || (i == nx && !(f.spec.open & kEast))) fl.mass = 0;
      fc.x[fi] = fl;
      if (i > 0 && i < nx) {
        const int a = k * nx + i - 1, b = a + 1;
        fc.lx[fi] = (fl.mass > 0 && is_lip(f, g, a, b)) || (fl.mass < 0 && is_lip(f, g, b, a));
      }
    }
    // The z faces below row k, and the top edge's with the last row.
    for (int kk = k; kk <= (k == nz - 1 ? nz : k); ++kk) {
      for (int i = 0; i < nx; ++i) {
        const int fi = kk * nx + i;
        Side l, r;
        fc.lz[fi] = 0;
        if (kk > 0) {
          const int c = (kk - 1) * nx + i;
          l = {depth_of(g, c), g.bed[c], vel(g, g.qz, c), vel(g, g.qx, c)};
        }
        if (kk < nz) {
          const int c = kk * nx + i;
          r = {depth_of(g, c), g.bed[c], vel(g, g.qz, c), vel(g, g.qx, c)};
        }
        if (kk == 0) l = (f.spec.open & kSouth) ? Side{0, r.b, 0, 0} : Side{r.h, r.b, -r.un, r.ut};
        if (kk == nz) r = (f.spec.open & kNorth) ? Side{0, l.b, 0, 0} : Side{l.h, l.b, -l.un, l.ut};
        Flux fl = hll(l, r, gr, g.sz.empty() ? -INFINITY : g.sz[fi]);
        if ((kk == 0 && !(f.spec.open & kSouth)) || (kk == nz && !(f.spec.open & kNorth))) {
          fl.mass = 0;
        }
        fc.z[fi] = fl;
        if (kk > 0 && kk < nz) {
          const int a = (kk - 1) * nx + i, b = kk * nx + i;
          fc.lz[fi] = (fl.mass > 0 && is_lip(f, g, a, b)) || (fl.mass < 0 && is_lip(f, g, b, a));
        }
      }
    }
  }
}

// Each cell's share of its outflow it can give (1 unless it would go below empty).
void shares(const FlowGrid& g, Faces& fc, double dts, int k0, int k1) {
  const int nx = g.nx;
  const double w = g.cell * dts / kQuantum;
  for (int k = k0; k < k1; ++k) {
    for (int i = 0; i < nx; ++i) {
      const int c = k * nx + i;
      double out = 0;
      const double e = fc.x[k * (nx + 1) + i + 1].mass, wv = fc.x[k * (nx + 1) + i].mass;
      const double n = fc.z[(k + 1) * nx + i].mass, s = fc.z[k * nx + i].mass;
      out += (e > 0 ? e : 0) + (wv < 0 ? -wv : 0) + (n > 0 ? n : 0) + (s < 0 ? -s : 0);
      out *= w;
      const double have = static_cast<double>(g.vol[c]);
      fc.scale[c] = out > have ? have / out : 1;
    }
  }
}

// The quanta across each face (the donor's share), row by row.
void quanta(const FlowGrid& g, Faces& fc, double dts, int k0, int k1) {
  const int nx = g.nx, nz = g.nz;
  const double w = g.cell * dts / kQuantum;
  for (int k = k0; k < k1; ++k) {
    for (int i = 0; i <= nx; ++i) {
      const int fi = k * (nx + 1) + i;
      const double m = fc.x[fi].mass * w;
      const int donor = m > 0 ? (i > 0 ? k * nx + i - 1 : -1) : (i < nx ? k * nx + i : -1);
      const double s = donor >= 0 ? fc.scale[donor] : 1;
      fc.mx[fi] = static_cast<int64_t>(m * s);
    }
    for (int kk = k; kk <= (k == nz - 1 ? nz : k); ++kk) {
      for (int i = 0; i < nx; ++i) {
        const int fi = kk * nx + i;
        const double m = fc.z[fi].mass * w;
        const int donor = m > 0 ? (kk > 0 ? (kk - 1) * nx + i : -1) : (kk < nz ? kk * nx + i : -1);
        const double s = donor >= 0 ? fc.scale[donor] : 1;
        fc.mz[fi] = static_cast<int64_t>(m * s);
      }
    }
  }
}

// Each cell's water and momentum from its faces.
void update(const Flow& f, FlowGrid& g, const Faces& fc, double dts, int k0, int k1) {
  const int nx = g.nx;
  const double r = dts / g.cell, gr = f.spec.gravity, n2 = f.spec.manning * f.spec.manning;
  for (int k = k0; k < k1; ++k) {
    for (int i = 0; i < nx; ++i) {
      const int c = k * nx + i;
      const int w = k * (nx + 1) + i, e = w + 1, s = k * nx + i, n = (k + 1) * nx + i;
      int64_t v = g.vol[c];
      v += (fc.lx[w] && fc.mx[w] > 0 ? 0 : fc.mx[w]) - (fc.lx[e] && fc.mx[e] < 0 ? 0 : fc.mx[e]);
      v += (fc.lz[s] && fc.mz[s] > 0 ? 0 : fc.mz[s]) - (fc.lz[n] && fc.mz[n] < 0 ? 0 : fc.mz[n]);
      g.vol[c] = v;
      // Momentum: what crosses each face, and the bed's push.
      double qx = g.qx[c] - r * ((fc.x[e].mn + fc.x[e].pl) - (fc.x[w].mn + fc.x[w].pr)) -
                  r * (fc.z[n].mt - fc.z[s].mt);
      double qz = g.qz[c] - r * ((fc.z[n].mn + fc.z[n].pl) - (fc.z[s].mn + fc.z[s].pr)) -
                  r * (fc.x[e].mt - fc.x[w].mt);
      const double h = depth_of(g, c);
      if (h <= kDry) {
        qx = qz = 0;
      } else {
        const double speed = std::sqrt(qx * qx + qz * qz) / h;
        const double fr = 1 + dts * gr * n2 * speed / (h * core::libm::cbrt(h)) +
                          dts * f.spec.loss * speed / (2 * g.cell);
        qx /= fr;
        qz /= fr;
      }
      g.qx[c] = qx;
      g.qz[c] = qz;
    }
  }
}

// Where a sheet leaving cell `c` toward (di, dk) at `v` m/s lands: the cell (or -1, off the
// region) and its time in the air.
void land(const Flow& f, const FlowGrid& g, int c, int di, int dk, double v, int& to,
          double& time) {
  const int i0 = c % g.nx, k0 = c / g.nx;
  const double y0 = surface_of(g, c), gr = f.spec.gravity;
  to = -1;
  time = 0;
  for (int m = 1; m < g.nx + g.nz; ++m) {
    const int i = i0 + di * m, k = k0 + dk * m;
    if (i < 0 || k < 0 || i >= g.nx || k >= g.nz) return;
    const int n = k * g.nx + i;
    const double drop = y0 - surface_of(g, n);
    const double t = drop > 0 ? std::sqrt(2 * drop / gr) : 0;
    if (v * t <= (m + 0.5) * g.cell) {
      to = n;
      time = t;
      return;
    }
  }
}

struct Over {
  int cell = 0, di = 0, dk = 0;
  int64_t q = 0;
};

void lips(Flow& f, FlowGrid& g, const Faces& fc, std::vector<Over>& over) {
  const int nx = g.nx, nz = g.nz;
  const auto add = [&](int cell, int di, int dk, int64_t q) {
    for (Over& o : over) {
      if (o.cell == cell && o.di == di && o.dk == dk) {
        o.q += q;
        return;
      }
    }
    over.push_back({cell, di, dk, q});
  };
  for (int k = 0; k < nz; ++k) {
    for (int i = 0; i <= nx; ++i) {
      const int fi = k * (nx + 1) + i;
      const int64_t q = fc.mx[fi];
      if (q == 0) continue;
      if (i == 0 || i == nx) {
        f.drained += q > 0 ? q : -q;
      } else if (fc.lx[fi]) {
        if (q > 0)
          add(k * nx + i - 1, 1, 0, q);
        else
          add(k * nx + i, -1, 0, -q);
      }
    }
  }
  for (int k = 0; k <= nz; ++k) {
    for (int i = 0; i < nx; ++i) {
      const int fi = k * nx + i;
      const int64_t q = fc.mz[fi];
      if (q == 0) continue;
      if (k == 0 || k == nz) {
        f.drained += q > 0 ? q : -q;
      } else if (fc.lz[fi]) {
        if (q > 0)
          add((k - 1) * nx + i, 0, 1, q);
        else
          add(k * nx + i, 0, -1, -q);
      }
    }
  }
}

void substeps(Flow& f, double dt) {
  FlowGrid& g = f.grids[f.tier];
  const int cells = g.nx * g.nz;
  const double gr = f.spec.gravity;
  double fastest = 0;
  for (int c = 0; c < cells; ++c) {
    const double h = depth_of(g, c);
    if (h <= kDry) continue;
    const double ux = std::fabs(g.qx[c] / h), uz = std::fabs(g.qz[c] / h);
    const double w = (ux > uz ? ux : uz) + std::sqrt(gr * h);
    fastest = fastest > w ? fastest : w;
  }
  int n = 1;
  if (fastest > 0) {
    n = static_cast<int>(std::ceil(dt * fastest / (0.4 * g.cell)));
    n = n < 1 ? 1 : n > kMaxSubsteps ? kMaxSubsteps : n;
  }
  const double dts = dt / n;
  // The faces and the overflows are scratch, kept between steps so a step allocates nothing
  // once the largest grid this thread steps has been seen; every entry is written before read.
  // (Bound to references: the bands' worker threads must see this thread's scratch.)
  thread_local Faces faces;
  thread_local std::vector<Over> overs;
  Faces& fc = faces;
  std::vector<Over>& over = overs;
  over.clear();
  fc.x.assign(static_cast<size_t>(g.nx + 1) * g.nz, {});
  fc.z.assign(static_cast<size_t>(g.nx) * (g.nz + 1), {});
  fc.mx.assign(fc.x.size(), 0);
  fc.mz.assign(fc.z.size(), 0);
  fc.lx.assign(fc.x.size(), 0);
  fc.lz.assign(fc.z.size(), 0);
  fc.scale.assign(static_cast<size_t>(cells), 1);
  f.falls.clear();
  // Under 64^2 cells at this tier a wake-up of the pool costs more than the rows it splits
  // (water-cost's table), so a small grid runs on the caller alone: the same bits either way.
  const bool small = static_cast<long>(g.nx) * g.nz < 64L * 64L;
  const size_t workers = f.spec.workers > 1 && !small ? static_cast<size_t>(f.spec.workers) : 1;
  const size_t rows = static_cast<size_t>(g.nz);
  for (int sub = 0; sub < n; ++sub) {
    for (Flow::Packet& p : f.air) {
      p.time -= dts;
      if (p.time <= 0 && p.vol > 0) {
        if (p.cell >= 0) {
          g.vol[p.cell] += p.vol;
        } else {
          f.drained += p.vol;
        }
        p.vol = 0;
      }
    }
    const auto band = [&](auto&& pass) {
      core::parallel_bands(rows, workers, [&](size_t a, size_t b) {
        pass(static_cast<int>(a), static_cast<int>(b));
      });
    };
    band([&](int a, int b) { fluxes(f, g, fc, a, b); });
    band([&](int a, int b) { shares(g, fc, dts, a, b); });
    band([&](int a, int b) { quanta(g, fc, dts, a, b); });
    band([&](int a, int b) { update(f, g, fc, dts, a, b); });
    lips(f, g, fc, over);
  }
  size_t live = 0;
  for (const Flow::Packet& p : f.air)
    if (p.vol > 0) f.air[live++] = p;
  f.air.resize(live);
  for (const Over& o : over) {
    const double h = depth_of(g, o.cell);
    const double u = h > kDry ? std::fabs(o.di ? g.qx[o.cell] : g.qz[o.cell]) / h : 0;
    const double v = u > std::sqrt(gr * (h > 0 ? h : 0)) ? u : std::sqrt(gr * (h > 0 ? h : 0));
    int to = -1;
    double time = 0;
    land(f, g, o.cell, o.di, o.dk, v, to, time);
    f.air.push_back({o.q, to, time});
    const int i = o.cell % g.nx, k = o.cell / g.nx;
    Fall fall;
    fall.x = f.spec.x0 + (i + 0.5 + 0.5 * o.di) * g.cell;
    fall.z = f.spec.z0 + (k + 0.5 + 0.5 * o.dk) * g.cell;
    fall.y = surface_of(g, o.cell);
    fall.vx = v * o.di;
    fall.vz = v * o.dk;
    fall.discharge = static_cast<double>(o.q) * kQuantum / dt;
    fall.time = time;
    if (to >= 0) {
      const int col = to % g.nx, row = to / g.nx;
      fall.land_x = f.spec.x0 + (col + 0.5) * g.cell;
      fall.land_z = f.spec.z0 + (row + 0.5) * g.cell;
      fall.land_y = surface_of(g, to);
    }
    f.falls.push_back(fall);
  }
}

void sources_and_drains(Flow& f, double dt) {
  FlowGrid& g = f.grids[f.tier];
  f.carry.resize(f.sources.size(), 0);
  for (size_t s = 0; s < f.sources.size(); ++s) {
    const int c = cell_at(f, g, f.sources[s].x, f.sources[s].z);
    if (c < 0) continue;
    const double want = f.sources[s].rate * dt / kQuantum + f.carry[s];
    const int64_t q = static_cast<int64_t>(std::floor(want));
    f.carry[s] = want - static_cast<double>(q);
    g.vol[c] += q;
    f.added += q;
  }
  for (const Drain& d : f.drains) {
    const int c = cell_at(f, g, d.x, d.z);
    if (c < 0) continue;
    const double h = depth_of(g, c);
    const double flow = d.cd * d.area * std::sqrt(2 * f.spec.gravity * h);
    int64_t q = static_cast<int64_t>(flow * dt / kQuantum);
    if (q > g.vol[c]) q = g.vol[c];
    g.vol[c] -= q;
    f.drained += q;
  }
}

// A face's crest at a tier: its sill if the tier has them, else the higher of its beds (a
// boundary face's one bed).
double crest_x(const FlowGrid& g, int i, int k) {
  const int fi = k * (g.nx + 1) + i;
  const double l = i > 0 ? g.bed[k * g.nx + i - 1] : -INFINITY;
  const double r = i < g.nx ? g.bed[k * g.nx + i] : -INFINITY;
  const double b = l > r ? l : r;
  return g.sx.empty() || g.sx[fi] < b ? b : g.sx[fi];
}

double crest_z(const FlowGrid& g, int i, int k) {
  const int fi = k * g.nx + i;
  const double l = k > 0 ? g.bed[(k - 1) * g.nx + i] : -INFINITY;
  const double r = k < g.nz ? g.bed[k * g.nx + i] : -INFINITY;
  const double b = l > r ? l : r;
  return g.sz.empty() || g.sz[fi] < b ? b : g.sz[fi];
}

FlowGrid coarsen(const FlowGrid& fine) {
  FlowGrid g;
  g.nx = fine.nx / 2;
  g.nz = fine.nz / 2;
  g.cell = fine.cell * 2;
  const size_t cells = static_cast<size_t>(g.nx) * g.nz;
  g.bed.assign(cells, 0);
  g.vol.assign(cells, 0);
  g.qx.assign(cells, 0);
  g.qz.assign(cells, 0);
  for (int k = 0; k < g.nz; ++k) {
    for (int i = 0; i < g.nx; ++i) {
      const int c = k * g.nx + i;
      const int a = 2 * k * fine.nx + 2 * i, b = a + 1, d = a + fine.nx, e = d + 1;
      g.bed[c] = 0.25 * (fine.bed[a] + fine.bed[b] + fine.bed[d] + fine.bed[e]);
      g.vol[c] = fine.vol[a] + fine.vol[b] + fine.vol[d] + fine.vol[e];
      g.qx[c] = 0.25 * (fine.qx[a] + fine.qx[b] + fine.qx[d] + fine.qx[e]);
      g.qz[c] = 0.25 * (fine.qz[a] + fine.qz[b] + fine.qz[d] + fine.qz[e]);
    }
  }
  // Each coarse face lies on two finer faces: its crest is theirs, averaged.
  g.sx.assign(static_cast<size_t>(g.nx + 1) * g.nz, 0);
  g.sz.assign(static_cast<size_t>(g.nx) * (g.nz + 1), 0);
  for (int k = 0; k < g.nz; ++k)
    for (int i = 0; i <= g.nx; ++i)
      g.sx[k * (g.nx + 1) + i] =
          0.5 * (crest_x(fine, 2 * i, 2 * k) + crest_x(fine, 2 * i, 2 * k + 1));
  for (int k = 0; k <= g.nz; ++k)
    for (int i = 0; i < g.nx; ++i)
      g.sz[k * g.nx + i] = 0.5 * (crest_z(fine, 2 * i, 2 * k) + crest_z(fine, 2 * i + 1, 2 * k));
  return g;
}

// A coarse cell's water levelled over its four fine beds, to the quantum, moving at its speed.
void refine(const FlowGrid& coarse, FlowGrid& fine) {
  const double area = fine.cell * fine.cell;
  for (int k = 0; k < coarse.nz; ++k) {
    for (int i = 0; i < coarse.nx; ++i) {
      const int c = k * coarse.nx + i;
      const int cells[4] = {2 * k * fine.nx + 2 * i, 2 * k * fine.nx + 2 * i + 1,
                            (2 * k + 1) * fine.nx + 2 * i, (2 * k + 1) * fine.nx + 2 * i + 1};
      double beds[4];
      for (int j = 0; j < 4; ++j) beds[j] = fine.bed[cells[j]];
      for (int a = 1; a < 4; ++a)
        for (int b = a; b > 0 && beds[b] < beds[b - 1]; --b) std::swap(beds[b], beds[b - 1]);
      double level = beds[0], left = static_cast<double>(coarse.vol[c]) * kQuantum;
      for (int j = 0; j < 4 && left > 0; ++j) {
        const double room = j + 1 < 4 ? (beds[j + 1] - level) * (j + 1) * area : INFINITY;
        if (left <= room) {
          level += left / ((j + 1) * area);
          left = 0;
        } else {
          left -= room;
          level = beds[j + 1];
        }
      }
      const double hc = depth_of(coarse, c);
      const double ux = hc > kDry ? coarse.qx[c] / hc : 0, uz = hc > kDry ? coarse.qz[c] / hc : 0;
      int64_t given = 0;
      int deepest = cells[0];
      for (const int fc : cells) {
        const double d = level - fine.bed[fc];
        fine.vol[fc] = d > 0 ? static_cast<int64_t>(d * area / kQuantum) : 0;
        given += fine.vol[fc];
        if (fine.bed[fc] < fine.bed[deepest]) deepest = fc;
      }
      fine.vol[deepest] += coarse.vol[c] - given;
      for (const int fc : cells) {
        const double h = depth_of(fine, fc);
        fine.qx[fc] = ux * h;
        fine.qz[fc] = uz * h;
      }
    }
  }
}

void need_tier0(Flow& f) {
  set_tier(f, 0);
  f.asleep = false;
}

}  // namespace

Flow make_flow(const FlowSpec& spec) {
  Flow f;
  f.spec = spec;
  FlowGrid& g = f.grids[0];
  g.nx = spec.nx;
  g.nz = spec.nz;
  g.cell = spec.cell;
  const size_t cells = static_cast<size_t>(spec.nx) * spec.nz;
  g.bed.assign(cells, 0);
  g.vol.assign(cells, 0);
  g.qx.assign(cells, 0);
  g.qz.assign(cells, 0);
  return f;
}

void set_bed(Flow& f, double x0, double z0, double x1, double z1, double height) {
  need_tier0(f);
  FlowGrid& g = f.grids[0];
  for (int k = 0; k < g.nz; ++k) {
    for (int i = 0; i < g.nx; ++i) {
      const double x = f.spec.x0 + (i + 0.5) * g.cell, z = f.spec.z0 + (k + 0.5) * g.cell;
      if (x >= x0 && x < x1 && z >= z0 && z < z1) g.bed[k * g.nx + i] = height;
    }
  }
}

void set_bed(Flow& f, double (*height)(const void* context, double x, double z),
             const void* context) {
  need_tier0(f);
  FlowGrid& g = f.grids[0];
  for (int k = 0; k < g.nz; ++k)
    for (int i = 0; i < g.nx; ++i)
      g.bed[k * g.nx + i] =
          height(context, f.spec.x0 + (i + 0.5) * g.cell, f.spec.z0 + (k + 0.5) * g.cell);
}

void fill_to(Flow& f, double x0, double z0, double x1, double z1, double level) {
  need_tier0(f);
  FlowGrid& g = f.grids[0];
  for (int k = 0; k < g.nz; ++k) {
    for (int i = 0; i < g.nx; ++i) {
      const double x = f.spec.x0 + (i + 0.5) * g.cell, z = f.spec.z0 + (k + 0.5) * g.cell;
      if (x < x0 || x >= x1 || z < z0 || z >= z1) continue;
      const int c = k * g.nx + i;
      const double d = level - g.bed[c];
      const int64_t want = d > 0 ? static_cast<int64_t>(d * g.cell * g.cell / kQuantum) : 0;
      if (want > g.vol[c]) {
        f.added += want - g.vol[c];
        g.vol[c] = want;
      }
    }
  }
}

void step_flow(Flow& f, double dt) {
  const bool far = f.tier == kTiers - 1;
  const bool fed = !f.sources.empty() || !f.drains.empty() || !f.air.empty();
  if (f.asleep && (!far || fed)) f.asleep = false;  // an actor near, or water coming or going
  if (f.asleep) return;
  f.t += dt;
  const int every = 1 << f.tier;  // a coarse tier steps at its share of the rate
  if (++f.phase < every) return;
  f.phase = 0;
  sources_and_drains(f, dt * every);
  substeps(f, dt * every);
  // Far and settled: asleep, its state held, until it is woken.
  if (far && !fed && f.air.empty() && flow_speed(f) < kSettled) f.asleep = true;
}

int64_t volume(const Flow& f) {
  const FlowGrid& g = f.grids[f.tier];
  int64_t v = 0;
  for (const int64_t c : g.vol) v += c;
  for (const Flow::Packet& p : f.air) v += p.vol;
  return v;
}

FlowSample flow_at(const Flow& f, double x, double z) {
  FlowSample s;
  const FlowGrid& g = f.grids[f.tier];
  const int c = cell_at(f, g, x, z);
  if (c < 0) return s;
  s.inside = true;
  s.bed = g.bed[c];
  const double h = depth_of(g, c);
  s.depth = h;
  s.surface = s.bed + h;
  if (h <= 0) return s;
  // The surface between the wet cells' centres.
  const double u = (x - f.spec.x0) / g.cell - 0.5, w = (z - f.spec.z0) / g.cell - 0.5;
  const int i0 = static_cast<int>(std::floor(u)), k0 = static_cast<int>(std::floor(w));
  const double fu = u - i0, fw = w - k0;
  double sum = 0, weight = 0;
  for (int dk = 0; dk < 2; ++dk) {
    for (int di = 0; di < 2; ++di) {
      const int i = i0 + di, k = k0 + dk;
      if (i < 0 || k < 0 || i >= g.nx || k >= g.nz) continue;
      const int n = k * g.nx + i;
      if (g.vol[n] <= 0) continue;
      const double wt = (di ? fu : 1 - fu) * (dk ? fw : 1 - fw);
      sum += wt * surface_of(g, n);
      weight += wt;
    }
  }
  if (weight > 0) s.surface = sum / weight;
  // A coarse tier's water stands level over the cell's own bed: a rock inside a coarse cell
  // stays dry, a hollow deep.
  if (f.tier > 0) s.bed = f.grids[0].bed[cell_at(f, f.grids[0], x, z)];
  s.depth = s.surface - s.bed;
  if (s.depth <= 0) {
    s.depth = 0;
    return s;
  }
  if (h > kDry) {
    s.vx = g.qx[c] / h;
    s.vz = g.qz[c] / h;
  }
  return s;
}

double flow_speed(const Flow& f) {
  const FlowGrid& g = f.grids[f.tier];
  double v = 0;
  for (size_t c = 0; c < g.vol.size(); ++c) {
    const double h = depth_of(g, static_cast<int>(c));
    if (h <= kDry) continue;
    const double s = std::sqrt(g.qx[c] * g.qx[c] + g.qz[c] * g.qz[c]) / h;
    v = v > s ? v : s;
  }
  return v;
}

void set_tier(Flow& f, int tier) {
  tier = tier < 0 ? 0 : tier >= kTiers ? kTiers - 1 : tier;
  // A sheet in the air still lands where it was going: its cell, at the new tier.
  const auto remap = [&](int from, int to) {
    const FlowGrid& a = f.grids[from];
    const FlowGrid& b = f.grids[to];
    for (Flow::Packet& p : f.air) {
      if (p.cell < 0) continue;
      const int i = p.cell % a.nx, k = p.cell / a.nx;
      p.cell = to > from ? (k / 2) * b.nx + i / 2 : (2 * k) * b.nx + 2 * i;
    }
  };
  while (f.tier < tier) {
    f.grids[f.tier + 1] = coarsen(f.grids[f.tier]);
    remap(f.tier, f.tier + 1);
    ++f.tier;
  }
  while (f.tier > tier) {
    refine(f.grids[f.tier], f.grids[f.tier - 1]);
    remap(f.tier, f.tier - 1);
    --f.tier;
  }
  f.phase = 0;
}

int tier_for(const Flow& f, std::span<const double> actors, double near, double mid, double band) {
  const double x0 = f.spec.x0, z0 = f.spec.z0;
  const double x1 = x0 + f.spec.nx * f.spec.cell, z1 = z0 + f.spec.nz * f.spec.cell;
  double nearest = INFINITY;
  for (size_t a = 0; a + 1 < actors.size(); a += 2) {
    const double dx = actors[a] < x0 ? x0 - actors[a] : actors[a] > x1 ? actors[a] - x1 : 0;
    const double dz = actors[a + 1] < z0   ? z0 - actors[a + 1]
                      : actors[a + 1] > z1 ? actors[a + 1] - z1
                                           : 0;
    const double d = std::sqrt(dx * dx + dz * dz);
    nearest = nearest < d ? nearest : d;
  }
  // An edge moves out by the band while the region is inside it, so it does not flicker.
  const double n = near + (f.tier == 0 ? band : 0), m = mid + (f.tier <= 1 ? band : 0);
  return nearest < n ? 0 : nearest < m ? 1 : 2;
}

}  // namespace ge::water
