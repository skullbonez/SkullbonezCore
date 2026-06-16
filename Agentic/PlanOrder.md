# Agentic Plan Order

Generated: 2026-06-16

Scope: top-level `Agentic/Plans/*.md` files only. Completed, failed, rejected, and nested historical plans are excluded.

This order reflects the current product direction: DX12 is the official production renderer. GL and DX11 have been retired from runtime code; their final parity evidence is historical context only.

Future Vulkan and Metal support should influence the engine contracts without becoming the near-term implementation target. Express render passes, shader metadata, materials, resources, and synchronization in engine-owned terms first, then map those contracts to DX12 now and Vulkan/Metal later.

Retirement policy: keep DX12 screenshot/debug-layer/WARP/GBV/PIX diagnostics as the replacement confidence stack for the retired cross-renderer parity checks.

## Tackle Order

1. [`dx12-only-renderer-retirement-plan.md`](Plans/dx12-only-renderer-retirement-plan.md)
   - Complete on branch: `codex/dx12-only-renderer-retirement`. Use as the retirement history and validation reference.

2. [`dx12-only-engine-architecture-plan.md`](Plans/Done/dx12-only-engine-architecture-plan.md)
   - Use as the umbrella architecture direction. The implementation now lives in smaller render/shader/resource slices rather than one rewrite.

3. [`render-resource-lifetime-plan.md`](Plans/render-resource-lifetime-plan.md)
   - Follow immediately because resize, framebuffer, shader, mesh, descriptor, and eventual device-loss lifetimes are pressure points for DX12-only visual/runtime weirdness. Keep source-vs-GPU separation because it also preserves a future Vulkan/Metal path.

4. [`shader-architecture-cleanup-plan.md`](Plans/shader-architecture-cleanup-plan.md)
   - Merged through PR #69/#72 and completed further on `codex/engine-cleanup`: shader inputs, texture slots, uniform names, pass contracts, the CPU `RenderMaterial` bridge, expanded object material payloads, typed object/shadow CBV uploads, shader contract checking, and the `t4` material table. Treat HLSL/DXC reflection as canonical, while keeping metadata portable enough for later SPIR-V/MSL mapping.

5. [`dx12-descriptor-upload-root-signature-plan.md`](Plans/dx12-descriptor-upload-root-signature-plan.md)
   - Merged through PR #70/#72 and completed further on `codex/engine-cleanup`: ordinary raster binding ABI and descriptor/upload lifetime constraints, now `b0 + t0..t4` with `t4` scoped to the object material table. Do not expand root signatures opportunistically; use this plan again only when a concrete descriptor-indexing, structured-buffer, or upload lifetime issue appears.

6. [`water-rendering-cleanup-plan.md`](Plans/water-rendering-cleanup-plan.md)
   - Partially implemented on `codex/post-pr73-roadmap`: explicit reflection/style contracts, water pass state setup, exact depth-write/blend-function restore, and intentional DX12 water baseline refresh are in place. Continue with remaining water-material and intersection-quality work only as a focused renderer slice.

7. [`material-system-v1-implementation-plan.md`](Plans/material-system-v1-implementation-plan.md)
   - Object-material v1 is implemented on `codex/engine-cleanup`. Use this plan next only for named material definitions, material asset records, terrain/water/post material unification, or a deliberate material-resource-model expansion.

8. [`architecture_pass_2026-06-02.md`](Plans/architecture_pass_2026-06-02.md)
   - Use as the broader checkpoint after the render-focused slices, then pull the next concrete runtime or physics boundary from it.

9. [`replay-system-plan.md`](Plans/replay-system-plan.md)
   - Useful once render and physics boundaries are easier to observe. It should consume stable capture, scene, and diagnostic APIs rather than add more runtime coupling.

10. [`worker-system-plan.md`](Plans/worker-system-plan.md)
   - High value, but wait until the physics/world/runtime boundaries are cleaner so deterministic parallelism has a safe data model.

11. [`tornado-mode-ui-cli-plan.md`](Plans/tornado-mode-ui-cli-plan.md)
    - Useful workflow polish, but lower priority than render correctness, determinism, and architecture boundaries.

12. [`autonomous-roadmap-orchestrator-plan.md`](Plans/autonomous-roadmap-orchestrator-plan.md)
    - Process automation belongs last until the technical backlog is better sorted and the desired merge/PR rhythm is settled.

## Immediate Recommendation

After `codex/post-pr73-roadmap`, the next concrete slice should either finish a
remaining water-material/intersection item from `water-rendering-cleanup-plan.md`
or continue the runtime/physics boundary work in
`architecture_pass_2026-06-02.md`. Use
`material-system-v1-implementation-plan.md` only for named material definitions,
material asset records, terrain/water/post material unification, or a deliberate
material-resource-model expansion. Keep changes small and validate renderer
work with `tools\validate_dx12_renderer.bat`; use `tools\validate_full.bat` for
broad runtime/pass changes.
