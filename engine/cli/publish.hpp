// `ge lab publish <id>... [--before=<tag>] [--width=<px>] [--out=<file>]`: a lab's last run as ONE
// self-contained HTML page for a report or a pull request. Every picture is shrunk to the width
// (720 by default), cut to 256 colours and inlined as a GIF (lab/gif.hpp), with its title, caption
// and what to look for under it; then the metrics, the tables and the notes. With --before, the
// kept run `previews/<id>@<tag>/` (`ge lab <id> --tag=<tag>`) stands beside it: each picture left
// and right, each metric with its before and its change. The page follows the published-page
// contract (a short title, colour tokens with a dark theme, a 16 px gutter, no outside file), so
// it is ready to publish as it is, and it says when it is too big to.
//
//   ge lab publish level
//   ge lab publish world --before=before --width=560
//   ge lab publish water lake --before=before    several labs on one page (previews/publish.html)
//
// ge:summary ge lab publish: a lab's run on one self-contained page, before beside after, to share
// ge:keywords cli ge lab publish page html review report pull-request pictures gif inline
// ge:keywords before after side-by-side artifact share contact-sheet publish_html base64
// ge:skill agent-interface labs
// ge:test tests/cli_publish_test.cpp
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cli/outcome.hpp"

namespace ge::cli {

namespace fs = std::filesystem;

struct PublishOptions {
  std::vector<std::string> ids;  // one lab, or several on one page
  std::string before;            // a kept run's tag, or ""
  int width = 720;               // the pictures' width on the page
  std::string out;               // the page's path; "" for previews/<id>/publish.html
};

// What the page shows, read off a run's report.json (and the kept run's).
struct PageShot {
  std::string id, title, caption, look_for;
  std::string gif, before_gif;  // base64, "" when the run has no such picture
  int width = 0, height = 0;
};
struct PageMetric {
  std::string key, value, unit, before;  // before "" when there is no kept run, or no such key
};
struct PageTable {
  std::string title;
  std::vector<std::string> columns;
  std::vector<std::vector<std::string>> rows;
};
struct Page {
  std::string lab, kind, question, before_tag;
  std::vector<std::pair<std::string, std::string>> inputs;
  std::vector<PageShot> shots;
  std::vector<PageMetric> metrics;
  std::vector<PageTable> tables;
  std::vector<std::string> notes;
};

std::string base64(std::string_view bytes);
std::string html_escape(std::string_view text);
// The page as HTML; a table longer than `max_rows` shows its first rows and says how many more.
std::string publish_html(const Page& page, size_t max_rows = 60);
// Several labs' runs on one page, one after another.
std::string publish_html(const std::vector<Page>& pages, size_t max_rows = 60);

Outcome run_lab_publish(const fs::path& repo, const PublishOptions& o);

}  // namespace ge::cli
