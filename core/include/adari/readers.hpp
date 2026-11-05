// SPDX-License-Identifier: Apache-2.0
// Readers turn editor files into the common Map. Each throws
// std::runtime_error with the file name and a readable reason on bad input.
#pragma once

#include <string>
#include <vector>

#include "adari/map.hpp"

namespace adari {

// ASCII maps: a small text format for tests and quick sketches.
//   # comment
//   cell_size 0.5
//   legend S point entry spawn      (name, then extra tags)
//   legend c region cover
//   legend D door gate
//   legend 1 option crates.a        (blocks walk and sight when chosen)
//   legend 2 option crates.b walk   (blocks walk only)
//   group crates optional
//   map
//   #######
//   #S.c.E#
//   #######
// Built-in cells: '#' or ' ' wall (blocks walk and sight), '.' floor,
// '~' water (blocks walk only), '%' foliage (blocks sight only),
// 'v' '^' '<' '>' one-way cells (travel only in the arrow's direction).
Map parse_ascii(const std::string& text, const std::string& file_name = "<ascii>");

// Native JSON: {"adari_map": 1, "rows": [...], "legend": {...}, ...}; see
// docs/formats.md.
Map parse_native(const std::string& json_text, const std::string& file_name = "<json>");

// Tiled JSON maps (.tmj, or .json written by Tiled). Orthogonal maps only in
// this stage. External tilesets must be JSON (.tsj); base_dir resolves them.
Map parse_tiled(const std::string& json_text, const std::string& file_name,
                const std::string& base_dir);

// LDtk projects. level selects a level by identifier; empty means the first.
Map parse_ldtk(const std::string& json_text, const std::string& file_name,
               const std::string& base_dir, const std::string& level = {});

// Picks a reader from the extension and contents.
Map load_map(const std::string& path, const std::string& level = {});

// Cells whose centres lie inside the polygon (even-odd rule). Coordinates are
// in cells.
std::vector<int> rasterize(const Map& map, const std::vector<std::pair<double, double>>& polygon);

}  // namespace adari
