// A BLADE IN THE WIND (docs/grass.md § Sway): the shape every blade is drawn in, as a pure
// function of its kind, its own numbers (cover.hpp's `Blade`), the air at it and the clock. The
// GPU draws exactly this (render/grass.wgsl's `blade_shape`, `wind_at_blade`); `blade_shape` and
// `wind_at_blade` here are the CPU twins the tests hold it to and the CPU labs draw with.
//
// - THE AIR AT THE GRASS (`WindGrid`): the engine's wind (wind/field.hpp's `wind_at`, at the
//   blades' height over the ground, with the field's shelter and every local source, a rotor's
//   downwash included) sampled on a square grid round a point, a plan vector a node. The gusts are
//   CARRIED DOWNWIND: a node reads the field's air at t - (its distance along the mean bearing) /
//   (the mean speed), so a gust crosses a meadow as a front, the way it is seen on a real field.
//   `wind_at_blade` reads the grid bilinearly and ruffles it with a value noise of 6 m blown along
//   with the wind (the small cats-paws a grid of a few metres cannot hold).
// - THE BEND: a blade is a quadratic Bézier from its root (v0) through a guide (v1) to its tip
//   (v2), of a length that never changes (the I3D 2017 per-blade model's length correction). The
//   wind bends it by b = w² / (w² + give²) (the drag on a blade grows with the speed squared and
//   its stiffness pushes back), its rest lean giving way to the wind's bearing as b grows, to at
//   most kMaxBend off the vertical. On top of that it SWAYS at its own frequency (the kind's `sway`
//   within ±15 %, the phase its own), most in a middling wind and least in a calm or a gale that
//   holds it flat, and a little across the wind.
//
// ge:summary A blade's shape in the wind: the wind grid with travelling gusts, bend, sway, Bézier
// ge:keywords grass sway bend wind-grid gust front travelling-gust bezier blade-shape length
// ge:keywords give flutter cats-paw wind_at_blade fill_wind_grid blade_shape bezier_point
// ge:skill grass
// ge:test tests/grass_sway_test.cpp
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "core/heightfield.hpp"
#include "grass/cover.hpp"
#include "grass/kind.hpp"
#include "wind/field.hpp"
#include "wind/sources.hpp"

namespace ge::grass {

inline constexpr float kMaxBend = 1.35f;    // rad off the vertical, a blade flattened by a gale
inline constexpr float kRuffleCell = 6.0f;  // m, the cats-paws' noise
inline constexpr int kWindGridMax = 64;     // nodes a side

// The air at the grass round a point: node (i, k) at (x0 + i * cell, z0 + k * cell), its plan
// velocity (x, z) m/s; `mean` the field's mean as a plan vector (the cats-paws drift with it).
struct WindGrid {
  float x0 = 0, z0 = 0, cell = 1;
  int n = 0;
  float mean[2] = {0, 0};
  std::vector<float> air;  // n * n * 2, row-major in z
};

// What `fill_wind_grid` reads: the field and its optional shelter and sources, the ground under
// the grass (null: flat at y = 0) and the height over it the air is asked at.
struct GrassAir {
  const wind::Field* field = nullptr;
  const wind::Shelter* shelter = nullptr;
  std::span<const wind::Source> sources;
  const core::Heightfield* ground = nullptr;
  double height = 0.5;  // m over the ground: a blade's middle
};

// The grid of `n` x `n` nodes over a square `span` m across centred on (cx, cz), at `t`. Its
// origin snaps to whole cells, so a grid that follows a camera never swims.
void fill_wind_grid(WindGrid& grid, const GrassAir& air, double cx, double cz, double span, int n,
                    double t);

// The plan air (m/s) at a blade's root at `t`: the grid bilinear (clamped at its edge), ruffled.
void wind_at_blade(const WindGrid& grid, float x, float z, float t, float out[2]);

// A blade's three control points (world m) and its width at the root.
struct Shape {
  float v0[3], v1[3], v2[3];
  float width;
};

// THE SHAPE of `blade` of `kind` in plan air `wind` (m/s) at `t`.
Shape blade_shape(const Kind& kind, const Blade& blade, const float wind[2], float t);

// The point at `s` (0 the root .. 1 the tip) along a shape's curve.
void bezier_point(const Shape& shape, float s, float out[3]);

// How far the wind bends a blade of `kind`: 0 calm .. 1 flat.
float bend_of(const Kind& kind, float speed);

}  // namespace ge::grass
