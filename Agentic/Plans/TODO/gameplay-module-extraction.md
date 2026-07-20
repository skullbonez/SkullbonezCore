# Gameplay Module Extraction

Status: Active — 3/4 tasks (T0-T2 complete; T3 remains)
Owner: repository owner; registered 2026-07-20 as campaign plan 7 of 8
Evidence: `../../Reports/2026-07-20/engine-architecture-review.md` (finding G)
Ledger: T0-T3
Depends on: `physics-settings-snapshot` C1 (force-stage inputs are typed
values, giving the external-force seam its shape) and
`../../Reports/2026-07-20/render-graph-completion-closure.md` (satisfied;
the callback-owned pass-registration seam required by T2 is available).

## Objective

**Owner decision 2026-07-20: extracted gameplay content lives in a new
top-level `SkullbonezSource/Gameplay/` module** that sits above Physics and
Rendering. Move `TornadoGameplay`/`TornadoField` out of `Physics/` and
`TornadoVisualPass` ownership out of the fixed renderer pass list, plugging
in through two engine seams: a data-driven external-force lane on the force
stage and the plan-5 pass-registration path. The engine/content boundary
becomes a directory boundary provable by grep.

## Problem / Evidence

`PhysicsWorld` owns and sequences `TornadoGameplay` as a "stay-behind"
member (`PhysicsWorld.h:147`, `ApplyTornadoGameplay` on the facade), with
tornado config surfaces (`SetTornadoFieldConfig` etc.) on the physics
boundary; `RuntimeRenderer.h:319` bakes `TornadoVisualPass` into the fixed
pass list. Content is fused into the two hottest core modules; every future
game feature copying this pattern makes the engine less an engine.

## Non-Goals

- No behavior change: tornado force application order, magnitudes, worker
  scheduling, and determinism are preserved exactly. The current varied
  physics baseline does not contain a tornado scene, so T1 must add a direct
  deterministic force witness before the move and preserve it exactly after
  the move. Existing physics CSV remains byte-exact; zero refresh authorized.
- Buoyancy, water, terrain, and world environment stay in the engine — they
  are environment, not content.
- No general plugin/scripting system: the external-force lane is a typed,
  fixed-capacity engine seam with `Gameplay/` as its first (compile-time)
  consumer.
- No visual change to the tornado; `TornadoVisualPass` moves ownership, not
  implementation.
- No new inheritance anywhere on the hot path (Hot-Path Data and
  Inheritance Review Rule): the force lane is value records in bounded
  arrays consumed by `PhysicsForceStage`, not a `*Sink`/callback interface.

## Binding Decisions

1. New module `SkullbonezSource/Gameplay/` (own filter folder in the
   vcxproj; direction rule from plan 1 extends: `Gameplay/` may include
   Core/Maths/Physics/Rendering/Scene contracts; nothing below it may
   include `Gameplay/`).
2. The force seam is a bounded per-tick value input to the force stage
   (e.g. `ExternalForceField` records: center/axis/falloff/strength rows in
   a fixed-capacity span) supplied by the owner that steps physics.
   Deterministic iteration order is part of the contract; the tornado's
   existing pair-scratch/worker behavior must map onto it without
   reordering floating-point accumulation (knife-edge risk recorded in the
   Danger Zones physics-determinism row).
3. If a lossless mapping to generic force records is not provable
   byte-exact, the fallback is an owner-sequenced typed hook at the exact
   current call position (`ApplyTornadoGameplay` timing), still owned by
   `Gameplay/` — recorded explicitly, with the generic lane as its deletion
   condition. Divergence is never normalized by baseline refresh.
4. Tornado config/query surfaces (`SetTornadoFieldConfig`,
   `GetTornadoSystemElapsedSeconds`, UI tab plumbing, replay/diagnostic
   references) move to the `Gameplay/` owner; physics keeps zero
   tornado-named API.
5. `TornadoVisualPass` registers through the plan-5 pass path from
   `Gameplay/`; `RuntimeRenderer` keeps zero tornado-named members.

## T0 Census (2026-07-21)

`git grep -il tornado -- . ':(exclude)Agentic/**'` finds 82 tracked files.
This inventory is the closure source of truth; T3 must rerun and reconcile it.

| Surface | Tracked files containing tornado vocabulary | T1-T3 disposition |
|---|---|---|
| Physics | `PhysicsBodyStore.h`, `PhysicsEngine.cpp/.h`, `PhysicsRuntimeSettings.h`, `PhysicsSolverSnapshot.h`, `PhysicsWorld.cpp/.h`, `Stages/PhysicsSleepController.Wake.cpp`, `TornadoField.cpp/.h`, `TornadoGameplay.cpp/.h` | T1 moves content/config/state to `Gameplay/` and replaces the physics-facing names with the value lane below. Closure target: zero rows. |
| Runtime render | `RenderPresentationSettings.h`, `RuntimeRenderer.cpp/.h`, `RuntimeRenderInputs.h`, `RuntimeRenderPasses.cpp/.h` | T2 moves visual settings, snapshot, pass state, GPU lifecycle, and graph callback payload to `Gameplay/`. Closure target: zero rows. |
| Rendering | `DX12/GeneratedShaderReflection.h`, `DX12/RenderBackendDX12.DynamicGeometry.cpp`, `RenderPipeline.cpp`, `RenderSceneSnapshot.h`, `ShaderContracts.h` | T2 replaces content-specific shader/draw vocabulary with the existing generic transient-colored-triangle contract; generated reflection and manifest keys move with the gameplay shader contract. Closure target: zero rows. |
| Runtime composition and tools | `DevelopmentTools/ImGuiEditorOwner.cpp`, `InputController.cpp/.h`, `InputFrame.cpp`, `OperatorCommandApplier.cpp/.h`, `Run.cpp`, `RunLaunchOptions.h`, `RuntimeDiagnostics.cpp`, `RuntimeStressController.cpp`, `Scene/RunScene.cpp`, `Scene/SceneRuntimeReset.cpp/.h`, `Startup/StartupCommandLine.cpp/.h`, `Startup/StartupLaunchResolution.cpp`, `UI/OperatorEditorFrameComposer.cpp`, `UiTextPass.cpp` | T1 wires the Gameplay owner into the fixed-step value lane; T3 routes input, startup, reset, stress, diagnostics, and UI composition to that owner. Runtime composition may name Gameplay; Physics, Rendering, and `Runtime/Render` may not. |
| Replay | `ReplayPrediction.cpp`, `ReplayPredictionArchive.cpp`, `ReplayPredictionView.h`, `ReplayRecorder.cpp/.h`, `ReplayRestoreService.h`, `ReplayV2Artifact.cpp` | T3 records/restores Gameplay-owned values without changing replay ordering, schema, or sample values. |
| Scene authoring | `AuthoredScene.cpp/.h`, `AuthoredSceneParser.cpp`, `AuthoredSceneParserRuntime.cpp`, `AuthoredSceneParserSchema.h` | T3 keeps authored schema spelling stable while projecting parsed values into Gameplay ownership. No schema/version bump is intended. |
| UI | `OperatorEditorExchange.cpp/.h`, `UI.cpp/.h`, `UICommands.h`, `UIFrameComposition.cpp`, `UILayout.h`, `UITabPhysics.cpp/.h` | T3 preserves operator command/value vocabulary and moves the owning command target from Physics/renderer to Gameplay. |
| Core, assets, and config | `Assets/AssetSystem.cpp`, `Core/Config.cpp/.h`, `SkullbonezData/engine.cfg` | T3 moves content config projection to Gameplay. The only config-file hit is `physics_parallel_tornado_field`; its authored spelling remains stable unless a versioned migration is explicitly added. |
| Data and shader | `scenes/aaa_ragdoll_sunset_showcase.scene.json`, `scenes/tornado_alley_showcase.scene.json`, `scenes/tornado_village_rampage.scene.json`, `shaders/shader_manifest.json`, `shaders/transient_colored_triangles.hlsl` | Authored scene content names stay content names. The three scenes author 1, 3, and 3 vortices respectively. T2 moved the shared packed-triangle shader to a content-neutral build key without changing shader behavior. |
| Tests and tools | `TestDeterminism.cpp`, `TestOwnerRequestQueues.cpp`, `TestPhysicsStageState.cpp`, `TestStartup.cpp`, `tools/allocation_policy_allowlist.json`, `tools/replay_query.py`, `tools/validate_project_filters.py` | Tests follow the new owner and add the missing direct force witness; allowlist/project-filter/query tooling follows moved paths without weakening checks. |
| Project metadata | `SKULLBONEZ_CORE.vcxproj/.filters`, `SKULLBONEZ_PHYSICS.vcxproj/.filters` | T1 adds the `Gameplay` filter and moves compilation ownership out of the physics-only project. |
| Baselines | No tracked file under `TestOutput/baselines/` contains tornado vocabulary. `validate_physics.bat` runs `physics_bench_varied.scene.json`, which has no tornado configuration. | This is a validation gap, not refresh authority. T1 adds a focused deterministic witness and still runs the normal byte-exact physics gate. |

## T0 Bound Seam Contract

Binding-decision path **2** is selected. The current behavior maps losslessly to
a Physics-owned cylindrical external-force primitive, so path 3 is not
authorized at T0. If the T1 pre-move witness disproves that statement, T1 must
stop and amend this plan with the exact-position typed hook, its owner, reason,
deletion condition, and comparison evidence before source changes continue.

### External-force lane

- Physics owns the value vocabulary, tentatively
  `ExternalCylindricalForceField` and `ExternalForceFrameInput`; Gameplay owns
  authored configuration, time evolution, and the fixed storage that publishes
  those records. Neither side stores a callback, service, host pointer, or
  polymorphic object.
- The field capacity is **64** records per fixed step. Committed authored scenes
  currently peak at three. Storage is allocated/prepared before steady gameplay;
  overflow is a recoverable scene/config failure with owner, requested count,
  and capacity. There is no truncation or growth fallback.
- Each record carries center and vertical extent in metres; radius in metres;
  inward, tangential, lift, outward-eject, and upward-eject acceleration in
  metres/second squared; ejection-band fraction; minimum exposure and repeat
  cooldown in seconds; and maximum per-step delta velocity in metres/second.
  Per-body exposure/cooldown state is bounded by
  `Scene::Capacity::MAX_SCENE_OBJECTS` and remains Gameplay-owned mutable value
  storage borrowed only for the step.
- Gameplay publishes active fields in authored/source order after its existing
  grow/shrink, drift, and repulsion update. Physics consumes that span in the
  same order. For each body row in ascending dense-row order, it sums fields
  left-to-right, selects the strongest capture field with the existing strict
  `>` comparison, and applies the existing deterministic ejection-slot formula.
  No reduction, sort, handle lookup, or reordered floating-point accumulation
  is permitted.
- The lane executes immediately after base `PhysicsForceStage::ApplyForces` and
  before `PhysicsSleepController::FlushPendingAwakeBodyIndices` and broadphase,
  matching the current call position. Fixed-body release scans rows ascending;
  released wake rows preserve append order; each movable body writes only its
  own hot-state velocity row.
- `parallelTornadoField` becomes a Gameplay execution setting but retains exact
  behavior: the existing allocation-free worker pool partitions only body rows;
  every worker reads the immutable ordered field span and writes disjoint body
  and Gameplay state rows. Serial and parallel modes use identical per-body
  field iteration and arithmetic.
- The fixed-step composition owner builds the Gameplay frame input before
  `SceneWorld::StepPhysics` and passes it through a Physics-named value boundary.
  Physics never includes `Gameplay/`; diagnostics/wake results leave Physics as
  generic row/value outputs and are interpreted by Gameplay after the step.

### Render pass registration

- T2 introduces a content-neutral, stack-scoped world-extension registration
  scope owned by RuntimeRenderer/Rendering. It exposes the current color/depth
  graph resources, the existing frame command/view values, and synchronous
  typed `RenderGraph::SetPassCallback` registration; it exposes no renderer,
  scene-container, replay-owner, or backend service pointer.
- The higher composition layer opens that scope at the current scheduling point:
  after opaque terrain/water and before the optional transparent-body replay
  pass. `Gameplay::TornadoVisualPass` appends exactly one graphics pass, declares
  color/depth writes, registers its concrete stack payload, and the renderer
  compiles/executes that appended range before the scope ends.
- Gameplay owns visual settings, replay-to-visual sampling, transient vertex
  storage, GPU prepare/release, and the callback trampoline. RuntimeRenderer
  retains only the generic registration scope and ordering; it has no tornado
  member, method, snapshot, pass name, or resource-lifecycle branch.
- The callback remains synchronous and fixed-capacity as guaranteed by
  `RenderGraph`; payload borrows cannot escape graph execution. Existing visual
  timing label, raster state, world-view/depth use, and draw ordering remain
  unchanged.

## Tasks

- [x] T0 — Seam design and census: enumerate every tornado reference in
  `Physics/`, `Runtime/`, `Rendering/`, `UI/`, replay, config, scenes, and
  baselines; specify the external-force lane contract (capacity, ordering,
  units, worker interaction) and the pass-registration usage; name which
  binding-decision path (2 or 3) the evidence supports. Output: census +
  contract committed into this plan. No validation (documentation).
- [x] T1 — Physics extraction: create `Gameplay/`, move
  `TornadoGameplay`/`TornadoField` there, feed forces through the seam,
  delete tornado members/APIs from `PhysicsWorld`/`PhysicsEngine`.
  Proof: `grep -irn "tornado" SkullbonezSource/Physics` returns zero rows.
  Validation: add/run the focused deterministic tornado force witness, then
  `tools\validate_physics.bat` (existing CSV remains byte-exact) +
  `tools\validate_perf.bat` (force-stage hot path touched);
  `tools\validate_physics_deep.bat` if any SkullScope tornado diagnostics
  move.
- [x] T2 — Render extraction: `TornadoVisualPass` ownership and its
  settings snapshot move to `Gameplay/`, registering through the plan-5
  pass path; `RuntimeRenderer` loses tornado members. Proof:
  `grep -irn "tornado" SkullbonezSource/Runtime/Render
  SkullbonezSource/Rendering` returns zero rows. Validation:
  `tools\validate_dx12_renderer.bat` + `tools\run_graphics_stress.bat 1`.
- [ ] T3 — Closure: UI/replay/config reference reconciliation lands on the
  `Gameplay/` owner; direction-rule grep extended in `AGENTS.md`
  (`Gameplay/` inclusion rule); independent rubber-duck review (boundary
  check: physics/render kept zero content vocabulary; the seam did not
  become a callback pack); final gates. Validation:
  `tools\validate_full.bat` + `tools\validate_physics.bat` at closure tip.

## Acceptance

- `Physics/`, `Rendering/`, and `Runtime/Render/` contain zero
  tornado-named source; `Gameplay/` owns the content end to end.
- Physics CSV byte-exact and DX12 baselines identical with zero refresh; if
  binding-decision path 3 was used, the recorded hook names its deletion
  condition.
- Perf gate shows no force-stage regression outside noise (record numbers).
- Independent review clear on seam shape (bounded value lane, no hot-path
  inheritance/callbacks).

## Validation Summary

T1 complete 2026-07-21. `TornadoField`/`TornadoGameplay` and mutable
configuration/timers now live in `Gameplay/`; Physics consumes a 64-record
ordered `ExternalForceFrameInput` through `ExternalForceStage`, with fixed
release scratch and no callback/inheritance seam. Scene parsing retains a
neutral authored DTO and Runtime projects it into Gameplay, preserving the
direction rule. Replay composes Physics and Gameplay values without changing
artifact field order; invalid artifacts over the Gameplay cap fail before
restore. `rg -i tornado SkullbonezSource/Physics` returns zero rows.

Evidence at the final source tip: focused one-step witness 1/1 case and 7/7
assertions; `tools\validate_full.bat` 199.48 s PASS (including the required
physics gate, 44,401-line byte-exact CSV, zero DX12 errors, unchanged images);
`tools\validate_perf.bat` 106.09 s PASS (zero steady-gameplay allocations and
no DX12/physics regressions); replay visual fidelity 440.53 s PASS (one process,
one generation, 2,401 ticks, all positive/negative controls). Allocation policy
scan reports 409 files and zero allowlist errors. SkullScope surfaces did not
move, so `validate_physics_deep` was not required. Comment audit checked 55/55
touched source-bearing files with zero deferred.

T2 complete 2026-07-21. Gameplay owns visual settings, live/replay time
selection, transient triangle/debug-line storage, resource prepare/release,
and the callback trampoline. Run supplies a stack-scoped typed registration;
RuntimeRenderer opens only the content-neutral post-water scope and compiles/
executes its one appended pass before the scope ends. Rendering uses the generic
transient-colored-triangle shader/draw key. `rg -i tornado
SkullbonezSource/Runtime/Render SkullbonezSource/Rendering` returns zero rows.

Evidence at the final source tip: shader freshness passes for 43 stages;
allocation policy scans 412 files with zero allowlist errors; project-filter
validation reports 728/728 covered items; `tools\validate_fast.bat` 80.83 s
PASS with zero-warning Profile/Debug builds; `tools\validate_dx12_renderer.bat`
57.63 s PASS with zero InfoQueue errors and all three screenshots within their
committed thresholds; `tools\run_graphics_stress.bat 1` 62.74 s PASS with a
PID-scoped stop and empty stderr. No baseline was refreshed. Comment audit
checked 31/31 touched source-bearing files with zero deferred.

T3: `validate_full` +
`validate_physics` at final source.
