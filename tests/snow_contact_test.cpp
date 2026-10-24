// The snow contact laws (engine/snow/contact.hpp): Bekker's sink turned round is the pressure's
// own curve for every kind and width; a shallow column caps it; compaction and bulldozing grow
// with the sink; traction follows Mohr–Coulomb and Janosi–Hanamoto's shape against slip; glide
// is least near −5 °C; planing sinks e^-1 as deep at its speed; and a table of every kind's
// numbers keeps its golden (tests/golden/snow_contact.txt; GE_GOLDEN_WRITE=1 when a move is meant,
// with the snow-contact lab's tables before and after in the PR).
//
// ge:summary Tests: the snow contact laws against their curves, and their golden table
// ge:keywords test snow contact bekker sinkage traction janosi glide planing golden
// ge:skill snow testing
// ge:test tests/snow_contact_test.cpp
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "golden.hpp"
#include "snow/contact.hpp"
#include "snow/kinds.hpp"

namespace sn = ge::snow;

namespace {

constexpr const char* kGolden = "tests/golden/snow_contact.txt";

std::string hex(uint64_t v) {
  static const char* d = "0123456789abcdef";
  std::string s(16, '0');
  for (int i = 15; i >= 0; i--, v >>= 4) s[i] = d[v & 15];
  return s;
}

sn::Kind kind_of(int k) { return static_cast<sn::Kind>(k); }

}  // namespace

TEST(every_kinds_sink_lies_on_its_bekker_curve_for_every_load_and_width) {
  const double widths[] = {0.05, 0.1, 0.2, 0.4, 1.0};
  const double loads[] = {1e3, 5e3, 1e4, 3e4, 1e5};
  for (int k = 0; k < static_cast<int>(sn::Kind::count); k++) {
    const sn::ContactRow& row = sn::contact_row(kind_of(k));
    for (const double b : widths) {
      for (const double p : loads) {
        const double z = sn::bekker_sink(row, b, p);
        CHECK(z > 0);
        const double back = sn::bekker_pressure(row, b, z);
        CHECK_MSG(std::fabs(back - p) <= 0.05 * p,
                  std::string(sn::kind_row(kind_of(k)).name) + ": sink off its curve");
      }
    }
    // Bekker's size effect: where the cohesive modulus counts, a wider contact sinks deeper under
    // the same pressure (its pressure bulb reaches deeper snow).
    if (row.kc > 0) CHECK(sn::bekker_sink(row, 0.4, 1e4) > sn::bekker_sink(row, 0.05, 1e4));
  }
}

TEST(each_fitted_kind_sinks_its_stated_depth_under_a_10_kpa_plate) {
  const double sink10[] = {0.58, 0.137, 0.02, 0.01, 0.04, 0.01, 0.012, 0.0005};
  for (int k = 0; k < static_cast<int>(sn::Kind::count); k++) {
    const double z = sn::bekker_sink(sn::contact_row(kind_of(k)), 0.1, 1e4);
    CHECK_MSG(std::fabs(z - sink10[k]) <= 0.05 * sink10[k],
              std::string(sn::kind_row(kind_of(k)).name) + " off its fitted sink");
  }
}

TEST(a_shallow_column_caps_the_sink_at_what_it_can_compact) {
  // 5 cm of powder under a tyre's 60 kPa: Bekker alone would sink it far deeper.
  const double free_sink = sn::bekker_sink(sn::contact_row(sn::Kind::powder), 0.2, 6e4);
  const double z = sn::column_sink(sn::Kind::powder, 0.05, 70, 0.2, 6e4);
  CHECK(free_sink > 0.05);
  CHECK(z < 0.05 && z > 0.03);
  CHECK(z == sn::sink_cap(0.05, 70, 6e4, sn::kind_row(sn::Kind::powder).cap));
  // Snow already packed past what a pressure reaches does not sink under it.
  CHECK(sn::column_sink(sn::Kind::groomed, 0.3, 500, 0.1, 1e4) == 0);
}

TEST(resistance_grows_with_the_sink_and_the_speed) {
  const sn::ContactRow& row = sn::contact_row(sn::Kind::powder);
  CHECK(sn::compaction_resistance(row, 0.4, 0.2) > sn::compaction_resistance(row, 0.4, 0.1));
  const double slow = sn::bulldozing_resistance(row, 100, 0.4, 0.2, 1);
  const double fast = sn::bulldozing_resistance(row, 100, 0.4, 0.2, 10);
  CHECK(fast > slow);
  CHECK(sn::bulldozing_resistance(row, 100, 0.4, 0, 10) == 0);
}

TEST(traction_follows_mohr_coulomb_and_janosis_shape) {
  const sn::ContactRow& row = sn::contact_row(sn::Kind::settled);
  const double p = 2e4;
  const double tmax = sn::shear_strength(row, p);
  CHECK(std::fabs(tmax - (row.cohesion + p * std::tan(row.phi * 3.141592653589793 / 180))) <
        1e-6 * tmax);
  // At a slip of one shear modulus the stress is 1 - 1/e of the strength.
  CHECK(std::fabs(sn::shear_at(row, p, row.shear_k) / tmax - (1 - std::exp(-1.0))) < 1e-12);
  // A track's traction rises with slip towards area * tau_max and never passes it.
  double last = 0;
  for (int i = 1; i <= 20; i++) {
    const double f = sn::tread_traction(row, 0.5, p, i * 0.05, 3);
    CHECK(f > last && f < 0.5 * tmax);
    last = f;
  }
  CHECK(sn::tread_traction(row, 0.5, p, 0, 3) == 0);
}

TEST(glide_is_least_near_minus_five_and_planing_lifts_a_fast_contact) {
  for (int k = 0; k < static_cast<int>(sn::Kind::count); k++) {
    const sn::Kind kind = kind_of(k);
    const double best = sn::glide_friction(kind, -5);
    CHECK(sn::glide_friction(kind, -20) > best && sn::glide_friction(kind, 0) > best);
  }
  CHECK(sn::tyre_friction(sn::Kind::ice, 0) < sn::tyre_friction(sn::Kind::ice, -10));
  CHECK(sn::planing_sink(0.3, 0, 15) == 0.3);
  CHECK(std::fabs(sn::planing_sink(0.3, 15, 15) - 0.3 * std::exp(-1.0)) < 1e-12);
}

namespace {

bool env_set(const char* name) {
#ifdef _MSC_VER
  char* v = nullptr;
  size_t n = 0;
  const bool set = _dupenv_s(&v, &n, name) == 0 && v != nullptr;
  std::free(v);
  return set;
#else
  return std::getenv(name) != nullptr;
#endif
}

}  // namespace

TEST(every_kinds_numbers_keep_their_golden) {
  const bool write = env_set("GE_GOLDEN_WRITE");
  std::vector<std::string> got;
  for (int k = 0; k < static_cast<int>(sn::Kind::count); k++) {
    const sn::Kind kind = kind_of(k);
    const sn::ContactRow& row = sn::contact_row(kind);
    const double z = sn::column_sink(kind, 0.5, sn::kind_row(kind).loose, 0.1, 3e4);
    const double values[] = {
        z,
        sn::compaction_resistance(row, 0.4, z),
        sn::bulldozing_resistance(row, sn::kind_row(kind).loose, 0.4, z, 8),
        sn::tread_traction(row, 0.5, 5e3, 0.2, 3),
        sn::glide_friction(kind, -15),
        sn::tyre_friction(kind, -1),
    };
    std::string line(sn::kind_row(kind).name);
    for (const double v : values) line += " " + hex(ge::test::bits(v));
    got.push_back(line);
  }
  if (write) {
    std::ofstream out(ge::test::root_path(kGolden));
    out << "# kind sink compaction bulldozing traction glide tyre (snow_contact_test: 30 kPa on a "
           "10 cm plate in 50 cm of loose snow; a track 0.4 m wide at 8 m/s; 0.5 m2 at 5 kPa "
           "slipping 20 % over 3 m; glide at -15 C; a tyre at -1 C)\n";
    for (const auto& line : got) out << line << "\n";
    return;
  }
  const auto rows = ge::test::golden_words(kGolden);
  CHECK_MSG(rows.size() == got.size(),
            std::string("no golden rows: write ") + kGolden + " with GE_GOLDEN_WRITE=1");
  for (size_t i = 0; i < rows.size() && i < got.size(); i++) {
    std::string want;
    for (const auto& w : rows[i]) want += (want.empty() ? "" : " ") + w;
    CHECK_MSG(got[i] == want, "a contact law moved:\n  got  " + got[i] + "\n  want " + want +
                                  "\nIf the move is meant, rewrite " + kGolden +
                                  " with GE_GOLDEN_WRITE=1 and put the snow-contact lab's tables "
                                  "before and after in the PR.");
  }
}
