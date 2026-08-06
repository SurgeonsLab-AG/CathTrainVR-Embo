#include "StlMesh.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <algorithm>

namespace cn {

namespace {

/// Vertices are welded on a quantisation grid rather than by exact float
/// equality: STL files are written by meshers that emit the same corner with
/// slightly different rounding in adjacent facets.
constexpr double kWeldGrid = 1e-6;

struct QuantKey {
    int64_t x, y, z;
    bool operator==(const QuantKey &o) const { return x == o.x && y == o.y && z == o.z; }
};

struct QuantKeyHash {
    size_t operator()(const QuantKey &k) const {
        size_t h = 1469598103934665603ull;
        for (int64_t v : {k.x, k.y, k.z}) {
            h ^= static_cast<size_t>(v);
            h *= 1099511628211ull;
        }
        return h;
    }
};

QuantKey quantise(const Vec3 &p) {
    return {static_cast<int64_t>(std::llround(p.x / kWeldGrid)),
            static_cast<int64_t>(std::llround(p.y / kWeldGrid)),
            static_cast<int64_t>(std::llround(p.z / kWeldGrid))};
}

struct EdgeKey {
    uint32_t a, b;
    bool operator==(const EdgeKey &o) const { return a == o.a && b == o.b; }
};

struct EdgeKeyHash {
    size_t operator()(const EdgeKey &k) const {
        return std::hash<uint64_t>()((static_cast<uint64_t>(k.a) << 32) | k.b);
    }
};

} // namespace

bool StlMesh::load(const std::string &path, std::string *error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    f.seekg(0);
    if (size < 84) {
        if (error) *error = "file too small to be an STL";
        return false;
    }

    // Binary STL is identified by the triangle count in the header matching the
    // file length exactly. Sniffing for the literal "solid" is unreliable -
    // several exporters write it into the 80-byte binary header too.
    char header[84];
    f.read(header, 84);
    uint32_t declared = 0;
    std::memcpy(&declared, header + 80, 4);
    f.seekg(0);

    const bool binary = (static_cast<std::streamoff>(84) +
                         static_cast<std::streamoff>(declared) * 50) == size;
    return binary ? loadBinary(path, error) : loadAscii(path, error);
}

bool StlMesh::loadBinary(const std::string &path, std::string *error) {
    std::ifstream f(path, std::ios::binary);
    char header[80];
    f.read(header, 80);
    uint32_t n = 0;
    f.read(reinterpret_cast<char *>(&n), 4);

    std::vector<Vec3> raw;
    raw.reserve(static_cast<size_t>(n) * 3);

    std::vector<char> buffer(50);
    for (uint32_t i = 0; i < n; ++i) {
        f.read(buffer.data(), 50);
        if (!f) {
            if (error) *error = "truncated binary STL at facet " + std::to_string(i);
            return false;
        }
        // bytes 0..11 are the facet normal, which we ignore and recompute:
        // exported normals are frequently zero or inconsistently wound.
        const float *v = reinterpret_cast<const float *>(buffer.data() + 12);
        for (int k = 0; k < 3; ++k)
            raw.emplace_back(v[k * 3 + 0], v[k * 3 + 1], v[k * 3 + 2]);
    }
    weld(raw);
    buildTopology();
    return true;
}

bool StlMesh::loadAscii(const std::string &path, std::string *error) {
    std::ifstream f(path);
    std::string token;
    std::vector<Vec3> raw;
    while (f >> token) {
        if (token == "vertex") {
            float x, y, z;
            if (!(f >> x >> y >> z)) {
                if (error) *error = "malformed vertex in ASCII STL";
                return false;
            }
            raw.emplace_back(x, y, z);
        }
    }
    if (raw.size() < 3 || raw.size() % 3 != 0) {
        if (error) *error = "ASCII STL vertex count is not a multiple of 3";
        return false;
    }
    weld(raw);
    buildTopology();
    return true;
}

void StlMesh::weld(const std::vector<Vec3> &raw) {
    m_vertices.clear();
    m_triangles.clear();
    m_bounds = Box3{};

    std::unordered_map<QuantKey, uint32_t, QuantKeyHash> table;
    table.reserve(raw.size());

    std::vector<uint32_t> remap(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        const QuantKey key = quantise(raw[i]);
        auto it = table.find(key);
        if (it == table.end()) {
            const uint32_t id = static_cast<uint32_t>(m_vertices.size());
            table.emplace(key, id);
            m_vertices.push_back(raw[i]);
            m_bounds.extend(raw[i]);
            remap[i] = id;
        } else {
            remap[i] = it->second;
        }
    }

    m_triangles.reserve(raw.size() / 3);
    for (size_t i = 0; i + 2 < raw.size(); i += 3) {
        Triangle t{{remap[i], remap[i + 1], remap[i + 2]}};
        // Drop degenerate facets - they add no connectivity and would make the
        // per-triangle normal NaN.
        if (t.v[0] == t.v[1] || t.v[1] == t.v[2] || t.v[0] == t.v[2]) continue;
        m_triangles.push_back(t);
    }
}

void StlMesh::buildTopology() {
    std::unordered_map<EdgeKey, uint32_t, EdgeKeyHash> table;
    table.reserve(m_triangles.size() * 3);
    m_edges.clear();

    auto addEdge = [&](uint32_t a, uint32_t b) {
        EdgeKey key{std::min(a, b), std::max(a, b)};
        auto it = table.find(key);
        if (it == table.end()) {
            table.emplace(key, static_cast<uint32_t>(m_edges.size()));
            m_edges.push_back({key.a, key.b, 1});
        } else if (m_edges[it->second].count < 255) {
            ++m_edges[it->second].count;
        }
    };

    for (const Triangle &t : m_triangles) {
        addEdge(t.v[0], t.v[1]);
        addEdge(t.v[1], t.v[2]);
        addEdge(t.v[2], t.v[0]);
    }

    // CSR adjacency: count, prefix-sum, scatter.
    const size_t n = m_vertices.size();
    m_adjOffset.assign(n + 1, 0);
    for (const Edge &e : m_edges) {
        ++m_adjOffset[e.a + 1];
        ++m_adjOffset[e.b + 1];
    }
    for (size_t i = 0; i < n; ++i) m_adjOffset[i + 1] += m_adjOffset[i];

    m_adjacency.assign(m_adjOffset[n], 0);
    std::vector<uint32_t> cursor(m_adjOffset.begin(), m_adjOffset.end() - 1);
    for (const Edge &e : m_edges) {
        m_adjacency[cursor[e.a]++] = e.b;
        m_adjacency[cursor[e.b]++] = e.a;
    }

    findComponents();
    findBoundaryLoops();
}

void StlMesh::findComponents() {
    const size_t n = m_vertices.size();
    m_component.assign(n, -1);
    m_componentCount = 0;

    std::vector<uint32_t> stack;
    for (uint32_t s = 0; s < n; ++s) {
        if (m_component[s] != -1) continue;
        const int32_t id = m_componentCount++;
        m_component[s] = id;
        stack.push_back(s);
        while (!stack.empty()) {
            const uint32_t v = stack.back();
            stack.pop_back();
            for (uint32_t k = m_adjOffset[v]; k < m_adjOffset[v + 1]; ++k) {
                const uint32_t w = m_adjacency[k];
                if (m_component[w] == -1) {
                    m_component[w] = id;
                    stack.push_back(w);
                }
            }
        }
    }
}

void StlMesh::findBoundaryLoops() {
    const size_t n = m_vertices.size();
    m_boundaryLoop.assign(n, -1);
    m_loopVertices.clear();
    m_componentLoops.assign(m_componentCount, {});

    // Adjacency restricted to boundary edges (count == 1). On these tube
    // segments each open rim is one connected chain in that sub-graph.
    std::vector<std::vector<uint32_t>> boundaryAdj(n);
    for (const Edge &e : m_edges) {
        if (e.count != 1) continue;
        boundaryAdj[e.a].push_back(e.b);
        boundaryAdj[e.b].push_back(e.a);
    }

    m_boundaryLoopCount = 0;
    std::vector<uint32_t> stack;
    for (uint32_t s = 0; s < n; ++s) {
        if (boundaryAdj[s].empty() || m_boundaryLoop[s] != -1) continue;
        const int32_t id = m_boundaryLoopCount++;
        m_loopVertices.emplace_back();
        m_componentLoops[m_component[s]].push_back(id);
        m_boundaryLoop[s] = id;
        stack.push_back(s);
        while (!stack.empty()) {
            const uint32_t v = stack.back();
            stack.pop_back();
            m_loopVertices[id].push_back(v);
            for (uint32_t w : boundaryAdj[v]) {
                if (m_boundaryLoop[w] == -1) {
                    m_boundaryLoop[w] = id;
                    stack.push_back(w);
                }
            }
        }
    }
}

std::vector<float> StlMesh::buildInterleavedRenderData() const {
    std::vector<Vec3> normals(m_vertices.size(), Vec3());
    for (const Triangle &t : m_triangles) {
        const Vec3 &a = m_vertices[t.v[0]];
        const Vec3 &b = m_vertices[t.v[1]];
        const Vec3 &c = m_vertices[t.v[2]];
        // Area-weighted accumulation: the un-normalised cross product already
        // scales with triangle area, which is the weighting we want.
        const Vec3 fn = (b - a).cross(c - a);
        normals[t.v[0]] += fn;
        normals[t.v[1]] += fn;
        normals[t.v[2]] += fn;
    }

    std::vector<float> out;
    out.reserve(m_vertices.size() * 6);
    for (size_t i = 0; i < m_vertices.size(); ++i) {
        const Vec3 &p = m_vertices[i];
        const Vec3 nrm = normals[i].normalized();
        out.insert(out.end(), {p.x, p.y, p.z, nrm.x, nrm.y, nrm.z});
    }
    return out;
}

std::vector<uint32_t> StlMesh::buildIndexData() const {
    std::vector<uint32_t> out;
    out.reserve(m_triangles.size() * 3);
    for (const Triangle &t : m_triangles)
        out.insert(out.end(), {t.v[0], t.v[1], t.v[2]});
    return out;
}

} // namespace cn
