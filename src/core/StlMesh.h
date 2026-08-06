#pragma once
#include "Vec3.h"
#include <string>
#include <vector>
#include <cstdint>

namespace cn {

/// Triangle surface mesh loaded from an STL file.
///
/// STL stores every triangle as three independent vertices, so the first thing
/// we do after reading is *weld* coincident vertices. Without welding the mesh
/// has no connectivity at all, and the centerline extractor - which walks
/// geodesic distance over the surface graph - would see 3*N isolated points.
class StlMesh {
public:
    struct Triangle { uint32_t v[3]; };

    /// Undirected edge of the welded mesh. `count` is how many triangles use
    /// it: 2 for a manifold interior edge, 1 for a boundary (open rim) edge.
    struct Edge { uint32_t a, b; uint8_t count; };

    bool load(const std::string &path, std::string *error = nullptr);

    const std::vector<Vec3> &vertices() const { return m_vertices; }
    const std::vector<Triangle> &triangles() const { return m_triangles; }
    const std::vector<Edge> &edges() const { return m_edges; }
    const Box3 &bounds() const { return m_bounds; }

    /// CSR adjacency over welded vertices: neighbours of v are
    /// adjacency()[adjacencyOffset()[v] .. adjacencyOffset()[v+1]).
    const std::vector<uint32_t> &adjacency() const { return m_adjacency; }
    const std::vector<uint32_t> &adjacencyOffset() const { return m_adjOffset; }

    /// Per-vertex connected-component id, and the number of components.
    const std::vector<int32_t> &componentOfVertex() const { return m_component; }
    int componentCount() const { return m_componentCount; }

    /// Per-vertex boundary-loop id, or -1 for interior vertices. Loops are
    /// numbered globally; loopsOfComponent() groups them per component.
    const std::vector<int32_t> &boundaryLoopOfVertex() const { return m_boundaryLoop; }
    int boundaryLoopCount() const { return m_boundaryLoopCount; }

    /// Vertex indices belonging to a given boundary loop.
    const std::vector<uint32_t> &boundaryLoopVertices(int loop) const { return m_loopVertices[loop]; }
    /// Boundary loop ids owned by a given connected component.
    const std::vector<int> &loopsOfComponent(int component) const { return m_componentLoops[component]; }

    /// Flat float array (x,y,z,nx,ny,nz per vertex) ready for a GL buffer.
    std::vector<float> buildInterleavedRenderData() const;
    std::vector<uint32_t> buildIndexData() const;

private:
    bool loadBinary(const std::string &path, std::string *error);
    bool loadAscii(const std::string &path, std::string *error);
    void weld(const std::vector<Vec3> &raw);
    void buildTopology();
    void findComponents();
    void findBoundaryLoops();

    std::vector<Vec3> m_vertices;
    std::vector<Triangle> m_triangles;
    std::vector<Edge> m_edges;
    std::vector<uint32_t> m_adjacency;
    std::vector<uint32_t> m_adjOffset;
    std::vector<int32_t> m_component;
    std::vector<int32_t> m_boundaryLoop;
    std::vector<std::vector<uint32_t>> m_loopVertices;
    std::vector<std::vector<int>> m_componentLoops;
    int m_componentCount = 0;
    int m_boundaryLoopCount = 0;
    Box3 m_bounds;
};

} // namespace cn
