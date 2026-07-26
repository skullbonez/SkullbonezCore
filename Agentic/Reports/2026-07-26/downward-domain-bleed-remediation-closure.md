# Downward Domain Bleed Remediation Closure

Date: 2026-07-26

Branch: `nightrunner-25th-JUL-26`

Plan: completed at 6/6; live TODO deleted under master-plan inventory rule 4

Closure commit: the commit containing this report

Status: **complete**

## Outcome

All four registered downward-bleed rows are closed:

- Runtime/Prediction owns trajectory record meaning, logical capacities,
  adjacency repair, retention, and presentation vocabulary.
- Rendering owns only generic retained-geometry values, fixed instanced-ribbon
  shader ABI, bounded physical resources, and value-token upload planning.
- Physics owns the detached terrain collision view and support policy; no World,
  Scene, or Assets dependency remains under Physics.
- `BuoyancySystem` owns one fixed-capacity five-float fact row aligned with each
  live body; `PhysicsBodyRecord` contains none of those fluid facts.
- The nine unused Physics-to-Assets includes found by DB0 were deleted.

The final implementation adds no compatibility alias, forwarding facade,
callback pack, service bag, virtual seam, Runtime owner backpointer, hot-loop
handle lookup, or post-gameplay allocation privilege.

## Complete Before/After Census

| Bleed class | DB0 source state | Final source state |
|---|---|---|
| B1 Rendering feature vocabulary | Fourteen source/test files referenced `RetainedTrajectory*` or `RETAINED_TRAJECTORY*`; Rendering owned the 19-float record, feature capacities, continuation repair, buffers, and upload plans | Rendering has zero trajectory/replay/prediction/planning/porkchop rows and zero retired exact symbols. Prediction owns the 19-float record, logical capacities, packing, adjacency repair, and retained storage |
| B2 World terrain in Physics | Four World includes, nine unused Assets includes, one `Geometry::Terrain*` per body row, and hot World queries | Physics has zero World/Scene/Assets/Gameplay/Runtime/UI includes, zero `Geometry::Terrain` or terrain-pointer rows, and one scene-lifetime `PhysicsTerrainView` value |
| B3 fluid facts in body record | Five fluid/support scalars lived in every `PhysicsBodyRecord` | `PhysicsBodyRecord` contains none; `BuoyancyBodyFacts` owns exactly five floats in one 8,192-row `PhysicsFixedList` |
| B4 include residue | Nine Physics files included unused `Assets/AssetKeys.h` | zero Assets include rows and no replacement dependency |

The final generic retained-geometry contract remains:

- `RetainedGeometryCapacity`;
- `RetainedGeometryStreamToken`;
- `RetainedGeometryRangeToken`;
- `RetainedGeometryUploadPlan`;
- `BuildRetainedGeometryUploadPlan`; and
- `BuildRetainedGeometryRangeUploadPlan`.

The upper feature owner supplies active logical counts and packed records.
Rendering validates them against physical GPU/command budgets and interprets
only the generic instanced-ribbon ABI. Equality between the current configured
capacity and a backend cap does not transfer feature retention authority.

The final terrain boundary remains:

```cpp
struct PhysicsTerrainView
{
    std::span<const PhysicsTerrainCell> cells;
    // scalar sampling metadata
};

void PhysicsEngine::SetTerrainView( PhysicsTerrainView view ) noexcept;
void PhysicsEngine::ClearTerrainView() noexcept;
```

`SceneWorld::ReplaceTerrain` revokes the old view before replacing its backing
storage and publishes the new view afterward. Physics retains no World object,
owner pointer, callback, or virtual dispatch.

## Permanent Enforcement

`AGENTS.md` records the Physics upward-edge ban, Rendering feature-neutrality
rule, hot-record owner ruling, and exact review proofs.

`tools/check_dependency_graph.py` and
`tools/dependency_graph_rules.json` enforce:

- 27 package include rules;
- 46 negative include fixtures, including the planted Physics
  `#include "../World/Terrain.h"`;
- one bounded retired-vocabulary rule with planted
  `RetainedTrajectory` type and `RETAINED_TRAJECTORY` constant failures; and
- one production project-ownership rule.

The checker reports zero repository findings. It uses exact deleted names,
never a frozen occurrence count or broad spelling budget. The qualitative
feature-neutrality rule remains review-owned.

All closure proofs returned no rows:

```text
Physics upward includes
Physics Geometry::Terrain or Terrain* types
Physics retained descriptor/record terrain fields
Rendering trajectory/porkchop/replay/prediction vocabulary
Rendering RetainedTrajectory/RETAINED_TRAJECTORY symbols
lower-engine includes of Runtime/Replay
```

`PhysicsBodyRecord` lines 81-99 contain none of `volume`,
`projectedSurfaceArea`, `dragCoefficient`, `submergedVolumePercent`, or
`contactEpsilon`.

## Independent Ownership Review

The required hostile review was performed as a deliberately separate,
read-only rubber-duck pass. It inspected final package placement, the generic
retained contract, backend caps, terrain lifetime, body/fact alignment,
fixed-step consumers, static proofs, validation evidence, and touched comments.
No separate model or process is claimed.

Expected outcome: no feature vocabulary below its owner, no Physics row reaching
a World object, no generic seam secretly granting one feature authority, and no
uncovered determinism risk.

| Finding | Severity | Disposition |
|---|---|---|
| `ColliderStore.h` still claimed its shape drag copies fed fixed-step fluid forces after DB3 | Blocking stale ownership claim | corrected: those rows are shape/editor query copies and `BuoyancySystem` owns fixed-step fluid values |
| DX12 retained-resource caps lacked a local distinction between backend physical budgets and upper feature logical capacities | Non-blocking ownership ambiguity | added a `Concept:`/`Invariant:` block; caps require bounded GPU-memory/command evidence and may not mirror feature-policy growth automatically |

After remediation the review found no unresolved blocker, missing evidence,
backpointer, callback escape, duplicated mutable authority, feature-named
Rendering type, World object in Physics, or untested arithmetic change.

Residual risk is limited to future renamed feature vocabulary: exact tombstones
cannot identify a new synonym, so the standing broad zero-row review proof
remains mandatory by design.

## Determinism And Validation

| Phase/gate | Final evidence |
|---|---|
| DB1 DX12 renderer | pass; 43 shader stages current, zero InfoQueue errors, all committed captures accepted |
| DB1 bounded graphics stress | pass; 13,149 frames, 361 scene loads, zero upload drops, shutdown reconciliation delta zero |
| DB1 replay visual fidelity | pass; sole engine capture plus all offline comparisons and controls |
| DB2 physics | pass; two byte-identical 44,401-line runs and committed-baseline match |
| DB2 performance | pass; no hot-path or allocation regression |
| DB3 physics | pass; two byte-identical 44,401-line runs and committed-baseline match |
| DB3 performance/allocation | pass; zero gameplay allocations or reserve-policy violations |
| DB4 fast gate | pass; formatting, project ownership, dependency fixtures, Profile and Debug builds |
| DB4 CPU umbrella | pass; all six lanes, 393 cases, 2,403,315 assertions, every coverage floor |
| DB5 direct dependency validator | pass; 27 include rules, 46 negative edge fixtures, one content rule/two negative content fixtures, one project rule, zero findings |
| DB5 `tools\validate_full.bat` | pass in 265.2 seconds; mandatory CPU/coverage and all five runtime processes |
| DB5 DX12 comparison inside full | pass; zero validation errors, accepted committed baselines |
| DB5 physics comparison inside full | pass; lifecycle/runtime-handle smoke and byte-exact 44,401-line oracle |
| `python tools/check_related_paths.py --repo .` | pass; 566 files, 1,501 repository paths, zero findings |
| all static closure proofs | pass; zero rows |

DB2 and DB3 preserved float assignment, sampling, stage-read, iteration, and
reset order. DB5 changed only comments after the full behavioral gate exposed
no source defect. No baseline, golden, replay artifact, screenshot, shader,
scene, config, or physics CSV reference was refreshed.

## Comment Audit Checklist

Campaign base: `28764e53`, the final Replay partition commit and parent of DB0.

The final `git log --name-only 28764e53..HEAD` inventory plus the two DB5
comment-remediation files was reconciled against `git ls-files`.

Checked: 89/89. Deferred: 0. Unchecked: 0.

Eighty-eight hand-authored files have complete learning headers and verified
nearby ownership/lifetime/invariant comments. The generated reflection header
is checked under the generated-file exemption. All repository-relative
`Related:` paths resolve.

- [x] `SkullbonezData/generated/GeneratedShaderReflection.h` — generated exemption
- [x] `SkullbonezSource/Assets/AssetSystem.cpp`
- [x] `SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.cpp`
- [x] `SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h`
- [x] `SkullbonezSource/Physics/BuoyancySystem.cpp`
- [x] `SkullbonezSource/Physics/BuoyancySystem.h`
- [x] `SkullbonezSource/Physics/ColliderStore.h`
- [x] `SkullbonezSource/Physics/ObjectContactManifold.cpp`
- [x] `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- [x] `SkullbonezSource/Physics/PhysicsApi.h`
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.cpp`
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.h`
- [x] `SkullbonezSource/Physics/PhysicsEngine.cpp`
- [x] `SkullbonezSource/Physics/PhysicsEngine.h`
- [x] `SkullbonezSource/Physics/PhysicsTerrainView.cpp`
- [x] `SkullbonezSource/Physics/PhysicsTerrainView.h`
- [x] `SkullbonezSource/Physics/PhysicsWorld.cpp`
- [x] `SkullbonezSource/Physics/PhysicsWorld.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsStageContexts.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h`
- [x] `SkullbonezSource/Physics/TerrainContactManifold.cpp`
- [x] `SkullbonezSource/Physics/TerrainContactManifold.h`
- [x] `SkullbonezSource/Physics/TerrainSupportClassifier.h`
- [x] `SkullbonezSource/Rendering/DX12/Dx12Diagnostics.cpp`
- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp`
- [x] `SkullbonezSource/Rendering/DX12/Dx12ResourceBuilder.h`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Resources.cpp`
- [x] `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h`
- [x] `SkullbonezSource/Rendering/DX12/ShaderDX12.cpp`
- [x] `SkullbonezSource/Rendering/DX12/ShaderDX12.h`
- [x] `SkullbonezSource/Rendering/RenderCommandTypes.h`
- [x] `SkullbonezSource/Rendering/RenderDiagnosticsTypes.h`
- [x] `SkullbonezSource/Rendering/RenderGraph.cpp`
- [x] `SkullbonezSource/Rendering/RenderInstanceStore.cpp`
- [x] `SkullbonezSource/Rendering/RenderInstanceStore.h`
- [x] `SkullbonezSource/Rendering/ShaderContracts.h`
- [x] `SkullbonezSource/Rendering/ShaderReflectionContracts.h`
- [x] `SkullbonezSource/Rendering/WorldRenderExtension.h`
- [x] `SkullbonezSource/Runtime/App/Init.cpp`
- [x] `SkullbonezSource/Runtime/App/ReplayRuntime.cpp`
- [x] `SkullbonezSource/Runtime/App/Run.cpp`
- [x] `SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp`
- [x] `SkullbonezSource/Runtime/Capture/RuntimeStressController.cpp`
- [x] `SkullbonezSource/Runtime/Diagnostics/SceneMemoryDiagnostics.cpp`
- [x] `SkullbonezSource/Runtime/Editor/EditorHistory.cpp`
- [x] `SkullbonezSource/Runtime/Editor/EditorObjectPlacement.cpp`
- [x] `SkullbonezSource/Runtime/Editor/EditorTracer.cpp`
- [x] `SkullbonezSource/Runtime/Editor/LauncherTools.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.h`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionPresentation.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionPresentation.h`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionRetainedGeometry.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneController.Load.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneWorld.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneWorld.h`
- [x] `SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.cpp`
- [x] `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp`
- [x] `SkullbonezSource/Runtime/Tools/RuntimeTools.h`
- [x] `SkullbonezSource/Runtime/UI/OperatorEditorFrameComposer.cpp`
- [x] `SkullbonezSource/UI/UIRenderDiagnostics.h`
- [x] `SkullbonezSource/UI/UITabMemory.cpp`
- [x] `SkullbonezSource/World/Terrain.cpp`
- [x] `SkullbonezSource/World/Terrain.h`
- [x] `SkullbonezTests/TestCoverageFloorContracts.cpp`
- [x] `SkullbonezTests/TestDeterminism.cpp`
- [x] `SkullbonezTests/TestDx12OnlyRuntime.cpp`
- [x] `SkullbonezTests/TestPhysicsHandles.cpp`
- [x] `SkullbonezTests/TestPhysicsStageState.cpp`
- [x] `SkullbonezTests/TestReplayVisualPacket.cpp`
- [x] `SkullbonezTests/TestShaderReflectionContracts.cpp`
- [x] `SkullbonezTests/TestTerrain.cpp`
- [x] `tools/bake_shaders.py`
- [x] `tools/check_dependency_graph.py`
- [x] `tools/validate_project_filters.py`

## Plan History

- DB0 ratified B1-B3, registered the nine-row B4 include residue, fixed the
  exact value contracts, determinism strategy, and expected audit scope.
- DB1 moved trajectory semantics and retained ownership into Prediction while
  keeping a generic Rendering transport.
- DB2 installed the Physics-owned terrain view, moved support classification,
  and deleted every upward World/Assets include.
- DB3 moved five per-body fluid facts into a fixed-capacity aligned buoyancy
  store.
- DB4 installed package and exact retired-vocabulary rules with positive and
  planted negative fixtures.
- DB5 repeated the census/proofs, reconciled 89 source files, remediated two
  review findings, passed the final full gate, and closed the plan.

The active/future ledger was 7/19 immediately after DB5 completed. Inventory
rule 4 removes this completed six-task plan, leaving 1/13, or 8% rounded
overall. `invariant-ownership-governance-and-transaction-repair` GV1 is the
next binding task.
