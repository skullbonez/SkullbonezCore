# Progress: Consequence Look (plan 09)

Source plan: `fable_plans/09-consequence-look-plan.md`
Status: in progress; consequence grade and smooth replay-ribbon glow implemented
Last updated: 2026-07-07 (Phase 0-2 visual slice)

## How to work this file

- Do items in order; one checkbox = one verifiable action; tick only with the
  named evidence (a screenshot for visual items) pasted under the box.
  `[B]` + reason if blocked twice.
- Anchors are file + search string; locate with `rg -n "<anchor>" <file>`.
- Every item is render-only. Gate: `validate_dx12_renderer` + verify
  `dx12_validation.txt` = 0. Visual items intentionally change screenshot
  baselines only when the validation suite covers the changed image; update them
  deliberately, never as a silent side effect.
- Comment quality gate applies to every touched source file.

## Verified facts (do not re-derive)

- Final-image controls: `CinematicRenderConfig` in `Core/Config.h` - `exposure`
  (anchor `float exposure = 1.02f`), `gamma`, per-pass bools (`bloomEnabled`,
  `fogEnabled`, `godRaysEnabled`, `volumetricLightingEnabled`,
  `skyAtmosphereEnabled`, `terrainReliefEnabled`). Style grade lives in the
  style JSON `styleGrade[3]` + `terrainTint`/`skyZenith`/`skyHorizon`.
- Generic debug lines still use `DrawLinesColored` and `grid_line.hlsl`, but
  replay causal trails now route through `RunEditorTracer::AddReplayPathSegment`
  into smooth camera-facing replay ribbons.
- Causal boxes: `AddReplayCausalEntryMarker` (yellow) /
  `AddReplayCausalRestMarker` (grey) in RunEditorTracer.inl now emit replay
  ribbon outlines with core/glow style layers.
- Prediction state / ghost meshes: `BuildPredictionGhostDrawRequests`
  (ReplayRuntime.cpp, anchor `m_predictionGhostDrawRequests.clear()`),
  `ReplayPredictionGhostDrawRequest` struct (ReplayRuntime.h, has modelIndex,
  position, orientation, alpha).
- Prediction on/off + reveal: `RunReplayPredictionState.enabled` and
  `revealAnchor` (ReplayRuntime.h). Root/child colors are set inline in
  RunReplayPredictionVisualizer.inl (warm palette) - the two-tone work reads a
  "baseline vs live" flag here.

## Phase 0 - discovery (record inline)

- [x] D1. Find the tonemap/post entry that reads `exposure`/`gamma`:
  `rg -n "exposure|Tonemap|tonemap" SkullbonezSource/Runtime/RunPasses.cpp SkullbonezSource/Rendering`.
  Record the exact struct/uniform the final pass reads so a runtime grade
  override can be injected there (not by mutating the saved style).
  Evidence: `BindTonemapPassParams` in `SkullbonezSource/Runtime/RunPasses.cpp`
  writes `uExposure` and `uGamma` from the frame-local
  `CinematicRenderConfig`; the fable-09 runtime grade copies the config and
  modifies that copy in `Run::Render`.
- [x] D2. Confirm whether the post stack has a saturation/tint control already
  (style grade multiplies color?). Record how `styleGrade[3]` reaches the
  shader - that is the desaturation lever if present; if not, D3.
  Evidence: `BindTonemapPassParams` writes `uStyleGrade` as saturation,
  contrast, vignette, and style mode; `post_tonemap.hlsl` uses
  `uStyleGrade.x` for saturation at the final ACES-mapped color.
- [x] D3. Find `EmitLine` and `EmitShapeOutline` bodies in RunEditorTracer.inl
  and record how a line/outline becomes vertices (width? always 1px? additive
  or alpha blend?). This decides whether "fat underlay" is extra geometry or a
  shader/blend-state change.
  Evidence: before this slice, `EmitLine`/`EmitShapeOutline` appended xyz/rgb
  vertices and `DrawLinesColored` submitted a DX12 `LINELIST` through
  `grid_line.hlsl`, with no width or edge-distance signal. The new replay-only
  path emits camera-facing triangles instead of widening the old line list.
- [x] D4. Check if bloom (`bloomEnabled`) samples the same target the tracer
  lines draw into. `rg -n "bloom|Bloom" SkullbonezSource/Runtime/RunPasses.cpp`.
  If the lines are in the bloom input, glow is nearly free; if not, note it.
  Evidence: debug overlays write to `CinematicSceneColor` before the tonemap
  graph; `post_tonemap.hlsl::SampleBloom` samples `uSceneTex`, so replay ribbon
  `hdrScale` feeds bloom without a separate target.

## Phase 1 - consequence grade (biggest win)

- [x] P1.1 Add a runtime grade override (do NOT edit saved styles): a small
  `ConsequenceGrade { float strength; }` on the render frame path, and in the
  tonemap input (per D1) lerp exposure toward ~-1.5 stops, saturation toward
  ~0.25, and push a cool tint by `strength`. `strength` is a plain float the
  director/prediction toggle animates 0->1 over ~1s.
  Evidence: `RuntimeRenderer::RenderFrameEntry` now maintains
  `m_consequenceGradeStrength`, copies the active cinematic config, and applies
  a frame-local consequence grade without mutating saved styles.
- [x] P1.2 Drive `strength`: 1.0 when `prediction.enabled` (or a director
  phase requests it), 0.0 otherwise, eased. One writer, render-only state.
  Evidence: screenshot Predict-off (normal) vs Predict-on (dark/cool, bricks
  as silhouettes, boxes brightest). Gate: `validate_dx12_renderer`, update the
  butterfly-scene baseline intentionally. Commit.
  Evidence: `Run::Render` passes `m_replayRuntime.Prediction().enabled` as a
  renderer-owned grade request, which drives a one-second approach toward
  strength 1.0 and back toward 0.0. Screenshot proof:
  `TestOutput/interaction/prediction_ragdoll_wall_200_predict.bmp` shows the
  Predict-on cool grade with bright causal overlays.

## Phase 2 - smooth glowing lines and boxes

- [x] P2.1 Make causal lines and boxes anti-aliased/smooth before adding glow.
  Preferred shape: shader-supported screen-space line width with an
  anti-alias feather, then optional glow controls (`glowStrength`,
  `glowRadius`, or equivalent) so selected butterfly-effect lines can stand
  out. If the current line path cannot support shader feathering directly,
  emit camera-facing quads with smooth edge falloff rather than wider jagged
  wireframe. Keep the styling behind a `predictionGlow`/demo-look flag so
  non-demo overlays are unchanged.
  Evidence: `RunEditorTracer` now emits replay causal trails/boxes as
  camera-facing ribbon triangles. `replay_ribbon.hlsl` applies per-vertex
  `edgeFeather` for smooth alpha falloff, leaving generic debug overlays on
  the old line-list path.
- [x] P2.2 If D4 says lines are NOT in bloom input, add the line/outline target
  to the bloom source (cheapest real glow). If they are, expose a shader/debug
  line style option that raises selected line HDR intensity above 1.0 so bloom
  can pick them up without over-brightening every overlay.
  Evidence: side-by-side screenshot, current jagged hairline vs smooth
  anti-aliased glow/emphasis. Gate:
  `validate_dx12_renderer` (+ `validate_perf` if the extra draws are per-frame
  heavy - the tracer is already bounded by REPLAY_PATH_MAX_SEGMENTS). Commit.
  Evidence: `ReplayRibbonStyle` exposes width, alpha, edge feather, and
  `hdrScale`; causal entry/rest/horizon outlines and trails author separate
  glow/core layers. Screenshots:
  `TestOutput/interaction/butterfly_phase_1_chain_bloom.bmp` and
  `TestOutput/interaction/prediction_ragdoll_wall_200_predict.bmp`.

## Phase 3 - two-tone butterfly (depends on fable-plan-03 done + a baseline capture)

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

## Phase 4 - divergence counter

- [ ] P4.1 Compute a scalar: sum over matched bodies of
  `|baselineRestPose - currentRestPose|` (only bodies with a rest pose in both;
  reuse the resting-pose test from RunReplayPredictionHelpers.inl). Update it
  when a new prediction completes. Bounded, no per-frame cost beyond the sum.
- [ ] P4.2 Draw it big via the existing UI text pass (`rg -n "RunUiTextPass"`),
  behind a clean-demo toggle so it can be the only HUD element on screen.
  Format for impact (e.g. "DIVERGENCE 1,240 u"). Evidence: screenshot with the
  counter; nudge the input smaller and show it grow. Gate:
  `validate_dx12_renderer`. Commit.

## Closure

- [x] Z1. Add a `consequence.style.json` (dark/cool/desaturated) so the
  director can bind it as a phase's render type as an alternative to the
  runtime grade override - whichever reads better.
  Evidence: `SkullbonezData/styles/consequence.style.json`.
- [ ] Z2. Update baselines intentionally; note the deliberate visual change in
  `Agentic/SessionState.md`.
  Note: no committed baseline image has been refreshed in this slice yet. Do
  this only if the DX12 validation suite's committed baselines are deliberately
  updated for the new consequence look.
- [x] Z3. Update `fable_plans/09-consequence-look-plan.md` status + this file.
  Evidence: this progress file and `fable_plans/09-consequence-look-plan.md`
  now describe the completed Phase 1/2 slice and remaining Phase 3/4 work.
