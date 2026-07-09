# Demo Director Plan (scene phases + hand-authored camera)

Date: 2026-07-07
Status: Implemented through Phase 4 and closure; commit-gate validation passed
Impact area: runtime camera, live-style application, interaction, UI overlay;
no physics change
Validation for this document: none (documentation-only)

## Goal

A **director** that plays an ordered list of **phases** for the butterfly demo.
Each phase is `{ camera pose, style (render type), advance rule, optional
reveal-rate }`. The operator authors camera poses by hand (fly to a nice spot,
"set this phase's pose here"), because a human frames better than a machine.
During playback the operator can **grab** the camera (detaches to free-fly from
the current authored pose — no jump) and **release** it (smooth blend back to
the phase pose). Phases can advance on a timer, on a keypress, or when the
prediction reveal cursor reaches a normalized point (so the shot list can be
choreographed to the causal unfold).

## Why this shape (not a spline)

The operator said it plainly: they frame better than a machine. So the machine
owns only *timing and blending between operator-chosen poses*, never the poses
themselves. A hand-authored shot list is also reproducible: saved to
`SkullbonezData/shots/*.shot.json`, replayed byte-for-byte, and — because the
engine already has scripted interaction + deterministic physics — recordable as
a perfect take on demand.

## What already exists (reuse, do not rebuild)

- `RunCameraMode` enum
  (Demo/Scene/Inspect/Attach/Launcher/Manipulator/Director/Count) in
  `Runtime/RuntimeCameraMode.h`.
- Free-fly camera (fly-mode enter/exit, MouseLook, MoveCamera) already in
  `RunInput.cpp` — the pose-authoring tool.
- Live style application: `RunLiveStyle.cpp` hot-reloads a style file by calling
  `TestScene::LoadStyleFromFile(...)` and
  `ApplyLiveStyleScene(SceneRuntimeStyleContext, TestScene)` in
  `Runtime/Scene/SceneRuntimeStyle.cpp`. Styles live in
  `SkullbonezData/styles/*.style.json` and carry the full cinematic block
  (styleModes, exposure, gamma, grade, sky/sun/terrain). **A phase's "render
  type" is a style filename.**
- Scripted interaction + camera + screenshot actions in
  `RunInteractionAutomation.cpp` (`clickReplayControl` etc.) — the director
  gets automation actions so a take can run headless.
- Prediction reveal cursor (`RunReplayPredictionState::revealAnchor` +
  `REPLAY_PREDICTION_REVEAL_SECONDS_PER_SECOND`) — the reveal-synced advance
  rule reads its normalized 0..1 progress.

## Definition of Done

- A `.shot.json` shot list loads and plays: camera blends to each phase pose,
  the phase's style is applied, and the phase advances per its rule.
- Operator can author: fly free, press "set pose" to write the current camera
  pose into the selected phase, "set style" to bind the active live style,
  save the shot list.
- Grab/release: one key detaches to free-fly starting exactly at the authored
  pose (no pop); one key blends back over a configurable duration.
- Reveal-synced advance works: a phase can say "advance when reveal >= 0.6".
- The 200-brick butterfly demo ships with an authored `butterfly.shot.json`.

## Non-goals

- No automatic camera framing/AI. The machine never invents a pose.
- No new physics or determinism surface (pose blending is render-only state).
- Not a general timeline editor — a flat ordered phase list is enough.

See the executable checklist in `08-demo-director-progress.md`.
