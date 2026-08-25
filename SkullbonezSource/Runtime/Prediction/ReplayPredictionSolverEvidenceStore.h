/*
Purpose:
  Owns paired immutable build/committed stores for exact predicted solver evidence.

Invariants:
  - Low detail cannot reserve or append evidence storage.
  - Published rows and frame records never relocate or mutate.
  - Promotion publishes the new committed bank before the old bank may be reset.
  - ReleaseCapacity frees both banks while lifetime peaks remain historical.
  - Every release records one before/after checkpoint after worker join and
    before the caller can start a new build.
  - Every allocation uses the existing replay_prediction_working_set owner.
  - Archive commit swaps complete banks and never publishes a partially decoded prefix.
  - Frame views are valid only while their bank remains the selected published
    source; callers copy any retained result before promotion or replacement.
  - Capacity denial is distinct from invalid identity so callers may truncate
    optional evidence without masking publication-contract defects.
*/
#pragma once

#include "ReplayPredictionPackets.h"
#include "ReplayPredictionView.h"
#include "../Replay/ReplayIdentity.h"
#include "../../Physics/PhysicsDebugData.h"
#include "../../Physics/PhysicsSolverSnapshot.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace SkullbonezCore::Runtime
{
inline constexpr std::size_t REPLAY_PREDICTION_EVIDENCE_FRAME_SEGMENT_CAPACITY = 128u;
inline constexpr std::size_t REPLAY_PREDICTION_EVIDENCE_CONTACT_SEGMENT_CAPACITY = 256u;
inline constexpr std::size_t REPLAY_PREDICTION_EVIDENCE_PIPELINE_SEGMENT_CAPACITY = 1024u;
inline constexpr uint64_t REPLAY_PREDICTION_EVIDENCE_BANK_HARD_BYTES = 320ull * 1024ull * 1024ull;

using ReplayPredictionContactSpan = std::span<const Physics::PhysicsSolverPersistentContactSample>;
using ReplayPredictionPipelineSpan = std::span<const Physics::PhysicsPipelineRecord>;

enum class ReplayPredictionEvidenceAppendResult : uint8_t
{
    Appended,
    CapacityDenied,
    InvalidIdentity
};

struct ReplayPredictionEvidenceRange
{
    uint32_t begin = 0;
    uint32_t count = 0;
};

struct ReplayPredictionSolverEvidenceFrame
{
    ReplayPredictionEvidenceIdentity identity;
    ReplayPredictionEvidenceRange contacts;
    ReplayPredictionEvidenceRange pipeline;
    bool complete = false;
};

struct ReplayPredictionSolverEvidenceStoreMemoryStats
{
    uint64_t contactCapacityBytes = 0;
    uint64_t pipelineCapacityBytes = 0;
    uint64_t frameCapacityBytes = 0;
    uint64_t currentCapacityBytes = 0;
    uint64_t lifetimePeakCapacityBytes = 0;
    std::size_t contactCount = 0;
    std::size_t pipelineCount = 0;
    std::size_t publishedFrameCount = 0;
};

struct ReplayPredictionSolverEvidenceBanksMemoryStats
{
    ReplayPredictionSolverEvidenceStoreMemoryStats build;
    ReplayPredictionSolverEvidenceStoreMemoryStats committed;
    uint64_t currentContactCapacityBytes = 0;
    uint64_t currentPipelineCapacityBytes = 0;
    uint64_t currentFrameCapacityBytes = 0;
    uint64_t currentCapacityBytes = 0;
    uint64_t lifetimePeakCapacityBytes = 0;
    uint64_t releaseCheckpointCount = 0;
    uint64_t lastReleaseBeforeCapacityBytes = 0;
    uint64_t lastReleaseAfterCapacityBytes = 0;
};

class ReplayPredictionSolverEvidenceStore
{
  public:
    ReplayPredictionSolverEvidenceStore();
    ~ReplayPredictionSolverEvidenceStore();

    ReplayPredictionSolverEvidenceStore( const ReplayPredictionSolverEvidenceStore& ) = delete;
    ReplayPredictionSolverEvidenceStore& operator=( const ReplayPredictionSolverEvidenceStore& ) = delete;

    uint32_t Generation() const noexcept;
    ReplayPredictionDetailMode Mode() const noexcept;
    uint64_t BankEpoch() const noexcept;
    std::size_t PublishedFrameCount() const noexcept;

    const ReplayPredictionSolverEvidenceFrame* PublishedFrame( std::size_t index ) const noexcept;
    const ReplayPredictionSolverEvidenceFrame*
    FindPublishedFrame( const ReplayPredictionEvidenceIdentity& identity ) const noexcept;
    const Physics::PhysicsSolverPersistentContactSample* Contact( ReplayPredictionEvidenceRange range,
                                                                  std::size_t offset ) const noexcept;
    const Physics::PhysicsPipelineRecord* Pipeline( ReplayPredictionEvidenceRange range, std::size_t offset ) const noexcept;
    ReplayPredictionSolverEvidenceStoreMemoryStats CollectMemoryStats() const noexcept;

  private:
    friend class ReplayPredictionSolverEvidenceBanks;

    static constexpr std::size_t MAX_FRAME_SEGMENTS = 256u;
    static constexpr std::size_t MAX_CONTACT_SEGMENTS = 9216u;
    static constexpr std::size_t MAX_PIPELINE_SEGMENTS = 5888u;

    struct FrameSegment;
    struct ContactSegment;
    struct PipelineSegment;

    void BeginBank( uint32_t generation, ReplayPredictionDetailMode mode, uint64_t bankEpoch ) noexcept;
    bool Reserve( std::size_t requiredFrameCount, std::size_t requiredContactCount, std::size_t requiredPipelineCount,
                  int frameNumber );
    ReplayPredictionEvidenceAppendResult AppendFrame( const ReplayPredictionEvidenceIdentity& identity,
                                                      ReplayPredictionContactSpan contacts,
                                                      ReplayPredictionPipelineSpan pipeline, int frameNumber );
    void ResetPreservingCapacity() noexcept;
    void ReleaseCapacity() noexcept;

    ReplayPredictionSolverEvidenceFrame* MutableFrame( std::size_t index ) noexcept;
    Physics::PhysicsSolverPersistentContactSample* MutableContact( std::size_t index ) noexcept;
    Physics::PhysicsPipelineRecord* MutablePipeline( std::size_t index ) noexcept;
    bool EnsureFrameSegments( std::size_t requiredCount, int frameNumber );
    bool EnsureContactSegments( std::size_t requiredCount, int frameNumber );
    bool EnsurePipelineSegments( std::size_t requiredCount, int frameNumber );
    bool CanGrowBy( uint64_t bytes ) const noexcept;
    void RefreshLifetimePeak() noexcept;

    std::array<std::unique_ptr<FrameSegment>, MAX_FRAME_SEGMENTS> m_frameSegments;
    std::array<std::unique_ptr<ContactSegment>, MAX_CONTACT_SEGMENTS> m_contactSegments;
    std::array<std::unique_ptr<PipelineSegment>, MAX_PIPELINE_SEGMENTS> m_pipelineSegments;
    std::atomic<std::size_t> m_publishedFrameCount { 0u };
    std::size_t m_frameCount = 0;
    std::size_t m_contactCount = 0;
    std::size_t m_pipelineCount = 0;
    std::size_t m_frameSegmentCount = 0;
    std::size_t m_contactSegmentCount = 0;
    std::size_t m_pipelineSegmentCount = 0;
    uint32_t m_generation = 0;
    ReplayPredictionDetailMode m_mode = ReplayPredictionDetailMode::High;
    uint64_t m_bankEpoch = 0;
    uint64_t m_lifetimePeakCapacityBytes = 0;
};

// Lifetime: synchronous read-only borrow over one sealed frame in a segmented bank.
// The bank owner controls lifetime; consumers retain neither pointer beyond
// their command nor assumptions about physical segment layout.
struct ReplayPredictionSolverEvidenceFrameView
{
    const ReplayPredictionSolverEvidenceStore* store = nullptr;
    const ReplayPredictionSolverEvidenceFrame* frame = nullptr;

    bool Valid() const noexcept
    {
        return store && frame && frame->complete && frame->identity.mode == ReplayPredictionDetailMode::High;
    }
    std::size_t ContactCount() const noexcept
    {
        return Valid() ? frame->contacts.count : 0u;
    }
    std::size_t PipelineCount() const noexcept
    {
        return Valid() ? frame->pipeline.count : 0u;
    }
    const Physics::PhysicsSolverPersistentContactSample* Contact( std::size_t index ) const noexcept
    {
        return Valid() ? store->Contact( frame->contacts, index ) : nullptr;
    }
    const Physics::PhysicsPipelineRecord* Pipeline( std::size_t index ) const noexcept
    {
        return Valid() ? store->Pipeline( frame->pipeline, index ) : nullptr;
    }
};

class ReplayPredictionSolverEvidenceBanks
{
  public:
    uint64_t BeginBuild( uint32_t generation, ReplayPredictionDetailMode mode ) noexcept;
    bool ReserveBuild( std::size_t requiredFrameCount, std::size_t requiredContactCount, std::size_t requiredPipelineCount,
                       int frameNumber );
    bool AppendBuildFrame( ReplayFrameIndex frame, uint32_t topologyVersion, uint64_t publicationVersion,
                           ReplayPredictionContactSpan contacts, ReplayPredictionPipelineSpan pipeline, int frameNumber );
    ReplayPredictionEvidenceAppendResult AppendBuildFrameResult( ReplayFrameIndex frame, uint32_t topologyVersion,
                                                                 uint64_t publicationVersion,
                                                                 ReplayPredictionContactSpan contacts,
                                                                 ReplayPredictionPipelineSpan pipeline, int frameNumber );
    bool PromoteBuild() noexcept;
    void CancelBuild() noexcept;
    void ReleaseCapacity() noexcept;

    const ReplayPredictionSolverEvidenceStore& Build() const noexcept;
    const ReplayPredictionSolverEvidenceStore& Committed() const noexcept;
    ReplayPredictionSolverEvidenceBanksMemoryStats CollectMemoryStats() const noexcept;
    void SwapArchiveState( ReplayPredictionSolverEvidenceBanks& other ) noexcept;

  private:
    void RefreshLifetimePeak() noexcept;

    std::array<ReplayPredictionSolverEvidenceStore, 2> m_banks;
    std::atomic<uint8_t> m_committedIndex { 0u };
    uint8_t m_buildIndex = 1u;
    uint64_t m_nextEpoch = 1u;
    uint64_t m_lifetimePeakCapacityBytes = 0;
    uint64_t m_releaseCheckpointCount = 0;
    uint64_t m_lastReleaseBeforeCapacityBytes = 0;
    uint64_t m_lastReleaseAfterCapacityBytes = 0;
};
} // namespace SkullbonezCore::Runtime
