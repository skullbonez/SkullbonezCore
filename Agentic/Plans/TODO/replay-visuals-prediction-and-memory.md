# Replay: Trajectory Visuals, Prediction Job, Memory Quality, Code Size

Date: 2026-07-09 (consolidated mega plan)
Status: In progress - Stage 9 complete (prediction isolation, trajectory
visuals investigation, counters, memory accounting, draw-loop determinism,
same-target refresh reveal preservation, past-path visibility, contact
completeness reporting, the TrajectoryStore publication shell, the build-pass
writer migration, store-backed draw reads, the default-off legacy draw fallback,
frozen hierarchy topology/shared reveal clamp, contact-tick child activation,
the twice-run prediction determinism probe, the prediction worker job, and the
DX12 trajectory-ribbon renderer, visual polish, and replay memory data-model
tuning through presets/budget UI are complete; Stage 9 debug tooling/tests are
complete; code-size right-sizing remains open)
Impact area: replay runtime, replay prediction, trajectory overlay rendering,
DX12 transient geometry, physics stepping, UI
Consolidates: `replay-prediction-and-memory.md` (sections A/B/C — full text in
that file's git history) and the 2026-07-09 replay/prediction trajectory
visuals investigation (`REPLAY_PREDICTION_VISUALS_PLAN.md` +
`REPLAY_PREDICTION_VISUALS_PROGRESS.md`, never committed, so their substance
is carried in full here).

Context: `Runtime/Replay/` measures 18,147 lines across 24 files — 12.5% of
the engine, with the repo's largest file (`RunReplayTools.cpp`, 3,699 lines).
Replay carries the engine's only approved runtime-allocation exception. The
trajectory visuals (past paths, future prediction paths, causal markers)
flicker, pop, and shimmer, and the ribbon renderer spends heavy quad counts
and memory for poor visual quality. This plan owns making replay's trajectory
graphics clean/stable/polished AND making replay smaller, cheaper, and fully
isolated. The two goals share the same files and the same publication
contract, so they execute as one interleaved sequence (Stage table below).

Rule: do not run stages of this plan concurrently in separate sessions — they
share `RunReplayTools.cpp` and the prediction state structs, and
`TODO/interaction-state-machine.md` warns against parallel edits to replay/UI
code.

---

## Part I — Investigation record (trajectory visuals, 2026-07-09)

### System map

| Concern | File(s) | Key symbols |
|---|---|---|
| Overlay line/ribbon builder + submit | `SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp` (decl `Runtime/Tools/RuntimeTools.h`) | `EmitReplayRibbonSegmentTo`, `EmitReplayRibbonGlowPairTo`, `EmitReplayRibbonShapeOutlineTo`, `BuildReplayRibbonVertices`, `Render` |
| Frame-loop hookup | `SkullbonezSource/Runtime/Run.cpp:448-477` | tracer `Clear()` → `RenderReplayPathVisualizer` → cause-focus/velocity overlays → tracer `Render()` |
| Replay/prediction state owner | `SkullbonezSource/Runtime/Replay/ReplayRuntime.h/.cpp` | `RunReplayPredictionState` (`build`/`simulation`/`futureNodeCache`/`baseline`/`revealClock`), `PublishBuildFrameSlot` prefix contract, `ApplyPredictionFrameForRender`, ghost draw requests |
| Prediction build/step/draw | `SkullbonezSource/Runtime/Replay/RunReplayTools.cpp` | `BeginReplayPredictionJob`, `StepReplayPredictionJob`, `CaptureReplayPredictionFrame`, `SeedReplayPredictionEngine`, `DrawReplayPredictionOverlay`, `ReplayPredictionRevealFrameIndex`, `DrawReplayRootPath`, `DrawReplayChildPaths`, `BuildReplayFutureNodes` |
| Past/solver sample storage | `SkullbonezSource/Runtime/Replay/ReplayRecorder.h/.cpp` | presentation + solver ring buffers, `ForEachSampleChronological`, `REPLAY_FUTURE_BUFFER_SECONDS = 20` |
| Toggles / target picks | `RunReplayScrubberTools.cpp` (:604 predict toggle, :611 horizon), `ReplayInteractionController.cpp`, `RunReplayQueryTools.cpp:117`, `RunInput.cpp:1719` | |
| GPU submission | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp` | `DrawLinesColored` (1 px lines), `DrawTransientColoredTriangles` (`SoftAdditiveRibbon`), instanced-mesh path available |
| Ribbon shader | `SkullbonezData/shaders/soft_additive_ribbon.hlsl` | edge-feather PS, `hdrScale` feeds bloom |
| Budget constants | `SkullbonezSource/Runtime/RunInternal.h:100-101` | `REPLAY_PREDICTION_REFRESH_SECONDS = 0.35`, `REPLAY_PREDICTION_MAX_WORK_MILLISECONDS = 5.0` |

Structural facts: there is **no scene-graph parent/child transform hierarchy
on `GameModel`** — "root through children/descendants" means the causal
contact tree (`RunReplayPathTraceNode { id, parentId, firstFrame, depth,
contactDerived }`) built from `debugContacts`, with ragdoll parts collapsed
to torso. Prediction data is **already lock-step**: one private, isolated
`PhysicsEngine` copy stepped at `PHYSICS_FIXED_DT`, all bodies sampled per
tick into `RunReplayPredictionFrame`. The GPU side is already transient
(per-frame upload arena, no persistent meshes) — the churn is CPU-side.

### Confirmed failure modes (read directly from source)

1. **Draw passes are wall-clock-budgeted** (5 ms shared with prediction
   stepping and tree building); budget expiry breaks out mid-polyline, so
   visible geometry differs frame to frame with timing noise → flicker.
   Code comments document two prior flicker bugs of exactly this class
   (`RunReplayTools.cpp:3309`; the priority-ribbon buffer's reason for
   existing).
2. **Decimation phase shifts every tick**: `ordinal % stride` counted from
   the ring-buffer start (`REPLAY_PATH_MAX_SEGMENTS = 260`; 96 for retained
   trails; 261 for baseline) — the drawn sample set slides as the ring
   advances → past-path shimmer/crawl; stride also jumps at count
   thresholds.
3. **Prediction auto-refresh every 0.35 s** while uncommitted restarts the
   build and resets `revealClock.anchor` → repeated whole-tree unfold pop.
4. **Two ribbon segments per visible line step** (glow + core via
   `EmitReplayRibbonGlowPairTo`), each CPU-expanded to a camera-facing quad
   (6 verts × 11 floats). Shape outlines also route through ribbons: one
   sphere marker = 3 planes × 32 segments × 2 styles = **192 quads**.
5. **No joint welding**: per-segment side vectors → cracks + additive
   overlap doubling at joints; world-space width balloons near camera;
   **depth test OFF** for all ribbons → paths glow through geometry.
6. **Silent capacity drops**: `EmitReplayRibbonSegmentTo` refuses appends at
   capacity (32,768 segments), ordinary vs priority buffers survive
   differently → inconsistent partial frames.
7. **Per-frame heap allocation + full solver-ring re-walk per target** in
   the retained path: stack-local `targetVisualizer` with unreserved
   `futureNodes` vector, rebuilt and copied every frame
   (`RunReplayTools.cpp:3702-3766`).
8. **`frame.debugContacts` silently cleared** when the replay reserve
   refuses growth (`RunReplayTools.cpp:~2814`) → causal tree differs
   between builds.
9. **Child activation gate** `REPLAY_PREDICTION_CHILD_LINEAR_SPEED_SQ =
   8²` u/s hides slow-pushed children or pops them in late.
10. **Prediction stepping competes with drawing** on the frame loop for the
    same 5 ms (mitigated by a fresh draw timer, but build time still causes
    draw dropout) — the worker job (Stage 5) is the real fix.
11. **Everything rebuilds even when nothing changed**: committed prediction
    + paused sim still re-derives nodes, re-emits segments, re-expands
    quads, re-uploads megabytes per frame; tracer reserves ~16.5 MB vertex
    staging + ~2×1.7 MB segment + ~6 MB line buffers for throwaway output.
12. **Build→commit swap** (`simulation.frames.swap(buildFrames)`) plus
    scrub-track rescaling happen in one frame at completion.

### Hypotheses (confirm/kill in Stage 0 instrumentation)

- H1. The commit swap causes a one-frame root-line reshape.
- H2. Bloom (hdrScale up to 3.45) amplifies budget-dropout pops into
  "flashes".
- H3. During rebuild, `BuildPrefixShouldBePresented` draws the OLD committed
  future until the new prefix overtakes it → old→new snap mid-build.
- H4. Ghost draw requests (24-frame cap) pop on the same rebuild events.
- H5. Per-segment `steady_clock::now()` budget checks are measurable.

---

## Part II — Target design

### Architecture: five layers, one-way flow, explicit versioning

```
[Replay rings / Prediction engine]   (simulation state — unchanged owners)
        ▼ tick-indexed samples, published prefix
[TrajectoryBuildPass]                (generation — runs only on data change)
        ▼ versioned, immutable-once-published polylines
[TrajectoryStore]                    (storage/cache — the new center)
        ▼ read-only
[TrajectoryRenderer]                 (rendering — no time budget, no rebuild)
        ▲
[TrajectoryVisibilityControls]       (UI/debug toggles — pure flags)
```

- **TrajectoryStore** (new, `ReplayRuntime`-owned): fixed-capacity records
  keyed by `{ ReplayBodyId, lane }`, lane ∈ { PastRoot, FutureRoot,
  FutureChildIncoming, FutureChildOutgoing, RetainedTrail, BaselineRoot }.
  Record = tick-indexed control points, `version` counter,
  `publishedPointCount` prefix, style id, hierarchy fields
  (`parentId`/`depth`/`firstFrame`). Memory from the existing
  `replay_prediction_working_set` reserve owner (256 MB cap).
- **Invalidation rules** (encode as `Invariant:` comments): points append
  only under the published prefix, or the record is atomically replaced with
  `version+1`. Target/branch/scene/velocity-edit changes → full invalidate
  (existing `MarkPredictionDirty`/`ClearPathVisualizerState` sites). Reveal
  progress is a draw-time clamp in ticks, never an invalidation.
- **Tick-anchored decimation** at build time: keep a point iff
  `frameIndex % strideForRecord == 0`, plus first/last/present/child
  `firstFrame` ticks; stride chosen once per record from horizon length, not
  live ring count → no phase crawl, geometry changes only at the ends.
- **Budget the build, never the draw**: the renderer draws every published
  record fully, unconditionally; capacity is enforced at build time. Single
  writer (frame loop now, worker job after Stage 5) + published prefixes,
  generalizing the proven `buildFrameCount` pattern.

### Lock-step prediction

Ground truth exists (one engine, fixed dt, all bodies per frame). Enforce at
the draw layer: one shared `revealFrame` clamps every lane; children join at
their causal `firstFrame` sampled from the same frame arrays; the future-node
tree is **frozen per version** (building preview and committed result must
show the same node set / `firstFrame`s); replace the 8 u/s activation gate
with contact-tick activation + small accumulated-displacement threshold;
never silently drop contacts — mark the build `contactsIncomplete` and
surface it. Add a twice-run determinism probe (identical branch state →
byte-identical sampled polylines).

### Past path

One `PastRoot` record per target, append-only under tick-anchored decimation;
scrub moves only the draw-time `presentFrame` split (a shader constant, so
scrubbing re-colors without re-uploading). Explicit `pastPathVisible` toggle
(today past drawing is implicit whenever a target exists); toggle-off hides
without discarding data. Oldest points fall off retention with an age fade,
never a pop.

### Rendering strategy

- **Primary (Option A)** — vertex-shader-expanded, joint-welded ribbon:
  upload control points once per record version (position, prev, next,
  color/age, ±1 side flag); VS computes the perpendicular from prev→next →
  welded joints, constant screen-space width (world-min/max clamped);
  glow+core collapse into ONE pass (PS gradient over the existing edge
  coordinate). Depth **test on, write off**; optional dimmed depth-fail pass
  for x-ray polish. New `SkullbonezData/shaders/trajectory_ribbon.hlsl`
  registered in `Assets/AssetSystem.cpp`; new vertex layout beside
  `DrawTransientColoredTriangles`. ≥16× less vertex data per step, near-zero
  steady-state CPU.
- **Fallback (Option B)** — repaired CPU ribbon: single style pass (glow in
  existing PS), welded joints by averaging adjacent side vectors, draw from
  cache with no budget. No shader work; acceptable intermediate commit.
- **Markers**: entry/rest/horizon/baseline shape outlines leave the
  double-emit ribbon path — keep a single-pass ribbon outline only for the
  yellow entry box; grey/cyan/baseline markers use `DrawLinesColored` or a
  low-alpha instanced ghost mesh (`ReplayPredictionGhostDrawRequest` already
  exists). ≥4× marker geometry reduction.
- Not recommended: geometry shaders (portability contract), persistent GPU
  meshes per trajectory (churn), 1 px lines for hero paths.

### Memory quality (carried from the retired plan, section B)

Goal: substantially reduce replay memory while the default visual scrubber
stays lossless in look and feel. The June draft's field-level design predates
the plan-09 right-sizing and the snapshot table-drive — **re-derive the data
model against post-Stage-3 replay code before implementing**. Design intent
to carry forward:

- Body dictionary + visual delta frames instead of full per-frame body
  arrays; quantized visual modes as opt-in presets.
- Solver keyframes + deltas instead of dense solver snapshots.
- User-facing presets (Lossless Look / Balanced / Memory Saver / Diagnostics
  Heavy) with a hard budget enforced through `RuntimeReserveAllocator`.

---

## Part III — Interleaved stage sequence

Ordering rationale: visuals Stages 0–2 are small, independent, and kill most
visible flicker first. Stage 3's TrajectoryStore creates the single-writer
publication contract the worker job needs, so the worker job (old section A)
lands right after it and adopts the store. Memory tuning (old section B)
re-derives once against the settled store-based code. Code-size (old section
C) runs last, deleting code the rewrite has already orphaned.

| # | Stage | Origin | Files (expected) | Gate |
|---|---|---|---|---|
| 0 | Instrument & document | visuals P0 + B1 | `RunReplayTools.cpp`, `RunEditorTracer.cpp`, `Core/MainMemoryStats.h`, `UI/UITabMemory.cpp` | `validate_fast` |
| 1 | Deterministic drawing | visuals P1 | `RunReplayTools.cpp` | `validate_full` + `validate_replay_scrub` |
| 2 | Rebuild/reveal churn + toggles | visuals P2 | `RunReplayTools.cpp`, `ReplayRuntime.h/.cpp`, `RunReplayScrubberTools.cpp`, `ReplayOverlayRenderer.cpp` | `validate_full` + `validate_replay_scrub` |
| 3 | TrajectoryStore + build pass | visuals P3 | new `Runtime/Replay/TrajectoryStore.h/.cpp` (+vcxproj/filters), `RunReplayTools.cpp`, `ReplayRuntime.h/.cpp` | `validate_full` + `validate_replay_scrub` + `validate_perf` |
| 4 | Lock-step hierarchy correctness | visuals P4 | `RunReplayTools.cpp`, `RunInteractionAutomation.cpp`, `RunInteractionAutomationState.h` | `validate_full` + `validate_replay_scrub` + one `validate_physics` proof |
| 5 | Prediction worker job | old A1–A4 | `Core/AmortizedTask` wiring, `RunReplayTools.cpp`, `ReplayRuntime.*` | `validate_full` + renderer ×3 + `validate_perf` + both prediction proofs |
| 6 | Rendering backend (Option A; B as fallback commit) | visuals P5 | `trajectory_ribbon.hlsl`, `AssetSystem.cpp`, `IRenderCommandContext.h`, `RenderBackendDX12.DynamicGeometry.cpp/.h`, `RunEditorTracer.cpp` | `validate_dx12_renderer` ×3 + intentional baseline update |
| 7 | Visual polish | visuals P6 | shader + renderer styles | `validate_dx12_renderer` + baseline update |
| 8 | Replay memory data-model tuning | old B2–B4 | `ReplayRecorder.*`, `ReplayV2Artifact.*`, presets UI | `validate_full` + `validate_replay_scrub` per slice; determinism untouched or proven byte-exact |
| 9 | Debug tooling & tests | visuals P7 | `RunInteractionAutomation.cpp`, `RunReplayProbes.cpp`, extend `tools/validate_replay_scrub.bat` | `validate_fast` then `validate_replay_scrub` |
| 10 | Code-size right-sizing + legacy deletion | old C1–C3 + visuals P8 | `Runtime/Replay/*`, `RunEditorTracer.cpp`, `RuntimeTools.h` | `validate_full` + `validate_perf` |

### Stage checklists

Stage 0 — Instrument & document (merges old B1)
- [x] 0.1 Counters: segments emitted/dropped per lane, budget expiries per
  pass, rebuild causes, store bytes; surface via memory stats/UI tab.
  Complete 2026-07-09: added enum-indexed replay trajectory lanes, budget-pass
  expiries, rebuild causes, and store-byte slot to `MainMemoryReplayStats`;
  `RunEditorTracer` now counts emitted/dropped replay ribbon segments by lane,
  `ReplayRuntime` accumulates pass/rebuild counters, memory dump JSON exposes
  the full per-lane/per-pass breakdown, and the Memory tab shows compact
  trajectory segment/budget/rebuild lines. Validation:
  `tools\validate_fast.bat` passed (`Agentic/Reports/validate_fast_replay_visuals_stage0_1_20260709.log`).
- [x] 0.2 Per-category replay memory byte accounting (old B1) — this
  measurement decides how much of Stage 8 is worth building.
  Complete 2026-07-09: added fixed replay byte categories for presentation,
  solver, events, loaded replay, prediction, path/cause, render scratch, and
  future trajectory-store bytes. Recorder owners now contribute their own
  private ring/checkpoint/scratch/body/world/launcher/event storage, and
  `ReplayRuntime::CollectMemoryStats()` derives the broad totals from the same
  category table exposed in the memory dump JSON and Memory tab category rows.
  Validation: `tools\validate_fast.bat` passed
  (`Agentic/Reports/validate_fast_replay_visuals_stage0_2_20260709.log`).
- [x] 0.3 Manual repro session: confirm or kill H1-H5; record results here.
  Complete 2026-07-09: ran three focused Profile interaction launches plus one
  cinematic A/B pass. Local generated artifacts live under ignored
  `Agentic/Temp/replay_visuals_stage0_3_20260709/`; the tracked repro script is
  `Agentic/Reports/replay_visuals_stage0_3_rebuild_repro_20260709.json`.
  Documentation-only/repro-only slice, so no repository validation gate was
  required.

  Launch evidence:
  - Wall build repro:
    `Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\prediction_ragdoll_wall_200.scene.json --interaction-script SkullbonezData\interaction\prediction_ragdoll_wall_200_predict.json --interaction-report Agentic\Reports\replay_visuals_stage0_3_wall_existing_report_20260709.json --frames 240 --replay on --replay-seconds 3 --fixed-step --vsync off --memory-dump Agentic\Reports\replay_visuals_stage0_3_wall_existing_memory_20260709.json`
    passed in 00:00:05.015. Prediction path visible, live solver hash stable,
    active prefix 343 frames, future nodes 45, target displacement 197.886u.
    Counters: `prediction_step=224`, `retained_bounds=113`, dirty rebuilds 1,
    prediction bytes 185,731,324, render ghost request reserve 11,075,584 bytes.
  - Simple complete-build repro:
    `Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\replay_prediction_simple.scene.json --interaction-script SkullbonezData\interaction\replay_prediction_simple_verify.json --interaction-report Agentic\Reports\replay_visuals_stage0_3_simple_existing_report_20260709.json --frames 160 --replay on --replay-seconds 4 --fixed-step --vsync off --memory-dump Agentic\Reports\replay_visuals_stage0_3_simple_existing_memory_20260709.json`
    passed in 00:00:03.024. Final state had committed prediction frames
    (`predictionFrameCount=2401`, `predictionBuildFrameCount=0`), future nodes
    3, target displacement 86.38u. Counters: `prediction_step=2`,
    `retained_bounds=2`, dirty rebuilds 1.
  - Wall cinematic A/B:
    same wall script with `--cinematic off` passed in 00:00:04.024. The saved
    frame stayed visually/photometrically equivalent to the cinematic-on frame:
    mean RGB 51.74/54.81/52.22 vs 51.74/54.81/52.21, bright pixels 9,317 vs
    9,294, white-ish pixels 11,384 vs 11,365.
  - Rebuild/nudge repro:
    `Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\replay_prediction_simple.scene.json --interaction-script Agentic\Reports\replay_visuals_stage0_3_rebuild_repro_20260709.json --interaction-report Agentic\Reports\replay_visuals_stage0_3_rebuild_repro_report_20260709.json --frames 180 --replay on --replay-seconds 4 --fixed-step --vsync off --memory-dump Agentic\Reports\replay_visuals_stage0_3_rebuild_repro_memory_20260709.json`
    passed in 00:00:03.096. The scripted velocity nudge captured a baseline
    (`predictionBaselineVisible=true`, 181 baseline root points, 4 baseline
    body poses) and forced a second dirty rebuild. Final counters:
    `dirty=2`, `prediction_begin=1`, `retained_bounds=1`,
    `baseline_root` emitted segments 115,656, future-root emitted segments
    4,326, dropped segments 0.

  Hypothesis decisions:
  - H1 killed for the small complete-build repro. The simple scene reached the
    committed simulation-frame path (`predictionBuildFrameCount=0`) and the
    frame 42/70/120 captures did not show a one-frame root-line reshape. The
    wall scene did not complete its long 20s horizon inside 240 frames, so there
    is no heavy-wall commit-swap proof yet; Stage 9's planned geometry hash is
    the right strict detector.
  - H2 killed as a cinematic-bloom-specific root cause. The wall impact remains
    blown out, but `--cinematic off` produced essentially the same brightness
    and white-pixel counts. Treat the visible flash as additive ribbon/marker
    overdraw plus draw/build churn, not something Stage 6 can solve merely by
    disabling cinematic bloom.
  - H3 confirmed. The rebuild/nudge run captured the old baseline, then drew the
    shifted future immediately after the dirty rebuild. The early/mid/late
    captures show the old baseline marker/line and new future line coexisting
    while the dirty rebuild publishes, which matches the old-to-new snap
    described by `BuildPrefixShouldBePresented`.
  - H4 killed for the default repro and left as a ragdoll-toggle-only follow-up.
    Both wall and rebuild runs kept `RAGDOLL` off; final queued
    `ghost_requests=0` while the reserve remained 11,075,584 bytes. Source
    review confirms `BuildPredictionGhostDrawRequests()` returns false unless
    `ragdollVisualsEnabled` is true and the interaction automation currently has
    no `clickReplayControl: "ragdoll"` hook. Stage 9 should add that hook before
    promoting ghost-pop evidence into validation.
  - H5 confirmed as frame-budget pressure, with self-time still to be isolated.
    The wall run recorded 224 prediction-step budget expiries and 113 retained
    bounds expiries, and source review shows those checks route through
    `ReplayPredictionElapsedMilliseconds()` / `steady_clock::now()` from
    per-sample/per-frame draw and build loops. This is enough to justify Stage
    1 removing draw-loop wall-clock checks; a later perf probe can isolate the
    clock-call self-cost if needed.

Stage 1 — Deterministic drawing
- [x] 1.1 Remove wall-clock budget checks from *draw* loops (keep on
  build/tree passes).
  Complete 2026-07-10: removed retained and prediction draw-loop
  `steady_clock`/budget-pass bailouts from root, child, affected-body, ragdoll,
  marker, and baseline drawing. Kept wall-clock checks on prediction begin/step,
  retained bounds, retained build-tree, and prediction future-node build work.
- [x] 1.2 Tick-anchored decimation replacing `ordinal % stride` everywhere
  (260 / 96 / 261 sites).
  Complete 2026-07-10: replaced draw thinning that depended on visitor ordinal
  or frame slot with `ReplayFrameIndex` anchored stride tests, including
  retained root/child paths, prediction root/child paths, retained marker trails,
  ragdoll torso trails, affected-body trails, and the baseline root capture.
- [x] 1.3 Overflow → build-time caps; draw can never overflow.
  Complete 2026-07-10: added a frame-local ordinary replay-ribbon quota backed
  by `RunEditorTracer::ReplayPathRibbonSegmentCapacityRemaining()`. Ordinary
  trajectory and baseline path segments now pre-check both the shared quota and
  live tracer capacity before calling the tracer, so marker-outline consumption
  cannot stale the cap and path drawing never relies on tracer overflow drops.
  Touched-file comment audit inspected 3 source-bearing files
  (`RunReplayTools.cpp`, `RunEditorTracer.cpp`, `RuntimeTools.h`) with 0
  deferred. Validation: first `tools\validate_full.bat` attempt built Profile
  and Debug with 0 warnings/errors but failed formatting on `RunReplayTools.cpp`;
  after targeted clang-format, `tools\validate_full.bat` passed
  (`Agentic/Reports/validate_full_replay_visuals_stage1_20260710_r2.log`) with
  project filters clean, Profile/Debug builds clean, DX12 validation errors 0,
  DX12 screenshots matching baselines, and `physics_regression_solver.csv`
  byte-exact. `tools\validate_replay_scrub.bat` passed
  (`Agentic/Reports/validate_replay_scrub_replay_visuals_stage1_20260710.log`):
  scrub trace command `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off
  --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames
  120 --replay on --replay-seconds 1 --replay-scrub-test --physics-diag
  Debug\replay_scrub.physicsdiag.ndjson`; query
  `tools\physics_query.bat Debug\replay_scrub.physicsdiag.ndjson replay --limit
  8`; restore trace command `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync
  off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json
  --frames 120 --replay on --replay-seconds 1 --replay-restore-test
  --physics-diag Debug\replay_restore.physicsdiag.ndjson`; restore query
  `tools\physics_query.bat Debug\replay_restore.physicsdiag.ndjson restore
  --limit 8`. SkullScope data sizes: scrub trace 54,932 bytes, scrub SQLite
  225,280 bytes, scrub query output 1,512 bytes; restore trace 54,912 bytes,
  restore SQLite 225,280 bytes, restore query output 967 bytes.

Stage 2 — Rebuild/reveal churn + visibility controls
- [x] 2.1 Auto-refresh keeps the previous committed future + reveal progress
  until the new build's prefix reaches the current reveal cursor (no unfold
  restart on refresh). Preserve the demo-director
  `revealClock.secondsPerSecond` contract (`RunDemoDirector.cpp`).
  Complete 2026-07-10: same-target prediction refreshes now capture the
  currently revealed frame count before job reset, keep the committed future and
  future-node cache visible, and present the rebuilding `buildFrames` prefix
  only after the published prefix reaches that captured reveal cursor. Empty
  replacement jobs, target changes, disabled prediction, and scene-physics-off
  transitions still clear samples and restart the reveal anchor. The
  demo-director pacing contract is preserved because refresh setup does not
  mutate `revealClock.secondsPerSecond`. Prediction setup/step failure paths
  keep committed samples when an auto-refresh already has a visible future.
  Touched-file comment audit inspected 2 source-bearing files
  (`ReplayRuntime.h`, `RunReplayTools.cpp`) with 0 deferred. Focused
  `tools\validate_build.bat Profile` passed in 9.96s with 0 warnings/errors
  (`Agentic/Reports/validate_build_profile_replay_stage2_1_20260710.log`).
  Required Stage 2 gate: `tools\validate_full.bat` passed in 45.89s
  (`Agentic/Reports/validate_full_replay_visuals_stage2_1_20260710.log`) with
  project filters clean, Profile/Debug builds clean, formatting clean, DX12
  validation errors 0, DX12 screenshots matching committed baselines, and
  `physics_regression_solver.csv` byte-exact; `tools\validate_replay_scrub.bat`
  passed in 10.78s
  (`Agentic/Reports/validate_replay_scrub_replay_visuals_stage2_1_20260710.log`).
  SkullScope scrub command: `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync
  off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json
  --frames 120 --replay on --replay-seconds 1 --replay-scrub-test
  --physics-diag Debug\replay_scrub.physicsdiag.ndjson`; query:
  `tools\physics_query.bat Debug\replay_scrub.physicsdiag.ndjson replay --limit
  8`. Restore command: `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off
  --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames
  120 --replay on --replay-seconds 1 --replay-restore-test --physics-diag
  Debug\replay_restore.physicsdiag.ndjson`; restore query:
  `tools\physics_query.bat Debug\replay_restore.physicsdiag.ndjson restore
  --limit 8`. SkullScope data sizes: scrub trace 54,932 bytes, scrub SQLite
  225,280 bytes, scrub query output 1,512 bytes; restore trace 54,912 bytes,
  restore SQLite 225,280 bytes, restore query output 967 bytes; model-read query
  output total 2,479 bytes.
- [x] 2.2 Explicit `pastPathVisible` toggle; past lane renders iff target +
  toggle.
  Complete 2026-07-10: added replay-owned `pastPathVisible` and hover state,
  a `PAST` scrubber checkbox that shares layout/hitbox helpers with the rest of
  the scrubber, and a `clickReplayControl: "past"` / `"pastPath"` automation
  hook. Retained-path drawing now clears its automation-facing retained-node
  cache and returns unless both a path target exists and the past lane is
  visible; future prediction keeps its separate cache. Runtime transition,
  unfocused input, and unavailable-scrubber paths clear only hover state so the
  operator's visibility choice survives ordinary replay state churn.
- [x] 2.3 `contactsIncomplete` flag instead of silent `debugContacts.clear()`;
  surfaced in overlay UI + automation report.
  Complete 2026-07-10: `RunReplayPredictionFrame` now records
  `contactsIncomplete` when the replay reserve refuses a dense debug-contact
  payload. Prediction capture still publishes the body sample frame so the root
  trajectory remains usable, while the scrubber overlay labels the prediction
  `CONTACTS PARTIAL` and interaction automation reports
  `predictionContactsIncomplete`. Touched-file comment audit inspected 9
  source-bearing files (`ReplayRuntime.h`, `ReplayOverlayLayout.h`,
  `ReplayOverlayLayout.cpp`, `ReplayOverlayRenderer.cpp`,
  `RunReplayScrubberTools.cpp`, `RunReplayTools.cpp`, `ReplayRuntime.cpp`,
  `RunInput.cpp`, `RunInteractionAutomation.cpp`) with 0 deferred. Focused
  `tools\validate_build.bat Profile` passed in 9.78s with 0 warnings/errors
  (`Agentic/Reports/validate_build_profile_replay_stage2_2_3_20260710.log`).
  Required Stage 2 gate: `tools\validate_full.bat` passed in 40.05s
  (`Agentic/Reports/validate_full_replay_visuals_stage2_2_3_20260710.log`)
  with project filters clean, Profile/Debug builds clean, DX12 validation
  errors 0, DX12 screenshots matching committed baselines, and
  `physics_regression_solver.csv` byte-exact. `tools\validate_replay_scrub.bat`
  passed in 10.89s
  (`Agentic/Reports/validate_replay_scrub_replay_visuals_stage2_2_3_20260710.log`).
  SkullScope scrub command: `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync
  off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json
  --frames 120 --replay on --replay-seconds 1 --replay-scrub-test
  --physics-diag Debug\replay_scrub.physicsdiag.ndjson`; query:
  `tools\physics_query.bat Debug\replay_scrub.physicsdiag.ndjson replay --limit
  8`. Restore command: `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off
  --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames
  120 --replay on --replay-seconds 1 --replay-restore-test --physics-diag
  Debug\replay_restore.physicsdiag.ndjson`; restore query:
  `tools\physics_query.bat Debug\replay_restore.physicsdiag.ndjson restore
  --limit 8`. SkullScope data sizes: scrub trace 54,932 bytes, scrub SQLite
  225,280 bytes, scrub query output 1,512 bytes; restore trace 54,912 bytes,
  restore SQLite 225,280 bytes, restore query output 967 bytes; model-read query
  output total 2,479 bytes.

Stage 3 — TrajectoryStore + build pass
- [x] 3.1 Store records / versioning / published prefixes under
  `replay_prediction_working_set`.
  Complete 2026-07-10: added `ReplayPredictionReserve.h/.cpp` as the single
  owner wrapper for `replay_prediction_working_set` growth requests and added
  `TrajectoryStore.h/.cpp` with lane-keyed records, record versions,
  published point prefixes, capacity-checked appends, point/record reserve
  helpers, and store-byte accounting. `ReplayRuntime` now owns and clears the
  store with prediction state, reports its capacity through replay memory
  categories, and reserves large replay-only cause-row/ghost-request buffers
  only when replay capture/hash logging is configured so non-replay perf scenes
  do not carry dormant visualization capacity. Project/filter rules and the
  allocation allowlist were updated for the new replay files. The perf baseline
  JSONs were intentionally refreshed after `validate_perf` showed the old
  branch baseline had a stale ~49 MB process working-set offset; absolute perf
  budgets stayed clean, the allocation guard reported 0 gameplay violations,
  and a final `validate_perf` pass proved the refreshed baselines.

  Comment audit: inspected 8 touched source-bearing files with 0 deferred
  (`ReplayPredictionReserve.h/.cpp`, `TrajectoryStore.h/.cpp`,
  `ReplayRuntime.h/.cpp`, `RunReplayTools.cpp`,
  `tools/validate_project_filters.py`). Focused checks: allocation policy
  self-test passed, allocation scan passed
  (`scanned=296 direct_heap_findings=28 dynamic_stl_member_findings=0
  allowlist_errors=0`), `tools\validate_project_filters.bat` passed in 1.28s,
  and focused `tools\validate_build.bat Profile` passed in 5.44s with 0
  warnings/errors. Memory investigation showed non-replay perf-scene tracked
  replay bytes drop from 49.37 MB to 0.77 MB after moving the large replay-only
  reserves, and the allocation guard startup allocation bytes dropped from
  ~248.4 MB to ~199.8 MB.

  Required validation: `tools\validate_fast.bat` passed in 19.38s
  (`Agentic/Reports/validate_fast_replay_stage3_1_20260710.log`);
  `tools\validate_full.bat` passed in 31.89s
  (`Agentic/Reports/validate_full_replay_visuals_stage3_1_20260710.log`) with
  project filters clean, Profile/Debug builds clean, DX12 validation errors 0,
  DX12 screenshots matching committed baselines, and
  `physics_regression_solver.csv` byte-exact; `tools\validate_replay_scrub.bat`
  passed in 11.09s
  (`Agentic/Reports/validate_replay_scrub_replay_visuals_stage3_1_20260710.log`);
  and `tools\validate_perf.bat` passed in 30.79s
  (`Agentic/Reports/validate_perf_replay_stage3_1_20260710.log`).

  SkullScope accounting from the final replay scrub gate:
  - Scrub trace command: `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-scrub-test --physics-diag Debug\replay_scrub.physicsdiag.ndjson`
  - Scrub query: `tools\physics_query.bat Debug\replay_scrub.physicsdiag.ndjson replay --limit 8`; trace 54,932 bytes; SQLite cache 225,280 bytes; query output 1,512 bytes.
  - Restore trace command: `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-restore-test --physics-diag Debug\replay_restore.physicsdiag.ndjson`
  - Restore query: `tools\physics_query.bat Debug\replay_restore.physicsdiag.ndjson restore --limit 8`; trace 54,912 bytes; SQLite cache 225,280 bytes; query output 967 bytes.
  - Total model-read query output: 2,479 bytes. Raw NDJSON/SQLite sizes above
    are artifact sizes, not model-ingested text.
- [x] 3.2 Build pass fed by prediction publish events + solver ring appends;
  incremental cursors preserved.
  Complete 2026-07-10: the trajectory store now has active build writers for
  both solver-retained past paths and prediction futures. `ReplayRuntime`
  maintains a `PastRoot` cursor keyed by selected target, retained solver
  window, and recorder eviction count; it rebuilds from the chronological
  solver ring when target/window/eviction state changes and appends the newest
  solver sample during capture when the cursor is still valid. Prediction root
  samples publish into a build branch as `PublishBuildFrameSlot()` advances,
  then rebuild the committed `FutureRoot` branch after the build frames swap
  into the visible prediction. Future child incoming/outgoing records catch up
  from the published prefix and frozen future-node topology, with build-vs-
  committed branch ordinals kept separate so a same-target refresh cannot
  overwrite the previous visible future early. The legacy draw path still reads
  the old frame vectors until Stage 3.3 switches readers to the store and
  deletes the per-frame `targetVisualizer` rebuild/copy path.

  Comment audit: inspected 3 touched source-bearing files with 0 deferred
  (`ReplayRuntime.h`, `ReplayRuntime.cpp`, `RunReplayTools.cpp`). Focused
  checks passed: `tools\validate_build.bat Profile` in 9.20s with 0
  warnings/errors (`Agentic/Reports/validate_build_profile_replay_stage3_2_20260710.log`);
  `tools\validate_format.bat` passed
  (`Agentic/Reports/validate_format_replay_stage3_2_20260710.log`);
  allocation policy self-test passed; allocation scan passed
  (`scanned=296 direct_heap_findings=28 dynamic_stl_member_findings=0
  allowlist_errors=0`). Required validation passed:
  `tools\validate_full.bat` in ~47s
  (`Agentic/Reports/validate_full_replay_visuals_stage3_2_20260710.log`) with
  project filters clean, Profile/Debug builds clean, formatting clean, DX12
  validation errors 0, DX12 screenshots matching committed baselines, and
  `physics_regression_solver.csv` byte-exact; `tools\validate_replay_scrub.bat`
  in ~11s
  (`Agentic/Reports/validate_replay_scrub_replay_visuals_stage3_2_20260710.log`);
  and `tools\validate_perf.bat` in 28.47s
  (`Agentic/Reports/validate_perf_replay_stage3_2_20260710.log`) with
  allocation guard PASS, `gameplay_violations=0`, runtime reserve
  `policy_violations=0`, absolute DX12/PHYSICS_BENCH budgets clean, and no
  perf regressions.

  SkullScope accounting from the replay scrub gate:
  - Scrub trace command: `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-scrub-test --physics-diag Debug\replay_scrub.physicsdiag.ndjson`
  - Scrub query: `tools\physics_query.bat Debug\replay_scrub.physicsdiag.ndjson replay --limit 8`; trace 54,932 bytes; SQLite cache 225,280 bytes; query output 1,512 bytes.
  - Restore trace command: `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-restore-test --physics-diag Debug\replay_restore.physicsdiag.ndjson`
  - Restore query: `tools\physics_query.bat Debug\replay_restore.physicsdiag.ndjson restore --limit 8`; trace 54,912 bytes; SQLite cache 225,280 bytes; query output 967 bytes.
  - Total model-read query output: 2,479 bytes. Raw NDJSON/SQLite sizes above
    are artifact sizes, not model-ingested text.
- [x] 3.3 Draw passes read the store only; delete per-frame `targetVisualizer`
  rebuild + `futureNodes` copy (kills the per-frame allocation).
  Complete 2026-07-10: main trajectory ribbons now read the published
  `TrajectoryStore` records instead of walking prediction or solver frame
  vectors at draw time. Prediction root uses the `FutureRoot` lane, child
  incoming/outgoing ribbons use branch-separated `FutureChildIncoming` and
  `FutureChildOutgoing` records, and retained past root drawing recolors the
  selected `PastRoot` record around the current solver scrub frame. The
  butterfly baseline root is mirrored into the `BaselineRoot` lane so the
  baseline polyline follows the same draw contract; baseline entry/rest boxes
  still use retained pose data because they need orientation, not just points.

  The retained path visualizer no longer builds a per-frame
  `targetVisualizer`, no longer rebuilds retained future nodes from solver
  callbacks, and no longer copies `futureNodes` back into the live visualizer.
  It clears the retained report, refreshes the past trajectory store from the
  solver ring, draws the selected root from `PastRoot`, and resolves only the
  live target marker from the body/collider stores. Retained causal marker
  trails now look up matching `FutureChildOutgoing` records when available.
  The remaining frame-vector readers are intentionally marker/auxiliary paths:
  child/root rest and entry markers need orientation, while affected-body and
  ragdoll trails need velocity/model semantics that the Stage-3 trajectory
  lanes do not publish.

  Comment audit: inspected 2 touched source-bearing files with 0 deferred
  (`ReplayRuntime.h`, `RunReplayTools.cpp`). Focused checks passed:
  `tools\validate_build.bat Profile` in ~9.6s with 0 warnings/errors
  (`Agentic/Reports/validate_build_profile_replay_stage3_3_20260710.log`);
  `tools\validate_format.bat` passed
  (`Agentic/Reports/validate_format_replay_stage3_3_20260710.log`);
  allocation policy self-test passed; allocation scan passed
  (`scanned=296 direct_heap_findings=28 dynamic_stl_member_findings=0
  allowlist_errors=0`); and `git diff --check` passed. Required validation
  passed: `tools\validate_full.bat` in 39.58s
  (`Agentic/Reports/validate_full_replay_visuals_stage3_3_20260710.log`) with
  project filters clean, Profile/Debug builds clean, formatting clean, DX12
  validation errors 0, DX12 screenshots matching committed baselines, and
  `physics_regression_solver.csv` byte-exact; `tools\validate_replay_scrub.bat`
  in 10.93s
  (`Agentic/Reports/validate_replay_scrub_replay_visuals_stage3_3_20260710.log`);
  and `tools\validate_perf.bat` in 28.46s
  (`Agentic/Reports/validate_perf_replay_stage3_3_20260710.log`) with
  allocation guard PASS, `gameplay_violations=0`, runtime reserve
  `policy_violations=0`, absolute DX12/PHYSICS_BENCH budgets clean, and no
  perf regressions.

  SkullScope accounting from the replay scrub gate:
  - Scrub trace command: `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-scrub-test --physics-diag Debug\replay_scrub.physicsdiag.ndjson`
  - Scrub query: `tools\physics_query.bat Debug\replay_scrub.physicsdiag.ndjson replay --limit 8`; trace 54,932 bytes; SQLite cache 225,280 bytes; query output 1,512 bytes.
  - Restore trace command: `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-restore-test --physics-diag Debug\replay_restore.physicsdiag.ndjson`
  - Restore query: `tools\physics_query.bat Debug\replay_restore.physicsdiag.ndjson restore --limit 8`; trace 54,912 bytes; SQLite cache 225,280 bytes; query output 967 bytes.
  - Total model-read query output: 2,479 bytes. Raw NDJSON/SQLite sizes above
    are artifact sizes, not model-ingested text.
- [x] 3.4 Keep old draw path behind a compile-time fallback for one commit.
  Complete 2026-07-10: added
  `SKULLBONEZ_REPLAY_LEGACY_TRAJECTORY_DRAW_FALLBACK`, defaulting to `0`, as
  the one-commit rollback switch for the old retained solver-callback draw
  route. The guarded legacy path restores the pre-store retained bounds scan,
  stack-local `targetVisualizer` future-node rebuild, old root/child solver
  visitors, future-node report copy, and target marker draw only when the macro
  is deliberately enabled. The normal runtime path remains the Stage 3.3
  store-backed route: with the macro off, retained drawing refreshes
  `PastRoot` in `TrajectoryStore` and never compiles the legacy callbacks.

  The owning source comment names the fallback owner, reason, deletion
  condition, and checker budget. Delete the macro and every guarded helper after
  the next replay checkpoint validates the store path; before closing Stage 4,
  search for `SKULLBONEZ_REPLAY_LEGACY_TRAJECTORY_DRAW_FALLBACK` so the fallback
  does not quietly become permanent.

  Comment audit: inspected 1 touched source-bearing file with 0 deferred
  (`RunReplayTools.cpp`). Focused checks passed: default
  `tools\validate_build.bat Profile` in 6.99s with 0 warnings/errors
  (`Agentic/Reports/validate_build_profile_replay_stage3_4_20260710.log`);
  `tools\validate_format.bat` in 10.20s
  (`Agentic/Reports/validate_format_replay_stage3_4_20260710.log`);
  legacy-on Profile rebuild with
  `CL=/DSKULLBONEZ_REPLAY_LEGACY_TRAJECTORY_DRAW_FALLBACK=1` in 21.85s
  (`Agentic/Reports/rebuild_profile_replay_stage3_4_legacy_on_20260710.log`);
  default macro-off Profile rebuild in 21.58s
  (`Agentic/Reports/rebuild_profile_replay_stage3_4_default_20260710.log`);
  allocation policy self-test passed; and allocation scan passed
  (`scanned=296 direct_heap_findings=28 dynamic_stl_member_findings=0
  allowlist_errors=0`). Required validation passed:
  `tools\validate_full.bat` in 36.31s
  (`Agentic/Reports/validate_full_replay_visuals_stage3_4_20260710.log`) with
  project filters clean, Profile/Debug builds clean, formatting clean, DX12
  validation errors 0, DX12 screenshots matching committed baselines, and
  `physics_regression_solver.csv` byte-exact; `tools\validate_replay_scrub.bat`
  in 10.80s
  (`Agentic/Reports/validate_replay_scrub_replay_visuals_stage3_4_20260710.log`);
  and `tools\validate_perf.bat` in 28.54s
  (`Agentic/Reports/validate_perf_replay_stage3_4_20260710.log`) with
  allocation guard PASS, `gameplay_violations=0`, runtime reserve
  `policy_violations=0`, absolute DX12/PHYSICS_BENCH budgets clean, and no
  perf regressions.

  SkullScope accounting from the replay scrub gate:
  - Scrub trace command: `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-scrub-test --physics-diag Debug\replay_scrub.physicsdiag.ndjson`
  - Scrub query: `tools\physics_query.bat Debug\replay_scrub.physicsdiag.ndjson replay --limit 8`; trace 54,932 bytes; SQLite cache 225,280 bytes; query output 1,512 bytes.
  - Restore trace command: `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-restore-test --physics-diag Debug\replay_restore.physicsdiag.ndjson`
  - Restore query: `tools\physics_query.bat Debug\replay_restore.physicsdiag.ndjson restore --limit 8`; trace 54,912 bytes; SQLite cache 225,280 bytes; query output 967 bytes.
  - Total model-read query output: 2,479 bytes. Raw NDJSON/SQLite sizes above
    are artifact sizes, not model-ingested text.

Stage 4 — Lock-step hierarchy correctness
- [x] 4.1 Frozen tree per version; shared `revealFrame` clamp invariant.
  Complete 2026-07-10: future-node topology now publishes a monotonic
  `futureNodesTopologyVersion` whenever the node set/order/`firstFrame` values
  change. The next-version counter survives cache clears, so same-root rebuilds
  cannot reuse an older topology identity. Child trajectory records store the
  topology version they were built against, and prediction child drawing now
  requires the cache version, trajectory-build version, root id, build/commit
  branch, node count, and populated frame prefix to agree before any child lane
  is emitted.

  The prediction overlay now builds one `ReplayPredictionDrawFrameWindow` from
  the selected active frame prefix. Root, child, marker, affected-body, ragdoll,
  and retained-marker paths all consume that single `lastFrame` /
  `revealFrame` / stride bundle, which makes the shared reveal-clamp invariant
  explicit in source instead of relying on parallel locals.

  Comment audit: inspected 2 touched source-bearing files with 0 deferred
  (`ReplayRuntime.h`, `RunReplayTools.cpp`). Focused checks passed:
  `tools\validate_build.bat Profile` in 10.05s with 0 warnings/errors
  (`Agentic/Reports/validate_build_profile_replay_stage4_1_20260710.log`);
  `tools\validate_format.bat` in 11.10s
  (`Agentic/Reports/validate_format_replay_stage4_1_20260710.log`);
  allocation policy self-test passed; and allocation scan passed
  (`scanned=296 direct_heap_findings=28 dynamic_stl_member_findings=0
  allowlist_errors=0`). Required validation passed:
  `tools\validate_full.bat` in 38.46s
  (`Agentic/Reports/validate_full_replay_visuals_stage4_1_20260710.log`) with
  project filters clean, Profile/Debug builds clean, formatting clean, DX12
  validation errors 0, DX12 screenshots matching committed baselines, and
  `physics_regression_solver.csv` byte-exact; `tools\validate_replay_scrub.bat`
  in 10.81s
  (`Agentic/Reports/validate_replay_scrub_replay_visuals_stage4_1_20260710.log`);
  and `tools\validate_physics.bat` in 13.36s
  (`Agentic/Reports/validate_physics_replay_stage4_1_20260710.log`) with
  `VALIDATE_PHYSICS: ALL PASSED` and byte-exact solver baseline.

  SkullScope accounting from the replay scrub gate:
  - Scrub trace command: `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-scrub-test --physics-diag Debug\replay_scrub.physicsdiag.ndjson`
  - Scrub query: `tools\physics_query.bat Debug\replay_scrub.physicsdiag.ndjson replay --limit 8`; trace 54,932 bytes; SQLite cache 225,280 bytes; query output 1,512 bytes.
  - Restore trace command: `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-restore-test --physics-diag Debug\replay_restore.physicsdiag.ndjson`
  - Restore query: `tools\physics_query.bat Debug\replay_restore.physicsdiag.ndjson restore --limit 8`; trace 54,912 bytes; SQLite cache 225,280 bytes; query output 967 bytes.
  - Total model-read query output: 2,479 bytes. Raw NDJSON/SQLite sizes above
    are artifact sizes, not model-ingested text.
- [x] 4.2 Contact-tick child activation (replace 8 u/s gate).
  Complete 2026-07-10: contact-derived future nodes already use the solver
  debug-contact tick as `firstFrame`. The sparse-contact affected-body fallback
  now waits for 0.05 units of accumulated or net displacement from the first
  prediction sample before adding a child node, and it stamps the node on that
  replay frame. The old 8 u/s instantaneous speed threshold remains only for
  rest-marker and auxiliary-trail "moving" checks, so a one-frame velocity spike
  can no longer reorder the causal child tree while slow pushes still reveal at
  the first visible movement tick.

  Touched-source comment audit inspected 1 source-bearing file with 0 deferred
  (`RunReplayTools.cpp`); no subsystem checklist was required for this
  touched-file pass. Focused checks passed: `tools\validate_format.bat` in
  9.90s (`Agentic/Reports/validate_format_replay_stage4_2_20260710.log`);
  `tools\validate_build.bat Profile` in 6.55s with 0 warnings/errors
  (`Agentic/Reports/validate_build_profile_replay_stage4_2_20260710.log`);
  allocation policy self-test in 0.09s
  (`Agentic/Reports/allocation_policy_self_test_replay_stage4_2_20260710.log`);
  and allocation scan in 3.11s
  (`Agentic/Reports/allocation_policy_scan_replay_stage4_2_20260710.log`,
  `scanned=296 direct_heap_findings=28 dynamic_stl_member_findings=0
  allowlist_errors=0`). Required validation passed: `tools\validate_full.bat`
  in 35.63s
  (`Agentic/Reports/validate_full_replay_visuals_stage4_2_20260710.log`) with
  formatting clean, Profile/Debug builds clean, DX12 validation errors 0, DX12
  screenshots matching committed baselines, and `physics_regression_solver.csv`
  byte-exact; `tools\validate_replay_scrub.bat` in 10.81s
  (`Agentic/Reports/validate_replay_scrub_replay_visuals_stage4_2_20260710.log`);
  and `tools\validate_physics.bat` in 13.45s
  (`Agentic/Reports/validate_physics_replay_stage4_2_20260710.log`) with
  `VALIDATE_PHYSICS: ALL PASSED` and byte-exact solver baseline.

  SkullScope accounting from the replay scrub gate:
  - Scrub trace command: `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-scrub-test --physics-diag Debug\replay_scrub.physicsdiag.ndjson`
  - Scrub query: `tools\physics_query.bat Debug\replay_scrub.physicsdiag.ndjson replay --limit 8`; trace 54,932 bytes; SQLite cache 225,280 bytes; query output 1,512 bytes.
  - Restore trace command: `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-restore-test --physics-diag Debug\replay_restore.physicsdiag.ndjson`
  - Restore query: `tools\physics_query.bat Debug\replay_restore.physicsdiag.ndjson restore --limit 8`; trace 54,912 bytes; SQLite cache 225,280 bytes; query output 967 bytes.
  - Total model-read query output: 2,479 bytes. Raw NDJSON/SQLite sizes above
    are artifact sizes, not model-ingested text.
- [x] 4.3 Twice-run prediction determinism automation probe.
  Complete 2026-07-10: interaction automation reports now emit
  `predictionTrajectoryFingerprintReady`, `predictionTrajectoryFingerprint`,
  `predictionTrajectoryRecordCount`, and `predictionTrajectoryPointCount`. The
  fingerprint hashes published trajectory records, hierarchy metadata, and
  point bytes while intentionally excluding transient record versions and vector
  capacity. The new `predictionTrajectoryFingerprintReady` assertion lets
  scripts fail cleanly if the visible prediction prefix is not populated.

  Added `SkullbonezData/interaction/prediction_determinism_probe.json` and
  `tools\check_replay_prediction_determinism.py`. The checker launches the
  same fixed-step ragdoll-wall prediction script twice through
  `Debug\SKULLBONEZ_CORE.exe`, compares the fingerprint plus active prefix
  count, record count, point count, future-node count, and future-node build
  frame count, and stores bounded stdout/stderr excerpts. It is wired into
  `tools\validate_replay_scrub.bat` as the third replay probe.

  Touched-source comment audit inspected 4 source-bearing/tool files with 0
  deferred (`RunInteractionAutomation.cpp`, `RunInteractionAutomationState.h`,
  `tools/check_replay_prediction_determinism.py`,
  `tools/validate_replay_scrub.bat`); no subsystem checklist was required for
  this touched-file pass. Focused checks passed: Python syntax check for
  `tools/check_replay_prediction_determinism.py`; `tools\validate_format.bat`
  in 9.92s (`Agentic/Reports/validate_format_replay_stage4_3_20260710.log`);
  `tools\validate_build.bat Profile` in 9.76s with 0 warnings/errors
  (`Agentic/Reports/validate_build_profile_replay_stage4_3_20260710.log`);
  allocation policy self-test in 0.08s
  (`Agentic/Reports/allocation_policy_self_test_replay_stage4_3_20260710.log`);
  allocation scan in 3.10s
  (`Agentic/Reports/allocation_policy_scan_replay_stage4_3_20260710.log`,
  `scanned=296 direct_heap_findings=28 dynamic_stl_member_findings=0
  allowlist_errors=0`); and `tools\validate_fast.bat` in 20.41s
  (`Agentic/Reports/validate_fast_replay_stage4_3_20260710.log`). Required
  validation passed: `tools\validate_full.bat` in 29.47s
  (`Agentic/Reports/validate_full_replay_visuals_stage4_3_20260710.log`) with
  formatting clean, Profile/Debug builds clean, DX12 validation errors 0, DX12
  screenshots matching committed baselines, and `physics_regression_solver.csv`
  byte-exact; `tools\validate_replay_scrub.bat` in 59.86s
  (`Agentic/Reports/validate_replay_scrub_replay_visuals_stage4_3_20260710.log`)
  with the new prediction trajectory fingerprint
  `0x395F6E239C82A7B4` matching across two runs (401 records, 56,881 points,
  281 active frames); and `tools\validate_physics.bat` in 13.49s
  (`Agentic/Reports/validate_physics_replay_stage4_3_20260710.log`) with
  `VALIDATE_PHYSICS: ALL PASSED` and byte-exact solver baseline.

  SkullScope accounting from the replay scrub gate:
  - Scrub trace command: `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-scrub-test --physics-diag Debug\replay_scrub.physicsdiag.ndjson`
  - Scrub query: `tools\physics_query.bat Debug\replay_scrub.physicsdiag.ndjson replay --limit 8`; trace 54,932 bytes; SQLite cache 225,280 bytes; query output 1,512 bytes.
  - Restore trace command: `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-restore-test --physics-diag Debug\replay_restore.physicsdiag.ndjson`
  - Restore query: `tools\physics_query.bat Debug\replay_restore.physicsdiag.ndjson restore --limit 8`; trace 54,912 bytes; SQLite cache 225,280 bytes; query output 967 bytes.
  - Prediction determinism reports: `TestOutput\validation\replay_prediction_determinism\prediction_determinism_a.json` and `prediction_determinism_b.json`, 4,612 bytes each; bounded stdout excerpts 60,297 bytes each.
  - Total model-read SkullScope query output: 2,479 bytes. Raw NDJSON/SQLite
    sizes above are artifact sizes, not model-ingested text.

Stage 5 — Prediction worker job (old section A, adopts the store contract)
- [x] 5.1 Wrap the tick loop in `Core/AmortizedTask` (`SubmitTick(pool)`,
  `SetBudget(ticksPerSubmit)`), state owned by `RunReplayPredictionState`.
  Single-writer rule: only the job writes build frames / store records; the
  frame loop consumes published prefixes.
- [x] 5.2 Cancellation: `CancelPredictionJob` waits for or invalidates an
  in-flight task before clearing state.
- [x] 5.3 Scene-mutation guard: every begin/branch/scene-load path cancels
  the job; `Hazard:` comment that the prediction engine holds values only,
  never pointers into live stores.
- [x] 5.4 Gate: `validate_full` + 3 consecutive `validate_dx12_renderer`
  runs (frame pacing) + `validate_perf` + both prediction proofs.

  Complete 2026-07-10: prediction builds now run through an
  `AmortizedTask` owned by `RunReplayPredictionBuildState`. The worker advances
  the private prediction engine and writes only replay-owned build frames plus
  pre-sized trajectory slots; the frame loop submits bounded chunks and reads
  the acquire-loaded `buildFrameCount` prefix. `CancelPredictionJob` waits for
  in-flight worker slices before clearing build frames, trajectory records, or
  the private engine, and scene/timeline/restore mutation paths cancel the job
  before touching live scene authority.

  During validation, the first replay proof caught scheduler-sensitive
  trajectory reporting: the physics frame counts were stable, but cached child
  topology could encode how much budgeted draw work ran before the worker
  completed. The completion handoff now clears build-frame topology and rebuilds
  committed child topology plus child trajectory records once from the full
  finished frame buffer on the frame thread. A saturated future-node cache also
  publishes its prefix as complete for the visible buffer, so reports no longer
  include the incidental frame where the fixed node cap was reached.

  The replay allocation policy allowlist now documents the one replay-phase
  `std::make_unique<Threading::AmortizedTask>` allocation under the
  `replay_prediction_working_set` owner. Touched-source comment audit inspected
  five source-bearing files (`ReplayInteractionController.cpp`,
  `ReplayRuntime.cpp`, `ReplayRuntime.h`, `RunReplayScrubberTools.cpp`,
  `RunReplayTools.cpp`) with 0 deferred; no subsystem checklist was required
  because this was a touched-file pass.

  Focused checks passed after the completion-handoff fix: `tools\validate_format.bat`
  in 11.88s (`Agentic\Reports\validate_format_replay_stage5_completion_rebuild_after_fix_20260710.log`),
  `tools\validate_build.bat Debug` in 19.15s
  (`Agentic\Reports\validate_build_debug_replay_stage5_completion_rebuild_after_fix_20260710.log`),
  and `python tools\check_replay_prediction_determinism.py` in 15.20s
  (`Agentic\Reports\check_replay_prediction_determinism_stage5_completion_rebuild_20260710.log`).

  Required final gates passed:
  `tools\validate_full.bat` in 48.97s
  (`Agentic\Reports\validate_full_replay_visuals_stage5_final_20260710.log`:
  DX12 validation errors 0, screenshots matched baselines,
  `physics_regression_solver.csv` byte-exact);
  `tools\validate_dx12_renderer.bat` x3 in 22.63s, 22.55s, 22.61s
  (`Agentic\Reports\validate_dx12_renderer_replay_stage5_final_run1_20260710.log`,
  `...run2...`, `...run3...`; manifests
  `TestOutput\validation\dx12_renderer\20260709T174917Z\manifest.json`,
  `20260709T174939Z\manifest.json`, and
  `20260709T175002Z\manifest.json`; every run had DX12 validation errors 0 and
  screenshots matching baselines); `tools\validate_perf.bat` in 28.87s
  (`Agentic\Reports\validate_perf_replay_stage5_final_20260710.log`:
  `scanned=296 direct_heap_findings=29 dynamic_stl_member_findings=0
  allowlist_errors=0`, allocation guard `gameplay_violations=0`);
  and `tools\validate_replay_scrub.bat` in 23.74s
  (`Agentic\Reports\validate_replay_scrub_replay_visuals_stage5_final_20260710.log`).

  SkullScope accounting for the replay proof: scrub trace command
  `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-scrub-test --physics-diag Debug\replay_scrub.physicsdiag.ndjson`;
  restore trace command
  `Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-restore-test --physics-diag Debug\replay_restore.physicsdiag.ndjson`.
  Queries were `tools\physics_query.bat Debug\replay_scrub.physicsdiag.ndjson replay --limit 8`
  and `tools\physics_query.bat Debug\replay_restore.physicsdiag.ndjson restore --limit 8`.
  Raw artifact sizes: scrub NDJSON 54,932 bytes, scrub SQLite 225,280 bytes,
  restore NDJSON 54,912 bytes, restore SQLite 225,280 bytes. GPT-read query
  output was 1,512 bytes for scrub and 967 bytes for restore, 2,479 bytes
  total. Prediction reports were 4,614 and 4,615 bytes; bounded stdout excerpts
  were 60,294 bytes each. Final prediction fingerprint
  `0x0165312C5422A5F1` matched across two runs (402 records, 73,021 points,
  361 active frames).

Stage 6 — Rendering backend
- [x] 6.1 `trajectory_ribbon.hlsl` + vertex layout + registration; VS-welded
  expansion, single-pass glow, constant screen-space width.
- [x] 6.2 Depth test on / write off; optional dimmed depth-fail pass.
- [x] 6.3 Markers off the ribbon path (lines / ghost instances); yellow entry
  box keeps a single-pass ribbon outline.
- [x] 6.4 Old `soft_additive_ribbon` path retained until baselines approved.

  Complete 2026-07-10: added the DX12 `TrajectoryRibbon` transient triangle
  style, warmed it at renderer init, registered `shader.trajectory_ribbon` in
  the built-in shader asset table, added `SkullbonezData/shaders/trajectory_ribbon.hlsl`,
  and added the shader to the project and filter manifests. The shader expands
  compact start/end/width/color segment payloads with `SV_VertexID`, uses the
  current viewport to convert replay-ribbon width units into stable screen
  pixels, and folds glow/core into one pixel-shader pass.

  `RunEditorTracer` now emits one fixed-budget ribbon record per logical path
  segment, repacks each record into six identical 11-float segment vertices for
  shader-side expansion, draws ribbons with depth test on / depth write off,
  and keeps only the yellow causal entry marker on the ribbon path. Grey rest,
  cyan baseline, and horizon markers route through line outlines. The replay
  ribbon quota was reduced from two records to one record per logical path
  segment to match the single-pass emitter. Interaction automation now waits
  for the normal replay render-frame path to finish a prediction worker publish
  before writing reports, so topology/count/hash reporting samples committed
  prediction state without draining worker physics under the post-draw profiler
  scope.

  The old `soft_additive_ribbon` shader and style remain registered. No baseline
  image artifacts were updated in this slice because the DX12 render suite still
  matched the committed baselines; Stage 7 owns intentional visual-polish
  baseline changes if the suite captures change.

  Touched-source comment audit inspected 11 source-bearing files with 0
  deferred (`AssetSystem.cpp`, `RenderBackendDX12.DynamicGeometry.cpp`,
  `RenderBackendDX12.cpp`, `RenderBackendDX12.h`, `IRenderCommandContext.h`,
  `RunEditorTracer.cpp`, `RunReplayTools.cpp`, `RunInteractionAutomation.cpp`,
  `RuntimeTools.h`, and `trajectory_ribbon.hlsl`). `RunReplayTools.cpp` and
  `RuntimeTools.h` glossary wording was refreshed after the audit so replay
  ribbons are described as screen-space trajectory strokes, not camera-facing
  marker strokes. No subsystem checklist was required for this touched-file
  pass.

  Focused checks passed: `tools\validate_format.bat` in 10.09s after the
  final comment audit
  (`Agentic\Reports\validate_format_replay_stage6_post_audit_20260710.log`);
  `tools\validate_build.bat Profile` in 9.92s with 0 warnings/errors
  (`Agentic\Reports\validate_build_profile_replay_stage6_marker_split_20260710.log`);
  `python .\tools\validate_shaders.py` in 0.23s
  (`Agentic\Reports\validate_shaders_replay_stage6_marker_split_20260710.log`:
  0 errors, 11 pre-existing warnings); and `tools\validate_fast.bat` in
  25.44s (`Agentic\Reports\validate_fast_replay_stage6_marker_split_20260710.log`).

  Required final gates passed: `tools\validate_replay_scrub.bat` in 26.05s
  (`Agentic\Reports\validate_replay_scrub_replay_stage6_marker_split_20260710.log`);
  `tools\validate_dx12_renderer.bat` x3 in 23.91s, 22.73s, and 22.80s
  (`Agentic\Reports\validate_dx12_renderer_replay_stage6_marker_split_run1_20260710.log`,
  `...run2...`, `...run3...`; manifests
  `TestOutput\validation\dx12_renderer\20260709T183522Z\manifest.json`,
  `20260709T183552Z\manifest.json`, and
  `20260709T183620Z\manifest.json`; every run had DX12 validation errors 0 and
  screenshots matching baselines); and `tools\validate_full.bat` in 29.31s
  (`Agentic\Reports\validate_full_replay_stage6_marker_split_20260710.log`:
  project filters clean, Profile/Debug builds clean, DX12 validation errors 0,
  screenshots matching baselines, and `physics_regression_solver.csv` 20,001
  lines byte-exact).

  SkullScope accounting from the replay scrub gate:
  - Scrub trace command: `C:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-scrub-test --physics-diag C:\SkullbonezCore\Debug\replay_scrub.physicsdiag.ndjson`
  - Scrub query: `tools\physics_query.bat Debug\replay_scrub.physicsdiag.ndjson replay --limit 8`; trace 54,932 bytes; SQLite cache 225,280 bytes; query output 1,512 bytes.
  - Restore trace command: `C:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-restore-test --physics-diag C:\SkullbonezCore\Debug\replay_restore.physicsdiag.ndjson`
  - Restore query: `tools\physics_query.bat Debug\replay_restore.physicsdiag.ndjson restore --limit 8`; trace 54,912 bytes; SQLite cache 225,280 bytes; query output 967 bytes.
  - Total model-read SkullScope query output: 2,479 bytes. Raw NDJSON/SQLite
    sizes above are artifact sizes, not model-ingested text.
  - Prediction determinism reports were 4,614 bytes each; bounded stdout
    excerpts were 60,294 bytes each. Final prediction fingerprint
    `0x0165312C5422A5F1` matched across two runs (402 records, 73,021 points,
    361 active frames).

Stage 7 — Visual polish
- [x] 7.1 Age fade on past path; screen-space feather tuning; depth-behind
  dimming; per-depth gradient palette; entry-box emphasis.

  Complete 2026-07-10: replay trajectory color is now draw-time palette math
  instead of fixed inline RGB formulas. Past-root paths fade by retained-sample
  age, future-root paths shift from near warm mint to cooler horizon color, and
  child incoming/outgoing paths use a depth-indexed hue palette with brightness
  dimming for deeper branches. The legacy fallback path uses the same helpers
  so its behavior stays aligned while it remains default-off.

  `RunEditorTracer` now repacks trajectory ribbons as 13-float segment vertices
  carrying feather and HDR-emphasis style hints. The trajectory shader consumes
  those hints, adds tuned edge/core/shoulder feathering, and exposes
  `uRibbonStyle` so the DX12 backend can draw a faint depth-hint underlay before
  the normal depth-tested pass. The yellow causal entry box is wider/brighter
  for emphasis; rest/horizon/baseline markers remain on the Stage 6 line path.
  No visual baseline files were updated because all three DX12 renderer passes
  matched the committed baselines.

  Touched-source comment audit inspected 8 source-bearing files with 0 deferred
  (`RenderBackendDX12.DynamicGeometry.cpp`, `RenderBackendDX12.cpp`,
  `RenderBackendDX12.h`, `IRenderCommandContext.h`, `RunEditorTracer.cpp`,
  `RunReplayTools.cpp`, `RuntimeTools.h`, and `trajectory_ribbon.hlsl`). The
  audit refreshed the transient-triangle glossary and stale 11-float ribbon
  payload wording; no subsystem checklist was required for this touched-file
  pass.

  Focused checks passed: `tools\validate_format.bat` in 9.96s after the comment
  audit (`Agentic\Reports\validate_format_replay_stage7_post_audit_20260710.log`);
  `tools\validate_build.bat Profile` in 15.81s with 0 warnings/errors
  (`Agentic\Reports\validate_build_profile_replay_stage7_initial_20260710.log`);
  `python .\tools\validate_shaders.py` in 0.15s
  (`Agentic\Reports\validate_shaders_replay_stage7_final_20260710.log`: 0
  errors, 11 pre-existing warnings); and `git diff --check` passed.

  Final gates passed: `tools\validate_replay_scrub.bat` in 50.55s
  (`Agentic\Reports\validate_replay_scrub_replay_stage7_final_20260710.log`);
  `tools\validate_dx12_renderer.bat` x3 in 23.61s, 22.62s, and 22.74s
  (`Agentic\Reports\validate_dx12_renderer_replay_stage7_final_run1_20260710.log`,
  `...run2...`, `...run3...`; manifests
  `TestOutput\validation\dx12_renderer\20260709T185557Z\manifest.json`,
  `20260709T185619Z\manifest.json`, and
  `20260709T185642Z\manifest.json`; every run had DX12 validation errors 0 and
  screenshots matching baselines).

  SkullScope accounting from the replay scrub gate:
  - Scrub trace command: `C:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-scrub-test --physics-diag C:\SkullbonezCore\Debug\replay_scrub.physicsdiag.ndjson`
  - Scrub query: `tools\physics_query.bat Debug\replay_scrub.physicsdiag.ndjson replay --limit 8`; trace 54,932 bytes; SQLite cache 225,280 bytes; query output 1,512 bytes.
  - Restore trace command: `C:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-restore-test --physics-diag C:\SkullbonezCore\Debug\replay_restore.physicsdiag.ndjson`
  - Restore query: `tools\physics_query.bat Debug\replay_restore.physicsdiag.ndjson restore --limit 8`; trace 54,912 bytes; SQLite cache 225,280 bytes; query output 967 bytes.
  - Total model-read SkullScope query output: 2,479 bytes. Raw NDJSON/SQLite
    sizes above are artifact sizes, not model-ingested text.
  - Prediction determinism reports were 4,614 bytes each; bounded stdout
    excerpts were 60,294 bytes each. Final prediction fingerprint
    `0x0165312C5422A5F1` matched across two runs (402 records, 73,021 points,
    361 active frames).

Stage 8 — Replay memory data-model tuning (old B2–B4; re-derive first)
- [x] 8.1 Re-derive the data model against post-Stage-3 code (June draft in
  git history of `replay-memory-quality-tuning-plan.md`).
- [x] 8.2 Split body metadata from visual pose; add delta frames.
- [x] 8.3 Compact solver keyframes/deltas; artifact compatibility for saved
  replays.
- [x] 8.4 Presets + budget enforcement + UI sliders.

  Complete 2026-07-10: re-derived the memory model from current Stage-7 code
  before implementing. The retired June draft's high-level intent still holds,
  but the live code now has stronger constraints and better starting points:
  trajectory memory is isolated in `ReplayTrajectoryStore`, per-category replay
  memory accounting is live, saved V2 artifacts already dictionary-encode body
  identity with 32-byte pose rows, and solver restore/hash paths are table-driven
  through `ReplaySolverWorldSnapshot`.

  Current resident shape:
  - Presentation samples are still dense: each retained
    `ReplayPresentationSample` owns a full `bodies` vector every tick. Body rows
    mix stable metadata (`id`, `modelIndex`, `name`, `shapeKind`, `mass`,
    `fixed`) with dynamic visual/debug state (`position`, `orientation`,
    velocities, sleep flags, collision/contact summaries, island id).
  - Solver samples are denser: each `ReplaySolverFrameSample` owns full solver
    body rows, launcher visuals, and a `ReplaySolverWorldSnapshot` whose hash
    and restore paths include body-state vectors, sleep/island vectors, tornado
    timers, persistent contacts/cache, solver stats, debug contacts, pipeline
    trace, and collision-cell keys.
  - V2 artifacts prove a body dictionary is viable for saved presentation
    preview, but their current presentation load path reconstructs only identity
    plus pose. Live lossless scrub/hash must also preserve velocities, mass,
    fixed/sleep/contact fields, and frame counters.

  Revised implementation order:
  - 8.2 first adds a runtime presentation compaction model that separates stable
    visual body metadata from per-frame pose/debug deltas, while retaining a
    lossless reconstruction path to the existing `ReplayPresentationSample`
    public API. Default visual look and hashes must not change.
  - 8.3 starts with a solver field matrix before any solver delta format. Fields
    are classified as restore-critical, hash-critical, debug-only, or derived;
    only then can keyframe/delta storage and saved-artifact compatibility be
    implemented without weakening restore verification.
  - 8.4 lands after real storage knobs exist: presets and UI sliders resolve
    into one `ReplayRuntime` policy owner, then budget enforcement degrades
    solver/debug detail before visual quality in the default Lossless Look path.

  Documentation-only slice; no repository validation required.

  Complete 2026-07-10: presentation retention now keeps body identity/display
  metadata in `ReplayVisualBodyMetadata` and per-frame compact
  `ReplayVisualDeltaFrame` records. Capture writes body rows through a scratch
  list, stores per-frame body order plus changed dynamic state, and leaves
  retained `ReplayPresentationSample::bodies` empty until a caller asks for a
  full sample. `LatestSample()`, `SampleAtNormalized()`, and chronological
  copies reconstruct lossless presentation samples from the nearest keyframe
  plus deltas, with per-slot reconstruction cache preserving the old pointer
  lifetime contract. Ring eviction promotes the new oldest visual frame to a
  keyframe before overwriting its predecessor, so retained history remains
  seekable after wrap.

  Validation:
  - Touched-source comment audit: `ReplayRecorder.h` and `ReplayRecorder.cpp`
    inspected, 2 checked, 0 deferred; no subsystem checklist required.
  - `git diff --check` passed.
  - `tools\validate_format.bat` passed in 10.02s
    (`Agentic\Reports\validate_format_replay_stage8_2_post_fix_20260710.log`).
  - Focused replay recorder unit filter passed in 2.01s:
    `Profile\SKULLBONEZ_TESTS.exe --test-case="ReplayRecorder*"`
    (`Agentic\Reports\replay_recorder_unit_stage8_2_after_cache_20260710.log`).
  - `tools\validate_full.bat` passed in 67.76s
    (`Agentic\Reports\validate_full_replay_stage8_2_20260710.log`): Profile
    and Debug builds were 0 warnings/errors, DX12 validation errors were 0,
    screenshots matched baselines, and `physics_regression_solver.csv` matched
    byte-exactly.
  - `tools\validate_replay_scrub.bat` passed in 23.51s
    (`Agentic\Reports\validate_replay_scrub_replay_stage8_2_20260710.log`).
    SkullScope accounting: scrub trace 54,932 bytes, scrub SQLite 225,280
    bytes, scrub query output 1,512 bytes; restore trace 54,912 bytes, restore
    SQLite 225,280 bytes, restore query output 967 bytes; total model-read
    SkullScope query output 2,479 bytes. Prediction determinism fingerprint
    `0x0165312C5422A5F1` matched across two runs (402 records, 73,021 points,
    361 active frames).

  Complete 2026-07-10: solver retention now stores compact
  `ReplaySolverDeltaFrame` records instead of retaining dense body and world
  snapshots in every ring slot. Body identity/display/mass/inertia fields live
  in `ReplaySolverBodyMetadata`; per-frame pose, velocity, fixed/sleep/contact
  flags, island id, penetration, and impulse summaries live in
  `ReplaySolverBodyState` deltas. World scalar state is stored every frame,
  while each `ReplaySolverWorldSnapshot` vector stores a full payload on
  keyframes or size changes and sparse indexed edits otherwise. Ring eviction
  promotes the new oldest solver frame to a keyframe before its predecessor is
  overwritten, so retained history remains seekable after wrap.

  Artifact compatibility is preserved through the old dense public solver sample
  shape: `LatestSample()`, `SampleAtNormalized()`,
  `ForEachSampleChronological()`, and `CopySamplesChronological()` reconstruct
  from compact frames before callers observe samples. `ReplayV2Artifact` still
  receives dense solver checkpoints for its SCHK payloads, and old dense saved
  artifacts still load through the existing parser. A focused restore-probe
  failure caught an early single-cache pointer regression; the final code keeps
  separate latest and historical reconstruction caches so same-tick pointer
  comparisons remain valid.

  Solver field matrix:
  - Metadata dictionary: `id`, `modelIndex`, `name`, `shapeKind`, `mass`,
    `inverseMass`, `rotationalInertia`, and `inverseRotationalInertia` are
    stable identity/shape/mass data required for dense reconstruction.
  - Body state deltas: position, linear/angular velocity, orientation, fixed and
    sleep flags, collision contact, sleep-island visual id, contact count,
    max penetration, and normal impulse sum are frame-local hash/restore data.
  - World scalar state: version, model count, next sleep-island visual id,
    sleep/collision flags, tornado configs, elapsed tornado time, and solver
    stats are stored every retained solver frame.
  - World vector deltas: time/sleep/tornado/collision arrays, sleep-island
    tables, persistent contacts/cache/counts, debug contacts, pipeline trace,
    and collision-cell keys are full on keyframe/size change and sparse by
    index otherwise.

  Validation:
  - Touched-source comment audit: `ReplayRecorder.h` and `ReplayRecorder.cpp`
    inspected, 2 checked, 0 deferred; no subsystem checklist required.
  - `git diff --check` passed.
  - `tools\validate_format.bat` passed in 10.10s
    (`Agentic\Reports\validate_format_replay_stage8_3_final_20260710.log`).
  - `tools\validate_build.bat Profile` passed in 21.43s
    (`Agentic\Reports\validate_build_profile_replay_stage8_3_final_20260710.log`):
    Profile build 0 warnings/errors.
  - Focused replay recorder unit filter passed in 1.41s:
    `Profile\SKULLBONEZ_TESTS.exe --test-case=ReplayRecorder*`
    (`Agentic\Reports\replay_recorder_unit_stage8_3_final_20260710.log`).
  - `tools\validate_full.bat` passed in about 30s
    (`Agentic\Reports\validate_full_replay_stage8_3_final_20260710.log`):
    Profile and Debug builds were 0 warnings/errors, DX12 validation errors
    were 0, screenshots matched baselines, and `physics_regression_solver.csv`
    matched byte-exactly.
  - `tools\validate_replay_scrub.bat` passed in 41.02s
    (`Agentic\Reports\validate_replay_scrub_replay_stage8_3_final_20260710.log`).
    Scrub trace command: `C:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-scrub-test --physics-diag C:\SkullbonezCore\Debug\replay_scrub.physicsdiag.ndjson`.
    Scrub query: `tools\physics_query.bat Debug\replay_scrub.physicsdiag.ndjson replay --limit 8`;
    trace 54,932 bytes; SQLite cache 225,280 bytes; query output 1,512
    bytes. Restore trace command: `C:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-restore-test --physics-diag C:\SkullbonezCore\Debug\replay_restore.physicsdiag.ndjson`.
    Restore query: `tools\physics_query.bat Debug\replay_restore.physicsdiag.ndjson restore --limit 8`;
    trace 54,912 bytes; SQLite cache 225,280 bytes; query output 967 bytes.
    Total model-read SkullScope query output: 2,479 bytes. Prediction
    determinism fingerprint `0x0165312C5422A5F1` matched across two runs (402
    records, 73,021 points, 361 active frames); reports were 4,615 bytes each,
    bounded stdout outputs were 60,290 bytes each.

  Complete 2026-07-10: replay memory policy is now owned by `ReplayRuntime`.
  The policy exposes Lossless Look, Balanced, and Compact presets, plus
  retention and memory-budget sliders from the Memory tab. UI code emits
  one-frame commands only; `RunInput` applies them through
  `ReplayRuntime::ApplyMemoryPolicyRequest()`, which reconfigures the replay
  recorders and snaps scrub tracks back to the live edge when windows change.

  Budget resolution keeps the default visual scrubber lossless, then shortens
  solver/debug history before presentation history as requested budgets get
  smaller. The focused policy test covers the compact 60s/48 MiB case: visual
  retention resolves to 30s while solver retention resolves to 5s. Memory
  diagnostics and `skullbonez.main_memory.v1` dumps now expose the requested
  preset/retention/budget and the applied presentation/solver windows.

  Validation:
  - Touched-source comment audit inspected 14 source-bearing files with 0
    deferred; no subsystem checklist was required for this touched-file pass.
  - `git diff --check` passed.
  - `tools\validate_format.bat` passed in 10.02s
    (`Agentic\Reports\validate_format_replay_stage8_4_final_20260710.log`).
  - `tools\validate_build.bat Profile` passed in 21.13s
    (`Agentic\Reports\validate_build_profile_replay_stage8_4_final_20260710.log`):
    Profile build 0 warnings/errors.
  - Focused replay memory policy test passed in 1.91s:
    `Profile\SKULLBONEZ_TESTS.exe --test-case='*replay*memory*'`
    (`Agentic\Reports\replay_memory_policy_unit_stage8_4_final_20260710.log`).
  - Focused replay recorder unit filter passed in 2.07s:
    `Profile\SKULLBONEZ_TESTS.exe --test-case=ReplayRecorder*`
    (`Agentic\Reports\replay_recorder_unit_stage8_4_final_20260710.log`).
  - `tools\validate_full.bat` passed in 49.99s
    (`Agentic\Reports\validate_full_replay_stage8_4_final_20260710.log`):
    Profile and Debug builds were 0 warnings/errors, DX12 validation errors
    were 0, screenshots matched baselines, and `physics_regression_solver.csv`
    matched byte-exactly.
  - `tools\validate_replay_scrub.bat` passed in 25.53s
    (`Agentic\Reports\validate_replay_scrub_replay_stage8_4_final_20260710.log`).
    Scrub trace command: `C:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-scrub-test --physics-diag C:\SkullbonezCore\Debug\replay_scrub.physicsdiag.ndjson`.
    Scrub query: `tools\physics_query.bat Debug\replay_scrub.physicsdiag.ndjson replay --limit 8`;
    trace 54,932 bytes; SQLite cache 225,280 bytes; query output 1,512
    bytes. Restore trace command: `C:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-restore-test --physics-diag C:\SkullbonezCore\Debug\replay_restore.physicsdiag.ndjson`.
    Restore query: `tools\physics_query.bat Debug\replay_restore.physicsdiag.ndjson restore --limit 8`;
    trace 54,912 bytes; SQLite cache 225,280 bytes; query output 967 bytes.
    Total model-read SkullScope query output: 2,479 bytes. Prediction
    determinism fingerprint `0x0165312C5422A5F1` matched across two runs (402
    records, 73,021 points, 361 active frames); reports were 4,615 bytes each,
    bounded stdout outputs were 60,290 bytes each.

Stage 9 — Debug tooling & tests
- [x] 9.1 Overlay debug readout (record count/bytes/version churn).
- [x] 9.2 Flicker probe: frozen inputs → hash submitted trajectory vertex
  bytes for 120 frames, all identical; promote into `validate_replay_scrub`.
- [x] 9.3 Lock-step probe (Stage 4) + no-allocation steady-state assertion
  promoted into `validate_replay_scrub`.

  Complete 2026-07-10: memory diagnostics now expose live
  `TrajectoryStore` record count, stored/published point counts, max resident
  record version, and version churn in the Memory tab and
  `skullbonez.main_memory.v1` dumps. `RunEditorTracer` hashes the exact
  replay-ribbon vertex byte stream submitted to the transient trajectory
  renderer, and `ReplayRuntime` records a steady-window probe that requires the
  submitted hash and replay reserve-growth counter to stay fixed.

  The prediction determinism validation script now hides the live past lane,
  keeps the interaction run alive through frame 460, and requires: prediction
  path visible, live solver hash stable across prediction, store fingerprint
  ready, 120+ identical submitted-geometry frames, and no replay reserve growth
  during the steady submitted-geometry window. Final proof from Run A:
  submitted hash `0xB127A5094FB0F18F`, 186 stable frames (frames 275-460),
  6,729,216 submitted vertex bytes, 129,408 vertices, 21,568 segments, and
  reserve-growth counter fixed at 414. Store fingerprint remained
  `0x0165312C5422A5F1` across two runs (402 records, 73,021 points, 361 active
  frames).

  Validation:
  - Touched-source comment audit inspected 11 source/tool files with 0
    deferred; no subsystem checklist was required for this touched-file pass.
  - `git diff --check` passed.
  - Focused compile check `tools\validate_build.bat Profile` passed in 14.48s
    with 0 warnings/errors
    (`Agentic\Reports\validate_build_profile_replay_stage9_compile_20260710.log`).
  - `tools\validate_fast.bat` passed
    (`Agentic\Reports\validate_fast_replay_stage9_final_20260710.log`):
    formatting clean, project filters 0 errors, staged-size check 0
    violations, Profile/Debug builds 0 warnings/errors.
  - `tools\validate_replay_scrub.bat` passed
    (`Agentic\Reports\validate_replay_scrub_replay_stage9_final_20260710.log`).
    Scrub trace command: `C:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-scrub-test --physics-diag C:\SkullbonezCore\Debug\replay_scrub.physicsdiag.ndjson`.
    Scrub query: `tools\physics_query.bat Debug\replay_scrub.physicsdiag.ndjson replay --limit 8`;
    trace 54,932 bytes; SQLite cache 225,280 bytes; query output 1,512
    bytes. Restore trace command: `C:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-restore-test --physics-diag C:\SkullbonezCore\Debug\replay_restore.physicsdiag.ndjson`.
    Restore query: `tools\physics_query.bat Debug\replay_restore.physicsdiag.ndjson restore --limit 8`;
    trace 54,912 bytes; SQLite cache 225,280 bytes; query output 967 bytes.
    Total model-read SkullScope query output: 2,479 bytes. Prediction
    determinism reports were 5,712 bytes each; bounded stdout outputs were
    60,290 bytes each.

Stage 10 — Code-size right-sizing + cleanup (old C1–C3 + visuals P8)
- [ ] 10.1 Per-file responsibility inventory of `Runtime/Replay/` (what
  breaks if deleted).
- [ ] 10.2 Delete or merge twin/parallel helpers; `RunReplayTools.cpp`
  (3,699 lines) is the first target — the legacy double-emit ribbon path,
  stride machinery, and frame-loop budget machinery obsoleted by Stages 1–6
  go first.
- [ ] 10.3 After Stage 5, delete frame-loop budget machinery the worker job
  obsoletes; after Stage 8, delete superseded snapshot paths.
- [ ] 10.4 Right-size tracer reserves (~16.5 MB staging → control-point
  pool); record before/after perf + memory here and in `SessionState.md`.

---

## Acceptance

- [x] No flicker: 120-frame frozen-input geometry hash identical; geometry
  is a pure function of (data version, presentFrame, revealFrame).
- [ ] Past path renders only when enabled; changes only at its ends; age
  fade, no pops.
- [ ] Root/children/grandchildren draw from one shared predicted tick
  timeline; child points only at ticks ≥ `firstFrame`; frozen tree per
  version; trajectory lanes readable with ghost requests forced to 0.
- [x] Prediction stepping runs as a worker job; frame loop only consumes
  published prefixes.
- [ ] Zero per-frame heap allocations in the steady-state overlay path
  (`validate_perf` allocation guard, no replay growth events post-build).
- [ ] Bounded memory: store + staging within the registered
  `replay_prediction_working_set` reservation; per-category accounting live;
  replay memory has an enforced budget with default look unchanged.
- [ ] Clean visuals: welded joints, constant apparent width, depth-aware,
  single-pass glow; `validate_dx12_renderer` ×3 green on updated baselines,
  0 DX12 validation errors.
- [x] Determinism: `validate_physics` byte-exact; twice-run prediction probe
  byte-identical; `validate_replay_scrub` green with new probes.
- [ ] `Runtime/Replay/` line count materially down with scrub/restore probes
  passing.

## Validation map

| Slice | Gate |
|-------|------|
| Stage 0 instrumentation | `validate_fast` |
| Stages 1–2 draw determinism / churn | `validate_full` + `validate_replay_scrub` |
| Stage 3 store (+allocation) | `validate_full` + `validate_replay_scrub` + `validate_perf` |
| Stage 4 lock-step | `validate_full` + `validate_replay_scrub` + one `validate_physics` proof |
| Stage 5 prediction job | `validate_full` + renderer ×3 + `validate_perf` + prediction proofs |
| Stages 6–7 renderer/shader | `validate_dx12_renderer` ×3 + intentional baseline update |
| Stage 8 memory/data-model | `validate_full` + `validate_replay_scrub` per slice |
| Stages 9–10 tooling / deletion | `validate_fast` → `validate_replay_scrub`; `validate_full` + `validate_perf` |

Danger zones touched: DX12 upload buffer / frame allocator (Stage 6 → renderer
gate ×3), visual regression baselines (Stages 6–7 → intentional update via
`tools\update_baselines.bat`), fixed-step simulation behavior (Stage 5 →
physics/replay gates). Use a single independent rubber-duck review at the end
of the whole plan, not one per slice, unless the user asks for more.
