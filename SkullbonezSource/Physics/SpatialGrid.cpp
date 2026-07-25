/*
File: SkullbonezSource/Physics/SpatialGrid.cpp
Purpose:
  Partitions space into broadphase cells so physics can test nearby objects cheaply.

Summary:
  Persistent per-body integer ranges own ordinary cell membership across fixed
  steps. Fixed hash chains and intrusive back-links make changed cells reusable
  without allocation, while a separately stamped overlay carries swept motion
  for one step only. Canonical emission hides all storage-history ordering.

Glossary:
  AABB (Axis-Aligned Bounding Box): Box aligned to world axes, often used for
  cheap broadphase overlap tests.
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.
  Persistent membership: Ordinary cell occupancy retained until the body's
    integer range changes or a cold reset invalidates all ranges.
  Swept overlay: Velocity-dependent cell occupancy that expires at BeginFrame.
  Intrusive back-link: Index stored in pooled rows so removal can unlink both
    the cell chain and body chain without searching global storage.
  Lane F: Fatal invariant lane for should-never-happen engine state.
  Pair-source stamp: Frame generation marking a cell reached by an awake body
    without changing persistent membership stored in that cell.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - SpatialGrid capacity and index failures are Lane F invariants: callers
    cannot recover while preserving the frame's broadphase contract.
  - Persistent membership is a pure function of current integer ranges; only
    storage order carries history, and canonical pair emission removes it.
  - Production may skip unstamped sleep-only cells, while Debug can walk all
    retained cells to preserve bounded SleepPrunedPair evidence.

Related:
  - SkullbonezSource/Physics/SpatialGrid.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
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
void ValidateBroadphaseBounds( int index, const Vector3& minBounds, const Vector3& maxBounds, float inverseCellSize )
{
    const bool finite = std::isfinite( minBounds.x ) && std::isfinite( minBounds.y ) && std::isfinite( minBounds.z ) &&
                        std::isfinite( maxBounds.x ) && std::isfinite( maxBounds.y ) && std::isfinite( maxBounds.z );
    const float extent = SpatialGrid::MAX_WORLD_COORDINATE;
    const bool insideExtent = finite && fabsf( minBounds.x ) <= extent && fabsf( minBounds.y ) <= extent &&
                              fabsf( minBounds.z ) <= extent && fabsf( maxBounds.x ) <= extent &&
                              fabsf( maxBounds.y ) <= extent && fabsf( maxBounds.z ) <= extent;
    const bool ordered =
        finite && minBounds.x <= maxBounds.x && minBounds.y <= maxBounds.y && minBounds.z <= maxBounds.z;
    constexpr double MAX_CONVERTIBLE_CELL_COORDINATE = static_cast<double>( INT_MAX ) - 1024.0;
    const auto cellCoordinateIsRepresentable = [&]( float value )
    {
        // Why: perform the guard in double. Converting INT_MAX-1 to float rounds
        // up to 2^31 on MSVC, which would bless the very value the later int
        // conversion cannot represent.
        return std::fabs( static_cast<double>( value ) * static_cast<double>( inverseCellSize ) ) <=
               MAX_CONVERTIBLE_CELL_COORDINATE;
    };

    const bool convertible =
        insideExtent && cellCoordinateIsRepresentable( minBounds.x ) && cellCoordinateIsRepresentable( minBounds.y ) &&
        cellCoordinateIsRepresentable( minBounds.z ) && cellCoordinateIsRepresentable( maxBounds.x ) &&
        cellCoordinateIsRepresentable( maxBounds.y ) && cellCoordinateIsRepresentable( maxBounds.z );
    if ( !insideExtent || !ordered || !convertible )
    {
        // Lane F: non-finite, inverted, or unrepresentable physics bounds are
        // corrupt engine state. Continuing would either hide the body from
        // broadphase or invoke undefined float-to-int conversion behavior.
        SB_FATAL(
            "Physics/SpatialGrid",
            "SpatialGrid bounds invalid: body=%d min=(%.9g,%.9g,%.9g) max=(%.9g,%.9g,%.9g) "
            "max_world_coordinate=%.9g.",
            index,
            minBounds.x,
            minBounds.y,
            minBounds.z,
            maxBounds.x,
            maxBounds.y,
            maxBounds.z,
            extent
        );
    }
}

int16_t ClampVisualizationCell( int cell )
{
    // The hash key retains the full cell coordinate. Only the debug
    // visualization payload is narrowed, so clamp instead of wrapping it.
    return static_cast<int16_t>( (std::max)( static_cast<int>( INT16_MIN ),
                                             (std::min)( static_cast<int>( INT16_MAX ), cell ) ) );
}

int64_t SpatialCellKey( int ix, int iy, int iz )
{
    return ( int64_t( ix ) * 73856093 ) ^ ( int64_t( iy ) * 19349663 ) ^ ( int64_t( iz ) * 83492791 );
}
} // namespace


SpatialGrid::SpatialGrid( float fCellSize )
    : cellSize( 1.0f ), inverseCellSize( 1.0f ), overlayGeneration( 1 ), pairSourceGeneration( 1 ),
      freeBucketHead( -1 ), freeEntryHead( -1 ), persistentEntryCount( 0 ), persistentEntryHighWater( 0 ),
      objectCount( 0 ), activeBucketCount( 0 ), overlayEntryCount( 0 ), overlayActiveBucketCount( 0 ),
      cellObjectGeneration( 0 )
{
    Clear();
    SetCellSize( fCellSize );
}


void SpatialGrid::SetCellSize( float fCellSize )
{
    if ( fCellSize < MIN_CELL_SIZE || !std::isfinite( fCellSize ) )
    {
        // Lane F: an invalid cell size makes every subsequent float-to-cell
        // conversion unsafe; construction and runtime reconfiguration share
        // this owner boundary.
        SB_FATAL(
            "Physics/SpatialGrid",
            "SpatialGrid cell size invalid: value=%.9g minimum=%.9g.",
            fCellSize,
            MIN_CELL_SIZE
        );
    }

    if ( fCellSize == cellSize )
    {
        return;
    }

    cellSize = fCellSize;
    inverseCellSize = 1.0f / fCellSize;
    // Invariant: integer cell ranges are meaningful only for the cell size that
    // produced them. Runtime tuning and scene loads are cold rebuild boundaries.
    Clear();
}


// Full reset is intentionally cold. BeginFrame owns the steady-step transition.
void SpatialGrid::Clear()
{
    // Cold path: scene load, replay restore, or a cell-size change may invalidate
    // every dense-row membership. Steady fixed steps use BeginFrame instead.
    memset( cellObjectSeen, 0, sizeof( cellObjectSeen ) );
    for ( int bucketIndex = 0; bucketIndex < TABLE_SIZE; ++bucketIndex )
    {
        bucketHashHeads[bucketIndex] = -1;
        buckets[bucketIndex] = Bucket {};
        buckets[bucketIndex].activeIndex = -1;
        buckets[bucketIndex].nextFree = bucketIndex + 1 < TABLE_SIZE ? bucketIndex + 1 : -1;
    }
    for ( int entryIndex = 0; entryIndex < MAX_CELL_ENTRIES; ++entryIndex )
    {
        entries[entryIndex].nextFree = entryIndex + 1 < MAX_CELL_ENTRIES ? entryIndex + 1 : -1;
    }
    for ( BodyMembership& membership : bodyMemberships )
    {
        membership = BodyMembership {};
    }

    overlayGeneration = 1;
    pairSourceGeneration = 1;
    freeBucketHead = 0;
    freeEntryHead = 0;
    persistentEntryCount = 0;
    persistentEntryHighWater = 0;
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
        SB_FATAL(
            "Physics/SpatialGrid",
            "SpatialGrid frame object count invalid: count=%d capacity=%d.",
            currentObjectCount,
            SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS
        );
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
        SB_FATAL(
            "Physics/SpatialGrid",
            "SpatialGrid bucket capacity exceeded: capacity=%d active=%d key=%lld phase=steady_gameplay.",
            TABLE_SIZE,
            activeBucketCount,
            static_cast<long long>( key )
        );
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
    if ( freeEntryHead == -1 )
    {
        SB_FATAL(
            "Physics/SpatialGrid",
            "SpatialGrid persistent entry capacity exceeded: owner=Physics/SpatialGrid capacity=%d "
            "high_water=%d phase=steady_gameplay.",
            MAX_CELL_ENTRIES,
            persistentEntryHighWater
        );
    }
    const int entryIndex = freeEntryHead;
    freeEntryHead = entries[entryIndex].nextFree;
    ++persistentEntryCount;
    persistentEntryHighWater = (std::max)( persistentEntryHighWater, persistentEntryCount );
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
    const int bucketIndex = FindOrCreateBucket(
        SpatialCellKey( ix, iy, iz ),
        ClampVisualizationCell( ix ),
        ClampVisualizationCell( iy ),
        ClampVisualizationCell( iz )
    );

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
    if ( entryIndex < 0 || entryIndex >= MAX_CELL_ENTRIES )
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
    SB_FATAL(
        "Physics/SpatialGrid",
        "SpatialGrid cached membership missing during removal: body=%d cell=(%d,%d,%d).",
        index,
        ix,
        iy,
        iz
    );
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


SpatialGrid::CellRange
SpatialGrid::RangeForBounds( int index, const Vector3& minBounds, const Vector3& maxBounds, int capacity ) const
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

    const int64_t countX = int64_t( range.maxX ) - int64_t( range.minX ) + 1;
    const int64_t countY = int64_t( range.maxY ) - int64_t( range.minY ) + 1;
    const int64_t countZ = int64_t( range.maxZ ) - int64_t( range.minZ ) + 1;
    if ( countX > capacity || countY > capacity || countZ > capacity || countX * countY > capacity ||
         countX * countY * countZ > capacity )
    {
        SB_FATAL(
            "Physics/SpatialGrid",
            "SpatialGrid bounds exceed cell-entry capacity: body=%d cells=(%lld,%lld,%lld) capacity=%d.",
            index,
            static_cast<long long>( countX ),
            static_cast<long long>( countY ),
            static_cast<long long>( countZ ),
            capacity
        );
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
    MaintainBounds(
        index,
        Vector3( position.x - radius, position.y - radius, position.z - radius ),
        Vector3( position.x + radius, position.y + radius, position.z + radius )
    );
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
    const int bucketIndex = FindOrCreateBucket(
        SpatialCellKey( ix, iy, iz ),
        ClampVisualizationCell( ix ),
        ClampVisualizationCell( iy ),
        ClampVisualizationCell( iz )
    );

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
    if ( overlayEntryCount >= MAX_SWEPT_CELL_ENTRIES )
    {
        SB_FATAL(
            "Physics/SpatialGrid",
            "SpatialGrid swept-overlay capacity exceeded: owner=Physics/SpatialGrid capacity=%d "
            "high_water=%d phase=steady_gameplay.",
            MAX_SWEPT_CELL_ENTRIES,
            overlayEntryCount
        );
    }
    overlayEntries[overlayEntryCount] = SweptOverlayEntry { index, bucket.overlayHead, ix, iy, iz };

    bucket.overlayHead = overlayEntryCount++;
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


int SpatialGrid::CollectBucketObjects( const Bucket& bucket, int* outIndices, int capacity )
{
    ++cellObjectGeneration;
    if ( cellObjectGeneration == 0 )
    {
        memset( cellObjectSeen, 0, sizeof( cellObjectSeen ) );
        cellObjectGeneration = 1;
    }
    int count = 0;
    auto append = [&]( int objectIndex )
    {
        if ( objectIndex < 0 || objectIndex >= SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS )
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
        if ( current < 0 || current >= MAX_CELL_ENTRIES )
        {
            SB_FATAL( "Physics/SpatialGrid", "SpatialGrid persistent entry chain index out of bounds" );
        }
        append( entries[current].objectIndex );
    }
    if ( bucket.overlayGeneration == overlayGeneration )
    {
        for ( int current = bucket.overlayHead; current != -1; current = overlayEntries[current].next )
        {
            if ( current < 0 || current >= MAX_SWEPT_CELL_ENTRIES )
            {
                SB_FATAL( "Physics/SpatialGrid", "SpatialGrid swept-overlay entry chain index out of bounds" );
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
    const Vector3 minBounds(
        (std::min)( position.x, endPosition.x ) - radius,
        (std::min)( position.y, endPosition.y ) - radius,
        (std::min)( position.z, endPosition.z ) - radius
    );

    const Vector3 maxBounds(
        (std::max)( position.x, endPosition.x ) + radius,
        (std::max)( position.y, endPosition.y ) + radius,
        (std::max)( position.z, endPosition.z ) + radius
    );

    // The swept range is transient; persistent membership above remains a pure
    // function of the body's current position and radius.
    ValidateBroadphaseBounds( index, minBounds, maxBounds, inverseCellSize );

    const int minX = static_cast<int>( floorf( minBounds.x * inverseCellSize ) );
    const int minY = static_cast<int>( floorf( minBounds.y * inverseCellSize ) );
    const int minZ = static_cast<int>( floorf( minBounds.z * inverseCellSize ) );
    const int maxX = static_cast<int>( floorf( maxBounds.x * inverseCellSize ) );
    const int maxY = static_cast<int>( floorf( maxBounds.y * inverseCellSize ) );
    const int maxZ = static_cast<int>( floorf( maxBounds.z * inverseCellSize ) );
    const int64_t cellCountX = int64_t( maxX ) - int64_t( minX ) + 1;
    const int64_t cellCountY = int64_t( maxY ) - int64_t( minY ) + 1;
    const int64_t cellCountZ = int64_t( maxZ ) - int64_t( minZ ) + 1;
    const bool exactAabbFits = cellCountX <= MAX_SWEPT_AABB_CELLS && cellCountY <= MAX_SWEPT_AABB_CELLS &&
                               cellCountZ <= MAX_SWEPT_AABB_CELLS &&
                               cellCountX * cellCountY * cellCountZ <= MAX_SWEPT_AABB_CELLS;

    if ( exactAabbFits )
    {
        InsertOverlayBounds( index, minBounds, maxBounds );
        return;
    }

    const float distanceSq = displacement * displacement;
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

    InsertOverlayBounds(
        index,
        Vector3( position.x - radius, position.y - radius, position.z - radius ),
        Vector3( position.x + radius, position.y + radius, position.z + radius )
    );
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
        InsertOverlayBounds(
            index,
            Vector3( sample.x - radius, sample.y - radius, sample.z - radius ),
            Vector3( sample.x + radius, sample.y + radius, sample.z + radius )
        );

        ++visitedCells;
    }

    InsertOverlayBounds(
        index,
        Vector3( endPosition.x - radius, endPosition.y - radius, endPosition.z - radius ),
        Vector3( endPosition.x + radius, endPosition.y + radius, endPosition.z + radius )
    );
}


void SpatialGrid::MarkPairSourceCells( int index )
{
    if ( index < 0 || index >= objectCount )
    {
        SB_FATAL(
            "Physics/SpatialGrid",
            "SpatialGrid pair-source body index out of bounds: index=%d count=%d.",
            index,
            objectCount
        );
    }
    for ( int entryIndex = bodyMemberships[index].entryHead; entryIndex != -1;
          entryIndex = entries[entryIndex].nextForObject )
    {
        buckets[entries[entryIndex].bucketIndex].pairSourceGeneration = pairSourceGeneration;
    }
}


void SpatialGrid::ResetCandidatePairDedup()
{
    // Dedup bits are frame-local; stale bits would hide candidate pairs.
    assert(
        objectCount >= 0 && objectCount <= SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS && "objectCount OOB"
    );
    if ( objectCount < 0 || objectCount > SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS )
    {
        SB_FATAL( "Physics/SpatialGrid", "SpatialGrid object count out of bounds" );
    }
    int pairBits = objectCount * ( objectCount - 1 ) / 2;
    int wordsNeeded = ( pairBits + 63 ) / 64;
    if ( wordsNeeded > PAIR_WORDS )
    {
        wordsNeeded = PAIR_WORDS;
    }
    memset( pairSeen, 0, wordsNeeded * sizeof( uint64_t ) );
}


bool SpatialGrid::MarkCandidatePairFirstSeen(
    int a,
    int b,
    const SkullbonezCore::Physics::BroadphaseCandidateFilterContext* filter,
    std::vector<std::pair<int, int>>* sleepPrunedPairs
)
{
    // Triangular index: b*(b-1)/2 + a (requires the normalized a < b pair).
    assert( a >= 0 && a < b && b < objectCount && "candidate pair identity out of bounds" );
    if ( a < 0 || a >= b || b >= objectCount )
    {
        SB_FATAL( "Physics/SpatialGrid", "SpatialGrid candidate pair identity out of bounds" );
    }
    const int pairIndex = b * ( b - 1 ) / 2 + a;
    const int word = pairIndex >> 6;
    assert( word >= 0 && word < PAIR_WORDS && "pairSeen word index OOB" );
    if ( word < 0 || word >= PAIR_WORDS )
    {
        SB_FATAL( "Physics/SpatialGrid", "SpatialGrid pair dedup index out of bounds" );
    }
    const uint64_t bit = uint64_t( 1 ) << ( pairIndex & 63 );
    if ( pairSeen[word] & bit )
    {
        return false;
    }

    pairSeen[word] |= bit;
    if ( SkullbonezCore::Physics::BroadphaseCandidateBothSleeping( filter, a, b ) )
    {
        // Preserve the old diagnostic boundary: SleepPrunedPair described a
        // geometrically admitted candidate, not every dormant co-cell pair.
        if ( !SkullbonezCore::Physics::BroadphaseCandidateGeometryCanTouch( filter, a, b ) )
        {
            return false;
        }
        if ( sleepPrunedPairs )
        {
            if ( sleepPrunedPairs->size() >= sleepPrunedPairs->capacity() )
            {
                SB_FATAL(
                    "Physics/SpatialGrid",
                    "Sleep-pruned diagnostic reserve exhausted: capacity=%zu phase=diagnostic.",
                    sleepPrunedPairs->capacity()
                );
            }
            sleepPrunedPairs->emplace_back( a, b );
        }
        return false;
    }
    return SkullbonezCore::Physics::BroadphaseCandidateGeometryCanTouch( filter, a, b );
}


// Concept: discovery order is not solver order.
//
// Buckets and their linked lists are storage details whose order changes when
// the grid becomes persistent. Each newly discovered pair is therefore staged
// under its smaller body index, radix-sorted by its larger index, and only then
// emitted. The result is the history-free (minIndex,maxIndex) order that P1
// makes the byte-exact baseline for later broadphase work.
void SpatialGrid::GetCandidatePairs(
    std::vector<std::pair<int, int>>& outPairs,
    const SkullbonezCore::Physics::BroadphaseCandidateFilterContext* filter,
    std::vector<std::pair<int, int>>* sleepPrunedPairs,
    bool restrictToPairSourceCells
)
{
    outPairs.clear();
    if ( sleepPrunedPairs )
    {
        sleepPrunedPairs->clear();
    }
    ResetCandidatePairDedup();
    std::fill_n( candidatePairHeads, objectCount, -1 );
    int candidatePairNodeCount = 0;

    for ( int activeIndex = 0; activeIndex < activeBucketCount; ++activeIndex )
    {
        int bi = activeBuckets[activeIndex];
        assert( bi >= 0 && bi < TABLE_SIZE && "active bucket index OOB" );
        if ( bi < 0 || bi >= TABLE_SIZE )
        {
            SB_FATAL( "Physics/SpatialGrid", "SpatialGrid active bucket index out of bounds" );
        }
        Bucket& b = buckets[bi];
        if ( !b.occupied )
        {
            continue;
        }
        if ( restrictToPairSourceCells && b.pairSourceGeneration != pairSourceGeneration )
        {
            continue;
        }

        // Why: persistence keeps singleton cells alive across frames. Pair
        // collection must retain P1's cheap "fewer than two occupants" exit or
        // settled/sparse scenes pay one staging walk for every live cell. Only
        // the current stamped overlay contributes transient occupants.
        const int currentOverlayCount = b.overlayGeneration == overlayGeneration ? b.overlayCount : 0;
        if ( b.count + currentOverlayCount < 2 )
        {
            continue;
        }

        int cellIndices[SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS];
        const int cellCount =
            CollectBucketObjects( b, cellIndices, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
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

                if ( !MarkCandidatePairFirstSeen( a, bIdx, filter, sleepPrunedPairs ) )
                {
                    continue;
                }

                const size_t callerCapacity = outPairs.capacity();
                if ( candidatePairNodeCount >= MAX_CANDIDATE_PAIRS ||
                     static_cast<size_t>( candidatePairNodeCount ) >= callerCapacity )
                {
                    // Lane F: growing or dropping the list would respectively
                    // violate the runtime allocation policy or hide a collision.
                    SB_FATAL(
                        "Physics/SpatialGrid",
                        "Canonical candidate list exhausted: owner=Physics/SpatialGrid "
                        "capacity=%zu fixed_capacity=%d high_water=%d phase=steady_gameplay.",
                        callerCapacity,
                        MAX_CANDIDATE_PAIRS,
                        candidatePairNodeCount
                    );
                }
                candidatePairNodes[candidatePairNodeCount] = CandidatePairNode { bIdx, candidatePairHeads[a] };

                candidatePairHeads[a] = candidatePairNodeCount++;
            }
        }
    }

    // Two stable radix passes sort each per-minimum list across the 13 bits of
    // the supported 8,192-body index. Total work is proportional to bodies plus
    // accepted pairs and uses only the grid's fixed staging arrays.
    for ( int minIndex = 0; minIndex < objectCount; ++minIndex )
    {
        int pairCount = 0;
        for ( int nodeIndex = candidatePairHeads[minIndex]; nodeIndex != -1;
              nodeIndex = candidatePairNodes[nodeIndex].next )
        {
            candidatePairSortKeys[pairCount++] = candidatePairNodes[nodeIndex].maxIndex;
        }
        if ( pairCount == 0 )
        {
            continue;
        }

        int lowCounts[128] = {};
        int lowOffsets[128] = {};

        for ( int pairIndex = 0; pairIndex < pairCount; ++pairIndex )
        {
            ++lowCounts[candidatePairSortKeys[pairIndex] & 0x7f];
        }
        for ( int digit = 1; digit < 128; ++digit )
        {
            lowOffsets[digit] = lowOffsets[digit - 1] + lowCounts[digit - 1];
        }
        for ( int pairIndex = 0; pairIndex < pairCount; ++pairIndex )
        {
            const int value = candidatePairSortKeys[pairIndex];
            candidatePairSortScratch[lowOffsets[value & 0x7f]++] = value;
        }

        int highCounts[64] = {};
        int highOffsets[64] = {};

        for ( int pairIndex = 0; pairIndex < pairCount; ++pairIndex )
        {
            ++highCounts[( candidatePairSortScratch[pairIndex] >> 7 ) & 0x3f];
        }
        for ( int digit = 1; digit < 64; ++digit )
        {
            highOffsets[digit] = highOffsets[digit - 1] + highCounts[digit - 1];
        }
        for ( int pairIndex = 0; pairIndex < pairCount; ++pairIndex )
        {
            const int value = candidatePairSortScratch[pairIndex];
            candidatePairSortKeys[highOffsets[( value >> 7 ) & 0x3f]++] = value;
        }
        for ( int pairIndex = 0; pairIndex < pairCount; ++pairIndex )
        {
            outPairs.emplace_back( minIndex, candidatePairSortKeys[pairIndex] );
        }
    }
}


#if defined( _DEBUG )
void SpatialGrid::GetCandidatePairsLegacyForOracle(
    std::vector<std::pair<int, int>>& outPairs,
    const SkullbonezCore::Physics::BroadphaseCandidateFilterContext* filter
)
{
    outPairs.clear();
    ResetCandidatePairDedup();

    for ( int activeIndex = 0; activeIndex < activeBucketCount; ++activeIndex )
    {
        const int bucketIndex = activeBuckets[activeIndex];
        assert( bucketIndex >= 0 && bucketIndex < TABLE_SIZE && "active bucket index OOB" );
        if ( bucketIndex < 0 || bucketIndex >= TABLE_SIZE )
        {
            SB_FATAL( "Physics/SpatialGrid", "SpatialGrid active bucket index out of bounds" );
        }
        const Bucket& bucket = buckets[bucketIndex];
        if ( !bucket.occupied )
        {
            continue;
        }

        int cellIndices[SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS];
        const int cellCount =
            CollectBucketObjects( bucket, cellIndices, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
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
                if ( !MarkCandidatePairFirstSeen( a, b, filter, nullptr ) )
                {
                    continue;
                }
                if ( outPairs.size() >= outPairs.capacity() )
                {
                    SB_FATAL(
                        "Physics/SpatialGrid",
                        "Legacy oracle candidate reserve exhausted: capacity=%zu phase=diagnostic.",
                        outPairs.capacity()
                    );
                }
                outPairs.emplace_back( a, b );
            }
        }
    }
}
#endif


// Active cell info is written into the caller-provided array.
// Each entry contains the grid coordinate (ix, iy, iz) and object count.
void SpatialGrid::GetActiveCells( ActiveCell* outCells, int maxCells ) const
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
