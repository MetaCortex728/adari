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

struct TileInfo {
    bool solid = false;
    bool blocks_sight = false;
};

json prop(const json& owner, const std::string& name) {
    if (!owner.contains("properties")) return nullptr;
    for (const auto& p : owner["properties"])
        if (p.value("name", "") == name) return p.value("value", json());
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
    if (o.contains("class") && o["class"].is_string() && !o["class"].get<std::string>().empty())
        return o["class"].get<std::string>();
    if (o.contains("type") && o["type"].is_string()) return o["type"].get<std::string>();
    return {};
}

std::vector<std::string> csv(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
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

TileInfo tile_info(const json& tile) {
    TileInfo t;
    const std::string cls = lower(type_of(tile));
    const bool has_shapes = tile.contains("objectgroup") && tile["objectgroup"].contains("objects") &&
                            !tile["objectgroup"]["objects"].empty();
    t.solid = prop_bool(tile, "solid", prop_bool(tile, "collision", has_shapes || cls == "wall" || cls == "solid"));
    t.blocks_sight = prop_bool(tile, "blocks_sight", t.solid);
    return t;
}

}  // namespace

Map parse_tiled(const std::string& text, const std::string& file, const std::string& base_dir) {
    json j;
    try {
        j = json::parse(text);
    } catch (const json::parse_error& e) {
        fail(file, std::string("not valid JSON: ") + e.what());
    }
    const std::string orient = j.value("orientation", "orthogonal");
    if (orient != "orthogonal") fail(file, "only orthogonal Tiled maps are supported in this version (got '" + orient + "')");
    if (j.value("infinite", false)) fail(file, "infinite Tiled maps are not supported in this version");
    const int tw = j.value("tilewidth", 0);
    const int th = j.value("tileheight", 0);
    if (tw <= 0 || th <= 0) fail(file, "missing tilewidth or tileheight");

    Map map;
    map.name = file;
    map.source_file = file;
    map.resize(j.value("width", 0), j.value("height", 0));
    if (map.width <= 0 || map.height <= 0) fail(file, "missing width or height");
    {
        const json cs = prop(j, "adari_cell_size");
        if (cs.is_number()) map.cell_size = cs.get<double>();
    }
    std::fill(map.walk.begin(), map.walk.end(), 0);  // walkable only where a tile exists
    std::vector<std::uint8_t> any_tile(map.size(), 0);
    std::vector<std::uint8_t> solid(map.size(), 0);

    // Tile properties by global id.
    std::map<std::uint32_t, TileInfo> info;
    for (const auto& ts_ref : j.value("tilesets", json::array())) {
        json ts = ts_ref;
        const auto first = ts_ref.value("firstgid", 1u);
        if (ts_ref.contains("source")) {
            const std::string src = ts_ref["source"].get<std::string>();
            if (lower(src).size() >= 4 && lower(src).substr(src.size() - 4) == ".tsx")
                fail(file, "external tileset '" + src + "' is XML (.tsx); save it as JSON (.tsj)");
            try {
                ts = json::parse(read_file(join_path(base_dir, src)));
            } catch (const std::exception& e) {
                fail(file, "cannot read tileset '" + src + "': " + e.what());
            }
        }
        for (const auto& tile : ts.value("tiles", json::array()))
            info[first + tile.value("id", 0u)] = tile_info(tile);
    }

    const std::vector<std::string> optional = csv(prop_str(j, "adari_optional_groups"));

    std::function<void(const json&)> walk_layers;
    auto handle_object = [&](const json& o) {
        const double ox = o.value("x", 0.0) / tw;
        const double oy = o.value("y", 0.0) / th;
        const std::string name = o.value("name", std::string());
        std::vector<std::string> tags;
        const std::string cls = type_of(o);
        if (!cls.empty()) tags.push_back(cls);
        for (const auto& t : csv(prop_str(o, "adari_tags"))) tags.push_back(t);
        const std::string label = name.empty() ? (cls.empty() ? "object " + std::to_string(o.value("id", 0)) : cls) : name;

        if (o.value("point", false)) {
            map.points.push_back(Point{label, tags, ox, oy});
            return;
        }
        // Outline in cell units, rotated clockwise around (x, y) as Tiled does.
        std::vector<std::pair<double, double>> local;
        const double w = o.value("width", 0.0) / tw;
        const double h = o.value("height", 0.0) / th;
        if (o.contains("polygon")) {
            for (const auto& p : o["polygon"]) local.emplace_back(p.value("x", 0.0) / tw, p.value("y", 0.0) / th);
        } else if (o.value("ellipse", false)) {
            for (int k = 0; k < 24; ++k) {
                const double a = 2.0 * 3.14159265358979323846 * k / 24.0;
                local.emplace_back(w / 2 + w / 2 * std::cos(a), h / 2 + h / 2 * std::sin(a));
            }
        } else if (o.contains("gid")) {  // tile objects anchor at the bottom-left
            local = {{0, -h}, {w, -h}, {w, 0}, {0, 0}};
        } else {
            if (w <= 0 || h <= 0) {  // zero-size rectangle: treat as a point
                map.points.push_back(Point{label, tags, ox, oy});
                return;
            }
            local = {{0, 0}, {w, 0}, {w, h}, {0, h}};
        }
        const double rot = o.value("rotation", 0.0) * 3.14159265358979323846 / 180.0;
        std::vector<std::pair<double, double>> poly;
        for (const auto& [lx, ly] : local)
            poly.emplace_back(ox + lx * std::cos(rot) - ly * std::sin(rot), oy + lx * std::sin(rot) + ly * std::cos(rot));
        const std::vector<int> cells = rasterize(map, poly);

        const std::string variant = prop_str(o, "variant");
        const bool is_solid = prop_bool(o, "solid", false);
        const bool sight = prop_bool(o, "blocks_sight", is_solid || !variant.empty());
        if (!variant.empty()) {
            VariantOption& opt = option_for(map, variant, file);
            for (const auto& t : tags) opt.tags.push_back(t);
            if (prop_bool(o, "blocks_walk", true)) opt.block_walk.insert(opt.block_walk.end(), cells.begin(), cells.end());
            if (sight) opt.block_sight.insert(opt.block_sight.end(), cells.begin(), cells.end());
            return;
        }
        if (is_solid || prop_bool(o, "blocks_sight", false)) {
            for (int c : cells) {
                if (is_solid) solid[static_cast<std::size_t>(c)] = 1;
                if (sight) map.sight[static_cast<std::size_t>(c)] = 1;
            }
            if (tags.empty()) return;
        }
        Region r;
        r.name = label;
        r.tags = tags;
        r.door = prop_bool(o, "door", lower(cls) == "door");
        if (r.door && std::find(r.tags.begin(), r.tags.end(), "door") == r.tags.end()) r.tags.push_back("door");
        r.cells = cells;
        map.regions.push_back(r);
        // A tagged object with a centre also counts as a point, so selectors
        // like "spawn" work for rectangles too.
        if (!poly.empty() && !r.door) {
            double cx = 0, cy = 0;
            for (const auto& [x, y] : poly) {
                cx += x;
                cy += y;
            }
            map.points.push_back(Point{label, tags, cx / static_cast<double>(poly.size()), cy / static_cast<double>(poly.size())});
        }
    };
    walk_layers = [&](const json& layers) {
        for (const auto& layer : layers) {
            if (prop_str(layer, "adari") == "ignore") continue;
            const std::string type = layer.value("type", "");
            if (type == "group") {
                walk_layers(layer.value("layers", json::array()));
            } else if (type == "tilelayer") {
                if (layer.value("encoding", "csv") != "csv" || layer.contains("compression") ||
                    !layer.contains("data") || !layer["data"].is_array())
                    fail(file, "tile layer '" + layer.value("name", "") +
                                   "' must use CSV encoding (Map Properties > Tile Layer Format)");
                const auto& data = layer["data"];
                const int lw = layer.value("width", map.width);
                const int lx = layer.value("x", 0);
                const int ly = layer.value("y", 0);
                for (std::size_t k = 0; k < data.size(); ++k) {
                    const auto gid = data[k].get<std::uint32_t>() & kGidMask;
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
    for (std::size_t c = 0; c < map.size(); ++c) {
        map.walk[c] = any_tile[c] && !solid[c];
        if (!any_tile[c]) map.sight[c] = 1;  // void blocks sight too
    }
    for (const auto& g : optional) set_optional(map, g, file);
    finish(map, file);
    return map;
}

// ------------------------------------------------------------------- LDtk

namespace {

enum class IntMeaning { kFloor, kWall, kLowBlock, kSightOnly };

IntMeaning meaning_of(const std::string& identifier) {
    const std::string id = lower(identifier);
    auto has = [&](const char* w) { return id.find(w) != std::string::npos; };
    if (has("wall") || has("solid") || has("collision") || has("block") || has("rock")) return IntMeaning::kWall;
    if (has("water") || has("pit") || has("hole") || has("void") || has("chasm") || has("lava") ||
        has("fence") || has("glass") || has("window"))
        return IntMeaning::kLowBlock;
    if (has("foliage") || has("bush") || has("smoke") || has("curtain")) return IntMeaning::kSightOnly;
    return IntMeaning::kFloor;
}

json field(const json& entity, const std::string& name) {
    for (const auto& f : entity.value("fieldInstances", json::array()))
        if (f.value("__identifier", "") == name) return f.value("__value", json());
    return nullptr;
}

}  // namespace

Map parse_ldtk(const std::string& text, const std::string& file, const std::string& base_dir,
               const std::string& level_name) {
    json j;
    try {
        j = json::parse(text);
    } catch (const json::parse_error& e) {
        fail(file, std::string("not valid JSON: ") + e.what());
    }
    std::vector<json> levels;
    for (const auto& l : j.value("levels", json::array())) levels.push_back(l);
    for (const auto& w : j.value("worlds", json::array()))
        for (const auto& l : w.value("levels", json::array())) levels.push_back(l);
    if (levels.empty()) fail(file, "the project has no levels");
    json level;
    for (const auto& l : levels)
        if (level_name.empty() || l.value("identifier", "") == level_name) {
            level = l;
            break;
        }
    if (level.is_null()) fail(file, "no level named '" + level_name + "'");
    if (level.contains("externalRelPath") && level["externalRelPath"].is_string() &&
        (!level.contains("layerInstances") || level["layerInstances"].is_null())) {
        const std::string rel = level["externalRelPath"].get<std::string>();
        try {
            level = json::parse(read_file(join_path(base_dir, rel)));
        } catch (const std::exception& e) {
            fail(file, "cannot read external level '" + rel + "': " + e.what());
        }
    }

    // IntGrid value meanings from the layer definitions.
    std::map<int, std::map<int, IntMeaning>> meanings;  // layerDefUid -> value -> meaning
    for (const auto& ld : j["defs"].value("layers", json::array()))
        for (const auto& v : ld.value("intGridValues", json::array()))
            meanings[ld.value("uid", 0)][v.value("value", 0)] = meaning_of(v.value("identifier", ""));

    Map map;
    map.source_file = file;
    map.name = level.value("identifier", file);
    const json layers = level.value("layerInstances", json::array());
    int grid = 0;
    for (const auto& li : layers) {
        if (li.value("__type", "") != "IntGrid") continue;
        const int cw = li.value("__cWid", 0);
        const int ch = li.value("__cHei", 0);
        const int gs = li.value("__gridSize", 0);
        if (grid == 0) {
            grid = gs;
            map.resize(cw, ch);
        } else if (gs != grid || cw != map.width || ch != map.height) {
            fail(file, "IntGrid layers with different grid sizes are not supported in this version");
        }
        const auto& m = meanings[li.value("layerDefUid", 0)];
        const auto& csv_values = li.value("intGridCsv", json::array());
        for (std::size_t k = 0; k < csv_values.size() && k < map.size(); ++k) {
            const int v = csv_values[k].get<int>();
            if (v == 0) continue;
            const auto it = m.find(v);
            const IntMeaning mean = it == m.end() ? IntMeaning::kFloor : it->second;
            if (mean == IntMeaning::kWall || mean == IntMeaning::kLowBlock) map.walk[k] = 0;
            if (mean == IntMeaning::kWall || mean == IntMeaning::kSightOnly) map.sight[k] = 1;
        }
    }
    if (grid == 0) fail(file, "level '" + map.name + "' has no IntGrid layer to read walls from");

    for (const auto& li : layers) {
        if (li.value("__type", "") != "Entities") continue;
        const double offx = li.value("__pxTotalOffsetX", 0.0);
        const double offy = li.value("__pxTotalOffsetY", 0.0);
        for (const auto& e : li.value("entityInstances", json::array())) {
            const std::string id = e.value("__identifier", "");
            std::vector<std::string> tags = {id};
            if (lower(id) != id) tags.push_back(lower(id));
            for (const auto& t : e.value("__tags", json::array())) tags.push_back(t.get<std::string>());
            const json px = e.value("px", json::array({0, 0}));
            const double pivx = e.value("__pivot", json::array({0.5, 0.5})).at(0).get<double>();
            const double pivy = e.value("__pivot", json::array({0.5, 0.5})).at(1).get<double>();
            const double w = e.value("width", grid);
            const double h = e.value("height", grid);
            // Top-left of the entity box in pixels, then in cells.
            const double left = (px.at(0).get<double>() + offx - pivx * w) / grid;
            const double top = (px.at(1).get<double>() + offy - pivy * h) / grid;
            const double cw = w / grid;
            const double chh = h / grid;
            const json name_field = field(e, "name");
            const std::string name = name_field.is_string() ? name_field.get<std::string>() : id;
            const json variant = field(e, "variant");
            const std::vector<std::pair<double, double>> poly = {
                {left, top}, {left + cw, top}, {left + cw, top + chh}, {left, top + chh}};
            if (variant.is_string() && !variant.get<std::string>().empty()) {
                VariantOption& opt = option_for(map, variant.get<std::string>(), file);
                for (const auto& t : tags) opt.tags.push_back(t);
                const auto cells = rasterize(map, poly);
                const json bs = field(e, "blocks_sight");
                opt.block_walk.insert(opt.block_walk.end(), cells.begin(), cells.end());
                if (!bs.is_boolean() || bs.get<bool>()) opt.block_sight.insert(opt.block_sight.end(), cells.begin(), cells.end());
                continue;
            }
            map.points.push_back(Point{name, tags, left + cw / 2, top + chh / 2});
            const json door = field(e, "door");
            const bool is_door = (door.is_boolean() && door.get<bool>()) || lower(id) == "door";
            if (is_door || cw > 1.0 || chh > 1.0) {
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
}

// ----------------------------------------------------------------- loader

Map load_map(const std::string& path, const std::string& level) {
    const std::string text = read_file(path);
    const std::string ext = lower(path.substr(path.find_last_of('.') == std::string::npos ? path.size() : path.find_last_of('.')));
    const std::string dir = dir_of(path);
    if (ext == ".txt" || ext == ".map") return parse_ascii(text, path);
    if (ext == ".tmj") return parse_tiled(text, path, dir);
    if (ext == ".ldtk") return parse_ldtk(text, path, dir, level);
    if (ext == ".json") {
        if (text.find("\"adari_map\"") != std::string::npos) return parse_native(text, path);
        if (text.find("\"tiledversion\"") != std::string::npos) return parse_tiled(text, path, dir);
        if (text.find("\"jsonVersion\"") != std::string::npos) return parse_ldtk(text, path, dir, level);
    }
    throw std::runtime_error(path + ": unknown map format (expected .txt, .tmj, .ldtk, or JSON from Adari, Tiled or LDtk)");
}

}  // namespace adari
