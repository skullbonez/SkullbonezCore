# Downward Domain Bleed Remediation DB0 Census

Date: 2026-07-26
Branch: `nightrunner-25th-JUL-26`
Plan task: DB0
Source tip: `28764e53`
Status: **complete**

## Result

DB0 ratifies the three registered bleed classes from the final Replay
partition tip and adds one bounded drift row: nine unused
`Assets/AssetKeys.h` includes under Physics. No source behavior changed.

The implementation order remains:

1. DB1 makes retained geometry feature-neutral and moves all trajectory
   semantics to `Runtime/Prediction`.
2. DB2 installs one Physics-owned scene-terrain view, moves the terrain
   support classifier to Physics, removes the per-body World pointer, and
   deletes every upward World/Assets include.
3. DB3 moves the five environment/fluid facts out of `PhysicsBodyRecord` into
   a dense `BuoyancySystem` owner aligned with body rows.
4. DB4 makes the boundaries permanent in governance and the existing
   dependency validator.

## Disposition Table

| Id | Current lower-layer bleed | Owner | Target task | Final disposition |
|---|---|---|---|---|
| B1 | trajectory record/layout/capacity and retained DX12 machinery in Rendering | Runtime/Prediction for feature semantics; Rendering for generic upload | DB1 | Prediction owns the 19-float record, logical capacities, adjacency repair, and presentation vocabulary; Rendering receives generic values |
| B2 | `Geometry::Terrain*` and World terrain classifier inside Physics | Physics | DB2 | one scene-lifetime `PhysicsTerrainView`; no per-body World pointer |
| B3 | five fluid/environment fields in every `PhysicsBodyRecord` | `BuoyancySystem` | DB3 | fixed-capacity dense facts aligned with body/collider rows |
| B4 | nine unused `Assets/AssetKeys.h` includes under Physics | Physics | DB2 | delete; no replacement dependency or compatibility header |

No fourth behavioral bleed class was found. B4 is include residue with no
referenced Asset symbol.

## B1 - Retained Trajectory Census

### Definitions and backend state

| File and line | Symbol or responsibility |
|---|---|
| `Rendering/RenderCommandTypes.h:154-157` | 19 floats/segment, 24,000 ordinary segments, 3,000 priority segments, 4,096 ranges |
| `Rendering/RenderCommandTypes.h:163` | `RetainedTrajectoryDrawRange` |
| `Rendering/RenderCommandTypes.h:182` | `AppendRetainedTrajectoryRecord` |
| `Rendering/RenderCommandTypes.h:231` | `AppendRetainedTrajectoryContinuationRecord` |
| `Rendering/DX12/RenderBackendDX12.h:132` | per-frame `RetainedTrajectoryBufferDX12` cache |
| `Rendering/DX12/RenderBackendDX12.h:145` | `RetainedTrajectoryUploadPlanDX12` |
| `Rendering/DX12/RenderBackendDX12.h:153` | whole-stream upload-plan builder |
| `Rendering/DX12/RenderBackendDX12.h:179` | independent-range upload-plan builder |
| `Rendering/DX12/RenderBackendDX12.h:586-596` | trajectory allocation sizing and command initialization |
| `Rendering/DX12/RenderBackendDX12.h:627-640` | retained ribbon/range draw entry points |
| `Rendering/DX12/RenderBackendDX12.h:697-718` | ordinary/priority/line/compact capacities, frame buffers, command signature |
| `Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp:1062-1380` | upload, cache invalidation, range validation, indirect draw, and command signature implementation |
| `Rendering/DX12/RenderBackendDX12.cpp:546-567` | cold GPU allocation and command-signature startup |

The full exact-symbol sweep finds these fourteen source/test files:

- `Rendering/RenderCommandTypes.h`
- `Rendering/DX12/RenderBackendDX12.h`
- `Rendering/DX12/RenderBackendDX12.cpp`
- `Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp`
- `Runtime/App/ReplayRuntime.cpp`
- `Runtime/Editor/EditorTracer.cpp`
- `Runtime/Prediction/ReplayPredictionDrawing.h`
- `Runtime/Prediction/ReplayPredictionDrawing.cpp`
- `Runtime/Prediction/ReplayPredictionPresentation.h`
- `Runtime/Prediction/ReplayPredictionPresentation.cpp`
- `Runtime/Replay/ReplayVisualPacket.h`
- `Runtime/Tools/RuntimeTools.h`
- `SkullbonezTests/TestDx12OnlyRuntime.cpp`
- `SkullbonezTests/TestReplayVisualPacket.cpp`

This is drift from the registration-time seven-file summary because the
completed Replay partition moved consumers and made the retained presentation
owner explicit. The underlying bleed is unchanged.

### Additional Rendering feature vocabulary

The complete Rendering sweep found no porkchop, planning, cause-tree,
trip-planner, guide-arc, or intercept-readout vocabulary. It found these
additional trajectory/prediction spellings that belong to B1:

| Vocabulary | Files | DB1 disposition |
|---|---|---|
| `RenderUploadCategory::DebugPredictionOverlay` | `RenderDiagnosticsTypes.h`, `DX12/Dx12Diagnostics.cpp`, `DX12/Dx12FrameOwner.cpp`, dynamic geometry | generic retained-geometry upload category |
| `TransientTriangleStyle::TrajectoryRibbon*` and `IsTrajectoryRibbonStyle` | `RenderCommandTypes.h`, `RenderBackendDX12.cpp`, dynamic geometry | generic instanced-ribbon style |
| `trajectory_ribbon` shader contract/path | `ShaderContracts.h`, `DX12/GeneratedShaderReflection.h`, dynamic geometry, shader source path | feature-neutral retained-ribbon shader spelling; shader behavior unchanged |
| replay/prediction wording attached to generic render rows | `RenderInstanceStore.h/.cpp`, `WorldRenderExtension.h`, `RenderCommandTypes.h`, `RenderBackendDX12.cpp` | correct comments to describe one-frame pose/grouping or generic retained bytes |
| unrelated verb “replay” for an erased render-graph invocation | `RenderGraph.cpp` | reword to “rerun”; not a domain contract |

`simpleRagdollPart` and one-frame pose application are generic Rendering
values, not Replay owners. Their comments are stale feature examples, so DB1
will correct the comments without moving the values.

## Feature-Neutral Retained Geometry Contract

DB1 will keep the public Rendering seam constexpr/value-shaped:

```cpp
enum class RetainedGeometryLane : uint8_t
{
    Ordinary,
    Priority
};

struct RetainedGeometryCapacity
{
    uint32_t floatsPerRecord = 0;
    uint32_t ordinaryRecordCapacity = 0;
    uint32_t priorityRecordCapacity = 0;
    uint32_t ordinaryLineFloatCapacity = 0;
    uint32_t priorityLineFloatCapacity = 0;
    uint32_t rangeCapacity = 0;
};

struct RetainedGeometryStreamToken
{
    uint64_t identity = 0;
    uint64_t revision = 0;
};

struct RetainedGeometryRangeToken
{
    uint64_t identity = 0;
    uint64_t drawOrder = 0;
    uint32_t firstRecord = 0;
    uint32_t recordCapacity = 0;
    uint32_t recordCount = 0;
    uint32_t sourceVersion = 0;
    uint32_t cacheSlot = 0;
    uint32_t continuationRange = 0;
    RetainedGeometryLane lane = RetainedGeometryLane::Ordinary;
};

struct RetainedGeometryUploadPlan
{
    bool uploadRequired = false;
    std::size_t firstChangedUnit = 0;
};

constexpr RetainedGeometryUploadPlan BuildRetainedGeometryUploadPlan(
    RetainedGeometryStreamToken cached,
    std::size_t cachedUnitCount,
    RetainedGeometryStreamToken incoming,
    std::size_t incomingUnitCount,
    bool repairPreviousUnit ) noexcept;

constexpr RetainedGeometryUploadPlan BuildRetainedGeometryRangeUploadPlan(
    const RetainedGeometryRangeToken& cached,
    const RetainedGeometryRangeToken& incoming ) noexcept;
```

The current upload-plan comparisons and arithmetic remain byte-for-byte
equivalent: equal identity/revision is a no-op, append starts at the cached
unit, tail repair backs up one unit, and replacement/contraction starts at
zero.

`Runtime/Prediction/ReplayPredictionRetainedGeometry.h` will own:

- `PREDICTION_TRAJECTORY_FLOATS_PER_RECORD = 19`;
- 24,000 ordinary and 3,000 priority records;
- 4,096 logical ranges;
- the existing ordinary/priority line-float capacities;
- the exact 19-float record type and component meanings;
- the presentation-equality and continuity tests; and
- both append and continuation-tail repair helpers.

Prediction supplies `RetainedGeometryCapacity` at cold renderer
initialization. Rendering may retain only a generic backend safety maximum and
the configured active capacity; it may not restate feature capacities.
CPU/GPU storage is allocated or committed before steady gameplay. Draw calls
receive spans, tokens, stride, lane, and generic ribbon style synchronously;
no callback, virtual seam, feature include, or retained Runtime owner crosses
the boundary.

## B2 - Physics Terrain Census

### Upward includes

The final tip has thirteen upward include rows under Physics:

| Target | Rows | Files |
|---|---:|---|
| World | 4 | `PhysicsBodyStore.cpp` and `TerrainContactManifold.cpp` each include `Terrain.h` and `TerrainSupportClassifier.h` |
| Assets | 9 | `ObjectContactManifold.cpp`, `PersistentContactSolver.cpp`, `PhysicsBodyStore.cpp`, `PhysicsWorld.cpp`, `TerrainContactManifold.cpp`, and the Broadphase, Force, Narrowphase, and Terrain stage `.cpp` files |
| Scene | 0 | none |
| Gameplay | 0 | none |
| Runtime | 0 | none |
| UI | 0 | none |

Every Assets include is unused: the files contain no `Assets::` or Asset-key
reference. DB2 deletes all nine.

### Terrain pointer and query sites

| File and line | Phase | Use |
|---|---|---|
| `PhysicsApi.h:55-59,114,128,157` | authoring-cold | World forward declaration, descriptor field, constructor parameter, assignment |
| `PhysicsBodyStore.h:95` | retained hot row | one borrowed `Geometry::Terrain*` per body |
| `PhysicsBodyStore.cpp:157-306` | fixed-step hot | clamp/support queries: bounds, height+plane, height |
| `PhysicsBodyStore.cpp:412-625` | fixed-step hot | water clearance and buoyancy support: bounds and height |
| `PhysicsBodyStore.cpp:1024` | authoring/refresh-cold | pointer copied from descriptor to every body row |
| `TerrainContactManifold.h:59` | one-stage value | pointer copied into `TerrainContactBodyView` |
| `PhysicsTerrainStage.cpp:60-74` | fixed-step hot | copies the row pointer into the stage value |
| `TerrainContactManifold.cpp:69-575` | fixed-step hot | bounds, max-height early-out, height+plane, support classification |

The exact Physics query surface is only:

- `IsInBounds(float x, float z)`;
- `HeightAt(float x, float z)`;
- `HeightAndPlaneAt(float x, float z, float& height, Plane& plane)`; and
- `MaxHeight()`.

Physics never requests render vertices, normals alone, X/Z bounds, minimum
height, assets, shaders, or terrain mutation.

All production body creation paths receive either the one active
`SceneWorld::Terrain()` pointer or null when the scene has no terrain. The
census found no production scene with two terrain objects and no per-body
terrain opt-out inside a terrain scene. Therefore DB2 chooses one
scene-terrain slot, not a per-body handle/index.

`TerrainSupportClassifier.h` is physically under World but declares Physics
policy and is consumed by Physics plus one Runtime editor probe. DB2 moves the
classifier and constants to Physics. The editor may consume the stable
Physics value seam; World retains no classifier authority.

## Physics Terrain Boundary

The new Physics-owned value/borrow contract is:

```cpp
struct PhysicsTerrainCell
{
    Math::CollisionDetection::Plane triangleA;
    Math::CollisionDetection::Plane triangleB;
};

struct PhysicsTerrainView
{
    std::span<const PhysicsTerrainCell> cells;
    int quadsPerSide = 0;
    float scaledStepSize = 0.0f;
    float worldExtent = 0.0f;
    float maxHeight = 0.0f;
    bool flatSlope = false;
    float flatSlopeExtent = 0.0f;
    float slopeBaseY = 0.0f;
    float slopeX = 0.0f;
    float slopeZ = 0.0f;
    Math::CollisionDetection::Plane flatSlopePlane;

    bool IsValid() const noexcept;
    bool IsInBounds( float x, float z ) const noexcept;
    float HeightAt( float x, float z ) const;
    void HeightAndPlaneAt(
        float x,
        float z,
        float& outHeight,
        Math::CollisionDetection::Plane& outPlane ) const;
    float MaxHeight() const noexcept;
};

void PhysicsEngine::SetTerrainView( PhysicsTerrainView view ) noexcept;
void PhysicsEngine::ClearTerrainView() noexcept;
```

`World::Terrain` fills `PhysicsTerrainCell` directly while building its
existing collision cache and returns a borrowing `PhysicsTerrainView`. The
view contains no World type, owner pointer, function pointer, callback, or
virtual dispatch. Physics implements the query arithmetic.

`SceneWorld` is the lifetime owner:

- `SceneTerrain` precedes `PhysicsEngine` in member order, so Physics is
  destroyed before the borrowed cells.
- Scene load clears the Physics view before replacing a terrain and registers
  the replacement view after construction, before body registration or a
  fixed step.
- A terrain-less scene registers an invalid/empty view.
- `PhysicsBodyCreateDesc`, `PhysicsBodyRecord`, and
  `TerrainContactBodyView` carry no terrain pointer.
- PhysicsWorld passes its one retained view as a const value borrow to body
  integration and the terrain stage for the synchronous fixed step.

The moved `ClassifyBoxTerrainSupport` and vertex probes take
`const PhysicsTerrainView&`; their result types and policy math remain in
Physics. Tests construct the view from deterministic cells or a CPU Terrain
projection and no longer link Physics to World.

## B3 - Fluid/Environment Field Census

| `PhysicsBodyRecord` field | Cold writes/reads | Fixed-step reads/writes | Final owner |
|---|---|---|---|
| `volume` | descriptor creation and refresh; editor history/UI inspect reads | buoyancy force multiply | `BuoyancySystem` dense facts |
| `projectedSurfaceArea` | descriptor creation and refresh; editor history reads | viscous drag multiply | `BuoyancySystem` dense facts; collider copy remains collider authority |
| `dragCoefficient` | descriptor/policy creation and refresh; editor history reads | linear and angular drag | `BuoyancySystem` dense facts; collider copy remains collider authority |
| `submergedVolumePercent` | initialized to zero | sampled per force operation; targeted sphere sleep probe writes/reads; pose integration invalidates to zero | `BuoyancySystem` dense mutable snapshot |
| `contactEpsilon` | Physics policy stamps descriptor; editor history reads/writes | buoyancy terrain-support damping and terrain-stage body view | `BuoyancySystem` dense facts borrowed by terrain stage |

The five fields are not serialized in `PhysicsSolverSnapshot` and do not occur
in Replay artifact encode/decode. DB3 therefore does not change the replay
snapshot schema. Descriptor authoring values remain because editor undo/redo
and deterministic refresh need them; the live body row copies are removed.

DB3 makes the existing stateless helper a concrete state owner:

```cpp
struct BuoyancyBodyFacts
{
    float volume = 0.0f;
    float projectedSurfaceArea = 0.0f;
    float dragCoefficient = 0.0f;
    float submergedVolumePercent = 0.0f;
    float contactEpsilon = 0.05f;
};

class BuoyancySystem
{
  public:
    bool AppendBodyFacts( const PhysicsBodyCreateDesc& desc );
    bool RefreshBodyFacts( int bodyIndex, const PhysicsBodyCreateDesc& desc );
    bool EraseBodyFactsSwapLast( int bodyIndex );
    bool TrimToCount( int bodyCount );
    void Clear();
    std::span<const BuoyancyBodyFacts> Facts() const;
    std::span<BuoyancyBodyFacts> MutableFacts();
    // Existing sampling/sleep operations become instance methods over this
    // aligned store.

  private:
    PhysicsFixedList<BuoyancyBodyFacts, Scene::Capacity::MAX_SCENE_OBJECTS>
        m_bodyFacts { "BuoyancySystem.bodyFacts" };
};
```

The body/collider registration transaction appends this row at the same dense
index and rolls all owners back together. Refresh, trim, destroy/swap-last,
clear, and topology repair update the three counts atomically. PhysicsWorld
asserts body/collider/buoyancy count equality before a fixed step.

Force integration fetches `const BuoyancyBodyFacts&` once per body immediately
before the unchanged math. The terrain stage reads `contactEpsilon` from the
same index. The sleep controller mutates only the submerged fraction. UI and
editor history use a PhysicsEngine read seam over these facts or the existing
authored descriptor; they do not reach into the owner.

## Determinism Strategy

### DB2 terrain

- `PhysicsTerrainCell` stores the exact planes already built by
  `Terrain::BuildCollisionCache`; no plane is recomputed at the boundary.
- `IsInBounds`, quad selection, triangle selection, and plane-height division
  move with their existing comparison and float-operation order unchanged.
- Flat-slope height evaluation retains `base + slopeX*x + slopeZ*z` in that
  order.
- The support classifier moves without changing loop order, constants,
  contact ordering, profiling branch, or policy decisions.
- The single scene view replaces repeated equal pointers; stage/body iteration
  order is unchanged.
- `tools\validate_physics.bat` must reproduce the 44,401-line CSV byte for
  byte. `validate_tests.bat` proves Physics terrain paths without World source
  linkage, and `validate_perf.bat` checks the hot value view.

### DB3 buoyancy facts

- Descriptor stamping and the five scalar assignments remain in their current
  order at append/refresh.
- Each fixed-step function reads the aligned fact row at the same call point;
  buoyancy, drag, support, and sleep arithmetic are not rewritten or reordered.
- The submerged snapshot is reset and refreshed at the same pose/sleep phases.
- Swap-last/trim operations mirror body/collider row movement and assert count
  equality before the next step.
- No replay snapshot field exists, so capture/restore bytes do not change.
- `tools\validate_physics.bat` is byte-exact; the deep physics and perf gates
  prove SkullScope and hot-path behavior with no baseline refresh.

Any physics CSV byte difference reopens the task; no bounded divergence or
baseline update is permitted.

## Expected Touched-Source Comment Checklist

DB5 will reconcile this expected set with `git ls-files` from the campaign
base and audit every actually touched source-bearing file.

| Slice | Expected files |
|---|---|
| DB1 core | `Rendering/RenderCommandTypes.h`, `RenderDiagnosticsTypes.h`, `ShaderContracts.h`, DX12 backend header/main/dynamic-geometry, diagnostics/frame owner, generated reflection, retained-ribbon shader |
| DB1 consumers/tests | Runtime App replay composition, Editor tracer, Prediction drawing/presentation, Replay visual packet, RuntimeTools, `TestDx12OnlyRuntime.cpp`, `TestReplayVisualPacket.cpp`, project/filter metadata if paths change |
| DB2 Physics | new terrain view files, moved support classifier, `PhysicsApi.h`, body store, engine/world, terrain manifold/stage, and the nine Physics files with upward includes |
| DB2 World/Runtime/tests | `World/Terrain.h/.cpp`, `Runtime/Scene/SceneWorld.*`, scene load/setup and body-creation helpers, Editor launcher/history/placement, Startup probes, RuntimeTools, terrain/coverage/determinism/stage tests, project/filter metadata |
| DB3 | `BuoyancySystem.*`, body store, Physics engine/world/force/terrain/sleep paths, Physics API or authored-descriptor query seams, Runtime UI/editor consumers, focused coverage/stage tests |
| DB4 | `AGENTS.md`, dependency rule data, checker/fixture code only if the existing schema needs extension, validation documentation |

## DB0 Acceptance

- The three registered bleed sites reconcile to current source.
- B4 records all additional upward Physics includes with owner and target.
- Both target boundaries have exact value signatures, capacities, and lifetime
  rules.
- B2/B3 arithmetic-order and gate strategy is explicit.
- Expected final comment-audit scope is recorded.
- No source-bearing file or behavior changed; DB0 requires no repository
  validation.
