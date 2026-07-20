# Render HAL Modernization

Status: Registered — 0/6 tasks (M0-M5)
Owner: repository owner; registered 2026-07-20 as campaign plan 6 of 8
Evidence: `../../Reports/2026-07-20/engine-architecture-review.md` (finding F)
Ledger: M0-M5
Depends on: `render-graph-completion` (hard blocker — pass-declared state
needs the single graph execution path as its shape).

## Objective

Retire the OpenGL-2-era statefulness from the render HAL now that DX12 is
the only backend. Passes declare their raster state in typed per-pass/per-
bucket descriptions instead of mutating global
`SetDepthTest`/`SetBlendFunc`/`SetPolygonOffset` state between draws;
`PSOKey12` reverse-engineering shrinks toward precompiled per-pass PSO
tables; the DXR one-off interface becomes a typed pass description. The
scope is `IRenderCommandContext` and the DXR facet; the factory, capture,
and diagnostics facets already carry their weight and stay.

## Problem / Evidence

`IRenderCommandContext` is a stateful immediate API
(`SetDepthTest`/`SetBlendFunc`/`SetClipPlane`/`BindTexture(slot)`); the
backend rebuilds PSOs from accumulated state via `PSOKey12` hashing
(`RenderBackendDX12.h:155-176`). The abstraction was built for GL/DX11
parity that no longer exists — it now costs DX12's wins (precompiled PSOs,
parallel recording readiness, bindless) while protecting nothing.
`DispatchReflectionRays` takes eight individual sky texture handles and raw
`float*` matrices through the HAL (`RenderBackendDX12.h:830-846`).
`RenderBackendDX12` implements seven interfaces in one class
(`RenderBackendDX12.h:644-650`).

## Non-Goals

- No visual change; screenshot baselines and zero DX12 validation errors are
  the oracle for every slice, with zero refresh authorized.
- No parallel command recording, multi-queue, or bindless implementation in
  this plan — the modernization makes them *expressible*, it does not build
  them (the job-system plan owns that future).
- No renderer feature work; pass content is untouched.
- `IRenderResourceFactory`, `IRenderCaptureBackend`, `IRenderDiagnostics`,
  `IRenderDeviceLifecycle`, `IRenderShaderDevelopment` interfaces are out of
  scope except where a signature must carry a typed state block.
- No new inheritance; state blocks are value records.

## Binding Decisions

1. A typed `RasterStateDesc` value (depth test/write, blend enable/factors,
   cull, polygon offset, target format expectations) becomes part of pass
   input; draws inside a pass select from that pass's declared state
   buckets. Global mutable raster state on the command context is deleted
   at closure.
2. `PSOKey12` remains the cache identity but is populated from declared
   state descs, not accumulated setter state; state-desc-declared passes may
   precompile their PSOs at resource-creation time. Record cache hit/size
   evidence before/after.
3. Interface math types: `float*` matrix/vector parameters on migrated
   surfaces become the engine's `Matrix4`/`Vector3` types or typed spans.
4. The DXR facet generalizes to a typed reflection-pass description
   (environment texture set as a value record, not eight positional
   handles). Single-purpose is fine; positional soup is not.
5. Dead interface surface (e.g. `SetClipPlane` if unused, redundant state
   getters) is deleted with a usage-inventory proof, not kept "just in
   case".
6. Migration is pass-by-pass behind the plan-5 graph path; a migrated pass
   must not fall back to setter-driven state.
7. Every slice: DX12 gate + bounded stress; slices touching upload/frame
   allocator or PSO/root-signature lifetime run the renderer gate three
   consecutive times (Danger Zones).

## Tasks

- [ ] M0 — Contract and inventory: enumerate every `IRenderCommandContext`
  member with its callers; classify keep-as-is / migrate-to-state-desc /
  delete-with-proof; define `RasterStateDesc` and the per-pass state-bucket
  shape; record the PSO cache baseline (entry count, hit behavior) for the
  render test suite. Output: inventory + contract committed into this plan.
  No validation (documentation).
- [ ] M1 — State-desc pilot: migrate two structurally different passes
  (one opaque world pass, one blended overlay/UI pass) to declared state
  with precompiled PSOs; prove baseline-identical output and record PSO
  cache evidence. Validation: `tools\validate_dx12_renderer.bat` ×3 +
  `tools\run_graphics_stress.bat 1` + `tools\validate_perf.bat`.
- [ ] M2 — Full pass migration: remaining passes move to declared state;
  per-draw setter calls disappear from pass bodies. Validation:
  `tools\validate_dx12_renderer.bat` ×3 + `tools\run_graphics_stress.bat 1`.
- [ ] M3 — Setter retirement: delete the global raster-state setters/getters
  from `IRenderCommandContext` and their backend state tracking; `PSOKey12`
  population is state-desc-only. Typed math types per binding decision 3 on
  migrated signatures. Validation: `tools\validate_dx12_renderer.bat` ×3 +
  `tools\run_graphics_stress.bat 1` + `tools\validate_perf.bat`.
- [ ] M4 — DXR facet: replace `DispatchReflectionRays`/`InitDXR` positional
  surfaces with typed descriptions per binding decision 4; delete dead
  interface rows found in M0 with usage proof. Validation:
  `tools\validate_dx12_renderer.bat` + `tools\run_graphics_stress.bat 1`
  (DXR-capable machine evidence recorded; the suite's DXR scenes are the
  oracle).
- [ ] M5 — Closure: grep proofs (no raster setters on the interface, no
  setter calls in passes, no `float*` matrices on migrated surfaces);
  before/after PSO cache and perf numbers recorded; DX12-architecture CPU
  test target updated to pin the new contract; independent rubber-duck
  review (single end-of-plan). Validation: `tools\validate_full.bat` +
  `tools\validate_perf.bat` + `tools\run_graphics_stress.bat 1` at closure
  tip.

## Acceptance

- `IRenderCommandContext` carries no global mutable raster state; passes
  declare state; PSOs for declared passes precompile.
- DXR facet is typed; dead surface deleted with inventory proof.
- All screenshot baselines identical, zero DX12 validation errors, perf
  gate passes with recorded numbers (frame time must not regress outside
  noise; PSO-compile hitches must not appear in steady frames).
- Independent review clear.

## Validation Summary

Per-slice DX12 gate (+×3 where Danger Zones demand) + bounded stress with
recorded evidence; perf gate on M1/M3/M5; `validate_full` at closure.
