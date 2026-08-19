# At-Rest Ball Stability

Date: 2026-08-18
Status: Active; 1/8 phases complete
Impact area: Physics terrain contacts, persistent contact solving, rolling and
sliding response, sleep eligibility/transition, SkullScope diagnostics, tests,
known-issue baselines, and performance
Owner: Physics contact and sleep owners
Priority: Third in the active binding order, after `ORBIT_FORECAST`
Commit name: `REST_STABILITY`

## Goal

Diagnose and fix the visible failure of balls in
`SkullbonezData/scenes/at_rest.scene.json` to settle credibly. Balls currently
roll back and forth for too long, oscillate vertically while rolling, slide
before coming to rest, and struggle to enter the sleeping state.

The repair must be evidence-led. Use SkullScope to identify whether each
symptom comes from terrain manifold/support churn, normal bias or restitution,
friction/rolling response, warm-start/cache behavior, island support, or the
sleep transition. Do not tune an unrelated global constant until bounded trace
evidence names the responsible owner and a focused negative control proves the
measurement would catch the old behavior.

The completed scene must preserve physical wake behavior and deterministic
fixed-step execution while reaching a quiet, supported sleep state without a
solver-local velocity snap.

## Owner Direction

- Keep the authored `at_rest` masses, radii, restitution, gravity, timestep,
  time scale, and initial poses as the primary witness. The retired 1,800-frame
  playback cap was an arbitrary timeout, not a physics requirement; do not
  replace it with another time or frame cap to make the scene appear complete.
- The authoritative scene completion predicate is exact: `ball_a`, `ball_b`,
  and `ball_c` must all be in the Physics-owned sleeping state. The scene must
  continue stepping while any one of those balls remains awake. An operator
  may abort a diagnostic run, but an abort is a failed/incomplete witness and
  can never be reported as scene completion.
- Friction coefficients and sleep thresholds are owner-gated fallback
  hypotheses, not assumptions. Diagnose whether the existing values are truly
  insufficient, but first exhaust contact geometry, solver response, cache,
  support, and counter-reset defects that can manufacture the same symptoms.
- Non-production one-variable A/B probes may vary terrain/object/rolling/spin
  friction or `PhysicsSleepConfig.linearSpeed`, `angularSpeed`, and `frames` to
  measure sensitivity. No production policy value may change without an
  evidence packet and the owner's explicit approval.
- If a policy value is the only explanation consistent with the evidence,
  report the exact requested value/delta, affected consumers, before/after
  metrics, cross-scene consequences, and rejected proper fixes. Block that
  production edit until the owner says yes.
- `PhysicsSleepController` remains the only owner that transitions a body into
  sleep and zeroes residual velocities. Contact solving may publish support and
  quieting evidence, but it may not reintroduce a solver-local hard snap.
- Treat vertical vibration, tangential sliding, rolling reversals, and delayed
  sleep as separate measured symptoms. One fix may share a root cause, but a
  passing sleep time alone does not prove the motion is visually or physically
  stable.
- Preserve before/after executables, CSVs, SkullScope traces/caches, and bounded
  query outputs. No physics, replay, or known-issue baseline refresh is
  authorized until the owner reviews the candidate behavior.

## Current Evidence

- The authored witness previously stopped after 1,800 fixed-step frames because
  `playback.frames` was `1800` and `exitOnComplete` was `true`. That arbitrary
  stop has been removed: playback is unlimited and cannot auto-exit until the
  plan adds an authored all-three-ball sleep requirement. The three boxes remain
  control bodies; they need not sleep to complete the witness, but a ball-only
  fix must not destabilize them.
- `tools/validate_physics_deep.bat` already produces
  `Debug/physics_known_at_rest.csv`. The committed known-issue signature calls
  out mixed resting jitter/interpenetration, but an exact file hash is only a
  change detector, not a semantic settling oracle.
- SkullScope already records body sleep/support/inhibition state and contact
  slip speed, rolling residual, friction limit, warm-start state, and sleep
  support. Named queries cover summary, events, energy, rolling, solver,
  contacts, bodies, islands, stacks, and the `why_not_resting` question pack.
- Terrain rest policy applies rolling resistance and support eligibility in
  `PersistentContactSolver.cpp`; the sleep owner consumes support and quiet-
  frame policy in `PhysicsSleepController`. These responsibilities must not be
  collapsed into a second retained rest-state owner.

## RS0 Ratified Baseline — 2026-08-19

The generic gate resolves the three authored names after final scene population,
rejects missing or fixed targets, and samples the Physics-owned `awake` bytes in
the existing post-physics automation-gate pass. Its observation is deliberately
not latched: a later wake clears completion. The authoritative run below exited
naturally at physics frame 35,159 only when all three balls were simultaneously
asleep. `target_frames` remained `-1`; render frame 7,032 is diagnostic process
bookkeeping, not a completion timeout.

Exact generation command:

```bat
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData\scenes\at_rest.scene.json --physics-diag Debug\at_rest_before.physicsdiag.ndjson
```

The run contains 35,160 fixed physics steps at 1/120 s, reaches 292.991682 s,
and ends with six sleeping bodies. The query cache was built only after the
process closed. No watchdog, operator abort, baseline refresh, raw-NDJSON read,
or raw-SQLite read was used.

### Preserved Baseline Artifacts

Ignored preservation root:
`TestOutput/validation/candidates/REST_STABILITY_RS0_BEFORE/`.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `SKULLBONEZ_CORE_Debug_before.exe` | 13,446,144 | `442B1D2D4E0A7955739134F417000381137421CBC99F4A82F79F990499F0F5E1` |
| `SKULLBONEZ_TESTS_Debug_before.exe` | 16,533,504 | `30787DAC38EC6959FE6198892502C65400D9540D38212B46D5DC6EEC00B15EB5` |
| `physics_known_at_rest_before.csv` | 7,631,545 | `C91B03DA690F2BB9D93299639A52ACFFB98820407B0C4DF8EBD846AF8DE4A2FE` |
| `at_rest_before.physicsdiag.ndjson` | 269,418,914 | `869B947FB3C9DE1D941E331477FF6FF5E25C12E528A41DBB74316C537FEC469F` |
| `at_rest_before.physicsdiag.sqlite` | 124,059,648 | `F87E1362EE22007D2F2069A4FE6857743990A363463FB887F2BBF8B08E57E68F` |

The 0/1/4-worker witnesses each contain 210,961 CSV lines and 29,837,159
bytes. All three hash to
`8C3E4187B87F6FA24C2FCF972C43EE36711B42731414061CDE9748DB408222EF`.
Their equality proves the pre-fix scene trajectory and all-three-sleep exit are
worker-count deterministic; it does not claim that the trajectory is good.

### Ratified Semantic Targets

These are analyzer failures, never playback stop conditions. The scene remains
unlimited and continues until the exact all-three-Physics-asleep gate succeeds.

| Measure | RS0 target | Unit/owner basis |
|---|---:|---|
| Material impact | `preSolveClosingSpeed >= 2.0 m/s` | Existing contact restitution threshold |
| Tail audit onset | frame 7,200 / 60.0 s | 56% later than slowest box-control final sleep at 38.423 s |
| Aggregate quality deadline | all three balls asleep by frame 14,400 / 120.0 s | Measurement only; not a cap or exit path |
| Final sleep latency | <= 1,200 frames / 10.0 s after last material impact | Includes the existing 30 consecutive quiet-frame requirement |
| Post-impact vertical speed | max `abs(vy) <= 0.5 m/s` | Existing Physics sleep linear-speed boundary |
| Post-impact contact slip | max `slipSpeed <= 0.5 m/s` | Same engine-unit deadband as quiet linear motion |
| Pre-sleep accumulated slip | <= 0.25 of that ball's radius | Dimensionless per-body visual displacement bound |
| Late horizontal reversals | <= 1 per X axis and <= 1 per Z axis after frame 7,200 | Count sign changes only outside the `+/-0.5 m/s` deadband |
| Resting normal loop | no sub-2.0 m/s terrain contact at max 2.0 m/s separation bias followed by a >=2.0 m/s terrain re-impact without an external impact | Separates solver-created bounce from legitimate restitution |

### Baseline Semantic Table

| Ball | Final sleep frame | Last material impact | Latency | Post-impact max `abs(vy)` | Max slip | Approx. slip distance | Late X/Z reversals | Baseline ruling |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| `ball_a` / body 0 / radius 8 | 34,983 | 34,383 | 600 frames / 5.000 s | 0.140679 m/s | 2.206871 m/s | 7.6872 / 0.961 radius | 7 / 9 | Fails slip, displacement, reversals, aggregate deadline |
| `ball_b` / body 1 / radius 7 | 35,155 | 34,864 | 291 / 2.425 s | 0.969157 m/s | 0.815522 m/s | 1.6905 / 0.242 radius | 15 / 17 | Fails vertical, slip, reversals, aggregate deadline |
| `ball_c` / body 2 / radius 9 | 34,308 | 33,460 | 848 / 7.066 s | 0.703039 m/s | 2.199239 m/s | 4.4269 / 0.492 radius | 10 / 8 | Fails vertical, slip, displacement, reversals, aggregate deadline |

All three baseline latency values happen to pass the 10-second guard, which is
why latency alone is not the repair oracle. The scene still needs 292.99 s to
obtain one simultaneous all-asleep view.

### Named Causal Witnesses

- Vertical loop: `ball_c`, terrain contact `2:-1:0`, frame 635. Closing speed
  1.987702 m/s is below the material-impact boundary, yet penetration is
  0.398529, separation bias is clamped at 2.0 m/s, and slip/rolling residual is
  1.284800 m/s. The same contact re-impacts at frame 698 with closing speed
  2.759281 m/s. This is the first suspicious sub-threshold/max-bias-to-material-
  re-impact sequence.
- Rolling reversal: after the 60-second tail onset, `ball_a` reverses Z outside
  the 0.5 m/s deadband between frames 7,479 and 7,533. At frame 7,533 contact
  `0:-1:0` closes at 3.345478 m/s with penetration 0.312057 and rolling
  residual/slip 1.021412 m/s. The baseline totals 7/9, 15/17, and 10/8 X/Z
  reversals for balls A/B/C.
- Sleep reset: `ball_c` sleeps during frames 21,088..21,429, then wakes at
  frame 21,430 when sphere contact `0:2:0` closes at 0.494698 m/s with normal
  impulse 8.034171; its counter drops from 30 to 1 and island membership changes
  from 3 to 1. `ball_a` later sleeps through frame 30,373 and wakes at 30,374
  from `ball_c` contact closing at 3.130873 m/s. `ball_b` does not sleep until
  frame 35,155, when island 2 contains only sleeping `ball_b` at zero speed;
  `ball_c` has eight sleep segments before its final transition.

The controls settle much earlier without a ball-only completion requirement:
`box_a` final-sleeps at frame 1,289 / 10.741 s (last material impact 1,245,
max speed/omega 21.091541/3.419009); `box_b` at 970 / 8.083 s (last impact 889,
26.650600/5.031497); and `box_c` at 4,611 / 38.423 s after one wake (last
impact 4,426, 31.146898/1.975156). RS3-RS6 must retain or improve these controls.

### SkullScope Query And Size Ledger

The query artifacts below are copied beside the preserved baseline. `Output
read` is ASCII/UTF-8 query text actually exposed to a model, not the UTF-16
`Tee-Object` file size. Two initial parallel calls were truncated; their shared
40,106-character model payloads are counted once each and are not falsely
apportioned among commands. Later full-history query output was redirected to
`Out-Null` and read only by local metric code, so its GPT-read size is exactly
zero. The six truncated body/contact queries were rerun individually and shown
without truncation; three compact contact witnesses and two island witnesses
were also shown without truncation. The island rows cover `ball_c` alone asleep,
the `ball_a`/`ball_c` wake merge, and `ball_b` alone at its delayed final sleep.

| # | Exact `tools\physics_query.bat` query | Output read | Truncated / retained artifact |
|---:|---|---:|---|
| 1 | `Debug\at_rest_before.physicsdiag.ndjson summary` | 1,085 B | No; expected `PermissionError` while the trace owner still held the file; no artifact |
| 2 | `Debug\at_rest_before.physicsdiag.ndjson summary` | 2,796 B | No; `at_rest_before_summary.json` |
| 3 | `Debug\at_rest_before.physicsdiag.ndjson questions why_not_resting` | 1,462 B | No; `at_rest_before_why_not_resting.json` |
| 4 | `Debug\at_rest_before.physicsdiag.ndjson events --type failed_to_sleep,sleep_inhibited_quiet,unsupported_sleep,rolling_slip,friction_saturation,energy_spike,penetration_sustained,penetration_growing --limit 40` | 298 B | No; empty event set in `at_rest_before_events.json` |
| 5 | `Debug\at_rest_before.physicsdiag.ndjson rolling --limit 30` | 15,245 B | No; `at_rest_before_rolling.json` |
| 6 | `Debug\at_rest_before.physicsdiag.ndjson solver --include-convergence --limit 30` | 20,812 B | No; `at_rest_before_solver.json` |
| 7 | `Debug\at_rest_before.physicsdiag.ndjson energy` | 28,245 B | No; `at_rest_before_energy.json` |
| 8 | `Debug\at_rest_before.physicsdiag.ndjson body 0 --frames 0:35159 --limit 30` | 40,106 characters shared across rows 8-10 | Yes; full artifact contains 18,420 characters |
| 9 | `Debug\at_rest_before.physicsdiag.ndjson body 1 --frames 0:35159 --limit 30` | Included in row 8 shared payload | Yes; full artifact contains 19,080 characters |
| 10 | `Debug\at_rest_before.physicsdiag.ndjson body 2 --frames 0:35159 --limit 30` | Included in row 8 shared payload | Yes; full artifact contains 18,466 characters |
| 11 | `Debug\at_rest_before.physicsdiag.ndjson body --help` | 559 B | No |
| 12 | `Debug\at_rest_before.physicsdiag.ndjson stacks --frames 0:35159 --limit 50` | 9,573 B | No; `at_rest_before_stacks.json` |
| 13 | `Debug\at_rest_before.physicsdiag.ndjson contacts --help` | 710 B | No |
| 14 | `Debug\at_rest_before.physicsdiag.ndjson contacts --body 0 --top frame --limit 30` | 40,106 characters shared across rows 14-16 | Yes; full artifact contains 14,981 characters |
| 15 | `Debug\at_rest_before.physicsdiag.ndjson contacts --body 1 --top frame --limit 30` | Included in row 14 shared payload | Yes; full artifact contains 14,954 characters |
| 16 | `Debug\at_rest_before.physicsdiag.ndjson contacts --body 2 --top frame --limit 30` | Included in row 14 shared payload | Yes; full artifact contains 14,927 characters |
| 17 | `Debug\at_rest_before.physicsdiag.ndjson body --help` | 559 B | No; `rs0_body_help.txt` |
| 18 | `Debug\at_rest_before.physicsdiag.ndjson contacts --help` | 710 B | No; `rs0_contacts_help.txt` |
| 19 | `Debug\at_rest_before.physicsdiag.ndjson body 0 --frames 0:35159 --limit 40000` | 0 B | No model read; `rs0_body_0_all.json` |
| 20 | `Debug\at_rest_before.physicsdiag.ndjson body 1 --frames 0:35159 --limit 40000` | 0 B | No model read; `rs0_body_1_all.json` |
| 21 | `Debug\at_rest_before.physicsdiag.ndjson body 2 --frames 0:35159 --limit 40000` | 0 B | No model read; `rs0_body_2_all.json` |
| 22 | `Debug\at_rest_before.physicsdiag.ndjson contacts --frames 0:35159 --body 0 --top frame --limit 40000` | 0 B | No model read; `rs0_contacts_body_0_all.json` |
| 23 | `Debug\at_rest_before.physicsdiag.ndjson contacts --frames 0:35159 --body 1 --top frame --limit 40000` | 0 B | No model read; `rs0_contacts_body_1_all.json` |
| 24 | `Debug\at_rest_before.physicsdiag.ndjson contacts --frames 0:35159 --body 2 --top frame --limit 40000` | 0 B | No model read; `rs0_contacts_body_2_all.json` |
| 25 | `Debug\at_rest_before.physicsdiag.ndjson contacts --frames 0:35159 --body 3 --top frame --limit 40000` | 0 B | No model read; `rs0_contacts_body_3_all.json` |
| 26 | `Debug\at_rest_before.physicsdiag.ndjson contacts --frames 0:35159 --body 4 --top frame --limit 40000` | 0 B | No model read; `rs0_contacts_body_4_all.json` |
| 27 | `Debug\at_rest_before.physicsdiag.ndjson contacts --frames 0:35159 --body 5 --top frame --limit 40000` | 0 B | No model read; `rs0_contacts_body_5_all.json` |
| 28 | `Debug\at_rest_before.physicsdiag.ndjson body 3 --frames 1200:1350 --limit 500` | 0 B | No model read; `rs0_box_3_sleep_window.json` |
| 29 | `Debug\at_rest_before.physicsdiag.ndjson body 4 --frames 900:1020 --limit 500` | 0 B | No model read; `rs0_box_4_sleep_window.json` |
| 30 | `Debug\at_rest_before.physicsdiag.ndjson body 5 --frames 4500:4670 --limit 500` | 0 B | No model read; `rs0_box_5_sleep_window.json` |
| 31 | `Debug\at_rest_before.physicsdiag.ndjson contacts --frame 635 --body 2 --limit 10` | 772 B | No; `rs0_witness_vertical_f635_b2.json` |
| 32 | `Debug\at_rest_before.physicsdiag.ndjson contacts --frame 7533 --body 0 --limit 10` | 773 B | No; `rs0_witness_reversal_f7533_b0.json` |
| 33 | `Debug\at_rest_before.physicsdiag.ndjson contacts --frame 21430 --body 2 --limit 10` | 1,261 B | No; `rs0_witness_wake_f21430_b2.json` |
| 34 | `Debug\at_rest_before.physicsdiag.ndjson body 0 --frames 0:35159 --limit 30` | 18,420 characters | No; `rs0_rerun_body_0_limit30.json` |
| 35 | `Debug\at_rest_before.physicsdiag.ndjson body 1 --frames 0:35159 --limit 30` | 19,080 characters | No; `rs0_rerun_body_1_limit30.json` |
| 36 | `Debug\at_rest_before.physicsdiag.ndjson body 2 --frames 0:35159 --limit 30` | 18,466 characters | No; `rs0_rerun_body_2_limit30.json` |
| 37 | `Debug\at_rest_before.physicsdiag.ndjson contacts --body 0 --top frame --limit 30` | 14,981 characters | No; `rs0_rerun_contacts_body_0_limit30.json` |
| 38 | `Debug\at_rest_before.physicsdiag.ndjson contacts --body 1 --top frame --limit 30` | 14,954 characters | No; `rs0_rerun_contacts_body_1_limit30.json` |
| 39 | `Debug\at_rest_before.physicsdiag.ndjson contacts --body 2 --top frame --limit 30` | 14,927 characters | No; `rs0_rerun_contacts_body_2_limit30.json` |
| 40 | `Debug\at_rest_before.physicsdiag.ndjson island 3 --frame 21429` | 683 characters | No; `rs0_island_3_f21429.json` |
| 41 | `Debug\at_rest_before.physicsdiag.ndjson island 1 --frame 21430` | 841 characters | No; `rs0_island_1_f21430.json` |
| 42 | `Debug\at_rest_before.physicsdiag.ndjson island 2 --frame 35155` | 683 characters | No; `rs0_island_2_f35155.json` |

Total GPT-read SkullScope query output: **268,107 characters**. Truncation:
**yes**, limited to the original shared calls represented by rows 8-10 and
14-16; those six exact queries were individually rerun without truncation in
rows 34-39. Raw trace/cache bytes were never read by a model.

## SkullScope Diagnostic Contract

First add the generic authored sleep-completion gate described by RS0, then
generate the baseline trace from the unlimited scene and a preserved Debug
executable. The executable must exit only after all three named balls are
simultaneously asleep:

```bat
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData\scenes\at_rest.scene.json --physics-diag Debug\at_rest_before.physicsdiag.ndjson
```

Start broad and narrow to named balls, frames, contacts, and islands:

```bat
tools\physics_query.bat Debug\at_rest_before.physicsdiag.ndjson summary
tools\physics_query.bat Debug\at_rest_before.physicsdiag.ndjson questions why_not_resting
tools\physics_query.bat Debug\at_rest_before.physicsdiag.ndjson events --type failed_to_sleep,sleep_inhibited_quiet,unsupported_sleep,rolling_slip,friction_saturation,energy_spike,penetration_sustained,penetration_growing --limit 40
tools\physics_query.bat Debug\at_rest_before.physicsdiag.ndjson rolling --limit 30
tools\physics_query.bat Debug\at_rest_before.physicsdiag.ndjson solver --include-convergence --limit 30
tools\physics_query.bat Debug\at_rest_before.physicsdiag.ndjson energy
```

For each ball, follow suspicious windows with bounded `body`, `contacts`, and
`island` queries. Never ingest the full NDJSON or SQLite cache into model
context. Every phase handoff that uses SkullScope records the exact generation
command, raw trace/cache byte sizes, every query, per-query GPT-read output
size, total GPT-read size, and whether output was truncated, as required by
`Agentic/Skills/skore-skullscope/skill.md`.

Capture these semantic measures for each ball from first sustained terrain
support through its sleep transition. There is no successful end-of-run before
all three transitions have occurred:

- time of last material impact, first sustained support, and sleep transition;
- sleep-counter resets, inhibition reasons, island membership, and support
  churn;
- vertical position/speed envelope and contact penetration/bias/normal impulse;
- tangential slip distance/speed, friction saturation, accumulated tangent
  impulse, spin, rolling residual, and direction-reversal count;
- warm-start/cache continuity and solver convergence around each reversal or
  vertical oscillation; and
- kinetic-energy decay, any late energy injection, and wake events.

RS0 ratifies exact quality thresholds from engine units and the baseline. They
must require all three balls to settle materially sooner after their last real
impact, eliminate visible tail vibration and repeated rolling reversals, and
bound pre-sleep slip. A missed quality threshold is a failure measurement, not
a reason to stop playback: the scene continues until all three balls sleep.

## Phases

### RS0 — Establish Sleep Completion, Preserve, And Diagnose

- [x] Extend authored scene requirements with a generic named-dynamic-body sleep
  gate, resolve `ball_a`, `ball_b`, and `ball_c` at load, and feed its status
  through the existing Scene automation-gate owner. Do not add a second Physics
  sleep owner or let Runtime infer sleep from velocity thresholds.
- [x] Author `at_rest.scene.json` to auto-exit on that requirement while keeping
  `playback.frames: "unlimited"`. Prove it keeps stepping with any named ball
  awake and completes only when all three are simultaneously Physics-asleep.
- [x] Add parser/runtime tests for missing names, fixed bodies, one remaining
  awake ball, all three asleep, and wake-before-completion. No frame timeout or
  manually aborted trace may satisfy this gate.
- [x] Preserve the baseline Debug CORE/TESTS executables, known-issue CSV,
  SkullScope trace/cache, and exact hashes before any behavior-bearing edit.
- [x] Run the broad SkullScope packet, then bounded per-ball body/contact/island
  queries around first support, reversals, vertical peaks, sleep-counter resets,
  and final motion.
- [x] Record the causal sequence for each symptom and ratify semantic numeric
  acceptance thresholds. Distinguish initial impact motion from the late tail.
- [x] Record box control-body metrics and 0/1/4-worker hashes.

Evidence: complete SkullScope size/query accounting, baseline semantic table,
artifact hashes, and named first suspicious frame/body/contact for each symptom.

### RS1 — Add Semantic Oracles Before The Fix

- [ ] Add a bounded at-rest stability analyzer or focused test harness that
  reports the RS0 measures without treating the full CSV hash as success.
- [ ] Pin all three balls individually plus aggregate scene completion. Keep
  box behavior as a non-regression control.
- [ ] Add planted negative controls for vertical impulse oscillation, excessive
  slip, rolling reversal, and sleep-counter reset so each oracle demonstrably
  fails.
- [ ] Keep query output bounded and preserve the existing generic SkullScope
  schema unless diagnosis proves a missing fact.

Evidence: focused tests/analyzer fixtures, four failing negative controls, and
the unchanged baseline failing the new semantic target for the expected reason.

### RS2 — Adjudicate The Root Causes

- [ ] Use trace evidence and one-variable diagnostic A/Bs to classify terrain
  contact geometry/support, normal bias/restitution, tangent friction, rolling
  resistance, warm-start/cache continuity, and sleep policy separately.
- [ ] For every proposed edit, name the invariant-owning function/stage and the
  exact symptom/metric it owns. Reject broad global tuning without causal proof.
- [ ] Treat friction-coefficient and sleep-threshold changes as diagnostic
  probes only while proper solver/contact/support fixes remain viable. If the
  evidence isolates a policy deficiency, prepare the owner-decision packet and
  stop before changing production values.
- [ ] Record why each non-owning candidate was ruled out and whether the three
  balls share one cause or need distinct repairs.
- [ ] Update the plan with the selected repair behavior before implementation.

Evidence: bounded before/A/B query comparisons, selected owner rulings, rejected
alternatives, and independent read-only diagnosis review.

### RS3 — Remove Normal-Axis Vibration

- [ ] Repair only the diagnosed normal/support/manifold/cache cause of vertical
  tail oscillation.
- [ ] Preserve legitimate restitution for material impacts; resting contacts
  must not repeatedly manufacture bounce energy below the impact threshold.
- [ ] Prove penetration, position correction, bias, normal impulse, energy, and
  support classification remain bounded for spheres and box controls.
- [ ] Add focused terrain-sphere regressions that fail the pre-RS3 behavior.

Evidence: per-ball vertical envelopes and energy traces, focused solver tests,
and unchanged initial-impact behavior where the repair should be inactive.

### RS4 — Stop Excess Sliding And Rolling Reversals

- [ ] Repair the diagnosed tangent/rolling response without injecting generic
  damping or forcing angular velocity to zero in the contact solver.
- [ ] Keep static/dynamic friction and rolling resistance dimensionally and
  timestep consistent; preserve credible no-slip rolling before rest.
- [ ] Preserve existing friction coefficients unless isolated A/B evidence
  proves they are insufficient and the owner explicitly approves the recorded
  production change.
- [ ] Prove reduced tail slip, bounded friction saturation, monotonic late
  energy decay, and no repeated back-and-forth direction changes after the last
  material impact.
- [ ] Cover slopes/moving contacts or other existing friction cases so the
  at-rest repair does not pin legitimately moving balls.

Evidence: slip/rolling residual tables, reversal counts, focused friction and
rolling tests, and cross-scene motion controls.

### RS5 — Make Supported Quiet Balls Sleep Reliably

- [ ] Keep quiet thresholds, support propagation, inhibition, and final
  transition under `PhysicsSleepController` ownership.
- [ ] Fix spurious motion and counter-reset causes before proposing any change
  to the existing linear/angular thresholds or quiet-frame count. If policy is
  still the isolated cause, block the edit on explicit owner approval.
- [ ] Ensure all three supported balls accumulate consecutive quiet frames and
  sleep after their last material impact; report the measured latency against
  the RS0 quality target without using that target to stop the scene.
- [ ] Prove unsupported, externally forced, materially impacted, or island-
  connected bodies do not sleep early and wake exactly once when required.
- [ ] Add focused counter-reset and supported-transition tests that fail the old
  delayed-sleep sequence.

Evidence: per-ball support/counter/sleep timeline, wake regressions, island
tests, and no solver-local velocity snap.

### RS6 — Integrated Scene Attribution And Determinism

- [ ] Generate a fresh `at_rest_after` SkullScope trace and run the exact RS0
  query set plus `compare` against the preserved baseline.
- [ ] Run the semantic at-rest oracle twice and prove byte-identical candidate
  outputs plus 0/1/4-worker equality.
- [ ] Preserve before/after CORE/TESTS executables, CSVs, traces/caches, bounded
  query outputs, hashes, and the first attributed behavior difference.
- [ ] Check stacking, terrain-contact, slope/friction, and existing sleep scenes
  for regressions; measure physics hot-path cost and allocations.

Evidence: all-ball and box-control tables, deterministic repeat/worker hashes,
cross-scene attribution, performance, and complete SkullScope accounting.

### RS7 — Close The Plan

- [ ] Run the cumulative mapped gates after implementation and an independent
  physics/ownership/test-strength review.
- [ ] Audit every touched source-bearing file against the comment-style guide
  and reconcile its tracked-file checklist.
- [ ] Present the candidate semantic improvements and preserved artifacts for
  owner assessment. Refresh no golden without explicit approval.
- [ ] Run `tools\agent_validate.bat --plan-completion` once after review, update
  MASTER/Session ledgers, and delete this completed plan under repository
  convention.

Evidence: gate logs, review verdict, comment-audit counts, preserved artifact
hashes, baseline decision, and terminal plan-completion output.

## Acceptance Criteria

- [ ] All three balls meet the RS0 semantic targets for vertical stability,
  slip, reversal count, supported sleep latency, and final sleeping state.
- [ ] The authoritative `at_rest` run has no frame/time completion cap and exits
  only after `ball_a`, `ball_b`, and `ball_c` are all Physics-asleep. Any abort
  or watchdog termination is reported as incomplete, never as a pass.
- [ ] The boxes remain stable controls and no slope/moving-contact case is
  falsely pinned or slept.
- [ ] Late kinetic energy decays without solver-induced spikes, repeated resting
  restitution, or sustained/growing penetration.
- [ ] Sleep occurs only through `PhysicsSleepController` after consecutive quiet
  supported frames; wake behavior remains exact.
- [ ] Friction coefficients and sleep thresholds/frame counts are unchanged
  unless the final evidence packet records the owner's explicit approval for an
  exact policy change and its cross-scene validation scope.
- [ ] Candidate runs and 0/1/4-worker results are deterministic.
- [ ] The semantic oracle includes planted false-pass controls and does not use
  the known-issue hash as its only correctness signal.
- [ ] Every SkullScope use reports commands and raw/GPT-read size accounting.
- [ ] Physics/replay/known-issue baselines change only after explicit owner
  acceptance, with before/after executables retained.

## Validation Mapping

| Changed scope | Required pre-commit gate |
|---|---|
| Focused solver/sleep tests | `tools\validate_tests.bat` |
| Contact, terrain, friction, or sleep behavior | `tools\validate_physics.bat` and `tools\validate_physics_deep.bat` |
| Replay-visible solver values or sleep timing | `tools\validate_replay_visual_fidelity.bat` |
| Physics hot path | `tools\validate_perf.bat` plus allocation evidence |
| SkullScope diagnostic/query schema | focused query regression plus `tools\validate_physics.bat` |
| Final combined source state | `tools\agent_validate.bat --plan-completion` once at RS7 |

## Non-Goals

- Do not change the scene's masses, radii, restitution, poses, gravity,
  timestep, or time scale to manufacture a pass. Do not reintroduce a playback
  frame/time cap; the only completion boundary is all three named balls asleep.
- Do not add unconditional global damping, a contact-solver velocity snap, or a
  second retained rest/sleep state owner.
- Do not change terrain/object/rolling/spin friction coefficients or any sleep
  speed/frame threshold without isolated causal evidence and explicit owner
  approval. Diagnostic-only probes do not authorize a production edit.
- Do not disable warm starting, CCD, partial-TOI integration, support
  classification, or wake propagation to reduce visible motion.
- Do not tune material response or sleep policy merely to move the pass line
  around defective contact, solver, cache, support, or counter behavior.
- Do not accept all-balls sleep as sufficient while visible sliding, vertical
  vibration, or repeated rolling reversals remain; completion and motion
  quality are separate requirements.
- Do not ingest raw NDJSON/SQLite artifacts into model context or refresh a
  baseline before owner review.

## Reference Sites

- `SkullbonezData/scenes/at_rest.scene.json`
- `SkullbonezSource/Core/Config.h`
- `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- `SkullbonezSource/Physics/TerrainContactManifold.cpp`
- `SkullbonezSource/Physics/TerrainSupportClassifier.h`
- `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp`
- `SkullbonezSource/Physics/Stages/PhysicsSleepController.State.cpp`
- `SkullbonezSource/Physics/Diagnostics/SkullScope.cpp`
- `SkullbonezTests/TestPersistentContactSolver.cpp`
- `SkullbonezTests/TestSleepController.cpp`
- `SkullbonezTests/TestPhysicsStageState.cpp`
- `tools/physics_query.bat`
- `tools/physics_query.py`
- `tools/check_physics_known_issue_regression.py`
- `tools/validate_physics_deep.bat`
- `Agentic/Skills/skore-skullscope/skill.md`
- `Agentic/Reference/physics-query-reference.md`
