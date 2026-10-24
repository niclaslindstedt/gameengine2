// `ge lab publish` (publish.hpp): the run's report.json read flat, its pictures shrunk and inlined
// as GIFs, the kept run read the same way, and the page written.
//
// ge:summary ge lab publish's definitions: the report read, the pictures inlined, the page built
// ge:keywords cli ge lab publish html page base64 escape report-json flat metrics tables delta
// ge:skill agent-interface labs
// ge:test tests/cli_publish_test.cpp
#include "cli/publish.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>

#include "cli/finding.hpp"
#include "cli/json.hpp"
#include "lab/gif.hpp"
#include "lab/png.hpp"
#include "play/json_read.hpp"

namespace ge::cli {

namespace {

constexpr size_t kMaxPage = 15u << 20;  // a published page's limit is 16 MB: a margin under it

std::optional<double> number_of(const std::string& s) {
  if (s.empty()) return std::nullopt;
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  if (end == s.c_str() || *end != '\0') return std::nullopt;
  return v;
}

// A metric's value as the page shows it: six significant figures, the text as it is otherwise.
std::string shown(const std::string& s) {
  const auto v = number_of(s);
  if (!v) return s;
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.6g", *v);
  return buf;
}

std::string change_of(const std::string& before, const std::string& after) {
  const auto b = number_of(before), a = number_of(after);
  if (!b || !a) return before == after ? "" : "changed";
  if (*a == *b) return "";
  char buf[48];
  if (*b != 0) {
    std::snprintf(buf, sizeof buf, "%+.4g (%+.1f%%)", *a - *b,
                  (*a - *b) / (*b < 0 ? -*b : *b) * 100);
  } else {
    std::snprintf(buf, sizeof buf, "%+.4g", *a - *b);
  }
  return buf;
}

std::string at(const play::FlatJson& j, const std::string& key) {
  const auto it = j.find(key);
  return it == j.end() ? "" : it->second.text;
}

bool has(const play::FlatJson& j, const std::string& key) { return j.count(key) > 0; }

// A run's report.json, flat; false with `error` when it is missing or not JSON.
bool read_report(const fs::path& dir, play::FlatJson& out, std::string& error) {
  const auto text = read_text(dir / "report.json");
  if (!text) {
    error = "no " + (dir / "report.json").string();
    return false;
  }
  return play::read_flat(*text, out, error, true);
}

// The run's picture `file` without its caption band (the shot's own size, `crop_w` by `crop_h`,
// at its top left: the page sets the caption as text), shrunk, as a base64 GIF; "" when it
// cannot be read.
std::string gif_of(const fs::path& file, int crop_w, int crop_h, int width, int& w, int& h) {
  auto picture = lab::read_png(file.string());
  if (!picture) return "";
  if (crop_w > 0 && crop_h > 0 && crop_w <= picture->width && crop_h <= picture->height &&
      (crop_w < picture->width || crop_h < picture->height)) {
    lab::Picture cut{crop_w, crop_h,
                     std::vector<uint8_t>(static_cast<size_t>(crop_w) * crop_h * 4)};
    for (int y = 0; y < crop_h; ++y) {
      const auto row = picture->rgba.begin() + static_cast<std::ptrdiff_t>(y) * picture->width * 4;
      std::copy(row, row + static_cast<std::ptrdiff_t>(crop_w) * 4,
                cut.rgba.begin() + static_cast<std::ptrdiff_t>(y) * crop_w * 4);
    }
    picture = std::move(cut);
  }
  const lab::Picture small = lab::shrink(*picture, width);
  w = small.width;
  h = small.height;
  const std::vector<uint8_t> gif = lab::encode_gif(small);
  return base64(std::string_view(reinterpret_cast<const char*>(gif.data()), gif.size()));
}

}  // namespace

std::string base64(std::string_view bytes) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((bytes.size() + 2) / 3 * 4);
  for (size_t i = 0; i < bytes.size(); i += 3) {
    const auto byte = [&](size_t k) {
      return k < bytes.size() ? static_cast<uint32_t>(static_cast<uint8_t>(bytes[k])) : 0u;
    };
    const uint32_t v = byte(i) << 16 | byte(i + 1) << 8 | byte(i + 2);
    out += kAlphabet[(v >> 18) & 63];
    out += kAlphabet[(v >> 12) & 63];
    out += i + 1 < bytes.size() ? kAlphabet[(v >> 6) & 63] : '=';
    out += i + 2 < bytes.size() ? kAlphabet[v & 63] : '=';
  }
  return out;
}

std::string html_escape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        out += c;
    }
  }
  return out;
}

namespace {

// The document's head and its styles, opening <main>.
std::string page_head(const std::string& title) {
  std::string h;
  h += "<!doctype html>\n<meta charset=\"utf-8\">\n"
       "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
  h.append("<title>").append(html_escape(title)).append("</title>\n");
  h += R"(<style>
:root{--bg:#f6f7f8;--fg:#1d2226;--muted:#5e6970;--line:#d7dde1;--panel:#ffffff;--up:#2f7d4f;--down:#b0453a;
--body:system-ui,-apple-system,"Segoe UI",sans-serif;--mono:ui-monospace,"SF Mono",Menlo,Consolas,monospace}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){--bg:#121517;--fg:#e3e8eb;--muted:#97a3aa;--line:#2a3136;--panel:#1a1f22;--up:#62c08a;--down:#e5806f;color-scheme:dark}}
:root[data-theme="dark"]{--bg:#121517;--fg:#e3e8eb;--muted:#97a3aa;--line:#2a3136;--panel:#1a1f22;--up:#62c08a;--down:#e5806f;color-scheme:dark}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.5 var(--body);padding:24px max(16px,env(safe-area-inset-right,0px)) 40px max(16px,env(safe-area-inset-left,0px))}
main{max-width:1100px;margin:0 auto;display:grid;grid-template-columns:minmax(0,1fr);gap:28px}
h1{font-size:1.7rem;margin:0}h2{font-size:1.15rem;margin:0}
p{margin:0;color:var(--muted);max-width:75ch}
code{font-family:var(--mono);font-size:.88em}
section{display:grid;gap:12px;border-top:1px solid var(--line);padding-top:18px;min-width:0}
.pair{display:grid;gap:12px;grid-template-columns:repeat(auto-fit,minmax(min(100%,320px),1fr))}
figure{margin:0;display:grid;gap:6px;min-width:0}
figure img{width:100%;height:auto;image-rendering:pixelated;border-radius:4px;border:1px solid var(--line);background:var(--panel)}
figcaption{font:600 .75rem var(--mono);letter-spacing:.04em;text-transform:uppercase;color:var(--muted)}
.look{color:var(--fg)}
.scroll{overflow-x:auto;border:1px solid var(--line);border-radius:6px;background:var(--panel)}
table{border-collapse:collapse;font:.85rem/1.4 var(--mono);font-variant-numeric:tabular-nums;width:100%}
th,td{padding:5px 10px;text-align:left;white-space:nowrap;border-bottom:1px solid var(--line)}
th{color:var(--muted);font-weight:600}
td.up{color:var(--up)}td.down{color:var(--down)}
.inputs{display:flex;flex-wrap:wrap;gap:6px}
.inputs code{background:var(--panel);border:1px solid var(--line);border-radius:4px;padding:2px 6px}
</style>
<main>
)";
  return h;
}

// One lab's run: its header, pictures, metrics, tables and notes.
std::string page_body(const Page& page, size_t max_rows) {
  const bool paired = !page.before_tag.empty();
  std::string h;
  h.append("<header style=\"display:grid;gap:8px\"><h1>Lab <code>")
      .append(html_escape(page.lab))
      .append("</code></h1>");
  if (!page.question.empty()) h.append("<p>").append(html_escape(page.question)).append("</p>");
  if (!page.inputs.empty()) {
    h += "<div class=\"inputs\">";
    for (const auto& [k, v] : page.inputs)
      h.append("<code>--")
          .append(html_escape(k))
          .append("=")
          .append(html_escape(v))
          .append("</code>");
    h += "</div>";
  }
  if (paired)
    h.append("<p>Left: the kept run <code>")
        .append(html_escape(page.before_tag))
        .append("</code>. Right: this run.</p>");
  h += "</header>\n";

  for (const PageShot& s : page.shots) {
    h.append("<section><h2>").append(html_escape(s.title.empty() ? s.id : s.title)).append("</h2>");
    h += "<div class=\"pair\">";
    const auto figure = [&](const std::string& gif, const std::string& label) {
      h += "<figure>";
      if (paired) h.append("<figcaption>").append(label).append("</figcaption>");
      if (gif.empty()) {
        h += "<p>No such picture in this run.</p>";
      } else {
        h.append("<img alt=\"")
            .append(html_escape(s.title))
            .append("\" width=\"")
            .append(std::to_string(s.width))
            .append("\" height=\"")
            .append(std::to_string(s.height))
            .append("\" style=\"max-width:")
            .append(std::to_string(s.width * 2))
            .append("px\" src=\"data:image/gif;base64,")
            .append(gif)
            .append("\">");
      }
      h += "</figure>";
    };
    if (paired) figure(s.before_gif, "before");
    figure(s.gif, paired ? "after" : "");
    h += "</div>";
    if (!s.caption.empty()) h.append("<p>").append(html_escape(s.caption)).append("</p>");
    if (!s.look_for.empty())
      h.append("<p class=\"look\"><strong>Look for:</strong> ")
          .append(html_escape(s.look_for))
          .append("</p>");
    h += "</section>\n";
  }

  if (!page.metrics.empty()) {
    h += "<section><h2>Metrics</h2><div class=\"scroll\"><table><tr><th>metric</th>";
    if (paired) h += "<th>before</th>";
    h += "<th>value</th><th>unit</th>";
    if (paired) h += "<th>change</th>";
    h += "</tr>";
    for (const PageMetric& m : page.metrics) {
      h.append("<tr><td>").append(html_escape(m.key)).append("</td>");
      if (paired) h.append("<td>").append(html_escape(shown(m.before))).append("</td>");
      h.append("<td>").append(html_escape(shown(m.value))).append("</td><td>");
      h.append(html_escape(m.unit)).append("</td>");
      if (paired) {
        const std::string c = change_of(m.before, m.value);
        h.append("<td class=\"")
            .append(c.empty() ? "" : (c[0] == '-' ? "down" : "up"))
            .append("\">")
            .append(html_escape(c))
            .append("</td>");
      }
      h += "</tr>";
    }
    h += "</table></div></section>\n";
  }

  for (const PageTable& t : page.tables) {
    h.append("<section><h2>")
        .append(html_escape(t.title))
        .append("</h2><div class=\"scroll\"><table><tr>");
    for (const std::string& c : t.columns) h.append("<th>").append(html_escape(c)).append("</th>");
    h += "</tr>";
    for (size_t r = 0; r < t.rows.size() && r < max_rows; ++r) {
      h += "<tr>";
      for (const std::string& c : t.rows[r])
        h.append("<td>").append(html_escape(c)).append("</td>");
      h += "</tr>";
    }
    h += "</table></div>";
    if (t.rows.size() > max_rows)
      h.append("<p>")
          .append(std::to_string(t.rows.size() - max_rows))
          .append(" more rows in the run's report.json.</p>");
    h += "</section>\n";
  }

  if (!page.notes.empty()) {
    h += "<section><h2>Notes</h2>";
    for (const std::string& n : page.notes) h.append("<p>").append(html_escape(n)).append("</p>");
    h += "</section>\n";
  }
  return h;
}

}  // namespace

std::string publish_html(const std::vector<Page>& pages, size_t max_rows) {
  std::string h = page_head(pages.size() == 1 ? pages[0].lab + " lab run" : "Lab runs");
  for (size_t i = 0; i < pages.size(); ++i) {
    if (i > 0) h += "<hr style=\"width:100%;border:0;border-top:3px solid var(--line)\">\n";
    h += page_body(pages[i], max_rows);
  }
  h += "</main>\n";
  return h;
}

std::string publish_html(const Page& page, size_t max_rows) {
  return publish_html(std::vector<Page>{page}, max_rows);
}

namespace {

// One lab's run (and its kept run) read into `page`; false, with the finding in `out`, when the
// run or the kept run is missing.
bool read_page(const fs::path& repo, const std::string& id, const PublishOptions& o, Page& page,
               int& unreadable, Outcome& out) {
  const fs::path dir = repo / "previews" / id;
  play::FlatJson run;
  std::string error;
  if (!read_report(dir, run, error)) {
    out.findings.push_back({Severity::error, "no run of lab " + id + " to publish: " + error,
                            dir.string(), "run it first: ge lab " + id});
    return false;
  }
  play::FlatJson kept;
  const fs::path kept_dir = repo / "previews" / (id + "@" + o.before);
  bool paired = !o.before.empty();
  if (paired && !read_report(kept_dir, kept, error)) {
    // One lab without its kept run is a mistake; one of several is shown alone, and said so.
    const bool alone = o.ids.size() > 1;
    out.findings.push_back({alone ? Severity::warn : Severity::error,
                            "no kept run " + o.before + " of lab " + id +
                                (alone ? std::string(": shown without one") : std::string()),
                            kept_dir.string(),
                            "keep one first: ge lab " + id + " --tag=" + o.before +
                                " (before the change), then publish with --before=" + o.before});
    if (!alone) return false;
    paired = false;
    kept.clear();
  }

  page.lab = id;
  page.kind = at(run, "kind");
  page.question = at(run, "question");
  page.before_tag = paired ? o.before : "";
  for (const auto& [k, v] : run) {
    if (k.rfind("inputs.", 0) == 0) page.inputs.emplace_back(k.substr(7), v.text);
  }
  // The kept run's pictures by shot id.
  std::map<std::string, std::string> kept_file;  // the shot's id to its "shots.<i>." prefix
  for (int i = 0; has(kept, "shots." + std::to_string(i) + ".id"); ++i) {
    const std::string p = "shots." + std::to_string(i) + ".";
    kept_file[at(kept, p + "id")] = p;
  }
  for (int i = 0; has(run, "shots." + std::to_string(i) + ".id"); ++i) {
    const std::string p = "shots." + std::to_string(i) + ".";
    PageShot s;
    s.id = at(run, p + "id");
    s.title = at(run, p + "title");
    s.caption = at(run, p + "caption");
    s.look_for = at(run, p + "look_for");
    const auto size_at = [](const play::FlatJson& j, const std::string& key) {
      const auto v = number_of(at(j, key));
      return v ? static_cast<int>(*v) : 0;
    };
    s.gif = gif_of(dir / at(run, p + "file"), size_at(run, p + "width"), size_at(run, p + "height"),
                   o.width, s.width, s.height);
    if (s.gif.empty()) ++unreadable;
    if (const auto it = kept_file.find(s.id); it != kept_file.end()) {
      int w = 0, hh = 0;
      s.before_gif =
          gif_of(kept_dir / at(kept, it->second + "file"), size_at(kept, it->second + "width"),
                 size_at(kept, it->second + "height"), o.width, w, hh);
    }
    page.shots.push_back(std::move(s));
  }
  std::map<std::string, std::string> kept_value;
  for (int i = 0; has(kept, "metrics." + std::to_string(i) + ".key"); ++i) {
    const std::string p = "metrics." + std::to_string(i) + ".";
    kept_value[at(kept, p + "key")] = at(kept, p + "value");
  }
  for (int i = 0; has(run, "metrics." + std::to_string(i) + ".key"); ++i) {
    const std::string p = "metrics." + std::to_string(i) + ".";
    PageMetric m{at(run, p + "key"), at(run, p + "value"), at(run, p + "unit"), ""};
    if (const auto it = kept_value.find(m.key); it != kept_value.end()) m.before = it->second;
    page.metrics.push_back(std::move(m));
  }
  for (int t = 0; has(run, "tables." + std::to_string(t) + ".title"); ++t) {
    const std::string p = "tables." + std::to_string(t) + ".";
    PageTable table{at(run, p + "title"), {}, {}};
    for (int c = 0; has(run, p + "columns." + std::to_string(c)); ++c)
      table.columns.push_back(at(run, p + "columns." + std::to_string(c)));
    for (int r = 0; has(run, p + "rows." + std::to_string(r) + ".0"); ++r) {
      std::vector<std::string> row;
      for (int c = 0; has(run, p + "rows." + std::to_string(r) + "." + std::to_string(c)); ++c)
        row.push_back(at(run, p + "rows." + std::to_string(r) + "." + std::to_string(c)));
      table.rows.push_back(std::move(row));
    }
    page.tables.push_back(std::move(table));
  }
  for (int i = 0; has(run, "notes." + std::to_string(i)); ++i)
    page.notes.push_back(at(run, "notes." + std::to_string(i)));

  return true;
}

}  // namespace

Outcome run_lab_publish(const fs::path& repo, const PublishOptions& o) {
  Outcome out;
  std::vector<Page> pages;
  int unreadable = 0;
  size_t shots = 0, metrics = 0, tables = 0;
  for (const std::string& id : o.ids) {
    Page page;
    if (!read_page(repo, id, o, page, unreadable, out)) return out;
    shots += page.shots.size();
    metrics += page.metrics.size();
    tables += page.tables.size();
    pages.push_back(std::move(page));
  }
  std::string error;
  const std::string name = o.ids.size() == 1 ? o.ids[0] : "the labs";
  const fs::path dir = o.ids.size() == 1 ? repo / "previews" / o.ids[0] : repo / "previews";
  const std::string html = publish_html(pages);
  const fs::path file = o.out.empty() ? dir / "publish.html" : fs::path(o.out);
  if (!write_text(file, html, error)) {
    out.findings.push_back({Severity::error, "could not write the page: " + error, file.string(),
                            "check the directory is writable, or name another with --out"});
    return out;
  }
  if (unreadable > 0)
    out.findings.push_back({Severity::warn,
                            std::to_string(unreadable) + " of the run's pictures could not be read",
                            dir.string(), "rerun the lab, then publish again"});
  const double mb = static_cast<double>(html.size()) / (1u << 20);
  char size[32];
  std::snprintf(size, sizeof size, "%.1f MB", mb);
  if (html.size() > kMaxPage)
    out.findings.push_back(
        {Severity::error,
         std::string("the page is ") + size + ", past the 15 MB a published page may be",
         file.string(),
         "shrink it: --width=" + std::to_string(o.width / 2) +
             ", or run the lab with fewer shots"});
  out.text.append("  page: ").append(file.string()).append("  (").append(size).append(")\n");
  out.text.append("  pictures: ").append(std::to_string(shots));
  if (!o.before.empty()) out.text.append(", each beside the kept run ").append(o.before);
  out.text.append("\n  metrics: ").append(std::to_string(metrics));
  out.text.append(", tables: ").append(std::to_string(tables)).append("\n");
  out.result = json_obj({{"page", json_str(file.string())},
                         {"bytes", json_num(static_cast<long long>(html.size()))},
                         {"shots", json_num(static_cast<long long>(shots))},
                         {"before", json_str(o.before)}});
  out.summary = name + " on one page, " + size;
  out.next = "publish " + file.string() +
             " as it is (an Artifact page), and link it from the report and the PR";
  return out;
}

}  // namespace ge::cli
