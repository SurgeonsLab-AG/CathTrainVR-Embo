#pragma once
#include "CenterlineGraph.h"
#include "EmbolizationModel.h"
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
///   2. OCCLUSION-AWARE. Given a pointer to the shared EmbolizationModel,
///      the flood fill treats any link that model reports as substantially
///      cast as impassable — contrast literally cannot flow past it. This is
///      what makes a post-embolization angiogram mechanistically show
///      reduced/absent flow through the target vessel: it is a genuine
///      consequence of the same graph the embolic cast grew on, not a
///      scripted before/after image swap.
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

    /// Starts (or re-starts) a phase.
    ///   PreEmbolizationAngiogram / PostEmbolizationAngiogram — `node` is the
    ///     catheter tip's current graph node; the cast resets and regrows
    ///     from there, re-reading whatever the EmbolizationModel currently
    ///     reports as occluded at the moment this is called.
    ///   Flush — `node` is ignored; whatever contrast currently exists just
    ///     starts draining at the fast (flush) rate rather than the slow
    ///     (natural washout) one.
    ///   Idle — clears everything.
    void beginPhase(ContrastPhase phase, int32_t node = -1);

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

    const CenterlineGraph   *m_graph  = nullptr;
    const EmbolizationModel *m_emboli = nullptr;
    State m_state;
    std::vector<float> m_weightedDist;
};

} // namespace cn