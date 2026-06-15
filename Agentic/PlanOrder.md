# Agentic Plan Order

Generated: 2026-06-15

Scope: top-level `Agentic/Plans/*.md` files only. Completed, failed, rejected, and nested historical plans are excluded.

This order reflects the current product direction: DX12 is the official production renderer. GL and DX11 have been retired from runtime code; their final parity evidence is historical context only.

Future Vulkan and Metal support should influence the engine contracts without becoming the near-term implementation target. Express render passes, shader metadata, materials, resources, and synchronization in engine-owned terms first, then map those contracts to DX12 now and Vulkan/Metal later.

Retirement policy: keep DX12 screenshot/debug-layer/WARP/GBV/PIX diagnostics as the replacement confidence stack for the retired cross-renderer parity checks.

## Tackle Order

1. [`dx12-only-renderer-retirement-plan.md`](Plans/dx12-only-renderer-retirement-plan.md)
   - Active branch: `codex/dx12-only-renderer-retirement`. Backend and shader retirement is complete; finish the active documentation, baseline, and validation-tool cleanup.

2. [`dx12-only-engine-architecture-plan.md`](Plans/dx12-only-engine-architecture-plan.md)
   - Use as the umbrella architecture direction. Do not implement it as one rewrite; let it guide the smaller render/shader/resource slices below.

3. [`render-pipeline-extraction-plan.md`](Plans/render-pipeline-extraction-plan.md)
   - Resume after the DX12-only validation gate exists. Named render passes still matter, but the retirement gate must come first so GL/DX11 deletion does not weaken visual confidence.

4. [`render-resource-lifetime-plan.md`](Plans/render-resource-lifetime-plan.md)
   - Follow immediately because resize, framebuffer, shader, mesh, descriptor, and eventual device-loss lifetimes are pressure points for DX12-only visual/runtime weirdness. Keep source-vs-GPU separation because it also preserves a future Vulkan/Metal path.

5. [`shader-architecture-cleanup-plan.md`](Plans/shader-architecture-cleanup-plan.md)
   - Make shader inputs, texture slots, uniform names, and pass contracts explicit before expanding materials or post effects. Treat HLSL/DXC reflection as canonical, while keeping metadata portable enough for later SPIR-V/MSL mapping.

6. [`dx12-descriptor-upload-root-signature-plan.md`](Plans/dx12-descriptor-upload-root-signature-plan.md)
   - Keep close behind the shader work. Do not change root signatures first, but use this plan when a concrete DX12 binding, material table, descriptor, or upload lifetime issue appears.

7. [`water-rendering-cleanup-plan.md`](Plans/water-rendering-cleanup-plan.md)
   - Defer code-heavy work until after the DX12-only validation gate. Water cleanup should no longer expand GL/DX11 paths.

8. [`material-system-v1-implementation-plan.md`](Plans/material-system-v1-implementation-plan.md)
   - Add the material layer after shader contracts are clearer and the DX12-only gate exists. Keep the CPU material model backend-neutral without adding new GL/DX11 feature surface.

9. [`architecture_pass_2026-06-02.md`](Plans/architecture_pass_2026-06-02.md)
   - Use as the broader checkpoint after the render-focused slices, then pull the next concrete runtime or physics boundary from it.

10. [`replay-system-plan.md`](Plans/replay-system-plan.md)
   - Useful once render and physics boundaries are easier to observe. It should consume stable capture, scene, and diagnostic APIs rather than add more runtime coupling.

11. [`worker-system-plan.md`](Plans/worker-system-plan.md)
   - High value, but wait until the physics/world/runtime boundaries are cleaner so deterministic parallelism has a safe data model.

12. [`tornado-mode-ui-cli-plan.md`](Plans/tornado-mode-ui-cli-plan.md)
    - Useful workflow polish, but lower priority than render correctness, determinism, and architecture boundaries.

13. [`autonomous-roadmap-orchestrator-plan.md`](Plans/autonomous-roadmap-orchestrator-plan.md)
    - Process automation belongs last until the technical backlog is better sorted and the desired merge/PR rhythm is settled.

## Immediate Recommendation

Continue `dx12-only-renderer-retirement-plan.md` Phase 6: make active docs, baselines, and validation helpers consistently point at the DX12-only renderer gate.
