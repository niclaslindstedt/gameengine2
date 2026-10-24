// THE 2D PASS (render/ui_pass.hpp): one quad an instance, a box or a glyph, blended over the
// frame. A box's coverage is its rounded rectangle's signed distance at the pixel's centre, as
// the CPU raster (ui/raster.cpp) works it, its fill a paint (times an image) and its border a
// ring inside its edge; a glyph reads its coverage from the atlas. Colours stay sRGB-encoded and
// leave premultiplied, so the blend (one, one minus source alpha) is the raster's straight "over".

struct Screen {
  size: vec4f,  // width, height, 1 / width, 1 / height (px)
}

struct Item {
  rect: vec4f,   // x, y, w, h (px)
  clip: vec4f,   // x0, y0, x1, y1 (px)
  uv: vec4f,     // box: image u, v at the corner, du/dx, dv/dy; glyph: atlas x, y (texels)
  ink: vec4f,    // box: the border's colour; glyph: the colour
  shape: vec4f,  // radius, feather, border, opacity
  info: vec4u,   // kind (0 box, 1 glyph, 2 box with an image), paint index
}

struct Paint {
  line: vec4f,  // linear: from, to; radial: the centre
  form: vec4f,  // radius, aspect, kind (0 solid, 1 linear, 2 radial), stop count
  at: array<vec4f, 2>,
  colors: array<vec4f, 8>,
}

@group(0) @binding(0) var<uniform> screen: Screen;
@group(0) @binding(1) var<storage, read> items: array<Item>;
@group(0) @binding(2) var<storage, read> paints: array<Paint>;
@group(0) @binding(3) var atlas: texture_2d<f32>;
@group(0) @binding(4) var lin: sampler;
@group(1) @binding(0) var image: texture_2d<f32>;

const kGlyph = 1u;
const kSampled = 2u;

struct VertexOut {
  @builtin(position) position: vec4f,
  @location(0) @interpolate(flat) item: u32,
}

@vertex
fn vs_main(@builtin(vertex_index) v: u32, @builtin(instance_index) i: u32) -> VertexOut {
  let it = items[i];
  // What the quad must cover: the box's rectangle grown by its feather (a glyph's: its mask),
  // inside the clip.
  var grow = 0.0;
  if (it.info.x != kGlyph) {
    grow = max(it.shape.y, 0.5);
  }
  let lo = max(it.rect.xy - grow, it.clip.xy);
  let hi = max(min(it.rect.xy + it.rect.zw + grow, it.clip.zw), lo);
  let corner = array<vec2f, 6>(vec2f(0.0, 0.0), vec2f(1.0, 0.0), vec2f(0.0, 1.0),
                               vec2f(0.0, 1.0), vec2f(1.0, 0.0), vec2f(1.0, 1.0));
  let p = mix(lo, hi, corner[v]);
  var out: VertexOut;
  out.position = vec4f(p.x * screen.size.z * 2.0 - 1.0, 1.0 - p.y * screen.size.w * 2.0, 0.0, 1.0);
  out.item = i;
  return out;
}

// ui/raster.cpp's rounded_distance: from p to a box of half-size h, corner radius r, centred at
// the origin; negative inside.
fn rounded_distance(p: vec2f, h: vec2f, r: f32) -> f32 {
  let q = abs(p) - h + r;
  return length(max(q, vec2f(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

fn coverage(d: f32, feather: f32) -> f32 {
  return clamp(0.5 - d / feather, 0.0, 1.0);
}

fn stop_at(p: Paint, k: u32) -> f32 {
  return p.at[k / 4u][k % 4u];
}

// ui/paint.cpp's lookup: the stops' colour at t.
fn lookup(p: Paint, t: f32) -> vec4f {
  let n = u32(p.form.w);
  if (n <= 1u || t <= stop_at(p, 0u)) {
    return p.colors[0];
  }
  for (var k = 1u; k < n; k++) {
    let b = stop_at(p, k);
    if (t <= b) {
      let a = stop_at(p, k - 1u);
      let span = b - a;
      if (span > 0.0) {
        return mix(p.colors[k - 1u], p.colors[k], (t - a) / span);
      }
      return p.colors[k];
    }
  }
  return p.colors[n - 1u];
}

// ui/paint.cpp's Paint::at, at (x, y) in the box's frame.
fn paint_at(p: Paint, x: vec2f) -> vec4f {
  let kind = u32(p.form.z);
  if (kind == 1u) {
    let d = p.line.zw - p.line.xy;
    let len2 = dot(d, d);
    var t = 0.0;
    if (len2 > 0.0) {
      t = dot(x - p.line.xy, d) / len2;
    }
    return lookup(p, t);
  }
  if (kind == 2u) {
    let d = vec2f(x.x - p.line.x, (x.y - p.line.y) / p.form.y);
    return lookup(p, length(d) / p.form.x);
  }
  return p.colors[0];
}

// Interleaved gradient noise, -0.5 .. 0.5: half a level of dither on a gradient's fill.
fn dither(p: vec2f) -> f32 {
  return fract(52.9829189 * fract(dot(p, vec2f(0.06711056, 0.00583715)))) - 0.5;
}

@fragment
fn fs_main(in: VertexOut) -> @location(0) vec4f {
  let it = items[in.item];
  let p = in.position.xy;
  if (p.x < it.clip.x || p.x >= it.clip.z || p.y < it.clip.y || p.y >= it.clip.w) {
    discard;
  }
  if (it.info.x == kGlyph) {
    let t = vec2i(it.uv.xy) + vec2i(floor(p - it.rect.xy));
    let a = it.ink.a * textureLoad(atlas, t, 0).r;
    return vec4f(it.ink.rgb * a, a);
  }
  let half = it.rect.zw * 0.5;
  let r = clamp(it.shape.x, 0.0, min(half.x, half.y));
  let feather = max(it.shape.y, 0.5);
  let border = it.shape.z;
  let d = rounded_distance(p - (it.rect.xy + half), half, r);
  let outer = coverage(d, feather);
  if (outer <= 0.0) {
    discard;
  }
  var inner = outer;
  if (border > 0.0) {
    inner = coverage(d + border, feather);
  }
  let local = p - it.rect.xy;
  let paint = paints[it.info.y];
  var c = paint_at(paint, local);
  if (it.info.x == kSampled) {
    c *= textureSampleLevel(image, lin, it.uv.xy + local * it.uv.zw, 0.0);
  }
  let fill = c.a * inner * it.shape.w;
  var ring = 0.0;
  if (border > 0.0) {
    ring = it.ink.a * (outer - inner) * it.shape.w;
  }
  // The fill, then the border over it: two "over"s folded into one premultiplied colour.
  var rgb = it.ink.rgb * ring + c.rgb * fill * (1.0 - ring);
  let alpha = fill + ring - fill * ring;
  if (u32(paint.form.z) != 0u) {
    rgb += vec3f(dither(p) / 255.0 * fill * (1.0 - ring));
  }
  return vec4f(rgb, alpha);
}
