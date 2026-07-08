# Fable Plans

Authored 2026-07-06 by Claude (Fable) from a structural review of the codebase.
Each plan is self-contained: problem evidence, definition of done, phased
slices, per-slice validation, and guardrail ratchets. Documentation-only —
no repository validation required for this folder itself.

These plans complement, and where noted defer to, the authoritative set in
`Agentic/Plans/In_Progress/authoritative-plan-0*.md`. They cover the gaps that
set does not: testing, error-handling policy, identity unification, prediction
world isolation, and build/repo hygiene.

## Status snapshot (2026-07-08, post PR #106 merge + independent review)

| Plan | State | Remaining |
|------|-------|-----------|
| 01 unit-test pyramid | **DONE** (42 doctest cases; harness wired into `validate_fast`) | nothing |
| 02 global-service retirement | Phases 1-2 done; `Cfg()`/`EngineConfig::Instance`/`WorkerPool::Instance`/`Window::Instance` deleted | G2b (19 Physics/Debug `Gfx()` sites the original census missed — see CENSUS CORRECTION in the progress file), G3 endgame, L2 Profiler freeze, L3, closure |
| 03 prediction isolated world | Phases 1, 2, 4 done; mutation window deleted, guardrail live | Phase 3 (worker job) — OPTIONAL and **human-awake only**; do not run unsupervised |
| 04 build layering / hygiene | Phase 1 (repo hygiene) done | Phases 2-4 (lib split), phase 5 (mega-files; `PhysicsWorld.cpp` has grown to 3,947 lines) |
| 05 error-handling policy | Phase 1 (policy, `SB_FATAL`, `SbResult`, 355-throw ratchet) done | Phases 2-5 conversions (2-3 recommended; 4-5 are optional-value, DX12 last) |
| 06 stable identity | Not started (inventory pre-verified) | All phases |
| 07 blocker remediation | Quick wins + A0 + Cluster D UI slice done; 27 blockers remain | Clusters A1/B1 design docs are **human-awake**; do not attempt unsupervised |
| 08 demo director | Phases 0-2 + P3.1 done | P3.2 (reveal rate), phase 4, closure |
| 09 consequence look | Not started | All phases |

Rules that apply to EVERY census/inventory in these files:

- Use `git ls-files` + `grep -rn`, never bare `rg`. `.gitignore` contains
  `Debug/`, which hides the TRACKED directory
  `SkullbonezSource/Physics/Debug/` from `rg` — this already corrupted one
  recorded census (since corrected in 02-progress).
- Never raise a ratchet budget or add an allowlist row to make a checker
  green. Budgets only go down; a red ratchet means stop and report.
- New interaction proof scripts go in `SkullbonezData/interaction/`
  (committed), never `Agentic/Temp/` (gitignored, unreproducible).

## Index

Each plan has a paired `*-progress.md` — a checkbox-level implementation
checklist with verified code anchors, explicit target code, and per-item
evidence requirements, written so a less capable model can execute it. Work
from the progress file; the plan file is the rationale.

| Plan | Progress checklist | Problem | Depends on |
|------|--------------------|---------|------------|
| [01-unit-test-pyramid-plan.md](01-unit-test-pyramid-plan.md) | [progress](01-unit-test-pyramid-progress.md) | Zero unit tests; all verification is end-to-end golden files | Benefits from 02 and 04, but phase 0 starts today |
| [02-global-service-retirement-plan.md](02-global-service-retirement-plan.md) | [progress](02-global-service-retirement-progress.md) | Global service accessors + singleton lifetime hazards (`Cfg()`/`EngineConfig::Instance()` deleted; UI profiler snapshot done; `Gfx()`/profiler diagnostics cleanup remains) | Coordinates with `authoritative-plan-03-explicit-service-contexts` |
| [03-prediction-isolated-world-plan.md](03-prediction-isolated-world-plan.md) | [progress](03-prediction-isolated-world-progress.md) | Replay prediction mutates the live simulation and restores it every slice | `authoritative-plan-02-physics-store-authority`; enables physics-standalone goal |
| [04-build-layering-and-repo-hygiene-plan.md](04-build-layering-and-repo-hygiene-plan.md) | [progress](04-build-layering-and-repo-hygiene-progress.md) | Single 156-file vcxproj, 3,000+ line files, `Common.h` mega-header, 542 MiB pack with committed build junk | None to start |
| [05-unified-error-handling-policy-plan.md](05-unified-error-handling-policy-plan.md) | [progress](05-unified-error-handling-policy-progress.md) | 355 `throw` sites / 28 `catch` sites / fatal asserts / bool returns with no policy for which applies where | None to start |
| [06-stable-identity-plan.md](06-stable-identity-plan.md) | [progress](06-stable-identity-progress.md) | Dense `modelIndex` stored as identity and re-validated ad hoc; handle/id/index triality | Coordinates with `authoritative-plan-02` |
| [07-blocker-remediation-plan.md](07-blocker-remediation-plan.md) | (is itself the sequenced plan) | The 31 rows the 2026-07-07 overnight run blocked — clustered into four root causes with an unblock calendar | Reads the overnight blocker commits; feeds rows back to the overnight machine |
| [08-demo-director-plan.md](08-demo-director-plan.md) | [progress](08-demo-director-progress.md) | Butterfly demo needs hand-authored "scene phases" — camera pose + render type per phase, easy grab/release of the camera | Reuses live-style system + free-fly camera + interaction automation |
| [09-consequence-look-plan.md](09-consequence-look-plan.md) | [progress](09-consequence-look-progress.md) | Cinematic modes make the *world* pretty; the demo needs causality to be the light — grade, glowing lines, two-tone butterfly, divergence counter | Render-only; pairs with 08 (per-phase grade); phase 3 depends on plan 03 |

## Suggested order (original, 2026-07-06 — steps 1-3 and 5 are now done)

1. ~~**04 slice 1 (repo hygiene)**~~ — done 2026-07-07.
2. ~~**05 slice 1 (policy doc + ratchet)**~~ — done 2026-07-07.
3. ~~**01 phases 0–2**~~ — done 2026-07-07 (all phases, 0-4).
4. **06** and **02** in parallel with the authoritative set.
5. ~~**03**~~ — phases 1, 2, 4 done 2026-07-07 (phase 3 optional, human-awake).

## Remaining work order (2026-07-08)

Unsupervised-safe, in value order:

1. **08 P3.2 → phase 4 → closure** — demo-facing, small, well-anchored.
2. **09** (all phases) — render-only; pairs with 08; plan-03 dependency is met.
3. **06** (all phases) — not started, inventory pre-verified, real bug class.
4. **02 G2b** (the 19 Physics/Debug `Gfx()` conversions) and **05 phases 2-3**.
5. **04 phases 2-4** (lib split) — mechanical but merge-hostile; do when the
   worktree is otherwise quiet.

Human-awake only (STOP and involve the user before starting): plan-07
Cluster A1/B1 design docs, plan-07 RUN-015, plan-03 phase 3, plan-04's
history-rewrite decision, and 02 G3/SVC endgame deletions.
