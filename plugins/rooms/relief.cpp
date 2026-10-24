// The reliefs' height functions: value noise for the stone that runs on, cells and bevels for the
// patterns laid out on a surface, a jittered grid's nearest points for stones and dents.
//
// ge:summary The relief kinds' height functions and grid sizes
// ge:keywords relief height bevel voronoi jitter cells running-bond ridged noise pattern
// ge:skill rooms
// ge:test tests/rooms_build_test.cpp
#include "rooms/relief.hpp"

#include <algorithm>
#include <cmath>

#include "rooms/noise3.hpp"

namespace ge::rooms {

namespace {

constexpr std::string_view kNames[kReliefKinds] = {
    "none", "rock", "crag", "stones", "ashlar", "tiles", "planks", "corrugated", "dents"};

float smooth01(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

float hash01(int32_t a, int32_t b, uint32_t seed) {
  return float(hash3(a, b, 0x2545, seed) >> 8) * (1.0f / 16777216.0f);
}

// Blocks in rows: `w` x `h`, every other row shifted by `shift` of a block (planks shift by a
// hashed amount instead when `shift` < 0). The bevel is `bevel` m wide; each block stands proud by
// its own hashed amount within `vary`, and leans by its own within `lean`.
float blocks(float u, float v, float w, float h, float shift, float bevel, float vary, float lean,
             uint32_t seed) {
  const float row = std::floor(v / h);
  const auto r = int32_t(row);
  const float s = shift < 0 ? hash01(r, -7, seed) : (r & 1 ? shift : 0.0f);
  const float x = u / w + s;
  const float col = std::floor(x);
  const auto c = int32_t(col);
  const float du = x - col, dv = v / h - row;
  const float edge = std::min(std::min(du, 1.0f - du) * w, std::min(dv, 1.0f - dv) * h);
  const float tilt = (hash01(c, r, seed + 11) - 0.5f) * (du - 0.5f) +
                     (hash01(c, r, seed + 23) - 0.5f) * (dv - 0.5f);
  const float top = 1.0f - vary * hash01(c, r, seed) + lean * tilt;
  return std::clamp(smooth01(edge / bevel) * top, 0.0f, 1.0f);
}

// The nearest and the second-nearest of one jittered point a cell (cells `size` m), distances in
// cells, and the nearest's hash.
void nearest_points(float u, float v, float size, uint32_t seed, float* d1, float* d2, float* id) {
  const float gu = u / size, gv = v / size;
  const auto iu = int32_t(std::floor(gu)), iv = int32_t(std::floor(gv));
  float best = 1e9f, second = 1e9f, who = 0;
  for (int32_t b = iv - 1; b <= iv + 1; ++b) {
    for (int32_t a = iu - 1; a <= iu + 1; ++a) {
      const float px = float(a) + 0.15f + 0.7f * hash01(a, b, seed);
      const float py = float(b) + 0.15f + 0.7f * hash01(a, b, seed + 5);
      const float d = (px - gu) * (px - gu) + (py - gv) * (py - gv);
      if (d < best) {
        second = best;
        best = d;
        who = hash01(a, b, seed + 9);
      } else if (d < second) {
        second = d;
      }
    }
  }
  *d1 = std::sqrt(best);
  *d2 = std::sqrt(second);
  *id = who;
}

}  // namespace

std::string_view relief_name(Relief kind) {
  const auto i = size_t(kind);
  return i < size_t(kReliefKinds) ? kNames[i] : std::string_view("?");
}

bool relief_of(std::string_view name, Relief* out) {
  for (int i = 0; i < kReliefKinds; ++i) {
    if (kNames[i] == name) {
      *out = Relief(i);
      return true;
    }
  }
  return false;
}

float relief_cell(Relief kind, float scale) {
  // Grid steps across the largest feature, a relief's row: fine enough for its smallest one.
  constexpr float kPer[kReliefKinds] = {0, 10, 12, 8, 10, 10, 8, 6, 8};
  const auto i = size_t(kind);
  if (i == 0 || i >= size_t(kReliefKinds)) return 0.0f;
  return std::clamp(scale / kPer[i], kMinReliefCell, 0.5f);
}

float relief_cell(Relief kind, float scale, float area_m2) {
  const float cell = relief_cell(kind, scale);
  return cell > 0 ? std::max(cell, std::sqrt(area_m2 / kMaxReliefCells)) : 0.0f;
}

float relief_height(Relief kind, float u, float v, const float p[3], uint32_t seed, float scale) {
  switch (kind) {
    case Relief::none:
      return 0.0f;
    case Relief::rock:
      // fbm gathers round a half; stretched so the stone spans its depth.
      return std::clamp((fbm3(p[0] / scale, p[1] / scale, p[2] / scale, seed, 5) - 0.2f) / 0.6f,
                        0.0f, 1.0f);
    case Relief::crag: {
      // Ridged octaves: each octave's crease folded up into a crest, the finer ones riding on
      // the coarser where those stand high.
      float sum = 0, weight = 0.5f, total = 0, prev = 1;
      float x = p[0] / scale, y = p[1] / scale, z = p[2] / scale;
      for (int o = 0; o < 5; ++o) {
        float r =
            1.0f - std::fabs(2.0f * value_noise3(x, y, z, seed + uint32_t(o) * 0x632be5abu) - 1.0f);
        r *= r;
        sum += r * weight * prev;
        total += weight;
        prev = std::clamp(r * 1.5f, 0.0f, 1.0f);
        weight *= 0.5f;
        x *= 2.0f;
        y *= 2.0f;
        z *= 2.0f;
      }
      return std::clamp(sum / total * 1.6f, 0.0f, 1.0f);
    }
    case Relief::stones: {
      float d1, d2, id;
      nearest_points(u, v, scale, seed, &d1, &d2, &id);
      // Mortar where two stones are nearly as near; a stone domed up from its edge.
      const float edge = smooth01((d2 - d1) / 0.35f);
      const float dome = 1.0f - std::min(1.0f, d1 * d1);
      return std::clamp(edge * (0.55f + 0.3f * dome + 0.15f * id), 0.0f, 1.0f);
    }
    case Relief::ashlar:
      return blocks(u, v, 2.0f * scale, scale, 0.5f, 0.12f * scale, 0.35f, 0.15f, seed);
    case Relief::tiles:
      return blocks(u, v, scale, scale, 0.0f, 0.05f * scale, 0.1f, 0.6f, seed);
    case Relief::planks: {
      const float board = blocks(v, u, 6.0f * scale, scale, -1.0f, 0.04f * scale, 0.2f, 0.0f, seed);
      const float grain = value_noise3(u / (2.0f * scale), v / (0.08f * scale), 0.5f, seed + 3);
      return board * (0.9f + 0.1f * grain);
    }
    case Relief::corrugated: {
      const float t = u / scale - std::floor(u / scale);
      return 4.0f * t * (1.0f - t);
    }
    case Relief::dents: {
      float d1, d2, id;
      nearest_points(u, v, scale, seed, &d1, &d2, &id);
      const float r = 0.25f + 0.3f * id;  // a dent's radius, cells
      const float dent = 1.0f - smooth01(d1 / r);
      return 1.0f - 0.8f * dent * (0.4f + 0.6f * id);
    }
  }
  return 0.0f;
}

}  // namespace ge::rooms
