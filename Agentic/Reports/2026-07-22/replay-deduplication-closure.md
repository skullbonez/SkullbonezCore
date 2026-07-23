# Replay Deduplication Closure

Date: 2026-07-22
Branch: `nightrunner-22nd-JUL-26`
Result: Complete - RD0-RD3, 4/4

## Outcome

The six-domain Replay census is complete. All seven candidates have a binding
owner ruling: C1-C5 are consolidated, while C6-C7 retain separate values for
their different lifetimes. No candidate remains unruled, no Replay type moved
downward, and no reserve owner, phase gate, cap, high-water counter, growth
counter, or allocation privilege changed.

The owner required every Replay lane to pass before closure. The final reviewed
source passes the complete unit suite, v2 artifact save/restore, strict
two-generation allocation policy, the one-process visual-fidelity oracle,
physics determinism, and the broad repository gate. No artifact, golden,
screenshot baseline, physics CSV baseline, schema, or config was refreshed.

## RD3 Remediation

The final gates exposed three actionable failures that are now reported at the
automation boundary instead of requiring an interactive diagnosis:

- Unsupported Profile `--physics-diag` startup now writes the exact parser
  error to flushed stdout, writes the fatal boundary result to flushed stderr
  and EngineLog, and suppresses the modal dialog for hidden automation. The
  direct probe exits 1 without hanging and captures both messages.
- Editor box placement now materializes one stable `CollisionShape` before
  constructing body/collider descriptors. Editor placement and PhysicsEngine
  registration also fail through named `SB_FATAL` owners if a shape variant is
  ever valueless, so exception-disabled builds retain the owner and stage.
- Replay restore now applies tornado configuration before restoring retained
  per-body timers. A regression test pins disabled-system timer restoration.
  Loaded solver payloads are verified before live mutation, and restore hash
  failures identify the first differing world/count/launcher/snapshot/body
  stage in the captured reason.

Independent review initially found that capture and verification expressed the
same solver-hash sequence twice. The final implementation uses one internal
`BuildSolverHashBreakdown` primitive for capture, payload verification, and
stage diagnostics. The follow-up review verdict is PASS with no remaining
correctness, cohesion, exception, allocation, or dependency finding.

## Replay Candidate Disposition

| Candidate | Final disposition | Result |
|---|---|---|
| C1 | Deduplicated | One Prediction scheduling/timing/reveal policy |
| C2 | Deduplicated | One Prediction reserve/accounting policy; inventory unchanged |
| C3 | Deduplicated | Shared stable-id/model-row/ragdoll/value leaves |
| C4 | Deduplicated | Shared affected-body derivation and path-stride policy |
| C5 | Deduplicated | Canonical Presentation selection composed by overlays |
| C6 | Cohesion retain | Immutable cursor has a distinct cross-boundary lifetime |
| C7 | Cohesion retain | Comparison poses and retained markers have distinct archive lifetimes |

## Standing Replay Reviews

- Downward Replay include: none. The required scan over Physics, Rendering,
  Scene, World, and Core returns zero rows.
- Growth privilege: none added or expanded. The existing three-owner inventory
  is unchanged; the strict two-generation gate reports zero gameplay or reserve
  policy violations.
- Census completeness: C1-C7 all have exactly one ruling; zero deferred or
  unruled candidates remain.

## Touched-Source Comment Audit

Scope source: `git diff --name-only` filtered to source-bearing extensions.
Checklist path: this report. Checked: 14. Deferred: 0. Unchecked: none.

- [x] `SkullbonezSource/Gameplay/TornadoGameplay.cpp`
- [x] `SkullbonezSource/Physics/PhysicsEngine.cpp`
- [x] `SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.cpp`
- [x] `SkullbonezSource/Runtime/Init.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPredictionPublication.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPredictionPublicationOperations.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPredictionTopologyPublication.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRecorder.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`
- [x] `SkullbonezSource/Runtime/Startup/StartupCommandLine.cpp`
- [x] `SkullbonezTests/TestDeterminism.cpp`

Every file retains a Purpose, Summary, Glossary, Invariants, and Related
learning header. New ordering, lifetime, automation-log, exception-disabled,
and deterministic-hash hazards are documented beside the affected code.

## Final Evidence

| Command | Time | Result |
|---|---:|---|
| Profile startup rebuild | 4.49 s | PASS; zero errors |
| Hidden Profile `--physics-diag` process probe | 3.13 s | PASS; stdout ERROR, stderr FATAL, exit 1, no modal/hang |
| Focused tornado restore doctest | <1 s | PASS; 1/1 case, 13/13 assertions |
| Focused Replay doctests | <1 s | PASS; 53/53 cases, 799/799 assertions |
| `tools\validate_tests.bat` | 10.54 s | PASS; 346/346 cases, 68,715/68,715 assertions |
| `tools\validate_replay_allocation_policy.bat` | 12.78 s | PASS; strict two-generation policy clean |
| `tools\validate_replay_v2_artifact.bat` | 54.67 s | PASS; Debug/Profile save and restore |
| `tools\validate_replay_visual_fidelity.bat` | 471.77 s | PASS; 2,401 ticks, one process/generation/presentation, all controls |
| `tools\validate_physics.bat` | 23.08 s | PASS; 44,401-line CSV byte-exact |
| `tools\validate_full.bat` | 136.17 s | PASS; CPU umbrella, five runtime lanes, zero DX12 errors |

The first broad-gate attempt stopped at formatting after 7.68 seconds. Only
the two named touched implementations were formatted; the complete rerun above
then passed.

## Handoff

The completed TODO plan leaves the live ledger under inventory rule 4. The
round-2 campaign continues with `wide-signature-reduction` W0; its inventory
now measures the final post-Replay tree.
