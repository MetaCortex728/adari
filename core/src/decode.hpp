// SPDX-License-Identifier: Apache-2.0
// Small decoders the readers need: a minimal XML parser (enough for Tiled's
// TMX and TSX files), base64, and zlib/gzip inflate. Internal to the core.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace adari::detail {

struct XmlNode {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<XmlNode> children;
    std::string text;  // concatenated character data directly inside this element

    const std::string* attr(const std::string& key) const;
    std::string attr_or(const std::string& key, const std::string& fallback = {}) const;
    const XmlNode* child(const std::string& n) const;
};

// Parses a document and returns its root element. Supports elements,
// attributes, character data, CDATA, comments, processing instructions and the
// five predefined entities plus numeric character references. Throws
// std::runtime_error on malformed input.
XmlNode parse_xml(const std::string& text);

std::vector<std::uint8_t> base64_decode(const std::string& text);

// Decompresses a zlib stream (RFC 1950) or, with gzip = true, a gzip member
// (RFC 1952). Throws std::runtime_error on corrupt data.
std::vector<std::uint8_t> inflate(const std::vector<std::uint8_t>& data, bool gzip = false);

}  // namespace adari::detail
