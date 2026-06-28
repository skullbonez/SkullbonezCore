# Engine Architecture Next Steps Plan

Date: 2026-06-26
Status: Superseded roadmap; not authoritative for implementation
Impact area: runtime architecture, physics data ownership, DX12 renderer, render graph, assets, scene system, diagnostics
Validation for this document-only change: none required

## Goal

Turn the architecture assessment into an actionable sequence of small engine
refactors.

The engine is already past the "old demo with wrappers" stage. DX12 is the
production renderer, physics has deterministic regression coverage, replay and
diagnostics are real tools, and the runtime has named subsystem boundaries.
The remaining work is to make those boundaries authoritative instead of only
organizational.

Target outcome:

```text
Run
  Thin process shell and composition root.

RuntimeRenderer
  Consumes narrow frame snapshots and render-facing services.

RenderGraph
  Owns pass execution and resource-state transitions for selected passes, then
  expands deliberately.

PhysicsEngine / PhysicsScene
  Step authoritative body and collider stores, not GameModelCollection.

GameModelCollection
  Temporary compatibility facade, no longer the world-data owner.

AssetSystem
  Source asset registry plus material/mesh/cache ownership and invalidation.

Scene parser/config
  Deterministic formats with typed schemas and useful diagnostics.
```

## Current Assessment

Strengths to preserve:

- DX12-only renderer focus, with zero InfoQueue errors as a hard contract.
- Deterministic physics validation through byte-exact CSV baselines.
- SkullScope/queryable diagnostics that keep raw physics data out of model
  context while preserving debugging power.
- Explicit runtime subsystem names: scene, simulation, diagnostics, replay,
  tools, renderer, and interaction controller.
- Render pass extraction in `RuntimeRenderer::RenderFrame()`.
- Physics migration scaffolding through `PhysicsScene`, `PhysicsBodyStore`,
  `ColliderStore`, and `RenderInstanceStore`.

Main risks:

- `Run` still coordinates broad scene load/reset, input, render, replay, tools,
  diagnostics, camera, UI, world, and model behavior.
- `RuntimeRenderHost` is a wide borrowed-state bridge over many Run-owned
  systems.
- `GameModelCollection` still owns the authoritative `std::vector<GameModel>`.
- `PhysicsWorld` and `PhysicsEngine` still take `GameModelCollection&` for
  stepping and commands.
- `IRenderBackend` is still one wide interface for lifecycle, draw state,
  resource creation, capture, DXR, GPU timers, debug lines, dynamic geometry,
  and instancing.
- `RenderGraph` records pass/resource intent, but does not yet own pass
  callbacks or full command recording.
- `AssetSystem` has source records, but GPU cache/material/mesh ownership still
  lives in transitional helpers and backend paths.

## Design Rules

1. Preserve behavior while moving ownership. Refactors should be easy to review
   and boring to validate.
2. Do not change solver math while moving physics storage.
3. Do not widen `Run`, `RuntimeRenderHost`, or `GameModelCollection` as a
   convenience during extraction.
4. Prefer plain data contracts and handles over broad service bags.
5. Make every compatibility bridge temporary, named, and covered by a boundary
   check when practical.
6. Keep validation proportional to risk, but never skip required PR gates for
   behavior changes.
7. Use existing plans as owners for detailed implementation where they already
   exist instead of duplicating source of truth.

## Non-Goals

- Do not rewrite the engine.
- Do not introduce a general ECS framework before body, collider, render, and
  entity metadata boundaries are explicit.
- Do not reintroduce GL or DX11 runtime architecture.
- Do not replace deterministic scene/replay formats with editor-only formats.
- Do not batch all phases into one branch.
- Do not update physics or visual baselines as a side effect of "cleanup."

## Phase 0: Baseline Guardrails And Work Selection

Purpose: make the next implementation branch measurable before touching code.

Rubber-duck result for `nightrunner-26th-July`:

- Expected outcome: implement one reviewable architecture slice, not the whole
  multi-phase roadmap in one commit.
- Finding: the roadmap is directionally sound, but it must name the first
  implementation slice when invoked directly.
- Missing evidence: no runtime evidence is needed before the selected
  documentation/tooling slice; it does not change engine behavior.
- Next step: freeze the existing physics `GameModelCollection` compatibility
  surface in the runtime boundary checker so later physics-store work can shrink
  that surface deliberately.

Selected first implementation slice for `nightrunner-26th-July`:

1. Extend `tools\check_runtime_boundaries.py` to scan
   `SkullbonezSource\Physics\**\*.h` and `*.cpp` for non-comment
   `GameModelCollection` dependencies.
2. Encode the current dependencies as an explicit transitional allowlist. The
   allowlist is not approval of the architecture; it is a ratchet that prevents
   new physics code from depending on the legacy model container while Phase 2
   removes existing compatibility paths.
3. Keep the rule narrow:
   - comments do not count,
   - existing includes, forward declarations, signatures, and compatibility
     debug paths remain allowed,
   - any new file or new dependency line fails validation with a message that
     points toward physics stores, handles, diagnostics, or compatibility
     adapters.
4. Add self-tests for the new static rule:
   - existing allowlisted dependency passes,
   - a new dependency in another physics file fails,
   - a duplicate unallowlisted dependency in an existing file fails,
   - comments mentioning `GameModelCollection` do not fail.
5. Do not change C++ runtime behavior in this slice.

Validation for this selected slice:

- `tools\validate_fast.bat`
- `tools\validate_runtime_boundaries.bat`

Done criteria:

- Runtime boundary validation fails on new physics-layer
  `GameModelCollection` dependencies.
- The branch has a plan commit, an implementation commit, and validation output
  captured before the final commit.

Tasks:

1. Choose exactly one implementation owner plan:
   - `Agentic/Plans/run-shell-extraction-plan.md`
   - `Agentic/Plans/game-model-data-boundary-plan.md`
   - `Agentic/Plans/dx12-final-architecture-next-steps.md`
   - a new focused plan for assets, scene schema, or render graph callbacks.
2. Refresh the ownership map for the chosen area only.
3. Run `git status --short --branch` and treat pre-existing dirty files as
   user-owned.
4. Identify the smallest PR gate before implementation starts.
5. Add or extend boundary checks before moving broad ownership when a cheap
   static rule exists.

Validation:

- Documentation-only inventory: no validation required.
- Boundary/tooling edits: `tools\validate_fast.bat`, then the changed script or
  selected boundary command.

Exit criteria:

- A single implementation slice is selected.
- Required validation is named before editing.
- The chosen slice has clear "done" criteria.

## Phase 1: Make `Run` A Thin Shell

Purpose: stop using `Run` as the default home for behavior.

Owner plan: `Agentic/Plans/run-shell-extraction-plan.md`

Tasks:

1. Narrow `RuntimeRenderHost` first:
   - split world, model, replay overlay, tool overlay, UI, and texture services,
   - move replay overlay callbacks into `ReplayRuntime`,
   - move editor/launcher/manipulator overlay callbacks into `RuntimeTools`,
   - keep render passes consuming render-facing views, not mutable runtime
     internals.
2. Move scene lifecycle side effects out of `Run::LoadScene()`:
   - reset snapshots,
   - scene browser/deck movement,
   - scene UI overrides,
   - generated/authored setup decisions,
   - lifecycle notifications for replay, tools, diagnostics, simulation, and
     renderer.
3. Move input decision flow out of `Run::TakeInput()`:
   - camera mode requests,
   - tool/replay/editor command routing,
   - focus-loss cleanup,
   - UI command application,
   - cursor ownership.
4. Delete wrappers that only forward to subsystem methods after each migration.
5. Extend runtime boundary checks so future work cannot reintroduce stored
   `Run*`, stored `Run&`, or broad render-host fields.

Validation:

- `tools\validate_full.bat` for broad runtime movement.
- `tools\validate_fast.bat` may be enough for narrow diagnostics/input
  scaffolding that cannot affect launch/render/physics behavior.
- DX12 renderer validation is required if render pass order, resource lifetime,
  or overlay rendering changes.

Exit criteria:

- New replay/tool/scene/render behavior no longer needs a new `Run::*` helper.
- `RuntimeRenderHost` is reduced or split into narrow views.
- `Run` remains the process lifetime owner, not the subsystem behavior owner.

## Phase 2: Make Physics Stores Authoritative

Purpose: stop stepping physics through `GameModelCollection`.

Owner plan: `Agentic/Plans/game-model-data-boundary-plan.md`

Tasks:

1. Stabilize handles before moving data:
   - entity id,
   - physics body id,
   - collider id,
   - render instance id,
   - legacy model index mapping.
2. Move body authority into `PhysicsBodyStore`:
   - transform,
   - linear/angular velocity,
   - mass and inverse mass,
   - inertia,
   - sleep state,
   - pending forces and impulses.
3. Move collider authority into `ColliderStore`:
   - shape variant,
   - bounding radius,
   - restitution,
   - drag/fluid metadata,
   - contact release metadata where appropriate.
4. Change `PhysicsEngine::Step()` and `PhysicsWorld::RunPhysics()` to operate on
   stores and command buffers instead of `GameModelCollection&`.
5. Replace model-index commands with handle-based commands:
   - wake body,
   - seed asleep,
   - apply impulse,
   - set pending impulse,
   - joint creation,
   - diagnostics query.
6. Keep a temporary compatibility writeback to `GameModel` only while render,
   replay, scene snapshot, and tools still need it.
7. Remove `GameModelCollection::PhysicsModels()` from production physics paths.

Validation:

- `tools\validate_physics.bat` for each behavior-touching slice.
- `tools\validate_perf.bat` for storage layout, broadphase, or hot-loop
  iteration changes.
- `tools\validate_full.bat` when replay restore, scene load, or render
  presentation can change.

Exit criteria:

- Physics stepping no longer takes `GameModelCollection&`.
- Physics diagnostics expose store/handle views.
- Physics CSV remains byte-exact unless the branch intentionally changes
  behavior and updates baselines through the required gate.

## Phase 3: Make Render Instances A Projection

Purpose: decouple production rendering from the legacy model container.

Tasks:

1. Make `RenderInstanceStore` the source for renderer-facing transforms,
   material intent, visibility, fixed-state feedback, and shadow participation.
2. Move `IRenderSceneView` implementation away from `GameModelCollection` where
   practical:
   - model draw batches,
   - shadow caster batches,
   - DXR instance matrices,
   - material instance upload data.
3. Keep physics debug overlays reading physics/collider diagnostics, not render
   instance internals.
4. Route replay render pose override/restore through body/render handles.
5. Add old/new projection comparison temporarily if it catches missed material
   or visibility state during migration.

Validation:

- `tools\validate_dx12_renderer.bat`
- `tools\validate_full.bat` when replay render state or scene loading changes.
- `tools\validate_perf.bat` if object batching or instance upload hot paths
  change.

Exit criteria:

- Production object rendering does not require direct `GameModel` iteration.
- Shadow, DXR, and replay rendering agree on stable instance identity.
- Render output matches committed DX12 baselines.

## Phase 4: Move A First Pass Under Graph-Owned Execution

Purpose: make the render graph own real work, not only diagnostics.

Tasks:

1. Add pass callback support to `RenderGraph` without changing the full frame.
2. Pick a low-risk first candidate:
   - fullscreen quad based pass,
   - simple post or diagnostic pass,
   - no DXR reflection,
   - no swapchain present ownership,
   - no main scene/depth pass at first.
3. Have the graph own resource-state transitions for that pass.
4. Compare graph-owned barriers against live barrier diagnostics until the new
   path is trusted.
5. Expand to other passes only after the first pass is stable:
   - tonemap/volumetric,
   - reflection target,
   - shadow maps,
   - main scene target,
   - swapchain present.
6. Split backend capability interfaces only when graph-owned execution needs
   narrower access.

Validation:

- `tools\validate_dx12_renderer.bat`
- Verify `dx12_validation.txt` reports zero errors.
- For descriptor/upload hot paths, add `tools\validate_perf.bat`.

Exit criteria:

- At least one real pass records through graph-owned callbacks and barriers.
- The graph compile output is no longer only documentation for that pass.
- DX12 screenshots and validation logs remain clean.

## Phase 5: Split Backend Capabilities Under Pressure

Purpose: reduce renderer coupling without inventing abstraction for its own
sake.

Tasks:

1. Keep `IRenderBackend` as the compatibility facade while introducing narrow
   capability views:
   - core device/frame commands,
   - resource creation,
   - capture/readback,
   - GPU timers and platform profiler markers,
   - debug draw,
   - dynamic geometry,
   - DXR reflection.
2. Move callers to the narrow view only when the caller benefits immediately.
3. Remove no-op optional methods after all current callers use explicit
   capability queries or narrow views.
4. Keep DX12-specific concepts out of engine-facing headers unless the header is
   explicitly DX12-owned.

Validation:

- `tools\validate_dx12_renderer.bat`
- `tools\validate_full.bat` if runtime lifecycle or resize/device reset paths
  change.

Exit criteria:

- Higher-level render code no longer sees DXR/debug/timer/dynamic-geometry
  methods unless it needs that capability.
- DX12 remains the only runtime renderer, but backend internals are easier to
  reason about.

## Phase 6: Mature Assets, Materials, And Water Ownership

Purpose: make assets and style/material data survive renderer lifecycle changes
without relying on helper globals.

Tasks:

1. Extend `AssetSystem` beyond source records:
   - material records,
   - mesh records,
   - cache invalidation policy,
   - hot reload hooks,
   - explicit source-vs-GPU lifetime ownership.
2. Remove or shrink transitional active-asset bridges where render helpers still
   hold static GPU state.
3. Move object, terrain, water, sky, and post material contracts into pass-owned
   or asset-owned descriptions.
4. Continue water cleanup through a focused plan:
   - water render resources owned by the water pass/material layer,
   - `WorldEnvironment` limited to world/simulation data,
   - intersection/back-face behavior covered by focused visual scenes.
5. Avoid expanding root signatures or descriptor slots until a concrete
   material/pass requirement proves the need.

Validation:

- `tools\validate_dx12_renderer.bat` for renderer assets, shaders, water, and
  material behavior.
- `tools\validate_full.bat` if scene load or runtime lifecycle changes.
- `tools\validate_perf.bat` if material/instance hot paths change.

Exit criteria:

- Source assets and GPU resources have explicit separate lifetimes.
- Helper static render state is removed or owned by a renderer/asset context.
- Water/material behavior has focused visual coverage.

## Phase 7: Tighten Scene And Config Schemas

Purpose: keep deterministic scene files, but make parsing less bespoke.

Tasks:

1. Keep JSON scene/style/asset formats deterministic and compatibility aware.
2. Add typed schema metadata for high-churn areas:
   - objects,
   - physics/simulation,
   - cinematic/style,
   - capture/logging,
   - UI state,
   - asset instances.
3. Replace repeated field-local parsing with shared typed readers that produce
   exact diagnostics.
4. Keep writer/snapshot behavior aligned with the schema so scenes can round
   trip.
5. Add parser tests or scene-load fixtures when semantics can drift.

Validation:

- `tools\validate_fast.bat` for parser-only cleanup.
- `tools\validate_full.bat` when scene load/runtime behavior can change.

Exit criteria:

- Adding a new scene field does not require duplicating ad hoc parsing logic.
- Error messages name the field, expected type/range, and source path.
- Scene snapshot output remains stable unless intentionally changed.

## Phase 8: Preserve Observability As Architecture

Purpose: protect the refactors with evidence instead of taste.

Tasks:

1. Add boundary validators when a static rule is cheap:
   - no new stored `Run` references,
   - no new physics dependency on `GameModelCollection`,
   - no growth of `RuntimeRenderHost` without an explicit allowlist update,
   - no new renderer globals in helper paths.
2. Promote known physics bugs into focused diagnostics before solver changes:
   - stacking drift/topple,
   - terrain micro-bounce,
   - box/ball interpenetration,
   - water interaction cases if physics-coupled.
3. Improve profiler accounting:
   - explicit unbucketed time,
   - VSync-excluded view,
   - clearer parent/child scope accounting.
4. Keep SkullScope queries bounded and report query cost when used.
5. Keep validation logs and gate output tied to implementation commits.

Validation:

- `tools\validate_fast.bat` for validator/profiler UI-only changes.
- `tools\validate_physics.bat` or `tools\validate_physics_deep.bat` for new
  physics baselines/diagnostics.
- `tools\validate_perf.bat` for profiler or hot-path timing work.

Exit criteria:

- Regression tooling makes architectural backsliding visible.
- Physics and renderer changes have targeted evidence before broad validation.
- Perf/profiler output is harder to misread.

## Suggested Sequencing

Recommended order:

1. Phase 0: choose one slice and add guardrails.
2. Phase 1: shrink `Run` and narrow `RuntimeRenderHost`.
3. Phase 2: make physics stores authoritative.
4. Phase 3: make render instances a projection.
5. Phase 4: move one low-risk pass under graph-owned execution.
6. Phase 5: split backend capabilities only where graph/render work needs it.
7. Phase 6: mature assets/materials/water.
8. Phase 7: tighten scene/config schemas.
9. Phase 8: continuously improve boundary checks, diagnostics, and profiler
   evidence.

Parallel-friendly work:

- Scene/config schema cleanup can proceed independently when it avoids runtime
  behavior changes.
- Profiler accounting can proceed independently if it avoids physics/render
  hot paths.
- Asset source-record cleanup can proceed independently when it does not touch
  GPU lifetime or scene loading.

Do not parallelize:

- physics store authority and replay restore identity,
- render graph execution and backend resource lifetime,
- scene lifecycle extraction and broad `Run` input routing,
- baseline refresh and solver refactor.

## Risk Matrix

| Risk | Why It Matters | Mitigation |
|------|----------------|------------|
| Physics determinism drift | Store order, wake timing, or cache ownership can change byte-exact output. | Move storage separately from solver math; run `validate_physics` at each behavior gate. |
| Render output drift | Render instance projection or graph barriers can change screenshots. | Keep old/new comparison where useful; run DX12 renderer validation. |
| Runtime extraction hides behavior change | `Run` currently encodes many ordering assumptions. | Move one owner at a time; use lifecycle events and full validation. |
| Compatibility facade becomes permanent | Temporary bridges can become the new architecture. | Name bridges as compatibility paths and add boundary checks. |
| Backend split creates fake abstraction | DX12 is the only renderer, so abstraction can become ceremony. | Split only when a caller needs a narrower contract. |
| Asset/material cleanup expands binding ABI unnecessarily | Root signature and descriptor churn can destabilize renderer performance. | Require a concrete pass/material need before binding changes. |

## Validation Map

| Change | Required PR Gate |
|--------|------------------|
| Documentation-only plan updates | No validation required |
| Runtime shell extraction | `tools\validate_full.bat` |
| Narrow input/diagnostics scaffolding with no launch behavior change | `tools\validate_fast.bat` |
| Physics stores, solver-visible storage, sleep, broadphase, contacts | `tools\validate_physics.bat` |
| Physics storage or broadphase hot-loop changes | `tools\validate_physics.bat` plus `tools\validate_perf.bat` |
| Render passes, render host, graph execution, DX12 barriers, shaders, water/material rendering | `tools\validate_dx12_renderer.bat` |
| Render graph/backend/device lifetime touching runtime launch/reset | `tools\validate_full.bat` or `tools\validate_dx12_renderer.bat` plus focused launch evidence |
| Scene/config parser cleanup | `tools\validate_fast.bat` |
| Scene load semantics | `tools\validate_full.bat` |
| Asset source records only | `tools\validate_fast.bat` unless runtime/render lifecycle changes |
| Asset GPU cache/material/mesh ownership | `tools\validate_dx12_renderer.bat` |

## Success Criteria

- `Run` is a process shell, not the behavior owner for every subsystem.
- `RuntimeRenderHost` is gone or reduced to narrow render-facing views.
- `PhysicsWorld` and `PhysicsEngine` do not take `GameModelCollection&`.
- `GameModelCollection` is no longer authoritative world storage.
- Render passes can be scheduled by `RenderGraph` one family at a time.
- Backend capabilities are explicit where optional behavior exists.
- Asset source records, GPU caches, materials, and mesh records have clear
  lifetime ownership.
- Scene/config parsing has typed schemas and precise diagnostics.
- Existing validation remains strict: zero warnings, zero DX12 validation
  errors, and byte-exact physics output unless behavior changes are intentional.

## Handoff Notes

- 2026-06-28 supersession audit: this roadmap is no longer the authoritative
  implementation plan. Its work is now owned by narrower active plans:
  `run-shell-extraction-plan.md`, `carmack-global-service-lifetime-plan.md`,
  `carmack-physics-standalone-boundary-plan.md`,
  `physics-game-model-authority-plan.md`,
  `carmack-render-graph-resource-ownership-plan.md`, and
  `render-graph-irender-interface-plan.md`.
- Rubber-duck verdict: no blocking issue found for moving this document to
  `Done` as a superseded roadmap, provided future agents do not treat its
  success criteria as closed implementation evidence.
- Implementation from this plan should use
  `Agentic/Skills/orchestrator/SKILL.md` unless the user explicitly asks to
  bypass it.
- Each phase should become its own branch or small set of branches.
- Run `git status --short --branch` before editing and before committing.
- Treat unrelated dirty files as user-owned.
- Do not run repository validation scripts while iterating unless they answer a
  specific question; run the named gate before PR-bound commits.
- On feature branches, normal commits and pushes are allowed when ready. On
  `main`, commit or push only after explicit user confirmation.
