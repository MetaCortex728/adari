// SPDX-License-Identifier: Apache-2.0
// Readers turn editor files into the common Map. Each throws
// std::runtime_error with the file name and a readable reason on bad input.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "adari/map.hpp"

namespace adari {

// What a name in the source means for movement and sight. Readers guess from
// names (see docs/formats.md); `meanings` overrides the guess for an LDtk
// IntGrid value identifier or a Tiled tile class, matched case-insensitively.
// Values: "wall" (blocks walk and sight), "low" (blocks walk only), "sight"
// (blocks sight only), "floor".
struct ReaderOptions {
    std::string level;  // LDtk level identifier; empty means the first level
    std::map<std::string, std::string> meanings;
};

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

// Tiled maps, orthogonal and finite in this stage. parse_tiled reads JSON
// (.tmj, or .json written by Tiled); parse_tmx reads XML (.tmx). Tile data may
// be CSV, XML or base64 (uncompressed, zlib or gzip). External tilesets may be
// .tsj or .tsx; base_dir resolves them.
Map parse_tiled(const std::string& json_text, const std::string& file_name,
                const std::string& base_dir, const ReaderOptions& options = {});
Map parse_tmx(const std::string& xml_text, const std::string& file_name,
              const std::string& base_dir, const ReaderOptions& options = {});

// LDtk projects. options.level selects a level by identifier.
Map parse_ldtk(const std::string& json_text, const std::string& file_name,
               const std::string& base_dir, const ReaderOptions& options = {});

// Level identifiers of an LDtk project, in file order.
std::vector<std::string> ldtk_levels(const std::string& json_text);

// Painted-map spatial files: one JSON with several maps, each describing the
// gameplay layer over a painted background in coordinates normalized to the
// painting (0..1). Walk areas, object footprints (blocking movement and/or
// sight), foreground cover polygons, a route, an exit and named destinations.
// The painting is sampled on a 384 x 256 grid (3:2); cell_size is 16, so world
// units are pixels of a 6144 x 4096 painting. options.level selects a map by
// index ("0") or name ("map0"); empty means the first.
Map parse_painted(const std::string& json_text, const std::string& file_name, const ReaderOptions& options = {});
std::vector<std::string> painted_levels(const std::string& json_text);

// Levels in a multi-level file (LDtk or painted); empty for single-map files.
std::vector<std::string> list_levels(const std::string& path);

// Picks a reader from the extension and contents.
Map load_map(const std::string& path, const ReaderOptions& options = {});

// Cells whose centres lie inside the polygon (even-odd rule). Coordinates are
// in cells.
std::vector<int> rasterize(const Map& map, const std::vector<std::pair<double, double>>& polygon);

}  // namespace adari
