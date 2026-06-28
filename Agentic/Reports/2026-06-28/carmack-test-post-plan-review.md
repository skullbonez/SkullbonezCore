# Carmack-Test Post-Plan Review

Date: 2026-06-28

Scope: Current `Night-Runner-27th-June` branch after the Carmack plan
rubber-duck review and follow-up commits through `dd2a2727`. This review covers
the active non-static Carmack plans for render capability, global services,
render graph ownership, and standalone physics boundaries. The static/dynamic
allocation policy plan now has warning-bearing perf baseline evidence and an
owner phase map, but it remains planning policy work rather than a source-backed
allocation guard implementation in this pass.

Impact area: Mixed runtime ownership, physics, DX12/render, tooling,
validation, and documentation.

Validation note: This report is documentation-only. No repository validation is
required.

## Carmack-Test Verdict

Would a Carmack-style systems programmer use it? Not yet. The branch now has a
credible, better-instrumented architecture improvement path, but the evidence
still shows open ownership leaks in physics, global render service access, graph
resource lifetime, and performance proof.

Would it pass as a standalone physics engine? Not yet. A standalone smoke path
now constructs and steps `PhysicsStandaloneWorld` before worker/window/renderer
startup, but the main runtime physics facade and solver/store path still depend
on `PhysicsModelAccess` and compatibility access to game-model storage.

Would it pass a serious buy/rate bar? No, not yet. The implemented render
slices are credible and validated, but the engine still fails several
hard-checks that matter for a harsh systems review: hidden global state,
non-standalone physics boundaries, incomplete graph-owned resource lifetime, and
unresolved performance evidence.

## Evidence Ledger

| Area | Coverage | Evidence |
|------|----------|----------|
| Runtime ownership | Partial. Render frame/resource contexts now borrow narrower services, and the input callback bridge is HWND-bound, but broad service-locator debt remains. | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h:77`, `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h:119`, `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h:124`; counted globals and callback-bridge status are tracked in `Agentic/Plans/carmack-global-service-lifetime-plan.md`. |
| Encapsulation | Improving but capped. `IRenderBackend` is now a methodless temporary aggregate, and DXR is not inherited by it, but direct `Gfx()`/singleton service access remains guarded debt. | `Agentic/Plans/carmack-render-backend-capability-plan.md:514`, `Agentic/Plans/carmack-render-backend-capability-plan.md:629`, `tools/check_runtime_boundaries.py:382`. |
| Physics | Determinism is proven for the current app path, and a standalone smoke path exists; full standalone embedding is not proven because runtime physics still crosses compatibility storage. | `SkullbonezSource/Physics/PhysicsEngine.h:43`, `SkullbonezSource/Physics/PhysicsEngine.h:49`, `SkullbonezSource/Physics/PhysicsApi.cpp:907`, `SkullbonezSource/Runtime/Init.cpp:564`, `TestOutput/validation/agent_logs/physics_standalone_activation_validate_physics_final.log`. |
| DX12/render | Strong validation for the implemented slices, incomplete graph-owned lifetime. | `TestOutput/validation/agent_logs/render_dxr_capability_validate_dx12_renderer.log:18`, `TestOutput/validation/agent_logs/render_dxr_capability_validate_dx12_renderer.log:422`, `SkullbonezSource/Runtime/RunRender.cpp:189`, `SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp:203`, `SkullbonezSource/Rendering/DX12/BLASDX12.cpp:194`, `SkullbonezSource/Rendering/DX12/TLASDX12.cpp:205`. |
| Performance | Insufficient for a strong yes. A perf run exists, but it is not machine-comparable and warns on physics bench frame timing. | `TestOutput/validation/agent_logs/render_resource_context_validate_perf.log:239`, `TestOutput/validation/agent_logs/render_resource_context_validate_perf.log:453`, `TestOutput/validation/agent_logs/render_resource_context_validate_perf.log:463`. |
| Diagnostics | Decent guardrails and validation logs, plus standalone physics smoke output; no new focused SkullScope proof was added in this pass. | `tools/check_runtime_boundaries.py:79`, `tools/check_runtime_boundaries.py:120`, `tools/check_runtime_boundaries.py:386`, `SkullbonezSource/Runtime/Init.cpp:564`. |
| Validation | Strong for the source slices already committed; docs-only inventory commits correctly require no validation. | `TestOutput/validation/agent_logs/render_dxr_capability_validate_full.log:432`, `TestOutput/validation/agent_logs/render_dxr_capability_validate_full.log:469`, `TestOutput/validation/agent_logs/render_dxr_capability_validate_full.log:479`. |
| Data/assets | Not deeply reviewed in this pass. Existing repository rules require registered reusable assets, but these plans did not touch assets or scenes. | Insufficient focused evidence for this scope. |
| Maintainability | Better than before. Plans now carry inventories and checklists, and source slices added local comments for context/lifetime. Still not simple enough. | `Agentic/Reports/2026-06-28/carmack-plan-rubber-duck-review.md:5`, `Agentic/Plans/carmack-render-backend-capability-plan.md:504`, `Agentic/Plans/carmack-render-graph-resource-ownership-plan.md:123`, `Agentic/Plans/carmack-physics-standalone-boundary-plan.md:81`. |

## Worst Things

1. Blocking: Physics is still not standalone. The public facade still exposes
   `PhysicsModelAccess&`, and the solver/store still crosses into model-backed
   compatibility state. Evidence: `SkullbonezSource/Physics/PhysicsEngine.h:43`,
   `SkullbonezSource/Physics/PhysicsEngine.h:49`,
   `SkullbonezSource/Physics/PhysicsWorld.cpp:631`,
   `SkullbonezSource/Physics/PhysicsBodyStore.cpp:96`, and runtime compatibility
   borrowers in `SkullbonezSource/Runtime/RunFrame.cpp:676` and
   `SkullbonezSource/Runtime/RunFrame.cpp:1632`.
2. Blocking for a strong engine verdict: Global service-locator debt remains
   large. The counted surface still includes `Cfg()`, `Gfx()`,
   `GfxRayTracing()`, `WorkerPool::Instance()`, and other singleton/global
   paths. Evidence: the counted global-service surface in
   `Agentic/Plans/carmack-global-service-lifetime-plan.md`
   and `tools/check_runtime_boundaries.py:386`.
3. Blocking for render-graph completion: The graph still does not own all
   resource lifetime, descriptor lifetime, and barrier ordering. Transitional
   `Unknown` imports and backend-owned DX12 barriers remain. Evidence:
   `SkullbonezSource/Runtime/RunRender.cpp:189`,
   `SkullbonezSource/Rendering/RenderPipeline.cpp:118`,
   `SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp:203`,
   `SkullbonezSource/Rendering/DX12/BLASDX12.cpp:194`, and
   `SkullbonezSource/Rendering/DX12/TLASDX12.cpp:205`.
4. Missing evidence: Performance cannot score above the skill cap. The latest
   relevant perf evidence warns that comparison is machine-invalid and reports
   physics-bench frame timing regressions. Evidence:
   `TestOutput/validation/agent_logs/render_resource_context_validate_perf.log:239`,
   `TestOutput/validation/agent_logs/render_resource_context_validate_perf.log:453`.

## Best Things

1. The DX12 validation story is real. The DXR capability slice passed with zero
   DX12 validation errors and matching screenshots. Evidence:
   `TestOutput/validation/agent_logs/render_dxr_capability_validate_dx12_renderer.log:18`
   and `TestOutput/validation/agent_logs/render_dxr_capability_validate_dx12_renderer.log:422`.
2. Deterministic physics is protected on the current runtime path, and
   `--physics-standalone-smoke` exercises an isolated `PhysicsStandaloneWorld`
   before runtime startup. Evidence:
   `SkullbonezSource/Runtime/Init.cpp:564`,
   `SkullbonezSource/Physics/PhysicsApi.cpp:907`, and
   `TestOutput/validation/agent_logs/physics_standalone_activation_validate_physics_final.log`.
3. The new plan inventories make future slices less hand-wavy. The render
   backend, render graph, and physics plans now list current debt and concrete
   remaining work instead of claiming broad completion. Evidence:
   `Agentic/Plans/carmack-render-backend-capability-plan.md:504`,
   `Agentic/Plans/carmack-render-graph-resource-ownership-plan.md:123`, and
   `Agentic/Plans/carmack-physics-standalone-boundary-plan.md:81`.
4. Runtime boundary checks now guard the most dangerous regressions: global
   service growth, render graph `Unknown` access, wide backend methods, DXR on
   `IRenderBackend`, and physics compatibility borrowers. Evidence:
   `tools/check_runtime_boundaries.py:120`, `tools/check_runtime_boundaries.py:386`,
   `tools/check_runtime_boundaries.py:1612`, and
   `TestOutput/validation/agent_logs/render_dxr_capability_runtime_boundaries.log:2`.

## Scorecard

| Area | Score / 5 | Evidence | Reason |
|------|-----------|----------|--------|
| Runtime ownership and composition-root discipline | 3 | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h:124`; `Agentic/Plans/carmack-global-service-lifetime-plan.md` | Better explicit contexts, but global service count is still broad. |
| Encapsulation and dependency direction | 2 | `Agentic/Plans/carmack-render-backend-capability-plan.md:514`; `tools/check_runtime_boundaries.py:386` | `IRenderBackend` is narrowing, but the service-locator path caps the score. |
| Physics correctness, determinism, and data boundaries | 3 | `TestOutput/validation/agent_logs/render_dxr_capability_validate_full.log:469`; `SkullbonezSource/Physics/PhysicsEngine.h:49` | Determinism is strong; standalone data boundaries are not. |
| Performance model and allocation behavior | 2 | `TestOutput/validation/agent_logs/render_resource_context_validate_perf.log:239`; `TestOutput/validation/agent_logs/render_resource_context_validate_perf.log:453` | Skill cap applies because targeted performance evidence is not clean. |
| Render graph/resource lifetime/DX12 validation | 3 | `TestOutput/validation/agent_logs/render_dxr_capability_validate_dx12_renderer.log:422`; `SkullbonezSource/Runtime/RunRender.cpp:189` | DX12 validation is strong, graph ownership remains partial. |
| Debuggability and observability | 3 | `tools/check_runtime_boundaries.py:79`; `tools/check_runtime_boundaries.py:120` | Guardrails and diagnostics exist; no new focused diagnostic proof was added here. |
| Test, baseline, and validation integrity | 4 | `TestOutput/validation/agent_logs/render_dxr_capability_validate_full.log:479` | Recent source slices have meaningful gates and byte-exact/DX12 evidence. |
| Data-driven assets/scenes/configuration | 3 | Insufficient focused evidence | Repository rules are clear, but this pass did not inspect enough asset/scene behavior for a stronger score. |
| Maintainability and code-reading cost | 3 | `Agentic/Reports/2026-06-28/carmack-plan-rubber-duck-review.md:5` | The work is more honest and navigable, but still carries too many compatibility bridges. |

## Physics Engine Suitability

The engine has credible deterministic physics evidence for the integrated
runtime path, and it now has a standalone smoke command that constructs and
steps `PhysicsStandaloneWorld` without `Run`, renderer, scene parser, or
`GameModelCollection`. The hard blocker is the main runtime API shape:
`PhysicsEngine` still asks callers to provide `PhysicsModelAccess&`, and
`PhysicsWorld`/`PhysicsBodyStore` still move state through the legacy
model-compatibility layer. That makes the physics core useful inside
SkullbonezCore but not yet cleanly embeddable as a standalone physics engine.

The minimum acceptance path is still the plan's own checklist: replace one
step-critical model-backed store dependency with handle/view-owned state, expand
runtime callers toward durable physics handles, and prove deterministic
fixed-step output after each physics-visible migration.

## Robustness And Encapsulation

Robustness improved most in the render code. `RenderResourceContext` separates
GPU resource creation from draw-time frame context, and `IRenderRayTracing*`
is now borrowed through frame inputs rather than fetched inside the reflection
pass. The downside is that many old compatibility paths remain deliberately
counted rather than eliminated.

Encapsulation is therefore better but not yet simple. The global-service
ratchet is useful as a tripwire, but the system still accepts many globals as
current reality. The physics compatibility bridge is similarly honest but still
not the destination.

## Performance Judgment

Performance remains a weak point for the Carmack-style verdict. No final clean,
machine-comparable perf run supports a stronger score, and the most relevant
perf log from this pass warns that DX12 comparison is invalid across machines
while reporting frame-time warnings in `physics_bench`.

That does not prove the slices made performance worse, but it does mean the
branch cannot claim a serious performance bar yet.

## Required Fixes Before A Strong Yes

1. Move the first step-critical physics datum out of `PhysicsModelAccess` and
   into store/handle-owned state. Expected evidence: source diff,
   `tools\validate_physics.bat`, and a reviewer pass focused on deterministic
   ordering.
2. Migrate one compatibility borrower group from model indices to durable
   physics handles or immutable body views. Expected evidence: source diff,
   focused smoke evidence for stale/invalid handles, and `tools\validate_physics.bat`.
3. Migrate one graph-owned target family from backend-owned lifetime to explicit
   graph/imported-state ownership. Expected evidence: no new `Unknown` imports,
   `tools\validate_dx12_renderer.bat`, and perf evidence if transient
   allocation or descriptor reuse changes.
4. Classify and shrink one non-render global-service group, not just count it.
   Expected evidence: lowered allowlist, direct runtime-boundary pass, and a
   plan update that names the remaining owner.
5. Produce clean targeted performance evidence on a comparable machine or a
   refreshed accepted baseline. Expected evidence: `tools\validate_perf.bat`
   output with comparison validity explained.

## Validation Gaps

- Static/dynamic allocation policy has warning-bearing baseline evidence, but
  no runtime allocation guard or static checker implementation landed in this pass.
- Standalone physics smoke exists, but it does not prove the main runtime
  physics boundary is independent of `PhysicsModelAccess`.
- No graph-owned transient allocation/descriptor lifetime proof exists yet.
- Latest relevant perf evidence is warning-bearing and machine-limited.
- Data/assets were not deeply reviewed in this focused pass.
