// The grass's buffers and pipelines, the cover's upload, the frame's wind grid and params, the
// scatter and draw passes, and the read-backs labs and tests use.
//
// ge:summary Grass drawn definitions: buffers, pipelines, cover upload, wind grid, passes, probes
// ge:keywords render grass scatter draw cover upload wind-grid indirect readback probe
// ge:skill render grass
// ge:test tests/render_grass_test.cpp
#include "render/grass.hpp"

#include <algorithm>
#include <cstring>

#include "render/grass.wgsl.hpp"
#include "render/grass_scatter.wgsl.hpp"
#include "render/shadows.hpp"

namespace ge::render {

namespace {

using BU = wgpu::BufferUsage;
constexpr uint32_t kGroup = 64;  // grass_scatter.wgsl's workgroup size
constexpr uint32_t kRows = 65535;

// grass_common.wgsl's GrassKind, GrassTri and GrassParams.
struct GpuKind {
  float shape[4], lean[4], air[4], root[4], tip[4], dry_tip[4];
};
struct GpuTri {
  float a[4], b[4], c[4];
  uint32_t info[4];
};
struct Params {
  uint32_t cover[4];
  float lod[4];
  float wind[4];
  float mean[4];
};
struct DrawArgs {
  uint32_t vertex_count, instance_count, first_vertex, first_instance;
};

uint32_t strip_vertices(uint32_t segments) { return 2 * (segments + 1); }

std::vector<uint8_t> read_buffer(gpu::Device& device, const wgpu::Buffer& src, uint64_t bytes) {
  std::vector<uint8_t> out(bytes);
  if (bytes == 0) return out;
  wgpu::BufferDescriptor desc{};
  desc.label = "grass.readback";
  desc.size = bytes;
  desc.usage = BU::MapRead | BU::CopyDst;
  wgpu::Buffer rb = device.device.CreateBuffer(&desc);
  wgpu::CommandEncoder encoder = device.device.CreateCommandEncoder();
  encoder.CopyBufferToBuffer(src, 0, rb, 0, bytes);
  wgpu::CommandBuffer commands = encoder.Finish();
  device.queue.Submit(1, &commands);
  gpu::wait(device, rb.MapAsync(wgpu::MapMode::Read, 0, bytes, wgpu::CallbackMode::WaitAnyOnly,
                                [](wgpu::MapAsyncStatus, wgpu::StringView) {}));
  if (const void* p = rb.GetConstMappedRange(0, bytes)) std::memcpy(out.data(), p, bytes);
  rb.Unmap();
  return out;
}

void make_groups(gpu::Device& device, Grass& g) {
  g.scatter_group =
      make_group(device, "grass.scatter", g.scatter_layout,
                 {g.params, g.tris, g.kinds, g.near_blades, g.far_blades, g.args, g.wind});
  g.draw_group = make_group(device, "grass.draw", g.draw_layout,
                            {g.params, g.kinds, g.near_blades, g.far_blades, g.wind});
}

}  // namespace

void create_grass(gpu::Device& device, gpu::PipelineSet& pipelines, Grass& g,
                  const wgpu::BindGroupLayout& frame, const Terrain& terrain,
                  const GrassSettings& settings) {
  g.settings = settings;
  g.settings.capacity = std::max<uint32_t>(settings.capacity, 64);
  g.settings.wind_nodes = std::clamp(settings.wind_nodes, 2, grass::kWindGridMax);
  const uint64_t list = uint64_t{g.settings.capacity} * sizeof(grass::Blade);
  g.params = make_buffer(device, "grass.params", sizeof(Params), BU::Uniform);
  g.tris = make_buffer(device, "grass.tris", sizeof(GpuTri), BU::Storage);
  g.kinds = make_buffer(device, "grass.kinds", sizeof(GpuKind) * grass::kMaxKinds, BU::Storage);
  g.wind = make_buffer(device, "grass.wind",
                       sizeof(float) * 2 * grass::kWindGridMax * grass::kWindGridMax, BU::Storage);
  g.near_blades = make_buffer(device, "grass.near", list, BU::Storage | BU::CopySrc);
  g.far_blades = make_buffer(device, "grass.far", list, BU::Storage | BU::CopySrc);
  g.args = make_buffer(device, "grass.args", sizeof(DrawArgs) * 2,
                       BU::Storage | BU::Indirect | BU::CopySrc);

  g.scatter_layout = make_layout(device, "grass.scatter",
                                 {Slot::uniform, Slot::storage_read, Slot::storage_read,
                                  Slot::storage, Slot::storage, Slot::storage, Slot::storage_read},
                                 wgpu::ShaderStage::Compute);
  const wgpu::ShaderModule scatter =
      gpu::create_shader(device, wgsl::grass_scatter, "grass_scatter");
  const auto compute = [&](const char* label, const char* entry,
                           std::initializer_list<wgpu::BindGroupLayout> groups) {
    wgpu::ComputePipelineDescriptor cd{};
    cd.label = label;
    cd.layout = make_pipeline_layout(device, label, groups);
    cd.compute.module = scatter;
    cd.compute.entryPoint = entry;
    return pipelines.add_compute(device, cd);
  };
  g.p_scatter = compute("grass.scatter", "scatter", {frame, g.scatter_layout});
  g.p_clamp = compute("grass.clamp", "clamp_counts", {frame, g.scatter_layout});
  if (g.settings.probe) {
    g.probe_layout =
        make_layout(device, "grass.probe", {Slot::storage}, wgpu::ShaderStage::Compute);
    g.p_shapes = compute("grass.shapes", "shapes", {frame, g.scatter_layout, g.probe_layout});
  }

  const auto vf = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
  g.light_layout =
      make_layout(device, "grass.light", {Slot::uniform, Slot::texture, Slot::texture}, vf);
  g.light_group = make_group(device, "grass.light", g.light_layout,
                             {terrain.params, terrain.horizon.views[0], terrain.horizon.views[1]});
  g.draw_layout = make_layout(device, "grass.draw",
                              {Slot::uniform, Slot::storage_read, Slot::storage_read,
                               Slot::storage_read, Slot::storage_read},
                              vf);
  g.shadows = no_sun_shadows(device);

  const wgpu::ShaderModule draw = gpu::create_shader(device, wgsl::grass, "grass");
  wgpu::ColorTargetState colours[2]{};
  colours[0].format = kHdrFormat;
  colours[1].format = kMotionFormat;
  wgpu::FragmentState fragment{};
  fragment.module = draw;
  fragment.entryPoint = "fs_main";
  fragment.targetCount = 2;
  fragment.targets = colours;
  wgpu::DepthStencilState depth{};
  depth.format = kDepthFormat;
  depth.depthWriteEnabled = true;
  depth.depthCompare = wgpu::CompareFunction::Greater;  // reversed-z: nearer is greater
  const wgpu::PipelineLayout layout = make_pipeline_layout(
      device, "grass.draw", {frame, g.light_layout, g.draw_layout, sun_shadow_layout(device)});
  for (const auto& [entry, slot] : {std::pair{"vs_near", &g.p_near}, {"vs_far", &g.p_far}}) {
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = entry;
    desc.layout = layout;
    desc.vertex.module = draw;
    desc.vertex.entryPoint = entry;
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleStrip;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.depthStencil = &depth;
    desc.fragment = &fragment;
    *slot = pipelines.add_render(device, desc);
  }
  make_groups(device, g);
}

bool set_grass_cover(gpu::Device& device, Grass& g, const grass::Cover& cover) {
  if (cover.kinds.size() > grass::kMaxKinds) return false;
  std::vector<GpuKind> kinds(grass::kMaxKinds);
  for (size_t i = 0; i < cover.kinds.size(); ++i) {
    const grass::Kind& k = cover.kinds[i];
    GpuKind& o = kinds[i];
    const float shape[4] = {k.height, k.height_jitter, k.width, k.density};
    const float lean[4] = {k.lean, k.lean_jitter, k.clump, k.pull};
    const float air[4] = {k.give, k.sway, k.dry, 0};
    std::memcpy(o.shape, shape, sizeof shape);
    std::memcpy(o.lean, lean, sizeof lean);
    std::memcpy(o.air, air, sizeof air);
    for (size_t c = 0; c < 3; ++c) {
      o.root[c] = k.root[c];
      o.tip[c] = k.tip[c];
      o.dry_tip[c] = k.dry_tip[c];
    }
  }
  device.queue.WriteBuffer(g.kinds, 0, kinds.data(), kinds.size() * sizeof(GpuKind));
  std::vector<GpuTri> tris;
  tris.reserve(cover.tris.size());
  for (const grass::Tri& t : cover.tris) {
    if (t.kind >= cover.kinds.size()) continue;
    GpuTri o{};
    for (int k = 0; k < 3; ++k) {
      o.a[k] = t.a[k];
      o.b[k] = t.b[k];
      o.c[k] = t.c[k];
    }
    o.a[3] = t.share[0];
    o.b[3] = t.share[1];
    o.c[3] = t.share[2];
    o.info[0] = t.kind;
    o.info[1] = grass::tri_seed(t, cover.seed);
    tris.push_back(o);
  }
  g.tri_count = static_cast<uint32_t>(tris.size());
  g.tris = make_buffer(device, "grass.tris", std::max<size_t>(tris.size(), 1) * sizeof(GpuTri),
                       BU::Storage);
  if (!tris.empty()) device.queue.WriteBuffer(g.tris, 0, tris.data(), tris.size() * sizeof(GpuTri));
  make_groups(device, g);
  return true;
}

void bind_grass_shadows(Grass& g, const wgpu::BindGroup& shadows) { g.shadows = shadows; }

void prepare_grass(gpu::Device& device, Grass& g, const FrameView& view, const grass::GrassAir& air,
                   double t) {
  const GrassSettings& s = g.settings;
  grass::fill_wind_grid(g.grid, air, view.eye[0], view.eye[2], 2.0 * s.reach_m, s.wind_nodes, t);
  if (!g.grid.air.empty()) {
    device.queue.WriteBuffer(g.wind, 0, g.grid.air.data(), g.grid.air.size() * sizeof(float));
  }
  g.time = static_cast<float>(t);
  Params p{};
  p.cover[0] = g.tri_count;
  p.cover[1] = s.capacity;
  p.cover[2] = s.near_segments;
  p.cover[3] = s.far_segments;
  p.lod[0] = s.full_m;
  p.lod[1] = s.near_m;
  p.lod[2] = s.reach_m;
  p.lod[3] = s.min_keep;
  p.wind[0] = g.grid.x0;
  p.wind[1] = g.grid.z0;
  p.wind[2] = g.grid.cell;
  p.wind[3] = static_cast<float>(g.grid.n);
  p.mean[0] = g.grid.mean[0];
  p.mean[1] = g.grid.mean[1];
  p.mean[2] = g.time;
  device.queue.WriteBuffer(g.params, 0, &p, sizeof p);
  const DrawArgs args[2] = {{strip_vertices(s.near_segments), 0, 0, 0},
                            {strip_vertices(s.far_segments), 0, 0, 0}};
  device.queue.WriteBuffer(g.args, 0, args, sizeof args);
}

void encode_grass_scatter(const Grass& g, const gpu::PipelineSet& pipelines, FrameRecorder& rec,
                          wgpu::CommandEncoder& encoder, const wgpu::BindGroup& frame) {
  wgpu::ComputePassEncoder pass = rec.compute(encoder, "grass.scatter");
  if (g.tri_count > 0) {
    pass.SetPipeline(pipelines.compute(g.p_scatter));
    pass.SetBindGroup(0, frame);
    pass.SetBindGroup(1, g.scatter_group);
    pass.DispatchWorkgroups(std::min(g.tri_count, kRows), (g.tri_count + kRows - 1) / kRows);
    ++rec.current().dispatches;
  }
  pass.SetPipeline(pipelines.compute(g.p_clamp));
  pass.SetBindGroup(0, frame);
  pass.SetBindGroup(1, g.scatter_group);
  pass.DispatchWorkgroups(1);
  ++rec.current().dispatches;
  pass.End();
}

void encode_grass(const Grass& g, const gpu::PipelineSet& pipelines, FrameRecorder& rec,
                  wgpu::CommandEncoder& encoder, const FrameTargets& targets,
                  const wgpu::BindGroup& frame) {
  wgpu::RenderPassColorAttachment colours[2]{};
  colours[0].view = targets.hdr_view;
  colours[1].view = targets.motion_view;
  for (auto& c : colours) {
    c.loadOp = wgpu::LoadOp::Load;
    c.storeOp = wgpu::StoreOp::Store;
  }
  wgpu::RenderPassDepthStencilAttachment depth{};
  depth.view = targets.depth_view;
  depth.depthLoadOp = wgpu::LoadOp::Load;
  depth.depthStoreOp = wgpu::StoreOp::Store;
  wgpu::RenderPassDescriptor desc{};
  desc.colorAttachmentCount = 2;
  desc.colorAttachments = colours;
  desc.depthStencilAttachment = &depth;
  wgpu::RenderPassEncoder pass = rec.render(encoder, desc, "grass");
  pass.SetBindGroup(0, frame);
  pass.SetBindGroup(1, g.light_group);
  pass.SetBindGroup(2, g.draw_group);
  pass.SetBindGroup(3, g.shadows);
  pass.SetPipeline(pipelines.render(g.p_near));
  pass.DrawIndirect(g.args, 0);
  pass.SetPipeline(pipelines.render(g.p_far));
  pass.DrawIndirect(g.args, sizeof(DrawArgs));
  rec.current().draws += 2;
  pass.End();
}

GrassLists read_grass_blades(gpu::Device& device, const Grass& g) {
  GrassLists out;
  const std::vector<uint8_t> raw = read_buffer(device, g.args, sizeof(DrawArgs) * 2);
  DrawArgs args[2];
  std::memcpy(args, raw.data(), sizeof args);
  out.near_count = args[0].instance_count;
  out.far_count = args[1].instance_count;
  const auto list = [&](const wgpu::Buffer& b, uint32_t n) {
    n = std::min(n, g.settings.capacity);
    std::vector<grass::Blade> blades(n);
    const std::vector<uint8_t> bytes = read_buffer(device, b, uint64_t{n} * sizeof(grass::Blade));
    if (n > 0) std::memcpy(blades.data(), bytes.data(), bytes.size());
    return blades;
  };
  out.near = list(g.near_blades, out.near_count);
  out.far = list(g.far_blades, out.far_count);
  return out;
}

std::vector<float> probe_grass_shapes(gpu::Device& device, const gpu::PipelineSet& pipelines,
                                      const Grass& g, const wgpu::BindGroup& frame) {
  if (!g.settings.probe) return {};
  const uint64_t bytes = uint64_t{g.settings.capacity} * 3 * 16;
  const wgpu::Buffer probe = make_buffer(device, "grass.probe", bytes, BU::Storage | BU::CopySrc);
  const wgpu::BindGroup group = make_group(device, "grass.probe", g.probe_layout, {probe});
  wgpu::CommandEncoder encoder = device.device.CreateCommandEncoder();
  wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
  pass.SetPipeline(pipelines.compute(g.p_shapes));
  pass.SetBindGroup(0, frame);
  pass.SetBindGroup(1, g.scatter_group);
  pass.SetBindGroup(2, group);
  pass.DispatchWorkgroups((g.settings.capacity + kGroup - 1) / kGroup);
  pass.End();
  wgpu::CommandBuffer commands = encoder.Finish();
  device.queue.Submit(1, &commands);
  const std::vector<uint8_t> raw = read_buffer(device, probe, bytes);
  std::vector<float> out(raw.size() / sizeof(float));
  std::memcpy(out.data(), raw.data(), raw.size());
  return out;
}

}  // namespace ge::render
