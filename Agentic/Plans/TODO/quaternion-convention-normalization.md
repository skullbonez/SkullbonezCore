# Quaternion Convention Normalization

Date: 2026-07-29
Owner: skullbonez
State: Not started
Ledger tasks: 6 (QN0-QN5)
Branch: TBD (register at start)
PR: TBD

**Sequenced last in the campaign by owner direction.** It is the only plan here
that can move a baseline, and it is gated on hands-on owner visual acceptance
before any baseline is regenerated.

## Goal

Replace the engine's reversed-operand, transposed-matrix quaternion convention
with the standard Hamilton convention, so imported formulas, textbook
references, and future third-party math read correctly without transposition.

## Problem And Evidence

Measured on 2026-07-29 against `main` tip `90e4d52f`.

The convention is currently documented rather than corrected
(`SkullbonezSource/Maths/Quaternion.h:27-31`). Two coupled inversions:

**1. Reversed multiplication.** `Quaternion::operator*`
(`Quaternion.cpp:133-146`) computes the Hamilton product with all three
cross-term sign pairs flipped:

```
code:      x = pw*qx + px*qw - py*qz + pz*qy
Hamilton:  x = pw*qx + px*qw + py*qz - pz*qy
```

Flipping the cross-product sign is exactly swapping operand order, so
`p * q` in code equals Hamilton `q ⊗ p`.

**2. Transposed orientation matrix.** `GetOrientationMatrix`
(`Quaternion.cpp:120-130`) emits the standard active-rotation matrix's columns
as rows, i.e. `Rᵀ(q)`, which equals `R(q*)`.

**3. A compensating third wrong.** `RotateAboutAxis` (`Quaternion.cpp:102-117`)
negates the delta sine and pre-multiplies so that an active positive world
rotation still comes out positive under the other two inversions. Its own
comment says so.

### What this means

The three inversions cancel for *composed rotation behavior*. The engine is
self-consistent and visually correct today. What is wrong is the
**representation**: a stored `(x, y, z, w)` in this engine denotes the conjugate
of what any external reader, library, or textbook would read from those bytes.

That is why this change is expensive out of proportion to its three-line core.

### Blast radius

| Surface | Count | Note |
|---|---:|---|
| `GetOrientationMatrix` call sites | 55 | every consumer of the transposed basis |
| `TransposeMultiply` sites | 14 across 9 files | includes `PersistentContactSolver`, `ObjectContactManifold`, `PhysicsBodyStore`, `Ragdoll` hot paths — each is a candidate compensation site |
| `GetQtnRotatedAboutX/Y/Z` sites | 6 | produce positive-sine quaternions composed under the reversed multiply |
| Scene files storing raw orientation | 23 | `SkullbonezData/scenes/*.scene.json` |
| Replay artifact orientation | v2 binary | `ReplayV2Artifact.cpp:236` `AppendOrientation` writes raw floats |
| Authored Euler composition | 1 | `AuthoredSceneParserSchema.h:119-132` composes `xRotation * yRotation * zRotation` under the reversed multiply; the effective order inverts when the multiply is fixed |

Every persisted orientation must be conjugated — `(x,y,z,w) → (−x,−y,−z,w)` —
or authored and recorded rotations will invert. Under the repository's own rule
that authored schema changes are versioned migrations, this requires a scene
format version bump with a deterministic migration step, a replay artifact
version bump, upgraded committed files, and legacy/current/future/writer tests
in the same commit.

## The Acceptance Insight

**This is a representation change, not a behavior change.** That fact is the
plan's primary diagnostic tool and it determines what "looks right" means at the
owner gate.

Expected outcome after a correct implementation:

- Short horizon: the simulation is *visually indistinguishable*. Objects spawn
  in the same orientation, stacks settle the same way, ragdolls fall the same
  way.
- Long horizon: trajectories diverge, because the arithmetic spelling changed
  and rigid-body contact simulation is chaotic under last-bit perturbation.
  This is expected and is why baselines must be regenerated.

Therefore, at the QN4 gate:

- Divergence that grows gradually from visually identical initial frames is the
  **success** signal.
- Anything wrong at frame zero — a tree upside down, a building rotated ninety
  degrees, a ragdoll inverted, a camera facing backwards — is a **migration
  bug**, not acceptable divergence. Stop and fix; do not proceed to baselines.

## Design Constraints

- **No baseline moves before QN4 passes.** This is the binding owner condition.
  QN0-QN3 run entirely against the characterization tests and existing
  baselines-as-tripwires. QN5 is the only task permitted to regenerate.
- **Regenerate only from the final binary.** Per the repository baseline rule:
  physics CSV and SkullScope baselines come from the final Debug executable,
  final scene files, and final config that will be committed, and the matching
  gate reruns afterward against the updated files.
- **The Provenance-Only ruling does not apply.** That standing ruling covers
  hash-only reconciliation. This plan moves real behavioral values and requires
  the explicit owner acceptance recorded at QN4.
- **Migration is deterministic and reversible on paper.** Conjugation is exact
  in floating point — it negates three sign bits and touches no mantissa. The
  migration must be provably lossless, and a round-trip test must prove it.
- **No compatibility spelling.** No `LegacyQuaternion`, no
  `GetOrientationMatrixHamilton` alongside the old one, no convention flag. The
  old convention is deleted.

## Risk Register

Recorded so the owner can re-scope at QN0 with full information.

1. **A compensation site is missed.** 14 `TransposeMultiply` sites and 55 matrix
   consumers; one un-fixed site produces a subtly wrong inertia tensor or a
   mirrored contact basis that may not be visually obvious. Mitigation: QN0's
   characterization tests must cover rotation composition, matrix application,
   inertia transformation, and contact-basis construction *before* any edit.
2. **Committed scene content is silently wrong after migration.** Mitigation:
   QN3 migrates and QN4 inspects every one of the 23 scenes visually, not a
   sample.
3. **Baseline regeneration hides an unrelated regression.** Mitigation: QN5
   inspects the complete artifact delta and confirms no body loss, non-finite
   state, energy explosion, allocation failure, or schema change rides along.
4. **The value may not justify the cost.** The engine is self-consistent today.
   The benefit is future readability and third-party interoperability, not
   correctness. QN0 is the checkpoint at which the owner may stop with only a
   census spent.

## Ledger

- [ ] QN0 — Characterize and census. Write characterization tests that pin the
  current *observable* behavior — composed world rotation, matrix application to
  a vector, world-inertia transformation, contact tangent basis, camera basis —
  independent of internal representation. Census every one of the 55 matrix
  consumers, 14 transpose sites, 6 axis-constructor sites, and every
  serialization path, classifying each as compensation, neutral, or persisted
  data. **Owner checkpoint: stop-or-proceed decision recorded here.**
- [ ] QN1 — Correct the core. `operator*` becomes true Hamilton,
  `GetOrientationMatrix` returns the untransposed active-rotation matrix, and
  `RotateAboutAxis` drops its compensating sine negation and operand reversal.
  Every characterization test from QN0 must still pass.
- [ ] QN2 — Fix every compensation site identified by QN0, including the Euler
  composition order in `AuthoredSceneParserSchema.h`. Characterization tests
  pass; existing physics baselines are expected to differ and are used only as a
  tripwire, not refreshed.
- [ ] QN3 — Data migration. Bump the scene format version and the replay v2
  artifact version, add deterministic conjugation migration steps, upgrade all
  23 committed scene files, and add legacy / current / future / writer tests plus
  a conjugation round-trip losslessness test. Run
  `python tools\migrate_data_formats.py --check`.
- [ ] QN4 — **Owner visual acceptance gate. Blocking.** Build the final binary
  and run every one of the 23 migrated scenes plus the ragdoll, buoyancy,
  convex-hull stacking, and space/orbital scenes. The owner confirms initial
  frames are visually indistinguishable from the pre-change build and that
  divergence, where present, grows gradually rather than appearing at spawn.
  Record the owner's explicit acceptance, the scenes reviewed, and any scene
  rejected. **No baseline may be regenerated before this task is checked.**
- [ ] QN5 — Regenerate and close. From the final Debug executable and final
  committed content, regenerate physics CSV, SkullScope query, replay visual
  fidelity, and screenshot baselines. Inspect the complete delta for each.
  Rerun every mapped gate against the updated artifacts, audit comments, correct
  the now-obsolete convention documentation in `Quaternion.h`/`.cpp`, and pass
  independent review.

## Acceptance

- `Quaternion::operator*` is the textbook Hamilton product and
  `GetOrientationMatrix` is the untransposed active-rotation matrix. The
  `Anti-Hamilton composition` glossary entry and both convention invariants are
  deleted from `Quaternion.h`, not reworded.
- Every QN0 characterization test passes unchanged from before QN1 to after QN5.
  These tests are the proof that observable behavior did not move.
- Zero compatibility spelling, convention flag, or legacy path survives.
- All 23 scenes and the replay v2 artifact carry bumped format versions with
  deterministic migrations and full legacy/current/future/writer coverage.
- Conjugation round-trip is proved lossless by test.
- The QN4 owner acceptance is recorded with the scene list before any baseline
  file changes. A regenerated baseline without that record is a closure failure.
- The QN5 delta inspection confirms no body loss, non-finite state, energy
  explosion, allocation-policy failure, or unrelated scene/config/schema/render
  change rode along with the refresh.

## Validation

- Iteration: focused Profile build, `TestQuaternion`, `TestMatrix4`,
  `TestDeterminism`, `TestSceneParserUnit`.
- QN1-QN2: `tools\validate_tests.bat`; physics baselines are expected to differ
  and must not be refreshed at these tasks.
- QN3: `python tools\migrate_data_formats.py --check`,
  `tools\validate_tests.bat`, `tools\validate_all_cpu_tests.bat`.
- QN4: final Debug and Profile builds plus hands-on scene runs. No script gate;
  the deliverable is the recorded owner acceptance.
- QN5: `tools\validate_physics.bat`, `tools\validate_physics_deep.bat`,
  `tools\validate_dx12_renderer.bat`, `tools\run_graphics_stress.bat 1`,
  `tools\validate_replay_visual_fidelity.bat`, `tools\validate_perf.bat`, and
  `tools\validate_full.bat`. Every scene-file change also independently requires
  `validate_full` under the mapping table.

## Comment-Audit Checklist

- [ ] `SkullbonezSource/Maths/Quaternion.h`
- [ ] `SkullbonezSource/Maths/Quaternion.cpp`
- [ ] `SkullbonezSource/Maths/RotationMatrix.h`
- [ ] `SkullbonezSource/Maths/RotationMatrix.cpp`
- [ ] `SkullbonezSource/Maths/Matrix4.cpp`
- [ ] `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- [ ] `SkullbonezSource/Physics/ObjectContactManifold.cpp`
- [ ] `SkullbonezSource/Physics/PhysicsBodyStore.cpp`
- [ ] `SkullbonezSource/Physics/Ragdoll.cpp`
- [ ] `SkullbonezSource/Runtime/Camera/AttachedCameraController.cpp`
- [ ] `SkullbonezSource/Runtime/Interaction/RuntimePickGeometry.cpp`
- [ ] `SkullbonezSource/Scene/AuthoredSceneParserSchema.h`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp`
- [ ] `SkullbonezTests/TestQuaternion.cpp`

Reconcile against `git diff --name-only` at QN5; the QN0 census will add sites.
