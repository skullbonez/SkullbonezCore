# Plan: New Water System

## Problem
The current water rendering uses a 2-triangle quad mesh translated into multiple overlapping copies (±5000–6300 units). These copies massively overlap each other and the main water plane, causing z-fighting, near-field blue blobs, and purple triangle edge artifacts at the horizon.

## Approach
Replace the entire water system with a single large subdivided grid mesh with vertex-animated ripples via a dedicated `water.vert` / `water.frag` shader pair. Remove all 4 deep water translated copies.

## Design Decisions
- **Mesh**: 64×64 quad grid, position-only (no normals, no texcoords), spanning ±`FRUSTUM_CLIP_FAR_QTY` in X and Z
  - 64×64×6 = 24,576 vertices × 3 floats = ~288 KB — acceptable
- **Waves**: Two layered sine waves for a natural calm-ocean feel
  - Wave 1: amplitude 1.5, frequency 0.04, speed 1.2 (X axis)
  - Wave 2: amplitude 1.0, frequency 0.06, speed 0.8 (Z axis)
- **Color**: Deep ocean blue — `(0.05, 0.15, 0.42, 0.65)` semi-transparent, no texture
- **Time**: Use `cSimulationTimer.GetTimeSinceLastStart()` from `SkullbonezRun`, passed as `float` to `RenderFluid`
- **View matrix**: Pass `baseView` directly to `RenderFluid` — removes the `glGetFloatv` FFP readback

## Tasks

| ID | File | Change |
|----|------|--------|
| `w1` | `SkullbonezData/shaders/water.vert` | New shader — MVP + sine-wave Y displacement + `uTime` |
| `w2` | `SkullbonezData/shaders/water.frag` | New shader — flat `uColorTint` output, no texture |
| `w3` | `SkullbonezWorldEnvironment.h` | Update `RenderFluid(proj)` → `RenderFluid(view, proj, time)` |
| `w4` | `SkullbonezWorldEnvironment.cpp` | `BuildFluidMesh`: replace 2-tri quad with 64×64 position-only grid; `RenderFluid`: pass view+time, use water shader, deep blue tint, remove `glGetFloatv` |
| `w5` | `SkullbonezRun.cpp` | Remove 4 deep water translated copy blocks; update `RenderFluid` call to pass `baseView`, `proj`, and elapsed time |

## Pipeline
After all changes: build (0 errors, 0 warnings) → render test → update baselines → perf test → smoke test → commit.
