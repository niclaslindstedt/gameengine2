// `ge snow` (snow.hpp).
//
// ge:summary ge snow show and ge snow column over a saved snow layer
// ge:keywords cli ge snow show column snapshot png
// ge:skill agent-interface snow
// ge:test tests/cli_snow_test.cpp
#include "cli/snow.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "cli/json.hpp"
#include "lab/png.hpp"
#include "lab/raster.hpp"
#include "snow/layer.hpp"
#include "snow/snapshot.hpp"

namespace ge::cli {

namespace {

std::string num(double v) {
  char b[64];
  std::snprintf(b, sizeof b, "%.6g", v);
  return b;
}

bool read_layer(const std::string& file, snow::SnowWorld& world, Outcome& o) {
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    o.findings.push_back({Severity::error, "cannot read " + file, file,
                          "pass a snapshot written by snow::save (a .snow file)"});
    return false;
  }
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
  std::string error;
  if (!snow::load_bare(world, bytes, error)) {
    o.findings.push_back(
        {Severity::error, error, file, "write the snapshot again with snow::save"});
    return false;
  }
  return true;
}

}  // namespace

Outcome run_snow_show(const std::string& file, const std::string& out_dir) {
  Outcome o;
  snow::SnowWorld world;
  if (!read_layer(file, world, o)) return o;
  const snow::Layer& l = *world.layers[0];
  int pressed = 0, berm = 0;
  double deepest = 0, densest = 0;
  int tx0 = l.tile_cols, tz0 = l.tile_rows, tx1 = -1, tz1 = -1;
  for (const snow::Tile& t : l.tiles) {
    tx0 = t.tx < tx0 ? t.tx : tx0;
    tz0 = t.tz < tz0 ? t.tz : tz0;
    tx1 = t.tx > tx1 ? t.tx : tx1;
    tz1 = t.tz > tz1 ? t.tz : tz1;
    for (const snow::Cell& c : t.cells) {
      if (c.flags & snow::kPressed) pressed++;
      if (c.flags & snow::kBerm) berm++;
      if (c.flags & snow::kTouched) {
        const double rho = snow::cell_density(c);
        densest = rho > densest ? rho : densest;
      }
    }
  }
  // The deepest drop: the pressed sample furthest under the deepest untouched sample of its tile.
  for (const snow::Tile& t : l.tiles) {
    double cover = 0;
    for (const snow::Cell& c : t.cells) {
      if (!(c.flags & snow::kTouched))
        cover = snow::cell_depth(c) > cover ? snow::cell_depth(c) : cover;
    }
    for (const snow::Cell& c : t.cells) {
      if ((c.flags & snow::kPressed) && cover - snow::cell_depth(c) > deepest) {
        deepest = cover - snow::cell_depth(c);
      }
    }
  }
  const uint64_t mass = snow::mass_units(l);
  const bool held = mass + l.spilled == l.mass_in;
  const double memory_kb = static_cast<double>(l.tiles.size() * sizeof(snow::Tile)) / 1024;
  o.text = "region " + num(l.spec.region.x0) + "," + num(l.spec.region.z0) + " .. " +
           num(l.spec.region.x1) + "," + num(l.spec.region.z1) + " m, cell " + num(l.spec.cell) +
           " m\ntiles " + std::to_string(l.tiles.size()) + " of " +
           std::to_string(l.tile_cols * l.tile_rows) + " (" + num(memory_kb) +
           " KB)\npressed samples " + std::to_string(pressed) + ", berm samples " +
           std::to_string(berm) + "\ndeepest drop " + num(deepest) + " m, densest " + num(densest) +
           " kg/m3\nmass " + (held ? "held" : "NOT held") + "\n";
  if (!held) {
    o.findings.push_back({Severity::error,
                          "the tiles hold " + std::to_string(mass) + " mass units and spilled " +
                              std::to_string(l.spilled) + ", but started with " +
                              std::to_string(l.mass_in),
                          file, "a press made or lost snow: run tests/snow_press_test.cpp"});
  }
  std::vector<std::string> pictures;
  if (!l.tiles.empty()) {
    const int cols = (tx1 - tx0 + 1) * snow::kTileCells, rows = (tz1 - tz0 + 1) * snow::kTileCells;
    lab::Picture depth = lab::create_picture(cols, rows, {40, 40, 48, 255});
    lab::Picture dense = depth, state = depth;
    double most = 0.01;
    for (const snow::Tile& t : l.tiles) {
      for (const snow::Cell& c : t.cells)
        most = snow::cell_depth(c) > most ? snow::cell_depth(c) : most;
    }
    for (const snow::Tile& t : l.tiles) {
      for (int j = 0; j < snow::kTileCells; j++) {
        for (int i = 0; i < snow::kTileCells; i++) {
          const snow::Cell& c = t.cells[j * snow::kTileCells + i];
          const int px = (t.tx - tx0) * snow::kTileCells + i;
          const int py = rows - 1 - ((t.tz - tz0) * snow::kTileCells + j);  // +z up the picture
          const auto g = static_cast<uint8_t>(255 * snow::cell_depth(c) / most);
          lab::set_pixel(depth, px, py, {g, g, g, 255});
          const double s = snow::cell_density(c) / 600;
          const auto b = static_cast<uint8_t>(255 * (s > 1 ? 1 : s));
          lab::set_pixel(dense, px, py,
                         {static_cast<uint8_t>(250 - b * 0.85),
                          static_cast<uint8_t>(252 - b * 0.65), 255, 255});
          const lab::Rgba st = (c.flags & snow::kPressed) ? lab::Rgba{60, 110, 200, 255}
                               : (c.flags & snow::kBerm)  ? lab::Rgba{230, 160, 60, 255}
                                                          : lab::Rgba{150, 150, 158, 255};
          lab::set_pixel(state, px, py, st);
        }
      }
    }
    std::filesystem::path dir = out_dir.empty() ? std::filesystem::path(file).parent_path() / "snow"
                                                : std::filesystem::path(out_dir);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string stem = std::filesystem::path(file).stem().string();
    for (const auto& [name, pic] : {std::pair<const char*, const lab::Picture*>{"depth", &depth},
                                    {"density", &dense},
                                    {"state", &state}}) {
      const std::string path = (dir / (stem + "." + name + ".png")).string();
      if (lab::write_png(path, *pic)) {
        o.written.push_back(path);
        pictures.push_back(path);
      } else {
        o.findings.push_back({Severity::error, "cannot write " + path, path,
                              "pass --out=<dir> with a directory you can write"});
      }
    }
  }
  o.result = json_obj({{"tiles", json_num(static_cast<long long>(l.tiles.size()))},
                       {"tile_slots", json_num(static_cast<long long>(l.tile_cols) * l.tile_rows)},
                       {"memory_kb", num(memory_kb)},
                       {"cell", num(l.spec.cell)},
                       {"pressed", json_num(pressed)},
                       {"berm", json_num(berm)},
                       {"deepest_drop", num(deepest)},
                       {"densest", num(densest)},
                       {"mass_held", json_bool(held)},
                       {"pictures", json_strs(pictures)}});
  o.summary = std::to_string(l.tiles.size()) + " tiles, " + std::to_string(pressed) +
              " samples pressed, mass " + (held ? "held" : "NOT held");
  o.next = "ge snow column " + file + " --at=<x,z>";
  return o;
}

Outcome run_snow_column(const std::string& file, const std::string& at) {
  Outcome o;
  const size_t comma = at.find(',');
  double x = 0, z = 0;
  try {
    if (comma == std::string::npos) throw std::invalid_argument("no comma");
    x = std::stod(at.substr(0, comma));
    z = std::stod(at.substr(comma + 1));
  } catch (const std::exception&) {
    o.findings.push_back({Severity::error, "--at=" + at + " is not a point", "",
                          "pass --at=<x,z> in metres, such as --at=3.5,-12"});
    o.exit = 2;
    return o;
  }
  snow::SnowWorld world;
  if (!read_layer(file, world, o)) return o;
  const snow::Layer& l = *world.layers[0];
  const snow::Region& r = l.spec.region;
  if (x < r.x0 || x > r.x1 || z < r.z0 || z > r.z1) {
    o.findings.push_back({Severity::error,
                          "(" + num(x) + ", " + num(z) + ") lies outside the layer's region " +
                              num(r.x0) + "," + num(r.z0) + " .. " + num(r.x1) + "," + num(r.z1),
                          file, "pass a point inside the region (`ge snow show` prints it)"});
    return o;
  }
  const snow::Column c = snow::sample(l, x, z);
  o.text = "depth " + num(c.depth) + " m, density " + num(c.density) + " kg/m3, packed " +
           num(c.packed) + ", kind " + std::string(snow::kind_row(c.kind).name) +
           (c.touched ? ", pressed or thrown onto" : ", untouched (no snow kept)") + "\n";
  o.result = json_obj({{"x", num(x)},
                       {"z", num(z)},
                       {"depth", num(c.depth)},
                       {"density", num(c.density)},
                       {"packed", num(c.packed)},
                       {"kind", json_str(snow::kind_row(c.kind).name)},
                       {"touched", json_bool(c.touched)}});
  o.summary = "the column at " + num(x) + "," + num(z);
  return o;
}

}  // namespace ge::cli
