// WHAT A FRAME LOOKS AT, AND FROM WHERE: the `FrameView` a game extracts from its state once a
// frame (plain data; the renderer never reads the simulation any other way), and the camera's
// matrices built from it.
//
// CONVENTIONS (the world's, stated in engine/core/math.hpp): y is up; a heading is 0 along +z
// and turns clockwise seen from above, toward +x; a pitch is positive looking up. So the world
// is left-handed: the camera's right is up x forward.
//
// THE CAMERA IS AT THE ORIGIN of every matrix here: the GPU works in camera-relative
// coordinates (world minus the eye, in float), so a point 3 km away keeps its millimetres.
// View space looks down -z with x right and y up; the projection is REVERSED-Z with an
// INFINITE far plane (depth 1 at the near plane, 0 at infinity), the depth buffer is cleared to
// 0 and tested `greater`, which spreads float depth's precision evenly over kilometres.
// Matrices are column-major float, as WGSL reads a mat4x4f from a buffer.
//
// The sun is a world direction (toward it), with its elevation and azimuth (a world heading),
// as engine/core/solar.hpp gives them. It is the KEY LIGHT: the one that lights the sky's tables,
// casts the shadows and keys the exposure. At night it is the moon once the moon outshines the
// twilight (`Night::moon_key`, decided by render/night.hpp), and one unit of `sun_illuminance`
// is always the sun's illuminance outside the air (render/night.hpp's `kSunLux`), so the eye's
// adaptation can read how dark a night is.
//
// THE WEATHER in the picture is `FrameView::weather` (`Weather`): how much of the sun's direct
// light the cloud lets through, how much of the dome a grey deck covers (its grey spread from
// the light the clear day would put on the ground, so it follows the hour and the night), and
// the visual range of a fog or a fall, which closes every surface into the sky behind it. The
// default is a clear sky: nothing changes. What a world's weather kinds mean in these numbers is
// the world's (plugins/mountain/app/weather_look.hpp); wgsl/frame.wgsl reads them.
//
// THE REACH a frame is drawn to is `FrameView::reach_m`: past half of it a mist closes every
// surface but the cloud into the sky behind its pixel (the sky under the horizon drawn as the
// horizon's), so a picture that draws less far ends in a bright haze rather than at an edge;
// nothing past it is drawn.
// The default draws to the horizon, unmisted.
//
// THE CLOUD a frame is seen under is `FrameView::clouds` (`Clouds`): up to three layers, each a
// slab of the air (its base and its top, world metres) where a tileable noise passes the cut its
// cover sets, carried by the wind and changing its shape as it goes. render/clouds.hpp draws it
// and casts its shadow, each place its own share of the sun. The default is none.
//
// ge:summary The FrameView a game extracts each frame, and its camera matrices and frustum
// ge:keywords render frame-view camera view projection reverse-z infinite-far camera-relative
// ge:keywords frustum planes heading pitch fov mat4 inverse sun-direction camera_matrices
// ge:keywords look_dir sun_direction depth_distance distance-from-depth unproject weather
// ge:keywords overcast deck cloud-cover sun-pass visibility fog flat-light night moon key-light
// ge:keywords cloud-layer clouds heap sheet roll horizon-tilt
// ge:skill render
// ge:test tests/render_view_test.cpp
#pragma once

#include <cstdint>

namespace ge::render {

// A column-major 4x4 matrix: element (row r, column c) is m[c * 4 + r].
struct Mat4 {
  float m[16];
};

Mat4 mul(const Mat4& a, const Mat4& b);
// The general inverse; the identity's when `a` is singular.
Mat4 inverse(const Mat4& a);
// a * (x, y, z, w)
void transform(const Mat4& a, const float in[4], float out[4]);

// The weather a frame is seen under. The default changes nothing: a clear sky.
struct Weather {
  double sun_pass = 1.0;    // the share of the sun's direct light the cloud lets through, 0..1
  double cover = 0.0;       // the share of the dome the deck covers: 0 clear .. 1 a lid
  double visibility_m = 0;  // the visual range of a fog or a fall, m; 0: the clear air's alone
  double hidden = 0.0;      // 0..1: how much of the deck overhead the snow in the air hides
  // The deck: its tint (luminance 1), how much of the clear day's light on the ground it spreads
  // as its own (an overcast spreads more than the sun lit, a storm's half), and its grey
  // overhead as a share of its grey at the horizon (a deck is darker overhead).
  double deck_tint[3] = {0.86, 0.9, 0.98};
  double deck_light = 1.0;
  double deck_zenith = 0.6;
};

// ONE LAYER OF CLOUD (render/clouds.hpp): a slab of the air where the noise passes its cover's
// cut; within it a HEAP's top grows into the cell from a flat base, a SHEET thickens about its
// middle.
struct CloudLayer {
  double base_m = 0, top_m = 0;  // world heights of its base and its top, m
  double cover = 0;              // the share of the sky it covers, 0..1
  double cell_m = 1500;          // one cell's span across the wind, m
  double stretch = 1;            // how far a cell is drawn out along the wind
  double heading = 0;            // the heading the wind carries it toward, rad
  double drift_m = 0;            // how far the wind has carried it, m
  double evolve = 0;             // the shape's age in cell lives (the noise's third axis)
  double dome = 1;               // 1 a heap, 0 a sheet
  double density = 0.02;         // the extinction inside a full cell, 1/m
  double shadow_tau = 0;         // the optical depth a full column takes off the sun below
};
inline constexpr int kCloudLayers = 3;
// Where the reach's mist begins, as a share of the reach (FrameView::reach_m).
inline constexpr double kMistBegins = 0.5;
struct Clouds {
  int count = 0;
  CloudLayer layers[kCloudLayers];
  double steps = 1;     // the share of the march's full steps taken (the quality ladder's), 0.25..1
  bool quarter = true;  // marched at a quarter of the pixels, then upsampled (false: every pixel)
  bool spread = true;   // the quarter's march spread over four frames, the rest reprojected
};

// THE NIGHT a frame is seen under (render/night.hpp fills it from where the moon is). The
// default is none: no moon, no stars, no dark sky's glow.
struct Night {
  // The moon: where it stands (rad, a world heading), its illuminance outside the air (the sun's
  // units; 0 while the sun is up, when its light and its sky are nothing beside the day's), its
  // angular radius (0: not drawn), the lit surface's luminance outside the air in the sun's units
  // per steradian (the disc's scale: the phase law shared out over the lit part,
  // render/night.hpp), and the earthlit part's.
  double moon_elevation = -1.0;
  double moon_azimuth = 0;
  double moon_illuminance = 0;
  double moon_radius = 0;
  double moon_surface = 0;
  double earthshine = 0;
  // Its face (world unit vectors: its north pole, 0 N 0 E, 0 N 90 E) and the albedo map's scale
  // (render/moon.hpp: the map's byte over 255 times it is the albedo against the near side's
  // mean).
  double moon_pole[3] = {0, 1, 0};
  double moon_prime[3] = {0, 0, -1};
  double moon_east[3] = {1, 0, 0};
  double albedo_scale = 1;
  // The Earth's shadow on it (render/night.hpp): toward its axis, the umbra's and penumbra's
  // angular radii (rad; 0: none), and the disc's brightest point's light against full sunlight.
  double shadow[3] = {0, 0, 1};
  double umbra = 0;
  double penumbra = 0;
  double eclipse_peak = 1;
  // Whether the KEY LIGHT is the moon: FrameView's sun_* then stand for the moon, and these for
  // the real sun (the moon's bright limb faces it; its sky is drawn beside the moon's).
  bool moon_key = false;
  double sun_elevation = 0;
  double sun_azimuth = 0;
  double sun_illuminance = 1.0;
  // Whether the air's haze is the moon's light scattered (its sky outshines the twilight's).
  bool moon_haze = false;
  // The dark sky (the airglow and the stars too faint to see one by one): its zenith's luminance
  // and the light it gives a level face, the sun's units.
  double dark_sky = 0;
  double dark_light = 0;
  // The moon's direct light on a level face while the sun is the key under the horizon, the
  // sun's units, spread from above (only the key casts shadows).
  double other_light = 0;
  // The stars' sphere: the local sidereal angle and the latitude, rad.
  double sidereal = 0;
  double latitude = 0;
};

struct FrameView {
  // The eye, world metres, and where it looks.
  double eye[3] = {0, 0, 0};
  double heading = 0;   // rad, the world's convention
  double pitch = 0;     // rad, positive up
  double roll = 0;      // rad, right side down positive (engine/camera's pose); 0 changes nothing
  double fov_y = 1.0;   // vertical field of view, rad
  double near_m = 0.1;  // the near plane, m
  // The sun: elevation over the horizon (rad) and azimuth (a world heading).
  double sun_elevation = 0.5;
  double sun_azimuth = 3.14159265358979;
  double sun_illuminance = 1.0;  // relative; the sky and every light scale with it
  // The world's y = 0 over the sea, m: where the atmosphere starts.
  double altitude_m = 0;
  Weather weather{};  // the cloud and the fog; the default is a clear sky
  Night night{};      // the moon, the stars and the dark sky's glow; the default is none
  Clouds clouds{};    // the cloud's layers; the default is none
  // THE SCENE'S OWN LIGHT the eye adapts to: the luminance of the bright emitters filling the
  // view (a lava lake's mean glow, a lit street's), cd/m^2. The exposure keys a white face under
  // the sky or this, whichever is brighter, so a lava lake at night reads as a photograph of it
  // would, not blown out against a dark sky. 0: the sky's light alone.
  double adapt_cd = 0;
  // THE REACH, m: how far the picture draws. Past half of it a mist closes every surface into
  // the sky behind it, all of it by the reach, and nothing farther is drawn; 0: no reach, no mist.
  double reach_m = 0;
  double time_s = 0;   // the frame's time (the renderer reads no clock)
  uint64_t frame = 0;  // the frame's number (TAA's jitter is indexed by it)
};

// The unit direction a heading and pitch look along.
void look_dir(double heading, double pitch, double out[3]);
// The unit direction toward the sun: the KEY light's (the moon's when `night.moon_key`).
void sun_direction(const FrameView& view, double out[3]);
// The unit direction toward the real sun, whichever is the key.
void real_sun_direction(const FrameView& view, double out[3]);

struct CameraMatrices {
  Mat4 view;           // camera-relative world -> view (a rotation)
  Mat4 proj;           // view -> clip, reversed-z, infinite far
  Mat4 view_proj;      // camera-relative world -> clip
  Mat4 inv_view_proj;  // clip -> camera-relative world
  // The frustum's left, right, bottom, top and near planes in camera-relative world space:
  // (a, b, c, d) with a*x + b*y + c*z + d >= 0 inside, (a, b, c) of unit length.
  float planes[5][4];
};

CameraMatrices camera_matrices(const FrameView& view, double aspect);

// How far from the eye, m, lies the surface a depth buffer shows at (ndc_x, ndc_y) (-1..1, y
// up): the point on the near plane through that pixel, scaled out by 1 / `depth` (reversed-z
// with an infinite far plane puts depth at near / distance along the view). Infinity where the
// depth is 0, the cleared sky.
double depth_distance(const CameraMatrices& m, double ndc_x, double ndc_y, float depth);

}  // namespace ge::render
