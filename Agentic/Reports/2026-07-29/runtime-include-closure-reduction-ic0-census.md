# Runtime Include-Closure Reduction IC0 Census

Date: 2026-07-29
Branch: `nightrunner-29th-JUL-26`
Plan: `Agentic/Plans/TODO/runtime-include-closure-reduction.md`
Scope: documentation and measurement only

## Result

The current branch reproduces the plan-registration include baseline exactly:
254 production translation units, 317 first-party headers, 17 translation
units above 200 reachable headers, 26 above 150, and 62 above 100. The lower
median is 38 headers; the two central rows are 38 and 39. The maximum is 255.

The complete per-TU inventory is stored beside this report in
`runtime-include-closure-reduction-ic0-tu-closure.csv`.

The first IC1 Profile build corrected the initial classification and edge
ruling recorded in commit `bd7b7e24`. Treat the corrected evidence below as
authoritative.

The selected IC2 chain edge is:

```text
Physics/PhysicsEngine.h -> Physics/PhysicsWorld.h
```

`PhysicsEngine` remains the owner. IC2 will move its by-value `PhysicsWorld`
member behind the repository's existing owner-boundary `std::unique_ptr`
lifetime pattern and define construction/destruction out of line. Engine
commands keep the pointer behind the owner boundary, and engine-side body
iteration binds the world before entering the loop. This removes
`PhysicsWorld.h`, all sixteen stage headers,
and `SpatialGrid.h` from every one of the 61 TUs that reaches the public engine
contract.

This edge is higher value than moving `Run -> SceneController`, which helps
only the Run-facing closure, or `SceneWorld -> PhysicsEngine`, which leaves all
direct engine-contract users on the solver subtree. It preserves the existing
public engine command/query surface without adding a Runtime facade, forwarding
header, alias, umbrella declarations, service bag, or new ownership convention.

## Measurement Method

The census starts from tracked files below `SkullbonezSource`: all 254 `.cpp`
translation units and all 317 `.h`, `.hpp`, and `.inl` headers. It parses
first-party include operands, resolves local-relative then
`SkullbonezSource`-relative paths with a unique-name fallback, and walks the
resolved graph transitively. The registered top-five and six named fan-in rows
all reproduce exactly, cross-checking the resolver against the original
2026-07-29 main-tip measurement.

Counts are current structure requiring review, not budgets or ratchets.

## Distribution And Heavy Fan-In

| Measure | Count |
|---|---:|
| Translation units | 254 |
| First-party headers | 317 |
| Closure above 200 | 17 |
| Closure above 150 | 26 |
| Closure above 100 | 62 |
| Lower median | 38 |
| Maximum | 255 |

| Header | Translation units reached |
|---|---:|
| `Physics/PhysicsBodyStore.h` | 113 |
| `Physics/SpatialGrid.h` | 73 |
| `Physics/PhysicsWorld.h` | 66 |
| `Physics/PhysicsEngine.h` | 61 |
| `UI/UI.h` | 43 |
| `Runtime/Scene/SceneController.h` | 37 |

The widest general-purpose headers remain `Core/Common.h` at 200 TUs,
`Core/SbResult.h` at 198, `Maths/MathsCommon.h` and `Maths/Vector3.h` at 176,
and `Core/SceneCapacity.h` at 174. They are context, not IC1/IC2 targets.

## Direct `PhysicsEngine.h` Classification

There are 35 physical direct includes. `Physics/PhysicsEngine.cpp` is the
owner's implementation and `Runtime/Scene/SceneWorld.h` is the by-value owner
declaration. The remaining 33 rows are the IC0 classification set: 27 files
invoke or implement the concrete engine command/query contract and keep
`PhysicsEngine.h`; 6 value-only consumers move to `PhysicsApi.h` in IC1.

Here “Owner” includes a source file that directly implements or invokes the
concrete owner contract. It does not claim that every caller stores the engine.
The compile failure from moving query/command callers to `PhysicsApi.h` proved
that the earlier 3/30 split confused storage ownership with dependency on the
concrete contract.

| Direct includer | Classification | Evidence |
|---|---|---|
| `Runtime/App/ReplayRuntime.cpp` | Owner | Invokes the engine command/query contract for live/replay coordination. |
| `Runtime/App/ReplayScrubberTools.cpp` | Owner | Invokes body/collider query operations on a borrowed engine. |
| `Runtime/App/ReplayValidation.Probes.cpp` | Owner | Invokes engine queries and commands for validation probes. |
| `Runtime/App/ReplayValidation.cpp` | Owner | Invokes the concrete engine contract through `SceneWorld`. |
| `Runtime/App/RunFrame.cpp` | Owner | Sequences the concrete engine command contract for one frame. |
| `Runtime/Automation/InteractionAutomationController.cpp` | Owner | Invokes engine queries and commands for automation. |
| `Runtime/Automation/InteractionAutomationReportWriter.cpp` | Owner | Invokes engine queries while producing reports. |
| `Runtime/Camera/AttachedCameraController.cpp` | Consumer | No direct engine token; public physics values are the dependency. |
| `Runtime/Diagnostics/RuntimeDiagnostics.cpp` | Owner | Invokes the engine diagnostics/configuration contract. |
| `Runtime/Diagnostics/SceneMemoryDiagnostics.cpp` | Owner | Invokes concrete memory and capacity queries. |
| `Runtime/Editor/EditorGizmoTools.cpp` | Consumer | No direct engine token; editor values are the dependency. |
| `Runtime/Editor/EditorHistory.cpp` | Owner | Invokes concrete buoyancy queries for history projection. |
| `Runtime/Editor/EditorInteractionTools.cpp` | Owner | Invokes handle-based editor commands on the engine. |
| `Runtime/Editor/EditorObjectPlacement.cpp` | Consumer | No direct engine token; placement descriptors are the dependency. |
| `Runtime/Editor/MousePickupTools.cpp` | Owner | Invokes engine queries and commands through a borrow. |
| `Runtime/Planning/ReplayPlanningRuntime.cpp` | Owner | Invokes the live engine command/query contract. |
| `Runtime/Prediction/ReplayPrediction.cpp` | Owner | Implements the isolated prediction simulation and directly steps/seeds its engine. |
| `Runtime/Prediction/ReplayPredictionDrawing.cpp` | Owner | Invokes concrete body/collider engine queries for presentation. |
| `Runtime/Prediction/ReplayPredictionPublication.cpp` | Consumer | No direct engine token; publication values are the dependency. |
| `Runtime/Prediction/ReplayPredictionReserve.cpp` | Owner | Constructs, sizes, and owns the prediction engine backing. |
| `Runtime/Prediction/ReplayPredictionScheduling.cpp` | Consumer | No direct engine token; scheduling values are the dependency. |
| `Runtime/Prediction/ReplayPredictionTopologyPublication.cpp` | Consumer | No direct engine token; topology publication values are the dependency. |
| `Runtime/Render/RenderModelFramePublisher.cpp` | Owner | Invokes concrete engine queries for frame publication. |
| `Runtime/Replay/ReplayAuthoringVelocity.cpp` | Owner | Invokes authoring commands and body/collider queries. |
| `Runtime/Replay/ReplayRecorder.cpp` | Owner | Invokes contact, sleep, and pipeline engine queries. |
| `Runtime/Replay/ReplayRestoreService.h` | Owner | Header-only restore orchestration invokes engine commands. |
| `Runtime/Scene/SceneAuthoredSetup.cpp` | Owner | Invokes scene-creation commands on `SceneWorld`'s engine. |
| `Runtime/Scene/SceneController.cpp` | Owner | Invokes concrete engine count queries. |
| `Runtime/Scene/SceneGeneratedSetup.cpp` | Owner | Invokes generated-scene engine commands. |
| `Runtime/Startup/StartupProbeHarnesses.cpp` | Owner | Constructs standalone engines and proves their lifecycle directly. |
| `Runtime/Tools/RuntimeTools.cpp` | Owner | Invokes launcher queries and handle-based commands. |
| `Runtime/UI/OperatorEditorFrameComposer.cpp` | Owner | Invokes engine query/settings operations for UI composition. |
| `Runtime/UI/RuntimeViewModel.cpp` | Owner | Invokes the concrete engine model-count query. |

## Clean Full-Rebuild Baseline

Both configurations use the standard solution, platform, warning policy, and
installed project toolset with `/t:Rebuild`:

```text
MSBuild SKULLBONEZ_CORE.sln /t:Rebuild /p:Configuration=<config>
  /p:Platform=x64 /nologo /v:normal
  /clp:Summary;PerformanceSummary /warnaserror
```

| Configuration | Wall time | Result | Ignored raw log |
|---|---:|---|---|
| Debug x64 | 43.066 s | PASS | `TestOutput/validation/agent_logs/ic0_rebuild_debug.out.log` |
| Profile x64 | 44.614 s | PASS | `TestOutput/validation/agent_logs/ic0_rebuild_profile.out.log` |

These are single local wall-clock samples, suitable for before/after reporting
but not a claim of statistically significant build-speed change.

## Validation

IC0 changed documentation only. Both required clean rebuild measurements passed,
but no repository validation gate was required. No source, baseline, golden,
config, schema, allowlist, or committed runtime artifact changed.
