/*
File: SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.cpp
Purpose:
  Serializes and restores the presentation-bearing state of one completed prediction.

Summary:
  Schema v6 wraps the precise v5 lightweight prediction payload in an ordered
  section table and adds only the unique root/contact-event solver frames when
  the captured capability is High. The reader validates byte closure, counts,
  references, and path policy into replay-reserve-accounted candidate owners,
  then swaps them into the caller only after the complete artifact succeeds.

Glossary:
  Scalar codec: Explicit little-endian encoding for one integer or float field.
  Presentation cache: Derived prediction values consumed by overlay drawing.
  All-body bank: Additional body-keyed FutureRoot records used by space scenes.
  Captured capability: Detail present in the artifact, independent of the
    reader's active High/Low preference.

Invariants:
  - Every vector count is checked against a presentation-specific hard limit.
  - The complete payload fails closed above 128 MiB.
  - High evidence contains frame zero plus each unique contact-derived node's
    first frame in ascending order; Low archives have no evidence section.
  - Schemas v3-v6 use canonical Hamilton quaternion components; the reader
    conjugates schema v2 vector parts exactly once. Sectioned v4 remains readable.
  - Active Low validates a High evidence section but commits zero evidence
    capacity; v2/v3 artifacts always load with Low captured capability.
  - A failed load leaves prior lightweight state, evidence rows, capacity, and
    publication identity unchanged.
  - No deserialized value can create or schedule prediction physics work.

Related:
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.Automation.cpp
*/
#include "ReplayPredictionArchive.h"
#include "ReplayPrediction.h"
#include "ReplayPredictionReserve.h"

#include "../Replay/ReplayPathPackets.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>

namespace SkullbonezCore::Runtime
{
namespace
{
constexpr uint32_t REPLAY_PREDICTION_ARCHIVE_MAGIC = 0x44505652u; // "RVPD"
constexpr uint32_t REPLAY_PREDICTION_ARCHIVE_MINIMUM_SCHEMA = 2u;
constexpr uint32_t REPLAY_PREDICTION_ARCHIVE_QUATERNION_SCHEMA = 3u;
constexpr uint32_t REPLAY_PREDICTION_ARCHIVE_HISTORICAL_LIGHTWEIGHT_SCHEMA = 3u;
constexpr uint32_t REPLAY_PREDICTION_ARCHIVE_HISTORICAL_SECTIONED_SCHEMA = 4u;
constexpr uint32_t REPLAY_PREDICTION_ARCHIVE_PRECISE_LIGHTWEIGHT_SCHEMA = 5u;
constexpr uint32_t REPLAY_PREDICTION_ARCHIVE_SCHEMA = 6u;
bool IsLightweightPredictionArchiveSchema( uint32_t schema )
{
    return ( schema >= REPLAY_PREDICTION_ARCHIVE_MINIMUM_SCHEMA &&
             schema <= REPLAY_PREDICTION_ARCHIVE_HISTORICAL_LIGHTWEIGHT_SCHEMA ) ||
           schema == REPLAY_PREDICTION_ARCHIVE_PRECISE_LIGHTWEIGHT_SCHEMA;
}

bool IsSectionedPredictionArchiveSchema( uint32_t schema )
{
    return schema == REPLAY_PREDICTION_ARCHIVE_HISTORICAL_SECTIONED_SCHEMA || schema == REPLAY_PREDICTION_ARCHIVE_SCHEMA;
}
constexpr uint16_t REPLAY_TRAJECTORY_COMMITTED_BRANCH = 0u;
constexpr uint16_t REPLAY_TRAJECTORY_BUILD_BRANCH = 1u;
constexpr uint32_t REPLAY_PREDICTION_ARCHIVE_MAX_FRAMES = 7201u;
constexpr uint32_t REPLAY_PREDICTION_ARCHIVE_MAX_BODIES = static_cast<uint32_t>(
    SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
constexpr uint32_t REPLAY_PREDICTION_ARCHIVE_MAX_RECORDS = REPLAY_PREDICTION_MARKER_CAPACITY * 8u;
constexpr uint32_t REPLAY_PREDICTION_ARCHIVE_MAX_POINTS = 4000000u;
constexpr std::size_t REPLAY_PREDICTION_ARCHIVE_MAX_BYTES = 128u * 1024u * 1024u;

bool IsInactivePredictionWorkerBankRecord( const ReplayTrajectoryRecord& record,
                                           bool presentedTrajectoryUsesBuildBank ) noexcept
{
    const bool childLane = record.key.lane == ReplayTrajectoryLane::FutureChildIncoming ||
                           record.key.lane == ReplayTrajectoryLane::FutureChildOutgoing;
    const bool recordUsesBuildBank = record.key.branchOrdinal >= REPLAY_VISUAL_FUTURE_NODE_CAPACITY;

    return childLane && recordUsesBuildBank != presentedTrajectoryUsesBuildBank;
}

uint32_t CountCanonicalTrajectoryVersions( const ReplayTrajectoryStore& store,
                                           bool presentedTrajectoryUsesBuildBank ) noexcept
{
    uint32_t count = 0;

    for ( const ReplayTrajectoryRecord& record : store.ActiveRecords() )
    {
        if ( !IsInactivePredictionWorkerBankRecord( record, presentedTrajectoryUsesBuildBank ) )
        {
            ++count;
        }
    }

    return count;
}

void WriteReason( char* destination, std::size_t size, const char* message )
{
    if ( destination && size > 0 )
    {
        std::snprintf( destination, size, "%s", message ? message : "prediction archive failure" );
    }
}

class ArchiveWriter
{
  public:
    explicit ArchiveWriter( uint32_t schema = REPLAY_PREDICTION_ARCHIVE_PRECISE_LIGHTWEIGHT_SCHEMA ) : m_schema( schema )
    {
    }

    template <typename T> void Scalar( T value )
    {
        static_assert( std::is_integral_v<T> );
        using U = std::make_unsigned_t<T>;

        if ( m_overflow || m_bytes.size() > REPLAY_PREDICTION_ARCHIVE_MAX_BYTES - sizeof( U ) )
        {
            m_overflow = true;
            return;
        }

        U bits = static_cast<U>( value );

        for ( std::size_t byte = 0; byte < sizeof( U ); ++byte )
        {
            m_bytes.push_back( static_cast<uint8_t>( bits >> ( byte * 8u ) ) );
        }
    }

    void Boolean( bool value )
    {
        Scalar<uint8_t>( value ? 1u : 0u );
    }
    void Float( float value )
    {
        Scalar<uint32_t>( std::bit_cast<uint32_t>( value ) );
    }
    void Double( double value )
    {
        Scalar<uint64_t>( std::bit_cast<uint64_t>( value ) );
    }
    void Vector( const Math::Vector::Vector3& value )
    {
        Float( value.x );
        Float( value.y );
        Float( value.z );
    }
    void Quaternion( const Math::Orientation::Quaternion& value )
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;
        value.GetComponents( x, y, z, w );

        if ( m_schema < REPLAY_PREDICTION_ARCHIVE_QUATERNION_SCHEMA )
        {
            // Compatibility: schema v2 stored the conjugate representation.
            // This validation writer produces authentic historical bytes so the
            // reader's migration is tested against the actual old convention.
            Math::Orientation::ConjugateQuaternionVectorPart( x, y, z );
        }

        Float( x );
        Float( y );
        Float( z );
        Float( w );
    }
    void Bytes( std::span<const uint8_t> bytes )
    {
        if ( m_overflow || bytes.size() > REPLAY_PREDICTION_ARCHIVE_MAX_BYTES - m_bytes.size() )
        {
            m_overflow = true;
            return;
        }

        m_bytes.insert( m_bytes.end(), bytes.begin(), bytes.end() );
    }
    std::size_t Size() const noexcept
    {
        return m_bytes.size();
    }
    bool Valid() const noexcept
    {
        return !m_overflow;
    }
    std::vector<uint8_t> Finish()
    {
        return std::move( m_bytes );
    }

  private:
    std::vector<uint8_t> m_bytes;
    uint32_t m_schema = REPLAY_PREDICTION_ARCHIVE_SCHEMA;
    bool m_overflow = false;
};

class ArchiveReader
{
  public:
    explicit ArchiveReader( std::span<const uint8_t> bytes ) : m_bytes( bytes )
    {
    }

    template <typename T> bool Scalar( T& value )
    {
        static_assert( std::is_integral_v<T> );
        using U = std::make_unsigned_t<T>;

        if ( m_offset > m_bytes.size() || sizeof( U ) > m_bytes.size() - m_offset )
        {
            return false;
        }

        U bits = 0;

        for ( std::size_t byte = 0; byte < sizeof( U ); ++byte )
        {
            bits |= static_cast<U>( m_bytes[m_offset + byte] ) << ( byte * 8u );
        }

        m_offset += sizeof( U );
        value = static_cast<T>( bits );
        return true;
    }

    bool Boolean( bool& value )
    {
        uint8_t encoded = 0;

        if ( !Scalar( encoded ) || encoded > 1u )
        {
            return false;
        }

        value = encoded != 0u;
        return true;
    }
    bool Float( float& value )
    {
        uint32_t bits = 0;

        if ( !Scalar( bits ) )
        {
            return false;
        }

        value = std::bit_cast<float>( bits );
        return true;
    }
    bool Double( double& value )
    {
        uint64_t bits = 0;

        if ( !Scalar( bits ) )
        {
            return false;
        }

        value = std::bit_cast<double>( bits );
        return true;
    }
    bool Vector( Math::Vector::Vector3& value )
    {
        return Float( value.x ) && Float( value.y ) && Float( value.z );
    }
    bool Quaternion( Math::Orientation::Quaternion& value, uint32_t schema )
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;

        if ( !Float( x ) || !Float( y ) || !Float( z ) || !Float( w ) )
        {
            return false;
        }

        if ( schema < REPLAY_PREDICTION_ARCHIVE_QUATERNION_SCHEMA )
        {
            // Compatibility: schema v2 stored the conjugate representation.
            // Negating xyz changes only their sign bits and preserves w.
            Math::Orientation::ConjugateQuaternionVectorPart( x, y, z );
        }

        value = Math::Orientation::Quaternion( x, y, z, w );
        return true;
    }
    bool Finished() const noexcept
    {
        return m_offset == m_bytes.size();
    }
    std::size_t Offset() const noexcept
    {
        return m_offset;
    }

  private:
    std::span<const uint8_t> m_bytes;
    std::size_t m_offset = 0;
};

void WriteBody( ArchiveWriter& writer, const RunReplayPredictionBodySample& body )
{
    writer.Scalar( body.id.value );
    writer.Scalar( body.modelRow.value );
    writer.Vector( body.position );
    writer.Quaternion( body.orientation );
    writer.Vector( body.linearVelocity );
    writer.Boolean( body.sleeping );
}

bool ReadBody( ArchiveReader& reader, uint32_t schema, RunReplayPredictionBodySample& body )
{
    return reader.Scalar( body.id.value ) && reader.Scalar( body.modelRow.value ) && reader.Vector( body.position ) &&
           reader.Quaternion( body.orientation, schema ) && reader.Vector( body.linearVelocity ) &&
           reader.Boolean( body.sleeping );
}

void WriteNode( ArchiveWriter& writer, const RunReplayPathTraceNode& node )
{
    writer.Scalar( node.id.value );
    writer.Scalar( node.parentId.value );
    writer.Scalar( node.modelRow.value );
    writer.Scalar( node.parentModelRow.value );
    writer.Scalar( node.firstFrame );
    writer.Vector( node.contactPoint );
    writer.Vector( node.contactNormal );
    writer.Scalar( node.depth );
    writer.Boolean( node.contactDerived );
}

bool ReadNode( ArchiveReader& reader, RunReplayPathTraceNode& node )
{
    return reader.Scalar( node.id.value ) && reader.Scalar( node.parentId.value ) && reader.Scalar( node.modelRow.value ) &&
           reader.Scalar( node.parentModelRow.value ) && reader.Scalar( node.firstFrame ) &&
           reader.Vector( node.contactPoint ) && reader.Vector( node.contactNormal ) && reader.Scalar( node.depth ) &&
           reader.Boolean( node.contactDerived );
}

void WriteMarker( ArchiveWriter& writer, const ReplayPredictionRetainedMarker& marker )
{
    writer.Scalar( marker.id.value );
    writer.Scalar( marker.modelRow.value );
    writer.Boolean( marker.hasEntryPose );
    writer.Boolean( marker.hasRestPose );
    writer.Boolean( marker.hasHorizonPose );
    writer.Vector( marker.entryPosition );
    writer.Quaternion( marker.entryOrientation );
    writer.Vector( marker.restPosition );
    writer.Quaternion( marker.restOrientation );
    writer.Vector( marker.horizonPosition );
    writer.Quaternion( marker.horizonOrientation );
}

bool ReadMarker( ArchiveReader& reader, uint32_t schema, ReplayPredictionRetainedMarker& marker )
{
    return reader.Scalar( marker.id.value ) && reader.Scalar( marker.modelRow.value ) &&
           reader.Boolean( marker.hasEntryPose ) && reader.Boolean( marker.hasRestPose ) &&
           reader.Boolean( marker.hasHorizonPose ) && reader.Vector( marker.entryPosition ) &&
           reader.Quaternion( marker.entryOrientation, schema ) && reader.Vector( marker.restPosition ) &&
           reader.Quaternion( marker.restOrientation, schema ) && reader.Vector( marker.horizonPosition ) &&
           reader.Quaternion( marker.horizonOrientation, schema );
}

void WriteBaselinePose( ArchiveWriter& writer, const ReplayPredictionBaselineBodyPose& pose )
{
    writer.Scalar( pose.id.value );
    writer.Scalar( pose.modelRow.value );
    writer.Boolean( pose.hasEntryPose );
    writer.Boolean( pose.hasRestPose );
    writer.Vector( pose.entryPosition );
    writer.Quaternion( pose.entryOrientation );
    writer.Vector( pose.restPosition );
    writer.Quaternion( pose.restOrientation );
}

bool ReadBaselinePose( ArchiveReader& reader, uint32_t schema, ReplayPredictionBaselineBodyPose& pose )
{
    return reader.Scalar( pose.id.value ) && reader.Scalar( pose.modelRow.value ) && reader.Boolean( pose.hasEntryPose ) &&
           reader.Boolean( pose.hasRestPose ) && reader.Vector( pose.entryPosition ) &&
           reader.Quaternion( pose.entryOrientation, schema ) && reader.Vector( pose.restPosition ) &&
           reader.Quaternion( pose.restOrientation, schema );
}

bool ReadBoundedCount( ArchiveReader& reader, uint32_t maximum, uint32_t& count )
{
    return reader.Scalar( count ) && count <= maximum;
}

bool ReplayPredictionArchivePathPresentationMatchesRecords( const RunReplayPredictionState& prediction )
{
    const ReplayPredictionPresentationView presentation = ReplayPrediction::PresentationViewFromState( prediction, false );
    const uint16_t activeRootBranch = presentation.trajectory.usingBuildFrames ? REPLAY_TRAJECTORY_BUILD_BRANCH
                                                                               : REPLAY_TRAJECTORY_COMMITTED_BRANCH;
    bool hasAdditionalRoot = false;

    for ( const ReplayTrajectoryRecord& record : prediction.trajectoryStore.ActiveRecords() )
    {
        hasAdditionalRoot = hasAdditionalRoot || ( record.key.lane == ReplayTrajectoryLane::FutureRoot &&
                                                   record.key.branchOrdinal == activeRootBranch &&
                                                   record.key.bodyId != presentation.trajectory.buildRootId );
    }

    return ReplayPredictionPathPresentationShowsAllBodies( presentation.pathPresentation ) == hasAdditionalRoot;
}
} // namespace

namespace ReplayPredictionArchiveOperations
{
uint64_t ReplayPredictionArchiveCandidateAllocationBudgetBytes() noexcept
{
    // MSVC may request alignment bookkeeping beyond vector capacity bytes.
    // A bounded owner-local cushion covers every constructor reserve without
    // depending on one standard-library implementation's private layout.
    constexpr uint64_t constructorAllocationHeadroomBytes = 4096u;
    return sizeof( RunReplayPredictionState ) + sizeof( ReplayPredictionSolverEvidenceBanks ) +
           Gameplay::TornadoGameplay::InitialReserveBytes() +
           Gameplay::TornadoGameplay::MAX_ACTIVE_FORCE_FIELDS * sizeof( Gameplay::TornadoVortexConfig ) +
           2u * SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS * sizeof( float ) + constructorAllocationHeadroomBytes;
}

bool BuildReplayPredictionArchiveForSchemaValidation( const RunReplayPathVisualizerState& pathVisualizer,
                                                      const RunReplayPredictionState& prediction, uint32_t schema,
                                                      std::vector<uint8_t>& outBytes )
{
    outBytes.clear();

    // Invariant: a pending committed publication is the renderer-visible bank.
    // Durable replay must serialize that coherent snapshot, not a newer cache.
    const ReplayPredictionPresentationView presentation = ReplayPrediction::PresentationViewFromState( prediction, false );
    const std::span<const RunReplayPredictionFrame> committedFrames = presentation.timeline.frames;
    const bool usingVisibleSnapshot = prediction.committedPublication.pending ||
                                      ( prediction.committedPublication.visibleSnapshotCaptured &&
                                        !prediction.BuildPrefixShouldBePresented() );
    const RunReplayPredictionTrajectoryBuildState& presentedTrajectory = usingVisibleSnapshot
                                                                             ? prediction.committedPublication
                                                                                   .visibleTrajectoryBuild
                                                                             : prediction.trajectoryBuild;

    if ( !IsLightweightPredictionArchiveSchema( schema ) || prediction.build.building || !prediction.build.complete ||
         committedFrames.size() < 2u || committedFrames.size() > REPLAY_PREDICTION_ARCHIVE_MAX_FRAMES ||
         presentation.markers.retainedMarkers.size() > REPLAY_PREDICTION_MARKER_CAPACITY ||
         prediction.trajectoryStore.RecordCount() > REPLAY_PREDICTION_ARCHIVE_MAX_RECORDS ||
         !ReplayPredictionArchivePathPresentationMatchesRecords( prediction ) )
    {
        return false;
    }

    ArchiveWriter writer( schema );
    writer.Scalar( REPLAY_PREDICTION_ARCHIVE_MAGIC );
    writer.Scalar( schema );
    writer.Boolean( pathVisualizer.hasTarget );
    writer.Boolean( pathVisualizer.pastPathVisible );
    writer.Scalar( pathVisualizer.targetId.value );
    writer.Scalar( pathVisualizer.targetModelRow.value );
    uint8_t targetNameLength = 0;

    while ( targetNameLength + 1u < sizeof( pathVisualizer.targetName ) &&
            pathVisualizer.targetName[targetNameLength] != '\0' )
    {
        ++targetNameLength;
    }

    writer.Scalar( targetNameLength );

    for ( uint8_t index = 0; index < targetNameLength; ++index )
    {
        writer.Scalar( static_cast<uint8_t>( pathVisualizer.targetName[index] ) );
    }

    writer.Boolean( prediction.ragdollVisualsEnabled );
    writer.Float( prediction.simulation.horizonSeconds );
    writer.Scalar( prediction.simulation.targetModelRow.value );
    writer.Scalar( prediction.simulation.targetId.value );
    writer.Scalar( prediction.simulation.sourceFrameIndex );
    writer.Scalar( prediction.simulation.sourceSolverHash );
    writer.Double( prediction.simulation.sourceSimulationSeconds );
    writer.Scalar( prediction.build.generationBeginCount );

    writer.Scalar( static_cast<uint32_t>( committedFrames.size() ) );

    for ( const RunReplayPredictionFrame& frame : committedFrames )
    {
        if ( frame.bodies.size() > REPLAY_PREDICTION_ARCHIVE_MAX_BODIES )
        {
            return false;
        }

        writer.Scalar( frame.frameIndex );
        writer.Double( frame.simulationSeconds );

        if ( schema >= REPLAY_PREDICTION_ARCHIVE_PRECISE_LIGHTWEIGHT_SCHEMA )
        {
            writer.Double( frame.tornadoSystemElapsedSeconds );
        }
        else
        {
            writer.Float( static_cast<float>( frame.tornadoSystemElapsedSeconds ) );
        }

        writer.Boolean( frame.contactsIncomplete );
        writer.Scalar( static_cast<uint32_t>( frame.bodies.size() ) );

        for ( const RunReplayPredictionBodySample& body : frame.bodies )
        {
            WriteBody( writer, body );
        }
    }

    // Concept: topology versions are equality tokens, not durable generation
    // numbers. A completed RVPD contains one published topology, so first-
    // publication order maps it to one and reserves two for a later rebuild.
    const uint32_t canonicalTopologyVersion = presentation.topology.version != 0u ? 1u : 0u;
    const uint32_t canonicalNextTopologyVersion = canonicalTopologyVersion != 0u ? 2u : 1u;
    writer.Scalar( canonicalTopologyVersion );
    writer.Scalar( canonicalNextTopologyVersion );
    const bool presentedFutureNodesBuiltRagdollVisuals = usingVisibleSnapshot
                                                             ? prediction.committedPublication
                                                                   .visibleFutureNodesBuiltRagdollVisuals
                                                             : prediction.futureNodeCache.futureNodesBuiltRagdollVisuals;
    writer.Boolean( presentedFutureNodesBuiltRagdollVisuals );
    writer.Scalar( static_cast<uint32_t>( presentation.topology.futureNodes.size() ) );

    for ( const RunReplayPathTraceNode& node : presentation.topology.futureNodes )
    {
        WriteNode( writer, node );
    }

    writer.Scalar( static_cast<uint32_t>( presentation.markers.retainedMarkers.size() ) );

    for ( const ReplayPredictionRetainedMarker& marker : presentation.markers.retainedMarkers )
    {
        WriteMarker( writer, marker );
    }

    const uint32_t canonicalTrajectoryVersionCount = CountCanonicalTrajectoryVersions( prediction.trajectoryStore,
                                                                                       presentedTrajectory
                                                                                           .usingBuildFrames );
    writer.Scalar( canonicalTrajectoryVersionCount + 1u );
    writer.Scalar( static_cast<uint32_t>( prediction.trajectoryStore.RecordCount() ) );
    uint64_t totalPointCount = 0;
    uint32_t canonicalTrajectoryVersion = 1u;

    for ( const ReplayTrajectoryRecord& record : prediction.trajectoryStore.ActiveRecords() )
    {
        if ( IsInactivePredictionWorkerBankRecord( record, presentedTrajectory.usingBuildFrames ) )
        {
            // Hazard: this double-buffer bank is not part of the selected
            // publication, but its schedule-selected keys and point payloads
            // used to leak into the durable artifact. Keep one fixed-width
            // record slot while replacing all variable telemetry with an inert
            // constant; the reader layout and record-count contract stay intact.
            writer.Scalar( static_cast<uint32_t>( 0u ) );
            writer.Scalar( static_cast<uint8_t>( ReplayTrajectoryLane::FutureChildIncoming ) );
            const uint16_t inactiveBankSentinel = presentedTrajectory.usingBuildFrames ? REPLAY_TRAJECTORY_COMMITTED_BRANCH
                                                                                       : REPLAY_VISUAL_FUTURE_NODE_CAPACITY;
            writer.Scalar( inactiveBankSentinel );
            writer.Scalar( static_cast<uint32_t>( 0u ) );
            writer.Scalar( static_cast<uint32_t>( 0u ) );
            writer.Scalar( static_cast<uint16_t>( 0u ) );
            writer.Scalar( static_cast<uint32_t>( 0u ) );
            writer.Scalar( static_cast<int32_t>( 0 ) );
            writer.Scalar( static_cast<ReplayFrameIndex>( 0u ) );
            writer.Boolean( false );
            writer.Scalar( static_cast<uint32_t>( 0u ) );
            continue;
        }

        totalPointCount += record.points.size();

        if ( totalPointCount > REPLAY_PREDICTION_ARCHIVE_MAX_POINTS || record.publishedPointCount > record.points.size() )
        {
            return false;
        }

        writer.Scalar( record.key.bodyId.value );
        writer.Scalar( static_cast<uint8_t>( record.key.lane ) );
        writer.Scalar( record.key.branchOrdinal );
        writer.Scalar( canonicalTrajectoryVersion++ );
        writer.Scalar( static_cast<uint32_t>( record.publishedPointCount ) );
        writer.Scalar( record.styleId );
        writer.Scalar( record.parentId.value );
        writer.Scalar( record.depth );
        writer.Scalar( record.firstFrame );
        writer.Boolean( record.contactDerived );
        writer.Scalar( static_cast<uint32_t>( record.points.size() ) );

        for ( const ReplayTrajectoryPoint& point : record.points )
        {
            writer.Scalar( point.frameIndex );
            writer.Vector( point.position );
        }
    }

    writer.Scalar( presentedTrajectory.rootId.value );
    writer.Boolean( presentedTrajectory.usingBuildFrames );
    writer.Scalar( static_cast<uint32_t>( presentedTrajectory.rootFrameCount ) );
    writer.Scalar( static_cast<uint32_t>( presentedTrajectory.childFrameCount ) );
    writer.Scalar( static_cast<uint32_t>( presentedTrajectory.builtNodeCount ) );

    // Invariant: the build-state token must equal the canonical future-node
    // token whenever the live tokens matched; offline reconstruction exercises
    // the same equality check without depending on the process-local number.
    const uint32_t canonicalBuildTopologyVersion = presentedTrajectory.topologyVersion == presentation.topology.version
                                                       ? canonicalTopologyVersion
                                                       : 0u;

    writer.Scalar( canonicalBuildTopologyVersion );
    writer.Boolean( presentedTrajectory.valid );

    const ReplayPredictionBaselineSnapshot& baseline = prediction.baseline;
    writer.Boolean( baseline.valid );
    writer.Boolean( baseline.comparisonActive );
    writer.Scalar( baseline.rootId.value );
    writer.Scalar( baseline.rootModelRow.value );
    writer.Scalar( baseline.lastFrame );
    writer.Boolean( baseline.divergenceValid );
    writer.Float( baseline.divergenceUnits );
    writer.Scalar( static_cast<uint32_t>( baseline.rootPolyline.size() ) );

    for ( const ReplayPredictionBaselineRootPoint& point : baseline.rootPolyline )
    {
        writer.Scalar( point.frameIndex );
        writer.Vector( point.position );
    }

    writer.Scalar( static_cast<uint32_t>( baseline.bodyPoses.size() ) );

    for ( const ReplayPredictionBaselineBodyPose& pose : baseline.bodyPoses )
    {
        WriteBaselinePose( writer, pose );
    }

    const bool valid = writer.Valid();
    outBytes = writer.Finish();
    return valid && !outBytes.empty();
}

static bool BuildLegacyReplayPredictionArchive( const RunReplayPathVisualizerState& pathVisualizer,
                                                const RunReplayPredictionState& prediction, std::vector<uint8_t>& outBytes )
{
    return BuildReplayPredictionArchiveForSchemaValidation( pathVisualizer, prediction,
                                                            REPLAY_PREDICTION_ARCHIVE_PRECISE_LIGHTWEIGHT_SCHEMA, outBytes );
}

static bool LoadLegacyReplayPredictionArchive( std::span<const uint8_t> bytes, RunReplayPathVisualizerState& pathVisualizer,
                                               RunReplayPredictionState& prediction, char* outReason,
                                               std::size_t reasonSize )
{
    if ( bytes.size() > REPLAY_PREDICTION_ARCHIVE_MAX_BYTES )
    {
        WriteReason( outReason, reasonSize, "prediction archive exceeds byte cap" );
        return false;
    }

    ArchiveReader reader( bytes );
    uint32_t magic = 0;
    uint32_t schema = 0;
    bool archivedHasTarget = false;
    bool archivedPastPathVisible = false;
    bool ragdollVisuals = false;
    uint8_t targetNameLength = 0;
    pathVisualizer.targetName[0] = '\0';

    if ( !reader.Scalar( magic ) || !reader.Scalar( schema ) || magic != REPLAY_PREDICTION_ARCHIVE_MAGIC ||
         !IsLightweightPredictionArchiveSchema( schema ) || !reader.Boolean( archivedHasTarget ) ||
         !reader.Boolean( archivedPastPathVisible ) || !reader.Scalar( pathVisualizer.targetId.value ) ||
         !reader.Scalar( pathVisualizer.targetModelRow.value ) || !reader.Scalar( targetNameLength ) ||
         targetNameLength >= sizeof( pathVisualizer.targetName ) )
    {
        WriteReason( outReason, reasonSize, "invalid prediction archive header" );
        return false;
    }

    for ( uint8_t index = 0; index < targetNameLength; ++index )
    {
        uint8_t encoded = 0;

        if ( !reader.Scalar( encoded ) )
        {
            WriteReason( outReason, reasonSize, "truncated prediction target name" );
            return false;
        }

        pathVisualizer.targetName[index] = static_cast<char>( encoded );
    }

    pathVisualizer.targetName[targetNameLength] = '\0';

    if ( !reader.Boolean( ragdollVisuals ) )
    {
        WriteReason( outReason, reasonSize, "invalid prediction archive visual flags" );
        return false;
    }

    pathVisualizer.targets.clear();
    pathVisualizer.pastTrajectory = {};

    prediction.InvalidateCommittedFrames();
    prediction.simulation.frames.clear();
    prediction.futureNodeCache.futureNodes.clear();
    prediction.baseline.rootPolyline.clear();
    prediction.baseline.bodyPoses.clear();
    prediction.futureNodeCache.ResetRetainedMarkers();

    if ( !reader.Float( prediction.simulation.horizonSeconds ) ||
         !reader.Scalar( prediction.simulation.targetModelRow.value ) ||
         !reader.Scalar( prediction.simulation.targetId.value ) ||
         !reader.Scalar( prediction.simulation.sourceFrameIndex ) ||
         !reader.Scalar( prediction.simulation.sourceSolverHash ) ||
         !reader.Double( prediction.simulation.sourceSimulationSeconds ) ||
         !reader.Scalar( prediction.build.generationBeginCount ) )
    {
        WriteReason( outReason, reasonSize, "truncated prediction metadata" );
        return false;
    }

    uint32_t frameCount = 0;

    if ( !ReadBoundedCount( reader, REPLAY_PREDICTION_ARCHIVE_MAX_FRAMES, frameCount ) || frameCount < 2u )
    {
        WriteReason( outReason, reasonSize, "invalid prediction frame count" );
        return false;
    }

    prediction.simulation.frames.reserve( frameCount );

    for ( uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex )
    {
        RunReplayPredictionFrame frame;
        uint32_t bodyCount = 0;

        if ( !reader.Scalar( frame.frameIndex ) || !reader.Double( frame.simulationSeconds ) )
        {
            WriteReason( outReason, reasonSize, "truncated prediction frame" );
            return false;
        }

        if ( schema >= REPLAY_PREDICTION_ARCHIVE_PRECISE_LIGHTWEIGHT_SCHEMA )
        {
            if ( !reader.Double( frame.tornadoSystemElapsedSeconds ) )
            {
                WriteReason( outReason, reasonSize, "truncated prediction frame" );
                return false;
            }
        }
        else
        {
            float legacyElapsedSeconds = 0.0f;

            if ( !reader.Float( legacyElapsedSeconds ) )
            {
                WriteReason( outReason, reasonSize, "truncated prediction frame" );
                return false;
            }

            frame.tornadoSystemElapsedSeconds = static_cast<double>( legacyElapsedSeconds );
        }

        if ( !reader.Boolean( frame.contactsIncomplete ) ||
             !ReadBoundedCount( reader, REPLAY_PREDICTION_ARCHIVE_MAX_BODIES, bodyCount ) )
        {
            WriteReason( outReason, reasonSize, "truncated prediction frame" );
            return false;
        }

        frame.bodies.resize( bodyCount );

        for ( RunReplayPredictionBodySample& body : frame.bodies )
        {
            if ( !ReadBody( reader, schema, body ) )
            {
                WriteReason( outReason, reasonSize, "truncated prediction body" );
                return false;
            }
        }

        prediction.simulation.frames.push_back( std::move( frame ) );
    }

    uint32_t nodeCount = 0;

    if ( !reader.Scalar( prediction.futureNodeCache.futureNodesTopologyVersion ) ||
         !reader.Scalar( prediction.futureNodeCache.nextFutureNodesTopologyVersion ) ||
         !reader.Boolean( prediction.futureNodeCache.futureNodesBuiltRagdollVisuals ) ||
         !ReadBoundedCount( reader, REPLAY_PREDICTION_MARKER_CAPACITY, nodeCount ) )
    {
        WriteReason( outReason, reasonSize, "invalid future-node block" );
        return false;
    }

    prediction.futureNodeCache.futureNodes.resize( nodeCount );

    for ( RunReplayPathTraceNode& node : prediction.futureNodeCache.futureNodes )
    {
        if ( !ReadNode( reader, node ) )
        {
            WriteReason( outReason, reasonSize, "truncated future node" );
            return false;
        }
    }

    uint32_t markerCount = 0;

    if ( !ReadBoundedCount( reader, REPLAY_PREDICTION_MARKER_CAPACITY, markerCount ) )
    {
        WriteReason( outReason, reasonSize, "invalid retained-marker count" );
        return false;
    }

    for ( uint32_t markerIndex = 0; markerIndex < markerCount; ++markerIndex )
    {
        if ( !ReadMarker( reader, schema, prediction.futureNodeCache.retainedMarkers[markerIndex] ) )
        {
            WriteReason( outReason, reasonSize, "truncated retained marker" );
            return false;
        }
    }

    prediction.futureNodeCache.retainedMarkerCount = markerCount;
    prediction.futureNodeCache.retainedMarkersVersion = markerCount > 0u ? 1u : 0u;
    prediction.futureNodeCache.nextRetainedMarkersVersion = markerCount > 0u ? 2u : 1u;
    prediction.futureNodeCache.childMarkerScan.Reset();

    uint32_t recordCount = 0;

    if ( !reader.Scalar( prediction.trajectoryStore.nextVersion ) ||
         !ReadBoundedCount( reader, REPLAY_PREDICTION_ARCHIVE_MAX_RECORDS, recordCount ) )
    {
        WriteReason( outReason, reasonSize, "invalid trajectory record count" );
        return false;
    }

    std::vector<ReplayTrajectoryRecord> loadedTrajectoryRecords;
    loadedTrajectoryRecords.reserve( recordCount );
    uint64_t totalPointCount = 0;

    for ( uint32_t recordIndex = 0; recordIndex < recordCount; ++recordIndex )
    {
        ReplayTrajectoryRecord record;
        uint8_t lane = 0;
        uint32_t publishedPointCount = 0;
        uint32_t pointCount = 0;

        if ( !reader.Scalar( record.key.bodyId.value ) || !reader.Scalar( lane ) ||
             lane > static_cast<uint8_t>( ReplayTrajectoryLane::BaselineRoot ) ||
             !reader.Scalar( record.key.branchOrdinal ) || !reader.Scalar( record.version ) ||
             !reader.Scalar( publishedPointCount ) || !reader.Scalar( record.styleId ) ||
             !reader.Scalar( record.parentId.value ) || !reader.Scalar( record.depth ) ||
             !reader.Scalar( record.firstFrame ) || !reader.Boolean( record.contactDerived ) ||
             !ReadBoundedCount( reader, REPLAY_PREDICTION_ARCHIVE_MAX_POINTS, pointCount ) )
        {
            WriteReason( outReason, reasonSize, "invalid trajectory record" );
            return false;
        }

        totalPointCount += pointCount;

        if ( totalPointCount > REPLAY_PREDICTION_ARCHIVE_MAX_POINTS || publishedPointCount > pointCount )
        {
            WriteReason( outReason, reasonSize, "trajectory point budget exceeded" );
            return false;
        }

        record.key.lane = static_cast<ReplayTrajectoryLane>( lane );
        record.publishedPointCount = publishedPointCount;
        record.points.resize( pointCount );

        for ( ReplayTrajectoryPoint& point : record.points )
        {
            if ( !reader.Scalar( point.frameIndex ) || !reader.Vector( point.position ) )
            {
                WriteReason( outReason, reasonSize, "truncated trajectory point" );
                return false;
            }
        }

        loadedTrajectoryRecords.push_back( std::move( record ) );
    }

    uint32_t rootFrameCount = 0;
    uint32_t childFrameCount = 0;
    uint32_t builtNodeCount = 0;

    if ( !reader.Scalar( prediction.trajectoryBuild.rootId.value ) ||
         !reader.Boolean( prediction.trajectoryBuild.usingBuildFrames ) || !reader.Scalar( rootFrameCount ) ||
         !reader.Scalar( childFrameCount ) || !reader.Scalar( builtNodeCount ) ||
         !reader.Scalar( prediction.trajectoryBuild.topologyVersion ) ||
         !reader.Boolean( prediction.trajectoryBuild.valid ) )
    {
        WriteReason( outReason, reasonSize, "truncated trajectory build state" );
        return false;
    }

    prediction.trajectoryBuild.rootFrameCount = rootFrameCount;
    prediction.trajectoryBuild.childFrameCount = childFrameCount;
    prediction.trajectoryBuild.builtNodeCount = builtNodeCount;
    const uint16_t activeRootBranch = prediction.trajectoryBuild.usingBuildFrames ? REPLAY_TRAJECTORY_BUILD_BRANCH
                                                                                  : REPLAY_TRAJECTORY_COMMITTED_BRANCH;

    for ( const ReplayTrajectoryRecord& record : loadedTrajectoryRecords )
    {
        if ( record.key.lane != ReplayTrajectoryLane::FutureRoot || record.key.branchOrdinal != activeRootBranch ||
             record.key.bodyId.value == prediction.trajectoryBuild.rootId.value )
        {
            continue;
        }

        // Backward compatibility: schemas 2 and 3 predate an explicit policy
        // field. Their additional body-keyed FutureRoot records are durable
        // wire evidence that the archived publication used all-body space mode.
        prediction.trajectoryBuild.pathPresentation = ReplayPredictionPathPresentation::AllBodiesSpace;
        ++prediction.trajectoryBuild.builtAllBodyCount;
        prediction.trajectoryBuild.allBodyFrameCount = (std::max)( prediction.trajectoryBuild.allBodyFrameCount,
                                                                   record.publishedPointCount );
    }

    if ( ReplayPredictionPathPresentationShowsAllBodies( prediction.trajectoryBuild.pathPresentation ) )
    {
        ++prediction.trajectoryBuild.builtAllBodyCount; // The selected root owns the canonical root record.
        prediction.trajectoryBuild.allBodyBodyCount = prediction.trajectoryBuild.builtAllBodyCount;
    }

    if ( !ReplayPredictionArchivePathPresentationMatchesRecords( prediction ) )
    {
        WriteReason( outReason, reasonSize, "prediction archive path presentation mismatch" );
        return false;
    }

    ReplayPredictionBaselineSnapshot& baseline = prediction.baseline;
    uint32_t baselinePointCount = 0;
    uint32_t baselinePoseCount = 0;

    if ( !reader.Boolean( baseline.valid ) || !reader.Boolean( baseline.comparisonActive ) ||
         !reader.Scalar( baseline.rootId.value ) || !reader.Scalar( baseline.rootModelRow.value ) ||
         !reader.Scalar( baseline.lastFrame ) || !reader.Boolean( baseline.divergenceValid ) ||
         !reader.Float( baseline.divergenceUnits ) ||
         !ReadBoundedCount( reader, REPLAY_PREDICTION_BASELINE_ROOT_POINT_CAPACITY, baselinePointCount ) )
    {
        WriteReason( outReason, reasonSize, "invalid prediction baseline" );
        return false;
    }

    baseline.rootPolyline.resize( baselinePointCount );

    for ( ReplayPredictionBaselineRootPoint& point : baseline.rootPolyline )
    {
        if ( !reader.Scalar( point.frameIndex ) || !reader.Vector( point.position ) )
        {
            WriteReason( outReason, reasonSize, "truncated baseline point" );
            return false;
        }
    }

    if ( !ReadBoundedCount( reader, REPLAY_PREDICTION_MARKER_CAPACITY, baselinePoseCount ) )
    {
        WriteReason( outReason, reasonSize, "invalid baseline pose count" );
        return false;
    }

    baseline.bodyPoses.resize( baselinePoseCount );

    for ( ReplayPredictionBaselineBodyPose& pose : baseline.bodyPoses )
    {
        if ( !ReadBaselinePose( reader, schema, pose ) )
        {
            WriteReason( outReason, reasonSize, "truncated baseline pose" );
            return false;
        }
    }

    if ( !reader.Finished() )
    {
        WriteReason( outReason, reasonSize, "prediction archive has trailing bytes" );
        return false;
    }

    prediction.trajectoryStore.ReplaceRecordsFromArchive( std::move( loadedTrajectoryRecords ) );

    pathVisualizer.hasTarget = archivedHasTarget;
    pathVisualizer.pastPathVisible = archivedPastPathVisible;
    prediction.enabled = true;
    prediction.ragdollVisualsEnabled = ragdollVisuals;
    prediction.build.dirty = false;
    prediction.build.pendingLatestRestart = false;
    prediction.velocityDragPreview.Clear();
    prediction.build.building = false;
    prediction.build.complete = true;
    prediction.ResetBuildFramePublication();
    prediction.simulation.committedFrameCount = prediction.simulation.frames.size();
    prediction.futureNodeCache.futureNodesBuiltFrameCount = prediction.CommittedFrameCount();
    prediction.futureNodeCache.futureNodesBuiltContactIndex = 0;
    prediction.futureNodeCache.futureNodesBuiltTargetId = prediction.simulation.targetId;
    prediction.futureNodeCache.futureNodesBuiltFromBuildFrames = false;
    prediction.futureNodeCache.futureNodesCacheValid = true;
    prediction.revealClock.deterministicFrameEnabled = true;
    prediction.revealClock.deterministicFrame = 0;
    prediction.revealClock.presentedFrame = 0;
    prediction.revealClock.anchorValid = false;
    return true;
}

namespace
{
enum class ReplayPredictionArchiveSection : uint32_t
{
    Lightweight = 1u,
    SolverEvidence = 2u
};

constexpr uint32_t REPLAY_PREDICTION_ARCHIVE_SECTION_COUNT_LOW = 1u;
constexpr uint32_t REPLAY_PREDICTION_ARCHIVE_SECTION_COUNT_HIGH = 2u;

constexpr uint32_t REPLAY_PREDICTION_ARCHIVE_MAX_EVENT_FRAMES = REPLAY_PREDICTION_MARKER_CAPACITY;
constexpr uint32_t REPLAY_PREDICTION_ARCHIVE_MAX_EVENT_CONTACTS = 1000000u;
constexpr uint32_t REPLAY_PREDICTION_ARCHIVE_MAX_EVENT_PIPELINE_ROWS = 4000000u;
constexpr uint64_t REPLAY_PREDICTION_ARCHIVE_HEADER_BYTES = 24u;
constexpr uint64_t REPLAY_PREDICTION_ARCHIVE_SECTION_DESCRIPTOR_BYTES = 24u;

struct ReplayPredictionArchiveSectionDescriptor
{
    ReplayPredictionArchiveSection kind = ReplayPredictionArchiveSection::Lightweight;
    uint32_t count = 0;
    uint64_t offset = 0;
    uint64_t size = 0;
};

bool AddBounded( uint64_t left, uint64_t right, uint64_t maximum, uint64_t& out ) noexcept
{
    if ( left > maximum || right > maximum - left )
    {
        return false;
    }

    out = left + right;
    return true;
}

void WriteContact( ArchiveWriter& writer, const Physics::PhysicsSolverPersistentContactSample& contact )
{
    writer.Scalar( contact.bodyA );
    writer.Scalar( contact.bodyB );
    writer.Scalar( contact.featureId );
    writer.Scalar( contact.key );
    writer.Vector( contact.normal );
    writer.Vector( contact.tangent1 );
    writer.Vector( contact.tangent2 );
    writer.Vector( contact.rA );
    writer.Vector( contact.rB );
    writer.Float( contact.penetration );
    writer.Float( contact.normalMass );
    writer.Float( contact.tangentMass1 );
    writer.Float( contact.tangentMass2 );
    writer.Float( contact.bias );
    writer.Float( contact.frictionLimit );
    writer.Float( contact.accN );
    writer.Float( contact.accT1 );
    writer.Float( contact.accT2 );
    writer.Boolean( contact.warmStarted );
    writer.Boolean( contact.isTerrain );
    writer.Boolean( contact.supportsRestingPolicy );
    writer.Boolean( contact.allowsTangentFriction );
    writer.Boolean( contact.normalCoupledFriction );
    writer.Boolean( contact.inhibitsSleep );
    writer.Scalar( contact.manifoldPointCount );
    writer.Vector( contact.terrainNormal );
    writer.Float( contact.terrainWarmStart );
}

bool ReadContact( ArchiveReader& reader, Physics::PhysicsSolverPersistentContactSample& contact )
{
    return reader.Scalar( contact.bodyA ) && reader.Scalar( contact.bodyB ) && reader.Scalar( contact.featureId ) &&
           reader.Scalar( contact.key ) && reader.Vector( contact.normal ) && reader.Vector( contact.tangent1 ) &&
           reader.Vector( contact.tangent2 ) && reader.Vector( contact.rA ) && reader.Vector( contact.rB ) &&
           reader.Float( contact.penetration ) && reader.Float( contact.normalMass ) &&
           reader.Float( contact.tangentMass1 ) && reader.Float( contact.tangentMass2 ) && reader.Float( contact.bias ) &&
           reader.Float( contact.frictionLimit ) && reader.Float( contact.accN ) && reader.Float( contact.accT1 ) &&
           reader.Float( contact.accT2 ) && reader.Boolean( contact.warmStarted ) && reader.Boolean( contact.isTerrain ) &&
           reader.Boolean( contact.supportsRestingPolicy ) && reader.Boolean( contact.allowsTangentFriction ) &&
           reader.Boolean( contact.normalCoupledFriction ) && reader.Boolean( contact.inhibitsSleep ) &&
           reader.Scalar( contact.manifoldPointCount ) && reader.Vector( contact.terrainNormal ) &&
           reader.Float( contact.terrainWarmStart );
}

void WritePipeline( ArchiveWriter& writer, const Physics::PhysicsPipelineRecord& record )
{
    writer.Scalar( static_cast<uint8_t>( record.stage ) );
    writer.Scalar( record.bodyA );
    writer.Scalar( record.bodyB );
    writer.Scalar( record.iteration );
    writer.Scalar( record.featureId );
    writer.Vector( record.point );
    writer.Vector( record.normal );
    writer.Float( record.scalarA );
    writer.Float( record.scalarB );
    writer.Float( record.scalarC );
}

bool ReadPipeline( ArchiveReader& reader, Physics::PhysicsPipelineRecord& record )
{
    uint8_t stage = 0;

    if ( !reader.Scalar( stage ) || stage >= static_cast<uint8_t>( Physics::PhysicsPipelineStage::Count ) ||
         !reader.Scalar( record.bodyA ) || !reader.Scalar( record.bodyB ) || !reader.Scalar( record.iteration ) ||
         !reader.Scalar( record.featureId ) || !reader.Vector( record.point ) || !reader.Vector( record.normal ) ||
         !reader.Float( record.scalarA ) || !reader.Float( record.scalarB ) || !reader.Float( record.scalarC ) )
    {
        return false;
    }

    record.stage = static_cast<Physics::PhysicsPipelineStage>( stage );
    return true;
}

std::size_t CollectRequiredEvidenceFrames( const RunReplayPredictionState& prediction,
                                           std::array<ReplayFrameIndex, REPLAY_PREDICTION_ARCHIVE_MAX_EVENT_FRAMES>& out )
{
    std::size_t count = 1u;
    out[0] = 0u;
    const ReplayPredictionPresentationView presentation = ReplayPrediction::PresentationViewFromState( prediction, false );

    for ( const RunReplayPathTraceNode& node : presentation.topology.futureNodes )
    {
        if ( !node.contactDerived )
        {
            continue;
        }

        const ReplayFrameIndex frame = node.firstFrame;
        const auto end = out.begin() + static_cast<std::ptrdiff_t>( count );

        if ( std::find( out.begin(), end, frame ) == end )
        {
            if ( count >= out.size() )
            {
                return 0u;
            }

            out[count++] = frame;
        }
    }

    std::sort( out.begin(), out.begin() + static_cast<std::ptrdiff_t>( count ) );
    return count;
}

const ReplayPredictionSolverEvidenceFrame* FindEvidenceFrameByNumber( const ReplayPredictionSolverEvidenceStore& evidence,
                                                                      ReplayFrameIndex frame ) noexcept
{
    for ( std::size_t index = evidence.PublishedFrameCount(); index > 0u; --index )
    {
        const ReplayPredictionSolverEvidenceFrame* candidate = evidence.PublishedFrame( index - 1u );

        if ( candidate && candidate->complete && candidate->identity.frame == frame )
        {
            return candidate;
        }
    }

    return nullptr;
}

bool BuildEvidenceSection( const RunReplayPredictionState& prediction, const ReplayPredictionSolverEvidenceStore& evidence,
                           std::vector<uint8_t>& outBytes, uint32_t& outEventCount )
{
    std::array<ReplayFrameIndex, REPLAY_PREDICTION_ARCHIVE_MAX_EVENT_FRAMES> requiredFrames = {};
    const std::size_t eventCount = CollectRequiredEvidenceFrames( prediction, requiredFrames );

    if ( eventCount == 0u || evidence.Mode() != ReplayPredictionDetailMode::High )
    {
        return false;
    }

    ArchiveWriter writer;
    writer.Scalar( static_cast<uint32_t>( eventCount ) );
    uint64_t totalContacts = 0;
    uint64_t totalPipeline = 0;

    for ( std::size_t eventIndex = 0; eventIndex < eventCount; ++eventIndex )
    {
        const ReplayPredictionSolverEvidenceFrame* frame = FindEvidenceFrameByNumber( evidence, requiredFrames[eventIndex] );

        if ( !frame || frame->identity.generation != prediction.build.generationBeginCount ||
             frame->identity.mode != ReplayPredictionDetailMode::High || frame->identity.bankEpoch != evidence.BankEpoch() )
        {
            return false;
        }

        // Invariant: topology and publication stamps are process-local
        // invalidation tokens captured when each worker frame seals, while the
        // topology is still growing. The durable identity is this committed
        // bank/generation/frame tuple. Load assigns one new canonical topology
        // and publication stamp to the reconstructed committed bank.

        totalContacts += frame->contacts.count;
        totalPipeline += frame->pipeline.count;

        if ( totalContacts > REPLAY_PREDICTION_ARCHIVE_MAX_EVENT_CONTACTS ||
             totalPipeline > REPLAY_PREDICTION_ARCHIVE_MAX_EVENT_PIPELINE_ROWS )
        {
            return false;
        }

        writer.Scalar( frame->identity.frame );
        writer.Scalar( frame->contacts.count );
        writer.Scalar( frame->pipeline.count );

        for ( std::size_t index = 0; index < frame->contacts.count; ++index )
        {
            const Physics::PhysicsSolverPersistentContactSample* contact = evidence.Contact( frame->contacts, index );

            if ( !contact )
            {
                return false;
            }

            WriteContact( writer, *contact );
        }

        for ( std::size_t index = 0; index < frame->pipeline.count; ++index )
        {
            const Physics::PhysicsPipelineRecord* record = evidence.Pipeline( frame->pipeline, index );

            if ( !record )
            {
                return false;
            }

            WritePipeline( writer, *record );
        }
    }

    outEventCount = static_cast<uint32_t>( eventCount );
    const bool valid = writer.Valid();
    outBytes = writer.Finish();
    return valid && !outBytes.empty();
}

bool BodyIndexValidForFrame( int bodyIndex, std::size_t bodyCount ) noexcept
{
    return bodyIndex >= -1 && ( bodyIndex < 0 || static_cast<std::size_t>( bodyIndex ) < bodyCount );
}

std::size_t BodyCountForFrame( const RunReplayPredictionState& prediction, ReplayFrameIndex frame ) noexcept
{
    for ( const RunReplayPredictionFrame& candidate : prediction.CommittedFrames() )
    {
        if ( candidate.frameIndex == frame )
        {
            return candidate.bodies.size();
        }
    }

    return 0u;
}

bool ValidateEvidenceRows( std::span<const Physics::PhysicsSolverPersistentContactSample> contacts,
                           std::span<const Physics::PhysicsPipelineRecord> pipeline, std::size_t bodyCount ) noexcept
{
    if ( bodyCount == 0u )
    {
        return false;
    }

    for ( const Physics::PhysicsSolverPersistentContactSample& contact : contacts )
    {
        if ( !BodyIndexValidForFrame( contact.bodyA, bodyCount ) || !BodyIndexValidForFrame( contact.bodyB, bodyCount ) ||
             contact.bodyA < 0 || contact.manifoldPointCount == 0u )
        {
            return false;
        }
    }

    for ( const Physics::PhysicsPipelineRecord& record : pipeline )
    {
        if ( !BodyIndexValidForFrame( record.bodyA, bodyCount ) || !BodyIndexValidForFrame( record.bodyB, bodyCount ) ||
             !std::isfinite( record.scalarA ) || !std::isfinite( record.scalarB ) || !std::isfinite( record.scalarC ) )
        {
            return false;
        }
    }

    return true;
}

bool ParseEvidenceSection( std::span<const uint8_t> bytes, const RunReplayPredictionState& prediction,
                           ReplayPredictionDetailMode activePreference, ReplayPredictionSolverEvidenceBanks& outEvidence,
                           char* outReason, std::size_t reasonSize )
{
    ArchiveReader reader( bytes );
    uint32_t eventCount = 0;
    std::array<ReplayFrameIndex, REPLAY_PREDICTION_ARCHIVE_MAX_EVENT_FRAMES> requiredFrames = {};
    const std::size_t requiredCount = CollectRequiredEvidenceFrames( prediction, requiredFrames );

    if ( requiredCount == 0u || !ReadBoundedCount( reader, REPLAY_PREDICTION_ARCHIVE_MAX_EVENT_FRAMES, eventCount ) ||
         eventCount != requiredCount )
    {
        WriteReason( outReason, reasonSize, "prediction evidence event-frame closure mismatch" );
        return false;
    }

    if ( activePreference == ReplayPredictionDetailMode::High )
    {
        outEvidence.BeginBuild( prediction.build.generationBeginCount, ReplayPredictionDetailMode::High );
    }

    uint64_t totalContacts = 0;
    uint64_t totalPipeline = 0;

    for ( uint32_t eventIndex = 0; eventIndex < eventCount; ++eventIndex )
    {
        ReplayFrameIndex frame = 0;
        uint32_t contactCount = 0;
        uint32_t pipelineCount = 0;

        if ( !reader.Scalar( frame ) || frame != requiredFrames[eventIndex] ||
             !ReadBoundedCount( reader, REPLAY_PREDICTION_ARCHIVE_MAX_EVENT_CONTACTS, contactCount ) ||
             !ReadBoundedCount( reader, REPLAY_PREDICTION_ARCHIVE_MAX_EVENT_PIPELINE_ROWS, pipelineCount ) )
        {
            WriteReason( outReason, reasonSize, "invalid prediction evidence frame header" );
            return false;
        }

        totalContacts += contactCount;
        totalPipeline += pipelineCount;

        if ( totalContacts > REPLAY_PREDICTION_ARCHIVE_MAX_EVENT_CONTACTS ||
             totalPipeline > REPLAY_PREDICTION_ARCHIVE_MAX_EVENT_PIPELINE_ROWS )
        {
            WriteReason( outReason, reasonSize, "prediction evidence row budget exceeded" );
            return false;
        }

        std::vector<Physics::PhysicsSolverPersistentContactSample> contacts;
        std::vector<Physics::PhysicsPipelineRecord> pipeline;

        if ( !ReplayPredictionReserveOperations::ReserveReplayPredictionVector( contacts, contactCount,
                                                                                static_cast<int>( frame ),
                                                                                "ReplayPredictionArchive.contactStaging" ) ||
             !ReplayPredictionReserveOperations::ReserveReplayPredictionVector( pipeline, pipelineCount,
                                                                                static_cast<int>( frame ),
                                                                                "ReplayPredictionArchive.pipelineStaging" ) )
        {
            WriteReason( outReason, reasonSize, "prediction archive staging reserve denied" );
            return false;
        }

        contacts.resize( contactCount );
        pipeline.resize( pipelineCount );

        for ( Physics::PhysicsSolverPersistentContactSample& contact : contacts )
        {
            if ( !ReadContact( reader, contact ) )
            {
                WriteReason( outReason, reasonSize, "truncated prediction evidence contact" );
                return false;
            }
        }

        for ( Physics::PhysicsPipelineRecord& record : pipeline )
        {
            if ( !ReadPipeline( reader, record ) )
            {
                WriteReason( outReason, reasonSize, "truncated prediction evidence pipeline" );
                return false;
            }
        }

        if ( !ValidateEvidenceRows( contacts, pipeline, BodyCountForFrame( prediction, frame ) ) )
        {
            WriteReason( outReason, reasonSize, "prediction evidence referential closure failed" );
            return false;
        }

        if ( activePreference == ReplayPredictionDetailMode::High &&
             ( !outEvidence.ReserveBuild( eventIndex + 1u, static_cast<std::size_t>( totalContacts ),
                                          static_cast<std::size_t>( totalPipeline ), static_cast<int>( frame ) ) ||
               !outEvidence.AppendBuildFrame( frame, prediction.futureNodeCache.futureNodesTopologyVersion,
                                              prediction.trajectoryStore.publicationVersion, contacts, pipeline,
                                              static_cast<int>( frame ) ) ) )
        {
            WriteReason( outReason, reasonSize, "prediction evidence bank reserve denied" );
            return false;
        }
    }

    if ( !reader.Finished() )
    {
        WriteReason( outReason, reasonSize, "prediction evidence section has trailing bytes" );
        return false;
    }

    if ( activePreference == ReplayPredictionDetailMode::High && !outEvidence.PromoteBuild() )
    {
        WriteReason( outReason, reasonSize, "prediction evidence bank promotion failed" );
        return false;
    }

    return true;
}

void CommitArchivePayload( RunReplayPathVisualizerState& destinationPath, RunReplayPredictionState& destinationPrediction,
                           ReplayPredictionSolverEvidenceBanks& destinationEvidence,
                           RunReplayPathVisualizerState& candidatePath, RunReplayPredictionState& candidatePrediction,
                           ReplayPredictionSolverEvidenceBanks& candidateEvidence ) noexcept
{
    using std::swap;
    destinationPath.hasTarget = candidatePath.hasTarget;
    destinationPath.pastPathVisible = candidatePath.pastPathVisible;
    destinationPath.targetId = candidatePath.targetId;
    destinationPath.targetModelRow = candidatePath.targetModelRow;
    std::memcpy( destinationPath.targetName, candidatePath.targetName, sizeof( destinationPath.targetName ) );
    destinationPath.targets.clear();
    destinationPath.pastTrajectory = {};

    destinationPrediction.enabled = candidatePrediction.enabled;
    destinationPrediction.ragdollVisualsEnabled = candidatePrediction.ragdollVisualsEnabled;
    destinationPrediction.build.dirty = false;
    destinationPrediction.build.pendingLatestRestart = false;
    destinationPrediction.build.building = false;
    destinationPrediction.build.complete = true;
    destinationPrediction.build.generationBeginCount = candidatePrediction.build.generationBeginCount;
    destinationPrediction.build.buildFrames.clear();
    destinationPrediction.ResetBuildFramePublication();

    destinationPrediction.simulation.horizonSeconds = candidatePrediction.simulation.horizonSeconds;
    destinationPrediction.simulation.targetModelRow = candidatePrediction.simulation.targetModelRow;
    destinationPrediction.simulation.targetId = candidatePrediction.simulation.targetId;
    destinationPrediction.simulation.sourceFrameIndex = candidatePrediction.simulation.sourceFrameIndex;
    destinationPrediction.simulation.sourceSolverHash = candidatePrediction.simulation.sourceSolverHash;
    destinationPrediction.simulation.sourceSimulationSeconds = candidatePrediction.simulation.sourceSimulationSeconds;
    destinationPrediction.simulation.predictionEngine.swap( candidatePrediction.simulation.predictionEngine );
    destinationPrediction.simulation.predictionEngineReserveBytes = 0;
    destinationPrediction.simulation.predictionEngineReady = false;
    swap( destinationPrediction.simulation.predictionWorld, candidatePrediction.simulation.predictionWorld );
    destinationPrediction.simulation.predictionBodies.swap( candidatePrediction.simulation.predictionBodies );
    destinationPrediction.simulation.measuredTicksPerMs.store( 0.0, std::memory_order_release );
    destinationPrediction.simulation.probeElapsedMs = 0.0;
    destinationPrediction.simulation.probeTicksCompleted = 0;
    destinationPrediction.simulation.calibratedModelCount = -1;
    destinationPrediction.simulation.frames.swap( candidatePrediction.simulation.frames );
    destinationPrediction.simulation.committedFrameCount = candidatePrediction.simulation.committedFrameCount;

    destinationPrediction.futureNodeCache.futureNodes.swap( candidatePrediction.futureNodeCache.futureNodes );
    destinationPrediction.futureNodeCache.futureNodeBuildScratch.clear();
    destinationPrediction.futureNodeCache.futureNodesBuiltFrameCount = candidatePrediction.futureNodeCache
                                                                           .futureNodesBuiltFrameCount;
    destinationPrediction.futureNodeCache.futureNodesBuiltContactIndex = 0u;
    destinationPrediction.futureNodeCache.futureNodesBuiltTargetId = candidatePrediction.futureNodeCache
                                                                         .futureNodesBuiltTargetId;
    destinationPrediction.futureNodeCache.futureNodesTopologyVersion = candidatePrediction.futureNodeCache
                                                                           .futureNodesTopologyVersion;
    destinationPrediction.futureNodeCache.nextFutureNodesTopologyVersion = candidatePrediction.futureNodeCache
                                                                               .nextFutureNodesTopologyVersion;
    destinationPrediction.futureNodeCache.futureNodesBuiltRagdollVisuals = candidatePrediction.futureNodeCache
                                                                               .futureNodesBuiltRagdollVisuals;
    destinationPrediction.futureNodeCache.futureNodesBuiltFromBuildFrames = false;
    destinationPrediction.futureNodeCache.futureNodesCacheValid = true;
    destinationPrediction.futureNodeCache.retainedMarkers = candidatePrediction.futureNodeCache.retainedMarkers;
    destinationPrediction.futureNodeCache.retainedMarkerCount = candidatePrediction.futureNodeCache.retainedMarkerCount;
    destinationPrediction.futureNodeCache.retainedMarkersVersion = candidatePrediction.futureNodeCache
                                                                       .retainedMarkersVersion;
    destinationPrediction.futureNodeCache.nextRetainedMarkersVersion = candidatePrediction.futureNodeCache
                                                                           .nextRetainedMarkersVersion;
    destinationPrediction.futureNodeCache.childMarkerScan.Reset();

    swap( destinationPrediction.trajectoryStore, candidatePrediction.trajectoryStore );
    destinationPrediction.trajectoryBuild = candidatePrediction.trajectoryBuild;
    destinationPrediction.committedPublication.Reset();
    destinationPrediction.archivePresentationRestored = true;
    destinationPrediction.baseline = std::move( candidatePrediction.baseline );
    destinationPrediction.velocityDragPreview.Clear();
    destinationPrediction.revealClock = candidatePrediction.revealClock;
    destinationEvidence.SwapArchiveState( candidateEvidence );
}

bool AllocateArchiveCandidates( std::unique_ptr<RunReplayPredictionState>& prediction,
                                std::unique_ptr<ReplayPredictionSolverEvidenceBanks>& evidence )
{
    const uint64_t
        requestedBytes = ReplayPredictionArchiveOperations::ReplayPredictionArchiveCandidateAllocationBudgetBytes();
    Core::Allocation::RuntimeReserveGrowthResult result = {};

    if ( requestedBytes > static_cast<uint64_t>( ( std::numeric_limits<int>::max )() ) ||
         !ReplayPredictionReserveOperations::
             RequestReplayPredictionReserveGrowth( "ReplayPredictionArchive.transactionStaging", -1, 0,
                                                   static_cast<int>( requestedBytes ), 1, result, requestedBytes ) )
    {
        return false;
    }

    const Core::Allocation::RuntimeReserveOwnerHandle
        owner = ReplayPredictionReserveOperations::ReplayPredictionReserveOwner();
    Core::Allocation::RuntimeReserveAllocationScope allocationScope( owner, Core::Allocation::RuntimeReservePhase::Replay,
                                                                     result );
    prediction = std::make_unique<RunReplayPredictionState>();
    evidence = std::make_unique<ReplayPredictionSolverEvidenceBanks>();
    return prediction && evidence;
}

bool ParseCurrentArchiveHeader( std::span<const uint8_t> bytes, ReplayPredictionArchiveDetailCapability& capability,
                                ReplayPredictionPathPresentation& pathPresentation,
                                std::array<ReplayPredictionArchiveSectionDescriptor, 2>& sections, uint32_t& sectionCount,
                                char* outReason, std::size_t reasonSize )
{
    ArchiveReader reader( bytes );
    uint32_t magic = 0;
    uint32_t schema = 0;
    uint8_t encodedCapability = 0;
    uint8_t encodedPresentation = 0;
    uint16_t reserved = 0;
    uint64_t totalBytes = 0;

    if ( !reader.Scalar( magic ) || !reader.Scalar( schema ) || magic != REPLAY_PREDICTION_ARCHIVE_MAGIC ||
         !IsSectionedPredictionArchiveSchema( schema ) || !reader.Scalar( encodedCapability ) ||
         encodedCapability > static_cast<uint8_t>( ReplayPredictionArchiveDetailCapability::High ) ||
         !reader.Scalar( encodedPresentation ) ||
         encodedPresentation > static_cast<uint8_t>( ReplayPredictionPathPresentation::AllBodiesSpace ) ||
         !reader.Scalar( reserved ) || reserved != 0u || !reader.Scalar( sectionCount ) ||
         ( sectionCount != REPLAY_PREDICTION_ARCHIVE_SECTION_COUNT_LOW &&
           sectionCount != REPLAY_PREDICTION_ARCHIVE_SECTION_COUNT_HIGH ) ||
         !reader.Scalar( totalBytes ) || totalBytes != bytes.size() )
    {
        WriteReason( outReason, reasonSize, "invalid prediction archive header" );
        return false;
    }

    capability = static_cast<ReplayPredictionArchiveDetailCapability>( encodedCapability );
    pathPresentation = static_cast<ReplayPredictionPathPresentation>( encodedPresentation );

    if ( ( capability == ReplayPredictionArchiveDetailCapability::Low &&
           sectionCount != REPLAY_PREDICTION_ARCHIVE_SECTION_COUNT_LOW ) ||
         ( capability == ReplayPredictionArchiveDetailCapability::High &&
           sectionCount != REPLAY_PREDICTION_ARCHIVE_SECTION_COUNT_HIGH ) )
    {
        WriteReason( outReason, reasonSize, "prediction archive capability/section mismatch" );
        return false;
    }

    for ( uint32_t index = 0; index < sectionCount; ++index )
    {
        uint32_t kind = 0;
        uint32_t count = 0;
        uint64_t offset = 0;
        uint64_t size = 0;

        if ( !reader.Scalar( kind ) || !reader.Scalar( count ) || !reader.Scalar( offset ) || !reader.Scalar( size ) )
        {
            WriteReason( outReason, reasonSize, "truncated prediction archive section table" );
            return false;
        }

        sections[index] = { static_cast<ReplayPredictionArchiveSection>( kind ), count, offset, size };
    }

    uint64_t expectedOffset = REPLAY_PREDICTION_ARCHIVE_HEADER_BYTES +
                              static_cast<uint64_t>( sectionCount ) * REPLAY_PREDICTION_ARCHIVE_SECTION_DESCRIPTOR_BYTES;

    for ( uint32_t index = 0; index < sectionCount; ++index )
    {
        const ReplayPredictionArchiveSection expectedKind = index == 0u ? ReplayPredictionArchiveSection::Lightweight
                                                                        : ReplayPredictionArchiveSection::SolverEvidence;
        uint64_t end = 0;

        if ( sections[index].kind != expectedKind || sections[index].offset != expectedOffset ||
             sections[index].size == 0u || !AddBounded( sections[index].offset, sections[index].size, bytes.size(), end ) )
        {
            WriteReason( outReason, reasonSize, "invalid prediction archive section layout" );
            return false;
        }

        expectedOffset = end;
    }

    if ( expectedOffset != bytes.size() || sections[0].count != 1u ||
         ( sectionCount == 2u &&
           ( sections[1].count == 0u || sections[1].count > REPLAY_PREDICTION_ARCHIVE_MAX_EVENT_FRAMES ) ) )
    {
        WriteReason( outReason, reasonSize, "prediction archive section closure mismatch" );
        return false;
    }

    return true;
}
} // namespace

bool BuildReplayPredictionArchive( const RunReplayPathVisualizerState& pathVisualizer,
                                   const RunReplayPredictionState& prediction, ReplayPredictionDetailMode detailMode,
                                   const ReplayPredictionSolverEvidenceStore& evidence, std::vector<uint8_t>& outBytes )
{
    std::vector<uint8_t> legacyBytes;

    if ( !BuildLegacyReplayPredictionArchive( pathVisualizer, prediction, legacyBytes ) || legacyBytes.size() <= 8u )
    {
        return false;
    }

    const bool highCapability = detailMode == ReplayPredictionDetailMode::High && evidence.PublishedFrameCount() > 0u;
    std::vector<uint8_t> evidenceBytes;
    uint32_t eventCount = 0;

    if ( highCapability && !BuildEvidenceSection( prediction, evidence, evidenceBytes, eventCount ) )
    {
        return false;
    }

    const uint32_t sectionCount = highCapability ? REPLAY_PREDICTION_ARCHIVE_SECTION_COUNT_HIGH
                                                 : REPLAY_PREDICTION_ARCHIVE_SECTION_COUNT_LOW;
    const uint64_t firstOffset = REPLAY_PREDICTION_ARCHIVE_HEADER_BYTES +
                                 static_cast<uint64_t>( sectionCount ) * REPLAY_PREDICTION_ARCHIVE_SECTION_DESCRIPTOR_BYTES;
    const uint64_t lightweightSize = legacyBytes.size() - 8u;
    uint64_t evidenceOffset = 0;
    uint64_t totalBytes = 0;

    if ( !AddBounded( firstOffset, lightweightSize, REPLAY_PREDICTION_ARCHIVE_MAX_BYTES, evidenceOffset ) ||
         !AddBounded( evidenceOffset, evidenceBytes.size(), REPLAY_PREDICTION_ARCHIVE_MAX_BYTES, totalBytes ) )
    {
        return false;
    }

    ArchiveWriter writer( REPLAY_PREDICTION_ARCHIVE_SCHEMA );
    const ReplayPredictionPresentationView presentation = ReplayPrediction::PresentationViewFromState( prediction, false );
    writer.Scalar( REPLAY_PREDICTION_ARCHIVE_MAGIC );
    writer.Scalar( REPLAY_PREDICTION_ARCHIVE_SCHEMA );
    writer.Scalar( static_cast<uint8_t>( highCapability ? ReplayPredictionArchiveDetailCapability::High
                                                        : ReplayPredictionArchiveDetailCapability::Low ) );
    writer.Scalar( static_cast<uint8_t>( presentation.pathPresentation ) );
    writer.Scalar( static_cast<uint16_t>( 0u ) );
    writer.Scalar( sectionCount );
    writer.Scalar( totalBytes );
    writer.Scalar( static_cast<uint32_t>( ReplayPredictionArchiveSection::Lightweight ) );
    writer.Scalar( static_cast<uint32_t>( 1u ) );
    writer.Scalar( firstOffset );
    writer.Scalar( lightweightSize );

    if ( highCapability )
    {
        writer.Scalar( static_cast<uint32_t>( ReplayPredictionArchiveSection::SolverEvidence ) );
        writer.Scalar( eventCount );
        writer.Scalar( evidenceOffset );
        writer.Scalar( static_cast<uint64_t>( evidenceBytes.size() ) );
    }

    writer.Bytes( std::span<const uint8_t>( legacyBytes ).subspan( 8u ) );
    writer.Bytes( evidenceBytes );
    const bool valid = writer.Valid() && writer.Size() == totalBytes;
    outBytes = writer.Finish();
    return valid && !outBytes.empty();
}

bool LoadReplayPredictionArchive( std::span<const uint8_t> bytes, RunReplayPathVisualizerState& pathVisualizer,
                                  RunReplayPredictionState& prediction, ReplayPredictionSolverEvidenceBanks& evidence,
                                  ReplayPredictionDetailMode activePreference,
                                  ReplayPredictionArchiveDetailCapability& outCapturedCapability, char* outReason,
                                  std::size_t reasonSize )
{
    if ( bytes.size() > REPLAY_PREDICTION_ARCHIVE_MAX_BYTES || bytes.size() < 8u || prediction.build.building )
    {
        WriteReason( outReason, reasonSize,
                     prediction.build.building ? "prediction archive load requires an idle owner"
                                               : "prediction archive exceeds byte cap" );
        return false;
    }

    uint32_t schema = static_cast<uint32_t>( bytes[4] ) | ( static_cast<uint32_t>( bytes[5] ) << 8u ) |
                      ( static_cast<uint32_t>( bytes[6] ) << 16u ) | ( static_cast<uint32_t>( bytes[7] ) << 24u );
    ReplayPredictionArchiveDetailCapability capability = ReplayPredictionArchiveDetailCapability::Low;
    ReplayPredictionPathPresentation pathPresentation = ReplayPredictionPathPresentation::SelectedCausalTree;
    std::array<ReplayPredictionArchiveSectionDescriptor, 2> sections = {};
    uint32_t sectionCount = 0;
    std::span<const uint8_t> lightweightBytes = bytes;
    std::span<const uint8_t> evidenceBytes;
    std::vector<uint8_t> reconstructedLegacy;

    if ( IsSectionedPredictionArchiveSchema( schema ) )
    {
        if ( !ParseCurrentArchiveHeader( bytes, capability, pathPresentation, sections, sectionCount, outReason,
                                         reasonSize ) )
        {
            return false;
        }

        const uint32_t lightweightSchema = schema == REPLAY_PREDICTION_ARCHIVE_HISTORICAL_SECTIONED_SCHEMA
                                               ? REPLAY_PREDICTION_ARCHIVE_HISTORICAL_LIGHTWEIGHT_SCHEMA
                                               : REPLAY_PREDICTION_ARCHIVE_PRECISE_LIGHTWEIGHT_SCHEMA;
        ArchiveWriter legacyWriter( lightweightSchema );
        legacyWriter.Scalar( REPLAY_PREDICTION_ARCHIVE_MAGIC );
        legacyWriter.Scalar( lightweightSchema );
        legacyWriter.Bytes(
            bytes.subspan( static_cast<std::size_t>( sections[0].offset ), static_cast<std::size_t>( sections[0].size ) ) );
        reconstructedLegacy = legacyWriter.Finish();
        lightweightBytes = reconstructedLegacy;

        if ( sectionCount == REPLAY_PREDICTION_ARCHIVE_SECTION_COUNT_HIGH )
        {
            evidenceBytes = bytes.subspan( static_cast<std::size_t>( sections[1].offset ),
                                           static_cast<std::size_t>( sections[1].size ) );
        }
    }
    else if ( !IsLightweightPredictionArchiveSchema( schema ) )
    {
        WriteReason( outReason, reasonSize, "invalid prediction archive header" );
        return false;
    }

    std::unique_ptr<RunReplayPredictionState> candidatePrediction;
    std::unique_ptr<ReplayPredictionSolverEvidenceBanks> candidateEvidence;

    if ( !AllocateArchiveCandidates( candidatePrediction, candidateEvidence ) )
    {
        WriteReason( outReason, reasonSize, "prediction archive transaction staging reserve denied" );
        return false;
    }

    RunReplayPathVisualizerState candidatePath;

    if ( !LoadLegacyReplayPredictionArchive( lightweightBytes, candidatePath, *candidatePrediction, outReason, reasonSize ) )
    {
        return false;
    }

    if ( IsSectionedPredictionArchiveSchema( schema ) )
    {
        if ( candidatePrediction->trajectoryBuild.pathPresentation != pathPresentation )
        {
            WriteReason( outReason, reasonSize, "prediction archive path presentation mismatch" );
            return false;
        }

        if ( capability == ReplayPredictionArchiveDetailCapability::High &&
             !ParseEvidenceSection( evidenceBytes, *candidatePrediction, activePreference, *candidateEvidence, outReason,
                                    reasonSize ) )
        {
            return false;
        }
    }

    CommitArchivePayload( pathVisualizer, prediction, evidence, candidatePath, *candidatePrediction, *candidateEvidence );
    outCapturedCapability = capability;
    return true;
}

} // namespace ReplayPredictionArchiveOperations
} // namespace SkullbonezCore::Runtime
