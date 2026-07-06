# Fable Plans

Authored 2026-07-06 by Claude (Fable) from a structural review of the codebase.
Each plan is self-contained: problem evidence, definition of done, phased
slices, per-slice validation, and guardrail ratchets. Documentation-only —
no repository validation required for this folder itself.

These plans complement, and where noted defer to, the authoritative set in
`Agentic/Plans/In_Progress/authoritative-plan-0*.md`. They cover the gaps that
set does not: testing, error-handling policy, identity unification, prediction
world isolation, and build/repo hygiene.

## Index

| Plan | Problem | Depends on |
|------|---------|------------|
| [01-unit-test-pyramid-plan.md](01-unit-test-pyramid-plan.md) | Zero unit tests; all verification is end-to-end golden files | Benefits from 02 and 04, but phase 0 starts today |
| [02-global-service-retirement-plan.md](02-global-service-retirement-plan.md) | `Cfg()`/`Gfx()` global services (579 audited hits), singleton lifetime hazards | Coordinates with `authoritative-plan-03-explicit-service-contexts` |
| [03-prediction-isolated-world-plan.md](03-prediction-isolated-world-plan.md) | Replay prediction mutates the live simulation and restores it every slice | `authoritative-plan-02-physics-store-authority`; enables physics-standalone goal |
| [04-build-layering-and-repo-hygiene-plan.md](04-build-layering-and-repo-hygiene-plan.md) | Single 156-file vcxproj, 3,000+ line files, `Common.h` mega-header, 542 MiB pack with committed build junk | None to start |
| [05-unified-error-handling-policy-plan.md](05-unified-error-handling-policy-plan.md) | 355 `throw` sites / 28 `catch` sites / fatal asserts / bool returns with no policy for which applies where | None to start |
| [06-stable-identity-plan.md](06-stable-identity-plan.md) | Dense `modelIndex` stored as identity and re-validated ad hoc; handle/id/index triality | Coordinates with `authoritative-plan-02` |

## Suggested order

1. **04 slice 1 (repo hygiene)** — one sitting, stops the bleeding in git history.
2. **05 slice 1 (policy doc + ratchet)** — cheap, freezes the error-handling drift.
3. **01 phases 0–2** — the test harness pays for itself on every plan below.
4. **06** and **02** in parallel with the authoritative set.
5. **03** once physics store authority (their plan-02) makes the step path
   callable against explicit stores.
