// THE WIND AS A PURE FUNCTION OF PLACE AND TIME (docs/water.md § Wind). A `Field` is data built
// once (a mean, its gusts and veer, the ground's roughness, a world's rule on bearings); the air at
// any point and moment is computed from it and never stepped, so a replay, a ghost, a lockstep
// peer and a server read the same air with no state to send.
//
// - THE MEAN: a speed 10 m over open ground and the bearing it blows FROM (heading 0 = +z,
//   clockwise from above).
// - GUSTS AND VEER: up to `kMaxSwells` sines (a period, a share of the mean, a phase) summed onto
//   the speed, and one sine of veer (its share is the swing in radians either way). `lay_gusts`
//   spreads a default set over the turbulence's energetic band (5 to 60 s) for a measured
//   intensity: the speed's standard deviation over its mean.
// - THE FOLD: an optional rule a world puts on bearings (a mountain's: never up the slope),
//   applied to the veered bearing. The engine knows no slope; the world passes the function.
// - THE HEIGHT PROFILE: the log law, U(h) = U10 * ln(h / z0) / ln(10 / z0), over the ground's
//   roughness length z0 (`kRoughness`: open water, snow, grass, crops, forest, a town), zero at
//   and under z0.
// - SHELTER (`Shelter`): an optional baked grid over the plan, each cell what share of the wind is
//   kept and how far it is turned (a wood, a town, a ridge's lee), read bilinearly.
// - LOCAL SOURCES (`sources.hpp`): a rotor's downwash, a blast, a whirl, added as vectors.
//
// The open air at 10 m (`open_at`) is computed in exactly the order a world's own wind was before
// it read this module, so a world that moves onto it keeps its bits (the mountain's wind is held by
// tests/golden/mountain_air.txt).
//
// ge:summary The wind as a pure function: mean, gusts, veer, log-law profile, shelter, sources
// ge:keywords wind air gust veer turbulence intensity log-law roughness profile shelter fold
// ge:keywords open_at wind_at lay_gusts profile_at turbulence_for shelter_at phase_of field
// ge:skill water determinism
// ge:test tests/wind_field_test.cpp
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace ge::wind {

inline constexpr int kMaxSwells = 8;

// One sine: its period (s), its share (of the mean speed for a gust; radians either way for the
// veer) and its phase (rad).
struct Swell {
  double period = 0;
  double share = 0;
  double phase = 0;
};

// A world's rule on bearings: the bearing a wind FROM `from` really blows from.
using Fold = double (*)(double from);

// Roughness lengths z0, m, of the ground under the air (the log law's one parameter).
namespace kRoughness {
inline constexpr double water = 0.0002;  // open water at moderate winds
inline constexpr double snow = 0.003;    // a snowfield
inline constexpr double grass = 0.03;    // open grass, a piste, an airfield
inline constexpr double crops = 0.1;     // crops, scattered bushes
inline constexpr double forest = 0.8;    // forest, a village
inline constexpr double town = 1.5;      // a town's centre
}  // namespace kRoughness

struct Field {
  double mean = 0;  // m/s, 10 m over open ground
  double from = 0;  // the bearing it blows FROM, rad
  int gusts = 0;    // how many of `gust` are summed
  Swell gust[kMaxSwells]{};
  Swell veer{};  // share: the swing either way, rad; period 0: no veer
  Fold fold = nullptr;
  double roughness = kRoughness::grass;  // the ground's z0 under the air, m
};

// The air at a point and a moment.
struct Air {
  double x = 0, y = 0, z = 0;  // the air's velocity, m/s, world frame (the way it blows TO)
  double speed = 0;            // m/s, the whole vector's length
  double gust = 0.5;           // the gusts' sum as a share of their range: 0 a lull .. 1 the top
};

// A cell of the shelter grid: the share of the wind kept and how far it is turned (rad,
// clockwise from above).
struct ShelterAt {
  double keep = 1;
  double turn = 0;
};

// A baked grid over the plan: cell (i, k) stands at (x0 + i * cell, z0 + k * cell). Off the grid
// the nearest edge cell is read.
struct Shelter {
  double x0 = 0, z0 = 0, cell = 1;
  int nx = 0, nz = 0;
  std::vector<float> keep;  // nx * nz, row-major in z
  std::vector<float> turn;  // nx * nz, or empty for no turning
};

struct Source;

// Where the air is asked about: a world point and its height over the ground under it, m.
struct Where {
  double x = 0, y = 0, z = 0;
  double above = 10;
};

// A phase in [0, 2 pi) hashed off a seed and a salt.
double phase_of(uint32_t seed, uint32_t salt);

// The turbulence intensity the log law gives over a ground at a height: 1 / ln(h / z0).
double turbulence_for(double roughness, double height = 10);

// The default gusts and veer for an intensity (the speed's standard deviation over the mean),
// their phases hashed off `seed`: five gusts from 5 to 60 s and a veer of 47 s.
void lay_gusts(Field& field, uint32_t seed, double intensity);

// The speed at `height` over the ground as a share of the speed at 10 m (the log law): 0 at and
// under z0, 1 at 10 m.
double profile_at(double roughness, double height);

// The wind 10 m over open ground at `t`: the mean, the gusts, the veer and the fold.
Air open_at(const Field& field, double t);

// The shelter grid's cell at (x, z), bilinear.
ShelterAt shelter_at(const Shelter& shelter, double x, double z);

// THE AIR at a point: the open air brought down to its height by the profile, kept and turned by
// the shelter (if any), with every local source added.
Air wind_at(const Field& field, const Where& where, double t, const Shelter* shelter = nullptr,
            std::span<const Source> sources = {});

}  // namespace ge::wind
