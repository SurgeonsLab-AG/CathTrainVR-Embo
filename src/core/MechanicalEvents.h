#pragma once
#include "CatheterModel.h"
#include <string>
#include <vector>

namespace cn {

/// A single meaningful mechanical STATE TRANSITION for one device - never a
/// per-frame sample. MechanicalEventDetector is the only thing that
/// produces these: it watches CatheterModel::MechanicalState (the same
/// core-level feedback API a training UI reads) and reports only the
/// instants something actually changed, so a training layer can react to
/// "just entered significant contact" without re-deriving that from a
/// continuous stream of state itself.
struct MechanicalEvent {
    enum class Kind {
        RegimeChanged,        ///< MechanicalState::regime transitioned - covers wall contact,
                               ///< significant contact, excessive advancement, tight curvature,
                               ///< unsupported distal device, and recovery to Free
        HighTorsionalLoad,     ///< torsionalResistance crossed up past the "high" threshold
        TorsionNormalized,     ///< torsionalResistance dropped back below it
        LossOfSupport,         ///< effectiveSupport crossed down past the "supported" threshold
        SupportRegained,       ///< effectiveSupport crossed back up past it
    };

    Kind kind = Kind::RegimeChanged;
    /// Populated for Kind::RegimeChanged only; both equal Free at
    /// construction so a default-constructed event is never mistaken for a
    /// real Free->Free transition (update() never emits one).
    CatheterModel::MechanicalState::Regime fromRegime = CatheterModel::MechanicalState::Regime::Free;
    CatheterModel::MechanicalState::Regime toRegime   = CatheterModel::MechanicalState::Regime::Free;

    /// One-line description, e.g. "FREE -> MILD CONTACT" or
    /// "HIGH TORSIONAL LOAD" - for logging or a training-event feed,
    /// without every call site re-implementing the same formatting.
    std::string describe() const;
};

/// Turns a stream of MechanicalState samples (one per CatheterModel::
/// update()) into MechanicalEvents: zero in the common case (nothing
/// changed since the last call), one or more only at an actual transition.
/// Stateful and per-device - each active device that wants event feedback
/// owns its own detector, the same way it owns its own CatheterModel.
class MechanicalEventDetector {
public:
    /// Call once per update() with that call's mechanics() - typically
    /// right where the caller already reads mechanics() for logging/
    /// rendering. Returns the events (usually none) this sample newly
    /// crossed into.
    std::vector<MechanicalEvent> update(const CatheterModel::MechanicalState &m);

    /// Discards the remembered previous sample, so the NEXT update() call
    /// is treated as the first one (no transition reported, only the
    /// starting state remembered) - use when the device itself changed
    /// (e.g. MainWindow switched which catalogue device m_catheter is)
    /// rather than its mechanical state genuinely evolving, so a device
    /// swap is never misreported as a mechanical event.
    void reset();

private:
    bool m_initialized = false;
    CatheterModel::MechanicalState::Regime m_lastRegime = CatheterModel::MechanicalState::Regime::Free;
    bool m_highTorsion = false;
    bool m_lowSupport  = false;
};

} // namespace cn
