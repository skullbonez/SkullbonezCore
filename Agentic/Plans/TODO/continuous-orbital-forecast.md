# Continuous Orbital Forecast

Date: 2026-08-17
Status: Active; 2/7 phases complete
Impact area: Runtime Planning and Prediction, replay overlay UI/input, bounded
trajectory publication, mutual-gravity diagnostics, tests, documentation, and
DX12 visual verification
Owner: Runtime Planning continuous orbital forecast
Priority: Active after `PREDICT_SOLVER_DETAIL`
Commit name: `ORBIT_FORECAST`

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

The owner reactivated this plan on 2026-08-17. Implementation begins only when
the master binding order reaches `ORBIT_FORECAST` and follows the repo-local
orchestrator skill. Registration grants implementation and mapped validation
authority, but it does not grant a reserve-cap increase or baseline refresh.

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
  `replay_prediction_working_set` with the 960 MiB hard cap selected by
  `PREDICT_SOLVER_DETAIL` PSD2. Continuous forecast
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

## OF0 Owner Rulings And Pre-Change Witness - 2026-08-19

These rulings are the implementation contract for OF1-OF6. Later phases may
make the authoring syntax more precise, but they must not change the following
semantics without reopening OF0 and recording the owner decision here.

### Stability and lifecycle rulings

| Question | Ratified owner answer |
|---|---|
| Primary and core cohort | The scene-authored stability contract identifies the fixed sun as primary and Earth plus Mars as the two core orbiters. Scene loading resolves explicit authored membership to body IDs; runtime policy must not infer membership from names, colors, order, mass, or model rows. Numerical health is globally blocking for every configured member, including the auxiliary ship. The system-wide orbital-configuration horizon covers the blocking set formed by the primary plus both core orbiters. |
| Auxiliary ship | The ship is an explicitly authored auxiliary member with its own envelope, escape, and collision status. A ship-only orbital-configuration failure is visible and latched in that auxiliary status but does not end the system-wide stable horizon. Non-finite or unrepresentable ship pose/velocity remains a globally blocking numerical-health failure, as do private-step failure and invalid or incomplete publication. A later physical disturbance of Earth, Mars, or the primary ends the horizon only when a blocking-set rule itself fails. The existing bounded witness predicts a ship/Earth contact, so treating every configured contact as a system-wide orbital-configuration failure would contradict this ruling. |
| Radial envelopes | Radius is center-to-center distance from the authored primary on each complete private tick. The inclusive core pass intervals are Earth `[60, 100]` units and Mars `[90, 155]` units. The auxiliary ship interval is `[60, 100]` units. Crossing below the inner value or above the outer value is an immediate blocking failure for a core orbiter and an auxiliary-only failure for the ship. The primary is the reference and has no radial-envelope check. |
| Sustained escape | For each orbiter, the scene authors an escape-start radius and a 5.0 simulated-second grace interval: Earth `90`, Mars `140`, ship `90` units. Let `r = bodyPosition - primaryPosition`, `v = bodyVelocity - primaryVelocity`, and `epsilon = 0.5 * dot(v, v) - G * primaryMass / sqrt(dot(r, r) + softening^2)`, using the authored mutual-gravity `G` and softening. The predicate is true only when `epsilon > 0`, `dot(r, v) > 0`, and `length(r)` is at or beyond the authored escape-start radius. All three facts must hold continuously for 600 complete ticks at `PHYSICS_FIXED_DT == 1/120 s`; any false tick resets that body's consecutive count. The 600th consecutive tick latches the failure. Positive instantaneous energy alone never fails the body. Core/auxiliary treatment follows the cohort ruling above. |
| Collision | The collision-blocking set is the primary plus both core orbiters. A complete private tick whose Physics collision/contact summary identifies contact between any pair wholly inside that set immediately ends the system-wide stable horizon at that tick; this includes sun/Earth and sun/Mars, and there is no grace period. A contact involving the auxiliary ship latches the ship's auxiliary collision status only. Collision identity comes from resolved authored body IDs, never display names or geometry guesses. |
| Conservation | Conservation covers all configured members. Total mechanical energy is the sum of every member's `0.5 * mass * dot(velocity, velocity)` plus `-G * massA * massB / sqrt(dot(deltaPosition, deltaPosition) + softening^2)` for every unordered configured pair. Total angular momentum is the vector sum of `mass * cross(position - primaryPosition, velocity - primaryVelocity)` for every non-primary member. The UI reports signed energy drift `(E - E0) / abs(E0)`, non-negative angular-momentum drift `length(L - L0) / length(L0)`, maximum absolute energy drift, and maximum angular-momentum drift since the fresh seed. A non-finite seed is a globally blocking numerical-health failure. If `E0 == 0` or `length(L0) == 0`, that normalized diagnostic is typed unavailable rather than divided by zero or treated as instability. Conservation remains informational and cannot end either the core or auxiliary horizon. |
| Explicit reset | Reset first joins any in-flight worker slice, retires the current publication, snapshots the then-current authoritative live solver state, clears all latched failures and conservation maxima, and restarts absolute continuous tick/time at zero. It retains already-authorized warmed capacity. Ring wrap never performs any of these resets. |
| Other retirement paths | Disable, scene change, replay branch restore, and target/cohort change join in-flight work before retirement. Re-entry always seeds from a fresh live snapshot and may reuse warmed capacity. Bounded `PREDICT`, live Physics, and Replay remain separate and are not reset or rewritten by continuous-mode reset. |
| Full-speed CPU budget | Yes: the existing worker-local `5.0 ms` slice is the intended first-version full-speed speculative-physics budget. Current bounded `PREDICT` has two clocks: `Runtime/App/ReplayRuntime.cpp` uses a frame-side `budgetStart` to stop source preparation/admission after 5 ms, then `RunReplayPredictionWorkerRange` starts its own `probeStart` and yields only after the first indivisible completed tick at or beyond another 5 ms. Continuous mode preserves that exact dual-check behavior; it does not falsely claim setup time is subtracted from worker time. It supplies no finite horizon, reveal delay, or ordinary tick-count cap. The bounded deterministic-capture path's eight-tick submit cap is a test scheduler and must not apply to `CONTINUOUS`, including fixed-step automation. This ruling grants no background loop, reserve-cap increase, unified-deadline rewrite, or additional budget. |

The current reserve inventory remains unchanged: all Prediction storage uses the
Replay-phase `replay_prediction_working_set` owner with its 960 MiB hard cap.
The current closed-world Runtime package rules admit Planning as the product
owner and Prediction as the generic producer; OF0 adds neither a package edge
nor a growth privilege.

### Pre-change witnesses

- The witnessed source state is
  `b09584b2b75b7e4d5277188a0ed9b8b3d98e1ff2`. The Automation executable
  SHA-256 is
  `35ED4619399CD7CF1527BA96594F253A9AEF0025207697ECC3268338A1BE7C1A`,
  the Debug executable SHA-256 is
  `5E536AC5ED27DDCF7D68E5AC4B2482721C25C80184ED259E1D0080055295BCF3`,
  and the authored scene SHA-256 is
  `A35A8FF3BD9B4E076D221D413F1ECAF28FD9DD4E21705B69AF5127104DE5862D`.
- The tracked automation workflow
  `SkullbonezData/interaction/continuous_orbit_of0_baseline.json` ran twice in
  isolated Automation processes from the same source and scene. Both reports
  passed four assertions, completed all 14,401 bounded frames, retained
  13,066,240 prediction-evidence capacity bytes and 709,699,964 total Replay
  tracked-capacity bytes, and produced the same rendered submission hash
  `0x0E0FF9DB0F2EF6E0`.
- The two bounded reports are
  `TestOutput/validation/ORBIT_FORECAST_OF0/solar_system_bounded_prediction_report.json`
  and
  `TestOutput/validation/ORBIT_FORECAST_OF0/solar_system_bounded_prediction_report_repeat.json`;
  the screenshot is the sibling `solar_system_bounded_prediction.bmp`. The
  workflow asserts completion, path visibility, selected target, and
  fingerprint readiness; it deliberately does not pin an expected hash and is
  baseline evidence rather than a determinism proof.
- The two bounded private-simulation hashes were
  `0x8FCE7296D4B3401D` and `0x8885F59E0BA29A27`; the corresponding trajectory
  fingerprints were `0x222E4424B4F7890F` and `0xF1CD311109C25B23`.
  This pre-change witness therefore records a real bounded-prediction
  determinism defect even though the final submitted geometry hash matched.
  OF2 must preserve the bounded publication boundary, and OF6 must close the
  acceptance requirement for byte-identical bounded value and visual witnesses
  rather than treating the common submission hash as sufficient. The authored
  solar scene starts paused, so these reports correctly expose zero source/live
  solver hashes; they are not used as a live-state witness.
- Separate Debug fixed-step live-scene captures advanced 14,400 ticks (120
  simulated seconds) twice and produced byte-identical 57,601-line CSV files
  with SHA-256
  `F3D71F660228561D155E11511FDF58DBAD8F5EF966765A21B92B711420C2AE62`.
  Over that interval the primary-relative radial ranges were Earth
  `79.007632-80.827554`, Mars `119.792720-123.244592`, and ship
  `75.779818-84.226036` units. These observations ground the authored
  envelopes but do not themselves prove long-term stability.
  The files are
  `TestOutput/validation/ORBIT_FORECAST_OF0/solar_system_live_120s.csv` and
  `TestOutput/validation/ORBIT_FORECAST_OF0/solar_system_live_120s_repeat.csv`.

## OF1 Circular Publication Evidence - 2026-08-19

- `ContinuousPredictionSampleRing` owns one producer's fixed-capacity all-body
  position rows. `Prepare` checks every row/body/component product and routes
  all four backing-vector reserves through the existing Replay-phase
  `replay_prediction_working_set` owner and 960 MiB cap; `Start` rejects later
  preparation, and publication performs no capacity-changing operation.
- Each physical slot uses an odd/even monotonic version. The producer marks the
  slot odd, atomically overwrites its absolute tick and every body position,
  release-commits the even version, and only then release-publishes the next
  absolute-tick cursor. Readers acquire that cursor and accept a copied row
  only when its expected absolute tick and both version reads agree.
- A detached snapshot derives one logical oldest-to-newest interval and exposes
  either one physical segment or two chronological segments across wrap. It
  never invents a newest-to-oldest seam. Absolute ticks and slot versions fail
  closed before unsigned rollover; cancellation retires incomplete work until
  the producer is joined and reset.
- The focused Profile group passes 6/6 cases and 116/116 assertions for empty,
  partial, exactly full, one-wrap, multi-wrap, cancellation/reset, incomplete
  rows, absolute-tick rollover, and concurrent snapshots. The concurrent case
  also passed ten consecutive stress repetitions. `tools\validate_tests.bat`
  passes 610/610 cases and 2,483,870/2,483,870 assertions from this source state.
- Static allocation, dependency, build-configuration, and project-filter checks
  recognize the new owner with zero blocking diagnostics. The allocation
  allowlist records the exact vector members and post-reserve logical resizes;
  it grants no new registration, phase, cap, or growth path.

## OF2 Continuous Producer Evidence - 2026-08-19

- `ContinuousPredictionProducer` synchronously captures authoritative body and
  solver values, seeds a private `PhysicsEngine`, prepares the OF1 ring and
  Tornado storage through the existing `replay_prediction_working_set` owner,
  and only then admits worker submissions. It owns no target horizon, reveal
  state, deterministic-capture tick cap, App pointer, or bounded `PREDICT`
  publication.
- Frame admission and worker execution retain the OF0-ratified dual-clock
  semantics: an expired frame-side five-millisecond budget declines submission,
  while each accepted worker task starts its own five-millisecond slice and
  completes indivisible fixed ticks. The focused witness observes one submit
  advancing more than eight ticks and reaches at least 43,220 ticks (three full
  14,401-row windows plus 17 ticks) without completion or restart.
- The producer publishes only complete all-body position rows plus detached
  tick, simulated-time, measured-throughput, retained-byte, activity, in-flight,
  and failure values. Stop requests ring cancellation and joins the embedded
  worker task before publication is reset; warmed private-engine and ring
  capacity remain retained for reseed.
- The focused Profile case passes 53/53 assertions. It proves the authoritative
  Replay solver hash and an independent seven-slot bounded publication remain
  unchanged, oldest/newest rows stay coherent after three wraps, retained bytes
  and Replay-owner growths stay flat after warm-up and reseed, and immediate
  one-worker retirement joins safely. The exact body position at absolute tick
  1,024 matches between inline zero-thread and one-worker execution.
- `tools\validate_tests.bat` passes 611/611 cases and
  2,483,563/2,483,563 assertions. Allocation policy, strict two-generation
  Replay allocation, dependency, project-filter, build-configuration, glossary,
  signature, aggregate, extraction-scar, complexity (41/41), and reachability
  (96/96) checks have zero blocking diagnostics. Ten temporarily unrooted OF1
  ring operations carry exact `repair-plan` rulings naming OF4 in this live plan;
  OF4 must remove those rulings by composing the producer or deleting the
  surface.
- Performance validation passes. The mapped Physics and Replay visual gates
  reproduce only the inherited owner-controlled varied-CSV and
  `header.topologyVersion` oracle mismatches; no baseline was refreshed.

## OF3 Planning Stability Evidence - 2026-08-19

- `solar_system.scene.json` now authors one bounded orbital-stability contract:
  the fixed sun is Primary, Earth and Mars are CoreOrbiters, and the ship is
  Auxiliary. `AuthoredScene` parses names and policy without an upward Runtime
  dependency, `SceneAuthoredSetup` resolves each name exactly once to a stable
  scene-object ID, and `SceneWorld` retains the resolved value for Planning.
  Snapshot saving resolves those IDs back to current entity names and the
  save/reparse test proves the contract round trips.
- `ContinuousOrbitalStabilityAnalyzer` is Planning-owned and consumes detached
  complete body/contact tick values. Global numerical health rejects an
  incomplete, out-of-order, failed, nonfinite, or unrepresentable configured
  publication. Primary/core contact and core envelope or sustained-escape
  failures block system orbital health; auxiliary involvement latches a
  separately visible auxiliary failure without poisoning the core horizon.
- Escape uses the ratified fixed-primary softened law and requires outward
  motion, positive specific energy, the authored start radius, and 600
  consecutive fixed ticks. Informational conservation covers every configured
  member with softened all-pair potential, signed normalized energy drift, and
  nonnegative angular-momentum-vector drift; zero seed denominators make the
  corresponding measure unavailable. First failures remain latched while later
  valid observations continue advancing progress and diagnostics.
- The focused analyzer coverage comprises 8 cases, including stable finite
  motion, inclusive inner/outer envelopes, blocking and auxiliary contacts,
  sustained versus transient escape, malformed contracts, incomplete/private-
  step/publication failures, nonfinite state, zero conservation denominators,
  reset, and first-failure retention. `tools\validate_tests.bat` passes 620/620
  cases and 2,487,883/2,487,883 assertions.
- Automation, Debug, and Profile builds pass. Dependency, project-filter,
  build-configuration, formatting, glossary, signature, aggregate (89/89),
  extraction-scar, complexity (41/41), and reachability (118/118) inventories
  are current with zero blocking diagnostics. Until OF4 supplies the App
  production root, the analyzer and OF2 producer operations carry exact
  `repair-plan` rulings naming this live plan; OF4 must remove those rulings by
  composing the owners or deleting the unrooted surface.
- OF3 adds no downward Replay include, `PhysicsBodyRecord` field, reserve
  registration, cap increase, phase relaxation, or post-start growth path. The
  fixed 16-member Scene contract and analyzer counters are ordinary retained
  value storage, not a new allocation privilege.
- `tools\validate_fast.bat` passes all nine stages. The mapped Physics gate
  reproduces only the inherited owner-controlled 20,394-row
  `physics_regression_varied.csv` mismatch beginning at frame 102; OF3 changes
  no Physics source or expected output, and no baseline was refreshed.

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
- Do not add a new post-gameplay allocation privilege, increase the 960 MiB
  prediction reserve cap, or weaken its replay-phase check under this plan.
- Do not alter `PhysicsBodyRecord` or any hot Physics store row; forecast metrics
  belong in stage/product-owned bounded storage.
- Do not refresh Physics, Replay, SkullScope, or visual baselines without later
  explicit owner approval on an exact candidate transition.

## Phases

- [x] **OF0 - Ratify observable stability and interaction semantics.** Record
  exact owner answers for the solar-system core cohort, ship treatment, radial
  envelopes, sustained escape condition, collision policy, reset behavior, and
  whether the existing five-millisecond slice is the intended full-speed CPU
  budget. Capture a bounded-prediction and live-scene hash witness before code
  changes. No later phase is selectable until these answers are in this file.
- [x] **OF1 - Prove the circular publication primitive in isolation.** Add a
  fixed-capacity all-body sample ring with absolute tick identity, coherent-row
  release publication, logical oldest-to-newest iteration, wrap-safe segment
  boundaries, checked counters, and no post-start growth. Unit tests cover empty,
  partial, exactly full, one-wrap, multi-wrap, cancellation, and concurrent
  publication snapshots without involving UI or DX12.
- [x] **OF2 - Add the lower continuous prediction producer.** Seed and retain a
  private Physics engine through the existing prediction reserve owner, submit
  unlimited-target fixed-tick slices under the ratified frame budget, capture
  only bounded path/head values, and expose a detached view. Prove that live
  solver hashes and bounded `PREDICT` state remain unchanged while continuous
  forecast advances beyond 120 seconds and through at least three ring wraps.
- [x] **OF3 - Add Planning-owned stability analysis.** Parse or derive the
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

Validation remains deferred until a phase is being prepared for commit.

| Scope | Required pre-commit evidence |
|---|---|
| Ring and stability unit/value tests | Focused `SKULLBONEZ_TESTS` filters, then `tools\validate_tests.bat` |
| Runtime package or reserve changes | `tools\validate_dependency_graph.bat`, `tools\validate_replay_allocation_policy.bat`, and `tools\validate_fast.bat` |
| Private Physics stepping or stability metrics | Worker-count deterministic witness and `tools\validate_physics.bat` |
| Replay/Prediction value or overlay changes | `tools\validate_replay_visual_fidelity.bat` |
| Overlay/DX12 path changes | `tools\validate_dx12_renderer.bat` and `tools\run_graphics_stress.bat 1` |
| Continuous hot path | `tools\validate_perf.bat` plus a three-wrap retained-byte/growth witness |
| Final combined source state | `tools\agent_validate.bat --plan-completion` after the focused gates above |

## Registration State

The owner reactivated this plan and moved it from `WNF/` to `TODO/` on
2026-08-17. When the binding order reaches OF0, refresh the source evidence,
settle OF0, confirm the then-current prediction reserve inventory and Runtime
package rules, and implement through the repo-local orchestrator skill. Do not
begin it ahead of Predicted Solver Cause Hierarchy merely because it is active.

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
