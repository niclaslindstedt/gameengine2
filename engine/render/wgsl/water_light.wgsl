// THE WATER'S LIGHT (render/water.hpp, render/water_stream.hpp): what a pixel of water shows,
// shared by the still bodies' surface and the moving water's (rivers, flow regions, curtains).
// The including shader binds `scene_colour` (the opaque frame's colour) and `scene_depth` (its
// depth, R32F) before including this, beside render/wgsl/frame.wgsl.
//
// Fresnel (Schlick at n = 1.333) between what the surface mirrors along the normal (the opaque
// scene marched in screen space through the depth copy, else the sky's own model) and the water
// seen into: the scene behind bent by the normal and dimmed over the view's path and the light's
// way down to the bed, over the light the water itself sends back; the sun's glitter (Beckmann
// over the slopes the pixel cannot resolve); a body's glow; and foam, a white lit face over it
// all by its share.

// A body's optics, three bands (water/optics.hpp).
struct Optics {
  c: vec3f,         // beam attenuation, 1/m
  kd: vec3f,        // the daylight's diffuse attenuation, 1/m
  deep: vec3f,      // deep water's colour, pi Rrs
  emission: vec3f,  // its own glow
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

// The colour of a pixel of water before the haze: its normal `n`, its camera-relative place
// `rel`, its world plan place `xz`, its pixel `position`, the surface's level over the bed under
// it, the slope variance the pixel cannot resolve (`lost`), how far the normal bends the view in,
// the body's optics, and the foam's share.
fn shade_water(n: vec3f, rel: vec3f, xz: vec2f, position: vec2f, level: f32, lost: f32,
               refract: f32, o: Optics, foam: f32) -> vec3f {
  let dist = length(rel);
  let v = -rel / dist;
  let cos_v = max(dot(n, v), 1e-3);
  let world_y = rel.y + frame.eye.y;

  // What the surface mirrors: the opaque scene along the reflected ray where the screen holds
  // it, else the sky (kept over the horizon).
  let r0 = reflect(-v, n);
  let r = normalize(vec3f(r0.x, max(r0.y, 0.01), r0.z));
  let mirrored = reflected_scene(rel, normalize(vec3f(r0.x, max(r0.y, 0.002), r0.z)),
                                 max(0.02 * dist, 0.25));
  let sky = mix(sky_luminance(r), mirrored.rgb, mirrored.a);

  // Into the water: the scene behind the surface, bent by the normal.
  let size = vec2i(frame.size.xy);
  let pixel = vec2i(position);
  let uv = position * frame.size.zw;
  var behind = scene_rel(pixel, uv);
  var depth_view = max(length(behind.xyz) - dist, 0.0);
  let bend = n.xz * refract * min(depth_view, 4.0) / max(dist, 1.0);
  let uv2 = clamp(uv + bend, vec2f(0.0), vec2f(1.0));
  let pixel2 = clamp(vec2i(uv2 * frame.size.xy), vec2i(0), size - vec2i(1));
  let behind2 = scene_rel(pixel2, uv2);
  var at = uv;
  if (length(behind2.xyz) > dist) {
    behind = behind2;
    at = uv2;
    depth_view = max(length(behind2.xyz) - dist, 0.0);
  }
  let bed_depth = max(level - (behind.y + frame.eye.y), 0.0);
  let through = exp(-o.c * depth_view) * exp(-o.kd * bed_depth);
  let seen = textureSampleLevel(scene_colour, linear_clamp, at, 0.0).rgb;
  let sun = sun_light(world_y, xz);
  let light = sun * max(frame.sun.y, 0.0) + sky_irradiance(vec3f(0.0, 1.0, 0.0));
  let own = o.deep * light / PI;
  var into = seen * through + own * (1.0 - through);
  into += o.emission * (0.8 * frame.world.y / PI);

  // The sun's glitter over the slopes the pixel cannot resolve.
  let h = normalize(frame.sun.xyz + v);
  let mss = max(lost, 2e-4);
  let glint = sun * fresnel(dot(v, h)) * beckmann(dot(n, h), mss) / (4.0 * cos_v) *
              step(0.0, frame.sun.y) * step(0.0, dot(n, frame.sun.xyz));

  // Fresnel, feathered to nothing in the last centimetres at the shore.
  let f = fresnel(cos_v) * smoothstep(0.0, 0.15, depth_view);
  let c = mix(into, sky, f) + glint * smoothstep(0.0, 0.15, depth_view);
  // Foam: a white face lit by the sun on its normal and the sky.
  let lit = sun * max(dot(n, frame.sun.xyz), 0.0) + sky_irradiance(n);
  return mix(c, 0.85 * lit / PI, clamp(foam, 0.0, 1.0));
}
