// THE GRASS'S SCATTER (render/grass.hpp): one workgroup a cover triangle. The triangle is culled
// against the frustum (its sphere, raised by its kind's tallest blade) and the reach; each thread
// then takes every 64th slot of it, scatters that blade exactly as engine/grass does, and keeps
// it by its rank against the distance's share: every blade within `full_m`, (full_m / d)² of them
// past it, none past the reach. A kept blade near its rank's threshold or the reach shrinks to
// nothing rather than vanishing, and the fewer blades far off widen so the field keeps its cover.
// It is appended to the fine list (within `near_m`) or the coarse one, counted into that list's
// indirect draw; `clamp` then holds each count to the list's capacity.
#include "render/wgsl/frame.wgsl"
#include "render/wgsl/grass_common.wgsl"

struct DrawArgs {
  vertex_count: u32,
  instance_count: atomic<u32>,
  first_vertex: u32,
  first_instance: u32,
}

@group(1) @binding(0) var<uniform> gp: GrassParams;
@group(1) @binding(1) var<storage, read> tris: array<GrassTri>;
@group(1) @binding(2) var<storage, read> kinds: array<GrassKind>;
@group(1) @binding(3) var<storage, read_write> near_blades: array<Blade>;
@group(1) @binding(4) var<storage, read_write> far_blades: array<Blade>;
@group(1) @binding(5) var<storage, read_write> args: array<DrawArgs, 2>;
@group(1) @binding(6) var<storage, read> grass_wind: array<vec2f>;

const kGroup = 64u;

// The share of blades kept at `d` m from the eye.
fn keep_at(d: f32) -> f32 {
  let full = gp.lod.x;
  if (d <= full) {
    return 1.0;
  }
  let r = full / d;
  return max(r * r, gp.lod.w);
}

@compute @workgroup_size(64)
fn scatter(@builtin(workgroup_id) wg: vec3u, @builtin(local_invocation_index) lid: u32) {
  let index = wg.x + wg.y * 65535u;
  if (index >= gp.cover.x) {
    return;
  }
  let t = tris[index];
  let k = kinds[t.info.x];
  let tallest = k.shape.x * (1.0 + k.shape.y) * 1.25;
  let centre = (t.a.xyz + t.b.xyz + t.c.xyz) / 3.0;
  let radius = max(max(length(t.a.xyz - centre), length(t.b.xyz - centre)),
                   length(t.c.xyz - centre)) + tallest;
  let rel = centre - frame.eye.xyz;
  if (length(rel) - radius > gp.lod.z) {
    return;
  }
  for (var i = 0; i < 5; i++) {
    let p = frame.planes[i];
    if (dot(p.xyz, rel) + p.w < -radius) {
      return;
    }
  }
  let slots = blade_slots(t, k);
  for (var slot = lid; slot < slots; slot += kGroup) {
    let s = scatter_one(t, k, slot);
    if (!s.kept) {
      continue;
    }
    var b = s.blade;
    let r = b.root - frame.eye.xyz;
    let d = length(r);
    if (d > gp.lod.z) {
      continue;
    }
    let keep = keep_at(d);
    if (s.rank >= keep) {
      continue;
    }
    // The blade's own sphere against the frustum.
    let mid = r + vec3f(0.0, b.height * 0.5, 0.0);
    var seen = true;
    for (var i = 0; i < 5; i++) {
      let p = frame.planes[i];
      if (dot(p.xyz, mid) + p.w < -b.height) {
        seen = false;
      }
    }
    if (!seen) {
      continue;
    }
    // Shrink near the rank's threshold and the reach; widen the fewer far blades.
    var fade = clamp((gp.lod.z - d) / (0.15 * gp.lod.z), 0.0, 1.0);
    if (keep < 1.0) {
      fade = fade * clamp((keep - s.rank) / (0.2 * keep), 0.0, 1.0);
    }
    b.height = b.height * fade;
    b.width = b.width * min(inverseSqrt(keep), 3.0);
    if (b.height < 0.005) {
      continue;
    }
    if (d < gp.lod.y) {
      let at = atomicAdd(&args[0].instance_count, 1u);
      if (at < gp.cover.y) {
        near_blades[at] = b;
      }
    } else {
      let at = atomicAdd(&args[1].instance_count, 1u);
      if (at < gp.cover.y) {
        far_blades[at] = b;
      }
    }
  }
}

// Each list's count held to its capacity (one thread).
@compute @workgroup_size(1)
fn clamp_counts() {
  for (var i = 0u; i < 2u; i++) {
    let n = atomicLoad(&args[i].instance_count);
    atomicStore(&args[i].instance_count, min(n, gp.cover.y));
  }
}

// THE PROBE (tests only): the shape `blade_shape` gives each near blade at the params' time in
// the grid's air, three points a blade into `probe`.
@group(2) @binding(0) var<storage, read_write> probe: array<vec4f>;

@compute @workgroup_size(64)
fn shapes(@builtin(global_invocation_id) id: vec3u) {
  let n = min(atomicLoad(&args[0].instance_count), gp.cover.y);
  if (id.x >= n) {
    return;
  }
  let b = near_blades[id.x];
  let air = wind_at_blade(b.root.x, b.root.z, gp.mean.z);
  let s = blade_shape(kinds[b.seed >> 24u], b, air, gp.mean.z);
  probe[id.x * 3u] = vec4f(s.v0, air.x);
  probe[id.x * 3u + 1u] = vec4f(s.v1, air.y);
  probe[id.x * 3u + 2u] = vec4f(s.v2, 0.0);
}
