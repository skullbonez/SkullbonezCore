/*
File: SkullbonezSource/Runtime/Replay/TrajectoryStore.cpp
Purpose:
  Implements replay trajectory record replacement, prefix publication, and
  bounded reserve helpers.

Mental model:
  A trajectory record is immutable to readers up to its published prefix.
  Builders may reserve storage, replace records, append unpublished points, then
  publish a larger prefix once those points are coherent.

Glossary:
  Capacity bytes: Vector storage already reserved for records or point arrays.
  Replay allocation scope: RuntimeAllocationTracker phase used while approved
    replay growth performs the actual vector reserve.

Invariants:
  - Reserve helpers use `replay_prediction_working_set` and fail before vector
    reserve when the shared replay prediction hard cap would be exceeded.
  - Append never calls reserve; callers must reserve record point capacity first.

Related:
  - SkullbonezSource/Runtime/Replay/TrajectoryStore.h
  - SkullbonezSource/Runtime/Replay/ReplayPredictionReserve.h
*/
#include "TrajectoryStore.h"
#include "ReplayPredictionReserve.h"
#include "../Allocation/RuntimeAllocationTracker.h"
#include "../Allocation/RuntimeReserveAllocator.h"

#include <algorithm>
#include <limits>

namespace SkullbonezCore::Basics
{
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
    for ( ReplayTrajectoryRecord& record : records )
    {
        record.points.clear();
        record.publishedPointCount = 0;
    }
    records.clear();
}

ReplayTrajectoryRecord* ReplayTrajectoryStore::FindRecord( const ReplayTrajectoryRecordKey& key ) noexcept
{
    for ( ReplayTrajectoryRecord& record : records )
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
    for ( const ReplayTrajectoryRecord& record : records )
    {
        if ( record.key == key )
        {
            return &record;
        }
    }
    return nullptr;
}

ReplayTrajectoryRecord* ReplayTrajectoryStore::BeginReplaceRecord( const ReplayTrajectoryRecordKey& key,
                                                                   uint16_t styleId,
                                                                   ReplayBodyId parentId,
                                                                   int depth,
                                                                   ReplayFrameIndex firstFrame,
                                                                   bool contactDerived )
{
    ReplayTrajectoryRecord* record = FindRecord( key );
    if ( !record )
    {
        if ( records.size() >= records.capacity() )
        {
            return nullptr;
        }
        records.emplace_back();
        record = &records.back();
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
    record.publishedPointCount = (std::min)( pointCount, record.points.size() );
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

    Runtime::Allocation::RuntimeReserveGrowthResult result = {};
    if ( !RequestReplayPredictionReserveGrowth( "ReplayTrajectoryStore::records",
                                                frameNumber,
                                                static_cast<int>( oldStoreBytes ),
                                                static_cast<int>( requestedStoreBytes ),
                                                1,
                                                result ) )
    {
        return false;
    }

    const Runtime::Allocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
    Runtime::Allocation::RuntimeAllocationScope replayAllocationScope(
        Runtime::Allocation::RuntimeAllocationPhase::Replay );
    Runtime::Allocation::RuntimeReserveOwnerScope ownerScope( owner );
    Runtime::Allocation::RuntimeReserveGrowthScope growthScope( owner,
                                                                Runtime::Allocation::RuntimeReservePhase::Replay,
                                                                result );
    records.reserve( requestedCapacity );
    return requestedCapacity <= records.capacity();
}

bool ReplayTrajectoryStore::ReserveRecordPoints( ReplayTrajectoryRecord& record,
                                                 std::size_t requestedCapacity,
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

    Runtime::Allocation::RuntimeReserveGrowthResult result = {};
    if ( !RequestReplayPredictionReserveGrowth( "ReplayTrajectoryRecord::points",
                                                frameNumber,
                                                static_cast<int>( oldStoreBytes ),
                                                static_cast<int>( requestedStoreBytes ),
                                                1,
                                                result ) )
    {
        return false;
    }

    const Runtime::Allocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
    Runtime::Allocation::RuntimeAllocationScope replayAllocationScope(
        Runtime::Allocation::RuntimeAllocationPhase::Replay );
    Runtime::Allocation::RuntimeReserveOwnerScope ownerScope( owner );
    Runtime::Allocation::RuntimeReserveGrowthScope growthScope( owner,
                                                                Runtime::Allocation::RuntimeReservePhase::Replay,
                                                                result );
    record.points.reserve( requestedCapacity );
    return requestedCapacity <= record.points.capacity();
}

std::size_t ReplayTrajectoryStore::RecordCount() const noexcept
{
    return records.size();
}

std::size_t ReplayTrajectoryStore::PointCount() const noexcept
{
    std::size_t count = 0;
    for ( const ReplayTrajectoryRecord& record : records )
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
} // namespace SkullbonezCore::Basics
