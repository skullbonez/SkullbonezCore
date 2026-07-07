# Progress: Demo Director (plan 08)

Source plan: `fable_plans/08-demo-director-plan.md`
Status: phase 1 data model/load-save complete; phase 2 Director camera mode next
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
  Manipulator, Count. Add `Director` before `Count`.
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

- [ ] P2.1 Add `RunCameraMode::Director` to the enum (before `Count`); update
  every exhaustive switch the compiler flags (that is the checklist — build and
  fix each `-W4` switch warning). Add label in `CameraModeLabel`
  (RunInput.cpp:1081). Gate: build 0/0.
- [ ] P2.2 Director runtime state on the Run side (fixed, in RunState.h near
  RunCameraState): active shot list, current phase index, phase elapsed time,
  blend timer, and a `grabbed` bool + the free-fly pose captured at grab.
- [ ] P2.3 Per-frame director tick (new `RunDemoDirector.cpp`, `Run::` methods,
  called from the frame update near the other camera ticks — see
  `TickAttachedCamera` call site for where): if not grabbed, drive the rendered
  camera (D2) from the current phase pose, lerping over `blendInSeconds` on
  phase entry; apply the phase style once on entry (P3). Evidence: a 2-phase
  shot list visibly cuts camera + look between two poses.
- [ ] P2.4 GRAB: a key (pick an unused one; record it) sets `grabbed=true`,
  seeds the free-fly camera with the *current authored pose* (no jump), and
  routes input to free-fly. RELEASE: same key toggles `grabbed=false` and
  blends the rendered camera from the current free-fly pose back to the phase
  pose over `blendInSeconds`. Model this on the `modeBeforeAttach`
  save/restore pattern. Evidence: grab → fly around → release → smooth return,
  no pop, recorded via a short screen capture or eyeballed on launch.
- [ ] P2.5 "Set pose here": while grabbed (or in any fly mode), a key writes
  the current camera pose into the selected phase; another key steps the
  selected phase; another saves the shot list (P1.2). Evidence: author a pose
  live, save, reload, pose matches. Gate: `validate_dx12_renderer`. Commit.

## Phase 3 — render type (style) per phase

- [ ] P3.1 On phase entry, copy the live-style idiom from
  `RunLiveStyle.cpp:260-261`: `TestScene::LoadStyleFromFile(phase.stylePath,
  m_systems.assets)` followed by `ApplyLiveStyleScene(context, styleScene)`.
  Build the `SceneRuntimeStyleContext` the same way Run already does for the
  live-style harness. If D3 said apply is expensive, preload each phase's
  `TestScene` into the shot list at load and call `ApplyLiveStyleScene` with
  the cached scene.
- [ ] P3.2 Phase-entry also sets the prediction reveal rate to
  `phase.revealRate` (write `REPLAY_PREDICTION_REVEAL_SECONDS_PER_SECOND`'s
  runtime equivalent — if it is currently a constexpr constant in
  RunReplayTools.cpp, promote it to a runtime field on
  `RunReplayPredictionState` first, defaulting to 1.0, so the director can
  slow the unfold for the money shot). Gate: `validate_dx12_renderer` +
  `prediction_ragdoll_wall_200_predict` proof (reveal still works). Commit.

## Phase 4 — advance rules + automation + the demo shot list

- [ ] P4.1 Implement the three advance rules in the director tick: Manual
  (advance key), Timer (`phase elapsed >= timerSeconds`), RevealAtLeast
  (normalized reveal progress >= `revealThreshold` — read the reveal
  frame/lastFrame per the verified-facts source). On advance past the last
  phase: hold (do not loop) unless a shot-list `loop` flag says otherwise.
- [ ] P4.2 Automation actions in `RunInteractionAutomation.cpp`:
  `loadShotList`, `directorPlay`, `directorAdvance`, `directorGrab`,
  `directorRelease`, `setPhaseStyle` — so a scripted take is reproducible and
  screenshots can be pinned to phases. Evidence: a `*.json` interaction script
  that loads the shot list, plays it, and screenshots each phase; report
  `ok=1`.
- [ ] P4.3 Author `SkullbonezData/shots/butterfly.shot.json` for the 200-brick
  scene: e.g. phase 1 low behind the ball sighting the root line (dark style,
  RevealAtLeast 0.05), phase 2 dolly-up three-quarter as the wall blooms
  (RevealAtLeast 0.5, revealRate 0.5), phase 3 wide settle on the grey rest
  boxes (Timer). Poses hand-authored via P2.5. Evidence: play it start to
  finish on the real scene; attach a screenshot per phase.
  Gate: `validate_dx12_renderer`. Commit.

## Closure

- [ ] Z1. Document the director keys + `.shot.json` schema in
  `Agentic/Reference/runtime-reference.md`.
- [ ] Z2. Update `fable_plans/08-demo-director-plan.md` status + this file.
