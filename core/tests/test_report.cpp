// SPDX-License-Identifier: Apache-2.0
#include <doctest.h>

#include <string>

#include <nlohmann/json.hpp>

#include "adari/readers.hpp"
#include "adari/report.hpp"

using namespace adari;

namespace {

struct Fixture {
    Map map = parse_ascii("legend S point entry\nlegend E point exit\nmap\n#####\n#S#E#\n#####\n", "f.txt");
    Report rep = evaluate(map, parse_rules(R"({"rules":[
        {"id":"exits","template":"reachable","from":"entry","to":"exit"},
        {"id":"near-cover","template":"near","subject":"entry","object":"cover","distance":2,"severity":"warning"}]})"));
};

}  // namespace

TEST_CASE("text report lists each rule and points to the ASCII line") {
    Fixture f;
    const std::string t = to_text(f.map, f.rep);
    CHECK(t.find("FAIL exits") != std::string::npos);
    CHECK(t.find("SKIP near-cover") != std::string::npos);
    CHECK(t.find("f.txt:5:4") != std::string::npos);  // exit at (3,1), map starts on line 4
}

TEST_CASE("JSON report has a summary and findings") {
    Fixture f;
    const auto j = nlohmann::json::parse(to_json(f.map, f.rep));
    CHECK(j["summary"]["failed"] == true);
    CHECK(j["summary"]["errors"] == 1);
    CHECK(j["rules"].size() == 2);
    CHECK(j["rules"][1]["status"] == "not_checked");
    CHECK(j["findings"][0]["cells"][0] == nlohmann::json::array({3, 1}));
}

TEST_CASE("SARIF report is version 2.1.0 with file regions") {
    Fixture f;
    const auto j = nlohmann::json::parse(to_sarif(f.map, f.rep));
    CHECK(j["version"] == "2.1.0");
    const auto& run = j["runs"][0];
    CHECK(run["tool"]["driver"]["name"] == "adari");
    REQUIRE(run["results"].size() == 1);
    CHECK(run["results"][0]["ruleId"] == "exits");
    CHECK(run["results"][0]["level"] == "error");
    CHECK(run["results"][0]["locations"][0]["physicalLocation"]["region"]["startLine"] == 5);
    CHECK(run.contains("invocations"));  // the not-checked rule is reported
}

TEST_CASE("Adari JSON round-trips a map and its rule results") {
    const Map a = load_map(ADARI_SOURCE_DIR "/examples/courtyard/courtyard-broken.txt");
    const Map b = parse_native(to_native_json(a), "round.json");
    CHECK(b.walk == a.walk);
    CHECK(b.sight == a.sight);
    CHECK(b.points.size() == a.points.size());
    REQUIRE(b.regions.size() == a.regions.size());
    for (std::size_t i = 0; i < a.regions.size(); ++i) {
        CHECK(b.regions[i].cells == a.regions[i].cells);
        CHECK(b.regions[i].door == a.regions[i].door);
    }
    REQUIRE(b.groups.size() == a.groups.size());
    CHECK(b.groups[0].optional == a.groups[0].optional);
    const RuleSet rules = load_rules(ADARI_SOURCE_DIR "/examples/courtyard/rules.json");
    const Report ra = evaluate(a, rules);
    const Report rb = evaluate(b, rules);
    REQUIRE(ra.results.size() == rb.results.size());
    for (std::size_t i = 0; i < ra.results.size(); ++i) CHECK(ra.results[i].status == rb.results[i].status);
}

TEST_CASE("PNG encoder writes a valid header and the overlay has the map's size") {
    const auto png = encode_png(2, 1, {255, 0, 0, 0, 255, 0});
    REQUIRE(png.size() > 33);
    CHECK(png[0] == 0x89);
    CHECK(png[1] == 'P');
    CHECK(png[12] == 'I');
    CHECK(png[16] == 0);
    CHECK(png[19] == 2);  // width 2
    Fixture f;
    const auto o = overlay_png(f.map, f.rep, 4);
    CHECK(o[19] == 20);  // 5 cells x 4 px
    CHECK(o[23] == 12);  // 3 cells x 4 px
}
