// The looks' table, the distance transform and the compositor.
//
// ge:summary The ten looks, the exact distance transform, and the effect compositor
// ge:keywords effects looks compose distance-field edt felzenszwalb blur shadow glow bevel
// ge:skill ui
// ge:test tests/charset_effects_test.cpp
#include "charset/effects.hpp"

#include <algorithm>
#include <cmath>

#include "core/num.hpp"

namespace ge::charset {
namespace {

LayerUse use(Layer layer, double strength, double scale) { return {true, layer, strength, scale}; }

Look named(const char* name, const char* what) {
  Look l;
  l.name = name;
  l.use = what;
  return l;
}

std::array<Look, kLookCount> make_looks() {
  std::array<Look, kLookCount> l{};
  l[0] = named("plain", "the bare ink: a tinted message, a debug line");
  l[1] = named("shadow", "a HUD over bright snow or sky: white ink on a soft shadow");
  l[1].shadow_dx = 0.03;
  l[1].shadow_dy = 0.05;
  l[1].shadow_blur = 0.05;
  l[1].shadow_alpha = 0.8;
  l[2] = named("outlined", "readable over anything: subtitles, names over heads, a kill feed");
  l[2].top = {1, 0.86, 0.3};
  l[2].bottom = {1, 0.62, 0.12};
  l[2].outline = 0.055;
  l[2].outline_rgb = {0.05, 0.03, 0.02};
  l[3] = named("rusted", "a title plate of corroded metal: menus, a level's name");
  l[3].top = {0.86, 0.56, 0.32};
  l[3].bottom = {0.46, 0.22, 0.1};
  l[3].fill = use(Layer::rust, 0.85, 0.7);
  l[3].outline = 0.02;
  l[3].outline_rgb = {0.12, 0.05, 0.02};
  l[3].shadow_dx = 0.035;
  l[3].shadow_dy = 0.05;
  l[3].shadow_blur = 0.03;
  l[3].shadow_alpha = 0.9;
  l[3].bevel = 0.07;
  l[3].bevel_strength = 0.9;
  l[4] = named("phosphor", "a green CRT: the console, a terminal on a wall");
  l[4].top = {0.62, 1, 0.66};
  l[4].bottom = {0.45, 0.95, 0.5};
  l[4].glow = 0.22;
  l[4].glow_strength = 0.5;
  l[4].glow_rgb = {0.15, 0.95, 0.3};
  l[4].fill = use(Layer::grain, 0.25, 1);
  l[4].overlay = use(Layer::scanlines, 0.6, 0.5);
  l[5] = named("neon", "a sign at night: an arcade menu, a bar's name");
  l[5].top = {1, 0.9, 1};
  l[5].bottom = {1, 0.75, 0.95};
  l[5].glow = 0.35;
  l[5].glow_strength = 0.95;
  l[5].glow_rgb = {1, 0.18, 0.75};
  l[6] = named("worn", "stamped or stencilled paint, half gone: crates, signs, old paper");
  l[6].top = {0.93, 0.9, 0.8};
  l[6].bottom = {0.88, 0.84, 0.72};
  l[6].erode = use(Layer::grunge, 0.2, 1.2);
  l[6].fill = use(Layer::grain, 0.35, 1);
  l[7] = named("chrome", "polished steel: a logo, a high score, a sci-fi title");
  l[7].top = {0.97, 0.98, 1};
  l[7].bottom = {0.3, 0.36, 0.45};
  l[7].fill = use(Layer::brushed, 0.5, 0.5);
  l[7].outline = 0.03;
  l[7].outline_rgb = {0.06, 0.07, 0.1};
  l[7].shadow_dy = 0.04;
  l[7].shadow_blur = 0.04;
  l[7].shadow_alpha = 0.7;
  l[7].bevel = 0.06;
  l[7].bevel_strength = 1.1;
  l[8] = named("carved", "cut into cracked stone: a tomb, a gate, an old inscription");
  l[8].top = {0.62, 0.6, 0.56};
  l[8].bottom = {0.44, 0.42, 0.39};
  l[8].fill = use(Layer::cracks, 0.6, 0.6);
  l[8].bevel = 0.09;
  l[8].bevel_strength = 1.3;
  l[8].bevel_inset = true;
  l[9] = named("amber", "an amber lamp board: scores, timings, a station's departures");
  l[9].top = {1, 0.75, 0.25};
  l[9].bottom = {1, 0.62, 0.12};
  l[9].glow = 0.14;
  l[9].glow_strength = 0.65;
  l[9].glow_rgb = {1, 0.42, 0.04};
  l[9].fill = use(Layer::grain, 0.15, 1);
  return l;
}

const std::array<Look, kLookCount>& table() {
  static const std::array<Look, kLookCount> l = make_looks();
  return l;
}

// The squared distance transform of one row (Felzenszwalb and Huttenlocher's lower envelope of
// parabolas): f in, d out, n long.
void edt_1d(const double* f, double* d, int n, std::vector<int>& v, std::vector<double>& z) {
  constexpr double kInf = 1e20;
  auto cut = [&](int q, int p) {  // where q's parabola overtakes p's
    return ((f[q] + double(q) * q) - (f[p] + double(p) * p)) / (2.0 * q - 2.0 * p);
  };
  int k = 0;
  v[0] = 0;
  z[0] = -kInf;
  z[1] = kInf;
  for (int q = 1; q < n; ++q) {
    double s = cut(q, v[k]);
    while (s <= z[k]) s = cut(q, v[--k]);
    ++k;
    v[k] = q;
    z[k] = s;
    z[k + 1] = kInf;
  }
  k = 0;
  for (int q = 0; q < n; ++q) {
    while (z[k + 1] < q) ++k;
    const double dq = q - v[k];
    d[q] = dq * dq + f[v[k]];
  }
}

// The squared Euclidean distance of every pixel to the nearest `seed` pixel.
std::vector<double> edt(const std::vector<bool>& seed, int w, int h) {
  constexpr double kInf = 1e20;
  std::vector<double> g(static_cast<size_t>(w) * h);
  for (size_t i = 0; i < g.size(); ++i) g[i] = seed[i] ? 0 : kInf;
  const int n = std::max(w, h);
  std::vector<double> f(n), d(n), z(n + 1);
  std::vector<int> v(n);
  for (int x = 0; x < w; ++x) {
    for (int y = 0; y < h; ++y) f[y] = g[static_cast<size_t>(y) * w + x];
    edt_1d(f.data(), d.data(), h, v, z);
    for (int y = 0; y < h; ++y) g[static_cast<size_t>(y) * w + x] = d[y];
  }
  for (int y = 0; y < h; ++y) {
    edt_1d(&g[static_cast<size_t>(y) * w], d.data(), w, v, z);
    std::copy(d.begin(), d.begin() + w, g.begin() + static_cast<ptrdiff_t>(y) * w);
  }
  return g;
}

// A box blur of radius r (px) applied three times each way: close to a Gaussian.
void blur(std::vector<double>& a, int w, int h, int r) {
  if (r <= 0) return;
  std::vector<double> tmp(a.size());
  for (int pass = 0; pass < 3; ++pass) {
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        double s = 0;
        for (int k = -r; k <= r; ++k)
          s += a[static_cast<size_t>(y) * w + std::clamp(x + k, 0, w - 1)];
        tmp[static_cast<size_t>(y) * w + x] = s / (2 * r + 1);
      }
    }
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        double s = 0;
        for (int k = -r; k <= r; ++k)
          s += tmp[static_cast<size_t>(std::clamp(y + k, 0, h - 1)) * w + x];
        a[static_cast<size_t>(y) * w + x] = s / (2 * r + 1);
      }
    }
  }
}

double texel(LayerCache& layers, const LayerUse& u, int x, int y) {
  const Texture& t = layers.get(u.layer);
  return t.at(static_cast<int>(std::floor(x * u.scale)),
              static_cast<int>(std::floor(y * u.scale))) /
         255.0;
}

Rgb mix(Rgb a, Rgb b, double t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

}  // namespace

std::span<const Look> looks() { return table(); }
const Look& look(LookId id) { return table()[static_cast<size_t>(id)]; }
const Look* find_look(std::string_view name) {
  for (const Look& l : table()) {
    if (name == l.name) return &l;
  }
  return nullptr;
}

const Texture& LayerCache::get(Layer layer) {
  Texture& t = built[static_cast<size_t>(layer)];
  if (t.size == 0) t = make_layer(layer, 256);
  return t;
}

std::vector<double> distance_field(const Canvas& c) {
  const int w = c.width, h = c.height;
  std::vector<bool> in(static_cast<size_t>(w) * h), out(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    in[i] = c.cov[i] >= 0.5;
    out[i] = !in[i];
  }
  const std::vector<double> to_in = edt(in, w, h), to_out = edt(out, w, h);
  std::vector<double> sd(in.size());
  for (size_t i = 0; i < sd.size(); ++i) {
    const double cov = c.cov[i];
    if (cov > 0 && cov < 1) {
      sd[i] = 0.5 - cov;  // the rim: the coverage says where the edge crosses the pixel
    } else if (in[i]) {
      sd[i] = 0.5 - std::sqrt(to_out[i]);
    } else {
      sd[i] = std::sqrt(to_in[i]) - 0.5;
    }
  }
  return sd;
}

void compose(const Canvas& text, const TextPlace& place, const VMetrics& metrics, const Look& lk,
             LayerCache& layers, uint8_t* rgba) {
  const int w = text.width, h = text.height;
  const double u = place.cell_h;  // the look's sizes are shares of this
  Canvas ink = text;
  if (lk.erode.on) {
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const double m = texel(layers, lk.erode, x, y);
        ink.cov[static_cast<size_t>(y) * w + x] *=
            std::clamp((m - lk.erode.strength) * 5 + 0.5, 0.0, 1.0);
      }
    }
  }
  const bool need_field = lk.outline > 0 || lk.glow > 0 || lk.bevel > 0 || lk.shadow_alpha > 0;
  const std::vector<double> sd = need_field ? distance_field(ink) : std::vector<double>{};

  // The body a shadow is cast by: the ink and its outline.
  std::vector<double> shadow;
  if (lk.shadow_alpha > 0) {
    shadow.assign(ink.cov.size(), 0);
    const int dx = static_cast<int>(core::num::round(lk.shadow_dx * u));
    const int dy = static_cast<int>(core::num::round(lk.shadow_dy * u));
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const int sx = x - dx, sy = y - dy;
        if (sx < 0 || sy < 0 || sx >= w || sy >= h) continue;
        const double d = sd[static_cast<size_t>(sy) * w + sx];
        shadow[static_cast<size_t>(y) * w + x] = std::clamp(lk.outline * u - d + 0.5, 0.0, 1.0);
      }
    }
    blur(shadow, w, h, static_cast<int>(core::num::round(lk.shadow_blur * u / 2)));
  }

  const double line_h = place.cell_h + place.leading;
  for (int y = 0; y < h; ++y) {
    // Where in its line this row is, for the ink's gradient: 0 at the cap height, 1 on the
    // baseline.
    const double in_line = std::fmod(std::max(0.0, y + 0.5 - place.y), line_h) / place.cell_h;
    const double grad =
        std::clamp((in_line - metrics.cap) / (metrics.baseline - metrics.cap), 0.0, 1.0);
    const Rgb ink_rgb = mix(lk.top, lk.bottom, grad);
    for (int x = 0; x < w; ++x) {
      const size_t i = static_cast<size_t>(y) * w + x;
      uint8_t* px = rgba + i * 4;
      Rgb c{px[0] / 255.0, px[1] / 255.0, px[2] / 255.0};
      double touched = 0;
      if (!shadow.empty() && shadow[i] > 0) {
        c = mix(c, lk.shadow_rgb, shadow[i] * lk.shadow_alpha);
      }
      if (lk.glow > 0) {
        const double t = std::clamp(1 - std::max(sd[i], 0.0) / (lk.glow * u), 0.0, 1.0);
        const double g = lk.glow_strength * t * t;
        c = {std::min(1.0, c.r + lk.glow_rgb.r * g), std::min(1.0, c.g + lk.glow_rgb.g * g),
             std::min(1.0, c.b + lk.glow_rgb.b * g)};
        touched = std::max(touched, g);
      }
      if (lk.outline > 0) {
        const double a = std::clamp(lk.outline * u - sd[i] + 0.5, 0.0, 1.0);
        c = mix(c, lk.outline_rgb, a);
        touched = std::max(touched, a);
      }
      const double cov = ink.cov[i];
      if (cov > 0) {
        Rgb k = ink_rgb;
        if (lk.fill.on) {
          const double f =
              1 - lk.fill.strength + lk.fill.strength * texel(layers, lk.fill, x, y) * 2;
          k = {k.r * f, k.g * f, k.b * f};
        }
        if (lk.bevel > 0) {
          // The ink as a height that rises over `bevel` from its edge, lit from the top left.
          auto height = [&](int xx, int yy) {
            xx = std::clamp(xx, 0, w - 1);
            yy = std::clamp(yy, 0, h - 1);
            return std::clamp(-sd[static_cast<size_t>(yy) * w + xx] / (lk.bevel * u), 0.0, 1.0);
          };
          double gx = (height(x + 1, y) - height(x - 1, y)) * lk.bevel * u / 2;
          double gy = (height(x, y + 1) - height(x, y - 1)) * lk.bevel * u / 2;
          if (lk.bevel_inset) {
            gx = -gx;
            gy = -gy;
          }
          // n = (-gx, -gy, 1) normalised against a light at (-0.55, -0.65, 0.52), y down.
          const double nl = (0.55 * gx + 0.65 * gy + 0.52) / std::sqrt(gx * gx + gy * gy + 1);
          const double shade = 1 + (nl / 0.52 - 1) * lk.bevel_strength * 0.5;
          const double spec = std::max(0.0, nl - 0.9) * 4 * lk.bevel_strength;
          k = {std::min(1.0, std::max(0.0, k.r * shade + spec)),
               std::min(1.0, std::max(0.0, k.g * shade + spec)),
               std::min(1.0, std::max(0.0, k.b * shade + spec))};
        }
        c = mix(c, k, cov);
        touched = std::max(touched, cov);
      }
      if (lk.overlay.on && touched > 0) {
        const double f =
            1 - lk.overlay.strength + lk.overlay.strength * texel(layers, lk.overlay, x, y);
        c = mix(c, {c.r * f, c.g * f, c.b * f}, touched);
      }
      px[0] = static_cast<uint8_t>(core::num::round(std::clamp(c.r, 0.0, 1.0) * 255));
      px[1] = static_cast<uint8_t>(core::num::round(std::clamp(c.g, 0.0, 1.0) * 255));
      px[2] = static_cast<uint8_t>(core::num::round(std::clamp(c.b, 0.0, 1.0) * 255));
    }
  }
}

void paint_text(uint8_t* rgba, int width, int height, const Atlas& atlas, std::string_view text,
                const TextPlace& place, const Look& lk, LayerCache& layers) {
  double tw, th;
  text_size(text, place, tw, th);
  // Room for what reaches past the ink.
  const double reach = (lk.outline + lk.glow + lk.shadow_blur + 0.02) * place.cell_h +
                       std::max(std::fabs(lk.shadow_dx), std::fabs(lk.shadow_dy)) * place.cell_h;
  const int x0 = std::max(0, static_cast<int>(std::floor(place.x - reach)));
  const int y0 = std::max(0, static_cast<int>(std::floor(place.y - reach)));
  const int x1 = std::min(width, static_cast<int>(std::ceil(place.x + tw + reach)));
  const int y1 = std::min(height, static_cast<int>(std::ceil(place.y + th + reach)));
  if (x1 <= x0 || y1 <= y0) return;
  const int w = x1 - x0, h = y1 - y0;
  TextPlace local = place;
  local.x -= x0;
  local.y -= y0;
  Canvas c(w, h);
  draw_text(c, atlas, text, local);
  std::vector<uint8_t> region(static_cast<size_t>(w) * h * 4);
  for (int y = 0; y < h; ++y) {
    std::copy_n(rgba + (static_cast<size_t>(y0 + y) * width + x0) * 4, static_cast<size_t>(w) * 4,
                region.begin() + static_cast<ptrdiff_t>(y) * w * 4);
  }
  compose(c, local, font_info(atlas.font).metrics, lk, layers, region.data());
  for (int y = 0; y < h; ++y) {
    std::copy_n(region.begin() + static_cast<ptrdiff_t>(y) * w * 4, static_cast<size_t>(w) * 4,
                rgba + (static_cast<size_t>(y0 + y) * width + x0) * 4);
  }
}

}  // namespace ge::charset
