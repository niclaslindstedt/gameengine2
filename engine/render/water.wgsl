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
// never shimmers.
//
// THE LIGHT: Fresnel (Schlick at n = 1.333) between what the surface mirrors along the wave's
// normal (the opaque scene, marched in screen space through the depth copy, where the screen
// holds it; the sky's own model elsewhere) and the water seen into: the scene behind the surface (bent by the normal, falling back to
// the straight view where the bent ray lands in front of the water) dimmed by the light's way
// down to the bed and back up (exp(-Kd h), exp(-c d)), over the light the water itself sends
// back (its deep colour, pi Rrs, under the sun and the sky) where the view sees no bed; the
// sun's glitter (Beckmann over the unresolved slopes); a body's own glow; then the haze. The
// optics are the body's (water/optics.hpp), three bands.
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
  levels: array<vec4f, 12>,  // origin x, z, spacing, 0
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

// The scene behind the surface at a pixel: its camera-relative position (infinitely far where
// nothing opaque was drawn: the sky).
fn scene_rel(pixel: vec2i, uv: vec2f) -> vec4f {
  let z = textureLoad(scene_depth, pixel, 0).r;
  if (z <= 0.0) {
    return vec4f(view_ray(frame, uv) * 1e6, 0.0);
  }
  let ndc = vec2f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
  let p = frame.inv_view_proj * vec4f(ndc, z, 1.0);
  return vec4f(p.xyz / p.w, 1.0);
}

// The opaque scene along the reflected ray `r` from the surface at `rel` (camera-relative), by
// marching it in steps growing from `first` m across the depth copy: the colour where the ray
// first passes behind a surface, the crossing found by halving and kept where it lies on that
// surface (a ray passing behind a trunk goes on), and its weight
// (faded at the screen's top and bottom and the march's end; 0 where it leaves them or reaches
// only the sky, which the caller draws from the sky's own model).
fn reflected_scene(rel: vec3f, r: vec3f, first: f32) -> vec4f {
  if (r.y <= 0.0) {
    return vec4f(0.0);
  }
  let size = vec2i(frame.size.xy);
  var step = first;
  var prev = rel;
  for (var i = 0; i < 48; i++) {
    let p = prev + r * step;
    let c = frame.view_proj * vec4f(p, 1.0);
    if (c.w <= 0.0) {
      break;
    }
    let ndc = c.xyz / c.w;
    // Off the top or the bottom the march ends; off a side it goes on along the edge's column
    // (what lies just past the edge is most like the edge: no hole where the view is narrow).
    let uv = vec2f(clamp(ndc.x * 0.5 + 0.5, 0.0, 1.0), 0.5 - ndc.y * 0.5);
    if (uv.y < 0.0 || uv.y > 1.0) {
      break;
    }
    let pixel = clamp(vec2i(uv * frame.size.xy), vec2i(0), size - vec2i(1));
    let z = textureLoad(scene_depth, pixel, 0).r;
    if (z > ndc.z) {
      // Halve between the last point in front and this one behind, to where it crossed.
      var a = prev;
      var b = p;
      for (var k = 0; k < 6; k++) {
        let m = 0.5 * (a + b);
        let cm = frame.view_proj * vec4f(m, 1.0);
        let um = clamp(vec2f(cm.x / cm.w * 0.5 + 0.5, 0.5 - cm.y / cm.w * 0.5), vec2f(0.0),
                       vec2f(1.0));
        let pm = clamp(vec2i(um * frame.size.xy), vec2i(0), size - vec2i(1));
        if (textureLoad(scene_depth, pm, 0).r > cm.z / cm.w) {
          b = m;
        } else {
          a = m;
        }
      }
      let cb = frame.view_proj * vec4f(b, 1.0);
      let ub = clamp(vec2f(cb.x / cb.w * 0.5 + 0.5, 0.5 - cb.y / cb.w * 0.5), vec2f(0.0),
                     vec2f(1.0));
      let pb = clamp(vec2i(ub * frame.size.xy), vec2i(0), size - vec2i(1));
      // A hit where the crossing lies on the surface; else the ray passed behind something
      // thinner than the march saw (a trunk), and goes on.
      let gap = length(b) - length(scene_rel(pb, ub).xyz);
      if (gap < max(0.04 * length(b), 0.5)) {
        let edge = min(ub.y, 1.0 - ub.y);
        let w = smoothstep(0.0, 0.06, edge) * (1.0 - smoothstep(36.0, 48.0, f32(i)));
        return vec4f(textureSampleLevel(scene_colour, linear_clamp, ub, 0.0).rgb, w);
      }
    }
    prev = p;
    step *= 1.15;
  }
  return vec4f(0.0);
}

// Beckmann's distribution of the facets' normals for a mean square slope `mss`.
fn beckmann(cos_h: f32, mss: f32) -> f32 {
  let c2 = max(cos_h * cos_h, 1e-4);
  let tan2 = (1.0 - c2) / c2;
  return exp(-tan2 / mss) / (PI * mss * c2 * c2);
}

fn fresnel(cos_t: f32) -> f32 {
  let f = 1.0 - clamp(cos_t, 0.0, 1.0);
  return 0.02 + 0.98 * f * f * f * f * f;
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
  let n = normalize(vec3f(-slope.x, 1.0, -slope.y));
  let dist = length(in.rel);
  let v = -in.rel / dist;
  let cos_v = max(dot(n, v), 1e-3);
  let world_y = in.rel.y + frame.eye.y;

  // What the surface mirrors: the opaque scene along the reflected ray where the screen holds
  // it, else the sky (kept over the horizon).
  let r0 = reflect(-v, n);
  let r = normalize(vec3f(r0.x, max(r0.y, 0.01), r0.z));
  let mirrored = reflected_scene(in.rel, normalize(vec3f(r0.x, max(r0.y, 0.002), r0.z)),
                                 max(0.02 * dist, 0.25));
  // A body under a roof (a cave's pool) sees the sky through `open` of its dome (rough.w) and the
  // room's own light, a share of a white face's (emission.w), over the rest.
  let open = body.rough.w;
  let room = vec3f(white_luminance() * body.emission.w);
  let sky = mix(mix(room, sky_luminance(r), open), mirrored.rgb, mirrored.a);

  // Into the water: the scene behind the surface, bent by the normal.
  let size = vec2i(frame.size.xy);
  let pixel = vec2i(in.position.xy);
  let uv = in.position.xy * frame.size.zw;
  var behind = scene_rel(pixel, uv);
  var depth_view = max(length(behind.xyz) - dist, 0.0);
  let bend = n.xz * body.rough.z * min(depth_view, 4.0) / max(dist, 1.0);
  let uv2 = clamp(uv + bend, vec2f(0.0), vec2f(1.0));
  let pixel2 = clamp(vec2i(uv2 * frame.size.xy), vec2i(0), size - vec2i(1));
  let behind2 = scene_rel(pixel2, uv2);
  var at = uv;
  if (length(behind2.xyz) > dist) {
    behind = behind2;
    at = uv2;
    depth_view = max(length(behind2.xyz) - dist, 0.0);
  }
  let bed_depth = max(body.info.x - (behind.y + frame.eye.y), 0.0);
  let through = exp(-body.c.rgb * depth_view) * exp(-body.kd.rgb * bed_depth);
  let seen = textureSampleLevel(scene_colour, linear_clamp, at, 0.0).rgb;
  let sun = sun_light(world_y, in.xz) * open;
  let light = sun * max(frame.sun.y, 0.0) + sky_irradiance(vec3f(0.0, 1.0, 0.0)) * open +
              room * PI;
  let own = body.deep.rgb * light / PI;
  var into = seen * through + own * (1.0 - through);
  into += body.emission.rgb * (0.8 * frame.world.y / PI);

  // The sun's glitter over the slopes the pixel cannot resolve.
  let h = normalize(frame.sun.xyz + v);
  let mss = max(lost, 2e-4);
  let glint = sun * fresnel(dot(v, h)) * beckmann(dot(n, h), mss) / (4.0 * cos_v) *
              step(0.0, frame.sun.y) * step(0.0, dot(n, frame.sun.xyz));

  // Fresnel, feathered to nothing in the last centimetres at the shore.
  let f = fresnel(cos_v) * smoothstep(0.0, 0.15, depth_view);
  let c = mix(into, sky, f) + glint * smoothstep(0.0, 0.15, depth_view);
  var out: FragmentOut;
  out.colour = vec4f(haze(c, uv, dist), 1.0);
  out.motion = vec4f(screen_motion(frame, vec4f(in.rel, 1.0), in.position.xy), 0.0, 0.0);
  return out;
}
