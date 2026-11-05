// SPDX-License-Identifier: Apache-2.0
#include <doctest.h>

#include <cmath>

#include "adari/grid.hpp"
#include "adari/readers.hpp"

using namespace adari;

namespace {

Map ascii(const char* text) { return parse_ascii(text, "test"); }

}  // namespace

TEST_CASE("clearance measures distance to the nearest wall centre, edges included") {
    const Map m = ascii("map\n#####\n#...#\n#...#\n#...#\n#####\n");
    const auto c = clearance(m, m.walk);
    CHECK(c[static_cast<std::size_t>(m.index(2, 2))] == doctest::Approx(2.0));
    CHECK(c[static_cast<std::size_t>(m.index(1, 1))] == doctest::Approx(1.0));
    CHECK(c[static_cast<std::size_t>(m.index(0, 0))] == doctest::Approx(0.0));
}

TEST_CASE("passable keeps only cells far enough from walls for the radius") {
    Map m = ascii("cell_size 1\nmap\n#######\n#.....#\n#.....#\n#.....#\n#######\n");
    CHECK(passable(m, m.walk, 0.0) == m.walk);
    const auto p = passable(m, m.walk, 1.0);  // needs centre at least 1.5 cells from wall centres
    CHECK(p[static_cast<std::size_t>(m.index(3, 2))]);
    CHECK_FALSE(p[static_cast<std::size_t>(m.index(1, 1))]);
}

TEST_CASE("diagonal steps never cut a blocked corner") {
    const Map m = ascii("map\n.#\n#.\n");
    CHECK_FALSE(step_allowed(m, m.walk, m.index(0, 0), m.index(1, 1)));
    const Map open = ascii("map\n..\n..\n");
    CHECK(step_allowed(open, open.walk, open.index(0, 0), open.index(1, 1)));
}

TEST_CASE("one-way cells allow travel only along the arrow") {
    const Map m = ascii("map\n.\nv\n.\n");
    const int top = m.index(0, 0), mid = m.index(0, 1), bottom = m.index(0, 2);
    CHECK(step_allowed(m, m.walk, top, mid));
    CHECK(step_allowed(m, m.walk, mid, bottom));
    CHECK_FALSE(step_allowed(m, m.walk, bottom, mid));
    CHECK_FALSE(step_allowed(m, m.walk, mid, top));
    const auto fwd = reach(m, m.walk, {top});
    CHECK(fwd[static_cast<std::size_t>(bottom)] == 2);
    const auto back = reach(m, m.walk, {top}, true);
    CHECK(back[static_cast<std::size_t>(bottom)] == kUnreached);
}

TEST_CASE("shortest path uses diagonals and reports world length") {
    Map m = ascii("cell_size 2\nmap\n....\n....\n....\n");
    const Path p = shortest_path(m, m.walk, {m.index(0, 0)}, {m.index(3, 2)});
    REQUIRE(p.found);
    CHECK(p.length == doctest::Approx((2 * std::sqrt(2.0) + 1) * 2));
    CHECK(p.cells.front() == m.index(0, 0));
    CHECK(p.cells.back() == m.index(3, 2));
}

TEST_CASE("line of sight is blocked by sight blockers and corners") {
    const Map m = ascii("map\n.....\n..#..\n.....\n");
    CHECK_FALSE(line_of_sight(m, m.sight, m.index(0, 1), m.index(4, 1)));
    CHECK(line_of_sight(m, m.sight, m.index(0, 0), m.index(4, 0)));
    // Exactly through the corner between (1,0)/(2,1) diagonal neighbours.
    const Map c = ascii("map\n.#\n#.\n");
    CHECK_FALSE(line_of_sight(c, c.sight, c.index(0, 0), c.index(1, 1)));
    // Water blocks walking but not sight.
    const Map w = ascii("map\n.~.\n");
    CHECK(line_of_sight(w, w.sight, w.index(0, 0), w.index(2, 0)));
}

TEST_CASE("components split unconnected cell sets") {
    const Map m = ascii("map\n..#..\n");
    const auto comps = components(m, {m.index(0, 0), m.index(1, 0), m.index(3, 0), m.index(4, 0)});
    REQUIRE(comps.size() == 2);
    CHECK(comps[0].size() == 2);
}

TEST_CASE("distance field ignores walls") {
    const Map m = ascii("map\n.#.\n");
    const auto d = distance_field(m, {m.index(0, 0)});
    CHECK(d[static_cast<std::size_t>(m.index(2, 0))] == doctest::Approx(2.0));
}
