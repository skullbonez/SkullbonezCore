# Persistent Contact Convergence Early-Out Closure

Date: 2026-07-30

Branch: `nightrunner-30th-JUL-26`

Plan: completed CE0-CE3 and removed from `Agentic/Plans/TODO/` under inventory
rule 4.

State: Complete

## Owner Decision

The owner approved the CE1 recommendation to retain the current persistent-
contact stopping criterion. Dense-wall frames reaching the 12-iteration cap are
an honest report of row-level non-convergence, not a stale aggregate, an
over-broad residual, or a warm-start failure. The accepted outcome therefore
does not weaken the convergence threshold, add a hidden tolerance budget, raise
the iteration count, or refresh a Physics baseline.

The fixed-capacity diagnostic remains because it records the maximum row delta
for each bounded solver iteration without allocating or changing simulation
state. The controlled object-only chain remains as the deterministic oracle
that distinguishes genuine non-convergence from an accounting defect.

## Acceptance

- CE0 reproduced 1,000/1,000 cap-bound measured wall frames and proved that the
  prior pipeline exported too little per-iteration evidence to identify a
  cause.
- CE1 proved honest row-level non-convergence: every settled iteration-12
  maximum row exceeds the historical stopping threshold.
- Three 1,200-frame wall traces are byte-identical.
- The owner-approved retain branch satisfies the plan's alternate acceptance
  path: the current stop criterion remains correct even though the dense wall
  reaches the cap in every measured frame.
- No simulation behavior, convergence threshold, iteration count, terrain
  restitution, baseline, golden, config, schema, or allocation policy changed.

## Validation And Review

CE3 closes on the already completed final-source evidence because the CE2 owner
decision changes documentation only:

- focused Profile solver tests: 9/9 and 16/16 pass;
- final unit gate: 465 cases and 2,423,885 assertions pass;
- `tools\validate_format.bat`, all four ownership inventories,
  `tools\validate_physics.bat`, `tools\validate_physics_deep.bat`,
  `tools\validate_perf.bat`, and `tools\validate_full.bat`: pass;
- exact SkullScope query regression and three deterministic wall repeats: pass;
- touched-source comment audit: 9/9 checked, zero deferred;
- independent ownership review: zero blocking findings.

Detailed commands, hashes, trace accounting, row-family evidence, and review
accounting are preserved in
`persistent-contact-convergence-early-out-ce1.md`.
