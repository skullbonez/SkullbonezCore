# Replay Prediction Fidelity Probe Closure

Date: 2026-07-13

Branch: `nightrunner-13th-july`

Plan result: 5 / 5 tasks complete

## Outcome

Replay prediction now carries the recorder-owned solver digest on every
published future frame and the permanent scrub lane compares that frozen
future against later retained live solver headers for every tick in a bounded
fixed-step horizon. The assertion rejects incomplete prediction, seed mismatch,
retention loss, event-cursor changes, non-fixed ticks, discontinuous prediction
indices, and hash divergence instead of allowing a vacuous pass.

The probe exposed one real seed defect: prediction restored a newly captured
live snapshot while comparing against retained tick T. Transient solver/debug
fields can change later in the same render frame, so prediction now restores
the exact retained T body payload and world snapshot when the retained
frame/hash identity matches. The current live snapshot remains the bounded
fallback when no exact retained seed is available.

The engine-level doctest uses a sleeping terrain-supported body plus a
fixed/dynamic contact pair. It advances the copied private engine once before
restore to poison hidden state, restores captured body and solver state, then
compares full body fields byte-exactly for 64 ticks and specifically requires
the intended awake-to-sleep transition.

## Negative Controls

- Snapshot omission: temporarily zeroing `sleepState[0]` made the doctest fail
  at tick 1, body 0, field `isSleeping`. The mutation was removed. Evidence:
  `TestOutput/validation/replay_fidelity_negative_snapshot.log`.
- Predicted-state corruption: temporarily adding 1.0 to body 0's predicted
  x velocity at k=1 made `predictionMatchesLiveHorizon` report first divergence
  at k=1, seed T=4, with both hashes. The mutation was removed. Evidence:
  `TestOutput/validation/replay_fidelity_negative_velocity.log`.

## Validation Evidence

- `tools\validate_fast.bat`: passed in 70.8s. Formatting, project filters,
  file-size checks, Profile/Debug builds, and the included unit lane passed;
  builds had zero warnings and zero errors. Log:
  `TestOutput/validation/replay_fidelity_validate_fast.log`.
- `tools\validate_tests.bat`: final post-review run passed in 8.3s with
  180/180 cases and 4,665/4,665 assertions. Log:
  `TestOutput/validation/replay_fidelity_validate_tests_post_review.log`.
- `tools\validate_replay_scrub.bat`: passed in 162.3s. Scrub and restore
  probes passed; prediction determinism matched fingerprint
  `0x53177F546404605F`; submitted geometry held for at least 120 frames; the
  new fidelity step matched all 120 predicted/live hashes. Log:
  `TestOutput/validation/replay_fidelity_validate_replay_scrub.log`.
- `tools\validate_physics.bat`: passed in 54.8s. Standalone/runtime-handle
  smoke passed and `physics_regression_varied.csv` matched 44,401 lines
  byte-exactly. Log:
  `TestOutput/validation/replay_fidelity_validate_physics.log`.

## Comment-Style Audit

Touched-file audit completed against the repository comment-style guide.

- Checked: 11 source-bearing files.
- Deferred: 0.
- Unchecked: none.
- Scope: `InteractionAutomationController.cpp/.h`, `ReplayRecorder.cpp/.h`,
  `ReplayRuntime.cpp/.h`, `RunReplayTools.cpp`, `ReplaySolverHash.h`,
  `TestReplayPredictionFidelity.cpp`, `check_replay_prediction_fidelity.py`,
  and `validate_project_filters.py`.

`validate_replay_scrub.bat` is a small orchestration helper and was inspected
for bounded failure propagation; it does not require a full learning header.

## Independent Review

The first read-only rubber-duck pass found no blocker and judged plan closure
justified. It raised two non-blocking test-strength notes: copy assignment could
mask an omitted snapshot field, and the final sleep assertion accepted any
body's transition. The test now advances the private copy before restore and
requires body 2's awake-to-sleep transition. The post-review unit gate passed,
and the narrow follow-up review confirmed both notes closed with no new issue.

| Plan | Duck run | Reviewer | Reason | Prompt chars | Response chars | Tokens | Elapsed | Verdict | Follow-up |
|---|---|---|---|---:|---:|---|---|---|---|
| replay-prediction-fidelity-probe | fidelity-duck-01 | `/root/fidelity_rubber_duck` | Initial closure review | 889* | 2,071 | n/a | n/a | No blockers; two test-strength notes | Both addressed |
| replay-prediction-fidelity-probe | fidelity-duck-02 | `/root/fidelity_rubber_duck` | Narrow post-fix review | 655 | 382 | n/a | n/a | Notes closed; closure justified | None |

`*` The explicit first-pass prompt was 889 characters; the collaboration tool
also supplied implicit forked conversation context but did not expose its
character or token count. Elapsed review timing was not exposed, so no value is
invented.

## Next Work

`replay-monolith-decomposition` is unblocked. Its M0-M8 checklist contains nine
tasks (the MASTER denominator previously said eight and is corrected in the
same closure commit). The fidelity step in `validate_replay_scrub.bat` is the
binding regression detector for the prediction-owner extraction at M6.
