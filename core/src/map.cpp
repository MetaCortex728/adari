// SPDX-License-Identifier: Apache-2.0
#include "adari/map.hpp"

#include <algorithm>
#include <cmath>

namespace adari {

Cell Point::cell() const {
    return Cell{static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y))};
}

void Map::resize(int w, int h) {
    width = w;
    height = h;
    walk.assign(size(), 1);
    sight.assign(size(), 0);
    edge_block.assign(size(), 0);
}

State make_state(const Map& map, const Choice& choice, const std::vector<DoorOverride>& doors) {
    State s{map.walk, map.sight};
    for (std::size_t g = 0; g < map.groups.size(); ++g) {
        const VariantGroup& group = map.groups[g];
        const int pick = g < choice.size() ? choice[g] : -1;
        for (std::size_t o = 0; o < group.options.size(); ++o) {
            if (pick != -2 && pick != static_cast<int>(o)) continue;
            for (int c : group.options[o].block_walk) s.walk[static_cast<std::size_t>(c)] = 0;
            for (int c : group.options[o].block_sight) s.sight[static_cast<std::size_t>(c)] = 1;
        }
    }
    for (const Region& r : map.regions) {
        if (!r.door) continue;
        DoorMode mode = DoorMode::kOpen;
        for (const DoorOverride& d : doors)
            if (matches(d.selector, r.name, r.tags)) mode = d.mode;
        if (mode == DoorMode::kClosed) {
            for (int c : r.cells) {
                s.walk[static_cast<std::size_t>(c)] = 0;
                s.sight[static_cast<std::size_t>(c)] = 1;
            }
        }
    }
    return s;
}

bool matches(const std::string& selector, const std::string& name,
             const std::vector<std::string>& tags) {
    if (selector.rfind("name:", 0) == 0) return name == selector.substr(5);
    if (selector == "*") return true;
    return std::find(tags.begin(), tags.end(), selector) != tags.end();
}

std::vector<const Point*> select_points(const Map& map, const std::string& selector) {
    std::vector<const Point*> out;
    for (const Point& p : map.points)
        if (matches(selector, p.name, p.tags)) out.push_back(&p);
    return out;
}

std::vector<const Region*> select_regions(const Map& map, const std::string& selector) {
    std::vector<const Region*> out;
    for (const Region& r : map.regions)
        if (matches(selector, r.name, r.tags)) out.push_back(&r);
    return out;
}

std::vector<int> select_cells(const Map& map, const std::string& selector) {
    std::vector<int> out;
    for (const Point* p : select_points(map, selector)) {
        const Cell c = p->cell();
        if (map.inside(c.x, c.y)) out.push_back(map.index(c.x, c.y));
    }
    for (const Region* r : select_regions(map, selector))
        out.insert(out.end(), r->cells.begin(), r->cells.end());
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::string validate(const Map& map) {
    if (map.width <= 0 || map.height <= 0) return "map has no cells";
    if (!(map.cell_size > 0.0)) return "cell_size must be positive";
    if (map.walk.size() != map.size() || map.sight.size() != map.size() ||
        map.edge_block.size() != map.size())
        return "layer sizes do not match width x height";
    const int n = static_cast<int>(map.size());
    auto bad = [n](const std::vector<int>& cells) {
        return std::any_of(cells.begin(), cells.end(), [n](int c) { return c < 0 || c >= n; });
    };
    for (const Region& r : map.regions)
        if (bad(r.cells)) return "region '" + r.name + "' has a cell outside the map";
    for (const VariantGroup& g : map.groups) {
        if (g.options.empty()) return "variant group '" + g.name + "' has no options";
        for (const VariantOption& o : g.options)
            if (bad(o.block_walk) || bad(o.block_sight))
                return "variant option '" + g.name + "." + o.name + "' has a cell outside the map";
    }
    return {};
}

}  // namespace adari
