# EmboNav — Endovascular Navigation Console

Combined project: new project's Qt-free geometry/navigation core + old project's clinical UI.

## Layout

```
┌──────────────────────┬───────┬──────────────────────────────────────────────┐
│  C-arm OpenGL view   │  3D   │          Vessel view (QuadPanel)             │
│  (CarmView)          │ Fluro │  switches: 3D / Fluoro / Mesh / DSA          │
│  LAO 0.0°  CRAN 0.0°│ Mesh  ├──────────────────────────────────────────────┤
│  A/D=LAO W/S=CRAN…   │  DSA  │  Contrast Injection  |  Road Map             │
│                      │───────│  Embolisation step label                     │
│  ECG / ABP / Pleth   │Sheath │  [Squid] [Onyx] [DMS] [Glue] [Gel]          │
│                      │Cath   │  Status label                                │
└──────────────────────┴───────┴──────────────────────────────────────────────┘
                               Right dock (procedure panel):
                                 ▲ Advance  ▼ Retract  ◄ Left  Right ►
                                 Balloon toggle
                                 Access site radio buttons
                                 Device status readouts
                                 Calibration  |  Procedure log
```

## What came from each parent project

### From the old project (Embo)
- Dark stylesheet (exact `#2A2A2F` background, `#3A3A42` buttons, etc.)
- C-arm colours: head `0.88/0.89/0.92`, stand `0.75/0.77/0.80`, bed `0.96/0.82/0.68`,
  xray panel `0.25/0.55/0.95` at 0.15 opacity (translucent blue detector)
- C-arm background `0.05/0.05/0.09`
- Sheath Selection dialog (Left/Right side + Femoral/Radial/Carotid)
- Catheter Selection popup menu (Guide / Micro / Guide Wire) + `showDeviceSpecDialog()`
  with exact same device lists (Cobra C2, Marathon, Synchro-14, tip shapes, etc.)
- Contrast Injection dialog (Slow/Medium/Fast flow, Low/Medium/High volume)
- Road Map / DSA View toggles with correct opacity states
- ECG widget, embolisation step label, embolic agent buttons
- Procedure log, device status readouts, angle strip, key hints

### From the new project (catheter-nav) — navigation core
- `StlMesh` — loads `simplified_branches_.stl` at runtime, no baked paths
- `CenterlineExtractor` — geodesic level-set, no VTK
- `CenterlineGraph` — Dijkstra routing, branch labelling
- `AccessSiteResolver` — geometric femoral/radial detection
- `NavigationController` — live walk, stop at junction, retract to un-commit
- `CatheterModel` — follow-the-leader chain, wall clamp, balloon
- `VesselView` — OpenGL 3.3, reference + endoluminal cameras
- `CarmView` — Qt OpenGL C-arm renderer (replaces VTK actors)
- `QuadPanel` — stacked 3D/Fluoro/Mesh/DSA panes

### Body mesh alignment (Skeletal + Muscular inside vessel)
`loadBodyMeshes()` searches for `Muscular.stl` and `Skeletal.stl` next to
the vessel STL, then `.`, then `Data/3D`. When found, `setBodyMeshes()` is
called on all four vessel views so each can load and align them.

## Build

Requirements: CMake ≥ 3.20, C++17, Qt 6.4+ (Widgets, OpenGL, OpenGLWidgets)

```
./build.sh              # GUI + CLI, Release
./build.sh --core-only  # CLI only (no Qt)
./build.sh --debug      # Debug
```

Delete `Build/` and re-run CMake if you replace `CMakeLists.txt`.

## Data files

Place next to the executable (or in `Data/3D/`):
- `simplified_branches_.stl`  ← primary vessel
- `Muscular.stl`              ← soft tissue body layer
- `Skeletal.stl`              ← skeleton layer
- `carm_head.stl`, `carm_stand.stl`, `carm_bed.stl`, `carm_xray.stl`

## Controls

| Key | Action |
|-----|--------|
| Page Up / Page Down | Advance / retract device |
| ← / → | Choose left / right limb at bifurcation |
| Space | Inflate / deflate balloon |
| A / D | C-arm LAO / RAO 5° |
| W / S | C-arm CRAN / CAUD 5° |
| R | Reset C-arm |
| F6 | Toggle centerline |
| F7 | Camera follows device |

## File structure

```
EmboNav/
├── CMakeLists.txt
├── build.sh
├── README.md
├── Data/3D/            ← STL assets (not tracked by git)
├── src/
│   ├── main.cpp
│   ├── core/           ← Qt-free geometry + navigation (embonav_core)
│   │   ├── Vec3.h
│   │   ├── StlMesh.*
│   │   ├── MeshDistance.h
│   │   ├── CenterlineGraph.*
│   │   ├── CenterlineExtractor.*
│   │   ├── AccessSites.*
│   │   ├── NavigationController.*
│   │   └── CatheterModel.*
│   └── ui/             ← Qt6 UI layer
│       ├── MainWindow.*
│       ├── VesselView.*
│       ├── CarmView.*
│       ├── QuadPanel.*
│       ├── CineViewStudio.*
│       ├── ECGWidget.*
│       └── FluoroOverlay.*
└── tools/
    └── centerline_cli.cpp
```
