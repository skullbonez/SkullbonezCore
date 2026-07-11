# Render Visibility Architecture

Date: 2026-07-11
Status: Not started — 0%
Impact area: DX12 renderer submission, `RenderInstanceStore`, shadow and
reflection passes, perf baselines
Origin: 2026-07-11 architecture gap review. The renderer submits every scene
object every frame — and again for the reflection view and again for shadow
maps — with no object-level frustum culling anywhere
(`GameModelRenderer`/`RenderInstanceStore` contain none; only rasterizer
backface culling exists). GPU triangle clipping is not culling: vertices are
still transformed and draws still submitted. Renderer cost is O(scene)
regardless of what is visible.

## Goal

Per-view CPU visibility: each rendered view (main camera, planar reflection,
terrain shadow, object shadow) draws only instances whose bounds intersect
that view's volume. Culling is conservative (never drops a visible object),
render-only (physics untouched), allocation-free, and observable through
diagnostics.

## Scope decisions (binding)

- **Critical-path position.** P0 instrumentation may run at any time because it
  is read-only. Implementation waits for stable render-backend ownership and
  completes main, shadow, reflection, and instancing culling before
  `shadow-edge-quality.md` S1-S4, so shadow tuning measures the real visible
  workload.

- **Frustum culling only in this plan.** Occlusion culling, LOD, and GPU
  culling are explicitly out of scope; P6 records whether LOD gets its own
  plan.
- **Conservative correctness beats cull ratio.** A missed cull is a perf
  bug; a wrong cull is a rendering bug. Bounds get an epsilon margin and
  screenshots must not change.
- **Culling reads store data.** Bounds come from `RenderInstanceStore` /
  collider-derived world bounds already maintained for shadow prep
  (`BuildShadowCasterBatches` bounds accumulators); no `GameModel` reads in
  the cull loop (hot-path data rule).
- **Fixed-capacity outputs.** Per-view visible-index lists are preallocated
  to store capacity; no growth during steady gameplay.
- Terrain, sky, water, and fullscreen passes are not per-instance and stay
  outside the visibility system. Debug overlays and replay ghosts opt in
  only if trivially correct.

## Phases

### P0 — Instrumentation first

Add per-frame counters to the existing diagnostics/UI stats path: instances
submitted per view, draws per view, culled counts (zero until P1). Record
current numbers for the standard validation scenes in this plan as the
baseline story. Gate: `validate_dx12_renderer` (baselines unchanged —
counters only).

### P1 — Frustum math + main-view culling

- Frustum plane extraction from the view-projection matrix into a small
  value type in `Maths/` (6 planes; unit-tested against known
  inside/outside/straddle cases in `SkullbonezTests`).
- Sphere-vs-frustum test on per-instance world bounds with conservative
  epsilon; instances failing all-plane test are skipped by main-view
  submission. Per-view visible list is a preallocated index array filled
  each frame.
- The cull loop is a hot path: flat array in, flat array out, no handles, no
  callbacks, no per-frame allocation.

Gate: `validate_tests` (frustum math), `validate_dx12_renderer` (screenshots
byte-identical — culling must be invisible), `validate_perf`.

### P2 — Shadow view culling

Cull shadow casters against each shadow map's light-space volume, not the
camera frustum — an off-screen caster whose shadow lands on-screen must
still render into the map. Use the existing per-map orthographic bounds from
`ShadowPass::BuildTerrainFrameData`/`BuildObjectFrameData` as the cull
volume, extended along the light direction. Integrates with (does not
duplicate) the existing `BuildShadowCasterBatches` worker prep.

Gate: `validate_dx12_renderer` (shadow screenshots byte-identical),
`validate_perf`.

### P3 — Reflection view culling

Cull the mirrored-camera pass against the reflection frustum plus the water
clip plane (an instance entirely below the water surface cannot appear in
the reflection). DXR reflection path is unaffected (TLAS-based; out of
scope).

Gate: `validate_dx12_renderer` (water/reflection screenshots byte-identical).

### P4 — Instanced-batch compaction

Where instanced draws exist, cull per instance and compact the surviving
transforms into the instance upload buffer instead of choosing between
"whole batch" and "nothing". Touches the dynamic-VB/upload path.

Gate: `validate_dx12_renderer` ×3 consecutive (upload-buffer danger zone) +
`validate_perf`.

### P5 — Stress evidence + perf baseline

Run `tools\run_graphics_stress.bat 1` and a deliberately large generated
scene; record submitted-vs-culled counts and frame-time deltas here. Refresh
perf baselines intentionally (isolated commit) if the improvement trips
thresholds.

### P6 — LOD decision gate (owner decision)

Record whether object count / triangle density justifies a follow-up LOD
plan. Plan-only; no implementation here.

## Acceptance

- [ ] Every per-instance view (main, reflection, terrain shadow, object
      shadow) consumes a per-view visible set; stats prove non-zero culling
      in standard scenes with the camera moved off-center.
- [ ] All validation screenshots match baselines unchanged — culling is
      never visible.
- [ ] Frustum math has named unit tests including straddle/epsilon cases.
- [ ] Cull loops allocate nothing and read only store/value data.
- [ ] Physics CSV remains byte-exact (culling is render-only).

## Validation map

| Slice | Gate |
|-------|------|
| Frustum math | `validate_tests` |
| Main/shadow/reflection culling | `validate_dx12_renderer` (unchanged baselines) + `validate_perf` |
| Instance buffer compaction | `validate_dx12_renderer` ×3 + `validate_perf` |
| Stress evidence | `run_graphics_stress.bat 1` |
| Any physics-adjacent bound sourcing change | `validate_physics` (byte-exact CSV) |
