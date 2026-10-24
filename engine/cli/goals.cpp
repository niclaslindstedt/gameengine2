// `ge goals free` (goals.hpp): the roadmap's rows read off `docs/goals.md` and the status files at
// the base, at every remote branch and in the working tree.
//
// ge:summary ge goals free and claim: the rows parsed, the branches read, the next free row claimed
// ge:keywords cli ge goals roadmap milestone next-free git-show ls-tree for-each-ref
// ge:skill agent-interface
// ge:test tests/cli_goals_test.cpp
#include "cli/goals.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <set>
#include <thread>

#include "cli/build.hpp"
#include "cli/finding.hpp"
#include "cli/json.hpp"

namespace ge::cli {

namespace {

std::string quoted(const fs::path& p) { return "\"" + p.string() + "\""; }

// The pieces joined in one buffer.
template <class... Piece>
std::string cat(const Piece&... piece) {
  std::string out;
  (out.append(piece), ...);
  return out;
}

std::string first_line(std::string_view text) {
  return std::string(text.substr(0, text.find('\n')));
}

// "M17d" at the start of `s`: its number and suffix; number 0 when `s` does not open with a row.
GoalRow row_id(std::string_view s) {
  GoalRow r;
  if (s.size() < 2 || s[0] != 'M' || !std::isdigit(static_cast<unsigned char>(s[1]))) return r;
  std::size_t i = 1;
  while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
    r.number = r.number * 10 + (s[i] - '0');
    ++i;
  }
  std::size_t j = i;
  while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '.')) ++j;
  r.suffix = std::string(s.substr(i, j - i));
  return r;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

// A row's name: its first bold run, else its first eight words.
std::string title_of(std::string_view cell) {
  const std::size_t b = cell.find("**");
  if (b != std::string_view::npos) {
    const std::size_t e = cell.find("**", b + 2);
    if (e != std::string_view::npos) return std::string(cell.substr(b + 2, e - b - 2));
  }
  std::string out;
  int words = 0;
  for (std::size_t i = 0; i < cell.size() && words < 8; ++i) {
    if (cell[i] == ' ' && (out.empty() || out.back() == ' ')) continue;
    if (cell[i] == ' ') ++words;
    out += cell[i];
  }
  return std::string(trim(out));
}

std::string key_of(const GoalRow& r) { return "M" + std::to_string(r.number) + r.suffix; }

struct Source {
  std::string name;  // the ref, or "the working tree"
  std::vector<GoalRow> rows;
};

// The rows one ref holds: its table's and its status files'.
std::vector<GoalRow> rows_at(const fs::path& repo, const std::string& ref, const fs::path& log) {
  std::vector<GoalRow> rows;
  const std::string git = "git -C " + quoted(repo) + " ";
  const Logged md = run_to_log(git + "show \"" + ref + ":docs/goals.md\"", log);
  if (md.code == 0) rows = goal_rows(md.output);
  const Logged held = run_to_log(git + "show \"" + ref + ":docs/goals/reserved.md\"", log);
  if (held.code == 0) {
    for (GoalRow& r : goal_rows(held.output)) rows.push_back(std::move(r));
  }
  const Logged files = run_to_log(git + "ls-tree --name-only \"" + ref + "\" docs/goals/", log);
  if (files.code == 0) {
    for (std::string_view line : split_lines(files.output)) {
      const std::size_t slash = line.rfind('/');
      const GoalRow r = goal_row_of_file(trim(line.substr(slash + 1)));
      if (r.number != 0 || !r.suffix.empty()) rows.push_back(r);
    }
  }
  return rows;
}

// The working tree's rows, uncommitted ones included.
std::vector<GoalRow> rows_here(const fs::path& repo) {
  std::vector<GoalRow> rows;
  if (const auto md = read_text(repo / "docs" / "goals.md")) rows = goal_rows(*md);
  if (const auto held = read_text(repo / "docs" / "goals" / "reserved.md")) {
    for (GoalRow& r : goal_rows(*held)) rows.push_back(std::move(r));
  }
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(repo / "docs" / "goals", ec)) {
    const GoalRow r = goal_row_of_file(e.path().filename().string());
    if (r.number != 0 || !r.suffix.empty()) rows.push_back(r);
  }
  return rows;
}

}  // namespace

std::vector<GoalRow> goal_rows(std::string_view goals_md) {
  std::vector<GoalRow> out;
  for (std::string_view line : split_lines(goals_md)) {
    line = trim(line);
    if (line.empty() || line.front() != '|') continue;
    line.remove_prefix(1);
    const std::size_t bar = line.find('|');
    if (bar == std::string_view::npos) continue;
    GoalRow r = row_id(trim(line.substr(0, bar)));
    if (r.number == 0 && r.suffix.empty() && trim(line.substr(0, bar)) != "M0") continue;
    if (trim(line.substr(0, bar)).size() != key_of(r).size()) continue;  // "M17 and M18": prose
    std::string_view rest = line.substr(bar + 1);
    r.title = title_of(rest.substr(0, rest.find('|')));
    out.push_back(std::move(r));
  }
  return out;
}

GoalRow goal_row_of_file(std::string_view file_name) {
  if (file_name.size() < 4 || file_name.substr(file_name.size() - 3) != ".md") return {};
  const std::string_view stem = file_name.substr(0, file_name.size() - 3);
  GoalRow r = row_id(stem);
  if (key_of(r).size() != stem.size() || (r.number == 0 && stem != "M0" && stem.rfind("M0.", 0)))
    return {};
  return r;
}

int next_free_number(const std::vector<int>& taken) {
  int most = -1;
  for (const int n : taken) most = std::max(most, n);
  return most + 1;
}

std::string next_free_letter(const std::vector<std::string>& taken) {
  char most = 0;
  for (const std::string& s : taken) {
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') most = std::max(most, s[0]);
  }
  if (most == 'z') return "";
  return std::string(1, most == 0 ? 'a' : static_cast<char>(most + 1));
}

namespace {

// Every row a ref, branch or the working tree holds that the base does not, and the next free.
struct Scan {
  std::size_t branches = 0;  // the remote branches read besides the base
  int base_most = -1;        // the highest number on the base (of `number`'s rows, when one)
  std::map<std::pair<int, std::string>, std::vector<std::pair<std::string, std::string>>> claims;
  std::string next;
};

Scan scan(const fs::path& repo, int number, const std::string& base, const fs::path& log) {
  Scan out;
  const std::string git = "git -C " + quoted(repo) + " ";
  std::vector<Source> sources;
  sources.push_back({base, rows_at(repo, base, log)});
  const Logged refs =
      run_to_log(git + "for-each-ref \"--format=%(refname:short)\" refs/remotes/origin", log);
  for (std::string_view ref : split_lines(refs.output)) {
    ref = trim(ref);
    if (ref.empty() || ref == base || ref == "origin/HEAD" || ref == "origin") continue;
    sources.push_back({std::string(ref), rows_at(repo, std::string(ref), log)});
  }
  sources.push_back({"the working tree", rows_here(repo)});
  out.branches = sources.size() - 2;

  for (const GoalRow& r : sources[0].rows) {
    if (number < 0 || r.number == number) out.base_most = std::max(out.base_most, r.number);
  }
  std::set<std::string> on_base;
  for (const GoalRow& r : sources[0].rows) on_base.insert(key_of(r));
  std::vector<int> numbers;
  std::vector<std::string> letters;
  for (const Source& s : sources) {
    for (const GoalRow& r : s.rows) {
      if (number >= 0 && r.number != number) continue;
      numbers.push_back(r.number);
      if (number >= 0) letters.push_back(r.suffix);
      // A top-level number at or under the base's highest is the base's, renamed or retired.
      if (on_base.count(key_of(r)) ||
          (number < 0 && (!r.suffix.empty() || r.number <= out.base_most)))
        continue;
      auto& who = out.claims[{r.number, r.suffix}];
      if (std::none_of(who.begin(), who.end(), [&](const auto& w) { return w.first == s.name; }))
        who.emplace_back(s.name, r.title);
    }
  }
  out.next = number < 0 ? "M" + std::to_string(next_free_number(numbers))
                        : "M" + std::to_string(number) + next_free_letter(letters);
  return out;
}

// The number a row such as "M17" names (-1 for none), or a finding when it is not a row.
bool row_number(const std::string& row, int& number, Outcome& out) {
  number = -1;
  if (row.empty()) return true;
  const GoalRow r = row_id(row);
  if (r.number == 0 && row != "M0") {
    out.findings.push_back({Severity::error, "\"" + row + "\" is not a milestone row", "",
                            "name one as the roadmap does: ge goals free M17"});
    out.exit = 2;
    return false;
  }
  number = r.number;
  return true;
}

// Commits `docs/goals/reserved.md` alone, the message read from a file so no shell quotes it.
bool commit_reserved(const fs::path& repo, const std::string& message, const fs::path& log) {
  const fs::path msg = log.parent_path() / "ge-goals-message.txt";
  std::string error;
  if (!write_text(msg, message + "\n", error)) return false;
  const std::string git = "git -C " + quoted(repo) + " ";
  // Added first, so a reserved table this claim wrote fresh is committed too.
  return run_to_log(git + "add docs/goals/reserved.md", log).code == 0 &&
         run_to_log(git + "commit --quiet -F " + quoted(msg) + " -- docs/goals/reserved.md", log)
                 .code == 0;
}

}  // namespace

Outcome run_goals_free(const fs::path& repo, const std::string& row, const std::string& base,
                       bool fetch) {
  Outcome out;
  const fs::path log = repo / "build" / "ge-goals.log";
  std::error_code ec;
  fs::create_directories(log.parent_path(), ec);
  const std::string git = "git -C " + quoted(repo) + " ";
  int number = -1;
  if (!row_number(row, number, out)) return out;
  if (fetch && run_to_log(git + "fetch --quiet origin", log).code != 0) {
    out.findings.push_back({Severity::warn,
                            "git fetch origin failed; the branches are as last fetched",
                            log.string(), "check the remote: git remote -v"});
  }
  if (rows_at(repo, base, log).empty()) {
    out.findings.push_back({Severity::error, "no roadmap rows at " + base, "docs/goals.md",
                            "fetch it (git fetch origin), or name --base"});
    return out;
  }
  const Scan sc = scan(repo, number, base, log);

  std::vector<std::string> rows_json;
  out.text += "  read: " + base + ", " + std::to_string(sc.branches) +
              " other remote branches and the working tree\n";
  if (number < 0) out.text += "  highest on " + base + ": M" + std::to_string(sc.base_most) + "\n";
  if (sc.claims.empty()) out.text += "  no branch holds a row " + base + " does not\n";
  for (const auto& [id, who] : sc.claims) {
    const std::string key = "M" + std::to_string(id.first) + id.second;
    for (const auto& [where, title] : who) {
      out.text.append("  ").append(key).append("  ").append(where);
      if (!title.empty()) out.text.append("  ").append(title);
      out.text += '\n';
      rows_json.push_back(json_obj(
          {{"row", json_str(key)}, {"where", json_str(where)}, {"title", json_str(title)}}));
    }
  }
  out.text += "  next free: " + sc.next + "\n";
  out.result = json_obj(
      {{"base", json_str(base)}, {"claimed", json_arr(rows_json)}, {"next", json_str(sc.next)}});
  out.summary =
      sc.next + " is free (" + std::to_string(sc.claims.size()) + " rows held on branches only)";
  out.next = "claim it before you build on it: ge goals claim \"<what the row is>\"" +
             std::string(number < 0 ? "" : " --under=" + row) +
             " pushes its line to your branch at once";
  return out;
}

std::string with_reserved_row(std::string_view reserved_md, const std::string& key,
                              const std::string& title) {
  std::string md(reserved_md);
  if (md.find("| Row | What |") == std::string::npos) {
    md = "# Reserved roadmap rows\n\n"
         "Rows promised to a piece of work before any branch carries them, so `ge goals free` "
         "never\ndeals the same number twice. A row leaves this table in the pull request that "
         "adds it to\n[`../goals.md`](../goals.md).\n\n"
         "| Row | What |\n| --- | --- |\n";
  }
  if (!md.empty() && md.back() != '\n') md += '\n';
  return md.append("| ").append(key).append(" | **").append(title).append("** |\n");
}

std::string without_reserved_row(std::string_view reserved_md, const std::string& key) {
  std::string out;
  const std::string open = "| " + key + " |";
  for (std::string_view line : split_lines(reserved_md)) {
    if (line.rfind(open, 0) == 0) continue;
    out.append(line).append("\n");
  }
  return out;
}

std::size_t claim_winner(const std::vector<Claimant>& claimants) {
  std::size_t best = 0;
  for (std::size_t i = 1; i < claimants.size(); ++i) {
    const Claimant& a = claimants[i];
    const Claimant& b = claimants[best];
    if (a.time < b.time || (a.time == b.time && a.ref < b.ref)) best = i;
  }
  return best;
}

Outcome run_goals_claim(const fs::path& repo, const std::string& title, const std::string& under,
                        const std::string& base) {
  Outcome out;
  const fs::path log = repo / "build" / "ge-goals.log";
  std::error_code ec;
  fs::create_directories(log.parent_path(), ec);
  const std::string git = "git -C " + quoted(repo) + " ";
  auto fail = [&](const std::string& what, const std::string& next) {
    out.findings.push_back({Severity::error, what, log.string(), next});
    return out;
  };
  int number = -1;
  if (!row_number(under, number, out)) return out;
  const std::string branch =
      std::string(trim(first_line(run_to_log(git + "rev-parse --abbrev-ref HEAD", log).output)));
  if (branch.empty() || branch == "HEAD")
    return fail("ge goals claim pushes to your branch, and HEAD is detached",
                "check out your work's branch, then ge goals claim again");
  const fs::path reserved = repo / "docs" / "goals" / "reserved.md";
  const std::string mine = "origin/" + branch;
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (run_to_log(git + "fetch --quiet origin", log).code != 0)
      return fail("git fetch origin failed, so the branches' rows cannot be read",
                  "check the remote (git remote -v), then ge goals claim again");
    const std::string key = scan(repo, number, base, log).next;
    if (key.size() < 2 || key == "M" + std::to_string(number))
      return fail("no letter is free under " + under, "claim a top-level row: drop --under");
    const std::string before = read_text(reserved).value_or("");
    std::string error;
    if (!write_text(reserved, with_reserved_row(before, key, title), error))
      return fail("could not write " + reserved.string() + ": " + error,
                  "check the tree is writable");
    const std::string msg = cat("docs(goals): reserve ", key, " for ", title);
    if (!commit_reserved(repo, msg, log))
      return fail("git commit of the claim failed", "read the log, then ge goals claim again");
    const Logged pushed = run_to_log(cat(git, "push --quiet origin HEAD:refs/heads/", branch), log);
    if (pushed.code != 0)
      return fail("the claim's push was refused: " + first_line(pushed.output),
                  "bring the branch up to date (git pull --rebase origin " + branch +
                      "), then ge goals claim again; the claim's commit is on HEAD");
    // Wait out the window in which a claim pushed just before this one is not visible yet: a
    // claim seen later than this was committed later, so both sides agree who came first.
    std::this_thread::sleep_for(std::chrono::seconds(5));
    run_to_log(git + "fetch --quiet origin", log);
    const Scan after = scan(repo, number, base, log);
    std::vector<Claimant> claimants;
    const auto ct = [&](const std::string& ref) {
      const std::string t = std::string(
          trim(first_line(run_to_log(cat(git, "log --reverse --format=%ct \"-G^\\| ", key,
                                         " \\|\" ", ref, " -- docs/goals.md docs/goals/"),
                                     log)
                              .output)));
      return t.empty() ? 0LL : std::stoll(t);
    };
    claimants.push_back({mine, ct("HEAD")});
    const GoalRow id = row_id(key);
    if (const auto it = after.claims.find({id.number, id.suffix}); it != after.claims.end()) {
      for (const auto& [where, held] : it->second) {
        if (where == mine || where == "the working tree") continue;
        claimants.push_back({where, ct(where)});
      }
    }
    const Claimant& won = claimants[claim_winner(claimants)];
    if (won.ref == mine) {
      out.text += cat("  claimed: ", key, "  ", title, "\n  pushed: ", mine, "\n");
      out.result = json_obj({{"row", json_str(key)},
                             {"title", json_str(title)},
                             {"branch", json_str(mine)},
                             {"attempts", std::to_string(attempt + 1)}});
      out.summary = cat(key, " is yours (", title, ")");
      out.next = cat("add ", key, " to docs/goals.md with its status file docs/goals/", key,
                     ".md, and drop its line from docs/goals/reserved.md in the same pull request");
      return out;
    }
    out.text += "  " + key + " lost to " + won.ref + ", which claimed it first; trying the next\n";
    std::string ignored;
    write_text(reserved, without_reserved_row(read_text(reserved).value_or(""), key), ignored);
    commit_reserved(repo, "docs(goals): give up " + key + ", claimed first on " + won.ref, log);
    run_to_log(cat(git, "push --quiet origin HEAD:refs/heads/", branch), log);
  }
  return fail("three rows in a row were claimed first by other branches",
              "ge goals free --fetch to see who holds what, then ge goals claim again");
}

}  // namespace ge::cli
