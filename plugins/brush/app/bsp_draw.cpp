// The BSP renderer (bsp_draw.hpp): the map's buffers and atlases, its three pipelines, the lights
// picked each frame and the shadow cameras over them.
//
// ge:summary The BSP renderer: buffers, atlases, pipelines, the lights a frame and their shadows
// ge:keywords bsp brush render renderer classic v2 point-light cube-shadow sun-shadow lava-light
// ge:skill brush render
#include "brush/app/bsp_draw.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "brush/app/bsp.wgsl.hpp"
#include "brush/app/bsp_fix.wgsl.hpp"
#include "brush/app/bsp_shadow.wgsl.hpp"
#include "render/view.hpp"
#include "water/glow.hpp"

namespace ge::brush::app {

namespace r = ge::render;

namespace {

wgpu::Texture depth_array(gpu::Device& device, std::string_view label, uint32_t size,
                          uint32_t layers) {
  wgpu::TextureDescriptor desc{};
  desc.label = wgpu::StringView(label.data(), label.size());
  desc.size = {size, size, layers};
  desc.format = r::kDepthFormat;
  desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
  return device.device.CreateTexture(&desc);
}

wgpu::TextureView layer_view(const wgpu::Texture& t, uint32_t k) {
  wgpu::TextureViewDescriptor desc{};
  desc.dimension = wgpu::TextureViewDimension::e2D;
  desc.baseArrayLayer = k;
  desc.arrayLayerCount = 1;
  return t.CreateView(&desc);
}

wgpu::TextureView array_view(const wgpu::Texture& t, uint32_t layers) {
  wgpu::TextureViewDescriptor desc{};
  desc.dimension = wgpu::TextureViewDimension::e2DArray;
  desc.arrayLayerCount = layers;
  return t.CreateView(&desc);
}

void upload(gpu::Device& device, const wgpu::Texture& t, const Atlas& a) {
  for (uint32_t k = 0; k < a.mips; ++k) {
    wgpu::TexelCopyTextureInfo dst{};
    dst.texture = t;
    dst.mipLevel = k;
    wgpu::TexelCopyBufferLayout layout{};
    const uint32_t w = a.width >> k, h = a.height >> k;
    layout.bytesPerRow = w * 4;
    layout.rowsPerImage = h;
    const wgpu::Extent3D size{w, h, 1};
    device.queue.WriteTexture(&dst, a.levels[k].data(), a.levels[k].size(), &layout, &size);
  }
}

void set4(float out[4], double a, double b, double c, double d) {
  out[0] = float(a);
  out[1] = float(b);
  out[2] = float(c);
  out[3] = float(d);
}

// An orthographic camera looking along `dir` (unit) at `centre`, `half` m across, `depth` m deep.
GpuJob ortho_job(const double dir[3], const double centre[3], double half, double depth) {
  const double ref[3] = {0, std::fabs(dir[1]) < 0.99 ? 1.0 : 0.0,
                         std::fabs(dir[1]) < 0.99 ? 0.0 : 1.0};
  double x[3] = {ref[1] * dir[2] - ref[2] * dir[1], ref[2] * dir[0] - ref[0] * dir[2],
                 ref[0] * dir[1] - ref[1] * dir[0]};
  const double l = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
  for (double& c : x) c /= l;
  const double y[3] = {dir[1] * x[2] - dir[2] * x[1], dir[2] * x[0] - dir[0] * x[2],
                       dir[0] * x[1] - dir[1] * x[0]};
  GpuJob j{};
  set4(j.o, centre[0], centre[1], centre[2], half);
  set4(j.x, x[0], x[1], x[2], 0);
  set4(j.y, y[0], y[1], y[2], 0);
  set4(j.z, dir[0], dir[1], dir[2], 0);
  set4(j.info, 1, depth, 0, 0);
  return j;
}

}  // namespace

bool parse_mode(std::string_view word, Mode* out) {
  if (word == "classic")
    *out = Mode::classic;
  else if (word == "v2")
    *out = Mode::v2;
  else
    return false;
  return true;
}

std::vector<PointLight> lava_lights(const Scene& scene, double core_c, double cover) {
  std::vector<PointLight> out;
  // The pool's mean glow: the open melt's share at the core, the crust's at a dull red.
  const water::Glow melt = water::glow_of(core_c + 273.15, 0.9);
  const water::Glow crust = water::glow_of(core_c - 380 + 273.15, 0.9);
  const double m[3] = {melt.colour.r, melt.colour.g, melt.colour.b};
  const double c[3] = {crust.colour.r, crust.colour.g, crust.colour.b};
  double rgb[3];
  for (int k = 0; k < 3; ++k) {
    rgb[k] = (1 - cover) * melt.luminance * m[k] + cover * crust.luminance * c[k];
  }
  for (const LiquidPatch& p : scene.liquids) {
    if (p.kind != kLava) continue;
    const double w = p.box[2] - p.box[0], d = p.box[3] - p.box[1];
    // A light every 2.5 m or so, at most 64 a pool.
    double step = 2.5;
    while ((std::floor(w / step) + 1) * (std::floor(d / step) + 1) > 64) step *= 1.25;
    const int nx = std::max(1, int(w / step)), nz = std::max(1, int(d / step));
    const double cell = (w / nx) * (d / nz) * std::min(1.0, p.area / std::max(w * d, 1e-6));
    for (int i = 0; i < nx; ++i) {
      for (int j = 0; j < nz; ++j) {
        PointLight l;
        l.pos[0] = p.box[0] + (i + 0.5) * w / nx;
        l.pos[1] = p.level + 0.25;
        l.pos[2] = p.box[1] + (j + 0.5) * d / nz;
        l.physical = true;
        // A flat glowing patch seen from the side: half its normal intensity, on average.
        l.value = 0.5 * cell;
        l.reach = 14;
        for (int k = 0; k < 3; ++k) l.colour[k] = rgb[k];
        l.flicker = Flicker::steady;
        out.push_back(l);
      }
    }
  }
  return out;
}

void create_bsp(gpu::Device& device, gpu::PipelineSet& pipelines, BspDraw& d, const Scene& scene,
                const wgpu::BindGroupLayout& frame) {
  d.scene = &scene;
  // The indices by kind, so a mode draws the ranges it wants.
  std::vector<uint32_t> sorted;
  sorted.reserve(scene.indices.size());
  for (int k = 0; k < kKinds; ++k) {
    d.range[k][0] = uint32_t(sorted.size());
    for (size_t i = 0; i + 2 < scene.indices.size(); i += 3) {
      if (scene.vertices[scene.indices[i]].kind != uint32_t(k)) continue;
      sorted.insert(sorted.end(), {scene.indices[i], scene.indices[i + 1], scene.indices[i + 2]});
    }
    d.range[k][1] = uint32_t(sorted.size()) - d.range[k][0];
  }
  d.vertices = r::make_buffer(device, "bsp.vertices", scene.vertices.size() * sizeof(DrawVertex),
                              wgpu::BufferUsage::Vertex);
  device.queue.WriteBuffer(d.vertices, 0, scene.vertices.data(),
                           scene.vertices.size() * sizeof(DrawVertex));
  d.indices = r::make_buffer(device, "bsp.indices", std::max<size_t>(sorted.size(), 3) * 4,
                             wgpu::BufferUsage::Index);
  if (!sorted.empty()) device.queue.WriteBuffer(d.indices, 0, sorted.data(), sorted.size() * 4);

  const auto sampled = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
  d.atlas = r::make_texture(device, "bsp.atlas", scene.textures.width, scene.textures.height, 1,
                            wgpu::TextureFormat::RGBA8UnormSrgb, sampled, scene.textures.mips);
  upload(device, d.atlas, scene.textures);
  d.light = r::make_texture(device, "bsp.lightmaps", scene.light.width, scene.light.height, 1,
                            wgpu::TextureFormat::RGBA8Unorm, sampled);
  upload(device, d.light, scene.light);

  d.cubes = depth_array(device, "bsp.cubes", kCubeSize, kMaxShadowed * 6);
  for (uint32_t k = 0; k < kMaxShadowed * 6; ++k) d.cube_layers.push_back(layer_view(d.cubes, k));
  d.cubes_view = array_view(d.cubes, kMaxShadowed * 6);
  d.orthos = depth_array(device, "bsp.orthos", kOrthoSize, 2);
  for (uint32_t k = 0; k < 2; ++k) d.ortho_layers[k] = layer_view(d.orthos, k);
  d.orthos_view = array_view(d.orthos, 2);
  {
    wgpu::SamplerDescriptor s{};
    s.magFilter = s.minFilter = wgpu::FilterMode::Linear;
    s.addressModeU = s.addressModeV = wgpu::AddressMode::ClampToEdge;
    d.light_sampler = device.device.CreateSampler(&s);
    s.compare = wgpu::CompareFunction::LessEqual;
    d.compare = device.device.CreateSampler(&s);
  }
  d.uniform = r::make_buffer(device, "bsp.uniform", sizeof(GpuBsp), wgpu::BufferUsage::Uniform);
  d.lights =
      r::make_buffer(device, "bsp.lights", sizeof(GpuLight) * 512, wgpu::BufferUsage::Storage);
  d.jobs = r::make_buffer(device, "bsp.jobs", sizeof(GpuJob) * (kMaxShadowed * 6 + 2),
                          wgpu::BufferUsage::Storage);

  using r::Slot;
  const auto both = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
  d.layout =
      r::make_layout(device, "bsp",
                     {Slot::uniform, Slot::texture, Slot::texture, Slot::sampler,
                      Slot::storage_read, Slot::depth_array, Slot::compare, Slot::depth_array},
                     both);
  d.group = r::make_group(device, "bsp", d.layout,
                          {d.uniform, d.atlas.CreateView(), d.light.CreateView(), d.light_sampler,
                           d.lights, d.cubes_view, d.compare, d.orthos_view});
  d.fix_layout = r::make_layout(device, "bsp.sky", {Slot::texture}, wgpu::ShaderStage::Fragment);
  d.shadow_layout = r::make_layout(device, "bsp.shadows", {Slot::storage_read}, both);
  d.shadow_group = r::make_group(device, "bsp.shadows", d.shadow_layout, {d.jobs});

  wgpu::VertexAttribute attrs[6] = {};
  const uint64_t offsets[6] = {0, 12, 20, 28, 40, 56};
  const wgpu::VertexFormat formats[6] = {
      wgpu::VertexFormat::Float32x3, wgpu::VertexFormat::Float32x2, wgpu::VertexFormat::Float32x2,
      wgpu::VertexFormat::Float32x3, wgpu::VertexFormat::Float32x4, wgpu::VertexFormat::Uint32};
  for (uint32_t i = 0; i < 6; ++i) {
    attrs[i].shaderLocation = i;
    attrs[i].offset = offsets[i];
    attrs[i].format = formats[i];
  }
  wgpu::VertexBufferLayout vbl{};
  vbl.arrayStride = sizeof(DrawVertex);
  vbl.attributeCount = 6;
  vbl.attributes = attrs;
  {
    const wgpu::ShaderModule module = gpu::create_shader(device, wgsl::bsp, "bsp");
    wgpu::ColorTargetState colours[3] = {};
    colours[0].format = r::kHdrFormat;
    colours[1].format = r::kMotionFormat;
    colours[2].format = wgpu::TextureFormat::R8Unorm;
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 3;
    fragment.targets = colours;
    wgpu::DepthStencilState depth{};
    depth.format = r::kDepthFormat;
    depth.depthWriteEnabled = true;
    depth.depthCompare = wgpu::CompareFunction::Greater;
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "bsp.draw";
    desc.layout = r::make_pipeline_layout(device, "bsp.draw", {frame, d.layout});
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_main";
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vbl;
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.depthStencil = &depth;
    desc.fragment = &fragment;
    d.p_draw = pipelines.add_render(device, desc);
  }
  {
    const wgpu::ShaderModule module = gpu::create_shader(device, wgsl::bsp_fix, "bsp.sky");
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 0;
    wgpu::DepthStencilState depth{};
    depth.format = r::kDepthFormat;
    depth.depthWriteEnabled = true;
    depth.depthCompare = wgpu::CompareFunction::Always;
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "bsp.sky";
    desc.layout = r::make_pipeline_layout(device, "bsp.sky", {d.fix_layout});
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_main";
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.depthStencil = &depth;
    desc.fragment = &fragment;
    d.p_fix = pipelines.add_render(device, desc);
  }
  {
    const wgpu::ShaderModule module = gpu::create_shader(device, wgsl::bsp_shadow, "bsp.shadows");
    wgpu::VertexBufferLayout pos{};
    pos.arrayStride = sizeof(DrawVertex);
    pos.attributeCount = 1;
    pos.attributes = attrs;
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 0;
    wgpu::DepthStencilState depth{};
    depth.format = r::kDepthFormat;
    depth.depthWriteEnabled = true;
    depth.depthCompare = wgpu::CompareFunction::Less;
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "bsp.shadows";
    desc.layout = r::make_pipeline_layout(device, "bsp.shadows", {d.shadow_layout});
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_main";
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &pos;
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.depthStencil = &depth;
    desc.fragment = &fragment;
    d.p_shadow = pipelines.add_render(device, desc);
  }
  d.lava = lava_lights(scene, 1150, 0.3);
}

void create_bsp_targets(gpu::Device& device, BspDraw& d, const r::FrameTargets& targets) {
  d.mask = r::make_texture(
      device, "bsp.mask", targets.width, targets.height, 1, wgpu::TextureFormat::R8Unorm,
      wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding);
  d.mask_view = d.mask.CreateView();
  d.fix_group = r::make_group(device, "bsp.sky", d.fix_layout, {d.mask_view});
}

void prepare_bsp(gpu::Device& device, BspDraw& d, const r::FrameView& view, const Options& o) {
  const Scene& s = *d.scene;
  d.options = o;
  d.options.shadowed = std::clamp(o.shadowed, 0, kMaxShadowed);
  const bool lights = o.mode == Mode::v2 && o.lighting == Lighting::lights;
  // The lights near enough to the eye to reach what it sees, nearest first.
  struct Pick {
    const PointLight* l;
    double d;
  };
  std::vector<Pick> picks;
  if (lights) {
    const auto consider = [&](const PointLight& l) {
      const double reach = l.physical ? l.reach : l.reach * 1.5;
      const double dx = l.pos[0] - view.eye[0], dy = l.pos[1] - view.eye[1],
                   dz = l.pos[2] - view.eye[2];
      const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (dist < reach + 40) picks.push_back({&l, dist - reach});
    };
    for (const PointLight& l : s.lights) consider(l);
    if (o.lava_lights) {
      for (const PointLight& l : d.lava) consider(l);
    }
    std::sort(picks.begin(), picks.end(), [](const Pick& a, const Pick& b) { return a.d < b.d; });
    if (picks.size() > size_t(std::min(o.most, 512))) picks.resize(size_t(std::min(o.most, 512)));
  }
  d.frame_lights.clear();
  d.frame_jobs.clear();
  int shadowed = 0;
  for (const Pick& p : picks) {
    const PointLight& l = *p.l;
    GpuLight g{};
    if (l.physical) {
      set4(g.pos, l.pos[0], l.pos[1], l.pos[2], l.reach);
      set4(g.colour, l.colour[0] * l.value, l.colour[1] * l.value, l.colour[2] * l.value, 1);
    } else {
      // The classic's light at half its reach, value / 2 of 128, matched by an inverse square:
      // the strength a metre off is that times the half reach squared.
      const double half = l.value * kUnitM / 2;
      const double at1 = l.value / 256.0 * half * half * o.strength * flicker_at(l, view.time_s);
      set4(g.pos, l.pos[0], l.pos[1], l.pos[2], l.reach * 1.5);
      set4(g.colour, l.colour[0] * at1, l.colour[1] * at1, l.colour[2] * at1, 0);
    }
    set4(g.info, -1, 0, 0, 0);
    if (!l.physical && shadowed < d.options.shadowed) {
      g.info[0] = float(shadowed * 6);
      for (int f = 0; f < 6; ++f) {
        const int i = f / 2;
        const double sign = f % 2 ? -1.0 : 1.0;
        GpuJob j{};
        set4(j.o, l.pos[0], l.pos[1], l.pos[2], g.pos[3]);
        double ax[3] = {0, 0, 0}, ay[3] = {0, 0, 0}, az[3] = {0, 0, 0};
        ax[(i + 1) % 3] = 1;
        ay[(i + 2) % 3] = 1;
        az[i] = sign;
        set4(j.x, ax[0], ax[1], ax[2], 0);
        set4(j.y, ay[0], ay[1], ay[2], 0);
        set4(j.z, az[0], az[1], az[2], 0);
        set4(j.info, 0, 0, 0, 0);
        d.frame_jobs.push_back(j);
      }
      ++shadowed;
    }
    d.frame_lights.push_back(g);
  }
  d.shadowed = shadowed;
  // The sun's map and the zenith's over the whole map.
  const double centre[3] = {(s.mins[0] + s.maxs[0]) / 2, (s.mins[1] + s.maxs[1]) / 2,
                            (s.mins[2] + s.maxs[2]) / 2};
  const double ext[3] = {s.maxs[0] - s.mins[0], s.maxs[1] - s.mins[1], s.maxs[2] - s.mins[2]};
  const double radius = 0.5 * std::sqrt(ext[0] * ext[0] + ext[1] * ext[1] + ext[2] * ext[2]) + 1;
  double sun[3];
  r::sun_direction(view, sun);
  const double toward[3] = {-sun[0], -sun[1], -sun[2]};
  const double down[3] = {0, -1, 0};
  d.staged.ortho[0] = ortho_job(toward, centre, radius, radius);
  d.staged.ortho[1] = ortho_job(down, centre, radius, radius);
  d.frame_jobs.push_back(d.staged.ortho[0]);
  d.frame_jobs.push_back(d.staged.ortho[1]);

  set4(d.staged.info, o.mode == Mode::v2 ? 1 : 0, o.lighting == Lighting::lights ? 1 : 0,
       double(d.frame_lights.size()), o.ambient);
  set4(d.staged.info2, view.time_s, kCubeSize, kOrthoSize, o.brightness);
  set4(d.staged.info3, o.minlight, o.lighting == Lighting::lights ? o.bounce : 0, 0, 0);
  if (s.sky_texture < s.map.textures.size()) {
    // The sky texture's rectangle: any vertex of a sky face carries it.
    for (size_t k = 0; k < s.vertices.size(); ++k) {
      if (s.vertices[k].kind == kSky) {
        std::memcpy(d.staged.sky, s.vertices[k].rect, sizeof d.staged.sky);
        break;
      }
    }
  }
  device.queue.WriteBuffer(d.uniform, 0, &d.staged, sizeof d.staged);
  if (!d.frame_lights.empty()) {
    device.queue.WriteBuffer(d.lights, 0, d.frame_lights.data(),
                             d.frame_lights.size() * sizeof(GpuLight));
  }
  device.queue.WriteBuffer(d.jobs, 0, d.frame_jobs.data(), d.frame_jobs.size() * sizeof(GpuJob));
}

void encode_bsp(const BspDraw& d, const gpu::PipelineSet& pipelines, r::FrameRecorder& rec,
                wgpu::CommandEncoder& encoder, const r::FrameTargets& targets,
                const wgpu::BindGroup& frame) {
  const bool v2 = d.options.mode == Mode::v2;
  const bool lights = v2 && d.options.lighting == Lighting::lights;
  // The ranges the shadows see: the solid faces (the sky's do not stop the sun), and the
  // ranges drawn: v2 leaves the liquids to our bodies.
  const auto draw_solid = [&](wgpu::RenderPassEncoder& pass, uint32_t instance) {
    pass.DrawIndexed(d.range[kSolid][1], 1, d.range[kSolid][0], 0, instance);
  };
  rec.compute(encoder, "bsp.shadows").End();
  if (lights) {
    const auto shadow_pass = [&](const wgpu::TextureView& view, uint32_t job, bool sky) {
      wgpu::RenderPassDepthStencilAttachment depth{};
      depth.view = view;
      depth.depthLoadOp = wgpu::LoadOp::Clear;
      depth.depthStoreOp = wgpu::StoreOp::Store;
      depth.depthClearValue = 1.0f;
      wgpu::RenderPassDescriptor desc{};
      desc.depthStencilAttachment = &depth;
      wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&desc);
      pass.SetPipeline(pipelines.render(d.p_shadow));
      pass.SetBindGroup(0, d.shadow_group);
      pass.SetVertexBuffer(0, d.vertices);
      pass.SetIndexBuffer(d.indices, wgpu::IndexFormat::Uint32);
      draw_solid(pass, job);
      if (sky) pass.DrawIndexed(d.range[kSky][1], 1, d.range[kSky][0], 0, job);
      pass.End();
    };
    for (int k = 0; k < d.shadowed * 6; ++k)
      shadow_pass(d.cube_layers[size_t(k)], uint32_t(k), true);
    const uint32_t first_ortho = uint32_t(d.shadowed * 6);
    shadow_pass(d.ortho_layers[0], first_ortho, false);
    shadow_pass(d.ortho_layers[1], first_ortho + 1, false);
  }
  {
    wgpu::RenderPassColorAttachment colours[3]{};
    colours[0].view = targets.hdr_view;
    colours[1].view = targets.motion_view;
    colours[2].view = d.mask_view;
    for (auto& c : colours) {
      c.loadOp = wgpu::LoadOp::Clear;
      c.storeOp = wgpu::StoreOp::Store;
      c.clearValue = {0, 0, 0, 0};
    }
    wgpu::RenderPassDepthStencilAttachment depth{};
    depth.view = targets.depth_view;
    depth.depthLoadOp = wgpu::LoadOp::Clear;
    depth.depthStoreOp = wgpu::StoreOp::Store;
    depth.depthClearValue = 0.0f;
    wgpu::RenderPassDescriptor desc{};
    desc.colorAttachmentCount = 3;
    desc.colorAttachments = colours;
    desc.depthStencilAttachment = &depth;
    wgpu::RenderPassEncoder pass = rec.render(encoder, desc, "bsp");
    pass.SetPipeline(pipelines.render(d.p_draw));
    pass.SetBindGroup(0, frame);
    pass.SetBindGroup(1, d.group);
    pass.SetVertexBuffer(0, d.vertices);
    pass.SetIndexBuffer(d.indices, wgpu::IndexFormat::Uint32);
    const Kind drawn[] = {kSolid, kSky, kWarp, kWater, kSlime, kLava};
    for (Kind k : drawn) {
      if (v2 && (k == kWater || k == kSlime || k == kLava)) continue;
      if (d.range[k][1] == 0) continue;
      pass.DrawIndexed(d.range[k][1], 1, d.range[k][0], 0, 0);
      ++rec.current().draws;
    }
    pass.End();
  }
  if (v2) {
    wgpu::RenderPassDepthStencilAttachment depth{};
    depth.view = targets.depth_view;
    depth.depthLoadOp = wgpu::LoadOp::Load;
    depth.depthStoreOp = wgpu::StoreOp::Store;
    wgpu::RenderPassDescriptor desc{};
    desc.depthStencilAttachment = &depth;
    wgpu::RenderPassEncoder pass = rec.render(encoder, desc, "bsp.sky");
    pass.SetPipeline(pipelines.render(d.p_fix));
    pass.SetBindGroup(0, d.fix_group);
    pass.Draw(3);
    ++rec.current().draws;
    pass.End();
  } else {
    rec.compute(encoder, "bsp.sky").End();
  }
}

}  // namespace ge::brush::app
