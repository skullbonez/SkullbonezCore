# Physics Facade Unification Closure

Date: 2026-07-20
Branch: `nightrunner-20th-july`
Plan: `physics-facade-unification` (F0-F2, 3/3)

## Outcome

The plan is complete. `PhysicsEngine` is the single cohesive public physics
owner. The former `PhysicsScene` implementation, three private helpers, and all
physics coordination state now live directly on `PhysicsEngine`;
`PhysicsScene.h/.cpp` and their four project/filter rows are deleted.

No compatibility alias, forwarding header, aggregate scene read view, callback
pack, host pointer, service bag, or replacement owner was introduced. Public
method names and call order remain stable. The thirteen immutable reader APIs
project the same fields directly from Engine-owned stores and `PhysicsWorld`.
No baseline, golden, scene, shader, screenshot, replay artifact, or authored
data changed.

Retained implementation commits:

- `c66b6d6c` — complete 63/51-entry public-surface and forwarding inventory.
- `301a0716` — absorb/delete the duplicate facade and pass F1 physics validation.

## Inventory And Absorb

F0 proved that `PhysicsEngine` exposed 63 public entries and `PhysicsScene` 51.
The exact difference was thirteen field-specific Engine readers replacing one
aggregate `ReadView`. Forty-six Engine methods were same-name single-target
relays; `Step`, `SetSleepEnabled`, and `IsSleepEnabled` were name-translated
single-target relays. No method branched, reordered work, invoked a second
target, or owned unrelated logic.

F1 moved all 53 former Scene method/helper bodies and the cohesive state into
Engine. The state remains `PhysicsWorld`, authored descriptors, body/collider
stores, physics material, body limits, contact policy, last world forces and
validity, and the reused fixed-tree wake list. The only diagnostic-text mechanic
renamed the fatal owner from `Physics/PhysicsScene` to
`Physics/PhysicsEngine`, matching the surviving owner.

The first targeted compile exposed an over-broad comment replacement that had
also renamed `PhysicsSceneObjectId`. That identity spelling was restored before
successful builds or formal validation, as required by the Scene Object
Identity Policy.

## Exact Proofs

Every closure-tip proof returned zero rows:

| Proof | Rows |
|---|---:|
| standalone `PhysicsScene`, `PhysicsSceneReadView`, or deleted file path in source/tests | 0 |
| `PhysicsScene` project/filter registration | 0 |
| alias, class, struct, or forwarding compatibility shape | 0 |

`PhysicsSceneObjectId` is explicitly not the deleted facade: all 121 source rows
remain as the repository-mandated stable cross-system identity.

## Comment Quality

All 10 surviving touched source-bearing files were inspected against the
comment-style guide. The absorbed `PhysicsEngine` header/body teach the single
owner boundary, cohesive state, sequencing invariants, and borrowed reader
lifetimes; zero files were deferred.

## Independent Review

One independent read-only rubber-duck review covered the F0 inventory, F1
commit, current owner state, all 53 moved method/helper bodies, thirteen reader
projections, project metadata, exact proofs, identity policy, and F1 validation
log. It reported no blocking or non-blocking findings.

The reviewer confirmed method bodies and call order match after the three
intentional public-name mappings and diagnostic owner label, ownership remains
cohesive physics coordination state, no authority moved to another subsystem,
and no replacement god object or compatibility spelling survived. Review time
was approximately seven minutes.

## Validation

F1 targeted Profile and Debug builds passed in 48.03s with zero
warnings/errors. F1 `tools\validate_physics.bat` passed in 81.55s with the
44,401-line regression baseline byte-exact.

Closure-tip gates:

- `tools\validate_physics.bat` — 55.19s; zero warnings/errors and byte-exact
  deterministic physics.
- `tools\validate_full.bat` — 147.11s log span; 717/717 project/filter items,
  every CPU/coverage and five-process runtime lane passed, DX12 validation
  stayed at zero with all images accepted, and the 44,401-line physics baseline
  matched byte-for-byte.

The desktop tool surface could not expose a separate visible console, so output
is mirrored to `TestOutput/logs/f1_validate_physics.log`,
`TestOutput/logs/f2_validate_physics.log`, and
`TestOutput/logs/f2_validate_full.log`.

## Handoff

The completed three-task plan leaves the active/future ledger under inventory
rule 4, reducing the denominator from 47 to 44. Start
`physics-settings-snapshot` S0 next.
