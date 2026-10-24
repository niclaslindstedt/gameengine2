// The 2D layer's CPU raster, defined: a box's coverage from its rounded rectangle's signed
// distance, its fill (paint times image), its border, and a text run's glyphs, each clipped and
// blended over the target.
//
// ge:summary The CPU raster defined: rounded-box coverage, fill, border, glyph blits, blending
// ge:keywords ui raster coverage signed-distance rounded border glyph blend clip
// ge:skill ui
// ge:test tests/ui_raster_test.cpp
#include "ui/raster.hpp"

#include <algorithm>
#include <cmath>

#include "ui/font.hpp"

namespace ge::ui {

namespace {

uint8_t to_byte(float v) {
  return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

// The signed distance from (px, py) to a box of half-size (hw, hh) with corner radius r,
// centred at the origin: negative inside.
float rounded_distance(float px, float py, float hw, float hh, float r) {
  const float qx = std::fabs(px) - hw + r, qy = std::fabs(py) - hh + r;
  const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
  return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
}

float coverage(float d, float feather) { return std::clamp(0.5f - d / feather, 0.0f, 1.0f); }

void draw_box(const Box& b, Rect clip, Image& target) {
  if (b.rect.w <= 0 || b.rect.h <= 0 || b.opacity <= 0) return;
  const float feather = std::max(b.feather, 0.5f);
  const Rect reach =
      intersect(intersect(inset(b.rect, feather), clip),
                {0, 0, static_cast<float>(target.width), static_cast<float>(target.height)});
  if (reach.w <= 0 || reach.h <= 0) return;
  const float hw = b.rect.w / 2, hh = b.rect.h / 2;
  const float r = std::clamp(b.radius, 0.0f, std::min(hw, hh));
  const float cx = b.rect.x + hw, cy = b.rect.y + hh;
  const int x0 = static_cast<int>(std::floor(reach.x)),
            x1 = static_cast<int>(std::ceil(reach.right()));
  const int y0 = static_cast<int>(std::floor(reach.y)),
            y1 = static_cast<int>(std::ceil(reach.bottom()));
  const bool sampled = b.image != nullptr && !b.image->empty();
  const float su = sampled ? b.uv.w / b.rect.w : 0, sv = sampled ? b.uv.h / b.rect.h : 0;
  for (int y = y0; y < y1; y++) {
    const float py = static_cast<float>(y) + 0.5f;
    if (py < clip.y || py >= clip.bottom()) continue;
    uint8_t* row = target.rgba.data() + static_cast<size_t>(y) * target.width * 4;
    for (int x = x0; x < x1; x++) {
      const float px = static_cast<float>(x) + 0.5f;
      if (px < clip.x || px >= clip.right()) continue;
      const float d = rounded_distance(px - cx, py - cy, hw, hh, r);
      const float outer = coverage(d, feather);
      if (outer <= 0) continue;
      const float inner = b.border > 0 ? coverage(d + b.border, feather) : outer;
      const float lx = px - b.rect.x, ly = py - b.rect.y;
      if (inner > 0) {
        Color c = b.fill.at(lx, ly);
        if (sampled) {
          const Color s = sample(*b.image, b.uv.x + lx * su, b.uv.y + ly * sv);
          c = {c.r * s.r, c.g * s.g, c.b * s.b, c.a * s.a};
        }
        c.a *= inner * b.opacity;
        blend_over(row + static_cast<size_t>(x) * 4, c);
      }
      if (b.border > 0 && outer > inner) {
        Color c = b.border_color;
        c.a *= (outer - inner) * b.opacity;
        blend_over(row + static_cast<size_t>(x) * 4, c);
      }
    }
  }
}

void draw_text(const TextRun& t, Rect clip, Image& target) {
  if (t.font == nullptr || !t.font->loaded() || t.color.a <= 0) return;
  const Rect bounds =
      intersect(clip, {0, 0, static_cast<float>(target.width), static_cast<float>(target.height)});
  const int cx0 = static_cast<int>(std::ceil(bounds.x - 0.5f)),
            cx1 = static_cast<int>(std::ceil(bounds.right() - 0.5f));
  const int cy0 = static_cast<int>(std::ceil(bounds.y - 0.5f)),
            cy1 = static_cast<int>(std::ceil(bounds.bottom() - 0.5f));
  place_glyphs(*t.font, t.text, t.size, t.x, t.baseline, t.tracking,
               [&](const GlyphMask& m, int ox, int oy) {
                 for (int y = 0; y < m.h; y++) {
                   const int ty = oy + y;
                   if (ty < cy0 || ty >= cy1) continue;
                   uint8_t* row = target.rgba.data() + static_cast<size_t>(ty) * target.width * 4;
                   for (int x = 0; x < m.w; x++) {
                     const int tx = ox + x;
                     if (tx < cx0 || tx >= cx1) continue;
                     const uint8_t a = m.alpha[static_cast<size_t>(y) * m.w + x];
                     if (a == 0) continue;
                     Color c = t.color;
                     c.a *= static_cast<float>(a) / 255.0f;
                     blend_over(row + static_cast<size_t>(tx) * 4, c);
                   }
                 }
               });
}

}  // namespace

void blend_over(uint8_t* dst, Color src) {
  const float sa = std::clamp(src.a, 0.0f, 1.0f);
  if (sa <= 0) return;
  const float da = static_cast<float>(dst[3]) / 255.0f;
  const float oa = sa + da * (1 - sa);
  const float keep = da * (1 - sa);
  const float s[3] = {src.r, src.g, src.b};
  for (int k = 0; k < 3; k++) {
    const float d = static_cast<float>(dst[k]) / 255.0f;
    dst[k] = to_byte((s[k] * sa + d * keep) / oa);
  }
  dst[3] = to_byte(oa);
}

void render(const DrawList& list, Image& target) {
  if (target.empty()) return;
  for (const Cmd& c : list.cmds()) {
    if (c.kind == CmdKind::box) {
      draw_box(c.box, c.clip, target);
    } else {
      draw_text(c.text, c.clip, target);
    }
  }
}

}  // namespace ge::ui
