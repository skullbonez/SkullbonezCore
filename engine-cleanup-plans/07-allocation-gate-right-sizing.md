# 07 — Allocation-Gate Right-Sizing

Date: 2026-07-08
Status: Proposed
Priority: P2
Owner: Runtime / Allocation
Source issue: audit iss-05 (severity 4)

## Problem

The stated allocation policy is sweeping; its enforcement and realized coverage
are tiny; and the supporting machinery is AAA-grade for a solo engine.

Verified evidence:

- [`AGENTS.md`](../AGENTS.md) bans `new`/`delete`/`malloc`/STL growth repo-wide
  at runtime. But
  [`check_allocation_policy.py`](../tools/check_allocation_policy.py)
  `DYNAMIC_STL_MEMBER_TARGETS` is exactly two headers
  (`PhysicsBodyStore.h`, `ColliderStore.h`), and `BANNED_PATTERNS` contains no
  `.push_back`/`.reserve`/`.resize` — so Runtime's ~403 `std::vector` / ~259
  `std::string` sites are invisible to the "gate."
- [`RuntimeAllocationTracker.cpp`](../SkullbonezSource/Runtime/Allocation/RuntimeAllocationTracker.cpp)
  overrides all 12 `operator new`/`delete` forms with per-alloc 5-frame
  `CaptureStackBackTrace` and a 1024-slot callsite table;
  [`RuntimeReserveAllocator.cpp`](../SkullbonezSource/Runtime/Allocation/RuntimeReserveAllocator.cpp)
  (762 lines) adds a 160-owner registry + 256-entry growth ring behind an
  `atomic_flag` spinlock. Yet the only enforced gameplay check is `validate_perf`
  running **one** scene (180 frames), guard off by default.
- `PhysicsWorld` wraps ordinary `reserve()` of **debug-only** replay vectors in a
  registered `RuntimeReserveOwnerHandle` + 64 MB cap + growth request + three
  nested RAII scopes, hand-threaded for ~25 vectors — even though replay is
  explicitly *allowed* to grow.

## Goal

Match the machinery to the actual requirement. Pick one: enforce zero-alloc
where it genuinely matters (physics/solver hot loops) and delete the rest, or
keep the ambition and make the checker actually enforce it. Recommendation:
scope to physics/gameplay hot paths; delete the over-built global tracker.

## Approach

- [ ] **Phase 0 — Decide the real requirement.** Is steady-state zero-alloc a
  genuine target or aspiration? For a solo engine, scope it to the physics step
  and per-frame render submission.
- [ ] **Phase 1 — If scoped:** replace the global `new`/`delete` tracker +
  256-entry growth ring + 160-owner registry with a simple assert-on-alloc guard
  active only around the physics step.
- [ ] **Phase 2 — Unwrap debug replay `reserve()`.** Remove the owner-handle /
  growth-request ceremony from debug-only replay buffers (replay may grow by
  policy).
- [ ] **Phase 3 — Delete the regex allocation checker.** Remove
  `check_allocation_policy.py`'s regex matching (STL-member-name targets, banned
  patterns). Enforcement becomes the runtime assert-on-alloc guard from Phase 1
  (real, not regex). Rewrite the `AGENTS.md` allocation section to match.

## Risks

- The physics zero-alloc guarantee is real and valuable; do not weaken the
  physics-step guard. This plan removes *ceremony around debug/replay paths*,
  not the physics invariant.

## Step-by-step implementation

Step 0.1 is a judgment call — a smaller model must not decide it alone.

- [ ] **0.1 (DECIDE — stop for a human).** Decide the real requirement: is
  steady-state zero-alloc a genuine goal, or should it be scoped to the physics
  step and per-frame render submission? Do no code changes until this is decided.
- [ ] **1.1** *(if scoped)* Replace the global `new`/`delete` tracker, 256-entry
  growth ring, and 160-owner registry with a simple assert-on-alloc guard active
  only around the physics step. Gate: `validate_perf`. Commit.
- [ ] **2.1** Unwrap the `RuntimeReserveOwnerHandle` / growth-request ceremony
  around **debug-only** replay `reserve()` calls (replay may grow by policy).
  Gate: `validate_perf` + replay scrub. Commit.
- [ ] **3.1** Delete `check_allocation_policy.py`'s regex matching (and the file
  if nothing real remains); the physics-step runtime assert-guard (1.1) is the
  enforcement. Rewrite the `AGENTS.md` allocation section to match. Gate:
  `validate_perf`. Commit.

## Validation

`tools\validate_perf.bat`; `tools\validate_physics.bat` if the physics-step
guard changes.

## Acceptance (structural)

- [ ] Allocation infrastructure LOC matches the enforced scope (no 1,400-line
  tracker guarding a single 180-frame scene).
- [ ] Debug-only replay `reserve()` is not wrapped in owner-registration.
- [ ] No regex allocation checker remains; enforcement is the runtime
  assert-guard; `AGENTS.md` allocation prose matches.
