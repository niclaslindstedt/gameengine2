// The grid fonts' readers and builders: the art parsed once into tables, the terminal scaled
// nearest, the matrix's lamps and the segment display's bars laid out in the design space.
//
// ge:summary Parses the pixel art; builds the matrix lamps and the 16-segment bars as designs
// ge:keywords pixel-font terminal matrix segment parse art lamps bars segments_of
// ge:skill ui
// ge:test tests/charset_charset_test.cpp
#include "charset/pixel.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <vector>

namespace ge::charset {
namespace {

template <int W, int H>
struct Grid {
  std::array<std::array<std::array<bool, W>, H>, 128> ink{};
};

// Reads art in pixel_art.cpp's layout: an `@` line of hex codes, then H rows of W-wide glyphs
// separated by one space.
template <int W, int H>
Grid<W, H> parse(std::string_view art) {
  Grid<W, H> g;
  std::vector<int> codes;
  int row = 0;
  size_t i = 0;
  while (i < art.size()) {
    size_t end = art.find('\n', i);
    if (end == std::string_view::npos) end = art.size();
    const std::string_view line = art.substr(i, end - i);
    i = end + 1;
    if (line.empty()) continue;
    if (line[0] == '@') {
      codes.clear();
      row = 0;
      size_t k = 1;
      while (k < line.size()) {
        while (k < line.size() && line[k] == ' ') ++k;
        int v = 0;
        const auto [p, ec] = std::from_chars(line.data() + k, line.data() + line.size(), v, 16);
        if (ec != std::errc{}) break;
        codes.push_back(v);
        k = static_cast<size_t>(p - line.data());
      }
      continue;
    }
    if (row >= H) continue;
    for (size_t c = 0; c < codes.size(); ++c) {
      for (int x = 0; x < W; ++x) {
        const size_t at = c * (W + 1) + x;
        if (at < line.size() && line[at] == '#') g.ink[codes[c] & 127][row][x] = true;
      }
    }
    ++row;
  }
  return g;
}

const Grid<kTerminalW, kTerminalH>& terminal() {
  static const auto g = parse<kTerminalW, kTerminalH>(terminal_art());
  return g;
}
const Grid<kMatrixW, kMatrixH>& matrix() {
  static const auto g = parse<kMatrixW, kMatrixH>(matrix_art());
  return g;
}

// The shades are patterns, not art: every other pixel on every other row (light) or a
// checkerboard (medium), so a run of them tiles.
bool shade(uint8_t code, int x, int y) {
  if (code == 24) return (x % 2 == 0) && (y % 2 == 0);
  return (x + y) % 2 == 0;
}

// THE SEGMENTS, by letter, on the digit box x 18..82, y 0..100:
//   a b   the top bar's halves         e f   the bottom bar's (right, left)
//   c d   the right side (upper, lower) g h   the left side (lower, upper)
//   i j   the middle bar's halves      k m   the upper diagonals (left, right)
//   l o   the centre verticals (upper, lower)   n p   the lower diagonals (left, right)
//   .     the point                    q     the colon's two dots
constexpr double kL = 18, kR = 82, kC = 50, kT = 100, kB = 0, kM = 50;
constexpr double kBar = 12;   // a bar's thickness, units
constexpr double kGap = 1.8;  // the dark gap between two bars' tips

std::vector<Pt> bar(Pt a, Pt b) {
  // A bar from a to b with pointed (hexagonal) tips, pulled back from both ends by the gap.
  const double dx = b.x - a.x, dy = b.y - a.y;
  const double l = std::abs(dx) + std::abs(dy);  // axis-aligned: one of them is zero
  const double ux = dx / l, uy = dy / l;
  const double h = kBar / 2;
  const Pt p{a.x + ux * kGap, a.y + uy * kGap};
  const Pt q{b.x - ux * kGap, b.y - uy * kGap};
  return {p,
          {p.x + ux * h - uy * h, p.y + uy * h + ux * h},
          {q.x - ux * h - uy * h, q.y - uy * h + ux * h},
          q,
          {q.x - ux * h + uy * h, q.y - uy * h - ux * h},
          {p.x + ux * h + uy * h, p.y + uy * h - ux * h}};
}

std::vector<Pt> diagonal(Pt a, Pt b) {
  // A slanted bar from the box's corner `a` to its centre `b`, its ends cut level with the
  // bars round it, kGap clear of them.
  const double sx = b.x > a.x ? 1 : -1, sy = b.y > a.y ? 1 : -1;
  const double clear = kBar / 2 + kGap;
  const double dw = kBar * 0.8;
  const Pt out{a.x + sx * clear, a.y + sy * clear};
  const Pt in{b.x - sx * clear, b.y - sy * clear};
  return {out, {out.x + sx * dw, out.y}, in, {in.x - sx * dw, in.y}};
}

std::vector<Pt> segment(char s) {
  switch (s) {
    case 'a':
      return bar({kL, kT}, {kC, kT});
    case 'b':
      return bar({kC, kT}, {kR, kT});
    case 'c':
      return bar({kR, kM}, {kR, kT});
    case 'd':
      return bar({kR, kB}, {kR, kM});
    case 'e':
      return bar({kC, kB}, {kR, kB});
    case 'f':
      return bar({kL, kB}, {kC, kB});
    case 'g':
      return bar({kL, kB}, {kL, kM});
    case 'h':
      return bar({kL, kM}, {kL, kT});
    case 'i':
      return bar({kL, kM}, {kC, kM});
    case 'j':
      return bar({kC, kM}, {kR, kM});
    case 'l':
      return bar({kC, kM}, {kC, kT});
    case 'o':
      return bar({kC, kB}, {kC, kM});
    case 'k':
      return diagonal({kL, kT}, {kC, kM});
    case 'm':
      return diagonal({kR, kT}, {kC, kM});
    case 'n':
      return diagonal({kL, kB}, {kC, kM});
    case 'p':
      return diagonal({kR, kB}, {kC, kM});
    default:
      return {};
  }
}

void square_dot(Design& d, double x, double y, double r) {
  d.fills.push_back({{x - r, y - r}, {x + r, y - r}, {x + r, y + r}, {x - r, y + r}});
}

}  // namespace

bool terminal_pixel(uint8_t code, int x, int y) {
  if (code == 24 || code == 25) return shade(code, x, y);
  return terminal().ink[code & 127][y][x];
}

bool matrix_pixel(uint8_t code, int x, int y) {
  if (code == 24 || code == 25) return shade(code, x, y);
  return matrix().ink[code & 127][y][x];
}

void render_terminal(uint8_t code, int w, int h, uint8_t* out, int stride) {
  for (int py = 0; py < h; ++py) {
    const int gy = py * kTerminalH / h;
    for (int px = 0; px < w; ++px) {
      const int gx = px * kTerminalW / w;
      out[static_cast<size_t>(py) * stride + px] = terminal_pixel(code, gx, gy) ? 255 : 0;
    }
  }
}

Design matrix_design(uint8_t code) {
  // The lamps on a 13-unit pitch: columns centred on x 24..76, the baseline row's lamps
  // sitting on the baseline, the two descender rows below it.
  constexpr double kPitch = 13, kLamp = 5.4;
  Design d;
  for (int y = 0; y < kMatrixH; ++y) {
    for (int x = 0; x < kMatrixW; ++x) {
      if (!matrix_pixel(code, x, y)) continue;
      d.discs.push_back({kC + (x - 2) * kPitch, 6 + (6 - y) * kPitch, kLamp, kLamp});
    }
  }
  return d;
}

const char* segments_of(uint8_t code) {
  if (code >= 'a' && code <= 'z') code = static_cast<uint8_t>(code - 'a' + 'A');
  static constexpr const char* kAscii[95] = {
      "",          "l.",        "hl",         "cdefijlo",   "abhijdeflo", "ahmnde",   "abklnefp",
      "l",         "mp",        "kn",         "ijklmnop",   "ijlo",       "n",        "ij",
      ".",         "mn",        "abcdefghmn", "cd",         "abcijgef",   "abcdefj",  "hijcd",
      "abhijdef",  "abhgefdij", "abcd",       "abcdefghij", "abcdefhij",  "q",        "qn",
      "mp",        "ijef",      "kn",         "abcjo.",     "abchefgjl",  "abcdghij", "abcdefloj",
      "abghef",    "abcdeflo",  "abghefi",    "abghi",      "abghefdj",   "ghcdij",   "abeflo",
      "cdefg",     "ghimp",     "ghef",       "ghcdkm",     "ghcdkp",     "abcdefgh", "abchij",
      "abcdefghp", "abchijp",   "abhijdef",   "ablo",       "cdefgh",     "ghnm",     "ghcdnp",
      "kmnp",      "kmo",       "abmnef",     "ahgf",       "kp",         "bcde",     "np",
      "ef",        "k",         nullptr,      nullptr,      nullptr,      nullptr,    nullptr,
      nullptr,     nullptr,     nullptr,      nullptr,      nullptr,      nullptr,    nullptr,
      nullptr,     nullptr,     nullptr,      nullptr,      nullptr,      nullptr,    nullptr,
      nullptr,     nullptr,     nullptr,      nullptr,      nullptr,      nullptr,    nullptr,
      "bfloi",     "lo",        "aeloj",      "hijc",
  };
  if (code < 32 || code > 126) return nullptr;
  return kAscii[code - 32];
}

Design segment_design(uint8_t code) {
  Design d;
  const char* lit = segments_of(code);
  if (!lit) return d;
  for (const char* s = lit; *s; ++s) {
    if (*s == '.') {
      square_dot(d, kR + 9, kB + 4, kBar * 0.55);
    } else if (*s == 'q') {
      square_dot(d, kC, 72, kBar * 0.55);
      square_dot(d, kC, 26, kBar * 0.55);
    } else {
      d.fills.push_back(segment(*s));
    }
  }
  return d;
}

}  // namespace ge::charset
