// The water's detail band, its scene copies, bodies, pipelines and passes.
//
// ge:summary The water pass's detail waves, resources, pipelines, levels and passes
// ge:keywords render water detail-waves spectrum-tail scene-copy pipelines levels passes
// ge:skill render water
// ge:test tests/render_water_test.cpp
#include "render/water.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/libm/libm.hpp"
#include "core/prng.hpp"
#include "render/water.wgsl.hpp"
#include "render/water_copy.wgsl.hpp"
#include "render/water_stream.hpp"
#include "render/water_stream.wgsl.hpp"
#include "water/spectrum.hpp"

namespace ge::render {

namespace {

namespace lm = core::libm;
constexpr double kPi = 3.14159265358979323846;
constexpr uint32_t kWaveSlots = water::kMaxComponents + kWaterDetailWaves;
// How far down a covered coarse vertex sinks, in its level's spacings; where the morph starts.
constexpr float kSink = 2.0f;
constexpr float kMorphStart = 0.8f;

// The deep-water wavenumber of an angular frequency.
double deep_k(double w) { return w * w / water::kGravity; }
double deep_w(double length) { return std::sqrt(water::kGravity * 2 * kPi / length); }

void stage_body(Water::Slot& slot, const water::Body& body) {
  WaterGpuBody& b = slot.staged;
  b.info[0] = static_cast<float>(body.level);
  b.info[1] = 1.0f;
  b.info[2] = static_cast<float>(body.waves.count);
  for (int i = 0; i < 4; ++i) b.box[i] = static_cast<float>(body.box[i]);
  b.grid[0] = static_cast<float>(std::clamp(slot.draw.levels, 1, kWaterLevels));
  b.grid[1] = kSink;
  b.grid[2] = kMorphStart;
  b.grid[3] = static_cast<float>(slot.draw.rim);
  b.rough[1] = static_cast<float>(slot.draw.calm_mss);
  b.rough[2] = static_cast<float>(slot.draw.refraction);
  const water::Optics& o = body.optics;
  const auto put = [](float* out, const water::Rgb& c) {
    out[0] = static_cast<float>(c.r);
    out[1] = static_cast<float>(c.g);
    out[2] = static_cast<float>(c.b);
    out[3] = 0;
  };
  put(b.c, o.c);
  put(b.kd, o.kd);
  put(b.deep, o.deep);
  put(b.emission, o.emission);
  put(slot.veil, o.veil);
}

void upload_waves(gpu::Device& device, Water::Slot& slot, const water::Body& body) {
  const WaterDetail detail =
      detail_waves(body.waves, kWaterDetailWaves, slot.draw.seed + 977u * slot.draw.seed);
  slot.staged.info[3] = static_cast<float>(detail.waves.size());
  slot.staged.rough[0] = static_cast<float>(detail.tail_mss);
  std::vector<float> data(size_t{kWaveSlots} * 8, 0.0f);
  size_t i = 0;
  const auto put = [&](const water::Component& c) {
    float* w = &data[i * 8];
    w[0] = static_cast<float>(c.kx);
    w[1] = static_cast<float>(c.kz);
    w[2] = static_cast<float>(c.a);
    w[3] = static_cast<float>(c.w);
    // The phase taken round once, so a float keeps its bits of it.
    w[4] = static_cast<float>(std::fmod(c.phase, 2 * kPi));
    w[5] = static_cast<float>(c.k);
    ++i;
  };
  for (int c = 0; c < body.waves.count; ++c) put(body.waves.c[c]);
  for (const water::Component& c : detail.waves) put(c);
  device.queue.WriteBuffer(slot.waves, 0, data.data(), data.size() * sizeof(float));
}

}  // namespace

WaterDetail detail_waves(const water::Waves& laid, int count, uint32_t seed, double shortest,
                         double finest) {
  WaterDetail out;
  const water::Sea& sea = laid.sea;
  if (sea.hs <= 0 || sea.tp <= 0 || count <= 0) return out;
  const double norm = water::normaliser(sea);
  const double w0 = laid.w_hi > 0 ? laid.w_hi : 2 * kPi / sea.tp * 3;
  const double w1 = deep_w(shortest), w2 = deep_w(finest);
  if (w1 <= w0) return out;
  core::Rng rng = core::make_rng(seed);
  // Slices even in the log of the frequency; each its energy at its middle.
  const double step = lm::log(w1 / w0) / count;
  for (int i = 0; i < count; ++i) {
    const double lo = w0 * lm::exp(step * i), hi = w0 * lm::exp(step * (i + 1));
    double e = 0;
    constexpr int kTaps = 8;
    for (int j = 0; j < kTaps; ++j) {
      const double w = lo + (hi - lo) * (j + 0.5) / kTaps;
      e += water::jonswap_at(sea, w, norm) * (hi - lo) / kTaps;
    }
    const double w = std::sqrt(lo * hi), k = deep_k(w);
    // The short waves spread wider than the peak: up to a right angle either side.
    const double off = (core::next(rng) * 2 - 1) * kPi * 0.5;
    const double heading = sea.toward + off;
    water::Component c;
    c.w = w;
    c.k = k;
    c.kx = k * lm::sin(heading);
    c.kz = k * lm::cos(heading);
    c.a = std::sqrt(2 * e);
    c.phase = core::next(rng) * 2 * kPi;
    out.mss += 0.5 * (k * c.a) * (k * c.a);
    out.waves.push_back(c);
  }
  // The tail's slope variance, the integral of k^2 S over the band past the detail's.
  constexpr int kTail = 64;
  const double tail = lm::log(w2 / w1) / kTail;
  for (int i = 0; i < kTail; ++i) {
    const double lo = w1 * lm::exp(tail * i), hi = w1 * lm::exp(tail * (i + 1));
    const double w = std::sqrt(lo * hi), k = deep_k(w);
    out.tail_mss += k * k * water::jonswap_at(sea, w, norm) * (hi - lo);
  }
  return out;
}

void create_water(gpu::Device& device, gpu::PipelineSet& pipelines, Water& water,
                  const wgpu::BindGroupLayout& frame) {
  water.layout = make_layout(
      device, "water",
      {Slot::uniform, Slot::storage_read, Slot::texture, Slot::texture_raw, Slot::storage_read},
      wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment);
  water.copy_layout =
      make_layout(device, "water.copy", {Slot::texture, Slot::depth}, wgpu::ShaderStage::Fragment);

  {
    const wgpu::ShaderModule module = gpu::create_shader(device, wgsl::water_copy, "water.copy");
    wgpu::ColorTargetState colours[2] = {};
    colours[0].format = kHdrFormat;
    colours[1].format = wgpu::TextureFormat::R32Float;
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 2;
    fragment.targets = colours;
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "water.copy";
    desc.layout = make_pipeline_layout(device, "water.copy", {water.copy_layout});
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_main";
    desc.fragment = &fragment;
    water.p_copy = pipelines.add_render(device, desc);
  }
  {
    const wgpu::ShaderModule module = gpu::create_shader(device, wgsl::water, "water");
    wgpu::ConstantEntry grid{};
    grid.key = "kGrid";
    grid.value = kWaterGrid;
    wgpu::ColorTargetState colours[2] = {};
    colours[0].format = kHdrFormat;
    colours[1].format = kMotionFormat;
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_main";
    fragment.constantCount = 1;
    fragment.constants = &grid;
    fragment.targetCount = 2;
    fragment.targets = colours;
    wgpu::DepthStencilState depth{};
    depth.format = kDepthFormat;
    depth.depthWriteEnabled = true;
    depth.depthCompare = wgpu::CompareFunction::Greater;
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "water.draw";
    desc.layout = make_pipeline_layout(device, "water.draw", {frame, water.layout});
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_main";
    desc.vertex.constantCount = 1;
    desc.vertex.constants = &grid;
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.depthStencil = &depth;
    desc.fragment = &fragment;
    water.p_draw = pipelines.add_render(device, desc);
  }
  {
    Water::Streams& st = water.streams;
    st.layout =
        make_layout(device, "water.stream",
                    {Slot::storage_read, Slot::storage_read, Slot::texture, Slot::texture_raw},
                    wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment);
    st.optics = make_buffer(device, "water.stream.optics", size_t{kStreamBodies} * 64,
                            wgpu::BufferUsage::Storage);
    st.vertex_bytes = 4096;
    st.vertices =
        make_buffer(device, "water.stream.vertices", st.vertex_bytes, wgpu::BufferUsage::Storage);
    st.index_bytes = 4096;
    st.indices =
        make_buffer(device, "water.stream.indices", st.index_bytes, wgpu::BufferUsage::Index);
    const wgpu::ShaderModule module =
        gpu::create_shader(device, wgsl::water_stream, "water.stream");
    wgpu::ColorTargetState colours[2] = {};
    colours[0].format = kHdrFormat;
    colours[1].format = kMotionFormat;
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 2;
    fragment.targets = colours;
    wgpu::DepthStencilState depth{};
    depth.format = kDepthFormat;
    depth.depthWriteEnabled = true;
    depth.depthCompare = wgpu::CompareFunction::Greater;
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "water.stream";
    desc.layout = make_pipeline_layout(device, "water.stream", {frame, st.layout});
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_main";
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.depthStencil = &depth;
    desc.fragment = &fragment;
    st.p_draw = pipelines.add_render(device, desc);
  }
  // The grid's indices: kWaterGrid^2 quads of two triangles over (kWaterGrid + 1)^2 vertices.
  std::vector<uint32_t> idx;
  idx.reserve(size_t{kWaterGrid} * kWaterGrid * 6);
  const uint32_t n = kWaterGrid + 1;
  for (uint32_t y = 0; y < kWaterGrid; ++y) {
    for (uint32_t x = 0; x < kWaterGrid; ++x) {
      const uint32_t a = y * n + x, b = a + 1, c = a + n, d = c + 1;
      idx.insert(idx.end(), {a, c, b, b, c, d});
    }
  }
  water.indices =
      make_buffer(device, "water.indices", idx.size() * sizeof(uint32_t), wgpu::BufferUsage::Index);
  device.queue.WriteBuffer(water.indices, 0, idx.data(), idx.size() * sizeof(uint32_t));
}

void create_water_targets(gpu::Device& device, Water& water, const FrameTargets& targets) {
  const auto usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
  water.scene_colour = make_texture(device, "water.scene.colour", targets.width, targets.height, 1,
                                    kHdrFormat, usage);
  water.scene_depth = make_texture(device, "water.scene.depth", targets.width, targets.height, 1,
                                   wgpu::TextureFormat::R32Float, usage);
  water.scene_colour_view = water.scene_colour.CreateView();
  water.scene_depth_view = water.scene_depth.CreateView();
  water.copy_group =
      make_group(device, "water.copy", water.copy_layout, {targets.hdr_view, targets.depth_view});
  water.streams.group = make_group(device, "water.stream", water.streams.layout,
                                   {water.streams.optics, water.streams.vertices,
                                    water.scene_colour_view, water.scene_depth_view});
  for (Water::Slot& slot : water.bodies) {
    slot.group = make_group(
        device, "water", water.layout,
        {slot.uniform, slot.waves, water.scene_colour_view, water.scene_depth_view, slot.rings});
  }
}

int add_water_body(gpu::Device& device, Water& water, const water::Body& body,
                   const WaterDraw& draw) {
  Water::Slot slot;
  slot.draw = draw;
  slot.uniform =
      make_buffer(device, "water.body", sizeof(WaterGpuBody), wgpu::BufferUsage::Uniform);
  slot.waves =
      make_buffer(device, "water.waves", size_t{kWaveSlots} * 32, wgpu::BufferUsage::Storage);
  slot.rings =
      make_buffer(device, "water.rings", size_t{kWaterRings} * 32, wgpu::BufferUsage::Storage);
  stage_body(slot, body);
  upload_waves(device, slot, body);
  if (water.scene_colour_view) {
    slot.group = make_group(
        device, "water", water.layout,
        {slot.uniform, slot.waves, water.scene_colour_view, water.scene_depth_view, slot.rings});
  }
  water.bodies.push_back(std::move(slot));
  return static_cast<int>(water.bodies.size()) - 1;
}

void set_water_body(gpu::Device& device, Water& water, int index, const water::Body& body) {
  Water::Slot& slot = water.bodies[static_cast<size_t>(index)];
  stage_body(slot, body);
  upload_waves(device, slot, body);
}

void set_water_wakes(gpu::Device& device, Water& water, int index, const water::Wakes& wakes,
                     int body) {
  Water::Slot& slot = water.bodies[static_cast<size_t>(index)];
  std::vector<float> data;
  data.reserve(size_t{kWaterRings} * 8);
  int n = 0;
  for (auto r = wakes.rings.rbegin(); r != wakes.rings.rend() && n < kWaterRings; ++r) {
    if (r->body != body) continue;
    data.insert(data.end(), {static_cast<float>(r->x), static_cast<float>(r->z),
                             static_cast<float>(r->t0), static_cast<float>(r->volume),
                             static_cast<float>(r->size), static_cast<float>(r->shortest), 0, 0});
    ++n;
  }
  slot.staged.wake[0] = static_cast<float>(n);
  slot.staged.wake[1] = static_cast<float>(wakes.life);
  slot.staged.wake[2] = static_cast<float>(wakes.shortest);
  if (n > 0) device.queue.WriteBuffer(slot.rings, 0, data.data(), data.size() * sizeof(float));
}

void prepare_water(gpu::Device& device, Water& water, const FrameView& view) {
  water.eye_under = -1;
  for (Water::Slot& slot : water.bodies) {
    WaterGpuBody& b = slot.staged;
    slot.shown = !slot.hidden && view.eye[1] > b.info[0];
    if (!slot.hidden && !slot.shown && view.eye[0] >= b.box[0] && view.eye[0] <= b.box[2] &&
        view.eye[2] >= b.box[1] && view.eye[2] <= b.box[3] && water.eye_under < 0) {
      water.eye_under = static_cast<int>(&slot - water.bodies.data());
    }
    const int levels = static_cast<int>(b.grid[0]);
    const double half = kWaterGrid / 2.0;
    for (int i = 0; i < levels; ++i) {
      const double s = slot.draw.base_spacing * std::ldexp(1.0, i);
      const double cx = std::round(view.eye[0] / (2 * s)) * 2 * s;
      const double cz = std::round(view.eye[2] / (2 * s)) * 2 * s;
      b.levels[i][0] = static_cast<float>(cx - half * s);
      b.levels[i][1] = static_cast<float>(cz - half * s);
      b.levels[i][2] = static_cast<float>(s);
      b.levels[i][3] = 0;
    }
    device.queue.WriteBuffer(slot.uniform, 0, &b, sizeof b);
  }
}

void encode_water(const Water& water, const gpu::PipelineSet& pipelines, FrameRecorder& rec,
                  wgpu::CommandEncoder& encoder, const FrameTargets& targets,
                  const wgpu::BindGroup& frame) {
  // Nothing drawn and no eye under the water: the rows run empty (no copy), so every later pass
  // keeps its timing row.
  const bool drawn = std::any_of(water.bodies.begin(), water.bodies.end(),
                                 [](const Water::Slot& s) { return s.shown; }) ||
                     water.streams.count > 0;
  if (!drawn && water.eye_under < 0) {
    rec.compute(encoder, "water.copy").End();
    rec.compute(encoder, "water").End();
    return;
  }
  {
    wgpu::RenderPassColorAttachment colours[2]{};
    colours[0].view = water.scene_colour_view;
    colours[1].view = water.scene_depth_view;
    for (auto& c : colours) {
      c.loadOp = wgpu::LoadOp::Clear;
      c.storeOp = wgpu::StoreOp::Store;
    }
    wgpu::RenderPassDescriptor desc{};
    desc.colorAttachmentCount = 2;
    desc.colorAttachments = colours;
    wgpu::RenderPassEncoder pass = rec.render(encoder, desc, "water.copy");
    pass.SetPipeline(pipelines.render(water.p_copy));
    pass.SetBindGroup(0, water.copy_group);
    pass.Draw(3);
    ++rec.current().draws;
    pass.End();
  }
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
  wgpu::RenderPassEncoder pass = rec.render(encoder, desc, "water");
  pass.SetPipeline(pipelines.render(water.p_draw));
  pass.SetBindGroup(0, frame);
  pass.SetIndexBuffer(water.indices, wgpu::IndexFormat::Uint32);
  for (const Water::Slot& slot : water.bodies) {
    if (!slot.shown) continue;
    pass.SetBindGroup(1, slot.group);
    pass.DrawIndexed(kWaterGrid * kWaterGrid * 6, static_cast<uint32_t>(slot.staged.grid[0]));
    ++rec.current().draws;
  }
  if (water.streams.count > 0) {
    pass.SetPipeline(pipelines.render(water.streams.p_draw));
    pass.SetBindGroup(1, water.streams.group);
    pass.SetIndexBuffer(water.streams.indices, wgpu::IndexFormat::Uint32);
    pass.DrawIndexed(water.streams.count);
    ++rec.current().draws;
  }
  pass.End();
}

}  // namespace ge::render
