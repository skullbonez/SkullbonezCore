# DX12 Descriptor Upload Root Signature Plan

Status: planning draft  
Created: 2026-06-11  
Scope: DX12 descriptor management, upload allocation, root signature strategy, material-system readiness  
Implementation status: plan only, no code changes in this pass

## Goal

Plan the DX12 resource-binding cleanup needed to support future material and post-processing growth without destabilizing the current renderer.

This is not the first shader cleanup slice. The first material system should use packed instance params and the existing root signature. This plan describes what to do when the renderer genuinely needs more texture/material resources.

DX12 is now the official production renderer, but the engine should still name resource concepts in backend-neutral terms. A pass should ask for frame constants, material data, instance data, global textures, and pass-local resources; the DX12 implementation maps those concepts to root signatures and descriptor heaps. A future Vulkan or Metal backend should be able to map the same concepts to descriptor sets, argument buffers, or equivalent resource tables.

## Current Read

Current ordinary DX12 raster root signature:

- root parameter 0: CBV at `b0`,
- root parameter 1: SRV descriptor table for `t0`,
- root parameter 2: SRV descriptor table for `t1`,
- root parameter 3: SRV descriptor table for `t2`,
- static sampler `s0`: linear wrap,
- static sampler `s1`: linear clamp.

Current descriptor model:

- CPU staging heap for persistent SRV descriptors.
- Shader-visible heap for bound/transient descriptors.
- Static SRV allocation for textures/FBO registered SRVs.
- Transient SRV allocation copied into shader-visible heap.
- Bound texture slots are tracked for `t0`, `t1`, `t2`.

Current upload model:

- Per-frame upload buffers exist.
- Constant buffers are suballocated from upload space.
- Shader `FlushCB()` uploads reflected cbuffer data to a 256-byte-aligned allocation.

This works for current passes. It is narrow for material tables, more post textures, texture arrays, and debug capture buffers.

## Design Principles

1. Keep current root signature until a concrete feature needs more.
2. Prefer descriptor-table stability over per-draw descriptor churn.
3. Make descriptor allocation frame-scoped where resources are transient.
4. Keep DX12 validation zero-error.
5. Avoid bindless until the engine has enough texture/material pressure to justify it.
6. Keep material and pass contracts backend-neutral even when the first implementation is DX12-specific.
7. Prefer resource layouts that can later map to Vulkan descriptors and Metal argument buffers without changing scene/style data.

## Near-Term Recommendation

For material system v1:

- Do not change the DX12 root signature.
- Do not add structured buffers.
- Do not add bindless descriptors.
- Pack material params into instance data.

Reason:

- It reduces risk while material semantics are still being cleaned up.
- It avoids a DX12-heavy change blocking shader architecture cleanup.
- It keeps the CPU material model independent from the eventual GPU binding model.

## Medium-Term Needs

The root signature should be revisited when one of these is true:

- More than three pixel SRV slots are required in ordinary raster passes.
- Material tables are too large for instance data.
- Terrain needs multiple texture layers.
- Post stack needs more intermediate textures.
- Per-pass debug buffers need SRV access.
- Texture arrays or material textures become central.

## Candidate Binding Models

### Model A: Expand Fixed SRV Slots

Root signature:

- CBV `b0`.
- SRV tables for `t0` through `t7`.
- Static samplers remain.

Pros:

- Simple.
- Easy to map from existing `BindTexture(handle, slot)`.
- Works with HLSL source as currently written.

Cons:

- Root signature grows.
- Still requires descriptor copies.
- Does not scale elegantly.

Use if only 4-8 slots are needed.

### Model B: Single Descriptor Table Range

Root signature:

- CBV `b0`.
- One descriptor table with range `t0..tN`.
- Static samplers.

Pros:

- Cleaner root signature.
- Allows binding a contiguous set of descriptors once.
- Easier to batch descriptor copies.

Cons:

- Requires rebinding table when any slot changes.
- Existing code around `1 + slot` root params changes.

Good medium-term default.

### Model C: Material Texture

Use a 1D/2D texture to store material params.

Pros:

- Portable enough for future backend concepts because material rows stay data-oriented.
- Instance only carries material row/index.
- Avoids structured buffers.

Cons:

- Awkward packing.
- Requires shader sampling for material params.
- Precision/format choices matter.

Best first GPU material table option if packed instance params become too costly and portability matters more than DX12-native structured-buffer ergonomics.

### Model D: Structured Buffer Material Table

Use `StructuredBuffer<MaterialParams>` in HLSL and UBO/SSBO-like equivalent where available.

Pros:

- Natural in DX.
- Clean shader code.

Cons:

- Future backend compatibility is awkward.
- Requires more backend abstraction.
- More resource binding changes.

Good DX12-first option once material semantics are stable. If future Vulkan/Metal support is active at that point, define the engine material-table contract first and map it to each backend deliberately.

### Model E: Bindless Descriptor Heap

Large shader-visible descriptor heap, pass indices into shaders.

Pros:

- Scales well.
- Reduces per-bind descriptor copies.

Cons:

- Big architecture jump.
- Not aligned with the current compact DX12 material path.
- Requires broader renderer abstraction changes.

Do not do for v1/v2.

### Model F: Engine Resource Binding ABI

Define the binding contract in engine terms before changing root signatures:

- frame/pass constants,
- material table,
- instance buffer,
- mesh/geometry buffers,
- global texture table,
- pass-local SRVs/UAVs,
- sampler set,
- debug resources.

DX12 mapping should then choose root parameters, descriptor tables, register spaces, and static samplers. Future mappings can choose Vulkan descriptor sets or Metal argument buffers without changing material/style authoring.

## Descriptor Allocator Plan

### Static Descriptor Allocator

Owns descriptors for persistent resources:

- loaded textures,
- FBO color/depth SRVs,
- DXR reflection SRV,
- material texture if added.

Requirements:

- stable handles,
- tombstone or free-list behavior,
- debug name per allocation,
- capacity checks with clear errors.

### Transient Descriptor Allocator

Owns descriptors copied into shader-visible heap during a frame.

Requirements:

- frame-scoped reset,
- capacity budget per frame,
- no overwrite while GPU still reads descriptors,
- debug counter for descriptors used per frame.

### Proposed Debug Metrics

Track:

- static SRV count,
- transient SRV count per frame,
- peak transient SRV count,
- descriptor copy count per frame,
- descriptor heap capacity,
- failed/overflow attempts.

Expose in logs first, optional UI later.

## Upload Buffer Plan

The current per-frame upload buffers are a good base. Strengthen them:

1. Track upload usage per frame.
2. Track peak usage per frame.
3. Fail clearly before overflow in debug/dev mode.
4. Avoid full GPU stalls on routine growth.
5. Add alignment helpers for CBV, vertex, index, and arbitrary data.

For material v1:

- Larger instance payload may increase upload use.
- Validate with perf and upload peak counters before changing architecture.

## Root Signature Migration Plan

Do not migrate root signature opportunistically. Use a gated plan:

### Gate 1: Inventory

List every shader's resource needs:

- current slots,
- future slots,
- sampler modes,
- pass category.

### Gate 2: Choose Model

If max ordinary pass texture count is 4-8:

- choose expanded fixed slots or single table.

If material table is needed:

- prefer material texture first.

### Gate 3: Add Compatibility Layer

Keep public call:

```cpp
BindTexture(handle, slot)
```

Internally, DX12 can map it to new descriptor model.

### Gate 4: Update Shaders

Update HLSL register bindings and shader contract metadata together.

### Gate 5: Validate Aggressively

Run renderer validation and inspect DX12 validation log.

## Phase Plan

### Phase 1: Instrument Current DX12 Resource Binding

Tasks:

1. Add debug counters for transient/static descriptors.
2. Add upload peak usage counters.
3. Log root signature resource slot assumptions in debug mode.
4. No behavior change.

Validation:

- `tools\validate_dx12_renderer.bat`.

### Phase 2: Make Transient Descriptor Reset Explicit

Tasks:

1. Reset transient descriptor allocation at frame boundaries.
2. Ensure per-frame allocator index aligns with GPU fence safety.
3. Add overflow errors.

Validation:

- `tools\validate_dx12_renderer.bat`.
- Verify `dx12_validation.txt` is zero.

### Phase 3: Batch Descriptor Tables

Tasks:

1. Prepare for one descriptor table range without changing public API.
2. Reduce per-slot root parameter assumptions.
3. Keep current shader register layout if possible.

Validation:

- `tools\validate_dx12_renderer.bat`.

### Phase 4: Root Signature Expansion If Needed

Tasks:

1. Add more SRV slots or one descriptor range.
2. Update PSO cache key if root signature variants exist.
3. Update shader manifests.
4. Update HLSL register docs.

Validation:

- `tools\validate_dx12_renderer.bat`.
- Verify `dx12_validation.txt` is zero.
- Run DX12-heavy scene three consecutive times if resource barriers/uploads are touched.

### Phase 5: Material Table If Needed

Tasks:

1. Prefer material texture when backend portability matters more than DX12-native structured-buffer ergonomics.
2. Add material texture creation/update path.
3. Instance data carries material index.
4. Shaders sample material row.

Validation:

- `tools\validate_dx12_renderer.bat`.
- `tools\validate_perf.bat`.

## Validation Matrix

| Change | Validation |
|--------|------------|
| Plan/docs only | No validation required |
| DX12 debug counters | `tools\validate_dx12_renderer.bat` |
| Descriptor allocator behavior | `tools\validate_dx12_renderer.bat`, DX12 log zero |
| Upload allocation behavior | `tools\validate_dx12_renderer.bat`, DX12 log zero, perf if hot |
| Root signature change | `tools\validate_dx12_renderer.bat`, DX12 log zero |
| Material texture/table | `tools\validate_dx12_renderer.bat` plus `tools\validate_perf.bat` |

## Risks

| Risk | Mitigation |
|------|------------|
| Root signature change breaks all PSOs | Change in isolated slice and rebuild PSO cache key. |
| Descriptor overwrite while GPU reads | Make transient allocation frame-scoped and fence-aware. |
| Upload buffer overflow causes stalls | Track peaks and grow deliberately. |
| Material table leaks backend details | Keep the CPU material registry backend-neutral and isolate descriptor/table decisions in renderer code. |
| Debug counters perturb performance | Compile or gate debug counters appropriately. |

## Success Criteria

- Current material cleanup does not require DX12 root signature changes.
- DX12 descriptor/upload usage is measurable.
- Future root signature changes have a clear gate and validation plan.
- DX12 validation remains zero-error through all binding changes.
