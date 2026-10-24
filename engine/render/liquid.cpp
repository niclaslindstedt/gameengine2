// Lava and slime drawn: the looks, the scene copies, the bodies, the pipelines and the passes.
//
// ge:summary The liquid pass's named looks, resources, pipelines and passes
// ge:keywords render liquid lava slime looks glow-table ripples scene-copy pipelines passes
// ge:skill render water
// ge:test tests/render_liquid_test.cpp
#include "render/liquid.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "render/liquid.wgsl.hpp"
#include "render/water_copy.wgsl.hpp"
#include "water/glow.hpp"
#include "water/liquid.hpp"

namespace ge::render {

namespace {

constexpr double kKelvin = 273.15;
constexpr uint32_t kRippleSeed = 0x5117u;

LiquidLook lava(double cover, double crack, double plate, double ropes, double cold,
                double age_rate) {
  LiquidLook l;
  l.kind = LiquidKind::lava;
  l.lava.cover = cover;
  l.lava.crack = crack;
  l.lava.plate = plate;
  l.lava.ropes = ropes;
  l.lava.crust_cold = cold;
  l.lava.age_rate = age_rate;
  return l;
}

LiquidLook slime(water::Rgb absorb, water::Rgb scatter, double rough) {
  LiquidLook l;
  l.kind = LiquidKind::slime;
  l.slime.absorb = absorb;
  l.slime.scatter = scatter;
  l.slime.rough = rough;
  return l;
}

struct Named {
  std::string_view name;
  LiquidLook look;
};

std::array<Named, 7> build() {
  // Lava: a molten lake with its crust breaking into plates; the same crusted over near its
  // solidus.
  LiquidLook molten = lava(0.62, 0.07, 1.6, 1, 380, 0.02);
  LiquidLook crusted = lava(0.93, 0.035, 2.4, 0.3, 300, 0.008);
  crusted.lava.core = 1100;
  crusted.lava.crust_hot = 720;
  // Toxic: an opaque green, faintly glowing, bubbling.
  LiquidLook toxic = slime({150, 6, 240}, {30, 60, 30}, 0.12);
  toxic.slime.glow = {6, 60, 4};
  toxic.slime.pulse = 0.3;
  toxic.slime.bubbles = 1.5;
  toxic.slime.bubble_size = 0.08;
  // A clear gel: little scattering, a green-cyan tint over what lies in it, glassy.
  LiquidLook gel = slime({0.45, 0.08, 0.25}, {0.06, 0.06, 0.06}, 0.03);
  // A pale mucus: milky, yellow-green, very glossy, a few small bubbles.
  LiquidLook mucus = slime({3, 2, 12}, {20, 20, 20}, 0.04);
  mucus.slime.bubbles = 2;
  mucus.slime.bubble_size = 0.035;
  // A sludge: olive-brown, matte under its scum, bubbling, an oily sheen between the patches.
  LiquidLook sludge = slime({60, 50, 150}, {50, 50, 50}, 0.3);
  sludge.slime.scum = 0.45;
  sludge.slime.scum_albedo = {0.30, 0.28, 0.18};
  sludge.slime.bubbles = 3;
  sludge.slime.bubble_size = 0.05;
  sludge.slime.film = 320;
  // Tar: black glass with an oil's rainbow, a rare slow bubble.
  LiquidLook tar = slime({5000, 5000, 5000}, {0.5, 0.5, 0.5}, 0.05);
  tar.slime.film = 420;
  tar.slime.bubbles = 0.08;
  tar.slime.bubble_size = 0.14;
  return {{{"lava", molten},
           {"lava-crusted", crusted},
           {"slime", toxic},
           {"slime-gel", gel},
           {"slime-mucus", mucus},
           {"slime-sludge", sludge},
           {"slime-tar", tar}}};
}

const std::array<Named, 7>& rows() {
  static const std::array<Named, 7> table = build();
  return table;
}

void put(float* out, double a, double b, double c, double d = 0) {
  out[0] = static_cast<float>(a);
  out[1] = static_cast<float>(b);
  out[2] = static_cast<float>(c);
  out[3] = static_cast<float>(d);
}
void put(float* out, const water::Rgb& c, double w = 0) { put(out, c.r, c.g, c.b, w); }

void stage_body(Liquids::Slot& slot, const water::Body& body, const LiquidLook& look, int ripples) {
  LiquidGpuBody& b = slot.staged;
  put(b.info, body.level, look.kind == LiquidKind::lava ? 0 : 1, ripples, 0);
  for (int i = 0; i < 4; ++i) b.box[i] = static_cast<float>(body.box[i]);
  put(b.flow, look.flow[0], look.flow[1], look.open_sky, look.room);
  const LavaLook& l = look.lava;
  put(b.lava0, l.core + kKelvin, l.crust_hot + kKelvin, l.crust_cold + kKelvin, l.cover);
  put(b.lava1, l.plate, l.crack, l.ropes, l.age_rate);
  put(b.lava2, l.emissivity, l.crust_rough, l.melt_rough, 0);
  put(b.crust, l.crust_albedo);
  const SlimeLook& s = look.slime;
  put(b.absorb, s.absorb);
  put(b.scatter, s.scatter);
  put(b.glow, s.glow, s.pulse);
  put(b.slime0, s.rough, s.film, s.scum, s.bubbles);
  put(b.slime1, s.bubble_size, s.grime, s.debris, 0);
  put(b.scum, s.scum_albedo);
  const std::vector<water::Rgb> table = water::glow_table(kGlowLo, kGlowHi, kGlowEntries, 1.0);
  for (int i = 0; i < kGlowEntries; ++i) put(b.table[i], table[static_cast<size_t>(i)]);
}

// The ripples a body draws: the wind's sea over its fetch cut by its liquid (none on lava).
int upload_ripples(gpu::Device& device, Liquids::Slot& slot, const water::Body& body,
                   const LiquidLook& look, double wind, double toward, double fetch) {
  std::vector<float> data(size_t{kLiquidRipples} * 8, 0.0f);
  int n = 0;
  if (look.kind == LiquidKind::slime && wind > 0) {
    const std::vector<water::Component> r = water::ripples(
        body.stuff.liquid, body.stuff.density, wind, toward, fetch, kLiquidRipples, kRippleSeed);
    for (const water::Component& c : r) {
      if (c.a <= 0) continue;
      float* w = &data[static_cast<size_t>(n) * 8];
      put(w, c.kx, c.kz, c.a, c.w);
      put(w + 4, std::fmod(c.phase, 6.283185307179586), c.k, 0, 0);
      ++n;
    }
  }
  device.queue.WriteBuffer(slot.ripples, 0, data.data(), data.size() * sizeof(float));
  return n;
}

const std::array<std::string_view, 7>& names() {
  static const std::array<std::string_view, 7> out = [] {
    std::array<std::string_view, 7> n{};
    for (size_t i = 0; i < rows().size(); ++i) n[i] = rows()[i].name;
    return n;
  }();
  return out;
}

}  // namespace

const LiquidLook* find_liquid_look(std::string_view name) {
  for (const Named& n : rows())
    if (n.name == name) return &n.look;
  return nullptr;
}

std::span<const std::string_view> liquid_look_names() { return names(); }

double adapt_for(const LiquidLook& look, double key) {
  if (look.kind == LiquidKind::slime) {
    const water::Rgb& g = look.slime.glow;
    return key * (0.2126 * g.r + 0.7152 * g.g + 0.0722 * g.b);
  }
  const LavaLook& l = look.lava;
  return key * water::glow_of(l.core + kKelvin, l.emissivity).luminance;
}

void create_liquid(gpu::Device& device, gpu::PipelineSet& pipelines, Liquids& liquids,
                   const wgpu::BindGroupLayout& frame) {
  liquids.layout = make_layout(
      device, "liquid", {Slot::uniform, Slot::storage_read, Slot::texture, Slot::texture_raw},
      wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment);
  liquids.copy_layout =
      make_layout(device, "liquid.copy", {Slot::texture, Slot::depth}, wgpu::ShaderStage::Fragment);
  {
    const wgpu::ShaderModule module = gpu::create_shader(device, wgsl::water_copy, "liquid.copy");
    wgpu::ColorTargetState colours[2] = {};
    colours[0].format = kHdrFormat;
    colours[1].format = wgpu::TextureFormat::R32Float;
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 2;
    fragment.targets = colours;
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "liquid.copy";
    desc.layout = make_pipeline_layout(device, "liquid.copy", {liquids.copy_layout});
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_main";
    desc.fragment = &fragment;
    liquids.p_copy = pipelines.add_render(device, desc);
  }
  {
    const wgpu::ShaderModule module = gpu::create_shader(device, wgsl::liquid, "liquid");
    wgpu::ConstantEntry grid{};
    grid.key = "kGrid";
    grid.value = kLiquidGrid;
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
    desc.label = "liquid.draw";
    desc.layout = make_pipeline_layout(device, "liquid.draw", {frame, liquids.layout});
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_main";
    desc.vertex.constantCount = 1;
    desc.vertex.constants = &grid;
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.depthStencil = &depth;
    desc.fragment = &fragment;
    liquids.p_draw = pipelines.add_render(device, desc);
  }
  std::vector<uint32_t> idx;
  idx.reserve(size_t{kLiquidGrid} * kLiquidGrid * 6);
  const uint32_t n = kLiquidGrid + 1;
  for (uint32_t y = 0; y < kLiquidGrid; ++y) {
    for (uint32_t x = 0; x < kLiquidGrid; ++x) {
      const uint32_t a = y * n + x, b = a + 1, c = a + n, d = c + 1;
      idx.insert(idx.end(), {a, c, b, b, c, d});
    }
  }
  liquids.indices = make_buffer(device, "liquid.indices", idx.size() * sizeof(uint32_t),
                                wgpu::BufferUsage::Index);
  device.queue.WriteBuffer(liquids.indices, 0, idx.data(), idx.size() * sizeof(uint32_t));
}

void create_liquid_targets(gpu::Device& device, Liquids& liquids, const FrameTargets& targets) {
  const auto usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
  liquids.scene_colour = make_texture(device, "liquid.scene.colour", targets.width, targets.height,
                                      1, kHdrFormat, usage);
  liquids.scene_depth = make_texture(device, "liquid.scene.depth", targets.width, targets.height, 1,
                                     wgpu::TextureFormat::R32Float, usage);
  liquids.scene_colour_view = liquids.scene_colour.CreateView();
  liquids.scene_depth_view = liquids.scene_depth.CreateView();
  liquids.copy_group = make_group(device, "liquid.copy", liquids.copy_layout,
                                  {targets.hdr_view, targets.depth_view});
  for (Liquids::Slot& slot : liquids.bodies) {
    slot.group = make_group(
        device, "liquid", liquids.layout,
        {slot.uniform, slot.ripples, liquids.scene_colour_view, liquids.scene_depth_view});
  }
}

int add_liquid_body(gpu::Device& device, Liquids& liquids, const water::Body& body,
                    const LiquidLook& look, double wind, double toward, double fetch) {
  Liquids::Slot slot;
  slot.uniform =
      make_buffer(device, "liquid.body", sizeof(LiquidGpuBody), wgpu::BufferUsage::Uniform);
  slot.ripples = make_buffer(device, "liquid.ripples", size_t{kLiquidRipples} * 32,
                             wgpu::BufferUsage::Storage);
  const int n = upload_ripples(device, slot, body, look, wind, toward, fetch);
  stage_body(slot, body, look, n);
  if (liquids.scene_colour_view) {
    slot.group = make_group(
        device, "liquid", liquids.layout,
        {slot.uniform, slot.ripples, liquids.scene_colour_view, liquids.scene_depth_view});
  }
  liquids.bodies.push_back(std::move(slot));
  return static_cast<int>(liquids.bodies.size()) - 1;
}

void set_liquid_body(gpu::Device& device, Liquids& liquids, int index, const water::Body& body,
                     const LiquidLook& look, double wind, double toward, double fetch) {
  Liquids::Slot& slot = liquids.bodies[static_cast<size_t>(index)];
  const int n = upload_ripples(device, slot, body, look, wind, toward, fetch);
  stage_body(slot, body, look, n);
}

void prepare_liquid(gpu::Device& device, Liquids& liquids, const FrameView& view) {
  for (Liquids::Slot& slot : liquids.bodies) {
    slot.shown = !slot.hidden && view.eye[1] > slot.staged.info[0];
    device.queue.WriteBuffer(slot.uniform, 0, &slot.staged, sizeof slot.staged);
  }
}

void encode_liquid(const Liquids& liquids, const gpu::PipelineSet& pipelines, FrameRecorder& rec,
                   wgpu::CommandEncoder& encoder, const FrameTargets& targets,
                   const wgpu::BindGroup& frame) {
  const bool drawn = std::any_of(liquids.bodies.begin(), liquids.bodies.end(),
                                 [](const Liquids::Slot& s) { return s.shown; });
  if (!drawn) {
    rec.compute(encoder, "liquid.copy").End();
    rec.compute(encoder, "liquid").End();
    return;
  }
  {
    wgpu::RenderPassColorAttachment colours[2]{};
    colours[0].view = liquids.scene_colour_view;
    colours[1].view = liquids.scene_depth_view;
    for (auto& c : colours) {
      c.loadOp = wgpu::LoadOp::Clear;
      c.storeOp = wgpu::StoreOp::Store;
    }
    wgpu::RenderPassDescriptor desc{};
    desc.colorAttachmentCount = 2;
    desc.colorAttachments = colours;
    wgpu::RenderPassEncoder pass = rec.render(encoder, desc, "liquid.copy");
    pass.SetPipeline(pipelines.render(liquids.p_copy));
    pass.SetBindGroup(0, liquids.copy_group);
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
  wgpu::RenderPassEncoder pass = rec.render(encoder, desc, "liquid");
  pass.SetPipeline(pipelines.render(liquids.p_draw));
  pass.SetBindGroup(0, frame);
  pass.SetIndexBuffer(liquids.indices, wgpu::IndexFormat::Uint32);
  for (const Liquids::Slot& slot : liquids.bodies) {
    if (!slot.shown) continue;
    pass.SetBindGroup(1, slot.group);
    pass.DrawIndexed(kLiquidGrid * kLiquidGrid * 6);
    ++rec.current().draws;
  }
  pass.End();
}

}  // namespace ge::render
