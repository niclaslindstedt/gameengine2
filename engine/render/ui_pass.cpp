// The 2D pass's pipeline, atlas, images, instances and draw.
//
// ge:summary The 2D pass defined: the draw list to instances and paints, atlas packing, the draw
// ge:keywords render ui instances paints atlas shelf-packing glyph-upload image-upload ranges
// ge:skill render ui
// ge:test tests/render_ui_pass_test.cpp
#include "render/ui_pass.hpp"

#include <algorithm>
#include <cstring>
#include <string>

#include "core/output.hpp"

#include "render/ui_pass.wgsl.hpp"

namespace ge::render {

namespace {

constexpr uint32_t kKindBox = 0, kKindGlyph = 1, kKindSampled = 2;
constexpr uint32_t kPad = 1;  // texels between glyphs in the atlas, so no filter reads a neighbour

bool srgb_format(wgpu::TextureFormat f) {
  return f == wgpu::TextureFormat::RGBA8UnormSrgb || f == wgpu::TextureFormat::BGRA8UnormSrgb;
}

void make_main_group(gpu::Device& device, UiPass& pass) {
  pass.group = make_group(device, "ui", pass.layout,
                          {pass.uniforms, pass.items, pass.paints, pass.atlas_view, pass.sampler});
}

// Room for `n` of `size`-byte elements in `buffer`, growing it to the next power of two.
bool grow(gpu::Device& device, wgpu::Buffer& buffer, uint32_t& room, uint32_t n, uint64_t size,
          const char* label) {
  if (n <= room && buffer) return false;
  uint32_t want = std::max<uint32_t>(room, 64);
  while (want < n) want *= 2;
  buffer = make_buffer(device, label, want * size, wgpu::BufferUsage::Storage);
  room = want;
  return true;
}

UiPass::Picture make_picture(gpu::Device& device, const UiPass& pass, uint32_t width,
                             uint32_t height, const wgpu::TextureView* view) {
  UiPass::Picture p;
  p.width = width, p.height = height;
  if (view) {
    p.view = *view;
  } else {
    p.texture = make_texture(device, "ui.image", width, height, 1, wgpu::TextureFormat::RGBA8Unorm,
                             wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst);
    p.view = p.texture.CreateView();
  }
  p.group = make_group(device, "ui.image", pass.image_layout, {p.view});
  return p;
}

void write_pixels(gpu::Device& device, const wgpu::Texture& texture, const uint8_t* rgba,
                  uint32_t width, uint32_t height) {
  wgpu::TexelCopyTextureInfo dst{};
  dst.texture = texture;
  wgpu::TexelCopyBufferLayout layout{};
  layout.bytesPerRow = width * 4;
  layout.rowsPerImage = height;
  const wgpu::Extent3D size{width, height, 1};
  device.queue.WriteTexture(&dst, rgba, size_t{width} * height * 4, &layout, &size);
}

void reset_atlas(UiPass& pass) {
  pass.glyphs.clear();
  pass.shelf_x = pass.shelf_y = pass.shelf_h = 0;
}

// Where `m` sits in the atlas, copying it in when it is new; false when the atlas is full.
bool place_in_atlas(gpu::Device& device, UiPass& pass, const ui::GlyphMask& m, uint32_t& at) {
  if (const auto hit = pass.glyphs.find(&m); hit != pass.glyphs.end()) {
    at = hit->second;
    return true;
  }
  const auto w = static_cast<uint32_t>(m.w), h = static_cast<uint32_t>(m.h);
  if (w + kPad > kUiAtlasSize || h + kPad > kUiAtlasSize) return false;
  if (pass.shelf_x + w + kPad > kUiAtlasSize) {
    pass.shelf_y += pass.shelf_h;
    pass.shelf_x = 0, pass.shelf_h = 0;
  }
  if (pass.shelf_y + h + kPad > kUiAtlasSize) return false;
  const uint32_t x = pass.shelf_x, y = pass.shelf_y;
  pass.shelf_x += w + kPad;
  pass.shelf_h = std::max(pass.shelf_h, h + kPad);
  wgpu::TexelCopyTextureInfo dst{};
  dst.texture = pass.atlas;
  dst.origin = {x, y, 0};
  wgpu::TexelCopyBufferLayout layout{};
  layout.bytesPerRow = w;
  layout.rowsPerImage = h;
  const wgpu::Extent3D size{w, h, 1};
  device.queue.WriteTexture(&dst, m.alpha.data(), m.alpha.size(), &layout, &size);
  ++pass.glyphs_uploaded;
  at = x | (y << 16);
  pass.glyphs.emplace(&m, at);
  return true;
}

void set4(float (&dst)[4], float a, float b, float c, float d) {
  dst[0] = a, dst[1] = b, dst[2] = c, dst[3] = d;
}

void set_clip(UiItem& it, const ui::Rect& clip) {
  set4(it.clip, clip.x, clip.y, clip.right(), clip.bottom());
}

UiPaint to_paint(const ui::Paint& p) {
  UiPaint g{};
  set4(g.line, p.from_x, p.from_y, p.to_x, p.to_y);
  const float kind = p.kind == ui::PaintKind::linear   ? 1.0f
                     : p.kind == ui::PaintKind::radial ? 2.0f
                                                       : 0.0f;
  const int n = std::clamp(p.count, 1, ui::kMaxStops);
  set4(g.form, p.radius, p.aspect, kind, static_cast<float>(n));
  for (int i = 0; i < n; ++i) {
    const ui::Stop& s = p.stops[static_cast<size_t>(i)];
    g.at[i] = s.at;
    set4(g.colors[i], s.color.r, s.color.g, s.color.b, s.color.a);
  }
  return g;
}

// One attempt at the list's instances; false when the atlas filled up on the way.
bool stage(gpu::Device& device, UiPass& pass, const ui::DrawList& list) {
  pass.staged_items.clear();
  pass.staged_paints.clear();
  pass.ranges.clear();
  const wgpu::BindGroup* current = &pass.blank;
  bool fits = true;
  auto open_range = [&](const wgpu::BindGroup* image) {
    if (!pass.ranges.empty() && pass.ranges.back().image == image) return;
    const auto first = static_cast<uint32_t>(pass.staged_items.size());
    if (!pass.ranges.empty() && pass.ranges.back().count == 0) {
      pass.ranges.back().image = image;  // an empty range takes the new image
      return;
    }
    pass.ranges.push_back({first, 0, image});
  };
  open_range(current);
  for (const ui::Cmd& c : list.cmds()) {
    if (c.kind == ui::CmdKind::box) {
      const ui::Box& b = c.box;
      if (b.rect.w <= 0 || b.rect.h <= 0 || b.opacity <= 0) continue;
      UiItem it{};
      set4(it.rect, b.rect.x, b.rect.y, b.rect.w, b.rect.h);
      set_clip(it, c.clip);
      set4(it.ink, b.border_color.r, b.border_color.g, b.border_color.b, b.border_color.a);
      set4(it.shape, b.radius, b.feather, b.border, b.opacity);
      it.info[0] = kKindBox;
      it.info[1] = static_cast<uint32_t>(pass.staged_paints.size());
      auto found = b.image ? pass.pictures.find(b.image) : pass.pictures.end();
      if (b.image != nullptr && found == pass.pictures.end() && !b.image->empty()) {
        ui_image(device, pass, *b.image);
        found = pass.pictures.find(b.image);
      }
      if (found != pass.pictures.end()) {
        const UiPass::Picture& pic = found->second;
        const float iw = static_cast<float>(pic.width), ih = static_cast<float>(pic.height);
        set4(it.uv, b.uv.x / iw, b.uv.y / ih, b.uv.w / b.rect.w / iw, b.uv.h / b.rect.h / ih);
        it.info[0] = kKindSampled;
        current = &pic.group;
        open_range(current);
      }
      pass.staged_paints.push_back(to_paint(b.fill));
      pass.staged_items.push_back(it);
      ++pass.ranges.back().count;
    } else {
      const ui::TextRun& t = c.text;
      if (t.font == nullptr || !t.font->loaded() || t.color.a <= 0) continue;
      ui::place_glyphs(*t.font, t.text, t.size, t.x, t.baseline, t.tracking,
                       [&](const ui::GlyphMask& m, int left, int top) {
                         if (m.w <= 0 || m.h <= 0 || !fits) return;
                         uint32_t at = 0;
                         if (!place_in_atlas(device, pass, m, at)) {
                           fits = false;
                           return;
                         }
                         UiItem it{};
                         set4(it.rect, static_cast<float>(left), static_cast<float>(top),
                              static_cast<float>(m.w), static_cast<float>(m.h));
                         set_clip(it, c.clip);
                         set4(it.uv, static_cast<float>(at & 0xffff), static_cast<float>(at >> 16),
                              0, 0);
                         set4(it.ink, t.color.r, t.color.g, t.color.b, t.color.a);
                         it.info[0] = kKindGlyph;
                         pass.staged_items.push_back(it);
                         ++pass.ranges.back().count;
                       });
    }
  }
  return fits;
}

}  // namespace

void create_ui_pass(gpu::Device& device, gpu::PipelineSet& pipelines, UiPass& pass,
                    wgpu::TextureFormat format) {
  if (srgb_format(format)) {
    core::output::error(
        "render: create_ui_pass was handed an sRGB output format; the 2D pass blends "
        "sRGB-encoded colours and needs a UNORM target. next: draw into an RGBA8Unorm or "
        "BGRA8Unorm view of the output (render/ui_pass.hpp); skill: render.");
  }
  pass.output = format;
  const auto both = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
  pass.layout = make_layout(
      device, "ui",
      {Slot::uniform, Slot::storage_read, Slot::storage_read, Slot::texture, Slot::sampler}, both);
  pass.image_layout = make_layout(device, "ui.image", {Slot::texture}, wgpu::ShaderStage::Fragment);
  wgpu::SamplerDescriptor sd{};
  sd.label = "ui.linear";
  sd.addressModeU = sd.addressModeV = sd.addressModeW = wgpu::AddressMode::ClampToEdge;
  sd.magFilter = sd.minFilter = wgpu::FilterMode::Linear;
  pass.sampler = device.device.CreateSampler(&sd);
  pass.uniforms = make_buffer(device, "ui.screen", 16, wgpu::BufferUsage::Uniform);
  pass.atlas =
      make_texture(device, "ui.atlas", kUiAtlasSize, kUiAtlasSize, 1, wgpu::TextureFormat::R8Unorm,
                   wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst);
  pass.atlas_view = pass.atlas.CreateView();
  grow(device, pass.items, pass.item_room, 1, sizeof(UiItem), "ui.items");
  grow(device, pass.paints, pass.paint_room, 1, sizeof(UiPaint), "ui.paints");
  make_main_group(device, pass);
  const uint8_t white[4] = {255, 255, 255, 255};
  UiPass::Picture blank = make_picture(device, pass, 1, 1, nullptr);
  write_pixels(device, blank.texture, white, 1, 1);
  pass.blank = blank.group;

  const wgpu::ShaderModule module = gpu::create_shader(device, wgsl::ui_pass, "ui");
  wgpu::BlendState blend{};
  blend.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One,
                 wgpu::BlendFactor::OneMinusSrcAlpha};
  blend.alpha = blend.color;
  wgpu::ColorTargetState colour{};
  colour.format = format;
  colour.blend = &blend;
  wgpu::FragmentState fragment{};
  fragment.module = module;
  fragment.entryPoint = "fs_main";
  fragment.targetCount = 1;
  fragment.targets = &colour;
  wgpu::RenderPipelineDescriptor desc{};
  desc.label = "ui.draw";
  desc.layout = make_pipeline_layout(device, "ui.draw", {pass.layout, pass.image_layout});
  desc.vertex.module = module;
  desc.vertex.entryPoint = "vs_main";
  desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
  desc.fragment = &fragment;
  pass.p_draw = pipelines.add_render(device, desc);
}

void ui_image(gpu::Device& device, UiPass& pass, const ui::Image& image) {
  if (image.empty()) return;
  const auto w = static_cast<uint32_t>(image.width), h = static_cast<uint32_t>(image.height);
  auto found = pass.pictures.find(&image);
  if (found == pass.pictures.end() || !found->second.texture || found->second.width != w ||
      found->second.height != h) {
    pass.pictures[&image] = make_picture(device, pass, w, h, nullptr);
    found = pass.pictures.find(&image);
  }
  write_pixels(device, found->second.texture, image.rgba.data(), w, h);
}

void ui_texture(gpu::Device& device, UiPass& pass, const ui::Image* key,
                const wgpu::TextureView& view, uint32_t width, uint32_t height) {
  pass.pictures[key] = make_picture(device, pass, width, height, &view);
}

void prepare_ui(gpu::Device& device, UiPass& pass, const ui::DrawList& list, uint32_t width,
                uint32_t height) {
  pass.width = width, pass.height = height;
  if (!stage(device, pass, list)) {
    // Full: empty the atlas and set the whole list again from it.
    reset_atlas(pass);
    ++pass.atlas_resets;
    if (!stage(device, pass, list)) {
      core::output::error(
          "render: the 2D pass's glyph atlas (" + std::to_string(kUiAtlasSize) +
          " px a side) "
          "cannot hold one frame's glyphs; the glyphs past it are not drawn. next: draw fewer "
          "sizes of text a frame, or raise kUiAtlasSize (render/ui_pass.hpp); skill: render.");
    }
  }
  const float screen[4] = {static_cast<float>(width), static_cast<float>(height),
                           1.0f / static_cast<float>(std::max<uint32_t>(width, 1)),
                           1.0f / static_cast<float>(std::max<uint32_t>(height, 1))};
  device.queue.WriteBuffer(pass.uniforms, 0, screen, sizeof screen);
  const auto items = static_cast<uint32_t>(pass.staged_items.size());
  const auto paints = static_cast<uint32_t>(pass.staged_paints.size());
  const bool more_items =
      grow(device, pass.items, pass.item_room, items, sizeof(UiItem), "ui.items");
  const bool more_paints =
      grow(device, pass.paints, pass.paint_room, paints, sizeof(UiPaint), "ui.paints");
  if (more_items || more_paints) make_main_group(device, pass);
  if (items) {
    device.queue.WriteBuffer(pass.items, 0, pass.staged_items.data(), items * sizeof(UiItem));
  }
  if (paints) {
    device.queue.WriteBuffer(pass.paints, 0, pass.staged_paints.data(), paints * sizeof(UiPaint));
  }
}

void encode_ui(const UiPass& pass, const gpu::PipelineSet& pipelines, FrameRecorder& rec,
               wgpu::CommandEncoder& encoder, const wgpu::TextureView& output) {
  wgpu::RenderPassColorAttachment colour{};
  colour.view = output;
  colour.loadOp = wgpu::LoadOp::Load;
  colour.storeOp = wgpu::StoreOp::Store;
  wgpu::RenderPassDescriptor desc{};
  desc.colorAttachmentCount = 1;
  desc.colorAttachments = &colour;
  wgpu::RenderPassEncoder enc = rec.render(encoder, desc, "ui");
  enc.SetPipeline(pipelines.render(pass.p_draw));
  enc.SetBindGroup(0, pass.group);
  for (const UiPass::Range& r : pass.ranges) {
    if (r.count == 0) continue;
    enc.SetBindGroup(1, *r.image);
    enc.Draw(6, r.count, 0, r.first);
    ++rec.current().draws;
  }
  enc.End();
}

}  // namespace ge::render
