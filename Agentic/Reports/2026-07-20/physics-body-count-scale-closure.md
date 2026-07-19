# Physics Body-Count Scale Campaign Closure

Date: 2026-07-20

Plan: `physics-body-count-scale-campaign`, P0-P7, complete 8/8

Final branch: `main` (owner-directed; no branch switch during P7)

Retained implementation commit:
`8bc929bfe8d7d545011c4aa14d2c1ebb5b2c4f82`

## Outcome

The campaign is complete. Persistent broadphase membership, dormant-body work
omission, sleep-owned awake traversal, deterministic pair ordering, and bounded
sleep-support storage are retained. The experimental graph-colored solver is
not retained: P6's exact oracle passed, but the one-worker result regressed and
the 4/8-worker runs crashed, so the binding fallback restored P5 behavior and
recorded graph coloring as evidence-deferred.

P7 added direct byte-exact 0/1/4-worker determinism coverage and accepted the
deterministic sleeper/canonical-order transition under the owner's explicit
baseline-rewrite direction. Final physics baseline writers produced the same
bytes; performance baselines were refreshed; replay behavior stayed identical
and only final-source provenance moved.

## Final Physics Result

All values are final post-format Profile `Frame/Physics` P50 milliseconds.

| Scene | P0 | Final | Delta |
|---|---:|---:|---:|
| scale_200 | 0.1106 | 0.1075 | -2.80% |
| scale_520 | 0.8486 | 0.8021 | -5.48% |
| scale_1000 | 1.0688 | 1.0850 | +1.52% |
| scale_2000 | 1.7852 | 1.8878 | +5.75% |
| sleepy_5000 | 2.1479 | 1.3026 | -39.36% |

The central goal succeeded: 1,000 awake bodies inside a 5,000-body store are
only 20.06% more expensive than the all-awake 1,000-body scene, down from
100.96% at P0. Estimated sleepy-scene hot traffic fell from 95.4 to 82.6
bytes/body/step. The owner accepted the measured 1.52%/5.75% all-awake cost at
1,000/2,000 bodies for the 39.36% sleeping-heavy gain.

The ratified at-least-4,000-awake, approximately-1-ms target is missed: the
5,000-awake no-sleep witness measured 3.8445 ms. The sleeping-heavy scaling
target is met.

At the end of the 200-block wall scene, final physics is 0.0278 ms versus
0.1021 ms at P0: 72.77% lower, or 3.67x faster. One unsupported striker remains
awake after crossing the finite terrain edge; that is correct, not a sleeper
leak.

## Determinism And Capacity

- Algorithmic determinism: certified for the retained tested paths.
- Multithreaded determinism: certified by a direct 520-body doctest and an
  18/18 six-scene, 0/1/4-worker byte-exact process matrix.
- Cross-platform determinism: not certified; no cross-toolchain/platform
  equality lane exists.
- Rollback determinism: not provided; replay/snapshot support is not rollback
  resimulation certification.
- Every support-edge producer uses the shared `MAX_SCENE_OBJECTS * 4`
  fail-before-grow boundary. Exhaustion reports requested count, semantic cap,
  actual reserved capacity, high-water, owner, and gameplay phase before
  issuing Lane F.

The 18-process matrix executable was built from retained implementation commit
`8bc929bfe` immediately before two format-only corrections. They changed only
declaration wrapping and continuation indentation; final full and deep gates
rebuilt the corrected source and passed.

## Baselines And Artifacts

- Core varied CSV, known-issue signatures, and SkullScope query baseline were
  regenerated from the final Debug executable and remained byte-identical.
- DX12 and physics-bench performance baselines now identify `8bc929bfe` and
  contain the accepted final measurements.
- The one-process replay visual/causal baselines now identify full commit
  `8bc929bfe8d7d545011c4aa14d2c1ebb5b2c4f82`. No behavioral replay field
  changed; the causal visual hash was mechanically derived from the refreshed
  visual file.
- Measurement ledger:
  `../2026-07-18/body-count-scale-measurements.md`.
- Comment audit: `physics-body-count-p7-comment-audit.md`, 9/9 checked, zero
  deferred.

## Independent Review

One independent rubber-duck reviewer inspected determinism-transition hygiene,
history dependence, hot-path allocation, capacity diagnostics, and final test
coverage. The initial review found the steady-gameplay support-edge allocation
risk. After the shared bounded append/restore fix and fatal regression test,
the reviewer reported no blocking findings. Residual non-blocking risk: the new
unit fixture proves worker-count equality and parallel threshold use but does
not individually force the pending-wake queue; existing queue-focused tests
and the 18-process runtime matrix cover that surrounding behavior.

Reviewer: one independent agent. Review wall time: 16 minutes 43 seconds. The
runtime did not expose reviewer token accounting.

## Final Validation

| Command | Wall time | Result |
|---|---:|---|
| `tools\validate_full.bat` | 174.573 s | PASS |
| `tools\validate_physics_deep.bat` | 138.033 s | PASS |
| `tools\validate_perf.bat` | 104.668 s | PASS |
| `tools\validate_replay_visual_fidelity.bat` | 438.261 s | PASS |
| `tools\run_graphics_stress.bat 1` | 61.808 s | PASS |
| `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers --frames 1 ...` | 1.391 s | PASS |

The first broad-gate attempt failed in 7.163 seconds on two mechanical format
findings. Both were corrected, the format gate passed, and every final-source
gate above then passed. This was the only P7 closure blocker.

The final deep-physics lane generated a 103,785,259-byte NDJSON SkullScope
trace and a 50,823,168-byte SQLite cache. The gate ran its committed 21-query
packet internally; every query matched exactly and no query result text was
shown to the model. GPT-read data was therefore 0 bytes per query and 0 bytes
total.

## UI Scope Protection

P7 changed no UI ownership or default. Legacy remains the default development
UI. ImGui is explicit opt-in (`--dev-ui imgui`). The two surfaces may be
atomically hot-swapped, but never coexist at the same literal time; simultaneous
focus/input ownership remains forbidden. The only live MASTER task after this
closure is extended hands-on owner acceptance for ImGui/Tracy E17.
