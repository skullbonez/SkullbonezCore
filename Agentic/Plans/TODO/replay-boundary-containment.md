# Replay Boundary Containment

Status: Active — 1/3 tasks (RB0 complete; RB1-RB2 remain)
Owner: repository owner; registered 2026-07-20 as campaign plan 8 of 8
Evidence: `../../Reports/2026-07-20/engine-architecture-review.md` (finding H)
Ledger: RB0-RB2
Depends on: `dependency-direction-restoration` L2 (the solver-snapshot move
creates the boundary this plan makes permanent).

## Objective

Keep replay contained without shrinking it. Replay (34,594 lines at the
review tip — larger than all of Physics or Rendering) is working, validated,
and guarded by the frame-exact mega gate; its size is an asset, not a bug.
The risks are boundary and privilege creep: replay types leaking back into
physics/render headers, and allocation privileges growing without owner
visibility. This plan writes the boundary into `AGENTS.md`, audits the
current privilege inventory, and closes with grep-enforceable rules — it is
governance and audit, not refactoring.

## Problem / Evidence

Before plan 1, replay types had leaked into solver stage headers
(`ReplaySolverSnapshot.h` included by four physics headers) and
`PhysicsWorld.cpp` included `ReplayRetainedMemory.h`. Replay is the only
subsystem holding a `RuntimeReserveAllocator` growth privilege. The 2026-07-16
replay mass-reduction campaign fixed compilation mass but did not codify a
standing inbound-dependency rule, so nothing stops the leak from recurring.

## Non-Goals

- No replay refactoring, decomposition, TU splitting, or line-count target
  of any kind (the 2026-07-16 campaign's six owner boundaries stand).
- No replay behavior, artifact-format, or schema change.
- No new validation script or regex checker (Governance Review Model bans
  recreating frozen-count gates); enforcement is review-time grep proofs
  recorded in `AGENTS.md`, same as the plan-1 direction rule.
- Any source defect discovered by the RB1 audit spawns a follow-up plan row
  in `MASTER-PLAN.md`; it is not fixed inside this documentation-scoped plan
  unless it is a one-line include deletion.

## Binding Decisions

1. Standing rule for `AGENTS.md`: `Physics/`, `Rendering/`, `Scene/`,
   `World/`, and `Core/` must not include `Runtime/Replay/*`. Replay
   consumes physics state through `PhysicsApi.h`/`PhysicsEngine` public
   surfaces and the physics-owned solver snapshot; it contributes no types
   downward. Grep proof: `grep -rn "Runtime/Replay" SkullbonezSource/Physics
   SkullbonezSource/Rendering SkullbonezSource/Scene SkullbonezSource/World
   SkullbonezSource/Core` returns zero rows.
2. Replay's allocation privilege stays exactly as the Runtime Static
   Allocation Policy defines it: `RuntimeReserveAllocator`-registered owners
   with replay phase check, hard cap, and logged growth counter. The RB1
   audit table (owner, phase, cap, counter, policy comment) becomes the
   reference inventory; adding a registration later requires updating it in
   the same commit.
3. Reviews of replay-touching work check two questions: did an inbound
   dependency appear (rule 1), and did a privilege appear or grow outside
   the inventory (rule 2). Either finding blocks the touching plan, not
   this one.

## Tasks

- [x] RB0 — Codify: add the boundary rule, grep proof, and privilege-
  inventory contract to `AGENTS.md` (adjacent to the plan-1 direction rule);
  cross-link from `Agentic/README.md` hot-path/validation notes if needed.
  Run the rule-1 grep at current tip and record the result (expected zero
  after plan 1 L2; if plan 1 has not landed, record the pre-existing rows as
  plan-1-owned, not new debt). Documentation-only: no repository validation
  required.
- [ ] RB1 — Privilege and surface audit: enumerate every
  `RuntimeReserveAllocator` replay registration (owner, phase gate, cap,
  high-water/growth counter, policy comment) and every replay-facing surface
  physics/render expose (solver snapshot capture/restore, render pose
  override queue, presentation packets); confirm each against the allocation
  policy and boundary rules; record accepted surfaces with reasons. Output:
  audit tables committed to the closure report; follow-up rows filed for any
  defect. Documentation-only unless a one-line include deletion is applied
  (then `tools\validate_fast.bat`).
- [ ] RB2 — Closure: reconcile RB0 rule text against RB1 findings;
  independent rubber-duck review (is the rule enforceable as written; is the
  inventory complete); final grep proofs at closure tip recorded in the
  report. Documentation-only: no repository validation required.

## Acceptance

- `AGENTS.md` carries the inbound-dependency rule with its grep proof and
  the privilege-inventory contract.
- RB1 tables are complete: every replay reserve registration and every
  downward-facing replay surface is listed with its policy status; defects
  have filed follow-up rows.
- Rule-1 grep returns zero rows at closure tip (or names only rows owned by
  the still-open plan-1 task).
- Independent review clear.

## Validation Summary

RB0 complete 2026-07-21. `AGENTS.md` now makes Replay an upper Runtime
consumer, forbids `Runtime/Replay/*` includes from Physics, Rendering, Scene,
World, and Core, names the Physics-owned value surfaces replay may consume,
and requires replay-touching reviews to check both downward dependencies and
reserve-privilege drift. The reserve contract requires an inventory update in
the same commit as any registration, cap, phase-gate, or counter-coverage
change. `Agentic/README.md` cross-links the rule from the hot-path guidance.

Exact proof at the RB0 tip:

```powershell
rg -n '^#include[[:space:]]+.*Runtime/Replay/' SkullbonezSource/Physics SkullbonezSource/Rendering SkullbonezSource/Scene SkullbonezSource/World SkullbonezSource/Core
```

Result: zero rows. The diff is documentation-only, so no repository validation
is required. `validate_fast` remains required only if RB1 applies a one-line
include deletion.
