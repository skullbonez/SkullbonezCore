# Terrain Contact Seed T3

Date: 2026-07-27
Plan: `terrain-legacy-and-contact-seed-remediation`
Phase: T3
Result: PASS — ratified approximation documented and pinned after T4 review

## Owner Ruling Implemented

The existing terrain support approximation remains byte-identical. The
production literals are now named:

- `TERRAIN_RESTING_SUPPORT_SEED_SCALE = 1.0f`
- `TERRAIN_SHORELINE_SUPPORT_SEED_SCALE = 0.35f`

The seed-site contract now states that `gravityMagnitude` represents vertical
-Y gravity, the manifold normal is unit length, and `fabsf(normal.y)` projects
that gravity onto the terrain normal. The total seed is divided evenly across
the non-empty manifold. The contract also warns that a future
directional-gravity feature must replace both projection terms together;
reusing this scalar approximation would be wrong.

## Measured Behavior

The focused fixture uses a 2 kg body, gravity magnitude 30, and the fixed
1/120-second solver step. Its full weight impulse is therefore 0.5, the
shoreline impulse is 0.175, and the unseeded impulse is zero.

- First-frame resting support: an initial -0.25 vertical gravity step receives
  the full 0.5 impulse, is marked warm-started, is eligible for the persistent
  cache/rest policy, and finishes without downward velocity.
- Unsupported terrain edge: a real 0.75-radian tilted box produces a two-point
  manifold through `BuildTerrainContactManifold`, is classified as unsupported,
  and publishes sleep inhibition through `PhysicsTerrainStage::CommitCandidate`.
  An initial -0.0875 residual downward step receives a total 0.175 impulse,
  finishes with only 0.000131316 vertical-speed magnitude, is not cached, and
  does not gain rest policy. An otherwise identical synthetic zero-seed policy
  control remains un-warm-started and retains the full -0.0875 downward
  velocity.
- Three-box stack after one deliberately unconverged solver iteration:

| Seed | Bottom Y velocity | Middle Y velocity | Top Y velocity |
|---|---:|---:|---:|
| Full | 0 | -0.249842 | +0.011951 |
| 0.35 | 0 | -0.291092 | -0.0166234 |
| Zero | 0 | -0.313303 | -0.0320095 |

The base row is solved in all three cases, but greater seed strength propagates
support farther through the stack before convergence. The small positive top
velocity at full strength is recorded rather than hidden; the owner ruling
retains the existing approximation because final runtime behavior and all
committed oracles remain exact.

## Permanent Tests

Three focused cases now carry 58 assertions:

1. Full terrain seed prevents first-frame resting sink.
2. A real tilted-box terrain edge exercises production classification, terrain
   stage sleep inhibition, two-point seed division, solver response, and an
   otherwise identical synthetic zero-seed policy control.
3. Full, 0.35, and zero strength order the remaining sink in a one-iteration
   three-box stack.

## Validation

- `tools\validate_tests.bat`: PASS, 421/421 tests and 2,410,268 assertions after
  the T4 malformed-query probes were added.
- `tools\validate_physics.bat`: PASS; `physics_regression_varied.csv` is a
  44,401-line byte-exact match in both output runs.
- `tools\validate_physics_deep.bat`: PASS; all deep physics artifacts remain
  byte-exact, known-issue signatures match, and SkullScope
  `physics_query_varied.json` is an exact baseline match. The full/0.35/zero
  alternate seed measurements above come from the focused unit fixtures; zero
  is a synthetic policy control, not a runtime configuration or SkullScope
  variant.

No baseline, golden, config, replay, SkullScope, visual, or DX12 artifact moved.
