/*
File: SkullbonezSource/Runtime/Prediction/ReplayPredictionSolverEvidenceStore.cpp
Purpose:
  Implements bounded segmented prediction-evidence allocation and publication.

Summary:
  The build writer reserves whole coarse segments through Prediction's existing
  Replay-phase owner, copies rows into previously unpublished slots, writes one
  complete frame record, then advances an acquire/release prefix. Promotion
  flips bank identity without clearing the retired committed bank, so a reader
  that already observed it cannot race a storage reuse. Archive load uses the
  same physical-bank swap only after a cold candidate is fully validated.
  Append results distinguish bounded-capacity denial from invalid bank identity
  so live Prediction can retain a valid prefix without retrying a doomed build.

Invariants:
  - A frame prefix advances only after all referenced rows and metadata are final.
  - Capacity denial leaves no partially published frame.
  - Logical reset preserves segments; explicit release destroys them.
  - Current capacity is the exact sum of live segment allocations.
  - Archive swap performs no allocation and exposes no partially decoded bank.
  - Capacity denial leaves the exact sealed prefix promotable.

Related:
  - ReplayPredictionSolverEvidenceStore.h
  - ReplayPredictionReserve.h
  - SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h
*/
#include "ReplayPredictionSolverEvidenceStore.h"

#include "ReplayPredictionReserve.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/Allocation/RuntimeReserveAllocator.h"

#include <algorithm>
#include <limits>

namespace SkullbonezCore::Runtime
{
using namespace ReplayPredictionReserveOperations;

struct ReplayPredictionSolverEvidenceStore::FrameSegment
{
    std::array<ReplayPredictionSolverEvidenceFrame, REPLAY_PREDICTION_EVIDENCE_FRAME_SEGMENT_CAPACITY> rows;
};

struct ReplayPredictionSolverEvidenceStore::ContactSegment
{
    std::array<Physics::PhysicsSolverPersistentContactSample, REPLAY_PREDICTION_EVIDENCE_CONTACT_SEGMENT_CAPACITY> rows;
};

struct ReplayPredictionSolverEvidenceStore::PipelineSegment
{
    std::array<Physics::PhysicsPipelineRecord, REPLAY_PREDICTION_EVIDENCE_PIPELINE_SEGMENT_CAPACITY> rows;
};

namespace
{
template <typename T> bool CheckedAdd( std::size_t left, std::size_t right, T& out ) noexcept
{
    if ( right > ( std::numeric_limits<std::size_t>::max )() - left )
    {
        return false;
    }

    out = static_cast<T>( left + right );
    return true;
}

std::size_t RequiredSegments( std::size_t count, std::size_t segmentCapacity ) noexcept
{
    return count / segmentCapacity + ( count % segmentCapacity != 0u ? 1u : 0u );
}
} // namespace

ReplayPredictionSolverEvidenceStore::ReplayPredictionSolverEvidenceStore() = default;

ReplayPredictionSolverEvidenceStore::~ReplayPredictionSolverEvidenceStore() = default;

uint32_t ReplayPredictionSolverEvidenceStore::Generation() const noexcept
{
    return m_generation;
}

ReplayPredictionDetailMode ReplayPredictionSolverEvidenceStore::Mode() const noexcept
{
    return m_mode;
}

uint64_t ReplayPredictionSolverEvidenceStore::BankEpoch() const noexcept
{
    return m_bankEpoch;
}

std::size_t ReplayPredictionSolverEvidenceStore::PublishedFrameCount() const noexcept
{
    return m_publishedFrameCount.load( std::memory_order_acquire );
}

const ReplayPredictionSolverEvidenceFrame*
ReplayPredictionSolverEvidenceStore::PublishedFrame( std::size_t index ) const noexcept
{
    if ( index >= PublishedFrameCount() )
    {
        return nullptr;
    }

    const std::size_t segmentIndex = index / REPLAY_PREDICTION_EVIDENCE_FRAME_SEGMENT_CAPACITY;
    const std::size_t rowIndex = index % REPLAY_PREDICTION_EVIDENCE_FRAME_SEGMENT_CAPACITY;
    return segmentIndex < m_frameSegments.size() && m_frameSegments[segmentIndex]
               ? &m_frameSegments[segmentIndex]->rows[rowIndex]
               : nullptr;
}

const ReplayPredictionSolverEvidenceFrame*
ReplayPredictionSolverEvidenceStore::FindPublishedFrame( const ReplayPredictionEvidenceIdentity& identity ) const noexcept
{
    const std::size_t count = PublishedFrameCount();

    // Why: a same-frame replacement has a new publication identity. Search
    // newest-first so the exact current stamp resolves without allowing a row
    // carrying the old stamp to alias the replacement.
    for ( std::size_t index = count; index > 0u; --index )
    {
        const ReplayPredictionSolverEvidenceFrame* frame = PublishedFrame( index - 1u );

        if ( frame && frame->complete && frame->identity == identity )
        {
            return frame;
        }
    }

    return nullptr;
}

const Physics::PhysicsSolverPersistentContactSample*
ReplayPredictionSolverEvidenceStore::Contact( ReplayPredictionEvidenceRange range, std::size_t offset ) const noexcept
{
    if ( offset >= range.count )
    {
        return nullptr;
    }

    const uint64_t absolute = static_cast<uint64_t>( range.begin ) + static_cast<uint64_t>( offset );
    const uint64_t segmentIndex = absolute / REPLAY_PREDICTION_EVIDENCE_CONTACT_SEGMENT_CAPACITY;
    const uint64_t rowIndex = absolute % REPLAY_PREDICTION_EVIDENCE_CONTACT_SEGMENT_CAPACITY;
    return segmentIndex < m_contactSegments.size() && m_contactSegments[static_cast<std::size_t>( segmentIndex )]
               ? &m_contactSegments[static_cast<std::size_t>( segmentIndex )]->rows[static_cast<std::size_t>( rowIndex )]
               : nullptr;
}

const Physics::PhysicsPipelineRecord* ReplayPredictionSolverEvidenceStore::Pipeline( ReplayPredictionEvidenceRange range,
                                                                                     std::size_t offset ) const noexcept
{
    if ( offset >= range.count )
    {
        return nullptr;
    }

    const uint64_t absolute = static_cast<uint64_t>( range.begin ) + static_cast<uint64_t>( offset );
    const uint64_t segmentIndex = absolute / REPLAY_PREDICTION_EVIDENCE_PIPELINE_SEGMENT_CAPACITY;
    const uint64_t rowIndex = absolute % REPLAY_PREDICTION_EVIDENCE_PIPELINE_SEGMENT_CAPACITY;
    return segmentIndex < m_pipelineSegments.size() && m_pipelineSegments[static_cast<std::size_t>( segmentIndex )]
               ? &m_pipelineSegments[static_cast<std::size_t>( segmentIndex )]->rows[static_cast<std::size_t>( rowIndex )]
               : nullptr;
}

ReplayPredictionSolverEvidenceStoreMemoryStats ReplayPredictionSolverEvidenceStore::CollectMemoryStats() const noexcept
{
    ReplayPredictionSolverEvidenceStoreMemoryStats stats;
    stats.contactCapacityBytes = static_cast<uint64_t>( m_contactSegmentCount ) * sizeof( ContactSegment );
    stats.pipelineCapacityBytes = static_cast<uint64_t>( m_pipelineSegmentCount ) * sizeof( PipelineSegment );
    stats.frameCapacityBytes = static_cast<uint64_t>( m_frameSegmentCount ) * sizeof( FrameSegment );
    stats.currentCapacityBytes = stats.contactCapacityBytes + stats.pipelineCapacityBytes + stats.frameCapacityBytes;
    stats.lifetimePeakCapacityBytes = m_lifetimePeakCapacityBytes;
    stats.contactCount = m_contactCount;
    stats.pipelineCount = m_pipelineCount;
    stats.publishedFrameCount = PublishedFrameCount();
    return stats;
}

void ReplayPredictionSolverEvidenceStore::BeginBank( uint32_t generation, ReplayPredictionDetailMode mode,
                                                     uint64_t bankEpoch ) noexcept
{
    ResetPreservingCapacity();
    m_generation = generation;
    m_mode = mode;
    m_bankEpoch = bankEpoch;
}

bool ReplayPredictionSolverEvidenceStore::Reserve( std::size_t requiredFrameCount, std::size_t requiredContactCount,
                                                   std::size_t requiredPipelineCount, int frameNumber )
{
    if ( m_mode != ReplayPredictionDetailMode::High ||
         requiredFrameCount > MAX_FRAME_SEGMENTS * REPLAY_PREDICTION_EVIDENCE_FRAME_SEGMENT_CAPACITY ||
         requiredContactCount > MAX_CONTACT_SEGMENTS * REPLAY_PREDICTION_EVIDENCE_CONTACT_SEGMENT_CAPACITY ||
         requiredPipelineCount > MAX_PIPELINE_SEGMENTS * REPLAY_PREDICTION_EVIDENCE_PIPELINE_SEGMENT_CAPACITY ||
         requiredContactCount > ( std::numeric_limits<uint32_t>::max )() ||
         requiredPipelineCount > ( std::numeric_limits<uint32_t>::max )() )
    {
        return false;
    }

    const uint64_t requiredFrameBytes = static_cast<uint64_t>( RequiredSegments( requiredFrameCount,
                                                                                 REPLAY_PREDICTION_EVIDENCE_FRAME_SEGMENT_CAPACITY ) ) *
                                        sizeof( FrameSegment );
    const uint64_t requiredContactBytes = static_cast<uint64_t>( RequiredSegments( requiredContactCount,
                                                                                   REPLAY_PREDICTION_EVIDENCE_CONTACT_SEGMENT_CAPACITY ) ) *
                                          sizeof( ContactSegment );
    const uint64_t requiredPipelineBytes = static_cast<uint64_t>( RequiredSegments( requiredPipelineCount,
                                                                                    REPLAY_PREDICTION_EVIDENCE_PIPELINE_SEGMENT_CAPACITY ) ) *
                                           sizeof( PipelineSegment );

    // Why: preflight the complete rounded segment set before allocating its
    // first category. A request that cannot fit the bank cap leaves reusable
    // capacity unchanged instead of partially materializing a doomed shape.
    if ( requiredFrameBytes > REPLAY_PREDICTION_EVIDENCE_BANK_HARD_BYTES ||
         requiredContactBytes > REPLAY_PREDICTION_EVIDENCE_BANK_HARD_BYTES - requiredFrameBytes ||
         requiredPipelineBytes > REPLAY_PREDICTION_EVIDENCE_BANK_HARD_BYTES - requiredFrameBytes - requiredContactBytes )
    {
        return false;
    }

    return EnsureFrameSegments( requiredFrameCount, frameNumber ) &&
           EnsureContactSegments( requiredContactCount, frameNumber ) &&
           EnsurePipelineSegments( requiredPipelineCount, frameNumber );
}

ReplayPredictionEvidenceAppendResult
ReplayPredictionSolverEvidenceStore::AppendFrame( const ReplayPredictionEvidenceIdentity& identity,
                                                  ReplayPredictionContactSpan contacts,
                                                  ReplayPredictionPipelineSpan pipeline, int frameNumber )
{
    if ( identity.generation != m_generation || identity.mode != ReplayPredictionDetailMode::High ||
         identity.mode != m_mode || identity.bankEpoch != m_bankEpoch )
    {
        return ReplayPredictionEvidenceAppendResult::InvalidIdentity;
    }

    std::size_t requiredFrameCount = 0;
    std::size_t requiredContactCount = 0;
    std::size_t requiredPipelineCount = 0;

    if ( !CheckedAdd( m_frameCount, std::size_t { 1u }, requiredFrameCount ) ||
         !CheckedAdd( m_contactCount, contacts.size(), requiredContactCount ) ||
         !CheckedAdd( m_pipelineCount, pipeline.size(), requiredPipelineCount ) ||
         !Reserve( requiredFrameCount, requiredContactCount, requiredPipelineCount, frameNumber ) )
    {
        return ReplayPredictionEvidenceAppendResult::CapacityDenied;
    }

    const uint32_t contactBegin = static_cast<uint32_t>( m_contactCount );
    const uint32_t pipelineBegin = static_cast<uint32_t>( m_pipelineCount );

    for ( std::size_t index = 0; index < contacts.size(); ++index )
    {
        *MutableContact( m_contactCount + index ) = contacts[index];
    }

    for ( std::size_t index = 0; index < pipeline.size(); ++index )
    {
        *MutablePipeline( m_pipelineCount + index ) = pipeline[index];
    }

    ReplayPredictionSolverEvidenceFrame& frame = *MutableFrame( m_frameCount );
    frame.identity = identity;
    frame.contacts = { contactBegin, static_cast<uint32_t>( contacts.size() ) };
    frame.pipeline = { pipelineBegin, static_cast<uint32_t>( pipeline.size() ) };
    frame.complete = true;

    m_contactCount = requiredContactCount;
    m_pipelineCount = requiredPipelineCount;
    m_frameCount = requiredFrameCount;

    // Invariant: the acquire/release prefix is the only publication boundary.
    // Every referenced segment row and the complete flag are final before it.
    m_publishedFrameCount.store( m_frameCount, std::memory_order_release );
    return ReplayPredictionEvidenceAppendResult::Appended;
}

void ReplayPredictionSolverEvidenceStore::ResetPreservingCapacity() noexcept
{
    m_publishedFrameCount.store( 0u, std::memory_order_release );
    m_frameCount = 0u;
    m_contactCount = 0u;
    m_pipelineCount = 0u;
}

void ReplayPredictionSolverEvidenceStore::ReleaseCapacity() noexcept
{
    ResetPreservingCapacity();

    for ( std::unique_ptr<FrameSegment>& segment : m_frameSegments )
    {
        segment.reset();
    }

    for ( std::unique_ptr<ContactSegment>& segment : m_contactSegments )
    {
        segment.reset();
    }

    for ( std::unique_ptr<PipelineSegment>& segment : m_pipelineSegments )
    {
        segment.reset();
    }

    m_frameSegmentCount = 0u;
    m_contactSegmentCount = 0u;
    m_pipelineSegmentCount = 0u;
}

ReplayPredictionSolverEvidenceFrame* ReplayPredictionSolverEvidenceStore::MutableFrame( std::size_t index ) noexcept
{
    return &m_frameSegments[index / REPLAY_PREDICTION_EVIDENCE_FRAME_SEGMENT_CAPACITY]
                ->rows[index % REPLAY_PREDICTION_EVIDENCE_FRAME_SEGMENT_CAPACITY];
}

Physics::PhysicsSolverPersistentContactSample*
ReplayPredictionSolverEvidenceStore::MutableContact( std::size_t index ) noexcept
{
    return &m_contactSegments[index / REPLAY_PREDICTION_EVIDENCE_CONTACT_SEGMENT_CAPACITY]
                ->rows[index % REPLAY_PREDICTION_EVIDENCE_CONTACT_SEGMENT_CAPACITY];
}

Physics::PhysicsPipelineRecord* ReplayPredictionSolverEvidenceStore::MutablePipeline( std::size_t index ) noexcept
{
    return &m_pipelineSegments[index / REPLAY_PREDICTION_EVIDENCE_PIPELINE_SEGMENT_CAPACITY]
                ->rows[index % REPLAY_PREDICTION_EVIDENCE_PIPELINE_SEGMENT_CAPACITY];
}

bool ReplayPredictionSolverEvidenceStore::EnsureFrameSegments( std::size_t requiredCount, int frameNumber )
{
    const std::size_t requiredSegments = RequiredSegments( requiredCount,
                                                           REPLAY_PREDICTION_EVIDENCE_FRAME_SEGMENT_CAPACITY );

    while ( m_frameSegmentCount < requiredSegments )
    {
        if ( !CanGrowBy( sizeof( FrameSegment ) ) )
        {
            return false;
        }

        const uint64_t oldBytes = CollectMemoryStats().currentCapacityBytes;
        const uint64_t requestedBytes = oldBytes + sizeof( FrameSegment );
        Core::Allocation::RuntimeReserveGrowthResult result = {};

        if ( !RequestReplayPredictionReserveGrowth( "ReplayPredictionSolverEvidenceStore::frames", frameNumber,
                                                    static_cast<int>( oldBytes ), static_cast<int>( requestedBytes ), 1,
                                                    result ) )
        {
            return false;
        }

        const Core::Allocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
        Core::Allocation::RuntimeAllocationScope allocationScope( Core::Allocation::RuntimeAllocationPhase::Replay );
        Core::Allocation::RuntimeReserveOwnerScope ownerScope( owner );
        Core::Allocation::RuntimeReserveGrowthScope growthScope( owner, Core::Allocation::RuntimeReservePhase::Replay,
                                                                 result );
        m_frameSegments[m_frameSegmentCount] = std::make_unique<FrameSegment>();
        ++m_frameSegmentCount;
        RefreshLifetimePeak();
    }

    return true;
}

bool ReplayPredictionSolverEvidenceStore::EnsureContactSegments( std::size_t requiredCount, int frameNumber )
{
    const std::size_t requiredSegments = RequiredSegments( requiredCount,
                                                           REPLAY_PREDICTION_EVIDENCE_CONTACT_SEGMENT_CAPACITY );

    while ( m_contactSegmentCount < requiredSegments )
    {
        if ( !CanGrowBy( sizeof( ContactSegment ) ) )
        {
            return false;
        }

        const uint64_t oldBytes = CollectMemoryStats().currentCapacityBytes;
        const uint64_t requestedBytes = oldBytes + sizeof( ContactSegment );
        Core::Allocation::RuntimeReserveGrowthResult result = {};

        if ( !RequestReplayPredictionReserveGrowth( "ReplayPredictionSolverEvidenceStore::contacts", frameNumber,
                                                    static_cast<int>( oldBytes ), static_cast<int>( requestedBytes ), 1,
                                                    result ) )
        {
            return false;
        }

        const Core::Allocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
        Core::Allocation::RuntimeAllocationScope allocationScope( Core::Allocation::RuntimeAllocationPhase::Replay );
        Core::Allocation::RuntimeReserveOwnerScope ownerScope( owner );
        Core::Allocation::RuntimeReserveGrowthScope growthScope( owner, Core::Allocation::RuntimeReservePhase::Replay,
                                                                 result );
        m_contactSegments[m_contactSegmentCount] = std::make_unique<ContactSegment>();
        ++m_contactSegmentCount;
        RefreshLifetimePeak();
    }

    return true;
}

bool ReplayPredictionSolverEvidenceStore::EnsurePipelineSegments( std::size_t requiredCount, int frameNumber )
{
    const std::size_t requiredSegments = RequiredSegments( requiredCount,
                                                           REPLAY_PREDICTION_EVIDENCE_PIPELINE_SEGMENT_CAPACITY );

    while ( m_pipelineSegmentCount < requiredSegments )
    {
        if ( !CanGrowBy( sizeof( PipelineSegment ) ) )
        {
            return false;
        }

        const uint64_t oldBytes = CollectMemoryStats().currentCapacityBytes;
        const uint64_t requestedBytes = oldBytes + sizeof( PipelineSegment );
        Core::Allocation::RuntimeReserveGrowthResult result = {};

        if ( !RequestReplayPredictionReserveGrowth( "ReplayPredictionSolverEvidenceStore::pipeline", frameNumber,
                                                    static_cast<int>( oldBytes ), static_cast<int>( requestedBytes ), 1,
                                                    result ) )
        {
            return false;
        }

        const Core::Allocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
        Core::Allocation::RuntimeAllocationScope allocationScope( Core::Allocation::RuntimeAllocationPhase::Replay );
        Core::Allocation::RuntimeReserveOwnerScope ownerScope( owner );
        Core::Allocation::RuntimeReserveGrowthScope growthScope( owner, Core::Allocation::RuntimeReservePhase::Replay,
                                                                 result );
        m_pipelineSegments[m_pipelineSegmentCount] = std::make_unique<PipelineSegment>();
        ++m_pipelineSegmentCount;
        RefreshLifetimePeak();
    }

    return true;
}

bool ReplayPredictionSolverEvidenceStore::CanGrowBy( uint64_t bytes ) const noexcept
{
    const uint64_t current = CollectMemoryStats().currentCapacityBytes;
    return current <= REPLAY_PREDICTION_EVIDENCE_BANK_HARD_BYTES &&
           bytes <= REPLAY_PREDICTION_EVIDENCE_BANK_HARD_BYTES - current;
}

void ReplayPredictionSolverEvidenceStore::RefreshLifetimePeak() noexcept
{
    m_lifetimePeakCapacityBytes = (std::max)( m_lifetimePeakCapacityBytes, CollectMemoryStats().currentCapacityBytes );
}

uint64_t ReplayPredictionSolverEvidenceBanks::BeginBuild( uint32_t generation, ReplayPredictionDetailMode mode ) noexcept
{
    const uint64_t epoch = m_nextEpoch++;
    m_banks[m_buildIndex].BeginBank( generation, mode, epoch );
    return epoch;
}

bool ReplayPredictionSolverEvidenceBanks::ReserveBuild( std::size_t requiredFrameCount, std::size_t requiredContactCount,
                                                        std::size_t requiredPipelineCount, int frameNumber )
{
    const bool reserved = m_banks[m_buildIndex].Reserve( requiredFrameCount, requiredContactCount, requiredPipelineCount,
                                                         frameNumber );
    RefreshLifetimePeak();
    return reserved;
}

bool ReplayPredictionSolverEvidenceBanks::AppendBuildFrame( ReplayFrameIndex frame, uint32_t topologyVersion,
                                                            uint64_t publicationVersion,
                                                            ReplayPredictionContactSpan contacts,
                                                            ReplayPredictionPipelineSpan pipeline, int frameNumber )
{
    return AppendBuildFrameResult( frame, topologyVersion, publicationVersion, contacts, pipeline, frameNumber ) ==
           ReplayPredictionEvidenceAppendResult::Appended;
}

ReplayPredictionEvidenceAppendResult ReplayPredictionSolverEvidenceBanks::AppendBuildFrameResult( ReplayFrameIndex frame, uint32_t topologyVersion, uint64_t publicationVersion, ReplayPredictionContactSpan contacts,
                                                                                                  ReplayPredictionPipelineSpan pipeline, int frameNumber )
{
    ReplayPredictionSolverEvidenceStore& build = m_banks[m_buildIndex];
    const ReplayPredictionEvidenceIdentity identity = { build.Generation(), build.Mode(),      build.BankEpoch(), frame,
                                                        topologyVersion,    publicationVersion };
    const ReplayPredictionEvidenceAppendResult appended = build.AppendFrame( identity, contacts, pipeline, frameNumber );
    RefreshLifetimePeak();
    return appended;
}

bool ReplayPredictionSolverEvidenceBanks::PromoteBuild() noexcept
{
    if ( m_banks[m_buildIndex].PublishedFrameCount() == 0u )
    {
        return false;
    }

    const uint8_t oldCommitted = m_committedIndex.load( std::memory_order_acquire );
    m_committedIndex.store( m_buildIndex, std::memory_order_release );
    m_buildIndex = oldCommitted;
    RefreshLifetimePeak();
    return true;
}

void ReplayPredictionSolverEvidenceBanks::CancelBuild() noexcept
{
    m_banks[m_buildIndex].ResetPreservingCapacity();
}

void ReplayPredictionSolverEvidenceBanks::ReleaseCapacity() noexcept
{
    m_lastReleaseBeforeCapacityBytes = CollectMemoryStats().currentCapacityBytes;
    m_banks[0].ReleaseCapacity();
    m_banks[1].ReleaseCapacity();
    m_lastReleaseAfterCapacityBytes = m_banks[0].CollectMemoryStats().currentCapacityBytes +
                                      m_banks[1].CollectMemoryStats().currentCapacityBytes;
    ++m_releaseCheckpointCount;
}

const ReplayPredictionSolverEvidenceStore& ReplayPredictionSolverEvidenceBanks::Build() const noexcept
{
    return m_banks[m_buildIndex];
}

const ReplayPredictionSolverEvidenceStore& ReplayPredictionSolverEvidenceBanks::Committed() const noexcept
{
    return m_banks[m_committedIndex.load( std::memory_order_acquire )];
}

ReplayPredictionSolverEvidenceBanksMemoryStats ReplayPredictionSolverEvidenceBanks::CollectMemoryStats() const noexcept
{
    ReplayPredictionSolverEvidenceBanksMemoryStats stats;
    stats.build = Build().CollectMemoryStats();
    stats.committed = Committed().CollectMemoryStats();
    stats.currentContactCapacityBytes = stats.build.contactCapacityBytes + stats.committed.contactCapacityBytes;
    stats.currentPipelineCapacityBytes = stats.build.pipelineCapacityBytes + stats.committed.pipelineCapacityBytes;
    stats.currentFrameCapacityBytes = stats.build.frameCapacityBytes + stats.committed.frameCapacityBytes;
    stats.currentCapacityBytes = stats.build.currentCapacityBytes + stats.committed.currentCapacityBytes;
    stats.lifetimePeakCapacityBytes = (std::max)( m_lifetimePeakCapacityBytes, stats.currentCapacityBytes );
    stats.releaseCheckpointCount = m_releaseCheckpointCount;
    stats.lastReleaseBeforeCapacityBytes = m_lastReleaseBeforeCapacityBytes;
    stats.lastReleaseAfterCapacityBytes = m_lastReleaseAfterCapacityBytes;
    return stats;
}

void ReplayPredictionSolverEvidenceBanks::SwapArchiveState( ReplayPredictionSolverEvidenceBanks& other ) noexcept
{
    const uint64_t priorLifetimePeak = m_lifetimePeakCapacityBytes;
    const auto swapStore = []( ReplayPredictionSolverEvidenceStore& destination,
                              ReplayPredictionSolverEvidenceStore& candidate ) noexcept
    {
        const uint64_t combinedLifetimePeak = (std::max)( destination.m_lifetimePeakCapacityBytes,
                                                          candidate.m_lifetimePeakCapacityBytes );
        using std::swap;
        swap( destination.m_frameSegments, candidate.m_frameSegments );
        swap( destination.m_contactSegments, candidate.m_contactSegments );
        swap( destination.m_pipelineSegments, candidate.m_pipelineSegments );

        const std::size_t published = destination.m_publishedFrameCount.exchange( candidate.m_publishedFrameCount.load( std::memory_order_acquire ),
                                                                                  std::memory_order_acq_rel );
        candidate.m_publishedFrameCount.store( published, std::memory_order_release );
        swap( destination.m_frameCount, candidate.m_frameCount );
        swap( destination.m_contactCount, candidate.m_contactCount );
        swap( destination.m_pipelineCount, candidate.m_pipelineCount );
        swap( destination.m_frameSegmentCount, candidate.m_frameSegmentCount );
        swap( destination.m_contactSegmentCount, candidate.m_contactSegmentCount );
        swap( destination.m_pipelineSegmentCount, candidate.m_pipelineSegmentCount );
        swap( destination.m_generation, candidate.m_generation );
        swap( destination.m_mode, candidate.m_mode );
        swap( destination.m_bankEpoch, candidate.m_bankEpoch );
        destination.m_lifetimePeakCapacityBytes = (std::max)( combinedLifetimePeak,
                                                              destination.CollectMemoryStats().currentCapacityBytes );
        candidate.m_lifetimePeakCapacityBytes = (std::max)( combinedLifetimePeak,
                                                            candidate.CollectMemoryStats().currentCapacityBytes );
    };
    swapStore( m_banks[0], other.m_banks[0] );
    swapStore( m_banks[1], other.m_banks[1] );

    const uint8_t committed = m_committedIndex.exchange( other.m_committedIndex.load( std::memory_order_acquire ),
                                                         std::memory_order_acq_rel );
    other.m_committedIndex.store( committed, std::memory_order_release );
    using std::swap;
    swap( m_buildIndex, other.m_buildIndex );

    ReplayPredictionSolverEvidenceStore& committedStore = m_banks[m_committedIndex.load( std::memory_order_acquire )];

    if ( committedStore.PublishedFrameCount() > 0u )
    {
        // Invariant: candidate banks begin their epoch sequence independently.
        // Rebase every published identity to the destination owner's next
        // epoch so a pre-load row cannot resolve replacement storage by reuse.
        const uint64_t archiveEpoch = m_nextEpoch++;
        committedStore.m_bankEpoch = archiveEpoch;

        for ( std::size_t index = 0; index < committedStore.PublishedFrameCount(); ++index )
        {
            ReplayPredictionSolverEvidenceFrame* frame = committedStore.MutableFrame( index );

            if ( frame )
            {
                frame->identity.bankEpoch = archiveEpoch;
            }
        }
    }

    // Why: replacement changes current capacity, but it is not a release
    // checkpoint and must not erase this owner's historical accounting.
    m_lifetimePeakCapacityBytes = (std::max)( priorLifetimePeak, CollectMemoryStats().currentCapacityBytes );
}

void ReplayPredictionSolverEvidenceBanks::RefreshLifetimePeak() noexcept
{
    const uint64_t current = Build().CollectMemoryStats().currentCapacityBytes +
                             Committed().CollectMemoryStats().currentCapacityBytes;
    m_lifetimePeakCapacityBytes = (std::max)( m_lifetimePeakCapacityBytes, current );
}
} // namespace SkullbonezCore::Runtime
