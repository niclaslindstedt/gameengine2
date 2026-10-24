// engine/charset: the ten fonts, held by the engine's own checks.
//
//   * THE GRID: cell_rect tiles the atlas 16 x 8; code_of sends every byte outside the set to
//     the missing glyph.
//   * THE DESIGNS: every skeleton parses to the end; every printable code but the space and
//     every UI symbol has ink in every font; no ink touches a cell's edge (so a filtered sample
//     never reads a neighbour) but the box-drawing symbols', which tile.
//   * THE BUILD: an atlas is the same bytes on one worker and on four.
//   * THE COMMITTED ATLASES: engine/charset/atlases/<font>_<size>.png is what build_atlas makes,
//     pixel for pixel, for every font at 256, 512 and 1024. A deliberate change rewrites them:
//     `GE_GOLDEN_WRITE=1 build/<preset>/tests/charset_charset_test`, and the PR shows the
//     `charsets` lab's sheets before and after.
//
// ge:summary Tests for the charsets: the grid, the designs, the build, the committed atlases
// ge:keywords test charset font atlas glyph design margin committed golden png
// ge:skill ui testing
#include <cstdio>
#include <cstdlib>
#include <string>

#include "charset/charset.hpp"
#include "charset/design.hpp"
#include "charset/pixel.hpp"
#include "check.hpp"
#include "lab/png.hpp"

namespace {

using namespace ge::charset;

bool env_set(const char* name) {
#ifdef _MSC_VER
  char* v = nullptr;
  size_t n = 0;
  const bool set = _dupenv_s(&v, &n, name) == 0 && v != nullptr;
  std::free(v);
  return set;
#else
  return std::getenv(name) != nullptr;
#endif
}

bool tiles(uint8_t code) { return (code >= 13 && code <= 15) || (code >= 17 && code <= 25); }

int ink(const Atlas& a, uint8_t code) {
  const CellRect r = cell_rect(a.size, code);
  int n = 0;
  for (int y = r.y; y < r.y + r.h; ++y) {
    for (int x = r.x; x < r.x + r.w; ++x) n += a.at(x, y) > 0;
  }
  return n;
}

// The ink on the cell's outermost ring of pixels.
int rim(const Atlas& a, uint8_t code) {
  const CellRect r = cell_rect(a.size, code);
  int n = 0;
  for (int y = r.y; y < r.y + r.h; ++y) {
    for (int x = r.x; x < r.x + r.w; ++x) {
      const bool edge = x == r.x || y == r.y || x == r.x + r.w - 1 || y == r.y + r.h - 1;
      n += edge && a.at(x, y) > 0;
    }
  }
  return n;
}

TEST(grid_tiles_the_atlas) {
  CHECK(kCodes == 128);
  const CellRect a = cell_rect(256, 'A');  // 0x41: column 1, row 4
  CHECK(a.x == 16 && a.y == 128 && a.w == 16 && a.h == 32);
  const CellRect last = cell_rect(1024, 127);
  CHECK(last.x + last.w == 1024 && last.y + last.h == 1024);
  CHECK(code_of('A') == 'A');
  CHECK(code_of(1) == 1);
  CHECK(code_of(0) == kMissing);
  CHECK(code_of(200) == kMissing);
  CHECK(code_of(127) == kMissing);
  CHECK(find_font("gothic") && find_font("gothic")->id == FontId::gothic);
  CHECK(find_font("helvetica") == nullptr);
  CHECK(fonts().size() == static_cast<size_t>(kFontCount));
}

TEST(every_design_parses) {
  for (const Zero z : {Zero::plain, Zero::slashed, Zero::dotted}) {
    for (const bool single : {false, true}) {
      const Variant v{z, single};
      for (int c = 1; c < kCodes; ++c) {
        const Design d = design_of(static_cast<uint8_t>(c), v);
        CHECK_MSG(!d.bad, "code " + std::to_string(c) +
                              "'s design has an unknown operator: fix "
                              "its string in engine/charset/design_glyphs.cpp");
      }
    }
  }
  CHECK(parse_design("M 0 0 L 10 10 X").bad);
  const Design o = parse_design("O 50 50 20 30");
  CHECK(o.paths.size() == 1 && o.paths[0].closed && o.paths[0].ellipse);
}

TEST(segment_display_covers_ascii) {
  for (int c = 33; c <= 126; ++c) {
    CHECK_MSG(segments_of(static_cast<uint8_t>(c)) != nullptr,
              "the segment display has no shape for code " + std::to_string(c));
  }
  CHECK(std::string(segments_of('a')) == segments_of('A'));
}

TEST(every_glyph_has_ink_inside_its_cell) {
  for (const FontInfo& f : fonts()) {
    const Atlas a = build_atlas(f.id, 256);
    for (int c = 1; c < kCodes; ++c) {
      const auto code = static_cast<uint8_t>(c);
      if (code == ' ' || code == '\n') {
        CHECK(ink(a, code) == 0);
        continue;
      }
      CHECK_MSG(ink(a, code) > 0, std::string(f.name) + " draws nothing for code " +
                                      std::to_string(c) + ": look at the `charsets` lab's atlas");
      // A pixel font is sampled nearest, so only a drawn one must keep off its cell's edge.
      if (!f.pixel && !tiles(code)) {
        CHECK_MSG(rim(a, code) == 0, std::string(f.name) + "'s code " + std::to_string(c) +
                                         " touches its cell's edge at 256 px: pull its design "
                                         "in or thin the font's pen");
      }
    }
    CHECK(ink(a, 0) == 0 && ink(a, '\n') == 0);
  }
}

TEST(build_is_the_same_on_any_number_of_workers) {
  for (const FontId id : {FontId::mono, FontId::marker, FontId::matrix}) {
    CHECK(build_atlas(id, 512, 1).coverage == build_atlas(id, 512, 4).coverage);
  }
}

TEST(committed_atlases_are_what_the_builder_makes) {
  const bool write = env_set("GE_GOLDEN_WRITE");
  for (const FontInfo& f : fonts()) {
    for (const int size : kAtlasSizes) {
      const Atlas a = build_atlas(f.id, size);
      const std::string path = ge::test::root_path(atlas_path(f.id, size).c_str());
      if (write) {
        CHECK(ge::lab::write_file(path, ge::lab::encode_png_grey(size, size, a.coverage)));
        continue;
      }
      const auto pic = ge::lab::read_png(path);
      CHECK_MSG(pic.has_value(), path +
                                     " is missing or unreadable: rewrite the atlases with "
                                     "GE_GOLDEN_WRITE=1 build/<preset>/tests/charset_charset_test");
      if (!pic) continue;
      CHECK(pic->width == size && pic->height == size);
      if (pic->width != size || pic->height != size) continue;
      const std::vector<uint8_t>& rgba = pic->rgba;
      int differ = 0;
      for (size_t i = 0; i < a.coverage.size(); ++i) differ += rgba[i * 4] != a.coverage[i];
      CHECK_MSG(differ == 0, path + ": " + std::to_string(differ) +
                                 " pixels differ from build_atlas. If the change was meant, "
                                 "rewrite them (GE_GOLDEN_WRITE=1) and show the charsets lab");
    }
  }
}

}  // namespace
