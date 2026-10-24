// The ten fonts' catalog and styles, and the atlas builder over them.
//
// ge:summary The font catalog, each drawn font's style, and the atlas builder
// ge:keywords charset fonts catalog style build_atlas atlas_path font_info find_font
// ge:skill ui
// ge:test tests/charset_charset_test.cpp
#include "charset/charset.hpp"

#include <array>

#include "charset/pen.hpp"
#include "charset/pixel.hpp"
#include "core/parallel.hpp"

namespace ge::charset {
namespace {

// A drawn font's metrics off its cap and x heights in design units.
constexpr VMetrics drawn(double cap, double x) {
  return {kBaselineFromTop, (kCellTop - cap) / kCellH, (kCellTop - x) / kCellH};
}

constexpr std::array<FontInfo, kFontCount> kFonts = {{
    {FontId::terminal,
     "terminal",
     "console, debug readouts, a terminal's text",
     "the 8x16 character ROMs of text-mode PC screens",
     true,
     kTerminalW,
     kTerminalH,
     {12.0 / 16, 2.0 / 16, 5.0 / 16}},
    {FontId::matrix,
     "matrix",
     "scoreboards, timing boards, a station's departures",
     "5x7 LED and incandescent dot-matrix boards",
     false,
     0,
     0,
     {kBaselineFromTop, (kCellTop - 89.4) / kCellH, (kCellTop - 63.4) / kCellH}},
    {FontId::segment, "segment", "clocks, speed and score readouts, a dashboard",
     "16-segment vacuum-fluorescent and LCD displays", false, 0, 0, drawn(105, 105)},
    {FontId::mono, "mono", "the default: chat, menus, the console, settings",
     "humanist programming monospaces", false, 0, 0, drawn(100, 74)},
    {FontId::typewriter, "typewriter", "notes, letters, briefings, a mission's dossier",
     "office typewriter slab serifs", false, 0, 0, drawn(100, 70)},
    {FontId::square, "square", "a HUD, a cockpit, a sci-fi menu",
     "sixties squared extended grotesques", false, 0, 0, drawn(94, 78)},
    {FontId::stencil, "stencil", "crates, signs, a military menu",
     "spray-painted military stencils", false, 0, 0, drawn(100, 74)},
    {FontId::gothic, "gothic", "titles, a dark fantasy menu, the medieval look",
     "broad-nib blackletter and its angular hands", false, 0, 0, drawn(100, 70)},
    {FontId::rounded, "rounded", "big friendly HUD numbers, an arcade menu",
     "heavy rounded arcade and signage sans", false, 0, 0, drawn(100, 78)},
    {FontId::marker, "marker", "hand-written notes, comics, a scrawl on a wall",
     "felt-tip comic and sign lettering", false, 0, 0, drawn(100, 70)},
}};

Style style_of(FontId font) {
  Style s;
  s.seed = static_cast<uint32_t>(font) + 1;
  switch (font) {
    case FontId::terminal:
    case FontId::matrix:
    case FontId::count:
      break;
    case FontId::segment:  // its fills, and the skeletons of the codes it cannot show
      s.pen = PenShape::square;
      s.weight = 6.5;
      s.slant = 0.1;
      s.squareness = 4;
      break;
    case FontId::mono:
      s.weight = 7.5;
      s.variant.zero = Zero::dotted;
      break;
    case FontId::typewriter:
      s.weight = 6.2;
      s.serifs = true;
      s.serif = 13;
      s.rough = 1.6;
      s.rough_scale = 5;
      s.x_height = 70;
      s.variant.zero = Zero::plain;
      break;
    case FontId::square:
      s.pen = PenShape::square;
      s.weight = 7.5;
      s.squareness = 5;
      s.arc_step = 4;
      s.width = 1.1;
      s.cap = 94;
      s.x_height = 78;
      s.variant.zero = Zero::slashed;
      break;
    case FontId::stencil:
      s.pen = PenShape::nib;  // a square nib held level: flat ends on the baseline
      s.nib_angle = 0;
      s.nib_thin = 1;
      s.weight = 9;
      s.squareness = 3;
      s.stencil = true;
      s.stencil_gap = 7;
      s.variant.zero = Zero::plain;
      break;
    case FontId::gothic:
      s.pen = PenShape::nib;
      s.weight = 12;
      s.nib_angle = 38;
      s.nib_thin = 0.16;
      s.arc_step = 45;
      s.width = 0.88;
      s.lozenges = true;
      s.dot = 0.95;
      s.x_height = 70;
      s.variant.zero = Zero::plain;
      break;
    case FontId::rounded:
      s.weight = 11;
      s.squareness = 3;
      s.arc_step = 5;
      s.x_height = 78;
      s.dot = 1.1;
      s.variant.zero = Zero::plain;
      s.variant.single_storey_a = true;
      break;
    case FontId::marker:
      s.weight = 7;
      s.wobble = 5;
      s.jitter = 3;
      s.rough = 1.4;
      s.rough_scale = 7;
      s.slant = 0.08;
      s.arc_step = 8;
      s.x_height = 70;
      s.variant.zero = Zero::plain;
      s.variant.single_storey_a = true;
      break;
  }
  return s;
}

void render_cell(FontId font, const Style& style, uint8_t code, int w, int h, uint8_t* out,
                 int stride) {
  switch (font) {
    case FontId::terminal:
      render_terminal(code, w, h, out, stride);
      return;
    case FontId::matrix: {
      Style lamps;  // the lamps are fills: no pen, no slant
      render_design(matrix_design(code), code, lamps, w, h, out, stride);
      return;
    }
    case FontId::segment:
      if (segments_of(code)) {
        render_design(segment_design(code), code, style, w, h, out, stride);
        return;
      }
      break;
    default:
      break;
  }
  render_glyph(code, style, w, h, out, stride);
}

}  // namespace

std::span<const FontInfo> fonts() { return kFonts; }

const FontInfo& font_info(FontId id) { return kFonts[static_cast<size_t>(id)]; }

const FontInfo* find_font(std::string_view name) {
  for (const FontInfo& f : kFonts) {
    if (name == f.name) return &f;
  }
  return nullptr;
}

Atlas build_atlas(FontId font, int size, size_t workers) {
  Atlas a;
  a.font = font;
  a.size = size;
  a.coverage.assign(static_cast<size_t>(size) * size, 0);
  const Style style = style_of(font);
  const int w = a.cell_w(), h = a.cell_h();
  core::parallel_bands(
      kRows, workers ? workers : core::default_workers(), [&](size_t begin, size_t end) {
        for (size_t row = begin; row < end; ++row) {
          for (int col = 0; col < kColumns; ++col) {
            const auto code = static_cast<uint8_t>(row * kColumns + col);
            const CellRect r = cell_rect(size, code);
            uint8_t* out = a.coverage.data() + static_cast<size_t>(r.y) * size + r.x;
            if (code != 0 && code != ' ') {
              render_cell(font, style, code, w, h, out, size);
            }
          }
        }
      });
  return a;
}

std::string atlas_path(FontId font, int size) {
  return std::string("engine/charset/atlases/") + font_info(font).name + "_" +
         std::to_string(size) + ".png";
}

}  // namespace ge::charset
