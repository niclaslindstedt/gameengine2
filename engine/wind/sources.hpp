// LOCAL WIND SOURCES: air a game's own state makes, added to a field's wind as vectors
// (field.hpp's `wind_at`). Each is a closed form of where it is and how strong, so the air stays a
// pure function of the game's state and the clock.
//
// - A JET (`SourceKind::jet`): a round free jet out of a nozzle of radius `radius` at `strength`
//   m/s along `axis`, its centreline decaying as 6.2 diameters over the distance once past them
//   and spreading as a Gaussian whose half-width grows by a tenth of the distance. A rotor's
//   downwash is a jet pointed down; a vehicle's slipstream, one pointed back.
// - A WHIRL (`SourceKind::whirl`): a Rankine vortex about the vertical through its point: solid
//   rotation inside `radius`, falling as 1 / r outside, `strength` the speed at its rim (positive
//   turns clockwise seen from above), with an updraft of half that in its core fading over ten
//   radii of height. A dust devil, a snow devil, a fire's whirl.
// - A BLAST (`SourceKind::blast`): air thrown outward from a point at `t0`, its front running out
//   at the speed of sound; behind the front the outward speed is `strength` at `radius`, falling as
//   the inverse square of the distance and dying over `kBlastLife`.
//
// ge:summary Local wind sources a game's state makes: a jet, a whirl, a blast, as closed forms
// ge:keywords wind source jet downwash slipstream whirl vortex rankine blast shock source_at
// ge:skill water
// ge:test tests/wind_sources_test.cpp
#pragma once

#include <cstdint>

#include "wind/field.hpp"

namespace ge::wind {

inline constexpr double kSound = 340;      // m/s, a blast's front
inline constexpr double kBlastLife = 0.5;  // s, a blast's wind dies as exp(-age / this)

enum class SourceKind : uint8_t { jet, whirl, blast };

struct Source {
  SourceKind kind = SourceKind::jet;
  double x = 0, y = 0, z = 0;      // the nozzle, the whirl's foot, the blast's centre
  double ax = 0, ay = -1, az = 0;  // a jet's axis (unit)
  double radius = 1;               // m: the nozzle, the whirl's core, the blast's reference
  double strength = 0;             // m/s (see the header)
  double t0 = 0;                   // s, a blast's moment
};

// The air one source moves at (x, y, z) at `t`, m/s.
Air source_at(const Source& source, double x, double y, double z, double t);

}  // namespace ge::wind
