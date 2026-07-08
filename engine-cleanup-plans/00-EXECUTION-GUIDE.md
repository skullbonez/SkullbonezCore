# 00 — Execution Guide (read this first)

Date: 2026-07-08
Status: Proposed
Owner: Architecture cleanup
Audience: the implementing agent (assume a smaller/less-capable model)

This guide is the entry point for executing every plan in this folder. It gives
(1) the working protocol you must follow, (2) the order to do the plans in, and
(3) the campaign checklist. **The file numbers `01`–`13` are topics, NOT the
order of work. Follow the order in section 2 of this guide.**

---

## 1. Working protocol (the rules — do not deviate)

You are working through a checklist. Correctness and finishing beat speed.

1. **One step at a time.** Open the plan you are on. Find the first unchecked
   `[ ]` step in its "Step-by-step implementation" section. Do **only** that
   step. Do not read ahead and do three at once.
2. **Validate before moving on.** Every step names a validation command (or says
   "no validation — docs only"). Run exactly that command. If it passes, tick the
   box `[x]`. If it fails, **STOP**: diagnose and fix until it passes. Never
   start the next step while the current gate is red.
3. **Commit per step.** After a step passes, commit just that step's changes.
   Message format: `cleanup(NN): <plan short-name> step <k> — <what you did>`
   (e.g. `cleanup(02): physicsworld — step 1, extract DisjointSet`). One small
   commit per step keeps progress recoverable if you are interrupted.
4. **The checkboxes are the ledger.** Ticking `[ ]`→`[x]` in the plan file is how
   progress is tracked. Keep them honest: tick only after the step's validation
   passed.
5. **Hard gates are absolute.** Two gates may never be committed through while
   red:
   - **Physics determinism:** `tools\validate_physics.bat` must stay byte-exact.
   - **DX12:** `dx12_validation.txt` must read `0` after
     `tools\validate_dx12_renderer.bat`.
   If a step turns a hard gate red and you cannot make it green, revert that
   step's change and STOP for a human.
6. **Danger zones need extra care.** For any step touching DX12 barriers/resource
   state, run the renderer gate **three times** and confirm 0 validation errors.
   For physics, never reorder floating-point accumulation or island-merge order.
7. **When a step says "decide" — STOP and ask a human.** Some steps require a
   judgment call (e.g. "decide whether to finish RenderGraph or delete it",
   "decide the real allocation requirement", governance deletions). A smaller
   model must **not** guess these. Leave the box unchecked, write a one-line note
   under it, and surface it.
8. **Stay in scope.** Do only what the step says. Do not "tidy" adjacent code,
   rename unrelated things, or reformat files — that creates noise and breaks the
   boundary checker's frozen counts.
9. **Protect the worktree.** Never `git reset --hard`, force-push, rebase, or
   `git clean`. Never touch files you did not change for this step. Feature-branch
   commits are fine without asking; do not commit on `main`.
10. **If blocked, STOP cleanly.** Leave the current box unchecked, add a
    `> BLOCKED: <reason>` note beneath it, commit any safe partial work behind a
    clearly-labelled WIP commit, and hand off. Do not thrash.

**Definition of "a plan is done":** every box in its "Step-by-step
implementation" section is `[x]` **and** every box in its "Acceptance" section is
`[x]`. Only then move to the next plan in the order.

---

## 2. Proposed execution order

Ordered for a smaller model: safe mechanical wins first (build momentum and test
habits), then enabling decoupling, then the big god-object splits, with
cross-cutting work threaded in. Dependencies are called out so you never start
something whose prerequisite is unfinished.

| Order | Plan | Slice to do | Why here | Primary gate |
|------:|------|-------------|----------|--------------|
| 1 | [02](02-physicsworld-solver-decomposition.md) **Phase 0 only** | Extract `DisjointSet`, replace 3 copies | ~90-line mechanical dedup, byte-exact gated — safest high-value start | `validate_physics` |
| 2 | [12](12-ambient-singletons-log-profiler.md) | Unweld `Log` from prelude; make profiler pointer safe | Small, contained; sets up plan 04 | `validate_full` (+`validate_physics` for the sink step) |
| 3 | [06](06-inl-translation-unit-unsplitting.md) **editor files first** | Promote `RunEditor*.inl` to real TUs | Mechanical, build-gated; leave replay `.inl` for step 8 | `validate_fast` then `validate_full` |
| 4 | [13](13-facade-retirement.md) + [10](10-enginecontext-irenderbackend-boundary.md) | Narrow render interfaces, delete `IRenderBackend` aggregate, split `EngineContext`, fix DX12 aliases, collapse `SimulationController` (FAC-004) | Enabling decoupling; 13 is the rule, 10 is the execution | `validate_dx12_renderer` + `validate_full` |
| 5 | [08](08-renderhelper-global-state-removal.md) | De-static `RenderHelper`; RAII batches | Removes global render state; DX12-gated | `validate_dx12_renderer` |
| 6 | [11](11-render-abstraction-leaks.md) | Real backbuffer state; de-leak replay ribbons; RenderGraph honesty | Barrier danger zone — do after 08/10 stabilise the backend | `validate_dx12_renderer` ×3 |
| 7 | [01](01-run-god-object-decomposition.md) | Input command table → shrink `RunState` → shrink `Run` | Big; the flagship. Add its tests via plan 05 as you go | `validate_full` |
| 8 | [09](09-replay-subsystem-right-sizing.md) | Split prediction state; template twins; evict replay from `RunFrame`; finish replay `.inl` from step 3 | Big; depends on 01's `RunFrame` shrink and 06 | replay scrub regression + `validate_full` |
| 9 | [02](02-physicsworld-solver-decomposition.md) **rest** | Lift 33 lambdas to stages; evict gameplay; table-drive snapshot | Big, byte-exact; do after the DisjointSet warm-up | `validate_physics` per phase |
| 10 | [04](04-error-handling-policy-reconciliation.md) | `throw` → F/R/P lanes (no count — ratchet deleted per 03) | After physics (02) and profiler (12) are stable | `validate_full` + `validate_physics` |
| — | [05](05-behavioral-test-coverage.md) | **Continuous** | Do its Phase 0 map first; then each plan above adds its own tests as it lands; kill link stubs anytime | `validate_tests` |
| — | [03](03-governance-apparatus-reduction.md) | **Any time, needs human sign-off** | Delete the regex checker + all `MAX_*` ratchets entirely; contract change (edits `AGENTS.md`); independent of the code work | `validate_fast` |
| — | [07](07-allocation-gate-right-sizing.md) | **Any time, needs a "decide" call** | Requires the allocation-scope decision (rule 7) before code changes | `validate_perf` |

Rationale for the shape: steps 1–3 are low-risk and build the validate-and-commit
habit. Steps 4–6 decouple and clean the render/runtime wiring while its gate
(DX12) is exercised repeatedly. Steps 7–9 are the large decompositions, done once
momentum and (from plan 05) some test coverage exist. Step 10 reconciles error
handling last among code work because it touches physics and the profiler. Plans
05, 03, 07 are cross-cutting: 05 threads through everything; 03 and 07 are
judgment/sign-off gated and can slot in whenever a human is available.

---

## 3. Campaign checklist

Tick a plan here only when **both** its step list and its acceptance list are
fully `[x]`.

- [x] 1. Plan 02 Phase 0 — DisjointSet extracted
- [x] 2. Plan 12 — Log/Profiler ambient coupling removed
- [x] 3. Plan 06 — editor `.inl` promoted to real TUs
- [ ] 4. Plan 13 + 10 — facade rule applied; EngineContext/IRenderBackend/aliases/SimulationController done
  Note (2026-07-08): Plan 10 is complete; Plan 13 remains open only on FAC-005,
  which needs a human-owned public physics API plan before acting.
- [x] 5. Plan 08 — RenderHelper de-statised
- [ ] 6. Plan 11 — render abstraction leaks closed
- [x] 7. Plan 01 — Run decomposed
- [x] 8. Plan 09 — replay right-sized
- [ ] 9. Plan 02 rest — solver decomposed
- [ ] 10. Plan 04 — error handling reconciled
- [ ] C1. Plan 05 — behavioral coverage added (continuous)
- [ ] C2. Plan 03 — regex governance apparatus removed (needs sign-off)
- [ ] C3. Plan 07 — allocation gate right-sized (needs decision)

When every box above is `[x]`, the campaign is complete.

---

## 4. Notes

- Every plan carries a "Step-by-step implementation" section (added for this
  execution pass) with small, ordered, individually-validated steps. Follow those
  steps; this guide only sets the order between plans.
- All files here are documentation. Editing them needs no validation. Each
  *implementation* step names the validation its code change needs.
- If a step's validation command is unfamiliar, its meaning is in
  [`AGENTS.md`](../AGENTS.md) ("After Editing" / "File To Validation Mapping").
