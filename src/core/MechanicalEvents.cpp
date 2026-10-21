#include "MechanicalEvents.h"

namespace cn {

namespace {
// Same thresholds updateMechanicalState() already uses for its own Regime
// boundaries (0.5 for "resisted", 0.05 for "no support at all") - reused
// here rather than invented fresh, so an event fires at exactly the point
// the regime/API's own numbers would already call it meaningful.
constexpr float kHighTorsionThreshold = 0.5f;
constexpr float kLowSupportThreshold  = 0.05f;
} // namespace

std::string MechanicalEvent::describe() const {
    switch (kind) {
    case Kind::RegimeChanged:
        return std::string(mechanicalRegimeName(fromRegime)) + " -> " + mechanicalRegimeName(toRegime);
    case Kind::HighTorsionalLoad:
        return "HIGH TORSIONAL LOAD";
    case Kind::TorsionNormalized:
        return "TORSION NORMALIZED";
    case Kind::LossOfSupport:
        return "LOSS OF SUPPORT";
    case Kind::SupportRegained:
        return "SUPPORT REGAINED";
    }
    return "?";
}

void MechanicalEventDetector::reset() {
    m_initialized = false;
}

std::vector<MechanicalEvent> MechanicalEventDetector::update(const CatheterModel::MechanicalState &m) {
    std::vector<MechanicalEvent> events;

    const bool highTorsionNow = m.torsionalResistance >= kHighTorsionThreshold;
    const bool lowSupportNow  = m.effectiveSupport < kLowSupportThreshold;

    if (!m_initialized) {
        // First sample ever (or right after reset()) - remember where we
        // start, but a starting condition is not itself a transition, so
        // nothing is reported for it.
        m_lastRegime  = m.regime;
        m_highTorsion = highTorsionNow;
        m_lowSupport  = lowSupportNow;
        m_initialized = true;
        return events;
    }

    if (m.regime != m_lastRegime) {
        MechanicalEvent ev;
        ev.kind = MechanicalEvent::Kind::RegimeChanged;
        ev.fromRegime = m_lastRegime;
        ev.toRegime = m.regime;
        events.push_back(ev);
        m_lastRegime = m.regime;
    }

    if (highTorsionNow != m_highTorsion) {
        events.push_back({highTorsionNow ? MechanicalEvent::Kind::HighTorsionalLoad
                                          : MechanicalEvent::Kind::TorsionNormalized,
                           CatheterModel::MechanicalState::Regime::Free,
                           CatheterModel::MechanicalState::Regime::Free});
        m_highTorsion = highTorsionNow;
    }

    if (lowSupportNow != m_lowSupport) {
        events.push_back({lowSupportNow ? MechanicalEvent::Kind::LossOfSupport
                                         : MechanicalEvent::Kind::SupportRegained,
                           CatheterModel::MechanicalState::Regime::Free,
                           CatheterModel::MechanicalState::Regime::Free});
        m_lowSupport = lowSupportNow;
    }

    return events;
}

} // namespace cn
