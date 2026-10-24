// A FONT THE 2D LAYER WRITES WITH: a TrueType or OpenType file read by stb_truetype, its glyphs
// rasterized on demand at the pixel sizes asked for and kept (one coverage mask a glyph, a size
// and a quarter-pixel phase, so a run's glyphs sit at their true fractional pens), its kerning
// pairs and its vertical metrics. The engine ships one family compiled in (`ui_font`): Barlow
// Condensed at two weights, a narrow sans that keeps a big menu title or a dense HUD readout
// legible on a phone held upright; its licence (SIL OFL 1.1) is engine/ui/fonts/OFL.txt. A game
// loads its own from bytes the same way.
//
// SIZES are a glyph's pixel height, ascent minus descent: `size` 32 makes a line about 32 px
// tall before its line gap. Text is UTF-8; a code point the font lacks draws its missing glyph.
//
// The glyph cache is the font's only state and is not locked: a font is drawn from one thread
// (the frame's).
//
// ge:summary A TrueType font: glyph masks cached per size, kerning, metrics, the built-in family
// ge:keywords ui font text glyph truetype opentype stb_truetype rasterize kerning advance
// ge:keywords metrics ascent descent line-height utf8 barlow condensed ofl measure
// ge:skill ui
// ge:test tests/ui_font_test.cpp
#pragma once

#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ge::ui {

// One glyph's coverage at one size and phase: `alpha` is w x h, 0..255, its top-left at
// (pen_x + x0, baseline + y0).
struct GlyphMask {
  int w = 0, h = 0;
  int x0 = 0, y0 = 0;
  std::vector<uint8_t> alpha;
};

struct VMetrics {
  float ascent = 0;    // px above the baseline
  float descent = 0;   // px below it, positive
  float line_gap = 0;  // px between one line's descent and the next's ascent
  float line() const { return ascent + descent + line_gap; }
};

class Font {
 public:
  Font();
  ~Font();
  Font(Font&&) noexcept;
  Font& operator=(Font&&) noexcept;

  // Reads `ttf` (kept by reference: it must outlive the font, as compiled-in bytes do). False,
  // with the reason in `why`, when it is not a font stb_truetype reads.
  bool load(std::span<const uint8_t> ttf, std::string* why = nullptr);
  bool loaded() const { return impl_ != nullptr; }

  VMetrics metrics(float size) const;
  // The pen's advance over `text` at `size`, kerning and `tracking` (px after each glyph) included.
  float advance(std::string_view text, float size, float tracking = 0) const;
  // How much of `text` fits in `width` px: the byte length of the longest prefix whose advance
  // fits, ending on a code point.
  size_t fit(std::string_view text, float size, float width, float tracking = 0) const;

  // The glyph index a code point maps to (0 is the missing glyph).
  int glyph_index(uint32_t codepoint) const;
  float glyph_advance(int glyph, float size) const;
  float kern(int left, int right, float size) const;
  // The mask of `glyph` at `size` with the pen `phase` (0..1) of a pixel past an integer x.
  const GlyphMask& mask(int glyph, float size, float phase) const;
  size_t cached() const { return cache_.size(); }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  mutable std::unordered_map<uint64_t, GlyphMask> cache_;
};

// The next code point of UTF-8 `s` at byte `i`, advancing `i`; a malformed byte reads as
// U+FFFD and advances one byte.
uint32_t next_codepoint(std::string_view s, size_t& i);

// A run of `text` set at `size` from the pen `x` on `baseline`, `tracking` px after each glyph:
// `place(mask, left, top)` for every glyph, its mask's top-left pixel at (left, top). The one
// place a run's pen is advanced, so the CPU raster and the GPU pass (render/ui_pass.hpp) set the
// same pixels.
template <class Place>
void place_glyphs(const Font& font, std::string_view text, float size, float x, float baseline,
                  float tracking, Place&& place) {
  const int base = static_cast<int>(std::lround(baseline));
  float pen = x;
  int prev = -1;
  for (size_t i = 0; i < text.size();) {
    const int g = font.glyph_index(next_codepoint(text, i));
    if (prev >= 0) pen += font.kern(prev, g, size);
    const float whole = std::floor(pen);
    const GlyphMask& m = font.mask(g, size, pen - whole);
    place(m, static_cast<int>(whole) + m.x0, base + m.y0);
    pen += font.glyph_advance(g, size) + tracking;
    prev = g;
  }
}

enum class Weight : uint8_t { medium, semibold };

// The compiled-in UI family (Barlow Condensed), loaded on first use.
const Font& ui_font(Weight weight = Weight::semibold);

}  // namespace ge::ui
