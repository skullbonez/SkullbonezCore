# Maths Surface Reachability

Date: 2026-07-30
Status: NOT STARTED — 0/4 phases complete
Impact area: `SkullbonezSource/Maths/GeometricMath.{h,cpp}`,
`SkullbonezTests/TestGeometricMath.cpp`, Maths coverage floor, validation tooling
Owner: Maths + governance inventories
Priority: Medium

## Problem And Evidence

Source-only review at tip `91a8403d` on 2026-07-30 measured reachability of the
`GeometricMath` public surface across `SkullbonezSource/`, `SkullbonezTests/`,
and `Agentic/Tests/`, excluding `GeometricMath.{h,cpp}` themselves:

| Symbol | External references |
|---|---:|
| `ComputePlane` | 8 |
| `CalculateIntersectionTime` | 3 |
| `ComputeIntersectionPoint` | 1 |
| `GetHeightFromPlane` | 1 — `TestGeometricMath.cpp:88` only |
| `ComputeTriangleNormal` | 0 |
| `DeterminePointDistFromPlane` | 0 |
| `ComputeBarycentricCoordinates` | 0 |
| `IsPointInsideTriangle` | 0 |

`ComputeBarycentricCoordinates` and `IsPointInsideTriangle` have no reference
anywhere in the repository, including tests. `GetHeightFromPlane` is reached only
by the single assertion that tests it.

`GetHeightFromPlane` also carries a live numeric singularity.
`GeometricMath.cpp:176` computes `theta = _HALF_PI - acosf( normal.y )` and
`GeometricMath.cpp:180` returns `-( normalDist / sinf( theta ) )`. For a vertical
triangle `normal.y == 0`, so `theta == 0` and the return divides by zero. The
15-line derivation comment above the function explains the law-of-sines
construction and says nothing about the singularity. `acosf( normal.y )` is
additionally unclamped, so a normal whose `y` rounds past 1.0 yields NaN.

The governance layer cannot see any of this. `AGENTS.md` names four repeatable
inventories — wide signatures, authority-free aggregates, extraction scars,
function complexity — and none asks whether a symbol is reachable from an entry
point. Ratified subsystem coverage floors measure covered lines and cannot
distinguish a covered live function from a covered dead one, so a dead function
with a test contributes to the Maths floor exactly as a live one does.

## Goal

Delete the unreachable `GeometricMath` surface, and add one repeatable
inventory that reports symbols with no non-test caller so the next instance is
visible at review time rather than at the next fresh read.

## Non-Goals

- No behavior change to any reachable maths function. This plan is strictly
  byte-exact for physics.
- No coverage-floor *lowering* to accommodate the deletion. If removing covered
  dead lines moves a floor, the floor is re-ratified against the new measurement
  with the reason recorded, or the deletion is escalated — the floor is not
  quietly relaxed.
- No conversion of the reachability inventory into a count threshold, ratio, or
  "no more than N unreachable symbols" budget.
- The `GetHeightFromPlane` singularity is not repaired. Owner ruling 2026-07-30:
  delete rather than harden, because hardening retains a public maths API with
  no runtime caller.

## Phases

- [ ] **MR0 — Confirm reachability and intended callers.** Re-run the
  reachability measurement against the current tree with a type-aware
  Profile-preprocessed census, not a bare text search, so overload resolution and
  `_DEBUG`-only calls are visible — `vector-dot-product-api` VD2 proved a
  Profile-only census misses `_DEBUG` call sites. For each of the three deletion
  candidates, establish whether any heightfield, picking, terrain, or editor path
  was ever meant to call it and lost the edge in a refactor; a symbol that a live
  caller *should* reach is a wiring defect, not dead code, and is escalated
  rather than deleted. Confirm `ComputeTriangleNormal` and
  `DeterminePointDistFromPlane` are internal helpers of surviving functions and
  record which. Evidence:
  `Agentic/Reports/2026-07-30/maths-surface-reachability-mr0-census.md`.
- [ ] **MR1 — Delete the unreachable surface.** Remove `GetHeightFromPlane`,
  `ComputeBarycentricCoordinates`, and `IsPointInsideTriangle` from
  `GeometricMath.h` and `GeometricMath.cpp`, and remove the
  `TestGeometricMath.cpp:88` assertion. Reduce `ComputeTriangleNormal` and
  `DeterminePointDistFromPlane` to internal linkage if MR0 proves they are
  helpers with no external caller. Update the `GeometricMath.h` learning header
  so its stated surface matches what remains. Leave no deprecated alias,
  forwarding declaration, or commented-out body. Confirm the Maths coverage floor
  against the post-deletion measurement.
- [ ] **MR2 — Add the reachability inventory.** Create
  `tools/inventory_unreachable_symbols.py` following the existing inventory
  contract: it reports first-party definitions with no reference outside their
  own translation unit and their tests, separating "no reference at all" from
  "test-only reference". Owner verdicts live in
  `tools/reachability_rulings.json` keyed by file and normalized signature, with
  `retain-owner` naming why an unreferenced symbol is intentional (a planned
  seam, a platform-conditional path, a public API with a named future consumer)
  or `repair-plan` naming the active plan that owns deletion. An unruled row
  fails; a ruled one passes. Ship `--self-test` fixtures for test-only
  reachability, internal-linkage helpers, conditional-compilation-only callers,
  overload sets, a planted unruled symbol, and a stale ruling whose symbol moved.
  Wire the scan and self-test into `tools/validate_fast.bat` and add the row to
  `tools/README.md` and the `AGENTS.md` file-to-gate mapping in the same commit.
- [ ] **MR3 — Close the plan.** Rule every row the first repository scan
  produces; a large initial population is expected and each row needs a real
  judgement, not a bulk `retain-owner` sweep. Reconcile the `AGENTS.md`
  Governance Review Model inventory table, complete the touched-file comment
  audit, obtain one independent rubber-duck review answering all five ownership
  questions, and run the mapped gates. Evidence:
  `Agentic/Reports/2026-07-30/maths-surface-reachability-closure.md`.

## Dependencies And Decisions

- Barrier in: `build-configuration-parity` BP1 before MR1. BP1 changes which
  engine TUs the test binary compiles and therefore what the coverage
  instrumentation sees; MR1's floor recheck must run against the post-BP1
  configuration or it measures the wrong tree.
- Barrier out: MR1 before `inverse-trig-domain-guards` TD1. TD0's census covers
  every inverse-trig site including `GeometricMath.cpp:176`; deleting that
  function first keeps TD1 from hardening code this plan removes.
- Owner ruling 2026-07-30 recorded above: delete, do not harden. MR0 may
  escalate a specific symbol back to the owner only on evidence that a live
  caller was intended.
- MR2's first scan will surface far more than the Maths trio. Rows outside this
  plan's scope are ruled, not repaired here; a row needing real work gets a
  `repair-plan` verdict naming a follow-up plan registered in the same commit.

## Acceptance

`GeometricMath` exposes only functions with a proven non-test caller. No
divide-by-zero or unclamped-domain path remains in the Maths layer as a
consequence of this deletion. The Maths coverage floor holds against the
post-deletion measurement, or is re-ratified with a recorded reason.
`inventory_unreachable_symbols.py` reports zero unruled rows and fails closed on
a planted unreferenced symbol and on a stale ruling.

## Validation

`tools\validate_fast.bat` (tools change; includes `validate_tests`), then
`python tools\inventory_unreachable_symbols.py --self-test` and the repository
scan directly per the `tools/*` rule, then `tools\validate_coverage.bat`
directly because Maths instrumentation scope changes, then
`tools\validate_full.bat` because a public maths surface is removed. Physics must
remain byte-exact; deleting an uncalled function cannot move a physics byte, so a
CSV difference means MR0's reachability measurement was wrong and the deletion is
reverted.
