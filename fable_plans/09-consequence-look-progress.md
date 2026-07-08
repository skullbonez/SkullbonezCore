# Progress: Consequence Look (plan 09)

Source plan: `fable_plans/09-consequence-look-plan.md`
Status: not started
Last updated: 2026-07-07

## How to work this file

- Do items in order; one checkbox = one verifiable action; tick only with the
  named evidence (a screenshot for visual items) pasted under the box.
  `[B]` + reason if blocked twice.
- Anchors are file + search string; locate with `rg -n "<anchor>" <file>`.
- Every item is render-only. Gate: `validate_dx12_renderer` + verify
  `dx12_validation.txt` = 0. Visual items intentionally change screenshot
  baselines — update them deliberately (see AGENTS.md baseline rules), never
  as a silent side effect. Update ONLY the baselines your change is supposed
  to alter; if an unrelated baseline diffs, that is a regression to fix, not
  a baseline to refresh. When a slice touches `Runtime/*` files, the AGENTS.md
  file-to-validation map wins: run `validate_full` for that slice.
- New interaction proof scripts must live in `SkullbonezData/interaction/`
  (committed), never `Agentic/Temp/` (gitignored, unreproducible).
- P3.x depends on fable-plan-03 phases 1-2, which are DONE (private
  `predictionEngine` exists in `ReplayRuntime.h`); plan-03 phase 3 (worker
  job) is NOT needed for this plan.
- Comment quality gate applies to every touched source file.

## Verified facts (do not re-derive)

- Final-image controls: `CinematicRenderConfig` in `Core/Config.h` — `exposure`
  (anchor `float exposure = 1.02f`), `gamma`, per-pass bools (`bloomEnabled`,
  `fogEnabled`, `godRaysEnabled`, `volumetricLightingEnabled`,
  `skyAtmosphereEnabled`, `terrainReliefEnabled`). Style grade lives in the
  style JSON `styleGrade[3]` + `terrainTint`/`skyZenith`/`skyHorizon`.
- All causal LINES: `RunEditorTracer::AddReplayPathSegment`
  (`Runtime/Editor/RunEditorTracer.inl:471`) → `EmitLine(...)`, colors scaled
  by `RUN_EDITOR_TRACER_REPLAY_LINE_OPACITY`. Single choke point.
- Causal BOXES: `AddReplayCausalEntryMarker` (yellow) /
  `AddReplayCausalRestMarker` (grey) in RunEditorTracer.inl → `EmitShapeOutline`.
- Prediction state / ghost meshes: `BuildPredictionGhostDrawRequests`
  (ReplayRuntime.cpp, anchor `m_predictionGhostDrawRequests.clear()`),
  `ReplayPredictionGhostDrawRequest` struct (ReplayRuntime.h, has modelIndex,
  position, orientation, alpha).
- Prediction on/off + reveal: `RunReplayPredictionState.enabled` and
  `revealAnchor` (ReplayRuntime.h). Root/child colors are set inline in
  RunReplayPredictionVisualizer.inl (warm palette) — the two-tone work reads
  a "baseline vs live" flag here.

## Phase 0 — discovery (record inline)

- [ ] D1. Find the tonemap/post entry that reads `exposure`/`gamma`:
  `rg -n "exposure|Tonemap|tonemap" SkullbonezSource/Runtime/RunPasses.cpp SkullbonezSource/Rendering`.
  Record the exact struct/uniform the final pass reads so a runtime grade
  override can be injected there (not by mutating the saved style).
- [ ] D2. Confirm whether the post stack has a saturation/tint control already
  (style grade multiplies color?). Record how `styleGrade[3]` reaches the
  shader — that is the desaturation lever if present; if not, D3.
- [ ] D3. Find `EmitLine` and `EmitShapeOutline` bodies in RunEditorTracer.inl
  and record how a line/outline becomes vertices (width? always 1px? additive
  or alpha blend?). This decides whether "fat underlay" is extra geometry or a
  shader/blend-state change.
- [ ] D4. Check if bloom (`bloomEnabled`) samples the same target the tracer
  lines draw into. `rg -n "bloom|Bloom" SkullbonezSource/Runtime/RunPasses.cpp`.
  If the lines are in the bloom input, glow is nearly free; if not, note it.

## Phase 1 — consequence grade (biggest win)

- [ ] P1.1 Add a runtime grade override (do NOT edit saved styles): a small
  `ConsequenceGrade { float strength; }` on the render frame path, and in the
  tonemap input (per D1) lerp exposure toward ~−1.5 stops, saturation toward
  ~0.25, and push a cool tint by `strength`. `strength` is a plain float the
  director/prediction toggle animates 0→1 over ~1s.
- [ ] P1.2 Drive `strength`: 1.0 when `prediction.enabled` (or a director
  phase requests it), 0.0 otherwise, eased. One writer, render-only state.
  Evidence: screenshot Predict-off (normal) vs Predict-on (dark/cool, bricks
  as silhouettes, boxes brightest). Gate: `validate_dx12_renderer`, update the
  butterfly-scene baseline intentionally. Commit.

## Phase 2 — glowing lines and boxes

- [ ] P2.1 In `AddReplayPathSegment`, emit each segment as TWO draws: a wide,
  low-alpha additive underlay (color × ~0.4, several px if width is supported
  per D3; else a parallel offset quad) then the existing bright core. Same
  treatment factored into `EmitShapeOutline` for the yellow/grey boxes.
  Keep it behind a `predictionGlow` flag so non-demo overlays are unchanged.
- [ ] P2.2 If D4 says lines are NOT in bloom input, add the line/outline target
  to the bloom source (cheapest real glow). If they are, just raise line HDR
  intensity above 1.0 so bloom picks them up.
  Evidence: side-by-side screenshot, hairline vs glow. Gate:
  `validate_dx12_renderer` (+ `validate_perf` if the extra draws are per-frame
  heavy — the tracer is already bounded by REPLAY_PATH_MAX_SEGMENTS). Commit.

## Phase 3 — two-tone butterfly (depends on fable-plan-03 done + a baseline capture)

- [ ] P3.1 Add a retained baseline snapshot: when the operator starts editing
  the root velocity, copy the current committed prediction's per-body
  entry/rest poses + a downsampled root polyline into a bounded, preallocated
  buffer (register under the replay reserve owner per the allocation gate).
  This is small because the two-box design stores two poses per body, not full
  trails.
- [ ] P3.2 Render the baseline cold: draw the baseline root polyline and
  entry/rest boxes in desaturated cyan (reuse `AddReplayPathSegment`/marker
  calls with a cold palette + lower intensity), and the baseline rest meshes
  via the existing ghost path (`BuildPredictionGhostDrawRequests`) pointed at
  the baseline rest poses with low alpha. The live prediction keeps its warm
  palette and unfolds over the top.
  Evidence: screenshot mid-nudge showing cold baseline + warm new tree.
  Gate: `validate_dx12_renderer` + `prediction_ragdoll_wall_200_predict`.
  Commit.

## Phase 4 — divergence counter

- [ ] P4.1 Compute a scalar: sum over matched bodies of
  `|baselineRestPose − currentRestPose|` (only bodies with a rest pose in both;
  reuse the resting-pose test from RunReplayPredictionHelpers.inl). Update it
  when a new prediction completes. Bounded, no per-frame cost beyond the sum.
- [ ] P4.2 Draw it big via the existing UI text pass (`rg -n "RunUiTextPass"`),
  behind a clean-demo toggle so it can be the only HUD element on screen.
  Format for impact (e.g. "DIVERGENCE 1,240 u"). Evidence: screenshot with the
  counter; nudge the input smaller and show it grow. Gate:
  `validate_dx12_renderer`. Commit.

## Closure

- [ ] Z1. Add a `consequence.style.json` (dark/cool/desaturated) so the
  director can bind it as a phase's render type as an alternative to the
  runtime grade override — whichever reads better.
- [ ] Z2. Update baselines intentionally; note the deliberate visual change in
  `Agentic/SessionState.md`.
- [ ] Z3. Update `fable_plans/09-consequence-look-plan.md` status + this file.
