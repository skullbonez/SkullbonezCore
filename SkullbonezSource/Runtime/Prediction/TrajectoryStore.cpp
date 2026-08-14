/*
File: SkullbonezSource/Runtime/Prediction/TrajectoryStore.cpp
Purpose:
  Implements replay trajectory record replacement, prefix publication, and
  bounded reserve helpers.

Summary:
  A trajectory record is immutable to readers up to its published prefix.
  Builders reactivate capacity-compatible dormant records, append unpublished
  points, then publish a larger prefix once those points are coherent.

Glossary:
  Replay allocation scope: RuntimeAllocationTracker phase used while approved
    replay growth performs the actual vector reserve.

Invariants:
  - Reserve helpers use `replay_prediction_working_set` and fail before vector
    reserve when the shared replay prediction hard cap would be exceeded.
  - Append never calls reserve; callers must reserve record point capacity first.
  - Clear changes logical visibility but does not reclaim nested record storage.

Related:
  - SkullbonezSource/Runtime/Prediction/TrajectoryStore.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.h
  - Agentic/Reference/engine-glossary.md
*/
#include "TrajectoryStore.h"
#include "ReplayPredictionReserve.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/Allocation/RuntimeReserveAllocator.h"

#include <algorithm>
#include <limits>

namespace SkullbonezCore::Runtime
{
using namespace ReplayPredictionReserveOperations;
namespace
{
template <typename T> bool CapacityBytesForCount( std::size_t capacity, uint64_t& outBytes ) noexcept
{
    constexpr uint64_t elementBytes = static_cast<uint64_t>( sizeof( T ) );
    const uint64_t maxCapacity = ( std::numeric_limits<uint64_t>::max )() / elementBytes;

    if ( capacity > maxCapacity )
    {
        return false;
    }

    outBytes = static_cast<uint64_t>( capacity ) * elementBytes;
    return true;
}

bool ByteCountFitsReserveRequest( uint64_t bytes ) noexcept
{
    return bytes <= static_cast<uint64_t>( REPLAY_PREDICTION_RESERVE_HARD_BYTES ) &&
           bytes <= static_cast<uint64_t>( ( std::numeric_limits<int>::max )() );
}
} // namespace

bool operator==( const ReplayTrajectoryRecordKey& lhs, const ReplayTrajectoryRecordKey& rhs ) noexcept
{
    return lhs.bodyId.value == rhs.bodyId.value && lhs.lane == rhs.lane && lhs.branchOrdinal == rhs.branchOrdinal;
}

bool operator!=( const ReplayTrajectoryRecordKey& lhs, const ReplayTrajectoryRecordKey& rhs ) noexcept
{
    return !( lhs == rhs );
}

void ReplayTrajectoryStore::Clear() noexcept
{
    const bool hadPublishedState = activeRecordCount != 0u;

    for ( ReplayTrajectoryRecord& record : std::span<ReplayTrajectoryRecord>( records.data(), activeRecordCount ) )
    {
        record.publishedPointCount = 0;
    }

    activeRecordCount = 0;

    if ( hadPublishedState )
    {
        ++publicationVersion;
    }
}

ReplayTrajectoryRecord* ReplayTrajectoryStore::FindRecord( const ReplayTrajectoryRecordKey& key ) noexcept
{
    for ( ReplayTrajectoryRecord& record : std::span<ReplayTrajectoryRecord>( records.data(), activeRecordCount ) )
    {
        if ( record.key == key )
        {
            return &record;
        }
    }

    return nullptr;
}

const ReplayTrajectoryRecord* ReplayTrajectoryStore::FindRecord( const ReplayTrajectoryRecordKey& key ) const noexcept
{
    for ( const ReplayTrajectoryRecord& record : ActiveRecords() )
    {
        if ( record.key == key )
        {
            return &record;
        }
    }

    return nullptr;
}

ReplayTrajectoryRecord* ReplayTrajectoryStore::BeginReplaceRecord( const ReplayTrajectoryRecordKey& key, uint16_t styleId,
                                                                   Physics::PhysicsSceneObjectId parentId, int depth,
                                                                   ReplayFrameIndex firstFrame, bool contactDerived,
                                                                   std::size_t requiredPointCapacity )
{
    ReplayTrajectoryRecord* record = FindRecord( key );

    if ( !record )
    {
        const std::size_t dormantIndex = ReplayTrajectoryStoreOperations::SelectDormantRecordIndex( records,
                                                                                                    activeRecordCount, key,
                                                                                                    requiredPointCapacity );

        if ( dormantIndex < records.size() )
        {
            // Why: the selector prefers the same stable key, then any slot
            // already large enough, then the largest remaining slot. This
            // keeps replay reserve accounting flat without making record order
            // depend on the dormant bank's previous generation.
            std::iter_swap( records.begin() + static_cast<std::ptrdiff_t>( activeRecordCount ),
                            records.begin() + static_cast<std::ptrdiff_t>( dormantIndex ) );
            record = &records[activeRecordCount];
            ++activeRecordCount;
        }
        else
        {
            if ( records.size() >= records.capacity() )
            {
                return nullptr;
            }

            records.emplace_back();
            record = &records.back();
            ++activeRecordCount;
        }

        record->key = key;
    }

    record->version = AllocateVersion();
    record->publishedPointCount = 0;
    record->styleId = styleId;
    record->parentId = parentId;
    record->depth = depth;
    record->firstFrame = firstFrame;
    record->contactDerived = contactDerived;
    record->points.clear();
    ++publicationVersion;
    return record;
}

bool ReplayTrajectoryStore::TryAppendPoint( ReplayTrajectoryRecord& record, const ReplayTrajectoryPoint& point )
{
    if ( record.points.size() >= record.points.capacity() )
    {
        return false;
    }

    record.points.push_back( point );
    return true;
}

void ReplayTrajectoryStore::PublishPrefix( ReplayTrajectoryRecord& record, std::size_t pointCount ) noexcept
{
    const std::size_t publishedPointCount = (std::min)( pointCount, record.points.size() );

    if ( record.publishedPointCount != publishedPointCount )
    {
        record.publishedPointCount = publishedPointCount;
        ++publicationVersion;
    }
}

std::size_t ReplayTrajectoryStore::TrimPublishedPointsBeforeFrame( ReplayTrajectoryRecord& record,
                                                                   ReplayFrameIndex firstRetainedFrame ) noexcept
{
    const std::size_t publishedCount = (std::min)( record.publishedPointCount, record.points.size() );
    const auto publishedEnd = record.points.begin() + static_cast<std::ptrdiff_t>( publishedCount );
    const auto firstKept = std::lower_bound( record.points.begin(), publishedEnd, firstRetainedFrame,
                                             []( const ReplayTrajectoryPoint& point, ReplayFrameIndex frame )
                                             { return point.frameIndex < frame; } );

    const std::size_t removedCount = static_cast<std::size_t>( firstKept - record.points.begin() );

    if ( removedCount == 0u )
    {
        return 0u;
    }

    // Invariant: erase preserves vector capacity and record version. The next
    // capture can append into the freed slot, while draw readers keep a valid
    // published prefix instead of observing a replacement gap/flicker.
    record.points.erase( record.points.begin(), firstKept );
    record.publishedPointCount = publishedCount - removedCount;
    ++publicationVersion;

    if ( !record.points.empty() )
    {
        record.firstFrame = record.points.front().frameIndex;
    }
    else
    {
        record.firstFrame = firstRetainedFrame;
    }

    return removedCount;
}

bool ReplayTrajectoryStore::ReserveRecords( std::size_t requestedCapacity, int frameNumber )
{
    if ( requestedCapacity <= records.capacity() )
    {
        return true;
    }

    uint64_t oldRecordBytes = 0;
    uint64_t requestedRecordBytes = 0;

    if ( !CapacityBytesForCount<ReplayTrajectoryRecord>( records.capacity(), oldRecordBytes ) ||
         !CapacityBytesForCount<ReplayTrajectoryRecord>( requestedCapacity, requestedRecordBytes ) )
    {
        return false;
    }

    const uint64_t oldStoreBytes = CapacityBytes();

    if ( oldStoreBytes < oldRecordBytes ||
         requestedRecordBytes > ( std::numeric_limits<uint64_t>::max )() - ( oldStoreBytes - oldRecordBytes ) )
    {
        return false;
    }

    const uint64_t requestedStoreBytes = oldStoreBytes - oldRecordBytes + requestedRecordBytes;

    if ( !ByteCountFitsReserveRequest( requestedStoreBytes ) )
    {
        return false;
    }

    SkullbonezCore::Core::Allocation::RuntimeReserveGrowthResult result = {};

    if ( !RequestReplayPredictionReserveGrowth( "ReplayTrajectoryStore::records", frameNumber,
                                                static_cast<int>( oldStoreBytes ), static_cast<int>( requestedStoreBytes ),
                                                1, result ) )
    {
        return false;
    }

    const SkullbonezCore::Core::Allocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
    SkullbonezCore::Core::Allocation::RuntimeAllocationScope replayAllocationScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::Replay );

    SkullbonezCore::Core::Allocation::RuntimeReserveOwnerScope ownerScope( owner );
    SkullbonezCore::Core::Allocation::RuntimeReserveGrowthScope
        growthScope( owner, SkullbonezCore::Core::Allocation::RuntimeReservePhase::Replay, result );

    records.reserve( requestedCapacity );
    return requestedCapacity <= records.capacity();
}

bool ReplayTrajectoryStore::ReserveRecordPoints( ReplayTrajectoryRecord& record, std::size_t requestedCapacity,
                                                 int frameNumber )
{
    if ( requestedCapacity <= record.points.capacity() )
    {
        return true;
    }

    uint64_t oldPointBytes = 0;
    uint64_t requestedPointBytes = 0;

    if ( !CapacityBytesForCount<ReplayTrajectoryPoint>( record.points.capacity(), oldPointBytes ) ||
         !CapacityBytesForCount<ReplayTrajectoryPoint>( requestedCapacity, requestedPointBytes ) )
    {
        return false;
    }

    const uint64_t oldStoreBytes = CapacityBytes();

    if ( oldStoreBytes < oldPointBytes ||
         requestedPointBytes > ( std::numeric_limits<uint64_t>::max )() - ( oldStoreBytes - oldPointBytes ) )
    {
        return false;
    }

    const uint64_t requestedStoreBytes = oldStoreBytes - oldPointBytes + requestedPointBytes;

    if ( !ByteCountFitsReserveRequest( requestedStoreBytes ) )
    {
        return false;
    }

    SkullbonezCore::Core::Allocation::RuntimeReserveGrowthResult result = {};

    if ( !RequestReplayPredictionReserveGrowth( "ReplayTrajectoryRecord::points", frameNumber,
                                                static_cast<int>( oldStoreBytes ), static_cast<int>( requestedStoreBytes ),
                                                1, result ) )
    {
        return false;
    }

    const SkullbonezCore::Core::Allocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
    SkullbonezCore::Core::Allocation::RuntimeAllocationScope replayAllocationScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::Replay );

    SkullbonezCore::Core::Allocation::RuntimeReserveOwnerScope ownerScope( owner );
    SkullbonezCore::Core::Allocation::RuntimeReserveGrowthScope
        growthScope( owner, SkullbonezCore::Core::Allocation::RuntimeReservePhase::Replay, result );

    record.points.reserve( requestedCapacity );
    return requestedCapacity <= record.points.capacity();
}

std::size_t ReplayTrajectoryStore::RecordCount() const noexcept
{
    return activeRecordCount;
}

std::size_t ReplayTrajectoryStore::PointCount() const noexcept
{
    std::size_t count = 0;

    for ( const ReplayTrajectoryRecord& record : ActiveRecords() )
    {
        count += record.points.size();
    }

    return count;
}

uint64_t ReplayTrajectoryStore::CapacityBytes() const noexcept
{
    uint64_t recordBytes = 0;

    if ( !CapacityBytesForCount<ReplayTrajectoryRecord>( records.capacity(), recordBytes ) )
    {
        return 0;
    }

    uint64_t total = recordBytes;

    for ( const ReplayTrajectoryRecord& record : records )
    {
        uint64_t pointBytes = 0;

        if ( !CapacityBytesForCount<ReplayTrajectoryPoint>( record.points.capacity(), pointBytes ) ||
             total > ( std::numeric_limits<uint64_t>::max )() - pointBytes )
        {
            return 0;
        }

        total += pointBytes;
    }

    return total;
}

void ReplayTrajectoryStore::ReplaceRecordsFromArchive( std::vector<ReplayTrajectoryRecord>&& loadedRecords ) noexcept
{
    records = std::move( loadedRecords );
    activeRecordCount = records.size();
    ++publicationVersion;
}

uint32_t ReplayTrajectoryStore::AllocateVersion() noexcept
{
    const uint32_t version = nextVersion;
    ++nextVersion;

    if ( nextVersion == 0 )
    {
        nextVersion = 1;
    }

    return version == 0 ? AllocateVersion() : version;
}
} // namespace SkullbonezCore::Runtime
