# Pre-536 Physics Oracle Restoration

Date: 2026-07-31

Branch: `nightrunner-30th-JUL-26`

State: Owner-directed restoration

## Owner Ruling

Physics goldens are owner-controlled. An agent may not regenerate or accept a
changed physics oracle without the owner's explicit approval of that exact
transition. The baseline update in `536e0a60` was not accepted and is
superseded by this forward-history repair.

The uncommitted Angular Impulse Frame Correctness AI2 work was preserved before
the repair in the named local stash `paused AI2 world-inertia correction before
restoring pre-536 physics`. It is not part of this change.

## Restored Behavior

The repair removes the four behavioral slices that caused the rejected oracle
transition while retaining later unrelated physics diagnostics and hot-path
work:

- BV1 cached object-contact restitution suppression;
- BV2 SAT axis-family hysteresis;
- BV3 row-derived terrain first-touch support and terrain cache expansion;
- BV5 per-manifold position-correction division.

The historical terrain support seed, stateless SAT tie selection, original
object restitution branch, resting-policy cache boundary, and per-row position
correction are authoritative again.

Later convergence diagnostics remain available through the explicit SkullScope
`solver --include-convergence` option. They are excluded from the default
validated query packet so diagnostic-schema growth cannot silently redefine the
owner-controlled JSON oracle.

## Restored Oracle

The following files were restored byte-for-byte from `536e0a60^`; they were not
regenerated:

| Baseline | Restored SHA-256 |
|---|---|
| `physics_known_issue_signatures.json` | `09BD3EA10A019C628A8E58241B82790C3FD58DD6F1087E92722C989E76D8566A` |
| `physics_query_varied.json` | `320DFCA156C3B5BA000293420A67CE531FC6548088C3637AF896723919834FDB` |
| `physics_regression_varied.csv` | `D1E0EC54DE218EFA4923C1505E0FDAB1BD556BFA5E8F3BB595203C5EE6B8F752` |
| `shooting_reaction_volley.csv` | `F6F8ECA406F90ABA20304D5278F3FA13E1946799D81A871A7D0FEBB02B3F6815` |

## Direct Proof

A fresh Debug solution build completed with zero errors. Focused tests passed:

- ObjectContactManifold: 4/4 cases, 329/329 assertions;
- PersistentContactSolver: 11/11 cases, 140/140 assertions.

One waited, visible DX12 varied-scene launch wrote two complete internal runs.
Both contain 44,401 lines, differ from each other on zero lines, and differ
from the restored pre-`536` oracle on zero lines. This direct comparison used no
update flag and performed no baseline write.

Final validation passed without an update flag or baseline write:

- `tools\validate_fast.bat`;
- `tools\validate_tests.bat`;
- `tools\validate_physics.bat`;
- `tools\validate_physics_deep.bat`;
- strict compiled-symbol reachability: 79 ruled rows, zero blocking findings;
- strict glossary inventory: 964 unique terms, zero multi-file definitions or
  drift;
- focused SkullScope check: opt-in convergence projection populated and the
  default `physics_query_varied.json` packet matched exactly.

An independent read-only rubber-duck review returned `CLEAR`. It confirmed the
C++ rollback, exact pre-`536` baseline blobs, owner-only oracle governance,
current function-complexity ruling, later diagnostic preservation, default
query exactness, and explicit convergence access. No blocking or missing-
evidence finding remains.

## Superseded Evidence

`Agentic/Reports/2026-07-29/box-vibration-and-warm-start-integrity-closure.md`
remains in history to explain the rejected transition, but it no longer states
the authoritative solver policy or oracle. The excluded historical MASTER-PLAN
row points here instead.
