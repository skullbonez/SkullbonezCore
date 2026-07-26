# Scene-Sized Store Capacity SC3 — Exact Scene-Load Commit

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan: `scene-sized-store-capacity` SC3

## Outcome

SC3 is complete. Authored and generated scene setup now compute exact body,
shape-kind, and point-joint topology before publishing the first entity.
`SceneWorld` orders one commit across the existing concrete Physics owners
inside the SceneLoad allocation phase. Retained backing remains monotonic, while
the point-joint allowance is a separate logical scene limit that may shrink.

The commit also sizes the remaining raw Physics vectors: authored descriptors,
CCD clocks, point joints, mutual-gravity rows, sleep/island rows, support edges,
awake lists, fixed-tree release output, and broadphase query output. Every raw
vector has a stable allocation owner during SceneLoad. Isolated replay
prediction does not create a second authority; it may only reuse the existing
approved `replay_prediction_working_set` owner and its 256 MiB byte cap.

## Exactness And Mutation Preflight

- Loading 300 rows then committing 200 rows produces zero growth events and
  retains the larger physical backing.
- Committing 2,000 rows after 300 produces 67 granted growth events, exactly one
  per affected fixed/runtime owner. The 22 remaining raw vectors are each
  checked by stable owner name and requested row count.
- Point-joint backing committed at 12 rows can retain 12 physical slots while a
  later scene lowers its logical allowance to 8. Creating a ninth live joint
  fails loud with requested, logical, and retained capacities.
- A 9,000-body request fails loud with owner, request, and 8,192-row ceiling.
- Simple ragdoll creation preflights every box row and joint before the first
  body mutation, including editor/tool creation from zero retained capacity.
- Mixed generated scenes run the shared sampler over a copied RNG state to count
  exact sphere/box rows. The live RNG is untouched by preflight. The shipping
  probe pins seed 1 at 32 bodies to 24 spheres, 8 boxes, final state
  `0x72A6EE09`, and zero hull capacity.

No capacity bag, callback pack, second owner, baseline, golden, schema, scene,
or configuration change was introduced.

## Independent Review

Successive read-only reviews exposed and closed logical point-joint retention,
ragdoll preflight, mixed-generation RNG, replay-owner reuse, and evidence gaps.
The final review found one stale SceneLoad-only allowlist claim; the source and
allowlist now distinguish concrete SceneLoad attribution from outer Replay
attribution and name Replay's existing byte cap. The final follow-up returned
`ZERO BLOCKERS`.

The ownership review explicitly passed aggregate ownership, capability slices,
extraction scars, rename evasion, and false-invariant-claim checks. Inventory
evidence remained 1,205 aggregate candidates / 10 signalled / 10 ruled / 0
unruled and 89 extraction scars / 89 ruled / 0 unruled.

## Comment Audit

All 17 touched source-bearing files were inspected against the final
implementation. The audit corrected two false claims: the new raw-vector helper
now documents approved Replay-owner reuse, and the startup probe header no
longer claims appended output fields remain byte-identical. Checked: 17.
Deferred: 0. Unchecked: none. This was a touched-file audit, so no subsystem
checklist was required.

## Validation

- Focused capacity-growth doctest: PASS, 1 case / 2,343 assertions.
- Focused fatal-contract doctest: PASS, 1 case / 104 assertions.
- Full Profile doctest suite: PASS, 410 cases / 2,406,382 assertions.
- Physics standalone smoke: PASS, including ragdoll and generated-capacity
  preflight probes.
- `python tools\check_allocation_policy.py --self-test` and `--repo .`: PASS;
  461 files scanned and zero allowlist errors.
- `tools\validate_format.bat`: PASS; 569 source files and 317 headers clean.
- `tools\validate_fast.bat`: PASS in 158.3 s; zero project/filter, dependency,
  ownership, build, or test failures.
- `tools\validate_physics.bat`: PASS in 24.5 s; Debug/Profile builds and
  byte-exact deterministic physics regression.
- `tools\validate_perf.bat`: PASS in 92.4 s; allocation, physics, and DX12
  budgets accepted.
- `tools\validate_full.bat`: PASS in 330.5 s; mandatory CPU umbrella, coverage,
  zero-error DX12 validation with accepted screenshots, standalone physics
  smoke, and byte-exact physics regression.

No baseline or golden was refreshed.
