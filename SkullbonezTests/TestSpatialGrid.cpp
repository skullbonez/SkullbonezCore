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
//   Generation clear: O(1) clear that marks old buckets stale instead of
//     zeroing the whole table.
//
// Invariants:
//   - Output pair vectors must reserve capacity before GetCandidatePairs().
//   - Candidate pairs are normalized as (smaller index, larger index).
//   - Clear() is the public removal path; there is no per-object remove API.
//
// Related:
//   - SkullbonezSource/Physics/SpatialGrid.h
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"
#include "TestFixedSeed.h"

#include "../SkullbonezSource/Physics/SpatialGrid.h"

#include <utility>
#include <vector>

using SkullbonezCore::Math::CollisionDetection::SpatialGrid;
using SkullbonezCore::Math::Vector::Vector3;

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


TEST_CASE( "SpatialGrid: clear removes old generation contents from queries" )
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


TEST_CASE( "Property invariant: prepared AABB insert/query round-trips including zero extent [seed 0x16AABB00]" )
{
    SkullbonezTests::FixedSeed random( 0x16AABB00u );
    SpatialGrid& grid = TestGrid();

    // Invariant: two identical prepared bounds always share at least one cell,
    // even when min == max on every axis at a cell boundary.
    for ( int sample = 0; sample < 64; ++sample )
    {
        grid.Clear();
        const Vector3 center( random.Float( -50.0f, 50.0f ),
                              random.Float( -50.0f, 50.0f ),
                              random.Float( -50.0f, 50.0f ) );
        const Vector3 extent = ( sample % 8 == 0 )
                                   ? Vector3( 0.0f, 0.0f, 0.0f )
                                   : Vector3( random.Float( 0.0f, 4.0f ),
                                              random.Float( 0.0f, 4.0f ),
                                              random.Float( 0.0f, 4.0f ) );
        const Vector3 minimum = center - extent;
        const Vector3 maximum = center + extent;

        const Vector3 noDisplacement( 0.0f, 0.0f, 0.0f );
        grid.InsertPreparedBounds( 0, center, noDisplacement, 0.0f, minimum, maximum, false );
        grid.InsertPreparedBounds( 1, center, noDisplacement, 0.0f, minimum, maximum, false );
        const auto pairs = CandidatePairs( grid );

        REQUIRE( pairs.size() == 1u );
        CHECK( pairs[0] == std::make_pair( 0, 1 ) );
    }
}
