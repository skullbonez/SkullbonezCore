# Future Path Vector Splines — Clean Anti-Aliased Prediction Trajectories

Date: 2026-07-14
Status: Complete — 7/7 tasks complete
Impact area: trajectory ribbon shader, replay presentation (RunReplayTools /
RunEditorTracer), consequence-grade cinematic override, input bindings, replay
overlay UI, DX12 renderer validation
Owner: replay presentation

## Problem And Evidence

The owner reviewed the future-prediction view and rejected the current look on
three grounds:

1. **The consequence grade repaints the skybox.** Entering prediction mode
   lerps the whole cinematic grade toward a cool blue-gray, including the sky:
   `ApplyConsequenceGrade` pushes `skyHorizon` toward (0.22, 0.34, 0.58) and
   `skyZenith` toward (0.04, 0.08, 0.20)
   (`SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp:143-148`), and leaves
   clouds, sun glow, and god rays running. Owner ruling: in prediction mode the
   sky must be near-black with only a subtle horizon gradient, and the clouds
   must be gone entirely.
2. **The trajectory ribbons are too fat and too glowy.** The ribbon shader maps
   authored width as `clamp(width * 10, 1.5, 32)` pixels
   (`SkullbonezData/shaders/trajectory_ribbon.hlsl:69`) and the current
   authored path styles (width 1.05–2.10 at
   `SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp:141-144`) therefore
   render 10–26 px wide. The pixel shader then composites a three-band
   glow/shoulder/core falloff plus an HDR emphasis feed into bloom
   (`trajectory_ribbon.hlsl:144-157`). Zoomed out, overlapping fat glowing
   ribbons become an unreadable mass. Owner ruling: paths should read as
   thin (~2 px), anti-aliased, "beautiful vector splines" — not debug lines,
   not glow tubes.
3. **Temporary authoring machinery is still live.** The random look cycler
   `CycleReplayPredictionAuthoringLook`
   (`RunEditorTracer.cpp:156-472`, bound to `VK_OEM_PERIOD` at
   `SkullbonezSource/Runtime/InputController.Bindings.cpp:83`, dispatched at
   `SkullbonezSource/Runtime/InputFrameExecution.cpp:703`) and its
   saturation/gain hook inside `EmitReplayRibbonSegmentTo`
   (`RunEditorTracer.cpp:784-798`) carry an explicit deletion condition:
   "remove this method, action, and binding when the chosen preset is baked
   in." The owner has now chosen the direction, so the deletion condition is
   met.

Owner decisions recorded 2026-07-14:

- Sky in prediction mode: near-black dome with a subtle horizon gradient;
  clouds off. Terrain/object grading otherwise keeps the consequence look.
- Line width: fixed thin screen-space width (~1.5–2.5 px, tunable constant),
  analytically anti-aliased, stable at any zoom.
- Glow: removed from ordinary paths; kept **only** as an emphasis treatment
  for the selected/target object's path.
- Color modes, cycled with the **comma** key and reflected in the UI view:
  1. Lane flat color (clean single color per lane),
  2. Velocity heat (red = fast, blue = slow),
  3. Time-along-path gradient (now → horizon),
  4. Per-object hue (stable distinct hue per body),
  5. Causal depth (color keyed to cause-tree depth).
- The temporary authoring cycler and its ribbon saturation/gain hooks are
  deleted as part of this work.

## Goal

Prediction trajectories render as thin, analytically anti-aliased,
screen-space-constant vector splines over a near-black sky, with a
comma-cycled, UI-visible color-mode option set (lane flat, velocity heat,
time gradient, per-object hue, causal depth), and glow reserved exclusively
for the selected object's path.

## Non-Goals

- No change to prediction simulation, snapshot seeding, publish-prefix
  protocol, or trajectory capture. This plan is presentation only; the
  prediction fidelity contract stays owned by the permanent 200-box
  visual-fidelity gate (`tools\validate_replay_visual_fidelity.bat`) and the
  replay decomposition lane.
- No change to the 19-float ribbon vertex layout or the CPU→GPU upload
  contract unless a task below proves it necessary; color modes are computed
  CPU-side in the existing per-frame color lambdas.
- No new runtime allocation: the color mode is a value enum on existing
  presentation state; speed is derived at draw time from stored points.
- No new inheritance, no `*Sink`/`*Bridge` types, no hot-loop callbacks —
  color-mode selection resolves to plain values before segment emission.
- No general post-process restyle: only the sky/cloud terms of the
  consequence grade change; terrain/object grading keeps its current fade.

## Design

### D1: Sky treatment (consequence grade)

`ApplyConsequenceGrade` (`RuntimeRenderer.cpp:116-155`) keeps its crossfade
structure (`strength` in [0,1], smooth entry/exit via `ApproachFloat`), with
new sky targets at full strength:

- `skyZenith` → (0.0, 0.0, 0.0); `skyHorizon` → a subtle dark gradient on the
  order of (0.02, 0.03, 0.05) so the ground silhouette still reads.
- `cloudsEnabled` forced off once strength passes a small threshold (a bool
  cannot lerp; gate at s > ~0.15 to avoid a visible pop, and lerp
  `cloudIntensity` → 0 for the fade itself so the transition stays smooth).
- `sunIntensity` → 0, `skyGlowStrength` → 0, `godRaysEnabled` off,
  `sunShaftStrength` → 0 (a sun disc or shaft glow would repaint the black
  dome).
- Fog terms keep their current behavior (they cue depth on terrain/objects);
  volumetric strength should lerp down far enough that it cannot re-lighten
  the sky dome — verify against `post_volumetric_light.hlsl` output and tune.

### D2: Vector spline shader (`trajectory_ribbon.hlsl`)

- **Width:** interpret the per-vertex width channel directly as *pixels* with
  a tight clamp (≈ [1.0, 6.0]); authored styles set ~2 px for all ordinary
  lanes. Remove the `* 10` legacy mapping. The screen-space expansion,
  miter-join, and round-cap vertex logic (`main_vs`) already produce stable
  pixel-width geometry and stays.
- **Anti-aliasing:** replace the three-band glow/shoulder/core coverage
  (`main_ps` lines 144-151) with a single analytic edge: the shader already
  has normalized distance `edge` (rect body + round caps); coverage becomes
  `1 - smoothstep(1 - aaPx/halfWidthPx, 1, edge)` with `aaPx ≈ 1` so every
  line has a crisp core and ~1 px feather regardless of width. Quad geometry
  must overhang by the feather width (widen `widthPixels` by `aaPx` in the VS
  and renormalize) so the AA ramp is never clipped by the triangle edge.
- **Emphasis channel:** the existing per-vertex `style.y` (HDR emphasis)
  becomes the *selection emphasis* input. At 0 (ordinary paths) the output is
  plain SDR color with analytic AA and no bloom feed. Above 0 the shader adds
  a soft outer halo band and scales RGB above 1.0 so tonemap bloom picks it
  up — the current glow look, now exclusive to the selected object.
- Both pipeline variants (`TrajectoryRibbon`, `TrajectoryRibbonDepthHint`,
  `RenderBackendDX12.DynamicGeometry.cpp:81-127`) consume the same shader, so
  one HLSL change covers the visible and depth-hint passes; `uRibbonStyle`
  scalars (opacity/brightness/feather) keep their meaning but their tuned
  defaults move to the new thin-line look.

### D3: Color-mode architecture

New value enum owned by replay presentation state (alongside the existing
draw-side state in `RunReplayTools.cpp` / `ReplayRuntime.h` presentation
fields):

```
enum class ReplayPathColorMode : uint8_t
{ LaneFlat, VelocityHeat, TimeGradient, PerObjectHue, CausalDepth };
```

All trajectory draws already route per-frame colors through `colorForFrame`
lambdas at four call sites (`RunReplayTools.cpp:2618`, `2739`, `2873`,
`2887`) plus the all-body loop (`RunReplayTools.cpp:2699`), backed by the
palette helpers `ReplayPastRootColor`/`ReplayFutureRootColor`
(`RunReplayTools.cpp:1369-1386`), `ReplayDepthPalette`
(`RunReplayTools.cpp:1386`), and `ReplayChildIncomingColor`/`OutgoingColor`
(`RunReplayTools.cpp:2564-2583`). The mode plugs in as a shared color-resolve
helper those lambdas call with the record's context:

- **LaneFlat:** one fixed color per lane (future root, baseline, child
  incoming/outgoing, past root); the cleanest vector look.
- **VelocityHeat:** speed derived at draw time from consecutive stored points:
  `speed = |p[i] - p[i-1]| / ((frame[i] - frame[i-1]) * fixedDt)`. Stored
  `ReplayTrajectoryPoint` records carry only frame + position
  (`TrajectoryStore.h`), so derivation avoids any storage or capture change,
  and stride-sampled draws still average correctly across skipped frames.
  Map `saturate(speed / vMax)` (tunable `vMax`, default from typical scene
  speeds) through a blue→cyan→yellow→red ramp. The all-body and torso-trail
  loops read `RunReplayPredictionBodySample::linearVelocity`
  (`ReplayRuntime.h:495`) directly where samples are in hand.
- **TimeGradient:** reuse the already-computed `ReplayPathFrameT` value,
  mapped through a two-stop gradient (present → horizon).
- **PerObjectHue:** golden-ratio hue from `ReplayBodyId.value` so each body's
  path keeps a stable, well-separated hue across frames and sessions.
- **CausalDepth:** reuse `record.depth` / `node.depth` with
  `ReplayDepthPalette` for every lane, not just child branches.

Determinism note: `tools/check_replay_prediction_determinism.py` hashes the
submitted ribbon vertex bytes run-to-run. The color mode is deterministic
state with a fixed default (LaneFlat), so automation runs remain byte-stable.
The authoritative replay oracle does not press comma; focused color-mode
automation does press it in the same scripted order on every run. The default
is never randomized (the randomizer is deleted by T3).

### D4: Input + UI reflection

- Replace the deleted `CycleReplayPredictionAuthoringLook` action with
  `CycleReplayPathColorMode` bound to `VK_OEM_COMMA`
  (`InputController.Bindings.cpp` command table; the comma key is currently
  unbound — the old cycler used period). Dispatch follows the existing
  pattern at `InputFrameExecution.cpp:703`.
- The replay overlay HUD already labels prediction build state
  (`ReplayOverlayRenderer.cpp:776`); add the active color-mode name there and
  as a visible option row in the replay overlay panel (per owner: "reflected
  as an option in the UI view"), using the existing overlay layout/widget
  path (`ReplayOverlayLayout.*`) rather than a new UI tab.

### D5: Selected-object glow

The prediction target (root body, `prediction.simulation.targetModelRow` /
root `ReplayBodyId`) is the "selected object." Its FutureRoot (and PastRoot
while scrubbing) ribbons emit with emphasis > 0; every other lane emits
emphasis 0. `EmitReplayRibbonGlowPairTo` (`RunEditorTracer.cpp:818-836`)
collapses into the ordinary emit with an emphasis argument once glow is a
shader-side emphasis effect.

## Tasks

- [x] **T1 — Near-black prediction sky.** Retune `ApplyConsequenceGrade` per
  D1: near-black zenith, subtle dark horizon, clouds/sun-glow/god-rays/shafts
  fully out at strength 1, smooth crossfade preserved. Acceptance: entering
  prediction fades to a black dome with faint horizon; no clouds, sun disc,
  or shaft glow visible; leaving prediction restores the authored sky.
- [x] **T2 — Vector spline pixel shader.** Rework `trajectory_ribbon.hlsl`
  per D2: direct pixel-width interpretation with thin clamp, single analytic
  AA edge with ~1 px feather (geometry overhang included), glow bands removed
  from the emphasis-0 path, emphasis channel drives the selected-object halo
  + HDR feed. Retune `uRibbonStyle` defaults at the DX12 bind site.
  Acceptance: ordinary paths render ~2 px wide with smooth edges at any
  zoom/angle; no bloom contribution at emphasis 0; screenshot evidence
  before/after.
- [x] **T3 — Delete the temporary authoring machinery.** Remove
  `CycleReplayPredictionAuthoringLook`, its `RuntimeInputAction`, the
  `VK_OEM_PERIOD` binding, the dispatch case, the `TEMP_PREDICTION_*`/
  `TEMP_REPLAY_LOOK` logging, the `seed`/`saturation`/`colorGain` fields, and
  the saturation/gain math inside `EmitReplayRibbonSegmentTo`
  (`RunEditorTracer.cpp:784-798`). Bake the chosen fixed
  `ReplayRibbonStyle` constants (thin widths, full alpha, no HDR except the
  marker/emphasis style). Acceptance: `git grep TEMP_PREDICTION` and
  `git grep AuthoringLook` return nothing; ribbon colors pass through
  unmodified from the color-resolve helper.
- [x] **T4 — Color-mode architecture.** Add `ReplayPathColorMode` with all
  five modes per D3, resolved inside the existing `colorForFrame` lambdas and
  the all-body/torso-trail loops; default LaneFlat; draw-time speed
  derivation for VelocityHeat with a tunable `vMax`. Acceptance: each mode
  produces visibly distinct, correct coloring (verified against a scene with
  fast+slow bodies and a multi-level cause tree); zero steady-state
  allocations added. Closed 2026-07-15: the five value-only modes cover every
  stored/live/baseline/retained path lane. A 1,485-frame DX12 interaction run
  exercised a three-level cause tree with 13 trajectory records and 14,260
  points: all modes were visually distinct, 1,578 submitted segments had zero
  drops, the 136-frame unchanged window was stable, and replay reserve-growth
  remained 222 -> 222 throughout the steady-state observation window.
- [x] **T5 — Comma-key cycle + UI reflection.** New
  `CycleReplayPathColorMode` input action on `VK_OEM_COMMA`, overlay HUD
  label, and an option row in the replay overlay view per D4. Acceptance:
  comma cycles all five modes in order with the active name visible in the
  overlay; UI option row shows and changes the same state. Closed 2026-07-15:
  focused runtime-input coverage passed 4/4 tests and 915 assertions; DX12
  automation cycled Lane flat -> Velocity heat -> Time gradient -> Per-object
  hue -> Causal depth -> Lane flat, with both the scheduling HUD and dedicated
  `COLOR [,]` row reflecting every transition.
- [x] **T6 — Selected-object glow.** Wire emphasis per D5: only the
  prediction target's path carries emphasis > 0; halo + bloom feed visibly
  distinguish it; all other lanes stay flat vector lines. Acceptance:
  screenshot with target glowing amid flat-colored other-body paths. Closed
  2026-07-15: the tracer API remains zero-emphasis by default; only selected
  FutureRoot and selected PastRoot store draws pass 0.75 emphasis. Generic
  causal-entry markers were also returned to zero emphasis after the final
  rubber-duck audit caught their temporary authoring value. A late
  multi-level Lane-flat capture shows the cyan target root with a bounded halo
  against thin flat sibling paths. Profile built in 20.09 s with zero warnings
  or errors; the 1,485-frame replay run exited 0 in 10.37 s with stable
  submission, zero drops, and steady-state reserve growth 222 -> 222.
- [x] **T7 — Validation gate + baseline reconciliation.** This plan touches
  `Runtime/*`, `InputController*`, shaders, and DX12 bind code, so the gate is
  `tools\validate_full.bat`, then `tools\validate_dx12_renderer.bat`, then the
  mandatory `tools\run_graphics_stress.bat 1` (record command, measured
  runtime ≥ 10 s, crash-free exit). If committed DX12 screenshot baselines
  include prediction ribbons or the consequence sky, refresh them as an
  intentional visual-baseline update and rerun the renderer gate. Confirm
  `tools\validate_replay_scrub` determinism artifacts still pass byte-exact
  (presentation payload changed → its recorded fingerprints regenerate per
  that lane's rules). Resolve the visual-fidelity golden manifest per the
  binding sequencing decision above (owner-approved refresh, one gate
  invocation, one engine process). Acceptance: pasted command output for
  every gate, zero DX12 validation errors, zero `/W4` warnings.

## Dependencies And Decisions

- Independent of the live replay architecture lane
  (`replay-monolith-decomposition.md`): this plan changes presentation
  surfaces only. If the monolith decomposition lands first and moves draw
  code, the color-resolve helper follows the presentation owner it lands in.
- **Golden-manifest sequencing (binding).** The vector-spline restyle
  intentionally changes presented prediction frames, so the frame-exact
  200-box visual-fidelity golden manifest
  (`tools\validate_replay_visual_fidelity.bat`) cannot stay byte-identical
  through T1/T2/T6. Per MASTER inventory rule 11, a golden refresh requires
  explicit owner approval, and the decomposition lane requires the
  *unchanged* golden after every M-task. Therefore this plan's
  presentation-changing tasks land only with owner sign-off on a new golden,
  or after the decomposition tasks that depend on the current golden are
  closed — whichever the owner chooses at T7.
- Owner decisions above (2026-07-14) are binding: near-black sky with subtle
  horizon and no clouds; ~2 px fixed width; five color modes on comma; glow
  only for the selected object; cycler deleted.
- Comment standard applies to every touched source file
  (`Agentic/Reference/comment-style-guide.md`); the trajectory shader header
  and tracer glossary entries must be rewritten to describe the vector-spline
  contract, not the glow-band one.

## Progress

### T1 — Near-black prediction sky (complete 2026-07-15)

- `ApplyConsequenceGrade` now interpolates the zenith to black and the horizon
  to a faint blue-black target. Cloud, sun, sky-glow, shaft, and volumetric
  energy fade to zero before the discrete cloud/god-ray switches turn off.
- The consequence grade remains frame-local, so the one-second exit transition
  restores the scene-authored sky without retaining override state.
- Focused Profile build: `tools\validate_build.bat Profile` passed in 13.31 s
  with zero warnings and zero errors.
- Focused visual run: `replay_prediction_simple.scene.json` with a temporary
  enter/exit automation script passed in 10.2 s (exit 0). The active screenshot
  showed a black zenith, faint horizon, and no clouds/sun/shafts; the settled
  exit screenshot restored the authored cloudy sky. Both prediction-enabled
  assertions passed.
- Comment audit: 1/1 touched source file checked, zero deferred, zero unchecked.
  Formal full/renderer/stress gates remain owned by T7; no baseline was
  refreshed.

### T2 — Vector spline pixel shader (complete 2026-07-15)

- `trajectory_ribbon.hlsl` now treats authored width as literal full-width
  pixels, expands geometry for a one-pixel analytic feather, and computes
  rectangular-body/round-cap coverage from screen-pixel distance.
- The ordinary zero-emphasis branch emits flat display-range color with no
  halo or bloom feed. Positive emphasis alone reserves a three-pixel soft halo
  and raises selected color above display range. The 19-float vertex layout is
  unchanged.
- DX12 bind defaults now use the thin-line opacity/brightness/AA contract for
  both visible and depth-hint variants. Shader bake passed in 2.56 s; the
  focused Profile build passed in 13.75 s with zero warnings/errors.
- Before/after focused screenshots show the old glow tube reduced to a stable
  thin line; the post-T3 zero-emphasis capture visibly has no path halo.

### T3 — Delete temporary authoring machinery (complete 2026-07-15)

- Deleted the random authoring-look method and state, all `TEMP_PREDICTION_*` /
  `TEMP_REPLAY_LOOK` logging, the period-key action/binding/dispatch, and the
  final-boundary saturation/gain mutation.
- Fixed immutable styles now use 1.5–2.5 px widths and full alpha. T3 left the
  marker's temporary emphasis for T6 to resolve; T6 now sets all generic path,
  causal, baseline, and marker styles to zero and passes emphasis only for the
  selected root.
- Acceptance greps for `TEMP_PREDICTION`, `AuthoringLook`, and the period-bound
  replay action return no matches. Focused input-binding tests passed 4/4 with
  872 assertions; Profile build passed in 21.17 s with zero warnings/errors.
- Focused DX12 prediction automation passed in 3.91 s (exit 0) with all four
  assertions and a flat thin-line screenshot. Formal T7 gates and any
  owner-approved golden reconciliation remain pending; no baseline changed.

### T7 — Final gates and baseline reconciliation (complete 2026-07-15)

- The touched-source comment audit inspected 18/18 files: 17 hand-authored
  files have complete learning headers and local contract comments; the one
  generated reflection header retains its generated-file banner. Zero files
  are deferred or unchecked.
- After the final read-only ownership/style critique reset the generic causal
  marker emphasis to zero, the focused 1,485-frame rebuilt visual run passed in
  9.22 s: all four assertions passed, 13 trajectory records / 16,690 points
  produced 1,578 stable submitted segments with zero drops, and steady-state
  reserve growth stayed 224 -> 224. The Lane-flat capture shows a bounded cyan
  halo only on the selected root; the generic causal-entry styling is flat.
- `tools\validate_full.bat` passed from that final source in 123.31 s: mandatory CPU suites, Profile
  and Debug builds with zero warnings/errors, DX12 InfoQueue with zero errors,
  three matching screenshot baselines, standalone physics, and the 44,401-line
  byte-exact varied baseline all passed.
- `tools\validate_dx12_renderer.bat` passed from final source in 56.60 s with zero DX12 errors,
  all three screenshot comparisons green, and zero-warning Profile/Debug
  readiness builds.
- `tools\run_graphics_stress.bat 1` passed from final source in 62.14 s: 12,818 frames and 352
  scene loads completed before the PID-scoped timeout, stderr was empty, upload
  drops/flushes were zero, and shutdown memory reconciled exactly.
- `tools\validate_replay_scrub.bat --prove-failure-propagation` returned the
  required synthetic exit 37 in 0.06 s without launching an engine. The normal
  alias remains forbidden after the direct oracle.
- The owner explicitly approved the replay visual-fidelity golden refresh on
  2026-07-15. One `tools\validate_replay_visual_fidelity.bat` invocation proved
  the launcher shape, built Profile with zero warnings/errors, passed all 15
  typed-packet tests / 67 assertions, and launched exactly one engine process.
  That process generated prediction exactly once and wrote the complete report
  in 453.03 s; comparison stopped at the expected old-golden shader provenance
  difference before any offline control could run.
- From that same report, the cold approval lane regenerated
  `replay_visual_fidelity_200_box.json` with Profile working-base provenance
  `2f13168ff41c0aa524194a2c4e722c23e1e0641a`. The mechanically bound causal
  manifest was regenerated too; its approval guard proved all 2,401 activation
  rows and 199-node topology were unchanged before updating the visual-manifest
  hash/provenance metadata.
- The refreshed comparison passed at 2,401 visual ticks, 200 moved / 187 toppled
  / 200 settled wall bricks, 199 causal nodes, one prediction generation, one
  presented cascade, and 2,401 durable artifact packets. All nine focused
  false-pass controls plus the ten determinism mutations passed offline in
  39.68 s. No second engine process or normal scrub alias was launched.

## Acceptance (Plan Closure)

1. Prediction mode: near-black sky, subtle horizon, no clouds/sun/shafts,
   smooth enter/exit fade.
2. Ordinary trajectories: thin (~2 px), analytically anti-aliased,
   screen-space-constant vector lines with miter joins and round caps; no
   glow, no bloom feed.
3. Comma cycles LaneFlat → VelocityHeat → TimeGradient → PerObjectHue →
   CausalDepth, with the active mode visible in the replay overlay UI.
4. Selected-object path glows; nothing else does.
5. Authoring cycler and ribbon saturation/gain hooks are gone.
6. T7 validation evidence pasted; visual baselines intentionally reconciled.
