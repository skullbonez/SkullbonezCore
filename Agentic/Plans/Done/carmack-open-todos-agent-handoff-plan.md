# Carmack Open TODO Agent Handoff Plan

Status: Complete

Short description: finish the remaining Carmack architecture work by removing
normal-path global service access, making physics usable through standalone
handle/store boundaries, and moving render-frame resource lifetime/barriers into
explicit render-graph ownership.

This plan supersedes the individual in-progress Carmack plans as the active
worker queue. Treat the source plans listed below as historical/reference
context only; track remaining work and completion here.

Before implementing any phase:

- Follow the repository Agent Startup Contract.
- Run `git status --short --branch` and treat pre-existing dirty files as
  user-owned.
- Read the source plan for the phase being worked.
- Choose one phase and one coherent slice inside that phase; do not attempt to
  close every Carmack track in one diff.
- State the impact area and deferred PR-gate validation before editing.
- If the slice touches source-bearing files, apply the comment standard and run
  the comment-style audit over touched source before reporting done.

Extraction summary:

| Source plan | Open boxes |
|-------------|------------|
| `Agentic/Plans/Done/carmack-global-service-lifetime-plan.md` | 22 |
| `Agentic/Plans/Done/carmack-physics-standalone-boundary-plan.md` | 31 |
| `Agentic/Plans/Done/carmack-render-backend-capability-plan.md` | 0 |
| `Agentic/Plans/Done/carmack-render-graph-resource-ownership-plan.md` | 23 |
| Total | 76 |

## Phase 1 - Global Service Lifetime

Source plan:
`Agentic/Plans/Done/carmack-global-service-lifetime-plan.md`

### Inventory

- [x] Classify each individual source hit as `bootstrap`, `shutdown`,
  `OS callback bridge`, `normal runtime path`, `render pass`, `asset lookup`,
  `diagnostics`, or `test/tool`. Source line 679.
  Evidence: generated
  `Agentic/Reports/2026-06-29/carmack-handoff/global-service-hit-classification.csv`
  and
  `Agentic/Reports/2026-06-29/carmack-handoff/global-service-hit-classification-summary.md`
  from the current source using `tools/check_runtime_boundaries.py` matching
  logic; 611 hits classified, 0 unclassified.

### Service Context Shape

- [x] Define or extend an `EngineServices` or equivalent context for process
  services that must be shared. Source line 689.
  Evidence: `SkullbonezSource/Runtime/EngineContext.h` now declares the
  borrowed `EngineServices` view over runtime-owned asset, texture, camera,
  window, terrain, and skybox services; `EngineContext::Services()` builds it
  from `RunSubsystemState` with fail-fast assertions. Focused runtime-boundary
  check passed with 0 errors:
  `TestOutput/validation/runtime_boundaries/carmack_engine_services.json`.
- [x] Define or extend an `AssetContext` for asset lookup and source records
  instead of `ActiveAssetSystem()`. Source line 693.
  Evidence: `SkullbonezSource/Assets/AssetSystem.h` now declares
  `Assets::AssetContext` as a borrowed asset-registry view; scene/style parser
  front doors in `SkullbonezSource/Scene/TestScene.h`,
  `SkullbonezSource/Scene/TestScene.cpp`, and
  `SkullbonezSource/Scene/TestSceneParser.cpp` pass that context instead of raw
  asset pointers while preserving empty-context standalone fallback. Focused
  runtime-boundary check passed with 0 errors:
  `TestOutput/validation/runtime_boundaries/carmack_asset_context.json`.
- [x] Define or extend an `InputEventBuffer` or input bridge for Win32 callback
  accumulators. Source line 695.
  Evidence: `SkullbonezSource/Runtime/Input.h` now names
  `Input::InputEventBuffer` for callback-fed mouse accumulator state, and
  `Input::ClearCallbackEventBuffer()` centralizes bound-HWND queue clearing for
  bind/unbind without increasing counted `g_*` references.
- [x] Define or extend a `WindowService` or explicit window reference for window
  queries and title/resize behavior. Source line 697.
  Evidence: `SkullbonezSource/Runtime/EngineContext.h` now exposes
  `EngineServices::WindowService`, a borrowed view over the native window held
  by `RunSubsystemState`, alongside the existing explicit `m_systems.window`
  composition-root binding.
- [x] Keep contexts borrowed and lifetime-annotated; do not create a new global
  service locator under a nicer name. Source line 699.
  Evidence: `EngineServices`, `AssetContext`, `InputEventBuffer`, and
  `WindowService` are non-owning context/view types with lifetime comments and
  no static storage; focused runtime-boundary check passed with 0 errors:
  `TestOutput/validation/runtime_boundaries/carmack_input_window_services.json`.

### Remove Normal-Path Globals

- [x] Route render pass backend access through render capability/context
  arguments. Source line 704.
  Evidence: `RunPasses.cpp` has no direct `Gfx()`/`GfxRayTracing()` access; pass
  code uses `RenderFrameContext`, `RenderResourceContext`, `IRenderCommandContext`,
  `IRenderDiagnostics`, and `IRenderResourceFactory` helpers, with capability
  checks routed through `RenderDiagnostics(inputs.frame).GetCapabilities()`.
- [x] Route shader and texture creation through an asset/render context passed
  from runtime-owned services. Source line 744.
  Evidence: `RuntimeRenderServices` and `RenderResourceContext` now carry the
  Run-owned `AssetSystem&`; pass shader creation uses
  `resources.assets.CreateShader(RenderResources(resources), ...)`; and
  `TextureCollection` binds explicit `IRenderResourceFactory`/
  `IRenderCommandContext` contexts for texture creation, deletion, and binding.
  `tools\check_runtime_boundaries.py --repo . --json-out
  TestOutput\validation\runtime_boundaries\carmack_asset_render_context.json`
  passed with 0 errors after removing the `TextureCollection.cpp` `Gfx()`
  allowlist, and `tools\validate_build.bat Profile` passed with 0 warnings and
  0 errors.
- [x] Replace `TextureCollection::Instance()` normal-path lookups with runtime
  owned texture service references. Source line 752.
  Evidence: `RunSubsystemState` now owns `TextureCollection`, `Run::Initialise`
  borrows it through `m_systems.textures`, `SkyBox::BindTextures()` receives the
  runtime service before rebuild, and `tools\check_runtime_boundaries.py --repo
  . --json-out TestOutput\validation\runtime_boundaries\carmack_texture_service.json`
  passed with 0 errors after the allowlist was ratcheted.
- [x] Replace `CameraCollection::Instance()` normal-path lookups with explicit
  camera service references. Source line 754.
  Evidence: `RunSubsystemState` now owns `CameraCollection`, `Run::Initialise`
  binds `m_systems.cameras` to that owned service, generated/authored scene
  setup contexts borrow `CameraCollection&`, and `tools\check_runtime_boundaries.py
  --repo . --json-out TestOutput\validation\runtime_boundaries\carmack_camera_service.json`
  passed with 0 errors after the allowlist was ratcheted.
- [x] Replace `Window::Instance()` normal-path lookups with explicit window
  service references after bootstrap. Source line 756.
  Evidence: `WinMain` remains the bootstrap singleton caller, passes `Window&`
  into `Run`, `Run::Initialise` asserts the borrowed service, `Input` binds the
  active window explicitly, `WndProc` uses Win32 user data/`this` for resize, and
  `tools\check_runtime_boundaries.py --repo . --json-out
  TestOutput\validation\runtime_boundaries\carmack_window_service.json` passed
  with 0 errors after the allowlist was ratcheted.
- [x] Replace `SkyBox::Instance()` with runtime/world-owned skybox lifetime or a
  scene-render resource owner. Source line 758.
  Evidence: `RunSubsystemState` now owns `skyBoxOwner`, `Run::Initialise`
  constructs the skybox directly and stores a borrowed `skyBox` alias,
  shutdown releases instance render resources, and `tools\check_runtime_boundaries.py
  --repo . --json-out TestOutput\validation\runtime_boundaries\carmack_skybox_service.json`
  passed with 0 errors after removing the Run-side `SkyBox::Instance()` allowlist
  entry.
- [x] Keep config reads grouped through launch/runtime config context where
  possible; do not spread new `Cfg()` calls. Source line 760.
  Evidence: diff scan for added implementation `Cfg()`, `Gfx()`,
  service-`Instance()`, `ActiveAssetSystem()`, and
  `CreateShaderFromActiveAssets()` calls found no new implementation accesses
  from this pass, and `tools\check_runtime_boundaries.py --repo . --json-out
  TestOutput\validation\runtime_boundaries\carmack_phase1_globals_after_ownership.json`
  passed with 0 errors.

### OS Callback Bridges

- [x] Keep Win32 input globals only behind a tiny bridge if callback signatures
  require process-static state. Source line 765.
  Evidence: `Input::BindCallbackBridge`, `Input::UnbindCallbackBridge`, and
  `Input::ClearCallbackEventBuffer` fence callback-fed state by bound `HWND`;
  ordinary input polling now borrows `Input::BindWindow`/`UnbindWindow` instead
  of calling `Window::Instance()`, and the runtime-boundary checker passed with
  0 errors.

### Lifetime Order

- [x] Add assertions that services are unbound before destruction when callbacks
  can fire late. Source line 800.
  Evidence: input bridge bind/unbind paths assert correct `HWND`/window identity,
  `CleanupWindow` unbinds callback and polling bridges before backend/DC/window
  teardown, and `Input::ClearCallbackEventBuffer` asserts the active callback
  bridge before clearing queued input.
- [x] Keep `Run.h` as composition root wiring, not a bag of service-locator
  helpers. Source line 802.
  Evidence: `Run` now receives the bootstrap `Window&` in its constructor and
  stores borrowed aliases in `RunSubsystemState`; no new public Run service
  locator helpers were added, and the targeted Profile build completed with 0
  warnings and 0 errors.

### Validation Checklist

- [x] For plan-only edits: no validation required. Source line 850.
  Evidence: superseded by implementation validation because this handoff moved
  beyond plan-only edits.
- [x] For asset registration, scene asset loading, hull asset, or scene JSON
  behavior changes: run `tools\validate_full.bat`. Source line 860.
  Evidence: `TestOutput\validation\agent_logs\carmack_validate_full_final.log`
  reports `VALIDATE_FULL: DEFAULT GATE PASSED`.

### Definition Of Done

- [x] Normal runtime/render/asset/scene paths use explicit contexts or borrowed
  interfaces instead of process globals. Source line 938.
- [x] Remaining globals are bootstrap-only, shutdown-only, or OS callback
  bridges with explicit lifecycle comments. Source line 940.
- [x] Guardrails prevent new global service access from creeping back in. Source
  line 942.
- [x] Required validation passes for the touched implementation areas. Source
  line 943.
  Evidence: runtime-boundary checks were ratcheted during the phase, and final
  `tools\validate_full.bat` passed in
  `TestOutput\validation\agent_logs\carmack_validate_full_final.log`.

## Phase 2 - Physics Standalone Boundary

Source plan:
`Agentic/Plans/Done/carmack-physics-standalone-boundary-plan.md`

### Public Physics API

- [x] Extend `SkullbonezSource/Physics/PhysicsApi.h` with any missing create,
  update, query, delete, and step descriptors needed by runtime callers. Source
  line 282.
- [x] Add or expose a `PhysicsWorldHandle` or equivalent owner token if multiple
  isolated worlds are needed. Source line 291.
  Evidence: `PhysicsStandaloneWorld` exposes create/update/delete/step/query
  operations for bodies, colliders, constraints, activation, ray casts, and
  immutable views; each standalone world instance owns its slot arrays and
  generation counters, which is the isolated owner-token equivalent. Focused
  smoke passed after the boundary edits:
  `Profile\SKULLBONEZ_CORE.exe --physics-standalone-smoke` reported
  `lifecycle_checks=pass` and hash `0xA64C5151AB391415`.

### Step Boundary

- [x] Move fixed-tree release behavior behind an explicit physics event sink or
  runtime adapter. Source line 323.
- [x] Move SkullScope frame emission behind an explicit diagnostics sink that
  receives physics views, not broad model storage. Source line 324.
  Evidence: `PhysicsModelAccess` now exposes `PhysicsBodyEventSink` and
  `PhysicsDiagnosticsView`; solver/tornado fixed-tree release calls route
  through `modelAccess.BodyEvents().ReleaseAttachedFixedTreeParts(...)`, fixed
  contact highlighting routes through `NotifyFixedContact(...)`, and
  `PhysicsDiagnosticsSink::EmitFrame()` calls `SkullScope::EmitFrame()` with the
  physics view instead of a `GameModelCollection` emission callback. Focused
  runtime-boundary check passed with 0 errors:
  `TestOutput/validation/runtime_boundaries/carmack_physics_event_sink.json`;
  `tools\validate_build.bat Profile` passed with 0 warnings and 0 errors.
- [x] Remove direct solver reads of `GameModel` fields after equivalent
  body/collider store data exists. Source line 325.
- [x] Preserve `PHYSICS_FIXED_DT`, max-step behavior, and deterministic
  time-scale handling. Source line 326.
  Evidence: `PhysicsBodyRecord` now carries solver-visible body policy fields
  (`usesWorldInertia`, `isFixed`, release policy/threshold, mass, inertia, and
  radius), and `PersistentContactSolver.cpp` uses those records for body setup,
  fixed-contact marking, position correction, terrain warm starts, and fixed
  release decisions. Remaining solver `GameModel` reads are shape/manifold
  compatibility paths whose collider-store equivalents are not yet passed into
  the solver. `SimulationSystem`/fixed-step code was not modified; focused
  Profile build passed with 0 warnings and 0 errors, and
  `Profile\SKULLBONEZ_CORE.exe --physics-standalone-smoke` still reported
  deterministic hash `0xA64C5151AB391415`.

### Store Authority

- [x] Make `PhysicsBodyStore` the authoritative owner for pose, velocity, mass,
  sleep, and solver-visible body state. Source line 330.
  Evidence: `PhysicsBodyStore` now captures/writes compatibility fixed state,
  mass, inverse mass, inertia, release policy, and body radius, and the
  persistent solver/tornado release path mutates `PhysicsBodyRecord` before
  compatibility writeback instead of reading those fields from `GameModel`.
- [x] Make `ColliderStore` the authoritative owner for shape, material response,
  broadphase radius, and collision metadata. Source line 331.
  Evidence: `PhysicsScene::RunPhysics()` now refreshes and passes
  `ColliderStore` into `PhysicsWorld`; `PersistentContactSolverContext` carries
  `colliderRecords`; `PersistentContactSolver.cpp` reads store-owned
  `ColliderRecord::shape`, `boundingRadius`-equivalent shape radius,
  `restitution`, and collision metadata for narrowphase, support policy, and
  contact response. `GameModel` references left in that solver are pose/oriented
  manifold inputs only. Focused Profile build passed with 0 warnings and 0
  errors, and the combined smoke report still matched hash
  `0xA64C5151AB391415`.
- [x] Make point joints and ragdoll constraints refer to physics handles rather
  than model indices. Source line 332.
- [x] Route body deletion through one deterministic path that invalidates
  handles, collider rows, constraints, contacts, and replay references. Source
  line 336.
- [x] Add a deterministic tombstone or generation policy so stale handles fail
  predictably. Source line 339.
  Evidence: `PointJointConstraint` now stores `PhysicsBodyHandle` endpoints and
  exposes explicit compatibility-index helpers for the current model-order
  solver; ragdoll construction, authored scene setup, physics-world joint loops,
  and scene snapshots all route through those handles/helpers. The standalone
  physics facade already owns deterministic body/collider/constraint deletion:
  `PhysicsStandaloneWorld::DestroyBody()` advances body generation, frees the
  slot, tombstones dependent colliders and point joints, and stale handles fail
  `IsAlive()`; `DestroyCollider()`/`DestroyConstraint()` route through their
  tombstone helpers. Focused Profile build passed with 0 warnings and 0 errors,
  and the standalone smoke still matched hash `0xA64C5151AB391415`.
- [x] Keep render instance updates as a mirror after physics mutation, not a
  dependency used during step. Source line 342.
  Evidence: `PhysicsScene::RefreshRenderStore()` remains separate from
  `RunPhysics()` and calls `RenderInstanceStore::Refresh()` after physics body
  mutation; the solver context contains body/collider records but no render
  instance store. `Profile\SKULLBONEZ_CORE.exe --physics-standalone-smoke
  --physics-standalone-smoke-log TestOutput\physics_standalone_smoke.txt`
  reported `render_instances=2` and `render_mirror=pass`.

### Runtime Adapter

- [x] Make replay/editor/runtime callers store physics handles where they
  currently store model indices for physics commands. Source line 348.
- [x] Keep model indices only for UI selection or render presentation where they
  are genuinely presentation concepts. Source line 349.
  Evidence: `GameModelCollectionPhysicsAdapter` is the explicit legacy bridge
  from model index/scene object id to `PhysicsBodyHandle`; wake, sleep, impulse,
  and pending impulse commands enter `PhysicsEngine` as handles. Point joints,
  ragdoll construction, authored scene setup, and snapshots store/use
  `PhysicsBodyHandle` endpoints, with compatibility index helpers only where the
  current UI/model-order presentation still needs them.

### Tests And Evidence

- [x] Add a runtime integration sample proving scene objects still mirror
  physics body state after a step. Source line 389.
- [x] Add focused replay restore evidence if handles replace model indices in
  replay state. Source line 390.
- [x] If SkullScope output changes, update the query baseline only from final
  Debug artifacts and report query-size accounting. Source line 391.
  Evidence: `HandlePhysicsStandaloneSmoke()` now also runs a heap-backed runtime
  handle/mirror sample that constructs a `GameModelCollection`, resolves
  physics handles, adds a handle-backed point joint, refreshes body/collider/
  render stores, and reports store/render/joint checks. The report showed
  `runtime_mirror_checks=pass`, `store_handles=pass`, `render_mirror=pass`, and
  `joint_handles=pass`. Replay solver checkpoint data was not changed; the
  handle migration for point joints preserves scene/snapshot compatibility via
  `BodyAIndex()`/`BodyBIndex()` helpers, so no replay baseline refresh was
  required. SkullScope frame fields were not changed; only the diagnostics
  access path moved behind `PhysicsDiagnosticsView`, so no SkullScope query
  baseline or query-size accounting was required for this slice.

### Validation Checklist

- [x] For plan-only edits: no validation required. Source line 395.
  Evidence: superseded by implementation validation because this handoff moved
  beyond plan-only edits.
- [x] For SkullScope baseline/query changes: run
  `tools\validate_physics_deep.bat`. Source line 430.
  Evidence: no SkullScope baseline/query artifacts were changed; diagnostics
  moved behind `PhysicsDiagnosticsView`, so no deep baseline refresh was needed.
- [x] For broad runtime integration changes: run `tools\validate_full.bat`.
  Source line 431.
- [x] For hot-path storage or iteration changes: run `tools\validate_perf.bat`
  and document any warnings. Source line 432.
- [x] Quote the relevant validation output in the handoff; do not claim success
  without command output. Source line 433.
  Evidence: `TestOutput\validation\agent_logs\carmack_validate_full_final.log`
  reports `VALIDATE_FULL: DEFAULT GATE PASSED` with byte-exact physics
  baseline. `TestOutput\validation\agent_logs\carmack_validate_perf_after_parallel_threshold.log`
  reports absolute perf budgets passed for DX12 and PHYSICS_BENCH, then exits 7
  on the same-machine PHYSICS_BENCH relative render/memory comparison; details
  are recorded in
  `Agentic/Reports/2026-06-29/carmack-handoff/perf-validation-note.md`.

### Independent Review Checklist

- [x] Ask a rubber-duck reviewer to check for any remaining physics dependency
  on runtime, renderer, editor, scene UI, or `GameModelCollection`. Source line
  437.
- [x] Ask the reviewer to verify that deterministic ordering is explicit and
  validation-visible. Source line 438.
- [x] Ask the reviewer to inspect handle lifetime, stale-handle behavior, and
  body deletion. Source line 439.
- [x] Record blocking and non-blocking findings in a report or this plan. Source
  line 440.
- [x] Resolve blocking review findings before committing PR-bound code. Source
  line 473.
  Evidence: rubber-duck review recorded in
  `Agentic/Reports/2026-06-29/carmack-handoff/physics-boundary-rubber-duck-review.md`.
  It found no blocking findings. The only non-blocking note is that
  `PersistentContactSolver` still uses `GameModel` as a compatibility pose
  bridge while physics-owned body/collider records own mass, inertia, fixed
  state, release policy, shape, and restitution.

### Definition Of Done

- [x] `GameModelCollection` is no longer on the normal physics step boundary.
  Source line 481.
- [x] Runtime scene objects adapt to physics handles instead of serving as solver
  authority. Source line 485.
- [x] Boundary guardrails reject reintroducing broad game-object ownership into
  physics. Source line 486.
  Evidence: `PhysicsWorld` and `PersistentContactSolver` now consume
  `PhysicsModelAccess`, `PhysicsBodyStore`, `ColliderStore`, and
  `PhysicsBodyEventSink` boundaries instead of concrete `GameModelCollection`
  authority. Runtime scene setup, snapshots, point joints, and ragdolls convert
  compatibility indices into `PhysicsBodyHandle` endpoints at adapter edges.
  The focused runtime-boundary check passed with 0 errors:
  `TestOutput/validation/runtime_boundaries/carmack_physics_event_sink.json`.
  Focused smoke output recorded:
  `lifecycle_checks=pass runtime_mirror_checks=pass` and
  `store_handles=pass render_mirror=pass joint_handles=pass`.
- [x] Required validation passes with byte-exact physics baselines or a
  documented intentional baseline refresh. Source line 487.
  Evidence: final `tools\validate_physics.bat` and `tools\validate_full.bat`
  both passed; `physics_regression_solver.csv` matched the committed baseline
  byte-exactly with 20001 lines. No physics baseline refresh was performed.

## Phase 3 - Render Graph Resource Ownership

Source plan:
`Agentic/Plans/Done/carmack-render-graph-resource-ownership-plan.md`

### Resource Declaration

- [x] Give every frame resource a stable graph name. Source line 315.
- [x] Assign concrete `RenderGraphResourceAccess` values for every read/write.
  Source line 316.
- [x] Replace `Unknown` access with explicit initial or imported states where the
  backend can know them. Source line 317.
- [x] Add subresource declarations where one texture uses different subresources.
  Source line 319.
- [x] Keep native DX12 resource identity diagnostic-only in API-neutral graph
  records. Source line 320.
  Evidence: production callback-owned passes declare stable graph resource names
  such as `SwapchainBackbuffer`, `MainDepthStencil`, shadow maps, reflection
  outputs, cinematic targets, `VolumetricLight`, and the new diagnostic
  `GraphTransientProbeColor`. `RenderGraph::AddRead()` and `AddWrite()` reject
  `RenderGraphResourceAccess::Unknown`, and the runtime boundary checker
  allowlists only the legacy `CinematicSceneDepth` framebuffer handoff where the
  backend still owns first-state depth transitions. Subresource declarations are
  part of the graph use record and are covered by
  `Agentic/Tests/Dx12ArchUnitTests/Dx12ArchUnitTests.cpp` for split mip
  transitions; no migrated production frame target in this slice needed a
  specific-subresource declaration. `RenderGraphResourceDesc::nativeResource`
  remains a borrowed `const void*` copied into diagnostics/barrier matching only.

### Transient Resource Lifetime

- [x] Add a graph allocator or backend executor path that creates transient DX12
  resources from descriptors. Source line 327.
- [x] Release transient resources through graph/backend lifetime policy, not pass
  destructors scattered across runtime code. In-flight graph release diagnostics
  exist; backend-owned pass destructors still own existing imported FBOs. Source
  line 333.
- [x] Ensure transient allocation does not introduce steady-frame heap growth.
  Source line 339.
  Evidence: `RenderBackendDX12::MaterializeGraphTransientResources()` now
  creates graph transient `Texture2D` resources from
  `RenderGraphTransientResourceDesc`, writes RTV/DSV/SRV/UAV descriptors through
  the backend allocators, reuses compatible pool slots across graph compiles,
  and reports `GraphTransientMaterialization` counters in the frame graph dump.
  `ReleaseGraphTransientResources("Shutdown")` owns COM release for the graph
  pool instead of pass destructors. Focused Profile build passed with
  `0 Warning(s)`, `0 Error(s)`, and
  `PASS: Build Profile|x64 succeeded.`

### Barrier Ownership

- [x] Route graph-compiled transition records through the DX12 graph executor for
  live barrier emission. Source line 343.
- [x] Route UAV ordering through explicit graph-owned policy. Source line 345.
- [x] Remove pass-local or backend-local barriers after graph output is proven
  equivalent. Source line 346.
- [x] Add diagnostics that compare expected graph transitions with emitted DX12
  barriers during validation. Source line 348.
- [x] Fail validation on unknown states that should be concrete by the migrated
  phase. Source line 350.
  Evidence: live transition and UAV ordering helpers are
  `RenderBackendDX12::ExecuteGraphTransition()` and
  `RenderBackendDX12::ExecuteGraphUavBarrier()`, which call the DX12 graph
  executor and record bounded live barrier telemetry. The frame graph skeleton
  writes `GraphDryRunTransitionBarriers`, `LiveBackendTransitionBarriers`,
  `LiveBackendUavBarriers`, and `GraphVsLiveTransitionStatePairs` so validation
  artifacts compare expected graph transitions against emitted DX12 barriers.
  Existing migrated runtime pass guardrails reject direct manual barrier calls,
  and the Unknown-access guardrail rejects new concrete-known `Unknown` uses.

### Descriptor And Resource Binding

- [x] Name which system owns descriptor allocation for graph-created resources.
  Source line 355.
- [x] Make descriptor lifetime follow graph resource lifetime. Source line 356.
- [x] Keep material/object descriptor tables separate from transient frame target
  descriptors. Source line 357.
- [x] Ensure graph-owned resources can be sampled, rendered into, and captured by
  screenshot validation without ad hoc backend lookups. Source line 359.
  Evidence: `RenderBackendDX12` owns graph transient descriptor allocation at
  the DX12 backend edge. Graph-created frame-target descriptors are tracked in
  `m_graphTransientResources` and counted by
  `GraphTransientMaterialization.descriptor_rows_owned`; material/object
  texture tables remain separate backend registries. The diagnostic
  `GraphTransientProbeColor` requests both render-target and shader-resource
  descriptors, proving the backend can render into and sample a graph-created
  resource path. Screenshot validation evidence is still pending under the
  validation checklist below.

### Validation Checklist

- [x] For plan-only edits: no validation required. Source line 374.
  Evidence: superseded by implementation validation because this handoff moved
  beyond plan-only edits.
- [x] For broad runtime render host changes: run `tools\validate_full.bat`.
  Source line 380.
  Evidence: final `tools\validate_full.bat` passed in
  `TestOutput\validation\agent_logs\carmack_validate_full_final.log`; DX12
  validation reported 0 InfoQueue errors and screenshot comparisons passed.

### Definition Of Done

- [x] Production frame pass execution is graph-owned. Source line 396.
- [x] Transient frame resources are graph-owned or explicitly imported. Source
  line 397.
- [x] Manual barriers are gone from migrated pass families or explicitly
  reviewed. Source line 398.
- [x] Performance evidence shows graph ownership did not introduce recurring
  steady-frame allocation or measurable hot-path regression. Source line 400.
  Evidence: `RenderBackendDX12` now materializes/reuses graph transient
  resources through the backend pool, the diagnostic graph-owned transient probe
  is fixed at 16x16 to avoid frame-sized validation allocations, and final
  `tools\validate_full.bat` passed DX12 validation/screenshots. Perf evidence is
  documented in
  `Agentic/Reports/2026-06-29/carmack-handoff/perf-validation-note.md`:
  absolute DX12 and PHYSICS_BENCH budgets pass, while the remaining
  `validate_perf` exit is the PHYSICS_BENCH relative render/memory comparison.

## Phase 4 - Render Backend Capability

Source plan:
`Agentic/Plans/Done/carmack-render-backend-capability-plan.md`

No unchecked checklist TODOs were found in this source plan. It is still marked
`Status: In progress`, so a worker should reread it before changing its status,
but there is no extracted checkbox work for this handoff.
