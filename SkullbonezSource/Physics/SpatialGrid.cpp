/*
File: SkullbonezSource/Physics/SpatialGrid.cpp
Purpose:
  Partitions space into broadphase cells so physics can test nearby objects cheaply.

Summary:
  Persistent per-body integer ranges own ordinary cell membership across fixed
  steps. Fixed hash chains and intrusive back-links make changed cells reusable
  without allocation. A separately stamped overlay carries swept motion for one
  step only, while canonical emission hides all storage-history ordering.

Glossary:
  Intrusive back-link: Index stored in pooled rows so removal can unlink both
    the cell chain and body chain without searching global storage.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - SpatialGrid capacity and index failures are Lane F invariants: callers
    cannot recover while preserving the frame's broadphase contract.
  - Persistent membership is a pure function of current integer ranges; only
    storage order carries history, and canonical pair emission removes it.
  - Production pair work projects unstamped sleep-only cells out of its ordinal
    slices. Debug still walks those raw chains for integrity accounting.

Related:
  - SkullbonezSource/Physics/SpatialGrid.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reports/2026-07-29/broadphase-canonical-order-guard-bg0.md
  - Agentic/Reference/engine-glossary.md
*/

// Concept: the grid is a cheap maybe-colliding filter, not a collision solver.
//
// Each occupied cell emits every pair inside the cell, so a cell that is too
// large turns into a local O(n^2) pair factory. PhysicsWorld sizes the cell from
// current scene primitives before insertion; this type only owns hashing,
// storage, and duplicate-pair suppression.

#include "SpatialGrid.h"
#include "SolverBroadphaseStage.h"
#include "../Core/FatalError.h"
#include <algorithm>
#include <cfloat>
#include <climits>


using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Vector;

namespace
{
constexpr int RequiredBitCount( uint32_t value )
{
    int bitCount = 0;

    while ( value != 0u )
    {
        ++bitCount;
        value >>= 1u;
    }

    return bitCount;
}

// Invariant: canonical pair emission sorts every valid body index with one
// stable low-digit pass followed by one stable high-digit pass. These values
// derive from the scene ceiling, while the layout assertion deliberately
// requires a source review if that ceiling changes.
constexpr int kCandidatePairSortBitCount = RequiredBitCount( static_cast<uint32_t>( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS - 1 ) );
constexpr int kCandidatePairSortLowBitCount = ( kCandidatePairSortBitCount + 1 ) / 2;
constexpr int kCandidatePairSortHighBitCount = kCandidatePairSortBitCount - kCandidatePairSortLowBitCount;
constexpr int kCandidatePairSortLowBucketCount = 1 << kCandidatePairSortLowBitCount;
constexpr int kCandidatePairSortHighBucketCount = 1 << kCandidatePairSortHighBitCount;
constexpr int kCandidatePairSortLowMask = kCandidatePairSortLowBucketCount - 1;
constexpr int kCandidatePairSortHighMask = kCandidatePairSortHighBucketCount - 1;
constexpr int kCandidatePairSortMaximumIndex = ( kCandidatePairSortHighMask << kCandidatePairSortLowBitCount ) |
                                               kCandidatePairSortLowMask;

static_assert( kCandidatePairSortLowBitCount == 7 && kCandidatePairSortHighBitCount == 6,
               "MAX_SCENE_OBJECTS changed: review the canonical pair radix layout and instruction footprint." );
static_assert( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS - 1 <= kCandidatePairSortMaximumIndex,
               "Canonical pair radix digits must address every valid scene body index." );

void ValidateBroadphaseBounds( int index, const Vector3& minBounds, const Vector3& maxBounds, float inverseCellSize )
{
    const bool finite = std::isfinite( minBounds.x ) && std::isfinite( minBounds.y ) && std::isfinite( minBounds.z ) &&
                        std::isfinite( maxBounds.x ) && std::isfinite( maxBounds.y ) && std::isfinite( maxBounds.z );

    const float extent = SpatialGrid::MAX_WORLD_COORDINATE;
    const bool insideExtent = finite && fabsf( minBounds.x ) <= extent && fabsf( minBounds.y ) <= extent &&
                              fabsf( minBounds.z ) <= extent && fabsf( maxBounds.x ) <= extent &&
                              fabsf( maxBounds.y ) <= extent && fabsf( maxBounds.z ) <= extent;

    const bool ordered = finite && minBounds.x <= maxBounds.x && minBounds.y <= maxBounds.y && minBounds.z <= maxBounds.z;

    constexpr double MAX_CONVERTIBLE_CELL_COORDINATE = static_cast<double>( INT_MAX ) - 1024.0;
    const auto cellCoordinateIsRepresentable = [&]( float value )
    {

        // Why: perform the guard in double. Converting INT_MAX-1 to float rounds
        // up to 2^31 on MSVC, which would bless the very value the later int
        // conversion cannot represent.
        return std::fabs( static_cast<double>( value ) * static_cast<double>( inverseCellSize ) ) <=
               MAX_CONVERTIBLE_CELL_COORDINATE;
    };

    const bool convertible = insideExtent && cellCoordinateIsRepresentable( minBounds.x ) &&
                             cellCoordinateIsRepresentable( minBounds.y ) && cellCoordinateIsRepresentable( minBounds.z ) &&
                             cellCoordinateIsRepresentable( maxBounds.x ) && cellCoordinateIsRepresentable( maxBounds.y ) &&
                             cellCoordinateIsRepresentable( maxBounds.z );

    if ( !insideExtent || !ordered || !convertible )
    {

        // Lane F: non-finite, inverted, or unrepresentable physics bounds are
        // corrupt engine state. Continuing would either hide the body from
        // broadphase or invoke undefined float-to-int conversion behavior.
        SB_FATAL( "Physics/SpatialGrid",
                  "SpatialGrid bounds invalid: body=%d min=(%.9g,%.9g,%.9g) max=(%.9g,%.9g,%.9g) "
                  "max_world_coordinate=%.9g.",
                  index, minBounds.x, minBounds.y, minBounds.z, maxBounds.x, maxBounds.y, maxBounds.z, extent );
    }
}

int16_t ClampVisualizationCell( int cell )
{

    // The hash key retains the full cell coordinate. Only the debug
    // visualization payload is narrowed, so saturate instead of wrapping it.
    return static_cast<int16_t>( (std::max)( SpatialGrid::MIN_VISUALIZATION_CELL_COORDINATE,
                                             (std::min)( SpatialGrid::MAX_VISUALIZATION_CELL_COORDINATE, cell ) ) );
}

int64_t SpatialCellKey( int ix, int iy, int iz )
{
    return ( static_cast<int64_t>( ix ) * 73856093 ) ^ ( static_cast<int64_t>( iy ) * 19349663 ) ^
           ( static_cast<int64_t>( iz ) * 83492791 );
}
} // namespace


SpatialGrid::SpatialGrid( float requestedCellSize )
    : cellSize( 1.0f ), inverseCellSize( 1.0f ), overlayGeneration( 1 ), pairSourceGeneration( 1 ), freeBucketHead( -1 ),
      freeEntryHead( -1 ), persistentEntryCount( 0 ), objectCount( 0 ), activeBucketCount( 0 ), overlayEntryCount( 0 ),
      overlayActiveBucketCount( 0 ), cellObjectGeneration( 0 )
{
    Clear();
    SetCellSize( requestedCellSize );
}


void SpatialGrid::ReserveSceneCapacity( std::size_t bodyCapacity )
{
    const std::size_t persistentEntryCapacity = bodyCapacity * static_cast<std::size_t>( PERSISTENT_ENTRIES_PER_BODY ) +
                                                PERSISTENT_ENTRY_SPILL_ROWS;

    const std::size_t candidatePairCapacity = Physics::PhysicsCandidatePairCapacity( bodyCapacity );

    // Why: swept occupancy has its own overlay store. The retired additional
    // 4,096 persistent rows duplicated that unrelated ceiling; production's
    // scene-derived cell size bounds ordinary membership to eight cells/body.
    entries.Reserve( persistentEntryCapacity );
    overlayEntries.Reserve( MAX_SWEPT_CELL_ENTRIES );
    bodyMemberships.Reserve( bodyCapacity );
    pairMembershipOrdinals.Reserve( entries.capacity() + overlayEntries.capacity() );
    pairMembershipOffsets.Reserve( bodyCapacity + 1u );
    pairMembershipCounts.Reserve( bodyCapacity );
    candidatePairHeads.Reserve( bodyCapacity );
    candidatePairNodes.Reserve( candidatePairCapacity );
    candidatePairSortKeys.Reserve( candidatePairCapacity );
    candidatePairSortScratch.Reserve( candidatePairCapacity );
    cellObjectSeen.Reserve( bodyCapacity );

    // Lifetime: additional authored-capacity admission can run while existing
    // bodies retain memberships and generation stamps. Grow only the new dense
    // suffix; transient staging remains untouched until its owning reset path.
    bodyMemberships.ExtendDefaultTo( bodyCapacity );
    cellObjectSeen.ExtendDefaultTo( bodyCapacity );
}


void SpatialGrid::SetCellSize( float requestedCellSize )
{

    if ( requestedCellSize < MIN_CELL_SIZE || !std::isfinite( requestedCellSize ) )
    {

        // Lane F: an invalid cell size makes every subsequent float-to-cell
        // conversion unsafe; construction and runtime reconfiguration share
        // this owner boundary.
        SB_FATAL( "Physics/SpatialGrid", "SpatialGrid cell size invalid: value=%.9g minimum=%.9g.", requestedCellSize,
                  MIN_CELL_SIZE );
    }

    if ( requestedCellSize == cellSize )
    {
        return;
    }

    cellSize = requestedCellSize;
    inverseCellSize = 1.0f / requestedCellSize;

    // Invariant: integer cell ranges are meaningful only for the cell size that
    // produced them. Runtime tuning and scene loads are cold rebuild boundaries.
    Clear();
}


// Full reset is intentionally cold. BeginFrame owns the steady-step transition.
void SpatialGrid::Clear()
{

    // Cold path: scene load, replay restore, or a cell-size change may invalidate
    // every dense-row membership. Steady fixed steps use BeginFrame instead.
    std::fill( cellObjectSeen.begin(), cellObjectSeen.end(), 0u );
    entries.clear();
    pairMembershipOrdinals.clear();
    pairMembershipOffsets.clear();
    pairMembershipCounts.clear();
#if defined( _DEBUG )
    pairMembershipLogicalCapacityForTest = ( std::numeric_limits<std::size_t>::max )();
#endif
    candidatePairHeads.clear();
    candidatePairNodes.clear();
    candidatePairSortKeys.clear();
    candidatePairSortScratch.clear();
    overlayEntries.clear();

    for ( int bucketIndex = 0; bucketIndex < TABLE_SIZE; ++bucketIndex )
    {
        bucketHashHeads[bucketIndex] = -1;
        buckets[bucketIndex] = Bucket {};
        buckets[bucketIndex].activeIndex = -1;
        buckets[bucketIndex].nextFree = bucketIndex + 1 < TABLE_SIZE ? bucketIndex + 1 : -1;
    }

    for ( BodyMembership& membership : bodyMemberships )
    {
        membership = BodyMembership {};
    }

    overlayGeneration = 1;
    pairSourceGeneration = 1;
    freeBucketHead = 0;
    freeEntryHead = -1;
    persistentEntryCount = 0;
    objectCount = 0;
    activeBucketCount = 0;
    overlayEntryCount = 0;
    overlayActiveBucketCount = 0;
    cellObjectGeneration = 0;
    maintenanceStats = MaintenanceStats {};
}


void SpatialGrid::BeginFrame( int currentObjectCount )
{

    if ( currentObjectCount < 0 || currentObjectCount > SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS )
    {
        SB_FATAL( "Physics/SpatialGrid", "SpatialGrid frame object count invalid: count=%d capacity=%d.", currentObjectCount,
                  SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    }

    const std::size_t frameObjectCount = static_cast<std::size_t>( currentObjectCount );

    if ( frameObjectCount > bodyMemberships.size() )
    {
        bodyMemberships.ExtendDefaultTo( frameObjectCount );
        cellObjectSeen.ExtendDefaultTo( frameObjectCount );
    }

    maintenanceStats = MaintenanceStats {};
    ResetSweptOverlay();
    ++pairSourceGeneration;

    if ( pairSourceGeneration == 0u )
    {
        pairSourceGeneration = 1u;

        for ( Bucket& bucket : buckets )
        {
            bucket.pairSourceGeneration = 0u;
        }
    }

    for ( int index = currentObjectCount; index < objectCount; ++index )
    {
        RemoveBody( index );
    }

    objectCount = currentObjectCount;
}


int SpatialGrid::FindBucket( int64_t key ) const
{
    const int homeIndex = static_cast<int>( static_cast<uint64_t>( key ) & TABLE_MASK );

    for ( int bucketIndex = bucketHashHeads[homeIndex]; bucketIndex != -1; bucketIndex = buckets[bucketIndex].nextHash )
    {
        const Bucket& bucket = buckets[bucketIndex];

        if ( bucket.occupied && bucket.key == key )
        {
            return bucketIndex;
        }
    }

    return -1;
}


int SpatialGrid::FindOrCreateBucket( int64_t key, int16_t cx, int16_t cy, int16_t cz )
{
    const int existing = FindBucket( key );

    if ( existing != -1 )
    {
        return existing;
    }

    if ( freeBucketHead == -1 )
    {
        SB_FATAL( "Physics/SpatialGrid",
                  "SpatialGrid bucket capacity exceeded: capacity=%d active=%d key=%lld phase=steady_gameplay.", TABLE_SIZE,
                  activeBucketCount, static_cast<long long>( key ) );
    }

    const int bucketIndex = freeBucketHead;
    Bucket& created = buckets[bucketIndex];
    freeBucketHead = created.nextFree;
    const int homeIndex = static_cast<int>( static_cast<uint64_t>( key ) & TABLE_MASK );
    created.key = key;
    created.occupied = true;
    created.nextHash = bucketHashHeads[homeIndex];
    created.previousHash = -1;
    created.nextFree = -1;
    created.head = -1;
    created.count = 0;
    created.overlayGeneration = 0;
    created.overlayHead = -1;
    created.overlayCount = 0;
    created.activeIndex = activeBucketCount;
    created.ix = cx;
    created.iy = cy;
    created.iz = cz;

    if ( created.nextHash != -1 )
    {
        buckets[created.nextHash].previousHash = bucketIndex;
    }

    bucketHashHeads[homeIndex] = bucketIndex;

    if ( activeBucketCount >= TABLE_SIZE )
    {
        SB_FATAL( "Physics/SpatialGrid", "SpatialGrid active bucket capacity exceeded" );
    }

    activeBuckets[activeBucketCount++] = bucketIndex;
    return bucketIndex;
}


void SpatialGrid::RetireBucketIfEmpty( int bucketIndex )
{
    Bucket& bucket = buckets[bucketIndex];
    const bool hasCurrentOverlay = bucket.overlayGeneration == overlayGeneration && bucket.overlayCount > 0;

    if ( !bucket.occupied || bucket.count > 0 || hasCurrentOverlay )
    {
        return;
    }

    const int activeIndex = bucket.activeIndex;

    if ( activeIndex < 0 || activeIndex >= activeBucketCount || activeBuckets[activeIndex] != bucketIndex )
    {
        SB_FATAL( "Physics/SpatialGrid", "SpatialGrid active bucket backlink is corrupt" );
    }

    const int movedBucketIndex = activeBuckets[activeBucketCount - 1];
    activeBuckets[activeIndex] = movedBucketIndex;
    buckets[movedBucketIndex].activeIndex = activeIndex;
    --activeBucketCount;

    const int homeIndex = static_cast<int>( static_cast<uint64_t>( bucket.key ) & TABLE_MASK );

    if ( bucket.previousHash != -1 )
    {
        buckets[bucket.previousHash].nextHash = bucket.nextHash;
    }
    else
    {
        bucketHashHeads[homeIndex] = bucket.nextHash;
    }

    if ( bucket.nextHash != -1 )
    {
        buckets[bucket.nextHash].previousHash = bucket.previousHash;
    }

    bucket.occupied = false;
    bucket.nextHash = -1;
    bucket.previousHash = -1;
    bucket.head = -1;
    bucket.count = 0;
    bucket.overlayHead = -1;
    bucket.overlayCount = 0;
    bucket.activeIndex = -1;
    bucket.nextFree = freeBucketHead;
    freeBucketHead = bucketIndex;
}


int SpatialGrid::AllocatePersistentEntry()
{

    if ( freeEntryHead != -1 )
    {
        const int entryIndex = freeEntryHead;
        freeEntryHead = entries[entryIndex].nextFree;
        ++persistentEntryCount;
        return entryIndex;
    }

    const int entryIndex = static_cast<int>( entries.size() );
    entries.emplace_back();
    ++persistentEntryCount;
    return entryIndex;
}


void SpatialGrid::ReleasePersistentEntry( int entryIndex )
{
    entries[entryIndex].nextFree = freeEntryHead;
    freeEntryHead = entryIndex;
    --persistentEntryCount;
}


void SpatialGrid::InsertPersistentCell( int index, int ix, int iy, int iz )
{
    BodyMembership& membership = bodyMemberships[index];

    for ( int current = membership.entryHead; current != -1; current = entries[current].nextForObject )
    {
        const Entry& entry = entries[current];

        if ( entry.ix == ix && entry.iy == iy && entry.iz == iz )
        {
            return;
        }
    }

    const int bucketIndex = FindOrCreateBucket( SpatialCellKey( ix, iy, iz ), ClampVisualizationCell( ix ),
                                                ClampVisualizationCell( iy ), ClampVisualizationCell( iz ) );

    Bucket& bucket = buckets[bucketIndex];
    const int entryIndex = AllocatePersistentEntry();
    Entry& entry = entries[entryIndex];
    entry.objectIndex = index;
    entry.bucketIndex = bucketIndex;
    entry.nextInBucket = bucket.head;
    entry.previousInBucket = -1;
    entry.nextForObject = membership.entryHead;
    entry.previousForObject = -1;
    entry.ix = ix;
    entry.iy = iy;
    entry.iz = iz;

    if ( bucket.head != -1 )
    {
        entries[bucket.head].previousInBucket = entryIndex;
    }

    if ( membership.entryHead != -1 )
    {
        entries[membership.entryHead].previousForObject = entryIndex;
    }

    bucket.head = entryIndex;
    membership.entryHead = entryIndex;
    ++bucket.count;
    ++maintenanceStats.persistentCellsAdded;
}


void SpatialGrid::RemovePersistentEntry( int entryIndex )
{

    if ( entryIndex < 0 || static_cast<std::size_t>( entryIndex ) >= entries.size() )
    {
        SB_FATAL( "Physics/SpatialGrid", "SpatialGrid persistent entry index out of bounds" );
    }

    Entry& entry = entries[entryIndex];
    Bucket& bucket = buckets[entry.bucketIndex];
    BodyMembership& membership = bodyMemberships[entry.objectIndex];

    if ( entry.previousInBucket != -1 )
    {
        entries[entry.previousInBucket].nextInBucket = entry.nextInBucket;
    }
    else
    {
        bucket.head = entry.nextInBucket;
    }

    if ( entry.nextInBucket != -1 )
    {
        entries[entry.nextInBucket].previousInBucket = entry.previousInBucket;
    }

    if ( entry.previousForObject != -1 )
    {
        entries[entry.previousForObject].nextForObject = entry.nextForObject;
    }
    else
    {
        membership.entryHead = entry.nextForObject;
    }

    if ( entry.nextForObject != -1 )
    {
        entries[entry.nextForObject].previousForObject = entry.previousForObject;
    }

    --bucket.count;
    ++maintenanceStats.persistentCellsRemoved;
    const int bucketIndex = entry.bucketIndex;
    ReleasePersistentEntry( entryIndex );
    RetireBucketIfEmpty( bucketIndex );
}


void SpatialGrid::RemovePersistentCell( int index, int ix, int iy, int iz )
{

    for ( int current = bodyMemberships[index].entryHead; current != -1; current = entries[current].nextForObject )
    {
        const Entry& entry = entries[current];

        if ( entry.ix == ix && entry.iy == iy && entry.iz == iz )
        {
            RemovePersistentEntry( current );
            return;
        }
    }

    SB_FATAL( "Physics/SpatialGrid", "SpatialGrid cached membership missing during removal: body=%d cell=(%d,%d,%d).", index,
              ix, iy, iz );
}


void SpatialGrid::RemoveBody( int index )
{
    BodyMembership& membership = bodyMemberships[index];

    if ( !membership.active )
    {
        return;
    }

    while ( membership.entryHead != -1 )
    {
        RemovePersistentEntry( membership.entryHead );
    }

    membership = BodyMembership {};

    ++maintenanceStats.removedBodies;
}


SpatialGrid::CellRange SpatialGrid::RangeForBounds( int index, const Vector3& minBounds, const Vector3& maxBounds,
                                                    int capacity ) const
{
    ValidateBroadphaseBounds( index, minBounds, maxBounds, inverseCellSize );
    CellRange range {
        static_cast<int>( floorf( minBounds.x * inverseCellSize ) ),
        static_cast<int>( floorf( minBounds.y * inverseCellSize ) ),
        static_cast<int>( floorf( minBounds.z * inverseCellSize ) ),
        static_cast<int>( floorf( maxBounds.x * inverseCellSize ) ),
        static_cast<int>( floorf( maxBounds.y * inverseCellSize ) ),
        static_cast<int>( floorf( maxBounds.z * inverseCellSize ) ),
    };

    const int64_t countX = static_cast<int64_t>( range.maxX ) - static_cast<int64_t>( range.minX ) + 1;
    const int64_t countY = static_cast<int64_t>( range.maxY ) - static_cast<int64_t>( range.minY ) + 1;
    const int64_t countZ = static_cast<int64_t>( range.maxZ ) - static_cast<int64_t>( range.minZ ) + 1;

    if ( countX > capacity || countY > capacity || countZ > capacity || countX * countY > capacity ||
         countX * countY * countZ > capacity )
    {
        SB_FATAL( "Physics/SpatialGrid",
                  "SpatialGrid bounds exceed cell-entry capacity: body=%d cells=(%lld,%lld,%lld) capacity=%d.", index,
                  static_cast<long long>( countX ), static_cast<long long>( countY ), static_cast<long long>( countZ ),
                  capacity );
    }

    return range;
}


void SpatialGrid::InsertRangeDifference( int index, const CellRange& range, const CellRange* excludedRange )
{
    auto insertBox = [&]( int minX, int maxX, int minY, int maxY, int minZ, int maxZ )
    {

        if ( minX > maxX || minY > maxY || minZ > maxZ )
        {
            return;
        }

        for ( int ix = minX; ix <= maxX; ++ix )
        {

            for ( int iy = minY; iy <= maxY; ++iy )
            {

                for ( int iz = minZ; iz <= maxZ; ++iz )
                {
                    InsertPersistentCell( index, ix, iy, iz );
                }
            }
        }
    };

    if ( !excludedRange )
    {
        insertBox( range.minX, range.maxX, range.minY, range.maxY, range.minZ, range.maxZ );
        return;
    }

    const int overlapMinX = (std::max)( range.minX, excludedRange->minX );
    const int overlapMaxX = (std::min)( range.maxX, excludedRange->maxX );
    const int overlapMinY = (std::max)( range.minY, excludedRange->minY );
    const int overlapMaxY = (std::min)( range.maxY, excludedRange->maxY );
    const int overlapMinZ = (std::max)( range.minZ, excludedRange->minZ );
    const int overlapMaxZ = (std::min)( range.maxZ, excludedRange->maxZ );

    if ( overlapMinX > overlapMaxX || overlapMinY > overlapMaxY || overlapMinZ > overlapMaxZ )
    {
        insertBox( range.minX, range.maxX, range.minY, range.maxY, range.minZ, range.maxZ );
        return;
    }

    // Six non-overlapping slabs cover exactly range - overlap. Therefore a
    // one-cell boundary crossing performs two cell operations, not a rebuild.
    insertBox( range.minX, overlapMinX - 1, range.minY, range.maxY, range.minZ, range.maxZ );
    insertBox( overlapMaxX + 1, range.maxX, range.minY, range.maxY, range.minZ, range.maxZ );
    insertBox( overlapMinX, overlapMaxX, range.minY, overlapMinY - 1, range.minZ, range.maxZ );
    insertBox( overlapMinX, overlapMaxX, overlapMaxY + 1, range.maxY, range.minZ, range.maxZ );
    insertBox( overlapMinX, overlapMaxX, overlapMinY, overlapMaxY, range.minZ, overlapMinZ - 1 );
    insertBox( overlapMinX, overlapMaxX, overlapMinY, overlapMaxY, overlapMaxZ + 1, range.maxZ );
}


void SpatialGrid::RemoveRangeDifference( int index, const CellRange& range, const CellRange* retainedRange )
{
    auto removeBox = [&]( int minX, int maxX, int minY, int maxY, int minZ, int maxZ )
    {

        if ( minX > maxX || minY > maxY || minZ > maxZ )
        {
            return;
        }

        for ( int ix = minX; ix <= maxX; ++ix )
        {

            for ( int iy = minY; iy <= maxY; ++iy )
            {

                for ( int iz = minZ; iz <= maxZ; ++iz )
                {
                    RemovePersistentCell( index, ix, iy, iz );
                }
            }
        }
    };

    if ( !retainedRange )
    {
        removeBox( range.minX, range.maxX, range.minY, range.maxY, range.minZ, range.maxZ );
        return;
    }

    const int overlapMinX = (std::max)( range.minX, retainedRange->minX );
    const int overlapMaxX = (std::min)( range.maxX, retainedRange->maxX );
    const int overlapMinY = (std::max)( range.minY, retainedRange->minY );
    const int overlapMaxY = (std::min)( range.maxY, retainedRange->maxY );
    const int overlapMinZ = (std::max)( range.minZ, retainedRange->minZ );
    const int overlapMaxZ = (std::min)( range.maxZ, retainedRange->maxZ );

    if ( overlapMinX > overlapMaxX || overlapMinY > overlapMaxY || overlapMinZ > overlapMaxZ )
    {
        removeBox( range.minX, range.maxX, range.minY, range.maxY, range.minZ, range.maxZ );
        return;
    }

    removeBox( range.minX, overlapMinX - 1, range.minY, range.maxY, range.minZ, range.maxZ );
    removeBox( overlapMaxX + 1, range.maxX, range.minY, range.maxY, range.minZ, range.maxZ );
    removeBox( overlapMinX, overlapMaxX, range.minY, overlapMinY - 1, range.minZ, range.maxZ );
    removeBox( overlapMinX, overlapMaxX, overlapMaxY + 1, range.maxY, range.minZ, range.maxZ );
    removeBox( overlapMinX, overlapMaxX, overlapMinY, overlapMaxY, range.minZ, overlapMinZ - 1 );
    removeBox( overlapMinX, overlapMaxX, overlapMinY, overlapMaxY, overlapMaxZ + 1, range.maxZ );
}


void SpatialGrid::MaintainBounds( int index, const Vector3& minBounds, const Vector3& maxBounds )
{

    if ( index < 0 || index >= SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS )
    {
        SB_FATAL( "Physics/SpatialGrid", "SpatialGrid object index out of bounds" );
    }

    const std::size_t requiredBodyRows = static_cast<std::size_t>( index + 1 );

    if ( requiredBodyRows > bodyMemberships.size() )
    {
        bodyMemberships.ExtendDefaultTo( requiredBodyRows );
        cellObjectSeen.ExtendDefaultTo( requiredBodyRows );
    }

    const CellRange nextRange = RangeForBounds( index, minBounds, maxBounds, MAX_CELL_ENTRIES );

    if ( index >= objectCount )
    {
        objectCount = index + 1;
    }

    BodyMembership& membership = bodyMemberships[index];

    if ( !membership.active )
    {
        InsertRangeDifference( index, nextRange, nullptr );
        membership.range = nextRange;
        membership.active = true;
        ++maintenanceStats.insertedBodies;
        return;
    }

    const CellRange& oldRange = membership.range;

    if ( oldRange.minX == nextRange.minX && oldRange.minY == nextRange.minY && oldRange.minZ == nextRange.minZ &&
         oldRange.maxX == nextRange.maxX && oldRange.maxY == nextRange.maxY && oldRange.maxZ == nextRange.maxZ )
    {
        ++maintenanceStats.unchangedBodies;
        return;
    }

    RemoveRangeDifference( index, oldRange, &nextRange );
    InsertRangeDifference( index, nextRange, &oldRange );
    membership.range = nextRange;
    ++maintenanceStats.movedBodies;
}


// Insert an object into all grid cells it overlaps at its current position.
// A sphere at position P with radius R overlaps cells from:
//   min = floor((P - R) / cellSize)  to  max = floor((P + R) / cellSize)
void SpatialGrid::Insert( int index, const Vector3& position, float radius )
{
    MaintainBounds( index, Vector3( position.x - radius, position.y - radius, position.z - radius ),
                    Vector3( position.x + radius, position.y + radius, position.z + radius ) );
}


void SpatialGrid::ResetSweptOverlay()
{

    for ( int overlayIndex = 0; overlayIndex < overlayActiveBucketCount; ++overlayIndex )
    {
        const int bucketIndex = overlayActiveBuckets[overlayIndex];
        Bucket& bucket = buckets[bucketIndex];

        if ( bucket.occupied && bucket.overlayGeneration == overlayGeneration )
        {
            bucket.overlayHead = -1;
            bucket.overlayCount = 0;
            RetireBucketIfEmpty( bucketIndex );
        }
    }

    overlayEntryCount = 0;
    overlayEntries.clear();
    overlayActiveBucketCount = 0;
    ++overlayGeneration;

    if ( overlayGeneration == 0 )
    {
        overlayGeneration = 1;

        for ( Bucket& bucket : buckets )
        {
            bucket.overlayGeneration = 0;
        }
    }
}


void SpatialGrid::InsertOverlayCell( int index, int ix, int iy, int iz )
{

    // The body's ordinary membership wins when the swept volume revisits its
    // current cell. The overlay stores velocity-dependent cells only.

    for ( int current = bodyMemberships[index].entryHead; current != -1; current = entries[current].nextForObject )
    {
        const Entry& entry = entries[current];

        if ( entry.ix == ix && entry.iy == iy && entry.iz == iz )
        {
            return;
        }
    }

    const int bucketIndex = FindOrCreateBucket( SpatialCellKey( ix, iy, iz ), ClampVisualizationCell( ix ),
                                                ClampVisualizationCell( iy ), ClampVisualizationCell( iz ) );

    Bucket& bucket = buckets[bucketIndex];
    bucket.pairSourceGeneration = pairSourceGeneration;

    if ( bucket.overlayGeneration != overlayGeneration )
    {
        bucket.overlayGeneration = overlayGeneration;
        bucket.overlayHead = -1;
        bucket.overlayCount = 0;

        if ( overlayActiveBucketCount >= TABLE_SIZE )
        {
            SB_FATAL( "Physics/SpatialGrid", "SpatialGrid swept-overlay bucket capacity exceeded" );
        }

        overlayActiveBuckets[overlayActiveBucketCount++] = bucketIndex;
    }

    for ( int current = bucket.overlayHead; current != -1; current = overlayEntries[current].next )
    {
        const SweptOverlayEntry& entry = overlayEntries[current];

        if ( entry.objectIndex == index && entry.ix == ix && entry.iy == iy && entry.iz == iz )
        {
            return;
        }
    }

    const int overlayEntryIndex = static_cast<int>( overlayEntries.size() );
    overlayEntries.Append( SweptOverlayEntry { index, bucket.overlayHead, ix, iy, iz } );
    bucket.overlayHead = overlayEntryIndex;
    overlayEntryCount = static_cast<int>( overlayEntries.size() );
    ++bucket.overlayCount;
    ++maintenanceStats.sweptOverlayCellsAdded;
}


void SpatialGrid::InsertOverlayBounds( int index, const Vector3& minBounds, const Vector3& maxBounds )
{
    const CellRange range = RangeForBounds( index, minBounds, maxBounds, MAX_SWEPT_CELL_ENTRIES );

    for ( int ix = range.minX; ix <= range.maxX; ++ix )
    {

        for ( int iy = range.minY; iy <= range.maxY; ++iy )
        {

            for ( int iz = range.minZ; iz <= range.maxZ; ++iz )
            {
                InsertOverlayCell( index, ix, iy, iz );
            }
        }
    }
}


int SpatialGrid::CollectBucketObjects( const Bucket& bucket, int* outIndices, int capacity, std::size_t* observedRawRows,
                                       int expectedBucketIndex )
{
#if !defined( _DEBUG )
    static_cast<void>( expectedBucketIndex );
#endif
    ++cellObjectGeneration;

    if ( cellObjectGeneration == 0 )
    {
        std::fill( cellObjectSeen.begin(), cellObjectSeen.end(), 0u );
        cellObjectGeneration = 1;
    }

    int count = 0;
    auto append = [&]( int objectIndex )
    {

        if ( objectIndex < 0 || objectIndex >= objectCount )
        {
            SB_FATAL( "Physics/SpatialGrid", "SpatialGrid object index out of bounds in entry chain" );
        }

        if ( cellObjectSeen[objectIndex] == cellObjectGeneration )
        {
            return;
        }

        if ( count >= capacity )
        {
            SB_FATAL( "Physics/SpatialGrid", "SpatialGrid cell index staging overflow" );
        }

        cellObjectSeen[objectIndex] = cellObjectGeneration;
        outIndices[count++] = objectIndex;
    };

    for ( int current = bucket.head; current != -1; current = entries[current].nextInBucket )
    {

        if ( current < 0 || static_cast<std::size_t>( current ) >= entries.size() )
        {
            SB_FATAL( "Physics/SpatialGrid", "SpatialGrid persistent entry chain index out of bounds" );
        }

        if ( observedRawRows )
        {
            ++*observedRawRows;
        }

#if defined( _DEBUG )

        if ( expectedBucketIndex >= 0 && entries[current].bucketIndex != expectedBucketIndex )
        {
            SB_FATAL( "Physics/SpatialGrid",
                      "Pair membership persistent bucket owner mismatch: bucket=%d entry=%d entry_bucket=%d.",
                      expectedBucketIndex, current, entries[current].bucketIndex );
        }
#endif

        append( entries[current].objectIndex );
    }

    if ( bucket.overlayGeneration == overlayGeneration )
    {

        for ( int current = bucket.overlayHead; current != -1; current = overlayEntries[current].next )
        {

            if ( current < 0 || static_cast<std::size_t>( current ) >= overlayEntries.size() )
            {
                SB_FATAL( "Physics/SpatialGrid", "SpatialGrid swept-overlay entry chain index out of bounds" );
            }

            if ( observedRawRows )
            {
                ++*observedRawRows;
            }

            append( overlayEntries[current].objectIndex );
        }
    }

    return count;
}


// Insert a dynamic body over the world-space AABB swept by its center this tick.
// Narrowphase still computes the exact time-of-impact; this only prevents the
// broadphase from skipping a fast body that starts outside the target cell.
void SpatialGrid::InsertSwept( int index, const Vector3& position, const Vector3& displacement, float radius )
{
    Insert( index, position, radius );
    const Vector3 endPosition = position + displacement;
    const Vector3 minBounds( (std::min)( position.x, endPosition.x ) - radius,
                             (std::min)( position.y, endPosition.y ) - radius,
                             (std::min)( position.z, endPosition.z ) - radius );

    const Vector3 maxBounds( (std::max)( position.x, endPosition.x ) + radius,
                             (std::max)( position.y, endPosition.y ) + radius,
                             (std::max)( position.z, endPosition.z ) + radius );

    // The swept range is transient; persistent membership above remains a pure
    // function of the body's current position and radius.
    ValidateBroadphaseBounds( index, minBounds, maxBounds, inverseCellSize );

    const int minX = static_cast<int>( floorf( minBounds.x * inverseCellSize ) );
    const int minY = static_cast<int>( floorf( minBounds.y * inverseCellSize ) );
    const int minZ = static_cast<int>( floorf( minBounds.z * inverseCellSize ) );
    const int maxX = static_cast<int>( floorf( maxBounds.x * inverseCellSize ) );
    const int maxY = static_cast<int>( floorf( maxBounds.y * inverseCellSize ) );
    const int maxZ = static_cast<int>( floorf( maxBounds.z * inverseCellSize ) );
    const int64_t cellCountX = static_cast<int64_t>( maxX ) - static_cast<int64_t>( minX ) + 1;
    const int64_t cellCountY = static_cast<int64_t>( maxY ) - static_cast<int64_t>( minY ) + 1;
    const int64_t cellCountZ = static_cast<int64_t>( maxZ ) - static_cast<int64_t>( minZ ) + 1;
    const bool exactAabbFits = cellCountX <= MAX_SWEPT_AABB_CELLS && cellCountY <= MAX_SWEPT_AABB_CELLS &&
                               cellCountZ <= MAX_SWEPT_AABB_CELLS &&
                               cellCountX * cellCountY * cellCountZ <= MAX_SWEPT_AABB_CELLS;

    if ( exactAabbFits )
    {
        InsertOverlayBounds( index, minBounds, maxBounds );
        return;
    }

    const float distanceSq = Dot( displacement, displacement );

    if ( distanceSq <= TOLERANCE )
    {
        return;
    }

    auto cellFor = [&]( float value ) -> int { return static_cast<int>( floorf( value * inverseCellSize ) ); };

    int cx = cellFor( position.x );
    int cy = cellFor( position.y );
    int cz = cellFor( position.z );
    const int endX = cellFor( endPosition.x );
    const int endY = cellFor( endPosition.y );
    const int endZ = cellFor( endPosition.z );

    auto axisTraversal = [&]( float start, float delta, int cell, int& outStep, float& outTMax, float& outTDelta )
    {

        if ( fabsf( delta ) <= TOLERANCE )
        {
            outStep = 0;

            outTMax = FLT_MAX;
            outTDelta = FLT_MAX;
            return;
        }

        if ( delta > 0.0f )
        {
            outStep = 1;
            const float nextBoundary = static_cast<float>( cell + 1 ) * cellSize;
            outTMax = ( nextBoundary - start ) / delta;
            outTDelta = cellSize / delta;
        }
        else
        {
            outStep = -1;
            const float nextBoundary = static_cast<float>( cell ) * cellSize;
            outTMax = ( start - nextBoundary ) / -delta;
            outTDelta = cellSize / -delta;
        }

        if ( outTMax < 0.0f )
        {
            outTMax = 0.0f;
        }
    };

    int stepX = 0;
    int stepY = 0;
    int stepZ = 0;
    float tMaxX = FLT_MAX;
    float tMaxY = FLT_MAX;
    float tMaxZ = FLT_MAX;
    float tDeltaX = FLT_MAX;
    float tDeltaY = FLT_MAX;
    float tDeltaZ = FLT_MAX;
    axisTraversal( position.x, displacement.x, cx, stepX, tMaxX, tDeltaX );
    axisTraversal( position.y, displacement.y, cy, stepY, tMaxY, tDeltaY );
    axisTraversal( position.z, displacement.z, cz, stepZ, tMaxZ, tDeltaZ );

    InsertOverlayBounds( index, Vector3( position.x - radius, position.y - radius, position.z - radius ),
                         Vector3( position.x + radius, position.y + radius, position.z + radius ) );

    int visitedCells = 0;

    while ( ( cx != endX || cy != endY || cz != endZ ) && visitedCells < MAX_SWEPT_TRAVERSED_CELLS )
    {
        const float nextT = (std::min)( tMaxX, (std::min)( tMaxY, tMaxZ ) );
        constexpr float AXIS_TIE_EPSILON = 1e-5f;

        if ( tMaxX <= nextT + AXIS_TIE_EPSILON )
        {
            cx += stepX;
            tMaxX += tDeltaX;
        }

        if ( tMaxY <= nextT + AXIS_TIE_EPSILON )
        {
            cy += stepY;
            tMaxY += tDeltaY;
        }

        if ( tMaxZ <= nextT + AXIS_TIE_EPSILON )
        {
            cz += stepZ;
            tMaxZ += tDeltaZ;
        }

        const float t = (std::max)( 0.0f, (std::min)( 1.0f, nextT ) );
        const Vector3 sample = position + displacement * t;
        InsertOverlayBounds( index, Vector3( sample.x - radius, sample.y - radius, sample.z - radius ),
                             Vector3( sample.x + radius, sample.y + radius, sample.z + radius ) );

        ++visitedCells;
    }

    InsertOverlayBounds( index, Vector3( endPosition.x - radius, endPosition.y - radius, endPosition.z - radius ),
                         Vector3( endPosition.x + radius, endPosition.y + radius, endPosition.z + radius ) );
}


void SpatialGrid::MarkPairSourceCells( int index )
{

    if ( index < 0 || index >= objectCount )
    {
        SB_FATAL( "Physics/SpatialGrid", "SpatialGrid pair-source body index out of bounds: index=%d count=%d.", index,
                  objectCount );
    }

    for ( int entryIndex = bodyMemberships[index].entryHead; entryIndex != -1;
          entryIndex = entries[entryIndex].nextForObject )
    {
        buckets[entries[entryIndex].bucketIndex].pairSourceGeneration = pairSourceGeneration;
    }
}


// Concept: pair history is derived from bucket membership, not retained by pair.
//
// Each live body receives one slice of active-bucket ordinals. This preflight
// derives bounded raw slices from retained sources; the already-ascending pair
// traversal fills their unique prefixes only for eligible buckets with at least
// two unique occupants.
// Persistent and swept rows may name different exact cells that hash to the same
// Bucket, so CollectBucketObjects owns alias compaction before at most one ordinal
// is appended per participating body. The slices live only until the next
// collection and borrow no authority from PhysicsBodyRecord or another hot store.
void SpatialGrid::BuildPairMembershipIndex()
{
    assert( objectCount >= 0 && objectCount <= SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS && "objectCount OOB" );

    if ( objectCount < 0 || objectCount > SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS )
    {
        SB_FATAL( "Physics/SpatialGrid", "SpatialGrid object count out of bounds" );
    }

    const std::size_t liveBodyCount = static_cast<std::size_t>( objectCount );

    if ( pairMembershipOffsets.size() < liveBodyCount + 1u )
    {
        pairMembershipOffsets.ExtendDefaultTo( liveBodyCount + 1u );
    }

    if ( pairMembershipCounts.size() < liveBodyCount )
    {
        pairMembershipCounts.ExtendDefaultTo( liveBodyCount );
    }

    // Concept: a live persistent membership owns one Entry for every exact cell
    // in its retained inclusive range. Counting that range is O(1) per body and
    // avoids a redundant hot-path chain walk before the later ordinal fill.
    const auto persistentRowCount = [&]( int bodyIndex )
    {
        const BodyMembership& membership = bodyMemberships[bodyIndex];

        if ( !membership.active )
        {
            return uint32_t { 0u };
        }

        const uint64_t countX = static_cast<uint64_t>( static_cast<int64_t>( membership.range.maxX ) -
                                                       membership.range.minX + 1 );

        const uint64_t countY = static_cast<uint64_t>( static_cast<int64_t>( membership.range.maxY ) -
                                                       membership.range.minY + 1 );

        const uint64_t countZ = static_cast<uint64_t>( static_cast<int64_t>( membership.range.maxZ ) -
                                                       membership.range.minZ + 1 );

        const uint64_t rowCount = countX * countY * countZ;

        if ( rowCount > static_cast<uint64_t>( ( std::numeric_limits<uint32_t>::max )() ) )
        {
            SB_FATAL( "Physics/SpatialGrid", "Pair membership persistent range exceeds uint32: body=%d rows=%llu.",
                      bodyIndex, static_cast<unsigned long long>( rowCount ) );
        }

        return static_cast<uint32_t>( rowCount );
    };

    for ( int bodyIndex = 0; bodyIndex < objectCount; ++bodyIndex )
    {
        pairMembershipCounts[bodyIndex] = persistentRowCount( bodyIndex );
    }

#if defined( _DEBUG )
    const auto requireBucketOrdinal = [&]( int bucketIndex, int bodyIndex, const char* source )
    {

        if ( bucketIndex < 0 || bucketIndex >= TABLE_SIZE )
        {
            SB_FATAL( "Physics/SpatialGrid",
                      "Pair membership source bucket is invalid: source=%s body=%d bucket=%d capacity=%d.", source,
                      bodyIndex, bucketIndex, TABLE_SIZE );
        }

        const Bucket& bucket = buckets[bucketIndex];
        const int activeIndex = bucket.activeIndex;

        if ( !bucket.occupied || activeIndex < 0 || activeIndex >= activeBucketCount ||
             activeBuckets[activeIndex] != bucketIndex )
        {
            SB_FATAL( "Physics/SpatialGrid",
                      "Pair membership bucket backlink is corrupt: source=%s body=%d bucket=%d active_ordinal=%d "
                      "active_count=%d occupied=%d.",
                      source, bodyIndex, bucketIndex, activeIndex, activeBucketCount, bucket.occupied ? 1 : 0 );
        }

        return static_cast<PairMembershipOrdinal>( activeIndex );
    };
#endif

#if defined( _DEBUG )
    std::size_t observedPersistentRows = 0u;

    for ( int bodyIndex = 0; bodyIndex < objectCount; ++bodyIndex )
    {
        std::size_t chainRows = 0u;

        for ( int entryIndex = bodyMemberships[bodyIndex].entryHead; entryIndex != -1;
              entryIndex = entries[entryIndex].nextForObject )
        {

            if ( entryIndex < 0 || static_cast<std::size_t>( entryIndex ) >= entries.size() )
            {
                SB_FATAL( "Physics/SpatialGrid",
                          "Pair membership persistent chain index is invalid: body=%d entry=%d rows=%zu.", bodyIndex,
                          entryIndex, entries.size() );
            }

            const Entry& entry = entries[entryIndex];

            if ( entry.objectIndex != bodyIndex )
            {
                SB_FATAL( "Physics/SpatialGrid",
                          "Pair membership persistent chain owner mismatch: body=%d entry=%d entry_body=%d.", bodyIndex,
                          entryIndex, entry.objectIndex );
            }

            static_cast<void>( requireBucketOrdinal( entry.bucketIndex, bodyIndex, "persistent" ) );
            ++observedPersistentRows;
            ++chainRows;

            if ( chainRows > entries.size() )
            {
                SB_FATAL( "Physics/SpatialGrid", "Pair membership persistent chain cycles: body=%d rows=%zu.", bodyIndex,
                          chainRows );
            }
        }

        if ( chainRows != pairMembershipCounts[bodyIndex] )
        {
            SB_FATAL( "Physics/SpatialGrid",
                      "Pair membership persistent range count diverged: body=%d range_rows=%u chain_rows=%zu.", bodyIndex,
                      pairMembershipCounts[bodyIndex], chainRows );
        }
    }

    if ( observedPersistentRows != static_cast<std::size_t>( persistentEntryCount ) )
    {
        SB_FATAL( "Physics/SpatialGrid",
                  "Pair membership persistent row count diverged: observed=%zu live=%d retained_rows=%zu.",
                  observedPersistentRows, persistentEntryCount, entries.size() );
    }
#endif

    if ( overlayEntries.size() != static_cast<std::size_t>( overlayEntryCount ) )
    {
        SB_FATAL( "Physics/SpatialGrid", "Pair membership overlay row count diverged: rows=%zu live=%d generation=%u.",
                  overlayEntries.size(), overlayEntryCount, overlayGeneration );
    }

    for ( const SweptOverlayEntry& entry : overlayEntries )
    {

        if ( entry.objectIndex < 0 || entry.objectIndex >= objectCount )
        {
            SB_FATAL( "Physics/SpatialGrid", "Pair membership overlay body is invalid: body=%d admitted=%d.",
                      entry.objectIndex, objectCount );
        }

        // Lifetime: the traversal later reaches this row through its owning
        // bucket. This preflight needs only the validated body owner to reserve
        // its raw slice; no coordinate hash lookup is repeated here.
        ++pairMembershipCounts[entry.objectIndex];
    }

    uint64_t requestedRowsWide = 0u;
    pairMembershipOffsets[0] = 0u;

    for ( int bodyIndex = 0; bodyIndex < objectCount; ++bodyIndex )
    {
        const uint32_t rawCount = pairMembershipCounts[bodyIndex];
        requestedRowsWide += rawCount;

        if ( requestedRowsWide > static_cast<uint64_t>( ( std::numeric_limits<uint32_t>::max )() ) )
        {
            SB_FATAL( "Physics/SpatialGrid", "Pair membership row prefix exceeds uint32: body=%d rows=%llu.", bodyIndex,
                      static_cast<unsigned long long>( requestedRowsWide ) );
        }

        pairMembershipOffsets[bodyIndex + 1] = static_cast<uint32_t>( requestedRowsWide );
        pairMembershipCounts[bodyIndex] = 0u;
    }

    const std::size_t requestedRows = static_cast<std::size_t>( requestedRowsWide );
    const std::size_t physicalCapacity = pairMembershipOrdinals.capacity();
    std::size_t logicalCapacity = physicalCapacity;
#if defined( _DEBUG )
    logicalCapacity = (std::min)( logicalCapacity, pairMembershipLogicalCapacityForTest );
#endif

    if ( requestedRows > logicalCapacity )
    {
        using namespace SkullbonezCore::Core::Allocation;
        SB_FATAL( "Physics/SpatialGrid",
                  "Pair membership ordinal capacity exhausted: owner=SpatialGrid.pairMembershipOrdinals "
                  "requested=%zu logical_capacity=%zu reserved_capacity=%zu high_water=%zu admitted_bodies=%d "
                  "persistent_live=%d persistent_capacity=%zu overlay_live=%zu overlay_capacity=%zu phase=%s.",
                  requestedRows, logicalCapacity, physicalCapacity, pairMembershipOrdinals.high_water(), objectCount,
                  persistentEntryCount, entries.capacity(), overlayEntries.size(), overlayEntries.capacity(),
                  RuntimeReservePhaseName( GetRuntimeAllocationPhase() ) );
    }

    // Lifetime: keep the fixed lists at their observed high-water instead of
    // clearing value storage every query. This pass overwrites every live
    // offset/count and later reads ordinals only through those fresh bounds, so
    // rows beyond the current raw prefix carry no authority. The guard also
    // avoids republishing unchanged owner usage on every broadphase pass.

    if ( pairMembershipOrdinals.size() < requestedRows )
    {
        pairMembershipOrdinals.ExtendDefaultTo( requestedRows );
    }

    // Lifetime: counts now describe empty unique prefixes. Ascending bucket
    // traversal uses offset + count as each body's write cursor, so future
    // ordinals cannot influence an earlier-shared decision made in the current
    // bucket and no staging pointer escapes the synchronous query.
}


void SpatialGrid::AppendUniquePairMembershipOrdinal( int bodyIndex, int activeIndex )
{

    // Why: eligible collection already proves each body appears once in this
    // pair-bearing bucket. Bypassing a redundant prior-ordinal alias scan keeps
    // the hot path smaller; all builds still guard owner/range exhaustion and
    // Debug additionally proves strict order.

    if ( bodyIndex < 0 || bodyIndex >= objectCount || activeIndex < 0 || activeIndex >= activeBucketCount )
    {
        SB_FATAL( "Physics/SpatialGrid",
                  "Unique pair membership append is invalid: body=%d admitted=%d ordinal=%d active_count=%d.", bodyIndex,
                  objectCount, activeIndex, activeBucketCount );
    }

    const uint32_t uniqueCount = pairMembershipCounts[bodyIndex];
    const uint32_t writeIndex = pairMembershipOffsets[bodyIndex] + uniqueCount;

    if ( writeIndex >= pairMembershipOffsets[bodyIndex + 1] )
    {
        SB_FATAL( "Physics/SpatialGrid",
                  "Pair membership fill exceeded its counted slice: body=%d write=%u end=%u ordinal=%d.", bodyIndex,
                  writeIndex, pairMembershipOffsets[bodyIndex + 1], activeIndex );
    }

#if defined( _DEBUG )

    if ( uniqueCount > 0u && pairMembershipOrdinals[writeIndex - 1u] >= activeIndex )
    {
        SB_FATAL( "Physics/SpatialGrid",
                  "Unique pair membership fill lost strict ordinal order: body=%d previous=%u next=%d.", bodyIndex,
                  static_cast<unsigned int>( pairMembershipOrdinals[writeIndex - 1u] ), activeIndex );
    }
#endif

    pairMembershipOrdinals[writeIndex] = static_cast<PairMembershipOrdinal>( activeIndex );
    pairMembershipCounts[bodyIndex] = uniqueCount + 1u;
}


#if defined( _DEBUG )
void SpatialGrid::ObservePairBucketRows( const Bucket& bucket, int bucketIndex, int activeIndex,
                                         std::size_t& observedRawRows )
{
    std::size_t bucketRows = 0u;

    for ( int entryIndex = bucket.head; entryIndex != -1; entryIndex = entries[entryIndex].nextInBucket )
    {

        if ( entryIndex < 0 || static_cast<std::size_t>( entryIndex ) >= entries.size() )
        {
            SB_FATAL( "Physics/SpatialGrid", "Pair membership persistent entry is invalid: entry=%d ordinal=%d.", entryIndex,
                      activeIndex );
        }

        if ( entries[entryIndex].bucketIndex != bucketIndex )
        {
            SB_FATAL( "Physics/SpatialGrid",
                      "Pair membership persistent bucket owner mismatch: bucket=%d entry=%d entry_bucket=%d.", bucketIndex,
                      entryIndex, entries[entryIndex].bucketIndex );
        }

        ++bucketRows;
    }

    if ( bucket.overlayGeneration == overlayGeneration )
    {

        for ( int entryIndex = bucket.overlayHead; entryIndex != -1; entryIndex = overlayEntries[entryIndex].next )
        {

            if ( entryIndex < 0 || static_cast<std::size_t>( entryIndex ) >= overlayEntries.size() )
            {
                SB_FATAL( "Physics/SpatialGrid", "Pair membership overlay entry is invalid: entry=%d ordinal=%d.",
                          entryIndex, activeIndex );
            }

            ++bucketRows;
        }
    }

    const int currentOverlayCount = bucket.overlayGeneration == overlayGeneration ? bucket.overlayCount : 0;
    const int rawCount = bucket.count + currentOverlayCount;

    if ( bucket.count < 0 || currentOverlayCount < 0 || bucketRows != static_cast<std::size_t>( rawCount ) )
    {
        SB_FATAL( "Physics/SpatialGrid",
                  "Pair membership bucket chain count diverged: ordinal=%d observed=%zu persistent=%d overlay=%d.",
                  activeIndex, bucketRows, bucket.count, currentOverlayCount );
    }

    observedRawRows += bucketRows;
}
#endif


int SpatialGrid::CollectPairBucketObjects( const Bucket& bucket, int bucketIndex, int activeIndex, int* outIndices,
                                           int capacity, std::size_t& observedRawRows )
{
    const int currentOverlayCount = bucket.overlayGeneration == overlayGeneration ? bucket.overlayCount : 0;

    if ( bucket.count < 0 || currentOverlayCount < 0 )
    {
        SB_FATAL( "Physics/SpatialGrid", "Pair membership bucket count is invalid: persistent=%d overlay=%d ordinal=%d.",
                  bucket.count, currentOverlayCount, activeIndex );
    }

    const int rawCount = bucket.count + currentOverlayCount;

    // Hazard: omitting a singleton ordinal is an optimization, not permission to
    // hide corrupt owner or reserve state. Prove the same all-configuration slot
    // that an append would have consumed before taking either singleton exit.
    const auto requireOmittedOrdinalSlot = [&]( int bodyIndex )
    {

        if ( bodyIndex < 0 || bodyIndex >= objectCount || activeIndex < 0 || activeIndex >= activeBucketCount )
        {
            SB_FATAL( "Physics/SpatialGrid",
                      "Omitted singleton pair membership is invalid: body=%d admitted=%d ordinal=%d active_count=%d.",
                      bodyIndex, objectCount, activeIndex, activeBucketCount );
        }

        const uint32_t writeIndex = pairMembershipOffsets[bodyIndex] + pairMembershipCounts[bodyIndex];

        if ( writeIndex >= pairMembershipOffsets[bodyIndex + 1] )
        {
            SB_FATAL( "Physics/SpatialGrid",
                      "Omitted singleton pair membership exceeded its counted slice: body=%d write=%u end=%u ordinal=%d.",
                      bodyIndex, writeIndex, pairMembershipOffsets[bodyIndex + 1], activeIndex );
        }
    };

    if ( rawCount == 0 )
    {
        return 0;
    }

    // Why: persistent grids retain many singleton cells, but an ordinal owned by
    // only one body cannot be a shared first-seen witness. Preserve that cheap
    // exit without touching the membership slices; multi-row buckets reuse the
    // traversal's alias-deduped staging.

    if ( rawCount == 1 )
    {

        if ( capacity < 1 )
        {
            SB_FATAL( "Physics/SpatialGrid", "SpatialGrid cell index staging overflow" );
        }

        int bodyIndex = -1;

        if ( bucket.count == 1 )
        {
            const int entryIndex = bucket.head;

            if ( entryIndex < 0 || static_cast<std::size_t>( entryIndex ) >= entries.size() )
            {
                SB_FATAL( "Physics/SpatialGrid", "Pair membership singleton persistent entry is invalid: entry=%d.",
                          entryIndex );
            }

#if defined( _DEBUG )

            if ( entries[entryIndex].bucketIndex != bucketIndex )
            {
                SB_FATAL( "Physics/SpatialGrid",
                          "Pair membership persistent bucket owner mismatch: bucket=%d entry=%d entry_bucket=%d.",
                          bucketIndex, entryIndex, entries[entryIndex].bucketIndex );
            }
#endif

            bodyIndex = entries[entryIndex].objectIndex;
        }
        else
        {
            const int entryIndex = bucket.overlayHead;

            if ( entryIndex < 0 || static_cast<std::size_t>( entryIndex ) >= overlayEntries.size() )
            {
                SB_FATAL( "Physics/SpatialGrid", "Pair membership singleton overlay entry is invalid: entry=%d.",
                          entryIndex );
            }

            bodyIndex = overlayEntries[entryIndex].objectIndex;
        }

        requireOmittedOrdinalSlot( bodyIndex );
        outIndices[0] = bodyIndex;
#if defined( _DEBUG )
        ++observedRawRows;
#else
        static_cast<void>( observedRawRows );
#endif
        return 1;
    }

#if defined( _DEBUG )
    std::size_t bucketRows = 0u;
    const int count = CollectBucketObjects( bucket, outIndices, capacity, &bucketRows, bucketIndex );

    if ( bucketRows != static_cast<std::size_t>( rawCount ) )
    {
        SB_FATAL( "Physics/SpatialGrid",
                  "Pair membership bucket chain count diverged: ordinal=%d observed=%zu persistent=%d overlay=%d.",
                  activeIndex, bucketRows, bucket.count, currentOverlayCount );
    }

    observedRawRows += bucketRows;
#else
    static_cast<void>( bucketIndex );
    static_cast<void>( observedRawRows );
    const int count = CollectBucketObjects( bucket, outIndices, capacity );
#endif

    // Invariant: only buckets containing at least two unique bodies can own or
    // precede a candidate-pair decision. Omitting singleton-only ordinals leaves
    // every possible list intersection unchanged while avoiding hot writes for
    // the grid's common sparse-cell case.

    if ( count < 2 )
    {

        if ( count == 0 )
        {
            SB_FATAL( "Physics/SpatialGrid", "Non-empty pair bucket produced no unique body: ordinal=%d raw_rows=%d.",
                      activeIndex, rawCount );
        }

        requireOmittedOrdinalSlot( outIndices[0] );
        return count;
    }

    for ( int index = 0; index < count; ++index )
    {
        AppendUniquePairMembershipOrdinal( outIndices[index], activeIndex );
    }

    return count;
}


void SpatialGrid::RequirePairMembershipRowsObserved( std::size_t observedRows ) const
{
#if defined( _DEBUG )
    const std::size_t expectedRows = static_cast<std::size_t>( pairMembershipOffsets[objectCount] );

    if ( observedRows != expectedRows )
    {
        SB_FATAL( "Physics/SpatialGrid", "Pair membership bucket rows diverged: observed=%zu counted=%zu.", observedRows,
                  expectedRows );
    }
#else
    static_cast<void>( observedRows );
#endif
}


bool SpatialGrid::IsEarliestEligibleSharedBucket( int a, int b, int currentActiveIndex,
                                                  bool restrictToPairSourceCells ) const
{
    assert( a >= 0 && a < b && b < objectCount && "candidate pair identity out of bounds" );

#if defined( _DEBUG )

    if ( a < 0 || a >= b || b >= objectCount || currentActiveIndex < 0 || currentActiveIndex >= activeBucketCount )
    {
        SB_FATAL( "Physics/SpatialGrid",
                  "Pair membership query is invalid: pair=(%d,%d) active_ordinal=%d admitted=%d active_count=%d.", a, b,
                  currentActiveIndex, objectCount, activeBucketCount );
    }
#else
    static_cast<void>( currentActiveIndex );
#endif

    const uint32_t aCount = pairMembershipCounts[a];
    const uint32_t bCount = pairMembershipCounts[b];

#if defined( _DEBUG )

    if ( aCount == 0u || bCount == 0u )
    {
        SB_FATAL( "Physics/SpatialGrid",
                  "Pair membership omitted a current body: pair=(%d,%d) current_ordinal=%d restrict=%d a_count=%u "
                  "b_count=%u.",
                  a, b, currentActiveIndex, restrictToPairSourceCells ? 1 : 0, aCount, bCount );
    }
#endif

    const uint32_t aBegin = pairMembershipOffsets[a];
    const uint32_t bBegin = pairMembershipOffsets[b];
    const uint32_t aCurrent = aBegin + aCount - 1u;
    const uint32_t bCurrent = bBegin + bCount - 1u;

    // Invariant: traversal appends the current eligible bucket before testing
    // its pairs. It is therefore the last ordinal in both prefixes; only the
    // earlier prefixes need intersection and future buckets cannot affect the
    // answer.

#if defined( _DEBUG )

    if ( pairMembershipOrdinals[aCurrent] != currentActiveIndex || pairMembershipOrdinals[bCurrent] != currentActiveIndex )
    {
        SB_FATAL( "Physics/SpatialGrid",
                  "Pair membership omitted the current shared bucket: pair=(%d,%d) current_ordinal=%d "
                  "a_last=%u b_last=%u restrict=%d.",
                  a, b, currentActiveIndex, static_cast<unsigned int>( pairMembershipOrdinals[aCurrent] ),
                  static_cast<unsigned int>( pairMembershipOrdinals[bCurrent] ), restrictToPairSourceCells ? 1 : 0 );
    }
#endif

    // Why: repeated co-cell observations usually share a recent prior bucket.
    // Walk backward from the current tails so the common case returns after
    // one comparison; first observations still perform the same bounded merge.
    uint32_t aCursor = aCurrent;
    uint32_t bCursor = bCurrent;

    if ( aCursor == aBegin || bCursor == bBegin )
    {
        return true;
    }

    if ( pairMembershipOrdinals[aCursor - 1u] < pairMembershipOrdinals[bBegin] ||
         pairMembershipOrdinals[bCursor - 1u] < pairMembershipOrdinals[aBegin] )
    {
        return true;
    }

    while ( aCursor > aBegin && bCursor > bBegin )
    {
        const int aOrdinal = static_cast<int>( pairMembershipOrdinals[aCursor - 1u] );
        const int bOrdinal = static_cast<int>( pairMembershipOrdinals[bCursor - 1u] );

        if ( aOrdinal > bOrdinal )
        {
            --aCursor;
            continue;
        }

        if ( bOrdinal > aOrdinal )
        {
            --bCursor;
            continue;
        }

        const int sharedOrdinal = aOrdinal;

#if defined( _DEBUG )

        if ( sharedOrdinal < 0 || sharedOrdinal >= currentActiveIndex )
        {
            SB_FATAL( "Physics/SpatialGrid", "Pair membership earlier prefix is stale: pair=(%d,%d) shared=%d current=%d.",
                      a, b, sharedOrdinal, currentActiveIndex );
        }
#endif

        if ( restrictToPairSourceCells )
        {
            const int bucketIndex = activeBuckets[sharedOrdinal];

            if ( buckets[bucketIndex].pairSourceGeneration != pairSourceGeneration )
            {
                --aCursor;
                --bCursor;
                continue;
            }
        }

        return false;
    }

    return true;
}


bool SpatialGrid::MarkCandidatePairFirstSeen( int a, int b, int currentActiveIndex, bool restrictToPairSourceCells )
{
    return IsEarliestEligibleSharedBucket( a, b, currentActiveIndex, restrictToPairSourceCells );
}


bool SpatialGrid::FilterCandidatePairAfterFirstSeen( int a, int b,
                                                     const SkullbonezCore::Physics::PhysicsBodyStore& bodyStore,
                                                     const SkullbonezCore::Physics::ColliderStore& colliderStore,
                                                     std::span<const uint8_t> sleepState, float dt, float contactSkin,
                                                     SkullbonezCore::Physics::PhysicsCandidatePairList* sleepPrunedPairs )
{

    if ( SkullbonezCore::Physics::BroadphaseCandidateBothSleeping( sleepState, a, b ) )
    {

        // Preserve the old diagnostic boundary: SleepPrunedPair described a
        // geometrically admitted candidate, not every dormant co-cell pair.

        if ( !SkullbonezCore::Physics::BroadphaseCandidateGeometryCanTouch( bodyStore, colliderStore, dt, contactSkin, a,
                                                                            b ) )
        {
            return false;
        }

        if ( sleepPrunedPairs )
        {

            if ( sleepPrunedPairs->size() >= sleepPrunedPairs->capacity() )
            {
                SB_FATAL( "Physics/SpatialGrid", "Sleep-pruned diagnostic reserve exhausted: capacity=%zu phase=diagnostic.",
                          sleepPrunedPairs->capacity() );
            }

            sleepPrunedPairs->emplace_back( a, b );
        }

        return false;
    }

    return SkullbonezCore::Physics::BroadphaseCandidateGeometryCanTouch( bodyStore, colliderStore, dt, contactSkin, a, b );
}


// Concept: discovery order is not solver order.
//
// Buckets and their linked lists are storage details whose order changes when
// the grid becomes persistent. Each newly discovered pair is therefore staged
// under its smaller body index, radix-sorted by its larger index, and only then
// emitted. The result is the history-free (minIndex,maxIndex) order that P1
// makes the byte-exact baseline for later broadphase work.
void SpatialGrid::GetCandidatePairs( std::vector<std::pair<int, int>>& outPairs, bool restrictToPairSourceCells )
{

    // Why: unfiltered tooling/tests need pure co-cell membership without a
    // nullable physics-filter authority. Keep this traversal explicit so the
    // production overload always receives concrete stores and step values.
    outPairs.clear();
    BuildPairMembershipIndex();
    candidatePairHeads.ResetFill( static_cast<std::size_t>( objectCount ), -1 );
    candidatePairNodes.clear();
    int candidatePairNodeCount = 0;
    std::size_t observedPairMembershipRows = 0u;

    for ( int activeIndex = 0; activeIndex < activeBucketCount; ++activeIndex )
    {
        const int bucketIndex = activeBuckets[activeIndex];
        assert( bucketIndex >= 0 && bucketIndex < TABLE_SIZE && "active bucket index OOB" );

        if ( bucketIndex < 0 || bucketIndex >= TABLE_SIZE )
        {
            SB_FATAL( "Physics/SpatialGrid", "SpatialGrid active bucket index out of bounds" );
        }

        Bucket& bucket = buckets[bucketIndex];
        const int currentOverlayCount = bucket.overlayGeneration == overlayGeneration ? bucket.overlayCount : 0;

        if ( bucket.count < 0 || currentOverlayCount < 0 )
        {
            SB_FATAL( "Physics/SpatialGrid", "Pair membership bucket count is invalid: persistent=%d overlay=%d ordinal=%d.",
                      bucket.count, currentOverlayCount, activeIndex );
        }

        const int rawCellCount = bucket.count + currentOverlayCount;

        if ( !bucket.occupied )
        {

            if ( rawCellCount != 0 )
            {
                SB_FATAL( "Physics/SpatialGrid", "Inactive pair bucket retains rows: ordinal=%d rows=%d.", activeIndex,
                          rawCellCount );
            }

            continue;
        }

        if ( restrictToPairSourceCells && bucket.pairSourceGeneration != pairSourceGeneration )
        {
#if defined( _DEBUG )

            // Why: an unstamped bucket is outside the eligible set E and cannot
            // affect min(M(a) intersection M(b) intersection E). Debug still
            // walks its chains so the raw-row accounting proof is not weakened.
            ObservePairBucketRows( bucket, bucketIndex, activeIndex, observedPairMembershipRows );
#endif
            continue;
        }

        int cellIndices[SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS];
        const int cellCount = CollectPairBucketObjects( bucket, bucketIndex, activeIndex, cellIndices,
                                                        SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS,
                                                        observedPairMembershipRows );

        if ( cellCount < 2 )
        {
            continue;
        }

        for ( int i = 0; i < cellCount - 1; ++i )
        {

            for ( int j = i + 1; j < cellCount; ++j )
            {
                int a = cellIndices[i];
                int b = cellIndices[j];

                if ( a == b )
                {
                    continue;
                }

                if ( a > b )
                {
                    std::swap( a, b );
                }

                if ( !MarkCandidatePairFirstSeen( a, b, activeIndex, restrictToPairSourceCells ) )
                {
                    continue;
                }

                const size_t callerCapacity = outPairs.capacity();

                if ( static_cast<size_t>( candidatePairNodeCount ) >= callerCapacity )
                {
                    SB_FATAL( "Physics/SpatialGrid",
                              "Canonical candidate list exhausted: owner=Physics/SpatialGrid "
                              "capacity=%zu fixed_capacity=%d high_water=%d phase=steady_gameplay.",
                              callerCapacity, MAX_CANDIDATE_PAIRS, candidatePairNodeCount );
                }

                candidatePairNodes.Append( CandidatePairNode { b, candidatePairHeads[a] } );
                candidatePairHeads[a] = candidatePairNodeCount++;
            }
        }
    }

    RequirePairMembershipRowsObserved( observedPairMembershipRows );

    for ( int minIndex = 0; minIndex < objectCount; ++minIndex )
    {
        candidatePairSortKeys.clear();

        for ( int nodeIndex = candidatePairHeads[minIndex]; nodeIndex != -1; nodeIndex = candidatePairNodes[nodeIndex].next )
        {
            candidatePairSortKeys.Append( candidatePairNodes[nodeIndex].maxIndex );
        }

        const int pairCount = static_cast<int>( candidatePairSortKeys.size() );

        if ( pairCount == 0 )
        {
            continue;
        }

        int lowCounts[kCandidatePairSortLowBucketCount] = {};
        int lowOffsets[kCandidatePairSortLowBucketCount] = {};

        for ( int pairIndex = 0; pairIndex < pairCount; ++pairIndex )
        {
            ++lowCounts[candidatePairSortKeys[pairIndex] & kCandidatePairSortLowMask];
        }

        for ( int digit = 1; digit < kCandidatePairSortLowBucketCount; ++digit )
        {
            lowOffsets[digit] = lowOffsets[digit - 1] + lowCounts[digit - 1];
        }

        candidatePairSortScratch.ResetDefault( static_cast<std::size_t>( pairCount ) );

        for ( int pairIndex = 0; pairIndex < pairCount; ++pairIndex )
        {
            const int value = candidatePairSortKeys[pairIndex];
            candidatePairSortScratch[lowOffsets[value & kCandidatePairSortLowMask]++] = value;
        }

        int highCounts[kCandidatePairSortHighBucketCount] = {};
        int highOffsets[kCandidatePairSortHighBucketCount] = {};

        for ( int pairIndex = 0; pairIndex < pairCount; ++pairIndex )
        {
            ++highCounts[( candidatePairSortScratch[pairIndex] >> kCandidatePairSortLowBitCount ) &
                         kCandidatePairSortHighMask];
        }

        for ( int digit = 1; digit < kCandidatePairSortHighBucketCount; ++digit )
        {
            highOffsets[digit] = highOffsets[digit - 1] + highCounts[digit - 1];
        }

        for ( int pairIndex = 0; pairIndex < pairCount; ++pairIndex )
        {
            const int value = candidatePairSortScratch[pairIndex];
            candidatePairSortKeys[highOffsets[( value >> kCandidatePairSortLowBitCount ) &
                                              kCandidatePairSortHighMask]++] = value;
        }

        for ( int pairIndex = 0; pairIndex < pairCount; ++pairIndex )
        {
            outPairs.emplace_back( minIndex, candidatePairSortKeys[pairIndex] );
        }
    }
}


void SpatialGrid::GetFilteredCandidatePairsImpl( SkullbonezCore::Physics::PhysicsCandidatePairList& outPairs,
                                                 const SkullbonezCore::Physics::PhysicsBodyStore& bodyStore,
                                                 const SkullbonezCore::Physics::ColliderStore& colliderStore,
                                                 std::span<const uint8_t> sleepState, float dt, float contactSkin,
                                                 SkullbonezCore::Physics::PhysicsCandidatePairList* sleepPrunedPairs,
                                                 bool restrictToPairSourceCells )
{

    // Lifetime: sleepPrunedPairs is only an optional Debug evidence sink. It
    // carries no filtering authority; all admission facts are concrete borrows
    // whose lifetime ends when this synchronous collection returns.
    outPairs.clear();

    if ( sleepPrunedPairs )
    {
        sleepPrunedPairs->clear();
    }

    BuildPairMembershipIndex();
    candidatePairHeads.ResetFill( static_cast<std::size_t>( objectCount ), -1 );
    candidatePairNodes.clear();
    int candidatePairNodeCount = 0;
    std::size_t observedPairMembershipRows = 0u;

    for ( int activeIndex = 0; activeIndex < activeBucketCount; ++activeIndex )
    {
        int bi = activeBuckets[activeIndex];
        assert( bi >= 0 && bi < TABLE_SIZE && "active bucket index OOB" );

        if ( bi < 0 || bi >= TABLE_SIZE )
        {
            SB_FATAL( "Physics/SpatialGrid", "SpatialGrid active bucket index out of bounds" );
        }

        Bucket& b = buckets[bi];
        const int currentOverlayCount = b.overlayGeneration == overlayGeneration ? b.overlayCount : 0;

        if ( b.count < 0 || currentOverlayCount < 0 )
        {
            SB_FATAL( "Physics/SpatialGrid", "Pair membership bucket count is invalid: persistent=%d overlay=%d ordinal=%d.",
                      b.count, currentOverlayCount, activeIndex );
        }

        const int rawCellCount = b.count + currentOverlayCount;

        if ( !b.occupied )
        {

            if ( rawCellCount != 0 )
            {
                SB_FATAL( "Physics/SpatialGrid", "Inactive pair bucket retains rows: ordinal=%d rows=%d.", activeIndex,
                          rawCellCount );
            }

            continue;
        }

        if ( restrictToPairSourceCells && b.pairSourceGeneration != pairSourceGeneration )
        {
#if defined( _DEBUG )

            // Why: project each per-body prefix to eligible pair-source buckets.
            // Unstamped rows cannot own filtering, while Debug traversal retains
            // the raw bucket-chain integrity proof without production writes.
            ObservePairBucketRows( b, bi, activeIndex, observedPairMembershipRows );
#endif
            continue;
        }

        int cellIndices[SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS];
        const int cellCount = CollectPairBucketObjects( b, bi, activeIndex, cellIndices,
                                                        SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS,
                                                        observedPairMembershipRows );

        if ( cellCount < 2 )
        {
            continue;
        }

        for ( int i = 0; i < cellCount - 1; ++i )
        {

            for ( int j = i + 1; j < cellCount; ++j )
            {
                int a = cellIndices[i];
                int bIdx = cellIndices[j];

                // Hash collisions can place the same object in a bucket twice — skip self-pairs

                if ( a == bIdx )
                {
                    continue;
                }

                if ( a > bIdx )
                {
                    int tmp = a;
                    a = bIdx;
                    bIdx = tmp;
                }

                if ( !MarkCandidatePairFirstSeen( a, bIdx, activeIndex, restrictToPairSourceCells ) )
                {
                    continue;
                }

                if ( !FilterCandidatePairAfterFirstSeen( a, bIdx, bodyStore, colliderStore, sleepState, dt, contactSkin,
                                                         sleepPrunedPairs ) )
                {
                    continue;
                }

                const size_t callerCapacity = outPairs.capacity();

                if ( static_cast<size_t>( candidatePairNodeCount ) >= callerCapacity )
                {

                    // Lane F: growing or dropping the list would respectively
                    // violate the runtime allocation policy or hide a collision.
                    SB_FATAL( "Physics/SpatialGrid",
                              "Canonical candidate list exhausted: owner=Physics/SpatialGrid "
                              "capacity=%zu fixed_capacity=%d high_water=%d phase=steady_gameplay.",
                              callerCapacity, MAX_CANDIDATE_PAIRS, candidatePairNodeCount );
                }

                candidatePairNodes.Append( CandidatePairNode { bIdx, candidatePairHeads[a] } );
                candidatePairHeads[a] = candidatePairNodeCount++;
            }
        }
    }

    RequirePairMembershipRowsObserved( observedPairMembershipRows );

    // Two stable radix passes cover the complete derived body-index width.
    // Total work is proportional to bodies plus accepted pairs and uses only
    // the grid's fixed staging arrays.

    for ( int minIndex = 0; minIndex < objectCount; ++minIndex )
    {
        candidatePairSortKeys.clear();

        for ( int nodeIndex = candidatePairHeads[minIndex]; nodeIndex != -1; nodeIndex = candidatePairNodes[nodeIndex].next )
        {
            candidatePairSortKeys.Append( candidatePairNodes[nodeIndex].maxIndex );
        }

        const int pairCount = static_cast<int>( candidatePairSortKeys.size() );

        if ( pairCount == 0 )
        {
            continue;
        }

        int lowCounts[kCandidatePairSortLowBucketCount] = {};
        int lowOffsets[kCandidatePairSortLowBucketCount] = {};

        for ( int pairIndex = 0; pairIndex < pairCount; ++pairIndex )
        {
            ++lowCounts[candidatePairSortKeys[pairIndex] & kCandidatePairSortLowMask];
        }

        for ( int digit = 1; digit < kCandidatePairSortLowBucketCount; ++digit )
        {
            lowOffsets[digit] = lowOffsets[digit - 1] + lowCounts[digit - 1];
        }

        candidatePairSortScratch.ResetDefault( static_cast<std::size_t>( pairCount ) );

        for ( int pairIndex = 0; pairIndex < pairCount; ++pairIndex )
        {
            const int value = candidatePairSortKeys[pairIndex];
            candidatePairSortScratch[lowOffsets[value & kCandidatePairSortLowMask]++] = value;
        }

        int highCounts[kCandidatePairSortHighBucketCount] = {};
        int highOffsets[kCandidatePairSortHighBucketCount] = {};

        for ( int pairIndex = 0; pairIndex < pairCount; ++pairIndex )
        {
            ++highCounts[( candidatePairSortScratch[pairIndex] >> kCandidatePairSortLowBitCount ) &
                         kCandidatePairSortHighMask];
        }

        for ( int digit = 1; digit < kCandidatePairSortHighBucketCount; ++digit )
        {
            highOffsets[digit] = highOffsets[digit - 1] + highCounts[digit - 1];
        }

        for ( int pairIndex = 0; pairIndex < pairCount; ++pairIndex )
        {
            const int value = candidatePairSortScratch[pairIndex];
            candidatePairSortKeys[highOffsets[( value >> kCandidatePairSortLowBitCount ) &
                                              kCandidatePairSortHighMask]++] = value;
        }

        for ( int pairIndex = 0; pairIndex < pairCount; ++pairIndex )
        {
            outPairs.emplace_back( minIndex, candidatePairSortKeys[pairIndex] );
        }
    }
}


void SpatialGrid::GetFilteredCandidatePairs( SkullbonezCore::Physics::PhysicsCandidatePairList& outPairs,
                                             const SkullbonezCore::Physics::PhysicsBodyStore& bodyStore,
                                             const SkullbonezCore::Physics::ColliderStore& colliderStore,
                                             std::span<const uint8_t> sleepState, float dt, float contactSkin,
                                             SkullbonezCore::Physics::PhysicsCandidatePairList& sleepPrunedPairs,
                                             bool restrictToPairSourceCells )
{
    GetFilteredCandidatePairsImpl( outPairs, bodyStore, colliderStore, sleepState, dt, contactSkin, &sleepPrunedPairs,
                                   restrictToPairSourceCells );
}


void SpatialGrid::GetFilteredCandidatePairs( SkullbonezCore::Physics::PhysicsCandidatePairList& outPairs,
                                             const SkullbonezCore::Physics::PhysicsBodyStore& bodyStore,
                                             const SkullbonezCore::Physics::ColliderStore& colliderStore,
                                             std::span<const uint8_t> sleepState, float dt, float contactSkin,
                                             bool restrictToPairSourceCells )
{
    GetFilteredCandidatePairsImpl( outPairs, bodyStore, colliderStore, sleepState, dt, contactSkin, nullptr,
                                   restrictToPairSourceCells );
}


// Active cell info is written into the caller-provided array.
// Each entry contains the grid coordinate (ix, iy, iz) and object count.
void SpatialGrid::GetActiveCells( Physics::PhysicsBroadphaseActiveCell* outCells, int maxCells ) const
{
    int count = ( activeBucketCount < maxCells ) ? activeBucketCount : maxCells;

    for ( int i = 0; i < count; ++i )
    {
        int bi = activeBuckets[i];
        const Bucket& b = buckets[bi];
        outCells[i].ix = b.ix;
        outCells[i].iy = b.iy;
        outCells[i].iz = b.iz;
        outCells[i].objectCount = b.count + ( b.overlayGeneration == overlayGeneration ? b.overlayCount : 0 );
    }
}


uint64_t SpatialGrid::CollectDynamicMemoryBytes() const
{
    uint64_t bytes = static_cast<uint64_t>( entries.committed_bytes() + overlayEntries.committed_bytes() +
                                            bodyMemberships.committed_bytes() + pairMembershipOrdinals.committed_bytes() +
                                            pairMembershipOffsets.committed_bytes() +
                                            pairMembershipCounts.committed_bytes() + candidatePairHeads.committed_bytes() +
                                            candidatePairNodes.committed_bytes() + candidatePairSortKeys.committed_bytes() +
                                            candidatePairSortScratch.committed_bytes() + cellObjectSeen.committed_bytes() );

    return bytes;
}
