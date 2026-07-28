//
// File: SkullbonezTests/TestSpatialGrid.cpp
// Purpose:
//   Lock the first focused tests for SpatialGrid broadphase pairing.
//
// Summary:
//   SpatialGrid is a fixed-capacity broadphase index. Objects are inserted into
//   every cell touched by their bounding sphere or swept bounds, and candidate
//   pairs are emitted once even when two objects share multiple cells.
//
// Glossary:
//   Cell: Integer grid bucket covering one cube of world space.
//   Candidate pair: Pair of object indices that share at least one occupied
//     cell and still need narrowphase testing.
//   Persistent membership: Cell occupancy retained until a body's integer cell
//     range changes or a cold clear invalidates all ranges.
//   Swept overlay: Velocity-dependent cells that expire at the next BeginFrame.
//   Canonical pair order: Ascending `(smaller index, larger index)` order that
//     is independent of the order in which cells discover the pair.
//
// Invariants:
//   - Output pair vectors must reserve capacity before GetCandidatePairs().
//   - Candidate pairs are normalized and emitted in ascending canonical order.
//   - BeginFrame removes retired dense rows and expires only swept occupancy.
//   - Pair-source stamps restrict work without evicting sleeping membership.
//   - Clear() is a cold scene/config/replay reset, not the per-step path.
//
// Related:
//   - SkullbonezSource/Physics/SpatialGrid.h
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//   - Agentic/Reports/2026-07-29/broadphase-canonical-order-guard-closure.md
//

#include "../ThirdPtySource/doctest/doctest.h"
#include "TestFixedSeed.h"

#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "../SkullbonezSource/Physics/ColliderStore.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/SpatialGrid.h"

#include <utility>
#include <vector>

using SkullbonezCore::Math::CollisionDetection::SpatialGrid;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::PhysicsBodyCreateRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;

namespace
{
std::vector<std::pair<int, int>> CandidatePairs( SpatialGrid& grid, int reserveCount = 16 )
{
    std::vector<std::pair<int, int>> pairs;
    pairs.reserve( static_cast<std::size_t>( reserveCount ) );
    grid.GetCandidatePairs( pairs );
    return pairs;
}

bool HasPair( const std::vector<std::pair<int, int>>& pairs, int a, int b )
{
    if ( a > b )
    {
        const int tmp = a;
        a = b;
        b = tmp;
    }
    for ( const auto& pair : pairs )
    {
        if ( pair.first == a && pair.second == b )
        {
            return true;
        }
    }
    return false;
}

SpatialGrid& TestGrid()
{
    // Why: SpatialGrid owns large fixed arrays; static storage keeps the test
    // fixture aligned with runtime storage expectations instead of consuming the
    // doctest thread stack.
    static SpatialGrid grid( 10.0f );
    grid.Clear();
    grid.SetCellSize( 10.0f );
    return grid;
}

PhysicsBodyStore& CeilingBodyStore()
{
    // Why: the production-filtered ceiling proof needs dense rows through
    // index 8,191. Static owner storage keeps that capacity off the test stack.
    static PhysicsBodyStore store;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        store.ReserveCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    }

    store.Clear();
    return store;
}

ColliderStore& CeilingColliderStore()
{
    // Why: filtered broadphase consumes the collider row at every candidate
    // index, so this owner must mirror the body's complete dense prefix.
    static ColliderStore store;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        store.ReserveCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
        store.ReserveShapeCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS, 0u, 0u );
    }

    store.Clear();
    return store;
}
} // namespace


TEST_CASE( "SpatialGrid: insert/query returns a single deduplicated pair" )
{
    SpatialGrid& grid = TestGrid();
    grid.Insert( 0, Vector3( 5.0f, 5.0f, 5.0f ), 1.0f );
    grid.Insert( 1, Vector3( 6.0f, 5.0f, 5.0f ), 1.0f );

    const auto pairs = CandidatePairs( grid );

    CHECK( grid.GetActiveCellCount() == 1 );
    REQUIRE( pairs.size() == 1 );
    CHECK( pairs[0] == std::make_pair( 0, 1 ) );
}


TEST_CASE( "SpatialGrid: cell-boundary straddling pairs each shared cell once" )
{
    SpatialGrid& grid = TestGrid();
    grid.Insert( 0, Vector3( 9.9f, 5.0f, 5.0f ), 0.2f );
    grid.Insert( 1, Vector3( 10.2f, 5.0f, 5.0f ), 0.1f );
    grid.Insert( 2, Vector3( 8.0f, 5.0f, 5.0f ), 0.1f );

    const auto pairs = CandidatePairs( grid );

    CHECK( grid.GetActiveCellCount() == 2 );
    CHECK( pairs.size() == 2 );
    CHECK( HasPair( pairs, 0, 1 ) );
    CHECK( HasPair( pairs, 0, 2 ) );
    CHECK_FALSE( HasPair( pairs, 1, 2 ) );
}


TEST_CASE( "SpatialGrid: swept insert reaches a later cell without duplicate pairs" )
{
    SpatialGrid& grid = TestGrid();
    grid.InsertSwept( 0, Vector3( 1.0f, 5.0f, 5.0f ), Vector3( 19.0f, 0.0f, 0.0f ), 0.1f );
    grid.Insert( 1, Vector3( 20.0f, 5.0f, 5.0f ), 0.1f );

    const auto pairs = CandidatePairs( grid );

    CHECK( HasPair( pairs, 0, 1 ) );
    CHECK( pairs.size() == 1 );
}


TEST_CASE( "SpatialGrid: cold clear removes retained contents from queries" )
{
    SpatialGrid& grid = TestGrid();
    grid.Insert( 0, Vector3( 5.0f, 5.0f, 5.0f ), 1.0f );
    grid.Insert( 1, Vector3( 6.0f, 5.0f, 5.0f ), 1.0f );
    REQUIRE( CandidatePairs( grid ).size() == 1 );

    grid.Clear();
    const auto pairs = CandidatePairs( grid );

    CHECK( grid.GetActiveCellCount() == 0 );
    CHECK( pairs.empty() );
}


TEST_CASE( "SpatialGrid: unchanged integer ranges perform zero cell maintenance" )
{
    SpatialGrid& grid = TestGrid();
    grid.BeginFrame( 2 );
    grid.Insert( 0, Vector3( 5.0f, 5.0f, 5.0f ), 1.0f );
    grid.Insert( 1, Vector3( 6.0f, 5.0f, 5.0f ), 1.0f );
    CHECK( grid.GetMaintenanceStats().insertedBodies == 2 );
    CHECK( grid.GetMaintenanceStats().persistentCellsAdded == 2 );

    grid.BeginFrame( 2 );
    grid.Insert( 0, Vector3( 5.5f, 5.0f, 5.0f ), 1.0f );
    grid.Insert( 1, Vector3( 6.5f, 5.0f, 5.0f ), 1.0f );
    const auto pairs = CandidatePairs( grid );
    const auto& stats = grid.GetMaintenanceStats();

    CHECK( stats.unchangedBodies == 2 );
    CHECK( stats.movedBodies == 0 );
    CHECK( stats.persistentCellsAdded == 0 );
    CHECK( stats.persistentCellsRemoved == 0 );
    REQUIRE( pairs.size() == 1u );
    CHECK( pairs[0] == std::make_pair( 0, 1 ) );
}


TEST_CASE( "SpatialGrid: pair-source stamps skip retained cells with no awake body" )
{
    SpatialGrid& grid = TestGrid();
    grid.BeginFrame( 4 );
    grid.Insert( 0, Vector3( 5.0f, 5.0f, 5.0f ), 1.0f );
    grid.Insert( 1, Vector3( 6.0f, 5.0f, 5.0f ), 1.0f );
    grid.Insert( 2, Vector3( 25.0f, 5.0f, 5.0f ), 1.0f );
    grid.Insert( 3, Vector3( 26.0f, 5.0f, 5.0f ), 1.0f );
    grid.MarkPairSourceCells( 0 );
    std::vector<std::pair<int, int>> pairs;
    pairs.reserve( 4u );

    grid.GetCandidatePairs( pairs, true );
    REQUIRE( pairs.size() == 1u );
    CHECK( pairs[0] == std::make_pair( 0, 1 ) );

    // Membership persists into the next frame; changing only the stamp selects
    // the other occupied cell without reinsertion.
    grid.BeginFrame( 4 );
    grid.MarkPairSourceCells( 2 );
    grid.GetCandidatePairs( pairs, true );
    REQUIRE( pairs.size() == 1u );
    CHECK( pairs[0] == std::make_pair( 2, 3 ) );
}


TEST_CASE( "SpatialGrid: one-cell move updates only the changed range slabs" )
{
    SpatialGrid& grid = TestGrid();
    grid.BeginFrame( 2 );
    grid.Insert( 0, Vector3( 5.0f, 5.0f, 5.0f ), 1.0f );
    grid.Insert( 1, Vector3( 6.0f, 5.0f, 5.0f ), 1.0f );

    grid.BeginFrame( 2 );
    grid.Insert( 0, Vector3( 15.0f, 5.0f, 5.0f ), 1.0f );
    grid.Insert( 1, Vector3( 6.0f, 5.0f, 5.0f ), 1.0f );
    const auto pairs = CandidatePairs( grid );
    const auto& stats = grid.GetMaintenanceStats();

    CHECK( stats.movedBodies == 1 );
    CHECK( stats.unchangedBodies == 1 );
    CHECK( stats.persistentCellsAdded == 1 );
    CHECK( stats.persistentCellsRemoved == 1 );
    CHECK( pairs.empty() );
}


TEST_CASE( "SpatialGrid: dense-prefix shrink removes retired body memberships" )
{
    SpatialGrid& grid = TestGrid();
    grid.BeginFrame( 2 );
    grid.Insert( 0, Vector3( 5.0f, 5.0f, 5.0f ), 1.0f );
    grid.Insert( 1, Vector3( 15.0f, 5.0f, 5.0f ), 1.0f );
    CHECK( grid.GetActiveCellCount() == 2 );

    grid.BeginFrame( 1 );
    grid.Insert( 0, Vector3( 5.0f, 5.0f, 5.0f ), 1.0f );

    CHECK( grid.GetMaintenanceStats().removedBodies == 1 );
    CHECK( grid.GetMaintenanceStats().persistentCellsRemoved == 1 );
    CHECK( grid.GetActiveCellCount() == 1 );
    CHECK( CandidatePairs( grid ).empty() );
}


TEST_CASE( "SpatialGrid: swept overlay expires without polluting persistent membership" )
{
    SpatialGrid& grid = TestGrid();
    grid.BeginFrame( 2 );
    grid.InsertSwept( 0, Vector3( 1.0f, 5.0f, 5.0f ), Vector3( 19.0f, 0.0f, 0.0f ), 0.1f );
    grid.Insert( 1, Vector3( 20.0f, 5.0f, 5.0f ), 0.1f );
    REQUIRE( CandidatePairs( grid ).size() == 1u );
    CHECK( grid.GetMaintenanceStats().sweptOverlayCellsAdded > 0 );

    grid.BeginFrame( 2 );
    grid.Insert( 0, Vector3( 1.0f, 5.0f, 5.0f ), 0.1f );
    grid.Insert( 1, Vector3( 20.0f, 5.0f, 5.0f ), 0.1f );

    CHECK( grid.GetMaintenanceStats().sweptOverlayCellsAdded == 0 );
    CHECK( grid.GetMaintenanceStats().unchangedBodies == 2 );
    CHECK( CandidatePairs( grid ).empty() );
}


TEST_CASE( "SpatialGrid: changed cell size performs a cold membership reset" )
{
    SpatialGrid& grid = TestGrid();
    grid.BeginFrame( 2 );
    grid.Insert( 0, Vector3( 5.0f, 5.0f, 5.0f ), 1.0f );
    grid.Insert( 1, Vector3( 6.0f, 5.0f, 5.0f ), 1.0f );
    REQUIRE( CandidatePairs( grid ).size() == 1u );

    grid.SetCellSize( 5.0f );
    CHECK( grid.GetActiveCellCount() == 0 );
    CHECK( CandidatePairs( grid ).empty() );

    grid.BeginFrame( 2 );
    grid.Insert( 0, Vector3( 5.0f, 5.0f, 5.0f ), 1.0f );
    grid.Insert( 1, Vector3( 6.0f, 5.0f, 5.0f ), 1.0f );
    CHECK( CandidatePairs( grid ).size() == 1u );
}


TEST_CASE( "SpatialGrid: persistent entry and bucket slots reuse across long travel" )
{
    SpatialGrid& grid = TestGrid();
    grid.SetCellSize( 1.0f );
    grid.BeginFrame( 1 );
    for ( int cell = 0; cell < SpatialGrid::MAX_BUCKETS + 256; ++cell )
    {
        grid.Insert( 0, Vector3( static_cast<float>( cell ) + 0.25f, 0.25f, 0.25f ), 0.0f );
        CHECK( grid.GetActiveCellCount() == 1 );
    }

    CHECK( CandidatePairs( grid ).empty() );
}


TEST_CASE( "SpatialGrid: minimum cell size preserves exact-edge insert and query" )
{
    SpatialGrid& grid = TestGrid();
    grid.SetCellSize( SpatialGrid::MIN_CELL_SIZE );
    grid.Insert( 0, Vector3( 0.25f, 0.25f, 0.25f ), 0.25f );
    grid.Insert( 1, Vector3( 0.75f, 0.25f, 0.25f ), 0.25f );

    const auto pairs = CandidatePairs( grid );

    CHECK( grid.GetCellSize() == SpatialGrid::MIN_CELL_SIZE );
    CHECK( HasPair( pairs, 0, 1 ) );
    CHECK( pairs.size() == 1u );
}


TEST_CASE( "SpatialGrid: one degenerate cell emits every unique pair once" )
{
    SpatialGrid& grid = TestGrid();
    constexpr int kBodyCount = 4;
    for ( int body = 0; body < kBodyCount; ++body )
    {
        grid.Insert( body, Vector3( 5.0f, 5.0f, 5.0f ), 0.1f );
    }

    const auto pairs = CandidatePairs( grid );

    // Hazard: a single crowded cell is the documented O(n^2) broadphase
    // case. The contract is complete, deduplicated output—not hidden pruning.
    CHECK( pairs.size() == 6u );
    for ( int a = 0; a < kBodyCount; ++a )
    {
        for ( int b = a + 1; b < kBodyCount; ++b )
        {
            CHECK( HasPair( pairs, a, b ) );
        }
    }
}


TEST_CASE( "SpatialGrid: crowded-cell output is canonical regardless of insertion history" )
{
    SpatialGrid& grid = TestGrid();
    constexpr int insertionOrder[] = { 3, 1, 4, 0, 2 };
    for ( int body : insertionOrder )
    {
        grid.Insert( body, Vector3( 5.0f, 5.0f, 5.0f ), 0.1f );
    }

    const auto pairs = CandidatePairs( grid );
    const std::vector<std::pair<int, int>> expected = {
        { 0, 1 },
        { 0, 2 },
        { 0, 3 },
        { 0, 4 },
        { 1, 2 },
        { 1, 3 },
        { 1, 4 },
        { 2, 3 },
        { 2, 4 },
        { 3, 4 },
    };

    // Invariant: solver work order is a function of normalized body identity,
    // never the bucket-creation or linked-list order used to discover a pair.
    CHECK( pairs == expected );
}


TEST_CASE( "SpatialGrid: canonical output reaches the current scene-index ceiling" )
{
    SpatialGrid& grid = TestGrid();
    constexpr int kSceneCeiling = SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS;
    constexpr int insertionOrder[] = { kSceneCeiling - 1, 128, 4096, 0, kSceneCeiling - 2, 127 };
    constexpr int canonicalBodies[] = { 0, 127, 128, 4096, kSceneCeiling - 2, kSceneCeiling - 1 };
    grid.BeginFrame( kSceneCeiling );

    for ( int body : insertionOrder )
    {
        grid.Insert( body, Vector3( 5.0f, 5.0f, 5.0f ), 0.1f );
    }

    std::vector<std::pair<int, int>> expected;
    expected.reserve( 15u );

    for ( int smaller = 0; smaller < 6; ++smaller )
    {

        for ( int larger = smaller + 1; larger < 6; ++larger )
        {
            expected.emplace_back( canonicalBodies[smaller], canonicalBodies[larger] );
        }
    }

    // Hazard: this crosses both radix digits and reaches the largest valid
    // body index. Discovery order is deliberately non-canonical.
    const auto pairs = CandidatePairs( grid, static_cast<int>( expected.size() ) );
    CHECK( pairs == expected );

    PhysicsBodyStore& bodyStore = CeilingBodyStore();
    ColliderStore& colliderStore = CeilingColliderStore();
    const CollisionShape sphere( BoundingSphere( 0.1f, Vector3( 0.0f, 0.0f, 0.0f ), 0.0f ) );

    for ( int bodyIndex = 0; bodyIndex < kSceneCeiling; ++bodyIndex )
    {
        PhysicsBodyCreateRecord body;
        body.hot.position = Vector3( 5.0f, 5.0f, 5.0f );
        body.hot.boundingRadius = 0.1f;
        const auto bodyHandle = bodyStore.CreateBodyRecord( body );

        ColliderRecord collider;
        collider.body = bodyHandle;
        collider.boundingRadius = 0.1f;
        (void)colliderStore.CreateColliderRecord( collider, sphere );
    }

    std::vector<uint8_t> sleepState( static_cast<size_t>( kSceneCeiling ), 0u );
    SkullbonezCore::Physics::PhysicsCandidatePairList filteredPairs {
        "TestSpatialGrid.ceilingFilteredPairs",
        SkullbonezCore::Physics::PhysicsCapacityReason::ExplicitTestCapacity,
    };

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        filteredPairs.Reserve( expected.size() );
    }

    grid.GetFilteredCandidatePairs( filteredPairs, bodyStore, colliderStore, sleepState, 0.0f, 0.0f, false );
    REQUIRE( filteredPairs.size() == expected.size() );

    for ( size_t pairIndex = 0; pairIndex < expected.size(); ++pairIndex )
    {
        CHECK( filteredPairs[pairIndex] == expected[pairIndex] );
    }
}


TEST_CASE( "SpatialGrid: one fixed table retains all 8192 cells and existing-key lookup at capacity" )
{
    SpatialGrid& grid = TestGrid();
    grid.SetCellSize( 1.0f );
    constexpr int kPersistentCells = SpatialGrid::MAX_BUCKETS / 2;
    for ( int cell = 0; cell < kPersistentCells; ++cell )
    {
        grid.Insert( cell, Vector3( static_cast<float>( cell ) + 0.25f, 0.25f, 0.25f ), 0.0f );
    }

    // Fill the other half through one legal swept overlay. Its current-position
    // cell remains persistent and the following 4,095 cells are transient.
    const int sweptBody = kPersistentCells;
    const Vector3 sweepStart( static_cast<float>( kPersistentCells ) + 0.25f, 0.25f, 0.25f );
    grid.InsertSwept( sweptBody, sweepStart, Vector3( static_cast<float>( kPersistentCells - 1 ), 0.0f, 0.0f ), 0.0f );

    // Inserting another body into the final occupied key must succeed even when
    // no unused table row remains.
    const int targetBody = sweptBody + 1;
    const Vector3 finalCell( static_cast<float>( SpatialGrid::MAX_BUCKETS - 1 ) + 0.25f, 0.25f, 0.25f );
    grid.Insert( targetBody, finalCell, 0.0f );
    const auto pairs = CandidatePairs( grid );

    CHECK( grid.GetActiveCellCount() == SpatialGrid::MAX_BUCKETS );
    REQUIRE( pairs.size() == 1u );
    CHECK( pairs[0] == std::make_pair( sweptBody, targetBody ) );
}


TEST_CASE( "Property invariant: identical sphere insert/query round-trips including zero radius [seed 0x16AABB00]" )
{
    SkullbonezTests::FixedSeed random( 0x16AABB00u );
    SpatialGrid& grid = TestGrid();

    // Invariant: two identical sphere bounds always share at least one cell,
    // including the degenerate zero-radius case on a cell boundary.
    for ( int sample = 0; sample < 64; ++sample )
    {
        grid.Clear();
        const Vector3 center( random.Float( -50.0f, 50.0f ),
                              random.Float( -50.0f, 50.0f ),
                              random.Float( -50.0f, 50.0f ) );
        const float radius = sample % 8 == 0 ? 0.0f : random.Float( 0.0f, 4.0f );

        grid.Insert( 0, center, radius );
        grid.Insert( 1, center, radius );
        const auto pairs = CandidatePairs( grid );

        REQUIRE( pairs.size() == 1u );
        CHECK( pairs[0] == std::make_pair( 0, 1 ) );
    }
}
