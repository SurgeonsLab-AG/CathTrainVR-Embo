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

    /// How much the shaft resists bending, 0..1 - see
    /// CatheterModel::Params::shaftStiffness for what this actually does at
    /// render time. A stiff braided guide (Benchmark 071) cuts corners
    /// through a bend and leans on the outer wall; a soft flow-directed
    /// microcatheter (Marathon, Sonic) tracks close to the true centerline.
    float shaftStiffness      = 0.5f;

    bool  hasBalloon        = false;
    float balloonDiaMm      = 0.f;
    float balloonOffsetMm   = 0.f;

    const char *note        = "";

    /// Display colour in the 3D View, RGB 0..1. Defaults to the same blue
    /// every device rendered as before this field existed - it is the LAST
    /// member specifically so every existing catalog row (whose aggregate
    /// initializer only ever supplied values through `note`) keeps that
    /// default automatically; only a row that explicitly adds a trailing
    /// {r,g,b} looks different. Fluoro is unaffected - it renders every
    /// device in its own grayscale/radiopaque palette regardless of this.
    float colour[3] = {0.36f, 0.72f, 0.94f};

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

// ─────────────────────────────────────────────────────────────────────────────
// Diagnostic/selective catheter shapes.
//
// A "Cobra C2" and a "Simmons 2" are not different catheters in the sense a
// Sofia and a Benchmark are: same braided shaft, same OD/lumen/working length
// family, same handling. What differs is entirely the heat-set curve at the
// tip that decides which arch vessel it engages. Catalog those as one real
// physical body (kDiagnosticCatheter below) plus a set of named tip shapes,
// rather than as separate CatheterSpec entries that would otherwise repeat
// the same dimensions seven times over.
//
// The seven shapes mirror the standard reference sheet: a near-straight
// angled taper / vertebral curve, three Simmons sizes of increasing primary-
// loop angle and reach, a Newton's tight double-kink concentrated right at
// the tip, a Cobra's single smooth reverse curve, and a Headhunter's wider,
// less hooked single curve.
//
// The angles and length fractions below are reasoned from that reference
// shape, not traced from a manufacturer's dimensional drawing - they are
// what makes each preset visually distinct and in the right family (a SIM3
// loops further back than a SIM1; a Newt's kink sits only at the very tip;
// a CK1 is one open hook, no secondary bend), not a claim of clinical
// accuracy to a specific catalogue curve.
struct TipShapeDef {
    const char *name = "";
    const char *note = "";

    struct Seg { float lengthFraction; float curveDeg; };
    static constexpr int kMaxSegments = 4;
    Seg segs[kMaxSegments] {};
    int segCount = 0;

    /// Segments this shape needs to read as smoothly curved rather than
    /// faceted, and enough segment length to give the compound curve room to
    /// unfold before the shaft runs out. A single open hook (CK1, H1) needs
    /// far less resolution than a Simmons loop-back.
    int   recommendedFlexSegments = 10;
    float recommendedSegLenMm     = 1.2f;
};

/// Full tip-shape catalog. `count` receives the number of entries.
const TipShapeDef *tipShapeCatalog(size_t &count);

/// Look up by display name, e.g. "SIM2" or "Headhunter (H1)". Returns
/// nullptr when there is no match.
const TipShapeDef *findTipShape(const char *displayName);

/// The one physical catheter body every shape above is heat-set onto: a
/// generic braided 4/5 Fr diagnostic/selective catheter — 0.038" lumen,
/// 100 cm working length, the same family a real Cobra, Simmons, Headhunter
/// or vertebral catheter actually ships in. Selecting a different shape does
/// not change this body at all, which is the point: the shapes are
/// interchangeable heat-sets of one device, not seven different catheters.
const CatheterSpec &diagnosticCatheterSpec();

/// Bakes a named tip shape onto `base` (normally diagnosticCatheterSpec()'s
/// own params), converting degrees to radians and raising resolution
/// (tipFlexSegments up, segmentLength down) only as far as the shape's own
/// recommendation needs — never the other way, so a caller that already
/// asked for finer resolution than the shape requires keeps it.
/// `mmPerUnit` converts the shape's recommended segment length (mm) into the
/// same model units `base.segmentLength` is already expressed in.
CatheterModel::Params applyTipShape(CatheterModel::Params base, const TipShapeDef &shape,
                                    float mmPerUnit);

} // namespace cn