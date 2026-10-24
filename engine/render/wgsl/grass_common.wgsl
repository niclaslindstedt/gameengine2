// THE GRASS'S SHARED HALF (render/grass.hpp): the kinds, the cover's triangles, a blade, the
// hash, the scatter of one blade, the air at a blade and its shape in it. Every function here is
// the WGSL twin of engine/grass's (cover.cpp's `scatter_one`, sway.cpp's `wind_at_blade` and
// `blade_shape`), float for float in the same order; tests/render_grass_test.cpp holds them to it.

// grass/kind.hpp's Kind, as render/grass.cpp packs it.
struct GrassKind {
  shape: vec4f,   // height, height_jitter, width, density
  lean: vec4f,    // lean, lean_jitter, clump, pull
  air: vec4f,     // give, sway, dry, 0
  root: vec4f,    // rgb
  tip: vec4f,     // rgb
  dry_tip: vec4f, // rgb
}

// grass/cover.hpp's Tri.
struct GrassTri {
  a: vec4f,      // xyz, share at a
  b: vec4f,      // xyz, share at b
  c: vec4f,      // xyz, share at c
  info: vec4u,   // kind, its seed, 0, 0
}

// grass/cover.hpp's Blade.
struct Blade {
  root: vec3f,
  seed: u32,     // own hash (low 24 bits), kind (high 8)
  height: f32,
  width: f32,
  lean: vec2f,   // rest lean as a plan vector, rad
}

struct GrassParams {
  cover: vec4u,  // triangles, capacity (each list), segments near, segments far
  lod: vec4f,    // full_m (every blade kept within), near_m (fine blades within), reach_m, min_keep
  wind: vec4f,   // the grid's x0, z0, cell, nodes a side
  mean: vec4f,   // the mean plan air (x, z), the time (s), 0
}

const kGrassTau = 6.28318530717958647;
const kMaxBend = 1.35;
const kRuffleCell = 6.0;
const kMaxBladesPerTri = 4096u;

fn grass_hash(v: u32) -> u32 {
  let state = v * 747796405u + 2891336453u;
  let word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

fn grass_unit(h: u32) -> f32 {
  return f32(h >> 8u) * (1.0 / 16777216.0);
}

fn grass_area(t: GrassTri) -> f32 {
  let e1 = t.b.xyz - t.a.xyz;
  let e2 = t.c.xyz - t.a.xyz;
  let x = e1.y * e2.z - e1.z * e2.y;
  let y = e1.z * e2.x - e1.x * e2.z;
  let z = e1.x * e2.y - e1.y * e2.x;
  return 0.5 * sqrt(x * x + y * y + z * z);
}

fn blade_slots(t: GrassTri, k: GrassKind) -> u32 {
  let most = max(max(t.a.w, t.b.w), t.c.w);
  let expect = grass_area(t) * k.shape.w * most;
  var n = floor(expect);
  if (grass_unit(grass_hash(t.info.y ^ 0xa511e9b3u)) < expect - n) {
    n = n + 1.0;
  }
  return u32(min(n, f32(kMaxBladesPerTri)));
}

struct GrassBlade {
  blade: Blade,
  rank: f32,
  kept: bool,
}

fn scatter_one(t: GrassTri, k: GrassKind, slot: u32) -> GrassBlade {
  var out: GrassBlade;
  out.kept = false;
  let h0 = grass_hash(t.info.y + slot * 0x9e3779b9u);
  let h1 = grass_hash(h0);
  let h2 = grass_hash(h1);
  let h3 = grass_hash(h2);
  let h4 = grass_hash(h3);
  let h5 = grass_hash(h4);
  let h6 = grass_hash(h5);
  let h7 = grass_hash(h6);
  let h8 = grass_hash(h7);
  let s = sqrt(grass_unit(h0));
  let u = 1.0 - s;
  let v = grass_unit(h1) * s;
  let w = 1.0 - u - v;
  let p = t.a.xyz * u + t.b.xyz * v + t.c.xyz * w;
  let share = t.a.w * u + t.b.w * v + t.c.w * w;
  let most = max(max(t.a.w, t.b.w), t.c.w);
  if (grass_unit(h2) * most >= share) {
    return out;
  }
  out.rank = grass_unit(h3);

  var scale = 1.0;
  var toward = vec2f(0.0);
  let clump = k.lean.z;
  if (clump > 0.0) {
    let gx = floor(p.x / clump);
    let gz = floor(p.z / clump);
    var best = 1e30;
    var best_h = 0u;
    for (var dz = -1; dz <= 1; dz++) {
      for (var dx = -1; dx <= 1; dx++) {
        let cx = gx + f32(dx);
        let cz = gz + f32(dz);
        let ch = grass_hash(grass_hash(bitcast<u32>(i32(cx))) ^ bitcast<u32>(i32(cz)));
        let mx = (cx + 0.5 + (grass_unit(ch) - 0.5) * 0.8) * clump;
        let mz = (cz + 0.5 + (grass_unit(grass_hash(ch)) - 0.5) * 0.8) * clump;
        let ex = mx - p.x;
        let ez = mz - p.z;
        let d2 = ex * ex + ez * ez;
        if (d2 < best) {
          best = d2;
          best_h = ch;
          toward = vec2f(ex, ez);
        }
      }
    }
    scale = 0.75 + 0.5 * grass_unit(grass_hash(best_h ^ 0x5bd1e995u));
    let tl = sqrt(best);
    if (tl > 1e-6) {
      toward = toward / tl;
    }
  }
  var d = vec2f(grass_unit(h4) * 2.0 - 1.0, grass_unit(h5) * 2.0 - 1.0);
  var dl = sqrt(d.x * d.x + d.y * d.y) + 1e-6;
  d = d / dl;
  let pull = k.lean.w;
  var l = vec2f(d.x * (1.0 - pull) + toward.x * pull, d.y * (1.0 - pull) + toward.y * pull);
  dl = sqrt(l.x * l.x + l.y * l.y);
  if (dl < 1e-4) {
    l = d;
  } else {
    l = l / dl;
  }
  let angle = k.lean.x + k.lean.y * (grass_unit(h6) * 2.0 - 1.0);

  out.blade.root = p;
  out.blade.seed = (h0 & 0xffffffu) | (t.info.x << 24u);
  out.blade.height = k.shape.x * (1.0 + k.shape.y * (grass_unit(h7) * 2.0 - 1.0)) * scale;
  out.blade.width = k.shape.z * (0.8 + 0.4 * grass_unit(h8));
  out.blade.lean = l * angle;
  out.kept = true;
  return out;
}

// ── The air at a blade ──

fn grass_lattice(ix: f32, iz: f32) -> f32 {
  return grass_unit(grass_hash(grass_hash(bitcast<u32>(i32(ix))) ^ bitcast<u32>(i32(iz))));
}

fn grass_noise(x: f32, z: f32) -> f32 {
  let ix = floor(x);
  let iz = floor(z);
  var fx = x - ix;
  var fz = z - iz;
  fx = fx * fx * (3.0 - 2.0 * fx);
  fz = fz * fz * (3.0 - 2.0 * fz);
  let a = grass_lattice(ix, iz);
  let b = grass_lattice(ix + 1.0, iz);
  let c = grass_lattice(ix, iz + 1.0);
  let d = grass_lattice(ix + 1.0, iz + 1.0);
  let top = a + (b - a) * fx;
  let bottom = c + (d - c) * fx;
  return top + (bottom - top) * fz;
}

// The plan air at (x, z) at `t`: the grid (`grass_wind`, a vec2 a node, row-major in z, which
// the including shader binds beside `gp`) bilinear, ruffled.
fn wind_at_blade(x: f32, z: f32, t: f32) -> vec2f {
  let n = u32(gp.wind.w);
  if (n < 2u) {
    return vec2f(0.0);
  }
  let top = f32(n - 1u);
  let gx = clamp((x - gp.wind.x) / gp.wind.z, 0.0, top);
  let gz = clamp((z - gp.wind.y) / gp.wind.z, 0.0, top);
  let ix = min(floor(gx), top - 1.0);
  let iz = min(floor(gz), top - 1.0);
  let fx = gx - ix;
  let fz = gz - iz;
  let c00 = u32(iz) * n + u32(ix);
  let ruffle = 0.7 + 0.6 * grass_noise((x - gp.mean.x * t) / kRuffleCell,
                                        (z - gp.mean.y * t) / kRuffleCell);
  let a = grass_wind[c00] + (grass_wind[c00 + 1u] - grass_wind[c00]) * fx;
  let b = grass_wind[c00 + n] + (grass_wind[c00 + n + 1u] - grass_wind[c00 + n]) * fx;
  return (a + (b - a) * fz) * ruffle;
}

// ── A blade's shape ──

struct BladeShape {
  v0: vec3f,
  v1: vec3f,
  v2: vec3f,
}

fn bend_of(k: GrassKind, speed: f32) -> f32 {
  let w2 = speed * speed;
  return w2 / (w2 + k.air.x * k.air.x);
}

fn blade_shape(k: GrassKind, b: Blade, wind: vec2f, t: f32) -> BladeShape {
  let h = b.height;
  let hp = grass_hash(b.seed ^ 0x27d4eb2fu);
  let phase = grass_unit(hp) * kGrassTau;
  let freq = k.air.y * (0.85 + 0.3 * grass_unit(grass_hash(hp)));
  let w = sqrt(wind.x * wind.x + wind.y * wind.y);
  let bend = bend_of(k, w);
  var d = vec2f(0.0);
  var o = vec2f(1.0, 0.0);
  if (w > 1e-4) {
    d = wind / w;
    o = d;
  } else {
    let l = sqrt(b.lean.x * b.lean.x + b.lean.y * b.lean.y);
    if (l > 1e-6) {
      o = b.lean / l;
    }
  }
  let osc = sin(kGrassTau * freq * t + phase);
  let osc2 = sin(kGrassTau * freq * 1.37 * t + phase * 1.7);
  let amp = 0.04 + 0.6 * bend * (1.0 - bend);
  let ax = b.lean.x * (1.0 - bend) + d.x * (bend * kMaxBend) + o.x * (osc * amp) -
           o.y * (osc2 * amp * 0.35);
  let az = b.lean.y * (1.0 - bend) + d.y * (bend * kMaxBend) + o.y * (osc * amp) +
           o.x * (osc2 * amp * 0.35);
  let al = sqrt(ax * ax + az * az);
  let theta = min(al, kMaxBend + 0.1);
  var u = vec2f(1.0, 0.0);
  if (al > 1e-6) {
    u = vec2f(ax, az) / al;
  }
  let st = sin(theta);
  let ct = cos(theta);
  var s: BladeShape;
  s.v0 = b.root;
  let v2 = s.v0 + vec3f(h * u.x * st, h * ct, h * u.y * st);
  let v1 = s.v0 + vec3f(0.0, h * max(1.0 - st, 0.05), 0.0);
  let chord = length(v2 - s.v0);
  let poly = length(v1 - s.v0) + length(v2 - v1);
  let r = h / ((2.0 * chord + poly) / 3.0);
  s.v1 = s.v0 + r * (v1 - s.v0);
  s.v2 = s.v1 + r * (v2 - v1);
  return s;
}

fn bezier_at(s: BladeShape, u: f32) -> vec3f {
  return (1.0 - u) * (1.0 - u) * s.v0 + 2.0 * u * (1.0 - u) * s.v1 + u * u * s.v2;
}

fn bezier_tangent(s: BladeShape, u: f32) -> vec3f {
  return 2.0 * (1.0 - u) * (s.v1 - s.v0) + 2.0 * u * (s.v2 - s.v1);
}
