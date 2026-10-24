// THE WATER A GAME HAS LAID DOWN, AND THE ONE QUERY (docs/water.md § Bodies of water and the one
// query). A `World` holds bodies; everything anything asks of water goes through `sample`, so the
// physics, a swimmer, the sound and the renderer cannot disagree about where the surface is.
//
// Laying water is one call a body:
//
//   water::World world(wind_field);                       // the air the waves grow from
//   water::add_lake(world, shore, 412.0);                 // an outline and a level
//   water::add_ocean(world, 0.0);                         // a level to the horizon
//   water::add_river(world, centreline, 6.0);             // points with level and width, m^3/s
//   water::add_volume(world, hulls);                      // convex hulls (a brush world's water)
//   water::Sample s = water::sample(world, x, y, z, t);   // anything, anywhere, any time
//
// THE KINDS. A LAKE is a polygon at a level; its sea is grown by the wind's mean over the lake's
// effective fetch, measured per point on a baked grid (fetch.hpp) so its windward shore is calm
// and its lee shore rough; the components are laid for its longest fetch and scaled by each
// point's own. An OCEAN is unbounded at a level with a fully developed sea (or the fetch it is
// given). A CHANNEL (a river, a stream) is a centreline of points, each with its surface's level,
// its width and its depth: the surface falls down it and the current runs along it, fastest
// mid-stream (the profile carries the discharge through the parabolic section, 1.25 times the
// section's mean speed in the middle falling to nothing at the banks), with a ripple of its own. A
// VOLUME is a set of convex hulls, each a list of planes, its surface the hull's top over a point:
// a brush world's water, slime and lava, or any boxed water; a volume carries no waves
// (`Body::waves` may be laid by hand). A FLOW region (flow.hpp) is water simulated on a grid: a
// wall broken, a basin emptying, a flood; `step_flows` steps every region and `sample` reads its
// surface and current where its cells are wet. WAKES (wake.hpp): the rings in `World::wakes` ride
// on their lake's or ocean's surface in every sample.
//
// THE LOOK. A body's colour is what is in its water (optics.hpp): `Stuff::look` names a row of
// `water::looks()`, or `Stuff::water` states the game's own constituents; `Body::optics` is
// derived once when the body is laid, and the renderer reads it by the sample's body index.
//
// THE BED. A body may be handed the bed's height (`Bed`, a function and its context); with none,
// a lake and a channel are their `depth` deep and an ocean is deep. The bed sets the depth a
// sample reports and shallows the waves (their height held under 0.78 of the depth).
//
// ge:summary Bodies of water (lake, ocean, channel, volume) and the one sample query
// ge:keywords water world body lake ocean river channel stream volume brush hull add_lake
// ge:keywords add_ocean add_river add_volume sample surface depth bed current density look
// ge:keywords optics colour add_flow step_flows wakes
// ge:skill water
// ge:test tests/water_world_test.cpp
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "water/fetch.hpp"
#include "water/flow.hpp"
#include "water/optics.hpp"
#include "water/wake.hpp"
#include "water/waves.hpp"
#include "wind/field.hpp"

namespace ge::water {

enum class Kind : uint8_t { lake, ocean, channel, volume, flow };

// The bed's height under a point, m (world y).
struct Bed {
  double (*height)(const void* context, double x, double z) = nullptr;
  const void* context = nullptr;
};

// A point of a channel's centreline.
struct ChannelPoint {
  double x = 0, z = 0;
  double level = 0;  // the surface's height here, m
  double width = 4;  // bank to bank, m
  double depth = 1;  // mid-stream, m (when no bed is given)
};

// One plane of a convex hull: inside where nx x + ny y + nz z <= d.
struct Plane {
  double nx = 0, ny = 1, nz = 0, d = 0;
};
struct Hull {
  std::vector<Plane> planes;
};

// What a body is made of: its look (a name among `water::looks()`, or the constituents of the
// game's own in `water`, which win), and the numbers the physics needs.
struct Stuff {
  std::string look = "nordic-clear";
  double density = 1000;    // kg/m^3 (1025 sea water)
  double temperature = 10;  // degrees C
  std::optional<Constituents> water{};
};

struct Body {
  Kind kind = Kind::lake;
  Stuff stuff;
  double level = 0;  // a lake's and an ocean's surface, m
  double depth = 5;  // m, when no bed is given (an ocean: deep)
  Bed bed;
  std::vector<Point> shore;           // a lake's outline (fetch.hpp's Point)
  std::vector<ChannelPoint> channel;  // a channel's centreline
  double discharge = 0;               // a channel's, m^3/s
  std::vector<Hull> hulls;            // a volume's
  int flow = -1;                      // a flow region's index in `World::flows`
  Optics optics;                      // its colour, from its look (optics.hpp)
  Waves waves;                        // the sea laid on it
  FetchGrid fetch;                    // a lake's fetch per point
  double fetch_ref = 0;               // the fetch its waves were laid for, m
  double box[4] = {0, 0, 0, 0};       // x0, z0, x1, z1 over its plan (unbounded for an ocean)
};

struct World {
  World() = default;
  explicit World(const wind::Field& field) : wind(field) {}
  wind::Field wind;  // the air the waves grow from (its mean only)
  std::vector<Body> bodies;
  std::vector<Flow> flows;  // the flow regions (flow.hpp), stepped by `step_flows`
  uint32_t seed = 1;        // the waves' phases and directions, with the body's index
  Lay lay;                  // how every body's band is laid
  Wakes wakes;              // the rings on the lakes and the oceans (wake.hpp)
};

int add_lake(World& world, std::span<const Point> shore, double level, Stuff stuff = {},
             Bed bed = {}, double depth = 5);
int add_ocean(World& world, double level, Stuff stuff = {.look = "adriatic", .density = 1025},
              Bed bed = {}, double fetch = 1e9);
int add_river(World& world, std::span<const ChannelPoint> centreline, double discharge,
              Stuff stuff = {.look = "glacial"}, Bed bed = {});
int add_volume(World& world, std::span<const Hull> hulls, Stuff stuff = {});
// A flow region (flow.hpp) laid as a body: its water is what `sample` returns where it is wet.
int add_flow(World& world, Flow flow, Stuff stuff = {});
// Every flow region one step of `dt` s.
void step_flows(World& world, double dt);

// WHAT THE WATER IS at a point and a moment.
struct Sample {
  int body = -1;                  // the body whose water this is, or -1 (dry)
  double surface = 0;             // the surface's height over (x, z), m
  double nx = 0, ny = 1, nz = 0;  // the surface's normal
  double vx = 0, vy = 0, vz = 0;  // the water's own velocity at the surface: waves and current
  double depth = 0;               // how far the point is under the surface (negative above it), m
  double bed = 0;                 // the bed's height under the point, m
  double density = 1000;          // kg/m^3
  double temperature = 10;        // degrees C
  Breaking breaking;              // the foam's three shares
};

Sample sample(const World& world, double x, double y, double z, double t);

// The same for many points (a hull's probes, a crowd's feet).
void sample(const World& world, std::span<const double> xyz, double t, std::span<Sample> out);

}  // namespace ge::water
