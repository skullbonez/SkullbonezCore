# Run Composition Root Shrink Plan

Date: 2026-06-26
Status: Active architecture cleanup plan; replay loaded-presentation scrubber arming slice validated
Impact area: runtime architecture, editor tools, replay tools, scene runtime, render host boundaries
Validation for latest implementation slice: see the replay loaded-presentation scrubber arming section below

## Goal

Make `Run` shrink in source, not just in intent.

A refactor only counts when it deletes `Run::` declarations from
`SkullbonezSource/Runtime/Run.h`. Moving code between `Run*.cpp` files, adding
subsystem state, or adding callbacks from a subsystem back into `Run` is not
enough.

## Current In-Flight Branch Note

The first launcher helper slice is implemented and validated in:

- `SkullbonezSource/Runtime/Editor/LauncherTools.cpp`
- `SkullbonezSource/Runtime/Run.h`
- `SkullbonezSource/Runtime/RunFrame.cpp`
- `SkullbonezSource/Runtime/Scene/RunScene.cpp`
- `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp`
- `SkullbonezSource/Runtime/Tools/RuntimeTools.h`
- `tools/check_runtime_boundaries.py`

That slice moved ray-test line clear/add/tick behavior and launcher model/terrain
hit tests into `RuntimeTools`, routes scene/reset/replay restore call sites
through `m_runtimeTools`, and adds a `Run.h` private-method-count ratchet in
`tools/check_runtime_boundaries.py`.

Deleted `Run.h` declarations:

- `ClearRayCastTestLines`
- `AddRayCastTestLine`
- `TickRayCastTestLines`
- `TryRayCastTestHit`
- `TryLauncherTerrainHit`

New owner methods:

- `RuntimeTools::ClearRayCastTestLines()`
- `RuntimeTools::AddRayCastTestLine(...)`
- `RuntimeTools::TickRayCastTestLines(float)`
- `RuntimeTools::TryRayCastTestHit(...) const`
- `RuntimeTools::TryLauncherTerrainHit(...) const`

Validation:

- Targeted build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\run_composition_launcher_tools_profile_build_rerun.log`;
  passed with 0 warnings and 0 errors.
- Rubber duck: reviewer Bohr found no blocking defect; noted that the slice
  should not be treated as the whole launcher extraction because fire dispatch,
  laser/projectile behavior, and launcher repro snapshot helpers still live on
  `Run`.
- Pre-commit gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\run_composition_launcher_tools_validate_fast_final.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\run_composition_launcher_tools_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv`.

Remaining launcher shrink work is to move or delete the still-live `Run::`
launcher methods for fire dispatch, laser/projectile behavior, and launcher
repro snapshot helpers. This helper slice also keeps a compatibility bridge that
accepts caller-owned `std::vector<GameModel>` and raw terrain input; shrink that
bridge as the runtime tools boundary gains more ownership.

## Launcher Fire Slice

The second launcher slice moved laser/projectile fire behavior into
`RuntimeTools` while keeping `Run::FireRayCastTest()` as the temporary
composition-root dispatcher that computes camera rays, records replay fire
events, and updates `SceneState().modelCount` after projectile insertion.
Replay restore now applies launcher fire events through the same `RuntimeTools`
methods.

Deleted `Run.h` declarations:

- `FireLauncherLaser`
- `FireLauncherProjectile`

Deleted `Run::` definitions:

- `Run::FireLauncherLaser`
- `Run::FireLauncherProjectile`

New owner methods:

- `RuntimeTools::FireLauncherLaser(...)`
- `RuntimeTools::FireLauncherProjectile(...)`

Validation:

- Targeted build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\run_composition_launcher_fire_profile_build.log`;
  passed with 0 warnings and 0 errors.
- Rubber duck: reviewer Hooke found no blocking defect; noted that the new
  methods still accept `GameModelCollection&`, `WorldEnvironment&`, and raw
  `Terrain*`, which is acceptable compatibility debt for this slice.
- Formatting check: `tools\validate_format.bat`, logged at
  `TestOutput\validation\run_composition_launcher_fire_validate_format_rerun.log`;
  passed after targeted formatting of the touched launcher/runtime files.
- Pre-commit gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\run_composition_launcher_fire_validate_full_rerun.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv`.

Remaining launcher shrink work is now limited to the still-live
`Run::FireRayCastTest()` dispatcher and the debug-only launcher repro target and
snapshot helpers. Broader editor, replay, scene runtime, and render-host slices
remain active plan work.

## Launcher Dispatch Slice

The third launcher slice removed the remaining non-debug launcher fire dispatcher
from `Run`. `RuntimeTools` now builds the launcher camera ray and dispatches the
current launcher mode through `RuntimeTools::FireLauncherRay(...)`. Live input
and replay save-probe call sites still record replay launcher fire events at the
composition root before delegating to `RuntimeTools`, preserving replay event
ownership while deleting the old `Run` helper.

Deleted `Run.h` declarations:

- `FireRayCastTest`

Deleted `Run::` definitions:

- `Run::FireRayCastTest`

New owner methods:

- `RuntimeTools::TryBuildLauncherCameraRay(...)`
- `RuntimeTools::FireLauncherRay(...)`

Validation:

- Targeted build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\run_composition_launcher_dispatch_profile_build_rerun.log`;
  passed with 0 warnings and 0 errors.
- Rubber duck: reviewer Bernoulli found no blocking defect; noted that
  `FireLauncherLaser` and `FireLauncherProjectile` remain public and that
  launcher dispatch still uses the wide compatibility bridge to camera, model,
  world, and terrain services.
- Formatting check: `tools\validate_format.bat`, logged at
  `TestOutput\validation\run_composition_launcher_dispatch_validate_format_final.log`;
  passed after targeted formatting of `RuntimeTools`.
- Pre-commit gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\run_composition_launcher_dispatch_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv`.

Remaining launcher shrink work is now the debug-only launcher repro target and
snapshot helpers. Broader editor, replay, scene runtime, and render-host slices
remain active plan work.

## Launcher Repro Slice

The fourth launcher slice removed the debug-only launcher repro target picker and
snapshot writer from `Run`. `RuntimeTools` now owns the target picking and
snapshot text emission behind an explicit borrowed context; `RunInput` remains
the input/HUD adapter that translates the returned status into the existing
launcher-mode message. The `Run.h` private method ratchet was lowered to the
measured count so the deleted declarations cannot silently return.

Deleted `Run.h` declarations:

- `PickLauncherReproTarget`
- `WriteLauncherReproSnapshot`

Deleted `Run::` definitions:

- `Run::PickLauncherReproTarget`
- `Run::WriteLauncherReproSnapshot`

New owner methods:

- `RuntimeTools::PickLauncherReproTarget(...)`
- `RuntimeTools::WriteLauncherReproSnapshot(...)`

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\run_composition_launcher_repro_profile_build.log`;
  passed with 0 warnings and 0 errors.
- Targeted Debug build: `tools\validate_build.bat Debug`, logged at
  `TestOutput\validation\run_composition_launcher_repro_debug_build.log`;
  passed with 0 warnings and 0 errors and compiled the `_DEBUG` repro path.
- Runtime boundary check: `python tools\check_runtime_boundaries.py`; passed
  with 0 errors and wrote
  `TestOutput\validation\runtime_boundaries\summary.json`.
- Rubber duck: reviewer Leibniz first found the missing Debug-build evidence
  and a hardcoded HUD path drift risk. After the HUD path was tied back to
  `LAUNCHER_REPRO_SNAPSHOT_PATH`, Debug build evidence was added, and the
  ratchet was proven, Leibniz reported no blocking findings.
- Formatting check: `tools\validate_format.bat`, logged at
  `TestOutput\validation\run_composition_launcher_repro_validate_format.log`;
  passed after targeted formatting of the touched launcher/runtime files.
- Pre-commit gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\run_composition_launcher_repro_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv`.

The launcher extraction cluster is now complete for the methods called out by
this plan. Broader editor, replay, scene runtime, and render-host slices remain
active plan work.

## Editor Save Hotkeys Slice

The first editor slice moved F2/F3 scene snapshot and screenshot hotkey behavior
out of `Run`. `EditorTools` now owns the save-hotkey command handling through an
explicit borrowed context, while `RunInput` preserves the original ordering:
after UI keyboard blocking and before scene reset commands. The context exposes
the mutable services used by the command path instead of adding callbacks back
into `Run`.

Deleted `Run.h` declarations:

- `HandleEditorSaveHotkeys`

Deleted `Run::` definitions:

- `Run::HandleEditorSaveHotkeys`

New owner methods:

- `RunInternal::HandleEditorSaveHotkeys(...)`

Validation:

- Runtime boundary check: `python tools\check_runtime_boundaries.py`; passed
  with 0 errors and wrote
  `TestOutput\validation\runtime_boundaries\summary.json`.
- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\run_composition_editor_save_hotkeys_profile_build_rerun.log`;
  passed with 0 warnings and 0 errors.
- Rubber duck: reviewer Hilbert first noted that the `EditorTools` header still
  described all helpers as side-effect free and that the camera borrow was a
  raw pointer. After the header contract was updated and the context switched
  to `CameraCollection&`, Hilbert reported no blocking findings and did not
  consider direct F2/F3 key smoke a gate because the current interaction
  automation surface cannot press keyboard shortcuts.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\run_composition_editor_save_hotkeys_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds.
- Pre-commit gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\run_composition_editor_save_hotkeys_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv`.

Remaining editor shrink work includes placement preview, placement commit,
gizmo drag, editor UI commands, and editor overlay generation.

## Editor Placement Slice

The second editor slice moved terrain placement hit testing, placement preview
state, object-center calculation, and object placement commit behavior out of
`Run`. `Run` still builds the shared mouse world ray and records replay editor
placement events at the composition root, but live editor input, replay restore,
and replay save-probe paths now delegate the placement mechanics through
`RunInternal` editor-tool helpers and explicit borrowed contexts.

Validation exposed two infrastructure issues that were fixed in the same slice:
the replay v2 query/checker path did not understand solver snapshot version 2
tornado-system payloads, and checkpoint-plus-event restore could replay from a
stale `PhysicsBodyStore` pending impulse after serialized body state was
applied. Restore diagnostics now truncate long reason strings instead of
triggering debug CRT modals, and replay target probes quit cleanly after a
successful check.

Deleted `Run.h` declarations:

- `TryGetMouseTerrainPlacement` (both overloads)
- `TryComputeEditorObjectCenter`
- `TryComputeEditorPlacementPreview`
- `PlaceEditorObjectAtMouse`
- `PlaceEditorObjectAtTerrainPoint`

Deleted `Run::` definitions:

- `Run::TryGetMouseTerrainPlacement` (both overloads)
- `Run::TryComputeEditorObjectCenter`
- `Run::TryComputeEditorPlacementPreview`
- `Run::PlaceEditorObjectAtMouse`
- `Run::PlaceEditorObjectAtTerrainPoint`

New owner methods:

- `RunInternal::TryGetEditorTerrainPlacement(...)`
- `RunInternal::TryComputeEditorObjectCenter(...)`
- `RunInternal::TryUpdateEditorPlacementPreview(...)`
- `RunInternal::CanPlaceEditorObjectAtTerrainPoint(...)`
- `RunInternal::PlaceEditorObjectAtTerrainPoint(...)`

Supporting replay/physics/tooling fixes:

- `ReplayQuery` and the replay v2 checker accept solver snapshot versions 1 and
  2, including tornado-system snapshot data.
- `PhysicsEngine::ClearPendingBodyImpulses()` clears restored body-store pending
  impulses after replay sample state is applied.
- Replay restore probe reason writes use truncating copies, and target-file
  probes post quit after success.
- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method ratchet
  to 248.

Validation:

- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\run_composition_editor_placement_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds.
- Runtime boundary gate: `tools\validate_runtime_boundaries.bat`, logged at
  `TestOutput\validation\run_composition_editor_placement_validate_runtime_boundaries.log`;
  passed with 0 errors.
- Physics gate: `tools\validate_physics.bat`, logged at
  `TestOutput\validation\run_composition_editor_placement_validate_physics.log`;
  passed Debug build with 0 warnings/errors and byte-exact
  `physics_regression_solver.csv`.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\run_composition_editor_placement_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv`.
- Replay artifact gate: `tools\validate_replay_v2_artifact.bat`, logged at
  `TestOutput\validation\run_composition_editor_placement_validate_replay_v2_artifact.log`;
  passed save, target restore, generated-topology restore, replay query/export,
  and physics-query checks.
- Rubber duck: reviewer Herschel found no blocking defect; noted the harmless
  `Agentic/SessionState.md` line-ending warning, that
  `tornadoSystemVortexCount` is parsed but not surfaced in checkpoint JSON, and
  that a future live editor-placement smoke plus nonzero tornado-vortex fixture
  would reduce residual evidence risk.

Remaining editor shrink work includes gizmo drag, editor UI commands, and editor
overlay generation. Replay UI/tool behavior, scene runtime ownership, and
render-host splitting remain later plan slices.

## Editor Gizmo Slice

The third editor slice moved transform-gizmo mechanics out of `Run`.
`RunInternal` editor helpers now own gizmo drag capture helpers, axis/ring hit
testing, ray-to-axis/ring projection, selected-object translate/rotate/scale
mutation, and hot-axis preview updates through an explicit `EditorGizmoContext`.
`Run` still builds the shared mouse world ray, performs world-owner transitions,
routes final input, and records replay editor-transform events on drag release.

Deleted `Run.h` declarations:

- `BeginEditorGizmoDragGesture`
- `EndEditorGizmoDragGesture`
- `CancelEditorGizmoDragState`
- `HitEditorGizmoAxis`
- `HitEditorRotationGizmoAxis`
- `TryEditorAxisRayParameter`
- `TryEditorRotationRayAngle`
- `MoveSelectedEditorObjectAlongAxis`
- `RotateSelectedEditorObjectAroundAxis`
- `ScaleSelectedEditorObjectAlongAxis`

Deleted `Run::` definitions:

- `Run::BeginEditorGizmoDragGesture`
- `Run::EndEditorGizmoDragGesture`
- `Run::CancelEditorGizmoDragState`
- `Run::HitEditorGizmoAxis`
- `Run::HitEditorRotationGizmoAxis`
- `Run::TryEditorAxisRayParameter`
- `Run::TryEditorRotationRayAngle`
- `Run::MoveSelectedEditorObjectAlongAxis`
- `Run::RotateSelectedEditorObjectAroundAxis`
- `Run::ScaleSelectedEditorObjectAlongAxis`

New owner methods:

- `RunInternal::BeginEditorGizmoDragGesture(...)`
- `RunInternal::EndEditorGizmoDragGesture(...)`
- `RunInternal::CancelEditorGizmoDragState(...)`
- `RunInternal::HitEditorGizmoAxis(...)`
- `RunInternal::HitEditorRotationGizmoAxis(...)`
- `RunInternal::TryEditorAxisRayParameter(...)`
- `RunInternal::TryEditorRotationRayAngle(...)`
- `RunInternal::MoveSelectedEditorObjectAlongAxis(...)`
- `RunInternal::RotateSelectedEditorObjectAroundAxis(...)`
- `RunInternal::ScaleSelectedEditorObjectAlongAxis(...)`
- `RunInternal::UpdateEditorGizmoHotAxes(...)`

Boundary guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method ratchet
  to 238.
- The runtime-boundary rule now blocks future `Run.h` methods whose names carry
  `EditorGizmo` or `SelectedEditorObject`, so renamed gizmo mechanics cannot
  return under a nearby wrapper name.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\run_composition_editor_gizmo_profile_build.log`;
  passed with 0 warnings and 0 errors.
- Rubber duck: reviewer Archimedes found no blocking defect; after review, the
  boundary guard was broadened from exact old names to any `EditorGizmo` or
  `SelectedEditorObject` method declaration on `Run`.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\run_composition_editor_gizmo_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds.
- Runtime boundary gate: `tools\validate_runtime_boundaries.bat`, logged at
  `TestOutput\validation\run_composition_editor_gizmo_validate_runtime_boundaries.log`;
  passed with 0 errors.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\run_composition_editor_gizmo_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv`.
- Replay artifact gate: `tools\validate_replay_v2_artifact.bat`, logged at
  `TestOutput\validation\run_composition_editor_gizmo_validate_replay_v2_artifact.log`;
  passed save, target restore, generated-topology restore, replay query/export,
  and physics-query checks, including decoded editor-transform samples.
- Interaction click gate: `tools\validate_interaction_clicks.bat`, logged at
  `TestOutput\validation\run_composition_editor_gizmo_validate_interaction_clicks.log`;
  passed the existing live inspect-gizmo selection/visibility and replay
  prediction click reports.

Residual evidence risk: the existing automation proves gizmo selection and
visibility, but there is still no dedicated live translate/rotate/scale drag
smoke. The replay artifact gate covers encoded editor-transform samples, not
manual mouse feel.

## Editor UI/Mode Command Slice

The fourth editor slice moved editor UI/mode command mechanics out of `Run`.
`RunInternal` editor helpers now own unfocused editor reset, manipulation clear,
placement-mode state selection, editor keyboard shortcut capture, editor mode
state entry/exit, static-placement toggles, terrain-align toggles, and
object-type selection through explicit editor/gizmo contexts. `RunInput` still
applies final runtime side effects: interaction transitions, world-owner
selection, camera labels, fly-camera enter/exit/reset, cursor ownership, mouse
release, and runtime input action reporting.

Deleted `Run.h` declarations:

- `ResetEditorUnfocusedInputState`
- `ClearEditorManipulationState`
- `ToggleEditorPlacementMode`
- `HandleEditorKeyboardShortcuts`
- `ApplyEditorUICommands`

Deleted `Run::` definitions:

- `Run::ResetEditorUnfocusedInputState`
- `Run::ClearEditorManipulationState`
- `Run::ToggleEditorPlacementMode`
- `Run::HandleEditorKeyboardShortcuts`
- `Run::ApplyEditorUICommands`

New owner methods:

- `RunInternal::ResetEditorUnfocusedInputState(...)`
- `RunInternal::ClearEditorManipulationState(...)`
- `RunInternal::HandleEditorKeyboardShortcuts(...)`
- `RunInternal::SetEditorPlacementMode(...)`
- `RunInternal::ToggleEditorPlacementMode(...)`
- `RunInternal::EnterEditorModeState(...)`
- `RunInternal::ExitEditorModeState(...)`
- `RunInternal::SetEditorPlaceStaticObject(...)`
- `RunInternal::ToggleEditorPlaceStaticObject(...)`
- `RunInternal::ToggleEditorTerrainAlign(...)`
- `RunInternal::SelectEditorObjectType(...)`

Boundary guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method ratchet
  to 233.
- The runtime-boundary rule now blocks the exact editor UI/mode helper names
  from returning to `Run.h`.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\run_composition_editor_ui_profile_build.log`; passed
  with 0 warnings and 0 errors after adding the concrete `RuntimeTools` include
  for moved editor-state helpers.
- Runtime boundary check: `python tools\check_runtime_boundaries.py --repo .`;
  passed with 0 errors and wrote
  `TestOutput\validation\runtime_boundaries\summary.json`.
- Rubber duck: reviewer Confucius found no blocking behavior parity defect in
  editor enter/exit ordering, placement-mode toggles, object-type
  `enterPlacementMode` ordering, reset/clear parity, or the ratchet update.
  Non-blocking note: if editor command helpers keep expanding, consider
  separating them from placement/gizmo math into a narrower editor command
  helper header.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\run_composition_editor_ui_validate_fast.log`; passed
  formatting, project filters, runtime boundaries, Profile build, and Debug
  build in 87.20s.
- Runtime boundary gate: `tools\validate_runtime_boundaries.bat`, logged at
  `TestOutput\validation\run_composition_editor_ui_validate_runtime_boundaries.log`;
  passed with 0 errors in 0.51s.
- Interaction click gate: `tools\validate_interaction_clicks.bat`, logged at
  `TestOutput\validation\run_composition_editor_ui_validate_interaction_clicks.log`;
  passed the existing live inspect-gizmo and replay-prediction click reports in
  6.54s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\run_composition_editor_ui_validate_full.log`; passed
  project filters, runtime boundaries, Profile/Debug builds, DX12 validation
  with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 24.08s.

Remaining editor shrink work includes editor overlay generation. Replay UI/tool
behavior, scene runtime ownership, and render-host splitting remain later plan
slices.

## Editor Overlay/Preview Slice

The fifth editor slice moved editor interaction preview refresh and deterministic
tool-overlay trace construction out of `Run`. `Run` still builds shared mouse
world rays, clears invalid selections through the runtime interaction command
path, resolves attached-camera targets, appends replay overlays, renders the
shared tracer, and renders the launcher laser. The new overlay helper only
mutates editor preview/hot-axis state from explicit inputs and appends tool
geometry to the borrowed editor tracer.

Deleted `Run.h` declarations:

- `UpdateEditorInteractionPreview`
- `RenderEditorOverlay`

Deleted `Run::` definitions:

- `Run::UpdateEditorInteractionPreview`
- `Run::RenderEditorOverlay`

New owner methods:

- `RunInternal::UpdateEditorInteractionPreview(...)`
- `RunInternal::BuildEditorToolOverlayTrace(...)`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method ratchet
  to 231.
- The runtime-boundary rule now blocks editor overlay and interaction-preview
  helper declarations from returning to `Run.h`, including short future names
  such as `BuildEditorOverlay()` and `RefreshInteractionPreview()`.
- `tools/validate_project_filters.py` recognizes `EditorOverlayTools` under
  `Runtime\Editor`.
- `tools/validate_dx12_renderer.bat` now uses delayed expansion for its
  Profile-build log path so fail-fast DX12 build evidence prints a real path.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\profile_build_editor_overlay.log`; final rerun passed
  with 0 warnings and 0 errors in 6.69s after fixing a local-shadowing warning.
- Runtime boundary gate: `tools\validate_runtime_boundaries.bat`, logged at
  `TestOutput\validation\validate_runtime_boundaries_editor_overlay.log`;
  passed with 0 errors in 0.51s.
- Fast gate after tooling fixes: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\validate_fast_editor_overlay_final.log`; passed
  formatting, project filters, runtime boundaries, and Profile/Debug builds in
  13.17s.
- Interaction click gate: `tools\validate_interaction_clicks.bat`, logged at
  `TestOutput\validation\validate_interaction_clicks_editor_overlay.log`;
  passed the live inspect-gizmo and replay-prediction click reports in 7.10s.
- DX12 renderer gate after script fix: `tools\validate_dx12_renderer.bat`,
  logged at
  `TestOutput\validation\validate_dx12_renderer_editor_overlay_final.log`;
  passed formatting, Profile build, DX12 suite, DX12 validation errors 0,
  screenshot baseline comparison, and Profile/Debug ready builds in 16.64s.
- Rubber duck: reviewer Locke found no behavior blocker in the extraction and
  confirmed preview side effects and overlay render order stayed equivalent.
  Locke did block the first pass on dirty DX12 build-log evidence and an
  under-matching guardrail regex; both were fixed before the final fast and DX12
  gates.

Remaining editor shrink work is now outside the overlay/preview cluster. Replay
UI/tool behavior, scene runtime ownership, and render-host splitting remain
later plan slices.

## Replay Render-Query Owner Slice

This replay slice moved render-facing replay query ownership out of `Run`.
`ReplayRuntime` now owns loaded-presentation sampling, current presentation and
solver scrub sample selection, current prediction scrub-frame selection, and
replay focus-mask construction. `RuntimeRenderHost` now borrows one
`ReplayRuntime` instead of six replay sub-states and no longer callback-bounces
sample/focus-mask queries back into `Run`.

Deleted `Run.h` declarations:

- `BuildReplayFocusModelMask`
- `HasLoadedReplayPresentation`
- `LoadedReplayPresentationSampleAtNormalized`
- `LoadedReplayPresentationLatestSample`
- `IsReplayScrubPaused`
- `CurrentReplayScrubSample`
- `CurrentReplaySolverScrubSample`
- `CurrentReplayPredictionScrubFrame`

Deleted `Run::` definitions:

- `Run::BuildReplayFocusModelMask`
- `Run::HasLoadedReplayPresentation`
- `Run::LoadedReplayPresentationSampleAtNormalized`
- `Run::LoadedReplayPresentationLatestSample`
- `Run::IsReplayScrubPaused`
- `Run::CurrentReplayScrubSample`
- `Run::CurrentReplaySolverScrubSample`
- `Run::CurrentReplayPredictionScrubFrame`

New owner methods:

- `ReplayRuntime::HasLoadedPresentation()`
- `ReplayRuntime::LoadedPresentationSampleAtNormalized(...)`
- `ReplayRuntime::LoadedPresentationLatestSample()`
- `ReplayRuntime::IsScrubPaused()`
- `ReplayRuntime::CurrentScrubSample()`
- `ReplayRuntime::CurrentSolverScrubSample()`
- `ReplayRuntime::CurrentPredictionScrubFrame()`
- `ReplayRuntime::BuildFocusModelMask(...)`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` blocks the old replay render-query helper
  names from returning to `Run.h`.
- The `RuntimeRenderHost` allowlists now reject the old replay sub-state
  binding fields and the old sample/focus callback typedefs and fields.
- The `Run.h` private-method counter now counts pointer/ref return declarations.
  Because this broadens the metric, the new measured ceiling is 235 rather than
  a directly comparable decrement from the previous 231.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\replay_render_query_profile_build.log`; final rerun
  passed with 0 warnings and 0 errors in 1.21s.
- Runtime boundary check: `python tools\check_runtime_boundaries.py --repo .`;
  passed with 0 errors after the pointer/ref-return counter fix.
- Rubber duck: reviewer Copernicus first blocked on the private-method ratchet
  missing pointer-return declarations. After the regex and synthetic self-test
  fix, Copernicus confirmed the blocker was resolved and found no new blocker.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\replay_render_query_validate_fast.log`; passed
  formatting, project filters, runtime boundaries, and Profile/Debug builds in
  52.58s.
- Runtime boundary gate: `tools\validate_runtime_boundaries.bat`, logged at
  `TestOutput\validation\replay_render_query_validate_runtime_boundaries.log`;
  passed with 0 errors in 0.47s.
- Replay artifact gate: `tools\validate_replay_v2_artifact.bat`, logged at
  `TestOutput\validation\replay_render_query_validate_replay_v2_artifact.log`;
  passed save, target restore, generated-topology restore, replay query/export,
  and physics-query checks in 22.97s.
- Interaction click gate: `tools\validate_interaction_clicks.bat`, logged at
  `TestOutput\validation\replay_render_query_validate_interaction_clicks.log`;
  passed the existing live inspect-gizmo and replay-prediction click reports in
  6.93s.
- DX12 renderer gate: `tools\validate_dx12_renderer.bat`, logged at
  `TestOutput\validation\replay_render_query_validate_dx12_renderer.log`;
  passed formatting, Profile build reuse, DX12 suite, DX12 validation errors 0,
  screenshot baseline comparison, and Profile/Debug ready builds in 16.46s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\replay_render_query_validate_full.log`; passed project
  filters, runtime boundaries, Profile/Debug builds, DX12 validation with 0
  errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 23.01s.

The residual scrubber timeline duplication from this slice is resolved by the
following replay scrubber timeline owner slice.

## Diagnostics Perf Memory Wrapper Slice

This diagnostics slice removed the perf-memory checkpoint wrapper from `Run`.
Periodic and scene-start memory checkpoints now call `DiagnosticsRuntime`
directly, while the scene-load perf-log end checkpoint, forced pending flush,
and close behavior moved behind diagnostics APIs.

Deleted `Run.h` declarations:

- `LogPerfMemory`

Deleted `Run::` definitions:

- `Run::LogPerfMemory`

New owner methods:

- `RuntimeDiagnostics::ClosePerfLogWithMemoryCheckpoint(...)`
- `DiagnosticsController::ClosePerfLogWithMemoryCheckpoint(...)`
- `DiagnosticsRuntime::ClosePerfLogWithMemoryCheckpoint(...)`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet to 223.
- The boundary checker rejects `Run::LogPerfMemory` declarations and
  definitions, with synthetic header/source self-tests.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\perf_memory_wrapper_profile_build.log`;
  passed with 0 warnings and 0 errors in 43.75s.
- Rubber duck: reviewer Averroes found no blocking source defect or ordering
  drift. Non-blocking residual risks are the exact-name source guardrail and
  the still-live `RunScene` perf-log open/raw `PerfLog()` setup path.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\perf_memory_wrapper_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 50.74s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\perf_memory_wrapper_runtime_boundaries.log`;
  passed with 0 errors in 0.70s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\perf_memory_wrapper_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 25.43s.

## Scene-Control Wrapper Slice

This scene-runtime slice removed the thin scene-control wrappers from `Run`.
Scene browser selection, demo-scene loading, adjacent cinematic/scene cycling,
user reset, runtime-command advance, screenshot automation advance, and
exit-on-complete advance call sites now call `SceneRuntimeCoordinator`
directly. The old wrappers only forwarded arguments, so this slice deletes the
composition-root surface while preserving the existing scene coordinator
ownership.

Deleted `Run.h` declarations:

- `LoadSceneFromBrowserIndex`
- `LoadDemoSceneFromUI`
- `ApplyAdjacentCinematicMode`
- `LoadAdjacentSceneFromBrowser`
- `ResetCurrentScene`
- `AdvanceScene`

Deleted `Run::` definitions:

- `Run::LoadSceneFromBrowserIndex`
- `Run::LoadDemoSceneFromUI`
- `Run::ApplyAdjacentCinematicMode`
- `Run::LoadAdjacentSceneFromBrowser`
- `Run::ResetCurrentScene`
- `Run::AdvanceScene`

New direct owner calls:

- `SceneRuntimeCoordinator::LoadSceneFromBrowserIndex(...)`
- `SceneRuntimeCoordinator::LoadDemoSceneFromUI()`
- `SceneRuntimeCoordinator::ApplyAdjacentCinematicMode(...)`
- `SceneRuntimeCoordinator::LoadAdjacentSceneFromBrowser(...)`
- `SceneRuntimeCoordinator::ResetCurrentScene(...)`
- `SceneRuntimeCoordinator::AdvanceScene(...)`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 223 to 217.
- The boundary checker rejects the removed scene-control wrapper declarations
  in `Run.h`.
- The boundary checker rejects `Run::LoadSceneFromBrowserIndex`,
  `Run::LoadDemoSceneFromUI`, `Run::ApplyAdjacentCinematicMode`,
  `Run::LoadAdjacentSceneFromBrowser`, `Run::ResetCurrentScene`, and
  `Run::AdvanceScene` source definitions from returning.
- Synthetic header/source self-tests cover the old scene-control surface.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\scene_control_wrapper_profile_build.log`;
  passed with 0 warnings and 0 errors in 41.73s.
- Rubber duck: reviewer Chandrasekhar found no blocking defect. Reset ordering
  still enters interactive scene mode before forwarding preserve flags, and
  every `AdvanceScene` call still passes perf-test state, `sPerfPass`, and the
  current interactive-run preserve flag. The only residual risk is exact-name
  guardrails; renamed wrappers rely on the private-method ratchet and review.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\scene_control_wrapper_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in about 46.2s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\scene_control_wrapper_runtime_boundaries.log`;
  passed with 0 errors in 0.79s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\scene_control_wrapper_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 25.08s.

## Scene Coordinator Intent Slice

This scene-runtime slice removed the callback bounce from
`SceneRuntimeCoordinator`. The coordinator now owns only scene selection
decisions and returns explicit `SceneRuntimeControlAction` values. Existing
`Run` call sites execute those returned intents locally, so the coordinator no
longer stores function pointers back into the composition root.

Deleted `Run.h` declarations:

- `BuildSceneRuntimeCoordinatorCallbacks`

Deleted `Run::` definitions:

- `Run::BuildSceneRuntimeCoordinatorCallbacks`

Removed callback surface:

- `SceneRuntimeCoordinatorCallbacks`
- `SceneRuntimeCoordinatorCallbacks::enterInteractiveSceneRun`
- `SceneRuntimeCoordinatorCallbacks::clearCurrentSceneAutomation`
- `SceneRuntimeCoordinatorCallbacks::loadScene`
- `SceneRuntimeCoordinatorCallbacks::currentSceneBrowserIndex`
- `SceneRuntimeCoordinatorCallbacks::isCinematicTabActive`
- `SceneRuntimeCoordinatorCallbacks::applyCinematicModeFromBrowserIndex`
- `SceneRuntimeCoordinator::m_callbacks`

New owner shape:

- `SceneRuntimeCoordinator` constructs with only `SceneController&`.
- `SceneRuntimeControlAction` carries `None`,
  `ClearCurrentSceneAutomation`, `LoadScene`, and
  `ApplyCinematicModeFromBrowserIndex` intents.
- Scene browser, demo scene, adjacent scene/cinematic, reset, and advance
  decisions return control actions instead of invoking callbacks.
- `RunInput`, `RunFrame`, and `RunScene` execute those actions locally at the
  existing call sites without adding a new `Run` helper.

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 217 to 216.
- The boundary checker rejects `BuildSceneRuntimeCoordinatorCallbacks`
  declarations in `Run.h`.
- The boundary checker rejects `Run::BuildSceneRuntimeCoordinatorCallbacks`
  source definitions.
- The boundary checker rejects `SceneRuntimeCoordinatorCallbacks` and
  `m_callbacks` in `SceneRuntimeCoordinator.*`.
- Synthetic self-tests cover the removed `Run` builder and callback state.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\scene_coordinator_intent_profile_build.log`;
  passed with 0 warnings and 0 errors in 42.43s.
- Rubber duck: reviewer Popper found no blocking defect. Scene browser/demo
  loads still enter interactive mode before `LoadScene`, reset still forwards
  preserve flags unchanged after entering interactive mode, adjacent cinematic
  fallback still maps a no-action result to browser scene cycling, and
  `AdvanceScene` no-action still maps to the old no-next false path. Residual
  non-blocking risks are exact-name callback guardrails and duplicated local
  action executors.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\scene_coordinator_intent_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in about 45.3s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\scene_coordinator_intent_runtime_boundaries.log`;
  passed with 0 errors in 0.82s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\scene_coordinator_intent_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 25.31s.

## Diagnostics Perf-Log Lifecycle Slice

This diagnostics slice removed the perf-log tick wrapper from `Run`. `RunFrame`
now calls `DiagnosticsRuntime::TickPerfLog(...)` directly with the same pass,
frame, physics-time, and render-time context. Periodic memory checkpointing moved
inside `RuntimeDiagnostics::TickPerfLog(...)`, and scene-load perf-log
reset/config/open behavior moved behind diagnostics APIs instead of direct
`RunScene` mutation of perf-log lifecycle fields.

Deleted `Run.h` declarations:

- `TickPerfLog`

Deleted `Run::` definitions:

- `Run::TickPerfLog`

New owner methods:

- `RuntimeDiagnostics::ResetPerfLogForSceneLoad(...)`
- `RuntimeDiagnostics::ConfigurePerfLogFlush(...)`
- `RuntimeDiagnostics::OpenScenePerfLog(...)`
- `RuntimeDiagnostics::PerfTestActive(...)`
- `DiagnosticsController::ResetPerfLogForSceneLoad()`
- `DiagnosticsController::ConfigurePerfLogFlush(...)`
- `DiagnosticsController::OpenScenePerfLog(...)`
- `DiagnosticsController::PerfTestActive()`
- `DiagnosticsRuntime::ResetPerfLogForSceneLoad()`
- `DiagnosticsRuntime::ConfigurePerfLogFlush(...)`
- `DiagnosticsRuntime::OpenScenePerfLog(...)`
- `DiagnosticsRuntime::PerfTestActive()`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 216 to 215.
- The boundary checker rejects `TickPerfLog` declarations in `Run.h`.
- The boundary checker rejects `Run::TickPerfLog` source definitions.
- The boundary checker rejects direct `RunScene` access to perf-log lifecycle
  fields and `fopen_s(...)` calls tied to `m_diagnosticsRuntime.PerfLog()`.
- Synthetic self-tests cover the removed wrapper, direct perf-log lifecycle
  access, and the allowed unrelated `fopen_s(...)` case.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\perf_log_lifecycle_profile_build.log`;
  passed with 0 warnings and 0 errors in 43.85s.
- Rubber duck: reviewer Einstein found no blocking defect. The only
  non-blocking finding was that the first `RunScene` guardrail rejected every
  `fopen_s(...)`; it was narrowed to perf-log lifecycle access before final
  validation.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\perf_log_lifecycle_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in about 47.7s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\perf_log_lifecycle_runtime_boundaries.log`;
  passed with 0 errors in 0.89s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\perf_log_lifecycle_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 24.84s.

## Replay Scrubber Timeline Owner Slice

This replay slice moved scrubber timeline math and track-position mutation out
of `RunInternal.h` and into `ReplayRuntime`. `Run` still owns the current
scrubber input dispatcher, but it now mutates replay scrubber track positions
through the replay owner. `RuntimeRenderHost` no longer callback-bounces
scrubber visibility into `Run`; it asks `ReplayRuntime` directly using the
borrowed editor/UI state it already exposes to render passes.

Deleted `Run.h` declarations:

- `ShouldRenderReplayScrubber`

Deleted `Run::` definitions:

- `Run::ShouldRenderReplayScrubber`

Removed `RunInternal.h` helpers:

- `ReplayScrubberRetainedPastSeconds`
- `ReplayPredictionAvailableFutureSeconds`
- `ReplayScrubberPresentTrackPosition`
- `ReplayScrubberTimelineHasFuture`
- `ReplayScrubberAtPresentTrackPosition`
- `ReplayScrubberTrackPositionIsFuture`
- `ReplayScrubberSolverNormalizedFromTrack`
- `ReplayScrubberPredictionNormalizedFromTrack`
- `ReplayScrubberTrackPosition`
- `ReplayScrubberSetTrackPosition`
- `ReplayScrubberSyncActivePosition`
- `ReplayScrubberSetAllTrackPositions`

New owner methods:

- `ReplayRuntime::TrackPosition(...)`
- `ReplayRuntime::SetTrackPosition(...)`
- `ReplayRuntime::SyncActiveTrackPosition()`
- `ReplayRuntime::SetAllTrackPositions(...)`
- `ReplayRuntime::SolverPresentTrackPosition()`
- `ReplayRuntime::TimelineHasFuture(...)`
- `ReplayRuntime::AtPresentTrackPosition(...)`
- `ReplayRuntime::TrackPositionIsFuture(...)`
- `ReplayRuntime::SolverNormalizedFromTrack(...)`
- `ReplayRuntime::PredictionNormalizedFromTrack(...)`
- `ReplayRuntime::ShouldRenderScrubber(...)`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet to 234.
- The `Run.h` replay render-query rule now also blocks
  `ShouldRenderReplayScrubber` from returning to `Run`.
- The `RuntimeRenderHostCallbacks` allowlist rejects the removed
  `shouldRenderReplayScrubber` callback field.
- The boundary checker rejects reintroduced scrubber timeline/position helper
  definitions in `RunInternal.h`, including `static inline` variants.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\replay_scrubber_owner_profile_build.log`; final rerun
  passed with 0 warnings and 0 errors in 40.89s.
- Runtime boundary check: `python tools\check_runtime_boundaries.py --repo .`;
  passed with 0 errors after the `RunInternal.h` scrubber helper guard and
  synthetic self-test were tightened.
- Rubber duck: reviewer Arendt found no blocking defect. The non-blocking
  guardrail regex concern was fixed by matching `static inline` helper
  reintroductions before final validation.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\replay_scrubber_owner_validate_fast.log`; passed
  formatting, project filters, runtime boundaries, and Profile/Debug builds in
  51.03s.
- Runtime boundary gate: `tools\validate_runtime_boundaries.bat`, logged at
  `TestOutput\validation\replay_scrubber_owner_validate_runtime_boundaries.log`;
  passed with 0 errors in 0.52s.
- Replay artifact gate: `tools\validate_replay_v2_artifact.bat`, logged at
  `TestOutput\validation\replay_scrubber_owner_validate_replay_v2_artifact.log`;
  passed save, load, restore, generated-topology restore, replay query/export,
  and physics-query checks in 22.75s.
- Interaction click gate: `tools\validate_interaction_clicks.bat`, logged at
  `TestOutput\validation\replay_scrubber_owner_validate_interaction_clicks.log`;
  passed the existing live inspect-gizmo and replay-prediction click reports in
  6.45s.
- DX12 renderer gate: `tools\validate_dx12_renderer.bat`, logged at
  `TestOutput\validation\replay_scrubber_owner_validate_dx12_renderer.log`;
  passed formatting, Profile build, DX12 suite, DX12 validation errors 0,
  screenshot baseline comparison, and Profile/Debug ready builds in 16.74s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\replay_scrubber_owner_validate_full.log`; passed
  project filters, runtime boundaries, Profile/Debug builds, DX12 validation
  with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 23.07s.

Residual architecture risk: `TickReplayScrubberInput()` and scrubber overlay
drawing still live on `Run`. This slice deliberately did not move those
dispatchers; later replay UI/tool slices should move them behind replay-owned
input/overlay services without adding callbacks back into `Run`.

## Replay Prediction Ghost Host Slice

This render-host slice removed the replay prediction ghost draw callback from
`Run`. `RuntimeRenderHost` still exposes the same
`RenderReplayPredictionGhosts(...)` frame hook to `RuntimeRenderer`, but the
implementation now lives in `RuntimeRenderHost.cpp` and draws directly from the
host's borrowed `ReplayRuntime` and `GameModelCollection` services. The old
callback typedef/field and `Run` wrapper are gone, so prediction ghost rendering
no longer bounces from the renderer bridge back into the composition root.

Deleted `Run.h` declarations:

- `RenderReplayPredictionGhosts`

Deleted `Run::` definitions:

- `Run::RenderReplayPredictionGhosts`

Removed render-host callback surface:

- `RuntimeRenderHostCallbacks::ReplayPredictionGhostsFn`
- `RuntimeRenderHostCallbacks::renderReplayPredictionGhosts`
- `Run::BuildRuntimeRenderHostCallbacks()` assignment for
  `renderReplayPredictionGhosts`

New owner implementation:

- `RuntimeRenderHost::RenderReplayPredictionGhosts(...)`, implemented
  out-of-line in `Runtime/Render/RuntimeRenderHost.cpp`, builds prediction ghost
  draw requests through `ReplayRuntime`, reads model collision/material data
  from `GameModelCollection`, selects `TEXTURE_BOUNDING_SPHERE`, and emits the
  same transparent box batch as before.

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method ratchet
  to 233.
- The `Run.h` replay render-query rule now also blocks
  `RenderReplayPredictionGhosts` from returning to `Run`.
- The `RuntimeRenderHostCallbacks` allowlists reject the removed
  `ReplayPredictionGhostsFn` typedef and `renderReplayPredictionGhosts` field.
- Synthetic self-tests cover the old `Run` helper, callback typedef, and
  callback field.
- `SKULLBONEZ_CORE.vcxproj.filters` now declares `Source Files\Runtime\Render`
  so new render-owner source files match the existing render header filter.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\replay_prediction_ghost_host_profile_build.log`;
  final rerun passed with 0 warnings and 0 errors in 40.28s.
- Rubber duck: reviewer Aquinas found no blocking defect. Aquinas noted the
  first inline implementation widened `RuntimeRenderHost.h`; this was fixed by
  moving the draw body into `RuntimeRenderHost.cpp` before final validation.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\replay_prediction_ghost_host_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 48.25s.
- Runtime boundary gate: `tools\validate_runtime_boundaries.bat`, logged at
  `TestOutput\validation\replay_prediction_ghost_host_validate_runtime_boundaries.log`;
  passed with 0 errors in 0.51s.
- Replay artifact gate: `tools\validate_replay_v2_artifact.bat`, logged at
  `TestOutput\validation\replay_prediction_ghost_host_validate_replay_v2_artifact.log`;
  passed save, load, restore, generated-topology restore, replay query/export,
  and physics-query checks in 22.63s.
- DX12 renderer gate: `tools\validate_dx12_renderer.bat`, logged at
  `TestOutput\validation\replay_prediction_ghost_host_validate_dx12_renderer.log`;
  passed formatting, Profile build, DX12 suite, DX12 validation errors 0, and
  screenshot baseline comparison in 17.39s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\replay_prediction_ghost_host_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 23.09s.

Residual architecture risk: `RuntimeRenderHost` is still broad and still has
callback debt for editor/replay overlay presentation. Replay prediction job
generation, scrubber input, cause tree rows, velocity editing, and replay
overlay construction still live on `Run`.

## Replay Prediction Job-State Owner Slice

This replay slice moved replay prediction job/cache mutation out of `Run`.
`ReplayRuntime` now owns prediction future-node cache clearing, job cancelation,
cache invalidation, dirty marking, and path-visualizer state clearing. `Run`
still owns the live input/render dispatchers that need camera, mouse-ray, model,
physics, and tracer services, but those paths now ask `ReplayRuntime` to mutate
replay prediction state instead of routing through private `Run` wrappers.

Deleted `Run.h` declarations:

- `MarkReplayPredictionDirty`
- `ClearReplayPredictionCache`
- `CancelReplayPredictionJob`

Deleted `Run::` definitions:

- `Run::MarkReplayPredictionDirty`
- `Run::ClearReplayPredictionCache`
- `Run::CancelReplayPredictionJob`

New owner methods:

- `ReplayRuntime::ClearPredictionFutureNodeCache()`
- `ReplayRuntime::CancelPredictionJob(...)`
- `ReplayRuntime::ClearPredictionCache()`
- `ReplayRuntime::MarkPredictionDirty()`
- `ReplayRuntime::ClearPathVisualizerState()`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 215 to 212.
- The `Run.h` rules reject the removed prediction job-state helpers.
- Runtime source guardrails reject `Run::MarkReplayPredictionDirty`,
  `Run::ClearReplayPredictionCache`, and `Run::CancelReplayPredictionJob`
  definitions from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_prediction_job_state_profile_build.log`;
  passed with 0 warnings and 0 errors in 47.13s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_prediction_job_state_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 53.11s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_prediction_job_state_runtime_boundaries.log`;
  passed with 0 errors in 0.91s.
- Replay artifact gate: `tools\validate_replay_v2_artifact.bat`, logged at
  `TestOutput\validation\agent_logs\replay_prediction_job_state_validate_replay_v2_artifact.log`;
  passed save, load, restore, generated-topology restore, replay query/export,
  and physics-query checks in 22.49s. SkullScope/query accounting from the gate:
  primary runtime trace 83,296 bytes, replay query trace 2,465 bytes, restore
  failure trace 1,580 bytes, SQLite caches 204,800 bytes each, restore failure
  query output 1,030 bytes, replay query output total 18,493 bytes, generated
  topology runtime trace 189,763 bytes.
- Interaction click gate: `tools\validate_interaction_clicks.bat`, logged at
  `TestOutput\validation\agent_logs\replay_prediction_job_state_validate_interaction_clicks.log`;
  passed inspect-gizmo and replay-prediction click reports in 6.56s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_prediction_job_state_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 23.16s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: replay prediction frame generation, visualizer
drawing, scrubber input, cause tree rows, velocity editing, and replay overlay
construction still live on `Run`.

## Replay Path-State Wrapper Removal Slice

This replay slice deleted the remaining thin `Run` wrapper that only cleared the
replay camera focus and replay path visualizer state. Callers now perform the
focus clear at the call site and ask `ReplayRuntime` to clear the visualizer
state directly, keeping path-state mutation owned by the replay runtime instead
of by a private `Run` helper.

Deleted `Run.h` declaration:

- `ClearReplayPathVisualizer`

Deleted `Run::` definition:

- `Run::ClearReplayPathVisualizer`

Updated direct owner flow:

- `ResetReplayTimelineForActiveScene`, `ArmLoadedReplayPresentationScrubber`,
  `ClearReplayInteractionForRuntimeTransition`, `TickReplayCauseTreeInput`, and
  `TryPickReplayPathTargetFromMouse` now call `ClearReplayCameraFocus( true )`
  followed by `m_replayRuntime.ClearPathVisualizerState()`.

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 212 to 211.
- The `Run.h` rules reject `ClearReplayPathVisualizer` from returning.
- Runtime source guardrails reject `Run::ClearReplayPathVisualizer` definitions
  from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_path_state_wrapper_profile_build.log`;
  passed with 0 warnings and 0 errors in 41.97s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_path_state_wrapper_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 49.63s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_path_state_wrapper_runtime_boundaries.log`;
  passed with 0 errors in 0.97s.
- Replay artifact gate: `tools\validate_replay_v2_artifact.bat`, logged at
  `TestOutput\validation\agent_logs\replay_path_state_wrapper_validate_replay_v2_artifact.log`;
  passed save, load, restore, generated-topology restore, replay query/export,
  and physics-query checks in 23.21s. SkullScope/query accounting from the gate:
  primary runtime trace 83,296 bytes, replay query trace 2,465 bytes, restore
  failure trace 1,580 bytes, SQLite caches 204,800 bytes each, restore failure
  query output 1,030 bytes, replay query output total 18,493 bytes, generated
  topology runtime trace 189,763 bytes.
- SkullScope trace commands used by the replay artifact gate:
  `C:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/replay_v2_solver_one.scene.json --frames 120 --replay on --replay-seconds 1 --replay-save-probe C:\SkullbonezCore\TestOutput\validation\replay_v2\replay_save_probe.skreplay --physics-diag C:\SkullbonezCore\TestOutput\validation\replay_v2\replay_save_probe_runtime.physicsdiag.ndjson`;
  `C:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/replay_v2_solver_one.scene.json --replay-restore-failure-file-probe C:\SkullbonezCore\TestOutput\validation\replay_v2\replay_save_probe.skreplay --physics-diag C:\SkullbonezCore\TestOutput\validation\replay_v2\replay_restore_failure.physicsdiag.ndjson`;
  `C:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/replay_v2_generated_topology.scene.json --frames 120 --replay on --replay-seconds 1 --replay-save-probe C:\SkullbonezCore\TestOutput\validation\replay_v2\replay_generated_topology_probe.skreplay --physics-diag C:\SkullbonezCore\TestOutput\validation\replay_v2\replay_generated_topology_runtime.physicsdiag.ndjson`;
  `tools\replay_query.bat TestOutput\validation\replay_v2\replay_save_probe.skreplay export-skullscope --frames 0:5 --out TestOutput\validation\replay_v2\replay_save_probe.physicsdiag.ndjson --run-id replay_v2_artifact`.
- SkullScope query commands used by the replay artifact gate:
  `tools\physics_query.bat TestOutput\validation\replay_v2\replay_restore_failure.physicsdiag.ndjson restore --limit 4`;
  `tools\physics_query.bat TestOutput\validation\replay_v2\replay_save_probe.physicsdiag.ndjson summary`.
- Interaction click gate: `tools\validate_interaction_clicks.bat`, logged at
  `TestOutput\validation\agent_logs\replay_path_state_wrapper_validate_interaction_clicks.log`;
  passed inspect-gizmo and replay-prediction click reports in 6.86s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_path_state_wrapper_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 23.35s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: replay prediction frame generation, visualizer
drawing, scrubber input, cause tree rows, velocity editing, replay overlay
construction, and the larger scene load/render-host splits still remain.

## Replay Cause-Tree Body Lookup Owner Slice

This replay slice moved the cause-tree body position/radius resolver out of
`Run`. `ReplayRuntime` now owns the lookup policy across active prediction
frames, retained solver scrub samples, and the live model fallback. `Run` still
owns camera activation because that code reaches camera collections, input
cursor state, and replay-inspection camera transitions.

Deleted `Run.h` declaration:

- `TryResolveReplayCauseTreeBodyPosition`

Deleted `Run::` definition:

- `Run::TryResolveReplayCauseTreeBodyPosition`

New owner method:

- `ReplayRuntime::ResolveCauseTreeBodyPosition(...)`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 211 to 210.
- The `Run.h` rules reject `TryResolveReplayCauseTreeBodyPosition` from
  returning.
- Runtime source guardrails reject `Run::TryResolveReplayCauseTreeBodyPosition`
  definitions from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`,
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.h`,
  `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and `tools/check_runtime_boundaries.py`.
- `ReplayRuntime.cpp` and `ReplayRuntime.h` gained cause-tree glossary wording
  because this slice moved cause-tree lookup behavior into that owner.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_cause_tree_lookup_profile_build.log`;
  passed with 0 warnings and 0 errors in 44.67s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_cause_tree_lookup_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 93.88s. An initial pre-format fast run stopped on
  `RunReplayTools.cpp`; the final gate above passed after formatting only that
  touched file.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_cause_tree_lookup_runtime_boundaries.log`;
  passed with 0 errors in 1.02s.
- Replay artifact gate: `tools\validate_replay_v2_artifact.bat`, logged at
  `TestOutput\validation\agent_logs\replay_cause_tree_lookup_validate_replay_v2_artifact.log`;
  passed save, load, restore, generated-topology restore, replay query/export,
  and physics-query checks in 22.87s. SkullScope/query accounting from the gate:
  primary runtime trace 83,296 bytes, replay query trace 2,465 bytes, restore
  failure trace 1,580 bytes, SQLite caches 204,800 bytes each, restore failure
  query output 1,030 bytes, replay query output total 18,493 bytes, generated
  topology runtime trace 189,763 bytes.
- SkullScope trace commands used by the replay artifact gate:
  `C:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/replay_v2_solver_one.scene.json --frames 120 --replay on --replay-seconds 1 --replay-save-probe C:\SkullbonezCore\TestOutput\validation\replay_v2\replay_save_probe.skreplay --physics-diag C:\SkullbonezCore\TestOutput\validation\replay_v2\replay_save_probe_runtime.physicsdiag.ndjson`;
  `C:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/replay_v2_solver_one.scene.json --replay-restore-failure-file-probe C:\SkullbonezCore\TestOutput\validation\replay_v2\replay_save_probe.skreplay --physics-diag C:\SkullbonezCore\TestOutput\validation\replay_v2\replay_restore_failure.physicsdiag.ndjson`;
  `C:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/replay_v2_generated_topology.scene.json --frames 120 --replay on --replay-seconds 1 --replay-save-probe C:\SkullbonezCore\TestOutput\validation\replay_v2\replay_generated_topology_probe.skreplay --physics-diag C:\SkullbonezCore\TestOutput\validation\replay_v2\replay_generated_topology_runtime.physicsdiag.ndjson`;
  `tools\replay_query.bat TestOutput\validation\replay_v2\replay_save_probe.skreplay export-skullscope --frames 0:5 --out TestOutput\validation\replay_v2\replay_save_probe.physicsdiag.ndjson --run-id replay_v2_artifact`.
- SkullScope query commands used by the replay artifact gate:
  `tools\physics_query.bat TestOutput\validation\replay_v2\replay_restore_failure.physicsdiag.ndjson restore --limit 4`;
  `tools\physics_query.bat TestOutput\validation\replay_v2\replay_save_probe.physicsdiag.ndjson summary`.
- Interaction click gate: `tools\validate_interaction_clicks.bat`, logged at
  `TestOutput\validation\agent_logs\replay_cause_tree_lookup_validate_interaction_clicks.log`;
  passed inspect-gizmo and replay-prediction click reports in 6.48s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_cause_tree_lookup_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 23.56s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: camera focus activation, replay prediction frame
generation, visualizer drawing, scrubber input, cause tree row UI, velocity
editing, replay overlay construction, and the larger scene load/render-host
splits still remain.

## Replay Cause-Tree Dead Focus Wrapper Slice

This replay slice deleted the unused private `Run` helper that wrapped a body
id into a cause-tree row and forwarded to `ActivateReplayCameraForCauseRow`.
There were no live callers, so the behavior surface is unchanged; cause-tree
row activation remains explicit at the existing call sites.

Deleted `Run.h` declaration:

- `FocusReplayCauseTreeBody`

Deleted `Run::` definition:

- `Run::FocusReplayCauseTreeBody`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 210 to 209.
- The `Run.h` rules reject `FocusReplayCauseTreeBody` from returning.
- Runtime source guardrails reject `Run::FocusReplayCauseTreeBody` definitions
  from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and `tools/check_runtime_boundaries.py`.
- Existing learning headers were sufficient; this slice deleted a dead wrapper
  and added no dense new runtime behavior.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_cause_tree_focus_wrapper_profile_build.log`;
  passed with 0 warnings and 0 errors in 39.46s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_cause_tree_focus_wrapper_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 48.20s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_cause_tree_focus_wrapper_runtime_boundaries.log`;
  passed with 0 errors in 1.07s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_cause_tree_focus_wrapper_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 25.47s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: camera focus activation, replay prediction frame
generation, visualizer drawing, scrubber input, cause tree row UI, velocity
editing, replay overlay construction, and the larger scene load/render-host
splits still remain.

## Replay Velocity Target Owner Slice

This replay slice moved velocity-edit target model resolution out of `Run`.
`ReplayRuntime` now owns the lookup policy for the current path visualizer
target: use the cached model index only when its `ReplayBodyId` still matches,
then fall back to scanning the borrowed model list. The remaining `Run`
velocity-edit hit-testing and drawing code still borrows the resolved index
because it needs ray geometry, model transforms, and tracer output.

Deleted `Run.h` declaration:

- `ResolveReplayVelocityEditModelIndex`

Deleted `Run::` definition:

- `Run::ResolveReplayVelocityEditModelIndex`

New owner method:

- `ReplayRuntime::ResolveVelocityEditModelIndex(...)`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 209 to 208.
- The `Run.h` rules reject `ResolveReplayVelocityEditModelIndex` from
  returning.
- Runtime source guardrails reject `Run::ResolveReplayVelocityEditModelIndex`
  definitions from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`,
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.h`,
  `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and `tools/check_runtime_boundaries.py`.
- `ReplayRuntime.cpp` and `ReplayRuntime.h` gained velocity-edit glossary
  wording because this slice moved velocity target lookup into that owner.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_velocity_target_owner_profile_build.log`;
  passed with 0 warnings and 0 errors in 44.19s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_velocity_target_owner_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 51.63s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_velocity_target_owner_runtime_boundaries.log`;
  passed with 0 errors in 1.12s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_velocity_target_owner_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 25.76s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: velocity edit toggle/input/geometry/drawing,
camera focus activation, replay prediction frame generation, visualizer drawing,
scrubber input, cause tree row UI, replay overlay construction, and the larger
scene load/render-host splits still remain.

## Replay Velocity Hit Helper File-Local Slice

This replay slice removed the velocity-edit hit and ray-parameter helpers from
`Run`. The helpers are now file-local functions in `RunReplayTools.cpp` that
borrow `ReplayRuntime` and the current model list explicitly, while the public
`Run` replay input and drag methods remain the composition-root adapters for
window input, camera rays, and model mutation.

Deleted `Run.h` declarations:

- `HitReplayVelocityLinearAxis`
- `HitReplayVelocityAngularAxis`
- `TryReplayVelocityAxisRayParameter`
- `TryReplayVelocityAngularRayAngle`

Deleted `Run::` definitions:

- `Run::HitReplayVelocityLinearAxis`
- `Run::HitReplayVelocityAngularAxis`
- `Run::TryReplayVelocityAxisRayParameter`
- `Run::TryReplayVelocityAngularRayAngle`

New owner surface:

- File-local `HitReplayVelocityLinearAxis(...)`
- File-local `HitReplayVelocityAngularAxis(...)`
- File-local `TryReplayVelocityAxisRayParameter(...)`
- File-local `TryReplayVelocityAngularRayAngle(...)`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 208 to 204.
- The `Run.h` rules reject the replay velocity hit and ray-parameter helper
  names from returning.
- Runtime source guardrails reject `Run::HitReplayVelocity...` and
  `Run::TryReplayVelocity...` definitions from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and `tools/check_runtime_boundaries.py`.
- Existing surrounding comments remain appropriate for this file-local helper
  conversion; no new dense subsystem comment was required.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_velocity_hit_helpers_profile_build.log`;
  passed with 0 warnings and 0 errors in 39.41s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_velocity_hit_helpers_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 48.27s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_velocity_hit_helpers_runtime_boundaries.log`;
  passed with 0 errors in 1.17s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_velocity_hit_helpers_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 25.51s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: velocity edit toggle/input/drawing,
camera focus activation, replay prediction frame generation, visualizer drawing,
scrubber input, cause tree row UI, replay overlay construction, and the larger
scene load/render-host splits still remain.

## Replay Velocity Edit Toggle Ownership Slice

This replay slice removed the remaining `Run` wrapper for toggling velocity edit
mode. `ReplayRuntime::SetVelocityEditEnabled(...)` now owns the pure replay
state transition: toggle state, axis hover/active reset, prediction enablement,
horizon clamping, and prediction dirtying. The two `Run` call sites keep the
composition-root responsibilities that still require input/interaction context:
canceling active replay gestures, entering interactive scene run mode, holding
live replay advance, and switching the world interaction owner.

Deleted `Run.h` declaration:

- `SetReplayVelocityEditEnabled`

Deleted `Run::` definition:

- `Run::SetReplayVelocityEditEnabled`

New owner method:

- `ReplayRuntime::SetVelocityEditEnabled(...)`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 204 to 203.
- The `Run.h` rules reject `SetReplayVelocityEditEnabled` from returning.
- Runtime source guardrails reject `Run::SetReplayVelocityEditEnabled`
  definitions from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`,
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.h`,
  `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp`,
  `SkullbonezSource/Runtime/RunInput.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and `tools/check_runtime_boundaries.py`.
- Existing `ReplayRuntime` glossary wording already describes velocity edit
  ownership; no new dense subsystem comment was required.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Initial targeted Profile build failed on unqualified replay-overlay constants
  and a too-narrow `RunInput.cpp` timestamp scope; both were corrected before
  final validation.
- Targeted Profile build rerun: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_velocity_toggle_owner_profile_build_rerun.log`;
  passed with 0 warnings and 0 errors in 18.70s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_velocity_toggle_owner_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 51.92s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_velocity_toggle_owner_runtime_boundaries.log`;
  passed with 0 errors in 1.22s.
- Interaction smoke: `tools\validate_interaction_clicks.bat`, logged at
  `TestOutput\validation\agent_logs\replay_velocity_toggle_owner_validate_interaction_clicks.log`;
  passed the inspect-gizmo and replay-prediction click reports in 6.65s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_velocity_toggle_owner_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 24.88s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: velocity edit input/drag/drawing, camera focus
activation, replay prediction frame generation, visualizer drawing, scrubber
input, cause tree row UI, replay overlay construction, and the larger scene
load/render-host splits still remain.

## Replay Velocity Apply Helper File-Local Slice

This replay slice removed the velocity-edit model-apply helper from `Run`. The
helper is now a file-local function in `RunReplayTools.cpp` that explicitly
borrows `ReplayRuntime`, `GameModelCollection`, target model index, velocity
vectors, and the scrubber visibility deadline. The remaining `Run` drag method
still owns ray interpretation and interaction/gesture state, while the helper
keeps the velocity clamp, wake, physics-stream invalidation, prediction dirtying,
and scrubber visibility update together without requiring a `Run.h` declaration.

Deleted `Run.h` declaration:

- `ApplyReplayVelocityEditToModel`

Deleted `Run::` definition:

- `Run::ApplyReplayVelocityEditToModel`

New owner surface:

- File-local `ApplyReplayVelocityEditToModel(...)`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 203 to 202.
- The `Run.h` rules reject `ApplyReplayVelocityEditToModel` from returning.
- Runtime source guardrails reject `Run::ApplyReplayVelocityEditToModel`
  definitions from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and `tools/check_runtime_boundaries.py`.
- Existing surrounding comments remain appropriate for this file-local helper
  conversion; no new dense subsystem comment was required.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Initial targeted Profile builds failed while qualifying the borrowed model
  collection parameter; the helper was corrected to use the full
  `SkullbonezCore::GameObjects::GameModelCollection` type before final gates.
- Targeted Profile build final: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_velocity_apply_helper_profile_build_final.log`;
  passed with 0 warnings and 0 errors in 9.18s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_velocity_apply_helper_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 48.45s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_velocity_apply_helper_runtime_boundaries.log`;
  passed with 0 errors in 1.29s.
- Interaction smoke: `tools\validate_interaction_clicks.bat`, logged at
  `TestOutput\validation\agent_logs\replay_velocity_apply_helper_validate_interaction_clicks.log`;
  passed the inspect-gizmo and replay-prediction click reports in 6.65s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_velocity_apply_helper_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 25.02s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: velocity edit input/drag/drawing, camera focus
activation, replay prediction frame generation, visualizer drawing, scrubber
input, cause tree row UI, replay overlay construction, and the larger scene
load/render-host splits still remain.

## Scene Context Builder File-Local Slice

This scene-load slice removed the scene setup context builders from `Run.h`.
Generated and authored scene setup still receive the same camera, terrain,
scene state, config, world, model collection, physics engine, required-contact,
and object-type override dependencies, but the builder functions are now
anonymous-namespace helpers in the source files that need them instead of
private `Run` wrappers.

Deleted `Run.h` declarations:

- `BuildSceneAuthoredCameraContext`
- `BuildSceneAuthoredModelContext`
- `BuildSceneGeneratedCameraContext`
- `BuildSceneGeneratedModelContext`

Deleted `Run::` definitions:

- `Run::BuildSceneAuthoredCameraContext`
- `Run::BuildSceneAuthoredModelContext`
- `Run::BuildSceneGeneratedCameraContext`
- `Run::BuildSceneGeneratedModelContext`

New owner surface:

- File-local `BuildSceneAuthoredCameraContext(...)` and
  `BuildSceneAuthoredModelContext(...)` in `Runtime/Scene/RunScene.cpp`.
- File-local `BuildSceneGeneratedModelContext(...)` in
  `Runtime/Scene/RunScene.cpp` and `Runtime/RunFrame.cpp`.
- File-local `BuildSceneGeneratedCameraContext(...)` in
  `Runtime/Scene/RunScene.cpp` and `Runtime/RunRender.cpp`.

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 202 to 198.
- The `Run.h` rules reject `BuildSceneAuthoredCameraContext`,
  `BuildSceneAuthoredModelContext`, `BuildSceneGeneratedCameraContext`, and
  `BuildSceneGeneratedModelContext` declarations from returning.
- Runtime source guardrails reject `Run::BuildScene...Context` definitions from
  returning while still allowing file-local helpers with explicit borrowed
  dependencies.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Scene/RunScene.cpp`,
  `SkullbonezSource/Runtime/RunFrame.cpp`,
  `SkullbonezSource/Runtime/RunRender.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and
  `tools/check_runtime_boundaries.py`.
- The slice adds no new explanatory comments. Existing scene/replay/render
  comments remain focused on ownership and behavior, and the new helper shape
  is explicit in function signatures.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Initial `tools\validate_fast.bat` run stopped at formatting for
  `RunFrame.cpp` and `RunScene.cpp`; those touched files were formatted
  directly with VS `clang-format` before final gates.
- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\scene_context_builders_profile_build.log`;
  passed with 0 warnings and 0 errors in 39.78s.
- Fast gate rerun: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\scene_context_builders_validate_fast_rerun.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 55.08s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\scene_context_builders_runtime_boundaries.log`;
  passed with 0 errors in 1.32s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\scene_context_builders_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 26.64s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: scene load still sequences teardown, generated and
authored object population, UI override application, and replay reset from
`Run`; render-host splitting, shared cine/path helper cleanup, and remaining
replay tool/helper ownership remain active.

## Generated Camera Setup Wrapper Slice

This scene-load slice removed the thin generated-camera setup wrapper from
`Run`. Generated demo scene loads now call
`SceneGeneratedSetup::SetUpCameras(...)` directly from `RunScene.cpp` using the
file-local generated camera context introduced by the previous slice.
`RunRender.cpp` no longer owns a scene-load camera setup method.

Deleted `Run.h` declaration:

- `SetUpCameras`

Deleted `Run::` definition:

- `Run::SetUpCameras`

New owner surface:

- `RunScene.cpp` calls `SceneGeneratedSetup::SetUpCameras(...)` with
  `BuildSceneGeneratedCameraContext( m_systems.cameras, *m_systems.terrain )`
  at the generated-scene load site.

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 198 to 197.
- The `Run.h` rules reject `SetUpCameras` declarations from returning.
- Runtime source guardrails reject `Run::SetUpCameras` definitions from
  returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Scene/RunScene.cpp`,
  `SkullbonezSource/Runtime/RunRender.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and
  `tools/check_runtime_boundaries.py`.
- No new explanatory comments were added. Existing render and scene comments
  remain appropriate because this slice removes a wrapper and makes the
  generated setup call explicit.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\generated_camera_setup_profile_build.log`;
  passed with 0 warnings and 0 errors in 39.74s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\generated_camera_setup_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 48.45s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\generated_camera_setup_runtime_boundaries.log`;
  passed with 0 errors in 1.38s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\generated_camera_setup_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 26.55s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: scene load still sequences terrain/world setup,
teardown, generated and authored object population, UI override application,
and replay reset from `Run`; render-host splitting, shared cine/path helper
cleanup, and remaining replay tool/helper ownership remain active.

## Scene Terrain/World Setup Wrapper Slice

This scene-load slice removed the terrain and world setup wrappers from `Run`.
Generated and authored scene loads now call file-local helpers in `RunScene.cpp`
with explicit borrowed `RunSubsystemState`, `WorldEnvironment`, config, terrain,
and optional renderer dependencies. The helper order preserves the old behavior:
select or rebuild terrain, apply engine-config world defaults, apply authored
scene world overrides when present, then apply the `--no-water` override.

Deleted `Run.h` declarations:

- `ApplyConfiguredWorldEnvironment`
- `ApplyNoWaterOverride`
- `UseDefaultTerrain`
- `UseFlatSlopeTerrain`
- `UpdateWorldTerrainBounds`

Deleted `Run::` definitions:

- `Run::ApplyConfiguredWorldEnvironment`
- `Run::ApplyNoWaterOverride`
- `Run::UseDefaultTerrain`
- `Run::UseFlatSlopeTerrain`
- `Run::UpdateWorldTerrainBounds`

New owner surface:

- File-local `ApplyConfiguredWorldEnvironment(...)`,
  `ApplyNoWaterOverride(...)`, `UseDefaultTerrain(...)`,
  `UseFlatSlopeTerrain(...)`, and `UpdateWorldTerrainBounds(...)` in
  `Runtime/Scene/RunScene.cpp`.

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 197 to 192.
- The `Run.h` rules reject the five terrain/world setup wrapper declarations
  from returning.
- Runtime source guardrails reject the five `Run::` terrain/world setup
  definitions from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Scene/RunScene.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and
  `tools/check_runtime_boundaries.py`.
- No new explanatory comments were added. Existing terrain/world scene-load
  comments remain useful and the helper signatures now show the borrowed
  dependencies directly.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Initial targeted Profile build failed because `Terrain::GetXZBounds()` is
  non-const; the file-local helper parameters were corrected from
  `const Terrain*` to `Terrain*` before final gates.
- Targeted Profile build rerun: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\scene_terrain_world_profile_build_rerun.log`;
  passed with 0 warnings and 0 errors in 7.30s.
- Initial `tools\validate_fast.bat` run stopped at formatting for
  `RunScene.cpp`; that touched file was formatted directly with VS
  `clang-format` before final gates.
- Fast gate rerun: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\scene_terrain_world_validate_fast_rerun.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 53.07s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\scene_terrain_world_runtime_boundaries.log`;
  passed with 0 errors in 1.43s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\scene_terrain_world_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 26.77s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: scene load still sequences teardown, generated and
authored object population, UI override application, tornado setup/sync, and
replay reset from `Run`; render-host splitting, shared cine/path helper cleanup,
and remaining replay tool/helper ownership remain active.

## Replay Prediction Capture Helper Slice

This replay-tool slice removed the prediction body/frame capture wrappers from
`Run`. `RunReplayTools.cpp` now keeps the capture and restore helpers file-local
with explicit `GameModelCollection` and `ReplayRuntime` parameters, while the
remaining prediction job methods still sequence the budgeted prediction build.

Deleted `Run.h` declarations:

- `CaptureReplayPredictionBodyState`
- `ApplyReplayPredictionBodyState`
- `CaptureReplayPredictionFrame`

Deleted `Run::` definitions:

- `Run::CaptureReplayPredictionBodyState`
- `Run::ApplyReplayPredictionBodyState`
- `Run::CaptureReplayPredictionFrame`

New owner surface:

- File-local `CaptureReplayPredictionBodyState(...)`,
  `ApplyReplayPredictionBodyState(...)`, and
  `CaptureReplayPredictionFrame(...)` in
  `Runtime/Replay/RunReplayTools.cpp`.

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 192 to 189.
- The `Run.h` rules reject the three replay prediction capture helper
  declarations from returning.
- Runtime source guardrails reject the three `Run::` replay prediction capture
  definitions from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and
  `tools/check_runtime_boundaries.py`.
- No new explanatory comments were added. Existing replay prediction comments
  moved with the helper bodies and still describe the fork-join and large-frame
  capture thresholds.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_prediction_capture_profile_build.log`;
  passed with 0 warnings and 0 errors in 39.57s.
- Initial `tools\validate_fast.bat` run stopped at formatting for
  `RunReplayTools.cpp`; that touched file was formatted directly with VS
  `clang-format` before final gates.
- Fast gate rerun: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_prediction_capture_validate_fast_rerun.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 52.43s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_prediction_capture_runtime_boundaries.log`;
  passed with 0 errors in 1.46s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_prediction_capture_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 27.32s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: prediction job lifecycle and prediction visualizer
wrappers still remain on `Run`; render-host splitting, shared cine/path helper
cleanup, and remaining scene load ownership remain active.

## Replay Prediction Lifecycle Helper Slice

This replay-tool slice removed the prediction job lifecycle wrappers from
`Run`. `RunReplayTools.cpp` now keeps `BeginReplayPredictionJob(...)`,
`StepReplayPredictionJob(...)`, and `RenderReplayPredictionVisualizer(...)`
file-local with explicit `ReplayRuntime`, `GameModelCollection`, scene-physics,
and timer inputs. `Run::RenderReplayPathVisualizer` still owns the outer replay
visualizer frame budget and passes those inputs into the local prediction helper.

Deleted `Run.h` declarations:

- `BeginReplayPredictionJob`
- `StepReplayPredictionJob`
- `RenderReplayPredictionVisualizer`

Deleted `Run::` definitions:

- `Run::BeginReplayPredictionJob`
- `Run::StepReplayPredictionJob`
- `Run::RenderReplayPredictionVisualizer`

New owner surface:

- File-local `BeginReplayPredictionJob(...)`,
  `StepReplayPredictionJob(...)`, and
  `RenderReplayPredictionVisualizer(...)` in
  `Runtime/Replay/RunReplayTools.cpp`.

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 189 to 186.
- The `Run.h` rules reject the three replay prediction lifecycle helper
  declarations from returning.
- Runtime source guardrails reject the three `Run::` replay prediction
  lifecycle definitions from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and
  `tools/check_runtime_boundaries.py`.
- No new explanatory comments were added. Existing prediction hazard and budget
  comments remain attached to the moved helper bodies.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_prediction_lifecycle_profile_build.log`;
  passed with 0 warnings and 0 errors in 39.75s.
- Initial `tools\validate_fast.bat` run stopped at formatting for
  `RunReplayTools.cpp`; that touched file was formatted directly with VS
  `clang-format` before final gates.
- Fast gate rerun: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_prediction_lifecycle_validate_fast_rerun.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 51.09s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_prediction_lifecycle_runtime_boundaries.log`;
  passed with 0 errors in 1.52s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_prediction_lifecycle_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 27.06s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: `RenderReplayPathVisualizer` still remains on
`Run`; render-host splitting, shared cine/path helper cleanup, and remaining
scene load ownership remain active.

## Replay Velocity Drag Helper Slice

This replay-tool slice removed the velocity edit drag wrapper from `Run`.
`TickReplayVelocityEditInput` now owns the scoped drag lambda directly, keeping
the drag-state reset, ray math, model mutation, and scrubber visibility update
inside the only caller.

Deleted `Run.h` declaration:

- `ApplyReplayVelocityEditDrag`

Deleted `Run::` definition:

- `Run::ApplyReplayVelocityEditDrag`

New owner surface:

- Scoped `applyReplayVelocityEditDrag` lambda inside
  `Runtime/Replay/RunReplayTools.cpp` `Run::TickReplayVelocityEditInput`.

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 186 to 185.
- The existing replay velocity apply-helper guard now rejects
  `ApplyReplayVelocityEditDrag` declarations and `Run::` definitions from
  returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and
  `tools/check_runtime_boundaries.py`.
- No explanatory comments were added. The drag logic remains adjacent to the
  input branch that owns the gesture lifecycle.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_velocity_drag_profile_build.log`;
  passed with 0 warnings and 0 errors in 39.62s.
- Initial `tools\validate_fast.bat` run stopped at formatting for
  `RunReplayTools.cpp`; that touched file was formatted directly with VS
  `clang-format` before final gates.
- Fast gate rerun: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_velocity_drag_validate_fast_rerun.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 52.16s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_velocity_drag_runtime_boundaries.log`;
  passed with 0 errors in 1.52s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_velocity_drag_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 26.34s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: replay overlay rendering and cause-tree camera focus
helpers still remain on `Run`; render-host splitting, shared cine/path helper
cleanup, and remaining scene load ownership remain active.

## Editable Scene Snapshot Helper Slice

This scene-runtime slice removed the editable-scene snapshot persistence wrapper
from `Run`. `SaveCurrentSceneDefaults` still owns the UI-facing command, but the
editable scene snapshot write now goes through a file-local helper in
`RunScene.cpp` with explicit scene, model, world, camera, and debug-hidden state
inputs.

Deleted `Run.h` declaration:

- `SaveCurrentEditableSceneSnapshot`

Deleted `Run::` definition:

- `Run::SaveCurrentEditableSceneSnapshot`

New owner surface:

- File-local `SaveCurrentEditableSceneSnapshot(...)` helper in
  `Runtime/Scene/RunScene.cpp`.

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 185 to 184.
- New header and source guardrails reject
  `SaveCurrentEditableSceneSnapshot` declarations and
  `Run::SaveCurrentEditableSceneSnapshot` definitions from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Scene/RunScene.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and
  `tools/check_runtime_boundaries.py`.
- No explanatory comments were added. The helper signature exposes the borrowed
  scene, model, world, camera, and visibility dependencies directly.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Initial targeted Profile build caught a constness mismatch because
  `GameModelCollection::SaveSceneSnapshot` expects mutable `WorldEnvironment&`;
  the helper now accepts `WorldEnvironment&`.
- Targeted Profile build rerun: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\editable_scene_snapshot_profile_build_rerun.log`;
  passed with 0 warnings and 0 errors in 7.35s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\editable_scene_snapshot_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 47.65s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\editable_scene_snapshot_runtime_boundaries.log`;
  passed with 0 errors in 1.58s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\editable_scene_snapshot_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 26.38s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: `SaveCurrentSceneDefaults` still remains on `Run`;
scene persistence ownership, render-host splitting, shared cine/path helper
cleanup, and remaining scene load ownership remain active.

## Scene Tornado Defaults Helper Slice

This scene-runtime slice removed the active-scene tornado defaulting wrapper from
`Run`. Scene load still decides when to reset tornado state, apply scene-authored
systems, process CLI overrides, and sync to physics, but the basin-centered
field-default policy is now file-local in `RunScene.cpp`.

Deleted `Run.h` declaration:

- `ApplyTornadoDefaultsForActiveScene`

Deleted `Run::` definition:

- `Run::ApplyTornadoDefaultsForActiveScene`

New owner surface:

- File-local `ApplyTornadoDefaultsForActiveScene(...)` helper in
  `Runtime/Scene/RunScene.cpp`, with explicit `RunRuntimeSettings&`,
  `WorldEnvironment&`, and `CinematicRenderConfig&` inputs.

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 184 to 183.
- New header and source guardrails reject
  `ApplyTornadoDefaultsForActiveScene` declarations and
  `Run::ApplyTornadoDefaultsForActiveScene` definitions from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Scene/RunScene.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and
  `tools/check_runtime_boundaries.py`.
- No explanatory comments were added. The helper signature names the runtime
  settings, world, and cinematic inputs needed to derive the field defaults.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Initial targeted Profile build caught a namespace qualification mismatch in
  the file-local helper; the helper now uses the imported `TornadoFieldConfig`
  type directly.
- Targeted Profile build rerun: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\scene_tornado_defaults_profile_build_rerun.log`;
  passed with 0 warnings and 0 errors in 7.37s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\scene_tornado_defaults_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 48.67s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\scene_tornado_defaults_runtime_boundaries.log`;
  passed with 0 errors in 1.66s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\scene_tornado_defaults_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 27.16s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: `SyncTornadoFieldToPhysics` still remains on `Run`
because scene load, CLI overrides, and UI input all call it; scene persistence,
render-host splitting, shared cine/path helper cleanup, and remaining scene load
ownership remain active.

## Replay Cause-Tree Camera Activation Helper Slice

This replay-tool slice removed the cause-tree row camera activation wrapper from
`Run`. `TickReplayCauseTreeInput` is the only caller, so the focus/camera setup
logic now lives as a scoped lambda inside that input path instead of as a
private `Run` method.

Deleted `Run.h` declaration:

- `ActivateReplayCameraForCauseRow`

Deleted `Run::` definition:

- `Run::ActivateReplayCameraForCauseRow`

New owner surface:

- Scoped `activateReplayCameraForCauseRow` lambda inside
  `Runtime/Replay/RunReplayTools.cpp` `Run::TickReplayCauseTreeInput`.

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 183 to 182.
- New header and source guardrails reject
  `ActivateReplayCameraForCauseRow` declarations and
  `Run::ActivateReplayCameraForCauseRow` definitions from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and
  `tools/check_runtime_boundaries.py`.
- No explanatory comments were added. The helper remains scoped beside the
  cause-tree input branch that owns row selection and mouse focus.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_cause_camera_activation_profile_build.log`;
  passed with 0 warnings and 0 errors in 39.72s.
- Initial `tools\validate_fast.bat` run stopped at formatting for
  `RunReplayTools.cpp`; that touched file was formatted directly with VS
  `clang-format` before final gates.
- Fast gate rerun: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_cause_camera_activation_validate_fast_rerun.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 51.42s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_cause_camera_activation_runtime_boundaries.log`;
  passed with 0 errors in 1.68s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_cause_camera_activation_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 26.06s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: cause-tree input, focus clearing, replay inspection
camera state, replay overlay rendering, and render-host splitting still remain
active `Run`/replay-tool boundaries.

## Replay Render-State Helper Slice

This render/replay slice removed the replay render-state wrapper cluster from
`Run`. `Run::Render` is the only caller, so scrubbed-sample application,
launcher visual backup/restore, and render-pose restore now live as scoped
lambdas inside the render frame path.

Deleted `Run.h` declarations:

- `ApplyReplayRenderStateForFrame`
- `RestoreReplayRenderStateForFrame`
- `ApplyReplayLauncherVisualSampleForRender`
- `RestoreReplayLauncherVisualForRender`

Deleted `Run::` definitions:

- `Run::ApplyReplayRenderStateForFrame`
- `Run::RestoreReplayRenderStateForFrame`
- `Run::ApplyReplayLauncherVisualSampleForRender`
- `Run::RestoreReplayLauncherVisualForRender`

New owner surface:

- Scoped render-frame lambdas inside `Runtime/RunRender.cpp` `Run::Render`.

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 182 to 178.
- New header and source guardrails reject the removed replay render-state helper
  declarations and `Run::` definitions from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/RunRender.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and
  `tools/check_runtime_boundaries.py`.
- No explanatory comments were added. The lambdas sit next to the render frame
  that applies and restores temporary replay render state.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_render_state_profile_build.log`;
  passed with 0 warnings and 0 errors in 39.70s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_render_state_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 48.80s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_render_state_runtime_boundaries.log`;
  passed with 0 errors in 1.77s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_render_state_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 26.18s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: launcher visual sample build/restore still remains
on `Run` because capture/replay restore paths outside render also use it;
render-host splitting, replay overlay rendering, and shared replay camera state
remain active.

## Replay Launcher Visual Sample Helper Slice

This replay/launcher slice moved launcher visual sample build/restore ownership
from `Run` into `RuntimeTools`, which already owns ray-test line state and laser
shot state. Capture, restore, and render paths now call `m_runtimeTools` instead
of private `Run` wrappers.

Deleted `Run.h` declarations:

- `BuildReplayLauncherVisualSample`
- `RestoreReplayLauncherVisualSample`

Deleted `Run::` definitions:

- `Run::BuildReplayLauncherVisualSample`
- `Run::RestoreReplayLauncherVisualSample`

New owner methods:

- `RuntimeTools::BuildReplayLauncherVisualSample(...) const`
- `RuntimeTools::RestoreReplayLauncherVisualSample(...)`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 178 to 176.
- New header and source guardrails reject the removed replay launcher visual
  sample helper declarations and `Run::` definitions from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Tools/RuntimeTools.h`,
  `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp`,
  `SkullbonezSource/Runtime/RunFrame.cpp`,
  `SkullbonezSource/Runtime/Run.cpp`,
  `SkullbonezSource/Runtime/RunRender.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and
  `tools/check_runtime_boundaries.py`.
- No explanatory comments were added. The moved methods sit beside the
  ray-test and laser state they capture and restore.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_launcher_visual_runtime_tools_profile_build.log`;
  passed with 0 warnings and 0 errors in 42.67s.
- Initial `tools\validate_fast.bat` run stopped at formatting for
  `RuntimeTools.cpp`; that touched file was formatted directly with VS
  `clang-format` before final gates.
- Fast gate rerun: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_launcher_visual_runtime_tools_validate_fast_rerun.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 54.47s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_launcher_visual_runtime_tools_runtime_boundaries.log`;
  passed with 0 errors in 1.85s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_launcher_visual_runtime_tools_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 26.54s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: replay restore/apply helpers and render-host
splitting remain active; this slice only moved the launcher visual state
snapshot/restore surface to the state owner.

## Replay Sample Comparison Helper Slice

This replay capture slice removed the replay presentation/solver mismatch
diagnostic wrapper from `Run`. The comparison is now a file-local helper in
`RunFrame.cpp` beside `Run::CaptureReplayPhysicsStep`, with explicit
`ReplayRuntime&` and `RunReplayMismatchState&` inputs.

Deleted `Run.h` declarations:

- `CompareLatestReplaySamples`

Deleted `Run::` definitions:

- `Run::CompareLatestReplaySamples`

New owner surface:

- File-local `CompareLatestReplaySamples(...)` in `RunFrame.cpp`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 176 to 175.
- New header and source guardrails reject the removed replay sample comparison
  helper declaration and `Run::` definition from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/RunFrame.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and
  `tools/check_runtime_boundaries.py`.
- No explanatory comments were added. The helper remains colocated with the
  replay capture path that consumes it.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_sample_compare_profile_build.log`;
  passed with 0 warnings and 0 errors in 39.63s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_sample_compare_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 47.99s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_sample_compare_runtime_boundaries.log`;
  passed with 0 errors in 1.83s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_sample_compare_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 26.12s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: replay restore/apply helpers, cross-file replay
probe/hash helpers, and render-host splitting remain active; this slice only
localized presentation/solver mismatch diagnostics.

## Replay Presentation Picker Helper Slice

This replay scrubber slice removed the private replay presentation artifact
picker wrapper from `Run`. The file-picker command is now scoped to
`Run::TickReplayScrubberInput` as the only caller, while the public
`LoadReplayPresentationArtifact(...)` API remains available for startup and
debug probe flows.

Deleted `Run.h` declarations:

- `PromptLoadReplayPresentationArtifact`

Deleted `Run::` definitions:

- `Run::PromptLoadReplayPresentationArtifact`

New owner surface:

- Scoped `promptLoadReplayPresentationArtifact` lambda inside
  `Run::TickReplayScrubberInput`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 175 to 174.
- New header and source guardrails reject the removed replay presentation
  picker helper declaration and `Run::` definition from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and
  `tools/check_runtime_boundaries.py`.
- No explanatory comments were added. The picker command now lives at its
  scrubber button call site.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_presentation_picker_profile_build.log`;
  passed with 0 warnings and 0 errors in 39.56s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_presentation_picker_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 48.86s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_presentation_picker_runtime_boundaries.log`;
  passed with 0 errors in 1.92s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_presentation_picker_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 26.80s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: replay restore/apply helpers, cross-file replay
probe/hash helpers, and render-host splitting remain active; this slice only
localized the UI prompt wrapper for loading presentation artifacts.

## Tornado Sync Helper Slice

This runtime/scene slice removed the private tornado physics sync wrapper from
`Run`. The three existing sync points now call a `RunInternal` helper with
explicit `GameModelCollection&` and `RunRuntimeSettings&` inputs, keeping the
bridge between UI/CLI scene tornado settings and physics state out of `Run.h`.

Deleted `Run.h` declarations:

- `SyncTornadoFieldToPhysics`

Deleted `Run::` definitions:

- `Run::SyncTornadoFieldToPhysics`

New owner surface:

- `RunInternal::SyncTornadoRuntimeSettingsToPhysics(...)`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 174 to 173.
- New header and source guardrails reject the removed tornado physics sync
  wrapper declaration and `Run::` definition from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/RunInternal.h`,
  `SkullbonezSource/Runtime/Run.cpp`,
  `SkullbonezSource/Runtime/RunInput.cpp`,
  `SkullbonezSource/Runtime/Run.h`,
  `SkullbonezSource/Runtime/Scene/RunScene.cpp`, and
  `tools/check_runtime_boundaries.py`.
- No explanatory comments were added. The helper name carries the bridge
  contract and all call sites already sit in tornado setting update paths.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\tornado_sync_helper_profile_build.log`;
  passed with 0 warnings and 0 errors in 39.98s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\tornado_sync_helper_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 48.32s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\tornado_sync_helper_runtime_boundaries.log`;
  passed with 0 errors in 1.90s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\tornado_sync_helper_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 26.47s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: scene load setup phases and broad replay
restore/apply helpers remain active; this slice only removed the narrow tornado
settings-to-physics sync wrapper.

## Replay Scrubber Save Helper Slice

This replay scrubber slice removed the private replay-buffer save wrapper from
`Run`. The helper is now file-local to `RunReplayTools.cpp` with explicit
`ReplayRuntime&`, track, and timestamp inputs, colocated with the scrubber save
button path that uses it.

Deleted `Run.h` declarations:

- `SaveReplayBufferFromScrubber`

Deleted `Run::` definitions:

- `Run::SaveReplayBufferFromScrubber`

New owner surface:

- File-local `SaveReplayBufferFromScrubber(...)` in
  `Runtime/Replay/RunReplayTools.cpp`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 173 to 172.
- New header and source guardrails reject the removed replay scrubber save
  helper declaration and `Run::` definition from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp`,
  `SkullbonezSource/Runtime/Run.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and
  `tools/check_runtime_boundaries.py`.
- No explanatory comments were added. The moved helper sits beside the scrubber
  input branch that triggers replay saves.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_scrubber_save_profile_build.log`;
  passed with 0 warnings and 0 errors in 39.69s.
- Initial `tools\validate_fast.bat` stopped at formatting for
  `RunReplayTools.cpp`; that touched file was formatted directly with VS
  `clang-format` before final gates.
- Fast gate rerun: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_scrubber_save_validate_fast_rerun.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 52.88s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_scrubber_save_runtime_boundaries.log`;
  passed with 0 errors in 1.98s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_scrubber_save_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 26.86s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: replay scrubber/camera state, replay restore/apply
helpers, and render-host splitting remain active; this slice only localized the
scrubber save-file helper.

## Replay Restore Event Helper Slice

This replay restore slice removed the private replay-event application wrapper
from `Run`. The event application logic is now a scoped lambda inside
`Run::RestoreReplayV2ArtifactTargetState`, colocated with the v2 target restore
path that consumes replay events while applying checkpoint state.

Deleted `Run.h` declarations:

- `ApplyReplayEventForRestoreTarget`

Deleted `Run::` definitions:

- `Run::ApplyReplayEventForRestoreTarget`

New owner surface:

- Scoped `applyReplayEventForRestoreTarget` lambda in
  `Run::RestoreReplayV2ArtifactTargetState`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 172 to 171.
- New header and source guardrails reject the removed replay restore event
  helper declaration and `Run::` definition from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/RunFrame.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and
  `tools/check_runtime_boundaries.py`.
- No explanatory comments were added. The replay event logic remains beside the
  v2 restore loop that applies serialized replay state.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_restore_event_profile_build.log`;
  passed with 0 warnings and 0 errors in 39.55s.
- Initial `tools\validate_fast.bat` stopped at formatting for `RunFrame.cpp`;
  that touched file was formatted directly with VS `clang-format` before final
  gates.
- Fast gate rerun: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_restore_event_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 51.94s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_restore_event_runtime_boundaries.log`;
  passed with 0 errors in 2.05s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_restore_event_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 26.70s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: solver apply/hash helpers, replay inspection/camera
helpers, and render-host splitting remain active; this slice only localized the
v2 target-restore event application helper.

## Replay Inspection Camera Update Wrapper Slice

This replay inspection slice removed the private camera-update wrapper from
`Run`. Replay camera activation is now driven by a replay-owned state predicate,
and the remaining `Run` call sites choose `EnterReplayInspectionCamera` or
`ExitReplayInspectionCamera` directly where interaction ownership is already
being updated.

Deleted `Run.h` declarations:

- `UpdateReplayInspectionCamera`

Deleted `Run::` definitions:

- `Run::UpdateReplayInspectionCamera`

New owner surface:

- `ReplayRuntime::ShouldUseInspectionCamera()`

Updated call sites:

- `Run::ArmLoadedReplayPresentationScrubber`
- `Run::SetReplayLiveAdvanceHeld`
- `Run::TickReplayScrubberInput`
- `Run::ClearReplayCameraFocus`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 171 to 170.
- New header and source guardrails reject the removed replay inspection camera
  update wrapper declaration and `Run::` definition from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`,
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.h`,
  `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp`,
  `SkullbonezSource/Runtime/Run.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and
  `tools/check_runtime_boundaries.py`.
- No explanatory comments were added. The predicate is short and named around
  the replay-owned state decision.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_inspection_camera_profile_build.log`;
  passed with 0 warnings and 0 errors in 44.49s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_inspection_camera_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 53.68s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_inspection_camera_runtime_boundaries.log`;
  passed with 0 errors in 2.12s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_inspection_camera_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 26.51s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: solver apply/hash helpers, the remaining
replay-inspection camera enter/exit and focus-control methods, and render-host
splitting remain active; this slice only removed the narrow update wrapper.

## Replay Scrubber Reset Wrapper Slice

This replay scrubber slice removed the private scrubber reset wrapper from
`Run`. The preserved-field reset now belongs to `ReplayRuntime`, while `Run`
call sites explicitly perform the inspection-camera exit only when the replay
runtime reports that the old wrapper would have exited.

Deleted `Run.h` declarations:

- `ResetReplayScrubber`

Deleted `Run::` definitions:

- `Run::ResetReplayScrubber`

New owner surface:

- `ReplayRuntime::ResetScrubberState()`

Updated call sites:

- `Run::SetReplayRecording`
- `Run::ResetReplayTimelineForActiveScene`
- `Run::ClearReplayInteractionForRuntimeTransition`
- `Run::TakeInput`
- `Run::TickReplayScrubberInput`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 170 to 169.
- New header and source guardrails reject the removed replay scrubber reset
  wrapper declaration and `Run::` definition from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`,
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.h`,
  `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp`,
  `SkullbonezSource/Runtime/Run.cpp`,
  `SkullbonezSource/Runtime/Run.h`,
  `SkullbonezSource/Runtime/RunInput.cpp`, and
  `tools/check_runtime_boundaries.py`.
- No explanatory comments were added. The new replay runtime method keeps the
  old reset preservation policy in one state-owner method.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_scrubber_reset_profile_build.log`;
  passed with 0 warnings and 0 errors in 44.54s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_scrubber_reset_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 53.06s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_scrubber_reset_runtime_boundaries.log`;
  passed with 0 errors in 2.12s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_scrubber_reset_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 27.58s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: solver apply/hash helpers, the remaining replay
inspection camera enter/exit and focus-control methods, and render-host
splitting remain active; this slice only moved scrubber reset state ownership.

## Replay Event Frame Cursor Wrapper Slice

This replay event cursor slice removed the private `Run` wrapper around the
replay runtime's next event-frame cursor. Record/event call sites now read the
cursor directly from `ReplayRuntime`, keeping the state-owner lookup explicit at
the point where replay events are emitted.

Deleted `Run.h` declarations:

- `NextReplayEventFrameIndex`

Deleted `Run::` definitions:

- `Run::NextReplayEventFrameIndex`

Existing owner surface:

- `ReplayRuntime::NextEventFrameIndex()`

Updated call sites:

- `Run::RecordReplayWorldOverrideEvent`
- `Run::RecordReplayLauncherConfigEvent`
- `Run::RecordReplayLauncherFireEvent`
- `Run::RecordReplayEditorPlaceEvent`
- `Run::RecordReplayEditorTransformEvent`
- `Run::DrainRuntimeCommands`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 169 to 168.
- New header and source guardrails reject the removed replay event frame cursor
  wrapper declaration and `Run::` definition from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Run.cpp`,
  `SkullbonezSource/Runtime/Run.h`,
  `SkullbonezSource/Runtime/RunInput.cpp`, and
  `tools/check_runtime_boundaries.py`.
- No comments were added. The replacement calls are direct state-owner reads.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_event_frame_cursor_profile_build.log`;
  passed with 0 warnings and 0 errors in 39.61s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_event_frame_cursor_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 49.29s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_event_frame_cursor_runtime_boundaries.log`;
  passed with 0 errors in 2.15s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_event_frame_cursor_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 27.47s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: the remaining replay event recording wrappers,
solver apply/hash helpers, replay inspection camera enter/exit and focus-control
methods, and render-host splitting remain active; this slice only removed the
narrow event-frame cursor wrapper.

## Replay Event Record Wrapper Slice

This replay event record slice removed the private `Run` wrapper around replay
event appends. Existing `Run` packaging helpers still compute the same event
payloads, but the append operation now goes directly through the replay runtime
owner at each emission point.

Deleted `Run.h` declarations:

- `RecordReplayEvent`

Deleted `Run::` definitions:

- `Run::RecordReplayEvent`

Existing owner surface:

- `ReplayRuntime::RecordEvent(...)`

Updated call sites:

- `Run::ResetReplayTimelineForActiveScene`
- `Run::RecordReplayWorldOverrideEvent`
- `Run::RecordReplayLauncherConfigEvent`
- `Run::RecordReplayLauncherFireEvent`
- `Run::RecordReplayGeneratedSceneConfigEvent`
- `Run::RecordReplayEditorPlaceEvent`
- `Run::RecordReplayEditorTransformEvent`
- `Run::RestoreReplaySolverSampleAsLive`
- `Run::RestoreReplayV2ArtifactTargetState`
- `Run::DrainRuntimeCommands`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 168 to 167.
- New header and source guardrails reject the removed replay event record
  wrapper declaration and `Run::` definition from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Run.cpp`,
  `SkullbonezSource/Runtime/Run.h`,
  `SkullbonezSource/Runtime/RunFrame.cpp`,
  `SkullbonezSource/Runtime/RunInput.cpp`, and
  `tools/check_runtime_boundaries.py`.
- No comments were added. The replacement calls are direct owner-method calls.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_event_record_profile_build.log`;
  passed with 0 warnings and 0 errors in 39.60s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_event_record_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 48.59s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_event_record_runtime_boundaries.log`;
  passed with 0 errors in 2.25s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_event_record_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 26.86s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: the remaining replay event payload packaging
helpers, solver apply/hash helpers, replay inspection camera enter/exit and
focus-control methods, and render-host splitting remain active; this slice only
removed the narrow event append wrapper.

## Replay Generated-Scene Config Wrapper Slice

This replay generated-scene config slice removed the private `Run` helper that
only served `ResetReplayTimelineForActiveScene`. The generated-scene config
payload now stays local to timeline reset immediately after the timeline-start
event append.

Deleted `Run.h` declarations:

- `RecordReplayGeneratedSceneConfigEvent`

Deleted `Run::` definitions:

- `Run::RecordReplayGeneratedSceneConfigEvent`

Existing owner surface:

- `ReplayRuntime::RecordEvent(...)`

Updated call sites:

- `Run::ResetReplayTimelineForActiveScene`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 167 to 166.
- New header and source guardrails reject the removed replay generated-scene
  config wrapper declaration and `Run::` definition from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Run.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and
  `tools/check_runtime_boundaries.py`.
- No comments were added. The localized block preserves the existing payload
  construction in the only caller.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_generated_scene_config_profile_build.log`;
  passed with 0 warnings and 0 errors in 5.76s after formatting the touched
  `Run.cpp` file.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_generated_scene_config_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 49.88s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_generated_scene_config_runtime_boundaries.log`;
  passed with 0 errors in 2.25s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_generated_scene_config_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 26.87s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: the remaining replay event payload packaging
helpers, solver apply/hash helpers, replay inspection camera enter/exit and
focus-control methods, and render-host splitting remain active; this slice only
removed the single-caller generated-scene config wrapper.

## Replay Physics Capture Wrapper Slice

This replay physics capture slice removed the private capture-only hook and its
unused thunk from `Run`. `AfterPhysicsStep` now performs the replay capture
directly after mouse-pickup angular-velocity restoration, preserving the old
capture-enabled guard and debug replay probe timing.

Deleted `Run.h` declarations:

- `CaptureReplayPhysicsStep`
- `CaptureReplayPhysicsStepThunk`

Deleted `Run::` definitions:

- `Run::CaptureReplayPhysicsStep`
- `Run::CaptureReplayPhysicsStepThunk`

Retained owner surface:

- `Run::AfterPhysicsStep`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 166 to 164.
- New header and source guardrails reject the removed replay physics capture
  wrapper and thunk declarations and `Run::` definitions from returning.
- Synthetic self-tests cover both removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/RunFrame.cpp`,
  `SkullbonezSource/Runtime/Run.h`, and
  `tools/check_runtime_boundaries.py`.
- No comments were added. The inlined block keeps the old profiler scope and
  debug probe calls in the capture-enabled branch.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_physics_capture_profile_build.log`;
  passed with 0 warnings and 0 errors in 39.58s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_physics_capture_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 48.73s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_physics_capture_runtime_boundaries.log`;
  passed with 0 errors in 2.44s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_physics_capture_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 27.72s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: the remaining replay event payload packaging
helpers, solver apply/hash helpers, replay inspection camera enter/exit and
focus-control methods, and render-host splitting remain active; this slice only
collapsed the replay capture hook into the post-physics step.

## Replay World Override Event Wrapper Slice

This replay world override slice moved the exact world-scalar event payload from
`Run` into `ReplayRuntime`. `ApplyUIWorldOverride` now updates the world and
asks the replay owner to append the event, while `ReplayRuntime` owns the change
flags, float-bit payloads, and FNV hash used by the v2 event stream.

Deleted `Run.h` declarations:

- `RecordReplayWorldOverrideEvent`

Deleted `Run::` definitions:

- `Run::RecordReplayWorldOverrideEvent`

New owner surface:

- `ReplayRuntime::RecordWorldOverrideEvent(...)`

Updated call sites:

- `Run::ApplyUIWorldOverride`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 164 to 163.
- New header and source guardrails reject the removed replay world override
  event wrapper declaration and `Run::` definition from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`,
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.h`,
  `SkullbonezSource/Runtime/Run.cpp`,
  `SkullbonezSource/Runtime/Run.h`,
  `SkullbonezSource/Runtime/Scene/RunScene.cpp`, and
  `tools/check_runtime_boundaries.py`.
- No comments were added. The new replay runtime method preserves the previous
  event flags, float payload bits, event-frame cursor, and hash text.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_world_override_profile_build.log`;
  passed with 0 warnings and 0 errors in 44.51s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_world_override_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 53.86s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_world_override_runtime_boundaries.log`;
  passed with 0 errors in 2.39s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_world_override_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 27.44s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: the remaining replay event payload packaging
helpers, solver apply/hash helpers, replay inspection camera enter/exit and
focus-control methods, and render-host splitting remain active; this slice only
moved the world override event payload to `ReplayRuntime`.

## Replay Launcher Config Event Wrapper Slice

This replay launcher config slice moved launcher-setting event payload ownership
from `Run` into `ReplayRuntime`. UI slider updates and the replay-save probe now
ask `ReplayRuntime` to append launcher config events, while the replay owner
keeps the changed flags, event-frame cursor, float-bit payloads, FNV hash, and
event text in one place.

Deleted `Run.h` declarations:

- `RecordReplayLauncherConfigEvent`

Deleted `Run::` definitions:

- `Run::RecordReplayLauncherConfigEvent`

New owner surface:

- `ReplayRuntime::RecordLauncherConfigEvent(...)`

Updated call sites:

- `Run::TakeInput`
- `Run::TickReplaySaveProbe`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 163 to 162.
- New header and source guardrails reject the removed replay launcher config
  event wrapper declaration and `Run::` definition from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`,
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.h`,
  `SkullbonezSource/Runtime/Run.cpp`,
  `SkullbonezSource/Runtime/Run.h`,
  `SkullbonezSource/Runtime/RunFrame.cpp`,
  `SkullbonezSource/Runtime/RunInput.cpp`, and
  `tools/check_runtime_boundaries.py`.
- No comments were added. The moved replay runtime method preserves the previous
  launcher config event semantics and payload shape.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_launcher_config_profile_build.log`;
  passed with 0 warnings and 0 errors in 44.70s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_launcher_config_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 53.27s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_launcher_config_runtime_boundaries.log`;
  passed with 0 errors in 2.47s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_launcher_config_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 27.00s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: the remaining replay fire/editor event payload
packaging helpers, solver apply/hash helpers, replay inspection camera
enter/exit and focus-control methods, and render-host splitting remain active;
this slice only moved launcher config event payload ownership to
`ReplayRuntime`.

## Replay Launcher Fire Event Wrapper Slice

This replay launcher fire slice moved the camera-ray fire event payload from
`Run` into `ReplayRuntime`. Runtime input and the replay-save probe now pass the
ray, fire mode, launcher scalars, and pre-fire model count to the replay owner,
which builds the `ray9:` payload, flags, float-bit values, event-frame cursor,
and FNV hash.

Deleted `Run.h` declarations:

- `RecordReplayLauncherFireEvent`

Deleted `Run::` definitions:

- `Run::RecordReplayLauncherFireEvent`

New owner surface:

- `ReplayRuntime::RecordLauncherFireEvent(...)`

Updated call sites:

- `Run::TakeInput`
- `Run::TickReplaySaveProbe`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 162 to 161.
- New header and source guardrails reject the removed replay launcher fire event
  wrapper declaration and `Run::` definition from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`,
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.h`,
  `SkullbonezSource/Runtime/Run.cpp`,
  `SkullbonezSource/Runtime/Run.h`,
  `SkullbonezSource/Runtime/RunFrame.cpp`,
  `SkullbonezSource/Runtime/RunInput.cpp`, and
  `tools/check_runtime_boundaries.py`.
- No comments were added. The moved replay runtime method preserves the previous
  launcher fire event semantics and payload shape.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_launcher_fire_profile_build.log`;
  passed with 0 warnings and 0 errors in 44.34s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_launcher_fire_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 54.58s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_launcher_fire_runtime_boundaries.log`;
  passed with 0 errors in 2.50s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_launcher_fire_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 27.14s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: the remaining editor event payload packaging
helpers, solver apply/hash helpers, replay inspection camera enter/exit and
focus-control methods, and render-host splitting remain active; this slice only
moved launcher fire event payload ownership to `ReplayRuntime`.

## Replay Editor Place Event Wrapper Slice

This replay editor placement slice moved placement event payload construction
from `Run` into `ReplayRuntime`. Editor commit handling and the replay-save
probe now pass the placement recipe outputs to the replay owner, which builds
the `place7:` payload, flags, event-frame cursor, integer/float hash, and event
values.

Deleted `Run.h` declarations:

- `RecordReplayEditorPlaceEvent`

Deleted `Run::` definitions:

- `Run::RecordReplayEditorPlaceEvent`

New owner surface:

- `ReplayRuntime::RecordEditorPlaceEvent(...)`

Updated call sites:

- `Run::TickEditorToolInput`
- `Run::TickReplaySaveProbe`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 161 to 160.
- New header and source guardrails reject the removed replay editor place event
  wrapper declaration and `Run::` definition from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Editor/RunEditorTools.cpp`,
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`,
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.h`,
  `SkullbonezSource/Runtime/Run.cpp`,
  `SkullbonezSource/Runtime/Run.h`,
  `SkullbonezSource/Runtime/RunFrame.cpp`, and
  `tools/check_runtime_boundaries.py`.
- No comments were added. The moved replay runtime method preserves the previous
  editor placement event semantics and payload shape.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_editor_place_profile_build.log`;
  passed with 0 warnings and 0 errors in 44.33s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_editor_place_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 53.55s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_editor_place_runtime_boundaries.log`;
  passed with 0 errors in 2.53s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_editor_place_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 27.52s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: the remaining editor transform event payload
packaging helper, solver apply/hash helpers, replay inspection camera
enter/exit and focus-control methods, and render-host splitting remain active;
this slice only moved editor placement event payload ownership to
`ReplayRuntime`.

## Replay Editor Transform Event Wrapper Slice

This replay editor transform slice moved transform/scale gizmo event payload
construction from `Run` into `ReplayRuntime`. Editor drag release handling and
the replay-save probe now pass the model, changed flags, model count, and scale
details to the replay owner, which validates scale payloads and builds the
`xform7:`/`xform8:` payload, event-frame cursor, flags, body id, and FNV hash.

Deleted `Run.h` declarations:

- `RecordReplayEditorTransformEvent`

Deleted `Run::` definitions:

- `Run::RecordReplayEditorTransformEvent`

New owner surface:

- `ReplayRuntime::RecordEditorTransformEvent(...)`

Updated call sites:

- `Run::TickEditorToolInput`
- `Run::TickReplaySaveProbe`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 160 to 159.
- New header and source guardrails reject the removed replay editor transform
  event wrapper declaration and `Run::` definition from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Editor/RunEditorTools.cpp`,
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`,
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.h`,
  `SkullbonezSource/Runtime/Run.cpp`,
  `SkullbonezSource/Runtime/Run.h`,
  `SkullbonezSource/Runtime/RunFrame.cpp`, and
  `tools/check_runtime_boundaries.py`.
- No comments were added. The moved replay runtime method preserves the previous
  editor transform event semantics and payload shape.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_editor_transform_profile_build.log`;
  passed with 0 warnings and 0 errors in 44.27s.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_editor_transform_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 57.31s. The first fast run stopped on formatting for
  `ReplayRuntime.cpp`; Visual Studio `clang-format.exe` was applied only to that
  touched file before the passing rerun.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_editor_transform_runtime_boundaries.log`;
  passed with 0 errors in 2.61s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_editor_transform_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 27.60s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: solver apply/hash helpers, replay inspection camera
enter/exit and focus-control methods, and render-host splitting remain active;
this slice only moved editor transform event payload ownership to
`ReplayRuntime`.

## Replay Loaded-Presentation Scrubber Arming Slice

This replay helper slice moved loaded-presentation scrubber state arming from
`Run` into `ReplayRuntime`. The replay artifact load path and debug load probe
still perform `Run`-owned interaction/camera transitions explicitly, but the
replay-owned scrubber, prediction, velocity-edit, path, and visibility state now
changes through `ReplayRuntime::ArmLoadedPresentationScrubber(...)`.

Deleted `Run.h` declarations:

- `ArmLoadedReplayPresentationScrubber`

Deleted `Run::` definitions:

- `Run::ArmLoadedReplayPresentationScrubber`

New owner surface:

- `ReplayRuntime::ArmLoadedPresentationScrubber(float normalized, double now)`

Updated call sites:

- `Run::LoadReplayPresentationArtifact`
- `Run::VerifyLoadedReplayPresentationProbe`

Boundary/tooling guard:

- `tools/check_runtime_boundaries.py` lowers the `Run.h` private-method
  ratchet from 159 to 158.
- New header and source guardrails reject the removed loaded-presentation
  scrubber arming wrapper declaration and `Run::` definition from returning.
- Synthetic self-tests cover the removed header and source surfaces.

Comment-style audit:

- Touched source-bearing files inspected:
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`,
  `SkullbonezSource/Runtime/Replay/ReplayRuntime.h`,
  `SkullbonezSource/Runtime/Run.cpp`,
  `SkullbonezSource/Runtime/Run.h`,
  `SkullbonezSource/Runtime/RunFrame.cpp`, and
  `tools/check_runtime_boundaries.py`.
- No comments were added. Existing replay scrubber behavior and timing are
  preserved while the replay-owned state mutation moved behind `ReplayRuntime`.
- No subsystem-wide checklist was required; this was a touched-file audit, not
  a comment remediation pass.

Validation:

- Targeted Profile build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\agent_logs\replay_loaded_scrubber_profile_build.log`;
  passed with 0 warnings and 0 errors in 13.17s after qualifying the replay
  overlay timing constant in the moved method.
- Fast gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\agent_logs\replay_loaded_scrubber_validate_fast.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds in 53.48s.
- Runtime boundary gate: `python tools\check_runtime_boundaries.py --repo .`,
  logged at
  `TestOutput\validation\agent_logs\replay_loaded_scrubber_runtime_boundaries.log`;
  passed with 0 errors in 2.67s.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\agent_logs\replay_loaded_scrubber_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv` in 27.52s.

Rubber-duck review was intentionally deferred by explicit user instruction until
the end of the remaining plan work. Do not move this plan to `Done/` until the
final rubber-duck pass is satisfied.

Residual architecture risk: `SetReplayLiveAdvanceHeld`, replay inspection
camera enter/exit and focus-control methods, solver apply/hash helpers, and
render-host splitting remain active; this slice only moved loaded-presentation
scrubber state ownership to `ReplayRuntime`.

## Rules

- Each implementation slice must remove a coherent cluster of `Run::` methods.
- New subsystem state without moved behavior does not count as shrinkage.
- New callbacks from subsystem code back into `Run` are migration debt.
- Behavior-preserving slices should stay small enough to validate and revert.
- Each PR should report deleted `Run.h` declarations and deleted `Run::`
  definitions.

## First Slices

1. Move launcher behavior into `RuntimeTools`.
   - Move ray-test lines, hit tests, laser fire, projectile fire, and launcher
     visual sample helpers out of `Run`.
   - Target files: `Runtime/Editor/LauncherTools.cpp`,
     `Runtime/Tools/RuntimeTools.*`.

2. Move editor behavior into an editor tool owner.
   - Move placement preview, gizmo drag, object placement, editor UI commands,
     save hotkeys, and editor overlay generation out of `Run`.
   - Target files: `Runtime/Editor/RunEditorTools.cpp`,
     `Runtime/Editor/EditorTools.*`, `Runtime/Tools/RuntimeTools.*`.

3. Move replay UI/tool behavior into `ReplayRuntime`.
   - Move scrubber input, cause tree rows, velocity edit, prediction jobs,
     focus mask building, and replay overlay construction out of `Run`.
   - Target files: `Runtime/Replay/RunReplayTools.cpp`,
     `Runtime/Replay/ReplayRuntime.*`.

4. Make scene loading owned by scene runtime code.
   - Stop using `SceneRuntimeCoordinator` as a callback shell around
     `Run::LoadScene`.
   - Move reset snapshot, UI override clearing, perf-log close, generated setup,
     authored setup, world/terrain setup, and scene advancement side effects.
   - Target files: `Runtime/Scene/RunScene.cpp`,
     `Runtime/Scene/SceneRuntimeCoordinator.*`.

5. Split `RuntimeRenderHost`.
   - Replace the wide render host with narrow render-facing views:
     world/models, replay overlay, tool overlay, UI, diagnostics.
   - This unblocks deleting render callbacks such as editor overlay and replay
     prediction ghost rendering from `Run`.

## Adjacent Architecture Plan

This plan covers architecture work from the broader engine assessment that is
not directly solved by shrinking `Run`. Keep these as separate implementation
slices. Do not mix them into the in-flight launcher extraction.

### 1. Make Physics Stores Authoritative

Problem: `PhysicsBodyStore`, `ColliderStore`, and `RenderInstanceStore` exist,
but physics stepping still takes `GameModelCollection&`.

Actions:

- Move body transform, velocity, mass, sleep, force, and impulse authority into
  `PhysicsBodyStore`.
- Move shape, restitution, drag, broadphase radius, and release metadata into
  `ColliderStore`.
- Change `PhysicsEngine::Step()` and `PhysicsWorld::RunPhysics()` to operate on
  stores and command buffers instead of `GameModelCollection&`.
- Keep compatibility writeback to `GameModel` only while render, replay, editor,
  and scene snapshot code still need it.
- Add or tighten the boundary check that blocks new physics-layer
  `GameModelCollection` dependencies.

Validation:

- `tools\validate_physics.bat`
- Add `tools\validate_perf.bat` for storage layout, broadphase, or hot-loop work.

### 2. Make Render Instances A Projection

Problem: production rendering still treats `GameModelCollection` as the render
scene view.

Actions:

- Make `RenderInstanceStore` the render-facing source for transforms, material
  intent, fixed-body feedback, visibility, and shadow participation.
- Move object, shadow, and DXR instance paths away from direct `GameModel`
  iteration.
- Keep temporary old/new projection comparison if it catches material,
  transform, or visibility drift.

Validation:

- `tools\validate_dx12_renderer.bat`
- Add `tools\validate_perf.bat` for object batching or instance upload changes.

### 3. Move One Real Pass Under `RenderGraph`

Problem: `RenderGraph` records pass/resource intent, but command recording still
lives outside the graph.

Actions:

- Add pass callback support to `RenderGraph`.
- Pick one low-risk first pass: a fullscreen, post, or diagnostic pass with no
  DXR and no swapchain ownership.
- Have the graph own barriers for that pass.
- Compare graph-owned barriers against existing live barrier diagnostics before
  expanding to scene, water, shadow, or present paths.

Validation:

- `tools\validate_dx12_renderer.bat`
- Verify `dx12_validation.txt` remains zero-error.

### 4. Split Renderer Capability Interfaces Under Pressure

Problem: `IRenderBackend` still exposes lifecycle, resources, capture, DXR, GPU
timers, debug lines, dynamic geometry, and instancing in one interface.

Actions:

- Keep `IRenderBackend` as the compatibility facade.
- Introduce narrow views only when a caller benefits immediately:
  capture/readback, GPU timers, debug draw, dynamic geometry, DXR reflection.
- Remove no-op optional methods only after callers use explicit capability
  queries or narrow views.

Validation:

- `tools\validate_dx12_renderer.bat`
- Use `tools\validate_full.bat` if runtime lifecycle, resize, or device reset
  behavior changes.

### 5. Mature Assets, Materials, And Water Ownership

Problem: `AssetSystem` owns source records, but material, mesh, GPU cache,
terrain, water, sky, and post ownership are still transitional.

Actions:

- Add material and mesh source records.
- Add cache invalidation and hot reload policy.
- Separate source asset lifetime from GPU resource lifetime.
- Move water render resources and material/style binding toward the water
  pass/material layer.
- Keep `WorldEnvironment` focused on world simulation data.

Validation:

- `tools\validate_dx12_renderer.bat`
- Use `tools\validate_full.bat` if scene load or runtime resource lifecycle
  changes.

### 6. Tighten Scene And Config Schemas

Problem: scene JSON is deterministic and useful, but many fields still use
handwritten parser bodies.

Actions:

- Add typed schema helpers for high-churn areas: objects, physics, cinematic,
  capture/logging, UI, and asset instances.
- Improve diagnostics so errors name the field, expected type/range, and source
  path.
- Keep scene/style/asset formats deterministic and snapshot-friendly.

Validation:

- `tools\validate_fast.bat` for parser-only cleanup.
- `tools\validate_full.bat` if scene load behavior can change.

### 7. Preserve Observability As Architecture

Problem: refactors are only safe because this repo has strong validation and
diagnostic contracts. Those contracts should grow with the boundaries.

Actions:

- Add a private-method-count ratchet for `Run.h`.
- Keep the physics `GameModelCollection` dependency allowlist shrinking.
- Block `RuntimeRenderHost` growth without an explicit allowlist update.
- Improve profiler reporting for unbucketed time and parent/child accounting.
- Keep SkullScope query output bounded and report data-size cost when used.

Validation:

- `tools\validate_fast.bat` for boundary/tooling checks.
- `tools\validate_perf.bat` for profiler accounting changes.

### Not Now

- Do not add worker parallelism until physics stores are authoritative.
- Do not introduce a broad ECS before body, collider, render, and scene identity
  are explicit.
- Do not restore GL or DX11 runtime paths.
- Do not combine baseline refreshes with cleanup refactors.

## Ratchet

Extend `tools/check_runtime_boundaries.py` so `Run.h` cannot grow in private
method count without an explicit allowlist update.

For each shrink slice, record:

- deleted `Run.h` declarations,
- deleted `Run::` definitions,
- new owner class/methods,
- validation command selected for the PR gate.

## Validation

Documentation-only updates need no validation. Implementation slices should use:

- launcher/editor/input/tool behavior: `tools\validate_full.bat`;
- physics impulses or body mutation: `tools\validate_physics.bat`;
- render host or overlay rendering: `tools\validate_dx12_renderer.bat`;
- broad uncertain slices: `tools\agent_validate.bat`.
