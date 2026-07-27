# Terrain Legacy And Contact Seed — T0 Baseline

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan phase: T0

## Byte-Exact Baseline

Before any terrain or solver source edit:

- `tools\validate_physics.bat` passed. The checker split the current Debug
  output into two 44,401-row runs and verified each run byte-exact against the
  committed 44,401-row `physics_regression_varied.csv`.
- `tools\validate_physics_deep.bat` passed. The varied, solver, bullet,
  shooting, space, stacking, at-rest, and terrain-contact artifacts matched;
  `physics_known_issue_signatures.json` matched; and the generated SkullScope
  `physics_query_varied.json` packet matched exactly.
- Profile and Debug finished ready with zero build warnings or errors.

No mismatch was absorbed and no baseline moved.

## Focused Oracle

The smallest current focused set covering terrain sampling, terrain-row warm
start, edge/resting support, and end-to-end terrain penetration is:

1. `Physics terrain view: analytic and cached-cell sampling stay detached from World`
   — 12 assertions.
2. `Persistent contact solver: warm-start cache is reused on a matching terrain row`
   — 8 assertions.
3. `Persistent contact solver: a box gains sleep support only after toppling from its edge`
   — 10 assertions.
4. `PhysicsEngine invariants: settled bodies stay within terrain penetration tolerance`
   — 17 assertions.

All four cases and all 47 assertions pass from the baseline Debug binary.

The current suite does not directly isolate the shoreline `0.35f` seed scale.
That is the measured coverage gap T3 is required to close with the first-frame
resting and shoreline no-bob tests; it is not a pre-existing output mismatch.
