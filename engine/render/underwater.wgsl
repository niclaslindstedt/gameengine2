// THE VIEW UNDER WATER (render/underwater.hpp): the opaque frame, copied before the water's
// passes, redrawn as an eye under the surface sees it: the water's fog by its beam
// attenuation into its own lit veil (what stands under the surface lit by the day dimmed on its
// way down), the eye's adaptation to the dark, the blur by distance, Snell's window and the mirror past
// it where the view meets the surface first, the wobble and a stylised tint.
#include "render/wgsl/frame.wgsl"

struct Under {
  info: vec4f,  // the level (world m), the eye's depth (m), the blur (px), its range (m)
  c: vec4f,     // beam attenuation, rgb (1/m)
  kd: vec4f,    // daylight's diffuse attenuation, rgb (1/m)
  veil: vec4f,  // the veil's colour at the surface (white = a white face in daylight)
  look: vec4f,  // magnify, warp (of the height), warp rate (Hz), Snell's window on (1)
  tint: vec4f,  // rgb, mix
  fog: vec4f,   // the fog's scale, the eye's adaptation (the veil's luma it lifts to; 0 off)
}

@group(1) @binding(0) var<uniform> under: Under;
@group(1) @binding(1) var scene_colour: texture_2d<f32>;
@group(1) @binding(2) var scene_depth: texture_2d<f32>;  // R32F: the opaque depth

struct VertexOut {
  @builtin(position) position: vec4f,
}

@vertex
fn vs_main(@builtin(vertex_index) i: u32) -> VertexOut {
  let p = vec2f(f32((i << 1u) & 2u), f32(i & 2u)) * 2.0 - 1.0;
  var out: VertexOut;
  out.position = vec4f(p, 0.0, 1.0);
  return out;
}

// The distance to the opaque scene through a uv, m (far where only the sky was drawn).
fn scene_distance(uv: vec2f) -> f32 {
  let size = vec2i(frame.size.xy);
  let pixel = clamp(vec2i(uv * frame.size.xy), vec2i(0), size - vec2i(1));
  let z = textureLoad(scene_depth, pixel, 0).r;
  if (z <= 0.0) {
    return 1e6;
  }
  let ndc = vec2f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
  let p = frame.inv_view_proj * vec4f(ndc, z, 1.0);
  return length(p.xyz / p.w);
}

// Eight taps round a disc of `radius` px and the centre.
fn blurred(uv: vec2f, radius: f32) -> vec3f {
  var sum = textureSampleLevel(scene_colour, linear_clamp, uv, 0.0).rgb;
  if (radius < 0.5) {
    return sum;
  }
  let px = frame.size.zw * radius;
  for (var i = 0; i < 8; i++) {
    let a = f32(i) * 0.785398 + 0.3;
    let r = select(1.0, 0.55, (i & 1) == 1);
    sum += textureSampleLevel(scene_colour, linear_clamp, uv + vec2f(cos(a), sin(a)) * r * px,
                              0.0).rgb;
  }
  return sum / 9.0;
}

@fragment
fn fs_main(in: VertexOut) -> @location(0) vec4f {
  let t = frame.time.x;
  let uv0 = in.position.xy * frame.size.zw;
  // The mask's magnification about the centre, then the slow wobble.
  var uv = (uv0 - 0.5) / under.look.x + 0.5;
  let w = 6.2831853 * under.look.z * t;
  uv += under.look.y * vec2f(sin(uv.y * 23.0 + w), cos(uv.x * 17.0 + 1.3 * w));
  uv = clamp(uv, vec2f(0.0), vec2f(1.0));
  let ray = normalize(view_ray(frame, uv));
  let dist = scene_distance(uv);

  // The light down here: the day's, dimmed over the water above the eye.
  let eye_depth = max(under.info.y, 0.0);
  let sun = sun_light(frame.eye.y, frame.eye.xz);
  let day = sun * max(frame.sun.y, 0.0) + sky_irradiance(vec3f(0.0, 1.0, 0.0));
  let veil = under.veil.rgb * day / PI * exp(-under.kd.rgb * eye_depth);

  let radius = under.info.z * (1.0 - exp(-min(dist, 1e3) * 0.693 / max(under.info.w, 0.01)));
  var seen = blurred(uv, radius);
  var path = dist;
  // The surface first: the world above through Snell's window, the water mirrored past it.
  if (ray.y > 1e-4) {
    let to_surface = eye_depth / ray.y;
    if (to_surface < dist) {
      path = to_surface;
      if (under.look.w > 0.5) {
        let window = smoothstep(0.645, 0.675, ray.y);  // cos 48.6 degrees, softened
        let f = 1.0 - ray.y;
        let fresnel = 0.02 + 0.98 * f * f * f * f * f;
        seen = mix(veil, seen * (1.0 - fresnel), window);
      }
    }
  }
  // What stands under the surface was lit by the day dimmed on its way down.
  let point_depth = under.info.x - (frame.eye.y + ray.y * min(path, 1e4));
  seen *= exp(-under.kd.rgb * max(point_depth, 0.0));
  let tr = exp(-under.c.rgb * under.fog.x * path);
  var colour = seen * tr + veil * (1.0 - tr);
  // The eye adapts to the dark: the veil's luma lifted toward `fog.y`, at most six times.
  if (under.fog.y > 0.0) {
    let v = dot(under.veil.rgb * exp(-under.kd.rgb * eye_depth), vec3f(0.2126, 0.7152, 0.0722));
    colour *= clamp(under.fog.y / max(v, 1e-4), 1.0, 6.0);
  }
  let luma = dot(colour, vec3f(0.2126, 0.7152, 0.0722));
  colour = mix(colour, under.tint.rgb * luma, under.tint.w);
  return vec4f(colour, 1.0);
}
