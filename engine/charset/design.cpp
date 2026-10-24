// The design language's parser and the path arithmetic the pen shares with it.
//
// ge:summary Parses a glyph's design string into paths, dots, fills and discs
// ge:keywords glyph design parse source path arc ellipse path_end parse_design design_of
// ge:skill ui
// ge:test tests/charset_charset_test.cpp
#include "charset/design.hpp"

#include <algorithm>

#include <charconv>
#include <cmath>
#include <string>

#include "core/libm/libm.hpp"

namespace ge::charset {
namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

Pt on_ellipse(double cx, double cy, double rx, double ry, double deg) {
  const double a = deg * kDegToRad;
  return {cx + rx * core::libm::cos(a), cy + ry * core::libm::sin(a)};
}

struct Reader {
  std::string_view s;
  size_t i = 0;
  void skip() {
    while (i < s.size() && s[i] == ' ') ++i;
  }
  bool done() {
    skip();
    return i >= s.size();
  }
  char op() {
    skip();
    return s[i++];
  }
  double num() {
    skip();
    double v = 0;
    const auto [end, ec] = std::from_chars(s.data() + i, s.data() + s.size(), v);
    if (ec != std::errc{}) return 0;
    i = static_cast<size_t>(end - s.data());
    return v;
  }
};

bool near(Pt a, Pt b) { return std::fabs(a.x - b.x) < 1.5 && std::fabs(a.y - b.y) < 1.5; }

}  // namespace

Pt path_end(const Path& path) {
  Pt at = path.start;
  for (const Piece& p : path.pieces) {
    at = p.arc ? on_ellipse(p.cx, p.cy, p.rx, p.ry, p.a1) : p.to;
  }
  return at;
}

Design parse_design(std::string_view source) {
  Design d;
  Reader r{source};
  Path* open = nullptr;
  while (!r.done()) {
    const char op = r.op();
    switch (op) {
      case 'M': {
        d.paths.push_back({});
        open = &d.paths.back();
        open->start = {r.num(), r.num()};
        break;
      }
      case 'L': {
        Pt to{r.num(), r.num()};
        if (!open) {
          d.paths.push_back({});
          open = &d.paths.back();
          open->start = to;
          break;
        }
        open->pieces.push_back({false, to});
        break;
      }
      case 'A': {
        Piece p;
        p.arc = true;
        p.cx = r.num();
        p.cy = r.num();
        p.rx = r.num();
        p.ry = r.num();
        p.a0 = r.num();
        p.a1 = r.num();
        const Pt from = on_ellipse(p.cx, p.cy, p.rx, p.ry, p.a0);
        if (!open || !near(path_end(*open), from)) {
          d.paths.push_back({});
          open = &d.paths.back();
          open->start = from;
        }
        open->pieces.push_back(p);
        break;
      }
      case 'O': {
        Piece p;
        p.arc = true;
        p.cx = r.num();
        p.cy = r.num();
        p.rx = r.num();
        p.ry = r.num();
        p.a0 = 90;
        p.a1 = 450;
        Path path;
        path.start = on_ellipse(p.cx, p.cy, p.rx, p.ry, 90);
        path.pieces.push_back(p);
        path.closed = true;
        path.ellipse = true;
        d.paths.push_back(path);
        open = nullptr;
        break;
      }
      case 'Z': {
        if (open) {
          open->pieces.push_back({false, open->start});
          open->closed = true;
        }
        open = nullptr;
        break;
      }
      case 'D':
        d.dots.push_back({r.num(), r.num()});
        break;
      case 'F': {
        const int n = static_cast<int>(r.num());
        std::vector<Pt> poly;
        poly.reserve(static_cast<size_t>(std::max(n, 0)));
        for (int k = 0; k < n; ++k) poly.push_back({r.num(), r.num()});
        d.fills.push_back(std::move(poly));
        break;
      }
      case 'E':
        d.discs.push_back({r.num(), r.num(), r.num(), r.num()});
        break;
      default:  // a typo ends the glyph; tests/charset_charset_test.cpp catches it
        d.bad = true;
        return d;
    }
  }
  return d;
}

Design design_of(uint8_t code, const Variant& variant) {
  Design d = parse_design(design_source(code, variant));
  if (code == 24) d.dither = 1;
  if (code == 25) d.dither = 2;
  return d;
}

}  // namespace ge::charset
