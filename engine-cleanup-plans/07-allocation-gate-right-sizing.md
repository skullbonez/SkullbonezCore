# 07 — Allocation-Gate Right-Sizing

Date: 2026-07-08
Status: In Progress (owner decision recorded on 2026-07-09)
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

Right-size the machinery without weakening the requirement. The owner decision
is explicit: runtime allocation policy is **global zero allocation by default**,
not a physics/render-only hot-path policy. Runtime allocations are banned unless
the owner explicitly greenlights an exception and that exception is routed
through the project special allocator/approval path.

Current approved runtime exception: replay only. Replay may allocate or grow at
runtime only through the special allocator path with registered owner,
phase/cap policy, counters, and diagnostics. No other runtime subsystem should
allocate, reserve/grow STL storage, call `new`/`delete`/`malloc`/`free`, or use
equivalent heap paths during runtime.

## Owner Decision - 2026-07-09

Do **not** weaken allocation enforcement to only physics/render hot paths. If
the existing allocation apparatus is too heavy, simplify the implementation
while preserving broad enforcement that catches unapproved runtime allocations.
Require owner approval before adding any new runtime allocation exception.

## Approach

- [x] **Phase 0 - Decide the real requirement.** The requirement is global
  runtime zero allocation by default, with owner-approved special allocator
  exceptions only. Replay is the only currently approved exception.
- [x] **Phase 1 - Inventory the current apparatus against the requirement.**
  Identify which parts of `RuntimeAllocationTracker`,
  `RuntimeReserveAllocator`, `check_allocation_policy.py`, validation scripts,
  and allowlist data enforce the global zero-allocation policy, and which parts
  are ceremony that can be removed without weakening coverage.
- [ ] **Phase 2 - Right-size runtime enforcement.** Keep or replace the global
  runtime allocation guard so unapproved runtime allocations fail. Preserve the
  replay special allocator path with registered owner, phase/cap policy,
  counters, and diagnostics. Gate: `validate_perf`; add `validate_physics` if
  physics runtime phases or allocation scopes change.
- [ ] **Phase 3 - Right-size static/tool enforcement.** Remove frozen regex
  ratchets and narrow spelling checks, but keep broad enough pass/fail checks to
  catch unapproved runtime allocation APIs and unauthorized exception paths.
  Gate: `validate_fast`, then the changed allocation check or replacement.
- [ ] **Phase 4 - Update `AGENTS.md` and closure evidence.** Make the repo
  contract match the owner decision: global runtime zero allocation by default,
  replay-only approved runtime exception through the special allocator path,
  owner approval required for any new exception. Gate: documentation-only if no
  code/tool changed in this step; otherwise use the smallest gate named above.

## Risks

- The global runtime zero-allocation guarantee is intentional. Do not shrink it
  to physics/render hot paths while simplifying the apparatus.
- Replay is the only approved runtime allocation exception. Any broader
  exception needs a new owner decision before implementation.

## Step-by-step implementation

Step 0.1 is decided by owner steering. Do not reopen it unless the owner changes
the policy.

- [x] **0.1 (DECIDE - stop for a human).** Decide the real requirement: global
  zero allocation by default, not a hot-path-only policy. Approved exception:
  replay only, through the special allocator path with owner/phase/cap/counters
  and diagnostics. No code changes in this decision step.
- [x] **1.1** Inventory current allocation enforcement against the global policy
  and list the parts to keep, simplify, or delete. No code change; commit the
  inventory. Documentation-only.

  Completed 2026-07-10:
  - Added `07-allocation-gate-inventory.md`.
  - Current apparatus is 2,274 lines across the runtime allocation tracker,
    runtime reserve allocator, static checker, and allowlist.
  - `check_allocation_policy.py --repo .` scans 296 source files and reports
    30 direct heap/reserve findings, all allowlisted, with 0 dynamic STL member
    findings.
  - `check_allocation_policy.py --self-test` passed.
  - Inventory conclusion: keep the global runtime allocation guard, replay-only
    reserve growth gate, allowlist metadata, and `validate_perf`; simplify
    callsite/growth diagnostics and duplicate phase machinery; broaden static
    STL/growth enforcement without adding frozen counts.
- [ ] **2.1** Simplify the runtime allocation apparatus without weakening global
  runtime enforcement. Replay remains the only approved runtime allocation
  exception and must keep owner/phase/cap/counter/diagnostic reporting. Gate:
  `validate_perf`; add `validate_physics` if physics runtime scopes change.
  Commit.
- [ ] **3.1** Simplify or replace static allocation enforcement so it remains a
  pass/fail guard against unapproved runtime allocation APIs, not a frozen
  regex ratchet. Gate: `validate_fast`, then run the changed checker or
  replacement directly. Commit.
- [ ] **4.1** Update `AGENTS.md` allocation policy wording and the plan closure
  evidence to match the owner decision. Commit.

## Validation

`tools\validate_perf.bat` for runtime allocation guard changes;
`tools\validate_physics.bat` if physics runtime scopes change;
`tools\validate_fast.bat` plus the changed checker/replacement for tool changes.
Documentation-only steps need no repository validation.

## Acceptance (structural)

- [ ] Allocation infrastructure LOC matches the enforced global runtime
  zero-allocation policy.
- [ ] Replay is the only approved runtime allocation exception and remains
  routed through the special allocator path with registered owner, phase/cap
  policy, counters, and diagnostics.
- [ ] Unapproved runtime allocations, STL growth, heap calls, and equivalent
  paths are still caught by pass/fail enforcement.
- [ ] No frozen allocation `MAX_*` ratchet or vocabulary-policing checker
  remains.
- [ ] `AGENTS.md` allocation prose matches the global zero-allocation policy.
