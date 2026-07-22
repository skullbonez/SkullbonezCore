# Replay Subsystem Consolidation RC2 — Prediction Core Split

Date: 2026-07-22
Branch: `nightrunner`
Status: Complete; standing provenance blocker recorded and bypassed per owner directive

## Outcome

RC2 separates Prediction's three concurrency responsibilities without changing
the replay boundary or prediction behavior:

- `ReplayPredictionWorkerSchedule` exclusively owns the in-place amortized task,
  submission budget, in-flight join, and reset. `ReplayPredictionScheduling.cpp`
  owns cancellation, destruction, and visible-prefix promotion.
- `ReplayPredictionIsolatedSimulation` owns the private engine, tornado/world
  state, body backup, calibration, and completed frames. The typed
  `ReplayPredictionSimulationSlice` is the only WorkerPool operation.
- `ReplayPredictionPublication` exclusively owns the release/acquire published
  prefix and worker-failure atomics. Trajectory/baseline publication and causal
  topology publication are physically separated behind internal operations.

The former 4,488-line `ReplayPrediction.cpp` is now 2,083 lines. New owner units
are `ReplayPredictionPublication.cpp` (1,390),
`ReplayPredictionTopologyPublication.cpp` (1,653), and
`ReplayPredictionScheduling.cpp` (117). The 2,084-line drawing unit remains an
RC3 Presentation responsibility. The core's near-target 2,083-line survivor is
subject to RC6's independent cohesion review rather than silently waived.

## Concurrency Proof

- Prefix publication lives only in `ReplayPredictionPublication`: a worker
  release-publishes a completed slot, and readers acquire one bounded count.
- Cancellation, promotion, and state destruction all call the same schedule
  join before task reset or build-bank mutation.
- A focused publication test verifies bounded prefix exposure, worker-failure
  publication, and reset. Existing scheduling/coalescing tests remain green.
- Focused Profile solution build passed in 3.1 s; focused Prediction tests passed
  4 cases / 24 assertions in 1.9 s.

## Allocation And Boundary Proof

- Reserve inventory remains exactly three registered owners: recorder 32 MiB,
  solver snapshot 8 MiB, and prediction working set 256 MiB. No registration,
  cap, phase gate, high-water counter, or growth counter changed.
- Allocation allowlist rows were mechanically relocated to the three new owner
  files; they describe the same prediction owner/cap. Self-test plus repository
  scan passed in 9.4 s across 414 files with zero allowlist errors.
- Because allocation-adjacent calls moved physically, RC6 must recollect the
  plan's strict two-generation allocation evidence once at the closure tip.
- Dependency-direction and downward-Replay include proofs returned no rows.
  No callback pack, `void*`, exception, new inheritance seam, or reserve owner
  was introduced.

## Validation

| Evidence | Result |
|---|---|
| `tools\validate_tests.bat` | PASS in 11.1 s; 344 cases / 68,699 assertions, 99/99 test project/filter items |
| `tools\validate_fast.bat` | First attempts stopped at three split-file formatting findings, then one header alignment and four project-filter inventory stems; all were metadata/layout-only corrections |
| final `tools\validate_fast.bat` | PASS in 62.1 s; 282 headers clean, 730/730 production project/filter items, zero-warning Profile/Debug builds |
| allocation self-test + repo scan | PASS in 9.4 s; 414 files, zero allowlist errors |
| replay visual fidelity, exactly one invocation | BLOCKED after 422.9 s at unchanged config provenance; launcher shape passed one process / one generation / one presented cascade / zero nested scrub, Automation built, and 16 cases / 72 assertions passed |
| `tools\validate_full.bat` | PASS in 108.2 s; CPU/coverage umbrella and five runtime lanes, accepted DX12 images, byte-exact 44,401-line physics baseline |

The single mega-gate invocation reported expected config SHA
`83401df03cb6e212a6a74a38e815fc550d57aa983fc9b792c2c8f4e5c784a3f4`
and actual
`bd0bb719aad7231cf500ca9a61af7d2f017e557b1b18b7de82df7eb93a3b5d93`.
It was not retried. No config, golden, screenshot, scene, or baseline metadata was
edited.

## Comment Audit

Nine touched source-bearing files were inspected against the comment-style
guide: the Prediction core/header, three new implementations, three owner/internal
headers, and the focused test. All nine have the required learning header where
non-trivial and nearby `Concept:`, `Why:`, `Invariant:`, `Lifetime:`, or
`Hazard:` comments for concurrency, allocation, and ownership-sensitive code.
Deferred count: zero.
