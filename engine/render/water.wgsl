// THE WATER'S SURFACE (render/water.hpp; docs/water.md § Drawn): a body of water drawn over the
// opaque scene, a grid of nested square levels round the eye at the body's level.
//
// THE GEOMETRY: level i is kGrid x kGrid quads of `spacing * 2^i`, centred on the eye snapped to
// twice its spacing, so no vertex slides over the waves as the eye moves; toward its edge a
// level's odd vertices morph onto the coarser level's grid, and a coarse vertex a finer level
// covers sinks under it (the terrain's scheme, render/terrain.hpp). The last level's rim runs
// out to the horizon. Every vertex is clamped into the body's box, and a fragment outside it is
// dropped; the shore is where the opaque ground stands over the water (the depth test).
//
// THE WAVES are the simulation's components (water/waves.hpp), summed in its formula: a linear
// wave with its second-order crest, eta = a sin(phi) - 1/2 k a^2 cos(2 phi), the height a
// function of the undisplaced point, so the drawn crest is the crest a float rides. A vertex
// keeps the components at least four of its level's spacings long (faded in up to eight). Every
// pixel sums their slopes and the DETAIL components' (the picture's own, shorter than the
// simulation keeps), each faded out as its wavelength falls under four pixels; the slope
// variance a pixel cannot resolve (the faded components' and the spectrum's tail past the
// detail band) becomes the sun's glitter's roughness, so far water glints as a broad path and
// never shimmers. The WAKES' rings (water/wake.hpp) are summed the same way, kept by the vertex
// and the pixel as a wave is.
//
// THE LIGHT is render/wgsl/water_light.wgsl's `shade_water`, the moving water's too.
#include "render/wgsl/frame.wgsl"

override kGrid: u32 = 64u;

struct WaterBody {
  info: vec4f,       // level (world m), the waves' amplitude scale, components, detail components
  box: vec4f,        // x0, z0, x1, z1 (world m)
  grid: vec4f,       // levels, sink (spacings), morph start, the rim's reach (times)
  rough: vec4f,      // the tail's slope variance, the calm surface's, refraction's strength, 0
  c: vec4f,          // beam attenuation, rgb (1/m)
  kd: vec4f,         // the daylight's diffuse attenuation, rgb (1/m)
  deep: vec4f,       // deep water's colour, pi Rrs, rgb
  emission: vec4f,   // a body's own glow, rgb (white = a white face in daylight)
  wake: vec4f,       // rings, their life (s), the shortest wave kept (m), 0
  levels: array<vec4f, 12>,  // origin x, z, spacing, 0
}

// A wake's ring (water/wake.hpp): x, z, the moment it was set off, its volume; its source's
// size, its own shortest wave, 0, 0.
struct Ring {
  at: vec4f,
  more: vec4f,
}

// A wave: kx, kz, amplitude, angular frequency; phase, k, 0, 0.
struct Wave {
  dir: vec4f,
  more: vec4f,
}

@group(1) @binding(0) var<uniform> body: WaterBody;
@group(1) @binding(1) var<storage, read> waves: array<Wave>;  // the simulation's, then the detail
@group(1) @binding(2) var scene_colour: texture_2d<f32>;
@group(1) @binding(3) var scene_depth: texture_2d<f32>;      // R32F: the opaque depth
@group(1) @binding(4) var<storage, read> rings: array<Ring>;  // the body's wake, newest first

#include "render/wgsl/water_light.wgsl"

struct VertexOut {
  @builtin(position) position: vec4f,
  @location(0) rel: vec3f,  // camera-relative world position, m
  @location(1) xz: vec2f,   // the undisplaced world plan position, m
}

const kMorphEnd = 0.95;

// The height of the components at least `shortest` m long (faded in to twice that) at `xz`.
fn height_at(xz: vec2f, t: f32, shortest: f32) -> f32 {
  var h = 0.0;
  let n = u32(body.info.z);
  for (var i = 0u; i < n; i++) {
    let w = waves[i];
    let k = w.more.y;
    let keep = smoothstep(shortest, 2.0 * shortest, 6.2831853 / k);
    if (keep <= 0.0) {
      continue;
    }
    let a = w.dir.z * body.info.y;
    let phi = dot(w.dir.xy, xz) - w.dir.w * t + w.more.x;
    h += keep * (a * sin(phi) - 0.5 * k * a * a * cos(2.0 * phi));
  }
  return h;
}

const kGravity = 9.80665;

// THE WAKES: every ring's far field (water/wake.cpp's `wake_at`), its waves shorter than
// `shortest` m faded out (and their slopes' variance handed back in `lost`); the height and its
// slope.
fn wake_at(xz: vec2f, t: f32, shortest: f32) -> vec4f {
  var out = vec4f(0.0);  // height, d/dx, d/dz, the slope variance dropped
  let n = u32(body.wake.x);
  let life = body.wake.y;
  for (var i = 0u; i < n; i++) {
    let r = rings[i];
    let tau = t - r.at.z;
    if (tau <= 0.0) {
      continue;
    }
    let e = xz - r.at.xy;
    let rs = sqrt(dot(e, e) + r.more.x * r.more.x);
    let phase = kGravity * tau * tau / (4.0 * rs);
    let k = phase / rs;
    let lambda = 6.2831853 / k;
    let lowest = max(body.wake.z, r.more.y);
    let a = r.at.w * k / (1.4142136 * PI * rs) * smoothstep(lowest, 2.0 * lowest, lambda) *
            (1.0 - smoothstep(life * 0.6666667, life, tau));
    let keep = smoothstep(shortest, 2.0 * shortest, lambda);
    out.w += (1.0 - keep) * 0.5 * (k * a) * (k * a);
    if (keep <= 0.0) {
      continue;
    }
    // The phase taken round in f32 from where it is still exact enough.
    let p = phase - 6.2831853 * floor(phase / 6.2831853);
    out.x += keep * a * cos(p);
    out.y += keep * a * k * sin(p) * e.x / rs;
    out.z += keep * a * k * sin(p) * e.y / rs;
  }
  return out;
}

@vertex
fn vs_main(@builtin(vertex_index) vid: u32, @builtin(instance_index) level: u32) -> VertexOut {
  let n = kGrid + 1u;
  let g = vec2f(f32(vid % n), f32(vid / n));
  let lv = body.levels[level];
  let s = lv.z;
  let at = lv.xy + g * s;
  let half = f32(kGrid) * 0.5 * s;
  let d = max(abs(at.x - frame.eye.x), abs(at.y - frame.eye.z)) / half;
  let last = level + 1u >= u32(body.grid.x);
  var m = clamp((d - body.grid.z) / (kMorphEnd - body.grid.z), 0.0, 1.0);
  if (last) {
    m = 0.0;
  }
  var xz = lv.xy + (g - fract(g * 0.5) * 2.0 * m) * s;
  let edge = g.x == 0.0 || g.y == 0.0 || g.x == f32(kGrid) || g.y == f32(kGrid);
  if (last && edge) {
    xz = frame.eye.xz + (xz - frame.eye.xz) * body.grid.w;
  }
  xz = clamp(xz, body.box.xy, body.box.zw);
  var y = body.info.x + height_at(xz, frame.time.x, 4.0 * s);
  y += wake_at(xz, frame.time.x, 4.0 * s).x;
  if (level > 0u) {
    let f = body.levels[level - 1u];
    let lo = f.xy + vec2f(0.25 * s);
    let hi = f.xy + vec2f(f32(kGrid) * f.z) - vec2f(0.25 * s);
    if (all(xz > lo) && all(xz < hi)) {
      y -= body.grid.y * s;
    }
  }
  var out: VertexOut;
  out.rel = vec3f(xz.x, y, xz.y) - frame.eye.xyz;
  out.position = frame.view_proj * vec4f(out.rel, 1.0);
  out.xz = xz;
  return out;
}

struct FragmentOut {
  @location(0) colour: vec4f,
  @location(1) motion: vec4f,
}

@fragment
fn fs_main(in: VertexOut) -> FragmentOut {
  // The pixel's footprint on the water, m (before anything can discard).
  let foot = max(length(dpdx(in.xz)), length(dpdy(in.xz)));
  if (any(in.xz < body.box.xy) || any(in.xz > body.box.zw)) {
    discard;
  }
  let t = frame.time.x;
  var slope = vec2f(0.0);
  var lost = body.rough.x + body.rough.y;
  let total = u32(body.info.z + body.info.w);
  for (var i = 0u; i < total; i++) {
    let w = waves[i];
    let k = w.more.y;
    let a = w.dir.z * body.info.y;
    let keep = smoothstep(2.0 * foot, 4.0 * foot, 6.2831853 / k);
    lost += (1.0 - keep) * 0.5 * (k * a) * (k * a);
    if (keep <= 0.0) {
      continue;
    }
    let phi = dot(w.dir.xy, in.xz) - w.dir.w * t + w.more.x;
    slope += keep * (a * cos(phi) + k * a * a * sin(2.0 * phi)) * w.dir.xy;
  }
  let wake = wake_at(in.xz, t, 2.0 * foot);
  slope += wake.yz;
  lost += wake.w;
  let n = normalize(vec3f(-slope.x, 1.0, -slope.y));
  let dist = length(in.rel);
  let uv = in.position.xy * frame.size.zw;
  let c = shade_water(n, in.rel, in.xz, in.position.xy, body.info.x, lost, body.rough.z,
                      Optics(body.c.rgb, body.kd.rgb, body.deep.rgb, body.emission.rgb), 0.0);
  var out: FragmentOut;
  out.colour = vec4f(haze(c, uv, dist), 1.0);
  out.motion = vec4f(screen_motion(frame, vec4f(in.rel, 1.0), in.position.xy), 0.0, 0.0);
  return out;
}
