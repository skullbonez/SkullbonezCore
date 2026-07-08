# Engine Cleanup Plans

Date: 2026-07-08
Status: To Eval (proposals — not yet approved work)
Owner: Architecture cleanup

> **Start here: [`00-EXECUTION-GUIDE.md`](00-EXECUTION-GUIDE.md)** — it holds the
> working protocol and the order to do these in. The file numbers below are
> topics, not the execution order.

These plans come from an adversarial architecture audit of SkullbonezCore
(~155K lines first-party C++). Each issue was found by an independent critic,
clustered, and verified against source. The through-line the audit found: **the
codebase invests heavily in *policing* architecture (a 16K-line boundary linter,
275 docs, formal gates) while the architecture itself carries classic
god-objects, a documented policy its code contradicts, and almost no behavioral
tests.** These plans target substance, not ceremony.

Every plan follows two rules learned from the facade-retirement review:

1. **Acceptance is structural and measurable** (types deleted, function line
   counts down, tests exist) — never "the word is gone" or "a comment changed."
2. **No plan adds boundary-checker rules as its enforcement.** One plan
   (`03`) *removes* that apparatus entirely — the regex linter and every frozen
   `MAX_*` ratchet are deleted, not trimmed.

Status legend: `Proposed` (drafted here) · `In Progress` · `Complete` / `Done`.

## Priority index

| # | Plan | Issue | Priority | Status |
|---|------|-------|----------|--------|
| 01 | [Run god-object decomposition](01-run-god-object-decomposition.md) | `Run` owns ~40 subsystems across 16 TUs; 1,664-line `TakeInput()` | P0 | Complete |
| 02 | [PhysicsWorld solver decomposition](02-physicsworld-solver-decomposition.md) | DisjointSet extraction complete; Phase 1 lambda inventory recorded; stage extraction remains open | P0 | In Progress |
| 03 | [Governance apparatus removal](03-governance-apparatus-reduction.md) | Delete the 16,090-line regex boundary linter + all frozen `MAX_*` ratchets; 275 docs (~42%); two plan trees | P1 | Proposed |
| 04 | [Error-handling policy reconciliation](04-error-handling-policy-reconciliation.md) | Exceptions "banned" yet 283 `throw` vs 2 `SB_FATAL`; ratchet frozen | P1 | Proposed |
| 05 | [Behavioral test coverage](05-behavioral-test-coverage.md) | 59 tests cover input, replay restore, physics invariants, asset lookup, terrain, and replay boundary fixtures; link-stub count is 0 | P1 | Complete |
| 06 | [`.inl` translation-unit un-splitting](06-inl-translation-unit-unsplitting.md) | Non-template `.inl` spliced mid-`.cpp` → ~5K-line pseudo-modules | P2 | Complete |
| 07 | [Allocation-gate right-sizing](07-allocation-gate-right-sizing.md) | Repo-wide prose ban vs 2-file enforcement; ~1,400 lines of alloc infra | P2 | Proposed |
| 08 | [RenderHelper global-state removal](08-renderhelper-global-state-removal.md) | Whole primitive-render layer is process-global static state | P2 | Complete |
| 09 | [Replay subsystem right-sizing](09-replay-subsystem-right-sizing.md) | ~17K-line replay; shadow physics engine; 50-field god-struct; twin helpers | P2 | Complete |
| 10 | [EngineContext / IRenderBackend boundary](10-enginecontext-irenderbackend-boundary.md) | 13-ptr context bag with unused `Services()`; aggregate backend + dangling aliases | P2 | Complete |
| 11 | [Render abstraction leaks](11-render-abstraction-leaks.md) | RenderGraph diagnostic-only; backbuffer state = 1 bool; replay call in generic interface | P2 | In Progress |
| 12 | [Ambient singletons: Log / Profiler](12-ambient-singletons-log-profiler.md) | `Log()` welded into prelude; Profiler caches a dangling-prone borrowed pointer | P3 | Done |
| 13 | [Facade retirement (cross-cutting rule)](13-facade-retirement.md) | "Graduate-or-delete; a rename is not done" — principle + FAC inventory; executed by plans 01/10 | rule | In Progress |

## Related

- [`13-facade-retirement.md`](13-facade-retirement.md) — the cross-cutting
  "graduate-or-delete; a rename is not done" rule and FAC inventory. Moved here
  from `Agentic/Plans/To_Eval/`. Its concrete work is executed by plans 01
  (FAC-003) and 10 (FAC-001/002/004/007).

## Notes

- All files here are documentation. Creating them requires no repository
  validation. Each plan names the validation its *implementation* would need.
- Plan `03` proposes editing `AGENTS.md` itself and needs explicit owner
  sign-off before any deletion, because it changes the project contract.
