//
// File: SkullbonezTests/TestSpatialGrid.cpp
// Purpose:
//   Lock the first focused tests for SpatialGrid broadphase pairing.
//
// Summary:
//   SpatialGrid is a fixed-capacity broadphase index. Objects are inserted into
//   every cell touched by their bounding sphere or swept bounds, and candidate
//   pairs are emitted once even when two objects share multiple cells. Focused
//   fixtures also preserve traversal-first diagnostic order and prove the
//   per-body eligible shared-bucket membership index without retaining one bit
//   for every possible body pair. The separate runtime-contract harness owns
//   the planted fixed-capacity exhaustion fatal.
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
//   First-seen order: Traversal order at the first cell that discovers a pair;
//     Debug sleep-pruned diagnostics preserve this order before later sorting.
//
// Invariants:
//   - Output pair vectors must reserve capacity before GetCandidatePairs().
//   - Candidate pairs are normalized and emitted in ascending canonical order.
//   - BeginFrame removes retired dense rows and expires only swept occupancy.
//   - Pair-source stamps restrict work without evicting sleeping membership.
//   - Clear() is a cold scene/config/replay reset, not the per-step path.
//   - Geometry admission occurs only at the earliest eligible shared bucket,
//     even when overlapping bodies share several cells or fail the predicate.
//   - SceneLoad reservation fixes every scene-sized backing store. BeginFrame
//     and Clear retain backing/high-water; additional admission also preserves
//     existing persistent membership and generation state.
//
// Related:
//   - SkullbonezSource/Physics/SpatialGrid.h
//   - SkullbonezTests/TestRuntimeContracts.cpp
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//   - Agentic/Reports/2026-07-29/broadphase-canonical-order-guard-closure.md
//

#include "../ThirdPtySource/doctest/doctest.h"
#include "TestColliderStoreFixtures.h"
#include "TestFixedSeed.h"

#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h"
#include "../SkullbonezSource/Physics/ColliderStore.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/SpatialGrid.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::CollisionDetection::SpatialGrid;
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

    // Why: fixed hash topology remains too large for the test thread stack.
    // Reserve the supported ceiling once so behavior tests can choose arbitrary
    // dense body indices without each case restating scene admission.
    static SpatialGrid grid( 10.0f );

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        grid.ReserveSceneCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    }

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


TEST_CASE( "SpatialGrid: scene reserve sizes every registered store from its owning ceiling" )
{
    auto grid = std::make_unique<SpatialGrid>( 10.0f );

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        grid->ReserveSceneCapacity( 3u );
    }

    CHECK( grid->GetPersistentEntryCapacity() == 1048u );
    CHECK( grid->GetBodyMembershipCapacity() == 3u );
    CHECK( grid->GetPairMembershipOrdinalCapacity() == 5144u );
    CHECK( grid->GetPairMembershipOffsetCapacity() == 4u );
    CHECK( grid->GetPairMembershipCountCapacity() == 3u );
    CHECK( grid->GetCandidatePairHeadCapacity() == 3u );
    CHECK( grid->GetCandidatePairNodeCapacity() == 12u );
    CHECK( grid->GetCandidatePairSortKeyCapacity() == 12u );
    CHECK( grid->GetCandidatePairSortScratchCapacity() == 12u );
    CHECK( grid->GetCellObjectSeenCapacity() == 3u );
    CHECK( grid->GetSweptOverlayEntryCapacity() == 4096u );
    CHECK( grid->CollectDynamicMemoryBytes() == 134468u );

    const auto capacityRows = SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::CapacityRows();
    const auto findRow = [capacityRows]( const char* ownerName )
    {
        return std::find_if( capacityRows.begin(), capacityRows.end(), [ownerName]( const auto& row )
                             { return row.ownerName && std::strcmp( row.ownerName, ownerName ) == 0; } );
    };
    struct ExpectedOwner
    {
        const char* name;
        const char* reason;
        int capacity;
    };
    const ExpectedOwner expectedOwners[] = {
        { "SpatialGrid.entries", SkullbonezCore::Physics::PhysicsCapacityReason::SpatialGridPersistentEntries, 1048 },
        { "SpatialGrid.bodyMemberships", SkullbonezCore::Physics::PhysicsCapacityReason::SpatialGridBodyMemberships, 3 },
        { "SpatialGrid.pairMembershipOrdinals",
          SkullbonezCore::Physics::PhysicsCapacityReason::SpatialGridPairMembershipOrdinals, 5144 },
        { "SpatialGrid.pairMembershipOffsets",
          SkullbonezCore::Physics::PhysicsCapacityReason::SpatialGridPairMembershipOffsets, 4 },
        { "SpatialGrid.pairMembershipCounts",
          SkullbonezCore::Physics::PhysicsCapacityReason::SpatialGridPairMembershipCounts, 3 },
        { "SpatialGrid.candidatePairHeads", SkullbonezCore::Physics::PhysicsCapacityReason::SpatialGridCandidatePairHeads,
          3 },
        { "SpatialGrid.candidatePairNodes", SkullbonezCore::Physics::PhysicsCapacityReason::SpatialGridCandidatePairNodes,
          12 },
        { "SpatialGrid.candidatePairSortKeys",
          SkullbonezCore::Physics::PhysicsCapacityReason::SpatialGridCandidatePairSortKeys, 12 },
        { "SpatialGrid.candidatePairSortScratch",
          SkullbonezCore::Physics::PhysicsCapacityReason::SpatialGridCandidatePairSortScratch, 12 },
        { "SpatialGrid.cellObjectSeen", SkullbonezCore::Physics::PhysicsCapacityReason::SpatialGridCellObjectSeen, 3 },
        { "SpatialGrid.overlayEntries", SkullbonezCore::Physics::PhysicsCapacityReason::SpatialGridSweptOverlayEntries,
          4096 },
    };

    for ( const ExpectedOwner& expected : expectedOwners )
    {
        const auto row = findRow( expected.name );
        REQUIRE( row != capacityRows.end() );
        CHECK( std::strcmp( row->capacityReason, expected.reason ) == 0 );
        CHECK( row->currentCapacity == expected.capacity );
    }
}


TEST_CASE( "SpatialGrid: scene reserve covers the multi-oversized shoreline layout" )
{
    auto grid = std::make_unique<SpatialGrid>( 24.0f );

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        grid->ReserveSceneCapacity( 4u );
    }

    grid->BeginFrame( 4 );

    // Invariant: these conservative spheres reproduce the four authored
    // shoreline-lever bodies. Their combined persistent occupancy exceeds the
    // retired 64-row reservation and must still fit without physics-phase growth.
    grid->Insert( 0, Vector3( 500.0f, 1.6f, 540.0f ), 42.27f );
    grid->Insert( 1, Vector3( 498.0f, 1.5f, 462.0f ), 29.19f );
    grid->Insert( 2, Vector3( 500.0f, 3.6f, 492.0f ), 9.48f );
    grid->Insert( 3, Vector3( 490.0f, 2.7f, 516.0f ), 9.48f );

    CHECK( grid->GetPersistentEntryHighWater() > 64u );
    CHECK( grid->GetPersistentEntryHighWater() <= grid->GetPersistentEntryCapacity() );
}


TEST_CASE( "SpatialGrid: scene reserve covers dense ordinary occupancy plus one oversized body" )
{
    auto grid = std::make_unique<SpatialGrid>( 10.0f );
    constexpr int ordinaryBodyCount = 100;
    constexpr int totalBodyCount = ordinaryBodyCount + 1;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        grid->ReserveSceneCapacity( totalBodyCount );
    }

    grid->BeginFrame( totalBodyCount );

    for ( int bodyIndex = 0; bodyIndex < ordinaryBodyCount; ++bodyIndex )
    {
        const float separatedCellBoundary = static_cast<float>( bodyIndex * 100 );
        grid->Insert( bodyIndex, Vector3( separatedCellBoundary, 0.0f, 0.0f ), 1.0f );
    }

    // Invariant: the oversized row reproduces the stress layout's combination
    // of more than eight persistent cells for one body and dense ordinary rows.
    grid->Insert( ordinaryBodyCount, Vector3( 20000.0f, 0.0f, 0.0f ), 40.0f );

    CHECK( grid->GetPersistentEntryHighWater() > 1340u );
    CHECK( grid->GetPersistentEntryHighWater() <= grid->GetPersistentEntryCapacity() );
}


TEST_CASE( "SpatialGrid: additional scene reserve preserves live membership and extends only new dense rows" )
{
    auto grid = std::make_unique<SpatialGrid>( 10.0f );

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        grid->ReserveSceneCapacity( 2u );
    }

    grid->BeginFrame( 2 );
    grid->Insert( 0, Vector3( 5.0f, 5.0f, 5.0f ), 1.0f );
    grid->Insert( 1, Vector3( 6.0f, 5.0f, 5.0f ), 1.0f );
    REQUIRE( CandidatePairs( *grid ).size() == 1u );
    const std::size_t entryHighWater = grid->GetPersistentEntryHighWater();

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        grid->ReserveSceneCapacity( 4u );
    }

    const auto pairsAfterReserve = CandidatePairs( *grid );
    REQUIRE( pairsAfterReserve.size() == 1u );
    CHECK( pairsAfterReserve[0] == std::make_pair( 0, 1 ) );
    CHECK( grid->GetPersistentEntryHighWater() == entryHighWater );
    CHECK( grid->GetBodyMembershipCapacity() == 4u );
    CHECK( grid->GetPairMembershipOrdinalCapacity() == 5152u );
    CHECK( grid->GetPairMembershipOffsetCapacity() == 5u );
    CHECK( grid->GetPairMembershipCountCapacity() == 4u );
    CHECK( grid->GetCellObjectSeenCapacity() == 4u );

    grid->BeginFrame( 4 );
    grid->Insert( 2, Vector3( 25.0f, 5.0f, 5.0f ), 1.0f );
    grid->Insert( 3, Vector3( 26.0f, 5.0f, 5.0f ), 1.0f );
    CHECK( HasPair( CandidatePairs( *grid ), 2, 3 ) );
}


TEST_CASE( "SpatialGrid: Clear and cell-size reset retain scene backing and high-water" )
{
    auto grid = std::make_unique<SpatialGrid>( 10.0f );

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        grid->ReserveSceneCapacity( 2u );
    }

    grid->BeginFrame( 2 );
    grid->Insert( 0, Vector3( 5.0f, 5.0f, 5.0f ), 1.0f );
    grid->Insert( 1, Vector3( 6.0f, 5.0f, 5.0f ), 1.0f );
    REQUIRE( CandidatePairs( *grid ).size() == 1u );
    grid->InsertSwept( 0, Vector3( 5.0f, 5.0f, 5.0f ), Vector3( 20.0f, 0.0f, 0.0f ), 1.0f );
    CHECK( grid->GetPersistentEntryHighWater() == 2u );
    CHECK( grid->GetPairMembershipOrdinalHighWater() == 2u );
    CHECK( grid->GetSweptOverlayEntryHighWater() > 0u );

    const std::size_t entryCapacity = grid->GetPersistentEntryCapacity();
    const std::size_t pairMembershipOrdinalCapacity = grid->GetPairMembershipOrdinalCapacity();
    const std::size_t pairMembershipOffsetCapacity = grid->GetPairMembershipOffsetCapacity();
    const std::size_t pairMembershipCountCapacity = grid->GetPairMembershipCountCapacity();
    const std::size_t overlayCapacity = grid->GetSweptOverlayEntryCapacity();
    const std::size_t overlayHighWater = grid->GetSweptOverlayEntryHighWater();
    grid->Clear();
    grid->SetCellSize( 5.0f );

    CHECK( grid->GetPersistentEntryCapacity() == entryCapacity );
    CHECK( grid->GetPairMembershipOrdinalCapacity() == pairMembershipOrdinalCapacity );
    CHECK( grid->GetPairMembershipOffsetCapacity() == pairMembershipOffsetCapacity );
    CHECK( grid->GetPairMembershipCountCapacity() == pairMembershipCountCapacity );
    CHECK( grid->GetSweptOverlayEntryCapacity() == overlayCapacity );
    CHECK( grid->GetPersistentEntryHighWater() == 2u );
    CHECK( grid->GetPairMembershipOrdinalHighWater() == 2u );
    CHECK( grid->GetSweptOverlayEntryHighWater() == overlayHighWater );
    CHECK( grid->GetActiveCellCount() == 0 );
}


TEST_CASE( "SpatialGrid: BeginFrame shrinks retained rows without growing backing" )
{
    using SkullbonezCore::Core::Allocation::ResetRuntimeAllocationCounters;
    using SkullbonezCore::Core::Allocation::RuntimeAllocationGuardMode;
    using SkullbonezCore::Core::Allocation::RuntimeAllocationGuardViolationCount;
    using SkullbonezCore::Core::Allocation::RuntimeAllocationPhase;
    using SkullbonezCore::Core::Allocation::RuntimeAllocationScope;
    using SkullbonezCore::Core::Allocation::SetRuntimeAllocationGuardMode;

    auto grid = std::make_unique<SpatialGrid>( 10.0f );
    {
        RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
        grid->ReserveSceneCapacity( 2u );
    }

    grid->BeginFrame( 2 );
    grid->Insert( 0, Vector3( 5.0f, 5.0f, 5.0f ), 1.0f );
    grid->Insert( 1, Vector3( 15.0f, 5.0f, 5.0f ), 1.0f );
    const std::size_t entryCapacity = grid->GetPersistentEntryCapacity();
    const std::size_t bodyMembershipCapacity = grid->GetBodyMembershipCapacity();
    const std::size_t pairMembershipOrdinalCapacity = grid->GetPairMembershipOrdinalCapacity();
    const std::size_t pairMembershipOffsetCapacity = grid->GetPairMembershipOffsetCapacity();
    const std::size_t pairMembershipCountCapacity = grid->GetPairMembershipCountCapacity();
    const std::size_t candidatePairHeadCapacity = grid->GetCandidatePairHeadCapacity();
    const std::size_t candidatePairNodeCapacity = grid->GetCandidatePairNodeCapacity();
    const std::size_t candidatePairSortKeyCapacity = grid->GetCandidatePairSortKeyCapacity();
    const std::size_t candidatePairSortScratchCapacity = grid->GetCandidatePairSortScratchCapacity();
    const std::size_t cellObjectSeenCapacity = grid->GetCellObjectSeenCapacity();
    const std::size_t sweptOverlayEntryCapacity = grid->GetSweptOverlayEntryCapacity();

    ResetRuntimeAllocationCounters();
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Gameplay );
    {
        RuntimeAllocationScope gameplayScope( RuntimeAllocationPhase::Physics );
        grid->BeginFrame( 1 );
    }
    const uint64_t violations = RuntimeAllocationGuardViolationCount();
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Off );

    CHECK( violations == 0u );
    CHECK( grid->GetPersistentEntryCapacity() == entryCapacity );
    CHECK( grid->GetBodyMembershipCapacity() == bodyMembershipCapacity );
    CHECK( grid->GetPairMembershipOrdinalCapacity() == pairMembershipOrdinalCapacity );
    CHECK( grid->GetPairMembershipOffsetCapacity() == pairMembershipOffsetCapacity );
    CHECK( grid->GetPairMembershipCountCapacity() == pairMembershipCountCapacity );
    CHECK( grid->GetCandidatePairHeadCapacity() == candidatePairHeadCapacity );
    CHECK( grid->GetCandidatePairNodeCapacity() == candidatePairNodeCapacity );
    CHECK( grid->GetCandidatePairSortKeyCapacity() == candidatePairSortKeyCapacity );
    CHECK( grid->GetCandidatePairSortScratchCapacity() == candidatePairSortScratchCapacity );
    CHECK( grid->GetCellObjectSeenCapacity() == cellObjectSeenCapacity );
    CHECK( grid->GetSweptOverlayEntryCapacity() == sweptOverlayEntryCapacity );
    CHECK( grid->GetMaintenanceStats().removedBodies == 1 );
}


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
#if defined( _DEBUG )
    CHECK( grid.GetPairMembershipUniqueCountForTest( 0 ) == 1u );
    CHECK( grid.GetPairMembershipUniqueCountForTest( 1 ) == 1u );
    CHECK( grid.GetPairMembershipUniqueCountForTest( 2 ) == 0u );
    CHECK( grid.GetPairMembershipUniqueCountForTest( 3 ) == 0u );
#endif

    // Membership persists into the next frame; changing only the stamp selects
    // the other occupied cell without reinsertion.
    grid.BeginFrame( 4 );
    grid.MarkPairSourceCells( 2 );
    grid.GetCandidatePairs( pairs, true );
    REQUIRE( pairs.size() == 1u );
    CHECK( pairs[0] == std::make_pair( 2, 3 ) );
#if defined( _DEBUG )
    CHECK( grid.GetPairMembershipUniqueCountForTest( 0 ) == 0u );
    CHECK( grid.GetPairMembershipUniqueCountForTest( 1 ) == 0u );
    CHECK( grid.GetPairMembershipUniqueCountForTest( 2 ) == 1u );
    CHECK( grid.GetPairMembershipUniqueCountForTest( 3 ) == 1u );
#endif
}


TEST_CASE( "SpatialGrid: earliest eligible shared bucket projects out an earlier unstamped bucket" )
{
    SpatialGrid& grid = TestGrid();
    grid.SetCellSize( 1.0f );
    grid.BeginFrame( 2 );

    // Both bodies retain the unstamped start cell. Their swept overlay stamps
    // cells one and two, so the membership index must omit the earlier
    // ineligible shared ordinal before assigning first-seen ownership.
    grid.InsertSwept( 0, Vector3( 0.25f, 0.25f, 0.25f ), Vector3( 2.0f, 0.0f, 0.0f ), 0.0f );
    grid.InsertSwept( 1, Vector3( 0.25f, 0.25f, 0.25f ), Vector3( 2.0f, 0.0f, 0.0f ), 0.0f );
    std::vector<std::pair<int, int>> pairs;
    pairs.reserve( 1u );
    grid.GetCandidatePairs( pairs, true );

    REQUIRE( pairs.size() == 1u );
    CHECK( pairs[0] == std::make_pair( 0, 1 ) );
#if defined( _DEBUG )
    CHECK( grid.GetPairMembershipUniqueCountForTest( 0 ) == 2u );
    CHECK( grid.GetPairMembershipUniqueCountForTest( 1 ) == 2u );
#endif

    auto bodyStore = std::make_unique<PhysicsBodyStore>();
    auto colliderStore = std::make_unique<ColliderStore>();
    SkullbonezCore::Physics::PhysicsCandidatePairList filteredPairs {
        "TestSpatialGrid.earliestEligibleFilteredPairs",
        SkullbonezCore::Physics::PhysicsCapacityReason::ExplicitTestCapacity,
    };

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        bodyStore->ReserveCapacity( 2u );
        colliderStore->ReserveCapacity( 2u );
        colliderStore->ReserveShapeCapacity( 2u, 0u, 0u );
        filteredPairs.Reserve( 1u );
    }

    const Vector3 source( 0.25f, 0.25f, 0.25f );
    const CollisionShape sphere( BoundingSphere( 0.1f, Vector3( 0.0f, 0.0f, 0.0f ), 0.0f ) );

    for ( int bodyIndex = 0; bodyIndex < 2; ++bodyIndex )
    {
        PhysicsBodyCreateRecord body;
        body.hot.position = source;
        body.hot.boundingRadius = 0.1f;
        const auto bodyHandle = bodyStore->CreateBodyRecord( body );
        ColliderRecord collider;
        collider.body = bodyHandle;
        collider.boundingRadius = 0.1f;
        (void)SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( *colliderStore, collider, sphere );
    }

    const std::vector<uint8_t> sleepState( 2u, 0u );
    grid.GetFilteredCandidatePairs( filteredPairs, *bodyStore, *colliderStore, sleepState, 0.0f, 0.0f, true );
    REQUIRE( filteredPairs.size() == 1u );
    CHECK( filteredPairs[0] == std::make_pair( 0, 1 ) );
}


TEST_CASE( "SpatialGrid: restricted pairing joins persistent and swept membership in one bucket" )
{
    SpatialGrid& grid = TestGrid();
    grid.SetCellSize( 1.0f );
    grid.BeginFrame( 2 );
    grid.Insert( 0, Vector3( 2.25f, 0.25f, 0.25f ), 0.0f );
    grid.InsertSwept( 1, Vector3( 0.25f, 0.25f, 0.25f ), Vector3( 2.0f, 0.0f, 0.0f ), 0.0f );
    std::vector<std::pair<int, int>> pairs;
    pairs.reserve( 1u );
    grid.GetCandidatePairs( pairs, true );

    // The swept body stamps cell two. That bucket's other occupant is a
    // persistent row, proving one ordinal prefix merges both source stores
    // before earliest-shared evaluation.
    REQUIRE( pairs.size() == 1u );
    CHECK( pairs[0] == std::make_pair( 0, 1 ) );
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


TEST_CASE( "SpatialGrid: exact coordinate hash aliases share one conservative bucket" )
{
    SpatialGrid& grid = TestGrid();
    grid.SetCellSize( 1.0f );
    grid.BeginFrame( 2 );
    const Vector3 positions[] = {
        Vector3( 0.25f, -0.75f, 1.25f ),
        Vector3( 0.25f, 1.25f, -0.75f ),
    };

    // Hazard: cells (0,-1,1) and (0,1,-1) both hash to -98,484,010.
    // Bucket identity intentionally preserves the resulting false positive;
    // exact-coordinate membership would silently change the broadphase.
    grid.Insert( 0, positions[0], 0.1f );
    grid.Insert( 1, positions[1], 0.1f );
    const auto unfilteredPairs = CandidatePairs( grid, 1 );
    REQUIRE( unfilteredPairs.size() == 1u );
    CHECK( unfilteredPairs[0] == std::make_pair( 0, 1 ) );
    CHECK( grid.GetActiveCellCount() == 1 );

    auto bodyStore = std::make_unique<PhysicsBodyStore>();
    auto colliderStore = std::make_unique<ColliderStore>();
    SkullbonezCore::Physics::PhysicsCandidatePairList filteredPairs {
        "TestSpatialGrid.hashAliasFilteredPairs",
        SkullbonezCore::Physics::PhysicsCapacityReason::ExplicitTestCapacity,
    };

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        bodyStore->ReserveCapacity( 2u );
        colliderStore->ReserveCapacity( 2u );
        colliderStore->ReserveShapeCapacity( 2u, 0u, 0u );
        filteredPairs.Reserve( 1u );
    }

    const CollisionShape sphere( BoundingSphere( 0.1f, Vector3( 0.0f, 0.0f, 0.0f ), 0.0f ) );

    for ( int bodyIndex = 0; bodyIndex < 2; ++bodyIndex )
    {
        PhysicsBodyCreateRecord body;
        body.hot.position = positions[bodyIndex];
        body.hot.boundingRadius = 0.1f;
        const auto bodyHandle = bodyStore->CreateBodyRecord( body );
        ColliderRecord collider;
        collider.body = bodyHandle;
        collider.boundingRadius = 0.1f;
        (void)SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( *colliderStore, collider, sphere );
    }

    const std::vector<uint8_t> sleepState( 2u, 0u );
    grid.MarkPairSourceCells( 0 );
    grid.GetFilteredCandidatePairs( filteredPairs, *bodyStore, *colliderStore, sleepState, 0.0f, 0.0f, true );
    CHECK( filteredPairs.empty() );
}


TEST_CASE( "SpatialGrid: swept coordinate aliases retain only pair-bearing bucket memberships" )
{
    auto grid = std::make_unique<SpatialGrid>( 1.0f );

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        grid->ReserveSceneCapacity( 2u );
    }

    grid->BeginFrame( 2 );
    const Vector3 source( 0.25f, -0.75f, 1.25f );
    const Vector3 displacement( 0.0f, 2.0f, -2.0f );
    grid->InsertSwept( 0, source, displacement, 0.0f );
    grid->Insert( 1, source, 0.0f );

    const auto pairs = CandidatePairs( *grid, 1 );
    REQUIRE( pairs.size() == 1u );
    CHECK( pairs[0] == std::make_pair( 0, 1 ) );
    CHECK( grid->GetActiveCellCount() == 7 );
    CHECK( grid->GetPairMembershipOrdinalHighWater() == 10u );
#if defined( _DEBUG )
    CHECK( grid->GetPairMembershipUniqueCountForTest( 0 ) == 1u );
    CHECK( grid->GetPairMembershipUniqueCountForTest( 1 ) == 1u );
#endif
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
        { 0, 1 }, { 0, 2 }, { 0, 3 }, { 0, 4 }, { 1, 2 }, { 1, 3 }, { 1, 4 }, { 2, 3 }, { 2, 4 }, { 3, 4 },
    };

    // Invariant: solver work order is a function of normalized body identity,
    // never the bucket-creation or linked-list order used to discover a pair.
    CHECK( pairs == expected );
}


TEST_CASE( "SpatialGrid: filtered first-seen order stays observable when geometry rejects a pair" )
{
    SpatialGrid& grid = TestGrid();
    grid.SetCellSize( SpatialGrid::MIN_CELL_SIZE );
    grid.BeginFrame( 3 );

    auto bodyStore = std::make_unique<PhysicsBodyStore>();
    auto colliderStore = std::make_unique<ColliderStore>();
    SkullbonezCore::Physics::PhysicsCandidatePairList candidatePairs {
        "TestSpatialGrid.firstSeenCandidates",
        SkullbonezCore::Physics::PhysicsCapacityReason::ExplicitTestCapacity,
    };

    SkullbonezCore::Physics::PhysicsCandidatePairList sleepPrunedPairs {
        "TestSpatialGrid.firstSeenSleepPruned",
        SkullbonezCore::Physics::PhysicsCapacityReason::ExplicitTestCapacity,
    };

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        bodyStore->ReserveCapacity( 3u );
        colliderStore->ReserveCapacity( 3u );
        colliderStore->ReserveShapeCapacity( 3u, 0u, 0u );
        candidatePairs.Reserve( 3u );
        sleepPrunedPairs.Reserve( 3u );
    }

    const Vector3 positions[] = {
        Vector3( 0.01f, 100.01f, 0.01f ),
        Vector3( 0.25f, 100.25f, 0.25f ),
        Vector3( 0.49f, 100.49f, 0.49f ),
    };

    const CollisionShape sphere( BoundingSphere( 0.25f, Vector3( 0.0f, 0.0f, 0.0f ), 0.0f ) );

    for ( int bodyIndex = 0; bodyIndex < 3; ++bodyIndex )
    {
        PhysicsBodyCreateRecord body;
        body.hot.position = positions[bodyIndex];
        body.hot.boundingRadius = 0.25f;
        const auto bodyHandle = bodyStore->CreateBodyRecord( body );

        ColliderRecord collider;
        collider.body = bodyHandle;
        collider.boundingRadius = 0.25f;
        (void)SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( *colliderStore, collider, sphere );
        grid.Insert( bodyIndex, positions[bodyIndex], 0.25f );
    }

    const std::vector<uint8_t> sleepState( 3u, 1u );
    grid.GetFilteredCandidatePairs( candidatePairs, *bodyStore, *colliderStore, sleepState, 0.0f, 0.0f, sleepPrunedPairs,
                                    false );

    CHECK( candidatePairs.empty() );
    REQUIRE( sleepPrunedPairs.size() == 2u );

    // Hazard: pair (0,2) shares a cell but fails geometry. The other two rows
    // deliberately pin traversal-first order rather than canonical sort order.
    CHECK( sleepPrunedPairs[0] == std::make_pair( 1, 2 ) );
    CHECK( sleepPrunedPairs[1] == std::make_pair( 0, 1 ) );
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
        (void)SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliderStore, collider, sphere );
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
        const Vector3 center( random.Float( -50.0f, 50.0f ), random.Float( -50.0f, 50.0f ), random.Float( -50.0f, 50.0f ) );
        const float radius = sample % 8 == 0 ? 0.0f : random.Float( 0.0f, 4.0f );

        grid.Insert( 0, center, radius );
        grid.Insert( 1, center, radius );
        const auto pairs = CandidatePairs( grid );

        REQUIRE( pairs.size() == 1u );
        CHECK( pairs[0] == std::make_pair( 0, 1 ) );
    }
}
