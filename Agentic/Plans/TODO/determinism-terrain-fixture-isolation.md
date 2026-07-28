# Determinism Terrain Fixture Isolation

Date: 2026-07-28
Status: ACTIVE — 2/3 phases complete
Impact area: Physics tests, startup probe terrain lifetime, test order independence
Owner: Tests + Physics terrain
Priority: Medium

## Problem And Evidence

`SkullbonezTests/TestDeterminism.cpp` exposes `FlatTestTerrain()` and
`DeepSpaceTestTerrain()` as mutable function-local statics shared by test cases.
Physics borrows each terrain view, so the long lifetime is currently convenient,
but a future terrain cache or mutation would make tests order-dependent.

## Goal

Give each test or explicit test fixture its own config/terrain lifetime while
keeping every borrowed PhysicsTerrainView valid for the engine operations that
consume it.

## Phases

- [x] **TF0 — Map terrain borrows and test lifetimes.** Identify every helper
  and test using the shared terrains, the exact view lifetime, and any cache or
  allocation behavior that fixture construction must preserve. Evidence:
  `../../Reports/2026-07-28/determinism-terrain-fixture-isolation-tf0-census.md`.
- [x] **TF1 — Introduce an owning per-test fixture.** Place config, terrain, and
  engine in honest lifetime order; pass terrain explicitly to helpers and
  remove default arguments/function statics. Do not add a generic test context
  bag—one fixture owns the terrain-borrow invariant and states it. Evidence:
  `../../Reports/2026-07-28/determinism-terrain-fixture-isolation-tf1.md`.
- [ ] **TF2 — Prove order independence.** Add repeated/reordered construction
  coverage, run the determinism suite with randomized doctest order where
  supported, audit comments, and complete Physics/broad validation.

## Acceptance

No mutable terrain object is shared implicitly across test cases, every borrowed
view has a locally provable owner lifetime, and repeated or reordered runs
produce the same byte-exact results.

## TF0 Binding Decisions

- The tracked-tree census found four shared function-local terrains and one
  terrain-bearing default argument. `TestDeterminism.cpp` reaches 18 cases
  (17 flat, one deep); `TestPhysicsHandles.cpp` and `TestTerrain.cpp` add one
  shared-terrain case each.
- Separate from the 4/55 tracked-test census,
  `Runtime/Startup/StartupProbeHarnesses.cpp` declares its lifecycle engine
  before config/terrain, so reverse destruction retires the retained-view owner
  first. TF1 reorders those existing locals to config, terrain, then heap
  engine and corrects the lifetime comment.
- There is no `TerrainConfig` type. `Terrain` retains an `EngineConfig*`, and a
  cached-heightfield `PhysicsTerrainView` can retain a span into terrain-owned
  collision cells.
- The TF1 invariant owner declares config, terrain, then heap engine so reverse
  destruction is engine, terrain, config. Multi-engine comparisons use
  independent fixtures. The prediction seed case keeps its one per-test terrain
  alive through both source and destination engine lifetimes.
- Terrain views become explicit helper inputs. The nullable/default
  `AddMutualGravityBody` terrain path is deleted because no current caller uses
  the null branch.
- Analytic test terrains populate no collision cache and allocate no terrain
  heap storage in the render-free test build. Existing heap-engine ownership
  and SceneLoad-scoped reserves remain unchanged.
- No user question is required for TF1. SoA layout and all baseline artifacts
  remain out of scope.

## TF1 Outcome

- `DeterminismTerrainFixture` owns exactly one config, analytic terrain, and
  heap engine in declaration order. It is non-copyable and non-movable, and
  exposes only its engine and terrain view.
- All 18 terrain-bearing determinism cases use per-test fixtures. Multi-engine
  comparisons use independent fixtures, including the three deep-space solar
  worlds and the three worker-count gravity worlds.
- `TestPhysicsHandles.cpp` and `TestTerrain.cpp` now use ordinary per-case
  terrain owners. The prediction case keeps its one terrain alive through both
  engine lifetimes.
- The four shared terrain statics, the terrain-bearing default argument, and
  the unused nullable/`ClearTerrainView()` helper branch are deleted.
- The startup lifecycle probe now declares config, terrain, then heap engine,
  matching the retained-view destruction contract.
- No body layout, SoA storage, baseline, golden, config, schema, or performance
  artifact changed.

## Validation

TF1 passed its focused determinism and related terrain tests, full unit suite,
Profile build, standalone lifecycle smoke, dependency graph, and all ownership
inventories. Exact formatting passes for all four touched source files; the
repository-wide format gate remains blocked only by the owner's three
uncommitted warm-start files. TF2 owns randomized/repeated order coverage,
`tools\validate_physics.bat`, and `tools\validate_full.bat`.
