// SPDX-License-Identifier: Apache-2.0
// Each template is shown a map it must accept and one it must reject.
#include <doctest.h>

#include <stdexcept>
#include <string>

#include "adari/readers.hpp"
#include "adari/rules.hpp"

using namespace adari;

namespace {

const char* kLegend =
    "legend S point entry\n"
    "legend E point exit\n"
    "legend R point reward\n"
    "legend P point spawn\n"
    "legend c region cover\n"
    "legend B region boss\n"
    "legend D door gate\n"
    "legend k region keep_clear\n";

Map map_of(const std::string& body) { return parse_ascii(std::string(kLegend) + body, "test.txt"); }

Report run(const Map& m, const std::string& rule_json) {
    return evaluate(m, parse_rules(R"({"rules":[)" + rule_json + "]}"));
}

Status status(const Report& r) {
    REQUIRE(r.results.size() == 1);
    return r.results[0].status;
}

}  // namespace

TEST_CASE("rule files are validated") {
    CHECK_THROWS_AS(parse_rules("{}"), std::runtime_error);
    CHECK_THROWS_AS(parse_rules(R"({"rules":[{"id":"a","template":"nope"}]})"), std::runtime_error);
    CHECK_THROWS_AS(parse_rules(R"({"rules":[{"id":"a","template":"reachable","from":"entry"}]})"),
                    std::runtime_error);
    CHECK_THROWS_AS(parse_rules(R"({"rules":[{"id":"a","template":"reachable","from":"x","to":"y"},
                                             {"id":"a","template":"reachable","from":"x","to":"y"}]})"),
                    std::runtime_error);
    CHECK_THROWS_AS(parse_rules(R"({"rules":[{"id":"a","template":"path_length","from":"x","to":"y"}]})"),
                    std::runtime_error);
    const RuleSet ok = parse_rules(R"({"rules":[{"id":"a","template":"reachable","from":"x","to":"y",
                                       "severity":"warning","radius":0.4}]})");
    CHECK(ok.rules[0].severity == Severity::kWarning);
    CHECK(ok.rules[0].radius == doctest::Approx(0.4));
}

TEST_CASE("reachable") {
    const std::string rule = R"({"id":"exits","template":"reachable","from":"entry","to":"exit"})";
    CHECK(status(run(map_of("map\n#####\n#S.E#\n#####\n"), rule)) == Status::kPassed);
    const Report r = run(map_of("map\n#####\n#S#E#\n#####\n"), rule);
    CHECK(status(r) == Status::kFailed);
    REQUIRE(r.findings.size() == 1);
    CHECK(r.findings[0].message.find("not reachable") != std::string::npos);
    CHECK(r.failed());
}

TEST_CASE("reachable respects agent radius") {
    const Map m = map_of("cell_size 1\nmap\n#######\n#.....#\n#S...E#\n#.....#\n#######\n");
    CHECK(status(run(m, R"({"id":"r","template":"reachable","from":"entry","to":"exit","radius":0.4})")) ==
          Status::kPassed);
    CHECK(status(run(m, R"({"id":"r","template":"reachable","from":"entry","to":"exit","radius":1.2})")) ==
          Status::kFailed);
}

TEST_CASE("selectors that match nothing are reported, not passed") {
    const Report r = run(map_of("map\n#####\n#S.E#\n#####\n"),
                         R"({"id":"x","template":"reachable","from":"entry","to":"treasure"})");
    CHECK(status(r) == Status::kNotChecked);
    CHECK(r.failed());
}

TEST_CASE("no_isolated_walkable") {
    const std::string rule = R"({"id":"iso","template":"no_isolated_walkable","from":"entry"})";
    CHECK(status(run(map_of("map\n######\n#S...#\n######\n"), rule)) == Status::kPassed);
    const Report r = run(map_of("map\n######\n#S#..#\n######\n"), rule);
    CHECK(status(r) == Status::kFailed);
    REQUIRE(r.findings.size() == 1);
    CHECK(r.findings[0].cells.size() == 2);
    // min_cells ignores small pockets.
    CHECK(status(run(map_of("map\n######\n#S#..#\n######\n"),
                     R"({"id":"iso","template":"no_isolated_walkable","from":"entry","min_cells":3})")) ==
          Status::kPassed);
}

TEST_CASE("path_length") {
    const Map m = map_of("map\n#######\n#S...E#\n#######\n");  // 4 cells apart
    CHECK(status(run(m, R"({"id":"p","template":"path_length","from":"entry","to":"exit","min":3,"max":5})")) ==
          Status::kPassed);
    const Report longer = run(m, R"({"id":"p","template":"path_length","from":"entry","to":"exit","max":3})");
    CHECK(status(longer) == Status::kFailed);
    REQUIRE(longer.findings.size() == 1);
    CHECK(longer.findings[0].measured == doctest::Approx(4.0));
    CHECK(longer.findings[0].path.size() == 5);
    CHECK(status(run(m, R"({"id":"p","template":"path_length","from":"entry","to":"exit","min":6})")) ==
          Status::kFailed);
}

TEST_CASE("not_visible_until") {
    const std::string rule = R"({"id":"v","template":"not_visible_until","target":"boss","from":"entry","door":"gate"})";
    CHECK(status(run(map_of("map\n#########\n#S..#.BB#\n#...D...#\n#########\n"), rule)) == Status::kPassed);
    // A window (water: blocks walking, not sight) in the wall lets the boss room be seen.
    const Report r = run(map_of("map\n#########\n#S..~.BB#\n#...D...#\n#########\n"), rule);
    CHECK(status(r) == Status::kFailed);
    REQUIRE(r.findings.size() == 1);
    CHECK(r.findings[0].path.size() == 2);
}

TEST_CASE("visible_from") {
    const std::string rule = R"({"id":"v","template":"visible_from","target":"reward","from":"entry"})";
    CHECK(status(run(map_of("map\n#######\n#S...R#\n#######\n"), rule)) == Status::kPassed);
    CHECK(status(run(map_of("map\n#######\n#S.%.R#\n#######\n"), rule)) == Status::kFailed);
}

TEST_CASE("near") {
    const std::string rule = R"({"id":"n","template":"near","subject":"spawn","object":"cover","distance":2})";
    CHECK(status(run(map_of("map\n########\n#P.c...#\n########\n"), rule)) == Status::kPassed);
    const Report r = run(map_of("map\n########\n#P....c#\n########\n"), rule);
    CHECK(status(r) == Status::kFailed);
    REQUIRE(r.findings.size() == 1);
    CHECK(r.findings[0].measured == doctest::Approx(5.0));
}

TEST_CASE("clear_of checks every variant option against reserved cells") {
    const std::string rule = R"({"id":"c","template":"clear_of","region":"keep_clear","blocker":"*"})";
    auto native = [](const char* option_b) {
        return std::string(R"({"adari_map":1,"rows":["#######","#S....#","#.....#","#######"],
            "legend":{"S":"point entry"},
            "regions":[{"name":"lane","tags":["keep_clear"],"polygon":[[1,1],[6,1],[6,2],[1,2]]}],
            "options":[{"group":"crates","name":"a","polygon":[[2,2],[3,2],[3,3],[2,3]]},
                       {"group":"crates","name":"b","polygon":)") + option_b + "}]}";
    };
    CHECK(status(run(parse_native(native("[[4,2],[5,2],[5,3],[4,3]]")), rule)) == Status::kPassed);
    const Report r = run(parse_native(native("[[4,1],[5,1],[5,3],[4,3]]")), rule);
    CHECK(status(r) == Status::kFailed);
    REQUIRE(r.findings.size() == 1);
    CHECK(r.findings[0].variant_text == "crates=b");
    CHECK(r.findings[0].cells.size() == 1);
}

TEST_CASE("no_softlock finds cells you can enter but not leave") {
    const std::string rule = R"({"id":"s","template":"no_softlock","from":"entry","to":"exit"})";
    CHECK(status(run(map_of("map\n#####\n#S.E#\n#####\n"), rule)) == Status::kPassed);
    // A one-way drop into a pit with no way back to the exit.
    const Report r = run(map_of("map\n#####\n#S.E#\n#v###\n#.###\n#####\n"), rule);
    CHECK(status(r) == Status::kFailed);
    REQUIRE(r.findings.size() == 1);
    CHECK(r.findings[0].cells.size() == 2);
}

TEST_CASE("variants: a relaxed bound proves a rule for every variant") {
    const Map m = map_of("legend 1 option crates.a\nlegend 2 option crates.b\ngroup crates optional\n"
                         "map\n#######\n#S...E#\n#.1.2.#\n#######\n");
    const Report r = run(m, R"({"id":"r","template":"reachable","from":"entry","to":"exit"})");
    CHECK(status(r) == Status::kPassed);
    CHECK(r.results[0].method == "bound");
    CHECK(r.results[0].variants_total == 3);
}

TEST_CASE("variants: enumeration finds the failing variant with the fewest options") {
    // Option b closes the corridor; a does not. The barrels group adds noise.
    const Map m = map_of("legend 1 option crates.a\nlegend 2 option crates.b\n"
                         "legend 3 option barrels.x\ngroup barrels optional\n"
                         "map\n#########\n#S..2..E#\n#.1...3.#\n#########\n");
    // Here the corridor is one cell high, so option b blocks it.
    const Map tight = map_of("legend 1 option crates.a\nlegend 2 option crates.b\n"
                             "legend 3 option barrels.x\ngroup barrels optional\n"
                             "map\n#########\n#S..2..E#\n####1#3##\n#########\n");
    const Report r = run(tight, R"({"id":"r","template":"reachable","from":"entry","to":"exit"})");
    CHECK(status(r) == Status::kFailed);
    CHECK(r.results[0].method == "every variant");
    CHECK(r.results[0].variants_total == 4);
    CHECK(r.results[0].variants_failed == 2);
    REQUIRE(r.findings.size() == 1);
    CHECK(r.findings[0].variant_text == "crates=b, barrels=none");
    // The roomy map passes through the bound.
    CHECK(status(run(m, R"({"id":"r","template":"reachable","from":"entry","to":"exit"})")) == Status::kPassed);
}

TEST_CASE("variants: hiding rules use the bound with no optional blockers") {
    const std::string rule = R"({"id":"v","template":"not_visible_until","target":"boss","from":"entry","door":"gate"})";
    // A screen that only exists in option a: without it the boss room is visible.
    const Map m = map_of("legend 1 option screen.a sight\ngroup screen optional\n"
                         "map\n#######\n#S.1.B#\n###D###\n#######\n");
    const Report r = run(m, rule);
    CHECK(status(r) == Status::kFailed);
    REQUIRE(r.findings.size() == 1);
    CHECK(r.findings[0].variant_text == "screen=none");
}

TEST_CASE("doors can be forced closed for movement rules") {
    const Map m = map_of("map\n#######\n#S.D.E#\n#######\n");
    CHECK(status(run(m, R"({"id":"r","template":"reachable","from":"entry","to":"exit"})")) == Status::kPassed);
    CHECK(status(run(m, R"({"id":"r","template":"reachable","from":"entry","to":"exit","doors":{"gate":"closed"}})")) ==
          Status::kFailed);
}
