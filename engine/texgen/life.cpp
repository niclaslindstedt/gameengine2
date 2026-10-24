// The life painters: what time and the place do to a surface. Each one reads the height the
// layers before it built (its relief: pits and joints against crests and edges; which way it
// faces), so grime pools in the joints the bricks cut, wear rubs the edges they raise, dust and
// snow lie on their ledges and the glow fills the cracks. Their amounts are what a level turns
// (`amount=$wear`), which is how a texture ages with its room.
//
// ge:summary Life painters: cracks, scratches, dirt, dust, grime, wear, rust, streaks, moss
// ge:keywords life aging weathering cracks scratches dirt dust grime wear edge-wear rust
// ge:keywords streaks stains moss snow glow emissive lava cavity
// ge:skill textures
// ge:test tests/texgen_bake_test.cpp
#include <algorithm>
#include <cmath>

#include "core/libm/libm.hpp"
#include "texgen/layers.hpp"
#include "texgen/noise.hpp"

namespace ge::texgen {
namespace {

int texels(const Surface& s, double share) {
  return std::max(1, static_cast<int>(std::floor(share * s.size + 0.5)));
}

// A mask's threshold from an amount: amount 0 covers nothing, 1 nearly everything.
double cover(double score, double amount, double soft = 0.15) {
  const double t = 1 - amount;
  return smoothstep(t - soft, t + soft, score);
}

}  // namespace

void paint_cracks(Surface& s, const Args& a) {
  NoiseSpec edges{NoiseKind::edges, a.i("period"), 1, 0.5, 1, a.n("warp"), a.seed};
  NoiseSpec patch{NoiseKind::perlin, 3, 3, 0.5, 1, 0, a.seed + 11};
  const double width = a.n("width"), amount = a.n("amount"), depth = a.n("depth");
  const Rgb colour = a.c("color");
  const double aa = edges.period / static_cast<double>(s.size);
  for_texels(s.size, [&](int x, int y, double u, double v) {
    const double e = sample(edges, u, v);
    // Wider where the patch is strongest; gone where it is under the amount.
    const double here = cover(sample(patch, u, v), amount, 0.12);
    const double line = smoothstep(width * here + aa, width * here * 0.3, e) * here;
    if (line <= 0) return;
    const size_t i = s.at(x, y);
    set_colour(s, i, mix(colour_at(s, i), colour, line * 0.9));
    s.plane[kHeight][i] = static_cast<float>(clamp01(s.plane[kHeight][i] - depth * line));
    pull(s, kRough, i, 1.0, line);
  });
}

void paint_scratches(Surface& s, const Args& a) {
  const int count = a.i("count"), size = s.size;
  const double length = a.n("length") * size, amount = a.n("amount");
  const double base = a.n("angle") * (3.141592653589793 / 180), spread = a.n("spread");
  std::vector<float> lit(static_cast<size_t>(size) * size, 0.0f);
  for (int k = 0; k < count; ++k) {
    const auto kk = static_cast<uint32_t>(k);
    const double x0 = hash01(kk, 1, a.seed) * size, y0 = hash01(kk, 2, a.seed) * size;
    const double ang = base + (2 * hash01(kk, 3, a.seed) - 1) * spread * (3.141592653589793 / 180);
    const double len = length * (0.25 + 0.75 * hash01(kk, 4, a.seed));
    const double bright = 0.35 + 0.65 * hash01(kk, 5, a.seed);
    const double dx = core::libm::cos(ang), dy = core::libm::sin(ang);
    for (int step = 0; step * 0.5 < len; ++step) {
      const double d = step * 0.5;
      // A scratch fades in and out along its length.
      const double fade = std::min(1.0, std::min(d, len - d) / (0.2 * len + 1e-9));
      const size_t at = s.wrapped(static_cast<int>(std::floor(x0 + dx * d)),
                                  static_cast<int>(std::floor(y0 + dy * d)));
      lit[at] = std::max(lit[at], static_cast<float>(bright * fade));
    }
  }
  const Rgb colour = a.c("color");
  for_texels(size, [&](int x, int y, double, double) {
    const size_t i = s.at(x, y);
    const double m = lit[i] * amount;
    if (m <= 0) return;
    set_colour(s, i, mix(colour_at(s, i), colour, m));
    s.plane[kHeight][i] = static_cast<float>(clamp01(s.plane[kHeight][i] - 0.04 * m));
    pull(s, kRough, i, 0.25, m * 0.8);
  });
}

void paint_dirt(Surface& s, const Args& a) {
  NoiseSpec blot{NoiseKind::perlin, a.i("period"), 5, 0.55, 1, 0.3, a.seed};
  const std::vector<float> rel = relief(s, texels(s, 0.02));
  const double amount = a.n("amount"), cav = a.n("cavity");
  const Rgb colour = a.c("color");
  for_texels(s.size, [&](int x, int y, double u, double v) {
    const size_t i = s.at(x, y);
    const double score = sample(blot, u, v) * (1 - cav * 0.5) + cav * clamp01(rel[i] * 4 + 0.25);
    const double m = cover(score, amount, 0.2) * (0.35 + 0.65 * amount);
    if (m <= 0) return;
    set_colour(s, i, mix(colour_at(s, i), colour, m * 0.85));
    pull(s, kRough, i, 1.0, m * 0.6);
    pull(s, kMetal, i, 0.0, m);
  });
}

void paint_dust(Surface& s, const Args& a) {
  NoiseSpec veil{NoiseKind::value, 6, 4, 0.5, 1, 0, a.seed};
  NoiseSpec speck{NoiseKind::white, s.size, 1, 0.5, 1, 0, a.seed + 1};
  const std::vector<float> up = up_facing(s);
  const double amount = a.n("amount"), ledges = a.n("ledges");
  const Rgb colour = a.c("color");
  for_texels(s.size, [&](int x, int y, double u, double v) {
    const size_t i = s.at(x, y);
    const double film = amount * (0.25 + 0.35 * sample(veil, u, v));
    const double ledge = ledges * amount * std::min(1.0, up[i] * 2.5);
    const double grain = sample(speck, u, v) > 1 - 0.08 * amount ? 0.5 * amount : 0;
    const double m = clamp01(film + ledge + grain);
    set_colour(s, i, mix(colour_at(s, i), colour, m));
    pull(s, kRough, i, 1.0, m * 0.7);
    pull(s, kMetal, i, 0.0, m);
  });
}

void paint_grime(Surface& s, const Args& a) {
  NoiseSpec breakup{NoiseKind::perlin, 6, 4, 0.5, 1, 0, a.seed};
  const std::vector<float> rel = relief(s, texels(s, a.n("reach")));
  const double amount = a.n("amount");
  const Rgb colour = a.c("color");
  for_texels(s.size, [&](int x, int y, double u, double v) {
    const size_t i = s.at(x, y);
    const double pit = smoothstep(0.004, 0.06, rel[i]);
    const double m = amount * pit * (0.55 + 0.45 * sample(breakup, u, v));
    if (m <= 0) return;
    set_colour(s, i, mix(colour_at(s, i), colour, m));
    pull(s, kRough, i, 1.0, m * 0.5);
  });
}

void paint_wear(Surface& s, const Args& a) {
  NoiseSpec breakup{NoiseKind::perlin, 10, 4, 0.55, 1, 0, a.seed};
  const std::vector<float> rel = relief(s, texels(s, a.n("reach")));
  const double amount = a.n("amount"), dr = a.n("rough"), metal = a.n("metal");
  const Rgb colour = a.c("color");
  for_texels(s.size, [&](int x, int y, double u, double v) {
    const size_t i = s.at(x, y);
    const double edge = clamp01(-rel[i] * 10);
    // Even fully worn, the faces keep most of their colour: wear is the edges' and the high spots'.
    const double m = cover(edge * 0.75 + 0.35 * sample(breakup, u, v), amount * 0.8, 0.08);
    if (m <= 0) return;
    set_colour(s, i, mix(colour_at(s, i), colour, m));
    s.plane[kRough][i] = static_cast<float>(clamp01(s.plane[kRough][i] + dr * m));
    if (metal > 0) pull(s, kMetal, i, metal, m);
  });
}

void paint_rust(Surface& s, const Args& a) {
  NoiseSpec patch{NoiseKind::perlin, a.i("period"), 5, 0.6, 1, 0.3, a.seed};
  NoiseSpec fine{NoiseKind::value, 64, 3, 0.5, 1, 0, a.seed + 2};
  const std::vector<float> rel = relief(s, texels(s, 0.01));
  const double amount = a.n("amount");
  const Rgb deep = a.c("color"), fresh = a.c("color2");
  for_texels(s.size, [&](int x, int y, double u, double v) {
    const size_t i = s.at(x, y);
    // Rust starts where water stands (pits) and where paint is thinnest (edges).
    const double site = 0.6 * sample(patch, u, v) + 0.4 * clamp01(std::fabs(rel[i]) * 6);
    const double m = cover(site, amount, 0.1);
    if (m <= 0) return;
    const double f = sample(fine, u, v);
    set_colour(s, i, mix(colour_at(s, i), mix(deep, fresh, f), m));
    pull(s, kRough, i, 0.95, m);
    pull(s, kMetal, i, 0.0, m);
    s.plane[kHeight][i] = static_cast<float>(clamp01(s.plane[kHeight][i] - 0.04 * m * f));
  });
}

void paint_streaks(Surface& s, const Args& a) {
  const int count = a.i("count");
  const double amount = a.n("amount"), length = a.n("length"), width = a.n("width");
  const Rgb colour = a.c("color");
  NoiseSpec wobble{NoiseKind::value, 16, 2, 0.5, 1, 0, a.seed + 3};
  // Each streak's place, size and strength, drawn once.
  struct Streak {
    double x, top, w, len, k;
  };
  std::vector<Streak> streaks(static_cast<size_t>(count));
  for (int k = 0; k < count; ++k) {
    const auto kk = static_cast<uint32_t>(k);
    streaks[k] = {hash01(kk, 2, a.seed), hash01(kk, 4, a.seed),
                  width * (0.3 + 0.7 * hash01(kk, 1, a.seed)),
                  length * (0.3 + 0.7 * hash01(kk, 3, a.seed)), 0.4 + 0.6 * hash01(kk, 5, a.seed)};
  }
  for_texels(s.size, [&](int x, int y, double u, double v) {
    const double wob = sample(wobble, u, v) - 0.5;
    double m = 0;
    for (const Streak& st : streaks) {
      double dx = std::fabs(u - st.x + st.w * wob);
      dx = std::min(dx, 1 - dx);
      if (dx > st.w) continue;
      double t = v - st.top;
      t -= std::floor(t);  // wrapped: how far below the streak's head this texel is
      if (t > st.len) continue;
      const double along = 1 - t / st.len;
      m = std::max(m, along * along * (1 - dx / st.w) * st.k);
    }
    if (m <= 0) return;
    const size_t i = s.at(x, y);
    set_colour(s, i, mix(colour_at(s, i), colour, m * amount));
    pull(s, kRough, i, 0.9, m * amount * 0.5);
  });
}

void paint_moss(Surface& s, const Args& a) {
  NoiseSpec patch{NoiseKind::perlin, a.i("period"), 5, 0.55, 1, 0.3, a.seed};
  NoiseSpec tuft{NoiseKind::value, 96, 2, 0.5, 1, 0, a.seed + 1};
  const std::vector<float> rel = relief(s, texels(s, 0.015));
  const double amount = a.n("amount");
  const Rgb shade = a.c("color"), lit = a.c("color2");
  for_texels(s.size, [&](int x, int y, double u, double v) {
    const size_t i = s.at(x, y);
    const double site = 0.55 * sample(patch, u, v) + 0.45 * clamp01(rel[i] * 6 + 0.3);
    const double m = cover(site, amount, 0.1);
    if (m <= 0) return;
    const double t = sample(tuft, u, v);
    set_colour(s, i, mix(colour_at(s, i), mix(shade, lit, t), m));
    pull(s, kRough, i, 0.95, m);
    pull(s, kMetal, i, 0.0, m);
    s.plane[kHeight][i] = static_cast<float>(clamp01(s.plane[kHeight][i] + 0.06 * m * t));
  });
}

void paint_snow(Surface& s, const Args& a) {
  NoiseSpec drift{NoiseKind::perlin, a.i("period"), 4, 0.5, 1, 0.2, a.seed};
  const std::vector<float> up = up_facing(s);
  const std::vector<float> rel = relief(s, texels(s, 0.012));
  const double amount = a.n("amount");
  const Rgb colour = a.c("color");
  for_texels(s.size, [&](int x, int y, double u, double v) {
    const size_t i = s.at(x, y);
    // Snow sits on what faces up and fills the joints; the drift breaks its edge.
    const double site =
        0.45 * std::min(1.0, up[i] * 2.0) + 0.3 * clamp01(rel[i] * 6) + 0.35 * sample(drift, u, v);
    const double m = cover(site, amount, 0.06);
    if (m <= 0) return;
    set_colour(s, i, mix(colour_at(s, i), colour, m));
    pull(s, kRough, i, 0.55, m);
    pull(s, kMetal, i, 0.0, m);
    s.plane[kHeight][i] = static_cast<float>(clamp01(s.plane[kHeight][i] + 0.08 * m));
  });
}

void paint_glow(Surface& s, const Args& a) {
  const std::string& pattern = a.w("pattern");
  const double intensity = a.n("intensity"), amount = a.n("amount"), width = a.n("width");
  const double threshold = a.n("threshold");
  const int period = a.i("period");
  const Rgb colour = a.c("color");
  NoiseSpec field{NoiseKind::perlin, period, 4, 0.5, 1, 0, a.seed};
  if (pattern == "cracks") field = {NoiseKind::edges, period, 1, 0.5, 1, a.n("warp"), a.seed};
  if (pattern == "spots") field = {NoiseKind::cells, period, 1, 0.5, 0.8, 0, a.seed};
  const double aa = period / static_cast<double>(s.size);
  for_texels(s.size, [&](int x, int y, double u, double v) {
    const size_t i = s.at(x, y);
    double m = 0;
    if (pattern == "cavity") {
      m = smoothstep(threshold, threshold - 0.12, s.plane[kHeight][i]);
    } else if (pattern == "lines") {
      const double fx = u * period - std::floor(u * period),
                   fy = v * period - std::floor(v * period);
      const double d = std::min(std::min(fx, 1 - fx), std::min(fy, 1 - fy));
      m = smoothstep(width * 0.5 + aa, width * 0.5 - aa, d);
    } else if (pattern == "noise") {
      m = smoothstep(1 - threshold, 1 - threshold + 0.1, sample(field, u, v));
    } else {
      // cracks and spots: under `width` of the cellular field.
      m = smoothstep(width + aa, std::max(0.0, width - aa), sample(field, u, v));
      if (pattern == "spots")
        m *= hash01(static_cast<uint32_t>(u * period), static_cast<uint32_t>(v * period), a.seed) <
                     amount
                 ? 1
                 : 0;
    }
    if (pattern != "spots") m *= amount;
    if (m <= 0) return;
    s.plane[kGlowR][i] = static_cast<float>(s.plane[kGlowR][i] + colour.r * intensity * m);
    s.plane[kGlowG][i] = static_cast<float>(s.plane[kGlowG][i] + colour.g * intensity * m);
    s.plane[kGlowB][i] = static_cast<float>(s.plane[kGlowB][i] + colour.b * intensity * m);
    // What glows is lit from within: its colour goes the light's way and it is smooth.
    set_colour(s, i, mix(colour_at(s, i), colour, m * 0.6));
    pull(s, kRough, i, 0.3, m * 0.5);
  });
}

}  // namespace ge::texgen
