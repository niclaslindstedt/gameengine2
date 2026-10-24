// MOVING WATER (render/water_stream.hpp; docs/water.md § Drawn): the channels' ribbons, the
// flow regions' grids and the waterfalls' curtains, built on the CPU each frame and read here
// from a storage buffer by the index.
//
// THE SURFACE: the mesh's own normal (a river falling down its bed) tipped by the ripples the
// current carries, a FLOW MAP in two phases: the ripples read at the point moved back along the
// current by a share of a cycle, two reads half a cycle apart, each weighed by its distance from
// its own reset, so the pattern runs downstream at the water's own speed and never stretches;
// rougher where the water runs faster. FOAM as lace: a pattern carried the same way, cut to the
// vertex's share. THE CURTAIN: aerated white with streaks running down it at the sheet's speed,
// the water seen through its thin parts. The light is render/wgsl/water_light.wgsl's.
#include "render/wgsl/frame.wgsl"

struct StreamBody {
  c: vec4f,
  kd: vec4f,
  deep: vec4f,
  emission: vec4f,
}

// The vertex: x, y, z, foam (a curtain: across, m); the current (a curtain: its speed, 0), the
// depth (a curtain: along, m), the tag (the body times 2, plus 1 on a curtain).
struct StreamVertex {
  a: vec4f,
  b: vec4f,
}

@group(1) @binding(0) var<storage, read> bodies: array<StreamBody>;
@group(1) @binding(1) var<storage, read> vertices: array<StreamVertex>;
@group(1) @binding(2) var scene_colour: texture_2d<f32>;
@group(1) @binding(3) var scene_depth: texture_2d<f32>;

#include "render/wgsl/water_light.wgsl"

struct VertexOut {
  @builtin(position) position: vec4f,
  @location(0) rel: vec3f,
  @location(1) xz: vec2f,
  @location(2) flow: vec2f,
  @location(3) foam: f32,
  @location(4) along: f32,
  @location(5) @interpolate(flat) tag: u32,
}

@vertex
fn vs_main(@builtin(vertex_index) vid: u32) -> VertexOut {
  let v = vertices[vid];
  var out: VertexOut;
  out.rel = v.a.xyz - frame.eye.xyz;
  out.position = frame.view_proj * vec4f(out.rel, 1.0);
  out.xz = v.a.xz;
  out.flow = v.b.xy;
  out.foam = v.a.w;
  out.along = v.b.z;
  out.tag = u32(v.b.w + 0.5);
  return out;
}

struct FragmentOut {
  @location(0) colour: vec4f,
  @location(1) motion: vec4f,
}

fn hash2(p: vec2f) -> f32 {
  return fract(sin(dot(p, vec2f(127.1, 311.7))) * 43758.5453);
}

// Value noise, 0..1.
fn noise(p: vec2f) -> f32 {
  let i = floor(p);
  let f = fract(p);
  let u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash2(i), hash2(i + vec2f(1.0, 0.0)), u.x),
             mix(hash2(i + vec2f(0.0, 1.0)), hash2(i + vec2f(1.0, 1.0)), u.x), u.y);
}

// The ripples' slope at `p`: six short waves across every heading, the shortest dropped where
// the pixel is wider than a quarter of them; and the lace's pattern there.
fn ripples(p: vec2f, foot: f32) -> vec3f {
  var s = vec2f(0.0);
  for (var i = 0; i < 6; i++) {
    let a = f32(i) * 1.0471976 + 0.37;
    let d = vec2f(cos(a), sin(a));
    let len = 0.22 * pow(1.55, f32(i));
    let k = 6.2831853 / len;
    let keep = smoothstep(2.0 * foot, 4.0 * foot, len);
    s += keep * cos(dot(d, p) * k + f32(i) * 1.7) * d;
  }
  let lace = noise(p * 2.3) * 0.65 + noise(p * 6.1) * 0.35;
  return vec3f(s / 6.0, lace);
}

const kCycle = 1.6;  // s: the flow map's cycle

@fragment
fn fs_main(in: VertexOut) -> FragmentOut {
  let foot = max(length(dpdx(in.xz)), length(dpdy(in.xz)));
  let body = bodies[in.tag / 2u];
  let o = Optics(body.c.rgb, body.kd.rgb, body.deep.rgb, body.emission.rgb);
  let t = frame.time.x;
  let dist = length(in.rel);
  let uv = in.position.xy * frame.size.zw;
  // The mesh's own normal, facing the eye's side.
  var geo = normalize(cross(dpdy(in.rel), dpdx(in.rel)));
  if (geo.y < 0.0) {
    geo = -geo;
  }
  var c: vec3f;
  if ((in.tag & 1u) == 1u) {
    // The curtain: aerated white, streaks of denser white running down it at the sheet's speed,
    // the water seen through the thin parts between them.
    let down = in.along - in.flow.x * t;
    let streak = noise(vec2f(in.foam * 3.0, down * 0.45)) * 0.6 +
                 noise(vec2f(in.foam * 9.0, down * 1.3)) * 0.4;
    let thick = smoothstep(0.3, 0.7, streak);
    // Aerated water scatters light every way: lit as a face tipped well up toward the sky, so a
    // curtain the sun is behind still reads white.
    let lit = normalize(geo + vec3f(0.0, 1.5, 0.0));
    c = shade_water(lit, in.rel, in.xz, in.position.xy, in.rel.y + frame.eye.y, 0.02, 0.02, o,
                    0.7 + 0.3 * thick);
  } else {
    let speed = length(in.flow);
    let pa = fract(t / kCycle);
    let pb = fract(t / kCycle + 0.5);
    let wa = 1.0 - abs(2.0 * pa - 1.0);
    let ra = ripples(in.xz - in.flow * pa * kCycle, foot);
    let rb = ripples(in.xz - in.flow * pb * kCycle + vec2f(3.1, 1.7), foot);
    let r = ra * wa + rb * (1.0 - wa);
    let rough = 0.05 + 0.12 * smoothstep(0.2, 3.0, speed);
    let slope = r.xy * rough;
    let n = normalize(vec3f(geo.x / geo.y - slope.x, 1.0, geo.z / geo.y - slope.y));
    let lace = smoothstep(1.0 - in.foam, 1.0 - in.foam + 0.18, r.z) * step(0.02, in.foam);
    c = shade_water(n, in.rel, in.xz, in.position.xy, in.rel.y + frame.eye.y, 0.01 * rough,
                    0.04, o, lace);
  }
  var out: FragmentOut;
  out.colour = vec4f(haze(c, uv, dist), 1.0);
  out.motion = vec4f(screen_motion(frame, vec4f(in.rel, 1.0), in.position.xy), 0.0, 0.0);
  return out;
}
