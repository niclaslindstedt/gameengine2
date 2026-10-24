// THE ROOM LEVEL AS TEXT (`.gelv.txt`): the form an agent writes a level in and reads one back
// in, line for line the same model as the bytes (codec.hpp), in metres. `ge`-side tools convert
// between the two; `parse(print(level))` is the level again and `print` is canonical.
//
//   # a comment (a '#' at a line's start or after a space); blank lines are skipped
//   material <name> rgb=<r>,<g>,<b> [relief=<kind>,<depth_cm>,<scale_dm>] [tex <reference...>]
//   room <name> at=<x>,<y>,<z> seed=<u32> floor=<y>:<material> ceiling=<y>:<material>
//        [cave=<depth_dm>,<scale_dm>,<cell_cm>]
//   corner <x> <z> <wall material>          (the room above's corners, counter-clockwise)
//   portal <room> <wall> <room|outside> at=<along> width=<w> sill=<s> head=<h> [trim=<material>]
//   solid <room> <material> <min x> <min y> <min z> <max x> <max y> <max z>
//   surface <material> <surface reference...>    (a procedural surface drawn in its place)
//   group <name>
//   brush <solid|water|slime|lava|sky|clip|trigger> [group=<name>]
//   face <nx>,<ny>,<nz> <d> <material> s=<x>,<y>,<z>,<off> t=<x>,<y>,<z>,<off> [hidden] [smooth]
//   entity <class> [group=<name>]
//   key <name> <value...>                         (the entity above's; the rest of the line)
//
// A material may also say `size=<w>,<h>`, its texture's size in texels. A face's plane and
// projection are absolute metres, written as floats in the fewest digits that read back to
// their bits; the projections are pooled in the order faces first use them. A value may hold
// spaces, and a `#` inside a word (`tint=#8a5a40`); a `#` that begins the line or follows a
// space starts a comment.
//
// `at=` of a room is absolute; corners, floor, ceiling, a portal's numbers and a solid's box are
// relative to its room, all in metres (rounded to the format's 1/128 m). A relief's kinds are
// relief.hpp's. `tex` takes the rest of the line as the texture reference
// (`cave_rock seed=7 wear=0.8`). A room is named before a portal or solid names it. Errors name the
// line and say what to write instead.
//
// ge:summary The room level's text form (.gelv.txt): parse and print, metres, line-numbered errors
// ge:keywords text parse print authoring agent gelv-txt metres line errors canonical
// ge:skill rooms
// ge:test tests/rooms_level_test.cpp
#pragma once

#include <string>
#include <string_view>

#include "rooms/level.hpp"

namespace ge::rooms {

// The level written in `text`, linked; false with "line <n>: <what>; <what to do>" in `*error`.
bool parse(std::string_view text, Level* out, std::string* error);
// `level` as canonical text.
std::string print(const Level& level);

}  // namespace ge::rooms
