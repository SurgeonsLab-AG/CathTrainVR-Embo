#include "VesselView.h"

#include <QDebug>
#include <QOpenGLShaderProgram>
#include <QOpenGLFramebufferObject>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QVector>
#include <QQuaternion>
#include <QSurfaceFormat>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

namespace {

const char *kVertexShader = R"(#version 330 core
layout(location = 0) in vec3  aPos;
layout(location = 1) in vec3  aNormal;
// Supplied only by the vessel wall. Relying on these reading back as 0 for
// other batches was not enough: attribute defaults are context state rather than
// VAO state, and the body layers are drawn with raw glDrawElements outside
// drawBatch(), so the guarantee was too subtle to be worth trusting. The
// uPulsatile uniform below makes it explicit at every draw call instead.
layout(location = 2) in float aRootDist;    // arc length from the aortic root
layout(location = 3) in float aPulseAmp;    // displacement amplitude, model units

uniform mat4  uViewProj;
uniform float uPhase;      // cardiac phase 0..1
uniform float uCycleSec;   // seconds per beat at the current rate
uniform float uPwv;        // pulse wave velocity, model units/second
uniform float uStrain;     // peak fractional radius increase; 0 disables
uniform int   uPulsatile;  // 1 ONLY for the vessel wall; 0 for everything else
uniform float uPulseLUT[64];
// Arc length from the root within which transit delay is NOT applied — see
// the comment in main() below for why this exists.
uniform float uHeartTravelExempt;

out vec3 vNormal;
out vec3 vWorld;
// Fluoro's heart-region accumulation (see kFragmentShader, uXrayIsVessel)
// reuses this exact per-vertex value as a mask for "is this part of the
// vessel the heart/great-vessel region" — it is already precisely that:
// aPulseAmp is computed in rebuildWallBatch as localRadius * spatialWeight
// * calibreWeight * superiorWeight, and superiorWeight is what multiplies
// it down to zero above the aortic arch, which is what already keeps the
// carotids from physically pulsing. The same product works unchanged as a
// visibility mask, with no separate centerline query needed.
out float vHeartMask;

// The pressure pulse is defined once, in cn::CardiacCycle, and handed to the
// shader as 64 samples. Duplicating the waveform in GLSL would guarantee the
// wall and the ECG drift apart the first time one of them is edited.
float pulseAt(float p) {
    float x = fract(p) * 64.0;
    int   i = int(x);
    return mix(uPulseLUT[i], uPulseLUT[(i + 1) % 64], fract(x));
}

void main() {
    vec3 pos = aPos;
    // Two independent conditions have to hold. uPulsatile is set by the draw
    // call, so a batch that never opts in cannot move whatever its vertex data
    // happens to contain — the Muscular and Skeletal layers among them.
    if (uPulsatile == 1 && uStrain > 0.0 && aPulseAmp > 0.0) {
        // The pressure wave travels: a vertex 1 m down the tree distends about
        // 170 ms after the aortic root at 6 m/s, which is a fifth of a cycle.
        // That transit-delay model is correct for the downstream ARTERIAL
        // tree, where a real elastic-wall pressure wave genuinely does take
        // time to propagate. It is NOT correct for the heart itself: cardiac
        // contraction spreads through the myocardium via electrical
        // conduction, effectively simultaneously across the whole chamber at
        // this scale - so applying the arterial transit-delay formula inside
        // the heart region too (root vs. an adjoining coronary origin only
        // millimetres away) made those points bulge at a visibly different
        // moment, reading as two separate, uncoordinated pumps rather than
        // one - which is the actual reason a vessel near/adjoining the heart
        // could look like it "pumps separately". uHeartTravelExempt (set to
        // HeartRegion::coreRadius + falloff from C++) removes exactly that:
        // every vertex within the heart region shares zero relative delay,
        // so the whole region moves as one synchronized pump; only distance
        // travelled BEYOND it accrues real transit delay.
        float travelDist = max(aRootDist - uHeartTravelExempt, 0.0);
        float delayed = uPhase - (travelDist / uPwv) / max(uCycleSec, 1e-4);
        pos += normalize(aNormal) * (aPulseAmp * uStrain * pulseAt(delayed));
    }
    vNormal = aNormal;
    vWorld  = pos;
    vHeartMask = aPulseAmp;
    gl_Position = uViewProj * vec4(pos, 1.0);
}
)";

const char *kFragmentShader = R"(#version 330 core
in vec3 vNormal;
in vec3 vWorld;
in float vHeartMask;   // see kVertexShader — only meaningful when uXrayIsVessel==1
uniform vec4  uColor;
uniform vec3  uCameraPos;
uniform float uHeartCentreZ;   // world Z of the heart/aortic-root centre — anatomical anchor for mode 1's regional dampening
uniform float uLiveTime;       // mode 2: continuously increasing, for genuinely time-varying (flickering) grain — see paintGL for how this is derived
uniform vec3  uLightDir;
uniform vec3  uSheen;
uniform float uSpecular;
uniform float uShininess;
uniform float uAmbient;    // 0.12 default (vessel), 0.25 for 3D bone, 0.70 for fluoro bone
uniform float uDiffuse;    // 0.88 default (vessel), 0.75 for 3D bone, 0.30 for fluoro bone
uniform float uFalloff;
uniform int   uShaded;
uniform int   uHeadlight;
// Fluoro X-ray projection is two passes now, both using this same program:
//
//   uXrayMode == 1 ("accumulate"): drawn into an offscreen floating-point
//   buffer (see paintGL/ensureXrayFbo), one material at a time, with
//   glColorMask restricting each draw to its own channel (R=muscle/soft
//   tissue, G=bone — Skeletal.stl and Muscular.stl are this simulation's
//   only two body-attenuation materials; there is no separate "soft
//   tissue" mesh, Muscular.stl IS the soft-tissue/muscle envelope) and
//   GL_ONE/GL_ONE additive blending. A float target never clamps, so these
//   raw sums stay exact no matter how many overlapping fragments a ray
//   crosses — the thing that made every earlier attempt at this either
//   invisible or blown out to solid white. Vessel is NOT accumulated here;
//   it is drawn as a separate overlay on top of the finished projection,
//   the same way the catheter and guidewire already are (see paintGL,
//   right after Pass 2).
//
//   uXrayMode == 2 ("composite"): a single full-screen triangle samples
//   that buffer and resolves it against VesselView::XrayTuning — see the
//   X-Ray Calibration dialog (MainWindow) for the live-adjustable controls
//   these uniforms are set from every frame.
uniform int   uXrayMode;
uniform int   uXrayIsVessel;   // mode 1: 1 while accumulating the heart-region vessel wall, 0 for muscle/bone
uniform sampler2D uXrayTex;    // mode 2: the accumulated buffer
uniform vec2  uViewportPx;     // mode 2: viewport size, for gl_FragCoord -> UV

uniform vec3  uBackgroundColor;

uniform vec3  uBoneColor;
uniform float uBoneOpacity;
uniform float uBoneAmbient;
uniform float uBoneDiffuse;
uniform float uBoneSpecular;

uniform vec3  uMuscleColor;
uniform float uMuscleOpacity;
uniform float uMuscleAmbient;
uniform float uMuscleDiffuse;
uniform float uMuscleSpecular;

uniform float uGammaG;
uniform float uContrastG;
uniform float uBrightnessG;
uniform float uBoneIntensityMult;
uniform float uSoftTissueMult;
uniform float uBoneBrightness;
uniform float uMuscleBrightness;
// Contrast + embolic material — accumulated into the alpha channel (see
// paintGL Pass 1) rather than drawn as a separate lit/alpha-blended layer
// on top of the finished projection. uForeignDensityScale is set PER DRAW
// CALL in Pass 1 (different for contrast vs. each embolic agent, since
// their real radiopacity differs); uForeignOpacity is the shared Beer-
// Lambert response coefficient, set once in Pass 2.
uniform float uForeignDensityScale;
uniform float uForeignOpacity;
// Contrast pools bright/white (per explicit request: "look like a real
// radiopaque contrast bolus... bright/white initially") rather than dark,
// but ONLY in plain Fluoro mode with no embolic material active. Road Map
// keeps the original dark appearance (explicitly not to be changed), and so
// does any moment embolic material is present: contrast and embolic share
// this same accumulated channel (summed together in Pass 1, already
// combined by the time Pass 2 reads it here), so there is no way to colour
// one bright and the other dark at a pixel where both happen to overlap -
// keeping embolic's own, unmodified dark appearance correct takes priority
// over contrast's bright one in that shared situation.
uniform float uContrastBrightMode;
uniform float uForeignGrainAmount;
uniform float uForeignIsFluid;    // 1.0 for contrast (a real flowing liquid), 0.0 for embolic cast (a permanent solid deposit) - see the fluid-flow noise term below for why this distinction exists
// Fluoro vessel glitch — tiny, animated dropout speckle along the vessel
// wall only, mimicking small live-fluoroscopy detector artifacts. Zero for
// every other draw call sharing this shaded path (catheter, bone/muscle in
// the 3D view, host devices, etc.) - only ever set non-zero right around
// the Fluoro vessel wall's own draw call, and restored to 0 immediately
// after, so nothing else sharing this program is affected.
uniform float uVesselGlitchAmount;
uniform float uVesselGlitchTime;
out vec4 fragColor;

// Cheap deterministic hash, 0..1. Textures bone/muscle with grain instead of
// a perfectly flat color (mode 1: tissue/film-grain texture, keyed off world
// position so it's baked onto the anatomy and stable from any angle), and
// separately textures the final image with detector noise (mode 2: keyed off
// screen position instead, since that's a property of the imaging chain, not
// the anatomy).
float hash13(vec3 p) {
    // Multiply/fract based, not sin()-based: the previous version
    // (fract(sin(dot(p, magic)) * large_constant)) is a well-known GLSL
    // hash trick, but it breaks down once the argument to sin() gets
    // large — GPU sine implementations have much less precise range
    // reduction than a CPU's, so for large inputs the result stops being
    // effectively random and starts showing structured, repeating
    // patterns instead. gl_FragCoord.xy (mode 2's screen-space grain, used
    // for the whole background as well as the anatomy) reaches into the
    // thousands for any realistic viewport size, squarely in the range
    // where that showed up as visible dotted/patterned artifacts rather
    // than genuine noise. This formula never calls a transcendental
    // function at all, so it has no equivalent large-argument failure mode.
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

void main() {
    if (uXrayMode == 1) {
        // View-independent contribution — deliberately NOT a function of the
        // surface normal at all, for either material. Any dependence on
        // which way a triangle faces the camera is shading, full stop, and
        // that's what kept an earlier attempt (a per-fragment grazing-angle
        // boost for bone) reading as a subtly lit 3D model. The
        // cortical/medullary bright-rim/dark-centre effect that attempt was
        // for is now done in mode 2, as pure screen-space processing on the
        // already-accumulated density map — the same kind of edge
        // enhancement real radiographic image processing uses, with no
        // dependence on 3D surface geometry at all, and (unlike the
        // per-fragment version) not something that gets diluted by
        // averaging into a sum before this shader's own exp() saturates it.
        //
        // This stage only accumulates raw, grain-textured crossing counts —
        // every brightness/contrast decision (Ambient, Diffuse, Opacity,
        // intensity multipliers, colour, cortical/medullary contrast)
        // happens once in mode 2 below, reading VesselView::XrayTuning,
        // rather than being split across both passes.
        float grain = hash13(vWorld * 41.0);
        // Reduced from a 0.7 swing: in thin/sparse regions (a single bone
        // edge, few overlapping fragments) that much per-fragment variance
        // wasn't being averaged out by much else, and read as speckling on
        // top of the anatomy even with the hash function itself fixed.
        float v = 1.0 + 0.45 * (grain - 0.5);

        if (uXrayIsVessel == 0) {
            // Brightness reduction for the rib cage and mouth/jaw regions,
            // so they blend more naturally with the surrounding anatomy
            // instead of standing out. Identified by world Z position
            // relative to the heart/aortic-root centre — robust to camera
            // movement and C-arm rotation, unlike a screen-space
            // heuristic, since it's anchored to the actual loaded anatomy
            // rather than the current view. Honest caveat: the two Z-
            // offsets and band widths below are anatomically-reasoned
            // ESTIMATES (typical adult proportions relative to the aortic
            // root — ribs surrounding the heart at roughly its own level,
            // the mouth roughly a neck-plus-face height above it) rather
            // than values read back from labelled regions in this specific
            // model. If they don't land exactly on the rib cage/mouth once
            // rendered, these four numbers are what to adjust.
            //
            // Rib cage dampening raised substantially (0.90 -> 0.30 at the
            // band's centre) two rounds ago: the ribcage's own physics is
            // why it reads so much brighter than, say, the hand — a chest
            // X-ray ray crosses many overlapping ribs plus the spine and
            // sternum, while a hand/wrist ray crosses far fewer bone
            // layers, so the ribcage sits much further into the
            // Beer-Lambert curve's saturating region. That centre value is
            // unchanged here (it was calibrated directly against a hand-
            // bone brightness target and the request this round is
            // explicit about leaving that untouched) — only the band's
            // width is widened (220 -> 320), since a bright wash was still
            // visible in the chest-wall area surrounding the rib cage,
            // meaning the falloff wasn't reaching far enough out. Mouth
            // dampening (mild, unflagged this round) is unchanged.
            float dz = vWorld.z - uHeartCentreZ;
            float ribDist   = abs(dz - 0.0)   / 320.0;
            float mouthDist = abs(dz - 380.0) / 55.0;
            float ribDampen   = mix(0.30, 1.0, clamp(ribDist,   0.0, 1.0));
            float mouthDampen = mix(0.90, 1.0, clamp(mouthDist, 0.0, 1.0));
            v *= ribDampen * mouthDampen;
        }

        if (uXrayIsVessel == 1) {
            // vHeartMask (== aPulseAmp from the vertex shader) is already
            // exactly the right signal: large near the aortic root/heart,
            // smoothly fading with distance, and driven to zero above the
            // arch by superiorWeight — which is the same product that
            // already keeps the carotids from physically pulsing. Reusing
            // it here as a visibility gate means the carotids contribute
            // nothing to this channel with no separate region query needed.
            // The thresholds below are a reasoned estimate (aPulseAmp is in
            // model units, roughly a few mm near a ~15 mm aortic root)
            // rather than a value read back from the actual geometry.
            // smoothstep with a floor, not a plain linear ramp: a linear
            // ramp from zero lets even a small residual mask value (from
            // m_heart.peripheralFraction, present beyond the core but
            // still below the superior cutoff) contribute a faint signal —
            // scattered along distant vessel branches, that read as stray
            // dots rather than a clean heart-region highlight. Requiring
            // the mask to clearly clear a threshold before contributing
            // anything keeps this to the core region only.
            //
            // Loosened again (was smoothstep(0.5, 1.5, ...), then
            // smoothstep(0.3, 1.0, ...)): this gate only controls how much
            // of the heart region shows up at all, not the pulsation motion
            // itself (that's genuine vertex displacement, driven by the
            // same uPulsatile mechanism as the 3D View, and untouched here).
            // Widening it further is about making the visible region larger
            // so the actual motion has more area to read clearly across.
            v *= smoothstep(0.15, 0.6, vHeartMask);
        }

        if (uXrayIsVessel == 2) {
            // Contrast/embolic material — a real substance sitting inside the
            // vessel lumen, not anatomy, so neither the rib/mouth dampening
            // above (a skeletal-anatomy correction) nor the heart-region
            // pulsation mask applies. Grain-textured the same way bone and
            // muscle are (the same base grain computed above, before either
            // of those anatomy-specific corrections), for the same reason:
            // a perfectly flat density value reads as a painted graphic, not
            // an actual X-ray-attenuating substance.
            //
            // uForeignGrainAmount RESCALES that base grain specifically for
            // this material, set per draw call alongside uForeignDensityScale
            // in Pass 1 (paintGL) - this is what actually gives each embolic
            // agent its own visual CHARACTER rather than only a different
            // brightness: Onyx/SQUID (viscous, cohesive, deliberately
            // injected under control) use a low value for a smooth, dense,
            // uniform cast; Glue (thin, fast-polymerizing, the least
            // controllable of the four) uses a high value for a visibly
            // irregular, patchy deposit; PHIL sits between the two; plain
            // contrast keeps the grain essentially at its shared default -
            // a flowing liquid bolus, not a textured solid.
            float agentGrain = 1.0 + uForeignGrainAmount * 0.45 * (grain - 0.5);
            float fv = agentGrain * uForeignDensityScale;

            // Fluid flow animation - contrast only (uForeignIsFluid), never
            // the embolic cast, which is a genuinely permanent solid deposit
            // that should never appear to move once it has set.
            //
            // A low-frequency, high-amplitude directional flow band, not
            // random noise: a sine wave along Z (vessels broadly run along
            // the body's long axis), phase-shifted by uLiveTime so the
            // bands visibly travel through the vessel over real time. This
            // replaces an earlier, much weaker attempt (high-frequency hash
            // noise at +/-12%) that turned out invisible in practice for
            // two compounding reasons: high-frequency random noise tends to
            // average toward near-zero net variation once this fragment's
            // contribution is summed additively with every other
            // overlapping triangle in the accumulation pass, and whatever
            // survives that gets compressed further by the composite
            // pass's own smoothstep(0.05, 0.6, acc.a) downstream. A
            // low-frequency sine wave keeps its shape under additive
            // summation instead of cancelling out, and a much larger
            // amplitude (0.35x-1.4x, not +/-12%) is what actually reaches
            // the far side of that compression as a real, visible
            // difference rather than nothing.
            if (uForeignIsFluid > 0.5) {
                float flowPhase = vWorld.z * 0.06 - uLiveTime * 3.0;
                float flowBand = sin(flowPhase) * 0.5 + 0.5;   // 0..1
                fv *= mix(0.35, 1.4, flowBand);
            }

            fragColor = vec4(0.0, 0.0, 0.0, fv);
            return;
        }
        fragColor = vec4(v, v, v, 1.0);
        return;
    }

    if (uXrayMode == 2) {
        vec2 uv = gl_FragCoord.xy / max(uViewportPx, vec2(1.0));
        // r=muscle/soft tissue (accumulated in mode 1, from Muscular.stl),
        // g=bone (from Skeletal.stl), b=heart-region vessel wall (from the
        // vessel mesh, masked to the heart/great-vessel region only — see
        // uXrayIsVessel in mode 1 above), a=contrast/embolic material (from
        // whichever of those the shared ContrastInjectionModel/
        // EmbolizationModel instances currently have active — see
        // uXrayIsVessel==2 in mode 1 above).
        vec4 acc = texture(uXrayTex, uv);

        // Screen-space processing on the already-accumulated density map —
        // the same kind of edge enhancement real radiographic image
        // processing applies, not anything dependent on 3D surface
        // geometry. One pixel of the accumulation buffer, in UV terms.
        vec2 texel = 1.0 / max(uViewportPx, vec2(1.0));

        // Muscle is always smooth and soft, never showing sharp per-
        // triangle detail: a 9-tap box blur (orthogonal + diagonal, 1.5
        // texels out) on its own accumulated density — this is what keeps
        // a limb's actual mesh silhouette from reading as a crisp cutout,
        // fading it toward the background the way real soft tissue does.
        float muscleSmooth = acc.r
            + texture(uXrayTex, uv + vec2( 1.5,  0.0) * texel).r
            + texture(uXrayTex, uv + vec2(-1.5,  0.0) * texel).r
            + texture(uXrayTex, uv + vec2( 0.0,  1.5) * texel).r
            + texture(uXrayTex, uv + vec2( 0.0, -1.5) * texel).r
            + texture(uXrayTex, uv + vec2( 1.5,  1.5) * texel).r
            + texture(uXrayTex, uv + vec2(-1.5,  1.5) * texel).r
            + texture(uXrayTex, uv + vec2( 1.5, -1.5) * texel).r
            + texture(uXrayTex, uv + vec2(-1.5, -1.5) * texel).r;
        muscleSmooth *= (1.0 / 9.0);

        // Cortical/medullary contrast for bone: a local gradient magnitude
        // (how much the bone density map changes right around this pixel)
        // is high at a bone's silhouette edge and low in the uniform
        // interior of a thick cross-section — exactly the "bright rim,
        // darker centre" real bone shows, without needing any normal or
        // view-direction information at all. Divided by the local density
        // (plus a small floor) so it reads as a RELATIVE change rather
        // than being dominated by absolute density magnitude.
        float bG = acc.g;
        float bL = texture(uXrayTex, uv - vec2(texel.x, 0.0)).g;
        float bR = texture(uXrayTex, uv + vec2(texel.x, 0.0)).g;
        float bU = texture(uXrayTex, uv + vec2(0.0, texel.y)).g;
        float bD = texture(uXrayTex, uv - vec2(0.0, texel.y)).g;
        float boneGradient = abs(bG - bL) + abs(bG - bR) + abs(bG - bU) + abs(bG - bD);
        float boneEdge = clamp(boneGradient / (bG + 2.0), 0.0, 1.0);

        // Muscle brighter around the outer regions of bones: a small
        // secondary boost on top of the uniform baseline below (real
        // tissue immediately around bone — periosteum, perivascular
        // structures — is somewhat denser), sampled at a tight 2-texel
        // radius so it's a subtle glow right at a bone's own edge, not a
        // wash across an entire limb's cross-section.
        float boneNearby =
            ( texture(uXrayTex, uv + vec2( 2.0, 0.0) * texel).g
            + texture(uXrayTex, uv + vec2(-2.0, 0.0) * texel).g
            + texture(uXrayTex, uv + vec2(0.0,  2.0) * texel).g
            + texture(uXrayTex, uv + vec2(0.0, -2.0) * texel).g) * 0.25;

        // True Beer-Lambert now, replacing an earlier "shape * opacity"
        // ceiling model. That model treated Opacity as a hard cap on an
        // already-bounded [0,1] density shape, so a material could never
        // display brighter than its own Opacity value no matter how much
        // of it accumulated — with DSA's actual muscle Opacity (0.04)
        // ported in, that meant muscle could never exceed ~0.04 brightness
        // regardless of density, and read as completely invisible next to
        // a dark background. That was the wrong model: DSA's 0.04 is a
        // PER-LAYER alpha in a compositing stack with potentially dozens
        // of overlapping muscle layers, and repeated alpha-over
        // compositing of many thin layers accumulates to far more visible
        // than the per-layer value alone — which is exactly what
        // Beer-Lambert (attenuation accumulates, then exponentiates once)
        // reproduces correctly. Opacity is now the attenuation coefficient
        // per accumulated crossing; Ambient+Diffuse (summing to ~1.0 for
        // bone, ~1.05 for muscle in the DSA-ported defaults) scale the
        // overall attenuation strength, so both stay meaningfully
        // connected to the same DSA-derived numbers, just combined
        // through a formula that actually behaves like a DRR projection.
        //
        // uBoneIntensityMult/uBoneBrightness and uSoftTissueMult/
        // uMuscleBrightness are folded in here, per material, before the
        // two attenuations are ever combined — that's what keeps them
        // independent: uMuscleBrightness appears nowhere in boneAtten, and
        // vice versa.
        float boneAtten = acc.g * uBoneOpacity * (uBoneAmbient + uBoneDiffuse)
                         * uBoneIntensityMult * uBoneBrightness;
        // Cortical/medullary contrast: asymmetric on purpose, matching
        // "make the centre darker" (the stronger word) against "keep the
        // outer layer SLIGHTLY brighter" (the weaker one) — 0.70 at the
        // centre (a clearly darker interior) against 1.10 at the edge (a
        // gentle rim, not a hard outline).
        boneAtten *= mix(0.70, 1.10, boneEdge);
        // The specular glint term that used to sit here is removed: it was
        // built as uBoneSpecular * pow(fract(acc.g * 37.0), 3.0) — since
        // acc.g varies continuously and smoothly across a bone's surface,
        // multiplying it by 37 and taking the fractional part made the
        // value repeatedly cycle through 0-1 roughly every 1/37th unit of
        // density change, and the cube emphasized only the peak of each
        // cycle. That's a periodic function of a smoothly-varying signal —
        // exactly the mechanism that produces scattered, discrete-looking
        // bright spikes, reported as "dotted glitches". Removing it here
        // (uBoneSpecular/uMuscleSpecular remain declared and are still set
        // from C++ every frame, so nothing else needs to change) leaves
        // every other X-ray characteristic — the smooth muscular overlay,
        // darker bone centre, brighter outer cortical layer, tissue grain,
        // detector noise — completely untouched.

        // Muscle: uniform across the whole silhouette, not gated by bone
        // proximity — an earlier version concentrated Muscle Brightness's
        // whole effect into a halo near bone specifically, and comparing
        // the result against a real reference image made clear that was
        // wrong: real soft tissue gives the WHOLE torso and both arms their
        // visible volume, not just a thin band pressed against the bones.
        // Muscle Brightness scales this uniform baseline directly; the
        // Opacity/Ambient/Diffuse stay exactly DSA's ported values.
        float muscleAtten = muscleSmooth * uMuscleOpacity * (uMuscleAmbient + uMuscleDiffuse)
                          * uSoftTissueMult * uMuscleBrightness;
        // A soft glow/halo where muscle surrounds bone, on top of the
        // uniform baseline — raised from a 0.15 cap to 0.28, per request,
        // for a more visible "soft, subtle gray halo around the bones".
        // Still not the only source of visibility (the uniform baseline
        // above still gives the rest of the silhouette its own volume),
        // and it self-moderates in the rib cage specifically: boneNearby
        // is sampled from acc.g, which the rib-cage dampening above
        // already reduces there, so this boost is naturally smaller in
        // that region even at the higher cap, rather than compounding with
        // the wash the dampening is trying to remove.
        muscleAtten *= 1.0 + clamp(boneNearby * 0.02, 0.0, 0.28);

        // Live, time-varying grain — genuinely flickers frame to frame,
        // unlike the per-fragment tissue grain (mode 1, baked onto world
        // position, stable across frames) or the screen-space hash used
        // elsewhere in this file, neither of which change over time at
        // all. uLiveTime is a continuously increasing value derived from
        // the same shared cardiac clock already sampled every frame for
        // the pulse LUT (see paintGL) — reusing it avoids needing a
        // separate timer just for this. Multiplicative on muscleAtten
        // specifically, so this is naturally confined to wherever muscle
        // actually contributes: background and pure-bone/vessel regions
        // already have muscleAtten == 0 at this point, and zero times
        // anything stays zero — no separate masking needed. Kept small
        // (+/-3%) and at a coarse spatial frequency (half-resolution-ish),
        // matching "fine, smooth... very subtle" rather than a per-pixel
        // flicker that would read as a glitch.
        float liveGrain = hash13(vec3(gl_FragCoord.xy * 0.5, uLiveTime * 8.0)) - 0.5;
        muscleAtten *= 1.0 + liveGrain * 0.03;
        // Small additive component on top of the multiplicative one above,
        // gated by a fast-saturating "is there muscle here at all" signal
        // rather than scaling with muscleAtten's own magnitude — this is
        // what keeps grain subtly visible in thin/outer muscle regions
        // (a limb's edge, a thin cross-section) where muscleAtten itself
        // is small, and the purely multiplicative term above would fade
        // out along with it. Still zero wherever there's no muscle mesh
        // at all (muscleSmooth == 0), so background stays untouched.
        muscleAtten += liveGrain * 0.02 * clamp(muscleSmooth * 3.0, 0.0, 1.0);

        // Same removal as bone's specular term above, same reason: this
        // was uMuscleSpecular * pow(fract(acc.r * 37.0), 3.0), the same
        // periodic-in-a-smooth-signal construction that produced dotted
        // artifacts. Muscle Specular defaults to 0.0 already (DSA's own
        // value), so this had no visible effect in practice regardless.

        // Heart-region vessel: shown clearly, not hidden the way the rest
        // of the vessel tree still is — acc.b only has anything in it at
        // all where uXrayIsVessel's heart-mask gate (mode 1) let a fragment
        // through, so this term is naturally zero over the carotids and
        // the rest of the peripheral tree. A distinct reddish tint (not a
        // calibration-exposed parameter this round) is what makes it read
        // as clearly identifiable as the pulsating heart region rather
        // than another shade of the same gray as bone and muscle.
        //
        // Sharp shape signal, not a linear response to density: a linear
        // acc.b * coefficient (what this was) makes the vessel's edge a
        // soft brightness ramp, and the same real geometric pulsation this
        // shares with the 3D View (identical uPulsatile vertex displacement,
        // untouched here) then just nudges that ramp slightly — reading as
        // "brightness changing", not "a shape visibly expanding and
        // contracting". smoothstep turns acc.b into a fast-saturating,
        // near-binary "is the vessel here" signal instead: the moving
        // boundary now sweeps across a crisp edge rather than a gradual
        // one, which is what makes the same underlying motion actually
        // read as a shape growing and shrinking each beat.
        float heartShape = smoothstep(0.25, 0.9, acc.b);
        float heartAtten = heartShape * 0.90;
        const vec3 kHeartColor = vec3(0.85, 0.25, 0.22);

        // Contrast/embolic material — acc.a only has anything in it where
        // Pass 1's uXrayIsVessel==2 branch wrote a fragment (the contrast
        // column or the embolic cast), scaled per-substance by
        // uForeignDensityScale at accumulation time (a contrast bucket's own
        // concentration; an embolic agent's own real radiopacity). The same
        // smoothstep-then-scale shape as the heart-region channel, for the
        // same reason: a clean, well-defined edge rather than a soft haze,
        // which is what a real radiopaque bolus/cast actually looks like
        // under fluoroscopy - crisp against the background, not glowing.
        //
        // Deliberately kept OUT of totalAtten/materialColor below (unlike an
        // earlier version of this, which summed it in there alongside bone/
        // muscle/heart): that path mixed toward a near-white colour as MORE
        // contrast accumulated, meaning more contrast produced a BRIGHTER
        // pixel — backwards from how this console is meant to look.
        // CineViewStudio already assumes the opposite convention (its own
        // m_invertEnabled comment: "X-ray look defaults to inverted - dark
        // contrast on light tissue"), and it's what was being explicitly
        // asked for. foreignShape is applied as its own DARKENING pass much
        // further down instead, after all tissue tone-mapping is done, so
        // contrast/embolic material pools dark on top of the tissue image
        // regardless of what's underneath, rather than competing with bone/
        // muscle for whichever is brighter.
        // Two different saturation curves for the same accumulated density,
        // chosen by uContrastBrightMode - NOT the same shape stretched by a
        // brightness multiplier further down, because the actual problem is
        // where this curve saturates to 1.0, not how bright 1.0 itself ends
        // up being.
        //
        // Dark mode (Road Map, or whenever embolic material is active - see
        // uContrastBrightMode's own comment) keeps the original narrow
        // (0.05, 0.6) range completely unchanged: it was deliberately tuned
        // to saturate quickly, for a crisp, well-defined dark edge rather
        // than a soft haze - correct for that look, and left exactly as-is.
        //
        // Bright mode (uContrastBrightMode's plain-Fluoro state - the name
        // is legacy from when this mode's target colour was white; it now
        // targets a dark gray instead, but the widened range below is still
        // needed for the same reason regardless of target colour) uses a
        // much wider (0.05, 2.5) range instead - 2.5 matches the top of the
        // per-bucket density scale contrast's own concentration tiers
        // already produce (ContrastInjectionModel / buildContrastInto's
        // own uForeignDensityScale, 0.9x-2.5x). The narrow range was the
        // actual bug behind an earlier "purely whitish" report: any
        // reasonably concentrated contrast easily exceeds 0.6 in
        // accumulated density well before reaching the top of that real
        // 0.9-2.5 range, so nearly the entire column was saturating to
        // foreignShape=1.0 regardless of which concentration tier it
        // actually came from - reading as uniformly, maximally saturated
        // rather than showing "concentrated at the injection point,
        // gradually less concentrated as it travels". The wider range lets
        // that real concentration variation actually reach the screen as a
        // visible difference instead of being saturated away before it
        // gets there.
        float foreignShapeDark   = smoothstep(0.05, 0.6, acc.a);
        float foreignShapeBright = smoothstep(0.05, 2.5, acc.a);
        float foreignShape = mix(foreignShapeDark, foreignShapeBright, uContrastBrightMode);

        // Tissue-only sum - foreignShape/uForeignOpacity intentionally do
        // NOT appear here any more.
        float totalAtten = boneAtten + muscleAtten + heartAtten;
        float combinedShape = 1.0 - exp(-totalAtten);

        // Explicit early exit for empty screen space: rather than relying
        // on the downstream gamma/contrast/brightness math happening to
        // evaluate to exactly uBackgroundColor when there's no anatomy
        // here at all (which it should, but is an implicit property of
        // several chained formulas rather than something guaranteed by
        // construction), this exits directly with the background colour
        // and skips every remaining step entirely. The X-ray effect —
        // everything below this line — is now only ever computed for
        // pixels where Muscular.stl, Skeletal.stl, the heart-region vessel,
        // or contrast/embolic material actually contributed something;
        // background and empty screen space never reaches any of it.
        //
        // foreignShape has to be checked here TOO now that it no longer
        // feeds combinedShape directly: a pixel that is pure contrast with
        // no tissue behind it at all (perfectly possible - a vessel does
        // not always have muscle directly behind it in projection) would
        // otherwise incorrectly take the background exit and never render.
        if (combinedShape < 0.001 && foreignShape < 0.001) {
            fragColor = vec4(uBackgroundColor, 1.0);
            return;
        }

        // Colour is a proper weighted average of the THREE TISSUE
        // materials' contributions — the same rule the two-material mix()
        // used to apply (mix(muscleColor, boneColor, boneAtten/total) is
        // exactly this formula for two terms), generalized to three so the
        // heart region's reddish tint blends in correctly wherever it
        // overlaps bone or muscle instead of one material simply
        // overriding another. Contrast/embolic material is deliberately
        // absent from this blend now — see foreignShape's own comment above
        // for why it is applied as a separate darkening pass instead.
        //
        // Heart weight boosted (x4) in THIS blend specifically — not in
        // totalAtten/combinedShape above, which stays exactly as computed,
        // so overall brightness is unchanged. A rib sitting in front of or
        // behind the heart in projection is common (the heart is inside
        // the rib cage), and with an unboosted, purely proportional blend,
        // bone's typically larger attenuation at the same pixel would
        // dilute the reddish tint toward bone's own colour there — reading
        // as the pulsation being hidden behind the ribs rather than
        // visible through them. Boosting only the colour weight (not the
        // brightness) is what keeps the heart-region tint identifiable
        // wherever it overlaps bone, without changing how bright that
        // pixel is overall.
        float kHeartColorWeight = heartAtten * 4.0;
        float colorTotal = boneAtten + muscleAtten + kHeartColorWeight;
        vec3 materialColor = (boneAtten * uBoneColor + muscleAtten * uMuscleColor
                             + kHeartColorWeight * kHeartColor) / max(colorTotal, 1e-4);
        vec3 rgb = mix(uBackgroundColor, materialColor, clamp(combinedShape, 0.0, 1.0));

        // Global post-processing — gamma, then contrast (pivoted at 0.5 so
        // it cannot shift the black point), then a final brightness
        // multiply — the same order and the same three controls a photo
        // editor or a real fluoroscopy console applies to a detector signal.
        rgb = pow(clamp(rgb, 0.0, 1.0), vec3(uGammaG));
        rgb = (rgb - 0.5) * uContrastG + 0.5;
        rgb = rgb * uBrightnessG;

        // Contrast/embolic darkening — applied AFTER tissue tone-mapping, so
        // it reads as a consistently dark, radiopaque signal regardless of
        // how bright/contrasty the tissue calibration happens to be set,
        // rather than being just another additive brightness competing with
        // bone for the same pixel. uForeignOpacity controls how strongly it
        // pools toward the target colour - near-saturated at 1.0, only a
        // subtle shift at low values - the same single shared coefficient
        // Pass 2 already sets once per frame for this channel.
        //
        // Target colour itself now depends on uContrastBrightMode (see its
        // own uniform comment): a dark grayish tone for plain Fluoro mode
        // (per explicit request, changed from an earlier near-white
        // target), or the original near-black otherwise (Road Map, or
        // whenever embolic material is active - the two genuinely cannot
        // be coloured independently once summed into this one shared
        // accumulated channel).
        vec3 kContrastDark = vec3(0.03, 0.03, 0.035);
        vec3 kContrastPlainFluoro = vec3(0.30, 0.30, 0.28);
        vec3 kContrastTarget = mix(kContrastDark, kContrastPlainFluoro, uContrastBrightMode);
        rgb = mix(rgb, kContrastTarget, clamp(foreignShape * uForeignOpacity, 0.0, 1.0));

        // The screen-space detector grain that used to sit here is removed
        // entirely. Two prior rounds tried to fix it in place — replacing
        // the hash function (which had a real, well-documented large-
        // argument precision problem), then gating it by combinedShape so
        // it faded to zero away from anatomy — and grain was still visible
        // in the background afterward. Rather than a third incremental
        // adjustment, removing the term outright is the one change that
        // GUARANTEES the background can no longer carry any of it: with no
        // per-pixel randomization applied at all, background pixels equal
        // exactly uBackgroundColor, full stop, regardless of whatever
        // subtle issue might still remain in a gating calculation. The
        // per-fragment tissue grain (mode 1, hash13(vWorld * 41.0)) is
        // unrelated to this and untouched — that's what still gives bone
        // and muscle their texture; it never reaches empty background at
        // all, since mode 1 only runs where a mesh fragment is actually
        // rasterized.
        // Subtle detector falloff — a real image intensifier's output
        // brightness genuinely falls off gently toward the edge of its
        // round field of view; this is a soft radial darkening, not a hard
        // circular cutout or a decorative vignette. Deliberately mild (at
        // most ~12% darkening at the corners, and only beginning well past
        // the centre) so it reads as authentic detector physics rather
        // than a stylised overlay - a real fluoro frame's falloff is
        // something you'd only consciously notice if you went looking for
        // it, not an obvious dark ring.
        vec2 vignetteUv = gl_FragCoord.xy / max(uViewportPx, vec2(1.0));
        float vignetteDist = length(vignetteUv - vec2(0.5)) * 1.35;
        float vignette = 1.0 - 0.12 * smoothstep(0.55, 1.05, vignetteDist);
        rgb *= vignette;

        rgb = clamp(rgb, 0.0, 1.0);
        fragColor = vec4(rgb, 1.0);
        return;
    }

    if (uShaded == 0) { fragColor = uColor; return; }

    vec3 n = normalize(vNormal);
    vec3 toEye = uCameraPos - vWorld;
    float dist = length(toEye);
    vec3 v = toEye / max(dist, 1e-8);

    // Flip the normal toward the eye rather than taking abs(): tissue is
    // rendered translucent and we see back faces constantly, and a flipped
    // normal gives a correct specular lobe where abs() gives a doubled one.
    if (dot(n, v) < 0.0) n = -n;

    // Two-light model — key from upper-front, fill from lower-back (45%)
    // Ensures all faces of tube meshes receive some diffuse contribution.
    vec3  Lkey  = normalize(vec3(0.2,  0.4,  0.6));
    vec3  Lfill = normalize(vec3(-0.2, -0.4, -0.3));
    float diffKey  = max(dot(n, Lkey),  0.0);
    float diffFill = max(dot(n, Lfill), 0.0) * 0.45;
    float diff = (uHeadlight == 1) ? max(dot(n, v), 0.0)
                                   : (diffKey + diffFill);
    vec3  h    = normalize(Lkey + v);
    float spec = pow(max(dot(n, h), 0.0), uShininess) * uSpecular;
    float fres = pow(1.0 - clamp(dot(n, v), 0.0, 1.0), 3.0);

    float atten = 1.0;
    if (uHeadlight == 1) atten = 1.0 / (1.0 + pow(dist / max(uFalloff, 1e-6), 2.0));

    vec3 rgb = uColor.rgb * (uAmbient + uDiffuse * diff) * atten
             + uSheen * spec * atten
             + uSheen * fres * 0.09;
    // Soft-knee roll-off: at low/mid brightness this is nearly the identity
    // (rgb=0.5 -> ~0.46), so anatomy that was already reading fine is
    // essentially unchanged. It only bites where diffuse+specular+Fresnel
    // were stacking up near or past 1.0 and hard-clipping to flat white
    // with no gradient left in it — the "overexposed" look — easing that
    // down to a soft, still-bright peak instead.
    rgb = rgb / (1.0 + rgb * 0.18);
    float a = clamp(uColor.a + fres * uColor.a * 1.2, 0.0, 1.0);
    // Sparse, animated dropout speckle - zero-cost (a single extra hash
    // call) for every other draw call sharing this path, since
    // uVesselGlitchAmount defaults to 0 and the branch below is then a
    // no-op. Coarse hash picks small eligible patches (~a few mm across,
    // matching "tiny irregular dotted/fragmented POINTS", not per-pixel
    // grain), re-salted by uVesselGlitchTime so which patches are eligible
    // changes over time; fine hash then decides the actual per-fragment
    // dropout within an eligible patch, giving each glitch point an
    // irregular, fragmented edge rather than a hard-edged dot. Alpha
    // reduction only, never brightened and never fully to zero - keeps the
    // vessel shape and color intact underneath, satisfying "must not
    // obscure the vessel".
    if (uVesselGlitchAmount > 0.0) {
        float patchHash = hash13(floor(vWorld * 180.0) + uVesselGlitchTime);
        if (patchHash < uVesselGlitchAmount) {
            float fineHash = hash13(vWorld * 900.0 + uVesselGlitchTime * 1.7);
            a *= mix(1.0, 0.35, smoothstep(0.15, 0.85, fineHash));
        }
    }
    fragColor = vec4(rgb, a);
}
)";

QVector3D toQt(const cn::Vec3 &v) { return QVector3D(v.x, v.y, v.z); }

/// Chain indices that split the device into its three materials.
constexpr int kTipEnd = 3;    // nodes 0..3   soft tapered tip
constexpr int kBandEnd = 4;   // nodes 3..4   radiopaque marker band

} // namespace

void VesselView::Batch::destroy() {
    vao.destroy();
    vbo.destroy();
    ibo.destroy();
}

VesselView::VesselView(QWidget *parent) : QOpenGLWidget(parent) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSamples(4);
    fmt.setDepthBufferSize(24);
    setFormat(fmt);
    setMinimumSize(520, 420);
    setFocusPolicy(Qt::StrongFocus);
    m_fluoroClock.start();   // backs the Fluoroscopy HUD's TIME read-out

    // The device advances at a physical speed; the camera is eased toward it
    // separately so that neither the zoom nor the recentring snaps.
    m_camEase.setInterval(16);
    connect(&m_camEase, &QTimer::timeout, this, [this] {
        bool busy = false;
        const float da = m_camArcTarget - m_camArc;
        if (std::fabs(da) > 1e-7f) { m_camArc += da * 0.25f; busy = true; }
        else m_camArc = m_camArcTarget;

        const QVector3D dp = m_followTargetGoal - m_followTarget;
        if (dp.lengthSquared() > 1e-16f) { m_followTarget += dp * 0.22f; busy = true; }
        else m_followTarget = m_followTargetGoal;

        const float dd = m_followDistanceGoal - m_followDistance;
        if (std::fabs(dd) > 1e-9f) { m_followDistance += dd * 0.15f; busy = true; }
        else m_followDistance = m_followDistanceGoal;

        if (!busy) m_camEase.stop();
        update();
    });
}

VesselView::~VesselView() {
    makeCurrent();
    m_meshBatch.destroy();
    m_centerlineBatch.destroy();
    // Body layers, sheath parts and the balloon were all leaking GL objects
    // here — only the mesh, centerline and three device spans were released.
    m_muscularBatch.destroy();
    m_skeletalBatch.destroy();
    m_xrayQuadBatch.destroy();   // Fluoro X-ray compositing-pass triangle
    m_xrayFbo.reset();           // Fluoro X-ray offscreen accumulation buffer
    m_sheathCannulaBatch.destroy();
    m_sheathHubBatch.destroy();
    m_sheathValveBatch.destroy();
    m_sheathArmBatch.destroy();
    if (m_deviceBatches) {
        m_deviceBatches->tip.destroy();
        m_deviceBatches->band.destroy();
        m_deviceBatches->shaft.destroy();
        m_deviceBatches->balloon.destroy();
    }
    for (auto &hb : m_hostBatches)
        if (hb) { hb->tip.destroy(); hb->band.destroy(); hb->shaft.destroy(); hb->balloon.destroy(); }
    m_program.reset();
    doneCurrent();
}

// ------------------------------------------------------------------ inputs

void VesselView::setMesh(const cn::StlMesh *mesh) {
    m_mesh = mesh;
    m_meshDirty = true;
    if (mesh) m_modelScale = std::max(1e-6f, mesh->bounds().diagonal());
    resetCamera();
    update();
}

void VesselView::setGraph(const cn::CenterlineGraph *graph) {
    m_graph = graph;
    m_centerlineDirty = true;
    update();
}

void VesselView::setDevice(const cn::CatheterModel *device) {
    m_device = device;
    if (!m_deviceBatches) m_deviceBatches = std::make_unique<DeviceBatches>();
    m_deviceDirty = true;
    update();
}

void VesselView::setEmbolization(const cn::EmbolizationModel *emb) {
    m_emboli = emb;
    m_emboliDirty = true;
    update();
}

void VesselView::refreshEmbolization() {
    m_emboliDirty = true;
    update();
}

void VesselView::setContrastInjection(const cn::ContrastInjectionModel *contrast) {
    m_contrast = contrast;
    m_contrastDirty = true;
    update();
}

void VesselView::refreshContrastInjection() {
    m_contrastDirty = true;
    update();
}

void VesselView::setCameraTrack(const cn::CenterlinePath *path, float arc) {
    m_camPath = path;
    // Always track the arc: the endoluminal eye is *inside* the device and has
    // to follow it regardless. m_cameraFollows only decides whether the
    // reference camera re-targets, and gating this on it froze the endoluminal
    // view whenever following was switched off.
    m_camArcTarget = arc;
    startCameraEase();
    update();
}

void VesselView::setCameraFollows(bool v) {
    m_cameraFollows = v;
    if (v) startCameraEase();
    update();
}

void VesselView::startCameraEase() {
    if (!m_camEase.isActive()) m_camEase.start();
}

void VesselView::setMode(Mode m) {
    m_mode = m;
    // Entering follow mode from elsewhere: snap once rather than sweeping the
    // camera across the model.
    if (m == Mode::Reference && m_device) {
        updateFollowFrame();
        m_followTarget = m_followTargetGoal;
        m_followDistance = m_followDistanceGoal;
    }
    emit cameraChanged();
    update();
}

void VesselView::setPalette(Palette p) {
    m_palette = p;
    update();
}

void VesselView::refreshDevice() {
    m_deviceDirty = true;
    update();
}

void VesselView::setFrontalLock(bool v) {
    m_frontalLock = v;
    if (v) {
        m_azimuth = 180.f;
        m_elevation = 0.f;
        emit cameraChanged();
    }
    update();
}

void VesselView::showWholeVessel() {
    if (!m_mesh) return;
    const cn::Box3 b = m_mesh->bounds();
    if (!b.valid()) return;

    m_cameraFollows = false;
    // +Z shifts target up → vessel appears lower in viewport
    const float downShift = b.size().z * 0.22f;
    m_target = toQt(b.center()) + QVector3D(0.f, 0.f, downShift);
    m_azimuth = 180.f;
    m_elevation = 0.f;

    // Frame the bounding box in the frontal projection: X across the screen,
    // Z up it. Fitting the diagonal instead would leave the tree floating in
    // the middle of a mostly empty viewport, because this model is a thin
    // sheet - its Y extent is a twentieth of its height.
    //
    // One formula for both palettes, using the FOV each actually renders with
    // and the same margin. Arterial used to multiply the fit by 2.00 and
    // Angiographic by 1.00, so the anatomy was half the size in one pane and
    // edge-to-edge in the other — the same model at two different scales
    // depending on which button was pressed.
    const float halfV  = std::max(b.size().z, 1e-6f) * 0.5f;
    const float halfH  = std::max(b.size().x, 1e-6f) * 0.5f;
    const float aspect = height() > 0 ? float(width()) / float(height()) : 1.f;
    const float tanHalf = std::tan(qDegreesToRadians(fovDegrees() * 0.5f));

    m_distance = kAnatomyFrameMargin *
                 std::max(halfV / tanHalf, halfH / (tanHalf * std::max(aspect, 0.2f)));
    update();
}

void VesselView::focusRibCage() {
    // Same guard showWholeVessel() uses, and for the same reason: calling
    // this before any vessel data is loaded has nothing valid to focus on.
    // This is the actual, confirmed cause of a prior regression — an
    // earlier call site invoked this during initial widget construction,
    // before mesh/centerline data was loaded, so m_heartCentre was still
    // at its default (0,0,0) and the resulting camera state was invalid
    // across every linked view at once.
    if (!m_mesh) return;

    // Same distance formula as showWholeVessel() (same FOV, same style of
    // margin), applied to an assumed rib-cage half-size instead of the
    // whole mesh's bounding box — this is what gives a closer, more
    // zoomed-in framing rather than the whole-body view. These half-sizes
    // are reasoned estimates for an adult thorax (consistent with the
    // rib-cage Z-band already used for Fluoro's rib dampening,
    // kFragmentShader), not read back from a labelled rib-cage region in
    // this specific model — kHeadFrameMargin's own comment ("tighter than
    // kAnatomyFrameMargin... meant to be close") is the established
    // precedent this follows for a margin tighter than the whole-vessel
    // default.
    static constexpr float kRibCageFrameMargin = 1.10f;
    const float halfV = 0.20f;   // ~400 mm vertical extent (sternum to lower ribs)
    const float halfH = 0.17f;   // ~340 mm horizontal extent (rib cage width)
    const float aspect = height() > 0 ? float(width()) / float(height()) : 1.f;
    // Clamped well away from zero: a degenerate/near-zero FOV would
    // otherwise divide out to an enormous or infinite distance, the other
    // half of what made the prior regression a total black screen rather
    // than just a mis-centred view.
    const float tanHalf = std::max(std::tan(qDegreesToRadians(fovDegrees() * 0.5f)), 1e-3f);
    const float dist = kRibCageFrameMargin *
                       std::max(halfV / tanHalf, halfH / (tanHalf * std::max(aspect, 0.2f)));

    // Ribs surround the heart at roughly its own level — the same anchor
    // point Fluoro's rib-cage dampening already uses, so no separate
    // anatomical query is needed.
    setCamera(180.f, 0.f, dist, m_heartCentre);

    // setCamera() itself deliberately never emits (syncCameraFrom() relies on
    // that to avoid a ping-pong loop between linked views) - every OTHER
    // direct camera-framing function (showWholeVessel, showFluoroDefault,
    // showHeadSagittal...) emits explicitly right after, and this one had
    // been missing it: calling focusRibCage() directly (rather than via
    // resetCamera(), which does emit) silently never reached the other
    // linked view at all.
    emit cameraChanged();
}

void VesselView::setCamera(float az, float el, float dist, const QVector3D &tgt) {
    m_cameraFollows = false;
    m_azimuth   = az;
    m_elevation = el;
    m_distance  = dist;
    m_target    = tgt;
    update();
}

void VesselView::syncCameraFrom(const VesselView &other) {
    // Effective target/distance, not the raw static ones. While camera-
    // follow is active, other's own m_target/m_distance sit stale and
    // completely unused for rendering (paintGL() itself branches on
    // m_cameraFollows to pick between the static and follow pair - see
    // that branch's own comment) - syncing the static pair instead of
    // whatever is actually on screen was pulling in wherever the static
    // camera was left before follow-mode took over (typically the
    // head-sagittal startup default), not the tracked device position
    // other is actually showing.
    const bool otherFollowing = other.m_cameraFollows && other.m_device;
    const QVector3D otherTarget   = otherFollowing ? other.m_followTarget   : other.m_target;
    const float      otherDistance = otherFollowing ? other.m_followDistance : other.m_distance;

    // Distance is scaled by the ratio of each view's own FOV, not copied
    // verbatim. 3D and Fluoro deliberately use different FOVs (40°/30° -
    // see fovDegrees()'s own comment: Fluoro's narrower cone approximates a
    // real X-ray detector's field of view) — the SAME raw distance number
    // at two different FOVs frames a different apparent amount of anatomy,
    // so copying it unscaled left the two views with byte-identical
    // azimuth/elevation/distance/target yet a visibly different on-screen
    // zoom, which is exactly what "the exact same movement, the exact same
    // zoom" rules out. Keeping (distance * tan(fov/2)) constant across the
    // sync is what keeps the apparent half-height of the frame the same
    // regardless of which of the two FOVs is doing the looking.
    const float tanHalfMine  = std::tan(qDegreesToRadians(fovDegrees() * 0.5f));
    const float tanHalfTheir = std::tan(qDegreesToRadians(other.fovDegrees() * 0.5f));
    const float scaledDistance = otherDistance * (tanHalfTheir / std::max(tanHalfMine, 1e-4f));
    setCamera(other.azimuth(), other.elevation(), scaledDistance, otherTarget);

    // setCamera() above unconditionally turns follow-mode off (see its own
    // comment) - that is correct for an actual manual camera move, but
    // wrong here: if the source view is actively tracking the device, the
    // destination should be too, not permanently knocked out of follow
    // mode by the very sync that is supposed to keep them showing the same
    // thing. Re-enabling it afterward (rather than skipping setCamera()
    // entirely when following) still gives this view a sane static
    // fallback position - wherever the device currently is - for the
    // instant before its own next follow-frame update, rather than
    // whatever stale static state it happened to have from before.
    if (otherFollowing) setCameraFollows(true);
}

// ─────────────────────────────────────────────────────────────────────────────
// showHeadSagittal — close lateral view of the head and cerebral vessels.
//
// Azimuth 90 puts the eye on +X looking back along -X: a sagittal projection,
// with the screen's horizontal axis running anterior-posterior (Y) and its
// vertical axis running cranio-caudal (Z). The other framings all sit at
// azimuth 180, which is AP and superimposes the left and right circulations.
//
// The crop threshold is measured rather than guessed: above 0.82 of the Z span
// the tree narrows from +/-288 mm to +/-103 mm in X and still holds two thirds
// of the mesh's vertices, so that is where the cerebral region begins.
//
// Framing uses Y for the horizontal half-extent, not X — looking along X, the
// model's X thickness is depth, and sizing the camera off it would push the
// view much too far back.
// ─────────────────────────────────────────────────────────────────────────────
void VesselView::showHeadSagittal() {
    if (!m_mesh) { showWholeVessel(); return; }

    const cn::Box3 b = m_mesh->bounds();
    const float zTop  = b.hi.z;
    const float zCrop = b.lo.z + (b.hi.z - b.lo.z) * kHeadCropFraction;

    // Measure the head region's own Y extent rather than using the whole
    // model's: the cerebral tree is not centred on the trunk's midline.
    float yLo =  std::numeric_limits<float>::max();
    float yHi = -std::numeric_limits<float>::max();
    for (const cn::Vec3 &v : m_mesh->vertices()) {
        if (v.z < zCrop) continue;
        yLo = std::min(yLo, v.y);
        yHi = std::max(yHi, v.y);
    }
    if (yLo > yHi) { yLo = b.lo.y; yHi = b.hi.y; }

    const float halfZ = (zTop - zCrop) * 0.5f;
    const float halfY = (yHi - yLo) * 0.5f;

    m_cameraFollows = false;
    m_target = QVector3D(toQt(b.center()).x(),
                         (yLo + yHi) * 0.5f,
                         (zCrop + zTop) * 0.5f);
    m_azimuth   = 90.f;    // eye on +X, looking along -X: patient's left lateral
    m_elevation = 0.f;

    const float aspect  = height() > 0 ? float(width()) / float(height()) : 1.f;
    const float tanHalf = std::tan(qDegreesToRadians(fovDegrees() * 0.5f));
    m_distance = kHeadFrameMargin *
                 std::max(halfZ / tanHalf,
                          halfY / (tanHalf * std::max(aspect, 0.2f)));

    emit cameraChanged();
    update();
}

void VesselView::showFluoroDefault() {
    // Frame the head/brain region for the fluoro view — matches the old project's
    // frameOnFace() approach: crop to the top 30% of the vessel Z-extent (brain end),
    // set target to that sub-region's centre, compute distance from its diagonal.
    //
    // The 8° FOV (set in paintGL for Angiographic palette) simulates parallel
    // projection. At 8° FOV, distance = subRegionHalf / tan(4°) gives correct framing.
    if (!m_mesh) { showWholeVessel(); return; }

    const cn::Box3 b = m_mesh->bounds();
    // Top 30% of Z-extent = brain/head region (vessel goes groin→brain in +Z)
    const float zTop    = b.hi.z;
    const float zCrop   = b.lo.z + (b.hi.z - b.lo.z) * 0.70f;  // bottom of the top-30% region
    const float croppedCentreZ = (zCrop + zTop) * 0.5f;
    const float croppedHalfZ   = (zTop - zCrop) * 0.5f;
    const float croppedHalfX   = b.size().x * 0.5f;

    // Target = centre of cropped head region, slightly lower for better framing
    m_cameraFollows = false;
    // Shift target down so more of the head region fills the upper viewport area
    m_target = QVector3D(toQt(b.center()).x(), toQt(b.center()).y(),
                         croppedCentreZ - croppedHalfZ * 0.5f);
    m_azimuth   = 180.f;   // AP (front)
    m_elevation = 0.f;

    // Same rule as showWholeVessel, applied to the cropped head region: the FOV
    // this palette actually renders with, and the same margin. The hard-coded
    // tan(10 deg) did not match the 30 deg FOV in use, and the extra 0.62 on top
    // was compensating for that — together they put the head at a different
    // scale from the anatomy in every other pane.
    const float aspect = height() > 0 ? float(width()) / float(height()) : 1.f;
    const float tanHalf = std::tan(qDegreesToRadians(fovDegrees() * 0.5f));
    m_distance = kAnatomyFrameMargin *
                 std::max(croppedHalfZ / tanHalf,
                          croppedHalfX / (tanHalf * std::max(aspect, 0.2f)));

    emit cameraChanged();
    update();
}

void VesselView::focusOn(const QVector3D &position, float lumenRadius) {
    // Opening on the whole tree after a puncture is chosen is useless: the
    // access vessel is a couple of millimetres across in a model a metre long.
    m_cameraFollows = true;   // from here on the camera rides the device
    m_target = position;
    m_followTarget = position;
    m_followTargetGoal = position;
    const float d = std::clamp(std::max(lumenRadius * 18.f, m_modelScale * 0.008f),
                               m_modelScale * 0.006f, m_modelScale * 0.45f);
    m_distance = d;
    m_followDistance = d;
    m_followDistanceGoal = d;
    update();
}

void VesselView::resetCamera() {
    m_followZoom = 1.f;
    m_endoFov    = 72.f;
    // The C-arm angles are deliberately NOT cleared here. They mirror the rig,
    // which is shared by every view; zeroing them locally would put this view
    // at a different orientation from the others until the next rotation.
    // Resetting the rig is a separate action (R -> CarmController::resetAll),
    // and that pushes 0/0 to every view at once.
    //
    // Previously branched on palette (Fluoro -> head region, everything else
    // -> whole vessel) - meaning "Reset Camera" put the two panes at two
    // different cameras every time, the opposite of every view sharing one
    // camera state. focusRibCage() now emits cameraChanged() itself, so
    // calling it here already propagates to every linked view.
    focusRibCage();
}

// ------------------------------------------------------------------ camera

void VesselView::updateFollowFrame() {
    if (!m_device) return;
    const auto &st = m_device->state();

    // Only the *target* follows the device. Orientation stays on the overview's
    // orbit angles, so the operator keeps the vantage they set up and the scene
    // never rotates itself out from under them.
    m_followTargetGoal = toQt(st.tipPosition);

    // Auto-zoom to the vessel, not to the model: framing on the local lumen is
    // what keeps a 2 mm distal branch and a 20 mm trunk both legible.
    const float lumen = std::max(st.lumenRadiusAtTip, m_device->params().deviceRadius);
    const float want = m_followZoom * std::max(lumen * 16.f,
                                               m_device->params().deviceRadius * 34.f);
    m_followDistanceGoal = std::clamp(want, m_modelScale * 0.006f, m_modelScale * 0.45f);
    if (m_followDistance <= 0.f) m_followDistance = m_followDistanceGoal;
    startCameraEase();
}

void VesselView::updateEndoluminalFrame() {
    if (!m_camPath || m_camPath->empty()) return;

    const float total = m_camPath->totalLength();
    const float standoff = m_modelScale * m_endoStandoffFrac;
    const float lookahead = m_modelScale * m_endoLookaheadFrac;

    const float sEye = std::clamp(m_camArc - standoff, 0.f, total);
    const float sLook = std::clamp(sEye + lookahead, 0.f, total);

    m_endoEye = toQt(m_camPath->pointAt(sEye));
    QVector3D fwd = toQt(m_camPath->pointAt(sLook)) - m_endoEye;
    if (fwd.lengthSquared() < 1e-14f) fwd = toQt(m_camPath->tangentAt(sEye));
    if (fwd.lengthSquared() < 1e-14f) fwd = QVector3D(0, 0, 1);
    fwd.normalize();

    QVector3D up = m_endoUp - fwd * QVector3D::dotProduct(m_endoUp, fwd);
    if (up.lengthSquared() < 1e-10f) {
        const QVector3D seed = std::fabs(fwd.z()) < 0.9f ? QVector3D(0, 0, 1) : QVector3D(1, 0, 0);
        up = seed - fwd * QVector3D::dotProduct(seed, fwd);
    }
    m_endoUp = up.normalized();
    m_endoForward = fwd;
}

void VesselView::syncCameraToDevice() {
    if (m_mode == Mode::Endoluminal) updateEndoluminalFrame();
    else if (m_mode == Mode::Reference) updateFollowFrame();
}

void VesselView::showEvent(QShowEvent *e) {
    QOpenGLWidget::showEvent(e);

    // The actual fix for "switch to Fluoro and the camera is briefly wrong"
    // (and the same for switching to any other pane while in follow mode):
    // updateFollowFrame()/updateEndoluminalFrame() are only ever called from
    // paintGL(), and Qt never calls paintGL() for a QStackedWidget page that
    // isn't the current one - so m_followTarget/m_followDistance (or the
    // endoluminal eye/forward) sit completely stale for however long this
    // view was hidden, however far the catheter actually moved in that
    // time. The very first frame after being shown would otherwise
    // recompute a fresh goal from the catheter's now-current position and
    // visibly EASE toward it from the stale one over the next several
    // frames, rather than already being there - which is exactly what read
    // as "changes to a different view" on switching panes.
    //
    // Recomputing the goal here and then snapping the eased value straight
    // to it (bypassing the gradual interpolation entirely, just for this
    // one moment) means the very first frame rendered after becoming
    // visible again already shows the correct camera, with no visible
    // catch-up at all.
    if (m_mode == Mode::Reference && m_cameraFollows && m_device) {
        updateFollowFrame();
        m_followTarget   = m_followTargetGoal;
        m_followDistance = m_followDistanceGoal;
    } else if (m_mode == Mode::Endoluminal) {
        updateEndoluminalFrame();
    }
}

QVector3D VesselView::cameraPosition() const {
    if (m_mode == Mode::Endoluminal) return m_endoEye;

    // One orbit, two targets. Following is a property of the reference view,
    // not a separate camera: the operator keeps the vantage they set up either
    // way, and only what the camera is pointed at changes.
    // C-arm angles are added here rather than baked into m_azimuth/m_elevation,
    // so the rig orientation is identical in every view and the operator's own
    // orbit is left untouched underneath it.
    // LAO > 0 swings the detector left, which reads as the camera rotating
    // right, hence + on azimuth.
    const float az = qDegreesToRadians(m_azimuth + m_carmLAO);
    const float el = qDegreesToRadians(std::clamp(m_elevation + m_carmCRAN, -89.f, 89.f));
    const bool following = m_cameraFollows && m_device;
    const QVector3D at = following ? m_followTarget : m_target;
    const float d = following ? m_followDistance : m_distance;
    // Z-up spherical: azimuth sweeps the XY plane, elevation lifts toward +Z.
    return at + QVector3D(d * std::cos(el) * std::sin(az),
                          d * std::cos(el) * std::cos(az),
                          d * std::sin(el));
}

QVector3D VesselView::cameraForward() const {
    if (m_mode == Mode::Endoluminal) return m_endoForward;
    const QVector3D at = (m_cameraFollows && m_device) ? m_followTarget : m_target;
    const QVector3D d = at - cameraPosition();
    return d.lengthSquared() > 1e-16f ? d.normalized() : QVector3D(0, 0, 1);
}

QVector3D VesselView::cameraUp() const {
    if (m_mode == Mode::Endoluminal) return m_endoUp;
    return QVector3D(0, 0, 1);   // the vessel stands up
}

QVector3D VesselView::screenRight() const {
    return QVector3D::crossProduct(cameraForward(), cameraUp()).normalized();
}

QVector3D VesselView::screenUp() const {
    return QVector3D::crossProduct(screenRight(), cameraForward()).normalized();
}

float VesselView::fovDegrees() const {
    // Angiographic uses a moderate FOV — narrow enough to reduce perspective
    // distortion the way a real X-ray does, but not so narrow the scene looks
    // flat from side angles. 40 suits the anatomical 3D view.
    return (m_palette == Palette::Angiographic) ? 30.f : 40.f;
}

QMatrix4x4 VesselView::viewProjection() const {
    const float aspect = height() > 0 ? float(width()) / float(height()) : 1.f;
    QMatrix4x4 proj, view;
    if (m_mode == Mode::Endoluminal) {
        proj.perspective(m_endoFov, aspect, m_modelScale * 2e-4f, m_modelScale * 1.2f);
        view.lookAt(m_endoEye, m_endoEye + m_endoForward, m_endoUp);
    } else {
        const bool following = m_cameraFollows && m_device;
        const QVector3D at = following ? m_followTarget : m_target;
        const float d = std::max(following ? m_followDistance : m_distance, m_modelScale * 1e-4f);
        // Angiographic uses a moderate FOV — narrow enough to reduce perspective
        // distortion (like a real X-ray), but not so narrow that the scene looks
        // completely flat from side angles.
        proj.perspective(fovDegrees(), aspect, d * 0.001f, m_modelScale * 8.f);
        view.lookAt(cameraPosition(), at, QVector3D(0, 0, 1));
    }
    return proj * view;
}

// ---------------------------------------------------------------- materials

VesselView::Material VesselView::wallMaterial() const {
    Material m;
    const bool endo = m_mode == Mode::Endoluminal;
    switch (m_palette) {
    case Palette::Arterial:
        // Old project exact colour: (1.0, 0.20, 0.3686, 0.30)
        // High ambient so all faces read as arterial red regardless of normal orientation
        // #C21807 = (0.7608, 0.0941, 0.0275)
        m.colour  = endo ? QVector4D(0.92f, 0.24f, 0.33f, 1.f)
                        : QVector4D(0.92f, 0.24f, 0.33f, m_wallOpacity);
        m.sheen    = QVector3D(1.0f, 0.85f, 0.85f);
        m.specular = endo ? 0.45f : 0.20f;
        m.shininess= 22.f;
        m.ambient  = 0.22f;   // lower ambient — less bright
        break;
    case Palette::Angiographic:
        // Barely visible by default in plain Fluoro, per explicit spec:
        // "No injection -> vessel barely visible" - contrast is what makes
        // a vessel actually show up on a real fluoroscopy frame, not the
        // vessel wall itself, which has similar radiodensity to
        // surrounding tissue. Deliberately a low, non-zero opacity (0.10)
        // rather than fully hidden - "barely visible" is not the same as
        // invisible, and this is independent of m_wallOpacity, which stays
        // a global slider affecting the other palettes (3D, Monochrome)
        // unchanged. Road Map mode still overrides colour/opacity in the
        // draw section below, unaffected by this.
        m.colour = endo ? QVector4D(0.88f, 0.88f, 0.90f, 1.f)
                        : QVector4D(0.85f, 0.86f, 0.90f, 0.10f);
        m.sheen = QVector3D(0.f, 0.f, 0.f);
        m.specular = 0.0f;
        m.shininess = 1.f;
        break;
    case Palette::Monochrome:
        m.colour = endo ? QVector4D(0.55f, 0.55f, 0.55f, 1.f)
                        : QVector4D(0.62f, 0.62f, 0.62f, m_wallOpacity);
        m.sheen = QVector3D(0.9f, 0.9f, 0.9f);
        m.specular = 0.15f;
        m.shininess = 18.f;
        break;
    }
    return m;
}

// --------------------------------------------------------------------- GL

void VesselView::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Never cull: endoluminal rendering shows the inside of an outward-facing
    // surface, exactly the faces culling would remove.
    glDisable(GL_CULL_FACE);

    m_program = std::make_unique<QOpenGLShaderProgram>();
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader))
        qWarning() << "VesselView: vertex shader failed to compile:" << m_program->log();
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader))
        qWarning() << "VesselView: fragment shader failed to compile:" << m_program->log();
    m_program->bindAttributeLocation("aPos", 0);
    m_program->bindAttributeLocation("aNormal", 1);
    if (!m_program->link())
        qWarning() << "VesselView: shader program failed to link:" << m_program->log();
}

void VesselView::resizeGL(int w, int h) { glViewport(0, 0, w, h); }

void VesselView::ensureBatch(Batch &b) {
    if (b.ready) return;
    b.vao.create();
    b.vbo.create();
    b.vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    b.ready = true;
}

void VesselView::uploadBatch(Batch &b, const std::vector<float> &interleaved,
                             const std::vector<uint32_t> &indices) {
    ensureBatch(b);
    QOpenGLVertexArrayObject::Binder binder(&b.vao);

    b.vbo.bind();
    b.vbo.allocate(interleaved.data(), static_cast<int>(interleaved.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void *>(3 * sizeof(float)));

    b.indexed = !indices.empty();
    if (b.indexed) {
        if (!b.ibo.isCreated()) {
            b.ibo.create();
            b.ibo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        }
        b.ibo.bind();
        b.ibo.allocate(indices.data(), static_cast<int>(indices.size() * sizeof(uint32_t)));
        b.elementCount = static_cast<int>(indices.size());
    } else {
        b.elementCount = static_cast<int>(interleaved.size() / 6);
    }
}

void VesselView::drawBatch(Batch &b, GLenum mode, const Material &m, bool shaded,
                           bool pulsatile) {
    if (!b.ready || b.elementCount == 0) return;
    // Default off: a caller has to ask for pulsation explicitly.
    m_program->setUniformValue("uPulsatile", pulsatile ? 1 : 0);
    m_program->setUniformValue("uColor", m.colour);
    m_program->setUniformValue("uSheen", m.sheen);
    m_program->setUniformValue("uSpecular", m.specular);
    m_program->setUniformValue("uShininess", m.shininess);
    m_program->setUniformValue("uShaded", shaded ? 1 : 0);
    // Vessel uses default lighting; body meshes set these themselves before drawing
    m_program->setUniformValue("uAmbient", m.ambient);
    m_program->setUniformValue("uDiffuse", 1.0f - m.ambient);
    QOpenGLVertexArrayObject::Binder binder(&b.vao);
    if (b.indexed) glDrawElements(mode, b.elementCount, GL_UNSIGNED_INT, nullptr);
    else           glDrawArrays(mode, 0, b.elementCount);
}

// Uploads an 8-float-per-vertex wall buffer: position, normal, arc length from
// the aortic root, and local lumen radius. The last two drive the pulsation and
// are computed once here rather than per frame.
void VesselView::uploadWallBatch(Batch &b, const std::vector<float> &interleaved,
                                 const std::vector<uint32_t> &indices) {
    ensureBatch(b);
    QOpenGLVertexArrayObject::Binder binder(&b.vao);
    b.vbo.bind();
    b.vbo.allocate(interleaved.data(), static_cast<int>(interleaved.size() * sizeof(float)));
    const int stride = 8 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void *>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void *>(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void *>(7 * sizeof(float)));
    if (!b.ibo.isCreated()) {
        b.ibo.create();
        b.ibo.setUsagePattern(QOpenGLBuffer::StaticDraw);
    }
    b.ibo.bind();
    b.ibo.allocate(indices.data(), static_cast<int>(indices.size() * sizeof(uint32_t)));
    b.indexed = true;
    b.elementCount = static_cast<int>(indices.size());
}

void VesselView::rebuildMeshBatch() {
    m_meshDirty = false;
    if (!m_mesh) { m_meshBatch.elementCount = 0; return; }

    const std::vector<float> base = m_mesh->buildInterleavedRenderData();
    const std::vector<uint32_t> idx = m_mesh->buildIndexData();
    const size_t nVerts = base.size() / 6;

    std::vector<float> wall(nVerts * 8, 0.f);
    for (size_t i = 0; i < nVerts; ++i)
        for (int k = 0; k < 6; ++k) wall[i * 8 + k] = base[i * 6 + k];

    // Without a centerline there is nothing to measure distance or calibre
    // against, so the wall simply does not pulse — attributes stay 0.
    if (m_graph && m_graph->nodeCount() > 1) {
        const auto &nodes = m_graph->nodes();

        // Aortic root proxy: the widest node in the tree. On this model that is
        // the 15 mm aorta, which is where the pressure wave actually starts.
        int32_t root = 0;
        for (size_t n = 1; n < nodes.size(); ++n)
            if (nodes[n].radius > nodes[root].radius) root = static_cast<int32_t>(n);

        // Arc length from the root to every node, over the graph.
        const std::vector<float> arc = m_graph->distancesFrom(root);

        // Uniform grid over the centerline nodes, so assigning 260k mesh
        // vertices to their nearest node is O(n) rather than O(n*m).
        float cell = 0.f;
        for (const auto &l : m_graph->links()) cell += l.length;
        cell = std::max(cell / std::max<size_t>(m_graph->links().size(), 1) * 6.f, 1e-4f);

        std::unordered_map<int64_t, std::vector<int32_t>> grid;
        auto key = [&](float x, float y, float z) {
            const int64_t cx = int64_t(std::floor(x / cell));
            const int64_t cy = int64_t(std::floor(y / cell));
            const int64_t cz = int64_t(std::floor(z / cell));
            return (cx * 73856093LL) ^ (cy * 19349663LL) ^ (cz * 83492791LL);
        };
        for (size_t n = 0; n < nodes.size(); ++n)
            grid[key(nodes[n].position.x, nodes[n].position.y, nodes[n].position.z)]
                .push_back(static_cast<int32_t>(n));

        float maxArc = 0.f;
        for (float a : arc)
            if (a < std::numeric_limits<float>::max()) maxArc = std::max(maxArc, a);

        // ── Heart region ────────────────────────────────────────────────────
        // The pulse is confined to the heart and great vessels rather than
        // applied to the whole tree. Auto-location takes the widest node in the
        // graph: on this model that is the 15 mm aortic root, which is exactly
        // where the pump is. Set autoLocate false to pin it by hand.
        cn::Vec3 heart = nodes[root].position;
        if (!m_heart.autoLocate)
            heart = cn::Vec3(m_heart.centre.x(), m_heart.centre.y(), m_heart.centre.z());
        m_heartCentre = QVector3D(heart.x, heart.y, heart.z);

        const float core  = std::max(m_heart.coreRadius, 1e-6f);
        const float fade  = std::max(m_heart.falloff, 1e-6f);
        const float amp   = m_heart.heartAmplify;
        const float perif = std::clamp(m_heart.peripheralFraction, 0.f, 1.f);

        // Full amplitude out to coreRadius, smoothstepping down to the
        // peripheral level by coreRadius + falloff. A hard cut-off would show
        // as a visible seam ring across the aorta.
        auto spatialWeight = [&](const cn::Vec3 &v) {
            const float d = (v - heart).length();
            if (d <= core) return amp;
            const float t = std::clamp((d - core) / fade, 0.f, 1.f);
            const float s = t * t * (3.f - 2.f * t);        // smoothstep
            return amp + (perif * amp - amp) * s;
        };

        // Calibre gate — sub-millimetre noise only. It is NOT what excludes the
        // carotids: they and the coronaries are both small, so no radius
        // threshold separates them, and setting one high enough to drop the
        // carotids also drops the coronaries that make up most of the moving
        // surface. The superior cut-off below does that job instead.
        const float cFull = std::max(m_heart.calibreFull, 1e-6f);
        const float cZero = std::clamp(m_heart.calibreZero, 0.f, cFull - 1e-6f);
        auto calibreWeight = [&](float lumenR) {
            const float t = std::clamp((lumenR - cZero) / (cFull - cZero), 0.f, 1.f);
            return t * t * (3.f - 2.f * t);                 // smoothstep
        };

        // Superior cut-off: the heart region ends just above the arch. This is
        // what keeps the carotids static — they ascend past the arch while the
        // coronaries sit at or below the root, so position separates them where
        // calibre cannot. No inferior limit: the descending aorta belongs to the
        // great vessels as much as the ascending one does.
        const float supLim  = m_heart.superiorLimit;
        const float supFade = std::max(m_heart.superiorFade, 1e-6f);
        auto superiorWeight = [&](const cn::Vec3 &v) {
            const float dz = v.z - heart.z;
            if (dz <= supLim) return 1.f;                   // at or below the arch
            const float t = std::clamp((dz - supLim) / supFade, 0.f, 1.f);
            return 1.f - t * t * (3.f - 2.f * t);
        };

        for (size_t i = 0; i < nVerts; ++i) {
            const cn::Vec3 p(wall[i * 8 + 0], wall[i * 8 + 1], wall[i * 8 + 2]);
            int32_t best = -1;
            float bestD = std::numeric_limits<float>::max();
            const int64_t cx = int64_t(std::floor(p.x / cell));
            const int64_t cy = int64_t(std::floor(p.y / cell));
            const int64_t cz = int64_t(std::floor(p.z / cell));
            for (int dx = -1; dx <= 1 && best < 0; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dz = -1; dz <= 1; ++dz) {
                        auto it = grid.find(((cx + dx) * 73856093LL) ^
                                            ((cy + dy) * 19349663LL) ^
                                            ((cz + dz) * 83492791LL));
                        if (it == grid.end()) continue;
                        for (int32_t n : it->second) {
                            const float d = (nodes[n].position - p).lengthSquared();
                            if (d < bestD) { bestD = d; best = n; }
                        }
                    }
            if (best < 0) continue;   // vertex far from any centerline: no pulse
            const float a = arc[best];
            wall[i * 8 + 6] = (a < std::numeric_limits<float>::max()) ? a : maxArc;

            // Distance to the centerline IS the local radius on a tube, and it
            // is a better figure than the node's stored radius because it varies
            // smoothly across the surface instead of stepping at each node.
            const float localRadius = std::sqrt(bestD);

            // Localise the motion to the heart: near the pump AND large enough
            // to be a great vessel. The amplitude is folded into this one
            // attribute rather than carried as separate weights, because the
            // shader only ever uses their product.
            //
            // The calibre gate reads the graph's own MaximumInscribedSphereRadius
            // at the nearest node rather than the vertex's distance to the
            // centerline: the stored radius is what VMTK measured, and it does
            // not wobble across a surface the way the point distance does.
            const float lumenR = nodes[best].radius;
            wall[i * 8 + 7] = localRadius
                              * spatialWeight(p)          // near the pump
                              * calibreWeight(lumenR)     // and a great vessel
                              * superiorWeight(p);        // and not above the arch
        }
    }

    uploadWallBatch(m_meshBatch, wall, idx);
}

void VesselView::setHeartRegion(const HeartRegion &r) {
    m_heart = r;
    // The weighting is baked into the vertex buffer, so the wall has to be
    // rebuilt — this is not a uniform that can be changed per frame.
    m_meshDirty = true;
    update();
}

void VesselView::setCardiacCycle(const cn::CardiacCycle *c) {
    m_cycle = c;
    if (c) {
        // Sample the wall-motion waveform once into the LUT the shader indexes.
        // Kept normalised 0..1 so uStrain alone sets the amplitude.
        //
        // This reads CardiacCycle::wallWaveform() rather than re-deriving a
        // shape from arterialPressure() here. The wall follows a chamber fill
        // and release — a smooth expand, a brief hold, a smooth contract — while
        // the ABP trace still follows the true pressure pulse with its dicrotic
        // notch. Two shapes, one clock, one definition of each.
        for (int i = 0; i < kPulseLutSize; ++i) {
            const float p = static_cast<float>(i) / static_cast<float>(kPulseLutSize);
            m_pulseLUT[i] = std::clamp(c->wallWaveform(p), 0.f, 1.f);
        }
    }
    update();
}

void VesselView::rebuildCenterlineBatch() {
    m_centerlineDirty = false;
    if (!m_graph) { m_centerlineBatch.elementCount = 0; return; }
    std::vector<float> v;
    v.reserve(m_graph->links().size() * 12);
    for (const auto &l : m_graph->links())
        for (int32_t id : {l.from, l.to}) {
            const cn::Vec3 &p = m_graph->nodes()[id].position;
            v.insert(v.end(), {p.x, p.y, p.z, 0.f, 1.f, 0.f});
        }
    uploadBatch(m_centerlineBatch, v, {});
}


void VesselView::rebuildBodyMeshBatch(const QString &path, Batch &batch,
                                      const BodyAlign &align) {
    if (path.isEmpty()) { batch.elementCount = 0; return; }
    cn::StlMesh bodyMesh;
    if (!bodyMesh.load(path.toStdString())) { batch.elementCount = 0; return; }
    if (!m_mesh) { batch.elementCount = 0; return; }

    // ── Body-to-vessel placement ─────────────────────────────────────────────
    //
    // Muscular.stl, Skeletal.stl and the vessel STL are authored in one frame at
    // one scale. Measured bounds (mm):
    //
    //     vessel    X -287.6..287.6   Y  -88.4.. 95.0   Z  38.6..1700.6
    //     skeletal  X -334.6..334.6   Y -137.3..112.8   Z   9.2..1705.5
    //     muscular  X -335.7..334.9   Y -137.2..132.7   Z   6.5..1707.8
    //
    // X centres agree to 0.4 mm and Z centres to 12 mm. Vessel nests inside
    // skeletal nests inside muscular on every axis, and the body correctly
    // extends 30 mm below the vessel (the arterial tree stops at the ankle) and
    // a few mm above it (scalp above the cerebral vessels). So the correct
    // transform is the identity.
    //
    // The previous code fitted each body mesh's bounding box onto the vessel's,
    // per axis. Because the vessel's Y extent is a fact about where arteries run
    // (183 mm) and not about body depth (250-270 mm), that fit compressed the
    // skeleton to 73% of its authored depth and 86% of its width, pulled the top
    // of the skull DOWN 8.5 mm and lifted the feet UP 33 mm. Two call sites also
    // passed different margins and opposite back-shifts, so the 3D and Fluoro
    // views could place the muscle layer 30 mm apart.
    //
    // `align` is a fine adjustment on top of identity and is a no-op by default.
    // `m_bodyAutoFit` restores the old per-axis stretch for a body mesh that
    // genuinely comes from a different source.

    const cn::Box3 vb = m_mesh->bounds();
    const cn::Vec3 vesselC = vb.center();

    float sx = align.scale, sy = align.scale, sz = align.scale;
    cn::Vec3 pivot = vesselC;

    if (m_bodyAutoFit) {
        const cn::Box3 bb = bodyMesh.bounds();
        pivot = bb.center();
        sx = (std::max(vb.size().x, 1e-6f) / std::max(bb.size().x, 1e-6f)) * align.scale;
        sy = (std::max(vb.size().y, 1e-6f) / std::max(bb.size().y, 1e-6f)) * align.scale;
        sz = (std::max(vb.size().z, 1e-6f) / std::max(bb.size().z, 1e-6f)) * align.scale;
    }

    // With identity (scale 1, offset 0, autofit off) this loop reproduces the
    // source vertices exactly: (v - pivot) * 1 + pivot + 0.
    const auto &verts = bodyMesh.buildInterleavedRenderData();
    std::vector<float> transformed;
    transformed.reserve(verts.size());
    for (size_t i = 0; i + 5 < verts.size(); i += 6) {
        const float wx = (verts[i]     - pivot.x) * sx + vesselC.x + align.offset.x();
        const float wy = (verts[i + 1] - pivot.y) * sy + vesselC.y + align.offset.y();
        const float wz = (verts[i + 2] - pivot.z) * sz + vesselC.z + align.offset.z();
        transformed.insert(transformed.end(),
                           {wx, wy, wz, verts[i + 3], verts[i + 4], verts[i + 5]});
    }
    uploadBatch(batch, transformed, bodyMesh.buildIndexData());
}

void VesselView::ensureXrayFbo(int w, int h) {
    w = std::max(w, 1);
    h = std::max(h, 1);
    if (m_xrayFbo && m_xrayFbo->width() == w && m_xrayFbo->height() == h) return;

    QOpenGLFramebufferObjectFormat fmt;
    // Floating point, not the normalized [0,1] the default framebuffer uses:
    // that's the whole point. Raw attenuation sums here are never clamped,
    // no matter how many overlapping mesh fragments a ray crosses, so the
    // exponential in the compositing pass sees the true total and can never
    // be fooled into blowing out to solid white by an unexpectedly dense
    // stack of geometry.
    fmt.setInternalTextureFormat(GL_RGBA32F);
    fmt.setAttachment(QOpenGLFramebufferObject::NoAttachment);  // no depth needed
    fmt.setSamples(0);
    m_xrayFbo = std::make_unique<QOpenGLFramebufferObject>(w, h, fmt);
    if (!m_xrayFbo->isValid()) { m_xrayFbo.reset(); return; }

    // Linear filtering on the way back out is what turns this buffer's
    // deliberately lower resolution into softness rather than visible
    // blockiness when the compositing pass samples it.
    glBindTexture(GL_TEXTURE_2D, m_xrayFbo->texture());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void VesselView::ensureXrayQuad() {
    if (m_xrayQuadBatch.ready) return;
    // One triangle, big enough that its clip-space footprint covers the
    // whole [-1,1] viewport (the classic "big triangle" full-screen trick —
    // one draw call, no diagonal seam a two-triangle quad would have).
    // Drawn with uViewProj forced to identity (see the compositing pass in
    // paintGL), so these coordinates ARE the clip-space coordinates rather
    // than being transformed by the scene camera. Normals are unused by
    // that pass, so they're left zeroed.
    const std::vector<float> verts = {
        -1.f, -1.f, 0.f,  0.f, 0.f, 1.f,
        3.f, -1.f, 0.f,  0.f, 0.f, 1.f,
        -1.f,  3.f, 0.f,  0.f, 0.f, 1.f,
    };
    uploadBatch(m_xrayQuadBatch, verts, {});
}

void VesselView::setBodyAlignment(const BodyAlign &muscular, const BodyAlign &skeletal) {
    m_muscularAlign = muscular;
    m_skeletalAlign = skeletal;
    m_bodyMeshDirty = true;
    update();
}

void VesselView::nudgeBodyLayers(float dz, float dScale) {
    m_muscularAlign.offset.setZ(m_muscularAlign.offset.z() + dz);
    m_skeletalAlign.offset.setZ(m_skeletalAlign.offset.z() + dz);
    m_muscularAlign.scale += dScale;
    m_skeletalAlign.scale += dScale;
    m_bodyMeshDirty = true;
    update();
}

// ─────────────────────────────────────────────────────────────────────────────
// Sheath geometry
// ─────────────────────────────────────────────────────────────────────────────

void VesselView::appendTube(const std::vector<cn::Vec3> &path,
                            const std::vector<float> &radii, int sides,
                            std::vector<float> &vOut, std::vector<uint32_t> &iOut,
                            bool capStart, bool capEnd) {
    if (path.size() < 2 || radii.size() != path.size() || sides < 3) return;
    // A zero-length first segment makes every frame vector NaN, which then
    // propagates into the vertex buffer and takes the whole draw with it.
    if ((path[1] - path[0]).lengthSquared() < 1e-18f) return;

    const uint32_t base = static_cast<uint32_t>(vOut.size() / 6);

    // Parallel transport rather than a fresh Frenet frame per ring: the cannula
    // follows the vessel centreline, and recomputing the frame at every sample
    // makes the tube visibly twist wherever the curvature flips.
    cn::Vec3 tan = (path[1] - path[0]).normalized();
    cn::Vec3 ref = cn::anyPerpendicular(tan);

    for (size_t k = 0; k < path.size(); ++k) {
        cn::Vec3 t;
        if (k == 0)                    t = (path[1] - path[0]).normalized();
        else if (k + 1 == path.size()) t = (path[k] - path[k - 1]).normalized();
        else                           t = (path[k + 1] - path[k - 1]).normalized();
        if (t.lengthSquared() < 0.5f) t = tan;

        // Rotate the reference vector by the same rotation that takes the old
        // tangent to the new one.
        const cn::Vec3 axis = tan.cross(t);
        const float    s2   = axis.lengthSquared();
        if (s2 > 1e-12f) {
            const float ang = std::atan2(std::sqrt(s2),
                                         std::max(-1.f, std::min(1.f, tan.dot(t))));
            ref = cn::rotateAbout(ref, axis.normalized(), ang);
        }
        tan = t;
        ref = (ref - t * ref.dot(t)).normalized();
        const cn::Vec3 bit = t.cross(ref);

        for (int s = 0; s < sides; ++s) {
            const float a = 2.f * float(M_PI) * float(s) / float(sides);
            const cn::Vec3 n = ref * std::cos(a) + bit * std::sin(a);
            const cn::Vec3 p = path[k] + n * radii[k];
            vOut.insert(vOut.end(), {p.x, p.y, p.z, n.x, n.y, n.z});
        }
    }

    for (size_t k = 0; k + 1 < path.size(); ++k) {
        for (int s = 0; s < sides; ++s) {
            const uint32_t a = base + uint32_t(k * sides + s);
            const uint32_t b = base + uint32_t(k * sides + (s + 1) % sides);
            const uint32_t cc = a + uint32_t(sides);
            const uint32_t d = b + uint32_t(sides);
            iOut.insert(iOut.end(), {a, b, d, a, d, cc});
        }
    }

    auto cap = [&](size_t ring, const cn::Vec3 &nrm) {
        const uint32_t centre = static_cast<uint32_t>(vOut.size() / 6);
        vOut.insert(vOut.end(), {path[ring].x, path[ring].y, path[ring].z,
                                 nrm.x, nrm.y, nrm.z});
        for (int s = 0; s < sides; ++s) {
            const uint32_t a = base + uint32_t(ring * sides + s);
            const uint32_t b = base + uint32_t(ring * sides + (s + 1) % sides);
            iOut.insert(iOut.end(), {centre, a, b});
        }
    };
    if (capStart) cap(0, (path[0] - path[1]).normalized());
    if (capEnd)   cap(path.size() - 1, (path.back() - path[path.size() - 2]).normalized());
}

void VesselView::setSheath(const cn::SheathGeometry &geom) {
    if (!geom.valid || geom.cannulaPath.size() < 2) { clearSheath(); return; }
    // Store only. Building the batches touches glEnableVertexAttribArray and
    // friends, whose function pointers are resolved in initializeGL; calling
    // them from a dialog handler with no current context crashes.
    m_sheathGeom    = geom;
    m_sheathVisible = true;
    m_sheathDirty   = true;
    update();
}

void VesselView::rebuildSheathBatches() {
    m_sheathDirty = false;
    const cn::SheathGeometry &geom = m_sheathGeom;
    if (!geom.valid || geom.cannulaPath.size() < 2) { m_sheathVisible = false; return; }

    m_sheathHubColour = QVector3D(geom.hubColour[0], geom.hubColour[1], geom.hubColour[2]);
    m_sheathOversized = geom.oversized;

    const int sides = 20;
    std::vector<float> v;
    std::vector<uint32_t> i;

    // ── Cannula ──────────────────────────────────────────────────────────────
    // Tapered over the distal few millimetres, exactly as a real introducer is:
    // the taper is what lets it follow the dilator through the arteriotomy.
    {
        std::vector<float> radii(geom.cannulaPath.size(), geom.cannulaOuterRadius);
        float arc = 0.f;
        for (size_t k = 0; k < geom.cannulaPath.size(); ++k) {
            if (k > 0) arc += cn::distance(geom.cannulaPath[k], geom.cannulaPath[k - 1]);
            if (geom.tipTaperLength > 1e-9f && arc < geom.tipTaperLength) {
                const float t = arc / geom.tipTaperLength;
                radii[k] = geom.cannulaOuterRadius * (0.45f + 0.55f * t);
            }
        }
        v.clear(); i.clear();
        appendTube(geom.cannulaPath, radii, sides, v, i, true, false);
        if (!m_sheathCannulaBatch.ready) ensureBatch(m_sheathCannulaBatch);
        uploadBatch(m_sheathCannulaBatch, v, i);
    }

    // ── Hub ──────────────────────────────────────────────────────────────────
    // Flared: narrow at the skin end where the strain relief meets the cannula,
    // full width at the back where the valve screws on.
    {
        const cn::Vec3 a = geom.hubBase, b = geom.hubTop;
        std::vector<cn::Vec3> p;
        std::vector<float>    r;
        const int rings = 8;
        for (int k = 0; k <= rings; ++k) {
            const float t = float(k) / float(rings);
            p.push_back(cn::lerp(a, b, t));
            // smoothstep flare from the strain relief out to the full body
            const float f = t * t * (3.f - 2.f * t);
            r.push_back(geom.cannulaOuterRadius * 1.6f +
                        (geom.hubRadius - geom.cannulaOuterRadius * 1.6f) * f);
        }
        v.clear(); i.clear();
        appendTube(p, r, sides, v, i, true, true);
        if (!m_sheathHubBatch.ready) ensureBatch(m_sheathHubBatch);
        uploadBatch(m_sheathHubBatch, v, i);
    }

    // ── Haemostasis valve cap ────────────────────────────────────────────────
    {
        std::vector<cn::Vec3> p{geom.hubTop, cn::lerp(geom.hubTop, geom.valveTop, 0.5f),
                                geom.valveTop};
        std::vector<float>    r{geom.valveRadius, geom.valveRadius,
                             geom.valveRadius * 0.82f};
        v.clear(); i.clear();
        appendTube(p, r, sides, v, i, true, true);
        if (!m_sheathValveBatch.ready) ensureBatch(m_sheathValveBatch);
        uploadBatch(m_sheathValveBatch, v, i);
    }

    // ── Side arm + three-way stopcock ────────────────────────────────────────
    {
        v.clear(); i.clear();
        std::vector<cn::Vec3> p;
        std::vector<float>    r;
        const int rings = 10;
        for (int k = 0; k <= rings; ++k) {
            const float t = float(k) / float(rings);
            p.push_back(cn::lerp(geom.sideArmOrigin, geom.sideArmEnd, t));
            r.push_back(geom.sideArmRadius);
        }
        appendTube(p, r, 12, v, i, false, false);

        // Stopcock body: a short barrel with a transverse handle port.
        const cn::Vec3 s0 = geom.stopcockCentre - geom.sideArmDir * (geom.stopcockLength * 0.5f);
        const cn::Vec3 s1 = geom.stopcockCentre + geom.sideArmDir * (geom.stopcockLength * 0.5f);
        appendTube({s0, s1}, {geom.stopcockRadius, geom.stopcockRadius},
                   12, v, i, true, true);
        const cn::Vec3 hnd = cn::anyPerpendicular(geom.sideArmDir);
        appendTube({geom.stopcockCentre, geom.stopcockCentre + hnd * (geom.stopcockRadius * 1.9f)},
                   {geom.stopcockRadius * 0.42f, geom.stopcockRadius * 0.52f},
                   10, v, i, true, true);

        if (!m_sheathArmBatch.ready) ensureBatch(m_sheathArmBatch);
        uploadBatch(m_sheathArmBatch, v, i);
    }
}

// Builds the three material spans (and the balloon) for one device into one
// DeviceBatches. Factored out so the parked host devices of a coaxial stack —
// the guide a microcatheter was introduced through — render the same way as the
// active device without duplicating the meshing.
void VesselView::buildDeviceInto(const cn::CatheterModel *dev, DeviceBatches &b, float hideBehindDepth) {
    if (!dev) {
        b.tip.elementCount = b.band.elementCount = 0;
        b.shaft.elementCount = b.balloon.elementCount = 0;
        return;
    }
    std::vector<float> v;
    std::vector<uint32_t> i;
    dev->buildTubeMesh(16, v, i, 0, kTipEnd);
    uploadBatch(b.tip, v, i);
    dev->buildTubeMesh(16, v, i, kTipEnd, kBandEnd);
    uploadBatch(b.band, v, i);

    // Clip the shaft's proximal end at hideBehindDepth (the host catheter's
    // own tip depth, when this device was telescoped through one) - see
    // setDeviceHiddenBehindDepth()'s own comment for why the shaft would
    // otherwise extend all the way back to the access site: nodes[] is
    // ordered tip-first, and its proximal-most node is always arc-length
    // zero, not wherever a host catheter's tip happens to be - CatheterModel
    // itself has no concept of "telescoped through a host" to clip against
    // on its own.
    int shaftLastNode = -1;
    if (hideBehindDepth > 0.f) {
        const auto &nodes = dev->state().nodes;
        const float visibleArc = dev->state().insertionDepth - hideBehindDepth;
        if (visibleArc <= 0.f) {
            // The tip has not actually advanced past the host's own tip yet
            // (e.g. the instant after switching devices) - nothing but the
            // tip/band should render, which is correct: the microcatheter
            // has not left the guide yet.
            shaftLastNode = kBandEnd;
        } else {
            float cumulative = 0.f;
            for (size_t k = static_cast<size_t>(kBandEnd); k + 1 < nodes.size(); ++k) {
                cumulative += cn::distance(nodes[k], nodes[k + 1]);
                if (cumulative >= visibleArc) { shaftLastNode = static_cast<int>(k + 1); break; }
            }
        }
    }
    dev->buildTubeMesh(16, v, i, kBandEnd, shaftLastNode);
    uploadBatch(b.shaft, v, i);
}

void VesselView::buildTubesFromSpans(const cn::CenterlineGraph *graph,
                                     const std::vector<CastSpan> &spans, Batch &out,
                                     float irregularity) {
    std::vector<float> verts;
    std::vector<uint32_t> idx;
    if (!graph || spans.empty()) { uploadBatch(out, verts, idx); return; }

    const cn::CenterlineGraph &g = *graph;
    constexpr int kSides = 10;
    // Fits comfortably inside the lumen at each end - the same spirit as
    // CatheterModel::Params::lumenFitFraction for a DRAWN (not physical)
    // radius: the cast/contrast should read as filling the vessel, not
    // exceeding it, regardless of the coarse per-node radius sampling.
    constexpr float kFitFraction = 0.85f;

    // Simple, deterministic (not time-based) hash - same inputs always give
    // the same output, so the irregularity pattern is stable across frames
    // rather than flickering, but varies with position so it does not
    // repeat an identical bump at every single ring.
    auto jitterHash = [](float x, float y, float z) -> float {
        float h = std::sin(x * 127.1f + y * 311.7f + z * 74.7f) * 43758.5453f;
        return h - std::floor(h);   // 0..1
    };

    for (const auto &span : spans) {
        if (span.frac <= 0.f || span.from < 0 || span.to < 0) continue;
        if (static_cast<size_t>(span.from) >= g.nodeCount() ||
            static_cast<size_t>(span.to)   >= g.nodeCount())
            continue;

        const auto &na = g.nodes()[static_cast<size_t>(span.from)];
        const auto &nb = g.nodes()[static_cast<size_t>(span.to)];

        const cn::Vec3 pa = na.position;
        // Partial fill: the leading edge stops part-way to `to` rather than
        // jumping straight to the full link length, which is what makes the
        // fill visibly "flow" outward over successive updates instead of
        // popping fully-formed one link at a time.
        const cn::Vec3 pb = cn::lerp(pa, nb.position, span.frac);

        const cn::Vec3 axisVec = pb - pa;
        const float len = axisVec.length();
        if (len < 1e-6f) continue;
        const cn::Vec3 dir = axisVec / len;

        const float ra = std::max(na.radius * kFitFraction, 1e-4f);
        const float rbFull = std::max(nb.radius * kFitFraction, 1e-4f);
        // Interpolate the radius toward the partial point too, so a leading
        // edge stopping between a wide and a narrow node tapers smoothly
        // rather than snapping to the far end's calibre early.
        const float rb = ra + (rbFull - ra) * span.frac;

        const cn::Vec3 refv = cn::anyPerpendicular(dir);
        const uint32_t base = static_cast<uint32_t>(verts.size() / 6);

        for (int ring = 0; ring < 2; ++ring) {
            const cn::Vec3 centre = (ring == 0) ? pa : pb;
            const float r = (ring == 0) ? ra : rb;
            for (int s = 0; s < kSides; ++s) {
                const float ang = (2.0f * float(M_PI) * float(s)) / float(kSides);
                const cn::Vec3 rim = cn::rotateAbout(refv, dir, ang);
                // Deterministic radius jitter, zero when irregularity is 0
                // (every existing caller's exact prior behaviour). Seeded
                // from the ring centre's own position and side index, so
                // the bumps sit at fixed points in space rather than
                // spinning or flickering, and differ from one ring to the
                // next along the span instead of repeating identically.
                float rJit = r;
                if (irregularity > 0.f) {
                    const float h = jitterHash(centre.x * 40.f + float(s) * 3.1f,
                                               centre.y * 40.f, centre.z * 40.f);
                    rJit = r * (1.f + irregularity * (h - 0.5f));
                }
                const cn::Vec3 pos = centre + rim * rJit;
                const cn::Vec3 nrm = rim.normalized();
                verts.insert(verts.end(), {pos.x, pos.y, pos.z, nrm.x, nrm.y, nrm.z});
            }
        }
        for (int s = 0; s < kSides; ++s) {
            const uint32_t a0 = base + static_cast<uint32_t>(s);
            const uint32_t a1 = base + static_cast<uint32_t>((s + 1) % kSides);
            const uint32_t b0 = a0 + static_cast<uint32_t>(kSides);
            const uint32_t b1 = a1 + static_cast<uint32_t>(kSides);
            idx.insert(idx.end(), {a0, b0, a1,  a1, b0, b1});
        }

        // Cap the leading edge: cheap (one fan of kSides triangles) and
        // harmless even where a further link continues the fill, since the
        // cap sits flush with - and is hidden inside - that next segment's
        // own tube. Without it, a partially-filled span (the common case
        // while actively injecting or draining) would show as a see-through
        // open pipe.
        const uint32_t centreIdx = static_cast<uint32_t>(verts.size() / 6);
        verts.insert(verts.end(), {pb.x, pb.y, pb.z, dir.x, dir.y, dir.z});
        for (int s = 0; s < kSides; ++s) {
            const uint32_t b0 = base + static_cast<uint32_t>(kSides + s);
            const uint32_t b1 = base + static_cast<uint32_t>(kSides + (s + 1) % kSides);
            idx.insert(idx.end(), {b0, b1, centreIdx});
        }
    }
    uploadBatch(out, verts, idx);
}

void VesselView::buildEmboliInto(const cn::EmbolizationModel *emb, Batch &outSet, Batch &outGrowing) {
    if (!emb || !emb->graph() || !emb->active()) {
        std::vector<float> v; std::vector<uint32_t> i;
        uploadBatch(outSet, v, i);
        uploadBatch(outGrowing, v, i);
        return;
    }
    // 0.98 rather than exactly 1.0: tolerates float accumulation in
    // fillFraction so a link that is visually indistinguishable from fully
    // set renders as set immediately, rather than spending one extra tick
    // looking like it's still actively growing.
    constexpr float kEmboliSetThreshold = 0.98f;
    std::vector<CastSpan> setSpans, growingSpans;
    setSpans.reserve(emb->state().links.size());
    for (const auto &link : emb->state().links) {
        CastSpan span{link.from, link.to, link.fillFraction};
        if (link.fillFraction >= kEmboliSetThreshold) setSpans.push_back(span);
        else                                          growingSpans.push_back(span);
    }
    // Irregularity scaled by (1 - viscosity), the same real distinction
    // uForeignGrainAmount's own comment already establishes: a viscous,
    // cohesive agent (Onyx/SQUID) forms a smooth, dense, uniform cast; a
    // thin, fast-polymerizing one (Glue) forms a visibly irregular, patchy
    // deposit. Applied to the actual mesh geometry here (a lumpy
    // cross-section, not a perfect cylinder), on top of the shader's own
    // grain texture, so a deposited cast reads as accumulated material
    // rather than a machine-smooth tube regardless of viewing angle or
    // zoom level.
    const cn::EmbolicAgentSpec &spec = cn::embolicAgentSpec(emb->agent());
    const float irregularity = std::clamp(0.35f * (1.f - spec.viscosity), 0.03f, 0.35f);
    buildTubesFromSpans(emb->graph(), setSpans, outSet, irregularity);
    buildTubesFromSpans(emb->graph(), growingSpans, outGrowing, irregularity);
}

void VesselView::buildContrastInto(const cn::ContrastInjectionModel *contrast,
                                   Batch (&buckets)[kContrastBuckets]) {
    std::vector<CastSpan> bucketSpans[kContrastBuckets];

    if (contrast && contrast->phase() != cn::ContrastPhase::Idle) {
        // graph() isn't exposed by ContrastInjectionModel (it never needed to
        // be read back before now) - MainWindow wires the same m_graph into
        // both models, so this view already has that pointer via m_graph
        // directly.
        //
        // Bucketed by the link's OWN level (concentration), not by any new
        // distance computation: level is already 0 near the advancing
        // leading edge (a span that has only just started filling) and 1 on
        // an established column close behind it (ContrastInjectionModel's
        // own fill-front math). That is exactly the gradient a real
        // contrast bolus shows - concentrated where it has been flowing
        // longest, dilute at its advancing tip - so no extra state or
        // distance query is needed; it falls straight out of data the model
        // already produces.
        for (const auto &link : contrast->state().links) {
            if (link.level <= 0.f) continue;
            const int b = std::clamp(static_cast<int>(link.level * kContrastBuckets),
                                     0, kContrastBuckets - 1);
            bucketSpans[b].push_back({link.from, link.to, link.level});
        }
    }
    for (int b = 0; b < kContrastBuckets; ++b)
        buildTubesFromSpans(m_graph, bucketSpans[b], buckets[b]);
}

void VesselView::rebuildDeviceBatches() {
    m_deviceDirty = false;
    if (!m_deviceBatches) return;
    // setDevice(nullptr) means "no device in the patient" — e.g. a sheath is in
    // but no catheter has been selected yet. Zero the spans so the previous
    // device stops being drawn instead of lingering as a stale mesh.
    buildDeviceInto(m_device, *m_deviceBatches, m_deviceHiddenBehindDepth);
    if (!m_device) return;
    rebuildBalloonBatch();

    // Parked host devices of the coaxial stack, from outermost inwards.
    m_hostBatches.resize(m_hostDevices.size());
    for (size_t k = 0; k < m_hostDevices.size(); ++k) {
        if (!m_hostBatches[k]) m_hostBatches[k] = std::make_unique<DeviceBatches>();
        buildDeviceInto(m_hostDevices[k], *m_hostBatches[k]);
    }
}

void VesselView::setHostDevices(const std::vector<const cn::CatheterModel *> &hosts) {
    m_hostDevices = hosts;
    m_deviceDirty = true;
    update();
}

void VesselView::rebuildBalloonBatch() {
    if (!m_device || !m_deviceBatches) return;
    std::vector<float> v;
    std::vector<uint32_t> i;
    m_device->buildBalloonMesh(18, 24, v, i);
    uploadBatch(m_deviceBatches->balloon, v, i);
}

void VesselView::paintGL() {
    if (m_meshDirty) rebuildMeshBatch();
    if (m_centerlineDirty) rebuildCenterlineBatch();
    if (m_deviceDirty) rebuildDeviceBatches();
    if (m_emboliDirty) { buildEmboliInto(m_emboli, m_emboliBatchSet, m_emboliBatchGrowing); m_emboliDirty = false; }
    if (m_contrastDirty) { buildContrastInto(m_contrast, m_contrastBatches); m_contrastDirty = false; }
    if (m_sheathDirty) rebuildSheathBatches();
    if (m_bodyMeshDirty) {
        // Same alignment as setBodyMeshes(). Previously these two sites used
        // different margins and opposite back-shifts, so which one ran last
        // decided where the muscle layer ended up.
        rebuildBodyMeshBatch(m_skeletalPath, m_skeletalBatch, m_skeletalAlign);
        rebuildBodyMeshBatch(m_muscularPath, m_muscularBatch, m_muscularAlign);
        m_bodyMeshDirty = false;
    }
    if (m_mode == Mode::Endoluminal) updateEndoluminalFrame();
    else if (m_mode == Mode::Reference) updateFollowFrame();

    const bool endo   = m_mode == Mode::Endoluminal;
    const bool fluoro = (m_palette == Palette::Angiographic) && !endo;
    if (endo)
        glClearColor(0.02f, 0.014f, 0.014f, 1.f);
    else if (fluoro || m_dsaActive || m_roadMapActive)
        glClearColor(0.03f, 0.03f, 0.035f, 1.f);   // dark — the X-ray accumulation
    // (below) adds brightness up from here
    else
        glClearColor(0.0f, 0.0f, 0.0f, 1.f);   // black for 3D/mesh
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Frozen roadmap mask, drawn first as a flat background layer - a
    // complete, self-contained QPainter session, fully ended before any
    // raw GL shader work below begins. This is what makes the mask stay
    // fixed in screen space regardless of what the camera/C-arm angle
    // does afterward: it is a captured 2D image, not 3D geometry being
    // re-projected each frame, so nothing here can rotate or re-frame it -
    // only an explicit captureRoadmapMask() ("Re-mask") call ever replaces
    // it. Everything drawn after this (bone, muscle, the LIVE near-
    // invisible vessel, catheter) is genuinely live and responds normally.
    drawRoadmapMaskLayer();

    if (!m_program) return;

    m_program->bind();
    m_program->setUniformValue("uViewProj", viewProjection());

    // Pulsation uniforms. Only the wall batch carries attributes 2 and 3, so
    // every other batch — including the Muscular and Skeletal layers — sees
    // aPulseAmp == 0 and is left undisplaced.
    if (m_cycle && m_pulsation) {
        m_program->setUniformValue("uPhase",    m_cycle->phase());
        m_program->setUniformValue("uCycleSec", m_cycle->cycleSeconds());
        m_program->setUniformValue("uPwv",      m_cycle->params().pulseWaveSpeed);
        m_program->setUniformValue("uStrain",   m_cycle->params().distensionStrain);
        m_program->setUniformValueArray("uPulseLUT", m_pulseLUT, kPulseLutSize, 1);
        // Same radius the amplitude falloff already uses (rebuildWallBatch's
        // spatialWeight) - the region that's amplified as "the heart" is the
        // same region that should move as one synchronized pump, so the two
        // boundaries have to agree or a vertex could be inside one and
        // outside the other.
        m_program->setUniformValue("uHeartTravelExempt", m_heart.coreRadius + m_heart.falloff);
    } else {
        m_program->setUniformValue("uStrain", 0.f);
    }
    m_program->setUniformValue("uPulsatile", 0);   // nothing pulses unless it asks
    m_program->setUniformValue("uCameraPos", cameraPosition());
    m_program->setUniformValue("uHeartCentreZ", m_heartCentre.z());
    // Continuously increasing (never wraps, unlike phase() alone), reusing
    // the same shared clock rather than adding a separate timer just for
    // live grain. Independent of m_pulsation — grain should keep flickering
    // even if the pulsation displacement itself is toggled off.
    m_program->setUniformValue("uLiveTime",
                               m_cycle ? (static_cast<float>(m_cycle->beatCount()) + m_cycle->phase()) : 0.0f);
    m_program->setUniformValue("uLightDir", QVector3D(-0.35f, -0.72f, -0.6f).normalized());
    m_program->setUniformValue("uHeadlight", endo ? 1 : 0);
    m_program->setUniformValue("uFalloff",   endo ? m_modelScale * 0.4f : 0.0f);
    // Off by default in every VesselView instance; only the Fluoro X-ray
    // branch below turns it on, and only for its own two passes.
    m_program->setUniformValue("uXrayMode",    0);
    m_program->setUniformValue("uXrayIsVessel", 0);

    Material line;
    line.colour = QVector4D(0.35f, 0.62f, 0.75f, 1.f);

    if (endo) {
        // The eye is inside the catheter, so its own tube would wrap the camera
        // and fill the frame. A real intraluminal camera shows nothing of its
        // own hardware either.
        drawBatch(m_meshBatch, GL_TRIANGLES, wallMaterial(), true, /*pulsatile=*/true);
    } else {
        if (m_showCenterline) {
            glLineWidth(1.f);
            drawBatch(m_centerlineBatch, GL_LINES, line, false);
        }
        // ── Body mesh layers ────────────────────────────────────────────────
        // Two different draws share these two meshes, chosen by `fluoro`:
        //   * Fluoro (Angiographic palette): X-ray / DRR-style projection —
        //     see the branch immediately below.
        //   * Everything else (3D / Mesh / DSA — arterial3D or m_dsaActive):
        //     the old-project translucent layering, unchanged — Muscular
        //     (outermost, opacity 0.04-0.05, flat/unlit), Skeletal (Phong
        //     shaded), then the vessel wall on top. Old project used VTK
        //     depth-peeling for this; here it's plain alpha blending with
        //     glDepthMask(GL_FALSE) so the layers accumulate without writing
        //     the depth buffer.

        const bool hasMuscular = m_muscularBatch.elementCount > 0;
        const bool hasSkeletal = m_skeletalBatch.elementCount > 0;
        const bool hasVessel   = m_meshBatch.elementCount > 0;
        const bool arterial3D  = (m_palette == Palette::Arterial);

        if (fluoro) {
            // ── X-ray / DRR-style projection — Fluoro View only ─────────────
            //
            // Real fluoroscopy is not lit 3D geometry — it is a projection.
            // X-rays travel straight through the patient and the detector
            // reads how much of each ray got absorbed. Every earlier attempt
            // at this tried to fake that inside a single forward-rendering
            // pass — additive blending relying on the framebuffer's [0,1]
            // clamp as an accidental ceiling, then alpha "over" compositing
            // relying on a hand-picked target brightness — and each was
            // still fundamentally approximating Beer-Lambert through a blend
            // equation rather than actually computing it.
            //
            // This does the real thing, in two passes:
            //   1. ACCUMULATE — Skeletal.stl and Muscular.stl's raw
            //      attenuation contributions summed into an offscreen
            //      FLOATING-POINT buffer (never clamped, unlike the default
            //      framebuffer), one channel per material (R=muscle/soft
            //      tissue, G=bone), via glColorMask. Vessel is deliberately
            //      NOT part of this — see the overlay draw after Pass 2.
            //   2. COMPOSITE — one full-screen triangle reads that buffer,
            //      sums the channels into the total attenuation the ray
            //      actually accumulated, applies the real Beer-Lambert
            //      exponential exactly once (kFragmentShader's uXrayMode==2
            //      branch) — the mathematically correct point to do it,
            //      which a single blend equation can never be — then a
            //      window/level contrast stretch and a gamma curve, the way
            //      a real console processes its detector signal for display.
            //
            // Source/detector geometry: there is no separate X-ray camera
            // to build. The camera used for both passes is whatever this
            // frame's regular uViewProj already is (set above, from the
            // normal camera/C-arm-synced code) — that camera, already kept
            // in sync with the simulated C-arm's angles elsewhere in this
            // class, IS the source/detector geometry (a perspective camera
            // is a point-source projection onto a plane, which is exactly
            // what an X-ray source and detector are). Reusing it rather
            // than adding a second one is what makes this automatically
            // follow the C-arm the same way everything else in this view
            // already does, with no additional synchronisation code needed.
            GLint vp[4];
            glGetIntegerv(GL_VIEWPORT, vp);
            // Was 2/3 resolution (before that, half): anatomy was still
            // reading as too soft to make out clearly. Full resolution here
            // — the softness this projection needs now comes only from the
            // widget-level blur (QuadPanel.cpp), not from this buffer too.
            const int fboW = std::max(vp[2], 64);
            const int fboH = std::max(vp[3], 64);
            ensureXrayFbo(fboW, fboH);

            if (m_xrayFbo) {
                // ── Pass 1: accumulate ───────────────────────────────────────
                m_xrayFbo->bind();
                glViewport(0, 0, fboW, fboH);
                glClearColor(0.f, 0.f, 0.f, 0.f);
                glClear(GL_COLOR_BUFFER_BIT);

                glDisable(GL_DEPTH_TEST);
                glDepthMask(GL_FALSE);
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE);   // additive — safe here: the
                // target is float and never
                // clamps, so the raw sum
                // stays exact regardless of
                // how many fragments overlap.

                m_program->setUniformValue("uXrayMode",  1);
                m_program->setUniformValue("uPulsatile", 0);

                // Muscle -> red channel only. Raw crossing count, grain-
                // textured — every brightness/contrast decision happens in
                // the compositing pass (mode 2) via VesselView::XrayTuning
                // (Muscle Ambient/Diffuse/Opacity/Soft-Tissue Multiplier/
                // Muscle Brightness), set from the X-Ray Calibration dialog
                // (MainWindow). A procedural, mesh-free version of this
                // soft-tissue layer was tried and then reverted — this is
                // Muscular.stl's own accumulated density again.
                if (hasMuscular) {
                    glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE);
                    QOpenGLVertexArrayObject::Binder bnd(&m_muscularBatch.vao);
                    glDrawElements(GL_TRIANGLES, m_muscularBatch.elementCount, GL_UNSIGNED_INT, nullptr);
                }

                // Bone -> green channel only. Likewise raw here; shaped
                // entirely in mode 2 via Bone Ambient/Diffuse/Opacity/Bone
                // Intensity Multiplier, plus the screen-space cortical/
                // medullary contrast mode 2 now applies directly to this
                // channel's accumulated density map.
                if (hasSkeletal) {
                    glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_FALSE);
                    QOpenGLVertexArrayObject::Binder bnd(&m_skeletalBatch.vao);
                    glDrawElements(GL_TRIANGLES, m_skeletalBatch.elementCount, GL_UNSIGNED_INT, nullptr);
                }

                // Vessel: drawn again, but only its heart/great-vessel
                // region — see uXrayIsVessel above. uPulsatile=1 here means
                // the vertex shader's existing displacement runs exactly as
                // it does everywhere else this batch is drawn (aRootDist/
                // aPulseAmp, uPhase/uCycleSec/uPwv/uStrain/uPulseLUT — all
                // already set once per frame near the top of paintGL), so
                // this is genuine geometric expansion and contraction with
                // the cardiac cycle, not a brightness pulse standing in for
                // one. The carotids and the rest of the peripheral tree
                // still contribute nothing here: vHeartMask (mode 1) is
                // already driven to zero for them by the same
                // superiorWeight product that keeps them from physically
                // pulsing at all.
                if (hasVessel) {
                    m_program->setUniformValue("uXrayIsVessel", 1);
                    m_program->setUniformValue("uPulsatile",    1);
                    glColorMask(GL_FALSE, GL_FALSE, GL_TRUE, GL_FALSE);
                    QOpenGLVertexArrayObject::Binder bnd(&m_meshBatch.vao);
                    glDrawElements(GL_TRIANGLES, m_meshBatch.elementCount, GL_UNSIGNED_INT, nullptr);
                    m_program->setUniformValue("uPulsatile",    0);
                    m_program->setUniformValue("uXrayIsVessel", 0);
                }

                // Contrast + embolic material -> alpha channel. This is what
                // actually makes them read as radiopaque signal WITHIN the
                // X-ray projection rather than a separately-lit 3D object
                // drawn on top of it: both now contribute real optical
                // depth to the SAME Beer-Lambert sum bone/muscle/heart
                // already go through (mode 2 above), instead of a forward-
                // shaded alpha-blended overlay. Combining them additively
                // into one shared channel is physically correct - different
                // materials' attenuation coefficients genuinely sum along
                // one ray - so scaling each draw's OWN density by its own
                // real radiopacity BEFORE they are summed here is what keeps
                // a tantalum-based agent (Onyx/SQUID) reading brighter than
                // an iodine-based one (PHIL/Glue) once mode 2 resolves the
                // combined channel, without needing to tag or separate them
                // downstream at all.
                m_program->setUniformValue("uXrayIsVessel", 2);
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);

                if (m_contrast && m_contrast->phase() != cn::ContrastPhase::Idle) {
                    // Iodinated contrast is one of the most radiopaque
                    // things ever injected into a vessel - a high density
                    // scale, independent of any embolic agent. Bucket-
                    // scaled by concentration (t): the dilute leading edge
                    // genuinely contributes less optical depth than an
                    // established column, which is what gives "gradual
                    // dilution" as a real Beer-Lambert consequence instead
                    // of an alpha-blended fade. Low, near-baseline grain: a
                    // real contrast bolus is a well-mixed liquid, not a
                    // textured solid deposit the way an embolic cast is.
                    m_program->setUniformValue("uForeignGrainAmount", 0.30f);
                    m_program->setUniformValue("uForeignIsFluid", 1.0f);
                    for (int b = 0; b < kContrastBuckets; ++b) {
                        if (m_contrastBatches[b].elementCount == 0) continue;
                        const float t = (static_cast<float>(b) + 0.5f) / static_cast<float>(kContrastBuckets);
                        m_program->setUniformValue("uForeignDensityScale", 0.9f + 1.6f * t);
                        QOpenGLVertexArrayObject::Binder bnd(&m_contrastBatches[b].vao);
                        glDrawElements(GL_TRIANGLES, m_contrastBatches[b].elementCount, GL_UNSIGNED_INT, nullptr);
                    }
                }

                if (m_emboli && m_emboli->active() &&
                    (m_emboliBatchSet.elementCount > 0 || m_emboliBatchGrowing.elementCount > 0)) {
                    // uForeignIsFluid explicitly off here - a set embolic
                    // cast is a genuinely permanent solid deposit and must
                    // never show the flow animation, regardless of whatever
                    // the contrast draw above this last left the uniform at.
                    m_program->setUniformValue("uForeignIsFluid", 0.0f);
                    // Radiopacity-derived density scale: Onyx/SQUID
                    // (tantalum, ~0.85-0.90) read bright; PHIL/Glue (iodine,
                    // ~0.55-0.58) read dimmer but still clearly visible -
                    // the same EmbolicAgentSpec::radiopacity every other
                    // agent-dependent brightness decision in this file
                    // already uses.
                    const cn::EmbolicAgentSpec &spec = cn::embolicAgentSpec(m_emboli->agent());
                    const float baseDensity = 0.6f + 1.8f * spec.radiopacity;
                    // Grain amount derived from the SAME real viscosity
                    // value that already governs how each agent flows and
                    // branches (EmbolizationModel) - a viscous, cohesive,
                    // deliberately-controlled agent (Onyx, SQUID) forms a
                    // smooth, dense, uniform cast; a thin, fast-
                    // polymerizing one (Glue) forms a visibly irregular,
                    // patchy deposit, which is exactly the real clinical
                    // distinction between them, not an arbitrary per-agent
                    // knob. PHIL falls naturally in between.
                    const float grainAmount = std::clamp(1.3f - 1.3f * spec.viscosity, 0.1f, 1.1f);
                    m_program->setUniformValue("uForeignGrainAmount", grainAmount);

                    if (m_emboliBatchSet.elementCount > 0) {
                        m_program->setUniformValue("uForeignDensityScale", baseDensity);
                        QOpenGLVertexArrayObject::Binder bnd(&m_emboliBatchSet.vao);
                        glDrawElements(GL_TRIANGLES, m_emboliBatchSet.elementCount, GL_UNSIGNED_INT, nullptr);
                    }
                    if (m_emboliBatchGrowing.elementCount > 0) {
                        // Boosted density for the still-actively-filling
                        // leading edge - real embolic material at its own
                        // growing/precipitating front reads visibly
                        // different (fresher, denser-looking before fully
                        // set) than the already-solidified cast behind it.
                        // This is the actual "realistic release" signal:
                        // during active injection the growing tip is now
                        // visually distinct, not indistinguishable from a
                        // static final result. A link crossing the set
                        // threshold moves to the normal-density batch on
                        // the very next rebuild, so there is no visible
                        // seam or pop once it fully forms.
                        m_program->setUniformValue("uForeignDensityScale", baseDensity * 1.22f);
                        QOpenGLVertexArrayObject::Binder bnd(&m_emboliBatchGrowing.vao);
                        glDrawElements(GL_TRIANGLES, m_emboliBatchGrowing.elementCount, GL_UNSIGNED_INT, nullptr);
                    }
                }

                m_program->setUniformValue("uXrayIsVessel", 0);

                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glDisable(GL_BLEND);

                // Back to the widget's own target (NOT framebuffer 0 — a
                // QOpenGLWidget renders into its own internally-managed FBO,
                // and defaultFramebufferObject() is the Qt-provided way to
                // get that id back correctly) and the full-resolution
                // viewport, before pass 2 draws to the screen.
                glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
                glViewport(vp[0], vp[1], vp[2], vp[3]);

                // ── Pass 2: composite ────────────────────────────────────────
                ensureXrayQuad();
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, m_xrayFbo->texture());
                m_program->setUniformValue("uXrayTex", 0);
                m_program->setUniformValue("uViewportPx", QVector2D(float(vp[2]), float(vp[3])));
                m_program->setUniformValue("uXrayMode", 2);
                m_program->setUniformValue("uBackgroundColor", m_xrayTuning.backgroundColor);
                m_program->setUniformValue("uBoneColor",       m_xrayTuning.boneColor);
                m_program->setUniformValue("uBoneOpacity",     m_xrayTuning.boneOpacity);
                m_program->setUniformValue("uBoneAmbient",     m_xrayTuning.boneAmbient);
                m_program->setUniformValue("uBoneDiffuse",     m_xrayTuning.boneDiffuse);
                m_program->setUniformValue("uBoneSpecular",    m_xrayTuning.boneSpecular);
                m_program->setUniformValue("uMuscleColor",     m_xrayTuning.muscleColor);
                m_program->setUniformValue("uMuscleOpacity",   m_xrayTuning.muscleOpacity);
                m_program->setUniformValue("uMuscleAmbient",   m_xrayTuning.muscleAmbient);
                m_program->setUniformValue("uMuscleDiffuse",   m_xrayTuning.muscleDiffuse);
                m_program->setUniformValue("uMuscleSpecular",  m_xrayTuning.muscleSpecular);
                m_program->setUniformValue("uGammaG",          m_xrayTuning.gamma);
                m_program->setUniformValue("uContrastG",       m_xrayTuning.contrast);
                m_program->setUniformValue("uBrightnessG",     m_xrayTuning.brightness);
                m_program->setUniformValue("uBoneIntensityMult", m_xrayTuning.boneIntensityMult);
                m_program->setUniformValue("uSoftTissueMult",    m_xrayTuning.softTissueMult);
                m_program->setUniformValue("uBoneBrightness",    m_xrayTuning.boneBrightness);
                m_program->setUniformValue("uMuscleBrightness",  m_xrayTuning.muscleBrightness);
                // Fixed constant, not yet exposed in the X-Ray Calibration
                // dialog (that wasn't part of this request) - controls how
                // dark contrast/embolic material pools against the tissue
                // image beneath it (see foreignShape's own comment above,
                // in the composite pass). Raised from an earlier 0.55: that
                // value was tuned back when this channel BRIGHTENED the
                // image, and even at full concentration only reached a 55%
                // mix toward black — nowhere near dark enough once the
                // formula was corrected to darken instead. 0.85 lets a
                // fully-formed contrast column or embolic cast read as
                // genuinely near-black while still leaving a sliver of the
                // underlying tissue/vessel-wall texture faintly visible
                // through it, rather than a flat, textureless silhouette.
                m_program->setUniformValue("uForeignOpacity", 0.85f);
                // Bright/white contrast (per explicit request: "look like a
                // real radiopaque contrast bolus... bright/white initially")
                // only in plain Fluoro mode, with no embolic material
                // currently active - Road Map keeps its original dark
                // appearance (explicitly not to be changed), and so does
                // any moment embolic material is present, since it shares
                // this same accumulated channel with contrast and the two
                // cannot be coloured independently once summed - see
                // kContrastTarget's own comment in the composite pass.
                const bool emboliActive = m_emboli && m_emboli->active();
                m_program->setUniformValue("uContrastBrightMode",
                                           (!m_roadMapActive && !emboliActive) ? 1.0f : 0.0f);
                // Identity: the quad's vertices are already in clip space
                // (see ensureXrayQuad), so this draw must not be pushed
                // through the scene camera the way everything else is.
                m_program->setUniformValue("uViewProj", QMatrix4x4());

                glDisable(GL_DEPTH_TEST);
                glDepthMask(GL_FALSE);
                glDisable(GL_BLEND);   // opaque — this triangle covers every
                // pixel, replacing the frame's
                // earlier clear color entirely.
                {
                    QOpenGLVertexArrayObject::Binder bnd(&m_xrayQuadBatch.vao);
                    glDrawArrays(GL_TRIANGLES, 0, m_xrayQuadBatch.elementCount);
                }
                glBindTexture(GL_TEXTURE_2D, 0);

                // Restore the real camera matrix for everything drawn after
                // this (device, sheath, HUD overlay).
                m_program->setUniformValue("uViewProj", viewProjection());

                // Vessel — drawn as an ordinary translucent overlay ON TOP of
                // the composited X-ray image, not accumulated into the
                // Beer-Lambert projection above (a real angiogram only shows
                // the lumen once contrast dye is injected — Road Map / DSA —
                // so mixing it into the attenuation sum would be physically
                // wrong). This is the actual fix for "vessel not visible in
                // Fluoro": an earlier attempt only touched the non-fluoro
                // `else` branch below, which this `if (fluoro)` branch never
                // reaches at all — the vessel draw has to live here.
                //
                // Pass 2 above left depth test and blending disabled (the
                // composite quad is opaque and ignores depth); both are
                // re-enabled here so the vessel draws as normal 3D geometry
                // with correct self-occlusion and correct alpha blending,
                // then restored to the state the rest of paintGL expects.
                if (m_showWall && hasVessel) {
                    // uXrayMode is still 2 (composite) from Pass 2 above - the
                    // fragment shader's composite branch ignores uColor/
                    // uAmbient/uDiffuse entirely and samples uXrayTex instead,
                    // which is what was actually making the vessel render
                    // black rather than the grayish colour wallMaterial()
                    // sets. Reset to 0 (normal shading) so drawBatch's uColor
                    // is what actually reaches the framebuffer here.
                    m_program->setUniformValue("uXrayMode", 0);

                    glEnable(GL_DEPTH_TEST);
                    glDepthMask(GL_FALSE);
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                    Material wm = wallMaterial();   // ambient/specular/shininess/sheen only
                    wm.colour = QVector4D(m_xrayTuning.vesselColor, m_xrayTuning.vesselOpacity);
                    bool wallShaded = true;
                    if (m_roadMapActive) {
                        // Grayscale only, distinguished by intensity, not
                        // hue - this used to be a saturated red
                        // (#EA3C53), which is exactly the "colored, lit,
                        // 3D-shaded mesh" a real fluoroscopic roadmap never
                        // looks like. A real roadmap is a flat X-ray
                        // attenuation image: no hue, no shading, no
                        // highlight or gradient.
                        //
                        // Opacity is the mask-lifecycle-dependent value
                        // computed in updateRoadmapMask() (near-invisible
                        // baseline ~0.04 with no mask yet, boosted only
                        // for the single frame the mask is actually
                        // captured from) - not a fixed constant - see that
                        // function's own comment for the full mechanism.
                        const float grayLevel = 0.82f;
                        wm.colour   = QVector4D(grayLevel, grayLevel, grayLevel, m_roadmapVesselOpacity);
                        wm.ambient  = 1.0f;
                        wm.specular = 0.0f;
                        wallShaded  = false;   // flat attenuation, not a lit 3D object - see drawBatch's own shaded parameter
                    }
                    drawBatch(m_meshBatch, GL_TRIANGLES, wm, wallShaded, /*pulsatile=*/true);

                    glDepthMask(GL_TRUE);
                    glDisable(GL_BLEND);
                }
            }

            m_program->setUniformValue("uXrayMode", 0);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        } else {
            // Enable alpha blending for all body layers
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);

            // 1. Muscular — barely-there haze (same in all views per old project)
            if (hasMuscular) {
                // X-ray look: muscular = soft tissue silhouette (very faint, lighter colour)
                // Fluoro = DSA: same faint muscular haze
                const float musOp = (fluoro || m_dsaActive) ? 0.04f : 0.05f;
                m_program->setUniformValue("uColor", QVector4D(0.72f, 0.72f, 0.70f, musOp));
                m_program->setUniformValue("uSheen",    QVector3D(0.f, 0.f, 0.f));
                m_program->setUniformValue("uSpecular", 0.0f);
                m_program->setUniformValue("uShininess",1.0f);
                m_program->setUniformValue("uAmbient",  0.90f);
                m_program->setUniformValue("uDiffuse",  0.15f);
                m_program->setUniformValue("uShaded",   0);
                // Muscular.stl is anatomy, not vasculature: it never pulses.
                m_program->setUniformValue("uPulsatile", 0);
                QOpenGLVertexArrayObject::Binder bnd(&m_muscularBatch.vao);
                glDrawElements(GL_TRIANGLES, m_muscularBatch.elementCount, GL_UNSIGNED_INT, nullptr);
            }

            // 2. Skeletal — bone structure
            // Doc WhyFlattened: for 3D view use solid material (ambient=0.25, diffuse=0.75)
            //                   for fluoro keep X-ray flat (ambient=0.70, diffuse=0.30)
            if (hasSkeletal) {
                // 3D: 0.30 (clearly visible ghost), Fluoro: 0.08 (real X-ray = very faint bone)
                // X-ray look: bone = dark projection on white (high opacity, flat shading)
                // Fluoro: dark bone silhouette like real AP X-ray
                // 3D: lighter, more transparent ghost hint
                // Fluoro = DSA: same skeletal rendering
                const float skelOp = arterial3D ? 0.25f : 0.22f;  // DSA/fluoro: 0.22
                m_program->setUniformValue("uColor", QVector4D(0.86f, 0.86f, 0.84f, skelOp));
                if (arterial3D) {
                    // 3D: solid directional shading
                    m_program->setUniformValue("uAmbient",  0.25f);
                    m_program->setUniformValue("uDiffuse",  0.75f);
                    m_program->setUniformValue("uSheen",    QVector3D(0.86f, 0.86f, 0.84f));
                    m_program->setUniformValue("uSpecular", 0.25f);
                    m_program->setUniformValue("uShininess",30.0f);
                } else {
                    // DSA: medium flat
                    m_program->setUniformValue("uAmbient",  0.70f);
                    m_program->setUniformValue("uDiffuse",  0.30f);
                    m_program->setUniformValue("uSheen",    QVector3D(0.86f, 0.86f, 0.84f));
                    m_program->setUniformValue("uSpecular", 0.10f);
                    m_program->setUniformValue("uShininess",60.0f);
                }
                m_program->setUniformValue("uShaded",   1);
                // Skeletal.stl likewise: bone does not move with the cardiac cycle.
                m_program->setUniformValue("uPulsatile", 0);
                QOpenGLVertexArrayObject::Binder bnd(&m_skeletalBatch.vao);
                glDrawElements(GL_TRIANGLES, m_skeletalBatch.elementCount, GL_UNSIGNED_INT, nullptr);
            }

            // 3. Vessel — innermost
            // Old project: 3D opacity=0.30 (red), fluoro opacity=0.0 (hidden).
            // Now shown in Fluoro too (per request), at whatever m_wallOpacity
            // the caller set (QuadPanel defaults it to a visible value for the
            // fluoro VesselView) - the vessel no longer needs its own draw
            // call skipped, since wallMaterial() above already resolves the
            // right colour/opacity for whichever palette is active.
            if (m_showWall && !m_dsaActive) {
                Material wm = wallMaterial();
                bool wallShaded2 = true;
                if (m_roadMapActive) {
                    // Same fix, same reasoning as the Fluoro vessel draw
                    // above: grayscale only, unlit, mask-lifecycle-driven
                    // opacity - not a fixed red, fully-shaded constant.
                    const float grayLevel = 0.82f;
                    wm.colour   = QVector4D(grayLevel, grayLevel, grayLevel, m_roadmapVesselOpacity);
                    wm.ambient  = 1.0f;
                    wm.specular = 0.0f;
                    wallShaded2 = false;
                }
                // Sparse, animated dropout speckle - plain Fluoro only (not
                // Road Map, which has its own separately-tuned appearance
                // per explicit prior requirement not to touch it; not 3D/
                // DSA, which are not a live fluoroscopy feed at all).
                // uVesselGlitchTime reuses m_fluoroClock (already running,
                // already backing the Fluoro HUD's own TIME read-out)
                // rather than adding a second clock - divided down so the
                // hash pattern visibly re-salts a few times a second, not
                // once a millisecond (which would just look like flat
                // random per-frame noise instead of small evolving glitch
                // points).
                const bool vesselGlitchActive = fluoro && !m_roadMapActive;
                if (vesselGlitchActive) {
                    const float t = m_fluoroClock.isValid()
                    ? static_cast<float>(m_fluoroClock.elapsed()) * 0.004f : 0.f;
                    m_program->setUniformValue("uVesselGlitchAmount", 0.10f);
                    m_program->setUniformValue("uVesselGlitchTime", t);
                }
                // Wireframe mode (Mesh View): draw as lines, full opacity
                if (m_wireframe) {
                    // Wireframe: same colour+opacity as 3D view but rendered as mesh lines
                    // Draw filled surface first (gives depth), then wireframe lines on top
                    glPolygonOffset(1.f, 1.f);
                    glEnable(GL_POLYGON_OFFSET_FILL);
                    drawBatch(m_meshBatch, GL_TRIANGLES, wm, wallShaded2, /*pulsatile=*/true);   // filled, same transparency
                    glDisable(GL_POLYGON_OFFSET_FILL);
                    // Wireframe lines: same colour, slightly more opaque so edges read clearly
                    Material wire = wm;
                    wire.colour.setW(std::min(wm.colour.w() + 0.15f, 1.0f));
                    wire.ambient = 0.8f;  wire.specular = 0.f;
                    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                    glLineWidth(1.2f);
                    drawBatch(m_meshBatch, GL_TRIANGLES, wire, false, /*pulsatile=*/true);
                    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                } else {
                    drawBatch(m_meshBatch, GL_TRIANGLES, wm, wallShaded2, /*pulsatile=*/true);
                }
                if (vesselGlitchActive) m_program->setUniformValue("uVesselGlitchAmount", 0.0f);
            }

            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }

        // Device on top in all views
        if (m_deviceBatches) {
            Material tip, band, shaft, host;
            if (fluoro) {
                // Subtle gray, visible against the X-ray projection without
                // reading as a lit 3D model: specular defaults to 0.f
                // (Material's own default), so leaving it unset here — not
                // just setting colour to gray while keeping the normal
                // view's shiny highlight values — is what actually gives
                // the flat look. Bands (the radiopaque marker rings) stay
                // slightly brighter than the shaft, matching how they read
                // as distinct bright rings on a real fluoroscopy frame.
                //
                // A microcatheter renders darker still - a real 2-3 Fr
                // microcatheter's braid is thin enough that it reads as a
                // subtle, almost black line on a real fluoroscopy frame,
                // distinctly darker than a guide/diagnostic catheter's own
                // heavier braid - same flat, no-specular STYLE as every
                // other device here, just a darker set of the same tones,
                // per the explicit "subtle dark-black... consistent with
                // the existing Fluoro appearance" request.
                if (m_deviceIsMicrocatheter) {
                    tip.colour   = QVector4D(0.30f, 0.30f, 0.30f, 1.f);
                    band.colour  = QVector4D(0.38f, 0.38f, 0.38f, 1.f);
                    // Slightly translucent shaft (not the tip/band) - a
                    // real microcatheter's thin braid already reads as a
                    // faint line on fluoroscopy; full opacity here made it
                    // look like a solid drawn tube rather than a thin
                    // radiopaque line, per explicit request. The tip and
                    // marker band stay fully opaque so the distal tip
                    // remains unambiguously identifiable against the
                    // now-thinner-looking shaft.
                    shaft.colour = QVector4D(0.16f, 0.16f, 0.16f, 0.80f);
                } else {
                    tip.colour   = QVector4D(0.42f, 0.42f, 0.42f, 1.f);
                    band.colour  = QVector4D(0.50f, 0.50f, 0.50f, 1.f);
                    // Same reasoning as the microcatheter branch above -
                    // slightly translucent shaft only, tip/band untouched.
                    shaft.colour = QVector4D(0.30f, 0.30f, 0.30f, 0.80f);
                }
                if (m_roadMapActive) {
                    // Bright/white, unlit, per explicit request - the tip
                    // needs to read clearly against the darker vessel mask/
                    // bone, the way a real radiopaque catheter tip does
                    // under roadmap fluoroscopy. Overrides the general
                    // Fluoro tip colour above rather than replacing it,
                    // since that appearance is still correct for non-
                    // roadmap Fluoro use.
                    tip.colour = QVector4D(1.f, 1.f, 1.f, 1.f);
                }
                host.colour  = QVector4D(0.45f, 0.45f, 0.45f, 0.65f);
            } else {
                shaft.colour = QVector4D(m_deviceColor, 1.f);
                shaft.sheen = QVector3D(std::min(m_deviceColor.x() + 0.44f, 1.f),
                                        std::min(m_deviceColor.y() + 0.18f, 1.f),
                                        std::min(m_deviceColor.z() + 0.06f, 1.f));
                shaft.specular = 0.50f;
                shaft.shininess = 48.f;

                // Lightened tint of the shaft colour, not a fixed value -
                // this reproduces the exact look every device had before
                // per-device colour existed (solved so the catalog's own
                // default blue (0.36,0.72,0.94) lightens to very nearly the
                // old hardcoded tip colour (0.62,0.85,1.00)), while now
                // correctly following whatever colour a specific device
                // (Terumo Guide's sky-blue, Medtronic Micro's indigo)
                // actually specifies.
                constexpr float kTipLighten = 0.4f;
                const QVector3D white(1.f, 1.f, 1.f);
                const QVector3D tipTint = m_deviceColor * (1.f - kTipLighten) + white * kTipLighten;
                tip.colour = QVector4D(tipTint, 1.f);
                tip.sheen = QVector3D(1.f, 1.f, 1.f);
                tip.specular = 0.55f;
                tip.shininess = 40.f;

                // Marker bands stay a fixed near-white/neutral tone
                // regardless of device colour - a real radiopaque marker
                // band is a metallic ring, not tinted to match the
                // catheter's own body colour.
                band.colour = QVector4D(0.88f, 0.89f, 0.93f, 1.f);
                band.sheen = QVector3D(1.f, 1.f, 1.f);
                band.specular = 0.95f;
                band.shininess = 96.f;

                // Host devices first: the guide a microcatheter was telescoped
                // through is still in the patient and still visible, but it is no
                // longer the thing being steered, so it is drawn desaturated and
                // slightly translucent to keep the active device legible against it.
                host.colour    = QVector4D(0.55f, 0.60f, 0.68f, 0.75f);
                host.sheen     = QVector3D(0.75f, 0.80f, 0.88f);
                host.specular  = 0.30f;
                host.shininess = 32.f;
                host.ambient   = 0.20f;
            }
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            for (auto &hb : m_hostBatches) {
                if (!hb) continue;
                drawBatch(hb->shaft, GL_TRIANGLES, host, true);
                drawBatch(hb->band,  GL_TRIANGLES, host, true);
                drawBatch(hb->tip,   GL_TRIANGLES, host, true);
            }
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            drawBatch(m_deviceBatches->shaft, GL_TRIANGLES, shaft, true);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            drawBatch(m_deviceBatches->band,  GL_TRIANGLES, band,  true);
            drawBatch(m_deviceBatches->tip,   GL_TRIANGLES, tip,   true);

            Material balloon;
            const float ap = static_cast<float>(m_overlay.ballooonAppositionPct / 100.0);
            if (fluoro) {
                balloon.colour = QVector4D(0.85f, 0.85f, 0.85f, 0.30f + 0.28f * ap);
            } else {
                balloon.colour = QVector4D(0.90f, 0.95f, 1.00f, 0.30f + 0.28f * ap);
                balloon.sheen = QVector3D(1.f, 1.f, 1.f);
                balloon.specular = 0.95f;
                balloon.shininess = 90.f;
            }
            glDepthMask(GL_FALSE);
            drawBatch(m_deviceBatches->balloon, GL_TRIANGLES, balloon, true);
            glDepthMask(GL_TRUE);
        }

        // Embolic cast — non-fluoro/3D view only now. Fluoro rendering moved
        // to the DRR accumulation pipeline above (Pass 1's uXrayIsVessel==2
        // branch + Pass 2's foreignShape darkening pass): that is what makes
        // it read as genuine radiopaque signal WITHIN the X-ray projection -
        // a dark, radiopaque pool on top of the tissue image, the same way
        // contrast is - rather than a separately-lit 3D object alpha-blended
        // on top of the finished image. Drawing it AGAIN here in Fluoro
        // would double it up - once correctly via the DRR channel, once as
        // the old colored overlay -
        // so this block no longer runs there at all. The 3D view's own
        // appearance (colour, sheen, specular) is completely unchanged.
        if (!fluoro && m_emboli && m_emboli->active() &&
            (m_emboliBatchSet.elementCount > 0 || m_emboliBatchGrowing.elementCount > 0)) {
            const cn::EmbolicAgentSpec &spec = cn::embolicAgentSpec(m_emboli->agent());
            Material cast;
            cast.colour = QVector4D(spec.colour[0], spec.colour[1], spec.colour[2], spec.opacity);
            cast.sheen = QVector3D(spec.colour[0], spec.colour[1], spec.colour[2]);
            cast.specular = 0.25f;
            cast.shininess = 20.f;
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            // Same material for both - the 3D view's own appearance is
            // unchanged by this request, scoped to Fluoroscopy View only.
            drawBatch(m_emboliBatchSet, GL_TRIANGLES, cast, true);
            drawBatch(m_emboliBatchGrowing, GL_TRIANGLES, cast, true);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }

        // Contrast — non-fluoro/3D view only now, same reasoning as the
        // embolic cast above. Fluoro rendering moved entirely to the DRR
        // pipeline (Pass 1/2); this block's own appearance in the 3D view is
        // completely unchanged.
        if (!fluoro && m_contrast && m_contrast->phase() != cn::ContrastPhase::Idle) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            for (int b = 0; b < kContrastBuckets; ++b) {
                if (m_contrastBatches[b].elementCount == 0) continue;
                Material dye;
                dye.colour = QVector4D(0.85f, 0.85f, 0.70f, 0.35f);
                dye.sheen = QVector3D(1.f, 1.f, 0.95f);
                dye.specular = 0.6f;
                dye.shininess = 60.f;
                drawBatch(m_contrastBatches[b], GL_TRIANGLES, dye, true);
            }
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
    }
    // ── Introducer sheath (built by SheathManager, uploaded on setSheath) ──
    // Hidden entirely in Fluoro — the catheter above stays visible (in its
    // own gray palette) but the sheath is not part of the Fluoro
    // presentation at all. Every other view is unaffected: this is the
    // same condition as before with only `&& !fluoro` added.
    if (m_sheathVisible && !endo && !fluoro) {
        // Cannula: translucent PTFE. Drawn first so the opaque hub sits over it.
        Material cannula;
        cannula.colour    = QVector4D(0.86f, 0.88f, 0.90f, 0.55f);
        cannula.specular  = 0.75f;
        cannula.shininess = 64.f;
        cannula.ambient   = 0.28f;

        // Hub: opaque, colour-coded by French size. This is the part an
        // operator reads the sheath size off, so it is deliberately the most
        // saturated thing in the scene apart from the device itself.
        Material hub;
        hub.colour    = QVector4D(m_sheathHubColour, 1.f);
        hub.specular  = 0.45f;
        hub.shininess = 32.f;
        hub.ambient   = 0.22f;
        // An oversized sheath is flagged by desaturating the hub toward warning
        // red rather than by silently resizing the device.
        if (m_sheathOversized)
            hub.colour = QVector4D(0.85f, 0.18f, 0.14f, 1.f);

        Material valve;                               // silicone haemostasis cap
        valve.colour    = QVector4D(0.16f, 0.17f, 0.19f, 1.f);
        valve.specular  = 0.30f;
        valve.shininess = 24.f;
        valve.ambient   = 0.20f;

        Material arm;                                 // side-arm tubing + stopcock
        arm.colour    = QVector4D(0.80f, 0.82f, 0.85f, 0.85f);
        arm.specular  = 0.55f;
        arm.shininess = 40.f;
        arm.ambient   = 0.25f;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        drawBatch(m_sheathCannulaBatch, GL_TRIANGLES, cannula, true);
        drawBatch(m_sheathArmBatch,     GL_TRIANGLES, arm,     true);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        drawBatch(m_sheathHubBatch,   GL_TRIANGLES, hub,   true);
        drawBatch(m_sheathValveBatch, GL_TRIANGLES, valve, true);
    }

    m_program->release();

    paintOverlay();
}

// ----------------------------------------------------------------- overlay

void VesselView::paintOverlay() {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor amber(255, 196, 64);
    const QColor pale(214, 220, 226);
    const QColor alarm(255, 96, 96);
    // Labels sit back from their values, the way they do on the C-arm cluster:
    // the value is what is read, the label only says which value it is.
    const QColor dim(150, 156, 164);
    QFont mono("Titillium Web", 9);
    // Two sizes: a small label over a larger value, matching the C-arm console.
    QFont labelFont("Titillium Web", 8);
    labelFont.setStyleHint(QFont::SansSerif);
    QFont valueFont("Titillium Web", 11, QFont::Bold);
    valueFont.setStyleHint(QFont::SansSerif);
    const QFontMetrics fm2(labelFont), fmVal(valueFont);
    mono.setStyleHint(QFont::SansSerif);
    p.setFont(mono);

    if (m_showBranchMarkers && m_overlay.decisionPending && !m_overlay.options.empty()) {
        const QMatrix4x4 vp = viewProjection();
        for (const auto &o : m_overlay.options) {
            const QVector4D clip = vp * QVector4D(o.target, 1.f);
            if (clip.w() <= 0.f) continue;
            const float sx = (clip.x() / clip.w() * 0.5f + 0.5f) * width();
            const float sy = (1.f - (clip.y() / clip.w() * 0.5f + 0.5f)) * height();

            const QColor c = o.highlight ? amber : pale;
            p.setPen(QPen(c, o.highlight ? 2.0 : 1.2));
            p.setBrush(Qt::NoBrush);
            const qreal r = 16.0;
            p.drawEllipse(QPointF(sx, sy), r, r);
            p.drawLine(QPointF(sx - r - 6, sy), QPointF(sx - r, sy));
            p.drawLine(QPointF(sx + r, sy), QPointF(sx + r + 6, sy));
            p.drawText(QPointF(sx - r, sy - r - 6),
                       QString("%1  %2 mm  %3°")
                           .arg(o.label)
                           .arg(o.lumenMm, 0, 'f', 2)
                           .arg(o.takeoffDeg, 0, 'f', 0));
        }
        p.setPen(amber);
        // Dynamically reflects however many options actually exist - a
        // trifurcation draws a middle marker labeled "2" (see pushOverlay()'s
        // own labeling: index 0 = LEFT, last index = RIGHT, anything between
        // gets its plain 1-based number), but the help text used to always
        // say only "LEFT RIGHT", never mentioning that middle option or how
        // to reach it (Up-Arrow -> chooseStraightRequested -> the option
        // with the smallest takeoff angle) - an operator facing a real
        // trifurcation (e.g. the aortic arch: subclavian/carotid/back
        // toward the heart) had no on-screen indication a third choice was
        // even selectable, only its bare marker with no matching instruction.
        const QString junctionWord = (m_overlay.options.size() > 2) ? tr("JUNCTION") : tr("BIFURCATION");
        QString hint = tr("← LEFT   RIGHT →");
        if (m_overlay.options.size() > 2) hint += tr("   ↑ STRAIGHT");
        p.drawText(QRect(0, height() - 46, width(), 20), Qt::AlignHCenter,
                   junctionWord + tr(" — ") + hint);
    }

    if (m_overlay.deadEnd) {
        p.setPen(amber);
        p.drawText(QRect(0, height() - 46, width(), 20), Qt::AlignHCenter,
                   tr("DISTAL VESSEL — no further advance"));
    }

    // Shared row-spacing constants — pad/blockH/rowGap are also used by the
    // Fluoroscopy HUD block just below, which is why these stay even though
    // the telemetry column that used to be drawn here (ACCESS/DEVICE/BRANCH/
    // TURNS/DEPTH/LUMEN Ø/DEVICE Ø/CLEAR) is gone: that data is already
    // shown in full in the sidebar's Procedure Info/Device Status cards, so
    // drawing it a second time, directly on top of the anatomy, was a
    // genuine duplicate rather than something this view uniquely needed to
    // show.
    const int pad     = 10;
    const int lineH   = fm2.height();          // label line
    const int valueH  = fmVal.height();        // value line
    const int rowGap  = 7;                     // between one parameter and the next
    const int blockH  = lineH + valueH + rowGap;

    // ── Fluoroscopy HUD (FLUOROSCOPY header, LAO/CRAN/SID/MAG/FOV/FRAME,
    // kVp/mA/DAP/TIME, H/R/L/F orientation indicator) ───────────────────────
    // Shown in every palette now (3D/Fluoro/Mesh/DSA alike) - not gated to
    // Fluoro specifically any more, per the explicit request for one
    // standardized clinical overlay across every view rather than this HUD
    // only ever appearing in one of them. Still hidden during an
    // endoluminal fly-through, which is a first-person catheter view this
    // external console HUD has no place in.
    //
    // Deliberately reuses labelFont/valueFont/amber/pale/dim exactly as
    // already defined above rather than constructing anything new - every
    // piece of text in this whole overlay, old and new, therefore shares
    // the same font family, which is the actual point: a HUD reading as one
    // instrument, not several mismatched labels pasted together.
    //
    // LAO/CRAN are the console's real, live C-arm angle state (m_carmLAO/
    // m_carmCRAN — the exact values setCarmAngles() keeps synchronized
    // across every view). SID/MAG are now likewise real, live state
    // (m_hudSidCm/m_hudMagCm, set from CarmView's own sid()/magnification()
    // — see setDetectorInfo()) rather than a separately-hardcoded guess.
    // FOV/FRAME and kVp/mA/DAP are still fixed console defaults: this build
    // has no interactive field-of-view/generator model to draw real numbers
    // from for those specifically, so they are shown as typical clinical
    // values rather than invented ones that would imply a live reading that
    // does not exist. TIME is genuinely live, off this view's own
    // construction-time clock.
    if (m_mode != Mode::Endoluminal) {
        const QColor hudBg(0, 0, 0, 210);
        const QColor green(46, 204, 113);

        // "● FLUOROSCOPY" header pill, top-left.
        {
            const QString title = tr("FLUOROSCOPY");
            const QFontMetrics fmTitle(valueFont);
            const int titleW = fmTitle.horizontalAdvance(title) + 34;
            const int titleH = fmTitle.height() + 10;
            p.fillRect(QRect(0, 0, titleW, titleH), hudBg);
            p.setBrush(green);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(14, titleH / 2.0), 4.0, 4.0);
            p.setFont(valueFont);
            p.setPen(pale);
            p.drawText(QRect(24, 0, titleW - 24, titleH), Qt::AlignVCenter, title);
        }

        // LAO/CRAN/SID/MAG/FOV/FRAME — label left, value right after, same
        // stacked block shape the ACCESS/DEVICE/... column already uses,
        // just left-aligned instead of right-aligned.
        {
            struct HudRow { QString label, value; };
            const QVector<HudRow> hud = {
                                          {tr("LAO"),   tr("%1°").arg(m_carmLAO, 0, 'f', 0)},
                                          {tr("CRAN"),  tr("%1°").arg(m_carmCRAN, 0, 'f', 0)},
                                          {tr("SID"),   tr("%1 cm").arg(m_hudSidCm, 0, 'f', 0)},
                                          {tr("MAG"),   tr("%1 cm").arg(m_hudMagCm, 0, 'f', 0)},
                                          {tr("FOV"),   tr("25 cm")},
                                          {tr("FRAME"), tr("%1 fps").arg(m_hudFrameFps)},
                                          };
            const int labelW = 56;   // fixed column so every value lines up
            int hy = 34 + fm2.ascent();
            for (const auto &row : hud) {
                p.setFont(labelFont);
                p.setPen(pale);
                p.drawText(8, hy, row.label);
                p.setFont(valueFont);
                p.setPen(amber);
                p.drawText(8 + labelW, hy, row.value);
                hy += blockH;
            }
        }

        // kVp/mA/DAP/TIME — bottom-left, same label/value shape again.
        {
            const qint64 s = m_fluoroClock.isValid() ? m_fluoroClock.elapsed() / 1000 : 0;
            struct HudRow { QString label, value; };
            const QVector<HudRow> hud = {
                                          {tr("kVp"),  tr("78")},
                                          {tr("mA"),   tr("320")},
                                          {tr("DAP"),  tr("12.4 Gy·cm²")},
                                          {tr("TIME"), tr("%1:%2").arg(s / 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'))},
                                          };
            const int labelW = 40;
            int by = height() - pad - (blockH * hud.size() - rowGap) + fm2.ascent();
            for (const auto &row : hud) {
                p.setFont(labelFont);
                p.setPen(pale);
                p.drawText(8, by, row.label);
                p.setFont(valueFont);
                p.setPen(amber);
                p.drawText(8 + labelW, by, row.value);
                by += blockH;
            }
        }

        // I/J/L/K keyboard-navigation hint — bottom-right, small bordered
        // box. Genuinely accurate, not a placeholder: I/K/J/L are this
        // console's real Advance/Retract/Left/Right catheter navigation keys
        // (see buildUi()'s addNavKey() calls), laid out exactly the way they
        // act - I (advance) on top, K (retract) on the bottom, J (left) and
        // L (right) either side - rather than an anatomical Head/Foot/Right/
        // Left cube this build has no live camera-orientation tracking to
        // back honestly.
        {
            const int boxW = 76, boxH = 76;
            const QRect box(width() - boxW - 12, height() - boxH - 12, boxW, boxH);
            p.setPen(QPen(dim, 1));
            p.setBrush(hudBg);
            p.drawRoundedRect(box, 4, 4);

            p.setFont(valueFont);
            p.setPen(pale);
            p.drawText(box.adjusted(0, 4, 0, 0), Qt::AlignHCenter | Qt::AlignTop, tr("I"));
            p.drawText(box.adjusted(0, 0, 0, -4), Qt::AlignHCenter | Qt::AlignBottom, tr("K"));
            p.drawText(box.adjusted(6, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, tr("J"));
            p.drawText(box.adjusted(0, 0, -6, 0), Qt::AlignRight | Qt::AlignVCenter, tr("L"));
            p.setBrush(green);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(box.center()), 5.0, 5.0);
        }
    }

    if (m_overlay.wallContact) {
        // Feedback the operator cannot miss while looking at the vessel rather
        // than at the read-outs: the frame itself reddens with the severity.
        const float sev = std::clamp(m_overlay.contactSeverity, 0.15f, 1.f);
        const int bandPx = static_cast<int>(6 + 16 * sev);
        QColor edge = alarm;
        edge.setAlphaF(0.20 + 0.55 * static_cast<double>(sev));
        QPen border(edge, bandPx);
        p.setPen(border);
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRect(bandPx / 2, bandPx / 2, width() - bandPx, height() - bandPx));

        p.setPen(alarm);
        p.drawText(QRect(0, pad, width(), 20), Qt::AlignHCenter,
                   tr("WALL CONTACT   %1%").arg(static_cast<int>(sev * 100)));
    }

    // Small, unobtrusive status line - explicit small font rather than
    // whatever size was already active on the painter (which read as
    // large/prominent, not the "small unobtrusive indicator" this is
    // meant to be). ENDOLUMINAL still shows as plain text (a genuine mode
    // label, not a status dot), but the ordinary Reference-mode case drops
    // "REFERENCE" entirely per explicit request, showing just a green dot
    // + "FOLLOWING TIP" when camera-follow is active, or nothing at all
    // when it isn't.
    QFont statusFont("Titillium Web", 9);
    statusFont.setStyleHint(QFont::SansSerif);
    p.setFont(statusFont);
    const QFontMetrics statusFm(statusFont);

    QString modeTag;
    bool showDot = false;
    if (m_mode == Mode::Endoluminal) {
        modeTag = tr("ENDOLUMINAL");
    } else if (m_cameraFollows) {
        modeTag = tr("FOLLOWING TIP");
        showDot = true;
    }
    if (m_overlay.moving) modeTag += (modeTag.isEmpty() ? QString() : tr("   ")) + tr("▶ ADVANCING");

    if (!modeTag.isEmpty()) {
        const int dotDiameter = 6;
        const int gap = showDot ? 6 : 0;
        const int textW = statusFm.horizontalAdvance(modeTag);
        const int totalW = (showDot ? dotDiameter + gap : 0) + textW;
        const int startX = (width() - totalW) / 2;
        const int baselineY = height() - 16;

        if (showDot) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(46, 204, 113));   // #2ECC71 — same green as every other active/status indicator in this console
            p.drawEllipse(QRect(startX, baselineY - dotDiameter, dotDiameter, dotDiameter));
        }
        p.setPen(QColor(255, 255, 255, 130));
        p.drawText(startX + (showDot ? dotDiameter + gap : 0), baselineY, modeTag);
    }

    if (m_mode == Mode::Endoluminal) {
        p.setPen(QPen(QColor(255, 255, 255, 46), 1));
        const QPointF c(width() / 2.0, height() / 2.0);
        p.drawLine(c + QPointF(-14, 0), c + QPointF(-4, 0));
        p.drawLine(c + QPointF(4, 0), c + QPointF(14, 0));
        p.drawLine(c + QPointF(0, -14), c + QPointF(0, -4));
        p.drawLine(c + QPointF(0, 4), c + QPointF(0, 14));
    }
}

// ------------------------------------------------------------------- input

void VesselView::mousePressEvent(QMouseEvent *e) {
    // Alt+left-click: vessel surface picking (Layer 2 — click-to-refine sheath)
    if (m_pickingEnabled && e->button() == Qt::LeftButton &&
        (e->modifiers() & Qt::AltModifier) && m_graph) {
        // Unproject click to world-space ray using the current view/projection.
        const QPointF sp  = e->position();
        const float   W   = float(width()), H = float(height());
        const float   asp = H > 0.f ? W / H : 1.f;

        QMatrix4x4 proj, view;
        // Same FOV the frame was rendered with, or the unprojected ray misses
        // what the operator actually clicked on.
        proj.perspective(fovDegrees(), asp, m_modelScale * 0.001f, m_modelScale * 8.f);
        view.lookAt(cameraPosition(),
                    (m_cameraFollows && m_device) ? m_followTarget : m_target,
                    QVector3D(0, 0, 1));
        const QMatrix4x4 vpInv = (proj * view).inverted();

        // NDC coordinates of click
        const float nx =  (sp.x() / W) * 2.f - 1.f;
        const float ny = -(sp.y() / H) * 2.f + 1.f;

        // Unproject near and far plane to get ray
        const QVector4D n4 = vpInv * QVector4D(nx, ny, -1.f, 1.f);
        const QVector4D f4 = vpInv * QVector4D(nx, ny,  1.f, 1.f);
        const QVector3D nearPt(n4.x()/n4.w(), n4.y()/n4.w(), n4.z()/n4.w());
        const QVector3D farPt (f4.x()/f4.w(), f4.y()/f4.w(), f4.z()/f4.w());
        const QVector3D ray = (farPt - nearPt).normalized();

        // Linear scan: find graph node nearest to this ray
        float   bestD = std::numeric_limits<float>::max();
        int32_t bestN = -1;
        for (int32_t i = 0; i < (int32_t)m_graph->nodeCount(); ++i) {
            const auto &nd = m_graph->nodes()[i];
            const QVector3D p(nd.position.x, nd.position.y, nd.position.z);
            const QVector3D cp = p - nearPt;
            const float t = QVector3D::dotProduct(cp, ray);
            if (t < 0.f) continue;   // behind camera
            const float d = (p - (nearPt + ray * t)).length();
            if (d < bestD) { bestD = d; bestN = i; }
        }

        // Tolerance = 5% of camera distance — generous enough for thin tubes
        const float tol = (m_cameraFollows ? m_followDistance : m_distance) * 0.05f;
        if (bestN >= 0 && bestD < tol) {
            const auto &nd = m_graph->nodes()[bestN];
            emit vesselSurfacePicked(QVector3D(nd.position.x,
                                               nd.position.y,
                                               nd.position.z));
            e->accept();
            return;
        }
    }
    m_lastMouse = e->position().toPoint();
    setFocus(Qt::MouseFocusReason);
}

void VesselView::mouseMoveEvent(QMouseEvent *e) {
    const QPoint d = e->position().toPoint() - m_lastMouse;
    m_lastMouse = e->position().toPoint();

    if (m_mode == Mode::Endoluminal) {
        // Inside the vessel the only free axis is roll about the direction of
        // travel - exactly the wrist rotation the operator has.
        if (e->buttons() & Qt::LeftButton) {
            m_endoUp = QQuaternion::fromAxisAndAngle(m_endoForward, d.x() * 0.5f)
            .rotatedVector(m_endoUp);
            emit cameraChanged();
            update();
        }
        return;
    }
    if ((e->buttons() & Qt::LeftButton) && !m_frontalLock) {
        m_azimuth -= d.x() * 0.15f;   // slowed — was 0.4, too fast
        m_elevation = std::clamp(m_elevation + d.y() * 0.15f, -89.f, 89.f);
        emit cameraChanged();
        update();
    } else if (e->buttons() & (Qt::MiddleButton | Qt::RightButton)) {
        const QVector3D fwd = cameraForward();
        const QVector3D right = QVector3D::crossProduct(fwd, QVector3D(0, 0, 1)).normalized();
        const QVector3D up = QVector3D::crossProduct(right, fwd);
        const float k = (m_cameraFollows ? m_followDistance : m_distance) * 0.0015f;
        m_target += -right * d.x() * k + up * d.y() * k;
        update();
    }
}

void VesselView::wheelEvent(QWheelEvent *e) {
    const float steps = e->angleDelta().y() / 120.f;
    if (m_mode == Mode::Endoluminal) {
        m_endoFov = std::clamp(m_endoFov - steps * 3.f, 30.f, 110.f);
    } else if (m_cameraFollows && m_device) {
        // Scales the auto-zoom rather than replacing it, so framing still
        // adapts as the device moves between a trunk and a distal branch.
        m_followZoom = std::clamp(m_followZoom * std::pow(0.85f, steps), 0.15f, 8.f);
        startCameraEase();
    } else {
        m_distance = std::max(m_distance * std::pow(0.88f, steps), 1e-4f);
    }
    update();
}

void VesselView::keyPressEvent(QKeyEvent *e) {
    switch (e->key()) {
    case Qt::Key_PageUp:   emit advanceRequested();     e->accept(); return;
    case Qt::Key_PageDown: emit retractRequested();     e->accept(); return;
    case Qt::Key_Left:     emit chooseLeftRequested();  e->accept(); return;
    case Qt::Key_Right:    emit chooseRightRequested(); e->accept(); return;
    case Qt::Key_Up:       emit chooseStraightRequested(); e->accept(); return;
    case Qt::Key_Space:    emit balloonToggleRequested(); e->accept(); return;
    default: break;
    }
    QOpenGLWidget::keyPressEvent(e);
}

void VesselView::setCarmAngles(float lao, float cran) {
    // ── One C-arm state, applied identically in every view ───────────────────
    //
    // This used to accumulate the DELTA since the last call into m_azimuth and
    // m_elevation. That makes the orbit path-dependent: a view that missed a
    // call, was constructed later, or started from a different framing ends up
    // at a different orientation for the same rig angles, and the views drift
    // apart with no way back. It is precisely how separate per-view C-arm
    // positions came about.
    //
    // The angles are now stored as an ABSOLUTE offset and added to the orbit
    // where the camera basis is built. Every view given the same (lao, cran)
    // therefore shows the same C-arm orientation by construction, while keeping
    // its own zoom, pan and target — which the delta scheme was trying to
    // protect and this preserves anyway.
    if (std::fabs(lao - m_carmLAO) < 1e-4f && std::fabs(cran - m_carmCRAN) < 1e-4f)
        return;

    m_carmLAO  = lao;
    m_carmCRAN = cran;

    update();
    // Deliberately NOT emit cameraChanged() here. CarmController already
    // applies the rig's angle directly to every linked view in its own loop
    // (rotateLinkedCamerasIncremental) — synchronizing the angle across
    // views does not need the generic cameraChanged -> syncCameraFrom path
    // at all, and that path is actively wrong for a pure C-arm move: this
    // was the actual cause of the vessel view snapping to the stale
    // sheath/access-site target on every W/A/S/D press.
    //
    // The mechanism: syncCameraFrom() calls setCamera(), which
    // unconditionally sets m_cameraFollows = false and copies the SOURCE
    // view's static m_azimuth/m_target fields. Those static fields are only
    // ever written by an explicit re-framing call (focusOn() at sheath
    // placement, for instance) - while camera-follow mode is active, the
    // camera actually shown each frame is computed dynamically from the
    // device's current position and never written back into those static
    // fields. So the static fields silently go stale the moment the operator
    // starts navigating - and every C-arm press was copying that stale,
    // long-out-of-date target across via the emit above, while also killing
    // follow mode on whichever view received it, exposing the stale target
    // for real. Removing the emit stops a pure orientation change from
    // touching target/distance/follow-state on ANY view at all - it now
    // only ever changes what setCarmAngles() itself changed: the rig angle.
    // Mouse orbit/zoom and the explicit re-framing functions still go
    // through setCamera() directly and still sync correctly; only this one
    // spurious side channel is gone.
}

void VesselView::setBodyMeshes(const QString &muscularPath, const QString &skeletalPath) {
    m_muscularPath  = muscularPath;
    m_skeletalPath  = skeletalPath;
    m_bodyMeshDirty = true;
    // Trigger immediate rebuild if GL is already up.
    // Per the alignment doc: different margins for different layers so they nest:
    // skeletal (inner) margin=1.04, muscular (outer) margin=1.05.
    if (isValid()) {
        makeCurrent();
        rebuildBodyMeshBatch(m_skeletalPath, m_skeletalBatch, m_skeletalAlign);
        rebuildBodyMeshBatch(m_muscularPath, m_muscularBatch, m_muscularAlign);
        m_bodyMeshDirty = false;
        doneCurrent();
    }
    update();
}

// ─────────────────────────────────────────────────────────────────────────────
// Roadmap mask capture — real clinical roadmapping, not a live-rendered
// overlay. See RoadmapState's own comment (VesselView.h) and the individual
// function comments below for the full mechanism.
// ─────────────────────────────────────────────────────────────────────────────
void VesselView::captureRoadmapMask() {
    if (!isValid() || !m_mesh) return;

    // Boost the LIVE vessel to a visible grayscale level for exactly the
    // one frame grabFramebuffer() is about to force - this is the moment a
    // real roadmap mask is taken: contrast momentarily fills the vessel,
    // the frame is captured, then the live vessel returns to its near-
    // invisible baseline while the CAPTURED frame becomes the roadmap
    // background from here on, not the live 3D mesh.
    const float savedOpacity = m_roadmapVesselOpacity;
    m_roadmapVesselOpacity = 0.90f;

    // grabFramebuffer() (the same mechanism CineViewStudio already uses
    // for frame capture) forces a fresh paintGL() with the boosted
    // opacity in effect, then reads the result back as a QImage - this is
    // the ONE moment the vessel is actually drawn from live 3D geometry;
    // every frame after this reads back the frozen result instead.
    const QImage frame = grabFramebuffer();

    m_roadmapVesselOpacity = savedOpacity;   // revert the LIVE vessel immediately, regardless of capture success

    if (frame.isNull()) return;

    m_roadmapMaskImage = frame;
    m_roadmapState = RoadmapState::Masked;
    update();
}

void VesselView::clearRoadmapMask() {
    m_roadmapMaskImage = QImage();
    m_roadmapState = RoadmapState::NoMask;
    m_roadmapVesselOpacity = 0.04f;
    update();
}

void VesselView::drawRoadmapMaskLayer() {
    if (m_roadmapState == RoadmapState::NoMask || m_roadmapMaskImage.isNull()) return;
    // A complete, self-contained QPainter session — begun and fully ended
    // here, before any raw GL shader work in paintGL() runs — rather than
    // a custom textured-quad GL shader pass. Stretched to the widget's
    // current size deliberately: the mask is a frozen SCREEN-SPACE image,
    // so it fills the viewport exactly as captured regardless of any
    // camera change since — that fixed relationship between the mask and
    // the screen, even as the live layer drawn on top of it keeps
    // responding normally to camera/C-arm changes, is what actually
    // demonstrates real roadmap misregistration when the operator rotates
    // without re-masking.
    QPainter p(this);
    p.drawImage(rect(), m_roadmapMaskImage);
    p.end();
}