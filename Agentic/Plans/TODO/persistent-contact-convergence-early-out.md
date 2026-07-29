# Persistent Contact Convergence Early-Out

Date: 2026-07-29

Owner: skullbonez

State: In progress (CE0 next)

Ledger tasks: 4 (CE0-CE3)

Branch: `nightrunner-30th-JUL-26`

PR: TBD

## Problem And Evidence

Fresh BV4 evidence from the final BV3 Debug binary shows that
`prediction_ragdoll_wall_200` reaches the 12-iteration cap in every measured
frame from 200 through 1199. Minimum, average, and maximum iteration counts are
all 12 even though 95.796564% of contact rows warm-start and cache misses have
fallen to 7.218498%.

The controlled four-brick vibration fixture does reach the early-out in one
iteration with zero flips and zero misses. The problem is therefore dense-wall
convergence or its stop criterion, not a global failure of the early-out.

Current evidence:
`Agentic/Reports/2026-07-29/box-vibration-and-warm-start-integrity-bv4.md`.

## Goal

Identify which owned residual or row family keeps the dense wall at the
iteration cap, build a deterministic controlled oracle for that condition, and
restore an honest early-out without hiding the finding behind a larger iteration
count.

## Non-Goals

- Raising the solver iteration count. The prior 48-iteration experiment was
  worse and is not a repair.
- Changing terrain restitution or its rolling/rest policy.
- Treating the chaotic wall end state as a direct A/B gameplay oracle.
- Refreshing a baseline merely because the current stop criterion is
  inconvenient.
- Adding an authority-free solver context, callback pack, or retained
  cross-owner borrow.

## Dependencies And Decisions

- Start condition satisfied: Box Vibration And Warm-Start Integrity closed with
  its final source and baseline state recorded in
  `Agentic/Reports/2026-07-29/box-vibration-and-warm-start-integrity-closure.md`.
- CE0 and CE1 are diagnostic and must remain byte-exact.
- This plan does not inherit the Box campaign's bounded-divergence authority.
  A behavior-changing CE2 requires an explicit owner decision after CE1 records
  the controlled oracle and proposed ownership boundary. Without that decision,
  CE2 is marked blocked or parked rather than baselined.
- Use the wall only for within-run structural rates. The CE1 acceptance oracle
  must be a smaller controlled scene or unit fixture.

## Tasks

- [ ] **CE0 — Current-source convergence census.** Reproduce BV4 from the
      post-Box tip, inventory the early-out residuals and row families using
      existing diagnostics first, and record whether new diagnostic-only
      instrumentation is actually needed.
- [ ] **CE1 — Controlled cause oracle.** Isolate the row family or residual
      keeping dense contact at the cap. Add a deterministic focused fixture that
      fails for the measured cause and distinguishes honest non-convergence from
      a stale/over-broad stopping metric.
- [ ] **CE2 — Owner-approved correction.** After the required decision, make the
      smallest owner-correct change. Do not raise iterations, weaken terrain
      restitution, or add a hidden tolerance budget. Prove the controlled oracle
      and wall structural rate without deleting existing contact coverage.
- [ ] **CE3 — Closure.** Run independent ownership review, touched-source comment
      audit, ownership inventories, all mapped validation, and a closure report
      with exact pre/post convergence evidence.

## Acceptance

- The controlled cause oracle fails without CE2 and passes with it.
- The wall records at least one legitimate early-out frame in the same
  200-1199 window, or an owner-approved retain decision explains why the
  existing stop criterion is correct despite all frames reaching the cap.
- No iteration-count increase, terrain-restitution change, NaN/Inf output,
  unbounded allocation, authority-free aggregate, or extraction scar.
- Deterministic repeat artifacts match.

## Validation

Mapped cumulatively:

- Focused convergence fixture and persistent-contact solver tests.
- `tools\validate_format.bat`
- `tools\validate_tests.bat`
- `tools\validate_physics.bat`
- `tools\validate_physics_deep.bat`
- `tools\validate_full.bat`
- SkullScope trace/query accounting for every runtime measurement.
