#include "CatheterCatalog.h"
#include <cmath>
#include <cstring>

namespace cn {

const char *deviceClassName(DeviceClass c) {
    switch (c) {
    case DeviceClass::BaseGuide:     return "base guide";
    case DeviceClass::Guide:         return "guide";
    case DeviceClass::GuideSheath:   return "guide sheath";
    case DeviceClass::DistalAccess:  return "distal access";
    case DeviceClass::Microcatheter: return "microcatheter";
    case DeviceClass::Guidewire:     return "guidewire";
    case DeviceClass::Embolectomy:   return "embolectomy";
    }
    return "?";
}

namespace {

// Dimensions are the manufacturers' published figures, converted from inches at
// 25.4 mm/in. French sizes are quoted where the maker quotes them.
//
// The kinematic columns (curve, deflection, flex segments, segment length) are
// tuned so the devices rank the way they do on the table: stiffness and
// trackability increase down the guide list, and the pre-shaped Cobra curve is
// the only one that selects a branch without operator input.
const CatheterSpec kCatalog[] = {

{ "Cobra C2 (base guide)", "Terumo Radifocus / Cook", DeviceClass::BaseGuide,
 { { "4 Fr", 4.05f, 0.89f, 1.35f, 100.f },
  { "5 Fr", 5.01f, 0.97f, 1.67f, 100.f } }, 2, 1,
 /*prox*/ 0.f, /*curve*/ 135.f, /*maxDefl*/ 60.f, /*flexSeg*/ 5, /*segLen*/ 2.0f,
 false, 0.f, 0.f,
 "0.035-0.038\" lumen. The C2 reverse curve is the point of it: the pre-shaped tip "
 "catches an arch-vessel ostium a straight guide slides past. Stiff braided shaft, "
 "so it selects well but tracks poorly." },

    { "Benchmark 071 (guide)", "Penumbra", DeviceClass::Guide,
     { { "071 (8 Fr)", 7.92f, 1.80f, 2.64f, 95.f } }, 1, 0,
     /*prox*/ 0.f, /*curve*/ 0.f, /*maxDefl*/ 35.f, /*flexSeg*/ 4, /*segLen*/ 2.0f,
     false, 0.f, 0.f,
     "0.071\" lumen, 0.104\" OD, single size. Large bore for aspiration, but the widest "
     "and stiffest guide here — it parks in the cervical ICA and goes no further." },

    { "Envoy DA (guide)", "Cerenovus", DeviceClass::Guide,
     { { "5 Fr", 5.79f, 1.42f, 1.93f, 105.f },
      { "6 Fr", 6.72f, 1.80f, 2.24f, 105.f },
      { "7 Fr", 7.53f, 2.06f, 2.51f, 100.f } }, 3, 1,
     /*prox*/ 0.f, /*curve*/ 0.f, /*maxDefl*/ 45.f, /*flexSeg*/ 6, /*segLen*/ 1.8f,
     false, 0.f, 0.f,
     "Distal-access guide. The 6 Fr carries the same 0.071\" lumen as a Benchmark in a "
     "slimmer 0.088\" body, so it reaches higher in the petrous segment." },

    { "Neuron MAX 088 (guide sheath)", "Penumbra", DeviceClass::GuideSheath,
     { { "088", 8.07f, 2.24f, 2.69f, 90.f } }, 1, 0,
     /*prox*/ 2.95f, /*curve*/ 0.f, /*maxDefl*/ 55.f, /*flexSeg*/ 10, /*segLen*/ 1.5f,
     false, 0.f, 0.f,
     "0.088\" lumen long sheath, single size. Replaces the femoral sheath rather than "
     "passing through it. Widest device in the set, but a long compliant distal section "
     "lets it track the arch into the common carotid." },

    { "Sofia 6F (distal access)", "MicroVention", DeviceClass::DistalAccess,
     { { "5 Fr", 5.19f, 1.40f, 1.73f, 125.f },
      { "6 Fr", 6.33f, 1.78f, 2.11f, 125.f },
      { "6 Fr Plus", 6.24f, 1.78f, 2.08f, 131.f } }, 3, 1,
     /*prox*/ 0.f, /*curve*/ 0.f, /*maxDefl*/ 80.f, /*flexSeg*/ 16, /*segLen*/ 1.0f,
     false, 0.f, 0.f,
     "The softest and longest-reaching large-bore catheter here — the one that actually "
     "gets to the M1 clot face. Coaxial through a Neuron MAX 088 or an Envoy DA." },

    { "Terumo Progreat", "Terumo", DeviceClass::Microcatheter,
     { { "2.7 Fr", 2.70f, 0.65f, 0.90f, 130.f },
      { "2.8 Fr", 2.85f, 0.71f, 0.95f, 130.f },
      { "3.0 Fr", 3.00f, 0.76f, 1.00f, 110.f } }, 3, 0,
     /*prox*/ 0.f, /*curve*/ 0.f, /*maxDefl*/ 75.f, /*flexSeg*/ 14, /*segLen*/ 0.8f,
     false, 0.f, 0.f,
     "Coaxial microcatheter with a shapeable tip." },

    { "Marathon (DMSO-compatible)", "Medtronic", DeviceClass::Microcatheter,
     { { "1.5 Fr tip", 1.50f, 0.33f, 0.50f, 165.f } }, 1, 0,
     /*prox*/ 0.9f, /*curve*/ 0.f, /*maxDefl*/ 95.f, /*flexSeg*/ 20, /*segLen*/ 0.6f,
     false, 0.f, 0.f,
     "1.5 Fr distal tip on a 2.7 Fr shaft, flow-directed, DMSO compatible for Onyx." },

    { "Apollo (detachable-tip)", "Medtronic", DeviceClass::Microcatheter,
     { { "1.5 cm tip", 1.89f, 0.33f, 0.63f, 165.f },
      { "3 cm tip", 1.89f, 0.33f, 0.63f, 165.f } }, 2, 0,
     /*prox*/ 0.9f, /*curve*/ 0.f, /*maxDefl*/ 90.f, /*flexSeg*/ 18, /*segLen*/ 0.6f,
     false, 0.f, 0.f,
     "Detachable distal tip, so the catheter can be withdrawn from a cast of set embolic." },

    { "Sonic (detachable-tip)", "Balt", DeviceClass::Microcatheter,
     { { "1.2 Fr", 1.20f, 0.30f, 0.40f, 165.f },
      { "1.5 Fr", 1.65f, 0.30f, 0.55f, 165.f } }, 2, 1,
     /*prox*/ 0.8f, /*curve*/ 0.f, /*maxDefl*/ 95.f, /*flexSeg*/ 20, /*segLen*/ 0.6f,
     false, 0.f, 0.f,
     "Flow-directed with a detachable tip." },

    { "Echelon-10 (DMSO-compatible)", "Medtronic", DeviceClass::Microcatheter,
     { { "0.017\"", 2.10f, 0.43f, 0.70f, 150.f } }, 1, 0,
     /*prox*/ 0.77f, /*curve*/ 0.f, /*maxDefl*/ 85.f, /*flexSeg*/ 16, /*segLen*/ 0.7f,
     false, 0.f, 0.f,
     "0.017\" lumen, over-the-wire, DMSO compatible." },

    { "Headway DUO", "MicroVention", DeviceClass::Microcatheter,
     { { "1.6 Fr", 1.59f, 0.42f, 0.53f, 156.f },
      { "1.6 Fr 167", 1.59f, 0.42f, 0.53f, 167.f } }, 2, 0,
     /*prox*/ 0.7f, /*curve*/ 0.f, /*maxDefl*/ 90.f, /*flexSeg*/ 18, /*segLen*/ 0.6f,
     false, 0.f, 0.f,
     "0.0165\" lumen — among the smallest that still takes a coil." },

    { "Synchro-14", "Stryker", DeviceClass::Guidewire,
     { { "0.014\"", 1.08f, 0.00f, 0.36f, 200.f },
      { "0.014\" 300", 1.08f, 0.00f, 0.36f, 300.f } }, 2, 0,
     /*prox*/ 0.f, /*curve*/ 30.f, /*maxDefl*/ 110.f, /*flexSeg*/ 24, /*segLen*/ 0.5f,
     false, 0.f, 0.f,
     "0.014\" neuro wire, shapeable tip." },

    { "Traxcess-14", "MicroVention", DeviceClass::Guidewire,
     { { "0.014\"", 1.08f, 0.00f, 0.36f, 200.f } }, 1, 0,
     /*prox*/ 0.f, /*curve*/ 30.f, /*maxDefl*/ 110.f, /*flexSeg*/ 24, /*segLen*/ 0.5f,
     false, 0.f, 0.f,
     "0.014\" hybrid wire with a detachable extension." },

    { "Mirage 008", "Medtronic", DeviceClass::Guidewire,
     { { "0.008\"", 0.60f, 0.00f, 0.20f, 200.f } }, 1, 0,
     /*prox*/ 0.f, /*curve*/ 25.f, /*maxDefl*/ 120.f, /*flexSeg*/ 28, /*segLen*/ 0.4f,
     false, 0.f, 0.f,
     "0.008\" hydrophilic wire for flow-directed microcatheters." },

    { "Hybrid 007", "Balt", DeviceClass::Guidewire,
     { { "0.007\"", 0.54f, 0.00f, 0.18f, 200.f } }, 1, 0,
     /*prox*/ 0.f, /*curve*/ 25.f, /*maxDefl*/ 120.f, /*flexSeg*/ 28, /*segLen*/ 0.4f,
     false, 0.f, 0.f,
     "0.007\" wire — the finest here." },

    { "2 Fr embolectomy (balloon 4 mm)", "Edwards Fogarty", DeviceClass::Embolectomy,
     { { "2 Fr", 2.00f, 0.00f, 0.67f, 80.f } }, 1, 0,
     /*prox*/ 0.f, /*curve*/ 0.f, /*maxDefl*/ 45.f, /*flexSeg*/ 6, /*segLen*/ 1.5f,
     true, 4.0f, 4.0f,
     "" },

    { "3 Fr embolectomy (balloon 5 mm)", "Edwards Fogarty", DeviceClass::Embolectomy,
     { { "3 Fr", 3.00f, 0.00f, 1.00f, 80.f } }, 1, 0,
     /*prox*/ 0.f, /*curve*/ 0.f, /*maxDefl*/ 45.f, /*flexSeg*/ 6, /*segLen*/ 1.5f,
     true, 5.0f, 4.5f,
     "" },

    { "4 Fr embolectomy (balloon 9 mm)", "Edwards Fogarty", DeviceClass::Embolectomy,
     { { "4 Fr", 4.00f, 0.00f, 1.33f, 80.f } }, 1, 0,
     /*prox*/ 0.f, /*curve*/ 0.f, /*maxDefl*/ 45.f, /*flexSeg*/ 6, /*segLen*/ 1.5f,
     true, 9.0f, 5.5f,
     "" },

    { "5 Fr embolectomy (balloon 11 mm)", "Edwards Fogarty", DeviceClass::Embolectomy,
     { { "5 Fr", 5.00f, 0.00f, 1.67f, 80.f } }, 1, 0,
     /*prox*/ 0.f, /*curve*/ 0.f, /*maxDefl*/ 45.f, /*flexSeg*/ 6, /*segLen*/ 1.5f,
     true, 11.0f, 6.0f,
     "" },

    { "6 Fr embolectomy (balloon 13 mm)", "Edwards Fogarty", DeviceClass::Embolectomy,
     { { "6 Fr", 6.00f, 0.00f, 2.00f, 80.f } }, 1, 0,
     /*prox*/ 0.f, /*curve*/ 0.f, /*maxDefl*/ 45.f, /*flexSeg*/ 6, /*segLen*/ 1.5f,
     true, 13.0f, 6.5f,
     "" },
    };

constexpr float kDegToRad = 3.14159265358979f / 180.f;

} // namespace

const CatheterSpec *catheterCatalog(size_t &count) {
    count = sizeof(kCatalog) / sizeof(kCatalog[0]);
    return kCatalog;
}

const CatheterSpec *findCatheter(const char *displayName) {
    if (!displayName) return nullptr;
    size_t n = 0;
    const CatheterSpec *cat = catheterCatalog(n);
    for (size_t i = 0; i < n; ++i)
        if (std::strcmp(cat[i].name, displayName) == 0) return &cat[i];
    return nullptr;
}

CatheterModel::Params catheterParams(const CatheterSpec &spec, int sizeIndex,
                                     float mmPerUnit) {
    const float u = 1.f / (mmPerUnit > 0.f ? mmPerUnit : 1000.f);
    const CatheterSize &sz = spec.size(sizeIndex);

    CatheterModel::Params p;
    p.deviceRadius    = 0.5f * sz.outerDiaMm * u;
    p.segmentLength   = spec.segmentLengthMm * u;
    p.tipFlexSegments = spec.tipFlexSegments;
    p.maxTipDeflection = spec.maxTipDeflectionDeg * kDegToRad;
    p.restingTipCurve  = spec.restingTipCurveDeg * kDegToRad;

    // Enough chain to span the device's own working length, so the shaft really
    // does reach back to the sheath instead of stopping short of it.
    const float lenModel = sz.workingLenCm * 10.f * u;
    p.segmentCount = 40;
    p.autoLength   = true;
    p.maxSegments  = static_cast<int>(lenModel / p.segmentLength) + 8;

    p.hasBalloon = spec.hasBalloon;
    if (spec.hasBalloon) {
        p.balloonMaxRadius = 0.5f * spec.balloonDiaMm * u;
        p.balloonOffset    = spec.balloonOffsetMm * u;
        p.balloonLength    = spec.balloonDiaMm * 0.75f * u;
    }
    p.balloonInflation = 0.f;
    return p;
}

bool fitsCoaxially(const CatheterSpec &inner, int sizeIndex, float hostInnerDiaMm,
                   float clearanceMm) {
    // Nominal sheath IDs run slightly generous, so a 2.64 mm Benchmark 071 does
    // pass an 8 Fr (2.67 mm) sheath in practice. A 2.69 mm Neuron MAX does not,
    // which is why it replaces the femoral sheath rather than going through it.
    if (hostInnerDiaMm <= 0.f) return true;   // nothing to pass through
    return inner.size(sizeIndex).outerDiaMm + clearanceMm <= hostInnerDiaMm;
}

int sizeIndexFor(const CatheterSpec &spec, const char *label) {
    if (label)
        for (int i = 0; i < spec.sizeCount; ++i)
            if (std::strcmp(spec.sizes[i].label, label) == 0) return i;
    return spec.defaultSize;
}

} // namespace cn