// A CLASSIC BSP MAP DRAWN (plugins/brush/app/bsp_draw.hpp): its surfaces with their textures
// (point-sampled from the atlas at the mip the pixel's footprint picks, wrapped inside each
// texture's rectangle) and either the map's own LIGHTMAPS or REAL-TIME LIGHTS.
//
// - MODE 0, CLASSIC: the textures times the lightmaps (a luxel of 128 is the texture as it is),
//   the liquids drawn as their own warped textures, unlit, and the sky faces as two scrolling
//   layers of the sky texture.
// - MODE 1, V2: the liquids are not drawn here (our water, lava and slime bodies are), and the sky
//   faces write a MASK the fix-up pass turns into the cleared depth, so our sky and clouds fill
//   them. Lit by the lightmaps (`info.y` 0) or by the lights (`info.y` 1): every point light's
//   inverse square under a window that closes at its reach, its shadow from a cube of depth
//   (the nearest lights'), the sun through the sky faces with its own shadow map, the sky's light
//   by how open a point is to the zenith (a second map from straight overhead), a floor of
//   ambient light, and the texels that glow.
//
// A light's strength is in the lightmap's units (1: a white face as the open day lights it, so
// the classic map's balance holds whatever the hour) or, for a glowing liquid's light, physical
// (cd); `colour.w` says which.
//
// ge:summary A classic BSP map drawn: atlas textures, lightmaps or real-time lights, the sky mask
// ge:keywords bsp brush render atlas mip lightmap point-light cube-shadow sun-shadow zenith
// ge:keywords openness sky-mask classic v2 warp fullbright
// ge:skill brush render
#include "render/wgsl/frame.wgsl"

struct Bsp {
  info: vec4f,   // mode (0 classic, 1 v2), lighting (0 lightmaps, 1 lights), lights, ambient
  info2: vec4f,  // time s, the cube face's size, the ortho map's size, the brightness
  sky: vec4f,    // the classic sky texture's rectangle
  info3: vec4f,  // v2's soft floor of light (minlight), the baked light's share as bounce
  ortho: array<Job, 2>,  // the sun's map and the zenith's
};

// A shadow map's camera: an origin, three axes (z the way it looks) and its reach: a point
// light's cube face (`info.x` 0; `o.w` its far) or an orthographic map (`info.x` 1; `o.w` its
// half width, `info.y` its half depth).
struct Job {
  o: vec4f,
  x: vec4f,
  y: vec4f,
  z: vec4f,
  info: vec4f,
};

struct Light {
  pos: vec4f,     // world m; w: its reach, m
  colour: vec4f,  // its strength (rgb, the flicker in it); w: 1 physical (cd), 0 lightmap units
  info: vec4f,    // x: its shadow's first layer (six a light), -1 none
};

@group(1) @binding(0) var<uniform> bsp: Bsp;
@group(1) @binding(1) var atlas: texture_2d<f32>;
@group(1) @binding(2) var light_atlas: texture_2d<f32>;
@group(1) @binding(3) var light_sampler: sampler;
@group(1) @binding(4) var<storage, read> lights: array<Light>;
@group(1) @binding(5) var cubes: texture_depth_2d_array;
@group(1) @binding(6) var shadow_cmp: sampler_comparison;
@group(1) @binding(7) var orthos: texture_depth_2d_array;

const kSky = 1u;
const kWater = 2u;
const kSlime = 3u;
const kLava = 4u;
const kWarp = 6u;

struct VsIn {
  @location(0) pos: vec3f,
  @location(1) st: vec2f,
  @location(2) lm: vec2f,
  @location(3) nrm: vec3f,
  @location(4) rect: vec4f,
  @location(5) kind: u32,
};

struct VsOut {
  @builtin(position) clip: vec4f,
  @location(0) world: vec3f,
  @location(1) st: vec2f,
  @location(2) lm: vec2f,
  @location(3) nrm: vec3f,
  @location(4) @interpolate(flat) rect: vec4f,
  @location(5) @interpolate(flat) kind: u32,
};

@vertex
fn vs_main(v: VsIn) -> VsOut {
  var o: VsOut;
  o.clip = frame.view_proj * vec4f(v.pos - frame.eye.xyz, 1.0);
  o.world = v.pos;
  o.st = v.st;
  o.lm = v.lm;
  o.nrm = v.nrm;
  o.rect = v.rect;
  o.kind = v.kind;
  return o;
}

// The texel at `st` (texels of mip 0) of the texture at `rect`, at mip `lod`, wrapped.
fn texel(st: vec2f, rect: vec4f, lod: f32) -> vec4f {
  let k = u32(clamp(lod, 0.0, 3.0));
  let s = f32(1u << k);
  let size = max(floor(rect.zw / s), vec2f(1.0));
  var t = floor(st / s);
  t = t - size * floor(t / size);
  return textureLoad(atlas, vec2i(floor(rect.xy / s) + t), i32(k));
}

// V2's floor: no face darker than about `m`, a lit one nearly as it was (sqrt(e^2 + m^2)).
fn floor_light(e: vec3f, m: f32) -> vec3f {
  return sqrt(e * e + vec3f(m * m));
}

fn lightmap(lm: vec2f) -> vec3f {
  if (lm.x < 0.0) {
    return vec3f(1.0);
  }
  return textureSampleLevel(light_atlas, light_sampler, lm, 0.0).rgb * (255.0 / 128.0);
}

// The classic sky: a far layer and a near one over it (its texel 0 clear), scrolling.
fn classic_sky(d: vec3f) -> vec3f {
  let k = 1.0 / (abs(d.y) * 3.0 + 0.35);
  let uv = d.xz * k * 48.0;
  let t = bsp.info2.x;
  let half = vec4f(bsp.sky.x + bsp.sky.z * 0.5, bsp.sky.y, bsp.sky.z * 0.5, bsp.sky.w);
  let near = vec4f(bsp.sky.x, bsp.sky.y, bsp.sky.z * 0.5, bsp.sky.w);
  let back = texel(uv + vec2f(t * 8.0), half, 0.0);
  let front = texel(uv + vec2f(t * 16.0), near, 0.0);
  return select(front.rgb, back.rgb, front.a > 0.002 && front.a < 0.006);
}

// The share of a point a cube of depth sees lit: its face by the major axis of `q` (from the
// light to the point), its texel there, 2 x 2 compared.
fn cube_shadow(q: vec3f, far: f32, base: i32) -> f32 {
  let a = abs(q);
  var i = 0;
  if (a.y > a.x && a.y >= a.z) {
    i = 1;
  } else if (a.z > a.x && a.z > a.y) {
    i = 2;
  }
  let m = a[i];
  let f = 2 * i + select(0, 1, q[i] < 0.0);
  let u = q[(i + 1) % 3] / m;
  let v = q[(i + 2) % 3] / m;
  let uv = vec2f(0.5 + 0.5 * u, 0.5 - 0.5 * v);
  let ref_depth = length(q) / far - 0.004;
  return textureSampleCompareLevel(cubes, shadow_cmp, uv, base + f, ref_depth);
}

// An orthographic map's lit share at `p` over a 3 x 3 spread of `spread` m.
fn ortho_lit(j: Job, layer: i32, p: vec3f, spread: f32) -> f32 {
  let q = p - j.o.xyz;
  let x = dot(q, j.x.xyz) / j.o.w;
  let y = dot(q, j.y.xyz) / j.o.w;
  let z = (dot(q, j.z.xyz) + j.info.y) / (2.0 * j.info.y);
  let step = spread / j.o.w * 0.5;
  var lit = 0.0;
  for (var dy = -1; dy <= 1; dy++) {
    for (var dx = -1; dx <= 1; dx++) {
      let uv = vec2f(0.5 + 0.5 * x + f32(dx) * step, 0.5 - 0.5 * y + f32(dy) * step);
      lit += textureSampleCompareLevel(orthos, shadow_cmp, uv, layer, z - 0.0015);
    }
  }
  return lit / 9.0;
}

struct FsOut {
  @location(0) colour: vec4f,
  @location(1) motion: vec4f,
  @location(2) mask: vec4f,
};

@fragment
fn fs_main(in: VsOut) -> FsOut {
  var out: FsOut;
  out.motion = vec4f(0.0);
  out.mask = vec4f(0.0);
  let classic = bsp.info.x < 0.5;
  let t = bsp.info2.x;
  var st = in.st;
  let warped = in.kind == kWater || in.kind == kSlime || in.kind == kLava || in.kind == kWarp;
  if (warped) {
    st = st + 8.0 * sin(st.yx * (6.2831853 / 128.0) + vec2f(t));
  }
  let footprint = max(length(dpdx(st)), length(dpdy(st)));
  let lod = log2(max(footprint, 1e-6));
  // A lightmap's 1 (and a light in its units): a white face as the open day lights it, times the
  // brightness (the eye adapted to a dim room).
  let white = white_luminance() * bsp.info2.w;
  let eye_dir = normalize(in.world - frame.eye.xyz);

  if (in.kind == kSky) {
    if (classic) {
      out.colour = vec4f(srgb_to_linear(classic_sky(eye_dir)) * white, 1.0);
    } else {
      out.mask = vec4f(1.0);
      out.colour = vec4f(0.0, 0.0, 0.0, 1.0);
    }
    return out;
  }

  let tx = texel(st, in.rect, lod);
  let albedo = tx.rgb;
  let glows = tx.a > 0.99;
  if (warped) {
    out.colour = vec4f(albedo * white, 1.0);
    return out;
  }
  var n = normalize(in.nrm);
  if (dot(n, eye_dir) > 0.0) {
    n = -n;
  }

  if (classic || bsp.info.y < 0.5) {
    var lit = lightmap(in.lm);
    if (!classic) {
      lit = floor_light(lit, bsp.info3.x);
    }
    if (glows) {
      lit = max(lit, vec3f(1.0));
    }
    out.colour = vec4f(albedo * lit * white, 1.0);
    return out;
  }

  // ── Real-time lights ──
  let p = in.world + n * 0.03;
  var e = vec3f(bsp.info.w);
  var phys = vec3f(0.0);
  let count = u32(bsp.info.z);
  for (var i = 0u; i < count; i++) {
    let l = lights[i];
    let q = p - l.pos.xyz;
    let d2 = dot(q, q);
    let reach = l.pos.w;
    if (d2 >= reach * reach) {
      continue;
    }
    let d = sqrt(d2);
    let ndl = dot(n, -q / max(d, 1e-4));
    if (ndl <= 0.0) {
      continue;
    }
    let r = d / reach;
    let r4 = r * r * r * r;
    let window = (1.0 - r4) * (1.0 - r4);
    var lit = ndl * window / (d2 + 0.04);
    if (l.info.x >= 0.0) {
      lit *= cube_shadow(q, reach, i32(l.info.x));
    }
    if (l.colour.w > 0.5) {
      phys += l.colour.rgb * lit;
    } else {
      e += l.colour.rgb * lit;
    }
  }
  // The light bounced round the room: the map's baked light (its light probes) at a share.
  e += lightmap(in.lm) * bsp.info3.y;
  e = floor_light(e, bsp.info3.x);
  var c = albedo * e * white;
  // A physical light: illuminance (lx) on the face to its luminance in the frame's units.
  c += albedo * phys / PI * frame.world.y / kSunLux;
  // The sun through the sky faces, and the sky's light by how open the point is overhead.
  let sun_dir = frame.sun.xyz;
  let sun_ndl = max(dot(n, sun_dir), 0.0);
  if (sun_ndl > 0.0 && frame.sun.y > 0.0) {
    let lit = ortho_lit(bsp.ortho[0], 0, p, 0.05);
    c += albedo / PI * sun_light(in.world.y, in.world.xz) * sun_ndl * lit;
  }
  let open = ortho_lit(bsp.ortho[1], 1, in.world + n * 0.3, 1.2);
  c += albedo / PI * sky_irradiance(n) * open;
  if (glows) {
    c += albedo * white;
  }
  out.colour = vec4f(c, 1.0);
  // A pixel whose light came out not a number (a degenerate face) or past a half float: black,
  // never a NaN for the bloom to spread over the frame.
  let bits = bitcast<vec3u>(c) & vec3u(0x7f800000u);
  if (any(bits == vec3u(0x7f800000u)) || any(abs(c) > vec3f(6e4))) {
    out.colour = vec4f(0.0, 0.0, 0.0, 1.0);
  }
  return out;
}

fn srgb_to_linear(c: vec3f) -> vec3f {
  return select(pow((c + 0.055) / 1.055, vec3f(2.4)), c / 12.92, c <= vec3f(0.04045));
}
