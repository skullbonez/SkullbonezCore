# Determinism Terrain Fixture Isolation TF0 Census

Date: 2026-07-28
Plan: `Agentic/Plans/DONE/determinism-terrain-fixture-isolation.md`
Phase: TF0
Impact area: Physics tests, startup probe terrain lifetime, test order independence

## Outcome

The tracked-test census found four mutable function-local `Terrain` objects
shared across test cases and one terrain-bearing default argument:

| Shared owner | Direct consumer | Test-case reach |
|---|---|---:|
| `FlatTestTerrain()` in `TestDeterminism.cpp` | `AddMicroBody`, `AddSupportedSleepBody`, and the default argument of `AddMutualGravityBody` | 17 |
| `DeepSpaceTestTerrain()` in `TestDeterminism.cpp` | `SeedAuthoredSolarWorld` through an explicit pointer | 1 |
| `PredictionSeedTestTerrain()` in `TestPhysicsHandles.cpp` | Replay-prediction seed/source-destruction case | 1 |
| `FlatCoverageTerrain()` in `TestTerrain.cpp` | Shape sweep/manifold coverage case | 1 |

`git ls-files` enumerated 55 tracked source-bearing paths under
`SkullbonezTests`. A tracked-tree search found no other static `Terrain` and no
other terrain pointer/reference default argument. Each shared terrain has its
own paired static `EngineConfig`. There is no `TerrainConfig` type in the
current tree: the construction and retained configuration owner is
`Core::EngineConfig`.

No current caller passes `nullptr` to `AddMutualGravityBody`, so its
`ClearTerrainView()` branch is unreachable in the present test suite. TF1 can
delete the nullable/default terrain spelling instead of preserving a
compatibility path.

The separate non-test lifecycle census found one additional ordering defect in
`Runtime/Startup/StartupProbeHarnesses.cpp`. It does not change the four-terrain
tracked-test count.

## `TestDeterminism.cpp` Reach

The flat terrain reaches these 17 cases:

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
depends on Earth gravity` is the sole deep-space terrain case. It binds the
same shared deep terrain to three engines.

The complete helper chain is:

- `AddMicroBody` -> flat terrain;
- `AddSupportedSleepBody` -> flat terrain;
- `SeedMicroWorld` -> `AddMicroBody`;
- `SeedSupportedSleepWorld` and `SeedParallelContactSleepWorld` ->
  `AddSupportedSleepBody`;
- `AddMutualGravityBody` -> default flat terrain;
- `SeedTwoBodyGravityWorld`, the chaotic-triple seed, and
  `CheckMutualGravityFieldExactAcrossWorkerCounts` -> `AddMutualGravityBody`;
- `SeedAuthoredSolarWorld` -> explicit deep terrain.

## Borrow And Destruction Contract

`Terrain` retains a `const EngineConfig*` and, for height maps, owns the
`m_cachedCollisionData` vector. `Terrain::PhysicsView()` returns a value packet
whose `cells` member is a span into that vector and whose remaining plane and
sampling fields are copied scalars.

`PhysicsEngine::SetTerrainView()` copies the packet into
`PhysicsWorld::m_terrainView`. `PhysicsEngine::Clear()` clears solver/stage
state but deliberately does not revoke that retained view. Every later
`Step()` may consume it until `ClearTerrainView()`, another `SetTerrainView()`,
or engine destruction. Replay-prediction topology seeding also copies the
source world's view, which is why the `TestPhysicsHandles` prediction engine
must die before its per-test terrain even after the source engine is reset.

The current flat and deep fixtures use analytic slopes. Their published cell
spans are empty and the planes/scalars are self-contained, so today's engine
steps do not dereference terrain storage after publication. TF1 must still
honor the uniform view contract rather than encode that implementation detail;
a future cached analytic representation must not silently reopen the lifetime
bug.

The required declaration order for the invariant owner is:

1. `EngineConfig`;
2. `Terrain`;
3. heap-owned `PhysicsEngine` instances.

C++ reverse destruction then destroys every engine first, the terrain second,
and its retained config last. A prediction destination must be declared after
the same terrain owner or be another later field of that owner. The existing
static functions happen to destroy each terrain before its paired config at
process exit, but they hide the more important per-test engine/view lifetime.

## Non-Test Lifecycle Witness

`RunPhysicsEngineLifecycleScenario()` currently declares its heap
`PhysicsEngine` first, then its local `EngineConfig`, then its local `Terrain`.
At function exit, reverse destruction therefore destroys the terrain and config
before the engine that still retains their published `PhysicsTerrainView`.
`PhysicsWorld::Clear()` does not revoke that view.

The current implicit engine destructor does not sample terrain, so the defect is
not an observed crash. It still violates the retained-view ownership contract
and would become a dangling-read hazard if engine teardown later consumed
terrain state. TF1 must reorder this production probe to config, terrain, then
heap engine and update its lifetime comment. This is a source lifecycle repair,
not another shared test fixture and not a change to the 4/55 census.

## Allocation And Cache Findings

The render-free analytic slope constructor performs scalar/plane setup only.
It does not resize `m_postData`, `m_terrainData`, or
`m_cachedCollisionData`, and the test build owns no terrain mesh or shader.
Replacing these statics with per-test analytic terrains therefore adds object
construction but no terrain heap allocation or cache population.

The engines remain heap-owned because their scene-sized stores are unsuitable
for the doctest thread stack. Existing `SceneLoad`-scoped reserve operations
remain the only fixture capacity allocations and must not move into fixed-step
execution.

The local height-map case in `TestTerrain.cpp` is already isolated. Its factory
allocates RAW bytes, posts, and one collision-cell vector, releases the RAW
bytes after construction, and destroys the heap terrain before its local
config. TF1 does not need to change that path. The other local terrain cases in
`TestCamera.cpp` and `TestTerrain.cpp`, plus the direct value-view case in
`TestPhysicsStageState.cpp`, already keep backing owners inside one test.

## Test Order And Repetition

Current callers only request `PhysicsView()` from the shared analytic terrains,
so no present mutation was found and ordinary order changes are expected to
remain byte-exact. That is not an isolation proof: the mutable objects survive
every case and every same-process repeat, and their helpers expose non-const
references. A future cache, resource rebuild, or test mutation would be
inherited by whichever case runs next.

The embedded doctest runner supports
`--order-by=rand --rand-seed=<int>`. TF2 should run at least two recorded seeds
from final source and add an in-process destroy/reconstruct witness that
alternates flat and deep terrain values. No baseline or golden refresh is
authorized.

## Binding TF1 Design

TF1 will add a narrow test-only invariant owner in
`TestDeterminism.cpp`. It owns exactly one config, one analytic terrain, and
one heap engine in the declaration order above; it is not a generic context or
parameter bag. Its documented rule is that no retained engine view can outlive
the config/terrain that published it.

Every seeding/body helper will receive the terrain view explicitly through that
owner. `AddMutualGravityBody` loses both its default argument and nullable
branch. Multi-engine comparisons use one independent fixture per engine so
they cannot accidentally prove equality through shared mutable terrain.

`TestPhysicsHandles.cpp` will use an explicit per-test owner whose terrain
outlives both the source and cloned prediction engine. `TestTerrain.cpp` will
replace `FlatCoverageTerrain()` with ordinary per-case config/terrain locals.
`StartupProbeHarnesses.cpp` will reorder the lifecycle scenario's existing
locals to config, terrain, then heap engine. No shared support header or new
production seam is needed.

No owner input is required for TF1. The standing SoA/performance ruling is
unchanged, and this phase proposes no body-layout or baseline change.

## Validation

Documentation-only census. No repository validation was required and no
baseline, golden, config, schema, or performance artifact changed.
