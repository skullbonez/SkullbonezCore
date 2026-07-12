# Determinism Contract Hardening

Date: 2026-07-12
Status: Not started — 0/4 phases complete
Impact area: build configuration (all vcxproj), worker pool chunking, physics
determinism documentation
Owner: physics/validation
Priority: Must do (2026-07-12 adversarial review)

## Problem And Evidence (measured 2026-07-12)

Byte-exact physics baselines are the repository's validation contract, but two
inputs to that contract are implicit rather than pinned:

- No `FloatingPointModel` element exists in any of the four vcxproj files
  (verified by grep across `*.vcxproj` on 2026-07-12). Builds ride MSVC's
  default `/fp:precise`. A future "enable /fp:fast" edit or a toolset bump that
  changes auto-vectorization would silently invalidate every physics CSV
  baseline with no build-file diff explaining why.
- `WorkerPool::MakeChunks` / `BuildChunkRangesNoAlloc`
  (`SkullbonezSource/Core/WorkerPool.h:93-101`) derive chunk boundaries from
  worker thread count. Ordered merge (`ParallelCollectOrdered`, `:112-120`)
  makes output order deterministic, but any per-chunk floating-point
  accumulation makes values a function of core count. The parallel narrowphase
  itself is safe (per-pair event slots committed serially in pair order,
  `SkullbonezSource/Physics/PhysicsWorld.cpp:3654-3699`), but the repository
  has no audited statement that *no* chunked path accumulates across chunk
  boundaries, and no pinned validation thread count if one does.

## Goal

The determinism envelope is explicit: floating-point model pinned in the build
files, every thread-count-sensitive accumulation either proven absent or
neutralized, and the resulting contract documented where baseline regeneration
instructions live.

## Non-Goals

- No cross-compiler or cross-vendor determinism guarantee; the contract remains
  MSVC v143 x64.
- No solver or integrator behavior changes; baselines must remain byte-exact
  through this plan.

## Phases

- [ ] **D1 — Pin the floating-point model.** Add explicit
  `<FloatingPointModel>Precise</FloatingPointModel>` to every configuration of
  `SKULLBONEZ_CORE`, `SKULLBONEZ_MATHS`, `SKULLBONEZ_PHYSICS`, and
  `SKULLBONEZ_TESTS`. Acceptance: a clean Profile and Debug build, then
  `tools\validate_physics.bat` byte-exact against the committed baseline —
  proving the pin encodes the status quo rather than changing it.
- [ ] **D2 — Chunk-accumulation audit.** Inventory every
  `ParallelFor*`/`ParallelForChunks*`/`ParallelCollectOrdered` call site and
  classify: per-item independent (safe), per-chunk output merged by
  order-stable concatenation (safe), or per-chunk numeric accumulation
  (thread-count-sensitive). For any site in the third class, either restructure
  to per-item staging with serial reduction or record a binding decision that
  pins the validation thread count in config. Acceptance: a dated audit table
  committed under `Agentic/Reports/` naming every call site and its class.
- [ ] **D3 — Document the contract.** Record in `AGENTS.md` (or the physics
  reference the baseline instructions live in): pinned FP model, toolset
  scope, thread-count sensitivity result from D2, and the rule that changing
  any of these requires regenerating baselines in the same commit.
  Acceptance: baseline-refresh instructions name every determinism input.
- [ ] **D4 — Isolation gate.** Land this plan in a commit window with no other
  physics-adjacent change in flight, then run `tools\validate_physics.bat` and
  `tools\validate_physics_deep.bat` from final committed state. Acceptance:
  both gates byte-exact; if either diverges, the divergence has exactly one
  suspect and this plan halts for owner decision instead of refreshing
  baselines.

## Dependencies And Decisions

- Must not run concurrently with any other plan that touches physics, config
  defaults, or baselines (D4 isolation requirement).
- Open decision for D2: if a thread-count-sensitive site is found and
  restructuring is expensive, the owner chooses between pinning validation
  thread count in `engine.cfg` versus restructuring; the decision is recorded
  as binding in MASTER-PLAN.

## Acceptance

FP model pinned in all four projects, D2 audit table committed, contract
documented, and both physics gates byte-exact from final state.

## Validation

`tools\validate_physics.bat` (mandatory), `tools\validate_physics_deep.bat`
(D4), plus `tools\validate_tests.bat` if any test project file changes. Build
config touches all projects, so finish with `tools\validate_full.bat` if any
non-physics divergence is suspected.
