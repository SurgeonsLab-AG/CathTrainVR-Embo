#pragma once
#include "CatheterModel.h"
#include <cstddef>

namespace cn {

// ─────────────────────────────────────────────────────────────────────────────
// Catalogue of the neurovascular devices the console offers.
//
// Every entry carries the manufacturer's real dimensions, because the numbers
// are what make the devices behave differently. A Sofia 6F reaches the M1 and a
// Benchmark 071 does not, and that is not a cosmetic difference: it falls out of
// a 2.11 mm outer diameter with a long compliant distal section versus 2.64 mm
// with a stiff one. Selecting by name therefore has to change the kinematics,
// not just a label in the log.
//
// Two properties do the work:
//
//   outerDiaMm       drives the containment constraint. Too big for the lumen
//                    ahead and the device reports wall contact and stops, which
//                    is the honest outcome.
//   tipFlexSegments  and segmentLengthMm set how much of the leading chain
//     + segmentLengthMm articulates and how finely. A long, finely-segmented flex zone
//                    tracks a tight carotid siphon; a short stiff one does not.
//
//   restingTipCurveDeg is the pre-shaped primary curve, applied at zero operator
//                    input. It is what makes a Cobra C2 select a branch that a
//                    straight-tipped guide slides past.
//
// Inner diameter is carried so coaxial compatibility can be checked: a device
// only passes through a sheath or guide whose ID exceeds its OD.
// ─────────────────────────────────────────────────────────────────────────────

enum class DeviceClass {
    BaseGuide,       ///< selective angiographic catheter used to select the arch vessels
    Guide,           ///< guide catheter, parked in the cervical carotid/vertebral
    GuideSheath,     ///< long sheath that replaces the femoral sheath
    DistalAccess,    ///< soft large-bore catheter tracked to the clot
    Microcatheter,   ///< delivers embolic or a stentriever
    Guidewire,
    Embolectomy      ///< Fogarty-style balloon
};

const char *deviceClassName(DeviceClass c);

/// One catalogue size of a device. A "6 Fr Envoy DA" and a "5 Fr Envoy DA" are
/// not the same catheter: the outer diameter, the lumen and often the working
/// length all change, and it is the outer diameter that decides whether the
/// device reaches the lesion. So each size carries its own dimensions rather
/// than being a label on one set of numbers.
struct CatheterSize {
    const char *label      = "";   ///< as printed on the box, e.g. "6 Fr"
    float frenchOd         = 0.f;  ///< nominal French of the OUTER diameter
    float innerDiaMm       = 0.f;  ///< lumen; what it accepts coaxially
    float outerDiaMm       = 0.f;  ///< what has to fit the vessel
    float workingLenCm     = 0.f;
};

constexpr int kMaxCatheterSizes = 4;

struct CatheterSpec {
    const char *name        = "";
    const char *maker       = "";
    DeviceClass devClass    = DeviceClass::Guide;

    CatheterSize sizes[kMaxCatheterSizes] {};
    int   sizeCount         = 0;
    int   defaultSize       = 0;   ///< index of the size most commonly used

    float proximalDiaMm     = 0.f; ///< 0 when the shaft is not stepped

    float restingTipCurveDeg  = 0.f; ///< pre-shaped primary curve
    float maxTipDeflectionDeg = 45.f;
    int   tipFlexSegments     = 6;
    float segmentLengthMm     = 1.5f;

    bool  hasBalloon        = false;
    float balloonDiaMm      = 0.f;
    float balloonOffsetMm   = 0.f;

    const char *note        = "";

    /// Bounds-checked size lookup; falls back to the default size.
    const CatheterSize &size(int i) const {
        if (i >= 0 && i < sizeCount) return sizes[i];
        return sizes[defaultSize >= 0 && defaultSize < sizeCount ? defaultSize : 0];
    }
};

/// Full catalogue. `count` receives the number of entries.
const CatheterSpec *catheterCatalog(size_t &count);

/// Look up by the exact display name used in the selection dialogs, e.g.
/// "Sofia 6F (distal access)". Returns nullptr when there is no match.
const CatheterSpec *findCatheter(const char *displayName);

/// Translate a catalogue entry into kinematic parameters.
/// `mmPerUnit` converts millimetres to model units (1000 for a metre-scale model).
CatheterModel::Params catheterParams(const CatheterSpec &spec, int sizeIndex, float mmPerUnit);

/// True when `inner` will pass coaxially through `outer` (OD < ID, with a
/// working clearance). This is what stops a Neuron MAX 088 being "advanced"
/// through a 6 Fr femoral sheath.
bool fitsCoaxially(const CatheterSpec &inner, int sizeIndex, float hostInnerDiaMm,
                   float clearanceMm = 0.02f);

/// Index of `label` within `spec`, or the default size when there is no match.
int sizeIndexFor(const CatheterSpec &spec, const char *label);

} // namespace cn