// engine/charset's text, layers and effects, held by the engine's own checks:
//
//   * THE LAYOUT: a run's size is its widest line by its lines on the cell grid; a pixel font
//     drawn at twice its grid is its atlas's pixels exactly.
//   * THE LAYERS: each spans 80 levels or more, wraps at its edges (the seam no rougher than the
//     inside), and is what engine/charset/layers/<name>_<size>.png holds (GE_GOLDEN_WRITE=1
//     rewrites them).
//   * THE DISTANCE FIELD: a disc's field is the distance to its rim, negative inside.
//   * THE LOOKS: `plain` draws the coverage as it is; every look draws something, and a glow
//     or a shadow reaches past the ink.
//
// ge:summary Tests for the text layout, the noise layers, the distance field and the looks
// ge:keywords test charset text layout layers tileable distance-field looks compose golden
// ge:skill ui testing
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "charset/effects.hpp"
#include "charset/layers.hpp"
#include "charset/text.hpp"
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

// Every layer at every committed size, made once for the whole file.
const Texture& layer_at(Layer id, int size) {
  static std::vector<Texture> made[kLayerCount];
  for (const Texture& t : made[static_cast<int>(id)]) {
    if (t.size == size) return t;
  }
  made[static_cast<int>(id)].push_back(make_layer(id, size));
  return made[static_cast<int>(id)].back();
}

TEST(text_size_is_the_cell_grid) {
  TextPlace p;
  p.cell_h = 40;
  double w, h;
  text_size("ABC\nDE", p, w, h);
  CHECK(w == 60 && h == 80);
  p.tracking = 2;
  p.leading = 4;
  text_size("ABC\nDE", p, w, h);
  CHECK(w == 64 && h == 84);
}

TEST(a_pixel_font_at_twice_its_grid_is_its_pixels) {
  const Atlas a = build_atlas(FontId::terminal, 256);  // 16 x 32 cells: the grid at 2x
  Canvas c(16, 32);
  TextPlace p;
  p.cell_h = 32;
  draw_text(c, a, "A", p);
  const CellRect r = cell_rect(256, 'A');
  int differ = 0;
  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 16; ++x) differ += c.at(x, y) != a.at(r.x + x, r.y + y) / 255.0;
  }
  CHECK(differ == 0);
}

TEST(layers_span_dark_and_light_and_wrap) {
  for (const LayerInfo& li : layers()) {
    const Texture& t = layer_at(li.id, 256);
    int lo = 255, hi = 0;
    for (int y = 0; y < 256; ++y) {
      for (int x = 0; x < 256; ++x) {
        lo = std::min<int>(lo, t.at(x, y));
        hi = std::max<int>(hi, t.at(x, y));
      }
    }
    // The seam's step against the mean step between neighbours anywhere in the texture.
    double inside = 0, seam = 0;
    for (int i = 0; i < 256; ++i) {
      seam += std::abs(t.at(0, i) - t.at(255, i)) + std::abs(t.at(i, 0) - t.at(i, 255));
      for (int j = 1; j < 256; ++j) {
        inside +=
            (std::abs(t.at(j, i) - t.at(j - 1, i)) + std::abs(t.at(i, j) - t.at(i, j - 1))) / 255.0;
      }
    }
    CHECK_MSG(hi - lo >= 80, std::string(li.name) + " spans only " + std::to_string(lo) + ".." +
                                 std::to_string(hi));
    CHECK_MSG(seam <= inside * 1.6 + 512, std::string(li.name) + " does not wrap: its seam is " +
                                              std::to_string(seam) + " against " +
                                              std::to_string(inside) + " inside");
  }
}

TEST(committed_layers_are_what_make_layer_makes) {
  const bool write = env_set("GE_GOLDEN_WRITE");
  for (const LayerInfo& li : layers()) {
    for (const int size : kLayerSizes) {
      const Texture& t = layer_at(li.id, size);
      const std::string path = ge::test::root_path(layer_path(li.id, size).c_str());
      if (write) {
        CHECK(ge::lab::write_file(path, ge::lab::encode_png_grey(size, size, t.value)));
        continue;
      }
      const auto pic = ge::lab::read_png(path);
      CHECK_MSG(pic && pic->width == size && pic->height == size,
                path + " is missing or the wrong size: GE_GOLDEN_WRITE=1 rewrites it");
      if (!pic.has_value() || pic->width != size || pic->height != size) continue;
      const std::vector<uint8_t>& rgba = pic->rgba;
      int differ = 0;
      for (size_t i = 0; i < t.value.size(); ++i) differ += rgba[i * 4] != t.value[i];
      CHECK_MSG(differ == 0, path + ": " + std::to_string(differ) + " texels differ");
    }
  }
}

TEST(distance_field_of_a_disc) {
  Canvas c(64, 64);
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      const double d = std::sqrt((x + 0.5 - 32) * (x + 0.5 - 32) + (y + 0.5 - 32) * (y + 0.5 - 32));
      c.cov[static_cast<size_t>(y) * 64 + x] = d < 10 ? 1 : 0;
    }
  }
  const std::vector<double> sd = distance_field(c);
  CHECK(std::fabs(sd[32 * 64 + 32] + 9.5) < 1.0);  // the centre: about the radius inside
  CHECK(std::fabs(sd[32 * 64 + 52] - 10.5) <
        1.0);  // 20.5 px out from the centre, 10.5 past the rim
  CHECK(sd[32 * 64 + 41] < 0 && sd[32 * 64 + 43] > 0);
}

TEST(looks_draw_what_they_say) {
  const Atlas a = build_atlas(FontId::mono, 256);
  Canvas c(120, 48);
  TextPlace p;
  p.x = 10;
  p.y = 8;
  p.cell_h = 32;
  draw_text(c, a, "Ag", p);
  LayerCache cache;
  const VMetrics& m = font_info(FontId::mono).metrics;
  for (const Look& lk : looks()) {
    // Over a black ground (what `plain` is checked on) and a grey one (a black shadow shows).
    std::vector<uint8_t> px(c.cov.size() * 4, 0), grey(c.cov.size() * 4, 128);
    for (size_t i = 3; i < px.size(); i += 4) px[i] = grey[i] = 255;
    compose(c, p, m, lk, cache, px.data());
    compose(c, p, m, lk, cache, grey.data());
    int lit = 0, beyond = 0;
    for (size_t i = 0; i < c.cov.size(); ++i) {
      lit += px[i * 4] + px[i * 4 + 1] + px[i * 4 + 2] > 0;
      const bool changed = grey[i * 4] != 128 || grey[i * 4 + 1] != 128 || grey[i * 4 + 2] != 128;
      beyond += changed && c.cov[i] == 0;
    }
    CHECK_MSG(lit > 0, std::string(lk.name) + " drew nothing");
    if (lk.glow > 0 || lk.shadow_alpha > 0 || lk.outline > 0) {
      CHECK_MSG(beyond > 0, std::string(lk.name) + " reached no pixel past the ink");
    }
    if (std::string(lk.name) == "plain") {
      int wrong = 0;
      for (size_t i = 0; i < c.cov.size(); ++i) {
        wrong += std::abs(px[i * 4] - static_cast<int>(std::lround(c.cov[i] * 255))) > 1;
      }
      CHECK(wrong == 0);
    }
  }
  CHECK(find_look("rusted") && find_look("nope") == nullptr);
}

}  // namespace
