# Run Composition Root Shrink Plan

Date: 2026-06-26
Status: Active architecture cleanup plan; launcher slices validated
Impact area: runtime architecture, editor tools, replay tools, scene runtime, render host boundaries
Validation for this document-only change: none required

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
