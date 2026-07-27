# Scene-Sized Store Capacity SC6 — Prediction Parity

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan: `scene-sized-store-capacity` SC6

## Outcome

SC6 is complete. `PhysicsEngine::ReserveSceneCapacityLike` gives cold engine
clones one Physics-owned way to commit the source engine's body, per-shape, and
point-joint capacities before copy assignment. Replay prediction calls that
seam inside its existing `replay_prediction_working_set` allocation, owner, and
growth scopes, so the isolated engine no longer reconstructs capacity from a
body count or lets fixed-list copy assignment infer it from live row counts.

The source `PhysicsEngine` remains the sole capacity authority. No capacity
aggregate, callback, Runtime dependency, second reserve owner, or new policy
exception was introduced.

## Measured 200-Body Reduction

The authoritative `prediction_ragdoll_wall_200` visual-fidelity scenario emitted
the private-engine request through the registered replay reserve owner:

| Measurement | Bytes | MiB |
|---|---:|---:|
| Pre-campaign same-scenario request | 171,278,688 | 163.344 |
| Final same-scenario request | 30,467,508 | 29.056 |
| Reduction | 140,811,180 | 134.288 |

That is an 82.21% reduction in retained private-engine bytes. SC7's independent
review found two pre-existing external-force rows that also needed a
scene-capacity commit; the final measurement above supersedes SC6's provisional
30,465,820-byte request by 1,688 bytes. The earlier
measurement is preserved in
`TestOutput/validation/replay_visual_fidelity/full_reveal_probe_incremental_candidate.log`;
the accepted SC6 measurement is in
`TestOutput/validation/replay_visual_fidelity/full_reveal_probe_profile.log`.
Both are the 200-body full-reveal profile scenario and both report the exact
`RunReplayPredictionSimulationState::predictionEngine` byte request.

The registered hard cap remains exactly 256 MiB
(`REPLAY_PREDICTION_RESERVE_HARD_BYTES`). The SC6 request consumes 11.35% of
that cap and leaves the existing owner registration and failure policy intact.

## Parity Proof

The focused 200-body clone test commits 80 sphere rows, 120 box rows, zero hull
rows, and seven point-joint rows. After cold copy assignment, the source and
clone have identical:

- body and collider capacities;
- per-shape capacities;
- point-joint backing and logical capacity;
- PhysicsWorld retained bytes;
- Debug/broadphase retained bytes; and
- scene-sized store retained bytes.

The end-to-end visual gate then executes the same seam under the Replay reserve
phase and proves the private engine still produces the frame-exact future.

## Comment Audit

All four touched source-bearing files were inspected against the final
implementation and the repository comment-style guide. Every file has a
learning header. The new public method documents its caller contract and source
authority; the prediction seeding invariant now matches the exact live-capacity
copy path. Checked: 4. Deferred: 0. Unchecked: none. This was a touched-file
audit, so no subsystem checklist was required. No term needs human-approved
wording.

## Validation

- Focused Debug prediction-capacity parity test: PASS, 1 case / 10 assertions.
- `tools\validate_build.bat Debug`: PASS.
- `python tools\check_allocation_policy.py --repo .`: PASS; 462 files scanned
  and zero allowlist errors.
- `tools\validate_replay_visual_fidelity.bat`: PASS in exactly one engine
  process and one prediction generation; 2,401 ticks, 200 causal nodes, one
  presented cascade, frame-exact output, durable artifact proof, and every
  false-pass control accepted.
- `tools\validate_full.bat`: PASS; 411 cases / 2,408,163 assertions, coverage,
  interaction/UI boundaries, engine processes, DX12 validation, and byte-exact
  44,401-line physics regression accepted.
- `tools\validate_format.bat`: PASS; 570 source files and 318 headers.
- `codegraph sync`: PASS; changed source and project files indexed before
  closure.

No baseline, golden, schema, scene, or configuration was refreshed.
