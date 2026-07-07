# Progress: Unit Test Pyramid (plan 01)

Source plan: `fable_plans/01-unit-test-pyramid-plan.md`
Status: in progress
Last updated: 2026-07-07

## How to work this file

- Do items in order; one checkbox = one verifiable action; tick only with the
  named evidence pasted under the box. `[B]` + reason if blocked twice.
- Comment quality gate applies to touched source; test files also get learning
  headers (they are source-bearing).

## Verified facts (do not re-derive)

- Build: single project `SKULLBONEZ_CORE.vcxproj`, GUID
  `{92972446-7D18-4AD1-AE43-15671C767306}`, toolset **v145** (note: AGENTS.md
  says v143 — the vcxproj is authoritative), `WarningLevel Level4`,
  `TreatWarningAsError false`, `LanguageStandard stdcpp17`, static CRT
  (`MultiThreadedDebug` / `MultiThreaded`), defines `_HAS_STD_BYTE=0` always;
  Debug adds `_DEBUG;SKULLBONEZ_PROFILE_ENABLED;SKULLBONEZ_PLATFORM_PROFILER_PIX;USE_PIX`,
  Release/Profile add `NDEBUG` (Profile also PROFILE/PIX defines). No
  `FloatingPointModel` element → default `/fp:precise` (determinism tests must
  keep this default).
- Solution: one project entry —
  `Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "SKULLBONEZ_CORE", "SKULLBONEZ_CORE.vcxproj", "{92972446-...}"`.
  A test project is added by imitating this line plus the per-config mapping
  block in GlobalSection(ProjectConfigurationPlatforms).
- `tools\validate_fast.bat` steps: [1] validate_format, [2]
  validate_project_filters, [3] validate_runtime_boundaries, [4]
  validate_build Profile, then validate_ready_builds. Tests slot in as a new
  step after [4] (they need a built test binary).
- `SkullbonezSource/ThirdParty/` does NOT exist yet — create it for the
  vendored doctest header.
  Execution note superseding this setup path: doctest now lives under the
  existing `ThirdPtySource/doctest/` tree, not `SkullbonezSource/ThirdParty/`,
  because `tools/validate_project_filters.py` treats every
  `SkullbonezSource/*.h` file as production-source project inventory.
- Determinism inputs: `PHYSICS_FIXED_DT = 1.0f / 120.0f`
  (Core/Common.h, anchor `PHYSICS_FIXED_DT`). Snapshot API:
  `PhysicsEngine::CaptureReplaySolverSnapshot( Basics::ReplaySolverWorldSnapshot&, int modelCount )`
  / `RestoreReplaySolverSnapshot(...)` (PhysicsEngine.h:184-185). Step API:
  `PhysicsEngine::Step( fixedDt, config, worldForces, workerPool, diagnosticNames, diagnosticNameCount )`
  (call site anchor: RunReplayTools.cpp `physicsEngine.Step(`).
- Handle APIs for store tests: `PhysicsBodyStore::HandleForModelIndex` /
  `ModelIndexForHandle` / `ResolveHandleForModelIndex`
  (PhysicsBodyStore.h:171/175/211); same trio on ColliderStore
  (ColliderStore.h:104/113/121); handle structs in PhysicsHandles.h:37/48
  (`index` + `generation`, `IsValid()`).

## Phase 0 — harness bootstrap

- [x] H1. Vendor doctest: create
  `ThirdPtySource/doctest/doctest.h` from the official
  single-header release (latest 2.4.x). Add a `ThirdPtySource/README.md` noting
  version + source URL + license (MIT). No other files.
- [x] H2. Create `SkullbonezTests/` at repo root with `TestMain.cpp`:
  ```cpp
  #define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
  #include "../ThirdPtySource/doctest/doctest.h"
  ```
  and one smoke file `TestSmoke.cpp`:
  ```cpp
  #include "../ThirdPtySource/doctest/doctest.h"
  TEST_CASE( "smoke: harness runs" ) { CHECK( 1 + 1 == 2 ); }
  ```
- [x] H3. Create `SKULLBONEZ_TESTS.vcxproj` by copying the shell of
  SKULLBONEZ_CORE.vcxproj and editing: new GUID (generate one), Application
  type (console: `<SubsystemType>Console` in Link settings /
  `<ConfigurationType>Application`), SAME toolset v145 / Level4 / stdcpp17 /
  CRT settings / `_HAS_STD_BYTE=0` / default fp — copy the ItemDefinitionGroup
  blocks verbatim, then strip PIX/profiler defines if they drag link deps
  (discovery: try with them first; remove only on link failure, and note
  which). ItemGroup lists TestMain.cpp + TestSmoke.cpp only at this step.
  Matching `SKULLBONEZ_TESTS.vcxproj.filters` (project-filter validation is a
  gate — imitate the CORE filters file structure).
- [x] H4. Add the project to SKULLBONEZ_CORE.sln (Project line + config
  mappings for Debug|x64, Profile|x64, Release|x64 imitating the existing
  block). Evidence: `msbuild SKULLBONEZ_CORE.sln /p:Configuration=Profile /p:Platform=x64`
  builds both projects, 0 warnings.
- [x] H5. Create `tools\validate_tests.bat` (imitate validate_build.bat
  header comment style): build SKULLBONEZ_TESTS Profile x64, run
  `Profile\SKULLBONEZ_TESTS.exe --duration=true`, exit nonzero on failure.
  Evidence: run it; smoke test green in under 10 seconds.
- [x] H6. Wire into `tools\validate_fast.bat` as step [5/5] after the Profile
  build (renumber the echo labels). Evidence: `tools\validate_fast.bat`
  passes end-to-end.
- [x] H7. Update AGENTS.md validation tables: row "Unit tests only" →
  `tools\validate_tests.bat`; note validate_fast now includes tests.
  Gate for the whole phase: `tools\validate_fast.bat`. Commit.

Execution evidence: phase 0 used vendored doctest v2.4.12 from
`ThirdPtySource/doctest/doctest.h`, `SKULLBONEZ_TESTS` GUID
`{4EA1011B-106E-4BC2-B328-E873367F4E42}`, `DOCTEST_CONFIG_USE_STD_HEADERS`
for current MSVC, and `DOCTEST_CONFIG_NO_RTTI` to match `/GR-`.
`tools\validate_tests.bat` passed in 5.192s with 1 doctest case / 1 assertion
passed. `tools\validate_fast.bat` passed in 34.166s with format, project
filters, runtime boundaries, Profile and Debug solution builds, ready builds,
and unit tests all green with 0 warnings/errors. A takeover rerun of
`tools\validate_fast.bat` also passed in 31.346s before commit. Logs:
`Agentic/Reports/2026-07-07/logs/fable-01-validate-tests.log`,
`Agentic/Reports/2026-07-07/logs/fable-01-validate-fast.log`, and
`Agentic/Reports/2026-07-07/logs/fable-01-validate-fast-rerun.log`.

## Phase 1 — pure math tests (no engine deps)

For each item: add the source file(s) under test to SKULLBONEZ_TESTS's
ItemGroup (compile-in; plan 04's Maths lib later replaces this with a project
reference), write the test file under `SkullbonezTests/`, run
`validate_tests`. DISCOVERY first per file: `rg -n "Cfg\(|Gfx\(|::Instance"
SkullbonezSource/Maths/<file>.cpp` must return zero hits; if not, `[B]` the
item against plan 02 and pick the next.

- [x] M1. `TestVector3.cpp` — Maths/Vector3: normalize of zero vector
  (document actual behavior — Vector3.cpp contains 5 `throw` sites; assert
  the throwing contract as it exists), dot/cross identities, magnitude vs
  magnitudeSquared consistency.

  Evidence: discovery `rg -n "Cfg\(|Gfx\(|::Instance"
  SkullbonezSource/Maths/Vector3.cpp` returned no hits. Added
  `SkullbonezTests/TestVector3.cpp` and compiled
  `SkullbonezSource/Maths/Vector3.cpp` into `SKULLBONEZ_TESTS`. The tests lock
  zero-vector `Normalise()` throwing, non-zero normalization, dot/cross basis
  identities, and `VectorMag`/`VectorMagSquared` consistency. Gate:
  `tools\validate_tests.bat` passed in 4.049s with 5 doctest cases and
  16 assertions all passing.
- [x] M2. `TestQuaternion.cpp` — Maths/Quaternion: Normalise() idempotence,
  axis-angle round-trip, slerp endpoints (t=0/t=1 exact), renormalization
  drift under repeated multiply (bound the error).

  Evidence: discovery `rg -n "Cfg\(|Gfx\(|::Instance"
  SkullbonezSource/Maths/Quaternion.cpp` returned no hits. Because Quaternion
  links through orientation-matrix code, the same discovery check on
  `SkullbonezSource/Maths/RotationMatrix.cpp` also returned no hits. Added
  `SkullbonezTests/TestQuaternion.cpp` and compiled
  `SkullbonezSource/Maths/Quaternion.cpp` plus
  `SkullbonezSource/Maths/RotationMatrix.cpp` into `SKULLBONEZ_TESTS`. Tests
  lock non-zero `Normalise()` idempotence, zero-quaternion reset to identity,
  axis-angle component sign convention and round-trip behavior, and bounded
  repeated-multiply drift followed by deterministic renormalization. Discovery
  also found that the current Quaternion API has no Slerp endpoint; that
  checklist subcase is not applicable until a Slerp API is introduced. Gate:
  `tools\validate_tests.bat` passed in 4.121s with 9 doctest cases and
  35 assertions all passing.
- [x] M3. `TestMatrix4.cpp` — inverse(identity)==identity, TRS compose vs
  manual, inverse(M)*M ≈ identity within epsilon.

  Evidence: CodeGraph mapped Matrix4 as currently uncovered and identified the
  default constructor, `Translate`, `Scale`, `RotateAxis`, `operator*`,
  `Data`, and `Inverse` as the pure-math surface to cover. Discovery
  `rg -n "Cfg\(|Gfx\(|::Instance" SkullbonezSource/Maths/Matrix4.cpp`
  returned no hits. Added `SkullbonezTests/TestMatrix4.cpp` and compiled
  `SkullbonezSource/Maths/Matrix4.cpp` into `SKULLBONEZ_TESTS`. Tests lock
  `Inverse()` of identity, TRS composition against manual column-major values,
  `Data()` aliasing the public matrix storage, and `original.Inverse() *
  original` returning identity within epsilon. Gate:
  `tools\validate_tests.bat` passed in 3.834s with 12 doctest cases and
  86 assertions all passing.
- [x] M4. `TestGeometricMath.cpp` — ray/sphere hit+miss+tangent, ray/box
  face/edge cases, the degenerate inputs near the file's 3 throw sites.
  Commit phase (gate: `validate_tests` + `validate_fast`).

  Evidence: CodeGraph mapped the current `GeometricMath` API as plane,
  triangle, and ray-segment helpers with no covering tests. Discovery
  `rg -n "Cfg\(|Gfx\(|::Instance" SkullbonezSource/Maths/GeometricMath.cpp`
  returned no hits. A focused source search found no public ray-sphere or
  ray-box helper under `SkullbonezSource/Maths`; those helpers currently live
  under runtime/physics surfaces, outside this pure-math phase. Added
  `SkullbonezTests/TestGeometricMath.cpp` and compiled
  `SkullbonezSource/Maths/GeometricMath.cpp` into `SKULLBONEZ_TESTS`. Tests
  lock plane construction, sloped-plane height, ray-plane hit/miss/boundary
  times, `NO_COLLISION` sentinel behavior, zero-normal throws,
  out-of-segment intersection throws, and collinear-triangle throws. The
  private barycentric throw site remains inaccessible through the public API
  without a test-only access hack, so it is recorded here rather than forced.
  Gate: `tools\validate_tests.bat` passed in 3.812s with 15 doctest cases and
  105 assertions all passing.

## Phase 2 — physics primitives and stores

- [x] S1. DISCOVERY: list what PhysicsBodyStore/ColliderStore need for
  standalone construction: `rg -n "class PhysicsBodyStore" -A 30
  SkullbonezSource/Physics/PhysicsBodyStore.h` — record ctor + row-add API
  here. If rows can only be created through GameModelCollection append, `[B]`
  store tests against authoritative-plan-02 and test only the pure query
  paths reachable from a default-constructed store.

  Evidence: CodeGraph mapped `PhysicsBodyStore` and `ColliderStore` creation,
  destruction, lookup, and handle-model-index paths with no covering tests.
  Confirmed public standalone construction is available:
  `PhysicsBodyStore::PhysicsBodyStore()`, `CreateBodyRecord( const
  PhysicsBodyRecord& )`, `CreateBodyRecord( const PhysicsBodyCreateDesc&, bool
  sleepEnabled )`, `DestroyBodyRecord( PhysicsBodyHandle )`,
  `HandleForModelIndex`, `ModelIndexForHandle`, `Contains`, `RecordForHandle`,
  and row accessors in `SkullbonezSource/Physics/PhysicsBodyStore.h`.
  Confirmed collider standalone construction is available:
  `ColliderStore::ColliderStore()`, `CreateColliderRecord( const
  ColliderRecord& )`, `DestroyColliderRecord( PhysicsColliderHandle )`,
  `HandleForModelIndex`, `HandleForBodyHandle`, `HandleForSceneObjectId`,
  `ModelIndexForHandle`, `Contains`, and `RecordForHandle` in
  `SkullbonezSource/Physics/ColliderStore.h`. Handles are
  index/generation pairs from `PhysicsHandles.h`; deletes close dense rows and
  increment handle generations. Store tests do not need to route through
  `GameModelCollection`.
- [x] S2. `TestBounds.cpp` — BoundingSphere/BoundingBox overlap/containment
  truth tables including touching-surface cases.

  Evidence: CodeGraph mapped `BoundingSphere` and `BoundingBox` as broadphase
  swept collision helpers rather than exact containment predicates. Discovery
  `rg -n "Cfg\(|Gfx\(|::Instance"
  SkullbonezSource/Physics/BoundingSphere.cpp
  SkullbonezSource/Physics/BoundingBox.cpp` returned no hits. Added
  `SkullbonezTests/TestBounds.cpp` and compiled `BoundingSphere.cpp`,
  `BoundingBox.cpp`, and required `ConvexHullShape.cpp` into
  `SKULLBONEZ_TESTS`; the hull source is needed because the bounds translation
  units define convex-hull overloads that reference hull accessors. Tests lock
  sphere swept hit/miss/tangent times, sphere static overlap/touching returning
  `NO_COLLISION`, box broadphase bounding-radius touching/miss/sweep behavior,
  and sphere-box symmetry for a shared setup. Gate:
  `tools\validate_tests.bat` passed in 3.748s with 19 doctest cases and
  115 assertions all passing after fixing project filters and adding the real
  hull dependency.
- [x] S3. `TestSpatialGrid.cpp` — insert/query round-trip, cell-boundary
  straddling, remove-then-query emptiness. (SpatialGrid.cpp has 13 invariant
  throws — trigger none; they become SB_FATAL under plan 05.)

  Evidence: CodeGraph mapped `SpatialGrid` as an uncovered broadphase cell
  index with fixed internal arrays and caller-owned pair output. Discovery
  `rg -n "Cfg\(|Gfx\(|::Instance" SkullbonezSource/Physics/SpatialGrid.cpp`
  returned no hits. Added `SkullbonezTests/TestSpatialGrid.cpp` and compiled
  `SkullbonezSource/Physics/SpatialGrid.cpp` into `SKULLBONEZ_TESTS`. Tests
  lock insert/query round-trip, pair deduplication, cell-boundary straddling,
  swept insertion into a later cell, and `Clear()`-then-query emptiness. The
  old "remove" wording maps to `Clear()` because the current public API has no
  per-object remove. The first run stack-overflowed because `SpatialGrid` owns
  large fixed arrays; the test fixture now uses static storage and resets by
  `Clear()`/`SetCellSize()`. Gate: `tools\validate_tests.bat` passed in
  3.710s with 23 doctest cases and 128 assertions all passing.
- [x] S4. `TestPhysicsHandles.cpp` — store handle semantics: fresh handle
  IsValid; ModelIndexForHandle(HandleForModelIndex(i)) == i for live rows;
  stale generation rejected after row removal (per S1 discovery);
  ResolveHandleForModelIndex hint fast path == slow path result.

  Evidence: CodeGraph mapped `PhysicsBodyStore` and `ColliderStore` handle
  creation/destruction paths, dense-row movement, replay-id lookup, and
  collider body/scene lookup as uncovered store behavior. Discovery
  `rg -n "Cfg\(|Gfx\(|::Instance"
  SkullbonezSource/Physics/PhysicsBodyStore.cpp
  SkullbonezSource/Physics/ColliderStore.cpp` returned no hits. Added
  `SkullbonezTests/TestPhysicsHandles.cpp` and compiled
  `PhysicsBodyStore.cpp` plus `ColliderStore.cpp` into `SKULLBONEZ_TESTS`.
  Tests lock fresh body handles, model-index/handle inverse lookup,
  replay-id lookup with both good and stale hints, body deletion dense-row
  movement, stale body generation rejection, collider lookup by body handle and
  scene object id, collider deletion dense-row movement, and stale collider
  generation rejection. `PhysicsBodyStore.cpp` also contains uncalled terrain
  integration helpers, so the unit harness adds
  `SkullbonezTests/TestTerrainLinkStubs.cpp`: a test-only Terrain stub that
  satisfies link-time references. It started as a loud throw stub for handle
  tests and was later promoted by D1-D3 into a deterministic flat-plane terrain
  fixture for the engine determinism micro-world. The first local-store run
  stack-overflowed because both stores own fixed-capacity runtime arrays; the
  final tests use static store fixtures and `Clear()` between cases. Gate:
  `tools\validate_tests.bat` passed in 4.011s with 27 doctest cases and
  174 assertions all passing.
- [x] S5. `TestConvexHull.cpp` — load a committed baked hull from
  `SkullbonezData/hulls/` (pick the smallest), assert face/edge/mass/inertia
  invariants the bake guarantees (ConvexHullShape.cpp's 41 validation throws
  document the invariants — mirror 5-8 of them as CHECKs on good data).
  Commit phase (gate: `validate_tests`).

  Evidence: CodeGraph mapped `ConvexHullShape::LoadFromFile`, baked topology
  getters, and mass/inertia getters as uncovered. Discovery
  `rg -n "Cfg\(|Gfx\(|::Instance" SkullbonezSource/Physics/ConvexHullShape.cpp`
  returned no hits. The smallest committed baked hull is
  `SkullbonezData/hulls/pyramid.hull` (1,150 bytes). Added
  `SkullbonezTests/TestConvexHull.cpp`; `ConvexHullShape.cpp` was already
  compiled into `SKULLBONEZ_TESTS` from S2. Tests lock the pyramid name, vertex
  count, face count, edge count, centered vertices, base face span, face normal
  lengths, face index ranges, edge vertex/face ranges, edge endpoint adjacency
  against both faces, baked center of mass, volume, default mass, bounding
  radius, projected surface area, inertia half-extents, and
  `ComputeBoxApproxInertia()`. Phase 2 physics primitive/store coverage is now
  complete. Gate: `tools\validate_tests.bat` passed in 3.744s with 30 doctest
  cases and 305 assertions all passing.

## Phase 3 — engine-adjacent units

- [x] E1. `TestReserveAllocator.cpp` — RuntimeReserveAllocator: RegisterOwner
  + RequestGrowth under cap grants; over-cap denies; growth counting.
  DISCOVERY: `rg -n "RegisterOwner|RequestGrowth" SkullbonezSource/Runtime/Allocation`
  — record signatures + any global state that needs reset between TEST_CASEs
  (if the allocator is a process-global registry, use unique owner names per
  case and note the constraint here).

  Evidence: CodeGraph mapped `RuntimeReserveAllocator::RegisterOwner`,
  `RequestGrowth`, `ResetCounters`, `CopyRecentGrowthEvents`,
  `GrowthEventCount`, policy-violation counters, and
  `RuntimeReserveGrowthScope` as the public test surface. Discovery
  `rg -n "RegisterOwner|RequestGrowth" SkullbonezSource/Runtime/Allocation`
  confirmed registration persists in fixed process-global owner storage while
  `ResetCounters()` clears owner counters, policy violations, and recent growth
  events. Added `SkullbonezTests/TestReserveAllocator.cpp` and compiled
  `SkullbonezSource/Runtime/Allocation/RuntimeReserveAllocator.cpp` into
  `SKULLBONEZ_TESTS`. Tests use unique owner names per case and lock replay
  growth grants under cap, event byte accounting, replay-growth scope approval,
  over-cap denial, policy violation counting, growth-count limit denial, event
  reason fields, and reset-without-unregistering-owner behavior. The first gate
  failed in project-filter validation because runtime allocation source belongs
  under `Source Files\Runtime\Allocation`; the filter was corrected. Final gate:
  `tools\validate_tests.bat` passed in 3.798s with 35 doctest cases and
  364 assertions all passing.
- [x] E2. `TestReplayRecorder.cpp` — ring-buffer wrap: fill past capacity,
  assert oldest overwritten + cursor restore matches
  (RunRayCastTestState.MAX_LINES pattern documents the ABI expectation;
  target the ReplayRecorder ring API — DISCOVERY: `rg -n "class ReplayRecorder" -A 40 SkullbonezSource/Runtime/Replay/ReplayRecorder.h`).

  Evidence: CodeGraph and focused source reads mapped `ReplayRecorder` as the
  presentation ring, with `Configure`, `ResetTimeline`,
  `CaptureFrameFromSolverSample`, `CopySamplesChronological`,
  `SampleAtNormalized`, `LatestSample`, and `GetStats` as the clean standalone
  surface. Discovery `rg -n "Cfg\(|Gfx\(|::Instance"
  SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp
  SkullbonezSource/Runtime/Replay/ReplayRecorder.h` returned no hits. Added
  `SkullbonezTests/TestReplayRecorder.cpp`, compiled `ReplayRecorder.cpp` into
  `SKULLBONEZ_TESTS`, and added `TestReplayRecorderLinkStubs.cpp` because the
  same translation unit also contains uncalled full-capture functions that link
  to camera/world/model/collection owners. Tests capture synthetic solver samples
  through `CaptureFrameFromSolverSample()` so they exercise the real
  presentation ring without live runtime owners. They lock retention capacity,
  oldest-frame eviction after wrap, chronological copy order, event cursor
  retention, latest sample, normalized scrub lookup, stats, and
  `ResetTimeline()` clearing samples/cursors while preserving capacity. The
  first gate failed at link on the uncalled full-capture owner hooks; the loud
  link stubs now throw if a focused ring test crosses that boundary. Final
  gate: `tools\validate_tests.bat` passed in 4.335s with 38 doctest cases and
  397 assertions all passing.
- [x] E3. `TestSceneParser.cpp` — smallest committed scene from
  `SkullbonezData/scenes/` parses ok; malformed JSON returns error (currently
  throws — assert the current contract; plan 05 converts it to SbResult and
  this test updates in the same commit).
  Commit phase (gate: `validate_tests`).

  Evidence: CodeGraph mapped `TestScene::LoadFromFile` through
  `LoadTestSceneFromFileImpl` into `TestSceneParser::LoadScene`. Discovery
  `rg -n "Cfg\(|Gfx\(|::Instance" SkullbonezSource/Scene/TestScene.cpp
  SkullbonezSource/Scene/TestSceneParser.cpp SkullbonezSource/Scene/TestScene.h`
  returned no hits. The smallest committed scene is
  `SkullbonezData/scenes/terrain_compare.scene.json` at 525 bytes. Added
  `SkullbonezTests/TestSceneParserUnit.cpp`, compiled `TestScene.cpp` and
  `TestSceneParser.cpp` into `SKULLBONEZ_TESTS`, and covered successful parsing
  of the `main` camera, disabled physics/text flags, water-hidden debug flag,
  screenshot frame/path, and empty body collections. The unit creates a
  temporary malformed scene file and asserts the current `std::runtime_error`
  message contains `Invalid JSON`, the path, and `TestScene::LoadFromFile`.
  The test file uses the `TestSceneParserUnit.cpp` name because MSVC object
  output collides by basename with the production parser TU. Added
  `TestSceneParserLinkStubs.cpp` as a loud test-only stub for uncalled
  `AssetSystem` asset-library lookup. The first gate failed on that basename
  collision and the uncalled asset-system symbol; final gate:
  `tools\validate_tests.bat` passed in 4.120s with 40 doctest cases and
  417 assertions all passing.

## Phase 4 — fast determinism property

- [x] D1. DISCOVERY: what a minimal PhysicsEngine needs — construct engine,
  add 3-5 body/collider rows (per S1 findings), default EngineConfig +
  PhysicsWorldForces, a WorkerPool (or the serial path — record how
  `REPLAY_PREDICTION_PARALLEL_BODY_MIN` gates worker use). If engine
  construction requires collection/scene plumbing, `[B]` against
  authoritative-plan-02/fable-03 P1 and stop the phase.
- [x] D2. `TestDeterminism.cpp` — step the micro-world 240 ticks twice from
  identical initial state (two engine instances), byte-compare body store
  poses/velocities each 60 ticks. Must run in milliseconds.
- [x] D3. Snapshot losslessness: capture ReplaySolverWorldSnapshot + body
  state mid-run, step 60 ticks, restore, re-step 60 ticks, compare against
  uninterrupted run — byte-equal. (This is the invariant fable-plan-03 leans
  on; cite this test from that plan's P2.7 evidence.)
  Commit phase (gate: `validate_tests` + `tools\validate_physics.bat` to
  prove the harness itself changed no engine behavior).

  Evidence: CodeGraph mapped `PhysicsEngine::Step`,
  `RegisterAuthoredBody`, `RegisterAuthoredCollider`, solver snapshot capture/
  restore, and replay body-state restore as the minimal engine path. The
  micro-world uses two default-constructed `PhysicsEngine` instances, three
  authored dynamic sphere bodies/colliders, explicit deterministic
  `EngineConfig::Instance()` settings, `PhysicsWorldForces`, `SetSleepEnabled(false)`,
  a local default `WorkerPool`, and a test-only flat Terrain fixture. Worker
  fan-out stays disabled because the three-body world sits far below the
  physics/replay parallel thresholds, including
  `REPLAY_PREDICTION_PARALLEL_BODY_MIN = 2048`.

  Added `SkullbonezTests/TestDeterminism.cpp`, promoted
  `TestTerrainLinkStubs.cpp` into a flat-plane query fixture, added
  `TestDiagnosticsLinkStubs.cpp` for Debug-only uncalled diagnostics symbols,
  and compiled the real `PhysicsEngine`/`PhysicsScene`/`PhysicsWorld`/solver
  dependencies into `SKULLBONEZ_TESTS`. The first unit gate failed because the
  engine step reached terrain with a null body terrain pointer; the fixture now
  passes the flat Terrain pointer through the authored body descriptor. The
  first physics gate failed while Debug linked uncalled diagnostics methods; the
  diagnostics link stubs keep those cold paths out of the unit harness.

  Tests lock 240 fixed ticks from identical initial state with byte-exact
  body-store pose/velocity comparisons every 60 ticks. The snapshot test steps
  120 ticks, captures `ReplaySolverWorldSnapshot` plus body replay state, steps
  both paths 60 ticks, restores solver and body state, re-steps 60 ticks, and
  byte-compares against the uninterrupted engine. Comment audit covered all
  touched source-bearing test/stub files. Final gates:
  `tools\validate_tests.bat` passed in 5.203s with 42 doctest cases and
  527 assertions all passing; `tools\validate_physics.bat` passed in 15.907s
  with `physics_regression_solver.csv` byte-exact against the 20,001-line
  baseline.

## Closure

- [x] Z1. AGENTS.md: add "bug fixes in covered subsystems add a regression
  test in the same commit" to the review section.
- [x] Z2. Update `fable_plans/01-unit-test-pyramid-plan.md` status + this
  file; record test count + runtime seconds here.
