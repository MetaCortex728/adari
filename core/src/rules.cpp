// SPDX-License-Identifier: Apache-2.0
#include "adari/rules.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "adari/grid.hpp"

namespace adari {

using nlohmann::json;

const char* to_string(Severity s) {
    switch (s) {
        case Severity::kError: return "error";
        case Severity::kWarning: return "warning";
        case Severity::kNote: return "note";
    }
    return "error";
}

const char* to_string(Status s) {
    switch (s) {
        case Status::kPassed: return "passed";
        case Status::kFailed: return "failed";
        case Status::kNotChecked: return "not_checked";
    }
    return "not_checked";
}

const std::vector<std::string>& templates() {
    static const std::vector<std::string> names = {
        "reachable", "no_isolated_walkable", "path_length", "not_visible_until",
        "visible_from", "near", "clear_of", "no_softlock"};
    return names;
}

std::size_t Report::count(Severity s) const {
    return static_cast<std::size_t>(
        std::count_if(findings.begin(), findings.end(), [s](const Finding& f) { return f.severity == s; }));
}

bool Report::failed() const {
    if (count(Severity::kError) > 0) return true;
    return std::any_of(results.begin(), results.end(), [](const RuleResult& r) {
        return r.status == Status::kNotChecked && r.severity == Severity::kError;
    });
}

// ---------------------------------------------------------------- parsing

namespace {

std::string str(const json& j, const char* key) {
    if (!j.contains(key)) return {};
    if (!j[key].is_string()) throw std::runtime_error(std::string("'") + key + "' must be a string");
    return j[key].get<std::string>();
}

double num(const json& j, const char* key, double fallback) {
    if (!j.contains(key)) return fallback;
    if (!j[key].is_number()) throw std::runtime_error(std::string("'") + key + "' must be a number");
    return j[key].get<double>();
}

Severity parse_severity(const std::string& s) {
    if (s.empty() || s == "error") return Severity::kError;
    if (s == "warning") return Severity::kWarning;
    if (s == "note") return Severity::kNote;
    throw std::runtime_error("severity must be error, warning or note, not '" + s + "'");
}

void require(const Rule& r, const std::string& value, const char* field) {
    if (value.empty())
        throw std::runtime_error("rule '" + r.id + "' (" + r.tmpl + ") needs '" + field + "'");
}

void check_fields(const Rule& r) {
    const std::string& t = r.tmpl;
    if (t == "reachable") { require(r, r.from, "from"); require(r, r.to, "to"); }
    else if (t == "no_isolated_walkable") { require(r, r.from, "from"); }
    else if (t == "path_length") {
        require(r, r.from, "from");
        require(r, r.to, "to");
        if (r.min < 0 && r.max < 0)
            throw std::runtime_error("rule '" + r.id + "' (path_length) needs 'min' or 'max'");
        if (r.min >= 0 && r.max >= 0 && r.min > r.max)
            throw std::runtime_error("rule '" + r.id + "' has min greater than max");
    } else if (t == "not_visible_until") {
        require(r, r.target, "target"); require(r, r.from, "from"); require(r, r.door, "door");
    } else if (t == "visible_from") { require(r, r.target, "target"); require(r, r.from, "from"); }
    else if (t == "near") {
        require(r, r.subject, "subject"); require(r, r.object, "object");
        if (!(r.distance > 0.0))
            throw std::runtime_error("rule '" + r.id + "' (near) needs a positive 'distance'");
    } else if (t == "clear_of") { require(r, r.region, "region"); require(r, r.blocker, "blocker"); }
    else if (t == "no_softlock") { require(r, r.from, "from"); require(r, r.to, "to"); }
    if (r.radius < 0.0) throw std::runtime_error("rule '" + r.id + "' has a negative radius");
}

}  // namespace

RuleSet parse_rules(const std::string& json_text) {
    json root;
    try {
        root = json::parse(json_text);
    } catch (const json::parse_error& e) {
        throw std::runtime_error(std::string("rule file is not valid JSON: ") + e.what());
    }
    if (!root.is_object() || !root.contains("rules") || !root["rules"].is_array())
        throw std::runtime_error("rule file needs a 'rules' array");

    RuleSet set;
    if (root.contains("options")) {
        const json& o = root["options"];
        set.options.max_combinations =
            static_cast<std::size_t>(num(o, "max_combinations", static_cast<double>(set.options.max_combinations)));
        set.options.sample_count =
            static_cast<std::size_t>(num(o, "sample_count", static_cast<double>(set.options.sample_count)));
        set.options.sample_seed =
            static_cast<std::uint32_t>(num(o, "sample_seed", static_cast<double>(set.options.sample_seed)));
        set.options.sight_samples =
            static_cast<std::size_t>(num(o, "sight_samples", static_cast<double>(set.options.sight_samples)));
    }

    if (root.contains("reader")) {
        const json& rd = root["reader"];
        if (!rd.is_object()) throw std::runtime_error("'reader' must be an object");
        if (rd.contains("meanings")) {
            if (!rd["meanings"].is_object()) throw std::runtime_error("'reader.meanings' must map names to meanings");
            for (const auto& [k, v] : rd["meanings"].items()) {
                if (!v.is_string()) throw std::runtime_error("meaning for '" + k + "' must be a string");
                set.meanings[k] = v.get<std::string>();
            }
        }
    }

    std::set<std::string> ids;
    const auto& known = templates();
    for (const json& j : root["rules"]) {
        if (!j.is_object()) throw std::runtime_error("each rule must be an object");
        Rule r;
        r.id = str(j, "id");
        r.tmpl = str(j, "template");
        if (r.id.empty()) throw std::runtime_error("every rule needs an 'id'");
        if (!ids.insert(r.id).second) throw std::runtime_error("duplicate rule id '" + r.id + "'");
        if (std::find(known.begin(), known.end(), r.tmpl) == known.end())
            throw std::runtime_error("rule '" + r.id + "' has unknown template '" + r.tmpl + "'");
        r.severity = parse_severity(str(j, "severity"));
        r.description = str(j, "description");
        r.from = str(j, "from");
        r.to = str(j, "to");
        r.target = str(j, "target");
        r.door = str(j, "door");
        r.subject = str(j, "subject");
        r.object = str(j, "object");
        r.region = str(j, "region");
        r.blocker = str(j, "blocker");
        r.radius = num(j, "radius", 0.0);
        r.min = num(j, "min", -1.0);
        r.max = num(j, "max", -1.0);
        r.distance = num(j, "distance", 0.0);
        r.min_cells = static_cast<std::size_t>(num(j, "min_cells", 1.0));
        if (j.contains("doors")) {
            if (!j["doors"].is_object()) throw std::runtime_error("'doors' must map selectors to open/closed");
            for (const auto& [sel, mode] : j["doors"].items()) {
                const std::string m = mode.is_string() ? mode.get<std::string>() : "";
                if (m != "open" && m != "closed")
                    throw std::runtime_error("door state for '" + sel + "' must be open or closed");
                r.doors.push_back({sel, m == "open" ? DoorMode::kOpen : DoorMode::kClosed});
            }
        }
        check_fields(r);
        set.rules.push_back(std::move(r));
    }
    return set;
}

RuleSet load_rules(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open rule file '" + path + "'");
    std::stringstream ss;
    ss << in.rdbuf();
    return parse_rules(ss.str());
}

std::string describe(const Map& map, const Choice& choice) {
    if (map.groups.empty()) return "single map";
    std::string out;
    for (std::size_t g = 0; g < map.groups.size(); ++g) {
        if (!out.empty()) out += ", ";
        const int c = g < choice.size() ? choice[g] : -1;
        out += map.groups[g].name + "=";
        if (c == -2) out += "(all options)";
        else if (c < 0) out += "none";
        else out += map.groups[g].options[static_cast<std::size_t>(c)].name;
    }
    return out;
}

// ------------------------------------------------------------- evaluation

namespace {

struct Outcome {
    bool not_checked = false;
    std::string reason;
    std::vector<Finding> failures;
};

std::string cell_text(const Map& map, int idx) {
    const Cell c = map.cell(idx);
    return "(" + std::to_string(c.x) + ", " + std::to_string(c.y) + ")";
}

std::string fmt(double v) {
    std::ostringstream os;
    os.setf(std::ios::fixed);
    os.precision(2);
    os << v;
    return os.str();
}

// A unit the rule talks about: a point (one cell) or a region (its cells).
struct Unit {
    std::string label;
    std::vector<int> cells;
};

std::vector<Unit> units(const Map& map, const std::string& selector) {
    std::vector<Unit> out;
    for (const Point* p : select_points(map, selector)) {
        const Cell c = p->cell();
        if (map.inside(c.x, c.y)) out.push_back({"point '" + p->name + "'", {map.index(c.x, c.y)}});
    }
    for (const Region* r : select_regions(map, selector))
        out.push_back({"region '" + r->name + "'", r->cells});
    return out;
}

// Variant option cells whose tags match the selector, for the chosen variant.
std::vector<int> option_cells(const Map& map, const Choice& choice, const std::string& selector) {
    std::vector<int> out;
    for (std::size_t g = 0; g < map.groups.size(); ++g) {
        const int pick = g < choice.size() ? choice[g] : -1;
        for (std::size_t o = 0; o < map.groups[g].options.size(); ++o) {
            if (pick != -2 && pick != static_cast<int>(o)) continue;
            const VariantOption& opt = map.groups[g].options[o];
            if (!matches(selector, map.groups[g].name + "." + opt.name, opt.tags)) continue;
            out.insert(out.end(), opt.block_walk.begin(), opt.block_walk.end());
            out.insert(out.end(), opt.block_sight.begin(), opt.block_sight.end());
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// Cells an agent can stand on for a set of cells. When none of them is
// passable (an exit drawn in the border wall, a door in a wall, a spawn on a
// prop), the passable 8-neighbours stand in for them: reaching a cell next to
// the target counts as reaching the target.
// Cells blocked only because the agent is too wide do not fall back: there the
// rule should fail.
std::vector<int> standable(const Map& map, const std::vector<std::uint8_t>& pass,
                           const std::vector<std::uint8_t>& walk, const std::vector<int>& cells) {
    std::vector<int> out;
    for (int c : cells)
        if (pass[static_cast<std::size_t>(c)]) out.push_back(c);
    if (!out.empty()) return out;
    for (int c : cells)
        if (walk[static_cast<std::size_t>(c)]) return out;  // walkable, just too tight
    for (int c : cells) {
        const Cell p = map.cell(c);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!map.inside(p.x + dx, p.y + dy)) continue;
                const int n = map.index(p.x + dx, p.y + dy);
                if (pass[static_cast<std::size_t>(n)]) out.push_back(n);
            }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

Finding base(const Rule& r) {
    Finding f;
    f.rule_id = r.id;
    f.tmpl = r.tmpl;
    f.severity = r.severity;
    return f;
}

Outcome nothing(const std::string& field, const std::string& selector) {
    Outcome o;
    o.not_checked = true;
    o.reason = "'" + field + "' selector '" + selector + "' matches nothing in the map";
    return o;
}

Outcome eval_reachable(const Map& map, const Rule& r, const State& s) {
    const std::vector<int> from = select_cells(map, r.from);
    if (from.empty()) return nothing("from", r.from);
    const std::vector<Unit> to = units(map, r.to);
    if (to.empty()) return nothing("to", r.to);
    const auto pass = passable(map, s.walk, r.radius);
    Outcome o;
    const std::vector<int> seeds = standable(map, pass, s.walk, from);
    if (seeds.empty()) {
        Finding f = base(r);
        f.message = "the start '" + r.from + "' is blocked for an agent of radius " + fmt(r.radius);
        f.cells = from;
        o.failures.push_back(std::move(f));
        return o;
    }
    const auto dist = reach(map, pass, seeds);
    for (const Unit& u : to) {
        const std::vector<int> goal = standable(map, pass, s.walk, u.cells);
        const bool ok = std::any_of(goal.begin(), goal.end(),
                                    [&](int c) { return dist[static_cast<std::size_t>(c)] != kUnreached; });
        if (ok) continue;
        Finding f = base(r);
        f.message = u.label + " is not reachable from '" + r.from + "' for an agent of radius " + fmt(r.radius);
        f.cells = u.cells;
        o.failures.push_back(std::move(f));
    }
    return o;
}

Outcome eval_isolated(const Map& map, const Rule& r, const State& s) {
    const std::vector<int> from = select_cells(map, r.from);
    if (from.empty()) return nothing("from", r.from);
    const auto pass = passable(map, s.walk, r.radius);
    const auto dist = reach(map, pass, standable(map, pass, s.walk, from));
    std::vector<int> lost;
    for (std::size_t i = 0; i < pass.size(); ++i)
        if (pass[i] && dist[i] == kUnreached) lost.push_back(static_cast<int>(i));
    Outcome o;
    for (const auto& comp : components(map, lost)) {
        if (comp.size() < std::max<std::size_t>(r.min_cells, 1)) continue;
        Finding f = base(r);
        f.message = "walkable area of " + std::to_string(comp.size()) + " cells near " +
                    cell_text(map, comp.front()) + " cannot be reached from '" + r.from + "'";
        f.cells = comp;
        f.measured = static_cast<double>(comp.size());
        f.has_measure = true;
        o.failures.push_back(std::move(f));
    }
    return o;
}

Outcome eval_path_length(const Map& map, const Rule& r, const State& s) {
    const std::vector<int> from = select_cells(map, r.from);
    if (from.empty()) return nothing("from", r.from);
    const std::vector<int> to = select_cells(map, r.to);
    if (to.empty()) return nothing("to", r.to);
    const auto pass = passable(map, s.walk, r.radius);
    const Path p = shortest_path(map, pass, standable(map, pass, s.walk, from), standable(map, pass, s.walk, to));
    Outcome o;
    Finding f = base(r);
    if (!p.found) {
        f.message = "no path from '" + r.from + "' to '" + r.to + "', so its length cannot be checked";
        f.cells = to;
        o.failures.push_back(std::move(f));
        return o;
    }
    f.measured = p.length;
    f.has_measure = true;
    f.path = p.cells;
    if (r.min >= 0 && p.length < r.min - 1e-9) {
        f.required = r.min;
        f.message = "shortest path from '" + r.from + "' to '" + r.to + "' is " + fmt(p.length) +
                    ", shorter than the minimum " + fmt(r.min);
        o.failures.push_back(std::move(f));
    } else if (r.max >= 0 && p.length > r.max + 1e-9) {
        f.required = r.max;
        f.message = "shortest path from '" + r.from + "' to '" + r.to + "' is " + fmt(p.length) +
                    ", longer than the maximum " + fmt(r.max);
        o.failures.push_back(std::move(f));
    }
    return o;
}

Outcome eval_not_visible(const Map& map, const Rule& r, const State& s, const Options& opt) {
    const std::vector<int> target = select_cells(map, r.target);
    if (target.empty()) return nothing("target", r.target);
    const std::vector<int> from = select_cells(map, r.from);
    if (from.empty()) return nothing("from", r.from);
    if (select_regions(map, r.door).empty()) return nothing("door", r.door);
    const SightPair sp = any_sight(map, s.sight, from, target, opt.sight_samples);
    Outcome o;
    if (sp.found) {
        Finding f = base(r);
        f.message = "'" + r.target + "' is visible from '" + r.from + "' at " + cell_text(map, sp.from) +
                    " while '" + r.door + "' is closed";
        f.path = {sp.from, sp.to};
        f.cells = {sp.to};
        o.failures.push_back(std::move(f));
    }
    return o;
}

Outcome eval_visible(const Map& map, const Rule& r, const State& s, const Options& opt) {
    const std::vector<int> target = select_cells(map, r.target);
    if (target.empty()) return nothing("target", r.target);
    const std::vector<int> from = select_cells(map, r.from);
    if (from.empty()) return nothing("from", r.from);
    const SightPair sp = any_sight(map, s.sight, from, target, opt.sight_samples);
    Outcome o;
    if (!sp.found) {
        Finding f = base(r);
        f.message = "'" + r.target + "' is not visible from anywhere in '" + r.from + "'" +
                    (sp.sampled ? " (cell sets were sampled)" : "");
        f.cells = target;
        o.failures.push_back(std::move(f));
    }
    return o;
}

Outcome eval_near(const Map& map, const Rule& r, const Choice& choice) {
    const std::vector<Unit> subjects = units(map, r.subject);
    if (subjects.empty()) return nothing("subject", r.subject);
    std::vector<int> objects = select_cells(map, r.object);
    if (objects.empty() && option_cells(map, Choice(map.groups.size(), -2), r.object).empty())
        return nothing("object", r.object);
    const std::vector<int> extra = option_cells(map, choice, r.object);
    objects.insert(objects.end(), extra.begin(), extra.end());
    Outcome o;
    if (objects.empty()) {
        for (const Unit& u : subjects) {
            Finding f = base(r);
            f.message = u.label + " has no '" + r.object + "' in this variant";
            f.cells = u.cells;
            o.failures.push_back(std::move(f));
        }
        return o;
    }
    const auto field = distance_field(map, objects);
    for (const Unit& u : subjects) {
        double best = std::numeric_limits<double>::infinity();
        for (int c : u.cells) best = std::min(best, static_cast<double>(field[static_cast<std::size_t>(c)]));
        const double d = best * map.cell_size;
        if (d <= r.distance + 1e-9) continue;
        Finding f = base(r);
        f.message = u.label + " is " + fmt(d) + " from the nearest '" + r.object + "', more than " +
                    fmt(r.distance);
        f.cells = u.cells;
        f.measured = d;
        f.required = r.distance;
        f.has_measure = true;
        o.failures.push_back(std::move(f));
    }
    return o;
}

Outcome eval_softlock(const Map& map, const Rule& r, const State& s) {
    const std::vector<int> from = select_cells(map, r.from);
    if (from.empty()) return nothing("from", r.from);
    const std::vector<int> exits = select_cells(map, r.to);
    if (exits.empty()) return nothing("to", r.to);
    const auto pass = passable(map, s.walk, r.radius);
    const std::vector<int> start = standable(map, pass, s.walk, from);
    const auto fwd = reach(map, pass, start);
    const auto bwd = reach(map, pass, standable(map, pass, s.walk, exits), true);
    Outcome o;
    // If the start itself cannot reach an exit, every cell would count as
    // stuck; report that once instead (the reachable rule covers the detail).
    const bool start_ok = std::any_of(start.begin(), start.end(), [&](int c) {
        return bwd[static_cast<std::size_t>(c)] != kUnreached;
    });
    if (!start_ok) {
        Finding f = base(r);
        f.message = "'" + r.from + "' itself has no way to '" + r.to + "'";
        f.cells = from;
        o.failures.push_back(std::move(f));
        return o;
    }
    std::vector<int> stuck;
    for (std::size_t i = 0; i < fwd.size(); ++i)
        if (fwd[i] != kUnreached && bwd[i] == kUnreached) stuck.push_back(static_cast<int>(i));
    for (const auto& comp : components(map, stuck)) {
        Finding f = base(r);
        f.message = std::to_string(comp.size()) + " cells near " + cell_text(map, comp.front()) +
                    " can be entered from '" + r.from + "' but have no way to '" + r.to + "'";
        f.cells = comp;
        o.failures.push_back(std::move(f));
    }
    return o;
}

Outcome eval_once(const Map& map, const Rule& r, const Choice& choice, const Options& opt) {
    std::vector<DoorOverride> doors = r.doors;
    if (r.tmpl == "not_visible_until") doors.push_back({r.door, DoorMode::kClosed});
    const State s = make_state(map, choice, doors);
    if (r.tmpl == "reachable") return eval_reachable(map, r, s);
    if (r.tmpl == "no_isolated_walkable") return eval_isolated(map, r, s);
    if (r.tmpl == "path_length") return eval_path_length(map, r, s);
    if (r.tmpl == "not_visible_until") return eval_not_visible(map, r, s, opt);
    if (r.tmpl == "visible_from") return eval_visible(map, r, s, opt);
    if (r.tmpl == "near") return eval_near(map, r, choice);
    if (r.tmpl == "no_softlock") return eval_softlock(map, r, s);
    Outcome o;
    o.not_checked = true;
    o.reason = "template '" + r.tmpl + "' is not evaluated per variant";
    return o;
}

// clear_of looks at every option on its own: a reserved region must not be
// covered by any option that matches the blocker selector, nor by a region.
void eval_clear_of(const Map& map, const Rule& r, RuleResult& res, std::vector<Finding>& out) {
    const std::vector<int> reserved = select_cells(map, r.region);
    if (reserved.empty()) {
        res.status = Status::kNotChecked;
        res.note = "'region' selector '" + r.region + "' matches nothing in the map";
        return;
    }
    std::vector<std::uint8_t> in(map.size(), 0);
    for (int c : reserved) in[static_cast<std::size_t>(c)] = 1;
    auto overlap = [&](const std::vector<int>& a, const std::vector<int>& b) {
        std::vector<int> o;
        for (const auto* v : {&a, &b})
            for (int c : *v)
                if (in[static_cast<std::size_t>(c)]) o.push_back(c);
        std::sort(o.begin(), o.end());
        o.erase(std::unique(o.begin(), o.end()), o.end());
        return o;
    };
    std::size_t checked = 0;
    for (std::size_t g = 0; g < map.groups.size(); ++g)
        for (std::size_t i = 0; i < map.groups[g].options.size(); ++i) {
            const VariantOption& opt = map.groups[g].options[i];
            const std::string full = map.groups[g].name + "." + opt.name;
            if (!matches(r.blocker, full, opt.tags)) continue;
            ++checked;
            const std::vector<int> hit = overlap(opt.block_walk, opt.block_sight);
            if (hit.empty()) continue;
            Finding f = base(r);
            f.message = "variant option '" + full + "' covers " + std::to_string(hit.size()) +
                        " reserved cells of '" + r.region + "'";
            f.cells = hit;
            Choice c(map.groups.size(), -1);
            c[g] = static_cast<int>(i);
            f.variant = c;
            f.variant_text = describe(map, c);
            out.push_back(std::move(f));
        }
    for (const Region* reg : select_regions(map, r.blocker)) {
        if (matches(r.region, reg->name, reg->tags)) continue;  // the reserved region itself
        if (reg->door) continue;                                // doors are part of the route
        ++checked;
        const std::vector<int> hit = overlap(reg->cells, {});
        if (hit.empty()) continue;
        Finding f = base(r);
        f.message = "region '" + reg->name + "' covers " + std::to_string(hit.size()) +
                    " reserved cells of '" + r.region + "'";
        f.cells = hit;
        out.push_back(std::move(f));
    }
    if (checked == 0) {
        res.status = Status::kNotChecked;
        res.note = "'blocker' selector '" + r.blocker + "' matches no variant option or region";
        return;
    }
    res.method = "each option";
    res.variants_checked = checked;
    res.variants_total = checked;
    res.status = std::any_of(out.begin(), out.end(), [&](const Finding& f) { return f.rule_id == r.id; })
                     ? Status::kFailed
                     : Status::kPassed;
}

// How blocking changes a rule: more blockers can only hurt (kHurts), only help
// (kHelps), or either (kMixed). Used to prove a rule for every variant with
// one evaluation of a relaxed bound.
enum class Effect { kHurts, kHelps, kMixed };

std::vector<Effect> effects(const Rule& r) {
    if (r.tmpl == "reachable" || r.tmpl == "visible_from") return {Effect::kHurts};
    if (r.tmpl == "not_visible_until") return {Effect::kHelps};
    if (r.tmpl == "path_length") {
        std::vector<Effect> e;
        if (r.max >= 0) e.push_back(Effect::kHurts);
        if (r.min >= 0) e.push_back(Effect::kHelps);
        return e;
    }
    return {Effect::kMixed};
}

std::size_t total_variants(const Map& map) {
    std::size_t n = 1;
    for (const VariantGroup& g : map.groups) {
        const std::size_t c = std::max<std::size_t>(g.choices(), 1);
        if (n > std::numeric_limits<std::size_t>::max() / c) return std::numeric_limits<std::size_t>::max();
        n *= c;
    }
    return n;
}

// Mixed-radix enumeration of variants. Each group's digit runs from -1 (none,
// optional groups only) to options - 1.
bool next_choice(const Map& map, Choice& c) {
    for (std::size_t g = 0; g < map.groups.size(); ++g) {
        const int last = static_cast<int>(map.groups[g].options.size()) - 1;
        if (c[g] < last) {
            ++c[g];
            return true;
        }
        c[g] = map.groups[g].optional ? -1 : 0;
    }
    return false;
}

Choice first_choice(const Map& map) {
    Choice c;
    for (const VariantGroup& g : map.groups) c.push_back(g.optional ? -1 : 0);
    return c;
}

std::size_t chosen_count(const Choice& c) {
    return static_cast<std::size_t>(std::count_if(c.begin(), c.end(), [](int v) { return v >= 0; }));
}

}  // namespace

Report evaluate(const Map& map, const RuleSet& set) {
    Report rep;
    rep.map_name = map.name;
    rep.map_file = map.source_file;
    const std::size_t total = total_variants(map);

    for (const Rule& r : set.rules) {
        RuleResult res;
        res.rule_id = r.id;
        res.tmpl = r.tmpl;
        res.severity = r.severity;
        res.variants_total = total;

        if (r.tmpl == "clear_of") {
            eval_clear_of(map, r, res, rep.findings);
            rep.results.push_back(res);
            continue;
        }

        if (map.groups.empty()) {
            Outcome o = eval_once(map, r, {}, set.options);
            res.method = "single map";
            res.variants_checked = 1;
            if (o.not_checked) {
                res.status = Status::kNotChecked;
                res.note = o.reason;
            } else {
                res.status = o.failures.empty() ? Status::kPassed : Status::kFailed;
                res.variants_failed = o.failures.empty() ? 0 : 1;
                for (Finding& f : o.failures) {
                    f.variant_text = "single map";
                    rep.findings.push_back(std::move(f));
                }
            }
            rep.results.push_back(res);
            continue;
        }

        // Try to prove the rule for every variant through relaxed bounds.
        const std::vector<Effect> eff = effects(r);
        bool proved = std::find(eff.begin(), eff.end(), Effect::kMixed) == eff.end();
        bool not_checked = false;
        for (Effect e : eff) {
            if (!proved) break;
            const Choice bound(map.groups.size(), e == Effect::kHurts ? -2 : -1);
            const Outcome o = eval_once(map, r, bound, set.options);
            if (o.not_checked) {
                not_checked = true;
                res.note = o.reason;
                break;
            }
            if (!o.failures.empty()) proved = false;
        }
        if (not_checked) {
            res.status = Status::kNotChecked;
            res.method = "bound";
            rep.results.push_back(res);
            continue;
        }
        if (proved) {
            res.status = Status::kPassed;
            res.method = "bound";
            res.variants_checked = total;
            res.note = "holds for the relaxed bound, so for every variant";
            rep.results.push_back(res);
            continue;
        }

        // Enumerate, or sample when there are too many variants.
        std::vector<Choice> todo;
        if (total <= set.options.max_combinations) {
            Choice c = first_choice(map);
            do todo.push_back(c);
            while (next_choice(map, c));
            res.method = "every variant";
        } else {
            std::mt19937 rng(set.options.sample_seed);
            for (std::size_t i = 0; i < set.options.sample_count; ++i) {
                Choice c;
                for (const VariantGroup& g : map.groups) {
                    const auto k = static_cast<std::uint32_t>(g.choices());
                    int v = static_cast<int>(rng() % k);
                    if (g.optional) v -= 1;
                    c.push_back(v);
                }
                todo.push_back(c);
            }
            res.method = "sampled";
        }

        std::vector<Finding> best;
        std::size_t best_size = std::numeric_limits<std::size_t>::max();
        for (const Choice& c : todo) {
            Outcome o = eval_once(map, r, c, set.options);
            ++res.variants_checked;
            if (o.not_checked) {
                not_checked = true;
                res.note = o.reason;
                break;
            }
            if (o.failures.empty()) continue;
            ++res.variants_failed;
            if (chosen_count(c) < best_size) {
                best_size = chosen_count(c);
                best = std::move(o.failures);
                for (Finding& f : best) {
                    f.variant = c;
                    f.variant_text = describe(map, c);
                }
            }
        }
        if (not_checked) {
            res.status = Status::kNotChecked;
        } else {
            res.status = res.variants_failed ? Status::kFailed : Status::kPassed;
            if (res.variants_failed) {
                res.note = "fails in " + std::to_string(res.variants_failed) + " of " +
                           std::to_string(res.variants_checked) + " variants checked; showing the one with the fewest options";
                for (Finding& f : best) rep.findings.push_back(std::move(f));
            }
        }
        rep.results.push_back(res);
    }
    return rep;
}

}  // namespace adari
