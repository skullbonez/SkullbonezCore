# Agentic Plan Order

Generated: 2026-06-13

Scope: top-level `Agentic/Plans/*.md` files only. Completed, failed, rejected, and nested historical plans are excluded.

This order reflects the current repo state: physics baselines match across GL and DX12, while the remaining suspicious differences are render-side ownership, pass structure, shader contract, and DX12 resource-binding concerns.

## Tackle Order

1. [`render-pipeline-extraction-plan.md`](Plans/render-pipeline-extraction-plan.md)
   - First because named render passes make the current frame order, reflection path, water path, shadow path, and debug overlays easier to inspect without changing output.

2. [`render-resource-lifetime-plan.md`](Plans/render-resource-lifetime-plan.md)
   - Follow immediately because renderer hot-switch, resize, framebuffer, shader, mesh, and descriptor lifetimes are likely pressure points for DX12-only visual/runtime weirdness.

3. [`shader-architecture-cleanup-plan.md`](Plans/shader-architecture-cleanup-plan.md)
   - Make shader inputs, texture slots, uniform names, and cross-backend contracts explicit before expanding materials or post effects.

4. [`dx12-descriptor-upload-root-signature-plan.md`](Plans/dx12-descriptor-upload-root-signature-plan.md)
   - Keep close behind the shader work. Do not change root signatures first, but use this plan when a concrete DX12 binding or descriptor lifetime issue appears.

5. [`water-rendering-cleanup-plan.md`](Plans/water-rendering-cleanup-plan.md)
   - Tackle after render-pass boundaries exist, because water depends on reflection ownership, shader inputs, and render target contracts.

6. [`material-system-v1-implementation-plan.md`](Plans/material-system-v1-implementation-plan.md)
   - Add the material layer after shader contracts are clearer, keeping the first version renderer-neutral and compatible with existing batching.

7. [`architecture_pass_2026-06-02.md`](Plans/architecture_pass_2026-06-02.md)
   - Use as the broader checkpoint after the render-focused slices, then pull the next concrete runtime or physics boundary from it.

8. [`replay-system-plan.md`](Plans/replay-system-plan.md)
   - Useful once render and physics boundaries are easier to observe. It should consume stable capture, scene, and diagnostic APIs rather than add more runtime coupling.

9. [`worker-system-plan.md`](Plans/worker-system-plan.md)
   - High value, but wait until the physics/world/runtime boundaries are cleaner so deterministic parallelism has a safe data model.

10. [`dx12-only-engine-architecture-plan.md`](Plans/dx12-only-engine-architecture-plan.md)
    - Keep as a strategic reference. Do not start it as implementation while tri-renderer parity remains the active contract.

11. [`tornado-mode-ui-cli-plan.md`](Plans/tornado-mode-ui-cli-plan.md)
    - Useful workflow polish, but lower priority than render correctness, determinism, and architecture boundaries.

12. [`autonomous-roadmap-orchestrator-plan.md`](Plans/autonomous-roadmap-orchestrator-plan.md)
    - Process automation belongs last until the technical backlog is better sorted and the desired merge/PR rhythm is settled.

## Immediate Recommendation

Start with item 1 and keep the first implementation slice deliberately small: extract render frame context or one named pass boundary, preserve output, then run `tools\validate_renderers.bat` only at the commit/PR gate.
