# Determinism Terrain Fixture Isolation TF1

Date: 2026-07-28
Plan: `Agentic/Plans/DONE/determinism-terrain-fixture-isolation.md`
Phase: TF1
Impact area: Physics tests, startup probe terrain lifetime, test order independence

## Outcome

The shared mutable analytic terrains are gone. `TestDeterminism.cpp` now owns
each terrain-bearing engine through a narrow `DeterminismTerrainFixture`.
The fixture owns exactly one `EngineConfig`, one `Terrain`, and one heap
`PhysicsEngine` in that declaration order. Reverse destruction therefore
retires the engine that retains the terrain view before the terrain and its
configuration. Copy and move operations are deleted; the fixture exposes only
`Engine()` and `TerrainView()`.

This is an invariant owner, not a generic context or capability bag. Helpers
continue to receive their concrete engine and now also receive the existing
`PhysicsTerrainView` value explicitly. No fixture or new aggregate is passed
through the helper surface.

The related prediction-source-destruction test owns one local deep terrain
through both heap-engine lifetimes. The terrain coverage test owns ordinary
per-case config/terrain locals. The startup lifecycle probe now declares
config, terrain, then heap engine and documents the same retained-view rule.

## Deletion Proof

The four shared helpers, hidden terrain default, and nullable helper branch are
deleted. Both commands returned no rows:

```powershell
rg -n "FlatTestTerrain|DeepSpaceTestTerrain|PredictionSeedTestTerrain|FlatCoverageTerrain|Terrain\s*\*\s*terrain\s*=|ClearTerrainView\(\)" SkullbonezTests/TestDeterminism.cpp SkullbonezTests/TestPhysicsHandles.cpp SkullbonezTests/TestTerrain.cpp
rg -n "static\s+.*Terrain|static\s+Terrain" SkullbonezTests
```

## Determinism Case Mapping

All 17 flat-terrain cases use per-test fixtures:

1. `Tornado force witness preserves exact one-step body state`
2. `PhysicsEngine determinism: micro-world matches at fixed tick intervals`
3. `PhysicsEngine multithreaded determinism: contact and sleep pipeline is exact across worker counts`
4. `Tornado external-force lane is byte-exact across serial and parallel body partitions`
5. `PhysicsEngine mutual gravity: pair force is antisymmetric`
6. `PhysicsEngine mutual gravity: softening keeps near pairs finite`
7. `PhysicsEngine mutual gravity: equal-mass two-body orbit stays bounded`
8. `PhysicsEngine mutual gravity: chaotic triple is deterministic`
9. `PhysicsEngine mutual gravity: parallel pair build is exact across worker counts`
10. `PhysicsEngine mutual gravity: large fields use an exact serial fallback`
11. `PhysicsEngine mutual gravity: elastic space collision preserves closing speed`
12. `PhysicsEngine invariants: settled bodies stay within terrain penetration tolerance`
13. `PhysicsEngine invariants: fluid damping does not add kinetic energy`
14. `PhysicsEngine invariants: authored velocity wakes a sleeping body`
15. `PhysicsEngine sleep policy: quiet supported body sleeps after threshold frames`
16. `PhysicsEngine determinism: solver snapshot plus body state restores losslessly`
17. `Replay solver sample restore: recorded frame reproduces future frame`

`PhysicsEngine solar assist: same-state 120-second forecast matches live and
depends on Earth gravity` uses three independent deep-space fixtures.
The worker-count helper likewise owns separate serial, one-worker, and
four-worker flat fixtures, so equality cannot pass through shared mutable
terrain state.

## Validation

| Proof | Result |
|---|---|
| `tools\validate_build.bat Profile` | PASS in 40.8 s; zero warnings/errors |
| `Profile\SKULLBONEZ_TESTS.exe --source-file="*TestDeterminism.cpp" --no-breaks=true --duration=true` | PASS in 1.2 s; 23/23 cases, 2,384,936 assertions |
| exact prediction-source-destruction and terrain-coverage doctests | PASS in 0.2 s; 2/2 cases, 339 assertions |
| `tools\validate_tests.bat` | PASS in 27.7 s |
| final-source `Profile\SKULLBONEZ_TESTS.exe --no-breaks=true` | PASS in 14.1 s; 437/437 cases, 2,419,129 assertions |
| `Profile\SKULLBONEZ_CORE.exe --physics-standalone-smoke` | PASS in 1.5 s; lifecycle/runtime handle smoke and exact repeat hash `0x953D97A226665242` |
| `tools\validate_dependency_graph.bat` | PASS in 3.3 s; zero findings |
| authority-free aggregate inventory | PASS in 23.7 s; 85/85 gated rows ruled, zero unruled/ambiguous |
| extraction-scar inventory | PASS in 27.7 s; 1/1 current finding ruled |
| wide-signature inventory | PASS in 27.6 s; zero unruled/stale rows |
| exact four-file format pipeline | PASS; all four touched source files clean |
| `tools\validate_format.bat` | Expected external block: only `PersistentContactSolver.cpp` and `PhysicsNarrowphaseStage.cpp` need formatting, both owner warm-start files outside TF1 |

TF2 owns randomized/repeated order coverage plus the broad Physics and full
validation gates. TF1 changed no baseline, golden, config, schema, performance
artifact, body layout, or SoA storage.

## Comment Audit

The touched-source audit is 4/4 checked with zero deferred files:

- `SkullbonezTests/TestDeterminism.cpp`
- `SkullbonezTests/TestPhysicsHandles.cpp`
- `SkullbonezTests/TestTerrain.cpp`
- `SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.cpp`

The fixture and startup probe name the retained-view invariant and destruction
order next to the code. The prediction case names the two-engine lifetime. The
terrain coverage locals are direct construction with no hidden ownership rule.
All repository-relative `Related:` entries resolve.

## Owner Questions

None for TF1. The standing owner ruling retains SoA, and this phase did not
alter layout or performance-sensitive storage.

The three warm-start files remained untouched and unstaged for owner evaluation.
