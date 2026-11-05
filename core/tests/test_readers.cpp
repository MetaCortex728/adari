// SPDX-License-Identifier: Apache-2.0
#include <doctest.h>

#include <stdexcept>
#include <string>

#include "adari/readers.hpp"

using namespace adari;

TEST_CASE("ASCII maps carry points, regions, doors and variant options") {
    const Map m = parse_ascii(
        "name hall\n"
        "cell_size 0.5\n"
        "legend S point entry spawn\n"
        "legend c region cover\n"
        "legend D door gate\n"
        "legend 1 option crates.a walk\n"
        "legend 2 option crates.b\n"
        "group crates optional\n"
        "map\n"
        "#######\n"
        "#S.c1D#\n"
        "#.~%2.#\n"
        "#######\n",
        "hall.txt");
    CHECK(m.name == "hall");
    CHECK(m.width == 7);
    CHECK(m.height == 4);
    CHECK(m.cell_size == doctest::Approx(0.5));
    CHECK(m.ascii_first_line == 10);
    REQUIRE(m.points.size() == 1);
    CHECK(m.points[0].tags == std::vector<std::string>{"entry", "spawn"});
    CHECK(m.points[0].x == doctest::Approx(1.5));
    REQUIRE(m.regions.size() == 2);
    CHECK(m.regions[1].door);
    CHECK(m.walk[static_cast<std::size_t>(m.index(2, 2))] == 0);   // water
    CHECK(m.sight[static_cast<std::size_t>(m.index(2, 2))] == 0);
    CHECK(m.walk[static_cast<std::size_t>(m.index(3, 2))] == 1);   // foliage
    CHECK(m.sight[static_cast<std::size_t>(m.index(3, 2))] == 1);
    REQUIRE(m.groups.size() == 1);
    CHECK(m.groups[0].optional);
    CHECK(m.groups[0].choices() == 3);
    const auto& a = m.groups[0].options[0];
    CHECK(a.name == "a");
    CHECK(a.block_walk.size() == 1);
    CHECK(a.block_sight.empty());  // "walk" option blocks walking only
}

TEST_CASE("ASCII errors name the file and line") {
    try {
        parse_ascii("map\n#X#\n", "bad.txt");
        FAIL("expected an error");
    } catch (const std::runtime_error& e) {
        CHECK(std::string(e.what()).find("bad.txt:2") != std::string::npos);
    }
    CHECK_THROWS_AS(parse_ascii("legend # point x\nmap\n.\n", "b.txt"), std::runtime_error);
    CHECK_THROWS_AS(parse_ascii("cell_size 1\n", "b.txt"), std::runtime_error);
}

TEST_CASE("native JSON adds polygons in cell units") {
    const Map m = parse_native(R"({"adari_map":1,"name":"yard","cell_size":2,
        "rows":["#####","#...#","#...#","#####"],
        "points":[{"name":"entry","tags":["entry"],"x":1.5,"y":1.5}],
        "regions":[{"name":"pit","tags":["hazard"],"polygon":[[2,1],[4,1],[4,3],[2,3]]}]})",
                               "yard.json");
    CHECK(m.name == "yard");
    CHECK(m.cell_size == doctest::Approx(2.0));
    REQUIRE(m.regions.size() == 1);
    CHECK(m.regions[0].cells.size() == 4);
    CHECK_THROWS_AS(parse_native(R"({"rows":[]})", "x.json"), std::runtime_error);
}

namespace {

const char* kTiled = R"({
  "tiledversion": "1.12.2", "orientation": "orthogonal", "width": 5, "height": 3,
  "tilewidth": 16, "tileheight": 16, "infinite": false,
  "tilesets": [{"firstgid": 1, "name": "t", "tiles": [
      {"id": 0, "properties": [{"name": "solid", "type": "bool", "value": true}]},
      {"id": 2, "objectgroup": {"objects": [{"id": 1, "x": 0, "y": 0, "width": 16, "height": 8}]}}]}],
  "layers": [
    {"type": "tilelayer", "name": "ground", "width": 5, "height": 3,
     "data": [1, 1, 1, 1, 1,   2147483649, 2, 2, 2, 3,   1, 1, 1, 1, 1]},
    {"type": "objectgroup", "name": "logic", "objects": [
      {"id": 10, "name": "start", "type": "entry", "point": true, "x": 24, "y": 24},
      {"id": 11, "name": "goal", "class": "exit", "point": true, "x": 56, "y": 24},
      {"id": 12, "name": "gate", "class": "door", "x": 32, "y": 16, "width": 16, "height": 16}]}
  ]})";

}  // namespace

TEST_CASE("Tiled: tile collision, flip flags, objects and doors") {
    const Map t = parse_tiled(kTiled, "t.tmj", "");
    const Map a = parse_ascii("legend S point entry\nlegend E point exit\nlegend D door gate\n"
                              "map\n#####\n#.D.#\n#####\n",
                              "a.txt");
    // Tile 3 has collision shapes, so (4,1) is solid like the ASCII wall; the
    // flipped wall tile at (0,1) is still a wall.
    CHECK(t.walk == a.walk);
    CHECK(t.sight == a.sight);
    REQUIRE(t.points.size() == 2);
    CHECK(t.points[0].name == "start");
    CHECK(t.points[0].tags == std::vector<std::string>{"entry"});
    CHECK(t.points[1].tags == std::vector<std::string>{"exit"});
    REQUIRE(t.regions.size() == 1);
    CHECK(t.regions[0].door);
    CHECK(t.regions[0].cells == std::vector<int>{t.index(2, 1)});
}

TEST_CASE("Tiled: unsupported layouts are refused clearly") {
    std::string iso = kTiled;
    iso.replace(iso.find("orthogonal"), 10, "isometric");
    CHECK_THROWS_AS(parse_tiled(iso, "iso.tmj", ""), std::runtime_error);
    CHECK_THROWS_AS(parse_tiled(R"({"orientation":"orthogonal","width":2,"height":2,"tilewidth":16,
        "tileheight":16,"tilesets":[{"firstgid":1,"source":"walls.tsx"}],"layers":[]})",
                                "x.tmj", ""),
                    std::runtime_error);
}

TEST_CASE("LDtk: IntGrid meanings and entities") {
    const char* ldtk = R"({
      "jsonVersion": "1.5.3",
      "defs": {"layers": [{"uid": 7, "identifier": "Collisions", "intGridValues": [
          {"value": 1, "identifier": "Wall"}, {"value": 2, "identifier": "Water"}]}]},
      "levels": [{"identifier": "Level_0", "layerInstances": [
        {"__type": "Entities", "__pxTotalOffsetX": 0, "__pxTotalOffsetY": 0, "entityInstances": [
          {"__identifier": "Entry", "px": [24, 24], "__pivot": [0.5, 0.5], "width": 16, "height": 16,
           "__tags": ["start"], "fieldInstances": []},
          {"__identifier": "Arena", "px": [48, 24], "__pivot": [0.5, 0.5], "width": 32, "height": 16,
           "__tags": [], "fieldInstances": [{"__identifier": "name", "__value": "arena_a"}]}]},
        {"__type": "IntGrid", "layerDefUid": 7, "__cWid": 5, "__cHei": 3, "__gridSize": 16,
         "intGridCsv": [1,1,1,1,1, 1,0,2,0,1, 1,1,1,1,1]}]}]})";
    const Map m = parse_ldtk(ldtk, "p.ldtk", "");
    CHECK(m.name == "Level_0");
    CHECK(m.walk[static_cast<std::size_t>(m.index(2, 1))] == 0);  // water
    CHECK(m.sight[static_cast<std::size_t>(m.index(2, 1))] == 0);
    CHECK(m.walk[static_cast<std::size_t>(m.index(0, 0))] == 0);  // wall
    CHECK(m.sight[static_cast<std::size_t>(m.index(0, 0))] == 1);
    REQUIRE(m.points.size() == 1);  // one-cell entities are points
    CHECK(m.points[0].x == doctest::Approx(1.5));
    CHECK(m.points[0].tags == std::vector<std::string>{"Entry", "entry", "start"});
    REQUIRE(m.regions.size() == 1);  // the 2x1 arena is a region
    CHECK(m.regions[0].name == "arena_a");
    CHECK(m.regions[0].cells.size() == 2);
    ReaderOptions missing;
    missing.level = "Missing";
    CHECK_THROWS_AS(parse_ldtk(ldtk, "p.ldtk", "", missing), std::runtime_error);
}

TEST_CASE("rasterize uses cell centres") {
    Map m;
    m.resize(4, 4);
    CHECK(rasterize(m, {{0, 0}, {2, 0}, {2, 2}, {0, 2}}).size() == 4);
    CHECK(rasterize(m, {{0.6, 0.6}, {1.4, 0.6}, {1.4, 1.4}, {0.6, 1.4}}).empty());
}
