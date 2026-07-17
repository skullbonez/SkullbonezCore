# Code-Level Red-Flags Remediation — Renderer Globals, Hot-Path Singletons, Friend Debt, Catch-Up Clamp, LTO Determinism

Date: 2026-07-17
Status: Active — 3/7 tasks
Branch: `nightrunner-17th-july` (owner-ratified at C0)
Impact area: `Rendering/Text.cpp`, `Core/Profiler.*`, `Core/Log.*`,
`Core/LockOrderValidator.*`, `Physics/PhysicsScene.h`,
`Physics/PhysicsEngine.*`, `Physics/SimulationSystem.cpp`, project settings
Owner: cross-cutting (per-task owner named below)

## Problem And Evidence (measured 2026-07-17 at the `main` tip, 0d77d51a4)

The 2026-07-17 hostile review's code-level findings, owner-scoped to exclude
any frame-buffering change (`FRAME_COUNT` stays 2 by explicit 2026-07-17
owner direction):

1. **Mutable file-scope statics in the text renderer.**
   `Rendering/Text.cpp:64-81` holds `static float s_batchBuf[...]`,
   `s_quadBatchBuf`, `s_batchVerts`, `s_quadBatchVerts`, and
   `static Matrix4 s_orthoProj`. Global mutable batch state makes text
   rendering single-instance, non-reentrant, and thread-hostile, and it is
   invisible to the ownership model the rest of the engine enforces.
2. **Singletons on hot paths.** `Profiler::Instance()` is resolved on every
   scope begin/end (`Core/Profiler.h:305-328`), `LockOrderValidator::Instance()`
   on every lock acquisition (`Core/LockOrderValidator.cpp:206-230`), and
   `EngineLog` uses a magic-static instance (`Core/Log.cpp:44`). These are
   hidden global dependencies of exactly the shape the runtime ownership
   rules ban elsewhere.
3. **Transitional friend facade in physics.** `Physics/PhysicsScene.h:189`
   declares `friend class PhysicsEngine` with a source comment promising to
   "remove the former transitional friend facade" — an ownership boundary
   that does not actually exist between the two central physics types.
4. **Catch-up clamp of 32 fixed steps per frame.**
   `Physics/SimulationSystem.cpp:41` allows up to 32 fixed physics ticks in
   one frame (`FIXED_STEP_TIME_SCALE_MAX_TICKS_PER_FRAME = 32`), turning any
   hitch into a large same-frame physics stall amplifier.
5. **Per-binary determinism under whole-program optimization.**
   All configurations build with `WholeProgramOptimization=true`
   (`SKULLBONEZ_CORE.vcxproj:35,51`), so unrelated code changes can reshuffle
   inlining and flip knife-edge physics branches; byte-exact CSV baselines
   are certified per binary and regress into a regeneration treadmill. The
   ff6e780e Profile-only flip diagnosed by `fp-envelope-hardening` is the
   recorded precedent.

## Goal

Renderer batch state becomes instance-owned; hot-path diagnostics lose their
per-call singleton resolution; the physics friend edge is deleted behind a
real boundary; the catch-up clamp is owner-ratified to a sane bound; and the
LTO determinism exposure is either structurally narrowed or explicitly
re-certified as accepted policy with evidence.

## Non-Goals

- **No `FRAME_COUNT` / frame-buffering change** (explicit owner exclusion).
- No profiler feature work, no new diagnostics surface.
- No physics behavior change outside the clamp task; zero baseline refresh
  everywhere except the clamp task's owner-approved path if (and only if)
  the owner selects a value that changes stall-frame behavior captured by a
  committed baseline — the default expectation is no refresh because the
  clamp only binds during abnormal hitches.

## Tasks

- [x] C0 — Census and rulings. Confirm each finding at the current tip with
  file:line evidence; owner ratifies the branch, the destination owner for
  text-batch state, the profiler access pattern (cached reference at scope
  construction vs. explicit handle plumbed through render/physics owners),
  the target clamp value (candidate: 5, with dropped-time accounting), and
  the LTO decision lane for C5 (structural narrowing vs. documented
  acceptance). Evidence: dated report under `Agentic/Reports/`.
- [x] C1 — Text renderer batch state becomes instance-owned. Move
  `s_batchBuf`/`s_quadBatchBuf`/counters/`s_orthoProj` into a fixed-capacity
  batch owner constructed at startup by the render owner the C0 ruling
  names, threaded to call sites by reference. No allocation-policy change:
  the arrays stay fixed-capacity, now as owned members. Gates:
  `validate_dx12_renderer`, then `tools\run_graphics_stress.bat 1` with
  recorded command, runtime, and exit evidence.
- [x] C2 — Hot-path singleton resolution removal. Per the C0 ruling: profiler
  scopes capture the instance once per scope (or receive a plumbed handle),
  `LockOrderValidator` lookup cost leaves the per-acquisition path in
  Profile/Release builds, and `EngineLog` access is documented as the single
  allowed cold-path magic static or converted to explicit ownership. No
  telemetry values change. Gates: `validate_perf` (hot-path change) plus
  `validate_tests`.
- [ ] C3 — Delete `friend class PhysicsEngine` from `PhysicsScene`. Give
  `PhysicsEngine` the narrow typed accessors the census shows it actually
  needs, then remove the friend edge and the transitional comment. Any
  accessor that would expose broad mutable internals is a design failure —
  the boundary moves, not the visibility. Gate: `validate_physics`
  byte-exact.
- [ ] C4 — Catch-up clamp ratification. Implement the owner-selected max
  ticks per frame with explicit dropped-time accounting and a diagnostics
  counter (hitch events must be visible, not silent). Fixed-step edge and
  replay determinism are unaffected on normal frames because the clamp binds
  only during hitches; prove it. Gates: `validate_physics` byte-exact plus
  `validate_tests` covering the clamp/drop arithmetic.
- [ ] C5 — LTO determinism exposure. Execute the C0-selected lane:
  (a) structural — evaluate disabling WPO/LTCG for the physics-owning
  project or isolating solver-critical TUs from cross-module inlining, with
  before/after perf evidence from `validate_perf` and a byte-exact
  `validate_physics` proof across two consecutive full rebuilds; or
  (b) acceptance — extend the certified-envelope documentation from
  `fp-envelope-hardening` with the WPO clause, the rebuild-stability
  evidence, and the standing treadmill cost, ratified by the owner. Either
  outcome produces a dated report under `Agentic/Reports/`.
- [ ] C6 — Independent review and closure. Independent pass confirms: no
  file-scope mutable statics remain in `Rendering/` product code, no
  per-call singleton resolution on the ratified hot paths, no friend edge in
  physics, clamp and LTO rulings recorded with evidence. Final gates:
  `validate_full` (multi-area plan) plus the DX12 stress proof for the C1
  outcome. Update MASTER-PLAN and delete this plan on closure.

## Dependencies And Decisions

- C0 rulings gate every implementation task; C4's clamp value and C5's lane
  are explicit owner decisions recorded in this plan before work starts.
- Execution order: C1 → C2 → C3 → C4 → C5 → C6 (isolated renderer fix first;
  build-system determinism work last because it invalidates perf baselines
  for anything measured after it).
- Zero-baseline-refresh binds all tasks except the narrow owner-approved C4
  path described in Non-Goals; any unexpected physics CSV diff is a revert.

Owner-ratified C0 decisions (2026-07-17): `RuntimeRenderer` owns the fixed
text batch; profiler handles are explicitly plumbed through hot runtime,
render, UI, and physics owners; `TrackedMutex` caches Debug-only validation;
`EngineLog` remains the documented cold/fatal static; the fixed-step clamp is
five ticks with explicit dropped-time facts and counters; and C5 structurally
narrows WPO/LTCG for solver-critical physics translation units. The evidence
and complete call-site census are recorded in
`Agentic/Reports/2026-07-17/code-level-red-flags-census.md`.

## Acceptance

- `grep` census shows zero mutable file-scope statics in `Rendering/`
  product TUs and zero `Instance()` calls on the ratified hot paths.
- `PhysicsScene.h` contains no friend declarations.
- Clamp behavior is tested, diagnosable, and owner-ratified.
- C5 lane closed with evidence; determinism contract documentation matches
  the actual build reality.
- All mapped gates pass from final source; `dx12_validation.txt` = 0.

## Validation

Per-task gates as listed. Multi-area closure runs `validate_full`; every
DX12-touching task carries the mandatory bounded graphics-stress run; physics
tasks are byte-exact against unchanged committed baselines.
