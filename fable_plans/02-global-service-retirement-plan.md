# Global Service Retirement Plan

Date: 2026-07-06
Status: Implemented 2026-07-08; service accessors deleted or frozen, and renderer-global owner/accessors removed
Impact area: runtime, rendering, core services; behavior-preserving refactor
Validation for this document: none (documentation-only)

## Relationship to existing plans

`Agentic/Plans/In_Progress/authoritative-plan-03-explicit-service-contexts.md`
owns the mechanical call-site migration, and
`Agentic/Plans/In_Progress/Inventories/carmack-global-service-579-hit-remediation-checklist.md`
is the current hit inventory (579 `Gfx()`/`Cfg()` call sites). **This plan does not
duplicate that work.** It adds the pieces that set is missing: the end-state
contract, singleton lifetime hardening, the Log/Profiler policy decision, and
a count ratchet so the number can never grow while the burn-down runs.

## Problem

- `Cfg()` is a mutable global accessor defined in `Core/Common.h:111` — the
  mega-header — so effectively every translation unit can (and does) reach
  into global engine configuration. `Gfx()` is the same pattern for the render
  backend (`IRenderBackend.h` documents that "callers still ask for
  IRenderBackend through Gfx()").
- 19 files implement `GetInstance()`/`Instance()`-style singletons
  (TextureCollection, Profiler, Log, WorkerPool, LockOrderValidator, ...).
- The repo's own danger-zone table lists "Singleton lifecycle: use-after-
  destroy, double-init crash" as a known crash class requiring `validate_full`
  — the architecture makes an entire bug class *expected*.
- Hidden global reads are the primary blocker for unit testing (plan 01) and
  for the physics-standalone goal
  (`Agentic/Plans/In_Progress/Inventories/physics-standalone-strict-goal-checklist.md`).

## End-state contract (definition of done)

1. **Constructed, not summoned.** Engine services (config, render backend,
   worker pool, asset system, audio) are constructed once by the composition
   root (`Run`, per authoritative-plan-01) and passed by reference through
   explicit context structs. No normal-path code calls a global accessor.
2. **`Cfg()` and `Gfx()` are deleted**, not deprecated. `Common.h` no longer
   declares any service accessor (coordinates with plan 04's `Common.h` split).
3. **Two sanctioned globals, frozen.** `Log` and `Profiler` remain global as
   true cross-cutting concerns, with three hard rules: no configuration reads,
   no ordering dependencies against other services, trivially destructible or
   leaked-on-exit (no use-after-destroy window). Everything else is injected.
4. **Lifetime is structural.** Service construction/destruction order is the
   composition root's member order — visible in one file, enforced by the
   compiler, not by init/shutdown call discipline.

## Phased slices

### Phase 1 — ratchet first (per the repo's own "guardrail before deletion" rule)

- Extend `tools/check_runtime_boundaries.py` with a `Gfx()`/`Cfg()`/
  `GetInstance()` call-site census and a stored budget (the current counts).
  Any commit that *increases* a count fails validation. Include the checker
  self-test the repo requires.
- This makes the 579 a high-water mark from day one, independent of how fast
  the burn-down proceeds.

### Phase 2 — service context structs

- Define narrow context records per consumer domain (the migration gate
  demands domain nouns, not a service bag): e.g. `RenderFrameServices`,
  `PhysicsStepInputs`, `SceneLoadServices`. Each holds references, is
  constructed by the composition root, and is passed down call trees that
  currently reach for globals.
- Order the burn-down by the checklist's own clustering: leaf utility code
  first (cheap wins, unblocks plan 01 tests), then render passes, then frame
  loop.

### Phase 3 — singleton lifetime hardening (parallel with phase 2)

- For each `GetInstance()` singleton: either (a) demote to a composition-root
  member and delete the static accessor, or (b) if it stays global under the
  frozen-globals rule, give it a trivially-safe lifetime (function-local
  static with no destructor-order dependency, or intentional leak) and
  document that in the class header.
- Delete the "use-after-destroy" row from the danger-zone table when the last
  ordering-dependent singleton is gone — that row disappearing is the
  acceptance test.

### Phase 4 — delete the accessors

- Remove `Cfg()` from `Common.h` and `Gfx()` from the render facade; the
  ratchet budgets drop to zero and become bans.

## Validation map

| Slice | Validation |
|-------|-----------|
| Checker/ratchet changes | `validate_fast`, then run the changed script |
| Leaf call-site migrations | `validate_fast` |
| Render pass migrations | `validate_dx12_renderer` |
| Frame loop / lifetime changes | `validate_full` (singleton lifecycle danger zone) |
| Physics-facing migrations | `validate_physics` |

## Risks

- The 579 sites hide ordering assumptions (code that works only because a
  global was initialized as a side effect of something else). Migrating a
  cluster can surface init-order bugs — which is precisely why lifetime moves
  to the composition root's member order in the same phase.
- Threaded consumers (WorkerPool jobs, Profiler GPU timers reading `Gfx()`)
  need the reference captured at job-creation time, not resolved at run time;
  audit those call sites individually rather than mechanically.
