# Nightrunner Manual Test Checklist

Date prepared: 2026-08-20  
Branch: `nightrunner-18th-AUG-26`  
Source snapshot: `608ff81f76d1cf44857c29950c2d106a7e7a3fb1`  
Scope: completed Predicted Solver Cause Hierarchy, Continuous Orbital Forecast,
At-Rest Ball Stability, and Invariant Hardening IH0-IH2 work currently on this
branch. IH3-IH7 are still implementation work and are listed separately at the
end rather than presented as ready for sign-off.

This is an operator checklist, not a substitute for the automated gates. Tick a
box only after observing the behavior yourself. Put screenshots, videos, dumps,
or notes in the Evidence field rather than changing a golden baseline.

## Test Record

- [ ] Tester:
- [ ] Date/time:
- [ ] Commit tested matches `608ff81f76d1cf44857c29950c2d106a7e7a3fb1`, or record the newer commit:
- [ ] Build/configuration: `Debug` / `Profile`
- [ ] GPU and driver:
- [ ] Display resolution and DPI scale:
- [ ] VSync setting:
- [ ] Worker/thread setting, if changed from default:
- [ ] Evidence directory:
- [ ] No Physics, replay, performance, or visual golden was refreshed during manual testing.

## Before Starting

- [ ] Confirm `git status --short --branch` shows the branch and any expected
  local-only files before testing.
- [ ] Build the `Profile|x64` application from the current source.
- [ ] Launch once with the generated demo and confirm the DX12 window reaches a
  responsive frame with no startup fatal or missing-resource dialog.
- [ ] Confirm the in-game UI opens, tabs can be selected, mouse capture releases
  over UI, and closing the UI returns camera control.
- [ ] Press `F3` once and confirm a screenshot is written, so later failures can
  be captured consistently.
- [ ] Keep a copy of stdout/stderr for any crash, fatal, device loss, or failed
  scene load.

Useful launch pattern:

```bat
Profile\SKULLBONEZ_CORE.exe --scene <scene-name> --interactive --replay on
```

For Physics overlays, add one or more of:

```bat
--physics-debug contacts --physics-debug-sleep on --physics-debug-terrain-contact on
```

## A. General Replay And Scrubber Regression

Launch an ordinary interactive scene with replay enabled and let it run long
enough to retain several seconds of history.

- [ ] Move the pointer near the bottom edge and confirm the replay scrubber
  reveals cleanly without overlapping or corrupting the main UI.
- [ ] Drag the presentation row into history. Confirm only that row's thumb
  moves and the solver row remains muted at its own position.
- [ ] Drag the solver row into history. Confirm Physics pauses and the selected
  historical body state appears without changing the free inspection camera.
- [ ] Return the active thumb to the live edge and confirm live simulation
  resumes.
- [ ] Press `P` in live, predicted, historical, and inspection views. Confirm it
  consistently toggles replay play/pause in every view.
- [ ] While paused, press Space once. Confirm Physics advances one step without
  clearing an existing prediction drawing.
- [ ] Pause on a retained solver frame and click `BRANCH` or press Enter. Confirm
  a new live branch is created only after the selected solver state restores.
- [ ] Click the replay save icon. Confirm a new
  `replays\replay_v2_####.skreplay` file appears.
- [ ] Click `LOAD`, choose that artifact, and confirm the top row changes to
  `V2 FILE` and scrubs smoothly from start to end.
- [ ] Branch from a loaded file frame and confirm the UI returns to a live child
  branch without an error or stale file-backed pose.
- [ ] Evidence/notes:

## B. Predicted Solver Cause Hierarchy — High Detail

Recommended start:

```bat
Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\at_rest.scene.json --interactive --replay on --predict ball_a --predict-seconds 20
```

- [ ] Confirm `HIGH DETAIL` is checked by default beside `ALT VEL`.
- [ ] Confirm `PREDICT` is enabled and a white-to-green predicted root path
  progressively fills without freezing the window.
- [ ] Where a predicted contact exists, confirm the incoming child path/ring is
  amber, and post-contact path/markers are grey.
- [ ] Confirm bodies outside the active cause chain are visually de-emphasized
  rather than hidden or rendered with corrupt materials.
- [ ] Confirm the right-side cause window reports `PREDICT` and contains an
  indented `Body -> Manifold -> SolverRow` hierarchy, not only synthetic flat
  contact rows.
- [ ] Expand/collapse or scroll the cause window and verify row indentation,
  clipping, scrollbar bounds, and selection highlights.
- [ ] Click a Body row. Confirm inspection pauses if needed and the camera points
  at that body.
- [ ] Click a Manifold row. Confirm the selected contact/manifold stays matched
  to the visible bodies and frame.
- [ ] Click a SolverRow. Confirm the solver-detail panel shows coherent stage
  values for that same selected row rather than stale values from another frame.
- [ ] Scrub to a different prediction frame and repeat the row selection. Confirm
  the tree, solver panel, paths, and camera all refer to the same frame.
- [ ] Resize or drag the cause window to screen edges. Confirm it stays
  interactable, clipped to the viewport, and does not trap unrelated input.
- [ ] Evidence/notes:

## C. High/Low Detail Transition And Memory Release

Continue from the populated High-detail state above.

- [ ] Press `F6` to open the memory overlay and record Replay total, evidence
  memory, and the displayed category totals before changing mode.
- [ ] Uncheck `HIGH DETAIL` while prediction evidence is populated.
- [ ] Confirm the entire predicted cause window disappears, including its
  content and hit target; merely hiding the solver-detail subpanel is not a pass.
- [ ] Confirm the compact `HIGH DETAIL` checkbox remains visible and usable.
- [ ] Confirm lightweight prediction paths remain available in Low mode.
- [ ] Confirm released evidence and Replay/category totals decrease together.
  The automated reference delta was 1,925,120 bytes; record the observed manual
  values rather than forcing them to equal a stale build.
- [ ] Click where the removed cause window used to be. Confirm no invisible row
  selection, camera jump, or blocked world input occurs.
- [ ] Re-check `HIGH DETAIL`. Confirm a fresh generation rebuilds and the exact
  hierarchy returns without stale rows from the previous generation.
- [ ] Toggle High -> Low -> High at least three times. Confirm memory returns to
  stable levels and does not grow on every cycle.
- [ ] Turn `PREDICT` off while High is selected. Confirm prediction paths and
  reader-visible evidence disappear promptly, while the detail preference
  remains checked for the next prediction.
- [ ] Reset the scene and confirm the High/Low preference survives the reset.
- [ ] Evidence/notes:

## D. Multi-Body Prediction And Velocity Editing

Launch:

```bat
Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\replay_velocity_four_ball.scene.json --interactive --replay on
```

- [ ] Select the first ball and enable `PREDICT` with High detail.
- [ ] Confirm the predicted chain contains multiple bodies when downstream
  contacts are forecast, with each Body/Manifold/SolverRow attached to the
  correct parent.
- [ ] Shift+click another body. Confirm additive selection promotes the new root
  without corrupting the earlier retained history selection.
- [ ] Miss-click without Shift. Confirm the selected traces clear.
- [ ] Press `Alt` outside editor mode or click `ALT VEL`. Confirm live simulation
  pauses and a velocity gizmo appears on the selected dynamic body.
- [ ] Drag a linear axis in both directions. Confirm prediction restarts from the
  newest velocity and downstream contacts appear/disappear accordingly.
- [ ] Drag an angular ring. Confirm its radius/color responds to spin magnitude
  and a sleeping body wakes when edited.
- [ ] Hold a gizmo drag for several seconds. Confirm the currently published
  prediction remains coherent while newer generations replace it; there should
  be no flicker to unrelated topology, stale markers, or UI stall.
- [ ] Exit velocity mode and confirm ordinary replay/prediction controls regain
  input ownership.
- [ ] Evidence/notes:

## E. Continuous Orbital Forecast — Normal Operation

Launch:

```bat
Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\solar_system.scene.json --interactive --replay on
```

Open the Scene tab and scroll to `Continuous orbital forecast`.

- [ ] Before starting, confirm the section reports `not started` and the
  `CONTINUOUS`, `Reset`, and `Exit` controls are visible.
- [ ] Click `CONTINUOUS`. Confirm it changes to `CONTINUOUS*` and Producer reports
  `available / running` or transitions rapidly between running/idle without a
  failed state.
- [ ] Confirm Simulated time and newest absolute tick advance monotonically.
- [ ] Confirm `Sim / real` remains finite and does not become zero indefinitely,
  NaN, or infinity.
- [ ] Confirm Window age grows toward 120 seconds and then remains a rolling
  window rather than growing without bound.
- [ ] Confirm Stability initially shows `numeric ok / system ok / auxiliary ok`
  for the authored solar scene.
- [ ] Confirm First cause is `none` while the system remains healthy.
- [ ] Confirm energy and angular-momentum drift fields are finite and their max
  values do not move backwards.
- [ ] Confirm Sun, Earth, Mars, and ship each have an authored-color forecast
  ribbon and one head marker at the coherent newest tick.
- [ ] Watch through the 120-second wrap. Confirm no ribbon draws a long diagonal
  across the ring's physical wrap seam and no path briefly connects newest to
  oldest incorrectly.
- [ ] Orbit/free-fly the camera while forecast work runs. Confirm the application
  remains responsive and no frame appears partially updated across bodies.
- [ ] Press `F6` and observe retained/replay memory before and after the window
  wraps. Confirm it reaches a stable high-water instead of growing continuously.
- [ ] Evidence/notes:

## F. Continuous Forecast Lifecycle And Interaction

- [ ] With Continuous active, enable bounded replay `PREDICT`. Confirm the product
  enforces mutual exclusion rather than running both prediction owners at once.
- [ ] Return to Continuous and click `Reset`. Confirm tick/time/window values
  restart from a fresh live snapshot, old ribbons disappear, and new ribbons
  begin coherently.
- [ ] Click `Exit`. Confirm the worker stops, presentation clears, and controls
  remain responsive.
- [ ] Start Continuous again after Exit. Confirm a clean new run begins.
- [ ] While Continuous is active, load a different scene. Confirm the old worker
  joins before the scene is retired, no old solar ribbons leak into the new
  scene, and no shutdown/use-after-free fatal appears.
- [ ] Reload `solar_system`, start Continuous, then reset the scene with `R`.
  Confirm forecast state retires and can be restarted cleanly.
- [ ] Close the application while Continuous is active. Confirm graceful shutdown
  without a hang or worker-lifetime fatal.
- [ ] Repeat a start/reset/exit cycle at least five times and confirm memory and
  responsiveness remain stable.
- [ ] Evidence/notes:

## G. Orbital Stability Semantics

These checks are primarily about what the operator readout communicates.

- [ ] Confirm Sun is treated as the fixed primary, Earth and Mars as the
  system-wide core cohort, and the ship as an auxiliary.
- [ ] In a scene/run where only the ship leaves its allowed orbital envelope,
  confirm auxiliary health fails visibly while system health and the core
  horizon continue.
- [ ] Confirm a non-finite state, invalid publication, or private-step failure is
  shown as a global numerical-health failure rather than an auxiliary warning.
- [ ] Confirm a Sun/Earth, Sun/Mars, or Earth/Mars collision is system-blocking.
- [ ] Confirm a collision involving the ship only is reported as auxiliary-only
  unless a separate numerical failure occurs.
- [ ] Confirm the first failure remains latched with cause, time, subject, and
  other-body IDs while later ticks continue where policy allows.
- [ ] Click Reset after a latched failure and confirm all failure state clears and
  a new first failure can latch.
- [ ] Evidence/notes:

## H. At-Rest Ball Stability

Launch with visual Physics diagnostics:

```bat
Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\at_rest.scene.json --interactive --physics-debug contacts --physics-debug-sleep on --physics-debug-terrain-contact on
```

- [ ] Observe `ball_a`, `ball_b`, and `ball_c` from first terrain contact until
  all three are asleep. Do not stop at the retired 1,800-frame boundary.
- [ ] Confirm no ball enters a repeating small vertical bounce or terrain
  re-impact cycle after settling contact begins.
- [ ] Confirm no ball keeps visibly skating across the terrain after its last
  material impact.
- [ ] Confirm each ball has at most one meaningful late reversal per horizontal
  axis; tiny sub-threshold sign noise does not count as visible rolling.
- [ ] Confirm rolling resistance does not introduce visible normal-axis spin or
  push a ball away from the terrain.
- [ ] Confirm each ball's sleep counter progresses through quiet supported frames
  and final sleep occurs through the normal sleep system, not a visible velocity
  snap.
- [ ] Confirm sleeping balls remain asleep when undisturbed.
- [ ] Apply a genuine external impact to a sleeping ball. Confirm it wakes once,
  responds physically, and can settle back to sleep.
- [ ] Confirm the three box controls remain stable and do not gain new jitter,
  excessive speed, or false sleeping behavior.
- [ ] Confirm contact normals and motion look correct on non-flat terrain; a
  moving sphere should meet a slope at the slope-normal pole rather than the
  world-up pole.
- [ ] Leave the scene running after all balls sleep. Confirm no delayed wake,
  accumulating penetration, or slow horizontal drift appears.
- [ ] Evidence/notes:

## I. Contact Solver Regression Tour

Use representative terrain, stacked-body, ragdoll/point-joint, and fast-impact
scenes already present in the scene browser.

- [ ] Terrain impacts still bounce when above the restitution threshold; stable
  sub-threshold support does not command a new separating velocity.
- [ ] A manifold with several contact points does not bounce harder merely
  because it contains more rows.
- [ ] Stacked boxes settle without position-correction explosions or obvious
  per-row over-correction.
- [ ] Contact convergence is not dominated by many already-converged rows; dense
  contact scenes remain responsive and visually stable.
- [ ] Point-jointed or ragdoll bodies do not acquire a first-frame kick after a
  pause/resume or scene reset, and warm-started joints remain attached.
- [ ] Fast partial-time-of-impact contacts do not tunnel, double-advance, or gain
  a visibly incorrect friction/bias response.
- [ ] Contact/pipeline overlays remain aligned with the bodies they describe.
- [ ] Evidence/notes:

## J. Determinism And Long-Run Responsiveness

- [ ] Run the same fixed-step scene twice with the same seed and worker setting;
  confirm visible motion and completion order repeat.
- [ ] Repeat with worker settings 0, 1, and 4 where your launch tooling exposes
  them. Confirm prediction/orbital paths do not visibly change by worker count.
- [ ] Run a 120-second prediction/forecast horizon and confirm incremental work
  does not freeze input or rendering for seconds at a time.
- [ ] Toggle prediction/forecast controls rapidly during a build. Confirm no
  crash, stale publication, or half-drawn multi-body state.
- [ ] Exercise scene load, reset, and application shutdown while worker work is
  active. Confirm every transition completes without a hang.
- [ ] With VSync off, confirm the red `WARNING: VSYNC OFF` label appears on the
  scrubber; restore VSync and confirm it disappears.
- [ ] Evidence/notes:

## K. Invariant Hardening IH1 — Terrain Input Boundaries

IH1 is largely a failure-lane change, so combine visual testing of valid scenes
with safe load attempts for malformed copies. Never overwrite an authored scene.

- [ ] Load several existing terrain scenes and confirm height, normals, collision,
  and object support are unchanged for valid divisible dimensions.
- [ ] Save a scene snapshot with `F2`, reload it, and confirm terrain topology and
  supported object placement round-trip.
- [ ] From a disposable copied scene, try zero and negative terrain step size.
  Confirm loading fails cleanly before partial world mutation or a crash.
- [ ] Try an undersized terrain and a non-divisible `mapSize = 5`, `stepSize = 2`
  disposable case. Confirm each is rejected with a diagnostic and the previous
  scene remains usable.
- [ ] After a rejected load, load a valid scene. Confirm the renderer, Physics,
  scene browser, and subsequent snapshot save still work.
- [ ] Evidence/notes:

## L. Invariant Hardening IH2 — Foundation Boundaries

- [ ] Load ordinary textured scenes and confirm all expected textures resolve;
  no final-slot asset is missing, substituted, or rendered black.
- [ ] Exercise a scene that reaches the authored texture-table boundary if one is
  available. Confirm the exact last slot succeeds and one-over fails through an
  owned diagnostic/fatal rather than memory corruption.
- [ ] Launch with tornado enabled and toggle tornado visuals from the Physics UI.
  Confirm forces and vectors render normally.
- [ ] Load an authored scene with exactly 64 tornado fields. Confirm it loads and
  renders; a disposable 65-field copy must fail the load transaction cleanly.
- [ ] Toggle/reload/exit while tornado visuals are active. Confirm no stale-frame
  borrow, cleared-resource dereference, or shutdown crash.
- [ ] Exercise representative camera/orbit/rotation paths and confirm no visible
  quaternion distortion, scaling, NaN, or camera flip was introduced.
- [ ] Confirm ordinary Vector3 normalization users still produce unit-direction
  behavior, while invalid user/authored input follows its checked failure lane.
- [ ] Evidence/notes:

## M. UI, Presentation, And Memory Polish

- [ ] Test at 100%, 125%, and 150% Windows DPI if available. Confirm Scene
  forecast controls, cause window, scrubber, and memory overlay remain readable
  and clickable.
- [ ] Test a narrow and a wide window. Confirm no cause-tree text, forecast
  status, or button draws outside its panel.
- [ ] Confirm `CONTINUOUS*`, `HIGH DETAIL`, `PREDICT`, `ALT VEL`, `Reset`, and
  `Exit` all have visible hover/active/disabled states.
- [ ] Confirm selected cause rows and solver details remain legible over both
  bright and dark scene backgrounds.
- [ ] Press `F6` repeatedly. Confirm the memory overlay toggles without shifting
  or disabling the replay controls beneath it.
- [ ] Confirm memory/replay category totals remain internally consistent before
  and after High-detail release, scene reset, and forecast exit.
- [ ] Evidence/notes:

## N. Known Owner-Controlled Stops — Do Not Refresh

These are expected comparison stops in the completed evidence, not permission
to update baselines during this manual pass.

- [ ] Record whether the current Physics regression comparison still reports the
  mapped `physics_regression_varied.csv` difference (35,303 lines in the RS7
  terminal run, first difference at line 1239).
- [ ] Record whether replay visual fidelity still stops at the inherited
  `header.topologyVersion` mismatch after its false-pass controls.
- [ ] Record any relative Physics performance noise separately from the absolute
  DX12/Physics budget result.
- [ ] Confirm no Physics, replay, performance, known-issue, or visual baseline was
  regenerated or copied over its canonical file.

## O. Items Not Yet Ready For Manual Sign-Off

The current branch reports Invariant Hardening at 3/8 phases. Do not mark these
areas complete solely from this checklist:

- [ ] IH3: Physics collider-store and remaining Physics assertion hardening.
- [ ] IH4: Rendering, DX12, World, Scene, primitive-batch, and preview-capacity
  hardening.
- [ ] IH5: Runtime, Input, Interaction, Replay, Planning, UI, and `Run` lifecycle
  hardening.
- [ ] IH6: invariant taxonomy, allocation-policy comments, and tool headers.
- [ ] IH7: integrated validation, final inventory reconciliation, independent
  review, and closure.

## Final Sign-Off

- [ ] Every applicable section above has a Pass, Fail, or Not Applicable result.
- [ ] Every failure includes reproduction steps, scene, commit, configuration,
  screenshot/video, and relevant stdout/stderr.
- [ ] No failure was hidden by changing a golden baseline.
- [ ] Replay High/Low, continuous forecast, at-rest stability, valid terrain,
  tornado, texture, scene reset/load, and shutdown received at least one complete
  end-to-end pass.
- [ ] Tester verdict: **PASS / PASS WITH KNOWN STOPS / FAIL**
- [ ] Summary notes:

