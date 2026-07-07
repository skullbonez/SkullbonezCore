# Overnight Blockers - 2026-07-07

Single remaining blocker ledger for the 7 July nightrunner pass. Current
remaining blocker count: 26. Rows are grouped by plan and ordered
hardest-to-unblock first inside each plan.

## Resolved During Remediation

- **PHYS-035** - `SkullbonezSource/Runtime/Replay/RunReplayPredictionVisualizer.inl` / `StepReplayPredictionJob mutation window`
  - Resolved 2026-07-07 through fable-03 phases 1, 2, and 4.
  - Result: replay prediction now seeds and steps a private replay-owned
    `PhysicsEngine`, deletes the live apply/restore mutation window, proves
    `liveSolverHashStableAcrossPrediction`, and has a runtime-boundary
    guardrail forbidding prediction restore calls against the live engine.
  - Evidence: commit `d475830d` completed the private-engine implementation
    with `tools\validate_physics.bat`, the ragdoll prediction interaction
    proof, `tools\validate_perf.bat`, and `tools\validate_full.bat`; the phase
    4 checker self-test and repo scan passed in the follow-up slice.

- **SVC-032** - `SkullbonezSource/UI/UITabProfiler.cpp` / `Gfx draw trace`
  - Resolved 2026-07-07 through the fable-02 UI profiler snapshot slice.
  - Result: `UITabProfiler` consumes draw-call trace rows from
    `ProfilerTab::FrameSnapshot`, filled by `RunUiTextPass` through the explicit
    `IRenderDiagnostics` borrow, and has 0 direct `Gfx()`/`IsGfxReady()` hits.
  - Evidence: checker global-service census lowered from 141 to 133; boundary
    self-test/scan, `tools\validate_fast.bat`, and `tools\validate_full.bat`
    passed for the slice.

- **SVC-033** - `SkullbonezSource/UI/UITabProfiler.cpp` / `Profiler::Instance`
  - Resolved 2026-07-07 through the same fable-02 UI profiler snapshot slice.
  - Result: `UITabProfiler` consumes marker rows, timeline data, expansion
    state, and worker-core samples from `ProfilerTab::FrameSnapshot`, with 0
    direct `Profiler::Instance()` hits remaining in the tab.
  - Evidence: checker allowlist rows for `UITabProfiler.cpp` were removed;
    boundary self-test/scan, `tools\validate_fast.bat`, and
    `tools\validate_full.bat` passed for the slice.

- **SVC-022** - `SkullbonezSource/Runtime/RuntimeDiagnostics.cpp` / `Profiler::Instance CSV`
  - Resolved 2026-07-07 through the fable-02 profiler diagnostics receiving-path
    slice.
  - Result: `RuntimeDiagnostics.cpp` and `RunUiTextPass.cpp` now have 0 direct
    `Profiler::Instance()` hits. Runtime startup resolves the sanctioned
    profiler singleton once in `Init.cpp`, then diagnostics/perf CSV, frame-time
    sampling, RuntimeRenderer, and the UI text pass consume the nullable
    startup-bound `Profiler*`.
  - Evidence: checker global-service census lowered from 133 to 129; boundary
    self-test/scan passed, `tools\validate_fast.bat` passed after targeted
    header formatting, and `tools\validate_full.bat` passed with DX12 validation
    errors 0, screenshots matched, and `physics_regression_solver.csv`
    byte-exact.

## Plan 02 - Physics Store Authority

- **PHYS-020** - `SkullbonezSource/Physics/PhysicsScene.h` / `m_world/m_bodyStore/m_colliderStore/m_renderInstanceStore`
  - Attempted: defer-row authority inspection of `PhysicsEngine`, `PhysicsScene`, `EngineContext`, and scene setup ownership.
  - Failure/reason: `PhysicsEngine` is still owned by `GameModelCollection`, `EngineContext` still borrows it through `GetPhysicsEngine`, and scene setup still passes models plus physics together; promoting ownership now interleaves PHYS-004, PHYS-009, and PHYS-025/PHYS-027.
  - Needed to unblock: dependency rows PHYS-004, PHYS-009, PHYS-025, and PHYS-027, or a human-awake ownership design for the runtime physics owner.

- **PHYS-004** - `SkullbonezSource/GameObjects/GameModelCollection.h` / `m_physicsEngine`
  - Attempted: defer-row inspection of moving physics engine ownership out of `GameModelCollection`.
  - Failure/reason: the ownership move cannot be completed as a one-row slice while broad `GetPhysicsEngine` and scene creation/load authority rows remain open; the failure mode is not verifiable in isolation.
  - Needed to unblock: complete PHYS-009 plus scene creation/load authority rows, or decide the new physics owner boundary in a human-awake session.

- **PHYS-009** - `SkullbonezSource/GameObjects/GameModelCollection.h` / `GetPhysicsEngine`
  - Attempted: caller census and deletion feasibility check.
  - Failure/reason: deleting `GameModelCollection::GetPhysicsEngine` requires migrating 124 references across runtime input, editor tools, replay prediction, scene setup/load, rendering, and snapshot writing; it overlaps PHYS-020 and PHYS-025/PHYS-026/PHYS-027.
  - Needed to unblock: narrow physics command/store APIs plus completed scene/replay caller migrations.

- **PHYS-027** - `SkullbonezSource/Runtime/Scene/RunScene.cpp` / `SetupAuthoredScene models/physics`
  - Attempted: defer-row inspection of authored/generated setup contexts.
  - Failure/reason: `RunScene` still constructs setup contexts with `GameModelCollection` and `PhysicsEngine` side by side; replacing them is the full scene creation pipeline split and depends on PHYS-020/PHYS-025.
  - Needed to unblock: entity, physics, and render setup context design after physics ownership is moved.

- **PHYS-025** - `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.h` / `SceneAuthoredSetupContext.models`
  - Attempted: scene setup authority inspection.
  - Failure/reason: authored scene setup still borrows `GameModelCollection` and `PhysicsEngine` together; owner-specific builders require the PHYS-027 scene creation pipeline split.
  - Needed to unblock: PHYS-027 and a builder API that creates scene entities and physics rows through their owners.

- **PHYS-026** - `SkullbonezSource/Runtime/Scene/RunScene.cpp` / `m_cGameModelCollection.Clear`
  - Attempted: scene reset ownership inspection.
  - Failure/reason: scene load and generated-count reset still clear `GameModelCollection` directly; moving reset ownership requires PHYS-018 plus replay/profiler follow-up behavior.
  - Needed to unblock: subsystem reset split and explicit replay/profiler reset ordering decision.

- **PHYS-016** - `SkullbonezSource/GameObjects/GameModelCollection.cpp` / `AppendGameModelAndPhysicsRows`
  - Attempted: append transaction split inspection.
  - Failure/reason: the method still creates entity rows, group metadata, body registration, and collider registration in one collection-owned transaction.
  - Needed to unblock: PHYS-017-style scene entity append ownership and PHYS-025/PHYS-027 scene creation pipeline changes.

- **PHYS-018** - `SkullbonezSource/GameObjects/GameModelCollection.cpp` / `Clear`
  - Attempted: clear/reset split inspection.
  - Failure/reason: `Clear` still resets scene entities, group metadata, and `PhysicsEngine` together; the real reset owner is the scene load lifecycle covered by PHYS-026/PHYS-027.
  - Needed to unblock: scene lifecycle reset owner with explicit subsystem reset contracts.

- **PHYS-021** - `SkullbonezSource/Physics/PhysicsWorld.h` / `m_spatialGrid/m_candidatePairs/m_timeRemaining`
  - Attempted: broadphase/sleep/contact/narrowphase scratch split inspection.
  - Failure/reason: the row itself requires authority migration first; PHYS-020 remains blocked and `PhysicsWorld` still owns hot scratch reserves together.
  - Needed to unblock: PHYS-020 plus performance and determinism validation for the split.

- **PHYS-022** - `SkullbonezSource/Physics/PhysicsWorld.h` / `m_persistentContacts and scratch arrays`
  - Attempted: contact diagnostics split inspection.
  - Failure/reason: persistent contacts, contact cache, solver stats, debug contacts, diagnostics views, and replay snapshots remain coupled through `PhysicsWorld`/`PersistentContactSolverContext`.
  - Needed to unblock: deterministic store authority from PHYS-020/PHYS-021, then byte-exact `validate_physics`.

- **PHYS-012** - `SkullbonezSource/GameObjects/GameModelCollection.cpp` / `ReserveForActiveGameModelCapacity`
  - Attempted: capacity-policy relocation inspection.
  - Failure/reason: collection still owns scene entity storage plus physics/render sidecar reservation, so moving only the clamp/config call would be cosmetic and would not make the collection a view.
  - Needed to unblock: scene/entity creation-storage split so capacity policy can live with the real owner.

## Plan 05 - Render Graph Backend Split

- **RGRAPH-003** - `SkullbonezSource/Rendering/IRenderCommandContext.h` / `IRenderCommandContext`
  - Attempted: split graph transient materialization into a new `IRenderGraphResourceContext` facet and thread it through runtime render inputs.
  - Failure/reason: two validation attempts failed; the first missed project-filter classification for the new rendering header, and the second was rejected by `tools/check_runtime_boundaries.py` as unapproved new inherited backend facet.
  - Needed to unblock: approved stable-boundary/inheritance budget or a design that removes graph materialization without a temporary aggregate facet.

- **RGRAPH-004** - `SkullbonezSource/Rendering/IRenderResourceFactory.h` / `IRenderResourceFactory`
  - Attempted: CodeGraph inspection of resource factory callers and possible narrower capabilities.
  - Failure/reason: no existing narrower resource-factory capability exists for shader, mesh, framebuffer, texture, dynamic-VB, and instancing callers; splitting safely needs interface design and inheritance approval.
  - Needed to unblock: approved resource capability design and inheritance budget.

- **RGRAPH-007** - `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h` / `RenderBackendDX12`
  - Attempted: defer-row concrete backend split inspection.
  - Failure/reason: `RenderBackendDX12` remains the aggregate implementation for raytracing, resource, graph, capture, diagnostics, and lifecycle services; it depends on blocked resource/API split rows and later concrete-owner rows.
  - Needed to unblock: RGRAPH-003/RGRAPH-004/RGRAPH-010 plus concrete ownership design.

- **RGRAPH-010** - `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h` / backend public API
  - Attempted: safe-row inspection after caller-facing runtime access moved to existing facets.
  - Failure/reason: the remaining public surface is now the concrete owner split covered by deferred backend rows, not a small safe caller migration.
  - Needed to unblock: resolve concrete backend ownership rows RGRAPH-007 and RGRAPH-022 through RGRAPH-029.

- **RGRAPH-014** - `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp` / `MaterializeGraphTransientResources`
  - Attempted: materializer extraction inspection.
  - Failure/reason: materialization owns DX12 resource creation, RTV/DSV/SRV/UAV descriptor allocation, texture-handle registration, pool-slot reuse, stats, and binding lookup through the aggregate command context.
  - Needed to unblock: RGRAPH-003/RGRAPH-004, then a real transient owner instead of a bridge.

- **RGRAPH-029** - `SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.cpp` / DX12 graph executor
  - Attempted: replay/debug-overlay callback execution inspection.
  - Failure/reason: selected passes already execute through API-neutral `RenderGraph::ExecuteCallbacks` inside `RuntimeRenderer`; moving them under the DX12 executor would require a new graph execution capability or concrete DX12 dependency plus overlay/replay proofs.
  - Needed to unblock: RGRAPH-003/RGRAPH-014 and a real graph execution capability with tracked interaction proofs.

- **RGRAPH-022** - `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp` / `InitDXR`
  - Attempted: DXR owner split inspection.
  - Failure/reason: DXR extraction reaches private device5 and command-list lifetime, descriptor allocation, reflection resource state, BLAS/TLAS/SBT state, graph barriers, and GPU waits.
  - Needed to unblock: RGRAPH-007/RGRAPH-010/RGRAPH-003 and a non-bridge `RayTracingBackendDX12` owner design.

- **RGRAPH-024** - `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp` / `CreateTexture2D`
  - Attempted: texture manager extraction inspection.
  - Failure/reason: texture creation still spans device resource creation, upload reservations, descriptor allocation, graph transitions/UAV barriers, mip compute state, SRV registration, and the backend texture handle vector.
  - Needed to unblock: resource factory/context and concrete backend authority split.

- **RGRAPH-023** - `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp` / `CreatePSO`
  - Attempted: pipeline cache extraction inspection.
  - Failure/reason: PSO creation and draw prep still read backend-owned root signature, shader bytecode, raster/depth/blend flags, current RTV format, command list, and fixed cache array.
  - Needed to unblock: RGRAPH-003/RGRAPH-007/RGRAPH-010/RGRAPH-029 and graph pass execution stability.

## Plan 03 - Explicit Service Contexts

- **SVC-001** - `SkullbonezSource/Rendering/IRenderBackend.cpp` / `s_gfxBackend`
  - Attempted: central renderer singleton migration assessment.
  - Failure/reason: active renderer singleton cannot be startup-only while compatibility callers still exist in physics debug visualizers, window resize, capture facade, and draw-call trace helpers. The profiler marker/timer dependency was removed by the 2026-07-07 Profiler renderer-diagnostics bind; `RunPasses.cpp` no longer uses the global readiness helper, and the stale `RunInput.cpp` readiness allowance has been deleted.
  - Needed to unblock: finish the remaining non-profiler renderer-service cleanup or approve a bounded compatibility decision.

- **SVC-002** - `SkullbonezSource/Rendering/IRenderBackend.cpp` / `Gfx`
  - Attempted: service locator deletion assessment.
  - Failure/reason: `Gfx()` still serves the backend facade, physics debug visualizers, capture compatibility, and draw-call trace helpers; the checker ratchet prevents growth but deletion needs dependent cleanup. Core profiler no longer depends on it, and the tornado visual pass now uses its explicit frame command context.
  - Needed to unblock: migrate remaining callers to explicit render capabilities and then delete the facade accessor.

## Plan 01 - Run Composition Root

- **RUN-010** - `SkullbonezSource/Runtime/RunInput.cpp` / `Run::TakeInput`
  - Attempted: defer-row inspection of semantic input routing.
  - Failure/reason: the method routes camera mode changes, editor transitions, replay scrub/velocity/cause-tree state, scene commands, diagnostics, UI stress, and interaction ownership through Run-private callbacks; available `validate_full` plus tracked proofs do not verify full command-routing equivalence after moving it behind `InputController` and a command queue.
  - Needed to unblock: human-awake decomposition into explicit command events and coverage for the command routing matrix.

- **RUN-011** - `SkullbonezSource/Runtime/RunInput.cpp` / `Run::DrainRuntimeCommands`
  - Attempted: defer-row dispatcher extraction inspection.
  - Failure/reason: the queue drains scene loading, cinematic mode, screenshots/default saves, scene creation, scene-advance quit policy, and replay command event logging in one ordered loop; `validate_full` does not prove every command variant.
  - Needed to unblock: explicit subsystem command handlers and ordering decisions.

- **RUN-009** - `SkullbonezSource/Runtime/Scene/RunScene.cpp` / `Run::LoadScene`
  - Attempted: defer-row scene load phase extraction inspection.
  - Failure/reason: the next meaningful extraction crosses Run-only interaction/camera hooks, diagnostics/capture reset, model teardown/population ordering, render backend state, and preserve-runtime semantics; available gates do not verify all authored/generated/interactive/preserve-runtime load paths.
  - Needed to unblock: human-awake design slice or narrower dependency rows for scene-load ordering.

- **RUN-015** - `SkullbonezSource/Runtime/RunFrame.cpp` / `Run::UpdateLogic`
  - Attempted: defer assessment against `Run::TickPhysics`, `UpdateLogic`, and `SimulationController`.
  - Failure/reason: moving fixed-step loop ownership would require callback-style pre/post hooks inside the hot loop or a wider transfer of Run-owned model, world, worker, replay, contact-audio, and manipulator responsibilities; available gates/proofs cannot verify camera, input, replay, scene automation, and deterministic physics ordering.
  - Needed to unblock: broader SimulationController ownership decision or narrower pre/post tick dependency rows with determinism proof.
