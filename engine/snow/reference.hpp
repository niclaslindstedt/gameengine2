// THE REFERENCE CONTACTS: a typical imprint of each thing that crosses snow, with the numbers it
// is pressed with, so a lab, a test and a game starting out press the same ski, boot, tyre, tread
// and body. Each is a contact at the origin heading along +z, stood anywhere by `place`, and the
// distance it moves between presses (`step`). The numbers:
//
// - SKI, CARVING: a ski 7 cm wide and 1.6 m long under half an 80 kg skier, sliding along its
//   length and cutting (a twentieth of what it cuts thrown aside).
// - SKI, SKIDDING: the pair slid sideways, scraping a band the skis' length wide; half of what it
//   cuts is thrown aside.
// - BOOT: a step of an 80 kg walker on a 30 x 11 cm sole, its peak load at heel strike.
// - TYRE: a car's tyre 20 cm wide under 300 kg, rolling; a tenth thrown.
// - TREAD: a snowmobile's belt, 38 cm wide and 2 m on the snow, under 250 kg; a sixth thrown.
// - BODY: a skier fallen and sliding on his side, 1.8 x 0.6 m; a third thrown.
//
// ge:summary Reference imprints: a carving and a skidding ski, a boot, a tyre, a tread, a body
// ge:keywords snow reference contacts ski carve skid boot footprint tyre tire tread body imprint
// ge:skill snow
// ge:test tests/snow_press_test.cpp
#pragma once

#include <string_view>

#include "core/libm/libm.hpp"
#include "snow/press.hpp"

namespace ge::snow {

struct ReferenceContact {
  std::string_view name;
  Imprint imprint;  // at the origin, heading +z
  double step = 0;  // m it moves between presses (0: it stands, as a boot does)
};

inline constexpr int kReferenceCount = 6;

inline ReferenceContact reference_contact(int i) {
  switch (i) {
    case 0:
      return {"ski-carve",
              Imprint{capsule(0, -0.8, 0, 0.8, 0.035), 392, 0.112, -1, 0.05, 6, 0.9, Motion::slide},
              0.25};
    case 1:
      return {"ski-skid",
              Imprint{capsule(-0.8, 0, 0.8, 0, 0.12), 785, 0.38, -1, 0.5, 4, 0.9, Motion::slide},
              0.2};
    case 2:
      return {"boot", Imprint{ellipse(0, 0, 0, 0.15, 0.055), 785, 0, -1, 0, 8, 0.9, Motion::step},
              0.75};
    case 3:
      return {"tyre", Imprint{box(0, 0, 0, 0.12, 0.1), 2943, 0, -1, 0.1, 8, 0.9, Motion::roll},
              0.2};
    case 4:
      return {"tread", Imprint{box(0, 0, 0, 1.0, 0.19), 2453, 0, -1, 0.15, 8, 0.9, Motion::roll},
              0.4};
    default:
      return {"body", Imprint{ellipse(0, 0, 0, 0.9, 0.3), 785, 0, -1, 0.33, 2, 0.9, Motion::slide},
              0.3};
  }
}

// `imp` stood at (x, z) heading `heading`: a capsule's ends (given in the contact's own frame, x
// across and z along) turned onto the heading, any other outline centred there.
inline Imprint place(const Imprint& imp, double x, double z, double heading) {
  Imprint out = imp;
  Shape& s = out.shape;
  const double sh = core::libm::sin(heading), ch = core::libm::cos(heading);
  if (s.kind == ShapeKind::capsule) {
    // The capsule's ends are in the contact's own frame (x across, z along); turn them.
    const double ax = s.ax * ch + s.az * sh, az = -s.ax * sh + s.az * ch;
    const double bx = s.bx * ch + s.bz * sh, bz = -s.bx * sh + s.bz * ch;
    s.ax = x + ax;
    s.az = z + az;
    s.bx = x + bx;
    s.bz = z + bz;
  } else {
    s.ax = s.bx = x;
    s.az = s.bz = z;
    s.heading = heading;
  }
  return out;
}

}  // namespace ge::snow
