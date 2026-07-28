# Principal Engineer Feedback Response

Date: 2026-07-28
Branch: `nightrunner-28th-JUL-26`
Baseline: main tip `0768593d`
Status: Bounded fixes implemented; seven follow-up plans registered

## Scope And Ruling

The owner asked for locally safe fixes now and implementation-ready plans for
the rest. The owner additionally ruled that Physics may return from the current
body SoA to AoS only when representative evidence shows no meaningful
performance degradation. The threshold and witness matrix remain explicit
questions in the owning plan.

The warm-start key guard is a special handoff: it is applied only after the
committed response as the final actionable change and left uncommitted for owner
evaluation. This response authorizes no baseline refresh.

## Finding Disposition

| Feedback | Disposition | Evidence / owner |
|---|---|---|
| Physics body SoA has no vectorized stage consumer, reconstructs scalar vectors, aligns control blocks, and scatters allocations | Completed later | `Agentic/Reports/2026-07-28/physics-body-hot-layout-closure.md` |
| Replay restore is a 12-argument same-class pure forwarder; numeric ceiling is being Goodharted | Exact forwarder removed; deeper operation/governance closed | `Agentic/Reports/2026-07-28/replay-restore-wide-signature-governance-closure.md` |
| Quaternion convention is hidden in the implementation | Fixed | Public header now states anti-Hamilton operand order, transposed Hamilton matrix convention, and axis-angle compensation |
| Warm-start key mask is duplicated and not capacity-guarded | Final uncommitted owner-review diff | No baseline refresh |
| `PhysicsFixedList` copy can phase-fatal through `Reserve()` | Planned | `Agentic/Plans/TODO/physics-fixed-list-copy-contract.md` |
| `SbResult` is 528 bytes on frame-reachable success paths | Planned | `Agentic/Plans/TODO/sbresult-compact-success-path.md` |
| Header identity constants have per-translation-unit internal linkage | Fixed | `IDENTITY_QUATERNION` and `IDENTITY_MATRIX` are `inline const` |
| `GetOrientationMatrix()` is non-const and forces mutable copies | Fixed | API/definition are const; cited Physics copies are removed; immutable conversion is compiled by the Quaternion test |
| Vector-vector `operator*` hides dot products | Planned | `Agentic/Plans/TODO/vector-dot-product-api.md` |
| Determinism terrain helpers share mutable function-local statics | Planned | `Agentic/Plans/TODO/determinism-terrain-fixture-isolation.md` |
| Runtime dependency regexes duplicate authoritative JSON | Planned | `Agentic/Plans/TODO/dependency-proof-generation.md` |
| IDE/F5 first-party builds do not enforce warning-as-error | Fixed | Every Level4 first-party project configuration now sets `TreatWarningAsError=true`; warning-suppressed third-party item groups remain exempt |

## Immediate Source Changes

- Deleted the `RestoreV2ArtifactTargetState` forwarding body and made the
  surviving implementation the single method used by product and Debug probes.
- Documented the quaternion multiplication/matrix convention in
  `Quaternion.h`, made orientation-matrix conversion const, removed the cited
  mutable Physics copies, and added asymmetric multiplication plus non-identity
  matrix goldens that distinguish the engine contract from textbook order.
- Changed namespace identity values to C++17 inline variables.
- Enabled warning-as-error inside the five first-party Visual Studio projects,
  including Debug, Release, Profile, Profile-WPO, and Automation where present.

## Questions Preserved For The Owner

The TODO plans carry the full context and proposed defaults. Blocking questions
cover:

1. the permissible AoS performance threshold and release-blocking witness
   matrix;
2. whether exact-ceiling wide signatures remain a hard numeric rule or become a
   qualitative review trigger, and whether all current 12-argument rows reopen;
3. whether explicit fixed-list cloning belongs only on concrete Physics owners;
4. the required lifetime/thread contract for compact `SbResult` diagnostics;
5. whether vector-vector `operator*` is deleted in one shot or temporarily
   retained.

## Validation

All final-source pre-commit gates passed:

| Command | Elapsed | Result |
|---|---:|---|
| `tools\validate_fast.bat` | 205.5 s | PASS: format, metadata, dependency graph, 86/86 aggregate rulings, 1/1 extraction-scar ruling, Profile build, and bundled tests |
| `tools\validate_tests.bat` | 15.0 s | PASS: unit-test project/build/harness, including the two new Quaternion goldens |
| `tools\validate_physics.bat` | 27.2 s | PASS: byte-exact Physics determinism and Debug/Profile readiness |
| `tools\validate_perf.bat` | 90.6 s | PASS: performance witnesses against committed baselines |
| `tools\validate_replay_visual_fidelity.bat` | 394.4 s | PASS: authoritative 200-box visual/causal/artifact proof and all false-pass controls |
| `tools\validate_dx12_renderer.bat` | 53.5 s | PASS: zero DX12 validation errors and accepted committed-baseline comparisons |
| `tools\validate_full.bat` | 310.6 s | PASS: default PR closure |

The first fast-gate attempt stopped at repository formatting. The repository
formatter was applied; Git verification showed that only the already reviewed
source/test files changed. The complete fast gate and every later gate then
passed. No performance, Physics, Replay, or DX12 baseline was refreshed.

The comment-style audit inspected all 9 touched source-bearing files and
deferred none. Each has the required learning header and nearby convention,
invariant, or ownership comments where the changed code needs them.

## Rubber-Duck Accounting

One read-only independent reviewer was used at the whole-response checkpoint:

| Prompt | Response | Tokens | Wall clock |
|---:|---:|---:|---:|
| 2,241 characters | 3,215 characters | unavailable | approximately 170 s; the collaboration API exposes no exact timer |

The reviewer found no source-behavior blocker and confirmed the five mandatory
ownership questions: no aggregate or capability-slice change, no new extraction
scar, no renamed Replay facade, and accurate source comments. It found one
blocking contradictory historical AoS paragraph in `MASTER-PLAN.md` and two
missing Quaternion goldens. The paragraph now distinguishes the superseded
2026-07-12 full-record ruling from the reopened hot-store evidence question,
and both goldens were added before the passing gate set above. Final review
status: zero blockers.
