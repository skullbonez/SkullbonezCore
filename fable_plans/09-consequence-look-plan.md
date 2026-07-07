# Consequence Look Plan (butterfly-demo visual impact)

Date: 2026-07-07
Status: Complete as of 2026-07-08
Impact area: rendering (tonemap/grade, line rendering, prediction ghosts, HUD);
no physics change
Validation for this document: none (documentation-only)

## Goal

Make the butterfly demo read as *causality is the light source*, not *debug
overlay on a pretty world*. Four moves, in impact order:

1. **Consequence grade** - when Predict engages, blend the whole scene into a
   dark, desaturated, cool grade so the muted bricks become silhouettes and the
   yellow/grey causal boxes + trails become the brightest things in frame.
   Implemented in `RuntimeRenderer::RenderFrameEntry` as a frame-local grade
   override driven by the prediction enablement bit that `Run` passes in.
2. **Smooth glowing lines** - the causal trails and box outlines get an
   anti-aliased energy look (smooth core + optional additive glow), not jaggy
   1px wireframe. Implemented as a replay-only ribbon shader with width,
   edge-feather, and HDR glow scale controls, leaving generic debug lines on
   the old line-list path.
3. **Two-tone butterfly** - the baseline future renders as cold cyan ghosts;
   the nudged future unfolds warm over it. Divergence is readable at a glance.
   Implemented with a retained `ReplayPredictionBaselineSnapshot`, cold ribbon
   path/box overlays, and low-alpha cyan baseline ghost requests.
4. **Divergence counter** - one big on-screen number (sum of
   baseline-vs-current rest-pose distances) that spins up as the trees split.
   Journalists quote numbers. Implemented in `RunUiTextPass` behind
   `--hide-top-text` for clean demo captures.

Pairs with the Demo Director (plan 08): the grade and reveal-rate are per-phase
so the director choreographs the look, not just the camera.

## Why these

The competing elements in the current frame are the sunset sky, warm terrain,
and red checkered wall - all fighting the thin colored lines that ARE the demo.
Inverting the visual hierarchy (world down, causality up) is the whole game.
Every move here is a render-config or line-draw change; none touch the
deterministic simulation, so all are gated by `validate_dx12_renderer` and the
existing screenshot baselines.

## What already exists (reuse)

- Final-image controls in `CinematicRenderConfig` (`Core/Config.h`): `exposure`
  (:141), `gamma`, per-pass toggles (bloom, fog, godRays...), plus the style
  system's `styleGrade[3]` and sky/terrain tints. The grade is a parameter
  set, not a new pass.
- Replay causal lines funnel through `RunEditorTracer::AddReplayPathSegment`
  (`Runtime/Editor/RunEditorTracer.inl`) into the replay ribbon path, scaled by
  `RUN_EDITOR_TRACER_REPLAY_LINE_OPACITY`. One choke point for the glow.
- Box outlines: `AddReplayCausalEntryMarker` / `AddReplayCausalRestMarker`
  (RunEditorTracer.inl) emit replay ribbon outline layers.
- Prediction ghost meshes (alpha'd real meshes) already render via the replay
  ghost path (`BuildPredictionGhostDrawRequests` in ReplayRuntime.cpp) - the
  two-tone baseline reuses it.
- Bloom pass toggle already exists (`bloomEnabled`) - debug overlays draw into
  the HDR scene color before tonemap, so replay ribbon HDR scale feeds bloom.

## Definition of Done

- Toggling Predict (or entering a graded director phase) crossfades into the
  consequence grade over ~1s and back out cleanly.
- Causal trails and boxes are smooth/anti-aliased first, then visibly glow
  where the butterfly demo needs emphasis. Shader options can boost selected
  lines with additive glow without making every debug overlay loud.
- With a baseline captured, the old future renders cold/ghosted and the new
  future warm; a divergence number is displayed and grows as they separate.
- No DX12 validation errors; committed screenshot baselines updated
  intentionally if a validation-suite baseline changes. The final 2026-07-08
  gates matched existing committed DX12 baselines, so no baseline refresh was
  needed.

## Non-goals

- No new physics, no change to prediction math or the reveal cursor logic.
- Not a full color-grading system - one authored "consequence" look plus the
  existing per-style grade is enough.

See the executable checklist in `09-consequence-look-progress.md`.
