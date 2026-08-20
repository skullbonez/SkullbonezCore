/*
File: ContinuousPredictionSampleRing.cpp
Purpose:
  Implements fixed-storage row writes, wrap publication, and concurrent reads.

Summary:
  The producer invalidates one physical slot, writes only atomic values, commits
  it with an even release version, then advances one release-published absolute
  cursor. A reader derives the complete rolling interval from that cursor and
  accepts a copied row only when its version stayed unchanged and even.

Invariants:
  - The producer alone mutates open-row bookkeeping.
  - Prepare and ResetAfterJoin run only while the producer and readers are joined.
  - Published absolute ticks never wrap and slot versions never wrap silently.

Related:
  - SkullbonezSource/Runtime/Prediction/ContinuousPredictionSampleRing.h
  - SkullbonezTests/TestReplayPredictionScheduling.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "ContinuousPredictionSampleRing.h"
#include "ReplayPredictionReserve.h"

#include <algorithm>
#include <limits>

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
template <typename T> T AtomicLoad( const T& value, std::memory_order order ) noexcept
{
    return std::atomic_ref<T>( const_cast<T&>( value ) ).load( order );
}

template <typename T> void AtomicStore( T& destination, T value, std::memory_order order ) noexcept
{
    std::atomic_ref<T>( destination ).store( value, order );
}

bool CheckedMultiplySize( std::size_t left, std::size_t right, std::size_t& out ) noexcept
{
    if ( left != 0u && right > ( std::numeric_limits<std::size_t>::max )() / left )
    {
        return false;
    }

    out = left * right;
    return true;
}

bool CheckedAddSize( std::size_t left, std::size_t right, std::size_t& out ) noexcept
{
    if ( right > ( std::numeric_limits<std::size_t>::max )() - left )
    {
        return false;
    }

    out = left + right;
    return true;
}
} // namespace

std::size_t ContinuousPredictionSampleRingSnapshot::SegmentCount() const noexcept
{
    if ( Empty() )
    {
        return 0u;
    }

    const std::size_t firstPhysicalRow = static_cast<std::size_t>( m_oldestAbsoluteTick % m_rowCapacity );
    const std::size_t firstSegmentRows = (std::min)( m_rowCount, m_rowCapacity - firstPhysicalRow );
    return firstSegmentRows < m_rowCount ? 2u : 1u;
}

ContinuousPredictionSampleRingSegment
ContinuousPredictionSampleRingSnapshot::SegmentAt( std::size_t segmentIndex ) const noexcept
{
    if ( Empty() )
    {
        return { 0u, 0u, 0u };
    }

    const std::size_t firstPhysicalRow = static_cast<std::size_t>( m_oldestAbsoluteTick % m_rowCapacity );
    const std::size_t firstSegmentRows = (std::min)( m_rowCount, m_rowCapacity - firstPhysicalRow );

    if ( segmentIndex == 0u )
    {
        return { 0u, firstPhysicalRow, firstSegmentRows };
    }

    if ( segmentIndex == 1u && firstSegmentRows < m_rowCount )
    {
        return { firstSegmentRows, 0u, m_rowCount - firstSegmentRows };
    }

    return { 0u, 0u, 0u };
}

bool ContinuousPredictionSampleRingSnapshot::TryReadRow( std::size_t logicalRowIndex,
                                                         std::span<Math::Vector::Vector3> outPositions,
                                                         std::uint64_t& outAbsoluteTick ) const noexcept
{
    return m_owner && m_owner->TryReadRow( *this, logicalRowIndex, outPositions, outAbsoluteTick );
}

bool ContinuousPredictionSampleRing::Prepare( std::size_t rowCapacity, std::size_t bodyCount ) noexcept
{
    if ( rowCapacity == 0u || bodyCount == 0u || m_started.load( std::memory_order_acquire ) || m_rowOpen )
    {
        return false;
    }

    if ( rowCapacity == m_rowCapacity && bodyCount == m_bodyCount && !m_slotVersions.empty() && !m_slotTicks.empty() &&
         !m_positionComponents.empty() && !m_bodyWritten.empty() )
    {
        return true;
    }

    std::size_t sampleCount = 0u;
    std::size_t positionComponentCount = 0u;

    if ( !CheckedMultiplySize( rowCapacity, bodyCount, sampleCount ) ||
         !CheckedMultiplySize( sampleCount, 3u, positionComponentCount ) )
    {
        return false;
    }

    const std::uint64_t generation = m_storageGeneration.load( std::memory_order_relaxed );

    if ( generation == ( std::numeric_limits<std::uint64_t>::max )() )
    {
        MarkFailure( true );
        return false;
    }

    const std::size_t oldVersionCapacity = m_slotVersions.capacity();
    const std::size_t oldTickCapacity = m_slotTicks.capacity();
    const std::size_t oldPositionCapacity = m_positionComponents.capacity();
    const std::size_t oldWrittenCapacity = m_bodyWritten.capacity();

    // Runtime allocation policy: the ring adds no growth privilege. Every
    // backing vector uses the existing Replay-phase prediction owner and cap.
    if ( !ReplayPredictionReserveOperations::
             ReserveReplayPredictionVector( m_slotVersions, rowCapacity, 0,
                                            "ContinuousPredictionSampleRing::slotVersions" ) ||
         !ReplayPredictionReserveOperations::ReserveReplayPredictionVector( m_slotTicks, rowCapacity, 0,
                                                                            "ContinuousPredictionSampleRing::slotTicks" ) ||
         !ReplayPredictionReserveOperations::
             ReserveReplayPredictionVector( m_positionComponents, positionComponentCount, 0,
                                            "ContinuousPredictionSampleRing::positionComponents" ) ||
         !ReplayPredictionReserveOperations::ReserveReplayPredictionVector( m_bodyWritten, bodyCount, 0,
                                                                            "ContinuousPredictionSampleRing::bodyWritten" ) )
    {
        return false;
    }

    m_slotVersions.resize( rowCapacity, 0u );
    m_slotTicks.resize( rowCapacity, 0u );
    m_positionComponents.resize( positionComponentCount, 0.0f );
    m_bodyWritten.resize( bodyCount, 0u );

    std::size_t retainedBytes = 0u;
    std::size_t bytes = 0u;

    if ( !CheckedMultiplySize( m_slotVersions.capacity(), sizeof( std::uint64_t ), bytes ) ||
         !CheckedAddSize( retainedBytes, bytes, retainedBytes ) ||
         !CheckedMultiplySize( m_slotTicks.capacity(), sizeof( std::uint64_t ), bytes ) ||
         !CheckedAddSize( retainedBytes, bytes, retainedBytes ) ||
         !CheckedMultiplySize( m_positionComponents.capacity(), sizeof( float ), bytes ) ||
         !CheckedAddSize( retainedBytes, bytes, retainedBytes ) ||
         !CheckedMultiplySize( m_bodyWritten.capacity(), sizeof( std::uint8_t ), bytes ) ||
         !CheckedAddSize( retainedBytes, bytes, retainedBytes ) )
    {
        return false;
    }

    const bool storageChanged = oldVersionCapacity != m_slotVersions.capacity() ||
                                oldTickCapacity != m_slotTicks.capacity() ||
                                oldPositionCapacity != m_positionComponents.capacity() ||
                                oldWrittenCapacity != m_bodyWritten.capacity() || rowCapacity != m_rowCapacity ||
                                bodyCount != m_bodyCount;
    m_rowCapacity = rowCapacity;
    m_bodyCount = bodyCount;
    m_positionComponentCount = positionComponentCount;
    m_retainedBytes = retainedBytes;
    m_storageGeneration.store( storageChanged ? generation + 1u : generation, std::memory_order_release );
    ResetAfterJoin();
    return true;
}

bool ContinuousPredictionSampleRing::Start( std::uint64_t firstAbsoluteTick ) noexcept
{
    if ( m_slotVersions.empty() || m_started.load( std::memory_order_acquire ) || m_rowOpen ||
         firstAbsoluteTick == ( std::numeric_limits<std::uint64_t>::max )() )
    {
        return false;
    }

    m_firstAbsoluteTick.store( firstAbsoluteTick, std::memory_order_relaxed );
    m_publishedNextAbsoluteTick.store( firstAbsoluteTick, std::memory_order_release );
    m_nextAbsoluteTick = firstAbsoluteTick;
    m_cancelRequested.store( false, std::memory_order_relaxed );
    m_failed.store( false, std::memory_order_relaxed );
    m_counterOverflowed.store( false, std::memory_order_relaxed );
    m_started.store( true, std::memory_order_release );
    return true;
}

bool ContinuousPredictionSampleRing::BeginRow( std::uint64_t absoluteTick ) noexcept
{
    if ( m_cancelRequested.load( std::memory_order_acquire ) )
    {
        return false;
    }

    if ( !m_started.load( std::memory_order_acquire ) || m_failed.load( std::memory_order_acquire ) || m_rowOpen ||
         absoluteTick != m_nextAbsoluteTick )
    {
        MarkFailure( false );
        return false;
    }

    if ( absoluteTick == ( std::numeric_limits<std::uint64_t>::max )() )
    {
        MarkFailure( true );
        return false;
    }

    const std::size_t physicalRow = static_cast<std::size_t>( absoluteTick % m_rowCapacity );
    const std::uint64_t priorVersion = AtomicLoad( m_slotVersions[physicalRow], std::memory_order_relaxed );

    if ( priorVersion > ( std::numeric_limits<std::uint64_t>::max )() - 2u || ( priorVersion & 1u ) != 0u )
    {
        MarkFailure( true );
        return false;
    }

    // Invariant: odd invalidation precedes every atomic value overwrite. A
    // concurrent reader that saw the old even version must reject at recheck.
    AtomicStore( m_slotVersions[physicalRow], priorVersion + 1u, std::memory_order_release );
    AtomicStore( m_slotTicks[physicalRow], absoluteTick, std::memory_order_relaxed );
    std::fill_n( m_bodyWritten.data(), m_bodyCount, static_cast<std::uint8_t>( 0u ) );
    m_openPhysicalRow = physicalRow;
    m_openCommitVersion = priorVersion + 2u;
    m_writtenBodyCount = 0u;
    m_rowOpen = true;
    return true;
}

bool ContinuousPredictionSampleRing::WriteBodyPosition( std::size_t bodyIndex,
                                                        const Math::Vector::Vector3& position ) noexcept
{
    if ( m_cancelRequested.load( std::memory_order_acquire ) )
    {
        return false;
    }

    if ( !m_rowOpen || m_failed.load( std::memory_order_acquire ) || bodyIndex >= m_bodyCount ||
         m_bodyWritten[bodyIndex] != 0u )
    {
        MarkFailure( false );
        return false;
    }

    const std::size_t component = ( m_openPhysicalRow * m_bodyCount + bodyIndex ) * 3u;
    AtomicStore( m_positionComponents[component], position.x, std::memory_order_relaxed );
    AtomicStore( m_positionComponents[component + 1u], position.y, std::memory_order_relaxed );
    AtomicStore( m_positionComponents[component + 2u], position.z, std::memory_order_relaxed );
    m_bodyWritten[bodyIndex] = 1u;
    ++m_writtenBodyCount;
    return true;
}

bool ContinuousPredictionSampleRing::PublishRow() noexcept
{
    if ( !m_rowOpen || m_cancelRequested.load( std::memory_order_acquire ) || m_failed.load( std::memory_order_acquire ) ||
         m_writtenBodyCount != m_bodyCount )
    {
        if ( !m_cancelRequested.load( std::memory_order_relaxed ) )
        {
            MarkFailure( false );
        }

        return false;
    }

    const std::uint64_t publishedNext = m_nextAbsoluteTick + 1u;
    AtomicStore( m_slotVersions[m_openPhysicalRow], m_openCommitVersion, std::memory_order_release );

    // Publication of one cursor after the even slot commit makes both the row
    // payload and the complete logical interval visible with one acquire load.
    m_publishedNextAbsoluteTick.store( publishedNext, std::memory_order_release );
    m_nextAbsoluteTick = publishedNext;
    m_rowOpen = false;
    return true;
}

void ContinuousPredictionSampleRing::RequestCancellation() noexcept
{
    m_cancelRequested.store( true, std::memory_order_release );
}

void ContinuousPredictionSampleRing::ResetAfterJoin() noexcept
{
    m_started.store( false, std::memory_order_release );
    m_cancelRequested.store( false, std::memory_order_relaxed );
    m_failed.store( false, std::memory_order_relaxed );
    m_counterOverflowed.store( false, std::memory_order_relaxed );
    m_firstAbsoluteTick.store( 0u, std::memory_order_relaxed );
    m_publishedNextAbsoluteTick.store( 0u, std::memory_order_release );
    m_nextAbsoluteTick = 0u;
    m_openPhysicalRow = 0u;
    m_openCommitVersion = 0u;
    m_writtenBodyCount = 0u;
    m_rowOpen = false;

    for ( std::size_t row = 0u; row < m_rowCapacity; ++row )
    {
        m_slotVersions[row] = 0u;
    }

    if ( !m_bodyWritten.empty() )
    {
        std::fill_n( m_bodyWritten.data(), m_bodyCount, static_cast<std::uint8_t>( 0u ) );
    }
}

ContinuousPredictionSampleRingSnapshot ContinuousPredictionSampleRing::AcquireSnapshot() const noexcept
{
    ContinuousPredictionSampleRingSnapshot snapshot;
    snapshot.m_owner = this;
    snapshot.m_storageGeneration = m_storageGeneration.load( std::memory_order_acquire );
    snapshot.m_bodyCount = m_bodyCount;
    snapshot.m_rowCapacity = m_rowCapacity;
    snapshot.m_cancelled = m_cancelRequested.load( std::memory_order_acquire );
    snapshot.m_failed = m_failed.load( std::memory_order_acquire );

    if ( snapshot.m_cancelled || snapshot.m_failed || !m_started.load( std::memory_order_acquire ) )
    {
        return snapshot;
    }

    const std::uint64_t firstAbsoluteTick = m_firstAbsoluteTick.load( std::memory_order_relaxed );
    const std::uint64_t publishedNextAbsoluteTick = m_publishedNextAbsoluteTick.load( std::memory_order_acquire );

    if ( publishedNextAbsoluteTick <= firstAbsoluteTick )
    {
        return snapshot;
    }

    const std::uint64_t publishedCount = publishedNextAbsoluteTick - firstAbsoluteTick;
    snapshot.m_rowCount = static_cast<std::size_t>( (std::min)( publishedCount, static_cast<std::uint64_t>( m_rowCapacity ) ) );
    snapshot.m_newestAbsoluteTick = publishedNextAbsoluteTick - 1u;
    snapshot.m_oldestAbsoluteTick = snapshot.m_newestAbsoluteTick - snapshot.m_rowCount + 1u;
    return snapshot;
}

bool ContinuousPredictionSampleRing::TryReadRow( const ContinuousPredictionSampleRingSnapshot& snapshot,
                                                 std::size_t logicalRowIndex, std::span<Math::Vector::Vector3> outPositions,
                                                 std::uint64_t& outAbsoluteTick ) const noexcept
{
    if ( snapshot.m_owner != this || snapshot.m_cancelled || snapshot.m_failed ||
         snapshot.m_storageGeneration != m_storageGeneration.load( std::memory_order_acquire ) ||
         logicalRowIndex >= snapshot.m_rowCount || outPositions.size() < snapshot.m_bodyCount )
    {
        return false;
    }

    const std::uint64_t expectedTick = snapshot.m_oldestAbsoluteTick + logicalRowIndex;
    const std::size_t physicalRow = static_cast<std::size_t>( expectedTick % snapshot.m_rowCapacity );
    const std::uint64_t beginVersion = AtomicLoad( m_slotVersions[physicalRow], std::memory_order_acquire );

    if ( beginVersion == 0u || ( beginVersion & 1u ) != 0u ||
         AtomicLoad( m_slotTicks[physicalRow], std::memory_order_relaxed ) != expectedTick )
    {
        return false;
    }

    for ( std::size_t body = 0u; body < snapshot.m_bodyCount; ++body )
    {
        const std::size_t component = ( physicalRow * snapshot.m_bodyCount + body ) * 3u;
        outPositions[body] = { AtomicLoad( m_positionComponents[component], std::memory_order_relaxed ),
                               AtomicLoad( m_positionComponents[component + 1u], std::memory_order_relaxed ),
                               AtomicLoad( m_positionComponents[component + 2u], std::memory_order_relaxed ) };
    }

    const std::uint64_t endVersion = AtomicLoad( m_slotVersions[physicalRow], std::memory_order_acquire );

    if ( endVersion != beginVersion || ( endVersion & 1u ) != 0u ||
         snapshot.m_storageGeneration != m_storageGeneration.load( std::memory_order_acquire ) )
    {
        return false;
    }

    outAbsoluteTick = expectedTick;
    return true;
}

void ContinuousPredictionSampleRing::MarkFailure( bool counterOverflow ) noexcept
{
    m_failed.store( true, std::memory_order_release );

    if ( counterOverflow )
    {
        m_counterOverflowed.store( true, std::memory_order_release );
    }
}
} // namespace Runtime
} // namespace SkullbonezCore
