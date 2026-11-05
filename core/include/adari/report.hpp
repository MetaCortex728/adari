// SPDX-License-Identifier: Apache-2.0
// Report writers: plain text for people, JSON for scripts, SARIF 2.1.0 for
// code-scanning tools, and a PNG overlay of the map with failures drawn on it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "adari/map.hpp"
#include "adari/rules.hpp"

namespace adari {

const char* version();

// What a reader made of a map, without any rules: size, walkable and
// sight-blocking share, separate walkable areas, tags, regions, doors and
// variant groups. Useful as a first run on an unfamiliar map.
std::string summarize(const Map& map);

// Writes a map as Adari JSON (rows plus explicit cell lists), so a map
// sampled inside an engine can be checked again by the command-line tool.
// Edge blocks (one-way cells) are not representable and are dropped.
std::string to_native_json(const Map& map);

std::string to_text(const Map& map, const Report& report);
std::string to_json(const Map& map, const Report& report);
std::string to_sarif(const Map& map, const Report& report);

// RGB image of the map: floor, walls, sight-only blockers, regions, points,
// failing cells in red and counterexample paths in orange. scale is pixels per
// cell; 0 picks one so the longer side is about 768 pixels (1 to 16).
std::vector<std::uint8_t> overlay_png(const Map& map, const Report& report, int scale = 0);

// Minimal PNG encoder (8-bit RGB, uncompressed deflate). Exposed for tests.
std::vector<std::uint8_t> encode_png(int width, int height, const std::vector<std::uint8_t>& rgb);

void write_file(const std::string& path, const std::string& data);
void write_file(const std::string& path, const std::vector<std::uint8_t>& data);

}  // namespace adari
