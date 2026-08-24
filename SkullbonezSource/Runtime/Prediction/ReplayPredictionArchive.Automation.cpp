/*
File: SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.Automation.cpp
Purpose:
  Verifies the RVPD prediction archive by restoring and rebuilding its bytes in
  the Automation configuration.

Summary:
  The sole replay-fidelity process captures one completed prediction. This
  non-presenting verifier restores that payload into temporary domain values
  and demands exact re-serialization before the process exits. It also proves
  High-to-Low projection, legacy migration, future rejection, strict section
  layout, and failure-atomic preservation of an already restored archive.

Glossary:
  RVPD (Replay Visual Prediction Data): Bounded little-endian payload containing
    the completed prediction values required by durable visual replay.
  Round trip: Decode followed by encode, with exact byte equality required.

Invariants:
  - This translation unit is linked only by the Automation engine configuration.
  - Verification never schedules prediction work or presents a second path.
  - Legacy verification rebuilds canonical bytes after migration.
  - Every rejected mutation leaves the prior canonical bytes and evidence
    capacity/counts unchanged.
  - Failure strings are frozen probe-output schema.

Related:
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.cpp
  - tools/validate_replay_visual_fidelity.bat
  - Agentic/Reference/engine-glossary.md
*/
#include "ReplayPredictionArchive.h"
#include "ReplayPrediction.h"
#include "ReplayPredictionPublicationOperations.h"

#include "../Replay/ReplayPathPackets.h"

#include "../../Core/Allocation/RuntimeAllocationTracker.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace CoreAllocation = SkullbonezCore::Core::Allocation;

namespace SkullbonezCore::Runtime::ReplayPredictionArchiveOperations
{
namespace
{
constexpr std::size_t ARCHIVE_SCHEMA_OFFSET = 4u;
constexpr std::size_t ARCHIVE_CAPABILITY_OFFSET = 8u;
constexpr std::size_t ARCHIVE_PATH_POLICY_OFFSET = 9u;
constexpr std::size_t ARCHIVE_SECTION_COUNT_OFFSET = 12u;
constexpr std::size_t ARCHIVE_TOTAL_BYTES_OFFSET = 16u;
constexpr std::size_t ARCHIVE_FIRST_DESCRIPTOR_OFFSET = 24u;
constexpr std::size_t ARCHIVE_DESCRIPTOR_BYTES = 24u;
constexpr std::size_t ARCHIVE_FIRST_SECTION_OFFSET_FIELD = 32u;
constexpr std::size_t ARCHIVE_SECOND_SECTION_OFFSET_FIELD = 56u;
constexpr std::size_t ARCHIVE_SECOND_SECTION_SIZE_FIELD = 64u;

void WriteAutomationReason( char* destination, std::size_t size, const char* message )
{
    if ( destination && size > 0 )
    {
        std::snprintf( destination, size, "%s", message ? message : "prediction archive failure" );
    }
}

uint64_t ReadLittleEndianU64( std::span<const uint8_t> bytes, std::size_t offset )
{
    uint64_t value = 0;

    for ( std::size_t byte = 0; byte < sizeof( value ); ++byte )
    {
        value |= static_cast<uint64_t>( bytes[offset + byte] ) << ( byte * 8u );
    }

    return value;
}

void WriteLittleEndianU32( std::vector<uint8_t>& bytes, std::size_t offset, uint32_t value )
{
    for ( std::size_t byte = 0; byte < sizeof( value ); ++byte )
    {
        bytes[offset + byte] = static_cast<uint8_t>( value >> ( byte * 8u ) );
    }
}

void WriteLittleEndianU64( std::vector<uint8_t>& bytes, std::size_t offset, uint64_t value )
{
    for ( std::size_t byte = 0; byte < sizeof( value ); ++byte )
    {
        bytes[offset + byte] = static_cast<uint8_t>( value >> ( byte * 8u ) );
    }
}

bool EvidenceStoreStatsEqual( const ReplayPredictionSolverEvidenceStoreMemoryStats& left,
                              const ReplayPredictionSolverEvidenceStoreMemoryStats& right )
{
    return left.contactCapacityBytes == right.contactCapacityBytes &&
           left.pipelineCapacityBytes == right.pipelineCapacityBytes &&
           left.frameCapacityBytes == right.frameCapacityBytes && left.currentCapacityBytes == right.currentCapacityBytes &&
           left.lifetimePeakCapacityBytes == right.lifetimePeakCapacityBytes && left.contactCount == right.contactCount &&
           left.pipelineCount == right.pipelineCount && left.publishedFrameCount == right.publishedFrameCount;
}

bool EvidenceBankStatsEqual( const ReplayPredictionSolverEvidenceBanksMemoryStats& left,
                             const ReplayPredictionSolverEvidenceBanksMemoryStats& right )
{
    return EvidenceStoreStatsEqual( left.build, right.build ) &&
           EvidenceStoreStatsEqual( left.committed, right.committed ) &&
           left.currentContactCapacityBytes == right.currentContactCapacityBytes &&
           left.currentPipelineCapacityBytes == right.currentPipelineCapacityBytes &&
           left.currentFrameCapacityBytes == right.currentFrameCapacityBytes &&
           left.currentCapacityBytes == right.currentCapacityBytes &&
           left.lifetimePeakCapacityBytes == right.lifetimePeakCapacityBytes &&
           left.releaseCheckpointCount == right.releaseCheckpointCount &&
           left.lastReleaseBeforeCapacityBytes == right.lastReleaseBeforeCapacityBytes &&
           left.lastReleaseAfterCapacityBytes == right.lastReleaseAfterCapacityBytes;
}

bool VerifyEvidenceReadableWithoutPhysics( const RunReplayPredictionState& prediction,
                                           const ReplayPredictionSolverEvidenceStore& evidence )
{
    if ( prediction.simulation.predictionEngine || evidence.PublishedFrameCount() == 0u )
    {
        return false;
    }

    bool sawContact = false;
    bool sawPipeline = false;

    for ( std::size_t index = 0; index < evidence.PublishedFrameCount(); ++index )
    {
        const ReplayPredictionSolverEvidenceFrame* frame = evidence.PublishedFrame( index );

        if ( !frame || !frame->complete || frame->identity.generation != prediction.build.generationBeginCount ||
             frame->identity.mode != ReplayPredictionDetailMode::High || frame->identity.bankEpoch != evidence.BankEpoch() )
        {
            return false;
        }

        if ( frame->contacts.count > 0u )
        {
            if ( !evidence.Contact( frame->contacts, frame->contacts.count - 1u ) )
            {
                return false;
            }

            sawContact = true;
        }

        if ( frame->pipeline.count > 0u )
        {
            if ( !evidence.Pipeline( frame->pipeline, frame->pipeline.count - 1u ) )
            {
                return false;
            }

            sawPipeline = true;
        }
    }

    return sawContact && sawPipeline;
}

bool VerifyRejectedArchivePreservesState( std::span<const uint8_t> rejectedBytes, const std::vector<uint8_t>& canonicalBytes,
                                          RunReplayPathVisualizerState& pathVisualizer, RunReplayPredictionState& prediction,
                                          ReplayPredictionSolverEvidenceBanks& evidence,
                                          ReplayPredictionArchiveDetailCapability preservedCapability,
                                          const char* expectedReason, char* outReason, std::size_t reasonSize )
{
    const ReplayPredictionSolverEvidenceBanksMemoryStats beforeStats = evidence.CollectMemoryStats();
    ReplayPredictionArchiveDetailCapability rejectedCapability = preservedCapability;
    char rejectionReason[128] = {};

    if ( LoadReplayPredictionArchive( rejectedBytes, pathVisualizer, prediction, evidence, ReplayPredictionDetailMode::High,
                                      rejectedCapability, rejectionReason, sizeof( rejectionReason ) ) )
    {
        WriteAutomationReason( outReason, reasonSize, "malformed prediction archive was accepted" );
        return false;
    }

    if ( expectedReason && std::strcmp( rejectionReason, expectedReason ) != 0 )
    {
        WriteAutomationReason( outReason, reasonSize, "prediction archive rejection reason diverged" );
        return false;
    }

    std::vector<uint8_t> rebuiltBytes;

    // Invariant: a failed transaction cannot change even reserve/high-water
    // observations. Comparing canonical bytes covers every durable lightweight
    // value; the explicit bank comparison covers the non-serialized capacity
    // and publication state that a partial evidence decode could otherwise leak.
    if ( rejectedCapability != preservedCapability ||
         !BuildReplayPredictionArchive( pathVisualizer, prediction, ReplayPredictionDetailMode::High, evidence.Committed(),
                                        rebuiltBytes ) ||
         rebuiltBytes != canonicalBytes || !EvidenceBankStatsEqual( beforeStats, evidence.CollectMemoryStats() ) )
    {
        WriteAutomationReason( outReason, reasonSize, "rejected prediction archive changed prior state" );
        return false;
    }

    return true;
}
} // namespace

bool VerifyReplayPredictionArchiveRoundTrip( std::span<const uint8_t> bytes, char* outReason, std::size_t reasonSize )
{
    // Test probe: this is bounded validation work performed after the sole live
    // capture. Temporary vectors are diagnostics artifacts, never steady-state
    // replay storage or a second presentation path.
    // Hazard: RunReplayPredictionState measures 792,936 bytes. Three of them as
    // locals asked for about 2.3 MB of frame, so __chkstk faulted while probing
    // down through the roughly 840 KB of stack still available at this call
    // depth, before the function's first statement ran.
    // Runtime allocation policy: owner replay_prediction_archive_verification;
    // phase Diagnostics; this translation unit links only in Automation, runs
    // once after the sole live capture, and never touches steady gameplay,
    // presentation, or a second prediction path. RunReplayPathVisualizerState is
    // only 160 bytes and stays on the stack.
    CoreAllocation::RuntimeAllocationScope verificationAllocationScope( CoreAllocation::RuntimeAllocationPhase::Diagnostics );

    RunReplayPathVisualizerState restoredPathVisualizer;
    const auto restoredPredictionStorage = std::make_unique<RunReplayPredictionState>();
    RunReplayPredictionState& restoredPrediction = *restoredPredictionStorage;
    const auto restoredEvidenceStorage = std::make_unique<ReplayPredictionSolverEvidenceBanks>();
    ReplayPredictionArchiveDetailCapability restoredCapability = ReplayPredictionArchiveDetailCapability::Low;

    if ( !LoadReplayPredictionArchive( bytes, restoredPathVisualizer, restoredPrediction, *restoredEvidenceStorage,
                                       ReplayPredictionDetailMode::High, restoredCapability, outReason, reasonSize ) )
    {
        return false;
    }

    std::vector<uint8_t> rebuiltBytes;

    if ( !BuildReplayPredictionArchive( restoredPathVisualizer, restoredPrediction, ReplayPredictionDetailMode::High,
                                        restoredEvidenceStorage->Committed(), rebuiltBytes ) )
    {
        WriteAutomationReason( outReason, reasonSize, "could not rebuild prediction archive" );
        return false;
    }

    if ( rebuiltBytes.size() != bytes.size() ||
         ( !bytes.empty() && std::memcmp( rebuiltBytes.data(), bytes.data(), bytes.size() ) != 0 ) )
    {
        WriteAutomationReason( outReason, reasonSize, "prediction archive round-trip bytes diverged" );
        return false;
    }

    ReplayTrajectoryRecord* prefixProbeSource = nullptr;

    for ( const ReplayTrajectoryRecord& record : restoredPrediction.trajectoryStore.ActiveRecords() )
    {
        if ( record.points.size() > 1u )
        {
            prefixProbeSource = restoredPrediction.trajectoryStore.FindRecord( record.key );
            break;
        }
    }

    if ( prefixProbeSource )
    {
        const std::size_t capturedPublishedPointCount = prefixProbeSource->publishedPointCount;
        prefixProbeSource->publishedPointCount = prefixProbeSource->points.size() - 1u;
        std::vector<uint8_t> prefixBytes;
        const bool prefixBuilt = BuildReplayPredictionArchive( restoredPathVisualizer, restoredPrediction,
                                                               ReplayPredictionDetailMode::High,
                                                               restoredEvidenceStorage->Committed(), prefixBytes );
        prefixProbeSource->publishedPointCount = capturedPublishedPointCount;

        RunReplayPathVisualizerState prefixPathVisualizer;
        const auto prefixPredictionStorage = std::make_unique<RunReplayPredictionState>();
        RunReplayPredictionState& prefixPrediction = *prefixPredictionStorage;
        const auto prefixEvidenceStorage = std::make_unique<ReplayPredictionSolverEvidenceBanks>();
        ReplayPredictionArchiveDetailCapability prefixCapability = ReplayPredictionArchiveDetailCapability::Low;

        if ( !prefixBuilt ||
             !LoadReplayPredictionArchive( prefixBytes, prefixPathVisualizer, prefixPrediction, *prefixEvidenceStorage,
                                           ReplayPredictionDetailMode::High, prefixCapability, outReason, reasonSize ) )
        {
            WriteAutomationReason( outReason, reasonSize, "published trajectory prefix archive probe failed" );
            return false;
        }

        std::vector<uint8_t> prefixRoundTripBytes;

        if ( !BuildReplayPredictionArchive( prefixPathVisualizer, prefixPrediction, ReplayPredictionDetailMode::High,
                                            prefixEvidenceStorage->Committed(), prefixRoundTripBytes ) ||
             prefixRoundTripBytes != prefixBytes )
        {
            WriteAutomationReason( outReason, reasonSize, "published trajectory prefix changed during archive round-trip" );
            return false;
        }

        ReplayPredictionPublicationOperations::UpdateReplayPredictionTrajectoryStore( prefixPrediction,
                                                                                      prefixPrediction.simulation.frames,
                                                                                      prefixPrediction.CommittedFrameCount(),
                                                                                      prefixPrediction.trajectoryBuild
                                                                                          .usingBuildFrames,
                                                                                      prefixPrediction.simulation.targetId,
                                                                                      std::chrono::steady_clock::now(),
                                                                                      0.0 );
        std::vector<uint8_t> projectedPrefixBytes;

        if ( !BuildReplayPredictionArchive( prefixPathVisualizer, prefixPrediction, ReplayPredictionDetailMode::High,
                                            prefixEvidenceStorage->Committed(), projectedPrefixBytes ) ||
             projectedPrefixBytes != prefixBytes )
        {
            WriteAutomationReason( outReason, reasonSize,
                                   "offline projection changed an archived trajectory publication prefix" );
            return false;
        }
    }

    if ( restoredCapability != ReplayPredictionArchiveDetailCapability::High || bytes.size() < 72u ||
         bytes[ARCHIVE_CAPABILITY_OFFSET] != static_cast<uint8_t>( ReplayPredictionArchiveDetailCapability::High ) ||
         !VerifyEvidenceReadableWithoutPhysics( restoredPrediction, restoredEvidenceStorage->Committed() ) )
    {
        WriteAutomationReason( outReason, reasonSize, "loaded High archive evidence is not inspectable without Physics" );
        return false;
    }

    const uint64_t firstLoadedEpoch = restoredEvidenceStorage->Committed().BankEpoch();

    if ( !LoadReplayPredictionArchive( bytes, restoredPathVisualizer, restoredPrediction, *restoredEvidenceStorage,
                                       ReplayPredictionDetailMode::High, restoredCapability, outReason, reasonSize ) ||
         restoredEvidenceStorage->Committed().BankEpoch() <= firstLoadedEpoch ||
         !VerifyEvidenceReadableWithoutPhysics( restoredPrediction, restoredEvidenceStorage->Committed() ) )
    {
        WriteAutomationReason( outReason, reasonSize, "archive replacement reused solver evidence identity" );
        return false;
    }

    RunReplayPathVisualizerState migratedPathVisualizer;
    const auto migratedPredictionStorage = std::make_unique<RunReplayPredictionState>();
    RunReplayPredictionState& migratedPrediction = *migratedPredictionStorage;
    const auto migratedEvidenceStorage = std::make_unique<ReplayPredictionSolverEvidenceBanks>();
    ReplayPredictionArchiveDetailCapability migratedCapability = ReplayPredictionArchiveDetailCapability::High;

    for ( const uint32_t legacySchema : { 2u, 3u } )
    {
        std::vector<uint8_t> legacyBytes;

        if ( !BuildReplayPredictionArchiveForSchemaValidation( restoredPathVisualizer, restoredPrediction, legacySchema,
                                                               legacyBytes ) ||
             !LoadReplayPredictionArchive( legacyBytes, migratedPathVisualizer, migratedPrediction, *migratedEvidenceStorage,
                                           ReplayPredictionDetailMode::High, migratedCapability, outReason, reasonSize ) )
        {
            return false;
        }

        const ReplayPredictionSolverEvidenceBanksMemoryStats migratedStats = migratedEvidenceStorage->CollectMemoryStats();

        if ( migratedCapability != ReplayPredictionArchiveDetailCapability::Low ||
             migratedPrediction.trajectoryBuild.pathPresentation != restoredPrediction.trajectoryBuild.pathPresentation ||
             migratedStats.currentCapacityBytes != 0u || migratedStats.committed.publishedFrameCount != 0u )
        {
            WriteAutomationReason( outReason, reasonSize, "legacy prediction archive did not remain Low detail" );
            return false;
        }
    }

    std::vector<uint8_t> migratedCanonicalBytes;

    if ( !BuildReplayPredictionArchive( migratedPathVisualizer, migratedPrediction, ReplayPredictionDetailMode::High,
                                        migratedEvidenceStorage->Committed(), migratedCanonicalBytes ) )
    {
        WriteAutomationReason( outReason, reasonSize, "legacy prediction archive migration diverged" );
        return false;
    }

    if ( migratedCanonicalBytes.size() < 48u || migratedCanonicalBytes[ARCHIVE_CAPABILITY_OFFSET] !=
                                                    static_cast<uint8_t>( ReplayPredictionArchiveDetailCapability::Low ) )
    {
        WriteAutomationReason( outReason, reasonSize, "legacy migration did not produce canonical Low archive" );
        return false;
    }

    // Active Low validates the complete High artifact but commits no evidence
    // capacity. Its canonical output remains Low, and a subsequent High reader
    // cannot manufacture evidence that the lightweight artifact does not own.
    RunReplayPathVisualizerState lowPathVisualizer;
    const auto lowPredictionStorage = std::make_unique<RunReplayPredictionState>();
    RunReplayPredictionState& lowPrediction = *lowPredictionStorage;
    const auto lowEvidenceStorage = std::make_unique<ReplayPredictionSolverEvidenceBanks>();
    ReplayPredictionArchiveDetailCapability lowCapturedCapability = ReplayPredictionArchiveDetailCapability::Low;

    if ( !LoadReplayPredictionArchive( bytes, lowPathVisualizer, lowPrediction, *lowEvidenceStorage,
                                       ReplayPredictionDetailMode::Low, lowCapturedCapability, outReason, reasonSize ) )
    {
        return false;
    }

    const ReplayPredictionSolverEvidenceBanksMemoryStats lowStats = lowEvidenceStorage->CollectMemoryStats();

    if ( lowCapturedCapability != ReplayPredictionArchiveDetailCapability::High || lowStats.currentCapacityBytes != 0u ||
         lowStats.committed.publishedFrameCount != 0u )
    {
        WriteAutomationReason( outReason, reasonSize, "Low archive load retained solver evidence capacity" );
        return false;
    }

    std::vector<uint8_t> lowCanonicalBytes;

    if ( !BuildReplayPredictionArchive( lowPathVisualizer, lowPrediction, ReplayPredictionDetailMode::Low,
                                        lowEvidenceStorage->Committed(), lowCanonicalBytes ) ||
         lowCanonicalBytes.size() < 48u ||
         lowCanonicalBytes[ARCHIVE_CAPABILITY_OFFSET] !=
             static_cast<uint8_t>( ReplayPredictionArchiveDetailCapability::Low ) ||
         !LoadReplayPredictionArchive( lowCanonicalBytes, migratedPathVisualizer, migratedPrediction,
                                       *migratedEvidenceStorage, ReplayPredictionDetailMode::High, migratedCapability,
                                       outReason, reasonSize ) ||
         migratedCapability != ReplayPredictionArchiveDetailCapability::Low ||
         migratedEvidenceStorage->CollectMemoryStats().currentCapacityBytes != 0u )
    {
        WriteAutomationReason( outReason, reasonSize, "Low archive load upgraded solver detail" );
        return false;
    }

    // Hazard: the offsets below are part of the RVPD v4 format contract. Each
    // mutation targets a distinct preflight rule, and every rejection is then
    // checked against the already committed High archive state.
    std::vector<uint8_t> mutation( bytes.begin(), bytes.end() );
    std::swap_ranges( mutation.begin() + ARCHIVE_FIRST_DESCRIPTOR_OFFSET,
                      mutation.begin() + ARCHIVE_FIRST_DESCRIPTOR_OFFSET + ARCHIVE_DESCRIPTOR_BYTES,
                      mutation.begin() + ARCHIVE_FIRST_DESCRIPTOR_OFFSET + ARCHIVE_DESCRIPTOR_BYTES );

    if ( !VerifyRejectedArchivePreservesState( mutation, rebuiltBytes, restoredPathVisualizer, restoredPrediction,
                                               *restoredEvidenceStorage, ReplayPredictionArchiveDetailCapability::Low,
                                               nullptr, outReason, reasonSize ) )
    {
        return false;
    }

    mutation.assign( bytes.begin(), bytes.end() );
    WriteLittleEndianU64( mutation, ARCHIVE_SECOND_SECTION_OFFSET_FIELD,
                          ReadLittleEndianU64( bytes, ARCHIVE_FIRST_SECTION_OFFSET_FIELD ) );

    if ( !VerifyRejectedArchivePreservesState( mutation, rebuiltBytes, restoredPathVisualizer, restoredPrediction,
                                               *restoredEvidenceStorage, ReplayPredictionArchiveDetailCapability::Low,
                                               nullptr, outReason, reasonSize ) )
    {
        return false;
    }

    mutation.assign( bytes.begin(), bytes.end() );
    WriteLittleEndianU64( mutation, ARCHIVE_SECOND_SECTION_SIZE_FIELD, ( std::numeric_limits<uint64_t>::max )() );

    if ( !VerifyRejectedArchivePreservesState( mutation, rebuiltBytes, restoredPathVisualizer, restoredPrediction,
                                               *restoredEvidenceStorage, ReplayPredictionArchiveDetailCapability::Low,
                                               nullptr, outReason, reasonSize ) )
    {
        return false;
    }

    const uint64_t evidenceOffset = ReadLittleEndianU64( bytes, ARCHIVE_SECOND_SECTION_OFFSET_FIELD );

    if ( evidenceOffset > bytes.size() - 12u )
    {
        WriteAutomationReason( outReason, reasonSize, "prediction evidence section is too small for mutation tests" );
        return false;
    }

    mutation.assign( bytes.begin(), bytes.end() );
    WriteLittleEndianU32( mutation, static_cast<std::size_t>( evidenceOffset ) + 4u, 1u );

    if ( !VerifyRejectedArchivePreservesState( mutation, rebuiltBytes, restoredPathVisualizer, restoredPrediction,
                                               *restoredEvidenceStorage, ReplayPredictionArchiveDetailCapability::Low,
                                               nullptr, outReason, reasonSize ) )
    {
        return false;
    }

    mutation.assign( bytes.begin(), bytes.end() );
    WriteLittleEndianU32( mutation, static_cast<std::size_t>( evidenceOffset ) + 8u, 1'000'001u );

    if ( !VerifyRejectedArchivePreservesState( mutation, rebuiltBytes, restoredPathVisualizer, restoredPrediction,
                                               *restoredEvidenceStorage, ReplayPredictionArchiveDetailCapability::Low,
                                               nullptr, outReason, reasonSize ) )
    {
        return false;
    }

    mutation.assign( bytes.begin(), bytes.end() - 1 );

    if ( !VerifyRejectedArchivePreservesState( mutation, rebuiltBytes, restoredPathVisualizer, restoredPrediction,
                                               *restoredEvidenceStorage, ReplayPredictionArchiveDetailCapability::Low,
                                               nullptr, outReason, reasonSize ) )
    {
        return false;
    }

    mutation.assign( bytes.begin(), bytes.end() );
    mutation[ARCHIVE_PATH_POLICY_OFFSET] = mutation[ARCHIVE_PATH_POLICY_OFFSET] == 0u ? 1u : 0u;

    if ( !VerifyRejectedArchivePreservesState( mutation, rebuiltBytes, restoredPathVisualizer, restoredPrediction,
                                               *restoredEvidenceStorage, ReplayPredictionArchiveDetailCapability::Low,
                                               "prediction archive path presentation mismatch", outReason, reasonSize ) )
    {
        return false;
    }

    mutation.assign( bytes.begin(), bytes.end() );
    mutation[ARCHIVE_CAPABILITY_OFFSET] = static_cast<uint8_t>( ReplayPredictionArchiveDetailCapability::Low );

    if ( !VerifyRejectedArchivePreservesState( mutation, rebuiltBytes, restoredPathVisualizer, restoredPrediction,
                                               *restoredEvidenceStorage, ReplayPredictionArchiveDetailCapability::Low,
                                               "prediction archive capability/section mismatch", outReason, reasonSize ) )
    {
        return false;
    }

    mutation.assign( bytes.begin(), bytes.end() );
    WriteLittleEndianU32( mutation, ARCHIVE_SECTION_COUNT_OFFSET, 1u );

    if ( !VerifyRejectedArchivePreservesState( mutation, rebuiltBytes, restoredPathVisualizer, restoredPrediction,
                                               *restoredEvidenceStorage, ReplayPredictionArchiveDetailCapability::Low,
                                               "prediction archive capability/section mismatch", outReason, reasonSize ) )
    {
        return false;
    }

    mutation.assign( bytes.begin(), bytes.end() );
    WriteLittleEndianU64( mutation, ARCHIVE_TOTAL_BYTES_OFFSET, bytes.size() + 1u );

    if ( !VerifyRejectedArchivePreservesState( mutation, rebuiltBytes, restoredPathVisualizer, restoredPrediction,
                                               *restoredEvidenceStorage, ReplayPredictionArchiveDetailCapability::Low,
                                               "invalid prediction archive header", outReason, reasonSize ) )
    {
        return false;
    }

    std::vector<uint8_t> futureBytes( bytes.begin(), bytes.end() );

    if ( futureBytes.size() < ARCHIVE_SCHEMA_OFFSET + sizeof( uint32_t ) )
    {
        WriteAutomationReason( outReason, reasonSize, "prediction archive header is truncated" );
        return false;
    }

    // Hazard: bytes 4..7 are the little-endian schema field immediately after
    // the fixed RVPD magic. A future value must fail closed before any payload
    // values are accepted.
    WriteLittleEndianU32( futureBytes, ARCHIVE_SCHEMA_OFFSET, 5u );

    if ( !VerifyRejectedArchivePreservesState( futureBytes, rebuiltBytes, restoredPathVisualizer, restoredPrediction,
                                               *restoredEvidenceStorage, ReplayPredictionArchiveDetailCapability::Low,
                                               "invalid prediction archive header", outReason, reasonSize ) )
    {
        return false;
    }

    return true;
}
} // namespace SkullbonezCore::Runtime::ReplayPredictionArchiveOperations
