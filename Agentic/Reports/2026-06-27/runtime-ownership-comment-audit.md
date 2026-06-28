# Runtime Ownership Comment Audit

Date: 2026-06-27
Plan: `Agentic/Plans/engine-evaluation-fix-01-runtime-ownership-plan.md`
Scope rule: touched source-bearing files from `git ls-files -m -o --exclude-standard`

Result: 47 checked, 0 deferred, 0 missing learning headers, 0 missing local teaching comments.

Validation scan:

- Required header sections checked: `File:`, `Purpose:`, `Mental model:`, `Glossary:`, `Invariants:`, `Related:`.
- Local teaching comments checked: `Concept:`, `Why:`, `Invariant:`, `Lifetime:`, or `Hazard:`.

Checked files:

- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp`
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h`
- [x] `SkullbonezSource/Runtime/Editor/RunEditorGizmoTools.inl`
- [x] `SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.inl`
- [x] `SkullbonezSource/Runtime/Editor/RunEditorOverlayTools.inl`
- [x] `SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.inl`
- [x] `SkullbonezSource/Runtime/Editor/RunEditorTools.cpp`
- [x] `SkullbonezSource/Runtime/Editor/RunEditorTracer.inl`
- [x] `SkullbonezSource/Runtime/Editor/RunMousePickupTools.inl`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderHost.cpp`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderHost.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRuntime.h`
- [x] `SkullbonezSource/Runtime/Replay/RunReplayCauseTreeTools.inl`
- [x] `SkullbonezSource/Runtime/Replay/RunReplayImportExport.inl`
- [x] `SkullbonezSource/Runtime/Replay/RunReplayPredictionHelpers.inl`
- [x] `SkullbonezSource/Runtime/Replay/RunReplayPredictionVisualizer.inl`
- [x] `SkullbonezSource/Runtime/Replay/RunReplayQueryTools.inl`
- [x] `SkullbonezSource/Runtime/Replay/RunReplayScrubberTools.inl`
- [x] `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp`
- [x] `SkullbonezSource/Runtime/Replay/RunReplayVelocityEdit.inl`
- [x] `SkullbonezSource/Runtime/Run.cpp`
- [x] `SkullbonezSource/Runtime/Run.h`
- [x] `SkullbonezSource/Runtime/RunFrame.cpp`
- [x] `SkullbonezSource/Runtime/RunInput.cpp`
- [x] `SkullbonezSource/Runtime/RunLiveStyle.cpp`
- [x] `SkullbonezSource/Runtime/RunPasses.cpp`
- [x] `SkullbonezSource/Runtime/RunRender.cpp`
- [x] `SkullbonezSource/Runtime/RunStress.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeInteractionCommands.h`
- [x] `SkullbonezSource/Runtime/RunUiTextPass.cpp`
- [x] `SkullbonezSource/Runtime/Scene/RunScene.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneController.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneController.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntime.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntime.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeUiOptions.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeUiOptions.h`
- [x] `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp`
- [x] `SkullbonezSource/Runtime/Tools/RuntimeTools.h`
- [x] `SkullbonezSource/UI/UI.cpp`
- [x] `SkullbonezSource/UI/UIEditorMiniPalette.inl`
- [x] `tools/check_runtime_boundaries.py`
- [x] `tools/validate_project_filters.py`

Deferred files: none.
