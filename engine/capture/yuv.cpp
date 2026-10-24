// RGBA to I420 (yuv.hpp).
//
// ge:summary yuv's definitions: the BT.709 integer matrix, the planes
// ge:keywords yuv i420 bt709 matrix luma chroma
// ge:skill replay
// ge:test tests/capture_yuv_test.cpp
#include "capture/yuv.hpp"

namespace ge::capture {
namespace {

// BT.709 at limited range, the coefficients scaled by 256 (each row sums to its range: 219 for
// luma, 0 for chroma), rounded by adding 128 before the shift.
inline uint8_t luma(int r, int g, int b) {
  return static_cast<uint8_t>(((47 * r + 157 * g + 16 * b + 128) >> 8) + 16);
}
inline uint8_t chroma_u(int r, int g, int b) {
  return static_cast<uint8_t>(((-26 * r - 86 * g + 112 * b + 128) >> 8) + 128);
}
inline uint8_t chroma_v(int r, int g, int b) {
  return static_cast<uint8_t>(((112 * r - 102 * g - 10 * b + 128) >> 8) + 128);
}

inline uint8_t clamp255(int v) { return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); }

}  // namespace

void to_i420(const lab::Picture& p, std::vector<uint8_t>& out) {
  const int w = p.width, h = p.height;
  out.resize(i420_bytes(w, h));
  uint8_t* y = out.data();
  uint8_t* u = y + static_cast<size_t>(w) * static_cast<size_t>(h);
  uint8_t* v = u + static_cast<size_t>(w / 2) * static_cast<size_t>(h / 2);
  const uint8_t* rgba = p.rgba.data();
  const auto at = [&](int x, int yy) { return rgba + 4 * (static_cast<size_t>(yy) * w + x); };
  for (int yy = 0; yy < h; yy++) {
    for (int x = 0; x < w; x++) {
      const uint8_t* c = at(x, yy);
      y[static_cast<size_t>(yy) * w + x] = luma(c[0], c[1], c[2]);
    }
  }
  for (int yy = 0; yy < h / 2; yy++) {
    for (int x = 0; x < w / 2; x++) {
      int r = 0, g = 0, b = 0;
      for (int dy = 0; dy < 2; dy++) {
        for (int dx = 0; dx < 2; dx++) {
          const uint8_t* c = at(2 * x + dx, 2 * yy + dy);
          r += c[0];
          g += c[1];
          b += c[2];
        }
      }
      const size_t i = static_cast<size_t>(yy) * (w / 2) + x;
      u[i] = chroma_u((r + 2) >> 2, (g + 2) >> 2, (b + 2) >> 2);
      v[i] = chroma_v((r + 2) >> 2, (g + 2) >> 2, (b + 2) >> 2);
    }
  }
}

lab::Picture from_i420(const uint8_t* i420, int width, int height) {
  lab::Picture p = lab::create_picture(width, height, {0, 0, 0, 255});
  const uint8_t* y = i420;
  const uint8_t* u = y + static_cast<size_t>(width) * static_cast<size_t>(height);
  const uint8_t* v = u + static_cast<size_t>(width / 2) * static_cast<size_t>(height / 2);
  for (int yy = 0; yy < height; yy++) {
    for (int x = 0; x < width; x++) {
      // BT.709's inverse at limited range, scaled by 256.
      const int c = 298 * (y[static_cast<size_t>(yy) * width + x] - 16);
      const size_t ci = static_cast<size_t>(yy / 2) * (width / 2) + static_cast<size_t>(x / 2);
      const int d = u[ci] - 128, e = v[ci] - 128;
      uint8_t* o = &p.rgba[4 * (static_cast<size_t>(yy) * width + x)];
      o[0] = clamp255((c + 459 * e + 128) >> 8);
      o[1] = clamp255((c - 55 * d - 136 * e + 128) >> 8);
      o[2] = clamp255((c + 541 * d + 128) >> 8);
    }
  }
  return p;
}

}  // namespace ge::capture
