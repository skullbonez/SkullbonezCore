# Naming N1 Comment-Style Audit

Date: 2026-07-18
Scope: every source-bearing file changed by the N1 `AuthoredScene` rename
Guide: `Agentic/Reference/comment-style-guide.md`

## Result

30 checked, 0 deferred, 0 unchecked.

The diff is a pure production-vocabulary rename plus the learning-header phrase
`test-scene JSON` → `authored-scene JSON`. Before formatting, a zero-context
diff scan found zero added or removed lines outside `TestScene` /
`AuthoredScene`, `test-scene` / `authored-scene`, and the matching display
heading. The repository formatter subsequently reflowed only the six files
named by the first gate attempt after the identifiers grew. No algorithm,
invariant, ownership, lifetime, unit, error lane, allocation behavior, or
validation path changed.

Every C++ file below retains all learning-header sections (`Purpose`, `Summary`,
`Glossary`, `Invariants`, and `Related`) and its existing local `Concept:`,
`Why:`, `Invariant:`, `Lifetime:`, or `Hazard:` teaching where non-obvious code
requires it. The two touched tool scripts use the guide's equivalent
`Mental model` heading; the small batch wrapper is self-explanatory and already
documents its parser-test purpose, invariants, and validation role. No new local
comment was needed because N1 changes no body logic.

## Checked Inventory

- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp`
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h`
- [x] `SkullbonezSource/Runtime/LiveStyleController.cpp`
- [x] `SkullbonezSource/Runtime/RunDemoDirector.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeStressController.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeTuning.cpp`
- [x] `SkullbonezSource/Runtime/Scene/RunScene.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeCreate.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeStyle.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeStyle.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeUiOptions.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeUiOptions.h`
- [x] `SkullbonezSource/Scene/AuthoredScene.cpp`
- [x] `SkullbonezSource/Scene/AuthoredScene.h`
- [x] `SkullbonezSource/Scene/AuthoredSceneParser.cpp`
- [x] `SkullbonezSource/Scene/AuthoredSceneParserAssets.cpp`
- [x] `SkullbonezSource/Scene/AuthoredSceneParserBodies.cpp`
- [x] `SkullbonezSource/Scene/AuthoredSceneParserPresentation.cpp`
- [x] `SkullbonezSource/Scene/AuthoredSceneParserRuntime.cpp`
- [x] `SkullbonezSource/Scene/AuthoredSceneParserSchema.h`
- [x] `SkullbonezSource/Scene/SceneSnapshotWriter.cpp`
- [x] `SkullbonezTests/TestAssetSystem.cpp`
- [x] `SkullbonezTests/TestSceneParserUnit.cpp`
- [x] `SkullbonezTests/TestSceneSnapshotWriter.cpp`
- [x] `Agentic/Tests/SceneParserUnitTests/SceneParserUnitTests.cpp`
- [x] `tools/migrate_data_formats.py`
- [x] `tools/validate_project_filters.py`
- [x] `tools/validate_scene_parser_tests.bat`

The three test filenames intentionally retain `TestScene*`: their `Test`
prefix names the test harness, while every production type, include, owner
diagnostic, and test assertion now uses `AuthoredScene*`.
