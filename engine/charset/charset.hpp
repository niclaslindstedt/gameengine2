// THE ENGINE'S BITMAP FONTS: ten monospaced ASCII charsets any game draws its messages, menus,
// console and HUD with, each built into a square glyph atlas by the engine's own C++ builder
// and committed at 256, 512 and 1024 px (engine/charset/atlases/<name>_<size>.png, grey
// coverage). docs/charsets.md is the design: why a fixed grid, the ten looks, the effect
// layers, and how a renderer draws them.
//
// THE ATLAS is a 16 x 8 grid of cells, one cell per code 0..127, code c at column c % 16 and
// row c / 16. A cell is twice as tall as it is wide (16 x 32 px at 256, 32 x 64 at 512, 64 x 128
// at 1024): a monospaced line is about twice its advance tall, so the cell IS the line and the
// advance, and text is laid out by multiplying. Codes 32..126 are printable ASCII; 1..31 are the
// UI symbols a game needs and ASCII has no glyph for (`Sym`: a cursor, arrows, a checkbox, a
// slider, box lines, shades, the degree sign; 10, the newline, stays empty); 127 is the MISSING
// glyph every code outside the set draws (`code_of`); 0 is empty. Nothing else (no Latin-1, no
// UTF-8 beyond ASCII).
//
// THE DESIGN SPACE every font is drawn in: a cell is 100 units wide and 200 tall, the baseline
// 56 units above its bottom (kBaselineFromTop gives it from the top). A glyph's ink stays inside
// the cell less a margin of a few units, so a filtered sample at any of the three sizes never
// reads its neighbour (box-drawing symbols reach the edges on purpose: they tile).
//
// BITS: an atlas is a pure function of the font and the size, the same bytes on every compiler
// (no <cmath> transcendental; tests/charset_charset_test.cpp holds the committed atlases to the
// builder, and its GE_GOLDEN_WRITE branch rewrites them).
//
// ge:summary The engine's bitmap fonts: ten ASCII charsets, the 16x8 atlas grid, the builder
// ge:keywords charset font bitmap-font atlas glyph monospace ascii conchars console menu hud
// ge:keywords build_atlas code_of cell_rect font_info fonts find_font sym missing-glyph
// ge:keywords texture-atlas glyph-sheet text
// ge:skill ui
// ge:test tests/charset_charset_test.cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ge::charset {

inline constexpr int kColumns = 16;
inline constexpr int kRows = 8;
inline constexpr int kCodes = kColumns * kRows;  // 128
inline constexpr uint8_t kFirstPrintable = 32;
inline constexpr uint8_t kLastPrintable = 126;
inline constexpr uint8_t kMissing = 127;

// The committed atlas sizes, px square. `build_atlas` takes any multiple of 16 from 64 up.
inline constexpr int kAtlasSizes[] = {256, 512, 1024};

// The design space (units): a cell is kCellW x kCellH, y up from the baseline.
inline constexpr double kCellW = 100;
inline constexpr double kCellH = 200;
inline constexpr double kCellTop = 144;     // the cell's top edge, above the baseline
inline constexpr double kCellBottom = -56;  // its bottom edge, below the baseline
inline constexpr double kBaselineFromTop = kCellTop / kCellH;  // 0.72 of the cell's height

// The UI symbols on codes 1..31.
enum class Sym : uint8_t {
  block = 1,   // the full glyph box: a block cursor
  underline,   // a bar under the baseline: an underline cursor
  arrow_left,  // filled triangles
  arrow_right,
  arrow_up,
  arrow_down,
  bullet,       // a filled dot
  box,          // an empty checkbox
  box_checked,  // a checkbox with a tick
  // 10 is left empty: a run of text reads it as a newline
  cross = 11,   // the multiplication sign / a close mark
  tick,         // a check mark
  slider_left,  // a slider: its left end, the track, its right end, the knob
  slider_track,
  slider_right,
  slider_knob,
  line_h,  // box drawing through the cell's centre, reaching its edges: they tile
  line_v,
  corner_tl,
  corner_tr,
  corner_bl,
  corner_br,
  line_cross,
  shade_light,   // a 1-in-4 dither of the whole cell
  shade_medium,  // a checkerboard of the whole cell
  degree,
  star,
  heart,
  diamond,
  radio,  // a ring, and a ring with a dot: a radio button off and on
  radio_on,
};
inline constexpr uint8_t sym(Sym s) { return static_cast<uint8_t>(s); }

enum class FontId : uint8_t {
  terminal,    // an 8x16 pixel font: text-mode PC screens, sampled nearest
  matrix,      // a 5x7 LED dot matrix: departure boards, scoreboards
  segment,     // a 16-segment display: alarm clocks, arcade cabinets, VFDs
  mono,        // a clean humanist monospace sans: programmers' editors
  typewriter,  // a slab serif with inky edges: an office typewriter
  square,      // a wide squared grotesque: sixties sci-fi, cockpit HUDs
  stencil,     // a bold stencil broken at its joins: crates and military kit
  gothic,      // an angular broad-nib hand: blackletter titles, the medieval look
  rounded,     // a heavy round-ended sans: arcade cabinets, friendly HUDs
  marker,      // a hand-lettered felt-tip: notes, comics, a scrawled message
  count,
};
inline constexpr int kFontCount = static_cast<int>(FontId::count);

// Vertical metrics, as shares of the cell's height measured down from its top, so a renderer
// that draws a cell `h` px tall puts the baseline at y + baseline * h.
struct VMetrics {
  double baseline;
  double cap;  // the top of a capital
  double x;    // the top of a lower-case letter without an ascender
};

struct FontInfo {
  FontId id;
  const char* name;      // the file stem and the flag value: "terminal"
  const char* look;      // one line on what it is for
  const char* inspired;  // the genre of real lettering it is drawn after (our own glyphs)
  bool pixel;            // a pixel font: sample it NEAREST, at whole multiples of its grid
  int grid_w, grid_h;    // a pixel font's native cell (8 x 16); 0 for a drawn one
  VMetrics metrics;
};

// Every font, in FontId order.
std::span<const FontInfo> fonts();
const FontInfo& font_info(FontId id);
// The font named `name`, or nullptr.
const FontInfo* find_font(std::string_view name);

// The code a character draws as: printable ASCII as itself, a UI symbol code as itself, every
// other byte (a control code, anything >= 127) as kMissing.
inline uint8_t code_of(unsigned char c) {
  if (c >= 1 && c <= kLastPrintable) return c;
  return kMissing;
}

// A cell's place in an atlas `size` px square.
struct CellRect {
  int x, y, w, h;
};
inline CellRect cell_rect(int size, uint8_t code) {
  const int w = size / kColumns;
  const int h = size / kRows;
  return {(code % kColumns) * w, (code / kColumns) * h, w, h};
}

// One font's atlas: `size` x `size` coverage bytes (0 none, 255 full ink), row-major from the
// top. A GPU uploads it as one R8 texture (premultiply in the shader: colour * coverage).
struct Atlas {
  FontId font = FontId::mono;
  int size = 0;
  std::vector<uint8_t> coverage;
  int cell_w() const { return size / kColumns; }
  int cell_h() const { return size / kRows; }
  uint8_t at(int x, int y) const { return coverage[static_cast<size_t>(y) * size + x]; }
};

// Builds `font`'s atlas at `size` px (a multiple of 16, at least 64). Each glyph is drawn from
// its design with analytic anti-aliasing (a drawn font) or scaled from its pixel grid (a pixel
// font: nearest, so `size` should be a multiple of 16 * grid_w). Rows of cells are built on
// `workers` threads; the bytes do not depend on how many.
Atlas build_atlas(FontId font, int size, size_t workers = 0);

// The committed atlas a run `cell_h` px tall samples best: the smallest whose cells are at least
// that tall (so it is only ever shrunk), else the largest.
inline int atlas_size_for(double cell_h) {
  for (const int s : kAtlasSizes) {
    if (s / static_cast<double>(kRows) >= cell_h) return s;
  }
  return kAtlasSizes[2];
}

// The repository path of a committed atlas: "engine/charset/atlases/mono_512.png".
std::string atlas_path(FontId font, int size);

}  // namespace ge::charset
