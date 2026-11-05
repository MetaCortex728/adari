// SPDX-License-Identifier: Apache-2.0
#include "adari/grid.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <queue>

namespace adari {
namespace {

constexpr int kDx[8] = {0, 1, 0, -1, 1, 1, -1, -1};
constexpr int kDy[8] = {-1, 0, 1, 0, -1, 1, 1, -1};

std::uint8_t dir_bit(int dx, int dy) {
    if (dy < 0) return kNorth;
    if (dx > 0) return kEast;
    if (dy > 0) return kSouth;
    return kWest;
}

// One-dimensional squared distance transform (Felzenszwalb & Huttenlocher).
void edt_1d(const std::vector<double>& f, std::vector<double>& d, std::vector<int>& v,
            std::vector<double>& z) {
    const int n = static_cast<int>(f.size());
    const double inf = std::numeric_limits<double>::infinity();
    int k = 0;
    v[0] = 0;
    z[0] = -inf;
    z[1] = inf;
    for (int q = 1; q < n; ++q) {
        if (f[static_cast<std::size_t>(q)] == inf) continue;
        while (true) {
            const int p = v[static_cast<std::size_t>(k)];
            if (f[static_cast<std::size_t>(p)] == inf) {
                // An infinite parabola never wins; replace it.
                v[static_cast<std::size_t>(k)] = q;
                z[static_cast<std::size_t>(k)] = -inf;
                z[static_cast<std::size_t>(k) + 1] = inf;
                break;
            }
            const double s = ((f[static_cast<std::size_t>(q)] + q * static_cast<double>(q)) -
                              (f[static_cast<std::size_t>(p)] + p * static_cast<double>(p))) /
                             (2.0 * q - 2.0 * p);
            if (s <= z[static_cast<std::size_t>(k)]) {
                if (k == 0) {
                    v[0] = q;
                    z[0] = -inf;
                    z[1] = inf;
                    break;
                }
                --k;
                continue;
            }
            ++k;
            v[static_cast<std::size_t>(k)] = q;
            z[static_cast<std::size_t>(k)] = s;
            z[static_cast<std::size_t>(k) + 1] = inf;
            break;
        }
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[static_cast<std::size_t>(k) + 1] < q) ++k;
        const int p = v[static_cast<std::size_t>(k)];
        const double fp = f[static_cast<std::size_t>(p)];
        d[static_cast<std::size_t>(q)] = fp == inf ? inf : (q - p) * static_cast<double>(q - p) + fp;
    }
}

// Two-dimensional squared distance transform in place: g holds 0 at source
// cells and infinity elsewhere, w x h, row-major.
void edt_2d(std::vector<double>& g, int w, int h) {
    const int n = std::max(w, h);
    std::vector<double> f(static_cast<std::size_t>(n)), d(static_cast<std::size_t>(n));
    std::vector<int> v(static_cast<std::size_t>(n));
    std::vector<double> z(static_cast<std::size_t>(n) + 1);

    for (int x = 0; x < w; ++x) {  // columns
        f.resize(static_cast<std::size_t>(h));
        d.resize(static_cast<std::size_t>(h));
        for (int y = 0; y < h; ++y) f[static_cast<std::size_t>(y)] = g[static_cast<std::size_t>(y * w + x)];
        edt_1d(f, d, v, z);
        for (int y = 0; y < h; ++y) g[static_cast<std::size_t>(y * w + x)] = d[static_cast<std::size_t>(y)];
    }
    for (int y = 0; y < h; ++y) {  // rows
        f.resize(static_cast<std::size_t>(w));
        d.resize(static_cast<std::size_t>(w));
        for (int x = 0; x < w; ++x) f[static_cast<std::size_t>(x)] = g[static_cast<std::size_t>(y * w + x)];
        edt_1d(f, d, v, z);
        for (int x = 0; x < w; ++x) g[static_cast<std::size_t>(y * w + x)] = d[static_cast<std::size_t>(x)];
    }
}

}  // namespace

std::vector<float> distance_field(const Map& map, const std::vector<int>& sources) {
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> g(map.size(), inf);
    for (int c : sources)
        if (c >= 0 && c < static_cast<int>(map.size())) g[static_cast<std::size_t>(c)] = 0.0;
    std::vector<float> out(map.size(), std::numeric_limits<float>::infinity());
    if (sources.empty()) return out;
    edt_2d(g, map.width, map.height);
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = static_cast<float>(std::sqrt(g[i]));
    return out;
}

std::vector<float> clearance(const Map& map, const std::vector<std::uint8_t>& walk) {
    // Pad by one blocked cell on every side so the map edge counts as a wall.
    const int w = map.width + 2;
    const int h = map.height + 2;
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> g(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0.0);
    for (int y = 0; y < map.height; ++y)
        for (int x = 0; x < map.width; ++x)
            if (walk[static_cast<std::size_t>(map.index(x, y))])
                g[static_cast<std::size_t>((y + 1) * w + (x + 1))] = inf;
    edt_2d(g, w, h);

    std::vector<float> out(map.size());
    for (int y = 0; y < map.height; ++y)
        for (int x = 0; x < map.width; ++x)
            out[static_cast<std::size_t>(map.index(x, y))] =
                static_cast<float>(std::sqrt(g[static_cast<std::size_t>((y + 1) * w + (x + 1))]));
    return out;
}

std::vector<std::uint8_t> passable(const Map& map, const std::vector<std::uint8_t>& walk,
                                   double radius) {
    if (radius <= 0.0) return walk;
    const std::vector<float> clear = clearance(map, walk);
    std::vector<std::uint8_t> out(map.size(), 0);
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = walk[i] && (static_cast<double>(clear[i]) - 0.5) * map.cell_size >= radius - 1e-9;
    return out;
}

bool step_allowed(const Map& map, const std::vector<std::uint8_t>& pass, int from, int to) {
    const Cell a = map.cell(from);
    const Cell b = map.cell(to);
    const int dx = b.x - a.x;
    const int dy = b.y - a.y;
    if (std::abs(dx) > 1 || std::abs(dy) > 1 || (dx == 0 && dy == 0)) return false;
    if (!map.inside(b.x, b.y) || !pass[static_cast<std::size_t>(to)] ||
        !pass[static_cast<std::size_t>(from)])
        return false;
    if (dx == 0 || dy == 0)
        return (map.edge_block[static_cast<std::size_t>(from)] & dir_bit(dx, dy)) == 0;
    const int h = map.index(a.x + dx, a.y);  // horizontal first
    const int v = map.index(a.x, a.y + dy);  // vertical first
    return step_allowed(map, pass, from, h) && step_allowed(map, pass, h, to) &&
           step_allowed(map, pass, from, v) && step_allowed(map, pass, v, to);
}

std::vector<int> reach(const Map& map, const std::vector<std::uint8_t>& pass,
                       const std::vector<int>& seeds, bool backward) {
    std::vector<int> dist(map.size(), kUnreached);
    std::queue<int> q;
    for (int s : seeds) {
        if (s < 0 || s >= static_cast<int>(map.size()) || !pass[static_cast<std::size_t>(s)]) continue;
        if (dist[static_cast<std::size_t>(s)] != kUnreached) continue;
        dist[static_cast<std::size_t>(s)] = 0;
        q.push(s);
    }
    while (!q.empty()) {
        const int c = q.front();
        q.pop();
        const Cell p = map.cell(c);
        for (int k = 0; k < 8; ++k) {
            const int nx = p.x + kDx[k];
            const int ny = p.y + kDy[k];
            if (!map.inside(nx, ny)) continue;
            const int n = map.index(nx, ny);
            if (dist[static_cast<std::size_t>(n)] != kUnreached) continue;
            const bool ok = backward ? step_allowed(map, pass, n, c) : step_allowed(map, pass, c, n);
            if (!ok) continue;
            dist[static_cast<std::size_t>(n)] = dist[static_cast<std::size_t>(c)] + 1;
            q.push(n);
        }
    }
    return dist;
}

Path shortest_path(const Map& map, const std::vector<std::uint8_t>& pass,
                   const std::vector<int>& sources, const std::vector<int>& targets) {
    Path out;
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> dist(map.size(), inf);
    std::vector<int> prev(map.size(), -1);
    std::vector<std::uint8_t> is_target(map.size(), 0);
    for (int t : targets)
        if (t >= 0 && t < static_cast<int>(map.size())) is_target[static_cast<std::size_t>(t)] = 1;

    using Item = std::pair<double, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    for (int s : sources) {
        if (s < 0 || s >= static_cast<int>(map.size()) || !pass[static_cast<std::size_t>(s)]) continue;
        dist[static_cast<std::size_t>(s)] = 0.0;
        pq.push({0.0, s});
    }
    const double diag = std::sqrt(2.0);
    while (!pq.empty()) {
        const auto [d, c] = pq.top();
        pq.pop();
        if (d > dist[static_cast<std::size_t>(c)]) continue;
        if (is_target[static_cast<std::size_t>(c)]) {
            out.found = true;
            out.length = d * map.cell_size;
            for (int at = c; at != -1; at = prev[static_cast<std::size_t>(at)]) out.cells.push_back(at);
            std::reverse(out.cells.begin(), out.cells.end());
            return out;
        }
        const Cell p = map.cell(c);
        for (int k = 0; k < 8; ++k) {
            const int nx = p.x + kDx[k];
            const int ny = p.y + kDy[k];
            if (!map.inside(nx, ny)) continue;
            const int n = map.index(nx, ny);
            if (!step_allowed(map, pass, c, n)) continue;
            const double nd = d + (k < 4 ? 1.0 : diag);
            if (nd < dist[static_cast<std::size_t>(n)]) {
                dist[static_cast<std::size_t>(n)] = nd;
                prev[static_cast<std::size_t>(n)] = c;
                pq.push({nd, n});
            }
        }
    }
    return out;
}

bool line_of_sight(const Map& map, const std::vector<std::uint8_t>& sight, int a, int b) {
    const Cell s = map.cell(a);
    const Cell e = map.cell(b);
    const int dx = std::abs(e.x - s.x);
    const int dy = std::abs(e.y - s.y);
    const int sx = e.x > s.x ? 1 : -1;
    const int sy = e.y > s.y ? 1 : -1;
    auto blocks = [&](int x, int y) {
        if (!map.inside(x, y)) return true;
        const int i = map.index(x, y);
        if (i == a || i == b) return false;
        return sight[static_cast<std::size_t>(i)] != 0;
    };
    // Walk the cells the segment between centres passes through. Crossing
    // times of vertical and horizontal cell borders are compared exactly as
    // integers: the k-th vertical border is crossed at (2k+1)/(2dx).
    int x = s.x;
    int y = s.y;
    int kx = 0;
    int ky = 0;
    while (kx < dx || ky < dy) {
        const long long tx = (2LL * kx + 1) * dy;  // scaled crossing time, vertical border
        const long long ty = (2LL * ky + 1) * dx;  // scaled crossing time, horizontal border
        if (ky >= dy || (kx < dx && tx < ty)) {
            x += sx;
            ++kx;
        } else if (kx >= dx || ty < tx) {
            y += sy;
            ++ky;
        } else {
            // Exactly through a corner: both side cells are touched.
            if (blocks(x + sx, y) || blocks(x, y + sy)) return false;
            x += sx;
            y += sy;
            ++kx;
            ++ky;
        }
        if (blocks(x, y)) return false;
    }
    return true;
}

SightPair any_sight(const Map& map, const std::vector<std::uint8_t>& sight,
                    const std::vector<int>& from, const std::vector<int>& to,
                    std::size_t max_samples) {
    SightPair out;
    auto thin = [&](const std::vector<int>& cells) {
        if (cells.size() <= max_samples || max_samples == 0) return cells;
        out.sampled = true;
        std::vector<int> t;
        const double stride = static_cast<double>(cells.size()) / static_cast<double>(max_samples);
        for (std::size_t i = 0; i < max_samples; ++i)
            t.push_back(cells[static_cast<std::size_t>(static_cast<double>(i) * stride)]);
        return t;
    };
    const std::vector<int> a = thin(from);
    const std::vector<int> b = thin(to);
    for (int i : a)
        for (int j : b)
            if (line_of_sight(map, sight, i, j)) {
                out.found = true;
                out.from = i;
                out.to = j;
                return out;
            }
    return out;
}

std::vector<std::vector<int>> components(const Map& map, const std::vector<int>& cells) {
    std::vector<std::uint8_t> in(map.size(), 0);
    for (int c : cells) in[static_cast<std::size_t>(c)] = 1;
    std::vector<std::uint8_t> seen(map.size(), 0);
    std::vector<int> sorted = cells;
    std::sort(sorted.begin(), sorted.end());
    std::vector<std::vector<int>> out;
    for (int start : sorted) {
        if (seen[static_cast<std::size_t>(start)]) continue;
        std::vector<int> comp;
        std::queue<int> q;
        q.push(start);
        seen[static_cast<std::size_t>(start)] = 1;
        while (!q.empty()) {
            const int c = q.front();
            q.pop();
            comp.push_back(c);
            const Cell p = map.cell(c);
            for (int k = 0; k < 8; ++k) {
                const int nx = p.x + kDx[k];
                const int ny = p.y + kDy[k];
                if (!map.inside(nx, ny)) continue;
                const int n = map.index(nx, ny);
                if (!in[static_cast<std::size_t>(n)] || seen[static_cast<std::size_t>(n)]) continue;
                seen[static_cast<std::size_t>(n)] = 1;
                q.push(n);
            }
        }
        std::sort(comp.begin(), comp.end());
        out.push_back(std::move(comp));
    }
    return out;
}

}  // namespace adari
