#pragma once
#include "CenterlineGraph.h"
#include "EmbolizationModel.h"
#include "CoilEmbolizationModel.h"
#include <vector>
#include <cstdint>

namespace cn {

/// The three procedural phases a real diagnostic-catheter contrast run
/// actually has, in order. Flush is not merely "no injection" - it is its
/// own state (accelerated washout) between the two angiogram phases, the
/// same way a real operator flushes the catheter with saline between runs
/// rather than just waiting for blood flow to clear it on its own.
enum class ContrastPhase { Idle, PreEmbolizationAngiogram, Flush, PostEmbolizationAngiogram };

/// Iodinated contrast: the SAME graph-distance flood-fill growth mechanism
/// EmbolizationModel uses (see that class's own comment for why this is a
/// deliberate reduced model, not a fluid simulation), but fundamentally
/// different in two ways that are the actual point of this class:
///
///   1. TEMPORARY. A permanent embolic cast never decays; contrast always
///      does — tick() shrinks the fill front over real time whether or not
///      anything is actively being injected, because blood flow washes
///      contrast out continuously. Flush phase simply shrinks it much
///      faster, the same way saline flushed through the catheter actually
///      clears it faster than blood flow alone would.
///
///   2. OCCLUSION-AWARE. Given pointers to the shared EmbolizationModel and
///      CoilEmbolizationModel, the flood fill treats any link EITHER model
///      reports as substantially cast/packed as impassable — contrast
///      literally cannot flow past it, regardless of which of the two
///      devices did the occluding. This is what makes a post-embolization
///      angiogram mechanistically show reduced/absent flow through the
///      target vessel whether the case was embolized with a liquid agent,
///      a coil nest, or both together (proximal protection coil + distal
///      liquid agent is a real, common MMA technique) — it is a genuine
///      consequence of the same graph both casts grew on, not a scripted
///      before/after image swap.
class ContrastInjectionModel {
public:
    struct ContrastLink {
        int32_t from = -1, to = -1;
        float weightedLength = 0.f;
        float level = 0.f;   ///< 0..1 — current concentration, NOT permanent
    };

    struct State {
        ContrastPhase phase        = ContrastPhase::Idle;
        int32_t       injectionNode = -1;
        float         volumeInjected  = 0.f;
        float         filledArcLength = 0.f;
        std::vector<ContrastLink> links;
    };

    void setGraph(const CenterlineGraph *g) { m_graph = g; }
    /// Optional. Null is a valid, meaningful state — it means "nothing is
    /// embolized yet", exactly the situation during a pre-embolization
    /// angiogram, and the flood fill then behaves as pure unobstructed flow.
    void setEmbolization(const EmbolizationModel *e) { m_emboli = e; }
    /// Optional, independent of setEmbolization() — a case can be coiled,
    /// liquid-embolized, or both, and either alone is enough to block a
    /// link. Null means "no coils deployed yet", the same meaningful-empty
    /// convention setEmbolization() already uses.
    void setCoils(const CoilEmbolizationModel *c) { m_coils = c; }

    /// Starts (or re-starts) a phase.
    ///   PreEmbolizationAngiogram / PostEmbolizationAngiogram — `node` is the
    ///     catheter tip's current graph node; the cast resets and regrows
    ///     from there, re-reading whatever the EmbolizationModel currently
    ///     reports as occluded at the moment this is called.
    ///   Flush — `node` is ignored; whatever contrast currently exists just
    ///     starts draining at the fast (flush) rate rather than the slow
    ///     (natural washout) one.
    ///   Idle — clears everything.
    ///
    /// `excludeNeighbor`, if >= 0, is treated as unreachable directly from
    /// `node` - the same fix EmbolizationModel::begin() already has, and
    /// for the identical reason: kContrastViscosity (0.12) is small but
    /// non-zero, so the weighted flood-fill still has a weaker version of
    /// the same wide-channel-preferring bias that made embolic material
    /// reflux backward into a wider parent vessel before that fix. Contrast
    /// is thinner and cares about it less, but the bias is not actually
    /// zero, so a genuinely correct fix needs the same exclusion, not just
    /// a smaller viscosity constant. The caller (MainWindow) already has
    /// findProximalNeighbor() for exactly this.
    void beginPhase(ContrastPhase phase, int32_t node = -1, int32_t excludeNeighbor = -1);

    /// Injects `dVolume` (>= 0) more contrast. No-op outside an angiogram
    /// phase (Idle/Flush do not accept new injection - a real operator does
    /// not inject while flushing).
    void inject(float dVolume);

    /// Advances real time by `dtSeconds`. Contrast always decays here -
    /// faster during Flush, slower otherwise - which is what gives it its
    /// "temporary, clears from the vessel" behaviour without any extra
    /// bookkeeping at the call site: just call this once per tick regardless
    /// of phase or whether inject() was also called this frame.
    void tick(float dtSeconds);

    void reset();

    ContrastPhase phase() const { return m_state.phase; }
    const State &state() const { return m_state; }

private:
    void recomputeWeightedDistances();
    void rebuildLinks();
    bool linkOccluded(int32_t a, int32_t b) const;

    const CenterlineGraph       *m_graph  = nullptr;
    const EmbolizationModel     *m_emboli = nullptr;
    const CoilEmbolizationModel *m_coils  = nullptr;
    State m_state;
    std::vector<float> m_weightedDist;
    /// See beginPhase()'s own comment - the proximal neighbor node the
    /// catheter's own body blocks, or -1 if none was given.
    int32_t m_excludedNeighbor = -1;
};

} // namespace cn