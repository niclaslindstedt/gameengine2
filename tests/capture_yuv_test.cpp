// RGBA to I420 (engine/capture/yuv.hpp): BT.709 at limited range (white 235, black 16, the
// primaries where the standard puts them), chroma the mean of each 2x2 block, the planes in
// order and at their sizes; and back again within a step or two.
//
// ge:summary Tests: I420 at BT.709 limited range, the planes, chroma averaged per block
// ge:keywords test yuv i420 bt709 limited-range chroma planes
// ge:skill replay testing
// ge:test tests/capture_yuv_test.cpp
#include <cstdint>
#include <vector>

#include "capture/yuv.hpp"
#include "check.hpp"

namespace gc = ge::capture;
namespace gl = ge::lab;

namespace {

// Y, U and V of a 2x2 picture of one colour.
std::vector<uint8_t> yuv_of(uint8_t r, uint8_t g, uint8_t b) {
  std::vector<uint8_t> out;
  gc::to_i420(gl::create_picture(2, 2, {r, g, b, 255}), out);
  return {out[0], out[4], out[5]};
}

}  // namespace

TEST(white_black_and_the_primaries_land_where_bt709_puts_them) {
  CHECK((yuv_of(255, 255, 255) == std::vector<uint8_t>{235, 128, 128}));
  CHECK((yuv_of(0, 0, 0) == std::vector<uint8_t>{16, 128, 128}));
  CHECK((yuv_of(255, 0, 0) == std::vector<uint8_t>{63, 102, 240}));
  CHECK((yuv_of(0, 255, 0) == std::vector<uint8_t>{172, 42, 26}));  // the 8-bit matrix: 173 exact
  CHECK((yuv_of(0, 0, 255) == std::vector<uint8_t>{32, 240, 118}));
}

TEST(the_planes_are_full_then_quarter_size_and_chroma_is_each_blocks_mean) {
  gl::Picture p = gl::create_picture(4, 2, {0, 0, 0, 255});
  // The left block half white, so its chroma stays grey and its luma is per pixel.
  for (int y = 0; y < 2; y++) {
    uint8_t* c = &p.rgba[4 * (static_cast<size_t>(y) * 4)];
    c[0] = c[1] = c[2] = 255;
  }
  std::vector<uint8_t> out;
  gc::to_i420(p, out);
  CHECK(out.size() == gc::i420_bytes(4, 2));
  CHECK(out.size() == 12);
  CHECK(out[0] == 235 && out[1] == 16 && out[4] == 235 && out[5] == 16);
  CHECK(out[8] == 128 && out[9] == 128);    // U of the two blocks
  CHECK(out[10] == 128 && out[11] == 128);  // V
}

TEST(back_from_i420_a_colour_comes_within_a_step_or_two_of_itself) {
  for (const ge::lab::Rgba c :
       {ge::lab::Rgba{255, 255, 255, 255}, ge::lab::Rgba{0, 0, 0, 255},
        ge::lab::Rgba{200, 40, 90, 255}, ge::lab::Rgba{30, 160, 220, 255}}) {
    std::vector<uint8_t> out;
    gc::to_i420(gl::create_picture(2, 2, c), out);
    const gl::Picture back = gc::from_i420(out.data(), 2, 2);
    for (int i = 0; i < 3; i++) {
      const int d =
          static_cast<int>(back.rgba[static_cast<size_t>(i)]) - static_cast<int>(i == 0   ? c.r
                                                                                 : i == 1 ? c.g
                                                                                          : c.b);
      CHECK(d >= -3 && d <= 3);
    }
  }
}
