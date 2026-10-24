// THE FETCH: how far the wind has blown over water to reach a point, which is what decides how
// big a sea it has grown there (spectrum.hpp's `grow`). Read off a body's outline: a long lake is
// rough at its lee shore and calm under its windward one, and a pond grows ripples whatever the
// wind.
//
// THE EFFECTIVE FETCH (the Shore Protection Manual's): from the point, rays upwind fanned
// +-42 degrees in 6 degree steps, each run to the shore, weighted by the cosine of its angle off
// the wind, F = sum F_i cos^2 a_i / sum cos a_i, so a narrow body gives the wind a shorter run than
// its length straight up the middle would say. `bake_fetch` measures it once a body on a grid over
// its outline; `fetch_at` reads the grid bilinearly.
//
// ge:summary The fetch over a body's outline: the effective fetch by rays upwind, baked on a grid
// ge:keywords water fetch effective-fetch shore outline polygon inside ray upwind lee windward
// ge:keywords bake_fetch fetch_at fetch_grid point-in-polygon
// ge:skill water
// ge:test tests/water_fetch_test.cpp
#pragma once

#include <span>
#include <vector>

namespace ge::water {

struct Point {
  double x = 0, z = 0;
};

// Whether (x, z) lies inside the outline (even-odd).
bool inside(std::span<const Point> outline, double x, double z);

// How far from (x, z) to the outline's edge heading `heading` (rad), m; 0 outside it.
double to_shore(std::span<const Point> outline, double x, double z, double heading);

// The effective fetch at (x, z) for a wind blowing TOWARD `toward`, m.
double effective_fetch(std::span<const Point> outline, double x, double z, double toward);

struct FetchGrid {
  double x0 = 0, z0 = 0, cell = 1;
  int nx = 0, nz = 0;
  std::vector<float> fetch;  // nx * nz, row-major in z; 0 on dry ground
  double longest = 0;        // the longest fetch on the grid, m
};

// The fetch over the outline's box on a grid of about `cells` across its longer side.
FetchGrid bake_fetch(std::span<const Point> outline, double toward, int cells = 64);

double fetch_at(const FetchGrid& grid, double x, double z);

}  // namespace ge::water
