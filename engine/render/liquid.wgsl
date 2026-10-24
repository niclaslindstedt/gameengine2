// LAVA AND SLIME (render/liquid.hpp; docs/liquids.md § Drawn): a body's flat surface drawn over
// the opaque scene by its look, a grid of quads over its box at its level (the slime's ripples
// lifting its vertices).
//
// LAVA: the surface's temperature is worked out per pixel and its light read from the glow
// table (a black body's radiance by temperature, cd/m^2, water/glow.hpp). The crust is a Voronoi
// tiling of plates, carried by the flow on two phases of a flow map (each phase's pattern drawn
// back along the flow for its share of the period and the two crossfaded, so it never
// stretches); a plate's age (hashed, running on at `age_rate`) cools its skin and widens its
// cracks, where the melt's heat shows, falling off exp(-d / w) from the crack. Open patches of
// melt where a slow noise passes `cover`. The crust's height (plates over cracks, the ropy folds
// across the flow) gives its normal; the crust and the melt are glasses of their roughness under
// the sun and the sky, and the glow is added. Detail under a pixel is filtered: a crack narrower
// than the pixel's footprint is widened to it with its heat spread over the width.
//
// SLIME: a turbid volume; its own colour the Kubelka-Munk reflectance of a thick layer from K
// and S, the scene behind seen through exp(-(K + S) d), a scum over it, ripples, bubbles that
// swell and burst into rings, an oily film's interference colours on its reflection, its gloss
// and its own glow.
#include "render/wgsl/frame.wgsl"

override kGrid: u32 = 96u;

struct LiquidBody {
  info: vec4f,
  box: vec4f,
  flow: vec4f,
  lava0: vec4f,   // core K, crust_hot K, crust_cold K, cover
  lava1: vec4f,   // plate, crack, ropes, age_rate
  lava2: vec4f,   // emissivity, crust_rough, melt_rough
  crust: vec4f,
  absorb: vec4f,
  scatter: vec4f,
  glow: vec4f,
  slime0: vec4f,  // rough, film nm, scum, bubbles
  slime1: vec4f,  // bubble size, grime, debris a m^2
  scum: vec4f,
  table: array<vec4f, 32>,
}

struct Ripple {
  dir: vec4f,   // kx, kz, amplitude, angular frequency
  more: vec4f,  // phase, k
}

@group(1) @binding(0) var<uniform> body: LiquidBody;
@group(1) @binding(1) var<storage, read> ripples: array<Ripple>;
@group(1) @binding(2) var scene_colour: texture_2d<f32>;
@group(1) @binding(3) var scene_depth: texture_2d<f32>;

const kGlowLo = 600.0;
const kGlowHi = 1700.0;
const kSunLuxCd = 128000.0;

struct VertexOut {
  @builtin(position) position: vec4f,
  @location(0) rel: vec3f,
  @location(1) xz: vec2f,
}

fn ripple_height(xz: vec2f, t: f32) -> f32 {
  var h = 0.0;
  let n = u32(body.info.z);
  for (var i = 0u; i < n; i++) {
    let r = ripples[i];
    h += r.dir.z * sin(dot(r.dir.xy, xz) - r.dir.w * t + r.more.x);
  }
  return h;
}

@vertex
fn vs_main(@builtin(vertex_index) vid: u32) -> VertexOut {
  let n = kGrid + 1u;
  let g = vec2f(f32(vid % n), f32(vid / n)) / f32(kGrid);
  let xz = mix(body.box.xy, body.box.zw, g);
  let y = body.info.x + ripple_height(xz, frame.time.x);
  var out: VertexOut;
  out.rel = vec3f(xz.x, y, xz.y) - frame.eye.xyz;
  out.position = frame.view_proj * vec4f(out.rel, 1.0);
  out.xz = xz;
  return out;
}

// A grimy pool's dirt: silt and rot, a dark brown.
const kGrimeAlbedo = vec3f(0.05, 0.04, 0.025);

// HASHES AND NOISE.
fn hash2(p: vec2i) -> vec2f {
  var h = vec2u(bitcast<u32>(p.x), bitcast<u32>(p.y));
  h = h * vec2u(1664525u, 1013904223u) + vec2u(1013904223u, 1664525u);
  h.x += h.y * 1664525u;
  h.y += h.x * 1664525u;
  h = h ^ (h >> vec2u(16u));
  h.x += h.y * 1664525u;
  h.y += h.x * 1664525u;
  h = h ^ (h >> vec2u(16u));
  return vec2f(h) * (1.0 / 4294967296.0);
}

fn value_noise(p: vec2f) -> f32 {
  let i = vec2i(floor(p));
  let f = fract(p);
  let u = f * f * (3.0 - 2.0 * f);
  let a = hash2(i).x;
  let b = hash2(i + vec2i(1, 0)).x;
  let c = hash2(i + vec2i(0, 1)).x;
  let d = hash2(i + vec2i(1, 1)).x;
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

fn fbm(p: vec2f) -> f32 {
  var s = 0.0;
  var a = 0.5;
  var q = p;
  for (var i = 0; i < 4; i++) {
    s += a * value_noise(q);
    q = q * 2.03 + vec2f(17.1, 9.7);
    a *= 0.5;
  }
  return s / 0.9375;
}

// A Voronoi cell at `p` (in cells): x the distance to the border (half of F2 - F1), y the cell's
// hash, zw the offset to its point.
fn voronoi(p: vec2f) -> vec4f {
  let i = vec2i(floor(p));
  let f = fract(p);
  var f1 = 8.0;
  var f2 = 8.0;
  var id = 0.0;
  var off = vec2f(0.0);
  for (var y = -1; y <= 1; y++) {
    for (var x = -1; x <= 1; x++) {
      let c = vec2i(x, y);
      let h = hash2(i + c);
      let d = vec2f(c) + 0.15 + 0.7 * h - f;
      let l = length(d);
      if (l < f1) {
        f2 = f1;
        f1 = l;
        id = fract(h.x * 7.31 + h.y * 3.17);
        off = d;
      } else if (l < f2) {
        f2 = l;
      }
    }
  }
  return vec4f(0.5 * (f2 - f1), id, off);
}

// THE LAVA'S SURFACE at plan point `q` (the flow taken out), its pixel footprint `foot` (m):
// x the temperature (K), y the height (m) for the normal, z the share that is open melt.
fn lava_at(q: vec2f, t: f32, foot: f32) -> vec3f {
  let core = body.lava0.x;
  let hot = body.lava0.y;
  let cold = body.lava0.z;
  let plate = body.lava1.x;
  // The open melt: where a slow noise (spread to about even over 0..1) passes the cover.
  let field = smoothstep(0.28, 0.72, fbm(q / (plate * 7.0) + vec2f(t * 0.004, -t * 0.003)));
  let cover = body.lava0.w;
  let open = smoothstep(cover - 0.03, cover + 0.03, field);
  // The plates, warped so no two are alike: an age per plate, running on and renewed; each
  // plate broken again into smaller ones along fainter, narrower cracks.
  let warp = vec2f(fbm(q / plate * 0.7), fbm(q / plate * 0.7 + vec2f(5.2, 1.3))) - 0.5;
  let pq = q / plate + 0.6 * warp;
  let v = voronoi(pq);
  let vs = voronoi(pq * 2.7 + vec2f(3.1, 7.7));
  let age = fract(v.y + t * body.lava1.w);
  // Plates near the open melt are young.
  let young = smoothstep(cover - 0.2, cover, field);
  let a = age * (1.0 - 0.85 * young);
  let skin = mix(hot, cold, sqrt(a));
  // The crack's heat off its line, its width growing with the plate's age, never under the
  // pixel (its heat then spread over the pixel's width).
  let w = body.lava1.y * (0.4 + 1.2 * a);
  let wf = max(w, foot);
  let edge = v.x * plate;
  let ws = 0.4 * w;
  let wsf = max(ws, foot);
  let crack = max(exp(-edge / wf) * (w / wf),
                  0.8 * exp(-vs.x * plate / 2.7 / wsf) * (ws / wsf) * (1.0 - a));
  var temp = skin + (core - skin) * crack;
  // The melt's own skin: cooler swirls where a film has begun to form, wrinkled into threads
  // drawn out along the flow, and hotter streaks where fresh melt wells up between them.
  let swirl = fbm(q * 0.9 + 0.8 * warp + vec2f(0.0, t * 0.05));
  let threads = abs(sin(9.0 * fbm(q * 2.3 + 1.5 * warp) + 3.0 * swirl));
  let thread_seen = smoothstep(0.02, 0.08, 0.25 / max(foot, 1e-3) * 0.01);
  let skin_on = smoothstep(0.42, 0.78, swirl);
  let melt = core + 45.0 * (1.0 - smoothstep(0.2, 0.5, swirl)) - 260.0 * skin_on -
             90.0 * (1.0 - threads) * skin_on * thread_seen;
  temp = mix(temp, max(melt, temp), open);
  // The height: plates stand over their cracks, the ropy folds across the flow on the crust.
  let flow = body.flow.xy;
  let along = select(vec2f(0.0, 1.0), normalize(flow), length(flow) > 1e-4);
  let rope_len = 0.12 * plate;
  let ropes = body.lava1.z * 0.02 * sin(6.2831853 * (dot(q, along) / rope_len +
              1.5 * fbm(q * 0.8))) * smoothstep(rope_len, 2.0 * rope_len, 3.0 * foot);
  let stand = 0.05 * plate * smoothstep(0.0, 1.5 * w, edge) * (0.5 + 0.5 * a);
  // The rind's own roughness: a fine knobbly texture where the pixel can hold it.
  let knobs = 0.006 * fbm(q * 14.0) * smoothstep(0.08, 0.02, foot);
  let h = (1.0 - open) * (stand + ropes + knobs);
  return vec3f(temp, h, open);
}

// The surface on the flow map's two phases, crossfaded.
fn lava_flowing(p: vec2f, t: f32, foot: f32) -> vec3f {
  let period = 8.0;
  let flow = body.flow.xy;
  let ph0 = fract(t / period);
  let ph1 = fract(t / period + 0.5);
  let s0 = lava_at(p - flow * ph0 * period + vec2f(31.0, 17.0) * floor(t / period), t, foot);
  let s1 = lava_at(p - flow * ph1 * period + vec2f(11.0, 43.0) * floor(t / period + 0.5) +
                   vec2f(5.3, 9.1), t, foot);
  let k = abs(1.0 - 2.0 * ph0);
  return mix(s1, s0, k);
}

// The glow of a surface at `kelvin`, in the frame's light.
fn glow_at(kelvin: f32) -> vec3f {
  let x = clamp((kelvin - kGlowLo) / (kGlowHi - kGlowLo), 0.0, 1.0) * 31.0;
  let i = u32(floor(x));
  let j = min(i + 1u, 31u);
  let c = mix(body.table[i].rgb, body.table[j].rgb, fract(x));
  // Under the table's first entry the glow falls on toward nothing.
  let below = clamp(kelvin / kGlowLo, 0.0, 1.0);
  return c * pow(below, 24.0) * frame.world.y / kSunLuxCd;
}

// GGX's specular for a roughness `r` with Schlick's Fresnel from `f0`.
fn ggx(n: vec3f, v: vec3f, l: vec3f, r: f32, f0: f32) -> f32 {
  let h = normalize(v + l);
  let nh = max(dot(n, h), 0.0);
  let nv = max(dot(n, v), 1e-3);
  let nl = max(dot(n, l), 0.0);
  let a2 = max(r * r * r * r, 1e-5);
  let d = a2 / (PI * pow(nh * nh * (a2 - 1.0) + 1.0, 2.0));
  let k = r * r * 0.5;
  let g = nv / (nv * (1.0 - k) + k) * nl / (nl * (1.0 - k) + k);
  let f = f0 + (1.0 - f0) * pow(1.0 - max(dot(v, h), 0.0), 5.0);
  return d * g * f / (4.0 * nv);
}

fn fresnel(cos_t: f32, f0: f32) -> f32 {
  return f0 + (1.0 - f0) * pow(1.0 - clamp(cos_t, 0.0, 1.0), 5.0);
}

// The scene behind a pixel: its camera-relative distance (huge for the sky).
fn behind_distance(pixel: vec2i, uv: vec2f) -> f32 {
  let z = textureLoad(scene_depth, pixel, 0).r;
  if (z <= 0.0) {
    return 1e6;
  }
  let ndc = vec2f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
  let p = frame.inv_view_proj * vec4f(ndc, z, 1.0);
  return length(p.xyz / p.w);
}

// A thin film's reflectance at three wavelengths (650, 550, 450 nm) for a thickness `d` nm of
// index 1.45 seen at `cos_v`: the two surfaces' reflections interfering, normalised to 1 mean.
fn film_tint(d: f32, cos_v: f32) -> vec3f {
  let cos_t = sqrt(max(1.0 - (1.0 - cos_v * cos_v) / (1.45 * 1.45), 0.0));
  let path = 4.0 * PI * 1.45 * d * cos_t;
  let r = 1.0 - cos(vec3f(path / 650.0, path / 550.0, path / 450.0));
  return r;
}

// One layer of bubbles at plan `p` in cells of `cell` m: the dome's height and slope (xyz: h,
// dh/dx, dh/dz) and how near its rim the point is (w, 1 at the rim); the rings of burst bubbles
// add their slope to yz. Each cell blows one bubble every 1 / (rate cell^2) s, at a point of its
// own each time, which swells over the first part of its period and bursts into a ring.
fn bubbles_at(p: vec2f, t: f32, cell: f32, size: f32, rate: f32) -> vec4f {
  let q = p / cell;
  let i = vec2i(floor(q));
  var out = vec4f(0.0);
  for (var y = -1; y <= 1; y++) {
    for (var x = -1; x <= 1; x++) {
      let c = i + vec2i(x, y);
      let h = hash2(c);
      let h2 = hash2(c + vec2i(57, 113));
      let period = 1.0 / max(rate * cell * cell, 1e-4);
      let s = t / period + h.x;
      let k = floor(s);
      let ph = fract(s);
      let at = (vec2f(c) + 0.2 + 0.6 * hash2(c + vec2i(i32(k), 7))) * cell;
      let d = p - at;
      let dist = max(length(d), 1e-5);
      let life = 0.25 + 0.5 * h2.x;
      if (ph < life) {
        let r = size * (0.4 + 0.6 * h2.y) * sqrt(ph / life);
        if (dist < r) {
          let hh = sqrt(r * r - dist * dist);
          let lift = 0.7;
          out.x = max(out.x, lift * hh);
          out.y += -lift * d.x / max(hh, 0.08 * r);
          out.z += -lift * d.y / max(hh, 0.08 * r);
          out.w = max(out.w, smoothstep(0.6 * r, r, dist));
        }
      } else {
        // The ring: a raised crest running out from the burst and dying away.
        let since = (ph - life) * period;
        let radius = size * (1.0 + 6.0 * since);
        let width = 0.35 * size;
        let u = (dist - radius) / width;
        let a = 0.25 * size * exp(-since * 2.5);
        let slope = a * exp(-u * u) * (-2.0 * u / width);
        out.y += slope * d.x / dist;
        out.z += slope * d.y / dist;
      }
    }
  }
  return out;
}

struct FragmentOut {
  @location(0) colour: vec4f,
  @location(1) motion: vec4f,
}

@fragment
fn fs_main(in: VertexOut) -> FragmentOut {
  let foot = max(length(dpdx(in.xz)), length(dpdy(in.xz)));
  if (any(in.xz < body.box.xy) || any(in.xz > body.box.zw)) {
    discard;
  }
  let t = frame.time.x;
  let dist = length(in.rel);
  let v = -in.rel / dist;
  let world_y = in.rel.y + frame.eye.y;
  // A body under a roof sees the sky through `open` of its dome and the room's light, a share of
  // a white face's, over the rest (flow.z, flow.w).
  let open_sky = body.flow.z;
  let room = vec3f(white_luminance() * body.flow.w);
  let sun = sun_light(world_y, in.xz) * step(0.0, frame.sun.y) * open_sky;
  let pixel = vec2i(in.position.xy);
  let uv = in.position.xy * frame.size.zw;
  let depth_view = max(behind_distance(pixel, uv) - dist, 0.0);
  let feather = smoothstep(0.0, 0.04, depth_view);
  var c = vec3f(0.0);

  if (body.info.y < 0.5) {
    // LAVA.
    let e = max(foot, 0.01);
    let s = lava_flowing(in.xz, t, foot);
    let sx = lava_flowing(in.xz + vec2f(e, 0.0), t, foot);
    let sz = lava_flowing(in.xz + vec2f(0.0, e), t, foot);
    let n = normalize(vec3f(-(sx.y - s.y) / e, 1.0, -(sz.y - s.y) / e));
    let open = s.z;
    let rough = mix(body.lava2.y, body.lava2.z, open);
    let cos_v = max(dot(n, v), 1e-3);
    let sky_d = sky_irradiance(n) * open_sky + room * PI;
    let lit = body.crust.rgb * (sun * max(dot(n, frame.sun.xyz), 0.0) + sky_d) / PI;
    let r = reflect(-v, n);
    let mirror = mix(room, sky_luminance(normalize(vec3f(r.x, max(r.y, 0.01), r.z))), open_sky) *
                 fresnel(cos_v, 0.04) * (1.0 - rough) * (1.0 - rough);
    let spec = sun * ggx(n, v, frame.sun.xyz, rough, 0.04);
    c = lit * (1.0 - open) + mirror + spec + glow_at(s.x) * body.lava2.x;
  } else {
    // SLIME.
    var slope = vec2f(0.0);
    let nr = u32(body.info.z);
    for (var i = 0u; i < nr; i++) {
      let w = ripples[i];
      let k = w.more.y;
      let keep = smoothstep(2.0 * foot, 4.0 * foot, 6.2831853 / k);
      let phi = dot(w.dir.xy, in.xz) - w.dir.w * t + w.more.x;
      slope += keep * w.dir.z * cos(phi) * w.dir.xy;
    }
    // The bubbles: two sizes, faded where they are under a few pixels.
    let size = body.slime1.x;
    let rate = body.slime0.w;
    var dome = 0.0;
    var rim = 0.0;
    if (rate > 0.0) {
      let b1 = bubbles_at(in.xz, t, size * 6.0, size, rate * 0.5);
      let b2 = bubbles_at(in.xz + vec2f(3.7, 1.9), t * 1.13, size * 3.5, size * 0.45, rate * 0.5);
      let seen1 = smoothstep(2.0 * foot, 4.0 * foot, size);
      let seen2 = smoothstep(2.0 * foot, 4.0 * foot, 0.45 * size);
      slope += b1.yz * seen1 + b2.yz * seen2;
      dome = max(step(1e-6, b1.x) * seen1, step(1e-6, b2.x) * seen2);
      rim = max(b1.w * seen1, b2.w * seen2);
    }
    let n0 = vec3f(0.0, 1.0, 0.0);
    let n = normalize(vec3f(-slope.x, 1.0, -slope.y));
    let cos_v = max(dot(n, v), 1e-3);
    // The marbling: the stuff's strength varies in slow swirls carried on the flow.
    let drift = in.xz - body.flow.xy * t;
    let swirl = fbm(drift * 0.15 + vec2f(0.0, t * 0.004));
    let marble = 0.7 + 0.6 * fbm(drift * 0.45 + 2.5 * vec2f(swirl, -swirl));
    // The volume's own colour (Kubelka-Munk) and what is seen through it.
    let K = body.absorb.rgb * marble;
    let S = max(body.scatter.rgb, vec3f(1e-4));
    let ks = K / S;
    let own = 1.0 + ks - sqrt(ks * ks + 2.0 * ks);
    let through = exp(-(K + S) * depth_view);
    let seen = textureLoad(scene_colour, pixel, 0).rgb;
    let light = sun * max(frame.sun.y, 0.0) + sky_irradiance(vec3f(0.0, 1.0, 0.0)) * open_sky +
                room * PI;
    let body_colour = own * (1.0 - through) * light / PI;
    var into = seen * through + body_colour;
    // A bubble's skin is thinner than the pool, so its dome is a little lighter, and darker where
    // it meets the surface.
    into *= 1.0 + dome * (0.5 - 1.1 * rim);
    // The scum: patches drifting with the surface's flow.
    let scum_field = fbm(drift * 0.9) * 0.7 + fbm(drift * 4.0) * 0.3;
    let scum = body.slime0.z * smoothstep(1.0 - body.slime0.z - 0.05, 1.0 - body.slime0.z + 0.1,
                                          scum_field) / max(body.slime0.z, 1e-3);
    let scum_share = clamp(scum, 0.0, 1.0) * step(1e-3, body.slime0.z);
    let scum_lit = body.scum.rgb *
                   (sun * max(dot(n0, frame.sun.xyz), 0.0) + sky_irradiance(n0) * open_sky + room * PI) /
                   PI;
    into = mix(into, scum_lit, scum_share);
    // The grime: silt and rot mottled over the whole pool, darker than the scum and matte.
    var cover = scum_share;
    let light_flat = (sun * max(dot(n0, frame.sun.xyz), 0.0) + sky_irradiance(n0) * open_sky +
                      room * PI) / PI;
    let grime = body.slime1.y;
    if (grime > 0.0) {
      let g = fbm(drift * 2.3 + vec2f(5.1, 7.7)) * 0.6 + fbm(drift * 11.0) * 0.4;
      let dirt = grime * smoothstep(0.55 - 0.5 * grime, 0.85 - 0.3 * grime, g);
      let grime_albedo = mix(kGrimeAlbedo, body.scum.rgb, 0.3 * g);
      into = mix(into, grime_albedo * light_flat, dirt * 0.95);
      cover = max(cover, dirt * 0.8);
    }
    // The debris: twigs, leaves and clumps afloat, drifting with the surface; `slime1.z` a square
    // metre's, each in its own cell, turned and stretched by its hash, faded under a pixel.
    let debris = body.slime1.z;
    if (debris > 0.0) {
      let cell = inverseSqrt(debris);
      let q = drift / cell;
      let k = vec2i(floor(q));
      var fleck = 0.0;
      var fleck_shade = 0.0;
      for (var dy = -1; dy <= 1; dy++) {
        for (var dx = -1; dx <= 1; dx++) {
          let kk = k + vec2i(dx, dy);
          let h = hash2(kk);
          let h2 = hash2(kk + vec2i(71, 13));
          let centre = (vec2f(kk) + h) * cell;
          let ang = h2.x * 6.2831853;
          let axis = vec2f(cos(ang), sin(ang));
          let rel = drift - centre;
          let size = cell * (0.05 + 0.12 * h2.y);
          let along = dot(rel, axis) / size;
          let across = dot(rel, vec2f(-axis.y, axis.x)) / (size * (0.18 + 0.6 * h.y));
          let r2 = along * along + across * across;
          let inside = 1.0 - smoothstep(0.7, 1.0, r2);
          if (inside > fleck) {
            fleck = inside;
            fleck_shade = h2.y;
          }
        }
      }
      let debris_albedo = mix(vec3f(0.035, 0.03, 0.02), vec3f(0.16, 0.12, 0.06), fleck_shade);
      into = mix(into, debris_albedo * light_flat, fleck);
      cover = max(cover, fleck);
    }
    // The glow: its own light, pulsing slowly, brighter in the bubbles.
    let pulse = 1.0 + body.glow.w * (sin(t * 0.7 + fbm(in.xz * 0.3) * 6.0) * 0.5 + 0.6 * dome);
    into += body.glow.rgb * pulse * (1.0 - cover) * frame.world.y / kSunLuxCd;
    // The reflection: the sky's, tinted by the film's interference where an oily film lies.
    let rough = mix(body.slime0.x, 0.8, cover);
    let r = reflect(-v, n);
    var mirror = mix(room, sky_luminance(normalize(vec3f(r.x, max(r.y, 0.01), r.z))), open_sky);
    var tint = vec3f(1.0);
    var tint_spec = vec3f(1.0);
    if (body.slime0.y > 0.0) {
      // The oil lies in patches, a few hundred nm thick, its colours a sheen over the reflection.
      let d = body.slime0.y * (0.6 + 0.8 * fbm(drift * 0.35 + vec2f(t * 0.01, 0.0)));
      let oily = smoothstep(0.42, 0.62, fbm(drift * 0.12 + vec2f(9.1, 3.3))) * (1.0 - cover);
      let film = film_tint(d, cos_v);
      tint = mix(vec3f(1.0), film, 0.55 * oily);
      tint_spec = mix(vec3f(1.0), film, 0.3 * oily);
    }
    let f = fresnel(cos_v, 0.03) * (1.0 - 0.8 * rough);
    let spec = sun * ggx(n, v, frame.sun.xyz, max(rough, 0.03), 0.03) * (1.0 - cover);
    // A bubble's wet skin is smoother than the pool's surface: a small sharp highlight.
    let sparkle = sun * ggx(n, v, frame.sun.xyz, 0.05, 0.03) * dome * (1.0 - cover);
    c = mix(into, mirror * tint, f * (1.0 - cover)) + (spec + sparkle) * tint_spec;
  }
  c = mix(textureLoad(scene_colour, pixel, 0).rgb, c, feather);
  var out: FragmentOut;
  out.colour = vec4f(haze(c, uv, dist), 1.0);
  out.motion = vec4f(screen_motion(frame, vec4f(in.rel, 1.0), in.position.xy), 0.0, 0.0);
  return out;
}
