# Box Vibration And Warm-Start Integrity Closure

> Superseded 2026-07-31 by owner direction. The baseline transition and the
> BV1/BV2/BV3/BV5 solver behavior accepted here were rolled back because the
> varied scene was visibly unstable. See
> `Agentic/Reports/2026-07-31/pre-536-physics-oracle-restoration.md`.

Date: 2026-07-29

Branch: `nightrunner-30th-JUL-26`

Plan: completed seven-task Box Vibration And Warm-Start Integrity campaign

State: Complete

## Outcome

The controlled four-box vibration fixture falls from 566 meaningful vertical
velocity flips to zero, leaves no frame at the 12-iteration cap, reaches a
one-iteration minimum, and records zero cache misses across its measured
window. Persistent object contacts suppress restitution only after they carry
cached load, SAT axis-family hysteresis preserves contact identity, terrain
cache misses use a row-derived first-touch estimate, and positional repair
shares one correction budget across a manifold.

The dense 200-brick wall still reaches the 12-iteration cap in every measured
frame. The separately queued convergence campaign later closed on the
owner-approved retain decision in
`Agentic/Reports/2026-07-30/persistent-contact-convergence-early-out-closure.md`;
the Box campaign did not hide the residual by raising the iteration count.

## Terrain Preservation Oracle

The final focused terrain-bounce fixture locks the exact pre-campaign
one-point sphere outcome after BV3 and BV5:

- first-touch row estimate: `0.5 N*s`;
- restitution bias: `4.5 m/s`;
- accumulated and published normal impulse: `21 N*s`;
- final vertical velocity: `4.5 m/s`;
- final vertical position: `1.0 m`.

The fixture therefore proves that the new terrain cache and position-correction
paths do not alter terrain restitution for this exact impact. The controlled
resting fixture also retains zero flips, zero cap-bound frames, and zero cache
misses after the final source changes.

## Ownership Review

The independent review found no authority-free aggregate, capability-slice
set, extraction scar, rename evasion, or false ownership claim in the
campaign. `PersistentContactSolveTransaction::PrecomputeRows` remains one
cohesive guarded phase: it turns constructed rows plus the previous cache into
effective masses, bias, friction bounds, warm-start impulses, and diagnostics,
and no participant borrow survives return. Its final exact-body digest is
recorded in `tools/function_complexity_rulings.json`.

The review initially blocked closure on the stale digest, incomplete eight-file
audit checklist, and an inexact terrain-restitution test. BV6 resolves all
three before any baseline transition.

## Comment Audit

The cumulative tracked source inventory is eight files:

1. `SkullbonezSource/Physics/ObjectContactManifold.cpp`
2. `SkullbonezSource/Physics/PersistentContactSolver.cpp`
3. `SkullbonezSource/Physics/PersistentContactSolver.h`
4. `SkullbonezSource/Physics/PhysicsDiagnosticsView.h`
5. `SkullbonezSource/Physics/PhysicsSolverSnapshot.h`
6. `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h`
7. `SkullbonezTests/TestObjectContactManifold.cpp`
8. `SkullbonezTests/TestPersistentContactSolver.cpp`

All eight were inspected against the comment style guide. BV6 corrected the
warm-start glossary to distinguish cache reuse from a cache-miss estimate,
removed an interim task label from a permanent invariant comment, and routed
campaign history to this closure report. The controlled scene
`SkullbonezData/scenes/box_vibration_t0.scene.json` was separately inspected
against the authored-scene schema. Checked: 8. Deferred: 0.

## Final Baseline Transition

One final Debug build generated every physics artifact after the terrain oracle
and comment audit were complete. Four committed goldens moved:

| Baseline | Final bytes / lines | Final SHA-256 | Reviewed delta |
|---|---:|---|---|
| `physics_regression_varied.csv` | 6,370,088 / 44,401 | `7F6B88B290F102E57345F894A4C27C2A9201EED74CCFC1E5D213488031B72572` | 35,091 lines |
| `shooting_reaction_volley.csv` | 95,684 / 641 | `6D66DAC168382E98679DFF48B23DCE576C8D91890805935EB0A29153A176504C` | 28 lines |
| `physics_known_issue_signatures.json` | 1,761 / 42 | `2CC515142B76BC2B6483D1CA907F9D55A76F04E531AB289AC2F583530AC24FFF` | seven signature lines |
| `physics_query_varied.json` | 102,663 / 3,432 | `EFA9ABC9D157AED75A761A5238382C4A7274E46D255EE568B7611CB2D222C3AF` | 1,662 additions / 1,607 deletions |

The two complete varied-scene runs are byte-identical. All three bullet sweeps
and the three-body chaos baseline remain exact and therefore did not move. The
known-issue packet records final solver, stacking, at-rest, and terrain-contact
signatures; no NaN or infinity appeared in the inspected cumulative outputs.

## Validation

- Final Debug build: PASS, zero warnings and errors.
- Exact terrain restitution oracle: PASS, 1 case / 10 assertions.
- `tools\validate_format.bat`: PASS, 573 implementations, 319 headers, and
  1,535 repository-relative Related paths.
- Ownership inventories: PASS, 85/85 gated aggregates ruled, 1/1 extraction
  scar ruled, every 12-or-more-parameter signature ruled, and 40/40 triggered
  function bodies ruled.
- `tools\validate_physics.bat`: PASS.
- `tools\validate_physics_deep.bat`: PASS; six CSV baselines, known-issue
  signatures, shooting reactions, and SkullScope queries match.
- `tools\validate_tests.bat`: PASS, 463 cases / 2,423,860 assertions.
- `tools\validate_full.bat`: PASS; CPU/coverage preflight, Automation and Debug
  builds, DX12 validation with zero InfoQueue errors, runtime lanes, and
  byte-exact Physics all completed.
