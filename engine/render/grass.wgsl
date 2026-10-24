// THE GRASS DRAWN (render/grass.hpp): one triangle strip a blade, instanced off the scatter's
// lists (`vs_near` with `cover.z` segments, `vs_far` with `cover.w`). Each vertex reads the air at
// its blade's root, bends the blade's Bézier (grass_common.wgsl's `blade_shape`), steps to its
// share of the curve and out to its side by the blade's width tapering to the tip, never under
// about a pixel (a blade edge-on is widened toward the eye's side, so the field never shimmers to
// threads). Lit two-sided: the sun past the ground's horizon and the shadow maps on the face
// toward the eye, the sun through the blade from behind (translucency, strongest at the tips), the
// sky's light dimmed toward the root (a dense field shades its own floor), and a faint sheen.
// Hazed; the motion target written as a solid surface's.
#include "render/wgsl/frame.wgsl"
#include "render/wgsl/terrain_common.wgsl"
#include "render/wgsl/sun_shadow.wgsl"
#include "render/wgsl/grass_common.wgsl"

@group(1) @binding(0) var<uniform> ground: TerrainParams;
@group(1) @binding(1) var horizon0: texture_2d<f32>;
@group(1) @binding(2) var horizon1: texture_2d<f32>;

@group(2) @binding(0) var<uniform> gp: GrassParams;
@group(2) @binding(1) var<storage, read> kinds: array<GrassKind>;
@group(2) @binding(2) var<storage, read> near_blades: array<Blade>;
@group(2) @binding(3) var<storage, read> far_blades: array<Blade>;
@group(2) @binding(4) var<storage, read> grass_wind: array<vec2f>;

@group(3) @binding(0) var<uniform> shadow: SunShadow;
@group(3) @binding(1) var shadow_maps: texture_depth_2d_array;
@group(3) @binding(2) var shadow_cmp: sampler_comparison;

struct VertexOut {
  @builtin(position) position: vec4f,
  @location(0) rel: vec3f,                      // camera-relative, m
  @location(1) normal: vec3f,                   // the blade's face, rounded across it
  @location(2) along: f32,                      // 0 at the root .. 1 at the tip
  @location(3) @interpolate(flat) colour: vec3f, // the tip's linear albedo
  @location(4) @interpolate(flat) root: vec3f,   // the root's
}

fn blade_vertex(b: Blade, vid: u32, segments: u32) -> VertexOut {
  var out: VertexOut;
  let kind = b.seed >> 24u;
  let k = kinds[kind];
  let t = gp.mean.z;
  let air = wind_at_blade(b.root.x, b.root.z, t);
  let s = blade_shape(k, b, air, t);
  let seg = vid / 2u;
  let side = f32(vid & 1u) * 2.0 - 1.0;
  let u = min(f32(seg) / f32(segments), 1.0);
  let p = bezier_at(s, u);
  let tangent = normalize(bezier_tangent(s, u) + vec3f(0.0, 1e-5, 0.0));
  // The blade's face: across it a hashed plan direction, square to the tangent.
  let hf = grass_hash(b.seed ^ 0x165667b1u);
  var across = normalize(vec3f(grass_unit(hf) * 2.0 - 1.0, 0.0,
                               grass_unit(grass_hash(hf)) * 2.0 - 1.0) + vec3f(1e-4, 0.0, 0.0));
  across = normalize(across - tangent * dot(across, tangent) + vec3f(0.0, 0.0, 1e-5));
  let rel_p = p - frame.eye.xyz;
  let to_eye = -normalize(rel_p);
  var face = normalize(cross(tangent, across));
  // Edge-on to the eye: turn the width toward the eye's side, so a blade never thins to a thread.
  let edge = 1.0 - abs(dot(face, to_eye));
  let view_side = normalize(cross(to_eye, tangent) + vec3f(1e-5, 0.0, 0.0));
  across = normalize(mix(across, view_side * sign(dot(view_side, across) + 1e-6), edge * 0.6));
  // Never under about a pixel across, so the far field keeps its cover and does not alias away.
  let pixel = length(rel_p) * 2.0 / (frame.size.y * abs(frame.view_proj[1][1]));
  let width = max(b.width * (1.0 - u * u), pixel * 0.7 * (1.0 - u));
  let corner = rel_p + across * (side * 0.5 * width);
  out.position = frame.view_proj * vec4f(corner, 1.0);
  out.rel = corner;
  face = normalize(cross(tangent, across));
  out.normal = normalize(face + across * (side * 0.45));
  out.along = u;
  let hc = grass_hash(b.seed ^ 0x2545f491u);
  var tip = k.tip.xyz;
  if (grass_unit(hc) < k.air.z) {
    tip = k.dry_tip.xyz;
  }
  let shade = 0.8 + 0.4 * grass_unit(grass_hash(hc));
  out.colour = tip * shade;
  out.root = k.root.xyz * shade;
  return out;
}

@vertex
fn vs_near(@builtin(vertex_index) vid: u32, @builtin(instance_index) iid: u32) -> VertexOut {
  return blade_vertex(near_blades[iid], vid, gp.cover.z);
}

@vertex
fn vs_far(@builtin(vertex_index) vid: u32, @builtin(instance_index) iid: u32) -> VertexOut {
  return blade_vertex(far_blades[iid], vid, gp.cover.w);
}

struct FragmentOut {
  @location(0) colour: vec4f,
  @location(1) motion: vec4f,
}

@fragment
fn fs_main(in: VertexOut) -> FragmentOut {
  let to_eye = -normalize(in.rel);
  var n = normalize(in.normal);
  if (dot(n, to_eye) < 0.0) {
    n = -n;
  }
  let y = in.rel.y + frame.eye.y;
  let xz = in.rel.xz + frame.eye.xz;
  let l = frame.sun.xyz;
  let lit = ground_shadow(ground, horizon0, horizon1, linear_clamp, l, xz) *
            sun_shadow(shadow, shadow_maps, shadow_cmp, in.rel, n, in.position.xy, frame.time.y);
  let sun = sun_light(y, xz) * lit;
  let albedo = mix(in.root, in.colour, pow(in.along, 0.7));
  // A dense field shades its own floor: the sky reaches the root a quarter as well as the tip.
  let floor_shade = mix(0.25, 1.0, smoothstep(0.0, 0.9, in.along));
  let facing = max(dot(n, l), 0.0);
  let through = max(dot(-n, l), 0.0) * 0.55 * mix(0.4, 1.0, in.along);
  let sheen = pow(max(dot(reflect(-l, n), to_eye), 0.0), 18.0) * 0.06 * in.along;
  let c = albedo / PI * (sun * (facing * floor_shade + through) + sky_irradiance(n) * floor_shade) +
          sun * sheen;
  var out: FragmentOut;
  out.colour = vec4f(haze(c, in.position.xy * frame.size.zw, length(in.rel)), 1.0);
  out.motion = vec4f(screen_motion(frame, vec4f(in.rel, 1.0), in.position.xy), 0.0, 0.0);
  return out;
}
