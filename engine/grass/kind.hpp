// KINDS OF GRASS (docs/grass.md § Kinds): what a field of grass looks like and how it moves, as
// a handful of numbers a game, a level or an agent writes. A kind is NOT a texture and NOT a mesh:
// it is the recipe every blade of a cover is grown from (cover.hpp), on the CPU and on the GPU
// alike (render/grass.hpp).
//
// - THE BLADE: its height (m) and how much that varies blade to blade (a share either way), its
//   width at the root (m; a blade tapers to its tip), its rest lean (rad off the vertical) and how
//   much that varies.
// - THE FIELD: how many blades a square metre holds (`density`; thick or thin), and the CLUMPS
//   blades grow in (`clump`: the size of a clump, m, 0 for none; `pull`: how far a blade leans
//   in toward its clump's middle, 0..1), which is what makes a field read as grass and not as
//   fur.
// - THE WIND: `give`, the wind speed (m/s at the blade's height) that bends a blade halfway to
//   flat, so a stiff short lawn has a large give and long soft grass a small one; `sway`, the
//   blade's own swaying frequency (Hz), lower for a taller blade.
// - THE COLOUR: linear RGB at the root and at the tip, and a `dry` share of blades turned to
//   `dry_tip`, so a meadow in late summer is not one green.
//
// PRESETS (`preset`): `lawn` (short, thin, dense), `meadow` (shin high), `long` (knee to thigh
// high, soft), `tussock` (clumped and tufted), `steppe` (dry, thin, wiry), `reeds` (tall, stiff,
// sparse). A RECIPE (`parse_kind`) is a preset's name and overrides, the form a level file and the
// `ge` command line write: `meadow height=0.6 density=300 width=0.004 dry=0.2`. `print_kind`
// writes the canonical recipe back, every field that differs from its preset.
//
// ge:summary Kinds of grass: blade height, width, density, lean, clumps, give, sway, colours
// ge:keywords grass kind preset recipe lawn meadow long-grass tussock steppe reeds density
// ge:keywords tall short thick thin clump give sway parse_kind print_kind
// ge:skill grass
// ge:test tests/grass_kind_test.cpp
#pragma once

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace ge::grass {

struct Kind {
  std::string name = "meadow";
  float height = 0.35f;        // m, the blade's length
  float height_jitter = 0.3f;  // the share either way a blade's height varies
  float width = 0.005f;        // m, at the root
  float density = 400;         // blades per square metre
  float lean = 0.25f;          // rad, the rest lean off the vertical
  float lean_jitter = 0.2f;    // rad either way
  float clump = 0.25f;         // m, a clump's size (0: no clumps)
  float pull = 0.4f;           // 0..1, how far a blade leans toward its clump's middle
  float give = 6;              // m/s at the blade's height that bends it halfway to flat
  float sway = 1.6f;           // Hz, the blade's own swaying
  std::array<float, 3> root{0.050f, 0.090f, 0.018f};  // linear RGB at the root
  std::array<float, 3> tip{0.180f, 0.300f, 0.060f};   // linear RGB at the tip
  float dry = 0.08f;                                  // the share of blades dried
  std::array<float, 3> dry_tip{0.42f, 0.36f, 0.14f};  // a dried blade's tip
};

// The named presets, in a fixed order (their ids in a cover).
std::span<const Kind> presets();

// A preset by name, or nothing.
std::optional<Kind> preset(std::string_view name);

// A recipe: a preset's name, then `field=value` words (height, height_jitter, width, density,
// lean, lean_jitter, clump, pull, give, sway, dry; root, tip and dry_tip as `r,g,b`). On a fault,
// nothing, with `error` set to what is wrong and what to write instead.
std::optional<Kind> parse_kind(std::string_view recipe, std::string* error = nullptr);

// The canonical recipe: the name, then every field that differs from that preset's, in the order
// above.
std::string print_kind(const Kind& kind);

// The colour the far ground is painted under a kind (the blades' mean, root to tip and dry share
// mixed by how much of each a viewer sees from afar, darkened by the shade a field casts into
// itself): what a terrain's albedo is painted under a cover so the blades thin into a ground of
// the same colour.
std::array<float, 3> far_colour(const Kind& kind);

}  // namespace ge::grass
