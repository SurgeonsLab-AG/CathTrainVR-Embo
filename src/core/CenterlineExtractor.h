#pragma once
#include "CenterlineGraph.h"
#include "StlMesh.h"

namespace cn {

/// Extracts a centerline tree from a tubular surface mesh.
///
/// Algorithm (geodesic level-set / Reeb-graph contraction):
///
///  1. For each connected shell, take one open rim (boundary loop) as a
///     multi-source seed and run Dijkstra over the *surface* edge graph.
///     On a tube the resulting iso-distance contours are cross-sections,
///     and they follow the vessel around bends - which a PCA / plane-slicing
///     approach does not.
///  2. Bin vertices by geodesic distance. Within a bin, split by surface
///     connectivity: one cluster per cross-section, so a shell that
///     bifurcates internally yields two clusters and the graph branches
///     automatically.
///  3. Each cluster contributes one centerline node at its centroid, with
///     radius = mean distance from centroid to the cluster's vertices. For a
///     closed contour that is exactly the local lumen radius.
///  4. Link nodes whose clusters share a surface edge.
///  5. Repair degenerate radii at the rims, join shells whose free ends are
///     within tolerance, then Laplacian-smooth.
///
/// The mesh does not have to be a single watertight solid: this model arrives
/// as 30 separate open tube shells that abut end to end, and step 5 stitches
/// them into one navigable tree.
class CenterlineExtractor {
public:
    struct Params {
        /// Geodesic bin width, in model units. Roughly the centerline node
        /// spacing. Too small and bins fragment into partial rings; a good
        /// default is ~4x the median mesh edge length.
        float sampleStep = 0.008f;

        /// Free ends closer than max(minJoinTolerance, joinRadiusFactor *
        /// local radius) get stitched together across shells.
        float joinRadiusFactor = 5.0f;
        float minJoinTolerance = 0.025f;

        /// Laplacian smoothing of the extracted polyline.
        int smoothIterations = 6;
        float smoothLambda = 0.35f;

        /// Radius repair passes (replace rim artefacts with neighbour median).
        int radiusRepairPasses = 2;

        /// Shells with fewer vertices than this are ignored as mesh debris.
        int minComponentVertices = 12;

        /// Auto-tune sampleStep from the mesh's median edge length.
        bool autoSampleStep = true;
        float autoStepFactor = 4.5f;
    };

    struct Report {
        int shellsProcessed = 0;
        int shellsSkipped = 0;
        int nodes = 0;
        int links = 0;
        int stitchedJoints = 0;
        int bifurcations = 0;
        int terminals = 0;
        float totalCenterlineLength = 0.f;
        float minRadius = 0.f, maxRadius = 0.f, meanRadius = 0.f;
        float effectiveSampleStep = 0.f;
    };

    CenterlineExtractor() = default;
    explicit CenterlineExtractor(const Params &p) : m_params(p) {}

    /// Runs the full pipeline. Returns false only if the mesh is unusable.
    bool extract(const StlMesh &mesh, CenterlineGraph &out, Report *report = nullptr);

    const Params &params() const { return m_params; }
    void setParams(const Params &p) { m_params = p; }

private:
    /// Geodesic distance from a seed vertex set, restricted to one shell.
    static void geodesicFromSeeds(const StlMesh &mesh,
                                  const std::vector<uint32_t> &shellVertices,
                                  const std::vector<uint32_t> &seeds,
                                  std::vector<float> &distOut);

    /// Vertex farthest from `from` within the shell - used when a shell has no
    /// open rim to seed from.
    static uint32_t farthestVertex(const StlMesh &mesh,
                                   const std::vector<uint32_t> &shellVertices,
                                   uint32_t from);

    void repairRadii(CenterlineGraph &g) const;
    int stitchFreeEnds(CenterlineGraph &g) const;

    Params m_params;
};

} // namespace cn
