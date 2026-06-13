# Agentic Plan Order

Generated: 2026-06-13

Scope: top-level `Agentic/Plans/*.md` files only. Completed, failed, rejected, and nested historical plans are excluded.

This order reflects the current product direction: DX12 is the canonical production renderer. GL and DX11 can remain useful as temporary comparison backends while they exist, but new render architecture should be designed around DX12-native ownership, diagnostics, and validation.

Future Vulkan and Metal support should influence the engine contracts without becoming the near-term implementation target. Express render passes, shader metadata, materials, resources, and synchronization in engine-owned terms first, then map those contracts to DX12 now and Vulkan/Metal later.

## Tackle Order

1. [`dx12-only-engine-architecture-plan.md`](Plans/dx12-only-engine-architecture-plan.md)
   - Use as the umbrella architecture direction. Do not implement it as one rewrite; let it guide the smaller render/shader/resource slices below.

2. [`render-pipeline-extraction-plan.md`](Plans/render-pipeline-extraction-plan.md)
   - First because named render passes make the current frame order, reflection path, water path, shadow path, and debug overlays easier to inspect without changing output.

3. [`render-resource-lifetime-plan.md`](Plans/render-resource-lifetime-plan.md)
   - Follow immediately because resize, framebuffer, shader, mesh, descriptor, and eventual device-loss lifetimes are pressure points for DX12-only visual/runtime weirdness. Keep source-vs-GPU separation because it also preserves a future Vulkan/Metal path.

4. [`shader-architecture-cleanup-plan.md`](Plans/shader-architecture-cleanup-plan.md)
   - Make shader inputs, texture slots, uniform names, and pass contracts explicit before expanding materials or post effects. Treat HLSL/DXC reflection as canonical, while keeping metadata portable enough for later SPIR-V/MSL mapping.

5. [`dx12-descriptor-upload-root-signature-plan.md`](Plans/dx12-descriptor-upload-root-signature-plan.md)
   - Keep close behind the shader work. Do not change root signatures first, but use this plan when a concrete DX12 binding, material table, descriptor, or upload lifetime issue appears.

6. [`water-rendering-cleanup-plan.md`](Plans/water-rendering-cleanup-plan.md)
   - Tackle after render-pass boundaries exist, because water depends on reflection ownership, shader inputs, and render target contracts.

7. [`material-system-v1-implementation-plan.md`](Plans/material-system-v1-implementation-plan.md)
   - Add the material layer after shader contracts are clearer. Keep the CPU material model backend-neutral, but allow the DX12 path to adopt GPU material tables once v1 semantics are proven.

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

Use item 1 as the north star, then start implementation with item 2 and keep the first slice deliberately small: extract render frame context or one named pass boundary, preserve output, then run the appropriate renderer/DX12 validation only at the commit/PR gate.
