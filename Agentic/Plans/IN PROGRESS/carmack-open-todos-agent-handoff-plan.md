# Carmack Open TODO Agent Handoff Plan

Status: In progress

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

- [ ] Classify each individual source hit as `bootstrap`, `shutdown`,
  `OS callback bridge`, `normal runtime path`, `render pass`, `asset lookup`,
  `diagnostics`, or `test/tool`. Source line 679.

### Service Context Shape

- [ ] Define or extend an `EngineServices` or equivalent context for process
  services that must be shared. Source line 689.
- [ ] Define or extend an `AssetContext` for asset lookup and source records
  instead of `ActiveAssetSystem()`. Source line 693.
- [ ] Define or extend an `InputEventBuffer` or input bridge for Win32 callback
  accumulators. Source line 695.
- [ ] Define or extend a `WindowService` or explicit window reference for window
  queries and title/resize behavior. Source line 697.
- [ ] Keep contexts borrowed and lifetime-annotated; do not create a new global
  service locator under a nicer name. Source line 699.

### Remove Normal-Path Globals

- [ ] Route render pass backend access through render capability/context
  arguments. Source line 704.
- [ ] Route shader and texture creation through an asset/render context passed
  from runtime-owned services. Source line 744.
- [ ] Replace `TextureCollection::Instance()` normal-path lookups with runtime
  owned texture service references. Source line 752.
- [ ] Replace `CameraCollection::Instance()` normal-path lookups with explicit
  camera service references. Source line 754.
- [ ] Replace `Window::Instance()` normal-path lookups with explicit window
  service references after bootstrap. Source line 756.
- [ ] Replace `SkyBox::Instance()` with runtime/world-owned skybox lifetime or a
  scene-render resource owner. Source line 758.
- [ ] Keep config reads grouped through launch/runtime config context where
  possible; do not spread new `Cfg()` calls. Source line 760.

### OS Callback Bridges

- [ ] Keep Win32 input globals only behind a tiny bridge if callback signatures
  require process-static state. Source line 765.

### Lifetime Order

- [ ] Add assertions that services are unbound before destruction when callbacks
  can fire late. Source line 800.
- [ ] Keep `Run.h` as composition root wiring, not a bag of service-locator
  helpers. Source line 802.

### Validation Checklist

- [ ] For plan-only edits: no validation required. Source line 850.
- [ ] For asset registration, scene asset loading, hull asset, or scene JSON
  behavior changes: run `tools\validate_full.bat`. Source line 860.

### Definition Of Done

- [ ] Normal runtime/render/asset/scene paths use explicit contexts or borrowed
  interfaces instead of process globals. Source line 938.
- [ ] Remaining globals are bootstrap-only, shutdown-only, or OS callback
  bridges with explicit lifecycle comments. Source line 940.
- [ ] Guardrails prevent new global service access from creeping back in. Source
  line 942.
- [ ] Required validation passes for the touched implementation areas. Source
  line 943.

## Phase 2 - Physics Standalone Boundary

Source plan:
`Agentic/Plans/Done/carmack-physics-standalone-boundary-plan.md`

### Public Physics API

- [ ] Extend `SkullbonezSource/Physics/PhysicsApi.h` with any missing create,
  update, query, delete, and step descriptors needed by runtime callers. Source
  line 282.
- [ ] Add or expose a `PhysicsWorldHandle` or equivalent owner token if multiple
  isolated worlds are needed. Source line 291.

### Step Boundary

- [ ] Move fixed-tree release behavior behind an explicit physics event sink or
  runtime adapter. Source line 323.
- [ ] Move SkullScope frame emission behind an explicit diagnostics sink that
  receives physics views, not broad model storage. Source line 324.
- [ ] Remove direct solver reads of `GameModel` fields after equivalent
  body/collider store data exists. Source line 325.
- [ ] Preserve `PHYSICS_FIXED_DT`, max-step behavior, and deterministic
  time-scale handling. Source line 326.

### Store Authority

- [ ] Make `PhysicsBodyStore` the authoritative owner for pose, velocity, mass,
  sleep, and solver-visible body state. Source line 330.
- [ ] Make `ColliderStore` the authoritative owner for shape, material response,
  broadphase radius, and collision metadata. Source line 331.
- [ ] Make point joints and ragdoll constraints refer to physics handles rather
  than model indices. Source line 332.
- [ ] Route body deletion through one deterministic path that invalidates
  handles, collider rows, constraints, contacts, and replay references. Source
  line 336.
- [ ] Add a deterministic tombstone or generation policy so stale handles fail
  predictably. Source line 339.
- [ ] Keep render instance updates as a mirror after physics mutation, not a
  dependency used during step. Source line 342.

### Runtime Adapter

- [ ] Make replay/editor/runtime callers store physics handles where they
  currently store model indices for physics commands. Source line 348.
- [ ] Keep model indices only for UI selection or render presentation where they
  are genuinely presentation concepts. Source line 349.

### Tests And Evidence

- [ ] Add a runtime integration sample proving scene objects still mirror
  physics body state after a step. Source line 389.
- [ ] Add focused replay restore evidence if handles replace model indices in
  replay state. Source line 390.
- [ ] If SkullScope output changes, update the query baseline only from final
  Debug artifacts and report query-size accounting. Source line 391.

### Validation Checklist

- [ ] For plan-only edits: no validation required. Source line 395.
- [ ] For SkullScope baseline/query changes: run
  `tools\validate_physics_deep.bat`. Source line 430.
- [ ] For broad runtime integration changes: run `tools\validate_full.bat`.
  Source line 431.
- [ ] For hot-path storage or iteration changes: run `tools\validate_perf.bat`
  and document any warnings. Source line 432.
- [ ] Quote the relevant validation output in the handoff; do not claim success
  without command output. Source line 433.

### Independent Review Checklist

- [ ] Ask a rubber-duck reviewer to check for any remaining physics dependency
  on runtime, renderer, editor, scene UI, or `GameModelCollection`. Source line
  437.
- [ ] Ask the reviewer to verify that deterministic ordering is explicit and
  validation-visible. Source line 438.
- [ ] Ask the reviewer to inspect handle lifetime, stale-handle behavior, and
  body deletion. Source line 439.
- [ ] Record blocking and non-blocking findings in a report or this plan. Source
  line 440.
- [ ] Resolve blocking review findings before committing PR-bound code. Source
  line 473.

### Definition Of Done

- [ ] `GameModelCollection` is no longer on the normal physics step boundary.
  Source line 481.
- [ ] Runtime scene objects adapt to physics handles instead of serving as solver
  authority. Source line 485.
- [ ] Boundary guardrails reject reintroducing broad game-object ownership into
  physics. Source line 486.
- [ ] Required validation passes with byte-exact physics baselines or a
  documented intentional baseline refresh. Source line 487.

## Phase 3 - Render Graph Resource Ownership

Source plan:
`Agentic/Plans/Done/carmack-render-graph-resource-ownership-plan.md`

### Resource Declaration

- [ ] Give every frame resource a stable graph name. Source line 315.
- [ ] Assign concrete `RenderGraphResourceAccess` values for every read/write.
  Source line 316.
- [ ] Replace `Unknown` access with explicit initial or imported states where the
  backend can know them. Source line 317.
- [ ] Add subresource declarations where one texture uses different subresources.
  Source line 319.
- [ ] Keep native DX12 resource identity diagnostic-only in API-neutral graph
  records. Source line 320.

### Transient Resource Lifetime

- [ ] Add a graph allocator or backend executor path that creates transient DX12
  resources from descriptors. Source line 327.
- [ ] Release transient resources through graph/backend lifetime policy, not pass
  destructors scattered across runtime code. In-flight graph release diagnostics
  exist; backend-owned pass destructors still own existing imported FBOs. Source
  line 333.
- [ ] Ensure transient allocation does not introduce steady-frame heap growth.
  Source line 339.

### Barrier Ownership

- [ ] Route graph-compiled transition records through the DX12 graph executor for
  live barrier emission. Source line 343.
- [ ] Route UAV ordering through explicit graph-owned policy. Source line 345.
- [ ] Remove pass-local or backend-local barriers after graph output is proven
  equivalent. Source line 346.
- [ ] Add diagnostics that compare expected graph transitions with emitted DX12
  barriers during validation. Source line 348.
- [ ] Fail validation on unknown states that should be concrete by the migrated
  phase. Source line 350.

### Descriptor And Resource Binding

- [ ] Name which system owns descriptor allocation for graph-created resources.
  Source line 355.
- [ ] Make descriptor lifetime follow graph resource lifetime. Source line 356.
- [ ] Keep material/object descriptor tables separate from transient frame target
  descriptors. Source line 357.
- [ ] Ensure graph-owned resources can be sampled, rendered into, and captured by
  screenshot validation without ad hoc backend lookups. Source line 359.

### Validation Checklist

- [ ] For plan-only edits: no validation required. Source line 374.
- [ ] For broad runtime render host changes: run `tools\validate_full.bat`.
  Current slice attempted this before the stop. Build and DX12 phases passed,
  then physics validation failed on `physics_regression_solver.csv` row count;
  no more validation was run after the user requested it. Source line 380.

### Definition Of Done

- [ ] Production frame pass execution is graph-owned. Source line 396.
- [ ] Transient frame resources are graph-owned or explicitly imported. Source
  line 397.
- [ ] Manual barriers are gone from migrated pass families or explicitly
  reviewed. Source line 398.
- [ ] Performance evidence shows graph ownership did not introduce recurring
  steady-frame allocation or measurable hot-path regression. Source line 400.

## Phase 4 - Render Backend Capability

Source plan:
`Agentic/Plans/Done/carmack-render-backend-capability-plan.md`

No unchecked checklist TODOs were found in this source plan. It is still marked
`Status: In progress`, so a worker should reread it before changing its status,
but there is no extracted checkbox work for this handoff.
