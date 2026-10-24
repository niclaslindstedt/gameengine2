// The view under water: the presets, the pipeline, the pass (underwater.hpp).
//
// ge:summary The view under water's presets, pipeline, uniforms and pass
// ge:keywords render underwater presets pipeline pass uniforms
// ge:skill render water
// ge:test tests/render_water_test.cpp
#include "render/underwater.hpp"

#include "render/underwater.wgsl.hpp"

namespace ge::render {

bool underwater_look(std::string_view name, UnderwaterLook& out) {
  UnderwaterLook l;
  if (name == "eyes-open") {
    // the default
  } else if (name == "mask") {
    l.blur = 0.6;
    l.magnify = 1.33;
    l.warp = 0.0008;
  } else if (name == "murky") {
    l.fog = 3;
    l.blur = 9;
    l.blur_range = 1;
  } else if (name == "classic") {
    l.fog = 0.25;
    l.blur = 0;
    l.warp = 0.006;
    l.warp_hz = 0.5;
    l.window = 0;
    l.tint[0] = 0.55;
    l.tint[1] = 0.85;
    l.tint[2] = 0.75;
    l.tint_mix = 0.6;
  } else {
    return false;
  }
  out = l;
  return true;
}

void create_underwater(gpu::Device& device, gpu::PipelineSet& pipelines, Underwater& under,
                       const wgpu::BindGroupLayout& frame) {
  under.layout =
      make_layout(device, "water.under", {Slot::uniform, Slot::texture, Slot::texture_raw},
                  wgpu::ShaderStage::Fragment);
  under.uniform = make_buffer(device, "water.under", sizeof(UnderGpu), wgpu::BufferUsage::Uniform);
  const wgpu::ShaderModule module = gpu::create_shader(device, wgsl::underwater, "water.under");
  wgpu::ColorTargetState colour{};
  colour.format = kHdrFormat;
  wgpu::FragmentState fragment{};
  fragment.module = module;
  fragment.entryPoint = "fs_main";
  fragment.targetCount = 1;
  fragment.targets = &colour;
  wgpu::RenderPipelineDescriptor desc{};
  desc.label = "water.under";
  desc.layout = make_pipeline_layout(device, "water.under", {frame, under.layout});
  desc.vertex.module = module;
  desc.vertex.entryPoint = "vs_main";
  desc.fragment = &fragment;
  under.p_under = pipelines.add_render(device, desc);
}

void bind_underwater(gpu::Device& device, Underwater& under, const Water& water) {
  under.group = make_group(device, "water.under", under.layout,
                           {under.uniform, water.scene_colour_view, water.scene_depth_view});
}

void prepare_underwater(gpu::Device& device, Underwater& under, const Water& water,
                        const FrameView& view, uint32_t render_height) {
  under.active = water.eye_under >= 0;
  if (!under.active) return;
  const Water::Slot& slot = water.bodies[static_cast<size_t>(water.eye_under)];
  const UnderwaterLook& l = under.look;
  UnderGpu& u = under.staged;
  u.info[0] = slot.staged.info[0];
  u.info[1] = static_cast<float>(slot.staged.info[0] - view.eye[1]);
  u.info[2] = static_cast<float>(l.blur * render_height / 1080.0);
  u.info[3] = static_cast<float>(l.blur_range);
  for (int i = 0; i < 4; ++i) {
    u.c[i] = slot.staged.c[i];
    u.kd[i] = slot.staged.kd[i];
    u.veil[i] = slot.veil[i];
  }
  u.look[0] = static_cast<float>(l.magnify);
  u.look[1] = static_cast<float>(l.warp);
  u.look[2] = static_cast<float>(l.warp_hz);
  u.look[3] = static_cast<float>(l.window);
  for (int i = 0; i < 3; ++i) u.tint[i] = static_cast<float>(l.tint[i]);
  u.tint[3] = static_cast<float>(l.tint_mix);
  u.fog[0] = static_cast<float>(l.fog);
  u.fog[1] = static_cast<float>(l.adapt);
  device.queue.WriteBuffer(under.uniform, 0, &u, sizeof u);
}

void encode_underwater(const Underwater& under, const gpu::PipelineSet& pipelines,
                       FrameRecorder& rec, wgpu::CommandEncoder& encoder,
                       const FrameTargets& targets, const wgpu::BindGroup& frame) {
  if (!under.active) {
    // The table's row runs empty, so every later pass keeps its timing row.
    rec.compute(encoder, "water.under").End();
    return;
  }
  wgpu::RenderPassColorAttachment colour{};
  colour.view = targets.hdr_view;
  colour.loadOp = wgpu::LoadOp::Load;
  colour.storeOp = wgpu::StoreOp::Store;
  wgpu::RenderPassDescriptor desc{};
  desc.colorAttachmentCount = 1;
  desc.colorAttachments = &colour;
  wgpu::RenderPassEncoder pass = rec.render(encoder, desc, "water.under");
  pass.SetPipeline(pipelines.render(under.p_under));
  pass.SetBindGroup(0, frame);
  pass.SetBindGroup(1, under.group);
  pass.Draw(3);
  ++rec.current().draws;
  pass.End();
}

}  // namespace ge::render
