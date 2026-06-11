# Test Harness Plan

## Problem
The engine uses random ball positions, random forces, and auto-cycling cameras — making visual regression testing impossible. We need a deterministic, data-driven test harness so we can capture reference screenshots per commit during the FFP→shader migration.

## Approach
Introduce a `.scene` file format that fully describes a scene (cameras, balls, physics settings). The engine loads a scene file via `--scene <path>` command line. No args = current "crazy mode" (300 random balls). Each camera in a scene produces a separate screenshot for regression comparison.

## Scene File Format
```
# Lines starting with # are comments. Blank lines ignored.
physics off
frames 1

camera main 600 80 600  580 24 580  0 1 0
ball test_ball 580 24 580  3.0 50.0 10.0 0.8
ball roller 550 100 550  2.0 30.0 10.0 0.6  500 0 300  1 1 1
```

Fields:
- `physics` — `on` or `off` (default: on)
- `frames` — integer frame count before hold, or `unlimited` (default: unlimited)
- `camera` — name posX posY posZ viewX viewY viewZ upX upY upZ (max 3)
- `ball` — name posX posY posZ radius mass moment restitution [forceX forceY forceZ forcePosX forcePosY forcePosZ]

Camera names are hashed at runtime via `HashStr()`. Max 3 cameras (matches `TOTAL_CAMERA_COUNT`).

## Implementation Steps

### 1. Scene Parser (`SkullbonezTestScene.h/.cpp`)
- `SceneCamera` struct: name, position, view, up vectors
- `SceneBall` struct: name, position, radius, mass, moment, restitution, force, forcePos
- `TestScene` class: physics flag, frame count, vectors of cameras/balls
- Static `LoadFromFile(const char* path)` — fopen_s, line-by-line, sscanf_s
- Validation: at least 1 camera, max 3 cameras

### 2. SkullbonezRun Scene Mode
- Constructor takes `const char* scenePath = nullptr`
- New members: `isSceneMode`, `scenePath`, `targetFrameCount`, `currentFrame`
- New private methods: `SetUpGameModelsFromScene(TestScene&)`, `SetUpCamerasFromScene(TestScene&)`
- `Initialise()`: branches on `isSceneMode` for model/camera setup
- `Run()`: if scene mode, skip `UpdateLogic` when physics off, count frames, hold after target reached
- `SetViewingOrientation()`: if scene mode, don't auto-cycle cameras — stay on first camera

### 3. Command Line Parsing (`SkullbonezInit.cpp`)
- Parse `szCmdLine` for `--scene <path>`
- Pass path to `SkullbonezRun` constructor
- No args = nullptr = current random behavior

### 4. Test Scene Files
- `SkullbonezData/scenes/water_ball_test.scene` — 1 ball partially in water at heightmap center, physics off, 1 frame
- `SkullbonezData/scenes/physics_roll.scene` — 1 ball on hill with force, 2 cameras, physics on, ~300 frames

### 5. Project Files
- Add TestScene .h/.cpp to vcxproj and filters

### 6. Render Test Skill
- `Skills/render-test/skill.md` — launches with `--scene`, captures screenshots, stores per commit hash

## Constraints
- Camera array is fixed at `TOTAL_CAMERA_COUNT` (3) — scene files limited to 3 cameras
- `HashStr()` is constexpr but callable at runtime for scene camera names
- `CameraCollection` singleton resets via `Destroy()` between runs
- Must maintain zero warnings at /W4
