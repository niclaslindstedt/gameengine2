// THE NAMED SNOWS (docs/snow.md § The model): what a layer's cover and every cell call their
// snow, and the densities each kind is read against. A column's PACKED SHARE is where its density
// lies between its kind's loose and packed densities, so a powder pressed under a boot reads
// partly packed and a groomed piste reads packed whatever its exact density; and no press packs a
// kind past its cap (ice is the densest snow gets).
//
// The rows are starting values from the snow classification's density classes; each kind's
// contact numbers (Bekker's moduli, cohesion and friction, glide) are its row in contact.hpp.
//
// ge:summary The named snows and their loose, packed and cap densities; the packed share
// ge:keywords snow kinds powder settled wind-packed groomed spring crust machine ice density
// ge:keywords packed-share kind_row packed_from_density density_from_packed
// ge:skill snow
// ge:test tests/snow_layer_test.cpp
#pragma once

#include <cstdint>
#include <string_view>

#include "core/math.hpp"

namespace ge::snow {

enum class Kind : uint8_t {
  powder,       // cold new snow
  settled,      // a few days old, rounded grains
  wind_packed,  // slab the wind has pressed
  groomed,      // a piste milled and pressed by a machine
  spring,       // wet, corn
  crust,        // a hard lid over soft snow
  machine,      // made by snow guns, dense and wet
  ice,          // polished by traffic or refrozen melt
  count,
};

struct KindRow {
  std::string_view name;
  double loose;   // kg/m3: the density that reads as nothing packed
  double packed;  // kg/m3: the density that reads as fully packed
  double cap;     // kg/m3: no press packs it denser
};

inline constexpr KindRow kKinds[] = {
    {"powder", 70, 450, 600},   {"settled", 200, 450, 600}, {"wind-packed", 330, 500, 650},
    {"groomed", 300, 480, 650}, {"spring", 380, 520, 650},  {"crust", 250, 500, 650},
    {"machine", 380, 520, 680}, {"ice", 830, 830, 917},
};
static_assert(sizeof(kKinds) / sizeof(kKinds[0]) == static_cast<int>(Kind::count),
              "one row per kind");

inline const KindRow& kind_row(Kind k) { return kKinds[static_cast<int>(k)]; }

// The packed share 0..1 of a column of kind `k` at density `rho` kg/m3.
inline double packed_from_density(Kind k, double rho) {
  const KindRow& r = kind_row(k);
  if (r.packed <= r.loose) return 1;
  return core::clamp((rho - r.loose) / (r.packed - r.loose), 0, 1);
}

// The density kg/m3 a packed share `packed` reads as, for kind `k` (the inverse of the above).
inline double density_from_packed(Kind k, double packed) {
  const KindRow& r = kind_row(k);
  return core::lerp(r.loose, r.packed, core::clamp(packed, 0, 1));
}

}  // namespace ge::snow
