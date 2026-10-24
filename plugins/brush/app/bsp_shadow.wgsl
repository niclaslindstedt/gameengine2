// THE BSP RENDERER'S SHADOW MAPS (plugins/brush/app/bsp_draw.hpp): the map's depth seen by one
// shadow camera a draw, the camera picked by the draw's first instance. A point light's cube
// face looks along its axis over a 90 degree square and stores the distance over the light's
// reach; an orthographic map (the sun's, the zenith's) stores how deep along its axis a point
// lies over its depth. bsp.wgsl reads both back the same way.
//
// ge:summary The BSP renderer's shadow maps: cube faces of distance and orthographic depth
// ge:keywords bsp brush render shadow cube-shadow ortho sun zenith depth
// ge:skill brush render

struct Job {
  o: vec4f,
  x: vec4f,
  y: vec4f,
  z: vec4f,
  info: vec4f,
};

@group(0) @binding(0) var<storage, read> jobs: array<Job>;

struct Out {
  @builtin(position) clip: vec4f,
  @location(0) q: vec3f,
  @location(1) @interpolate(flat) job: u32,
};

@vertex
fn vs_main(@location(0) pos: vec3f, @builtin(instance_index) job: u32) -> Out {
  let j = jobs[job];
  let q = pos - j.o.xyz;
  let x = dot(q, j.x.xyz);
  let y = dot(q, j.y.xyz);
  let z = dot(q, j.z.xyz);
  var o: Out;
  if (j.info.x < 0.5) {
    o.clip = vec4f(x, y, 0.5 * z, z);
  } else {
    o.clip = vec4f(x / j.o.w, y / j.o.w, 0.5, 1.0);
  }
  o.q = q;
  o.job = job;
  return o;
}

@fragment
fn fs_main(in: Out) -> @builtin(frag_depth) f32 {
  let j = jobs[in.job];
  if (j.info.x < 0.5) {
    return clamp(length(in.q) / j.o.w, 0.0, 1.0);
  }
  return clamp((dot(in.q, j.z.xyz) + j.info.y) / (2.0 * j.info.y), 0.0, 1.0);
}
