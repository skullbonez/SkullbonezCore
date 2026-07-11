# Entity Model Endgame Closure

Date: 2026-07-11
Branch: `nightrunner-11th-july`

## Delivered

- `PhysicsSceneObjectId` is documented as the single cross-system object
  identity. Subsystem handles remain the hot-path currency.
- `GameModel` and `GameModelCollection` are deleted without introducing a
  successor state bag, compatibility facade, or `SimulationController`.
- `SceneController` directly coordinates scene metadata, physics rows, and
  render-instance rows for atomic object creation, deletion, and topology
  changes. Renderer, diagnostics, tools, replay, and physics operations stay
  with their domain owners rather than becoming SceneController proxies.
- `RenderInstanceStore` is the sole owner of transient fixed-step and audio
  contact-feedback records. Swap deletion and shrinking retain feedback by
  stable scene-object identity.
- Runtime rendering now receives explicit render-instance, physics-query,
  worker-pool, and configuration inputs instead of reaching through the
  deleted collection facade.

## Structural and Focused Evidence

- Exact source/test searches for `GameModel`, `GameModelCollection`,
  `EntityId`, and `SimulationController` returned no type references.
- Visual Studio project/filter validation reported 604 project items and 604
  filter items with zero errors.
- Allocation-policy self-test and repository scan passed: 309 files, 39 direct
  heap findings, 138 dynamic-STL-member findings, 622 STL-growth findings, and
  zero allowlist errors.
- `SKULLBONEZ_TESTS.exe --test-case="RenderInstanceStore*"` passed 3/3 cases
  and 26/26 assertions, including swap-delete and real shrink retention.

## Comment Audit

The final touched-source inventory contained 77 source-bearing files. All 77 were
inspected against the comment-style guide; all have the required learning
header or qualify as an included class-body fragment, and meaningful ownership,
lifetime, identity, or invariant changes have nearby teaching comments.
Checked: 77. Deferred: 0. Unchecked: 0.

## Independent Review

The single plan-end rubber-duck review initially blocked closure because the
first extraction copied renderer configuration, debug packaging, physics
queries, and tool/diagnostic forwarding onto `SceneController`. Those unrelated
facades were removed and their callers now use the owning renderer, physics
engine/query layer, worker pool, or configuration directly. The same reviewer
confirmed the ownership blocker was resolved and found no new blocker. Its only
non-blocking test note was strengthened from a no-op trim to a real shrink.

The review tool did not expose token accounting; reviewer tokens are therefore
not available.

## Validation

- `tools\\validate_fast.bat`: passed; formatting, metadata, project filters,
  Profile/Debug builds, and `/W4` warnings clean.
- `tools\\validate_full.bat`: passed in 108.105 seconds; all CPU suites,
  135/135 doctest cases and 2,847/2,847 assertions, zero DX12 InfoQueue errors,
  matching screenshots, runtime handle smoke, and the 44,401-line varied
  physics baseline byte-exact.
- `tools\\validate_perf.bat`: completed in 35.751 seconds; zero steady-gameplay
  allocation violations, selected-ball structural proof passed, absolute DX12
  budgets passed, and no DX12 or physics-bench performance regressions.

Logs:

- `TestOutput/logs/validate_full_entity_endgame_20260711.log`
- `TestOutput/logs/validate_perf_entity_endgame_20260711.log`

No blockers remain.
