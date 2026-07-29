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

The selected IC2 chain edge is:

```text
Runtime/Scene/SceneWorld.h -> Physics/PhysicsEngine.h
```

`SceneWorld` remains the owner. IC2 will move its by-value `PhysicsEngine`
member behind the repository's existing composition-root `std::unique_ptr`
lifetime pattern, define construction/destruction out of line, and preserve the
same accessor authority. This breaks the solver-private subtree for every
non-physics translation unit that reaches `SceneWorld.h` without adding a
forwarding header, alias, umbrella declarations, service bag, or new ownership
convention.

This edge is higher value than moving `Run -> SceneController`: the latter
helps only the Run-facing closure and leaves every other `SceneWorld` consumer
unchanged. It is safer than moving `PhysicsEngine -> PhysicsWorld`: that edge is
inside the Physics owner and risks adding pointer access to hot owner methods.
The selected edge is the existing Runtime-to-Physics composition boundary, and
the same project already uses the pattern for `Run::m_operatorUi`.

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
owner's implementation and `Runtime/Scene/SceneWorld.h` is the selected IC2
ownership edge. The remaining 33 rows are the IC0 classification set:
3 owner implementations keep the concrete header and 30 consumers move to
`PhysicsApi.h` in IC1.

“Consumer” describes ownership, not whether the current file happens to call a
concrete method. Query and command consumers must receive the smallest honest
public values/commands needed at the existing owner edge; they do not gain a
solver-private include merely because the current API exposes a store.

| Direct includer | Classification | Evidence |
|---|---|---|
| `Runtime/App/ReplayRuntime.cpp` | Consumer | Coordinates live/replay queries and commands; owns no engine. |
| `Runtime/App/ReplayScrubberTools.cpp` | Consumer | Reads body/collider query state from a borrowed engine. |
| `Runtime/App/ReplayValidation.Probes.cpp` | Consumer | Probe orchestration borrows the engine and reads public evidence. |
| `Runtime/App/ReplayValidation.cpp` | Consumer | Validation orchestration borrows `SceneWorld` physics. |
| `Runtime/App/RunFrame.cpp` | Consumer | Sequences the owner for one frame; stores no physics authority. |
| `Runtime/Automation/InteractionAutomationController.cpp` | Consumer | Applies automation commands and reads detached/query facts. |
| `Runtime/Automation/InteractionAutomationReportWriter.cpp` | Consumer | Produces reports from borrowed query state. |
| `Runtime/Camera/AttachedCameraController.cpp` | Consumer | No direct engine token; public physics values are the dependency. |
| `Runtime/Diagnostics/RuntimeDiagnostics.cpp` | Consumer | Configures and samples a borrowed engine for diagnostics. |
| `Runtime/Diagnostics/SceneMemoryDiagnostics.cpp` | Consumer | Reads memory/capacity values; owns no simulation state. |
| `Runtime/Editor/EditorGizmoTools.cpp` | Consumer | No direct engine token; editor values are the dependency. |
| `Runtime/Editor/EditorHistory.cpp` | Consumer | Reads buoyancy/query facts for history projection. |
| `Runtime/Editor/EditorInteractionTools.cpp` | Consumer | Sends handle-based editor commands to the existing owner. |
| `Runtime/Editor/EditorObjectPlacement.cpp` | Consumer | No direct engine token; placement descriptors are the dependency. |
| `Runtime/Editor/MousePickupTools.cpp` | Consumer | Performs queries and commands through a borrowed engine. |
| `Runtime/Planning/ReplayPlanningRuntime.cpp` | Consumer | Reads and commands live physics but owns neither engine. |
| `Runtime/Prediction/ReplayPrediction.cpp` | Owner | Implements the isolated prediction simulation and directly steps/seeds its engine. |
| `Runtime/Prediction/ReplayPredictionDrawing.cpp` | Consumer | Projects borrowed body/collider query state into presentation. |
| `Runtime/Prediction/ReplayPredictionPublication.cpp` | Consumer | No direct engine token; publication values are the dependency. |
| `Runtime/Prediction/ReplayPredictionReserve.cpp` | Owner | Constructs, sizes, and owns the prediction engine backing. |
| `Runtime/Prediction/ReplayPredictionScheduling.cpp` | Consumer | No direct engine token; scheduling values are the dependency. |
| `Runtime/Prediction/ReplayPredictionTopologyPublication.cpp` | Consumer | No direct engine token; topology publication values are the dependency. |
| `Runtime/Render/RenderModelFramePublisher.cpp` | Consumer | Projects immutable physics views into one render frame. |
| `Runtime/Replay/ReplayAuthoringVelocity.cpp` | Consumer | Applies authoring commands and reads public body/collider facts. |
| `Runtime/Replay/ReplayRecorder.cpp` | Consumer | Records immutable contacts, sleep, and pipeline views. |
| `Runtime/Replay/ReplayRestoreService.h` | Consumer | Header-only restore orchestration applies commands to a borrowed owner. |
| `Runtime/Scene/SceneAuthoredSetup.cpp` | Consumer | Builds descriptors and issues commands to `SceneWorld`'s owner. |
| `Runtime/Scene/SceneController.cpp` | Consumer | Reads owner counts for controller policy; owns no physics state. |
| `Runtime/Scene/SceneGeneratedSetup.cpp` | Consumer | Issues generated-scene commands to `SceneWorld`'s owner. |
| `Runtime/Startup/StartupProbeHarnesses.cpp` | Owner | Constructs standalone engines and proves their lifecycle directly. |
| `Runtime/Tools/RuntimeTools.cpp` | Consumer | Performs launcher queries and handle-based commands against a borrow. |
| `Runtime/UI/OperatorEditorFrameComposer.cpp` | Consumer | Projects query/settings values for UI composition. |
| `Runtime/UI/RuntimeViewModel.cpp` | Consumer | Publishes model-count/query values only. |

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
