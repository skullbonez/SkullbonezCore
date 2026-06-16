# Agentic Plan Order

Generated: 2026-06-16

Scope: top-level `Agentic/Plans/*.md` files plus completed foundational plans
that future agents are likely to look for. Completed, failed, rejected, and
nested historical plans should not be treated as runnable work unless this file
explicitly says so.

DX12 is the official production renderer. GL and DX11 have been retired from
runtime code; their final parity evidence is historical context only. Future
Vulkan and Metal support should influence engine contracts without becoming the
near-term implementation target.

## Completed Foundation

These plans have landed on `main` and are archived under
`Agentic/Plans/Done`. Use them as history and design context, not as active work
queues.

1. [`dx12-only-renderer-retirement-plan.md`](Plans/Done/dx12-only-renderer-retirement-plan.md)
   - DX12 is the only runtime renderer; final GL/DX11 parity evidence is
     archived under `Agentic/Reports/2026-06-15/final-legacy-renderer-parity/`.

2. [`dx12-only-engine-architecture-plan.md`](Plans/Done/dx12-only-engine-architecture-plan.md)
   - Historical umbrella direction for the DX12-first architecture. Smaller
     render, shader, resource, and runtime slices now carry concrete work.

3. [`render-resource-lifetime-plan.md`](Plans/Done/render-resource-lifetime-plan.md)
   - Current lifecycle phases, pass-resource release hooks, source records, and
     DX12 device-lost diagnostics are in place.

4. [`render-pipeline-extraction-plan.md`](Plans/Done/render-pipeline-extraction-plan.md)
   - Frame orchestration and named render pass bodies/resources are split into
     focused runtime files.

5. [`shader-architecture-cleanup-plan.md`](Plans/Done/shader-architecture-cleanup-plan.md)
   - Object material contracts, typed upload paths, shader contract checking,
     and the `t4` material table landed on `main`.

6. [`dx12-descriptor-upload-root-signature-plan.md`](Plans/Done/dx12-descriptor-upload-root-signature-plan.md)
   - The ordinary raster ABI is documented and named as `b0 + t0..t4`, with
     descriptor/upload accounting in place.

7. [`material-system-v1-implementation-plan.md`](Plans/Done/material-system-v1-implementation-plan.md)
   - The object-material v1 slice is complete. Named material assets and
     terrain/water/post material unification should be new focused plans.

8. [`agent-docs-alignment-plan.md`](Plans/Done/agent-docs-alignment-plan.md)
   - The canonical startup contract, dirty-worktree guardrails,
     plan-orchestration default, provider instructions, scoped local rules, and
     review guidance are in place.

## Active Tackle Order

1. [`orchestration-framework-fix-plan.md`](Plans/orchestration-framework-fix-plan.md)
   - YAML-first policy, queue, machines, schemas, and agent-loop files are in
     place. Use this plan next only for checker, state-advance, report-guard,
     role, hook, or workflow-eval tooling.

2. [`water-rendering-cleanup-plan.md`](Plans/water-rendering-cleanup-plan.md)
   - Continue only as focused water material/intersection-quality renderer work.
     Use `tools\validate_dx12_renderer.bat` for renderer changes.

3. [`dx12-render-graph-completion-plan.md`](Plans/dx12-render-graph-completion-plan.md)
   - The next focused DX12 architecture slice for graph-owned resource-state
     validation/execution.

4. [`architecture_pass_2026-06-02.md`](Plans/architecture_pass_2026-06-02.md)
   - Broad checkpoint for runtime scene/simulation ownership, physics data
     separation, asset maturation, parser schemas, and global coupling.

5. [`replay-system-plan.md`](Plans/replay-system-plan.md)
   - Useful once render and physics boundaries are easier to observe. It should
     consume stable capture, scene, and diagnostic APIs.

6. [`worker-system-plan.md`](Plans/worker-system-plan.md)
   - High value, but wait until physics/world/runtime boundaries are cleaner so
     deterministic parallelism has a safe data model.

7. [`tornado-mode-ui-cli-plan.md`](Plans/tornado-mode-ui-cli-plan.md)
   - Workflow polish; lower priority than render correctness, determinism, and
     architecture boundaries.

8. [`autonomous-roadmap-orchestrator-plan.md`](Plans/autonomous-roadmap-orchestrator-plan.md)
   - Superseded in direction by the YAML-first orchestration plan unless a
     future process slice explicitly reopens it.

## Immediate Recommendation

After the current YAML foundation, prefer either a focused water-rendering slice
or the DX12 render graph completion plan. Use the broad architecture pass only
to select the next small runtime, physics, asset, parser, or render-graph
boundary. Implementation work sourced from `Agentic/Plans` defaults to the
orchestrator workflow.
