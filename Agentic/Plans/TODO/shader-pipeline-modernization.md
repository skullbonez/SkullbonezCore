# Shader Pipeline Modernization And Binding Standardization

Date: 2026-07-11
Status: Not started — 0%
Impact area: DX12 renderer, all HLSL shaders, shader tooling, build/validation
scripts
Origin: 2026-07-11 architecture gap review. Owner directive: complete
rearchitecture to best practice, including the binding model. Compilation
modernization and binding standardization are one plan because
reflection-driven binding (the fix for the hand-maintained slot ABI) is only
possible once the compiler layer is modernized.

## Problem

1. **Runtime FXC compilation at Shader Model 5.0.** Every raster shader is
   compiled from HLSL source text at startup via `D3DCompile(...)` targeting
   `vs_5_0`/`ps_5_0` (`SkullbonezSource/Rendering/DX12/ShaderDX12.cpp` ~122-165).
   This locks the engine out of the entire SM6.x feature surface (wave ops,
   enhanced barriers, SM6.6 dynamic resources), recompiles everything on every
   launch, and means shader compile errors are runtime Lane R events instead
   of build failures.
2. **Hand-maintained slot ABI.** CPU root signatures, input layouts, and
   descriptor bindings must match each shader "exactly" by manual discipline —
   every shader file's invariant header says so, and plan documents carry
   warnings like "do not steal t4". There is no shader-reflection-driven
   validation, so every cbuffer or slot edit is a latent mismatch.
3. **No pipeline caching.** No `ID3D12PipelineLibrary`, no on-disk shader
   cache; PSOs and bytecode are rebuilt from scratch each run.
4. **One precedent already exists.** `reflect.rt.hlsl` is compiled offline to
   `reflect.rt.dxil` (DXR requires DXC/SM6.3+), proving the repo already
   builds and loads DXIL for one shader family. This plan extends that model
   to everything.

## Goal

- All shaders compile **offline via DXC to DXIL** at a pinned SM6.x baseline;
  the runtime loads bytecode and never invokes a compiler in shipping paths.
- Binding contracts are **generated from shader reflection** at bake time and
  **validated at load time** — a CPU/HLSL mismatch becomes a named startup
  failure (or better, a build failure), never a silent corruption.
- Root signatures are consolidated to a small named set with one documented
  slot map, ending per-shader tribal-knowledge contracts.
- PSO/bytecode caching removes redundant startup work.
- Dev-only hot reload becomes possible (cold-path recompile + PSO rebuild).

## Scope decisions (binding)

- **SM6.0 minimum baseline; SM6.6 evaluated at the P5 gate.** Do not sprinkle
  per-shader model targets; one pinned baseline in the bake script.
- **Offline compilation is the architecture; runtime compile survives only as
  a dev-mode fallback** behind an explicit launch option, and may be deleted
  once hot reload (P6) exists.
- **No new inheritance, no allocation-policy exceptions.** Bytecode blobs are
  loaded pre-gameplay (cold path); reflection metadata is baked into fixed
  POD tables.
- **Bindless is a decision gate, not a default.** SM6.6
  `ResourceDescriptorHeap` indexing is evaluated on merit at P5 with the
  descriptor-management owner; the plan is complete without it if the owner
  declines.
- Binding order is fixed. `TODO/render-backend-decomposition.md` A2 first
  establishes the concrete pipeline/root-signature owner. P1-P3 then modernize
  bytecode and consolidate that owner's contract. P4 places its cache in that
  owner. `TODO/shadow-edge-quality.md` S1 follows P3 and extends the surviving
  named contract. P0 inventory may run before A2 because it is read-only.

## Phases

### P0 — Inventory and baseline decision

Inventory every shader (`SkullbonezData/shaders/*.hlsl`), its entry points,
cbuffer layouts, and current register slots into a table in this plan.
Confirm DXC availability in the toolchain (bundled with the Windows SDK the
repo already requires; record the pinned dxc version). Decide and record the
SM6.x baseline. Documentation-only; no validation.

### P1 — Offline DXC bake step

- Add `tools\bake_shaders.py` (+ `.bat` wrapper): compiles every raster
  shader to `.dxil` next to source (mirroring the `reflect.rt.dxil`
  convention), fails loudly on any warning, and writes a manifest with source
  hash → bytecode mapping so stale bytecode is detectable.
- `ShaderDX12` loads `.dxil` when present and current; falls back to runtime
  `D3DCompile` only under a dev launch option, with a log line naming the
  fallback.
- Wire the bake step into `tools\validate_dx12_renderer.bat` before the
  build step so stale/failed shader compiles fail the gate first.
- Hazards to verify explicitly: DXC vs FXC semantic differences —
  `#pragma pack_matrix(column_major)` honoring, sampler/register inference,
  and optimization differences that can shift pixel output. Expect near- but
  possibly not byte-identical images; if screenshots diff, inspect before
  accepting any baseline update.

Gate: `tools\validate_fast.bat` (new tool script), then
`tools\validate_dx12_renderer.bat`. Baseline update only if DXC output
legitimately shifts pixels, in an isolated reviewed commit.

### P2 — Reflection-generated binding metadata

- Extend the bake step: use DXC reflection to emit, per shader, a POD table
  of cbuffer sizes/field offsets, bound resource slots (t/s/u/b), and input
  layout signature into a generated header or binary sidecar.
- At shader load, validate the CPU-side binding contract against the
  reflected table: cbuffer size mismatch, missing slot, or layout mismatch is
  a named Lane R failure with owner diagnostics (fail at startup, not at
  draw).
- Add a `SkullbonezTests`/`Agentic/Tests` architecture test asserting every
  shipped shader's reflection table matches the CPU contract structs — the
  "CPU and HLSL must match by hand" review rule becomes machine-checked.

Gate: `tools\validate_tests.bat` + `tools\validate_dx12_renderer.bat`,
baselines unchanged (metadata-only phase).

### P3 — Root signature consolidation

- Design a small named set of root signatures (expected: one raster contract
  covering lit/unlit/water/post families, one for text/UI if its layout
  genuinely differs, DXR keeps its existing local/global signatures). Define
  each in one place with a documented slot map that replaces the per-shader
  invariant folklore; consider HLSL `[RootSignature]` attributes compiled at
  bake time so the shader is the single source of truth.
- Migrate shader families one commit per family; the reflection check from
  P2 verifies each migration mechanically.
- Retire "do not steal tN" style comments in favor of the named slot map;
  update `RenderRasterBindingContract.h` to be generated-from or verified-
  against the new map.

Gate per family: `tools\validate_dx12_renderer.bat`, baselines unchanged
(binding refactor must be visually inert). Root-signature changes are the
highest-risk slice in this plan — the shadow plan's warning applies to every
lit shader at once.

### P4 — PSO and bytecode caching

- Add `ID3D12PipelineLibrary`-based PSO cache (or driver-cache-friendly
  explicit blob cache) persisted under a writable cache dir; startup loads
  the library and misses compile-and-store. Cache invalidation keys on the
  P1 manifest hashes.
- Measure and record before/after startup time in this plan.

Gate: `tools\validate_dx12_renderer.bat` ×3 consecutive (cache warm/cold
paths touch frame startup), `tools\validate_perf.bat`.

### P5 — SM6.6 dynamic-resources decision gate (owner decision)

Evaluate moving material/texture binding to SM6.6 `ResourceDescriptorHeap`
indexing (bindless): what it deletes (per-draw descriptor table juggling,
slot budget pressure — the t4 class of problem disappears), what it costs
(descriptor lifetime discipline, debuggability, minimum feature level).
Present the trade to the owner; implement only on explicit approval as its
own phase with `validate_dx12_renderer` + stress gates. The plan is
completable with a "no, revisit later" recorded here.

### P6 — Dev hot reload (cold path, optional but cheap after P1/P4)

File-watch or manual-key recompile of changed shaders through the same DXC
path, PSO rebuild through the P4 cache, all outside steady gameplay
allocation rules (explicit cold utility action). Delete the runtime
`D3DCompile` fallback once this lands.

Gate: `tools\validate_dx12_renderer.bat`; hot reload itself is dev-only and
manually verified.

### P7 — Closure

Touched-file comment audit; single rubber-duck review; final
`tools\validate_full.bat`; update SessionState/MASTER-PLAN; delete plan files
on completion.

## Acceptance

- [ ] No shipping-path runtime shader compilation; all shaders load pinned
      SM6.x DXIL with hash-verified freshness.
- [ ] A deliberate CPU/HLSL cbuffer mismatch is caught by the P2 machine
      check (test this the same way behavioral-test-depth P5 drills bugs).
- [ ] Root signatures reduced to a named, documented set; no per-shader slot
      folklore comments remain.
- [ ] Startup does not recompile unchanged shaders or rebuild unchanged PSOs.
- [ ] `dx12_validation.txt` = 0 errors and screenshots match accepted
      baselines at every phase.

## Validation map

| Slice | Gate |
|-------|------|
| Bake tooling | `validate_fast`, then `validate_dx12_renderer` |
| DXC migration / root signatures | `validate_dx12_renderer` (baselines unchanged unless reviewed) |
| Reflection contract checks | `validate_tests` + `validate_dx12_renderer` |
| PSO cache | `validate_dx12_renderer` ×3 + `validate_perf` |
| Bindless (if approved) | `validate_dx12_renderer` + `run_graphics_stress.bat 1` |
