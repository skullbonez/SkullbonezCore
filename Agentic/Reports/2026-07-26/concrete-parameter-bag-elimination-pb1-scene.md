# Concrete Parameter-Bag Elimination PB1 Scene Repair

Date: 2026-07-26  
Implementation base: `aece09d6`  
Branch: `nightrunner-25th-JUL-26`

## Result

PB1 passes. Scene saving now composes three concrete owner publications,
scene-load callers borrow concrete owners without `SceneLoadPolicyInputs`, and
the mixed editor save/screenshot context is split into focused operations.

The retired symbols have zero definitions/usages:

- `SceneSaveView`;
- `SceneLoadPolicyInputs`;
- `SceneLoadConsumerOutputs`;
- `EditorSaveHotkeyContext`.

## Concrete Save Boundary

`SceneSaveRequest` now contains exactly four fields:

1. destination path;
2. `SceneWorldSaveState`;
3. `SceneSessionSaveState`;
4. `PresentationSaveState`.

`SceneWorld::GetSaveState()` publishes the world-owned entity/body/collider/
joint, environment, mutual-gravity, and camera facts. `SceneSessionState`
publishes simulation, editor, fixed-step, and flat-slope values.
`OverlayDebugState` publishes the two presentation visibility values.
`SceneSnapshotWriter` accepts only the composed request and retains no borrowed
store or request value.

All production entry points use that complete owner path:

- editor snapshot hotkey;
- scene-load-only snapshot output;
- editable-scene save before cold scene replacement.

The previous editor partial initialization is impossible because the editor
operation receives all three owner publications.

## Scene-Load Boundary

`SceneLoadTransaction::CaptureSubmittedState` detaches camera, navigation,
presentation, renderer-name, and scene-time values before the load phase.
`Load` and `SceneController::ExecutePending` borrow their remaining concrete
owners explicitly. Their signatures stay at or below the 12-parameter ceiling.

The transaction retains only:

- request and private detached output values;
- phase cursor;
- bounded renderer-name storage;
- scene-time scalar.

It stores no runtime owner pointer/reference and introduces no interface,
callback, type erasure, or replacement service bag.

## Editor Action Split

`HandleEditorSceneSaveHotkey` borrows SceneWorld and session owners plus a
detached `PresentationSaveState`. `HandleEditorScreenshotHotkey` borrows only
`CaptureController`. Input routing no longer constructs or passes a combined
scene/capture context.

## Focused Tests

`TestSceneSnapshotWriter.cpp` now verifies:

- all session-save fields;
- both presentation-save fields;
- gravity, fluid height/density, and mutual-gravity settings;
- camera position/view/up;
- flat-slope values;
- existing object, material, asset-part, and stable-identity round trips.

## Comment Audit

Touched-source inventory: 20 files checked, 20 compliant, 0 deferred.

The audit verified or corrected:

- SceneRuntime versus SceneController lifecycle ownership;
- SceneLoadTransaction value retention and phase sequencing;
- editor save/capture authority separation;
- SceneWorld save-publication lifetime;
- writer topology and borrow lifetime;
- test claims about persisted owner values.

Every touched source-bearing file has a learning header. No term needs
human-approved wording.

## Static Proofs

The retired-symbol scan returns no rows:

```powershell
rg -n '\b(SceneSaveView|SceneLoadPolicyInputs|SceneLoadConsumerOutputs|EditorSaveHotkeyContext)\b' `
  SkullbonezSource SkullbonezTests
```

The threshold-13 inventory has no changed Scene/load/editor operation. The
introduced-line scan has no inheritance, virtual, `std::function`, callback,
service, or bindings indirection.

## Validation

- `tools\validate_tests.bat`: PASS, 398/398 tests and 2,403,462 assertions.
- `tools\validate_full.bat`: PASS in 173.3 seconds.
  - formatting and `Related:` paths;
  - 783/783 project filters;
  - dependency graph;
  - Profile/Automation/Debug builds;
  - mandatory CPU and coverage chain;
  - Automation/replay/prediction runtime lanes;
  - DX12 renderer validation with zero refresh;
  - 44,401-line physics regression byte-exact.

One earlier wrapper was terminated by a five-minute command ceiling after
continuing in the background; its result was intentionally discarded. The
173.3-second rerun above is the authoritative broad result.
