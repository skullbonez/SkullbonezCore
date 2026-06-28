# Runtime Ownership Night Runner Report

Date: 2026-06-27
Branch: `Night-Runner-27th-June`
Plan: `Agentic/Plans/engine-evaluation-fix-01-runtime-ownership-plan.md`
Status: Implementation, final validation, and final rubber-duck signoff complete

## Startup State

- Agent Startup Contract read set: `AGENTS.md`, `README.md`, `Agentic/README.md`, and `Agentic/SessionState.md`.
- Live git status before edits: `## Night-Runner-27th-June`, clean.
- `Agentic/SessionState.md` still named an older branch, so live `git status --short --branch` is the branch source of truth.
- Related plan context read: `Agentic/Plans/run-shell-extraction-plan.md` and `Agentic/Plans/architecture_pass_2026-06-02.md`.
- Impact area: runtime architecture, scene system, renderer orchestration, replay, editor tools, diagnostics, UI, validation tooling.
- Required final gate: `tools\validate_full.bat`, because this plan changes broad runtime, scene, render-host, replay/tool, UI, and validation-tool surfaces.

## What Changed

Plan 1 turned the runtime from a large behavior owner into a thinner composition shell across the runtime, render host, scene lifecycle, replay/tools overlays, diagnostics, and the largest runtime/UI files.

- `RuntimeRenderHostBindings` is now a set of named borrowed views instead of a flat bag of runtime internals: `RenderRuntimeView`, `RenderWorldView`, `RenderSceneView`, `RenderReplayOverlayView`, `RenderToolOverlayView`, `RenderUiView`, and `RenderDiagnosticsView`.
- Replay overlay decisions moved behind `ReplayRuntime` predicates, including live advance, path visualizer, camera focus, and velocity-edit state.
- Tool overlay decisions moved behind `RuntimeTools` predicates, including lingered ray-cast line, selection overlay, mouse pickup, launcher shots, and launcher fire-mode label.
- `Run::DrawPrimitives()` was removed; render execution stays routed through `RuntimeRenderer::RenderFrame()` and render-host/pass services.
- Scene lifecycle events are explicit: `BeforeSceneUnload`, `AfterSceneCleared`, `BeforeScenePopulate`, `AfterScenePopulate`, and `AfterSceneActivated`.
- Scene-owned helpers now absorb authored scene UI policy, generated-scene population policy, scene-browser/UI override state, and scene automation setup policy.
- `DiagnosticsRuntime::ApplySceneAutomationOptions(...)` owns screenshot/perf automation setup that was previously sequenced in `RunScene.cpp`.
- Boundary tooling now rejects both broad top-level render-host binding growth and unexpected nested fields inside each render-host view.

## Post-Duck Fixes

The first rubber-duck pass blocked the work because several checked boxes were overclaimed. The fixes were made before any commit:

- Scene ownership was strengthened with `SceneRuntimeUiOptions` and `SceneGeneratedSetup::TrySetUpRequestedModels(...)`, moving authored UI decisions and generated population policy out of `RunScene.cpp`.
- `SceneController` now owns `RunSceneBrowserState` and `RunSceneUIOverrideState`; `Run.h` no longer stores those scene-owned states directly.
- `DiagnosticsRuntime` now owns scene automation setup through `ApplySceneAutomationOptions(...)`.
- Large-file splitting was expanded from shallow includes into named editor/replay ownership slices.
- `tools/check_runtime_boundaries.py` now validates allowed fields for every render-host view, not just the top-level view roots.
- Comment-style evidence was regenerated for the full touched source-bearing set.

## Line Counts

Fresh baseline before splitting:

| Lines | File |
|---:|---|
| 4750 | `SkullbonezSource/Runtime/Editor/RunEditorTools.cpp` |
| 4260 | `SkullbonezSource/UI/UI.cpp` |
| 3505 | `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp` |
| 2977 | `SkullbonezSource/Runtime/RunInput.cpp` |
| 2565 | `SkullbonezSource/Scene/TestSceneParser.cpp` |
| 2452 | `SkullbonezSource/Runtime/Init.cpp` |
| 2372 | `SkullbonezSource/Runtime/RunFrame.cpp` |
| 2297 | `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp` |
| 2041 | `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp` |
| 1699 | `SkullbonezSource/Runtime/RunPasses.cpp` |

Post-split physical line counts for the directly split files:

| Lines | File |
|---:|---|
| 1623 | `SkullbonezSource/Runtime/Editor/RunEditorTools.cpp` |
| 1897 | `SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.inl` |
| 355 | `SkullbonezSource/Runtime/Editor/RunEditorGizmoTools.inl` |
| 172 | `SkullbonezSource/Runtime/Editor/RunEditorOverlayTools.inl` |
| 483 | `SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.inl` |
| 568 | `SkullbonezSource/Runtime/Editor/RunEditorTracer.inl` |
| 234 | `SkullbonezSource/Runtime/Editor/RunMousePickupTools.inl` |
| 607 | `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp` |
| 1186 | `SkullbonezSource/Runtime/Replay/RunReplayPredictionHelpers.inl` |
| 64 | `SkullbonezSource/Runtime/Replay/RunReplayImportExport.inl` |
| 669 | `SkullbonezSource/Runtime/Replay/RunReplayScrubberTools.inl` |
| 267 | `SkullbonezSource/Runtime/Replay/RunReplayCauseTreeTools.inl` |
| 149 | `SkullbonezSource/Runtime/Replay/RunReplayQueryTools.inl` |
| 525 | `SkullbonezSource/Runtime/Replay/RunReplayVelocityEdit.inl` |
| 530 | `SkullbonezSource/Runtime/Replay/RunReplayPredictionVisualizer.inl` |
| 3001 | `SkullbonezSource/UI/UI.cpp` |
| 1599 | `SkullbonezSource/UI/UIEditorMiniPalette.inl` |
| 407 | `SkullbonezSource/Runtime/Run.h` |
| 68 | `SkullbonezSource/Runtime/RuntimeInteractionCommands.h` |

The new `.inl` files are source-bearing include slices, not new translation units. That preserves local linkage and compile order while making ownership surfaces explicit in Visual Studio and review. `tools/validate_project_filters.py` validates the new `.inl` slices as `ClInclude` entries.

`Run.h` is now 407 lines, below the 420-line `HEAD` baseline called out by final review. The final reduction comes from moving runtime interaction command/event record definitions into `RuntimeInteractionCommands.h`, leaving only forward declarations in `Run.h`.

## Guardrails

`tools/check_runtime_boundaries.py` now protects these runtime ownership seams:

- Runtime subsystems still cannot store `Run*` or `Run&`.
- `RuntimeRenderHostBindings` can expose only the named view roots.
- Every render-host view has an explicit allowed-field list.
- Old broad render-host fields, replay/tool callback bridges, scrubber/prediction callback fields, and main-memory callback fields are rejected by synthetic tests.
- New `Run` forwarding-wrapper growth remains guarded by existing runtime-boundary checks.
- Boundary failures name this plan as the owning remediation plan.

## Comment Audit

Audit artifact: `Agentic/Reports/2026-06-27/runtime-ownership-comment-audit.md`.

- Scope: touched source-bearing files from `git ls-files -m -o --exclude-standard`.
- Result: 47 checked, 0 deferred.
- Header criteria checked: `File:`, `Purpose:`, `Mental model:`, `Glossary:`, `Invariants:`, `Related:`.
- Local teaching-comment criteria checked: `Concept:`, `Why:`, `Invariant:`, `Lifetime:`, or `Hazard:`.

## Validation Evidence So Far

- `tools\validate_build.bat Profile`
  - First post-duck run failed because `SceneGeneratedSetup.h` exposed `DEFAULT_GAME_MODELS` to includers that did not include its definition.
  - Fix: made the request default independent and passed explicit defaults at call sites.
  - Rerun log: `TestOutput\validation\night_runner_runtime_ownership_post_duck_profile_build_rerun.log`.
  - Rerun result: build succeeded, `0 Warning(s)`, `0 Error(s)`.
  - Runtime: 54.494 seconds.
- `tools\validate_fast.bat`
  - First post-duck run failed because `SceneRuntimeUiOptions.*` needed project-filter classification.
  - Fix: added `SceneRuntimeUiOptions` to the runtime scene prefix rules.
  - Rerun log: `TestOutput\validation\night_runner_runtime_ownership_post_duck_validate_fast_rerun.log`.
  - Rerun result: formatting, project filters, runtime boundaries, Profile build, and ready Profile/Debug builds passed.
  - Runtime: 130.433 seconds.
- `tools\validate_fast.bat`
  - Final log after comment/report edits: `TestOutput\validation\night_runner_runtime_ownership_final_validate_fast.log`.
  - Result: `VALIDATE_FAST_EXIT=0`; formatting, project filters, runtime boundaries, Profile build, and ready Profile/Debug builds passed.
  - Runtime: 128.323 seconds.
- `tools\validate_full.bat`
  - Final log: `TestOutput\validation\night_runner_runtime_ownership_final_validate_full.log`.
  - Result: `VALIDATE_FULL_EXIT=0`; `VALIDATE_FULL: DEFAULT GATE PASSED`.
  - Runtime: 27.665 seconds.
  - Project metadata: project filters and runtime boundaries both passed with 0 errors.
  - Build evidence: Profile and Debug builds succeeded with `0 Warning(s)` and `0 Error(s)`.
  - DX12 evidence: InfoQueue validation errors were 0 and screenshots matched committed baselines.
  - Physics evidence: `physics_regression_solver.csv` matched the committed baseline byte-exactly across 20001 lines.
- `tools\validate_fast.bat`
  - Final log after the `Run.h` interaction-command extraction: `TestOutput\validation\night_runner_runtime_ownership_runh_fix_validate_fast.log`.
  - Result: `VALIDATE_FAST_EXIT=0`; formatting, project filters, runtime boundaries, Profile build, and ready Profile/Debug builds passed.
  - Runtime: 95.117 seconds.
- `tools\validate_full.bat`
  - Final log after the `Run.h` interaction-command extraction: `TestOutput\validation\night_runner_runtime_ownership_runh_fix_validate_full.log`.
  - Result: `VALIDATE_FULL_EXIT=0`; `VALIDATE_FULL: DEFAULT GATE PASSED`.
  - Runtime: 27.695 seconds.
  - Project metadata: project filters and runtime boundaries both passed with 0 errors.
  - Build evidence: Profile and Debug builds succeeded with `0 Warning(s)` and `0 Error(s)`.
  - DX12 evidence: InfoQueue validation errors were 0 and screenshots matched committed baselines.
  - Physics evidence: `physics_regression_solver.csv` matched the committed baseline byte-exactly across 20001 lines.

## Pending Before Commit

- None. Final rubber-duck review reported 80 checked, 0 unchecked, no blockers, and a `PASS` verdict for committing Plan 1.

## Residual Notes

- Physics storage/data boundary work is intentionally deferred to `Agentic/Plans/engine-evaluation-fix-02-physics-data-boundary-plan.md`.
- Render graph command-recording ownership is intentionally deferred to `Agentic/Plans/engine-evaluation-fix-03-render-graph-execution-plan.md`.
- `RunInput.cpp`, `Init.cpp`, and other runtime files remain future candidates, but Plan 1's acceptance target is met by narrowing render-host views, moving replay/tool/scene/diagnostics decisions to owners, adding guardrails, and decomposing the largest runtime/UI concentration files by ownership.
