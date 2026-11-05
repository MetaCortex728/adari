// SPDX-License-Identifier: Apache-2.0
// Grid algorithms behind the rule templates. All distances returned in world
// units use Map::cell_size; cells are squares and agents stand at cell centres.
#pragma once

#include <cstdint>
#include <vector>

#include "adari/map.hpp"

namespace adari {

// Euclidean distance, in cells, from each cell centre to the nearest blocked
// cell centre. Cells outside the map count as blocked. Exact (Felzenszwalb and
// Huttenlocher's separable transform).
std::vector<float> clearance(const Map& map, const std::vector<std::uint8_t>& walk);

// Euclidean distance, in cells, from each cell centre to the nearest of the
// given cells' centres, ignoring walls. Infinity when `sources` is empty.
std::vector<float> distance_field(const Map& map, const std::vector<int>& sources);

// Cells an agent of the given radius (world units) can stand on: walkable and
// at least `radius` from any blocked cell's edge. Radius 0 means every
// walkable cell.
std::vector<std::uint8_t> passable(const Map& map, const std::vector<std::uint8_t>& walk,
                                   double radius);

// Whether one 8-neighbour step is allowed. Orthogonal steps respect edge
// blocks; a diagonal step needs both orthogonal two-step routes to be open,
// so agents never cut corners.
bool step_allowed(const Map& map, const std::vector<std::uint8_t>& pass, int from, int to);

constexpr int kUnreached = -1;

// Breadth-first reach over 8-neighbour steps. Returns, per cell, the step
// count from the nearest seed, or kUnreached. backward = true follows steps in
// reverse (cells that can reach a seed).
std::vector<int> reach(const Map& map, const std::vector<std::uint8_t>& pass,
                       const std::vector<int>& seeds, bool backward = false);

struct Path {
    bool found = false;
    double length = 0.0;  // world units
    std::vector<int> cells;
};

// Shortest path from any source to any target (orthogonal step 1, diagonal
// sqrt(2), times cell_size).
Path shortest_path(const Map& map, const std::vector<std::uint8_t>& pass,
                   const std::vector<int>& sources, const std::vector<int>& targets);

// Whether the segment between two cell centres crosses no sight-blocking
// cell, the end cells excluded. Where the segment passes exactly through a
// cell corner, both cells beside it are tested and either one blocks.
bool line_of_sight(const Map& map, const std::vector<std::uint8_t>& sight, int a, int b);

struct SightPair {
    bool found = false;
    int from = -1;
    int to = -1;
    bool sampled = false;  // true when the cell sets were thinned before testing
};

// Looks for any pair (a in from, b in to) with line of sight. Large sets are
// thinned to an even stride of at most `max_samples` cells each; the result
// then reports sampled = true.
SightPair any_sight(const Map& map, const std::vector<std::uint8_t>& sight,
                    const std::vector<int>& from, const std::vector<int>& to,
                    std::size_t max_samples = 256);

// Groups the given cells into 8-connected components (undirected, ignoring
// edge blocks). Returned components are sorted by their smallest cell index.
std::vector<std::vector<int>> components(const Map& map, const std::vector<int>& cells);

}  // namespace adari
