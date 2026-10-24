// The textured software view of a classic map (textured_view.hpp).
//
// ge:summary The textured view's raster: near-plane cut, perspective-correct fill, the light rule
// ge:keywords lab bsp textured raster near-plane perspective-correct lightmap bilinear sky layers
// ge:skill brush labs
// ge:test tests/brush_read_test.cpp
#include "textured_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/libm/libm.hpp"

namespace ge::brush::labs {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kNear = 1.0;  // map units
constexpr int kSuper = 2;      // drawn at twice the size, then averaged down
// A display's gamma lift: the family's pictures were painted for screens of the time, darker
// than today's, and its own drawing brightened them the same way.
constexpr double kGamma = 0.7;

struct Cam {
  double eye[3], f[3], r[3], u[3], focal, cx, cy;
};

// A corner in the camera's space with what is interpolated across the triangle.
struct Corner {
  double c[3];  // right, up, forward
  double st[2], lm[2], world[3];
};

struct Target {
  int w, h;
  std::vector<float> rgb, depth;  // depth: 1 / forward distance, 0 = nothing drawn
};

double luxel(const Map& map, int32_t l, double u, double v) {
  if (l < 0) return 128;
  const Lightmap& lm = map.lightmaps[size_t(l)];
  const double x = std::clamp(u * lm.width - 0.5, 0.0, double(lm.width) - 1);
  const double y = std::clamp(v * lm.height - 0.5, 0.0, double(lm.height) - 1);
  const auto x0 = uint32_t(x), y0 = uint32_t(y);
  const uint32_t x1 = std::min(x0 + 1, lm.width - 1), y1 = std::min(y0 + 1, lm.height - 1);
  const double fx = x - x0, fy = y - y0;
  const auto g = [&](uint32_t a, uint32_t b) {
    const size_t at = lm.offset + (size_t(b) * lm.width + a) * 3;
    return (map.light_rgb[at] + map.light_rgb[at + 1] + map.light_rgb[at + 2]) / 3.0;
  };
  return (g(x0, y0) * (1 - fx) + g(x1, y0) * fx) * (1 - fy) +
         (g(x0, y1) * (1 - fx) + g(x1, y1) * fx) * fy;
}

uint8_t texel(const Texture& t, double s, double v) {
  const auto w = int64_t(t.width), h = int64_t(t.height);
  int64_t x = int64_t(std::floor(s)) % w, y = int64_t(std::floor(v)) % h;
  if (x < 0) x += w;
  if (y < 0) y += h;
  return t.indexed[size_t(y * w + x)];
}

// The sky's two layers along the direction from the eye: the back layer the picture's right
// half, the front its left half where a colour is not 0.
uint8_t sky(const Texture& t, const Cam& cam, const double* world) {
  double d[3] = {world[0] - cam.eye[0], world[1] - cam.eye[1], (world[2] - cam.eye[2]) * 3};
  const double len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  if (len <= 0) return 0;
  const double half = double(t.width) / 2, s = 6 * 63 * d[0] / len, v = 6 * 63 * d[1] / len;
  const auto at = [&](double ss, double vv, double offset) {
    auto x = int64_t(std::floor(ss)) % int64_t(half), y = int64_t(std::floor(vv)) % t.height;
    if (x < 0) x += int64_t(half);
    if (y < 0) y += t.height;
    return t.indexed[size_t(y * t.width + x + int64_t(offset))];
  };
  const uint8_t front = at(s * 1.2, v * 1.2, 0);
  return front != 0 ? front : at(s, v, half);
}

void fill(Target& tg, const Cam& cam, const Map& map, const Surface& surf,
          std::span<const uint8_t> pal, const Corner* k) {
  const Texture& tex = map.textures[surf.texture];
  const bool pictured = !tex.indexed.empty() && tex.width > 0 && tex.height > 0;
  double sx[3], sy[3], iz[3];
  for (int i = 0; i < 3; ++i) {
    iz[i] = 1 / k[i].c[2];
    sx[i] = cam.cx + k[i].c[0] * iz[i] * cam.focal;
    sy[i] = cam.cy - k[i].c[1] * iz[i] * cam.focal;
  }
  const double area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
  if (std::fabs(area) < 1e-12) return;
  const int x0 = std::max(0, int(std::floor(std::min({sx[0], sx[1], sx[2]}))));
  const int x1 = std::min(tg.w - 1, int(std::ceil(std::max({sx[0], sx[1], sx[2]}))));
  const int y0 = std::max(0, int(std::floor(std::min({sy[0], sy[1], sy[2]}))));
  const int y1 = std::min(tg.h - 1, int(std::ceil(std::max({sy[0], sy[1], sy[2]}))));
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const double px = x + 0.5, py = y + 0.5;
      double b[3];
      b[0] = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area;
      b[1] = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area;
      b[2] = 1 - b[0] - b[1];
      if (b[0] < 0 || b[1] < 0 || b[2] < 0) continue;
      const double w = b[0] * iz[0] + b[1] * iz[1] + b[2] * iz[2];
      const size_t at = size_t(y) * size_t(tg.w) + size_t(x);
      if (w <= tg.depth[at]) continue;
      double q[3];
      for (int i = 0; i < 3; ++i) q[i] = b[i] * iz[i] / w;
      const auto mix = [&](auto field, int n) {
        return q[0] * field(k[0])[n] + q[1] * field(k[1])[n] + q[2] * field(k[2])[n];
      };
      uint8_t index = 0;
      double light = 1;
      if (!pictured) {
        index = 8;  // a mid grey: a texture the map does not carry
      } else if (surf.kind == SurfaceKind::sky) {
        const double world[3] = {mix([](const Corner& c) { return c.world; }, 0),
                                 mix([](const Corner& c) { return c.world; }, 1),
                                 mix([](const Corner& c) { return c.world; }, 2)};
        index = sky(tex, cam, world);
      } else {
        index = texel(tex, mix([](const Corner& c) { return c.st; }, 0),
                      mix([](const Corner& c) { return c.st; }, 1));
        if (surf.kind != SurfaceKind::liquid && index < 224) {
          light = luxel(map, surf.lightmap, mix([](const Corner& c) { return c.lm; }, 0),
                        mix([](const Corner& c) { return c.lm; }, 1)) /
                  128.0;
        }
      }
      tg.depth[at] = float(w);
      for (int c = 0; c < 3; ++c) {
        tg.rgb[at * 3 + size_t(c)] =
            float(std::min(255.0, pal[size_t(index) * 3 + size_t(c)] * std::min(light, 2.0)));
      }
    }
  }
}

}  // namespace

size_t draw_textured(lab::Picture& p, int x0, int y0, int w, int h, const Map& map,
                     std::span<const uint8_t> palette, const TexturedEye& eye) {
  Cam cam{};
  const double yaw = eye.yaw_deg * kPi / 180, pitch = eye.pitch_deg * kPi / 180;
  for (int k = 0; k < 3; ++k) cam.eye[k] = eye.at[k];
  cam.f[0] = core::libm::cos(yaw) * core::libm::cos(pitch),
  cam.f[1] = core::libm::sin(yaw) * core::libm::cos(pitch);
  cam.f[2] = core::libm::sin(pitch);
  cam.r[0] = core::libm::sin(yaw), cam.r[1] = -core::libm::cos(yaw), cam.r[2] = 0;
  cam.u[0] = cam.r[1] * cam.f[2] - cam.r[2] * cam.f[1];
  cam.u[1] = cam.r[2] * cam.f[0] - cam.r[0] * cam.f[2];
  cam.u[2] = cam.r[0] * cam.f[1] - cam.r[1] * cam.f[0];
  Target tg{w * kSuper, h * kSuper, {}, {}};
  tg.rgb.assign(size_t(tg.w) * size_t(tg.h) * 3, 0);
  tg.depth.assign(size_t(tg.w) * size_t(tg.h), 0);
  cam.cx = tg.w / 2.0, cam.cy = tg.h / 2.0;
  cam.focal = (tg.w / 2.0) / core::libm::tan(eye.fov_deg * kPi / 360);

  // Brush models a trigger owns are volumes, never drawn.
  std::vector<uint8_t> hidden(map.surfaces.size(), 0);
  for (const Entity& e : map.entities) {
    const std::string& model = e.get("model");
    if (e.get("classname").rfind("trigger_", 0) != 0 || model.size() < 2 || model[0] != '*') {
      continue;
    }
    const size_t mi = size_t(std::atoi(model.c_str() + 1));
    if (mi >= map.models.size()) continue;
    const Model& md = map.models[mi];
    for (uint32_t s = md.first_surface;
         s < md.first_surface + md.surface_count && s < map.surfaces.size(); ++s) {
      hidden[s] = 1;
    }
  }

  size_t drawn = 0;
  for (size_t si = 0; si < map.surfaces.size(); ++si) {
    if (hidden[si]) continue;
    const Surface& surf = map.surfaces[si];
    for (uint32_t t = surf.first; t + 2 < surf.first + surf.count; t += 3) {
      Corner in[3];
      int behind = 0;
      for (int i = 0; i < 3; ++i) {
        const Vertex& v = map.vertices[map.indices[t + uint32_t(i)]];
        double d[3];
        for (int k = 0; k < 3; ++k) d[k] = v.pos[k] - cam.eye[k], in[i].world[k] = v.pos[k];
        in[i].c[0] = d[0] * cam.r[0] + d[1] * cam.r[1] + d[2] * cam.r[2];
        in[i].c[1] = d[0] * cam.u[0] + d[1] * cam.u[1] + d[2] * cam.u[2];
        in[i].c[2] = d[0] * cam.f[0] + d[1] * cam.f[1] + d[2] * cam.f[2];
        for (int k = 0; k < 2; ++k) in[i].st[k] = v.st[k], in[i].lm[k] = v.lm[k];
        behind += in[i].c[2] < kNear;
      }
      if (behind == 3) continue;
      // Cut at the near plane: the polygon of what is in front, fanned.
      Corner poly[4];
      int n = 0;
      for (int i = 0; i < 3; ++i) {
        const Corner& a = in[i];
        const Corner& b = in[(i + 1) % 3];
        const bool ain = a.c[2] >= kNear, bin = b.c[2] >= kNear;
        if (ain) poly[n++] = a;
        if (ain != bin) {
          const double s = (kNear - a.c[2]) / (b.c[2] - a.c[2]);
          Corner m;
          for (int k = 0; k < 3; ++k) {
            m.c[k] = a.c[k] + (b.c[k] - a.c[k]) * s;
            m.world[k] = a.world[k] + (b.world[k] - a.world[k]) * s;
          }
          for (int k = 0; k < 2; ++k) {
            m.st[k] = a.st[k] + (b.st[k] - a.st[k]) * s;
            m.lm[k] = a.lm[k] + (b.lm[k] - a.lm[k]) * s;
          }
          poly[n++] = m;
        }
      }
      for (int i = 1; i + 1 < n; ++i) {
        const Corner tri[3] = {poly[0], poly[i], poly[i + 1]};
        fill(tg, cam, map, surf, palette, tri);
      }
      ++drawn;
    }
  }

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      double c[3] = {0, 0, 0};
      for (int j = 0; j < kSuper; ++j) {
        for (int i = 0; i < kSuper; ++i) {
          const size_t at = size_t(y * kSuper + j) * size_t(tg.w) + size_t(x * kSuper + i);
          for (int k = 0; k < 3; ++k) c[k] += tg.rgb[at * 3 + size_t(k)];
        }
      }
      const double n = kSuper * kSuper;
      uint8_t out[3];
      for (int k = 0; k < 3; ++k) {
        out[k] = uint8_t(std::lround(255 * core::libm::pow(std::min(1.0, c[k] / n / 255), kGamma)));
      }
      lab::set_pixel(p, x0 + x, y0 + y, lab::Rgba{out[0], out[1], out[2]});
    }
  }
  return drawn;
}

}  // namespace ge::brush::labs
