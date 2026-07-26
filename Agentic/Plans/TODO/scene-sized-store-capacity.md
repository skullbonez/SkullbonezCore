# Scene-Sized Store Capacity

Date: 2026-07-26
Status: IN PROGRESS — drafted from the 2026-07-26 from-source architecture
review of `nightrunner-26th-JUL-26` at tip `35f6de4e`, extended by the same-day
capacity measurement below. Registered in `MASTER-PLAN.md` on 2026-07-26 as
plan 2 of the Architecture Follow-Up Campaign Round 5. Starts after
`governance-shape-to-judgment-conversion` closes. SC0-SC5 closed 2026-07-27.
6/8 phases complete; SC6 is binding.
Impact area: `Core/SceneCapacity.h`, `Physics/PhysicsFixedList.h`,
`Physics/ColliderStore.*`, `Physics/PhysicsBodyStore.*`, every
`Physics/Stages/*` store, `Physics/CollisionShape.h`,
`Runtime/Prediction/ReplayPrediction.h`, `Core/Fence.h`
Owner: physics + core allocation
Priority: High — this is the owner's headline request and the largest single
memory reduction available in the engine. It also closes a silent
allocation-policy breach in the fixed-step path.

## Problem And Evidence (measured 2026-07-26)

### 1. Every dense physics store is sized for 8,192 bodies in every scene

`Core/SceneCapacity.h:39` fixes `MAX_SCENE_OBJECTS = 8192`. Dense stores use it
as a compile-time array bound, so a 200-body scene pays the 8,192-body
footprint. Two measured instances:

- `ColliderStore::m_colliders` (`Physics/ColliderStore.h:161`) is
  `PhysicsFixedList<ColliderRecord, 8192>`. `source-blemish-remediation` B1
  recorded `sizeof(ColliderRecord) == 7228` from the final MSVC class-layout
  report. **That single member is 7,228 x 8,192 = 59,211,776 bytes (56.5 MiB),
  resident from construction, in every scene.**
- `PhysicsBodyStore`'s hot component arrays
  (`Physics/PhysicsBodyStore.h:438-484`) are 18 `float` arrays plus 2 `uint8_t`
  arrays at 8,192 entries: 606,208 bytes (592 KiB). The cold
  `PhysicsBodyRecordList` and seven handle/index maps add the same order again.

`Runtime/Prediction/ReplayPrediction.h:250` holds a second
`std::unique_ptr<Physics::PhysicsEngine>` for isolated future simulation, so
both figures are paid twice once prediction is armed.

`DEFAULT_SCENE_OBJECT_CAPACITY = 4000` and `DEFAULT_SCENE_OBJECTS = 300` exist
(`SceneCapacity.h:38,40`) but only clamp *active* counts. They do not reduce any
store's footprint.

### 2. A sphere pays for a convex hull

`ColliderRecord::shape` (`Physics/ColliderStore.h:68`) is
`Math::CollisionDetection::CollisionShape`, a `std::variant` whose largest
alternative is `ConvexHullShape`. That type embeds four fixed inline arrays
(`Physics/ConvexHullShape.h:72-75`): 64 vertices, 96 faces, 160 edges, and
`96 * 16 = 1536` face indices. The variant is therefore hull-sized for every
row, so a scene of 200 spheres still carries 200 full hull payloads. This is the
direct cause of the 7,228-byte record and of the 56.5 MiB figure above.

### 3. The fail-loud container is applied to the stores and not to the solver

`Physics/PhysicsFixedList.h` exists specifically to enforce the Runtime Static
Allocation Policy: SC1 permits registered scene-load capacity commits, while
append overflow remains fatal with owner, runtime/compile capacity, count, and
high-water diagnostics. The originating review counted 25 members under
`Physics/`; the SC1-corrected census found **43**. The review counted 40 retained
plain `std::vector` members; SC0 found **50**, including later broadphase oracle
rows and the contact side-effect storage omitted from the review summary:

| Owner | Members | Header |
|---|---:|---|
| `PhysicsSleepController` | 18 | `Stages/PhysicsSleepController.h:134-165` |
| `PhysicsNarrowphaseStage` | 7 | `Stages/PhysicsNarrowphaseStage.h:123-129` |
| `PhysicsContactSolverStage` | 10 | `Stages/PhysicsContactSolverStage.h:123-127,148-153` |
| `PhysicsStepDiagnostics` | 3 | `Stages/PhysicsStepDiagnostics.h:52-55` |
| `PhysicsTerrainStage` | 2 | `Stages/PhysicsTerrainStage.h:91-92` |
| `PhysicsForceStage` | 2 | `Stages/PhysicsForceStage.h:72-73` |
| `PhysicsWorld` | 2 | `PhysicsWorld.h:139,163` |
| `PhysicsBroadphaseStage` | 5 | `Stages/PhysicsBroadphaseStage.h:76-91` |
| `PhysicsEngine` | 1 | `PhysicsEngine.h:237` |

Three of those are `reserve`d **inside the fixed tick**:
`PersistentContactSolver.cpp:805` (`m_persistentContacts.reserve(candidatePairs
* 4)`), `:992` (`reserve(size() + terrainRowCount)`), and `:1591`
(`m_physicsDebugContacts.reserve(m_persistentContacts.size())`). The first two
are no-ops only while combined object/terrain contact rows fit the constructor's
`8192 * 4` reserve; the Debug reserve is likewise conditional on its current
capacity. Exceed either capacity and the vector reallocates mid-solve instead of
tripping — a silent breach of
`PhysicsWorld.h:35` ("scratch capacity is established during scene load and may
not grow while fixed ticks are running") and of the Runtime Static Allocation
Policy, in the one place where the policy has no teeth.

### 4. Container and const defects carried alongside

- `PhysicsFixedList` uses `std::aligned_storage` (`:313`), deprecated in C++23.
- Its copy and move constructors/assignments (`:103-148`) walk elements with
  `push_back`, paying placement-new per entry for trivially copyable types over
  up to 8,192 slots.
- Its `iterator`/`const_iterator` (`:48-96`) are `(owner, index)` pairs, not
  pointers, so they are not contiguous iterators and the copy path pays double
  indirection per element.
- All 20 hot component arrays in `PhysicsBodyStore` are declared `mutable`
  (`PhysicsBodyStore.h:438-484`). This is unnecessary: `HotFields() const`
  (`PhysicsBodyStore.cpp:1836`) only calls `.data()`, which resolves to the
  `const T*` overload and converts to `std::span<const float>` without it. The
  keyword makes the engine's authoritative simulation state mutable through a
  `const PhysicsBodyStore&` for no reason.
- `Core/Fence.h:84` declares `using FenceHandle = std::shared_ptr<Fence>;`. One
  occurrence exists in the tree — its own definition. A `shared_ptr` alias in a
  zero-allocation engine, dead.

## Goal

Dense physics storage is sized from the loaded scene, allocated only inside a
scene-load allocation phase, and never grown during a fixed tick. Re-loading a
scene that fits the current capacity allocates nothing. `MAX_SCENE_OBJECTS`
survives as an absolute ceiling that still fails loud. A collider row costs what
its shape costs.

## Non-Goals

- **No behavior change.** The 44,401-row physics regression CSV stays
  byte-exact, and every replay artifact, visual golden, and DX12 baseline stays
  unchanged. This plan moves storage, not physics.
- No removal of `MAX_SCENE_OBJECTS`. Owner decision at registration: it remains
  the hard upper bound and the fail-loud limit. Runtime capacity lives strictly
  beneath it.
- No shrink-on-load. Capacity is monotonic within a process: if the new scene
  fits, nothing happens. Releasing capacity is out of scope and is not a
  follow-up commitment.
- No heap growth path during gameplay. Growth is admitted only in the
  scene-load allocation phase, attributed to a registered owner.
- No new context/service bag, callback, interface, or virtual dispatch while
  moving storage. The `governance-shape-to-judgment-conversion` G1 rules are the
  acceptance test.
- No change to `Replay`'s separately governed growth privilege or its
  three-owner reserve inventory.
- No SIMD, solver-algorithm, or broadphase change of any kind.

## Phases

- [x] **SC0 — Capacity census and sizing contract.**
  Inventory every dense store under `Physics/` and the prediction engine: the 43
  `PhysicsFixedList` members, the 50 `std::vector` members, and their sizing
  sites (147 `reserve`/`resize`/`assign` calls measured 2026-07-26). For each
  row record: element size, current capacity source, whether it is indexed by
  body / collider / pair / contact / island, hot-or-cold, and the exact
  scene-derived quantity that should size it. Establish which owner knows the
  final body and collider count during scene load and at what phase, and confirm
  `PhysicsEngine::ReserveAuthoredBodyCapacity` /
  `PhysicsWorld::ReserveBodyScratchCapacity` (`PhysicsWorld.cpp:401`) is the
  right existing seam to generalize. Record the measured resident footprint
  before any change, per store, so SC7 can quote a real delta. Acceptance: every
  one of the 93 members has a named sizing quantity and a target owner; no row is
  unclassified.

  Closed 2026-07-27 with permanent evidence in
  `../../Reports/2026-07-27/scene-sized-store-capacity-sc0-census.md`. Current
  source corrected the originating counts from 25 fixed lists + 40 vectors to
  **43 fixed lists + 50 vectors = 93 retained dense rows**. The delta is the
  complete body/collider handle topology, two alias-typed `PhysicsEngine` rows,
  SC1's handle-keyed refresh scratch, the sleep owner's eighteenth row, later
  broadphase oracle buffers, and five retained contact side-effect vectors
  omitted by the review summary. All 93 rows have an MSVC element size, current
  capacity source, hot/cold classification, scene-derived or semantic fixed
  sizing quantity, and target owner. The Debug payload lower bound is
  112,042,496 bytes per engine.
  `SceneWorld -> PhysicsEngine::ReserveAuthoredBodyCapacity ->
  PhysicsWorld::ReserveBodyScratchCapacity` is the accepted seam to generalize.
  Authored setup owns exact expansion preflight after parse; generated setup
  already knows its counts; `SceneWorld` remains the one commit coordinator.

- [x] **SC1 — Runtime capacity in the fail-loud container.**
  Give `PhysicsFixedList` a runtime `Reserve(count)` that commits backing storage
  once, keeps `Capacity` as the compile-time absolute ceiling, and keeps
  `FailCapacityExceeded` as the response to exceeding either bound. Storage
  allocation is permitted only inside a scene-load allocation phase and must be
  attributed to a registered reserve owner. ReplayPrediction retains its
  separately governed exception: the container may allocate under its existing
  registered Replay owner and approved growth scope, but cannot create Replay
  authority itself. In the same phase, fix the carried defects: replace
  `std::aligned_storage` with an
  `alignas(alignof(T)) unsigned char[]` buffer; make copy/move use
  `std::memcpy` for trivially copyable `T` and element-wise construction
  otherwise; replace the `(owner, index)` iterators with pointer iterators so the
  type models `contiguous_range`; delete the 20 `mutable` keywords in
  `PhysicsBodyStore.h`; delete `FenceHandle`. Acceptance: focused container tests
  cover reserve-then-fill, exceeding runtime capacity, exceeding the compile
  ceiling, and trivial/non-trivial copy paths; `sizeof` and alignment
  static_asserts hold; `HotFields() const` compiles without `mutable`;
  `rg -n 'FenceHandle' SkullbonezSource SkullbonezTests` returns no rows.

  Closed 2026-07-27. `PhysicsFixedList` now owns aligned runtime backing,
  preserves the compile-time ceiling, exposes pointer iterators, and uses
  `memcpy` for trivial rows plus rollback-safe element construction otherwise.
  SceneWorld is the outer SceneLoad coordinator; leaf stores register named
  owners without manufacturing phase authority. ReplayPrediction pre-reserves
  its isolated engine only under its existing registered Replay owner and
  approved growth grant. The 20 `mutable` qualifiers and `FenceHandle` are gone.
  A feature-flag-dependent `RuntimeReserveOwnerDesc` layout found by the Profile
  allocation guard was made ABI-stable across engine libraries. Focused
  container tests, all 408 doctests / 2,403,974 assertions, physics regression,
  startup smoke, allocation guard, allocation-policy scans, `validate_perf`,
  `validate_full`, formatting, and aggregate governance pass. Independent
  review ended `ZERO BLOCKERS`.

- [x] **SC2 — Shape-sized collider rows.**
  Remove the hull payload from every non-hull collider row. The dense
  `ColliderRecord` keeps the hot scalars it already documents
  (`boundingRadius`, `restitution`, `friction`, `contactMaterialId`,
  `projectedSurfaceArea`, `dragCoefficient`) plus a shape kind and a typed
  reference into per-kind storage; `ConvexHullShape` payloads live in a separate
  hull store sized by the number of *hull* colliders in the scene, not by the
  scene object count. Narrowphase dispatch stays exhaustive and compile-time
  checked — `Physics/CollisionShape.h:166` already documents the N*N dispatch
  contract and it must remain non-virtual with no type erasure. Acceptance:
  `sizeof(ColliderRecord)` falls from 7,228 bytes to the measured hot-scalar
  size; a scene with zero hull colliders allocates zero hull storage; the
  physics regression CSV is byte-exact; `validate_physics` and
  `validate_perf` pass.

  Closed 2026-07-27. `ColliderRecord` is now an 80-byte hot row containing its
  existing scalars, shape kind, and a typed non-owning reference. Sphere, box,
  and hull payloads live in separate runtime-capacity stores; zero-hull scenes
  retain zero hull capacity. Copy, move, compaction, update, and backing-growth
  paths rebind references by per-kind storage index. Runtime consumers use
  exhaustive compile-time visitation without virtual dispatch or type erasure;
  the only owning bridge is the explicit cold `CopyCollisionShape` helper.
  Focused storage coverage passes 40/40 assertions, including forced live
  backing relocation and zero hull capacity. The 409-doctest broad gate,
  byte-exact 44,401-line physics oracle, DX12 and physics performance budgets,
  formatting, allocation policy, and governance inventories pass. Independent
  review ended `ZERO BLOCKERS`. Permanent evidence:
  `../../Reports/2026-07-27/scene-sized-store-capacity-sc2-shape-storage.md`.

- [x] **SC3 — Scene-load capacity commit.**
  One owner computes required capacity from the loaded scene and commits it for
  every store before the first fixed tick, inside the existing SceneLoad
  allocation phase. If the incoming scene fits current capacity, the commit is a
  no-op and allocates nothing. Generalize the existing
  `ReserveBodyScratchCapacity` seam rather than adding a parallel path; the
  commit is an ordered operation on concrete owners, not a capacity bag passed
  around. Exceeding `MAX_SCENE_OBJECTS` fails loud at load with owner, requested,
  and ceiling values — it must not truncate the scene. Acceptance: loading a
  300-body scene, then a 200-body scene, reports zero allocations for the second
  load; loading 300 then 2,000 reports exactly one attributed growth per affected
  owner; a 9,000-body scene fails loud at load; physics CSV byte-exact.
  Closed 2026-07-27. Authored/generated setup commits exact body, per-kind shape,
  and point-joint topology before mutation; editor ragdolls preflight bodies and
  joints; mixed generated preflight preserves the live RNG stream; retained
  point-joint backing is distinct from the current logical allowance. The
  300→200→2,000 proof reports zero then exactly 67 unique growth events, and
  fatal probes pin the 8,192 hard ceiling plus a retained-12/logical-8 ninth
  joint failure. Replay reconstruction reuses only its approved 256 MiB outer
  owner. Full, physics, performance, allocation, format, focused, and shipping
  smoke gates pass; independent review ended `ZERO BLOCKERS`. Permanent
  evidence:
  `../../Reports/2026-07-27/scene-sized-store-capacity-sc3-binding.md`.

- [x] **SC4 — Convert the hot solver and narrowphase vectors.**
  Move the 10 `PhysicsContactSolverStage` (including its five retained side
  effect rows), 7 `PhysicsNarrowphaseStage`, 5 `PhysicsBroadphaseStage`, and 2
  `PhysicsForceStage` members onto the runtime-capacity container, and delete
  the two in-tick contact reserves at `PersistentContactSolver.cpp:805` and
  `:992` plus the Debug-contact reserve at `:1591` so an overrun trips
  `FailCapacityExceeded` instead of reallocating. Preserve exact serial and
  parallel pair ordering and the mutual-gravity triangular pair scratch
  semantics. Acceptance: no `reserve` remains on the converted contact,
  narrowphase, broadphase, or force fixed-tick paths; the SC3-committed Debug
  contact row no longer reserves during the tick; remaining sleep/terrain/
  diagnostics operations are explicitly owned by SC5; physics CSV byte-exact;
  the existing worker-count determinism tests
  (`SkullbonezTests/TestDeterminism.cpp:1115,1161,1482`) pass unchanged at 0/1/4
  workers; `validate_physics` and `validate_perf` pass.
  Closed 2026-07-27. All 24 retained contact, narrowphase, broadphase, and
  force rows now use scene-committed `PhysicsFixedList` backing. Shared B/P/K
  formulas preserve candidate-pair ordering, contact bounds, and triangular
  mutual-gravity indexing. The three contact-tick reserves are deleted; Debug
  contact diagnostics commit before the tick. Profile growth remains 86 unique
  owners, while Debug records its three additional broadphase oracle owners for
  89. Focused Debug capacity coverage, the 410-doctest fast gate, allocation
  policy, byte-exact 44,401-line physics regression, and performance budgets
  pass. Permanent evidence:
  `../../Reports/2026-07-27/scene-sized-store-capacity-sc4-hot-stage-lists.md`.

- [x] **SC5 — Convert the sleep, terrain, diagnostics, and world vectors.**
  Move the 18 `PhysicsSleepController`, 2 `PhysicsTerrainStage`, 3
  `PhysicsStepDiagnostics`, 2 `PhysicsWorld`, and 1 `PhysicsEngine` members.
  `PhysicsWorld::m_timeRemaining` is the cross-stage CCD clock
  (`PhysicsWorld.h:137`) and must keep exactly its current semantics.
  `m_pointJointConstraints` keeps its handle-generation behavior. Diagnostics
  rows may stay Debug-only but must still be capacity-committed rather than
  grown. Acceptance: no `reserve`/`resize`/`assign` remains on a fixed-tick path
  under `Physics/`; zero `std::vector` members remain under `Physics/` except
  rows SC0 explicitly ruled cold-and-unbounded with a reason; physics CSV
  byte-exact; sleep/island focused tests and `validate_physics_deep` pass.
  Closed 2026-07-27. All 26 retained sleep, terrain, diagnostics, world, and
  engine rows now use scene-committed `PhysicsFixedList` backing. The CCD clock,
  point-joint handle generation and compaction, sleep/island ordering, terrain
  manifold order, and Debug pipeline rows retain their prior semantics.
  Profile growth is now 89 unique owners and Debug records 92. Focused capacity
  coverage, the 410-doctest fast gate, allocation policy, byte-exact
  44,401-line physics regression, deep physics diagnostics, and performance
  budgets pass. Permanent evidence:
  `../../Reports/2026-07-27/scene-sized-store-capacity-sc5-remaining-live-rows.md`.

- [ ] **SC6 — Prediction engine parity.**
  The isolated prediction engine (`Runtime/Prediction/ReplayPrediction.h:250`)
  receives the same scene-sized commit, so an armed prediction over a 200-body
  scene no longer pays a second 8,192-row footprint. Its existing 256 MB
  registered hard cap and reserve-owner registration stay in force and must be
  re-measured, not assumed. Acceptance: prediction resident footprint for a
  200-body scene falls by the measured delta; the replay visual-fidelity oracle
  is frame-exact with no golden refresh; `validate_replay_visual_fidelity.bat`
  passes in exactly one engine process and one prediction generation.

- [ ] **SC7 — Reconcile, review, and hand off.**
  Rerun the SC0 census at final source and publish before/after resident
  footprint per store for a 200-body, a 2,000-body, and the regression scene.
  Confirm zero in-tick allocation via the allocation gate. Complete the comment
  audit for every touched source file. Obtain one independent ownership review
  covering: did any capacity path become a bag or a callback, did any store gain
  a second capacity authority, and can any fixed-tick path still allocate.
  Acceptance: independent review clear; physics CSV byte-exact from the final
  Debug binary; `validate_full.bat`, `validate_physics.bat`,
  `validate_physics_deep.bat`, `validate_perf.bat`, and
  `validate_replay_visual_fidelity.bat` all pass with no baseline, golden,
  schema, or config change.

## Dependencies And Decisions

- Depends on `governance-shape-to-judgment-conversion` G1 for the acceptance
  rules used by SC7's review.
- `store-capacity-memory-reporting` depends on this plan: it registers and
  surfaces the capacities SC1-SC6 establish. Do not duplicate its reporting work
  here; SC0/SC7 measure footprints for evidence only.
- Owner decision ratified at registration: runtime capacity with
  `MAX_SCENE_OBJECTS = 8192` retained as an absolute fail-loud ceiling. The
  alternative of deleting the compile-time ceiling was considered and rejected.
- Owner decision ratified at registration: capacity is monotonic within a
  process. No shrink path, and no deferred follow-up row for one.
- SC1 amends the Runtime Static Allocation Policy in `AGENTS.md` to admit
  scene-load capacity commits by registered owners. That amendment belongs to
  this plan, not to G1, and must state the phase, the owner registration
  requirement, and that fixed-tick growth remains a policy failure.
- Owner-overridable default, agent does not stop: whether `SceneCapacity.h`
  should keep `DEFAULT_SCENE_OBJECT_CAPACITY = 4000` once capacity is
  scene-derived. SC0 reports every remaining consumer; SC3 leaves the constant
  in place unless the owner rules otherwise.

## Acceptance

- Resident dense-physics footprint for a 200-body scene falls by the SC7
  measured delta, with `ColliderStore` no longer carrying 56.5 MiB.
- A scene load that fits current capacity allocates nothing.
- No `reserve`, `resize`, or `assign` remains on any fixed-tick path under
  `Physics/`; overflow is fail-loud everywhere.
- `sizeof(ColliderRecord)` reflects only hot scalars; hull storage scales with
  hull colliders.
- Zero `mutable` on `PhysicsBodyStore` hot arrays; `FenceHandle` deleted;
  `PhysicsFixedList` free of `std::aligned_storage` and contiguous-iterable.
- Physics determinism preserved byte-exactly at 0, 1, and 4 workers.

## Validation

Per the `AGENTS.md` File To Validation Mapping, cumulative:

- `tools\validate_physics.bat` — `PhysicsWorld*`, `RigidBody*`, collider and
  bounding types changed.
- `tools\validate_physics_deep.bat` — SC5 touches SkullScope-visible
  diagnostics rows.
- `tools\validate_perf.bat` — `Core/Allocation` semantics and hot-path storage
  changed.
- `python tools\check_allocation_policy.py --self-test` and
  `--repo .` — allocation policy and allowlist rows change in SC1/SC3.
- `tools\validate_replay_visual_fidelity.bat` — SC6 touches the prediction
  engine. One engine process, one prediction generation, no golden refresh.
- `tools\validate_full.bat` — `Common.h`-class breadth; required at the closure
  gate and for `Runtime/*` edits in SC3/SC6.
