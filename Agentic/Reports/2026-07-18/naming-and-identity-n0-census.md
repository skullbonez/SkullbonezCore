# Naming And Identity Debt N0 Census And Rulings

Date: 2026-07-18
Measured tip: `bcaf3f4b2` on `nightrunner-17th-july`
Owner: repository naming policy
Scope: N0 of `naming-and-identity-debt`

## Ratified Decisions

The current feature branch is ratified for N1-N4. These names are final for
implementation; later tasks do not reopen naming unless source evidence proves
the target describes the wrong owner.

| Debt | Ratified target | Rationale |
|---|---|---|
| `TestScene` and `TestSceneParser*` | `AuthoredScene` and `AuthoredSceneParser*` | The value is the parsed production scene document, distinct from the live `SceneWorld`; `Scene` alone would collide with the namespace and lifecycle vocabulary. |
| `GameModelRenderer` | `Rendering::RenderInstanceRenderer` | It consumes `RenderInstanceStore` plus collider rows and submits prepared instances; no `GameModel` type or ownership remains. The class moves from the stale `GameObjects` namespace to `Rendering`. |
| `RuntimeTuning.*` | `OperatorCommandApplier.*` | The stateless module validates and applies one-frame operator/UI commands. It is not runtime storage or a tuning migration bag. N3 keeps command application value-oriented and moves only already-clean domain seams to their owners. |
| `RunDemoDirector.*` | `DemoDirectorPlayback.*` | The files define the existing `DemoDirectorPlayback` namespace and no `Run` member; this is a pure filename/include/project rename. |
| `RunUiTextPass.cpp` | `UiTextPass.cpp` | The TU implements `UiTextPass` declared by the render-pass module and no `Run` member; this is a pure filename/project/coverage-path rename. |

`Run`, `RunFrame`, `RunInput`, `RunRender`, root-owned `Run*State` records,
and genuine `Runtime*` process/frame owners retain their names for the reasons
in the complete ruling table below. N4 is intentionally limited to the two
low-radius filename families above; it is not a mechanical prefix sweep.

## Measured Primary Blast Radius

Counts exclude `Agentic/`, `SkullbonezData/`, and generated validation output.
Filename hits count as occurrences because project and tool metadata must move
with source.

| Rename | Current defining files | Referencing files / occurrences | Required project and tool work |
|---|---:|---:|---|
| `TestScene` family → `AuthoredScene` family | 8 | 34 / 533 | Production project+filters: 6 parser/source TUs and 2 headers. Test project+filters and `Agentic/Tests/SceneParserUnitTests.vcxproj`: the same 6 TUs. Update `validate_project_filters.py`; keep test-case filenames and `validate_scene_parser_tests.bat` because their `Test` prefix describes tests, not the production type. |
| `GameModelRenderer` → `RenderInstanceRenderer` | 2 | 8 / 28 | Production project+filters: 1 TU and 1 header. Update `allocation_policy_allowlist.json` and `validate_project_filters.py` path rows. |
| `RuntimeTuning` → `OperatorCommandApplier` | 2 | 17 / 21 | Production project+filters: 1 TU and 1 header. Update `validate_project_filters.py`; source consumers span input, render, replay validation, scene orchestration, and stress. |
| `RunDemoDirector` filenames → `DemoDirectorPlayback` | 2 | 11 / 16 | Production project+filters: 1 TU and 1 header. Update includes and `validate_project_filters.py`; namespace and function names already match the target. |
| `RunUiTextPass.cpp` → `UiTextPass.cpp` | 1 | 6 / 6 exact-path hits | Production project+filters: 1 TU. Update `coverage_floors.json` and `validate_project_filters.py`; `UiTextPass` symbols already match the target. |

### Exact `AuthoredScene` Consumer Set

The eight defining files are `Scene/TestScene.cpp`, `TestScene.h`,
`TestSceneParser.cpp`, `TestSceneParserAssets.cpp`,
`TestSceneParserBodies.cpp`, `TestSceneParserPresentation.cpp`,
`TestSceneParserRuntime.cpp`, and `TestSceneParserSchema.h`.

Production consumers are `Runtime/Diagnostics/DiagnosticsRuntime.cpp/.h`,
`Runtime/LiveStyleController.cpp`, `Runtime/RunDemoDirector.cpp`,
`Runtime/RuntimeStressController.cpp`, `Runtime/RuntimeTuning.cpp`,
`Runtime/Scene/RunScene.cpp`, `SceneAuthoredSetup.cpp/.h`,
`SceneRuntimeCreate.cpp`, `SceneRuntimeStyle.cpp/.h`,
`SceneRuntimeUiOptions.cpp/.h`, and `Scene/SceneSnapshotWriter.cpp`.
Test/tool/metadata consumers are `SkullbonezTests/TestAssetSystem.cpp`,
`TestSceneEntityStore.cpp`, `TestSceneParserUnit.cpp`,
`TestSceneSnapshotWriter.cpp`, both production/test project files and filters,
`tools/migrate_data_formats.py`, `tools/validate_project_filters.py`, and
`tools/validate_scene_parser_tests.bat`.

### Exact Renderer And Command Consumer Sets

- `GameModelRenderer`: definitions plus `Maths/Frustum.h`,
  `Runtime/Render/RuntimeRenderPasses.cpp`, the production project/filter,
  `tools/allocation_policy_allowlist.json`, and
  `tools/validate_project_filters.py`.
- `RuntimeTuning`: definitions plus `Runtime/InputFrame.cpp`,
  `InputFrameExecution.cpp`, `Render/RenderPresentationSettings.h`,
  `Render/RuntimeRenderer.cpp`, `Render/RuntimeRenderPasses.cpp`,
  `Replay/ReplayValidation.cpp`, `ReplayValidation.Probes.cpp`, `RunFrame.cpp`,
  `RunInput.cpp`, `RunRender.cpp`, `RuntimeStressController.cpp`,
  `Scene/RunScene.cpp`, the production project/filter, and
  `tools/validate_project_filters.py`.

## Complete `Run*` / `Runtime*` File Rulings

The tracked-source inventory contains 62 matching `.cpp`/`.h` files. Every
file is named below. `KEEP` means the prefix describes an intentional process,
frame, or `Run` composition-root boundary; `N3` and `N4` identify the only
ratified moves.

| Files | Ruling | Reason |
|---|---|---|
| `Runtime/Allocation/RuntimeAllocationTracker.cpp`, `.h` | KEEP | Global runtime-phase allocation policy and diagnostics, distinct from construction/tool allocation. |
| `Runtime/Allocation/RuntimeReserveAllocator.cpp`, `.h` | KEEP | Registered runtime reserve policy owner; name is the domain contract. |
| `Runtime/Editor/RunEditorGizmoTools.cpp` | KEEP | Implements `Run::*` editor composition methods; not a separate owner type. |
| `Runtime/Editor/RunEditorHistory.cpp` | KEEP | Implements root sequencing around the concrete editor-history owner. |
| `Runtime/Editor/RunEditorObjectPlacement.cpp` | KEEP | Implements `Run::*` placement sequencing. |
| `Runtime/Editor/RunEditorOverlayTools.cpp` | KEEP | Implements `Run::*` overlay/tool composition. |
| `Runtime/Editor/RunEditorPlacementAssets.cpp` | KEEP | Implements `Run::*` registered-asset placement sequencing. |
| `Runtime/Editor/RunEditorTools.cpp` | KEEP | Implements `Run::*` editor entry-point sequencing. |
| `Runtime/Editor/RunEditorTracer.cpp` | KEEP | Implements `Run::*` tracer composition and replay-facing presentation. |
| `Runtime/Editor/RunMousePickupTools.cpp` | KEEP | Implements `Run::*` mouse-pick tool sequencing. |
| `Runtime/Render/RuntimeRenderHost.cpp`, `.h` | KEEP | Typed host boundary for the live runtime renderer; it is not a generic host bag. |
| `Runtime/Render/RuntimeRenderInputs.h` | KEEP | Frame-local runtime render input records distinguish live presentation from asset/offline inputs. |
| `Runtime/Render/RuntimeRenderPasses.cpp`, `.h` | KEEP | Concrete live frame-pass module, distinct from the low-level backend and reusable asset renderers. |
| `Runtime/Render/RuntimeRenderResources.h` | KEEP | Runtime renderer resource bundle with one device epoch; name is lifecycle-specific. |
| `Runtime/Render/RuntimeRenderer.cpp`, `.h` | KEEP | Cohesive live frame/presentation composer, deliberately distinct from `RenderBackendDX12` and `RenderInstanceRenderer`. |
| `Runtime/Run.cpp`, `.h` | KEEP | Authorized top-level composition root and process/frame sequencer. |
| `Runtime/RunCameraState.cpp`, `.h` | KEEP | State is deliberately stored by `Run` and carries top-level operator-camera sequencing, not extracted owner authority. |
| `Runtime/RunDebugState.h` | KEEP | Bounded root-owned debug orchestration state. |
| `Runtime/RunDemoDirector.cpp`, `.h` | N4 → `DemoDirectorPlayback.cpp`, `.h` | Files define only the already-named playback module; `Run` is stale filename history. |
| `Runtime/RunFrame.cpp` | KEEP | Physical TU for top-level `Run::Frame` sequencing. |
| `Runtime/RunInput.cpp` | KEEP | Physical TU for top-level input-command application order. |
| `Runtime/RunLaunchOptions.Renderer.h` | KEEP | Renderer-specific portion of root launch options. |
| `Runtime/RunLaunchOptions.h` | KEEP | Value record consumed by the `Run` composition root at startup. |
| `Runtime/RunRender.cpp` | KEEP | Physical TU for root render-frame ordering. |
| `Runtime/RunStartupState.h` | KEEP | Root-owned partial-startup unwind state. |
| `Runtime/RunTimerState.h` | KEEP | Root-owned frame/timer sequencing state. |
| `Runtime/RunUiTextPass.cpp` | N4 → `UiTextPass.cpp` | Implements `UiTextPass`, not `Run`; no symbol rename is required. |
| `Runtime/RuntimeCameraMode.h` | KEEP | Despite the filename/type prefix mismatch, this is a 22-file operator-mode protocol; its runtime qualification prevents collision with camera math/types and a wide rename has low value. |
| `Runtime/RuntimeDiagnostics.cpp`, `.h` | KEEP | Runtime diagnostic command/snapshot owner, distinct from subsystem diagnostics. |
| `Runtime/RuntimeFileWriter.cpp`, `.h` | KEEP | Cold runtime artifact writer capability, distinct from authored-data writers. |
| `Runtime/RuntimeFrameViews.h` | KEEP | Typed borrowed projections whose validity is exactly one runtime frame. |
| `Runtime/RuntimeInteractionCommands.h` | KEEP | Live interaction command protocol crossing input/runtime owners. |
| `Runtime/RuntimeInteractionController.cpp`, `.h` | KEEP | Concrete owner of live interaction state and command application. |
| `Runtime/RuntimeOverlayDiagnostics.cpp`, `.h` | KEEP | Runtime overlay diagnostic projection, distinct from render-backend diagnostics. |
| `Runtime/RuntimePickGeometry.cpp`, `.h` | KEEP | Live pick-geometry policy shared by operator/editor paths; runtime qualification distinguishes it from collision geometry. |
| `Runtime/RuntimePickService.cpp`, `.h` | KEEP | Concrete live picking owner. |
| `Runtime/RuntimeStressController.cpp`, `.h` | KEEP | Concrete owner of runtime graphics/interaction stress scheduling. |
| `Runtime/RuntimeTuning.cpp`, `.h` | N3 → `OperatorCommandApplier.cpp`, `.h` | Current name is migration vocabulary; the module applies stateless operator commands. |
| `Runtime/RuntimeValidationHarness.cpp`, `.h` | KEEP | Explicit runtime probe harness, distinct from CPU unit-test infrastructure. |
| `Runtime/RuntimeViewModel.cpp`, `.h` | KEEP | Truthful read-only presentation snapshot built from runtime owners. |
| `Runtime/Scene/RunScene.cpp` | KEEP | Implements root scene-load/request sequencing around `SceneController` and `SceneWorld`. |
| `Runtime/Tools/RuntimeTools.cpp`, `.h` | KEEP | Concrete owner of live operator tool state, distinct from offline repository tools. |
| `Runtime/UI/RuntimeUiSurface.h` | KEEP | Typed UI surface capability tied to runtime lifetime. |

Reconciliation: 62/62 tracked matching files are ruled; 3 files move in N4,
2 move in N3, and 57 remain under explicit reasons. Counting by the N4 subset
alone, 3 move and 59 retain their current `Run*`/`Runtime*` names until N3
renames its separately owned pair.

## Implementation Order And Safety

1. N1 renames the authored-scene family and production vocabulary. Test file
   names retain `Test` where it means a test harness; their includes and type
   spellings still update.
2. N2 renames/moves the render-instance submitter and updates its project,
   allowlist, and tool metadata paths.
3. N3 renames the operator-command module and applies only clean domain seams;
   it introduces no context bag, bridge, compatibility alias, or forwarding
   owner.
4. N4 performs the two ratified low-radius filename moves, verifies every KEEP
   ruling, and closes with one independent pure-rename/ownership review.

The pre-plan byte-identity manifests are:

- `SkullbonezData/`: 347 tracked files, git-index manifest
  `311c995ea32415392d6ef9b58e755eb5a948a847`.
- `TestOutput/baselines/`: 24 tracked files, git-index manifest
  `d1de0ad4226bde0a74e297c7073ca7d8d5ef90be`.

N1-N4 must reproduce both manifests exactly. No authored schema, key, version,
baseline, golden, screenshot, or coverage floor may move.

## Validation Decision

N0 is documentation-only; no repository validation is required. N1 uses
`tools\validate_full.bat`. N2 uses `tools\validate_fast.bat` and adds the
renderer/stress gates only if a DX12 file is touched. N3 uses
`tools\validate_full.bat`. N4 uses `tools\validate_full.bat` after the final
independent review. Every source-bearing rename also receives the touched-file
comment audit before its task commit.

The N0 census, rulings, and ledger reconciliation took about four minutes.
