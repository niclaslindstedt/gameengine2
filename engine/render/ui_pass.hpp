// THE 2D PASS (docs/rendering.md § The 2D pass): the 2D layer's draw list (ui/draw.hpp) drawn
// by the GPU over a finished frame, the twin of the CPU raster (ui/raster.hpp) the labs and
// tests picture with. A menu, the HUD and the console are drawn this way in a game.
//
// ONE INSTANCED DRAW PER PICTURE. `prepare_ui` turns the list into instances: each box is one
// quad with its rounded rectangle, paint and border worked out in the fragment shader, and each
// glyph of a text run is one quad reading the glyph atlas. Every instance lives in one storage
// buffer and every paint in a second, so the whole list is one draw, split only where a box's
// image changes (each image is a texture with its own small bind group).
//
// THE SAME PIXELS AS THE CPU RASTER, within a level or two: coverage comes from the same signed
// distance at the pixel's centre, the clip keeps the same centres, text is set by the same
// `place_glyphs` from the same cached masks, an image is sampled bilinearly with its texel
// centres where `ui::sample` puts them, and colours blend as straight sRGB-encoded values, the
// way `blend_over` blends bytes. That is why the output must be a UNORM target (RGBA8Unorm or
// BGRA8Unorm): an *Srgb one would blend in linear light. A fill with a gradient adds a dither of
// half a level against banding, which the CPU raster does not. tests/render_ui_pass_test.cpp
// and the `menu-gpu` lab hold the two together.
//
// THE GLYPH ATLAS is one R8 texture filled on demand: a glyph mask is copied in the first time a
// list draws it and kept, keyed by the font's cached mask. When it is full it is emptied and
// refilled from the list being prepared (`atlas_resets` counts that; a list that needs more than
// the whole atlas loses the glyphs past it, with an error line).
//
// IMAGES. A box's `ui::Image` is uploaded the first time a list draws it and kept, keyed by its
// address; `ui_image` uploads it again after its pixels change. A picture the GPU already has
// (the live frame under a menu) is bound to a key with `ui_texture`, and a box that names that
// key draws it.
//
// Drawn last, after post: the pass loads the output and blends over it.
//
// ge:summary The 2D pass: the ui draw list as instanced quads over the frame, glyph atlas, images
// ge:keywords render ui 2d draw-list overlay menu hud instanced quad rounded-rect sdf glyph
// ge:keywords atlas glyph-atlas text image texture gradient dither premultiplied blend
// ge:keywords create_ui_pass prepare_ui encode_ui ui_image ui_texture
// ge:skill render ui
// ge:test tests/render_ui_pass_test.cpp
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "gpu/device.hpp"
#include "gpu/pipeline.hpp"
#include "render/frame.hpp"
#include "ui/draw.hpp"
#include "ui/font.hpp"

namespace ge::render {

inline constexpr uint32_t kUiAtlasSize = 1024;  // the glyph atlas, a side (R8)

// render/ui_pass.wgsl's `Item`, field for field: one box or one glyph.
struct UiItem {
  float rect[4];     // x, y, w, h (px)
  float clip[4];     // x0, y0, x1, y1 (px)
  float uv[4];       // a box's image: u, v at the rect's corner, du/dx, dv/dy; a glyph: atlas x, y
  float ink[4];      // a box's border colour; a glyph's colour
  float shape[4];    // radius, feather, border, opacity
  uint32_t info[4];  // kind (0 box, 1 glyph, 2 box with an image), paint index, 0, 0
};
static_assert(sizeof(UiItem) == 96, "match ui_pass.wgsl");

// render/ui_pass.wgsl's `Paint`: a ui::Paint's stops and shape.
struct UiPaint {
  float line[4];  // linear: from x, y, to x, y; radial: centre x, y
  float form[4];  // radius, aspect, kind (0 solid, 1 linear, 2 radial), stop count
  float at[8];    // each stop's place along the paint
  float colors[8][4];
};
static_assert(sizeof(UiPaint) == 192, "match ui_pass.wgsl");

struct UiPass {
  wgpu::TextureFormat output = wgpu::TextureFormat::RGBA8Unorm;
  wgpu::BindGroupLayout layout;        // the uniforms, the items, the paints, the atlas, sampler
  wgpu::BindGroupLayout image_layout;  // one image
  wgpu::Sampler sampler;
  wgpu::Buffer uniforms;
  wgpu::Buffer items, paints;
  uint32_t item_room = 0, paint_room = 0;
  wgpu::BindGroup group;  // made again when a buffer grows
  uint32_t p_draw = 0;

  // The glyph atlas and its shelves.
  wgpu::Texture atlas;
  wgpu::TextureView atlas_view;
  std::unordered_map<const ui::GlyphMask*, uint32_t> glyphs;  // packed x | y << 16
  uint32_t shelf_x = 0, shelf_y = 0, shelf_h = 0;
  uint32_t atlas_resets = 0;
  uint32_t glyphs_uploaded = 0;

  // The images a box may name, and a white one for a list's boxes before the first image.
  struct Picture {
    wgpu::Texture texture;
    wgpu::TextureView view;
    wgpu::BindGroup group;
    uint32_t width = 0, height = 0;
  };
  std::unordered_map<const ui::Image*, Picture> pictures;
  wgpu::BindGroup blank;

  // The prepared list: its instances and paints (kept for their capacity) and its draws.
  struct Range {
    uint32_t first = 0, count = 0;
    const wgpu::BindGroup* image = nullptr;
  };
  std::vector<UiItem> staged_items;
  std::vector<UiPaint> staged_paints;
  std::vector<Range> ranges;
  uint32_t width = 0, height = 0;
};

// Add the pass's pipeline for an output of `format` (a UNORM format; above) to the manifest and
// make its atlas and buffers.
void create_ui_pass(gpu::Device& device, gpu::PipelineSet& pipelines, UiPass& pass,
                    wgpu::TextureFormat format = wgpu::TextureFormat::RGBA8Unorm);

// Upload `image`'s pixels as the texture boxes naming it draw (again, after they change).
void ui_image(gpu::Device& device, UiPass& pass, const ui::Image& image);
// Bind a texture the GPU already has to `key`: a box whose image is `key` draws `view` (a
// filterable 2D texture, its colours sRGB-encoded) instead of the key's own pixels.
void ui_texture(gpu::Device& device, UiPass& pass, const ui::Image* key,
                const wgpu::TextureView& view, uint32_t width, uint32_t height);

// Turn `list` into the pass's instances for an output of width x height and upload them, with
// any glyph or image the GPU does not have yet. Once a frame, before `encode_ui`.
void prepare_ui(gpu::Device& device, UiPass& pass, const ui::DrawList& list, uint32_t width,
                uint32_t height);
// The pass ("ui"): the prepared list blended over `output` (loaded, not cleared).
void encode_ui(const UiPass& pass, const gpu::PipelineSet& pipelines, FrameRecorder& rec,
               wgpu::CommandEncoder& encoder, const wgpu::TextureView& output);

}  // namespace ge::render
