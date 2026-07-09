# Progress: Demo Director (plan 08)

Source plan: `fable_plans/08-demo-director-plan.md`
Status: Phase 4 and closure implemented; commit-gate validation passed
Last updated: 2026-07-07

## How to work this file

- Do items in order; one checkbox = one verifiable action; tick only with the
  named evidence pasted under the box. `[B]` + reason if blocked twice.
- Anchors are file + search string; locate with `rg -n "<anchor>" <file>`.
- Comment quality gate applies to every touched source file. This is UI/render
  orchestration only — no physics; gate is `validate_dx12_renderer` unless a
  box says otherwise.

## Verified facts (do not re-derive)

- `RunCameraMode` enum: `Runtime/RuntimeCameraMode.h` (anchor
  `enum class RunCameraMode`) — values Demo, Scene, Inspect, Attach, Launcher,
  Manipulator, Director, Count.
- Camera state struct: `RunState.h` (anchor `struct RunCameraState`) — holds
  `mode`, `modeBeforeLauncher`, `modeBeforeAttach`, `cameraTime`, tracking
  fields. The "return to previous mode" pattern (`modeBeforeAttach`) is the
  grab/release model to imitate.
- Live style apply: `Runtime/Scene/SceneRuntimeStyle.cpp` —
  `ApplyLiveStyleScene(SceneRuntimeStyleContext, const TestScene&)` (anchor
  `void ApplyLiveStyleScene`). Style file -> `TestScene::LoadStyleFromFile`;
  `RunLiveStyle.cpp:260-261` shows the exact load-then-apply idiom to copy.
- Styles: `SkullbonezData/styles/*.style.json`, cinematic block keys
  (styleModes[], exposure, gamma, styleGrade[3], sun*, sky*, terrain*).
- Interaction automation: `RunInteractionAutomation.cpp` — action dispatch
  (anchor `clickReplayControl`), report/screenshot plumbing. New director
  actions plug in beside the existing ones.
- Reveal progress source: `RunReplayPredictionState` in `ReplayRuntime.h`
  (anchor `revealAnchor`); normalized progress = revealFrame / lastFrame from
  `ReplayPredictionRevealFrameIndex` (RunReplayPredictionHelpers.inl).

## Phase 0 — discovery (record answers inline; ~30 min)

- [x] D1. Free-fly camera pose fields. `rg -n "EnterFlyModeCamera|MoveCamera|Vector3.*[Cc]amera" SkullbonezSource/Runtime/RunInput.cpp`
  and inspect the camera object it drives. Record: the exact members that
  fully define a camera pose (eye position + orientation/target + fov if any),
  and the read/write calls to get/set them. THIS GATES the pose struct in P1.1.

  Evidence recorded 2026-07-07:
  - Exact command returned `RunInput.cpp:1518` (`EnterFlyModeCamera`),
    `RunInput.cpp:3272` (`MoveCamera`), and `RunInput.cpp:3316`
    (`GetCameraTranslation`).
  - A camera pose is the selected `Camera` slot's `m_position`, `m_view`, and
    `m_upVector` (`Camera.h:67-69`). There is no per-camera FOV field; projection
    lives outside `CameraCollection`.
  - Capture calls: `CameraCollection::GetCameraTranslation()`,
    `GetCameraView()`, and `GetCameraUp()` (`CameraCollection.cpp:327`,
    `:514`, `:520`).
  - Write calls: `SetPrimaryPose(position, view, up)` for an immediate selected
    slot update, `TweenPrimaryToPose(position, view, up)` for a smooth return,
    and `OverrideRenderCameraForFrame(position, view, up)` when the director
    needs render-only pose injection (`CameraCollection.cpp:264`, `:270`,
    `:474`).
- [x] D2. Camera apply point. Find where the active view matrix is built each
  frame (`rg -n "LookAt|BuildView|viewMatrix|SetCamera" SkullbonezSource/Runtime`).
  Record the single function the director must feed a pose into so a phase pose
  becomes the rendered view.

  Evidence recorded 2026-07-07:
  - Exact command found the frame apply point at `RunRender.cpp:2049`, which
    calls `m_systems.cameras->SetCamera()`.
  - `CameraCollection::SetCamera()` selects the current camera or tween camera
    and calls `SetViewMatrix(...)`; `SetViewMatrix` writes `m_renderCamera` and
    builds `m_currentViewMatrix = Matrix4::LookAt(position, view, up)`
    (`CameraCollection.cpp:420-484`).
  - Director playback can therefore either set/tween the selected primary pose
    before `SetCamera()`, or call `OverrideRenderCameraForFrame()` after
    `SetCamera()` for per-frame override. The lower-risk first implementation
    should use `SetPrimaryPose`/`TweenPrimaryToPose` so existing view-matrix,
    render-camera, and audio-listener readers stay on the normal path.
- [x] D3. Style-apply cost. Confirm the live-style load/apply idiom is safe to call on
  a phase change mid-run (RunLiveStyle already calls it per changed file).
  Record whether it reloads GPU resources (expensive) or just sets config
  (cheap) — if expensive, P3 caches TestScene per phase at load.

  Evidence recorded 2026-07-07:
  - Exact style search found the live-style harness at `RunLiveStyle.cpp:254-269`:
    on file-stamp change it loads `TestScene::LoadStyleFromFile(...)`, calls
    `ApplyLiveStyleScene(SceneRuntimeStyleContext{...}, styleScene)`, and bumps
    `styleApplyCount`.
  - `ApplyLiveStyleScene` does not reload the scene. It resets/apply object
    materials, copies cinematic overrides into `activeCinematic`, mirrors scene
    cinematic flags when in scene mode, and clears the scene-browser look index
    (`SceneRuntimeStyle.cpp:298-318`).
  - Cost is CPU-side JSON parse plus an object-material pass over live models;
    it is appropriate on phase entry, not per-frame. No GPU-resource preload is
    required for the first director slice unless profiling later proves the JSON
    parse is visible.
- [x] D4. Mode-transition cleanup. Read `ApplyCameraMode`
  (RunInput.cpp:1727) + `ClearRuntimeInteractionStateForTransition`
  (RunInput.cpp:906). Record what must run when entering/leaving Director mode
  so grab/release does not leave stale interaction state.

  Evidence recorded 2026-07-07:
  - `ApplyCameraMode` normalizes the mode, derives a
    `RuntimeInteractionTransition` with `EnterInteractionForCameraMode`, runs
    `ApplyRuntimeInteractionTransitionCleanup`, updates camera-mode return
    state, cancels manipulator pickup when needed, and calls
    `EnterFlyModeCamera`/`ExitFlyModeCamera` only when fly ownership changes
    (`RunInput.cpp:1341-1421`).
  - `ClearRuntimeInteractionStateForTransition` clears replay drag state,
    manipulator pickup, editor interaction, and editor mode when the destination
    workspace/owner no longer owns them (`RunInput.cpp:631-668`).
  - `RuntimeInteractionController::EnterCameraMode` must gain an explicit
    `Director` case that maps to `EnterLive()` unless a later UI decision wants
    a new owner. Director's normal playback should not claim fly controls.
    Grab/release needs a separate runtime-state bit: on grab seed/select the
    free camera from the current director pose, call `EnterFlyModeCamera`, reset
    mouse look/cursor ownership, and allow `BuildRuntimeInputModeState` to see
    fly controls while grabbed; on release call `ExitFlyModeCamera` and tween
    back to the phase pose.

## Phase 1 — data model + loader

- [x] P1.1 Add `Runtime/DemoDirector.h` with plain-data structs (no
  inheritance, fixed-capacity per the allocation gate):
  ```cpp
  // Concept: a phase is one authored shot — a hand-placed camera pose plus a
  // render style, held until an advance rule fires. Poses are operator-owned;
  // the director only blends and times between them.
  enum class PhaseAdvance { Manual, Timer, RevealAtLeast };
  struct DemoPhase
  {
      char name[48] = {};
      // pose fields per D1 (e.g. Vector3 eye; Quaternion orientation; float fov)
      char stylePath[192] = {};        // SkullbonezData/styles/*.style.json
      PhaseAdvance advance = PhaseAdvance::Manual;
      float timerSeconds = 4.0f;       // for Timer
      float revealThreshold = 1.0f;    // for RevealAtLeast (0..1)
      float blendInSeconds = 1.0f;     // pose+grade lerp on entry
      float revealRate = 1.0f;         // optional per-phase REVEAL_SECONDS_PER_SECOND override
  };
  struct DemoShotList
  {
      static constexpr int MAX_PHASES = 32;
      std::array<DemoPhase, MAX_PHASES> phases = {};
      int phaseCount = 0;
  };
  ```
  Evidence recorded 2026-07-07:
  - Added `SkullbonezSource/Runtime/DemoDirector.h` with fixed-capacity
    `DemoShotList::MAX_PHASES = 32`, `DemoPhase`, `DemoCameraPose`, and
    `PhaseAdvance`.
  - Pose fields follow D1 exactly: eye/view/up `Vector3` values, no FOV field.
  - Added `loop` on `DemoShotList` now because phase 4 already names the
    hold-vs-loop behavior.
- [x] P1.2 JSON load/save beside the style loader (reuse the repo's JSON path
  used by `TestSceneParser`/style loading — do NOT add a new JSON lib).
  `LoadDemoShotList(path, DemoShotList&) -> bool` (Lane R style: bool + logged
  reason, no throw). Save writes the same schema. File location convention:
  `SkullbonezData/shots/*.shot.json`.
  Evidence: round-trip a hand-written 2-phase file, load→save→load equal.
  Gate: `validate_fast`. Commit.

## Phase 2 — Director camera mode + grab/release

  Evidence recorded 2026-07-07:
  - Added `SkullbonezSource/Runtime/DemoDirector.cpp` using the same
    `nlohmann::ordered_json` dependency as `TestSceneParser` and
    `RuntimeFileWriter::OpenTextFile` for saves.
  - Loader fills a temporary shot list and assigns `outShotList` only after the
    whole document is valid; bad input logs `[demo-director] ...` and returns
    false.
  - Added `SkullbonezTests/TestDemoDirector.cpp` with a hand-written 2-phase
    `.shot.json` fixture, save/load round-trip checks, and malformed-input
    false-return coverage.
  - Updated `SKULLBONEZ_CORE.vcxproj`, `SKULLBONEZ_TESTS.vcxproj`, filters, and
    `tools/validate_project_filters.py` so the new runtime files are covered by
    project metadata validation.
  - Gate: `tools\validate_fast.bat` passed on 2026-07-07 in 41.311s. Key lines:
    format passed; project filters passed; staged file size check passed;
    runtime boundaries passed; Profile and Debug builds succeeded with 0
    warnings/0 errors; `SKULLBONEZ_TESTS` reported 44 test cases and 574
    assertions passed.

- [x] P2.1 Add `RunCameraMode::Director` to the enum (before `Count`); update
  every exhaustive switch the compiler flags (that is the checklist — build and
  fix each `-W4` switch warning). Add label in `CameraModeLabel`
  (RunInput.cpp:1081). Gate: build 0/0.
  Evidence recorded 2026-07-07:
  - Added `RunCameraMode::Director` before `Count` and documented it in
    `RuntimeCameraMode.h`.
  - Wired Director through `CameraModeLabel`, `CameraModeName`,
    `TryParseCameraMode`, `CameraModeEnabledMask`, and the compact UI camera
    mode option table/default mask.
  - Mapped Director camera transitions to `RuntimeInteractionController::EnterLive`
    so it uses Live/None ownership while later director playback owns camera
    math explicitly.
  - Kept Director out of fly/launcher/manipulator routing; classified it as
    manual for generated-demo camera suppression and hid the runtime manual-mode
    badge so it does not advertise free-fly controls before grab/release exists.
  - Comment-quality audit scope:
    `RuntimeCameraMode.h`, `RunInput.cpp`, `RunInteractionAutomation.cpp`,
    `RuntimeInteractionController.cpp`, `RunUiTextPass.cpp`, `UI.cpp`, and
    `UI.h`. All touched files had learning headers; added a local invariant for
    the UI option table/enum-order contract.
  - Gate: `tools\validate_fast.bat` passed on 2026-07-07 in 45.504s. Log:
    `Agentic\Reports\2026-07-07\logs\fable-08-director-camera-mode-validate-fast.log`.
    Key lines: format passed; project filters passed; staged file size check
    passed; runtime boundaries passed; Profile and Debug builds succeeded with
    0 warnings/0 errors; `SKULLBONEZ_TESTS` reported 44 test cases and 574
    assertions passed.
- [x] P2.2 Director runtime state on the Run side (fixed, in RunState.h near
  RunCameraState): active shot list, current phase index, phase elapsed time,
  blend timer, and a `grabbed` bool + the free-fly pose captured at grab.
  Evidence recorded 2026-07-07:
  - Added `DemoDirectorPlaybackState` in `RunState.h` with fixed
    `DemoShotList activeShotList`, `hasActiveShotList`, `currentPhaseIndex`,
    `phaseElapsedSeconds`, `blendElapsedSeconds`, `blendStartPose`, `grabbed`,
    and `poseCapturedAtGrab`.
  - Nested the state as `RunCameraState::director` so Director playback extends
    the existing camera state shelf rather than increasing `Run`'s top-level
    private member count.
  - Initial `tools\validate_full.bat` caught the private-member ratchet when the
    state was briefly placed as a new `Run` member (`Run.h` found 42, max 41);
    the final diff keeps `Run.h` unchanged and passes the boundary checker.
  - Comment-quality audit scope: `RunState.h` and `Run.h`. `RunState.h` gained
    a `Director playback` glossary entry and a local concept comment for the
    fixed-capacity playback state.
  - Gate: `tools\validate_full.bat` passed on 2026-07-07 in 54.335s. Log:
    `Agentic\Reports\2026-07-07\logs\fable-08-director-runtime-state-validate-full.log`.
    Key lines: project filters passed; runtime boundaries passed; Profile and
    Debug builds succeeded with 0 warnings/0 errors; DX12 InfoQueue reported 0
    validation errors; DX12 screenshots matched committed baselines;
    `physics_regression_solver.csv` matched byte-exactly; `VALIDATE_FULL:
    DEFAULT GATE PASSED`.
- [x] P2.3 Per-frame director tick (new `RunDemoDirector.cpp`, `Run::` methods,
  called from the frame update near the other camera ticks — see
  `TickAttachedCamera` call site for where): if not grabbed, drive the rendered
  camera (D2) from the current phase pose, lerping over `blendInSeconds` on
  phase entry; apply the phase style once on entry (P3). Evidence: a 2-phase
  shot list visibly cuts camera + look between two poses.
  Evidence recorded 2026-07-07:
  - Added `SkullbonezSource/Runtime/RunDemoDirector.h/.cpp` as a narrow
    presentation-only helper module. The helper takes `RunCameraState` and
    `RunSubsystemState` explicitly, so `Run.h` does not grow new private
    methods and the runtime-boundary ratchet remains green.
  - `RunFrame.cpp` now calls `DemoDirectorPlayback::Tick(...)` immediately
    after `TickAttachedCamera()`, before render-side camera matrix setup.
    Director playback writes through `CameraCollection::SetPrimaryPose(...)`
    and is a no-op unless Director mode has a loaded non-empty shot list.
  - `RunInput.cpp` seeds Director blend/timer state when entering Director
    mode, and `RunInteractionAutomation.cpp` gained the proof-oriented
    `loadShotList` and `directorAdvance` verbs plus final report fields:
    `directorShotListLoaded`, `directorPhaseIndex`, and
    `directorPhaseCount`. The remaining P4.2 verbs stay open.
  - Added `RunDemoDirector` to `SKULLBONEZ_CORE.vcxproj`/filters and to
    `tools/validate_project_filters.py` so new source/header entries are
    covered by project metadata validation.
  - Comment-quality audit scope:
    `RunDemoDirector.h`, `RunDemoDirector.cpp`, `RunFrame.cpp`,
    `RunInput.cpp`, `RunInteractionAutomation.cpp`, `RunState.h`, and
    `tools/validate_project_filters.py`. All touched source-bearing files have
    learning headers; `RunInteractionAutomation.cpp` now describes interaction
    scripts rather than only world-click scripts.
  - `python tools\validate_project_filters.py` passed on 2026-07-07 in
    1.026s with 0 errors and 558 project/filter items.
  - Gate: `tools\validate_fast.bat` passed on 2026-07-07 in 45.866s. Log:
    `Agentic\Reports\2026-07-07\logs\fable-08-director-tick-validate-fast.log`.
    Key lines: formatting passed; project filters passed; staged file size
    check passed; runtime boundaries passed; Profile and Debug builds
    succeeded with 0 warnings/0 errors; unit tests passed.
  - Gate: `tools\validate_full.bat` passed on 2026-07-07 in 42.632s. Log:
    `Agentic\Reports\2026-07-07\logs\fable-08-director-tick-validate-full.log`.
    Key lines: project filters/runtime boundaries passed; Profile and Debug
    builds succeeded with 0 warnings/0 errors; DX12 InfoQueue reported 0
    validation errors; DX12 screenshots matched committed baselines;
    `physics_regression_solver.csv` matched byte-exactly; `VALIDATE_FULL:
    DEFAULT GATE PASSED`.
  - Runtime proof: `Profile\SKULLBONEZ_CORE.exe --scene
    SkullbonezData\scenes\interaction_inspect_gizmo_harness.scene.json
    --interaction-script Agentic\Temp\director_p2_3_interaction.json
    --interaction-report TestOutput\interaction\director_p2_3_report.json
    --frames 40 --vsync off` passed in 9.449s. Report final state:
    `cameraMode=Director`, `directorShotListLoaded=true`,
    `directorPhaseIndex=1`, `directorPhaseCount=2`; screenshot:
    `TestOutput\interaction\director_p2_3_after_advance.bmp`.
- [x] P2.4 GRAB: a key (pick an unused one; record it) sets `grabbed=true`,
  seeds the free-fly camera with the *current authored pose* (no jump), and
  routes input to free-fly. RELEASE: same key toggles `grabbed=false` and
  blends the rendered camera from the current free-fly pose back to the phase
  pose over `blendInSeconds`. Model this on the `modeBeforeAttach`
  save/restore pattern. Evidence: grab → fly around → release → smooth return,
  no pop, recorded via a short screen capture or eyeballed on launch.
  Evidence recorded 2026-07-07:
  - Picked `B` as the Director grab/release key; it was unused in the runtime
    keyboard action map.
  - `DemoDirectorPlayback::BeginGrab(...)` captures the visible Director pose,
    seeds the primary camera pose, resets blend timing, and marks
    `RunCameraState::director.grabbed=true`.
  - `DemoDirectorPlayback::EndGrab(...)` captures the operator's free-fly pose
    as `blendStartPose`, clears `grabbed`, and lets the existing Director tick
    blend back to the active phase pose over that phase's `blendInSeconds`.
  - `RunInput.cpp` routes Director mode through free-fly controls only while
    grabbed, calls `EnterFlyModeCamera()` on grab and `ExitFlyModeCamera()` on
    release, and keeps the HUD camera mode label as Director throughout.
  - `RunInteractionAutomation.cpp` gained `directorGrabbed` assertions/final
    report state and expanded `pressKey` parsing to support single
    alphanumeric keys (`B`, `W`, etc.) for scripted proof.
  - Comment-quality audit scope:
    `RunDemoDirector.h`, `RunDemoDirector.cpp`, `RunInput.cpp`,
    `InputController.h`, `InputController.cpp`,
    `RunInteractionAutomation.cpp`, and `RunState.h`. All touched
    source-bearing files have learning headers; added local `Why:` guidance for
    alphanumeric automation key parsing and a Director grab glossary entry.
  - Targeted build: `tools\validate_build.bat Profile` passed twice, first in
    8.43s and after the automation parser fix in 7.42s, both with 0 warnings
    and 0 errors. Logs:
    `Agentic\Reports\2026-07-07\logs\fable-08-director-grab-release-build-profile.log`
    and
    `Agentic\Reports\2026-07-07\logs\fable-08-director-grab-release-build-profile-rerun.log`.
  - Runtime proof: `Profile\SKULLBONEZ_CORE.exe --scene
    SkullbonezData\scenes\interaction_inspect_gizmo_harness.scene.json
    --interaction-script Agentic\Temp\director_p2_4_interaction.json
    --interaction-report TestOutput\interaction\director_p2_4_report.json
    --frames 45 --vsync off` passed in 10.231s after the parser fix. Report:
    `ok=true`, `cameraMode=Director`, `directorGrabbed=true` at frame 12,
    three `W` nudges consumed while grabbed, `directorGrabbed=false` at frame
    22, final state `directorShotListLoaded=true`, `directorPhaseIndex=0`,
    `directorPhaseCount=2`. Screenshots:
    `TestOutput\interaction\director_p2_4_grabbed.bmp` and
    `TestOutput\interaction\director_p2_4_released.bmp`. Log:
    `Agentic\Reports\2026-07-07\logs\fable-08-director-grab-release-interaction-proof-rerun.log`.
  - Gate: `tools\validate_full.bat` passed on 2026-07-07 in 46.698s. Log:
    `Agentic\Reports\2026-07-07\logs\fable-08-director-grab-release-validate-full.log`.
    Key lines: project filters/runtime boundaries passed; Profile and Debug
    builds succeeded with 0 warnings/0 errors; DX12 InfoQueue reported 0
    validation errors; DX12 screenshots matched committed baselines;
    `physics_regression_solver.csv` matched byte-exactly; `VALIDATE_FULL:
    DEFAULT GATE PASSED`.
- [x] P2.5 "Set pose here": while grabbed (or in any fly mode), a key writes
  the current camera pose into the selected phase; another key steps the
  selected phase; another saves the shot list (P1.2). Evidence: author a pose
  live, save, reload, pose matches. Gate: `validate_dx12_renderer`. Commit.
  Evidence recorded 2026-07-07:
  - Picked `J` for "set pose here", `K` for selected-phase step, and `L` for
    save. These keys were unused in the central runtime key map.
  - Added fixed `activeShotListPath` storage to Director playback state so the
    loaded `.shot.json` can be saved back through `SaveDemoShotList(...)`
    without adding a new `Run` private member.
  - Added `DemoDirectorPlayback::SetCurrentPhasePose(...)`,
    `SelectNextPhaseForAuthoring(...)`, and `SaveShotList(...)`. The selected
    phase remains `currentPhaseIndex`; authoring phase step cycles for editing
    without changing the existing non-looping playback advance semantics.
  - `RunInput.cpp` wires `J/K/L` through normal `RuntimeInputAction` edge
    detection. Authoring keys are inert when no playable shot list/current phase
    is available.
  - `RunInteractionAutomation.cpp` now reports selected Director phase name,
    path, and camera pose fields. It also gained a proof-oriented
    `setCameraPose` action that seeds the current camera pose; the authored
    write and save still happen through the real `J` and `L` key paths.
  - Comment-quality audit scope:
    `RunDemoDirector.h`, `RunDemoDirector.cpp`, `RunInput.cpp`,
    `InputController.h`, `InputController.cpp`,
    `RunInteractionAutomation.cpp`, and `RunState.h`. All touched
    source-bearing files have learning headers; new local comments explain
    cold authoring keys, loaded-path save ownership, and automation camera-pose
    seeding.
  - Targeted builds: `tools\validate_build.bat Profile` passed twice, first in
    8.20s and after the `setCameraPose` proof hook in 8.14s, both with 0
    warnings and 0 errors. Logs:
    `Agentic\Reports\2026-07-07\logs\fable-08-director-pose-authoring-build-profile.log`
    and
    `Agentic\Reports\2026-07-07\logs\fable-08-director-pose-authoring-build-profile-rerun.log`.
  - Runtime proof author/save: `Profile\SKULLBONEZ_CORE.exe --scene
    SkullbonezData\scenes\interaction_inspect_gizmo_harness.scene.json
    --interaction-script Agentic\Temp\director_p2_5_author_interaction.json
    --interaction-report TestOutput\interaction\director_p2_5_author_report.json
    --frames 55 --vsync off` passed in 10.591s. The report shows
    `B` grabbed Director, `setCameraPose` seeded the current pose, `J` captured
    phase 0, `L` saved the shot list, `K` selected phase 1, and screenshot
    `TestOutput\interaction\director_p2_5_author_saved.bmp` was written. Log:
    `Agentic\Reports\2026-07-07\logs\fable-08-director-pose-authoring-proof-author-rerun.log`.
  - Runtime proof reload: `Profile\SKULLBONEZ_CORE.exe --scene
    SkullbonezData\scenes\interaction_inspect_gizmo_harness.scene.json
    --interaction-script Agentic\Temp\director_p2_5_reload_interaction.json
    --interaction-report TestOutput\interaction\director_p2_5_reload_report.json
    --frames 35 --vsync off` passed in 9.395s. Reload report final state:
    `cameraMode=Director`, `directorShotListLoaded=true`,
    `directorPhaseIndex=0`, `directorPhaseName=author-wide`,
    `directorPhaseCameraEye=[470.0,112.0,575.0]`, and
    `directorPhaseCameraView=[500.0,84.0,505.0]`. A PowerShell JSON comparison
    confirmed the saved file's phase 0 position/view exactly matched the reload
    report (`poseMatch=True`, `viewMatch=True`). Screenshot:
    `TestOutput\interaction\director_p2_5_reloaded_pose.bmp`. Log:
    `Agentic\Reports\2026-07-07\logs\fable-08-director-pose-authoring-proof-reload.log`.
  - Gate: `tools\validate_full.bat` passed on 2026-07-07 in 47.012s. Log:
    `Agentic\Reports\2026-07-07\logs\fable-08-director-pose-authoring-validate-full.log`.
    Key lines: project filters/runtime boundaries passed; Profile and Debug
    builds succeeded with 0 warnings/0 errors; DX12 InfoQueue reported 0
    validation errors; DX12 screenshots matched committed baselines;
    `physics_regression_solver.csv` matched byte-exactly; `VALIDATE_FULL:
    DEFAULT GATE PASSED`.

## Phase 3 — render type (style) per phase

- [x] P3.1 On phase entry, copy the live-style idiom from
  `RunLiveStyle.cpp:260-261`: `TestScene::LoadStyleFromFile(phase.stylePath,
  m_systems.assets)` followed by `ApplyLiveStyleScene(context, styleScene)`.
  Build the `SceneRuntimeStyleContext` the same way Run already does for the
  live-style harness. If D3 said apply is expensive, preload each phase's
  `TestScene` into the shot list at load and call `ApplyLiveStyleScene` with
  the cached scene.
  Evidence recorded 2026-07-07:
  - `DemoDirectorPlayback::Tick(...)` now accepts the existing
    `SceneRuntimeStyleContext`, loads the active phase `stylePath` with
    `TestScene::LoadStyleFromFile(..., styleContext.assets)`, and applies it
    through `ApplyLiveStyleScene(...)`.
  - `DemoDirectorPlaybackState` records `appliedStylePhaseIndex`,
    `appliedStylePath`, and `appliedStyleCount`. The phase index plus exact path
    keeps style JSON out of the per-frame blend while still allowing same-phase
    authoring edits to request a new look.
  - `RunFrame.cpp` passes the same style context shape used by the live-style
    harness and adds a no-physics scene fallback: static authored scenes can
    skip `UpdateLogic`, but Director phase style/camera entry still needs to
    tick.
  - `RunInteractionAutomation.cpp` report final state now includes
    `directorPhaseStylePath`, `directorAppliedStylePhaseIndex`,
    `directorAppliedStylePath`, and `directorAppliedStyleCount`.
  - Runtime proof: `Profile\SKULLBONEZ_CORE.exe --scene
    SkullbonezData\scenes\interaction_inspect_gizmo_harness.scene.json
    --interaction-script Agentic\Temp\director_p3_1_style_interaction.json
    --interaction-report TestOutput\interaction\director_p3_1_style_report.json
    --frames 45 --vsync off` passed in 11.437s. The log contains
    `[demo-director] applied style SkullbonezData/styles/neon_cyberpunk.style.json
    for phase 0 (style-entry-neon)`, and the JSON proof confirmed
    `directorAppliedStyleCount=1`, `directorAppliedStylePhaseIndex=0`,
    `directorAppliedStylePath=SkullbonezData/styles/neon_cyberpunk.style.json`,
    and matching `directorPhaseStylePath`. Screenshot:
    `TestOutput\interaction\director_p3_1_style_entry.bmp`. Log:
    `Agentic\Reports\2026-07-07\logs\fable-08-p3-1-style-interaction-rerun.log`.
  - Comment-quality audit scope:
    `RunDemoDirector.h`, `RunDemoDirector.cpp`, `RunFrame.cpp`,
    `RunInteractionAutomation.cpp`, and `RunState.h`. All touched
    source-bearing files have learning headers; new or refreshed comments name
    phase style ownership, cold phase-entry JSON work, and the no-physics scene
    presentation fallback. Checklist path: N/A for touched-file pass; checked
    count 5, deferred count 0, unchecked files none.
  - Targeted builds: `tools\validate_build.bat Profile` passed in 8.844s before
    the no-physics fallback and 5.868s after it, both with 0 warnings and 0
    errors. Logs:
    `Agentic\Reports\2026-07-07\logs\fable-08-p3-1-build-profile.log` and
    `Agentic\Reports\2026-07-07\logs\fable-08-p3-1-build-profile-rerun.log`.
  - Formatting: `tools\validate_format.bat` passed in 8.398s after targeted
    clang-format/header-post-pass on touched source files. Log:
    `Agentic\Reports\2026-07-07\logs\fable-08-p3-1-validate-format-rerun.log`.
  - Gate: `tools\validate_full.bat` passed on 2026-07-07 in 55.619s. Log:
    `Agentic\Reports\2026-07-07\logs\fable-08-p3-1-validate-full-rerun.log`.
    Key lines: project filters/runtime boundaries passed; Profile and Debug
    builds succeeded with 0 warnings/0 errors; DX12 InfoQueue reported 0
    validation errors; DX12 screenshots matched committed baselines;
    `physics_regression_solver.csv` matched byte-exactly; `VALIDATE_FULL:
    DEFAULT GATE PASSED`.
- [x] P3.2 Phase-entry also sets the prediction reveal rate to
  `phase.revealRate` (write `REPLAY_PREDICTION_REVEAL_SECONDS_PER_SECOND`'s
  runtime equivalent — if it is currently a constexpr constant in
  RunReplayTools.cpp, promote it to a runtime field on
  `RunReplayPredictionState` first, defaulting to 1.0, so the director can
  slow the unfold for the money shot). Gate: `validate_dx12_renderer` +
  `prediction_ragdoll_wall_200_predict` proof (reveal still works). Commit.
  Evidence recorded 2026-07-07:
  - Promoted reveal pacing from the old
    `REPLAY_PREDICTION_REVEAL_SECONDS_PER_SECOND` constant to
    `RunReplayPredictionState::revealSecondsPerSecond`, defaulting to 1.0.
  - `ReplayPredictionRevealFrameIndex(...)` now reads the runtime rate through
    a non-positive fallback helper so malformed shot-list data cannot freeze
    the reveal cursor or divide by zero while prediction builds catch up.
  - `DemoDirectorPlayback::Tick(...)` now receives
    `RunReplayPredictionState&` from both frame paths and applies the active
    phase `revealRate` once per phase entry. Re-anchoring preserves already
    revealed prediction seconds, so slowing a money-shot phase changes future
    pacing without snapping the causal tree backward.
  - `RunInteractionAutomation.cpp` reports `directorPhaseRevealRate`,
    `directorAppliedRevealRatePhaseIndex`, `directorAppliedRevealRate`,
    `directorAppliedRevealRateCount`, and
    `predictionRevealSecondsPerSecond` for proof scripts.
  - Runtime proof: `Profile\SKULLBONEZ_CORE.exe --scene
    SkullbonezData\scenes\interaction_inspect_gizmo_harness.scene.json
    --interaction-script Agentic\Temp\director_p3_2_reveal_rate_interaction.json
    --interaction-report TestOutput\interaction\director_p3_2_reveal_rate_report.json
    --frames 35 --vsync off` passed with report `ok=true`,
    `directorAppliedRevealRate=0.25`, `directorAppliedRevealRateCount=1`, and
    `predictionRevealSecondsPerSecond=0.25`. Screenshot:
    `TestOutput\interaction\director_p3_2_reveal_rate.bmp`. Log:
    `Agentic\Reports\2026-07-07\logs\fable-08-p3-2-reveal-rate-interaction.log`.
  - Prediction reveal proof: `Profile\SKULLBONEZ_CORE.exe --scene
    SkullbonezData\scenes\prediction_ragdoll_wall_200.scene.json
    --interaction-script SkullbonezData\interaction\prediction_ragdoll_wall_200_predict.json
    --interaction-report TestOutput\interaction\prediction_ragdoll_wall_200_predict_report.json
    --frames 220 --replay on --replay-seconds 2 --fixed-step --vsync off
    --allocation-guard gameplay` passed with report `ok=true`,
    `predictionPathVisible=true`, `liveSolverHashStableAcrossPrediction=true`,
    `predictionRevealSecondsPerSecond=1.0`, and allocation guard
    `gameplay_violations=0`. Screenshot:
    `TestOutput\interaction\prediction_ragdoll_wall_200_predict.bmp`. Log:
    `Agentic\Reports\2026-07-07\logs\fable-08-p3-2-prediction-ragdoll-proof.log`.
  - Comment-quality audit scope: `ReplayRuntime.h`,
    `RunReplayPredictionHelpers.inl`, `RunReplayTools.cpp`,
    `RunDemoDirector.h`, `RunDemoDirector.cpp`, `RunFrame.cpp`,
    `RunInteractionAutomation.cpp`, and `RunState.h`. Checklist path: N/A for
    touched-file pass; checked count 8, deferred count 0, unchecked files none.
  - Gate: `tools\validate_fast.bat` passed after a targeted
    `RunDemoDirector.cpp` clang-format fix. Log:
    `Agentic\Reports\2026-07-07\logs\fable-08-p3-2-validate-fast.log`. Key
    lines: source formatting passed, project filters passed, staged file size
    check passed, runtime boundaries passed, unit tests passed, and
    Profile/Debug builds succeeded with 0 warnings/0 errors.
  - Gate: `tools\validate_dx12_renderer.bat` passed. Log:
    `Agentic\Reports\2026-07-07\logs\fable-08-p3-2-validate-dx12-renderer.log`.
    Key lines: DX12 InfoQueue reported 0 validation errors, DX12 screenshots
    matched committed baselines, and `VALIDATE_DX12_RENDERER: ALL PASSED`.
  - Gate: `tools\validate_full.bat` passed. Log:
    `Agentic\Reports\2026-07-07\logs\fable-08-p3-2-validate-full.log`. Key
    lines: runtime boundaries passed, Profile and Debug builds succeeded with
    0 warnings/0 errors, DX12 validation errors 0, screenshots matched,
    `physics_regression_solver.csv` matched byte-exactly, and `VALIDATE_FULL:
    DEFAULT GATE PASSED`.

## Phase 4 — advance rules + automation + the demo shot list

- [x] P4.1 Implement the three advance rules in the director tick: Manual
  (advance key), Timer (`phase elapsed >= timerSeconds`), RevealAtLeast
  (normalized reveal progress >= `revealThreshold` — read the reveal
  frame/lastFrame per the verified-facts source). On advance past the last
  phase: hold (do not loop) unless a shot-list `loop` flag says otherwise.
- [x] P4.2 Automation actions in `RunInteractionAutomation.cpp`:
  `loadShotList`, `directorPlay`, `directorAdvance`, `directorGrab`,
  `directorRelease`, `setPhaseStyle` — so a scripted take is reproducible and
  screenshots can be pinned to phases. Evidence: a `*.json` interaction script
  that loads the shot list, plays it, and screenshots each phase; report
  `ok=1`.
- [x] P4.3 Author `SkullbonezData/shots/butterfly.shot.json` for the 200-brick
  scene: e.g. phase 1 low behind the ball sighting the root line (dark style,
  RevealAtLeast 0.05), phase 2 dolly-up three-quarter as the wall blooms
  (RevealAtLeast 0.5, revealRate 0.5), phase 3 wide settle on the grey rest
  boxes (Timer). Poses hand-authored via P2.5. Evidence: play it start to
  finish on the real scene; attach a screenshot per phase.
  Gate: `validate_dx12_renderer`. Commit.

## Closure

- [x] Z1. Document the director keys + `.shot.json` schema in
  `Agentic/Reference/runtime-reference.md`.
- [x] Z2. Update `fable_plans/08-demo-director-plan.md` status + this file.

## Phase 4 evidence

Evidence recorded 2026-07-07:

P4.1:
- `RunDemoDirector.cpp` now evaluates `Manual`, `Timer`, and `RevealAtLeast`
  in `DemoDirectorPlayback::Tick`; reveal advance requires an active prediction
  frame range and uses revealed frame / last frame.
- Timer/hold/loop proof command:
  `Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\interaction_inspect_gizmo_harness.scene.json --interaction-script Agentic\Temp\director_p4_1_timer_loop_interaction.json --interaction-report TestOutput\interaction\director_p4_1_timer_loop_report.json --frames 60 --vsync off`
- Report `TestOutput\interaction\director_p4_1_timer_loop_report.json`:
  `ok=true`; timer advanced to `directorPhaseIndex=1`, non-loop final phase
  held at index 1 through frame 28, and a looped shot list wrapped back to
  `directorPhaseIndex=0` after a second `directorAdvance`.
- Screenshots: `TestOutput\interaction\director_p4_1_timer_hold.bmp` and
  `TestOutput\interaction\director_p4_1_loop_wrap.bmp`.
- Log:
  `Agentic\Reports\2026-07-07\logs\fable-08-p4-1-timer-loop-interaction.log`.

P4.2:
- `RunInteractionAutomation.cpp` now parses and reports `directorPlay`,
  `directorGrab`, `directorRelease`, and `setPhaseStyle`, plus phase
  assertions `directorPhaseIndex`, `directorPhaseName`, and
  `directorPhaseStylePath`.
- Automation proof command:
  `Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\interaction_inspect_gizmo_harness.scene.json --interaction-script Agentic\Temp\director_p4_2_automation_interaction.json --interaction-report TestOutput\interaction\director_p4_2_automation_report.json --frames 40 --vsync off`
- Report `TestOutput\interaction\director_p4_2_automation_report.json`:
  `ok=true`; camera mode was `Director`, `directorGrabbed` asserted true then
  false, and `directorPhaseStylePath` asserted
  `SkullbonezData/styles/neon_cyberpunk.style.json`.
- Screenshot: `TestOutput\interaction\director_p4_2_automation.bmp`.
- Log:
  `Agentic\Reports\2026-07-07\logs\fable-08-p4-2-automation-interaction.log`.

P4.3:
- Authored `SkullbonezData/shots/butterfly.shot.json` with three phases:
  `root-sighting` (`RevealAtLeast`, storm-front style, revealRate 0.30),
  `chain-bloom` (`RevealAtLeast`, neon-cyberpunk style, revealRate 0.55), and
  `settle-rest` (`Timer`, golden-hour style).
- Butterfly take proof command:
  `Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\prediction_ragdoll_wall_200.scene.json --interaction-script Agentic\Temp\butterfly_shot_take_interaction.json --interaction-report TestOutput\interaction\butterfly_shot_take_report.json --frames 430 --replay on --replay-seconds 2 --fixed-step --vsync off`
- Report `TestOutput\interaction\butterfly_shot_take_report.json`:
  `ok=true`; prediction target `prediction_striker_ball`,
  `predictionPathVisible=true`, `liveSolverHashStableAcrossPrediction=true`,
  and phase assertions reached indices 0/1/2 with names `root-sighting`,
  `chain-bloom`, and `settle-rest`.
- Screenshots: `TestOutput\interaction\butterfly_phase_0_root_sighting.bmp`,
  `TestOutput\interaction\butterfly_phase_1_chain_bloom.bmp`, and
  `TestOutput\interaction\butterfly_phase_2_settle_rest.bmp`.
- Log:
  `Agentic\Reports\2026-07-07\logs\fable-08-p4-3-butterfly-shot-take.log`.
- Non-director prediction proof rerun:
  `Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\prediction_ragdoll_wall_200.scene.json --interaction-script SkullbonezData\interaction\prediction_ragdoll_wall_200_predict.json --interaction-report TestOutput\interaction\prediction_ragdoll_wall_200_predict_report.json --frames 220 --replay on --replay-seconds 2 --fixed-step --vsync off --allocation-guard gameplay`
  passed with report `ok=true`, `predictionPathVisible=true`,
  `liveSolverHashStableAcrossPrediction=true`, and allocation guard
  `gameplay_violations=0`.

Closure:
- `Agentic/Reference/runtime-reference.md` now documents the shot-list schema,
  `Manual`/`Timer`/`RevealAtLeast`, `revealRate`, Director keys
  `B`/`J`/`K`/`L`, automation actions, and phase assertions.
- `08-demo-director-plan.md` and this file were updated to reflect completed
  Phase 4 and closure work.

Commit-gate validation recorded 2026-07-07:
- Touched-file comment audit inspected 4 source-bearing files
  (`RunDemoDirector.cpp`, `RunDemoDirector.h`, `RunInteractionAutomation.cpp`,
  `RunState.h`) with 0 deferred.
- `tools\validate_fast.bat` passed; source formatting, project filters,
  staged-size, runtime boundaries, unit tests, and Profile/Debug builds passed
  with 0 warnings/errors. Log:
  `Agentic\Reports\2026-07-07\logs\fable-08-p4-validate-fast.log`.
- `tools\validate_dx12_renderer.bat` passed; DX12 InfoQueue reported 0
  validation errors and screenshots matched committed baselines. Log:
  `Agentic\Reports\2026-07-07\logs\fable-08-p4-validate-dx12-renderer.log`.
- `tools\validate_full.bat` passed; DX12 errors 0, screenshots matched, and
  `physics_regression_solver.csv` matched byte-exactly. Log:
  `Agentic\Reports\2026-07-07\logs\fable-08-p4-validate-full.log`.
