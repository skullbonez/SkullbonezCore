# Tornado Visualization Plan

Status: planning draft
Created: 2026-06-23
Scope: DX12 tornado visual effect, render pass ordering, UI/runtime controls, scene defaults, validation
Implementation status: plan only, no code changes in this pass

## Goal

Give active tornado mode a readable in-world visual: a rotating funnel that sells wind, lift, and ground dust while keeping boxes and other captured items visible.

The visual should behave like a force-field suggestion, not a dense smoke column. The player should see:

- the tornado footprint and height,
- the direction of swirl and lift,
- objects being carried through the column,
- enough dust and debris motion to feel energetic,
- no opaque fog wall hiding the fun part.

## Current Read

Tornado behavior already has a runtime and physics path:

- `Physics::TornadoFieldConfig` owns enable state, center, radius, height, acceleration, ejection, and the existing debug-vector toggle.
- `RunRuntimeSettings::tornadoField` stores the live runtime config.
- `Run::SyncTornadoFieldToPhysics()` sends that config to `GameModelCollection`.
- `Physics::TornadoField::RenderVectors()` draws the existing velocity-field diagnostic through `Gfx().DrawLinesColored()`.
- `Run::DebugOverlayPass` draws tornado field vectors after world rendering when `visualizeVelocityField` is enabled.

That vector view is useful for debugging but is not the production visual. It renders opaque RGB lines at full opacity and with depth disabled in the current DX12 debug-line path, so it can sit on top of boxes/items and make the scene harder to read.

## Visual Contract

The production tornado should use these rules:

1. Opaque scene objects win visually.
   Boxes, balls, and carried objects render normally and write depth before the tornado visual pass. Tornado visuals use depth test on and depth write off.

2. The center stays open.
   Do not render a filled cone, cylinder, or volumetric fog slab. The column is described by sparse spiral ribbons, edge particles, and a ground skirt.

3. Alpha stays low.
   First target values should sit around `0.08` to `0.18` for the shell ribbons and `0.12` to `0.24` for ground dust. Avoid any single layer above `0.30` unless it is tiny and short-lived.

4. Motion carries the effect.
   Rotate spiral phases and particle/debris positions over time instead of raising opacity. A few moving streaks read better than a large translucent wall.

5. Debug vectors remain separate.
   Keep `visualizeVelocityField` as a diagnostic overlay. Add a separate production visual toggle so artists/users can have a nice tornado without field arrows.

## Target Architecture

### Runtime Settings

Add render-only tornado visual settings outside physics behavior:

```cpp
struct TornadoVisualSettings
{
    bool enabled = true;
    bool autoEnableWithTornado = true;
    float shellAlpha = 0.14f;
    float dustAlpha = 0.20f;
    float ribbonWidth = 5.5f;
    int ribbonCount = 7;
    int ribbonSegments = 48;
    int particleCount = 96;
    float rotationSpeed = 1.25f;
};
```

Recommended owner:

- Put this in the run/render settings area near `RunRuntimeSettings`, not inside `Physics::TornadoFieldConfig`.
- Pass the existing `Physics::TornadoFieldConfig` to the visual pass as read-only shape input.

Reasoning:

- Art tuning should not alter physics determinism or replay hashes.
- The force field can be off while a visual preview is off, but when tornado mode is enabled the default should show visuals automatically.
- Existing `visualizeVelocityField` can remain in the physics config because it already exists, but new art controls should not expand physics-owned state unless implementation later needs scene/replay persistence.

### Render Pass

Add a `TornadoVisualPass` owned by `Run`, beside the current extracted passes in `Run.h` / `RunPasses.cpp`.

Preferred ordering in `Run::DrawPrimitives()`:

1. shadow pass,
2. sky/reflection setup,
3. opaque object pass,
4. terrain pass,
5. water pass,
6. tornado visual pass,
7. debug transparent object pass or replay focus fade pass,
8. debug overlay pass,
9. cinematic post/tonemap.

Why this order:

- Opaque objects and terrain have already written depth, so tornado geometry behind them is hidden.
- Tornado in front of an object blends lightly over it, preserving readability.
- Debug transparent bodies still draw after the tornado, which keeps debugging modes readable.
- Debug vectors remain in `DebugOverlayPass`, so they stay clearly diagnostic.

### Renderer Backend Surface

Do not build the production tornado on `DrawLinesColored()`. It lacks alpha and uses the debug-line PSO.

Add a small transient colored triangle path to `IRenderBackend`, implemented in DX12:

```cpp
virtual void DrawTransientColoredTriangles(
    const float* data,
    int vertexCount,
    const float* viewProjMatrix16 );
```

Suggested vertex layout:

```text
position.xyz
color.rgba
```

Suggested shader:

- `SkullbonezData/shaders/tornado_fx.hlsl`
- `b0`: `uViewProj`
- VS transforms world position.
- PS returns vertex color alpha.

Suggested DX12 state:

- topology: triangle list,
- cull: none,
- depth test: enabled,
- depth write: disabled,
- blend: `SrcAlpha`, `OneMinusSrcAlpha`,
- render target: current world target,
- no textures for the first slice.

Keeping the first slice untextured avoids descriptor churn and makes validation artifacts easier to reason about.

## Geometry Design

### Spiral Ribbons

Generate several thin helical ribbon strips around the tornado shell.

Shape:

- radius tapers from about `0.32 * field.radius` at the base to `0.78 * field.radius` in the upper column,
- each ribbon climbs from `center.y` to `center.y + height`,
- each ribbon uses a phase offset so the shell has gaps,
- ribbon width should be small enough that most of the column remains empty.

CPU generation:

```cpp
for each ribbon:
    phase = ribbonIndex * twoPi / ribbonCount + visualTime * rotationSpeed
    for each segment:
        height01 = segment / ribbonSegments
        angle = phase + height01 * turns * twoPi
        center = field.center + cylindrical(radius(height01), angle, height)
        build a quad strip segment with camera-facing side vector
```

Alpha:

- fade in near the base,
- strongest around the outer shell,
- fade near the very top,
- lower alpha near the camera-facing center to avoid a flat curtain.

Use the camera right vector or a local tangent/binormal to give the strip a stable width. If camera-facing billboarding shimmers, switch to world-space ribbon normals derived from radial and tangent vectors.

### Edge Particles

Add simple CPU-billboarded dust/debris quads along the outer shell.

Rules:

- particles should live mostly near `0.55 * radius` to `1.0 * radius`,
- avoid particles in the core,
- use deterministic per-particle seeds, not `rand()`,
- make particles smaller and more numerous near the base,
- give a few high particles faster angular velocity to suggest lift.

First implementation can render all particles through the same transient triangle path as ribbons. Each particle is two triangles with per-vertex alpha.

### Ground Dust Skirt

Add a low, flattened ring at the base:

- two or three broken spiral bands,
- wider and lower than the vertical ribbons,
- strongest at the outer footprint,
- fades quickly above knee height.

This sells contact with the ground without filling the whole funnel.

### Optional Debris Streaks

If boxes still need more motion context, add a small set of short curved streaks:

- placed on the outer shell,
- tangent-aligned,
- low alpha,
- color warmer/brighter than dust,
- never more than a few dozen segments.

Do this after the ribbon and dust pass is validated. The first slice should stay simple.

## Keeping Caught Objects Visible

Primary visibility protections:

- no filled funnel mesh,
- no high-alpha center smoke,
- depth test on,
- depth write off,
- ordinary opaque object pass before tornado visual pass,
- debug transparent body pass after tornado visual pass.

Secondary readability tools if needed:

1. Reduce shell alpha when the camera is inside the field radius.
2. Add a radial center hole by fading alpha below about `0.28 * radius`.
3. Add a UI slider or config scalar for shell density.
4. Expose a read-only captured-body mask from physics and render a subtle rim/selection outline for objects currently inside the tornado.

The rim/outline is a later phase. It touches object rendering and should only be added after the sparse shell is tested in motion.

## UI And Runtime Controls

Keep current tornado controls and add a separate production-visual control:

- `Tornado` toggle: existing force field behavior.
- `Field Vectors`: existing diagnostic arrows.
- `Visual Shell`: new production tornado effect.
- Optional later `Density` or `Opacity` slider if tuning from config is not enough.

Default behavior:

- Enabling tornado turns on the visual shell when `autoEnableWithTornado` is true.
- Disabling tornado hides the visual shell unless the user explicitly pins a visual preview mode later.
- Field vectors remain off by default.

Avoid overloading the debug vector toggle. Users should not have to look at force arrows to see the tornado.

## Scene And Config Defaults

First pass can use built-in defaults derived from `TornadoFieldConfig`.

Optional later scene/runtime data:

```json
"tornadoVisuals": {
  "enabled": true,
  "shellAlpha": 0.14,
  "dustAlpha": 0.20,
  "ribbonCount": 7,
  "particleCount": 96
}
```

Do not add scene persistence in the first slice unless a specific scene or validation capture needs it. Runtime defaults are enough to prove the effect.

## Implementation Phases

### Phase 1: Render-Only Settings And Pass Stub

Tasks:

1. Add `TornadoVisualSettings` to runtime render settings.
2. Add `TornadoVisualPass` with `EnsureGpuResources`, `ReleaseGpuResources`, and `Render`.
3. Wire it into `Run::DrawPrimitives()` after water and before debug transparent bodies.
4. Add render-pipeline snapshot/debug graph awareness if the pass can execute.

Validation while iterating:

- No formal validation required until code is PR-bound.
- Use a targeted local launch only if needed to inspect pass order.

### Phase 2: DX12 Transient Alpha Triangles

Tasks:

1. Add `DrawTransientColoredTriangles()` to `IRenderBackend`.
2. Implement the DX12 upload/draw path next to other dynamic geometry.
3. Add `tornado_fx.hlsl`.
4. Create a named PSO with alpha blending, depth test on, depth write off, and cull off.
5. Record draw calls as `TornadoVisual` or a similarly named trace kind.

Guardrails:

- Restore or dirty renderer state so the next normal draw does not inherit the tornado PSO.
- Keep upload allocation bounded.
- Do not add texture descriptors in the first slice.

### Phase 3: Sparse Ribbon Shell

Tasks:

1. Generate fixed-capacity ribbon triangles in `TornadoVisualPass`.
2. Reserve vertex storage during resource ensure or pass construction.
3. Use deterministic phase offsets.
4. Use runtime or simulation time for rotation; if screenshots need stable baselines, support a freeze/tick source from fixed-step scene state.
5. Tune shell alpha, ribbon width, taper, and vertical fade.

Acceptance checks:

- boxes and balls remain readable inside the vortex,
- center is mostly empty,
- the tornado reads from wide and close cameras,
- turning off tornado stops the visual.

### Phase 4: Ground Skirt And Edge Particles

Tasks:

1. Add base dust bands using the same triangle path.
2. Add deterministic billboard particles along the outer shell.
3. Cap particle counts by quality setting.
4. Keep all particles outside the center hole.

Guardrails:

- Avoid per-frame allocation growth.
- Keep total generated vertices small enough for normal demo scenes.
- If perf impact is visible, add `tools\validate_perf.bat` to the PR gate.

### Phase 5: UI Polish And Debug Separation

Tasks:

1. Add a `Visual Shell` toggle beside existing tornado controls.
2. Keep `Field Vectors` as diagnostic-only.
3. Ensure UI frame data reports both flags independently.
4. Add command-line or config wiring only if users need startup control.

Suggested optional CLI:

```bat
Profile\SKULLBONEZ_CORE.exe --tornado --tornado-visuals on
Profile\SKULLBONEZ_CORE.exe --tornado --tornado-vectors off
```

### Phase 6: Optional Object Readability Enhancement

Only do this if sparse visuals are still not enough.

Tasks:

1. Expose read-only captured-body state from physics, based on existing tornado capture seconds.
2. Convert that into a render mask or per-model highlight scalar.
3. Render a subtle rim/tint for objects currently caught in the tornado.

Validation expands here because object rendering and physics snapshot boundaries may be touched.

## File Impact Map

Likely files for the first production slice:

- `SkullbonezSource/Runtime/Run.h`
- `SkullbonezSource/Runtime/RunRender.cpp`
- `SkullbonezSource/Runtime/RunPasses.cpp`
- `SkullbonezSource/Runtime/RunUiTextPass.cpp`
- `SkullbonezSource/Runtime/RunInput.cpp`
- `SkullbonezSource/Rendering/IRenderBackend.h`
- `SkullbonezSource/Rendering/DrawCallTrace.h`
- `SkullbonezSource/Rendering/RenderSceneSnapshot.h`
- `SkullbonezSource/Rendering/RenderPipeline.cpp`
- `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp`
- `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp` if the PSO creation is split there
- `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`
- `SkullbonezData/shaders/tornado_fx.hlsl`

Possible later files:

- UI control files if the visual toggle needs new widgets beyond existing tornado controls.
- Scene parser/exporter files if visual settings become scene-authored.
- Physics/game-model files only if captured-object highlighting is added.

## Validation Plan

This document-only plan requires no validation.

For implementation, PR-bound validation should be:

```bat
tools\validate_dx12_renderer.bat
```

Add this if the implementation touches physics-owned tornado config, replay snapshots, captured-body state, or physics baseline behavior:

```bat
tools\validate_physics.bat
```

Add this if the implementation raises particle counts, adds per-frame sorting, or changes hot render loops:

```bat
tools\validate_perf.bat
```

Manual smoke launches while iterating:

```bat
Profile\SKULLBONEZ_CORE.exe --tornado --vsync off
Profile\SKULLBONEZ_CORE.exe --tornado --tornado-vectors on --vsync off
```

The formal validation scripts remain PR/commit gates. Do not claim validation success without command output.

## Risks And Mitigations

| Risk | Mitigation |
|------|------------|
| Tornado hides caught objects | Use sparse shell geometry, center hole, low alpha, depth test on, and depth write off. |
| Transparent sorting artifacts | Avoid large overlapping transparent surfaces; use small ribbons and particles. Sort only if artifacts are visible. |
| Debug vectors become confused with production art | Keep separate toggles and separate draw-call trace scopes. |
| DX12 state leaks into later passes | Restore/dirty PSO and blend/depth state after the tornado draw path. |
| Per-frame heap allocations hurt perf | Pre-reserve vertex buffers and use fixed caps for ribbons and particles. |
| Validation screenshots become flaky | Use deterministic seeds and a fixed visual time source for validation scenes if needed. |
| Physics determinism changes accidentally | Keep visual settings outside physics force-field state; do not change `SampleAcceleration()` for this feature. |

## Success Criteria

- Tornado mode has a visible rotating funnel without using dense fog.
- Boxes/items inside the vortex remain clearly visible from normal gameplay cameras.
- Field vectors remain available as a separate debug overlay.
- DX12 validation reports zero validation errors.
- Any screenshot baseline changes are intentional and tied to a focused tornado visual scene or existing renderer suite update.
- Perf remains stable for the default generated demo.

