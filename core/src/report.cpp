// SPDX-License-Identifier: Apache-2.0
#include "adari/report.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#ifndef ADARI_VERSION_STRING
#define ADARI_VERSION_STRING "0.0.0"
#endif

namespace adari {

using nlohmann::json;
using ordered_json = nlohmann::ordered_json;

const char* version() { return ADARI_VERSION_STRING; }

namespace {

constexpr std::size_t kMaxCellsListed = 200;

ordered_json cells_json(const Map& map, const std::vector<int>& cells) {
    ordered_json a = ordered_json::array();
    for (std::size_t i = 0; i < cells.size() && i < kMaxCellsListed; ++i) {
        const Cell c = map.cell(cells[i]);
        a.push_back({c.x, c.y});
    }
    return a;
}

int primary_cell(const Finding& f) {
    if (!f.path.empty()) return f.path.front();
    if (!f.cells.empty()) return f.cells.front();
    return -1;
}

}  // namespace

std::string to_text(const Map& map, const Report& rep) {
    std::ostringstream os;
    os << "adari " << version() << "  map " << map.name << "  (" << map.width << "x" << map.height
       << " cells, " << map.groups.size() << " variant groups)\n\n";
    for (const RuleResult& r : rep.results) {
        os << "  " << (r.status == Status::kPassed ? "PASS " : r.status == Status::kFailed ? "FAIL " : "SKIP ")
           << r.rule_id << " [" << r.tmpl << ", " << to_string(r.severity) << "]";
        if (!r.method.empty()) os << "  " << r.method;
        if (r.method == "each option") os << " (" << r.variants_checked << " checked)";
        else if (r.variants_total > 1) os << " (" << r.variants_checked << "/" << r.variants_total << " variants)";
        os << "\n";
        if (!r.note.empty()) os << "       " << r.note << "\n";
        for (const Finding& f : rep.findings) {
            if (f.rule_id != r.rule_id) continue;
            os << "       - " << f.message;
            const int c = primary_cell(f);
            if (c >= 0) {
                const Cell cc = map.cell(c);
                os << "  at (" << cc.x << ", " << cc.y << ")";
                if (map.ascii_first_line > 0)
                    os << " " << map.source_file << ":" << (map.ascii_first_line + cc.y) << ":" << (cc.x + 1);
            }
            if (!map.groups.empty() && !f.variant_text.empty()) os << "  [" << f.variant_text << "]";
            os << "\n";
        }
    }
    os << "\n" << rep.count(Severity::kError) << " errors, " << rep.count(Severity::kWarning) << " warnings, "
       << rep.count(Severity::kNote) << " notes";
    const auto skipped = std::count_if(rep.results.begin(), rep.results.end(),
                                       [](const RuleResult& r) { return r.status == Status::kNotChecked; });
    if (skipped) os << ", " << skipped << " rules not checked";
    os << "\n";
    return os.str();
}

std::string to_json(const Map& map, const Report& rep) {
    ordered_json j;
    j["adari_report"] = 1;
    j["tool_version"] = version();
    j["map"] = {{"name", map.name}, {"file", map.source_file}, {"width", map.width},
                {"height", map.height}, {"cell_size", map.cell_size}};
    j["summary"] = {{"failed", rep.failed()},
                    {"errors", rep.count(Severity::kError)},
                    {"warnings", rep.count(Severity::kWarning)},
                    {"notes", rep.count(Severity::kNote)}};
    j["rules"] = ordered_json::array();
    for (const RuleResult& r : rep.results)
        j["rules"].push_back({{"id", r.rule_id},
                              {"template", r.tmpl},
                              {"severity", to_string(r.severity)},
                              {"status", to_string(r.status)},
                              {"method", r.method},
                              {"variants_checked", r.variants_checked},
                              {"variants_total", r.variants_total},
                              {"variants_failed", r.variants_failed},
                              {"note", r.note}});
    j["findings"] = ordered_json::array();
    for (const Finding& f : rep.findings) {
        ordered_json o{{"rule", f.rule_id}, {"template", f.tmpl}, {"severity", to_string(f.severity)},
                       {"message", f.message}, {"variant", f.variant_text}};
        if (f.has_measure) {
            o["measured"] = f.measured;
            o["required"] = f.required;
        }
        o["cells"] = cells_json(map, f.cells);
        o["cell_count"] = f.cells.size();
        if (!f.path.empty()) o["path"] = cells_json(map, f.path);
        j["findings"].push_back(o);
    }
    return j.dump(2) + "\n";
}

std::string to_sarif(const Map& map, const Report& rep) {
    ordered_json run;
    ordered_json driver{{"name", "adari"},
                        {"version", version()},
                        {"informationUri", "https://github.com/MetaCortex728/adari"}};
    driver["rules"] = ordered_json::array();
    for (const RuleResult& r : rep.results)
        driver["rules"].push_back({{"id", r.rule_id},
                                   {"shortDescription", {{"text", "Adari template " + r.tmpl}}},
                                   {"defaultConfiguration", {{"level", to_string(r.severity)}}}});
    run["tool"] = {{"driver", driver}};
    run["results"] = ordered_json::array();
    for (const Finding& f : rep.findings) {
        ordered_json res{{"ruleId", f.rule_id}, {"level", to_string(f.severity)}, {"message", {{"text", f.message}}}};
        ordered_json phys{{"artifactLocation", {{"uri", map.source_file}}}};
        const int c = primary_cell(f);
        if (c >= 0 && map.ascii_first_line > 0) {
            const Cell cc = map.cell(c);
            phys["region"] = {{"startLine", map.ascii_first_line + cc.y}, {"startColumn", cc.x + 1}};
        }
        res["locations"] = ordered_json::array({{{"physicalLocation", phys},
                                                 {"logicalLocations", {{{"name", map.name}, {"kind", "map"}}}}}});
        ordered_json props{{"template", f.tmpl}, {"variant", f.variant_text}, {"cells", cells_json(map, f.cells)}};
        if (!f.path.empty()) props["path"] = cells_json(map, f.path);
        if (f.has_measure) {
            props["measured"] = f.measured;
            props["required"] = f.required;
        }
        res["properties"] = props;
        run["results"].push_back(res);
    }
    for (const RuleResult& r : rep.results)
        if (r.status == Status::kNotChecked)
            run["invocations"] = ordered_json::array(
                {{{"executionSuccessful", true},
                  {"toolExecutionNotifications",
                   ordered_json::array({{{"level", "warning"},
                                         {"message", {{"text", "rule '" + r.rule_id + "' not checked: " + r.note}}}}})}}});
    ordered_json root{{"$schema", "https://json.schemastore.org/sarif-2.1.0.json"}, {"version", "2.1.0"}};
    root["runs"] = ordered_json::array({run});
    return root.dump(2) + "\n";
}

// -------------------------------------------------------------------- PNG

namespace {

std::uint32_t crc32(const std::uint8_t* data, std::size_t n, std::uint32_t crc = 0) {
    static std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (std::size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

void put32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(static_cast<std::uint8_t>(x >> 24));
    v.push_back(static_cast<std::uint8_t>(x >> 16));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x));
}

void chunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data) {
    put32(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    put32(out, crc32(body.data(), body.size()));
}

}  // namespace

std::vector<std::uint8_t> encode_png(int width, int height, const std::vector<std::uint8_t>& rgb) {
    if (width <= 0 || height <= 0 || rgb.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3)
        throw std::runtime_error("encode_png: bad image size");
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (static_cast<std::size_t>(width) * 3 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);  // filter: none
        const auto row = rgb.begin() + static_cast<std::ptrdiff_t>(y) * width * 3;
        raw.insert(raw.end(), row, row + width * 3);
    }
    // zlib stream with stored (uncompressed) deflate blocks.
    std::vector<std::uint8_t> z = {0x78, 0x01};
    std::size_t pos = 0;
    do {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - pos);
        const bool last = pos + n == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n));
        z.push_back(static_cast<std::uint8_t>(~n >> 8));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                 raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
    } while (pos < raw.size());
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t v : raw) {
        a = (a + v) % 65521u;
        b = (b + a) % 65521u;
    }
    put32(z, (b << 16) | a);

    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<std::uint8_t> ihdr;
    put32(ihdr, static_cast<std::uint32_t>(width));
    put32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8-bit, RGB
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

std::vector<std::uint8_t> overlay_png(const Map& map, const Report& rep, int scale) {
    if (scale <= 0) scale = std::clamp(768 / std::max(map.width, map.height), 1, 16);
    const int w = map.width * scale;
    const int h = map.height * scale;
    std::vector<std::uint8_t> img(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
    using RGB = std::array<std::uint8_t, 3>;
    std::vector<RGB> cell(map.size());
    for (std::size_t i = 0; i < map.size(); ++i) {
        if (!map.walk[i]) cell[i] = map.sight[i] ? RGB{40, 44, 52} : RGB{70, 110, 160};  // wall / water
        else cell[i] = map.sight[i] ? RGB{120, 150, 90} : RGB{226, 222, 210};          // foliage / floor
    }
    auto tint = [&](int c, RGB col, double a) {
        RGB& p = cell[static_cast<std::size_t>(c)];
        for (int k = 0; k < 3; ++k)
            p[static_cast<std::size_t>(k)] = static_cast<std::uint8_t>(p[static_cast<std::size_t>(k)] * (1 - a) +
                                                                       col[static_cast<std::size_t>(k)] * a);
    };
    for (const VariantGroup& g : map.groups)
        for (const VariantOption& o : g.options)
            for (int c : o.block_walk) tint(c, {150, 110, 60}, 0.35);
    for (const Region& r : map.regions)
        for (int c : r.cells) tint(c, r.door ? RGB{160, 90, 200} : RGB{80, 160, 200}, 0.35);
    // Options chosen in a failing variant are drawn darker, so the overlay
    // shows which prop caused the failure.
    for (const Finding& f : rep.findings)
        for (std::size_t g = 0; g < f.variant.size() && g < map.groups.size(); ++g) {
            const int pick = f.variant[g];
            if (pick < 0) continue;
            for (int c : map.groups[g].options[static_cast<std::size_t>(pick)].block_walk)
                tint(c, {110, 60, 20}, 0.85);
        }
    for (const Finding& f : rep.findings) {
        const RGB col = f.severity == Severity::kError ? RGB{220, 40, 40} : RGB{230, 170, 30};
        for (int c : f.cells) tint(c, col, 0.7);
        for (int c : f.path) tint(c, {245, 120, 20}, 0.8);
    }
    for (const Point& p : map.points) {
        const Cell c = p.cell();
        if (map.inside(c.x, c.y)) cell[static_cast<std::size_t>(map.index(c.x, c.y))] = {20, 120, 60};
    }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const RGB& p = cell[static_cast<std::size_t>(map.index(x / scale, y / scale))];
            const bool grid = scale >= 6 && (x % scale == 0 || y % scale == 0);
            const std::size_t o = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3;
            for (int k = 0; k < 3; ++k)
                img[o + static_cast<std::size_t>(k)] =
                    grid ? static_cast<std::uint8_t>(p[static_cast<std::size_t>(k)] * 0.85) : p[static_cast<std::size_t>(k)];
        }
    return encode_png(w, h, img);
}

void write_file(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write '" + path + "'");
    out << data;
}

void write_file(const std::string& path, const std::vector<std::uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write '" + path + "'");
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

}  // namespace adari
