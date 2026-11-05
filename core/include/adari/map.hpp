// SPDX-License-Identifier: Apache-2.0
// The common spatial model every reader produces and every rule reads.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace adari {

// Direction bits for edge blocking: movement out of a cell in that direction
// is not allowed. Used for one-way ledges and per-edge passability.
enum Dir : std::uint8_t { kNorth = 1, kEast = 2, kSouth = 4, kWest = 8 };

struct Cell {
    int x = 0;
    int y = 0;
    bool operator==(const Cell& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Cell& o) const { return !(*this == o); }
};

// A named point (spawn, entry, exit, reward) in cell coordinates. The point's
// cell is floor(x), floor(y).
struct Point {
    std::string name;
    std::vector<std::string> tags;
    double x = 0.0;
    double y = 0.0;
    Cell cell() const;
};

// A set of cells with tags. A door is a region that blocks movement and sight
// while it is closed.
struct Region {
    std::string name;
    std::vector<std::string> tags;
    std::vector<int> cells;  // cell indices, y * width + x
    bool door = false;
};

// One alternative of a variant group: the cells it blocks when chosen.
struct VariantOption {
    std::string name;
    std::vector<std::string> tags;
    std::vector<int> block_walk;
    std::vector<int> block_sight;
};

// A slot of the map that takes exactly one of its options per variant. An
// optional group can also take none.
struct VariantGroup {
    std::string name;
    std::vector<VariantOption> options;
    bool optional = false;
    std::size_t choices() const { return options.size() + (optional ? 1u : 0u); }
};

// Where a reader found something, so reports can point back into the source
// file. line and column are 1-based; zero means unknown.
struct SourceRef {
    std::string file;
    int line = 0;
    int column = 0;
};

struct Map {
    std::string name;
    std::string source_file;
    int width = 0;
    int height = 0;
    double cell_size = 1.0;  // world units per cell

    std::vector<std::uint8_t> walk;        // 1 = walkable
    std::vector<std::uint8_t> sight;       // 1 = blocks sight
    std::vector<std::uint8_t> edge_block;  // Dir bits blocked when leaving the cell

    std::vector<Point> points;
    std::vector<Region> regions;
    std::vector<VariantGroup> groups;

    // For ASCII maps: the text line of row 0, so findings map to file lines.
    int ascii_first_line = 0;

    // Things the reader wants the user to know, e.g. "no tile carries
    // collision data, so every tiled cell is floor".
    std::vector<std::string> notes;

    void resize(int w, int h);
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }
    int index(int x, int y) const { return y * width + x; }
    Cell cell(int idx) const { return Cell{idx % width, idx / width}; }
    std::size_t size() const { return static_cast<std::size_t>(width) * static_cast<std::size_t>(height); }
};

// One variant: for each group, the chosen option index, or -1 for none.
using Choice = std::vector<int>;

// Effective per-cell state of a map for one variant and one door state.
struct State {
    std::vector<std::uint8_t> walk;
    std::vector<std::uint8_t> sight;
};

enum class DoorMode { kOpen, kClosed };

struct DoorOverride {
    std::string selector;
    DoorMode mode = DoorMode::kClosed;
};

// Builds the effective state. Doors are open unless an override closes them.
// A choice index of -2 for a group means "union of every option" (a bound,
// not a real variant).
State make_state(const Map& map, const Choice& choice,
                 const std::vector<DoorOverride>& doors = {});

// Selectors: "tag" matches points and regions carrying the tag;
// "name:foo" matches by name.
bool matches(const std::string& selector, const std::string& name,
             const std::vector<std::string>& tags);

std::vector<const Point*> select_points(const Map& map, const std::string& selector);
std::vector<const Region*> select_regions(const Map& map, const std::string& selector);

// All cells a selector covers: point cells plus region cells, deduplicated.
std::vector<int> select_cells(const Map& map, const std::string& selector);

// Validates internal consistency (sizes, indices). Returns an empty string if
// the map is well formed, otherwise a description of the first problem.
std::string validate(const Map& map);

}  // namespace adari
