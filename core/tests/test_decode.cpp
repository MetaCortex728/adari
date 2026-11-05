// SPDX-License-Identifier: Apache-2.0
// The XML, base64 and inflate decoders behind the TMX reader, and the TMX
// reader itself with every tile data encoding Tiled writes.
#include <doctest.h>

#include <numeric>
#include <stdexcept>
#include <string>

#include "../src/decode.hpp"
#include "adari/readers.hpp"

using namespace adari;
using namespace adari::detail;

TEST_CASE("XML: elements, attributes, text, entities, comments") {
    const XmlNode n = parse_xml(
        "<?xml version=\"1.0\"?>\n<!-- c --><a x=\"1 &amp; 2\" y='q'><b/>t&lt;<![CDATA[<raw>]]><c k=\"&#65;\">v</c></a>");
    CHECK(n.name == "a");
    CHECK(n.attr_or("x") == "1 & 2");
    CHECK(n.attr_or("y") == "q");
    CHECK(n.children.size() == 2);
    CHECK(n.text == "t<<raw>");
    REQUIRE(n.child("c"));
    CHECK(n.child("c")->attr_or("k") == "A");
    CHECK(n.child("c")->text == "v");
    CHECK_THROWS_AS(parse_xml("<a><b></a>"), std::runtime_error);
}

TEST_CASE("base64 and inflate") {
    const auto raw = base64_decode("AQAAAAIAAAA=");
    CHECK(raw == std::vector<std::uint8_t>{1, 0, 0, 0, 2, 0, 0, 0});
    // Fixed-Huffman zlib stream of six little-endian uint32 values.
    const auto z = inflate(base64_decode("eNpjZGBgYETCTFAaAAB0AAg="));
    CHECK(z.size() == 24);
    CHECK(z[16] == 2);
    // The same data as a gzip member.
    CHECK(inflate(base64_decode("H4sIAAAAAAAC/2NkYGBgRMJMUBoApynXiBgAAAA="), true) == z);
    // A dynamic-Huffman block: 20 copies of a sentence and the bytes 0..255.
    const auto dyn = inflate(base64_decode(
        "eNoryUhVKCzNTM5WSCrKL89TSMuvUMgqzS0oVsgvSy1SKAFK5yRWVSqk5KfrgXmjikcVjyqmqmIGRiZmFlY2dg5OLm4eXj5+AUEhYRFRMXEJ"
        "SSlpGVk5eQVFJWUVVTV1DU0tbR1dPX0DQyNjE1MzcwtLK2sbWzt7B0cnZxdXN3cPTy9vH18//4DAoOCQ0LDwiMio6JjYuPiExKTklNS09IzM"
        "rOyc3Lz8gsKi4pLSsvKKyqrqmtq6+obGpuaW1rb2js6u7p7evv4JEydNnjJ12vQZM2fNnjN33vwFCxctXrJ02fIVK1etXrN23foNGzdt3rJ1"
        "2/YdO3ft3rN33/4DBw8dPnL02PETJ0+dPnP23PkLFy9dvnL12vUbN2/dvnP33v0HDx89fvL02fMXL1+9fvP23fsPHz99/vL12/cfP3/9/vP3"
        "338AMkDFHA=="));
    CHECK(dyn.size() == 1156);
    CHECK(std::accumulate(dyn.begin(), dyn.end(), 0u) % 65536u == 50444u);
    CHECK_THROWS_AS(inflate({0x78, 0x9C, 0xFF, 0xFF, 0xFF, 0xFF}), std::runtime_error);
}

namespace {

std::string tmx(const std::string& data_element) {
    return R"(<?xml version="1.0" encoding="UTF-8"?>
<map version="1.10" orientation="orthogonal" width="3" height="2" tilewidth="16" tileheight="16" infinite="0">
 <properties><property name="adari_cell_size" type="float" value="0.5"/></properties>
 <tileset firstgid="1" name="t" tilewidth="16" tileheight="16" tilecount="2" columns="2">
  <tile id="0"><properties><property name="solid" type="bool" value="true"/></properties></tile>
 </tileset>
 <layer id="1" name="ground" width="3" height="2">)" +
           data_element + R"(</layer>
 <objectgroup id="2" name="logic">
  <object id="1" name="start" type="entry" x="24" y="24"><point/></object>
  <object id="2" name="lane" class="keep_clear" x="0" y="16" width="48" height="16"/>
  <object id="3" name="arch" x="0" y="0"><polygon points="0,0 16,0 16,16"/></object>
 </objectgroup>
</map>)";
}

void check_ground(const Map& m) {
    CHECK(m.width == 3);
    CHECK(m.height == 2);
    CHECK(m.cell_size == doctest::Approx(0.5));
    for (int x = 0; x < 3; ++x) CHECK(m.walk[static_cast<std::size_t>(m.index(x, 0))] == 0);
    CHECK(m.walk[static_cast<std::size_t>(m.index(0, 1))] == 0);
    CHECK(m.walk[static_cast<std::size_t>(m.index(1, 1))] == 1);
    CHECK(m.walk[static_cast<std::size_t>(m.index(2, 1))] == 0);
    REQUIRE(m.points.size() == 1);
    CHECK(m.points[0].tags == std::vector<std::string>{"entry"});
    REQUIRE(m.regions.size() == 1);  // the untagged polygon carries no meaning
    CHECK(m.regions[0].tags == std::vector<std::string>{"keep_clear"});
    CHECK(m.regions[0].cells.size() == 3);
}

}  // namespace

TEST_CASE("TMX: every tile data encoding gives the same map") {
    check_ground(parse_tmx(tmx(R"(<data encoding="csv">1,1,1,
1,2,1</data>)"), "a.tmx", ""));
    check_ground(parse_tmx(tmx(R"(<data encoding="base64">AQAAAAEAAAABAAAAAQAAAAIAAAABAAAA</data>)"), "b.tmx", ""));
    check_ground(parse_tmx(tmx(R"(<data encoding="base64" compression="zlib">eNpjZGBgYETCTFAaAAB0AAg=</data>)"), "c.tmx", ""));
    check_ground(parse_tmx(tmx(R"(<data encoding="base64" compression="gzip">H4sIAAAAAAAC/2NkYGBgRMJMUBoApynXiBgAAAA=</data>)"),
                           "d.tmx", ""));
    check_ground(parse_tmx(tmx(R"(<data><tile gid="1"/><tile gid="1"/><tile gid="1"/><tile gid="1"/><tile gid="2"/><tile gid="1"/></data>)"),
                           "e.tmx", ""));
    CHECK_THROWS_AS(parse_tmx(tmx(R"(<data encoding="base64" compression="zstd">AAAA</data>)"), "f.tmx", ""),
                    std::runtime_error);
}

TEST_CASE("TMX: tile classes can be given a meaning") {
    const std::string map = R"(<map orientation="orthogonal" width="2" height="1" tilewidth="8" tileheight="8">
 <tileset firstgid="1" name="t"><tile id="0" type="Rock"/></tileset>
 <layer name="g" width="2" height="1"><data encoding="csv">1,2</data></layer></map>)";
    const Map plain = parse_tmx(map, "m.tmx", "");
    CHECK(plain.walk[0] == 1);
    REQUIRE(plain.notes.size() == 1);  // no collision data anywhere
    ReaderOptions opt;
    opt.meanings["rock"] = "wall";
    const Map ruled = parse_tmx(map, "m.tmx", "", opt);
    CHECK(ruled.walk[0] == 0);
    CHECK(ruled.walk[1] == 1);
    CHECK(ruled.notes.empty());
}
