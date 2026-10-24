// engine/water/glow.hpp: a black body's glow brightens steeply with its temperature and turns
// from deep red toward yellow-white; it is dim at the eye's threshold of red and outshines no
// sunlit face at a lava's heat; the table runs between its ends.
//
// ge:summary Test: a hot surface's glow: luminance and colour by temperature, the table
// ge:keywords test glow blackbody planck luminance colour lava table
// ge:skill water testing
#include <cmath>
#include <vector>

#include "check.hpp"
#include "water/glow.hpp"

namespace {

namespace gw = ge::water;

}  // namespace

TEST(the_glow_brightens_steeply_and_yellows) {
  double last = 0, last_ratio = 0;
  for (int i = 0; i <= 8; ++i) {
    const double c = 500 + 100 * i;
    const gw::Glow g = gw::glow_of(c + 273.15);
    CHECK(g.luminance > last * 2);  // more than doubling every 100 degrees in this span
    last = g.luminance;
    const double ratio = g.colour.g / g.colour.r;
    CHECK(ratio >= last_ratio);  // greener against red as it heats: red, orange, yellow
    last_ratio = ratio;
    CHECK(g.colour.r > g.colour.g && g.colour.g >= g.colour.b);
  }
}

TEST(a_lava_glows_bright_but_not_like_the_sun) {
  // The dull red the dark-adapted eye sees from about 525 C: well under a candela per m^2.
  CHECK(gw::glow_of(525 + 273.15).luminance < 1.0);
  // A lava at 1150 C: thousands of cd/m^2, under a sunlit white face's tens of thousands.
  const double lava = gw::glow_of(1150 + 273.15, 0.9).luminance;
  CHECK(lava > 1e3 && lava < 2e4);
  // The colour's luminance is 1.
  const gw::Rgb c = gw::glow_of(1400).colour;
  CHECK(std::fabs(0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b - 1) < 1e-9);
}

TEST(the_table_runs_between_its_ends) {
  const std::vector<gw::Rgb> t = gw::glow_table(700, 1600, 10);
  CHECK(t.size() == 10);
  const gw::Glow hi = gw::glow_of(1600);
  CHECK(std::fabs(t.back().r - hi.colour.r * hi.luminance) < 1e-9 * hi.luminance);
  CHECK(t.front().r < t.back().r);
}
