# EngineConfig Decomposition

Date: 2026-07-11
Status: Not started — 0%
Impact area: `Core/Config.h`/`Config.cpp`, every config consumer, engine.cfg
Origin: 2026-07-11 architecture gap review. `EngineConfig` is the largest
remaining catch-all bag: window setup, asset paths, frustum, physics
defaults, audio, and art direction in one flat class. Partial decomposition
already exists (`WindowConfig`, `ContactAudioConfig`, `SceneLightConfig`,
`OrdinaryRenderConfig`, `CinematicRenderConfig`); this plan finishes the job
so every field lives in a domain struct with a named owner, per the repo's
own domain-nouns-over-bags migration rule.

## Scope decisions (binding)

- **Same file format.** `engine.cfg` keys keep parsing exactly as today;
  this is an in-memory structure change plus parser table cleanup, not a
  config file migration (that belongs to `TODO/data-format-versioning.md`
  if cfg versioning is wanted).
- **Physics-default fields move in their own isolated commits** gated by
  `validate_physics` (byte-exact CSV) — gravity/fluid/drag/friction/sleep/
  solver/broadphase values are determinism-sensitive per `AGENTS.md`.
- **No global re-plumbing.** Consumers that already receive `EngineConfig&`
  keep receiving it; the win is that they read `config.physicsDefaults.x`
  instead of a flat field, and new systems can accept only their domain
  struct. Narrowing constructor signatures to domain structs is encouraged
  per touched owner but not a bulk sweep.
- **Table-driven parsing.** Key → field bindings become a declarative table
  per domain struct, replacing long if/else key matching, so adding a field
  is one row. `std::string` asset-path members stay (loaded pre-gameplay,
  cold path) unless the allocation checker objects.
- Coordinate with `TODO/dx12-post-final-cleanup.md` Phase 5 (shadow block
  dedupe + sun-field rename): if that phase lands first, fold its structs
  into this inventory; if this plan lands first, Phase 5 collapses to a
  rename.

## Phases

- [ ] E1. Inventory: table in this plan listing every remaining flat
      `EngineConfig` field → target domain struct → owner → validation gate.
      Expected domains: `AssetPathsConfig`, `FrustumConfig` (or fold into
      camera), `PhysicsDefaultsConfig`, `RuntimeFlagsConfig`; extend the
      existing structs where a domain already exists. Documentation-only.
- [ ] E2. Non-physics domains move (asset paths, frustum, runtime flags,
      remaining render odds and ends), one commit per domain, parser table
      updated in the same commit. Gate: `validate_fast`; `validate_dx12_renderer`
      if any render-consumed field moves.
- [ ] E3. Physics defaults move, isolated commit(s). Gate:
      `validate_physics` byte-exact per commit.
- [ ] E4. Parser cleanup: single declarative key-binding table per domain;
      unknown-key handling unchanged; `Dump()` regenerated from the same
      table so dump and parse cannot drift. Gate: `validate_fast` +
      `validate_full` (Config.h is broad scope in the validation map).
- [ ] E5. Closure: no flat data field remains directly on `EngineConfig`
      (accessors/structs only); comment audit; rubber-duck review;
      `validate_full`; MASTER-PLAN/SessionState update; delete plan.

## Acceptance

- [ ] Every `EngineConfig` field lives in a named domain struct with an
      owner comment; the class body is composition only.
- [ ] `engine.cfg` from before the plan parses identically (prove with a
      `Dump()` diff before/after on the committed cfg).
- [ ] `physics_regression_solver.csv` byte-exact after every
      physics-adjacent commit.
- [ ] Parse and dump are generated from one table per domain.

## Validation map

| Slice | Gate |
|-------|------|
| Non-physics domain moves | `validate_fast` (+ `validate_dx12_renderer` for render fields) |
| Physics-default moves | `validate_physics` (byte-exact CSV) |
| Parser table rework / final | `validate_full` (Config.h broad-scope row) |
