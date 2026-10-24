// `ge goals free` (engine/cli/goals.hpp), the part that needs no git: the roadmap's rows read off
// `docs/goals.md`'s tables and the status files' names, and the next free number and letter.
//
// ge:summary Test: ge goals free's rows parsed off the roadmap, and the next free row
// ge:keywords test cli goals roadmap milestone goal_rows next_free_number next_free_letter
// ge:skill testing agent-interface
#include <string>

#include "check.hpp"
#include "cli/goals.hpp"

using namespace ge::cli;

TEST(the_roadmaps_table_rows_are_read_with_their_names) {
  const std::string md =
      "# Goals\n\n"
      "| Row | What | Status |\n"
      "| --- | --- | --- |\n"
      "| M0 | **The core** (`engine/core`): the PRNG | [done](goals/M0.md) |\n"
      "| M0.1 | The libm vendored and pinned | [done](goals/M0.1.md) |\n"
      "| M17 | **Multiplayer** (`engine/net`) | [open](goals/M17.md) |\n"
      "| M17d | **Sessions**: lobby, teams | [open](goals/M17d.md) |\n"
      "| M17 and M18 | prose in a table, not a row | |\n"
      "Text that names M24 outside a table.\n";
  const auto rows = goal_rows(md);
  CHECK(rows.size() == 4);
  CHECK(rows[0].number == 0 && rows[0].suffix.empty() && rows[0].title == "The core");
  CHECK(rows[1].number == 0 && rows[1].suffix == ".1");
  CHECK(rows[1].title == "The libm vendored and pinned");
  CHECK(rows[2].number == 17 && rows[2].suffix.empty() && rows[2].title == "Multiplayer");
  CHECK(rows[3].number == 17 && rows[3].suffix == "d" && rows[3].title == "Sessions");
}

TEST(a_status_files_name_is_its_row) {
  CHECK(goal_row_of_file("M17d.md").number == 17);
  CHECK(goal_row_of_file("M17d.md").suffix == "d");
  CHECK(goal_row_of_file("M0.md").number == 0);
  CHECK(goal_row_of_file("M8e2.md").suffix == "e2");
  CHECK(goal_row_of_file("ge-backlog.md").number == 0);
  CHECK(goal_row_of_file("ge-backlog.md").suffix.empty());
  CHECK(goal_row_of_file("M17.txt").number == 0);
}

TEST(the_next_free_row_is_one_past_the_highest_taken) {
  CHECK(next_free_number({0, 3, 29, 17}) == 30);
  CHECK(next_free_number({}) == 0);
  CHECK(next_free_letter({"", "a", "e2", "c"}) == "f");
  CHECK(next_free_letter({""}) == "a");
  CHECK(next_free_letter({"z"}).empty());
}

TEST(a_claim_adds_its_line_to_the_reserved_table_and_gives_it_up_again) {
  const std::string held =
      "# Reserved roadmap rows\n\n| Row | What |\n| --- | --- |\n| M27 | **Fonts** |\n";
  const std::string with = with_reserved_row(held, "M34", "Lava and slime");
  CHECK(with == held + "| M34 | **Lava and slime** |\n");
  CHECK(without_reserved_row(with, "M34") == held);
  CHECK(without_reserved_row(with, "M3") == with);  // M3 is not M34
  const std::string fresh = with_reserved_row("", "M34", "Lava");
  CHECK(fresh.find("| Row | What |\n| --- | --- |\n| M34 | **Lava** |\n") != std::string::npos);
  CHECK(goal_rows(fresh).size() == 1 && goal_rows(fresh)[0].number == 34);
}

TEST(the_earliest_claim_wins_and_a_tie_goes_to_the_first_ref_by_name) {
  CHECK(claim_winner({{"origin/b", 100}, {"origin/a", 101}}) == 0);
  CHECK(claim_winner({{"origin/b", 100}, {"origin/a", 100}}) == 1);
  CHECK(claim_winner({{"origin/c", 0}}) == 0);
}
