// `ge lab publish` (engine/cli/publish.hpp): the page's pieces without a lab run: base64, the
// escaping, a page with and without a kept run, and a long table cut short.
//
// ge:summary Test: ge lab publish's page, its escaping, its base64 and its before-and-after
// ge:keywords test cli lab publish html base64 escape before after metric change
// ge:skill testing agent-interface
#include <string>

#include "check.hpp"
#include "cli/publish.hpp"

using namespace ge::cli;

namespace {

bool has(const std::string& s, const std::string& part) {
  return s.find(part) != std::string::npos;
}

Page sample() {
  Page p;
  p.lab = "level";
  p.question = "What did the generator build?";
  p.inputs = {{"seeds", "38"}};
  p.shots.push_back({"seed-38", "SEED 38 <FROM> ABOVE", "the face & the piste", "gates in order",
                     "R0lGOA==", "", 4, 2});
  p.metrics.push_back({"seed-38.gates", "27", "count", ""});
  PageTable t{"gates", {"gate", "s m"}, {}};
  for (int i = 0; i < 5; ++i) t.rows.push_back({std::to_string(i), "8.0"});
  p.tables.push_back(t);
  return p;
}

}  // namespace

TEST(base64_pads_to_whole_quads) {
  CHECK(base64("") == "");
  CHECK(base64("f") == "Zg==");
  CHECK(base64("fo") == "Zm8=");
  CHECK(base64("foo") == "Zm9v");
  CHECK(base64(std::string("\xff\x00\x10", 3)) == "/wAQ");
}

TEST(text_from_a_run_is_escaped) {
  CHECK(html_escape("a<b & \"c\">") == "a&lt;b &amp; &quot;c&quot;&gt;");
  const std::string page = publish_html(sample());
  CHECK(has(page, "SEED 38 &lt;FROM&gt; ABOVE"));
  CHECK(has(page, "the face &amp; the piste"));
  CHECK(!has(page, "<FROM>"));
}

TEST(a_page_holds_its_pictures_inline_and_follows_the_page_contract) {
  const std::string page = publish_html(sample());
  CHECK(has(page, "<title>level lab run</title>"));
  CHECK(has(page, "src=\"data:image/gif;base64,R0lGOA==\""));
  CHECK(has(page, "prefers-color-scheme:dark"));
  CHECK(has(page, ":root[data-theme=\"dark\"]"));
  CHECK(has(page, "--seeds=38"));
  CHECK(!has(page, "before</figcaption>"));
  CHECK(!has(page, "http://") && !has(page, "src=\"http"));
}

TEST(a_kept_run_stands_beside_the_run_with_each_metrics_change) {
  Page p = sample();
  p.before_tag = "before";
  p.shots[0].before_gif = "QkVGT1JF";
  p.metrics[0].before = "25";
  const std::string page = publish_html(p);
  CHECK(has(page, "before</figcaption>") && has(page, "after</figcaption>"));
  CHECK(has(page, "base64,QkVGT1JF"));
  CHECK(has(page, "<td class=\"up\">+2 (+8.0%)</td>"));
}

TEST(a_long_table_is_cut_and_says_how_much_more) {
  const std::string page = publish_html(sample(), 3);
  CHECK(has(page, "<td>2</td>") && !has(page, "<td>3</td>"));
  CHECK(has(page, "2 more rows in the run's report.json."));
}

TEST(several_labs_share_one_page_one_after_another) {
  Page a = sample();
  Page b = sample();
  b.lab = "para";
  const std::string page = publish_html(std::vector<Page>{a, b});
  CHECK(has(page, "<title>Lab runs</title>"));
  CHECK(has(page, "<code>level</code>") && has(page, "<code>para</code>"));
  CHECK(page.find("<code>level</code>") < page.find("<code>para</code>"));
}
