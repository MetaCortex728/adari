// SPDX-License-Identifier: Apache-2.0
#include "adari/readers.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "decode.hpp"

namespace adari {

using nlohmann::json;

namespace {

[[noreturn]] void fail(const std::string& file, const std::string& what, int line = 0) {
    std::string msg = file;
    if (line > 0) msg += ":" + std::to_string(line);
    throw std::runtime_error(msg + ": " + what);
}

std::vector<std::string> split_ws(const std::string& s) {
    std::istringstream is(s);
    std::vector<std::string> out;
    for (std::string t; is >> t;) out.push_back(t);
    return out;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open '" + path + "'");
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string join_path(const std::string& dir, const std::string& rel) {
    if (dir.empty() || rel.empty() || rel[0] == '/' || rel[0] == '\\' ||
        (rel.size() > 1 && rel[1] == ':'))
        return rel;
    const char last = dir.back();
    return (last == '/' || last == '\\') ? dir + rel : dir + "/" + rel;
}

std::string dir_of(const std::string& path) {
    const auto p = path.find_last_of("/\\");
    return p == std::string::npos ? std::string() : path.substr(0, p);
}

// Finds or creates a variant option "group.option".
VariantOption& option_for(Map& map, const std::string& full, const std::string& file) {
    const auto dot = full.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == full.size())
        fail(file, "variant option '" + full + "' must be written group.option");
    const std::string g = full.substr(0, dot);
    const std::string o = full.substr(dot + 1);
    auto git = std::find_if(map.groups.begin(), map.groups.end(),
                            [&](const VariantGroup& x) { return x.name == g; });
    if (git == map.groups.end()) {
        map.groups.push_back(VariantGroup{g, {}, false});
        git = map.groups.end() - 1;
    }
    auto oit = std::find_if(git->options.begin(), git->options.end(),
                            [&](const VariantOption& x) { return x.name == o; });
    if (oit == git->options.end()) {
        git->options.push_back(VariantOption{o, {}, {}, {}});
        oit = git->options.end() - 1;
    }
    return *oit;
}

void set_optional(Map& map, const std::string& group, const std::string& file) {
    for (VariantGroup& g : map.groups)
        if (g.name == group) {
            g.optional = true;
            return;
        }
    fail(file, "group '" + group + "' has no options");
}

void dedupe(std::vector<int>& v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
}

void finish(Map& map, const std::string& file) {
    for (Region& r : map.regions) dedupe(r.cells);
    for (VariantGroup& g : map.groups)
        for (VariantOption& o : g.options) {
            dedupe(o.block_walk);
            dedupe(o.block_sight);
        }
    const std::string err = validate(map);
    if (!err.empty()) fail(file, err);
}

}  // namespace

std::vector<int> rasterize(const Map& map, const std::vector<std::pair<double, double>>& poly) {
    std::vector<int> out;
    if (poly.size() < 3) return out;
    double minx = poly[0].first, maxx = minx, miny = poly[0].second, maxy = miny;
    for (const auto& [x, y] : poly) {
        minx = std::min(minx, x);
        maxx = std::max(maxx, x);
        miny = std::min(miny, y);
        maxy = std::max(maxy, y);
    }
    const int x0 = std::max(0, static_cast<int>(std::floor(minx)));
    const int x1 = std::min(map.width - 1, static_cast<int>(std::ceil(maxx)));
    const int y0 = std::max(0, static_cast<int>(std::floor(miny)));
    const int y1 = std::min(map.height - 1, static_cast<int>(std::ceil(maxy)));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const double px = x + 0.5;
            const double py = y + 0.5;
            bool in = false;
            for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
                const auto& [xi, yi] = poly[i];
                const auto& [xj, yj] = poly[j];
                if ((yi > py) != (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) in = !in;
            }
            if (in) out.push_back(map.index(x, y));
        }
    return out;
}

// ------------------------------------------------------------------ ASCII

Map parse_ascii(const std::string& text, const std::string& file) {
    struct Legend {
        std::string kind;  // point, region, door, option
        std::string name;
        std::vector<std::string> tags;
        bool walk_only = false;
        bool sight_only = false;
    };
    std::map<char, Legend> legend;
    std::vector<std::string> optional_groups;
    std::vector<std::string> rows;
    Map map;
    map.name = file;
    map.source_file = file;

    std::istringstream is(text);
    std::string line;
    int lineno = 0;
    bool in_map = false;
    while (std::getline(is, line)) {
        ++lineno;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (in_map) {
            if (line.empty()) continue;
            if (rows.empty()) map.ascii_first_line = lineno;
            rows.push_back(line);
            continue;
        }
        const auto words = split_ws(line);
        if (words.empty() || words[0][0] == '#') continue;
        if (words[0] == "map") {
            in_map = true;
        } else if (words[0] == "name" && words.size() >= 2) {
            map.name = words[1];
        } else if (words[0] == "cell_size" && words.size() == 2) {
            try {
                map.cell_size = std::stod(words[1]);
            } catch (...) {
                fail(file, "cell_size must be a number", lineno);
            }
        } else if (words[0] == "group" && words.size() == 3 && words[2] == "optional") {
            optional_groups.push_back(words[1]);
        } else if (words[0] == "legend" && words.size() >= 4 && words[1].size() == 1) {
            Legend l;
            l.kind = words[2];
            l.name = words[3];
            if (l.kind != "point" && l.kind != "region" && l.kind != "door" && l.kind != "option")
                fail(file, "legend kind must be point, region, door or option", lineno);
            if (l.kind == "option") {
                for (std::size_t i = 4; i < words.size(); ++i) {
                    if (words[i] == "walk") l.walk_only = true;
                    else if (words[i] == "sight") l.sight_only = true;
                    else l.tags.push_back(words[i]);
                }
            } else {
                l.tags.push_back(l.name);
                if (l.kind == "door") l.tags.push_back("door");
                for (std::size_t i = 4; i < words.size(); ++i) l.tags.push_back(words[i]);
            }
            const char ch = words[1][0];
            if (std::string("#. ~%v^<>").find(ch) != std::string::npos)
                fail(file, std::string("'") + ch + "' is a built-in cell and cannot be redefined", lineno);
            legend[ch] = l;
        } else {
            fail(file, "unknown line '" + line + "'", lineno);
        }
    }
    if (rows.empty()) fail(file, "no 'map' section");
    std::size_t w = 0;
    for (const auto& r : rows) w = std::max(w, r.size());
    map.resize(static_cast<int>(w), static_cast<int>(rows.size()));

    std::map<std::string, std::size_t> region_index;
    std::map<std::string, int> point_count;
    for (int y = 0; y < map.height; ++y) {
        const std::string& row = rows[static_cast<std::size_t>(y)];
        for (int x = 0; x < map.width; ++x) {
            const char ch = static_cast<std::size_t>(x) < row.size() ? row[static_cast<std::size_t>(x)] : ' ';
            const int i = map.index(x, y);
            const auto ui = static_cast<std::size_t>(i);
            switch (ch) {
                case '#':
                case ' ': map.walk[ui] = 0; map.sight[ui] = 1; continue;
                case '.': continue;
                case '~': map.walk[ui] = 0; continue;
                case '%': map.sight[ui] = 1; continue;
                case 'v':
                    map.edge_block[ui] |= kNorth;
                    if (map.inside(x, y + 1)) map.edge_block[static_cast<std::size_t>(map.index(x, y + 1))] |= kNorth;
                    continue;
                case '^':
                    map.edge_block[ui] |= kSouth;
                    if (map.inside(x, y - 1)) map.edge_block[static_cast<std::size_t>(map.index(x, y - 1))] |= kSouth;
                    continue;
                case '>':
                    map.edge_block[ui] |= kWest;
                    if (map.inside(x + 1, y)) map.edge_block[static_cast<std::size_t>(map.index(x + 1, y))] |= kWest;
                    continue;
                case '<':
                    map.edge_block[ui] |= kEast;
                    if (map.inside(x - 1, y)) map.edge_block[static_cast<std::size_t>(map.index(x - 1, y))] |= kEast;
                    continue;
                default: break;
            }
            const auto it = legend.find(ch);
            if (it == legend.end())
                fail(file, std::string("cell '") + ch + "' has no legend entry", map.ascii_first_line + y);
            const Legend& l = it->second;
            if (l.kind == "point") {
                const int n = ++point_count[l.name];
                Point p;
                p.name = n == 1 ? l.name : l.name + "#" + std::to_string(n);
                p.tags = l.tags;
                p.x = x + 0.5;
                p.y = y + 0.5;
                map.points.push_back(p);
            } else if (l.kind == "region" || l.kind == "door") {
                auto rit = region_index.find(l.name);
                if (rit == region_index.end()) {
                    Region r;
                    r.name = l.name;
                    r.tags = l.tags;
                    r.door = l.kind == "door";
                    map.regions.push_back(r);
                    rit = region_index.emplace(l.name, map.regions.size() - 1).first;
                }
                map.regions[rit->second].cells.push_back(i);
            } else {
                VariantOption& o = option_for(map, l.name, file);
                for (const auto& t : l.tags)
                    if (std::find(o.tags.begin(), o.tags.end(), t) == o.tags.end()) o.tags.push_back(t);
                if (!l.sight_only) o.block_walk.push_back(i);
                if (!l.walk_only) o.block_sight.push_back(i);
            }
        }
    }
    for (const auto& g : optional_groups) set_optional(map, g, file);
    finish(map, file);
    return map;
}

// ----------------------------------------------------------------- native

Map parse_native(const std::string& text, const std::string& file) {
    json j;
    try {
        j = json::parse(text);
    } catch (const json::parse_error& e) {
        fail(file, std::string("not valid JSON: ") + e.what());
    }
    if (!j.contains("adari_map")) fail(file, "missing 'adari_map' version field");
    if (j["adari_map"] != 1) fail(file, "unsupported adari_map version");
    if (!j.contains("rows") || !j["rows"].is_array()) fail(file, "missing 'rows'");

    std::string ascii;
    if (j.contains("cell_size")) ascii += "cell_size " + std::to_string(j["cell_size"].get<double>()) + "\n";
    if (j.contains("legend"))
        for (const auto& [k, v] : j["legend"].items()) ascii += "legend " + k + " " + v.get<std::string>() + "\n";
    if (j.contains("optional_groups"))
        for (const auto& g : j["optional_groups"]) ascii += "group " + g.get<std::string>() + " optional\n";
    ascii += "map\n";
    for (const auto& r : j["rows"]) ascii += r.get<std::string>() + "\n";
    Map map = parse_ascii(ascii, file);
    map.ascii_first_line = 0;  // rows are JSON strings, not file lines
    if (j.contains("name")) map.name = j["name"].get<std::string>();

    auto poly_of = [&](const json& o) {
        std::vector<std::pair<double, double>> p;
        for (const auto& v : o.at("polygon")) p.emplace_back(v.at(0).get<double>(), v.at(1).get<double>());
        return p;
    };
    auto tags_of = [](const json& o) {
        std::vector<std::string> t;
        if (o.contains("tags"))
            for (const auto& v : o["tags"]) t.push_back(v.get<std::string>());
        return t;
    };
    try {
        if (j.contains("points"))
            for (const auto& p : j["points"])
                map.points.push_back(Point{p.at("name").get<std::string>(), tags_of(p), p.at("x").get<double>(),
                                           p.at("y").get<double>()});
        if (j.contains("regions"))
            for (const auto& r : j["regions"]) {
                Region reg;
                reg.name = r.at("name").get<std::string>();
                reg.tags = tags_of(r);
                reg.door = r.value("door", false);
                reg.cells = rasterize(map, poly_of(r));
                map.regions.push_back(reg);
            }
        if (j.contains("options"))
            for (const auto& o : j["options"]) {
                VariantOption& opt = option_for(map, o.at("group").get<std::string>() + "." + o.at("name").get<std::string>(), file);
                for (const auto& t : tags_of(o)) opt.tags.push_back(t);
                const std::string blocks = o.value("blocks", std::string("both"));
                const auto cells = rasterize(map, poly_of(o));
                if (blocks != "sight") opt.block_walk.insert(opt.block_walk.end(), cells.begin(), cells.end());
                if (blocks != "walk") opt.block_sight.insert(opt.block_sight.end(), cells.begin(), cells.end());
            }
    } catch (const json::exception& e) {
        fail(file, std::string("bad points, regions or options: ") + e.what());
    }
    finish(map, file);
    return map;
}

// ------------------------------------------------------------------ Tiled

namespace {

constexpr std::uint32_t kGidMask = 0x0FFFFFFFu;  // clears the four flip/rotation flags

enum class Meaning { kFloor, kWall, kLow, kSight, kUnknown };

Meaning parse_meaning(const std::string& s, const std::string& file) {
    const std::string m = lower(s);
    if (m == "wall" || m == "solid") return Meaning::kWall;
    if (m == "low" || m == "walk") return Meaning::kLow;
    if (m == "sight") return Meaning::kSight;
    if (m == "floor" || m == "none") return Meaning::kFloor;
    fail(file, "meaning must be wall, low, sight or floor, not '" + s + "'");
}

// User overrides first, then nothing (the caller guesses).
Meaning override_for(const ReaderOptions& opt, const std::string& name, const std::string& file) {
    const std::string key = lower(name);
    for (const auto& [k, v] : opt.meanings)
        if (lower(k) == key) return parse_meaning(v, file);
    return Meaning::kUnknown;
}

// Safe string read: missing or null values give the fallback.
std::string s(const json& j, const char* key, const std::string& fallback = {}) {
    if (!j.is_object()) return fallback;
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : fallback;
}

double d(const json& j, const char* key, double fallback = 0.0) {
    if (!j.is_object()) return fallback;
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<double>() : fallback;
}

bool b(const json& j, const char* key, bool fallback = false) {
    if (!j.is_object()) return fallback;
    const auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

struct TileInfo {
    bool solid = false;
    bool blocks_sight = false;
};

json prop(const json& owner, const std::string& name) {
    if (!owner.is_object() || !owner.contains("properties") || !owner["properties"].is_array()) return nullptr;
    for (const auto& p : owner["properties"])
        if (s(p, "name") == name) return p.value("value", json());
    return nullptr;
}

bool prop_bool(const json& owner, const std::string& name, bool fallback) {
    const json v = prop(owner, name);
    return v.is_boolean() ? v.get<bool>() : fallback;
}

std::string prop_str(const json& owner, const std::string& name) {
    const json v = prop(owner, name);
    return v.is_string() ? v.get<std::string>() : std::string();
}

std::string type_of(const json& o) {
    const std::string cls = s(o, "class");
    return cls.empty() ? s(o, "type") : cls;
}

std::vector<std::string> csv(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == ',') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else if (!std::isspace(static_cast<unsigned char>(c))) {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

TileInfo tile_info(const json& tile, const ReaderOptions& opt, const std::string& file) {
    TileInfo t;
    const std::string cls = type_of(tile);
    const Meaning m = override_for(opt, cls, file);
    if (m != Meaning::kUnknown) {
        t.solid = m == Meaning::kWall || m == Meaning::kLow;
        t.blocks_sight = m == Meaning::kWall || m == Meaning::kSight;
        return t;
    }
    const bool has_shapes = tile.contains("objectgroup") && tile["objectgroup"].is_object() &&
                            tile["objectgroup"].contains("objects") && !tile["objectgroup"]["objects"].empty();
    const std::string lc = lower(cls);
    t.solid = prop_bool(tile, "solid", prop_bool(tile, "collision", has_shapes || lc == "wall" || lc == "solid"));
    t.blocks_sight = prop_bool(tile, "blocks_sight", t.solid);
    return t;
}

// Tile layer data as global ids, whatever the encoding.
std::vector<std::uint32_t> layer_gids(const json& layer, const std::string& file) {
    std::vector<std::uint32_t> out;
    const json& data = layer.at("data");
    if (data.is_array()) {
        for (const auto& v : data) out.push_back(v.get<std::uint32_t>());
        return out;
    }
    if (!data.is_string()) fail(file, "tile layer '" + s(layer, "name") + "' has no data");
    const std::string enc = s(layer, "encoding", "csv");
    if (enc == "csv") {
        for (const auto& t : csv(data.get<std::string>())) out.push_back(static_cast<std::uint32_t>(std::stoul(t)));
        return out;
    }
    if (enc != "base64") fail(file, "unknown tile layer encoding '" + enc + "'");
    std::vector<std::uint8_t> bytes = detail::base64_decode(data.get<std::string>());
    const std::string comp = s(layer, "compression");
    if (comp == "zlib" || comp == "gzip") bytes = detail::inflate(bytes, comp == "gzip");
    else if (!comp.empty()) fail(file, "tile layer compression '" + comp + "' is not supported (use zlib, gzip or none)");
    if (bytes.size() % 4 != 0) fail(file, "tile layer data is not a whole number of tiles");
    for (std::size_t k = 0; k + 3 < bytes.size(); k += 4)
        out.push_back(static_cast<std::uint32_t>(bytes[k]) | (static_cast<std::uint32_t>(bytes[k + 1]) << 8) |
                      (static_cast<std::uint32_t>(bytes[k + 2]) << 16) | (static_cast<std::uint32_t>(bytes[k + 3]) << 24));
    return out;
}

// ---- TMX / TSX to the JSON layout Tiled itself writes

json xml_properties(const detail::XmlNode& owner) {
    json props = json::array();
    const detail::XmlNode* p = owner.child("properties");
    if (!p) return props;
    for (const detail::XmlNode& prop_node : p->children) {
        if (prop_node.name != "property") continue;
        const std::string type = prop_node.attr_or("type", "string");
        const std::string raw = prop_node.attr("value") ? *prop_node.attr("value") : prop_node.text;
        json v = raw;
        try {
            if (type == "bool") v = raw == "true";
            else if (type == "int") v = std::stoll(raw);
            else if (type == "float") v = std::stod(raw);
        } catch (...) {
            v = raw;
        }
        props.push_back({{"name", prop_node.attr_or("name")}, {"type", type}, {"value", v}});
    }
    return props;
}

double num_attr(const detail::XmlNode& n, const char* key, double fallback = 0.0) {
    const std::string* v = n.attr(key);
    if (!v) return fallback;
    try {
        return std::stod(*v);
    } catch (...) {
        return fallback;
    }
}

json xml_object(const detail::XmlNode& o) {
    json j;
    j["id"] = static_cast<int>(num_attr(o, "id"));
    j["name"] = o.attr_or("name");
    j["type"] = o.attr_or("type");
    j["class"] = o.attr_or("class");
    j["x"] = num_attr(o, "x");
    j["y"] = num_attr(o, "y");
    j["width"] = num_attr(o, "width");
    j["height"] = num_attr(o, "height");
    j["rotation"] = num_attr(o, "rotation");
    if (o.attr("gid")) j["gid"] = static_cast<std::uint32_t>(std::stoul(*o.attr("gid")));
    if (o.child("point")) j["point"] = true;
    if (o.child("ellipse")) j["ellipse"] = true;
    if (const detail::XmlNode* poly = o.child("polygon")) {
        json pts = json::array();
        std::istringstream is(poly->attr_or("points"));
        for (std::string pair; is >> pair;) {
            const auto comma = pair.find(',');
            if (comma == std::string::npos) continue;
            pts.push_back({{"x", std::stod(pair.substr(0, comma))}, {"y", std::stod(pair.substr(comma + 1))}});
        }
        j["polygon"] = pts;
    }
    if (o.child("polyline")) j["polyline"] = true;
    j["properties"] = xml_properties(o);
    return j;
}

json xml_tileset(const detail::XmlNode& ts) {
    json j;
    j["name"] = ts.attr_or("name");
    j["tiles"] = json::array();
    for (const detail::XmlNode& t : ts.children) {
        if (t.name != "tile") continue;
        json tile;
        tile["id"] = static_cast<int>(num_attr(t, "id"));
        tile["type"] = t.attr_or("type");
        tile["class"] = t.attr_or("class");
        tile["properties"] = xml_properties(t);
        if (const detail::XmlNode* og = t.child("objectgroup")) {
            json objs = json::array();
            for (const detail::XmlNode& o : og->children)
                if (o.name == "object") objs.push_back(xml_object(o));
            tile["objectgroup"] = {{"objects", objs}};
        }
        j["tiles"].push_back(tile);
    }
    return j;
}

json xml_layers(const detail::XmlNode& parent, const std::string& file) {
    json layers = json::array();
    for (const detail::XmlNode& n : parent.children) {
        json l;
        l["name"] = n.attr_or("name");
        l["properties"] = xml_properties(n);
        if (n.name == "layer") {
            l["type"] = "tilelayer";
            l["width"] = static_cast<int>(num_attr(n, "width"));
            l["height"] = static_cast<int>(num_attr(n, "height"));
            const detail::XmlNode* data = n.child("data");
            if (!data) fail(file, "layer '" + n.attr_or("name") + "' has no data");
            if (data->child("chunk")) fail(file, "infinite Tiled maps are not supported in this version");
            const std::string enc = data->attr_or("encoding");
            if (enc.empty()) {
                json gids = json::array();
                for (const detail::XmlNode& t : data->children)
                    if (t.name == "tile") gids.push_back(static_cast<std::uint32_t>(std::stoul(t.attr_or("gid", "0"))));
                l["data"] = gids;
            } else {
                l["encoding"] = enc;
                if (data->attr("compression")) l["compression"] = *data->attr("compression");
                l["data"] = data->text;
            }
        } else if (n.name == "objectgroup") {
            l["type"] = "objectgroup";
            l["objects"] = json::array();
            for (const detail::XmlNode& o : n.children)
                if (o.name == "object") l["objects"].push_back(xml_object(o));
        } else if (n.name == "group") {
            l["type"] = "group";
            l["layers"] = xml_layers(n, file);
        } else {
            continue;  // image layers and other elements carry no collision
        }
        layers.push_back(l);
    }
    return layers;
}

json tmx_to_json(const detail::XmlNode& map, const std::string& file) {
    if (map.name != "map") fail(file, "not a Tiled map (root element is <" + map.name + ">)");
    json j;
    j["orientation"] = map.attr_or("orientation", "orthogonal");
    j["infinite"] = map.attr_or("infinite", "0") == "1";
    j["width"] = static_cast<int>(num_attr(map, "width"));
    j["height"] = static_cast<int>(num_attr(map, "height"));
    j["tilewidth"] = static_cast<int>(num_attr(map, "tilewidth"));
    j["tileheight"] = static_cast<int>(num_attr(map, "tileheight"));
    j["properties"] = xml_properties(map);
    j["tilesets"] = json::array();
    for (const detail::XmlNode& ts : map.children) {
        if (ts.name != "tileset") continue;
        json t = ts.attr("source") ? json{{"source", *ts.attr("source")}} : xml_tileset(ts);
        t["firstgid"] = static_cast<std::uint32_t>(num_attr(ts, "firstgid", 1));
        j["tilesets"].push_back(t);
    }
    j["layers"] = xml_layers(map, file);
    return j;
}

Map tiled_from_json(const json& j, const std::string& file, const std::string& base_dir, const ReaderOptions& opt) {
    const std::string orient = s(j, "orientation", "orthogonal");
    if (orient != "orthogonal") fail(file, "only orthogonal Tiled maps are supported in this version (got '" + orient + "')");
    if (b(j, "infinite")) fail(file, "infinite Tiled maps are not supported in this version");
    const double tw = d(j, "tilewidth");
    const double th = d(j, "tileheight");
    if (tw <= 0 || th <= 0) fail(file, "missing tilewidth or tileheight");

    Map map;
    map.name = file;
    map.source_file = file;
    map.resize(static_cast<int>(d(j, "width")), static_cast<int>(d(j, "height")));
    if (map.width <= 0 || map.height <= 0) fail(file, "missing width or height");
    {
        const json cs = prop(j, "adari_cell_size");
        if (cs.is_number()) map.cell_size = cs.get<double>();
    }
    std::fill(map.walk.begin(), map.walk.end(), 0);  // walkable only where a tile exists
    std::vector<std::uint8_t> any_tile(map.size(), 0);
    std::vector<std::uint8_t> solid(map.size(), 0);
    bool any_tile_layers = false;
    bool any_tile_objects = false;

    // Tile properties by global id.
    std::map<std::uint32_t, TileInfo> info;
    for (const auto& ts_ref : j.value("tilesets", json::array())) {
        json ts = ts_ref;
        const auto first = static_cast<std::uint32_t>(d(ts_ref, "firstgid", 1));
        const std::string src = s(ts_ref, "source");
        if (!src.empty()) {
            try {
                const std::string text = read_file(join_path(base_dir, src));
                const std::string ext = lower(src.substr(src.find_last_of('.') == std::string::npos ? src.size() : src.find_last_of('.')));
                ts = ext == ".tsx" ? xml_tileset(detail::parse_xml(text)) : json::parse(text);
            } catch (const std::exception& e) {
                fail(file, "cannot read tileset '" + src + "': " + e.what());
            }
        }
        for (const auto& tile : ts.value("tiles", json::array()))
            info[first + static_cast<std::uint32_t>(d(tile, "id"))] = tile_info(tile, opt, file);
    }

    const std::vector<std::string> optional = csv(prop_str(j, "adari_optional_groups"));

    auto handle_object = [&](const json& o) {
        if (b(o, "polyline")) return;  // open lines enclose no area
        const double ox = d(o, "x") / tw;
        const double oy = d(o, "y") / th;
        const std::string name = s(o, "name");
        std::vector<std::string> tags;
        const std::string cls = type_of(o);
        if (!cls.empty()) tags.push_back(cls);
        for (const auto& t : csv(prop_str(o, "adari_tags"))) tags.push_back(t);
        const std::string label = name.empty() ? (cls.empty() ? "object " + std::to_string(static_cast<int>(d(o, "id"))) : cls) : name;

        if (b(o, "point")) {
            map.points.push_back(Point{label, tags, ox, oy});
            return;
        }
        // Outline in cell units, rotated clockwise around (x, y) as Tiled does.
        std::vector<std::pair<double, double>> local;
        const double w = d(o, "width") / tw;
        const double h = d(o, "height") / th;
        if (o.contains("polygon") && o["polygon"].is_array()) {
            for (const auto& p : o["polygon"]) local.emplace_back(d(p, "x") / tw, d(p, "y") / th);
        } else if (b(o, "ellipse")) {
            for (int k = 0; k < 24; ++k) {
                const double a = 2.0 * 3.14159265358979323846 * k / 24.0;
                local.emplace_back(w / 2 + w / 2 * std::cos(a), h / 2 + h / 2 * std::sin(a));
            }
        } else if (o.contains("gid")) {  // tile objects anchor at the bottom-left
            local = {{0, -h}, {w, -h}, {w, 0}, {0, 0}};
            any_tile_objects = true;
        } else {
            if (w <= 0 || h <= 0) {  // zero-size rectangle: treat as a point
                map.points.push_back(Point{label, tags, ox, oy});
                return;
            }
            local = {{0, 0}, {w, 0}, {w, h}, {0, h}};
        }
        const double rot = d(o, "rotation") * 3.14159265358979323846 / 180.0;
        std::vector<std::pair<double, double>> poly;
        for (const auto& [lx, ly] : local)
            poly.emplace_back(ox + lx * std::cos(rot) - ly * std::sin(rot), oy + lx * std::sin(rot) + ly * std::cos(rot));
        const std::vector<int> cells = rasterize(map, poly);

        const std::string variant = prop_str(o, "variant");
        // A tile object is solid when its tile is (for example a crate tile
        // with a collision shape), unless the object says otherwise.
        bool tile_solid = false, tile_sight = false;
        if (o.contains("gid")) {
            const auto it = info.find(static_cast<std::uint32_t>(d(o, "gid")) & kGidMask);
            if (it != info.end()) {
                tile_solid = it->second.solid;
                tile_sight = it->second.blocks_sight;
            }
        }
        const bool is_solid = prop_bool(o, "solid", tile_solid);
        const bool sight = prop_bool(o, "blocks_sight", is_solid || tile_sight || !variant.empty());
        if (!variant.empty()) {
            VariantOption& vo = option_for(map, variant, file);
            for (const auto& t : tags) vo.tags.push_back(t);
            if (prop_bool(o, "blocks_walk", true)) vo.block_walk.insert(vo.block_walk.end(), cells.begin(), cells.end());
            if (sight) vo.block_sight.insert(vo.block_sight.end(), cells.begin(), cells.end());
            return;
        }
        if (is_solid || sight) {
            for (int c : cells) {
                if (is_solid) solid[static_cast<std::size_t>(c)] = 1;
                if (sight) map.sight[static_cast<std::size_t>(c)] = 1;
            }
            if (tags.empty()) return;
        }
        if (tags.empty()) return;  // untagged shapes carry no meaning for rules
        Region r;
        r.name = label;
        r.tags = tags;
        r.door = prop_bool(o, "door", lower(cls) == "door");
        if (r.door && std::find(r.tags.begin(), r.tags.end(), "door") == r.tags.end()) r.tags.push_back("door");
        r.cells = cells;
        map.regions.push_back(r);
    };
    std::function<void(const json&)> walk_layers = [&](const json& layers) {
        for (const auto& layer : layers) {
            if (prop_str(layer, "adari") == "ignore") continue;
            const std::string type = s(layer, "type");
            if (type == "group") {
                walk_layers(layer.value("layers", json::array()));
            } else if (type == "tilelayer") {
                if (layer.contains("chunks")) fail(file, "infinite Tiled maps are not supported in this version");
                const std::vector<std::uint32_t> gids = layer_gids(layer, file);
                any_tile_layers = any_tile_layers || std::any_of(gids.begin(), gids.end(), [](std::uint32_t g) {
                                      return (g & kGidMask) != 0;
                                  });
                const int lw = static_cast<int>(d(layer, "width", map.width));
                const int lx = static_cast<int>(d(layer, "x"));
                const int ly = static_cast<int>(d(layer, "y"));
                for (std::size_t k = 0; k < gids.size(); ++k) {
                    const auto gid = gids[k] & kGidMask;
                    if (gid == 0) continue;
                    const int x = lx + static_cast<int>(k) % lw;
                    const int y = ly + static_cast<int>(k) / lw;
                    if (!map.inside(x, y)) continue;
                    const auto c = static_cast<std::size_t>(map.index(x, y));
                    any_tile[c] = 1;
                    const auto it = info.find(gid);
                    if (it == info.end()) continue;
                    if (it->second.solid) solid[c] = 1;
                    if (it->second.blocks_sight) map.sight[c] = 1;
                }
            } else if (type == "objectgroup") {
                for (const auto& o : layer.value("objects", json::array())) handle_object(o);
            }
        }
    };
    try {
        walk_layers(j.value("layers", json::array()));
    } catch (const json::exception& e) {
        fail(file, std::string("unexpected Tiled data: ") + e.what());
    }
    // Maps built only from objects have no tile layers; there, empty cells are
    // open space rather than void.
    const bool void_is_open = !any_tile_layers;
    for (std::size_t c = 0; c < map.size(); ++c) {
        const bool present = any_tile[c] || void_is_open;
        map.walk[c] = present && !solid[c];
        if (!present) map.sight[c] = 1;  // void blocks sight too
    }
    if (void_is_open)
        map.notes.push_back("no tile layers: empty cells are open space and only solid objects block");
    const bool any_collision_data = std::any_of(info.begin(), info.end(), [](const auto& kv) {
        return kv.second.solid || kv.second.blocks_sight;
    });
    if (!any_collision_data && !any_tile_objects)
        map.notes.push_back(
            "no tile carries collision data (a 'solid' property, a wall class or collision shapes), so every "
            "tiled cell is floor; mark wall tiles in the tileset or pass --meaning CLASS=wall");
    for (const auto& g : optional) set_optional(map, g, file);
    finish(map, file);
    return map;
}

}  // namespace

Map parse_tiled(const std::string& text, const std::string& file, const std::string& base_dir,
                const ReaderOptions& opt) {
    json j;
    try {
        j = json::parse(text);
    } catch (const json::parse_error& e) {
        fail(file, std::string("not valid JSON: ") + e.what());
    }
    return tiled_from_json(j, file, base_dir, opt);
}

Map parse_tmx(const std::string& text, const std::string& file, const std::string& base_dir,
              const ReaderOptions& opt) {
    json j;
    try {
        j = tmx_to_json(detail::parse_xml(text), file);
    } catch (const std::runtime_error& e) {
        const std::string what = e.what();
        if (what.rfind(file, 0) == 0) throw;
        fail(file, what);
    }
    return tiled_from_json(j, file, base_dir, opt);
}

// ------------------------------------------------------------------- LDtk

namespace {

Meaning guess_meaning(const std::string& identifier, bool collision_layer) {
    const std::string id = lower(identifier);
    auto has = [&](const char* w) { return id.find(w) != std::string::npos; };
    if (has("floor") || has("ground") || has("grass") || has("path") || has("road") || has("ladder") ||
        has("bridge") || has("stair"))
        return Meaning::kFloor;
    if (has("wall") || has("solid") || has("collision") || has("block") || has("rock")) return Meaning::kWall;
    if (has("water") || has("pit") || has("hole") || has("void") || has("chasm") || has("lava") ||
        has("fence") || has("glass") || has("window") || has("spike"))
        return Meaning::kLow;
    if (has("foliage") || has("bush") || has("smoke") || has("curtain")) return Meaning::kSight;
    // In a layer named for collision, an unnamed or unknown value is solid.
    return collision_layer ? Meaning::kWall : Meaning::kFloor;
}

bool collision_layer_name(const std::string& identifier) {
    const std::string id = lower(identifier);
    return id.find("collision") != std::string::npos || id.find("wall") != std::string::npos ||
           id.find("solid") != std::string::npos;
}

json field(const json& entity, const std::string& name) {
    for (const auto& f : entity.value("fieldInstances", json::array()))
        if (s(f, "__identifier") == name) return f.value("__value", json());
    return nullptr;
}

std::vector<json> all_levels(const json& j) {
    std::vector<json> levels;
    for (const auto& l : j.value("levels", json::array())) levels.push_back(l);
    for (const auto& w : j.value("worlds", json::array()))
        for (const auto& l : w.value("levels", json::array())) levels.push_back(l);
    return levels;
}

}  // namespace

std::vector<std::string> ldtk_levels(const std::string& text) {
    std::vector<std::string> out;
    for (const json& l : all_levels(json::parse(text))) out.push_back(s(l, "identifier"));
    return out;
}

Map parse_ldtk(const std::string& text, const std::string& file, const std::string& base_dir,
               const ReaderOptions& opt) {
    json j;
    try {
        j = json::parse(text);
    } catch (const json::parse_error& e) {
        fail(file, std::string("not valid JSON: ") + e.what());
    }
    const std::vector<json> levels = all_levels(j);
    if (levels.empty()) fail(file, "the project has no levels");
    json level;
    for (const auto& l : levels)
        if (opt.level.empty() || s(l, "identifier") == opt.level) {
            level = l;
            break;
        }
    if (level.is_null()) fail(file, "no level named '" + opt.level + "'");
    if (!s(level, "externalRelPath").empty() && (!level.contains("layerInstances") || level["layerInstances"].is_null())) {
        const std::string rel = s(level, "externalRelPath");
        try {
            level = json::parse(read_file(join_path(base_dir, rel)));
        } catch (const std::exception& e) {
            fail(file, "cannot read external level '" + rel + "': " + e.what());
        }
    }

    try {
        // IntGrid value meanings from the layer definitions.
        std::map<int, std::map<int, Meaning>> meanings;  // layerDefUid -> value -> meaning
        const json defs = j.value("defs", json::object());
        for (const auto& ld : defs.value("layers", json::array())) {
            const bool coll = collision_layer_name(s(ld, "identifier"));
            for (const auto& v : ld.value("intGridValues", json::array())) {
                const std::string id = s(v, "identifier");
                Meaning m = id.empty() ? Meaning::kUnknown : override_for(opt, id, file);
                if (m == Meaning::kUnknown) m = guess_meaning(id, coll);
                meanings[static_cast<int>(d(ld, "uid"))][static_cast<int>(d(v, "value"))] = m;
            }
        }

        Map map;
        map.source_file = file;
        map.name = s(level, "identifier", file);
        const json layers = level.value("layerInstances", json::array());

        // The grid comes from the collision IntGrid layer if there is one,
        // otherwise from the first IntGrid layer. Other IntGrid layers are
        // resampled onto it by pixel overlap.
        const json* primary = nullptr;
        for (const auto& li : layers)
            if (s(li, "__type") == "IntGrid" && (primary == nullptr || (collision_layer_name(s(li, "__identifier")) &&
                                                                         !collision_layer_name(s(*primary, "__identifier")))))
                primary = &li;
        if (!primary) fail(file, "level '" + map.name + "' has no IntGrid layer to read walls from");
        const double grid = d(*primary, "__gridSize");
        const double ox0 = d(*primary, "__pxTotalOffsetX");
        const double oy0 = d(*primary, "__pxTotalOffsetY");
        if (grid <= 0) fail(file, "IntGrid layer has no grid size");
        map.resize(static_cast<int>(d(*primary, "__cWid")), static_cast<int>(d(*primary, "__cHei")));

        for (const auto& li : layers) {
            if (s(li, "__type") != "IntGrid") continue;
            const int cw = static_cast<int>(d(li, "__cWid"));
            const double gs = d(li, "__gridSize");
            const double lx = d(li, "__pxTotalOffsetX") - ox0;
            const double ly = d(li, "__pxTotalOffsetY") - oy0;
            const auto& m = meanings[static_cast<int>(d(li, "layerDefUid"))];
            const auto& values = li.value("intGridCsv", json::array());
            for (std::size_t k = 0; k < values.size(); ++k) {
                const int v = values[k].get<int>();
                if (v == 0) continue;
                const auto it = m.find(v);
                const Meaning mean = it == m.end() ? guess_meaning("", collision_layer_name(s(li, "__identifier")))
                                                   : it->second;
                if (mean == Meaning::kFloor) continue;
                // Pixel rectangle of the source cell, mapped to primary cells.
                const double px0 = lx + static_cast<double>(static_cast<int>(k) % cw) * gs;
                const double py0 = ly + static_cast<double>(static_cast<int>(k) / cw) * gs;
                const int x0 = static_cast<int>(std::floor(px0 / grid));
                const int y0 = static_cast<int>(std::floor(py0 / grid));
                const int x1 = static_cast<int>(std::ceil((px0 + gs) / grid)) - 1;
                const int y1 = static_cast<int>(std::ceil((py0 + gs) / grid)) - 1;
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x) {
                        if (!map.inside(x, y)) continue;
                        const auto c = static_cast<std::size_t>(map.index(x, y));
                        if (mean == Meaning::kWall || mean == Meaning::kLow) map.walk[c] = 0;
                        if (mean == Meaning::kWall || mean == Meaning::kSight) map.sight[c] = 1;
                    }
            }
        }

        // Cells where the level draws nothing at all (no IntGrid value, no
        // tile in any tile or auto layer) are void outside the playable area,
        // not floor. Only applied when the level has drawn tiles.
        {
            std::vector<std::uint8_t> drawn(map.size(), 0);
            bool any_drawn_tiles = false;
            auto mark = [&](double px, double py, double gs) {
                const int x0 = static_cast<int>(std::floor(px / grid));
                const int y0 = static_cast<int>(std::floor(py / grid));
                const int x1 = static_cast<int>(std::ceil((px + gs) / grid)) - 1;
                const int y1 = static_cast<int>(std::ceil((py + gs) / grid)) - 1;
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x)
                        if (map.inside(x, y)) drawn[static_cast<std::size_t>(map.index(x, y))] = 1;
            };
            for (const auto& li : layers) {
                const double gs = d(li, "__gridSize", grid);
                const double lx = d(li, "__pxTotalOffsetX") - ox0;
                const double ly = d(li, "__pxTotalOffsetY") - oy0;
                if (s(li, "__type") == "IntGrid") {
                    const int cw = static_cast<int>(d(li, "__cWid"));
                    const auto& values = li.value("intGridCsv", json::array());
                    for (std::size_t k = 0; k < values.size() && cw > 0; ++k)
                        if (values[k].get<int>() != 0)
                            mark(lx + static_cast<double>(static_cast<int>(k) % cw) * gs,
                                 ly + static_cast<double>(static_cast<int>(k) / cw) * gs, gs);
                }
                for (const char* key : {"gridTiles", "autoLayerTiles"})
                    for (const auto& t : li.value(key, json::array())) {
                        const json px = t.value("px", json());
                        if (!px.is_array() || px.size() < 2) continue;
                        any_drawn_tiles = true;
                        mark(lx + px.at(0).get<double>(), ly + px.at(1).get<double>(), gs);
                    }
            }
            if (any_drawn_tiles) {
                std::size_t voided = 0;
                for (std::size_t c = 0; c < map.size(); ++c)
                    if (!drawn[c] && map.walk[c]) {
                        map.walk[c] = 0;
                        map.sight[c] = 1;
                        ++voided;
                    }
                if (voided)
                    map.notes.push_back(std::to_string(voided) +
                                        " cells with nothing drawn on them are treated as void, not floor");
            }
        }

        for (const auto& li : layers) {
            if (s(li, "__type") != "Entities") continue;
            const double offx = d(li, "__pxTotalOffsetX") - ox0;
            const double offy = d(li, "__pxTotalOffsetY") - oy0;
            for (const auto& e : li.value("entityInstances", json::array())) {
                const std::string id = s(e, "__identifier");
                std::vector<std::string> tags = {id};
                if (lower(id) != id) tags.push_back(lower(id));
                for (const auto& t : e.value("__tags", json::array()))
                    if (t.is_string()) tags.push_back(t.get<std::string>());
                const json px = e.value("px", json::array({0, 0}));
                const json pivot = e.value("__pivot", json::array({0.5, 0.5}));
                const double w = d(e, "width", grid);
                const double h = d(e, "height", grid);
                // Top-left of the entity box in pixels, then in cells.
                const double left = (px.at(0).get<double>() + offx - pivot.at(0).get<double>() * w) / grid;
                const double top = (px.at(1).get<double>() + offy - pivot.at(1).get<double>() * h) / grid;
                const double cw = w / grid;
                const double chh = h / grid;
                const json name_field = field(e, "name");
                const std::string name = name_field.is_string() ? name_field.get<std::string>() : id;
                const json variant = field(e, "variant");
                const std::vector<std::pair<double, double>> poly = {
                    {left, top}, {left + cw, top}, {left + cw, top + chh}, {left, top + chh}};
                if (variant.is_string() && !variant.get<std::string>().empty()) {
                    VariantOption& vo = option_for(map, variant.get<std::string>(), file);
                    for (const auto& t : tags) vo.tags.push_back(t);
                    const auto cells = rasterize(map, poly);
                    const json bs = field(e, "blocks_sight");
                    vo.block_walk.insert(vo.block_walk.end(), cells.begin(), cells.end());
                    if (!bs.is_boolean() || bs.get<bool>()) vo.block_sight.insert(vo.block_sight.end(), cells.begin(), cells.end());
                    continue;
                }
                const json door = field(e, "door");
                const bool is_door = (door.is_boolean() && door.get<bool>()) || lower(id) == "door";
                // One cell or smaller: a point. Larger, or a door: a region.
                if (!is_door && cw <= 1.0 && chh <= 1.0) {
                    map.points.push_back(Point{name, tags, left + cw / 2, top + chh / 2});
                } else {
                    Region r;
                    r.name = name;
                    r.tags = tags;
                    r.door = is_door;
                    if (is_door && std::find(r.tags.begin(), r.tags.end(), "door") == r.tags.end()) r.tags.push_back("door");
                    r.cells = rasterize(map, poly);
                    map.regions.push_back(r);
                }
            }
        }
        finish(map, file);
        return map;
    } catch (const json::exception& e) {
        fail(file, std::string("unexpected LDtk data: ") + e.what());
    }
}

// ----------------------------------------------------------------- loader

Map load_map(const std::string& path, const ReaderOptions& opt) {
    const std::string text = read_file(path);
    const auto dot = path.find_last_of('.');
    const std::string ext = lower(dot == std::string::npos ? std::string() : path.substr(dot));
    const std::string dir = dir_of(path);
    if (ext == ".txt" || ext == ".map") return parse_ascii(text, path);
    if (ext == ".tmj") return parse_tiled(text, path, dir, opt);
    if (ext == ".tmx") return parse_tmx(text, path, dir, opt);
    if (ext == ".ldtk") return parse_ldtk(text, path, dir, opt);
    if (ext == ".json") {
        if (text.find("\"adari_map\"") != std::string::npos) return parse_native(text, path);
        if (text.find("\"tiledversion\"") != std::string::npos) return parse_tiled(text, path, dir, opt);
        if (text.find("\"jsonVersion\"") != std::string::npos) return parse_ldtk(text, path, dir, opt);
    }
    throw std::runtime_error(path + ": unknown map format (expected .txt, .tmx, .tmj, .ldtk, or JSON from Adari, Tiled or LDtk)");
}

}  // namespace adari
