# Continuous Orbital Forecast

Date: 2026-08-17
Status: WNF - owner-parked 2026-08-17; restore to `TODO/` only by explicit
owner decision. 0/7 phases complete.
Impact area: Runtime Planning and Prediction, replay overlay UI/input, bounded
trajectory publication, mutual-gravity diagnostics, tests, documentation, and
DX12 visual verification
Owner: Runtime Planning continuous orbital forecast
Priority: Parked owner-requested feature

## Owner Direction

Create an interactive mode for the authored solar-system scene that advances an
isolated future simulation continuously at the fastest rate permitted by the
prediction work slice. It must not stop at the ordinary 120-second prediction
horizon. The visible paths retain only the newest 120 simulated seconds: new
samples overwrite the oldest ring slots so the orbital lines visibly circulate
around the system while the absolute forecast clock continues to grow.

The mode also reports how far into the future the observed system remains
stable. This plan deliberately distinguishes an observed stability horizon from
a mathematical proof of N-body stability. It records the first detected
instability and its cause, but the simulation may continue so the operator can
watch the later behavior.

This file is planning authority only. It grants no production-edit, validation,
commit, reserve-cap, or baseline-refresh authority while it remains under
`WNF/`.

## Current Evidence - 2026-08-17

- `Runtime/Replay/ReplayCaptureLimits.h` owns the ordinary 20-second default and
  120-second maximum future.
- `ReplayPrediction::BeginFrameSimulation` converts the bounded horizon to a
  finite `targetTickCount`, allocates `predictionTicks + 1` complete frames, and
  pre-reserves each frame's body/contact payload before starting the worker.
- `RunReplayPredictionWorkerRange` stops when `targetTickCount` is reached and
  otherwise advances as many indivisible fixed ticks as fit before the existing
  five-millisecond worker stop.
- `ReplayPredictionTrajectoryStore` publishes append-only point prefixes. The
  mutual-gravity path builds one `FutureRoot` record per body and sizes each
  record to the complete bounded horizon. It has no circular ordering contract.
- `ReplayPredictionPresentationView` exposes one bounded frame span, one reveal
  cursor, completion state, throughput, and horizon seconds. Existing bounded
  consumers assume frame zero and an authoritative final horizon.
- `Runtime/Planning/ReplayOverlayRenderer.cpp` owns the operator-facing replay
  overlay. Per the Replay-Family Placement Rule, the new retained mode,
  stability readout, and orchestration belong in `Runtime/Planning/`, not
  `Runtime/Prediction/` or `Runtime/Replay/`.
- `SkullbonezData/scenes/solar_system.scene.json` enables softened mutual
  gravity with a fixed authored sun plus Earth, Mars, and a ship. The scene has
  no authored stability cohort, primary relationship, or acceptable orbital
  envelope today.
- The current prediction working set is registered under
  `replay_prediction_working_set` with a 256 MiB hard cap. Continuous forecast
  must reuse warmed storage under that existing owner; a new growth privilege
  or cap increase is outside this plan unless separately owner-approved and
  reconciled with the replay reserve inventory.
- The ImGui development editor still exposes a stale 1-20 second prediction
  slider while the legacy replay overlay exposes 1-120 seconds. UI parity must
  be repaired in the touching phase rather than copied into the new mode.

## Product Contract

### Mode semantics

- `PREDICT` remains the existing bounded, editable, replay-integrated forecast.
- `CONTINUOUS` is a separate mutually exclusive mode for mutual-gravity scenes.
  Entering it snapshots the current authoritative solver state into a private
  prediction engine; it never writes live Physics stores or advances replay.
- "Full speed" means no finite target tick, no reveal animation, and no
  artificial per-submit tick cap. Each render frame offers an effectively
  unbounded remaining range and publishes every complete tick that fits inside
  the existing prediction worker time slice. The first implementation does not
  monopolize the frame thread or create a permanently spinning background job.
- The visible trail is a 120-simulated-second rolling window. Absolute simulated
  tick/time, stability evidence, throughput, and the first failure record never
  reset merely because path slots wrap.
- Disable, scene change, branch restore, target/cohort change, or explicit reset
  joins worker work before retiring the private simulation. Re-entering the
  mode starts from a fresh live snapshot and may reuse warmed capacity.
- Ordinary path editing, causal trees, trip planning, porkchop analysis,
  prediction archives, and replay scrub selection continue to consume only the
  bounded `PREDICT` publication. They must never mistake a sliding ring for a
  frame-zero-to-horizon prediction.

### Bounded storage and publication

- Prediction owns a generic continuous-simulation producer with a fixed ring of
  complete all-body position rows. Planning owns the mode, stability policy,
  commands, readout, and presentation of that producer's detached values.
- The ring is sized once for 120 seconds at `PHYSICS_FIXED_DT`, plus the boundary
  sample needed to draw continuous segments. Every slot carries its absolute
  64-bit simulation tick. Physical slot index is never exposed as timeline
  order.
- A tick becomes reader-visible only after every monitored body's position for
  that tick is written. Publication exposes one coherent logical interval
  `[oldestAbsoluteTick, newestAbsoluteTick]`; wrap cannot join the newest point
  directly to the oldest point or mix body rows from different ticks.
- The ring stores only values needed for the rolling paths and head markers. It
  does not retain full `RunReplayPredictionFrame` body/contact payloads for an
  unbounded future. Contact/health diagnostics use fixed summaries and one
  latched first-failure record.
- Trajectory and draw-command capacity is reserved before the continuous worker
  starts. At least three complete ring wraps must show zero capacity growth,
  zero reserve-owner growth-count change, and a stable retained-byte high-water.
- Absolute counters use checked 64-bit arithmetic. Ring wrap is expected;
  absolute-counter overflow is a terminal diagnostic failure, never silent
  rollover.

### Stability contract

The UI uses the phrase `STABLE THROUGH <time>` only while all configured checks
have passed. If a check fails it latches `FIRST INSTABILITY <time>: <cause>`;
the forecast may keep advancing. If no threshold has been crossed, it says
`NO INSTABILITY OBSERVED THROUGH <time>`, never `stable forever`.

The first version reports three separate facts:

1. **Numerical health (blocking):** every monitored pose and velocity remains
   finite and representable, the private physics step succeeds, and the
   publication sequence remains valid.
2. **Orbital configuration (blocking):** no monitored core body collides with
   another core body, crosses its scene-authored inner/outer radial envelope,
   or remains on a scene-authored escape condition for the configured grace
   interval. A positive instantaneous two-body energy sample alone is not
   sufficient in a perturbed N-body system.
3. **Conservation quality (informational initially):** normalized mechanical
   energy and angular-momentum drift from the seed snapshot, plus their maxima,
   are shown as numerical-quality diagnostics. They do not silently define
   physical instability, especially while the authored sun is fixed and the
   system is not a free barycentric model.

`solar_system.scene.json` must author the stability contract explicitly rather
than relying on object names, render colors, or a guessed largest mass. The
recommended first cohort is sun as the fixed primary, Earth and Mars as core
orbital bodies, and ship as an auxiliary body with its own status. OF0 must
ratify the exact core cohort, radial envelopes, escape rule/grace interval, and
whether the ship can end the system-wide stable horizon before implementation.

## Non-Goals

- Do not remove or raise the ordinary 120-second bounded prediction limit.
- Do not replace `PREDICT`, change its archive schema, or feed continuous rows
  into trip-planner, porkchop, cause-tree, scrubber, or velocity-edit logic.
- Do not claim a mathematical Lyapunov horizon, long-term celestial accuracy,
  or proof that an N-body system is permanently stable.
- Do not infer stability ownership from body names, color, dense model row, or
  whichever body currently has the largest mass.
- Do not add full prediction frames, debug-contact vectors, or causal trees to
  every rolling tick.
- Do not add a second live input owner, mutate live/replay state from the worker,
  or let Planning retain App/Run references or callbacks.
- Do not add a new post-gameplay allocation privilege, increase the 256 MiB
  prediction reserve cap, or weaken its replay-phase check under this plan.
- Do not alter `PhysicsBodyRecord` or any hot Physics store row; forecast metrics
  belong in stage/product-owned bounded storage.
- Do not refresh Physics, Replay, SkullScope, or visual baselines without later
  explicit owner approval on an exact candidate transition.

## Phases

- [ ] **OF0 - Ratify observable stability and interaction semantics.** Record
  exact owner answers for the solar-system core cohort, ship treatment, radial
  envelopes, sustained escape condition, collision policy, reset behavior, and
  whether the existing five-millisecond slice is the intended full-speed CPU
  budget. Capture a bounded-prediction and live-scene hash witness before code
  changes. No later phase is selectable until these answers are in this file.
- [ ] **OF1 - Prove the circular publication primitive in isolation.** Add a
  fixed-capacity all-body sample ring with absolute tick identity, coherent-row
  release publication, logical oldest-to-newest iteration, wrap-safe segment
  boundaries, checked counters, and no post-start growth. Unit tests cover empty,
  partial, exactly full, one-wrap, multi-wrap, cancellation, and concurrent
  publication snapshots without involving UI or DX12.
- [ ] **OF2 - Add the lower continuous prediction producer.** Seed and retain a
  private Physics engine through the existing prediction reserve owner, submit
  unlimited-target fixed-tick slices under the ratified frame budget, capture
  only bounded path/head values, and expose a detached view. Prove that live
  solver hashes and bounded `PREDICT` state remain unchanged while continuous
  forecast advances beyond 120 seconds and through at least three ring wraps.
- [ ] **OF3 - Add Planning-owned stability analysis.** Parse or derive the
  ratified scene-authored cohort through a downward-safe value seam, calculate
  numerical health, orbital-envelope/escape/collision events, and conservation
  diagnostics, then latch the first blocking failure without stopping the
  producer. Focused tests plant one finite stable orbit, radial escape,
  collision, sustained-versus-transient escape, invalid numeric state, and
  auxiliary-only failure.
- [ ] **OF4 - Wire typed commands and operator readout.** Add mutually exclusive
  `PREDICT`/`CONTINUOUS` commands through existing UI value queues and App
  composition. Show simulated duration, achieved simulation/real-time rate,
  rolling-window age, stability status, first cause/time, and conservation
  diagnostics. Keep Legacy and ImGui surfaces behaviorally aligned, including
  the existing prediction horizon-range discrepancy. Add reset and exit actions
  without creating retained state in UI or Input.
- [ ] **OF5 - Draw the racing orbital window.** Planning converts the detached
  logical ring into bounded generic ribbon/head-marker packets; Rendering stays
  feature-neutral. Draw all configured bodies in authored colors, never bridge
  the ring seam, and publish every body's newest head at one coherent absolute
  tick. Add an automation probe for `solar_system.scene.json` that screenshots
  pre-wrap and post-wrap states and records that the absolute forecast time
  advanced while old geometry was overwritten.
- [ ] **OF6 - Close behavior, memory, determinism, and ownership.** Run focused
  wrap/stability tests, the solar-system automation probe, allocation-policy and
  replay visual-fidelity gates, worker-count deterministic witnesses, mapped
  Physics/DX12/performance validation, and at least a three-wrap stress witness.
  Audit every touched source file against the comment-style skill, run the
  dependency and ownership inventories, and obtain independent ownership review.
  The review must explicitly reject any sliding-window leakage into bounded
  prediction consumers, new reserve privilege, Runtime package violation, or
  second retained input/simulation owner.

## Acceptance

The feature is complete only when all of the following are demonstrated from
one final source state:

- Continuous forecast advances past 120 simulated seconds without completing,
  restarting, reallocating, or mutating live Physics/Replay state.
- The visible paths retain exactly the newest bounded window across repeated
  wraps, with no seam chord, stale body row, flicker, or cross-tick head marker.
- UI time remains monotonic across wraps and reports both simulated throughput
  and a first-instability record with an attributable typed cause.
- The solar-system stability cohort is authored and testable; no name/mass/color
  heuristic decides the result.
- Numerical health, orbital configuration, and conservation drift remain
  separately visible. The UI makes no eternal-stability claim.
- Disabling/resetting/changing scene joins worker work safely and bounded
  `PREDICT` still produces byte-identical value/visual witnesses for unchanged
  inputs.
- Retained bytes and reserve growth counters remain flat after warm-up through
  at least three complete 120-second ring wraps.
- Dependency, allocation, comment-quality, performance, deterministic Physics,
  Replay visual fidelity, DX12 validation, graphics stress, and independent
  ownership review all pass without a baseline refresh.

## Validation Mapping

Validation remains deferred until implementation is restored to `TODO/` and a
phase is being prepared for commit.

| Scope | Required pre-commit evidence |
|---|---|
| Ring and stability unit/value tests | Focused `SKULLBONEZ_TESTS` filters, then `tools\validate_tests.bat` |
| Runtime package or reserve changes | `tools\validate_dependency_graph.bat`, `tools\validate_replay_allocation_policy.bat`, and `tools\validate_fast.bat` |
| Private Physics stepping or stability metrics | Worker-count deterministic witness and `tools\validate_physics.bat` |
| Replay/Prediction value or overlay changes | `tools\validate_replay_visual_fidelity.bat` |
| Overlay/DX12 path changes | `tools\validate_dx12_renderer.bat` and `tools\run_graphics_stress.bat 1` |
| Continuous hot path | `tools\validate_perf.bat` plus a three-wrap retained-byte/growth witness |
| Final combined source state | `tools\validate_full.bat --plan-completion` after the focused gates above |

## Reactivation Condition

Move this file from `WNF/` to `TODO/` and register it in the active table only
when the owner explicitly resumes implementation. At reactivation, refresh the
source evidence, settle OF0, confirm the then-current prediction reserve
inventory and Runtime package rules, and implement through the repo-local
orchestrator skill. Do not begin it ahead of the currently binding active plan
merely because the document exists.

## Reference Sites

- `SkullbonezData/scenes/solar_system.scene.json`
- `SkullbonezSource/Runtime/Replay/ReplayCaptureLimits.h`
- `SkullbonezSource/Runtime/Prediction/ReplayPrediction.h`
- `SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp`
- `SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.cpp`
- `SkullbonezSource/Runtime/Prediction/TrajectoryStore.h`
- `SkullbonezSource/Runtime/Prediction/ReplayPredictionView.h`
- `SkullbonezSource/Runtime/Planning/ReplayPlanningRuntime.h`
- `SkullbonezSource/Runtime/Planning/ReplayOverlayPackets.h`
- `SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.cpp`
- `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp`
- `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp`
- `SkullbonezSource/UI/OperatorEditorExchange.h`
- `SkullbonezTests/TestReplayPredictionScheduling.cpp`
- `SkullbonezTests/TestReplayVisualPacket.cpp`
- `SkullbonezTests/TestRuntimeValueSeams.cpp`
- `Agentic/Reference/runtime-reference.md`
- `Agentic/Reference/engine-glossary.md`
