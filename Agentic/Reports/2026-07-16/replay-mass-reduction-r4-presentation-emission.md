# Replay Mass Reduction R4 — Presentation Emission Disposition

Date: 2026-07-16
Owner: replay
Task: `replay-mass-reduction` R4

## Outcome

R4 dispositioned D4, D5, and D7 without merging the separately owned 3D
ribbon, screen-space UI, and packet-telemetry concerns.

| ID | Disposition | Evidence |
|---|---|---|
| D4 | **UNIFY** | Ordinary and baseline segment wrappers performed the same capacity check, quota decrement, and lane-specific drop accounting in the same order. `TryAccountReplayPathSegment` now owns those mechanics; the wrappers retain their distinct final tracer record APIs, lane, and emphasis behavior. |
| D5 | **KEEP remaining loops** | `DrawReplayTrajectoryRecordSegments` already owns the genuinely shared point-range/stride/distance/emission traversal. Root and past lanes compose it with different color/range values. The child-outgoing, small-scene, ragdoll-torso, and affected-body loops have different endpoint carry, reveal-edge, stride partition, identity lookup, causal-depth, motion-lifetime, and marker semantics. Folding those into another parameter surface would hide policy rather than remove identical mechanics. |
| D7 | **UNIFY** | Presentation, solver, and prediction render-pose loops had identical identity-resolution, quaternion-normalization, override, mark, and unmatched-hide order. `ReplayPresentationApplyBodyRenderPoses` now owns the common loop with compile-time overloads for the only differences: model-row hint and orientation storage. `ReplayPresentationHideUnmatchedRenderBodies` owns the identical second pass. |

The D7 helper uses compile-time overload resolution, not a callback/interface
chain. It reads the same body order, submits overrides in the same order, marks
only successful overrides, and then hides unmatched live bodies in ascending
model-row order. No allocation, owner boundary, packet schema, renderer API,
golden, baseline, or provenance value changed.

The source diff is 186 insertions and 206 deletions across the three audited
files, a net reduction of 20 lines. Current physical counts are 2,061 lines for
`ReplayPredictionDrawing.cpp`, 1,286 for `ReplayPresentation.cpp`, and 485 for
`ReplayPresentation.h`.

## Comment Quality Audit

Touched-file audit: 3/3 checked, 0 deferred, 0 unchecked.

- `ReplayPredictionDrawing.cpp`: existing learning header remains current; the
  shared quota leaf documents traversal-after-exhaustion and distinct record
  shapes.
- `ReplayPresentation.cpp`: existing learning header remains current; the
  template documents its compile-time adapter, order, identity, normalization,
  and no-callback hot-path contract.
- `ReplayPresentation.h`: existing learning header and fixed-capacity match-
  table invariant remain current; obsolete private forwarding methods were
  removed.

This is a touched-file audit, so no subsystem checklist plan is required.

## Validation

The available shell was headless, so commands were mirrored to logs rather than
opened in an additional visible console.

- Targeted `tools\validate_build.bat Profile`: passed with zero warnings and
  zero errors in 15.42 s. Transcript: 69,090 bytes, SHA-256
  `4CB2EC471CCFB2F36C74C69E4CF17ECB59CE3D3E1E197B5A3D48045AF12EBADE`.
- `tools\validate_tests.bat`: passed 202/202 cases and 12,595/12,595 assertions
  in 3.74 s. Transcript: 660,776 bytes, SHA-256
  `7388D92E9F7D99E9C1EECEE64D819A8C0C25BE7EA2F1AF45F419B75513F9274B`.
- The first renderer-gate attempt stopped at its formatting preflight before
  any engine launch. Clang-format was applied only to the two edited `.cpp`
  files. Final `tools\validate_dx12_renderer.bat`: passed in 56.85 s with zero
  InfoQueue errors and all three screenshots within committed tolerances.
  Transcript: 138,380 bytes, SHA-256
  `84703B9591E70573D90CEACD2C03FBB065F5A9F70A023B04CD0076171A979497`.
- `tools\run_graphics_stress.bat 1`: ran for 61.89 s, remained crash-free, and
  closed PID 8296 by the script's scoped timeout. Transcript: 2,364 bytes,
  SHA-256
  `D37A2F7E2E09388646AA05019B5522548725A9C94BE5E2C4113AB93CAA33A0A3`.
- R4's single `tools\validate_replay_visual_fidelity.bat` invocation passed in
  474.85 s: one engine process, one prediction generation, 2,401 ticks, 200
  moved, 187 toppled, 199 causal nodes, and every negative/determinism control.
  Transcript: 77,340 bytes, SHA-256
  `599D592FDD2AB73E6921566282F6A06482E798EB91ED301D10A17B2F3D1E62D4`.

No validation artifact was promoted and no committed reference changed.
