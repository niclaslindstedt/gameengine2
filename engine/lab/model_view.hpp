// A MODEL SEEN BY THE LABS: an authored model (engine/assets) drawn on the CPU raster in
// perspective or orthographic, z-buffered, shaded by its materials, with its triangles' edges and
// its skeleton over it when asked, so a lab can turn a model round, look at it close, set its
// levels of detail side by side and film a clip from any angle, all without a GPU.
//
// - A `Shape` is the triangles a pose leaves: every mesh asked for, skinned by a palette
//   (`shape_of`), with the joints' places beside them for the bones.
// - A `ViewCam` is where the eye is: an orbit round a box at an azimuth and an elevation
//   (`orbit_cam`), a close-up of a sphere (`close_cam`), or an orthographic look along an axis
//   (`axis_cam`). Azimuth 0 looks at the model's front (from +z toward -z), 90 from its right (+x).
// - `draw_shape` fills one panel of a picture: drawn `supersample` times larger and averaged down,
//   each pixel's depth tested, each vertex lit (a key light over the right shoulder, a fill and
//   an ambient) and the light shared across its triangle; faces turned away are not drawn. The
//   edges are drawn depth-tested over the faces; the bones on top of everything.
// - `lod_groups` reads a model's levels of detail from its meshes' names: `<name>.lod<N>` or
//   `<name>_lod<N>` is level N of `<name>`; a mesh with no suffix is level 0.
// - `region_of` finds where a joint's skin is in a pose (the vertices it moves most), for a
//   close-up.
//
// Integer pixels from double arithmetic and the engine's own libm, so every picture is the same
// on every row and part of a lab's digest.
//
// ge:summary The labs' CPU model view: a posed model drawn z-buffered, shaded, wired and boned
// ge:keywords lab model-view model render raster z-buffer perspective orthographic turntable
// ge:keywords orbit close-up detail wireframe bones lod lod-groups supersample shape_of draw_shape
// ge:skill labs assets
// ge:test tests/lab_model_view_test.cpp
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "assets/model.hpp"
#include "assets/pose.hpp"
#include "lab/raster.hpp"

namespace ge::lab {

// A pose's triangles, model space.
struct Shape {
  std::vector<float> pos, nrm;        // xyz a vertex
  std::vector<uint8_t> part;          // a vertex's material
  std::vector<uint8_t> top;           // a vertex's joint of the largest weight
  std::vector<uint32_t> idx;          // three a triangle
  std::vector<assets::Affine> bones;  // every joint's global, for the skeleton
};

// The meshes `meshes` (indices into model.meshes; empty: every mesh at the finest level of
// detail, `meshes_at_lod(lod_groups(model), 0)`) posed by `global` (every joint's global
// transform: `assets::pose_globals`).
Shape shape_of(const assets::Model& model, std::span<const assets::Affine> global,
               std::span<const int> meshes = {});
// The model at rest, or clip `clip` at `t_s`.
Shape shape_at(const assets::Model& model, int clip, double t_s, std::span<const int> meshes = {});

struct ViewCam {
  double eye[3] = {0, 0, 5};
  double target[3] = {0, 0, 0};
  double fov_deg = 30;  // 0: orthographic
  double half_m = 1;    // orthographic: half the panel's height, m
};

// Round `box` at `azimuth_deg` (0: the front, from +z; 90: from +x) and `elevation_deg` above
// it, as near as keeps every corner of the box inside `fill` of a panel `aspect` (width over
// height) at `fov_deg`.
ViewCam orbit_cam(const assets::Box& box, double azimuth_deg, double elevation_deg,
                  double fov_deg = 30, double fill = 0.9, double aspect = 1);
// A sphere of `radius_m` round `centre` from `azimuth_deg`, `elevation_deg`, filling the panel.
ViewCam close_cam(const float centre[3], double radius_m, double azimuth_deg, double elevation_deg,
                  double fov_deg = 30);
// Orthographic along an axis: "front" (from +z), "back", "right" (from +x), "left", "top", with
// the box filling `fill` of the panel.
ViewCam axis_cam(const assets::Box& box, const std::string& side, double fill = 0.85);

struct ViewStyle {
  bool shade = true;
  bool wire = false;
  bool bones = false;
  bool labels = false;  // the joints' names beside the bones
  bool baked = false;   // every vertex at its material's own colour: light baked into it (a map's
                        // lightmaps), no key, fill or ambient over it
  bool floor = true;    // a grid on the box's floor, a line a metre
  Rgba background{24, 26, 32};
  Rgba edge{255, 255, 255, 70};
  Rgba bone{255, 210, 60};
  int supersample = 2;                 // 1..4
  double key[3] = {0.45, 0.75, 0.48};  // the key light's way (toward it), world; a raking one
                                       // brings a surface's relief out
};

// `shape` of `model` (for its materials and joint names) through `cam` into the panel at
// (x0, y0) of w x h; `floor_y` is where the grid lies (the box's floor).
void draw_shape(Picture& pic, int x0, int y0, int w, int h, const ViewCam& cam,
                const assets::Model& model, const Shape& shape, const ViewStyle& style,
                double floor_y = 0);

// The box round a shape's vertices.
assets::Box shape_box(const Shape& shape);

// A model's levels of detail: groups[g] is one mesh's levels, groups[g][n] the mesh index of
// level n (-1 where that level is missing); `names[g]` the mesh's name without its suffix.
struct LodGroups {
  std::vector<std::string> names;
  std::vector<std::vector<int>> levels;
  int most = 1;  // the most levels any group has
};
LodGroups lod_groups(const assets::Model& model);
// Every group's mesh at level n (its last level when it has fewer).
std::vector<int> meshes_at_lod(const LodGroups& lods, int n);

// Where joint `joint`'s skin is in `shape`: the centre and the radius of the vertices whose
// largest weight is its own or a descendant's within `depth` generations. False when it moves none.
bool region_of(const assets::Model& model, const Shape& shape, int joint, int depth,
               float centre[3], float* radius);

}  // namespace ge::lab
