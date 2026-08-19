# At-Rest Ball Stability

Date: 2026-08-18
Status: Active; 2/8 phases complete
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

## RS1 Semantic Oracle Evidence — 2026-08-19

`tools\analyze_at_rest_stability.py` reads through the generic SkullScope SQLite
cache, allowing the shared helper to rebuild that cache when stale; it neither
changes the trace/schema nor compares a golden hash. It emits bounded
summaries/first witnesses. `tools\check_at_rest_stability_analyzer.py` uses detached synthetic
records to prove clean body/control/completion fixtures, the four required
product negatives, and every false-pass boundary reopened by independent
review:

| Planted control | Exact required failure | Result |
|---|---|---|
| Sub-threshold max-bias terrain row followed by material terrain re-impact | `resting_reimpact` | Failed exactly this oracle |
| Post-impact contact slip at 0.6 m/s | `slip_speed` | Failed exactly this oracle |
| `+0.6, -0.6, +0.6 m/s` late X motion | `x_reversals` | Failed exactly this oracle |
| Quiet counter `5 -> 0` before sleep | `sleep_counter_reset` | Failed exactly this oracle |

An additional negative guard inserts a 3.0 m/s external body impact between the
max-bias row and later terrain impact; it passes the resting-loop rule. This
corrects the interpretation of the RS0 frame-635 `ball_c` witness: contact with
`box_c` at frame 642 interrupts the 635-to-698 chain, so that sequence remains
diagnostically suspicious but is not an uninterrupted resting re-impact. The
first bounded uninterrupted witnesses are `ball_a` 838-to-897, `ball_b`
947-to-989, and `ball_c` 918-to-976.

The unchanged baseline fails the semantic analyzer for
`ball_semantic_failure`, while all three box controls pass. A distinct authored
completion witness begins at physics frame 35,155, all six required body
timelines are contiguous and aligned through frame 35,159, and the generic
`process_end` record is not accepted without that witness. The analyzer exactly
reproduces final sleep frames 34,983 / 35,155 / 34,308, last material impacts
34,383 / 34,864 / 33,460, post-impact maximum `abs(vy)`
0.140679 / 0.969157 / 0.703039 m/s, maximum slip
2.206871 / 0.815522 / 2.199239 m/s, accumulated slip distance
7.687219 / 1.690542 / 4.426941 units, and late X/Z reversals
7/9, 15/17, and 10/8 for balls A/B/C. The scene remains unlimited and all
three balls are finally asleep; those completion facts do not hide the motion
quality failures.

Ignored preservation root:
`TestOutput/validation/candidates/REST_STABILITY_RS1_ORACLES/`.

| Artifact | Raw file bytes | SHA-256 |
|---|---:|---|
| `rs1_at_rest_baseline_semantic.json` | 6,065 | `E628C306D8AC968EF9A5B642E4E325076B029A5654CD66C4D151E7ED362EE94B` |
| `rs1_at_rest_analyzer_controls.json` | 6,346 | `FB3DBB75EA03C5F16920924D1B282C35F2EB26C479F55C12F186E5BC732A43C3` |

SkullScope analyzer-use accounting:

| # | Exact command | GPT-read output | Truncated / retained artifact |
|---:|---|---:|---|
| 1 | `python tools\analyze_at_rest_stability.py Debug\at_rest_before.physicsdiag.ndjson --expect-fail --output Debug\rs1_at_rest_baseline_semantic.json` | 5,007 normalized JSON characters | No; diagnostic first version was intentionally overwritten after it exposed incorrect `process_end` classification and max-per-frame rather than all-contact slip accumulation |
| 2 | `python tools\analyze_at_rest_stability.py Debug\at_rest_before.physicsdiag.ndjson --expect-fail --output Debug\rs1_at_rest_baseline_semantic.json` | 5,037 normalized JSON characters | No; final artifact is 5,216 CRLF bytes |
| 3 | `python tools\analyze_at_rest_stability.py Debug\at_rest_before.physicsdiag.ndjson --expect-fail --output Debug\rs1_at_rest_baseline_semantic.json` | 0 characters | No model read; final post-compile validation redirected stdout to `Out-Null` and reproduced the recorded SHA-256 |
| 4 | `python tools\physics_query.py Debug\at_rest_before.physicsdiag.ndjson events --limit 50` | 296 JSON characters | No; confirmed that the baseline carries no separate diagnostics event row |
| 5 | `python tools\physics_query.py Debug\at_rest_before.physicsdiag.ndjson frame 35159` | 3,566 JSON characters | No; terminal all-six-asleep frame summary and body snapshot |
| 6 | `python tools\physics_query.py Debug\at_rest_before.physicsdiag.ndjson frame 35158` | 3,566 JSON characters | No; preceding all-six-asleep frame summary and body snapshot |
| 7 | `python tools\physics_query.py Debug\at_rest_before.physicsdiag.ndjson frame 34983` | 4,608 JSON characters | No; proved the earlier five-asleep/one-awake state cannot satisfy the aggregate gate |
| 8 | `python tools\analyze_at_rest_stability.py Debug\at_rest_before.physicsdiag.ndjson --expect-fail --output Debug\rs1_at_rest_baseline_semantic.json` | 5,913 normalized JSON characters | No; reviewed output was superseded only to add explicit required-body start-frame evidence |
| 9 | `python tools\analyze_at_rest_stability.py Debug\at_rest_before.physicsdiag.ndjson --expect-fail --output Debug\rs1_at_rest_baseline_semantic.json` | 0 characters | No model read; final prefix-coverage hardening run was redirected and produced the recorded 6,065-byte SHA-256 artifact |

Total GPT-read SkullScope-derived output: **27,993 characters**. Truncation:
**no**. The separate 3,170-character planted-control report contains only
synthetic records and is not trace-derived. Raw NDJSON/SQLite bytes were never
read by a model.

The independent review's first pass found four false-pass paths and two
missing-evidence items. The repaired analyzer now rejects generic shutdown
without the terminal all-dynamic sleep witness, gapped or end-misaligned body
histories, and same-frame external-impact ambiguity; box controls additionally
pin their exact RS0 maximum speed and angular-speed envelopes. Isolated
fixtures cover accumulated sub-threshold slip, terminal-suffix failure,
wake-after-tail, same-frame interruption, transient box speed/omega regression,
generic-abort completion, gapped summaries, and end-misaligned bodies in
addition to the original four product negatives.

Formal validation on the final source bytes passed
`tools\validate_at_rest_stability.bat` with all 19 clean/negative cases and
`tools\validate_fast.bat` end to end: formatting and Related paths, project
filters (828/828 items), dependency and ownership gates, all seven current
inventories including 41/41 complexity rulings, Profile tests (627/627 tests;
2,520,346/2,520,346 assertions), Automation/Debug/Profile builds, and compiled
reachability. The touched-source comment audit inspected all three substantive
Python tools, with 3 checked and 0 deferred; the tiny batch wrapper is exempt.

## RS2 Root-Cause Adjudication — 2026-08-20

RS2 used the final RS1 analyzer against detached diagnostic copies of the
authored scene and the unchanged Debug executable. Each config A/B changed
exactly one `engine.cfg` value, generated a separate trace, and then restored
the tracked config before the next probe. The primary diagnostic scene changes
only the playback cap; object geometry, world gravity, requirements, terrain,
and material values match `at_rest.scene.json`. One additional scene A/B
changes only the height-map terrain to analytic flat terrain so sphere
manifold/sweep geometry is exercised directly. No probe value below is a
proposed production tuning change.

Shared launch command (replace `<scene>` and `<trace>` with the table row):

```bat
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --time-scale 100 --automation-hidden-window --scene TestOutput\validation\candidates\REST_STABILITY_RS2_CAUSES\<scene>.scene.json --physics-diag TestOutput\validation\candidates\REST_STABILITY_RS2_CAUSES\<trace>.physicsdiag.ndjson
```

Every row except `terrain_flat` used `<scene>=at_rest_rs2`; `terrain_flat`
used `<scene>=at_rest_rs2_flat`. In every case `<trace>` is the table name.

The executable SHA-256 was
`9417A33E0411A3FBFA37E89A65A5B7CC64B97428FC701B978E2D26BABECBF0B7`;
the restored production config SHA-256 was
`86D53F142EDCE01BAE4F425B4D828856A5F438DF2D73FE6E0E9A01A68ADBC33F`;
the detached diagnostic scene SHA-256 was
`B1AC947BE883834AB2CD78BC3AAAEA9CEC1E893500CA25635B138EA6DF4BE9C4`.
The flat-geometry scene SHA-256 was
`D68CA074E5DA0CA36059AC582E652CE14F954E68F758D9F28A4647B9588A2F03`.
The ignored preservation root is
`TestOutput/validation/candidates/REST_STABILITY_RS2_CAUSES/`.

| Trace | Sole A/B change from control | End / sleep | Reimpacts / late reversals | Max post-impact `abs(vy)` / slip | Box controls | Diagnostic ruling |
|---|---|---:|---:|---:|---:|---|
| `control` | None | 35,159 / 35,155 | 3 / 66 | 0.969157 / 2.206871 | 3/3 | Reproduced RS0 exactly. |
| `restitution_suppressed` | `contact_restitution_threshold: 2 -> 1000000` | 18,964 / 18,962 | 3 / 25 | 0.058190 / 5.059793 | 1/3 | Retained all three bias-to-reimpact chains, so restitution is not necessary to initiate the loop. The global threshold also regressed `box_a` and `box_c`; it is diagnostic only. |
| `normal_bias_zero` | `terrain_contact_baumgarte_beta: 0.3 -> 0` | 17,614 / 17,610 | 0 / 33 | 2.415141 / 1.414740 | 3/3 | Removed every uninterrupted resting reimpact but left slow penetration recovery and poor vertical/slip envelopes; nonzero recovery remains necessary. |
| `normal_bias_cap_half` | `terrain_max_baumgarte_bias: 2 -> 0.5` | 22,424 / 22,423 | 0 / 39 | 1.628077 / 1.473060 | 3/3 | Also removed every uninterrupted resting reimpact without deleting Baumgarte recovery; the current cap/velocity target is causal. |
| `terrain_friction_zero` | `friction_coeff: 0.8 -> 0` | 48,004 / 48,001 | 3 / 162 | 1.314425 / 1.989249 | 0/3 | Worsened every ball and all three box speed/omega controls; the coefficient is necessary and is not the cause. |
| `rolling_resistance_zero` | `rolling_friction_coeff: 0.02 -> 0` | 29,109 / 29,105 | 3 / 98 | 0.927100 / 1.669922 | 3/3 | Did not remove normal reimpacts or repeated reversals; simply deleting rolling resistance is not a repair. |
| `rolling_resistance_tenfold` | `rolling_friction_coeff: 0.02 -> 0.2` | 5,354 / 5,350 | 3 / 0 | 1.037479 / 8.104177 | 3/3 | Removed all late reversals and met the completion deadline, but torque-only damping manufactured a much larger slip peak; implementation, not coefficient magnitude, is deficient. |
| `sleep_frames_one` | `physics_sleep_frames: 30 -> 1` | 36,139 / 36,137 | 3 / 63 | 1.227731 / 3.557763 | 3/3 | Did not remove reimpact, vertical, slip, or reversal failures; sleep dwell is downstream, not causal. |
| `terrain_contact_band_zero` | `terrain_contact_threshold: 0.15 -> 0` | 16,564 / 16,560 | 3 / 28 | 1.235073 / 6.492037 | 2/3 | Inadmissible as a sphere discriminator: this setting is read only by box/hull point selection. Retained solely as disclosed negative evidence. |
| `candidate_bias_half_rolling_tenfold` | Combined diagnostic only: bias cap `0.5`, rolling coefficient `0.2` | 7,434 / 7,434 | 0 / 0 | 1.156262 / 13.900793 | 3/3 | Confirms the two diagnoses are distinct, but still fails slip/vertical targets; broad tuning is rejected. |
| `terrain_flat` | Scene-only: height map -> analytic flat terrain | 1,299 / 1,296 | 0 / 0 | 1.150646 / 0.000000 | 0/3 | Proves heightfield plane geometry is causal context for reimpact/wandering. It changes impact trajectories and invalidates box transient ceilings, so it cannot replace the authored terrain or prove the solver correct. |

The first exact control witness is `ball_a` contact `0:-1:0`. At frame 838
the stable-support row has closing speed 1.945360 m/s, penetration 0.337921,
and receives the full 2.0 m/s separation bias. The contact is absent at frame
839 and returns at frame 897 at 2.596175 m/s. Solver packets report zero cache
hits and one miss across frames 838-839; at frame 897 the aggregate one hit is
the continuing box row while the returning ball is the miss. This matches the
source lifetime: terrain gets the gravity support seed, but a no-contact frame
clears the exact-feature cache. `warmStarted=1` alone is not cache-continuity
evidence because the terrain seed deliberately sets that bit on cache misses.

### Selected Repair Behavior And Owners

1. **Normal-axis energy injection — selected for RS3.**
   `PersistentContactSolveTransaction::PrecomputeRows` owns terrain row bias
   and restitution; the existing terrain position-correction path owns
   non-energetic penetration cleanup. For a stable-support terrain row below
   the material-impact threshold, penetration recovery must not command a
   separating velocity that breaks contact and returns as a fresh material
   impact. RS3 will preserve restitution for genuine new impacts, keep bounded
   penetration recovery, and add a terrain-sphere regression for the exact
   max-bias-to-reimpact sequence. It will not globally zero Baumgarte or
   restitution and will keep the box controls within their RS0 envelopes.

2. **Rolling/slip coupling — selected for RS4.**
   `PersistentContactSolveTransaction::ApplyTerrainRestPolicy` diagnoses the
   current defect but is not the repair phase: it subtracts angular velocity
   after `SolveRows`, so friction cannot correct the contact-point slip until a
   later frame. On the control's late terrain rows, the largest ball-A residual
   is 1.065246 m/s while tangent/normal impulse is only
   `0.263113 / 2.465021 = 0.107`, far below the 0.8 coefficient. With rolling
   resistance disabled the top residual is about 0.000003 m/s; at tenfold it is
   10.651699 m/s. The coefficient is not saturated; the phase ordering is
   causal. RS4 will make `PrecomputeRows` construct the bounded rotational
   rolling-resistance row and `SolveRowsIterations` apply it before recomputing
   and solving the existing tangent lambdas in the same PGS iteration. The
   row operates only on angular velocity projected into the contact tangent
   plane; spin about the contact normal is not rolling. Its accumulated angular
   impulse vector is clamped to
   `rolling_friction_coeff * supporting_normal_impulse * effective_radius`,
   cannot overshoot zero rolling speed, and participates in the same
   convergence decision. It has its own normal-load-derived rolling budget and
   is not a second tangent impulse. `ApplyTerrainRestPolicy` will cease post-solve
   angular mutation. The repair must monotonically remove energy without
   increasing contact slip, generic damping, or unconditional angular zero;
   the production friction coefficient remains 0.8.

3. **Geometry, cache, and sleep rulings.** The flat scene proves heightfield
   normals are causal context, not a defect to hide: the product requires the
   authored height map to settle credibly. The sphere manifold always publishes
   one bottom-pole point, inherits the swept terrain plane normal, and grants
   stable support; the box/hull-only threshold probe says nothing about that
   path. The exact-feature cache carries no stale ball contact through the
   no-contact gap. `PhysicsSleepController` remains the sole sleep-policy owner;
   RS2 rejects only changing its 30-frame dwell, because the one-frame probe did
   not repair upstream motion. Broader support/inhibition/wake policy remains
   deferred to post-RS4 RS5 evidence. No per-ball material, friction, or sleep
   thresholds are selected.

All three balls share the same early normal bias/contact-loss/reimpact loop and
the same torque-only rolling-resistance model. Their different late traces are
trajectory and object-impact consequences, not evidence for per-ball tuning.
RS3 and RS4 therefore own two shared repairs; RS5 may change sleep policy only
if post-RS4 evidence still isolates a policy defect and an owner approves it.

### RS2 Preserved Artifacts And SkullScope Ledger

| Trace | NDJSON bytes / SHA-256 | SQLite bytes / SHA-256 | Semantic JSON bytes / SHA-256 |
|---|---|---|---|
| `control` | 269,418,960 / `F20374D8FDF736A95B0C5EE19311BB6C888F39AE4A27BD79056FB397FBCCF4D7` | 124,059,648 / `02E07A9163944FEB4512DCE5CD222E79715D659477AB635940E3BB28F4CC6DB9` | 6,133 / `D73D65A6ED1CCF83638ADAE98A79638E3DF305C336823809D0392952DCDD9BC9` |
| `restitution_suppressed` | 156,071,069 / `30426186A1DEEF6B2BDB3106DE44C0099EDF1901B10AC1F1939DDA5D8FBFBDF6` | 70,492,160 / `E16289D5E7022636694F7DF85ADE97E551038044720F5D82B7C861BDDE89E13C` | 6,114 / `B792B9DD8D33E26D9D266E6F3DB028A17A04AF4B6093A23438B7906A8E9004F7` |
| `normal_bias_zero` | 143,113,113 / `4E50CB6E9D314F31644CBFE2C3C9ACB8B805A98F4C4C8C302ED4069AC92AF74A` | 66,510,848 / `DB2ED8D0CDE391E08898C25FFC26541EC926E023DEFC6601C8B6EEC9F3931DA2` | 5,464 / `AC0B277710EA91F4146BE95F43F63FC1A136A09CF59D848714235B20B4797C81` |
| `normal_bias_cap_half` | 176,726,945 / `14B73D1474D655C9CA427BA96068786CAC2974411E1D3D24BD9A6036AA315CC4` | 81,154,048 / `0693B83389B5FAC5767766444BA8A2A39ABF9E683A1B66B17F2AF06212ADA39E` | 5,479 / `753808FFAB3B75912989B71C74FE03A2011F0BB2D62FB4C07F69E585E99AF414` |
| `terrain_friction_zero` | 582,758,853 / `C2FD15DAE22DAF767F56BAF9488C3084DBADC39D2780FA44F2B35D0A977CE067` | 249,102,336 / `8AFDBAB35A2D02EBB0A2D7D61A8A3E894FBDE4033F990CA7A76F1C963BFF651A` | 6,430 / `25BA075B4F137F88BCE28DACD792361B13CC60F86A1827C20D2FBFCB7EEC8F4F` |
| `rolling_resistance_zero` | 236,803,543 / `BF3A0DCF52564C927591F4AEAE5AF9A43C7B6A7F0ED0E6C186D1C32A2ABDCF3E` | 110,018,560 / `9C53CCC525EDAED1639F00A33C54B347CDC2D3FDB7E56029C32411120757EBDA` | 6,041 / `1F2028C6F3AD10E6EF0F83F24DB8EF50C5A0B4173C9D382EEBDEE2DA3B7DE5B4` |
| `rolling_resistance_tenfold` | 43,353,585 / `933331447A032103212F49E5A48FCCE557041164FD9887F106F69E7D3024D0B4` | 19,755,008 / `4D4B2C6753EF68B38D0CFFEBF43584BECBC4AF14C953F94044C7C0CB4773536C` | 5,737 / `CDD0A0157AED0AD909962106747BCE752C370B433429B0E5932B5E12906BB30C` |
| `sleep_frames_one` | 269,456,584 / `DF46AA0B6A5A7C4EB6533E2822C5CA702B42DF2BB7A0F4CA4FB7B36AC282E416` | 124,293,120 / `2478A22AA2AECB50C0C2D1D7643D5B69013086DEE2D7073D33701A53C5FCF67D` | 6,028 / `E463FF02CAC8323810A7C79A65B6BA29FCADD64B79C01F8341D7A3E1EEAB5BB8` |
| `terrain_contact_band_zero` | 132,591,050 / `8C0C296670FB7BA437B96734558B91ABDEE54ACFFD4D39A76F5430F6A4E8156A` | 60,993,536 / `E406C7D0836E193CFC399EC66328950AAE0A3981CCE50F3F0E6FB9F6DB5096E9` | 6,370 / `070D7CCFB429201A2ABF75F64F0C08FFF766C98EEC2F592CBD69B549EF1ECB5D` |
| `candidate_bias_half_rolling_tenfold` | 62,120,805 / `1AFCA5AC7D5B0A7354991E23AA26C2C1669CEB3C19642FEBF125E55DCB888FA9` | 27,250,688 / `8A7A95401240B5114404E1A94B03751FF2A10F2ACD843E41752A54388BB03E35` | 5,029 / `44F73FD90F6008CD090A6C7617680C94EE16C3B8E860BC323267E1328F3FBC4F` |
| `terrain_flat` | 9,939,782 / `02CBE62A1EA41B8F380E7B7CFBE6A75681209A3DE2A3A445392722877E705BA1` | 4,403,200 / `447C51B0D1209822B070505FD7D5291FE75E087663E3230446422E5751230CEE` | 5,012 / `54CEE13680CE607430B835061AC1E4144039765189F02CE8C37A61C908D6BAC2` |

Exact bounded `physics_query.bat` calls read by the main model:

| Trace | Exact arguments after the trace path | GPT-read characters |
|---|---|---:|
| `control` | `contacts --frame 838 --body 0 --limit 8` | 870, run twice |
| `control` | `contacts --frame 839 --body 0 --limit 8` | 430 |
| `control` | `contacts --frame 897 --body 0 --limit 8` | 874 |
| `control` | `frame 838` | 5,415 |
| `control` | `frame 839` | 5,403 |
| `control` | `frame 897` | 4,955 |
| `control` | `contacts --frame 896 --body 4 --limit 8` | 866 |
| `control` | `solver --frames 838:839 --limit 8` | 1,307 |
| `control` | `solver --frames 897:897 --limit 8` | 1,070 |
| `control` | `contacts --frame 7533 --body 0 --limit 8` | 871 |
| `control` | `contacts --body 0 --top slip --limit 3` | 1,830 |
| `control` | `contacts --body 0 --type sphere/terrain --top slip --limit 3` | 1,861 |
| `rolling_resistance_zero` | `contacts --body 0 --type sphere/terrain --top slip --limit 3` | 1,872 |
| `rolling_resistance_tenfold` | `contacts --body 0 --type sphere/terrain --top slip --limit 3` | 1,901 |
| `control` | `sql "pragma table_info(runs)" --limit 50` | 1,774 |
| `control` | `sql "select config_json from runs limit 1" --limit 1` | 977 |

The following exact projection command ran once for each listed trace and
proved the stored `config_json` values rather than trusting the temporary
`engine.cfg` edits:

```powershell
$root='TestOutput\validation\candidates\REST_STABILITY_RS2_CAUSES'
$names=@('control','restitution_suppressed','normal_bias_zero','normal_bias_cap_half','terrain_friction_zero','rolling_resistance_zero','rolling_resistance_tenfold','sleep_frames_one','terrain_contact_band_zero','candidate_bias_half_rolling_tenfold','terrain_flat')
$sql="select json_extract(config_json,'$.contact_restitution_threshold') as restitution_threshold,json_extract(config_json,'$.terrain_contact_baumgarte_beta') as terrain_beta,json_extract(config_json,'$.terrain_max_baumgarte_bias') as max_bias,json_extract(config_json,'$.friction_coeff') as terrain_friction,json_extract(config_json,'$.rolling_friction_coeff') as rolling_friction,json_extract(config_json,'$.physics_sleep_frames') as sleep_frames,json_extract(config_json,'$.terrain_contact_threshold') as contact_threshold from runs"
foreach($n in $names){ tools\physics_query.bat "$root\$n.physicsdiag.ndjson" sql $sql --limit 2 }
```

Per-trace projection output was: control 586,
`restitution_suppressed` 622, `normal_bias_zero` 604,
`normal_bias_cap_half` 612, `terrain_friction_zero` 614,
`rolling_resistance_zero` 617, `rolling_resistance_tenfold` 623,
`sleep_frames_one` 603, `terrain_contact_band_zero` 621,
`candidate_bias_half_rolling_tenfold` 641, and `terrain_flat` 596
characters, or 6,739 total.

Exact PowerShell semantic-reduction commands read by the model (line wrapping
below is only for Markdown readability):

```powershell
$r=Get-Content -Raw TestOutput\validation\candidates\REST_STABILITY_RS2_CAUSES\control.semantic.json | ConvertFrom-Json
$r | ConvertTo-Json -Depth 2
```

The command above produced 8,388 characters. The 9,849-character comparison
used this exact projection over the first seven one-variable reports:

```powershell
$root='TestOutput\validation\candidates\REST_STABILITY_RS2_CAUSES'; $names=@('control','restitution_suppressed','normal_bias_zero','terrain_friction_zero','rolling_resistance_zero','sleep_frames_one','terrain_contact_band_zero'); $lines=New-Object System.Collections.Generic.List[string]; foreach($name in $names){ $r=Get-Content -Raw "$root\$name.semantic.json"|ConvertFrom-Json; $lines.Add("VARIANT $name end=$($r.physics_end_frame) gate=$($r.aggregate.authored_sleep_gate_frame) failures=$($r.aggregate.failures -join ',')"); foreach($b in $r.balls){ $ri=if($null -eq $b.resting_reimpact){'-'}else{"$($b.resting_reimpact.resting_frame)>$($b.resting_reimpact.reimpact_frame)"}; $lines.Add(("{0} sleep={1} lastImpact={2} vy={3:F6} slip={4:F6} distR={5:F6} rev={6}/{7} reset={8} wake={9} reimpact={10} fail={11}" -f $b.name,$b.final_sleep_frame,$b.last_material_impact_frame,$b.maximum_post_impact_abs_vy,$b.maximum_post_impact_slip_speed,$b.post_impact_slip_radius_fraction,$b.late_x_reversals,$b.late_z_reversals,$b.sleep_counter_resets_after_tail,$b.wakes_after_tail,$ri,($b.failures -join ','))); } foreach($b in $r.box_controls){ $lines.Add(("{0} sleep={1} speed={2:F6}/{3:F6} omega={4:F6}/{5:F6} pass={6}" -f $b.name,$b.final_sleep_frame,$b.maximum_speed,$b.maximum_allowed_speed,$b.maximum_omega,$b.maximum_allowed_omega,$b.passed)); }} $text=($lines -join "`n")+"`n"; $text
```

The 806-character cross-variant table reduction used this exact command:

```powershell
$root='TestOutput\validation\candidates\REST_STABILITY_RS2_CAUSES';$names=@('control','restitution_suppressed','normal_bias_zero','normal_bias_cap_half','terrain_friction_zero','rolling_resistance_zero','rolling_resistance_tenfold','sleep_frames_one','terrain_contact_band_zero','candidate_bias_half_rolling_tenfold');foreach($n in $names){$r=Get-Content -Raw "$root\$n.semantic.json"|ConvertFrom-Json;$ri=@($r.balls|Where-Object{$null-ne$_.resting_reimpact}).Count;$rev=($r.balls|Measure-Object late_x_reversals -Sum).Sum+($r.balls|Measure-Object late_z_reversals -Sum).Sum;$vy=($r.balls|Measure-Object maximum_post_impact_abs_vy -Maximum).Maximum;$slip=($r.balls|Measure-Object maximum_post_impact_slip_speed -Maximum).Maximum;$box=@($r.box_controls|Where-Object{$_.passed}).Count;"$n reimpact=$ri rev=$rev maxvy=$vy maxslip=$slip box=$box/3"}
```

The `terrain_flat` projection, including all three ball failure, sleep, and
last-impact lines, produced 258 trace-derived characters:

```powershell
$r=Get-Content -Raw TestOutput\validation\candidates\REST_STABILITY_RS2_CAUSES\terrain_flat.semantic.json|ConvertFrom-Json;$ri=@($r.balls|Where-Object{$null-ne$_.resting_reimpact}).Count;$rev=($r.balls|Measure-Object late_x_reversals -Sum).Sum+($r.balls|Measure-Object late_z_reversals -Sum).Sum;$vy=($r.balls|Measure-Object maximum_post_impact_abs_vy -Maximum).Maximum;$slip=($r.balls|Measure-Object maximum_post_impact_slip_speed -Maximum).Maximum;$box=@($r.box_controls|Where-Object{$_.passed}).Count;"terrain_flat end=$($r.physics_end_frame) gate=$($r.aggregate.authored_sleep_gate_frame) reimpact=$ri rev=$rev maxvy=$vy maxslip=$slip box=$box/3";foreach($b in $r.balls){"$($b.name) failures=$($b.failures -join ',') sleep=$($b.final_sleep_frame) lastImpact=$($b.last_material_impact_frame)"}
```

These commands read only the compact semantic JSON reports produced by
`python tools\analyze_at_rest_stability.py <trace> --output <report>`; analyzer
stdout was redirected to `Out-Null` during report generation.

Main-model GPT-read SkullScope-derived output was **59,186 characters** with
**no truncation**. The reviewer separately read 19,148 untruncated characters:
pass 1 ran `control ... summary` (2,941) and `normal_bias_cap_half ... summary`
(2,963); pass 2 repeated the exact frame-7,533 contact query, the three exact
terrain-slip queries, and the eleven-query `$sql` configuration loop already
listed above (13,244). The raw NDJSON and SQLite artifacts were never read by
any model.

Independent review used thread
`01a0197e-c7e7-7c22-a220-d59357484f0b`, session
`C:\Users\sesch\.codex\sessions\2026\08\19\rollout-2026-08-19T20-08-54-01a0197e-c7e7-7c22-a220-d59357484f0b.jsonl`.
Pass 1 reported three blocking findings, two non-blocking findings, and three
missing-evidence items. The fix cycle replaced the sphere-irrelevant contact
band claim with the flat-terrain A/B, moved RS4 into the single PGS owner,
added friction-headroom/stage-order evidence, narrowed the restitution and
sleep conclusions, and completed exact command/config accounting. Fresh pass
2 reported zero findings, zero missing evidence, and a CLEAN verdict.

## RS3 Stable-Support Normal Repair — 2026-08-20

`PersistentContactSolveTransaction::PrecomputeRows` now gives a stable terrain
row below the configured material-impact threshold a zero velocity bias. The
normal constraint may cancel closing velocity, but it cannot command rebound;
`CorrectPositions` remains the sole non-energetic overlap-decay owner. Unstable
terrain overlap retains the existing bounded Baumgarte path, and contacts at or
above the material-impact threshold retain restitution. No material, friction,
rolling, sleep, terrain, or scene value changed.

Three direct solver cases pin those boundaries. The historical frame-838
witness uses closing speed 1.945360 m/s, penetration 0.337921, and the authored
2.0 m/s impact threshold; it now retains a positive normal impulse and upward
position correction while both `bias` and `separationBias` are zero and final
vertical velocity is approximately zero. A non-resting terrain row retains a
positive bias bounded by `terrain.maxBaumgarteBias`. The existing 6.0 m/s
terrain-impact case still produces restitution with zero separation bias.
`tools\validate_tests.bat` passes 629/629 cases and 2,521,088/2,521,088
assertions after rebuilding Profile.

The authored diagnostic run used the unchanged production config and this exact
command:

```bat
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --time-scale 100 --automation-hidden-window --scene TestOutput\validation\candidates\REST_STABILITY_RS2_CAUSES\at_rest_rs2.scene.json --physics-diag TestOutput\validation\candidates\REST_STABILITY_RS3_NORMAL\rs3_normal.physicsdiag.ndjson
```

The Debug executable SHA-256 was
`1C515BAD115A0F3C7643D45FB6886213FEC435B2E3D57F4E99BCEA33564AA40C`.
The trace is 143,113,113 bytes with SHA-256
`56273E21F13D0FFCD2EB21D02CA827FD2E5DEEC53FFD36A01E196C7AB50FC064`;
its 66,510,848-byte SQLite cache hashes to
`17C8FFCAFFB20D04E36803D7380430472E56A9442BF9B4937561A9FD902139BB`;
the 5,458-byte semantic report hashes to
`A15A79876D31DE195E1D5E8EAD516734C3D77D0E90CF6BB5FBA648E53D91A0C0`.
The ignored preservation root is
`TestOutput/validation/candidates/REST_STABILITY_RS3_NORMAL/`.

The old subthreshold max-bias loop is gone for every ball: the analyzer reports
zero resting reimpacts, and the SQL population of supported rows below 2.0 m/s
with positive separation bias falls from 702 to 0. Initial material-impact rows
are byte-identical before/after: ball A frame 304 has closing 24.018234,
penetration 0.293762, normal impulse 1008.765503; ball B frame 423 has
32.455158/0.445709/1051.546265; ball C frame 330 has
25.336464/0.573029/1185.746948. All six before/after separation biases are zero.

| Family/run | Contact rows | Material rows | Max penetration | Max normal impulse | Max separation bias | Supported rows | Quiet supported bias rows |
|---|---:|---:|---:|---:|---:|---:|---:|
| Boxes/control | 1,132 | 54 | 0.000031 | 636.369324 | 0 | 632 | 0 |
| Boxes/RS3 | 1,083 | 51 | 0.019897 | 636.369324 | 0 | 696 | 0 |
| Spheres/control | 13,589 | 934 | 0.587952 | 1185.746948 | 2.0 | 11,533 | 702 |
| Spheres/RS3 | 13,525 | 223 | 0.585693 | 1185.746948 | 0.255183 | 12,417 | 0 |

Position correction remains bounded and comparable: maximum single correction
is 8.361984 versus 8.394505 before RS3; maximum per-frame total is 12.256905
versus 10.888824, with at most 3 versus 2 corrected rows. The solver remains at
its configured 12-iteration ceiling. All box controls pass their RS0 envelopes:
box A 21.091541/3.419009 and sleep frame 1,289; box B
26.650600/5.031497 and frame 970; box C 31.146898/1.940049 and frame 5,087.

| Ball | Last material impact | Post-impact max `abs(vy)` | Post-impact max total energy | Final sleep | Remaining failures owned later |
|---|---:|---:|---:|---:|---|
| A | 13,222 | 1.070464 | 628.729560 | 17,610 | slip/reversals, latency, counter reset/wake |
| B | 14,817 | 2.415141 | 1377.563942 | 17,610 | slip/reversals, latency, counter reset |
| C | 13,908 | 2.115711 | 2263.262958 | 17,610 | slip/reversals, latency |

These finite envelopes are not a final-product pass and are not described as
monotonic decay. They contain no renewed resting impact, but late tangential
rolling still moves bodies across the heightfield and creates later free-flight
vertical motion. RS4 owns that coupled rolling/slip defect; RS5 owns the quiet
counter and sleep transition. The capped diagnostic scene therefore still
reports `scene_not_unlimited` and `ball_semantic_failure`; neither is used to
claim RS3 closure or to alter the canonical unlimited authored gate.

The lifecycle smoke's visible state and two-run hash remain deterministic, but
the intended normal impulse changed its exact state witness from
`953D97A226665242` to `6B91536A6023A15F`. The source constant was updated to
that twice-observed current value; the owner-controlled Physics CSV baseline
was not refreshed.

### RS3 SkullScope Query Ledger

Raw NDJSON and SQLite bytes were never model-read. The model saw 21,706
untruncated trace-derived characters: semantic report projection 1,019;
`summary` 2,952; `pragma table_info(contacts)` 2,219; before/after first-impact
queries 929/935; SQLite table list 935; `pragma table_info(bodies)` 3,219;
before/after post-impact envelope queries 830/836; ranked RS3 vertical peaks
1,335; before/after frame-7,200 energy envelopes 830/850;
`pragma table_info(solver_stats)` 1,335;
`pragma table_info(pipeline_stages)` 692; and before/after family plus
position-correction reductions 847/540/857/546.

Every SQL call used `tools\physics_query.bat <trace> sql <query> --limit 20`
except the three schema/list calls, which used `--limit 100`. The exact semantic
report projection and four substantive SQL queries were:

```powershell
$report=Get-Content -Raw TestOutput\validation\candidates\REST_STABILITY_RS3_NORMAL\rs3_normal.semantic.json|ConvertFrom-Json
$lines=New-Object System.Collections.Generic.List[string]
$lines.Add("end=$($report.physics_end_frame) gate=$($report.aggregate.authored_sleep_gate_frame) failures=$($report.aggregate.failures -join ',')")
foreach($b in $report.balls){$lines.Add("ball=$($b.name) sleep=$($b.final_sleep_frame) lastImpact=$($b.last_material_impact_frame) reimpact=$($null -ne $b.resting_reimpact) maxVy=$($b.maximum_post_impact_abs_vy) maxSlip=$($b.maximum_post_impact_slip_speed) distR=$($b.post_impact_slip_radius_fraction) rev=$($b.late_x_reversals)/$($b.late_z_reversals) failures=$($b.failures -join ',')")}
foreach($b in $report.box_controls){$lines.Add("box=$($b.name) pass=$($b.passed) sleep=$($b.final_sleep_frame) speed=$($b.maximum_speed)/$($b.maximum_allowed_speed) omega=$($b.maximum_omega)/$($b.maximum_allowed_omega)")}
$text=($lines -join "`n")+"`n";$text

select c.body_a,c.body_b,c.frame,c.contact_id,c.pre_solve_closing_speed,c.penetration,c.separation_bias,c.normal_impulse from contacts c join (select body_a,min(frame) as first_frame from contacts where body_a in (0,1,2) and pre_solve_closing_speed >= 2.0 group by body_a) f on c.body_a=f.body_a and c.frame=f.first_frame where c.pre_solve_closing_speed >= 2.0 order by c.body_a,c.contact_id

with ids(body_id) as (values(0),(1),(2)), last_impacts as (select ids.body_id,max(c.frame) as last_frame from ids join contacts c on c.body_a=ids.body_id or c.body_b=ids.body_id where c.pre_solve_closing_speed>=2.0 group by ids.body_id) select b.body_id,b.name,l.last_frame,max(abs(b.vel_y)) as max_abs_vy,max(b.linear_energy+b.angular_energy) as max_total_energy,max(b.speed) as max_speed,max(b.omega_mag) as max_omega from bodies b join last_impacts l on b.body_id=l.body_id and b.frame>=l.last_frame group by b.body_id,b.name,l.last_frame order by b.body_id

select case when body_a between 0 and 2 then 'sphere' else 'box' end as family,count(*) as contact_rows,max(penetration) as max_penetration,max(normal_impulse) as max_normal_impulse,max(separation_bias) as max_separation_bias,sum(case when supports_sleep=1 then 1 else 0 end) as supported_rows,sum(case when supports_sleep=1 and pre_solve_closing_speed<2.0 and separation_bias>0.0 then 1 else 0 end) as quiet_supported_bias_rows,sum(case when pre_solve_closing_speed>=2.0 then 1 else 0 end) as material_rows from contacts group by family order by family

select max(position_correction_rows) as max_rows_per_frame,max(position_correction_total) as max_total_per_frame,max(position_correction_max) as max_single_correction,max(row_count) as max_solver_rows,max(solver_iterations) as max_iterations from solver_stats
```

### RS3 Independent Review And Validation

Fresh read-only reviewer thread
`01a01af3-dc44-7da1-a027-9e6c63ad272f` used session
`C:\Users\sesch\.codex\sessions\2026\08\20\rollout-2026-08-20T02-56-24-01a01af3-dc44-7da1-a027-9e6c63ad272f.jsonl`.
Its final verdict was CLEAN with zero blocking findings, zero non-blocking
findings, and zero review-phase missing-evidence items. The reviewer confirmed
all seven current inventories, the cohesive `PrecomputeRows` complexity ruling,
no downward Replay include, no new or expanded growth privilege, and a touched
source comment audit of 3 checked / 0 deferred. Its own SkullScope read was
2,952 characters and zero raw NDJSON/SQLite bytes. At final telemetry capture
the reviewer reported 633,272,744 input, 618,676,352 cached input, 1,289,391
output, 340,889 reasoning output, and 1,829 seconds elapsed.

The cumulative pre-commit gates were run without refreshing protected oracles:

- `tools\validate_tests.bat` passed 629/629 cases and 2,521,088 assertions.
- `tools\validate_physics.bat` passed the lifecycle smoke, then reached the
  owner-controlled varied-scene comparison and reported 20,394 changed lines.
- `tools\validate_physics_deep.bat` retained byte-exact bullet wall/object/
  terrain and three-body controls, then reported the same 20,394 varied-scene
  lines plus 256 shooting-volley lines against unrefreshed CSVs.
- `tools\validate_replay_visual_fidelity.bat` passed its 17 typed/false-pass
  controls, then stopped at the inherited `header.topologyVersion` mismatch.
- `tools\validate_perf.bat` rebuilt Profile, then stopped before runtime timing
  at the pre-existing RS0 `SceneSleepingDynamicBodyGatePolicy.h` allocation
  policy row. RS3 adds no storage, allocation, capacity, or growth privilege.

The five ignored logs and their size/SHA-256 identities are:

| Gate log | Bytes | SHA-256 |
|---|---:|---|
| `rest_stability_rs3_validate_tests.log` | 5,180,088 | `D060AE306CE5904604B4AEC41AB87DD9D9570BCC41F027410E94B77A81FE6B4C` |
| `rest_stability_rs3_validate_physics.log` | 516,176 | `84A8E76B8C537D94E3406E31E036F71E263035BCC8202FFE971A79669FD4ABC1` |
| `rest_stability_rs3_validate_physics_deep.log` | 1,153,490 | `82773963CA200670C8A2C65933BC4C6762B5BF5E3DA1BF4E8E32AA53C4AE4973` |
| `rest_stability_rs3_validate_replay_visual_fidelity.log` | 46,826 | `673457665636EE70573E96AA75DF2CC18FF5749AF437A07F672896220649C805` |
| `rest_stability_rs3_validate_perf.log` | 46,446 | `C554CC51121BE0D5BF409F377A1E2A95EC0684BC367D0B66C57A825171E7213A` |

The final validation executables are Debug CORE
`CD57F46A8E41155B3B61130C0C6A5E31F2B1538BCA98DA174206320EA53C49C4`,
Profile CORE
`629D81EE9D27F1AC9C88A798A0F29CDDACD5DF8CF2D7D230288B52F584104F16`,
Automation CORE
`7655A51EB1BCB27266CC93EF0F72B48DD799CAD0DAAB4C7776E06B159D071282`,
and Profile TESTS
`3947D0F35B7A0F825DF299E545DED5C32CA79D158755522B9F1DFB6E242708C6`.
The expected owner-controlled stops remain open for the final combined plan
decision; none was refreshed or described as a pass in RS3.

### RS4 Spin-Policy Activation Owner Ruling

RS4 deliberately activates, but does not retune, the Core-owned
`PhysicsMaterialConfig::spinFrictionCoeff` policy. Core has long authored the
value as `0.3`, but `PhysicsEngine::RuntimeSettingsFromConfig` previously
omitted it from the Physics-owned cold settings snapshot, so the configured
normal-axis contact-patch policy was dead at the subsystem boundary. The RS4
projection repairs that missing field-faithfulness seam; the authored value,
config range, scene inputs, and every other friction coefficient remain
unchanged.

This activation is required by isolated evidence, not by the final sleep gate.
The rolling-only V1 candidate naturally failed to finish because `ball_a`
reached zero linear speed while retaining terrain-normal angular speed
`0.924937`. The final RS4 solver keeps spin separate from tangent-plane rolling,
clamps its angular impulse to contact-patch length times supporting normal
impulse, excludes material impacts, and recomputes contact velocity before the
existing sliding-friction row. Focused tests prove the exact spin bound,
separate-axis ownership, zero-spin-coefficient behavior, rolling's inability to
damp normal spin, material-impact exclusion, no-slip coupling, and slope motion.

Accordingly the current candidate is evidence for the coupled rolling-plus-spin
repair, never a rolling-only result. Post-last-impact terrain slip and rolling
residual are `0.000001` for balls A/B/C over 534/397/773 terrain rows. The
remaining `2.862317` B/C slip belongs solely to 37/33 sphere-sphere rows and is
left open for RS6 integrated acceptance; B's quiet-counter reset remains RS5
work. The trace does not serialize angular-row limits, so exact saturation
headroom is proved by the source clamps and focused bound tests, not falsely
claimed as a SkullScope measurement. After the global last material impact,
aggregate mechanical energy including gravitational potential decreases every
frame from `48709.165579` to `48066.857912`, with zero positive deltas.

### RS4 Exact-Provenance Evidence And Validation

The final post-format Debug executable was rebuilt from the reviewed source and
then ran the unchanged canonical unlimited scene with this exact command:

```bat
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --time-scale 100 --automation-hidden-window --scene SkullbonezData\scenes\at_rest.scene.json --physics-diag TestOutput\validation\candidates\REST_STABILITY_RS4_COUPLED_V4\at_rest_rs4.physicsdiag.ndjson
```

The executable hashes to
`F94743E7A6001D96581D470BE2CBC1F29A289A3619AAC60B9768CB1A521EA11E`.
The ignored V4 preservation root is
`TestOutput/validation/candidates/REST_STABILITY_RS4_COUPLED_V4/`:

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `at_rest_rs4.physicsdiag.ndjson` | 108,763,821 | `D97CA8D8AE042BDA64A662B729870056BF53C31626C1A6132D4C658CD88F596C` |
| `at_rest_rs4.physicsdiag.sqlite` | 50,102,272 | `495AFE4326BE982D6E70148B609A7812FE761B4DABBD47939009434DAB387D1C` |
| `at_rest_rs4.semantic.json` | 5,028 | `A4BE5EC0BB8AD537EA82B75D3F975ED02D96ADE762DA49287A0F69017BC926BA` |

The process ended naturally with `target_frames=-1`, `end_status=process_end`,
physics frame 13,544, authored all-asleep gate frame 13,541, aligned contiguous
body timelines, and all three balls finally Physics-asleep. Box A/B/C remain
passing controls at sleep frames 1,330/970/1,225 and retain their RS0 speed and
angular-speed ceilings. Post-last-impact terrain slip/rolling residual remains
`0.000001` for all balls; post-last-impact X/Z reversals are A 0/1, B 1/0,
C 0/1. The aggregate semantic oracle honestly remains red because fixed-frame
vertical/reversal checks and B/C object-contact slip are integrated RS6 work,
while B's one counter reset is RS5 work. RS4 does not use natural sleep or that
known remaining failure as a false phase pass.

The final focused and cumulative validation results are:

- `tools\validate_tests.bat` passed 634/634 cases and
  2,521,711/2,521,711 assertions in 40.025 seconds.
- `tools\validate_physics.bat` ran for 20.375 seconds, then reached the
  protected varied-scene CSV with 35,303 changed lines. No baseline changed.
- `tools\validate_physics_deep.bat` ran for 68.872 seconds, retained byte-exact
  bullet wall/object/terrain and three-body controls, then reported the same
  35,303 varied-scene lines plus 256 shooting-volley lines.
- `tools\validate_replay_visual_fidelity.bat` ran for 317.317 seconds, passed
  all 17 typed/false-pass controls and 75 assertions, then the authoritative
  process stopped at the inherited `header.topologyVersion` mismatch.
- `tools\validate_perf.bat` rebuilt Profile successfully in 32.789 seconds,
  then stopped before timing at the pre-existing RS0
  `SceneSleepingDynamicBodyGatePolicy.h:84` allocation-policy row.

`PersistentContact` grows by 32 bytes in already reserved solver-row storage:
3,072,000 bytes at the default 96,000 rows and 6,291,456 bytes at the 196,608
hard maximum. RS4 adds no allocation call, reserve call, per-body hot-store
field, post-start growth path, or Runtime reserve privilege. All seven final
inventories are current: wide signatures strict-pass; 89/89 gated aggregates;
1/1 ruled extraction scar; 41/41 complexity rulings; zero build-config
diagnostics across 154 ruled divergences; 91/91 reachability rulings; and zero
glossary duplicates, drift, or ruling issues. The generated dependency proof
and repository scan pass with zero findings. Touched-source comment audit:
8 checked, 0 deferred.

The five ignored validation logs are:

| Gate log | Bytes | SHA-256 |
|---|---:|---|
| `rest_stability_rs4_validate_tests.log` | 2,610,177 | `3C27B7D394A9110DF28083AD365711C79081D300F049EB17EBA6EDB37DC70572` |
| `rest_stability_rs4_validate_physics.log` | 258,088 | `DF6DE68E1E110046098509347741239A4A938F8C76B899237EFA6FA9A3D19ED0` |
| `rest_stability_rs4_validate_physics_deep.log` | 576,747 | `933263406134D1CE05B7C52B0F5CC0D431D8305322102E226AD01FA7C762D63E` |
| `rest_stability_rs4_validate_replay_visual_fidelity.log` | 40,436 | `402EEF47EEF2BDABE983E2FFCC243A2113438D2406C4C2DF7EFF6E1F997DB9AA` |
| `rest_stability_rs4_validate_perf.log` | 38,535 | `48A09B2DEE5BE65DB997238A43107AA32A3A71733641F37B13735C983D1BEDAD` |

Final Profile Core, Automation Core, and Profile Tests executable hashes are
`46411FEAA0F3CA23E46913BCA89536EB7B0BAA27E87C63F4E4A5005A2E316B6C`,
`009D82FBA6A329A94518FA471BF72FE5D8B1A14652DED00E938D094237CB7C60`,
and `B03132FBF263F2E3E78AA443BFE3F701756DC169D5DE42904A2C0AD48530CF64`.
No protected oracle was refreshed or described as a pass.

### RS4 Review And SkullScope Accounting

Independent reviewer thread `01a01624-d863-7891-af81-e1e203dd9f27`, session
`C:\Users\sesch\.codex\sessions\2026\08\19\rollout-2026-08-19T04-31-48-01a01624-d863-7891-af81-e1e203dd9f27.jsonl`, found one blocking
documentation omission and two nonblocking attribution/evidence notes in pass
1. The four-thread limit prevented a new session, so the orchestrator reused
this completed, unrelated OF0 worker with explicit zero-baseline RS4
instructions; it had not participated in RS4 implementation. After the
spin-policy owner ruling, pass 2 was CLEAN with zero blocking and zero
nonblocking findings: two passes, one fix cycle. RS4 reviewer delta across
both passes was 11,194,826 input, 10,763,008 cached input, 431,818 uncached
input, 17,243 output, and 7,474 reasoning-output tokens over 2,249.771 seconds.
The reviewer read exactly zero SkullScope characters and zero raw NDJSON/SQLite
bytes.

The main model never read raw NDJSON or SQLite bytes. Final V4 trace-derived
model input was exactly 7,902 characters: semantic report 5,028; terrain rows
657; family attribution 667; mechanical energy 641; empty energy/friction event
set 410; and post-impact reversal counts 499. Across RS4 investigation, the
exactly reconstructed untruncated SkullScope/query output total is 83,036
characters over 38 calls. One early unbounded summary was truncated by the tool
before an exact character count was captured, and one malformed query produced
truncated error text; both are explicitly excluded from 83,036 rather than
inventing precision. Every later call was bounded and counted. The final V4
commands were `tools\physics_query.bat <trace> sql <bounded-query> --limit 10`
for the four SQL witnesses, `events --type energy_spike,friction_saturation
--frames 7200:13544 --limit 20`, plus the semantic analyzer above.

### RS5 Counter-Reset Adjudication And Selected Repair

RS5 found no remaining Physics sleep-transition defect after RS4. The sole V4
`sleep_counter_reset` witness is ball B at frame 10,630: its counter moves from
1 to 0 while support moves from 1 to 0 during a live B/C sphere/sphere contact.
At that frame linear speed is `0.476590`, angular speed is `0.117631`, and the
contact carries closing speed `0.007066` plus pre-solve slip `1.425168`; the
body is therefore neither supported nor angularly quiet. Resetting the counter
is the required `PhysicsSleepController` response, not delayed-sleep policy.
Ball B later has its final material impact at frame 11,931.

The actual RS5 product invariant is the checklist wording below: after each
body's final material impact, a supported quiet body must accumulate consecutive
frames without resetting before its terminal sleep suffix. The RS1 analyzer had
instead counted every reset after the fixed frame-7,200 audit boundary, so it
falsely treated a valid abandoned attempt before a later impact as a final quiet-
run failure. RS5 narrows only that semantic window. It does not change Physics
linear/angular thresholds, quiet-frame count, support propagation, inhibition,
counter mutation, sleep transition, or wake behavior.

The repaired analyzer reports zero post-final-impact resets and zero tail wakes
for A/B/C. Their final-impact-to-terminal-sleep latencies are 984/566/907 frames,
all inside the ratified 1,200-frame quality target; all three terminal sleep
suffixes remain aligned to the natural unlimited process end. The checker now
includes controls that a pre-impact reset is valid, an ordinary post-impact
reset fails, and a reset on the first frame after the final impact also fails
exactly as `sleep_counter_reset`. Semantic report schema version 2 owns the
renamed post-final-impact fields. Focused Physics tests pin whole-island
supported transitions, positive-counter reset for both unsupported and nonquiet
states, and idempotent narrowphase material-impact wake publication; no
production Physics change is justified merely to make an over-broad diagnostic
green.

RS5 independent review used fresh thread
`01a01bc3-a1dc-7662-b146-0d33ee072abb` and session
`C:\Users\sesch\.codex\sessions\2026\08\20\rollout-2026-08-20T06-43-21-01a01bc3-a1dc-7662-b146-0d33ee072abb.jsonl`.
Pass 1 reported two blocking findings and one non-blocking finding: the boundary
row was not seeded, focused C++ owner tests were missing, and renamed semantic
fields still claimed schema version 1. One fix cycle added the exact
`finalImpact + 1` negative, schema version 2, positive-counter unsupported and
nonquiet resets, and idempotent narrowphase wake publication. Pass 2 was CLEAN
with zero findings. The pass-1 snapshot recorded 6,428,516 input tokens
(6,237,184 cached, 191,332 uncached), 12,708 output tokens, and 1,273 seconds;
pass 2 added 420,728 input (417,280 cached, 3,448 uncached), 1,655 output, and
284 seconds. The live work ledger owns the final attached-session deltas.

Mapped RS5 validation is current on the final formatted tree:

- `python tools\check_at_rest_stability_analyzer.py` passed 21/21 planted
  controls in 0.137 seconds.
- `tools\validate_tests.bat` rebuilt the changed test and passed 636/636 cases
  with 2,521,396/2,521,396 assertions in about 30.2 seconds.
- The first `tools\validate_fast.bat` attempt exposed formatter-owned layout in
  three earlier RS4 Physics files. `tools\format_fix.bat` changed layout only;
  the final `validate_fast` passed formatting, dependency proof, all seven
  inventories, Profile/Automation/Debug builds, tests, and compiled reachability
  in 552.007 seconds. No function-complexity ruling became stale: 41/41 current
  triggered bodies remained ruled.
- The schema-v2 semantic rerun against the preserved V4 trace completed in
  1.836 seconds. Its 5,078-byte JSON has SHA-256
  `93D637E4D52292C25D597B8D15F9294DE9A1463501957B101887D96F7F0589D1`,
  reports resets `0/0/0`, wakes `0/0/0`, and latencies `984/566/907`, and
  intentionally remains aggregate-red only for RS6 motion-quality work.
- `tools\validate_physics_deep.bat` ran for 76.566 seconds and reached the
  owner-controlled baseline comparison. Bullet wall/object/terrain sweeps and
  space three-body remained byte-exact; the known pre-RS5 RS3/RS4 behavior
  differences remain 35,303 varied-scene lines and 256 shooting lines. No
  Physics or SkullScope oracle was refreshed.

The touched-source comment audit is 7/7 checked with 0 deferred: the two Python
tools, the focused C++ test, and four formatter-only RS4 Physics files. Learning
headers, local ownership/validation claims, structured comments, and durable
`Related:` paths remain truthful; the mechanical formatting changed no policy.

RS5 SkullScope accounting keeps the 108,763,821-byte V4 NDJSON and
50,102,272-byte SQLite cache on disk with zero raw bytes read by either model.
The main agent exposed two bounded query results of 13,630 and 4,312 characters
plus the 5,078-character semantic JSON; the reviewer exposed six bounded query
results totaling 9,908 characters. Total RS5 model-read diagnostic text is
therefore exactly 32,928 characters/bytes, with no truncated result used for a
conclusion.

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

- [x] Add a bounded at-rest stability analyzer or focused test harness that
  reports the RS0 measures without treating the full CSV hash as success.
- [x] Pin all three balls individually plus aggregate scene completion. Keep
  box behavior as a non-regression control.
- [x] Add planted negative controls for vertical impulse oscillation, excessive
  slip, rolling reversal, and sleep-counter reset so each oracle demonstrably
  fails.
- [x] Keep query output bounded and preserve the existing generic SkullScope
  schema unless diagnosis proves a missing fact.

Evidence: focused tests/analyzer fixtures, four failing negative controls, and
the unchanged baseline failing the new semantic target for the expected reason.

### RS2 — Adjudicate The Root Causes

- [x] Use trace evidence and one-variable diagnostic A/Bs to classify terrain
  contact geometry/support, normal bias/restitution, tangent friction, rolling
  resistance, warm-start/cache continuity, and sleep policy separately.
- [x] For every proposed edit, name the invariant-owning function/stage and the
  exact symptom/metric it owns. Reject broad global tuning without causal proof.
- [x] Treat friction-coefficient and sleep-threshold changes as diagnostic
  probes only while proper solver/contact/support fixes remain viable. If the
  evidence isolates a policy deficiency, prepare the owner-decision packet and
  stop before changing production values.
- [x] Record why each non-owning candidate was ruled out and whether the three
  balls share one cause or need distinct repairs.
- [x] Update the plan with the selected repair behavior before implementation.

Evidence: bounded before/A/B query comparisons, selected owner rulings, rejected
alternatives, and independent read-only diagnosis review.

### RS3 — Remove Normal-Axis Vibration

- [x] Repair only the diagnosed normal/support/manifold/cache cause of vertical
  tail oscillation.
- [x] Preserve legitimate restitution for material impacts; resting contacts
  must not repeatedly manufacture bounce energy below the impact threshold.
- [x] Prove penetration, position correction, bias, normal impulse, energy, and
  support classification remain bounded for spheres and box controls.
- [x] Add focused terrain-sphere regressions that fail the pre-RS3 behavior.

Evidence: per-ball vertical envelopes and energy traces, focused solver tests,
and unchanged initial-impact behavior where the repair should be inactive.

### RS4 — Stop Excess Sliding And Rolling Reversals

- [x] Repair the diagnosed tangent/rolling response without injecting generic
  damping or forcing angular velocity to zero in the contact solver.
- [x] Keep static/dynamic friction and rolling resistance dimensionally and
  timestep consistent; preserve credible no-slip rolling before rest.
- [x] Preserve existing friction coefficients unless isolated A/B evidence
  proves they are insufficient and the owner explicitly approves the recorded
  production change.
- [x] Prove reduced tail slip, bounded friction saturation, monotonic late
  energy decay, and no repeated back-and-forth direction changes after the last
  material impact.
- [x] Cover slopes/moving contacts or other existing friction cases so the
  at-rest repair does not pin legitimately moving balls.

Evidence: slip/rolling residual tables, reversal counts, focused friction and
rolling tests, and cross-scene motion controls.

### RS5 — Make Supported Quiet Balls Sleep Reliably

- [x] Keep quiet thresholds, support propagation, inhibition, and final
  transition under `PhysicsSleepController` ownership.
- [x] Fix spurious motion and counter-reset causes before proposing any change
  to the existing linear/angular thresholds or quiet-frame count. If policy is
  still the isolated cause, block the edit on explicit owner approval.
- [x] Ensure all three supported balls accumulate consecutive quiet frames and
  sleep after their last material impact; report the measured latency against
  the RS0 quality target without using that target to stop the scene.
- [x] Prove unsupported, externally forced, materially impacted, or island-
  connected bodies do not sleep early and wake exactly once when required.
- [x] Add focused counter-reset and supported-transition tests that fail the old
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
