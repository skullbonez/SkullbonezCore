/*
File: SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.cpp
Purpose:
  Serializes and restores the presentation-bearing state of one completed prediction.

Summary:
  The writer captures typed frames, trajectory records, causal topology,
  retained marker poses, and butterfly baseline values. The reader validates
  all counts before allocating and restores a completed, non-generating future.

Glossary:
  Scalar codec: Explicit little-endian encoding for one integer or float field.
  Presentation cache: Derived prediction values consumed by overlay drawing.
  All-body bank: Additional body-keyed FutureRoot records used by space scenes.

Invariants:
  - Every vector count is checked against a presentation-specific hard limit.
  - The complete payload fails closed above 128 MiB.
  - Quaternion components retain their exact float bit patterns.
  - No deserialized value can create or schedule prediction physics work.

Related:
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.Automation.cpp
*/
#include "ReplayPredictionArchive.h"
#include "ReplayPrediction.h"

#include "../Replay/ReplayPathPackets.h"

#include <bit>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>

namespace SkullbonezCore::Runtime
{
namespace
{
constexpr uint32_t REPLAY_PREDICTION_ARCHIVE_MAGIC = 0x44505652u; // "RVPD"
constexpr uint32_t REPLAY_PREDICTION_ARCHIVE_SCHEMA = 2u;
constexpr uint16_t REPLAY_TRAJECTORY_COMMITTED_BRANCH = 0u;
constexpr uint16_t REPLAY_TRAJECTORY_BUILD_BRANCH = 1u;
constexpr uint32_t REPLAY_PREDICTION_ARCHIVE_MAX_FRAMES = 7201u;
constexpr uint32_t REPLAY_PREDICTION_ARCHIVE_MAX_BODIES = static_cast<uint32_t>( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
constexpr uint32_t REPLAY_PREDICTION_ARCHIVE_MAX_RECORDS = REPLAY_PREDICTION_MARKER_CAPACITY * 8u;
constexpr uint32_t REPLAY_PREDICTION_ARCHIVE_MAX_POINTS = 4000000u;
constexpr std::size_t REPLAY_PREDICTION_ARCHIVE_MAX_BYTES = 128u * 1024u * 1024u;

bool IsInactivePredictionWorkerBankRecord( const ReplayTrajectoryRecord& record ) noexcept
{
    const bool childLane = record.key.lane == ReplayTrajectoryLane::FutureChildIncoming ||
                           record.key.lane == ReplayTrajectoryLane::FutureChildOutgoing;

    return childLane && record.key.branchOrdinal >= REPLAY_VISUAL_FUTURE_NODE_CAPACITY;
}

uint32_t CountCanonicalTrajectoryVersions( const ReplayTrajectoryStore& store ) noexcept
{
    uint32_t count = 0;

    for ( const ReplayTrajectoryRecord& record : store.records )
    {

        if ( !IsInactivePredictionWorkerBankRecord( record ) )
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
        Float( x );
        Float( y );
        Float( z );
        Float( w );
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
    bool Quaternion( Math::Orientation::Quaternion& value )
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;

        if ( !Float( x ) || !Float( y ) || !Float( z ) || !Float( w ) )
        {
            return false;
        }

        value = Math::Orientation::Quaternion( x, y, z, w );
        return true;
    }
    bool Finished() const noexcept
    {
        return m_offset == m_bytes.size();
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

bool ReadBody( ArchiveReader& reader, RunReplayPredictionBodySample& body )
{
    return reader.Scalar( body.id.value ) && reader.Scalar( body.modelRow.value ) && reader.Vector( body.position ) &&
           reader.Quaternion( body.orientation ) && reader.Vector( body.linearVelocity ) && reader.Boolean( body.sleeping );
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

bool ReadMarker( ArchiveReader& reader, ReplayPredictionRetainedMarker& marker )
{
    return reader.Scalar( marker.id.value ) && reader.Scalar( marker.modelRow.value ) &&
           reader.Boolean( marker.hasEntryPose ) && reader.Boolean( marker.hasRestPose ) &&
           reader.Boolean( marker.hasHorizonPose ) && reader.Vector( marker.entryPosition ) &&
           reader.Quaternion( marker.entryOrientation ) && reader.Vector( marker.restPosition ) &&
           reader.Quaternion( marker.restOrientation ) && reader.Vector( marker.horizonPosition ) &&
           reader.Quaternion( marker.horizonOrientation );
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

bool ReadBaselinePose( ArchiveReader& reader, ReplayPredictionBaselineBodyPose& pose )
{
    return reader.Scalar( pose.id.value ) && reader.Scalar( pose.modelRow.value ) && reader.Boolean( pose.hasEntryPose ) &&
           reader.Boolean( pose.hasRestPose ) && reader.Vector( pose.entryPosition ) &&
           reader.Quaternion( pose.entryOrientation ) && reader.Vector( pose.restPosition ) &&
           reader.Quaternion( pose.restOrientation );
}

bool ReadBoundedCount( ArchiveReader& reader, uint32_t maximum, uint32_t& count )
{
    return reader.Scalar( count ) && count <= maximum;
}
} // namespace

namespace ReplayPredictionArchiveOperations
{
bool BuildReplayPredictionArchive( const RunReplayPathVisualizerState& pathVisualizer,
                                   const RunReplayPredictionState& prediction, std::vector<uint8_t>& outBytes )
{
    outBytes.clear();

    if ( prediction.build.building || !prediction.build.complete || prediction.simulation.frames.size() < 2u ||
         prediction.simulation.frames.size() > REPLAY_PREDICTION_ARCHIVE_MAX_FRAMES ||
         prediction.futureNodeCache.retainedMarkerCount > prediction.futureNodeCache.retainedMarkers.size() ||
         prediction.trajectoryStore.records.size() > REPLAY_PREDICTION_ARCHIVE_MAX_RECORDS )
    {
        return false;
    }

    ArchiveWriter writer;
    writer.Scalar( REPLAY_PREDICTION_ARCHIVE_MAGIC );
    writer.Scalar( REPLAY_PREDICTION_ARCHIVE_SCHEMA );
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

    writer.Scalar( static_cast<uint32_t>( prediction.simulation.frames.size() ) );

    for ( const RunReplayPredictionFrame& frame : prediction.simulation.frames )
    {

        if ( frame.bodies.size() > REPLAY_PREDICTION_ARCHIVE_MAX_BODIES )
        {
            return false;
        }

        writer.Scalar( frame.frameIndex );
        writer.Double( frame.simulationSeconds );
        writer.Float( frame.tornadoSystemElapsedSeconds );
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
    const uint32_t canonicalTopologyVersion = prediction.futureNodeCache.futureNodesTopologyVersion != 0u ? 1u : 0u;
    const uint32_t canonicalNextTopologyVersion = canonicalTopologyVersion != 0u ? 2u : 1u;
    writer.Scalar( canonicalTopologyVersion );
    writer.Scalar( canonicalNextTopologyVersion );
    writer.Boolean( prediction.futureNodeCache.futureNodesBuiltRagdollVisuals );
    writer.Scalar( static_cast<uint32_t>( prediction.futureNodeCache.futureNodes.size() ) );

    for ( const RunReplayPathTraceNode& node : prediction.futureNodeCache.futureNodes )
    {
        WriteNode( writer, node );
    }

    writer.Scalar( static_cast<uint32_t>( prediction.futureNodeCache.retainedMarkerCount ) );

    for ( std::size_t index = 0; index < prediction.futureNodeCache.retainedMarkerCount; ++index )
    {
        WriteMarker( writer, prediction.futureNodeCache.retainedMarkers[index] );
    }

    const uint32_t canonicalTrajectoryVersionCount = CountCanonicalTrajectoryVersions( prediction.trajectoryStore );
    writer.Scalar( canonicalTrajectoryVersionCount + 1u );
    writer.Scalar( static_cast<uint32_t>( prediction.trajectoryStore.records.size() ) );
    uint64_t totalPointCount = 0;
    uint32_t canonicalTrajectoryVersion = 1u;

    for ( const ReplayTrajectoryRecord& record : prediction.trajectoryStore.records )
    {
        totalPointCount += record.points.size();

        if ( totalPointCount > REPLAY_PREDICTION_ARCHIVE_MAX_POINTS || record.publishedPointCount > record.points.size() )
        {
            return false;
        }

        if ( IsInactivePredictionWorkerBankRecord( record ) )
        {

            // Hazard: this double-buffer bank is renderer-inactive after
            // completion, but its schedule-selected keys and point payloads
            // used to leak into the durable artifact. Keep one fixed-width
            // record slot while replacing all variable telemetry with an inert
            // constant; the reader layout and record-count contract stay intact.
            writer.Scalar( static_cast<uint32_t>( 0u ) );
            writer.Scalar( static_cast<uint8_t>( ReplayTrajectoryLane::FutureChildIncoming ) );
            writer.Scalar( static_cast<uint16_t>( REPLAY_VISUAL_FUTURE_NODE_CAPACITY ) );
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

    writer.Scalar( prediction.trajectoryBuild.rootId.value );
    writer.Boolean( prediction.trajectoryBuild.usingBuildFrames );
    writer.Scalar( static_cast<uint32_t>( prediction.trajectoryBuild.rootFrameCount ) );
    writer.Scalar( static_cast<uint32_t>( prediction.trajectoryBuild.childFrameCount ) );
    writer.Scalar( static_cast<uint32_t>( prediction.trajectoryBuild.builtNodeCount ) );

    // Invariant: the build-state token must equal the canonical future-node
    // token whenever the live tokens matched; offline reconstruction exercises
    // the same equality check without depending on the process-local number.
    const uint32_t canonicalBuildTopologyVersion = prediction.trajectoryBuild.topologyVersion ==
                                                           prediction.futureNodeCache.futureNodesTopologyVersion
                                                       ? canonicalTopologyVersion
                                                       : 0u;

    writer.Scalar( canonicalBuildTopologyVersion );
    writer.Boolean( prediction.trajectoryBuild.valid );

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

bool LoadReplayPredictionArchive( std::span<const uint8_t> bytes, RunReplayPathVisualizerState& pathVisualizer,
                                  RunReplayPredictionState& prediction, char* outReason, std::size_t reasonSize )
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
         schema != REPLAY_PREDICTION_ARCHIVE_SCHEMA || !reader.Boolean( archivedHasTarget ) ||
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

    RunReplayPredictionState& state = prediction;
    pathVisualizer.targets.clear();
    pathVisualizer.pastTrajectory = {};

    state.simulation.frames.clear();
    state.futureNodeCache.futureNodes.clear();
    state.trajectoryStore.records.clear();
    state.baseline.rootPolyline.clear();
    state.baseline.bodyPoses.clear();
    state.futureNodeCache.retainedMarkerCount = 0;

    if ( !reader.Float( state.simulation.horizonSeconds ) || !reader.Scalar( state.simulation.targetModelRow.value ) ||
         !reader.Scalar( state.simulation.targetId.value ) || !reader.Scalar( state.simulation.sourceFrameIndex ) ||
         !reader.Scalar( state.simulation.sourceSolverHash ) || !reader.Double( state.simulation.sourceSimulationSeconds ) ||
         !reader.Scalar( state.build.generationBeginCount ) )
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

    state.simulation.frames.reserve( frameCount );

    for ( uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex )
    {
        RunReplayPredictionFrame frame;
        uint32_t bodyCount = 0;

        if ( !reader.Scalar( frame.frameIndex ) || !reader.Double( frame.simulationSeconds ) ||
             !reader.Float( frame.tornadoSystemElapsedSeconds ) || !reader.Boolean( frame.contactsIncomplete ) ||
             !ReadBoundedCount( reader, REPLAY_PREDICTION_ARCHIVE_MAX_BODIES, bodyCount ) )
        {
            WriteReason( outReason, reasonSize, "truncated prediction frame" );
            return false;
        }

        frame.bodies.resize( bodyCount );

        for ( RunReplayPredictionBodySample& body : frame.bodies )
        {

            if ( !ReadBody( reader, body ) )
            {
                WriteReason( outReason, reasonSize, "truncated prediction body" );
                return false;
            }
        }

        state.simulation.frames.push_back( std::move( frame ) );
    }

    uint32_t nodeCount = 0;

    if ( !reader.Scalar( state.futureNodeCache.futureNodesTopologyVersion ) ||
         !reader.Scalar( state.futureNodeCache.nextFutureNodesTopologyVersion ) ||
         !reader.Boolean( state.futureNodeCache.futureNodesBuiltRagdollVisuals ) ||
         !ReadBoundedCount( reader, REPLAY_PREDICTION_MARKER_CAPACITY, nodeCount ) )
    {
        WriteReason( outReason, reasonSize, "invalid future-node block" );
        return false;
    }

    state.futureNodeCache.futureNodes.resize( nodeCount );

    for ( RunReplayPathTraceNode& node : state.futureNodeCache.futureNodes )
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

        if ( !ReadMarker( reader, state.futureNodeCache.retainedMarkers[markerIndex] ) )
        {
            WriteReason( outReason, reasonSize, "truncated retained marker" );
            return false;
        }
    }

    state.futureNodeCache.retainedMarkerCount = markerCount;

    uint32_t recordCount = 0;

    if ( !reader.Scalar( state.trajectoryStore.nextVersion ) ||
         !ReadBoundedCount( reader, REPLAY_PREDICTION_ARCHIVE_MAX_RECORDS, recordCount ) )
    {
        WriteReason( outReason, reasonSize, "invalid trajectory record count" );
        return false;
    }

    state.trajectoryStore.records.reserve( recordCount );
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

        state.trajectoryStore.records.push_back( std::move( record ) );
    }

    uint32_t rootFrameCount = 0;
    uint32_t childFrameCount = 0;
    uint32_t builtNodeCount = 0;

    if ( !reader.Scalar( state.trajectoryBuild.rootId.value ) || !reader.Boolean( state.trajectoryBuild.usingBuildFrames ) ||
         !reader.Scalar( rootFrameCount ) || !reader.Scalar( childFrameCount ) || !reader.Scalar( builtNodeCount ) ||
         !reader.Scalar( state.trajectoryBuild.topologyVersion ) || !reader.Boolean( state.trajectoryBuild.valid ) )
    {
        WriteReason( outReason, reasonSize, "truncated trajectory build state" );
        return false;
    }

    state.trajectoryBuild.rootFrameCount = rootFrameCount;
    state.trajectoryBuild.childFrameCount = childFrameCount;
    state.trajectoryBuild.builtNodeCount = builtNodeCount;
    const uint16_t activeRootBranch = state.trajectoryBuild.usingBuildFrames ? REPLAY_TRAJECTORY_BUILD_BRANCH
                                                                             : REPLAY_TRAJECTORY_COMMITTED_BRANCH;

    for ( const ReplayTrajectoryRecord& record : state.trajectoryStore.records )
    {

        if ( record.key.lane != ReplayTrajectoryLane::FutureRoot || record.key.branchOrdinal != activeRootBranch ||
             record.key.bodyId.value == state.trajectoryBuild.rootId.value )
        {
            continue;
        }

        // Backward compatibility: all-body space publication reuses the
        // existing FutureRoot wire shape, so schema-2 archives advertise the
        // mode through their additional body-keyed records.
        state.trajectoryBuild.allBodyPaths = true;
        ++state.trajectoryBuild.builtAllBodyCount;
        state.trajectoryBuild.allBodyFrameCount = (std::max)( state.trajectoryBuild.allBodyFrameCount,
                                                              record.publishedPointCount );
    }

    if ( state.trajectoryBuild.allBodyPaths )
    {
        ++state.trajectoryBuild.builtAllBodyCount; // The selected root owns the canonical root record.
    }

    ReplayPredictionBaselineSnapshot& baseline = state.baseline;
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

        if ( !ReadBaselinePose( reader, pose ) )
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

    pathVisualizer.hasTarget = archivedHasTarget;
    pathVisualizer.pastPathVisible = archivedPastPathVisible;
    state.enabled = true;
    state.ragdollVisualsEnabled = ragdollVisuals;
    state.build.dirty = false;
    state.build.pendingLatestRestart = false;
    state.build.liveVelocityEditRefreshPending = false;
    state.build.building = false;
    state.build.complete = true;
    state.ResetBuildFramePublication();
    state.futureNodeCache.futureNodesBuiltFrameCount = state.simulation.frames.size();
    state.futureNodeCache.futureNodesBuiltContactIndex = 0;
    state.futureNodeCache.futureNodesBuiltTargetId = state.simulation.targetId;
    state.futureNodeCache.futureNodesBuiltFromBuildFrames = false;
    state.futureNodeCache.futureNodesCacheValid = true;
    state.revealClock.deterministicFrameEnabled = true;
    state.revealClock.deterministicFrame = 0;
    state.revealClock.presentedFrame = 0;
    state.revealClock.anchorValid = false;
    return true;
}

} // namespace ReplayPredictionArchiveOperations
} // namespace SkullbonezCore::Runtime
