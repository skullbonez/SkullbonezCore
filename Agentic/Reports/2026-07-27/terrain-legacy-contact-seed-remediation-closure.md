# Terrain Legacy And Contact Seed Remediation Closure

Date: 2026-07-27
Plan: `terrain-legacy-and-contact-seed-remediation`
Phases: T0-T4
Result: COMPLETE — terrain axes, local index safety, and the ratified support
seed are explicit, tested, independently reviewed, and byte-exact

## Final Outcome

- Terrain height samples, post indices, collision cells, normal generation,
  render vertices, flat-slope cells, and Physics terrain lookup now name world
  X and world Z directly. No warning remains that a variable name is crossed or
  misleading.
- `Terrain::LocatePolygon` remains because the debug visualizer needs triangle
  vertices that the detached plane cache does not expose. Its query path now
  rejects non-finite coordinates, non-finite or non-positive scale, and
  finite-but-unrepresentable cells before float-to-integer conversion.
- Its four post indices use overflow-checked `size_t` arithmetic. A local cell
  invariant and a backing-vector guard prove the complete read window at the
  site.
- The existing contact approximation is ratified without changing production
  behavior. Full support is named `TERRAIN_RESTING_SUPPORT_SEED_SCALE = 1.0f`;
  unsupported shoreline/edge support is named
  `TERRAIN_SHORELINE_SUPPORT_SEED_SCALE = 0.35f`.
- The seed contract states its unit-normal, vertical -Y gravity, projected
  normal fraction, non-empty manifold, and equal-per-point assumptions. It also
  identifies directional gravity as a future hazard rather than silently
  claiming support for it.

## Final T0 Harness

The final Debug/Profile source reproduced the opening harness without moving an
oracle:

- `physics_regression_varied.csv`: 44,401 lines, byte-exact, two output runs.
- Deep physics: the varied regression, all three bullet sweeps, shooting
  volley, and three-body chaos artifacts are byte-exact.
- Known-issue signatures: exact.
- SkullScope `physics_query_varied.json`: exact committed-baseline match.
- Focused seed cases: 3/3 cases and 58/58 assertions.
- Full unit harness: 421/421 cases and 2,410,268/2,410,268 assertions.

## Permanent Behavioral Evidence

The first-frame full-support case pins the 2 kg, gravity-30,
1/120-second-step weight impulse at 0.5 and prevents resting sink.

The edge case creates a real 0.75-radian tilted box, builds its two-point
manifold through `BuildTerrainContactManifold`, and publishes it through
`PhysicsTerrainStage::CommitCandidate`. Production classification rejects it as
resting support, permits tangent friction, inhibits sleep, and prevents cache
or rest-policy admission. The 0.35 seed reduces the initial 0.0875 downward
speed to a 0.000131316 residual magnitude; an otherwise identical synthetic
zero-seed policy control retains the full -0.0875 speed.

The deliberately one-iteration three-box fixture pins every measured velocity:

| Seed | Bottom Y velocity | Middle Y velocity | Top Y velocity |
|---|---:|---:|---:|
| Full | 0 | -0.249842 | +0.011951 |
| 0.35 | 0 | -0.291092 | -0.0166234 |
| Zero | 0 | -0.313303 | -0.0320095 |

These alternate paths are focused-test measurements. Zero is a synthetic policy
control, not a runtime configuration or SkullScope variant; SkullScope verifies
the unchanged production baseline.

Four child-process probes pin `LocatePolygon`'s local fatal contracts:

1. exact upper-edge cell out of range;
2. NaN coordinate before conversion;
3. finite maximum-float cell before integer conversion;
4. non-finite terrain scale before conversion.

## Comment Audit And Independent Review

The T4 whole-file comment audit covered 4/4 required files:

- `World/Terrain.cpp`
- `Physics/PhysicsTerrainView.cpp`
- `Physics/PersistentContactSolver.cpp`
- `Runtime/Debug/PhysicsDebugVisualizer.cpp`

It replaced stale generic headers, crossed row/column and directional labels,
ambiguous triangle/index prose, and an incorrectly wrapped Feature ID glossary
entry. The final vocabulary distinguishes posts, quads, detached physics cells,
render relief, display-only debug state, and solver rows.

Independent review first found five blockers: residual crossed axis names,
malformed float conversion, a synthetic shoreline fixture, unpinned stack
measurements, and incomplete seed assumptions. All five were remediated and
re-reviewed. Final verdict: **PASS — ZERO BLOCKERS**. The reviewer directly
confirmed all four fatal probes and the focused 5/5, 31/31, and 22/22 assertion
groups.

## Final Validation

- `tools\validate_format.bat`: PASS.
- `tools\validate_tests.bat`: PASS, 421/421 tests and 2,410,268 assertions.
- Direct Debug fatal harness: PASS; all four terrain query probes terminated
  non-zero with their expected local `FATAL[Terrain]` diagnostic.
- `tools\validate_physics.bat`: PASS, 44,401-line byte-exact regression.
- `tools\validate_physics_deep.bat`: PASS, all deep artifacts and SkullScope
  exact.
- `tools\validate_perf.bat`: PASS on clean retry. The first attempt completed
  scene unload but left the last generated 5,000-body CSV row partially
  written; the unchanged retry regenerated and analyzed the complete matrix.
- `tools\validate_full.bat`: DEFAULT GATE PASSED and independently re-confirmed
  the 44,401-line physics oracle.

No physics, SkullScope, replay, visual, DX12, configuration, schema, golden, or
performance baseline was refreshed.
