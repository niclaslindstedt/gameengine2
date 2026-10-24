// THE AIR OVER THE SNOW AT RUN TIME: the wind and the falling snow at a moment, the mean R19 dealt
// (mapgen/weather.hpp: `Level::weather`) breathing in gusts and squalls. Both are PURE FUNCTIONS
// of (level, t): the gusts and the squalls are slow sines whose phases are hashed off the map's
// seed, so the same map at the same second has the same air on every machine and in every replay,
// and nothing here draws from a run's generator.
//
// - THE WIND (`wind_at`, the engine's `wind::Field` the map deals, `field_of`): the dealt 10 m mean
// times one plus three gusts (6.3, 17.9 and 53 s
//   periods, 0.14, 0.2 and 0.16 of the mean), veering up to 0.22 rad either way on a 41 s swing.
//   IT NEVER BLOWS UP THE MOUNTAIN: a bearing that would carry the air against +z (the fall line)
//   is mirrored across the slope (`downhill_from`), so an even deal round the compass becomes an
//   even deal over the half that blows across or down it. The fold is taken here, at run time, so
//   no map a seed builds moves.
// - THE FALL (`fall_at`): 0 (nothing) .. 1 (a blizzard at its worst), the dealt `snowfall` times
//   one plus three squalls (37, 113 and 347 s; 0.35, 0.4 and 0.25 of it) breathing by the sky's
//   kind (a flurry comes and goes to nothing, a steady fall barely wavers, a storm surges), and a
//   storm's gusts lifting snow off the ground into the air. Out of a sky that does not snow, 0.
//   With it, how far one can see through it (`visibility_in`: 45 m at a fall of 1, opening as
//   the inverse square of the fall, 20 km at none).
//
// Readers: the frame's falling snow and the particles' drift (games/fall_line/app/frame.hpp). The
// skier's drag against it, the sound's wind and the new snow a fall lays are later readers of the
// same two functions.
//
// ge:summary The wind and the falling snow at a moment: gusts, veer, squalls, visibility
// ge:keywords air wind gust veer downhill_from wind_at field_of fall_at snowfall squall visibility
// ge:keywords visibility_in weather blizzard flurries storm
// ge:skill mountain
// ge:test tests/mountain_air_test.cpp
#pragma once

#include "mountain/mapgen/types.hpp"
#include "wind/field.hpp"

namespace ge::mountain::air {

// The air at 10 m over open ground at a moment.
struct Wind {
  double x = 0, z = 0;  // the air's velocity, m/s, world frame (heading 0 along +z)
  double speed = 0;     // m/s
  double gust = 0.5;    // the gust over the mean: 0 a lull .. 1 the strongest the map gets
};

// The snow in the air at a moment.
struct Fall {
  double fall = 0;            // 0 (nothing) .. 1 (a blizzard at its worst)
  double visibility = 20000;  // how far one sees through it, m
};

// The bearing a wind FROM `from` really blows from: mirrored across the slope when it would blow
// up it, unchanged otherwise. Continuous through the fold.
double downhill_from(double from);

// The map's wind as the engine's field (engine/wind): the dealt mean and bearing, the three gusts
// and the veer with their phases hashed off the map's seed, the downhill fold, a snowfield's
// roughness. `wind_at` is the field's open air at 10 m, bit for bit what it was before the field.
wind::Field field_of(const mapgen::Level& level);

Wind wind_at(const mapgen::Level& level, double t);
Fall fall_at(const mapgen::Level& level, double t);

// How far one sees through a fall of `fall`, m.
double visibility_in(double fall);

}  // namespace ge::mountain::air
