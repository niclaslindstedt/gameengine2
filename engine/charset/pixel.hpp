// THE GRID FONTS: the three of the ten (charset.hpp) built from a grid rather than stroked by
// a pen. The terminal is an 8x16 pixel picture a glyph (pixel_art.cpp), scaled to the cell
// nearest-neighbour, so its pixels stay square and hard at every atlas size. The matrix is a
// 5x7 picture (two rows more for descenders) whose pixels are drawn as round lamps on a pitch,
// anti-aliased. The segment font lights the bars of a 16-segment display (plus its point and
// colon); like the display it has no lower case, so a lower-case letter shows its capital.
// The matrix and the segment designs are handed to the pen's raster (pen.hpp) as fills, so they
// slant and anti-alias like a drawn font.
//
// ge:summary The grid fonts: the 8x16 terminal, the 5x7 dot matrix, the 16-segment display
// ge:keywords pixel-font terminal matrix dot-matrix led segment sixteen-segment vfd lcd
// ge:keywords render_terminal matrix_design segment_design segments nearest
// ge:skill ui
// ge:test tests/charset_charset_test.cpp
#pragma once

#include <cstdint>
#include <string_view>

#include "charset/design.hpp"

namespace ge::charset {

inline constexpr int kTerminalW = 8;
inline constexpr int kTerminalH = 16;
inline constexpr int kMatrixW = 5;
inline constexpr int kMatrixH = 9;  // seven rows to the baseline, two for descenders

// The art, as pixel_art.cpp writes it.
std::string_view terminal_art();
std::string_view matrix_art();

// Whether the terminal's pixel (x, y) of `code` is ink (y down from the cell's top).
bool terminal_pixel(uint8_t code, int x, int y);
bool matrix_pixel(uint8_t code, int x, int y);

// `code` from the terminal's art into one cell `w` x `h` px, nearest-neighbour.
void render_terminal(uint8_t code, int w, int h, uint8_t* out, int stride);

// The matrix's lamps and the segment display's lit bars as designs (fills), in the design
// space. A code the display cannot show draws nothing; the caller draws its skeleton instead.
Design matrix_design(uint8_t code);
Design segment_design(uint8_t code);
// The segments `code` lights: a string of segment letters (pixel.cpp names them), or nullptr
// when the display has no shape for it.
const char* segments_of(uint8_t code);

}  // namespace ge::charset
