// THE SCENE UNDER THE WATER (render/water.hpp's "water.copy"): the opaque frame's colour and
// depth copied before the water's pass draws over them, so the water can see what is behind it.
@group(0) @binding(0) var hdr: texture_2d<f32>;
@group(0) @binding(1) var depth: texture_depth_2d;

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

struct CopyOut {
  @location(0) colour: vec4f,
  @location(1) depth: vec4f,
}

@fragment
fn fs_main(in: VertexOut) -> CopyOut {
  let p = vec2i(in.position.xy);
  var out: CopyOut;
  out.colour = textureLoad(hdr, p, 0);
  out.depth = vec4f(textureLoad(depth, p, 0), 0.0, 0.0, 0.0);
  return out;
}
