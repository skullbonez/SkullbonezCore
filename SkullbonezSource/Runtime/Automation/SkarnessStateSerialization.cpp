#include "SkarnessStateSerialization.h"

#if defined( SKULLBONEZ_SKARNESS )

#include "ReplayAutomationView.h"
#include "../../../ThirdPtySource/nlohmann/json.hpp"
#include "../Replay/ReplayVisualPacketFingerprint.h"

#include <algorithm>
#include <span>
#include <vector>

namespace SkullbonezCore::Runtime
{
namespace
{
using Json = nlohmann::ordered_json;

enum TopicIndex : std::size_t
{
    Session,
    SceneObjects,
    Selection,
    Input,
    Camera,
    FrameClocks,
    Timeline,
    PredictionControls,
    PredictionFrames,
    PredictionEvidence,
    PredictionTopology,
    PredictionTrajectories,
    Cause,
    Planning,
    VisualPacket,
    RenderSubmission,
    LegacyScene,
    LegacyReplay
};

Json Vec3( const Math::Vector::Vector3& value )
{
    return Json::array( { value.x, value.y, value.z } );
}

Json Quaternion( const Math::Orientation::Quaternion& value )
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
    value.GetComponents( x, y, z, w );
    return Json::array( { x, y, z, w } );
}

void Store( SkarnessSerializedStateTopics& topics, TopicIndex index, Json payload, uint64_t ownerVersion = 0,
            uint64_t appendCursor = 0, uint64_t evictCursor = 0 )
{
    SkarnessSerializedStateTopic& topic = topics[static_cast<std::size_t>( index )];
    topic.payload = payload.dump();
    topic.ownerVersion = ownerVersion;
    topic.appendCursor = appendCursor;
    topic.evictCursor = evictCursor;
}

Json BuildSession( const SkarnessFrameState& state )
{
    return { { "paused", state.paused }, { "sceneReady", state.sceneReady }, { "sceneGeneration", state.sceneGeneration } };
}

Json BuildScene( const SkarnessFrameState& state )
{
    return { { "scenePath", state.scenePath },
             { "objectCount", state.sceneObjectCount },
             { "physicsBodyCount", state.physicsBodyCount },
             { "lifecycleEvent", state.sceneLifecycleEvent },
             { "sceneMode", state.sceneMode } };
}

Json BuildSelection( const SkarnessFrameState& state )
{
    return { { "hasPathTarget", state.hasPathTarget },
             { "pathTargetId", state.pathTargetId },
             { "pathTargetModelRow", state.pathTargetModelRow },
             { "selectedCauseRow", state.selectedCauseRow },
             { "selectedCausePrimaryId", state.selectedCausePrimaryId },
             { "selectedCauseCounterpartId", state.selectedCauseCounterpartId } };
}

Json BuildInput( const SkarnessFrameState& state )
{
    return { { "captureEnabled", state.replayCaptureEnabled },        { "scrubPaused", state.replayScrubPaused },
             { "playbackPaused", state.replayPlaybackPaused },        { "predictionEnabled", state.predictionEnabled },
             { "velocityEditEnabled", state.velocityEditEnabled },    { "pastPathVisible", state.pastPathVisible },
             { "ragdollVisualsEnabled", state.ragdollVisualsEnabled } };
}

Json BuildCamera( const SkarnessFrameState& state )
{
    return { { "selectedCameraHash", state.selectedCameraHash },
             { "tweenActive", state.cameraTweenActive },
             { "tweenProgress", state.cameraTweenProgress },
             { "primaryEye", { state.cameraPrimaryEyeX, state.cameraPrimaryEyeY, state.cameraPrimaryEyeZ } },
             { "primaryView", { state.cameraPrimaryViewX, state.cameraPrimaryViewY, state.cameraPrimaryViewZ } },
             { "primaryUp", { state.cameraPrimaryUpX, state.cameraPrimaryUpY, state.cameraPrimaryUpZ } },
             { "renderEye", { state.cameraRenderEyeX, state.cameraRenderEyeY, state.cameraRenderEyeZ } },
             { "renderView", { state.cameraRenderViewX, state.cameraRenderViewY, state.cameraRenderViewZ } },
             { "renderUp", { state.cameraRenderUpX, state.cameraRenderUpY, state.cameraRenderUpZ } },
             { "renderRollRadians", state.cameraRenderRollRadians },
             { "inspectionActive", state.inspectionCameraActive },
             { "inspectionPivot", { state.inspectionPivotX, state.inspectionPivotY, state.inspectionPivotZ } } };
}

Json BuildTimeline( const ReplayAutomationView& replay, SkarnessStateDetail detail )
{
    const ReplayRecorderStats presentation = replay.presentationRecorder.GetStats();
    const ReplayRecorderStats solver = replay.solverStats;
    const ReplayEventRecorderStats events = replay.eventRecorder.GetStats();
    Json payload = { { "presentation",
                       { { "enabled", presentation.enabled },
                         { "sampleCount", presentation.sampleCount },
                         { "sampleCapacity", presentation.sampleCapacity },
                         { "totalCaptured", presentation.totalFramesCaptured },
                         { "totalEvicted", presentation.totalFramesEvicted },
                         { "nextFrame", presentation.nextFrameIndex },
                         { "checkpointCount", presentation.checkpointCount } } },
                     { "solver",
                       { { "enabled", solver.enabled },
                         { "sampleCount", solver.sampleCount },
                         { "sampleCapacity", solver.sampleCapacity },
                         { "totalCaptured", solver.totalFramesCaptured },
                         { "totalEvicted", solver.totalFramesEvicted },
                         { "nextFrame", solver.nextFrameIndex },
                         { "checkpointCount", solver.checkpointCount },
                         { "latestHash", solver.latestStateHash } } },
                     { "events",
                       { { "enabled", events.enabled },
                         { "eventCount", events.eventCount },
                         { "eventCapacity", events.eventCapacity },
                         { "totalCaptured", events.totalEventsCaptured },
                         { "totalEvicted", events.totalEventsEvicted },
                         { "nextSequence", events.nextSequence } } },
                     { "scrubber",
                       { { "visible", replay.scrubber.visible },
                         { "historicalPaused", replay.scrubber.historicalSamplePaused },
                         { "liveAdvanceHeld", replay.scrubber.liveAdvanceHeld },
                         { "position", replay.scrubber.position },
                         { "presentationPosition", replay.scrubber.presentationPosition },
                         { "solverPosition", replay.scrubber.solverPosition } } } };

    if ( detail != SkarnessStateDetail::Summary )
    {
        Json samples = Json::array();
        replay.solverRecorder.ForEachSampleChronological(
            [&]( const ReplaySolverFrameSample& sample )
            {
                Json row = { { "frame", sample.frameIndex },
                             { "sceneFrame", sample.sceneFrame },
                             { "simulationSeconds", sample.simulationSeconds },
                             { "solverHash", sample.solverHash },
                             { "presentationHash", sample.presentationHash },
                             { "bodyCount", sample.bodies.size() },
                             { "contactCount", sample.contactCount },
                             { "pipelineRecordCount", sample.pipelineRecordCount },
                             { "checkpoint", sample.checkpointBoundary } };

                if ( detail == SkarnessStateDetail::Full )
                {
                    Json bodies = Json::array();
                    for ( const ReplaySolverBodySample& body : sample.bodies )
                    {
                        bodies.push_back( { { "id", body.id.value },
                                            { "modelRow", body.modelRow.value },
                                            { "name", body.name },
                                            { "position", Vec3( body.position ) },
                                            { "linearVelocity", Vec3( body.linearVelocity ) },
                                            { "angularVelocity", Vec3( body.angularVelocity ) },
                                            { "sleeping", body.sleeping },
                                            { "fixed", body.fixed } } );
                    }
                    row["bodies"] = std::move( bodies );
                }
                samples.push_back( std::move( row ) );
            } );
        payload["solverSamples"] = std::move( samples );
    }
    return payload;
}

Json BuildPredictionControls( const SkarnessFrameState& state )
{
    return { { "enabled", state.predictionEnabled },
             { "building", state.predictionBuilding },
             { "complete", state.predictionComplete },
             { "dirty", state.predictionDirty },
             { "restartPending", state.predictionRestartPending },
             { "generationPermitted", state.predictionGenerationPermitted },
             { "highDetail", state.predictionHighDetail },
             { "horizonSeconds", state.predictionHorizonSeconds },
             { "revealProgress", state.predictionRevealProgress },
             { "generation", state.predictionGeneration },
             { "sourceTargetId", state.predictionSourceTargetId },
             { "sourceFrame", state.predictionSourceFrame },
             { "sourceSolverHash", state.predictionSourceSolverHash },
             { "workerFailed", state.predictionWorkerFailed } };
}

Json BuildPredictionFrames( const ReplayAutomationView& replay, SkarnessStateDetail detail )
{
    Json rows = Json::array();
    for ( const RunReplayPredictionFrame& frame : replay.activePredictionFrames )
    {
        Json row = { { "frame", frame.frameIndex },
                     { "simulationSeconds", frame.simulationSeconds },
                     { "bodyCount", frame.bodies.size() },
                     { "contactCount", frame.debugContacts.size() },
                     { "contactsIncomplete", frame.contactsIncomplete } };
        if ( detail == SkarnessStateDetail::Full )
        {
            Json bodies = Json::array();
            for ( const RunReplayPredictionBodySample& body : frame.bodies )
            {
                bodies.push_back( { { "id", body.id.value },
                                    { "modelRow", body.modelRow.value },
                                    { "position", Vec3( body.position ) },
                                    { "orientation", Quaternion( body.orientation ) },
                                    { "linearVelocity", Vec3( body.linearVelocity ) },
                                    { "sleeping", body.sleeping } } );
            }
            row["bodies"] = std::move( bodies );
        }
        rows.push_back( std::move( row ) );
    }
    return { { "count", replay.activePredictionFrames.size() }, { "frames", std::move( rows ) } };
}

Json ContactRow( const Physics::PhysicsSolverPersistentContactSample& contact )
{
    return { { "bodyA", contact.bodyA },
             { "bodyB", contact.bodyB },
             { "featureId", contact.featureId },
             { "normal", Vec3( contact.normal ) },
             { "penetration", contact.penetration },
             { "normalImpulse", contact.accN },
             { "tangentImpulse1", contact.accT1 },
             { "tangentImpulse2", contact.accT2 },
             { "warmStarted", contact.warmStarted },
             { "terrain", contact.isTerrain },
             { "manifoldPointCount", contact.manifoldPointCount } };
}

Json PipelineRow( const Physics::PhysicsPipelineRecord& row )
{
    return { { "stage", Physics::PhysicsPipelineStageName( row.stage ) },
             { "bodyA", row.bodyA },
             { "bodyB", row.bodyB },
             { "iteration", row.iteration },
             { "featureId", row.featureId },
             { "point", Vec3( row.point ) },
             { "normal", Vec3( row.normal ) },
             { "scalarA", row.scalarA },
             { "scalarB", row.scalarB },
             { "scalarC", row.scalarC } };
}

Json BuildPredictionEvidence( const ReplayAutomationView& replay, SkarnessStateDetail detail )
{
    const ReplayPredictionSolverEvidenceStore& evidence = replay.predictionEvidence;
    const auto memory = evidence.CollectMemoryStats();
    Json frames = Json::array();
    for ( std::size_t frameIndex = 0; frameIndex < evidence.PublishedFrameCount(); ++frameIndex )
    {
        const ReplayPredictionSolverEvidenceFrame* frame = evidence.PublishedFrame( frameIndex );
        if ( !frame )
        {
            continue;
        }
        Json row = { { "frame", frame->identity.frame },
                     { "generation", frame->identity.generation },
                     { "bankEpoch", frame->identity.bankEpoch },
                     { "topologyVersion", frame->identity.topologyVersion },
                     { "publicationVersion", frame->identity.publicationVersion },
                     { "contactCount", frame->contacts.count },
                     { "pipelineCount", frame->pipeline.count },
                     { "complete", frame->complete } };
        if ( detail == SkarnessStateDetail::Full )
        {
            Json contacts = Json::array();
            for ( std::size_t i = 0; i < frame->contacts.count; ++i )
            {
                if ( const auto* contact = evidence.Contact( frame->contacts, i ) )
                {
                    contacts.push_back( ContactRow( *contact ) );
                }
            }
            Json pipeline = Json::array();
            for ( std::size_t i = 0; i < frame->pipeline.count; ++i )
            {
                if ( const auto* record = evidence.Pipeline( frame->pipeline, i ) )
                {
                    pipeline.push_back( PipelineRow( *record ) );
                }
            }
            row["contacts"] = std::move( contacts );
            row["pipeline"] = std::move( pipeline );
        }
        frames.push_back( std::move( row ) );
    }
    return { { "generation", evidence.Generation() },
             { "bankEpoch", evidence.BankEpoch() },
             { "detailMode", static_cast<int>( evidence.Mode() ) },
             { "publishedFrameCount", evidence.PublishedFrameCount() },
             { "contactCount", memory.contactCount },
             { "pipelineCount", memory.pipelineCount },
             { "capacityBytes", memory.currentCapacityBytes },
             { "frames", std::move( frames ) } };
}

Json BuildTopology( const ReplayAutomationView& replay, SkarnessStateDetail detail )
{
    const ReplayVisualPacket& packet = replay.visualPacket;
    Json payload = { { "targetId", packet.header.targetId.value },
                     { "topologyVersion", packet.header.topologyVersion },
                     { "futureNodeCount", packet.futureNodes.size() } };
    if ( detail != SkarnessStateDetail::Summary )
    {
        Json nodes = Json::array();
        for ( const RunReplayPathTraceNode& node : packet.futureNodes )
        {
            nodes.push_back( { { "id", node.id.value },
                               { "parentId", node.parentId.value },
                               { "modelRow", node.modelRow.value },
                               { "parentModelRow", node.parentModelRow.value },
                               { "firstFrame", node.firstFrame },
                               { "contactPoint", Vec3( node.contactPoint ) },
                               { "contactNormal", Vec3( node.contactNormal ) },
                               { "depth", node.depth },
                               { "contactDerived", node.contactDerived } } );
        }
        payload["nodes"] = std::move( nodes );
    }
    return payload;
}

Json BuildTrajectories( const ReplayAutomationView& replay, SkarnessStateDetail detail )
{
    Json records = Json::array();
    for ( const ReplayTrajectoryRecord& record : replay.visualPacket.trajectoryRecords )
    {
        Json row = { { "bodyId", record.key.bodyId.value },
                     { "lane", static_cast<int>( record.key.lane ) },
                     { "branchOrdinal", record.key.branchOrdinal },
                     { "version", record.version },
                     { "publishedPointCount", record.publishedPointCount },
                     { "pointCount", record.points.size() },
                     { "styleId", record.styleId },
                     { "parentId", record.parentId.value },
                     { "depth", record.depth },
                     { "firstFrame", record.firstFrame },
                     { "contactDerived", record.contactDerived } };
        if ( detail == SkarnessStateDetail::Full )
        {
            Json points = Json::array();
            const std::size_t count = (std::min)( record.publishedPointCount, record.points.size() );
            for ( std::size_t i = 0; i < count; ++i )
            {
                points.push_back(
                    { { "frame", record.points[i].frameIndex }, { "position", Vec3( record.points[i].position ) } } );
            }
            row["points"] = std::move( points );
        }
        records.push_back( std::move( row ) );
    }
    return { { "topologyVersion", replay.visualPacket.header.topologyVersion },
             { "retainedRevision", replay.visualPacket.retainedPredictionRevision },
             { "recordCount", replay.visualPacket.trajectoryRecords.size() },
             { "records", std::move( records ) } };
}

Json BuildCause( const ReplayAutomationView& replay, SkarnessStateDetail detail )
{
    Json payload = { { "rowCount", replay.causeTree.rows.size() },
                     { "selectedRow", replay.causeTree.selectedRow },
                     { "focusedId", replay.causeTree.focusedId.value },
                     { "filterText", replay.causeTree.filterText },
                     { "filter", static_cast<int>( replay.causeTree.filter ) },
                     { "mode", static_cast<int>( replay.causeInspection.mode ) },
                     { "generation", replay.causeInspection.generation },
                     { "sourceFrame", replay.causeInspection.sourceFrame },
                     { "targetFrame", replay.causeInspection.targetFrame },
                     { "presentedFrame", replay.causeInspection.presentedFrame },
                     { "transportFrame", replay.causeInspection.transportFrame },
                     { "transportInFlight", replay.causeInspection.transportInFlight },
                     { "detailVisible", replay.causeInspection.detailVisible },
                     { "drawerOpen", replay.causeInspection.drawerOpen },
                     { "drawerProgress", replay.causeInspection.drawerProgress },
                     { "solverContactCount", replay.causeInspection.solverDetailContactRowCount },
                     { "solverPipelineCount", replay.causeInspection.solverDetailPipelineRecordCount } };
    if ( detail != SkarnessStateDetail::Summary )
    {
        Json rows = Json::array();
        for ( const RunReplayCauseTreeRow& row : replay.causeTree.rows )
        {
            rows.push_back( { { "kind", static_cast<int>( row.kind ) },
                              { "id", row.id.value },
                              { "parentId", row.parentId.value },
                              { "counterpartId", row.counterpartId.value },
                              { "firstFrame", row.firstFrame },
                              { "depth", row.depth },
                              { "contactIndex", row.contactIndex },
                              { "solverRowIndex", row.solverRowIndex },
                              { "pipelineIndex", row.pipelineIndex },
                              { "sourceGeneration", row.sourceGeneration },
                              { "sourceBankEpoch", row.sourceBankEpoch },
                              { "sourceTopologyVersion", row.sourceTopologyVersion },
                              { "sourcePublicationVersion", row.sourcePublicationVersion },
                              { "manifoldPointCount", row.manifoldPointCount },
                              { "penetration", row.penetration },
                              { "point", Vec3( row.point ) },
                              { "normal", Vec3( row.normal ) },
                              { "name", row.name },
                              { "detail", row.detail } } );
        }
        payload["rows"] = std::move( rows );
    }
    return payload;
}

Json BuildPlanning( const ReplayAutomationView& replay, SkarnessStateDetail detail )
{
    Json payload = { { "intercept",
                       { { "valid", replay.intercept.valid },
                         { "intercept", replay.intercept.intercept },
                         { "shipId", replay.intercept.shipId.value },
                         { "targetId", replay.intercept.targetId.value },
                         { "closestFrame", replay.intercept.closestFrame },
                         { "missDistance", replay.intercept.missDistance },
                         { "relativeSpeed", replay.intercept.relativeSpeed },
                         { "etaSeconds", replay.intercept.etaSeconds },
                         { "topologyVersion", replay.intercept.topologyVersion } } },
                     { "porkchop",
                       { { "visible", replay.porkchop.visible },
                         { "available", replay.porkchop.available },
                         { "building", replay.porkchop.building },
                         { "complete", replay.porkchop.complete },
                         { "targetId", replay.porkchop.targetId.value },
                         { "completedCells", replay.porkchop.completedCells },
                         { "selectedCell", replay.porkchop.selectedCell },
                         { "minimumDeltaV", replay.porkchop.minimumDeltaV } } },
                     { "trip",
                       { { "state", static_cast<int>( replay.tripPlanner.state ) },
                         { "shipId", replay.tripPlanner.shipId.value },
                         { "targetId", replay.tripPlanner.targetId.value },
                         { "timeOfFlightSeconds", replay.tripPlanner.timeOfFlightSeconds },
                         { "missDistance", replay.tripPlanner.missDistance },
                         { "iteration", replay.tripPlanner.iteration },
                         { "ghostCount", replay.tripPlanner.ghostCount },
                         { "visible", replay.tripPlanner.visible },
                         { "available", replay.tripPlanner.available },
                         { "noSolution", replay.tripPlanner.noSolution } } } };
    if ( detail == SkarnessStateDetail::Full )
    {
        payload["porkchop"]["deltaV"] = Json::array();
        for ( float value : replay.porkchop.deltaV )
        {
            payload["porkchop"]["deltaV"].push_back( value );
        }
    }
    return payload;
}

void AddFloatBuffer( Json& payload, const char* name, std::span<const float> values, SkarnessStateDetail detail )
{
    Json buffer = { { "count", values.size() },
                    { "bytes", values.size_bytes() },
                    { "hash", ReplayVisualPacketOperations::HashReplayVisualFloatBuffer( values ) } };
    if ( detail == SkarnessStateDetail::Full )
    {
        buffer["values"] = Json::array();
        for ( float value : values )
        {
            buffer["values"].push_back( value );
        }
    }
    payload[name] = std::move( buffer );
}

Json BuildRenderGeometryEvidence( const ReplayVisualPacket& packet )
{
    const ReplayVisualPacketBufferFacts facts = ReplayVisualPacketFingerprintOperations::BuildReplayVisualPacketBufferFacts(
        packet );
    const uint64_t lineBytes = facts.ordinaryLineBytes + facts.priorityLineBytes;
    const uint64_t ribbonBytes = facts.ordinaryRibbonBytes + facts.priorityRibbonBytes;
    const uint64_t geometryBytes = lineBytes + ribbonBytes + facts.expandedVertexBytes;
    const uint64_t lineHash = ReplayVisualPacketOperations::CombineReplayVisualSubmissionHashes( facts.ordinaryLineHash,
                                                                                                 facts.ordinaryLineBytes,
                                                                                                 facts.priorityLineHash,
                                                                                                 facts.priorityLineBytes );
    const uint64_t
        ribbonHash = ReplayVisualPacketOperations::CombineReplayVisualSubmissionHashes( facts.ordinaryRibbonHash,
                                                                                        facts.ordinaryRibbonBytes,
                                                                                        facts.priorityRibbonHash,
                                                                                        facts.priorityRibbonBytes );
    const uint64_t geometryHash = ReplayVisualPacketOperations::CombineReplayVisualSubmissionHashes( lineHash, lineBytes,
                                                                                                     ribbonHash,
                                                                                                     ribbonBytes );

    // Invariant: renderer telemetry permits zero as the hash for an empty vertex
    // lane. Preserve that sentinel while deriving every non-empty byte from the
    // packet spans themselves, so stale submission telemetry cannot self-agree.
    const uint64_t vertexHash = facts.expandedVertexBytes == 0u && packet.submission.vertexHash == 0u
                                    ? 0u
                                    : facts.expandedVertexHash;
    const uint64_t
        submissionHash = ReplayVisualPacketOperations::CombineReplayVisualSubmissionHashes( geometryHash,
                                                                                            lineBytes + ribbonBytes,
                                                                                            vertexHash,
                                                                                            facts.expandedVertexBytes );
    const char* spanMismatch = ReplayVisualPacketFingerprintOperations::FindReplayVisualPacketSubmissionSpanMismatch(
        packet );

    return { { "lineBytes", lineBytes },
             { "ribbonBytes", ribbonBytes },
             { "vertexBytes", facts.expandedVertexBytes },
             { "geometryBytes", geometryBytes },
             { "submissionHash", submissionHash },
             { "spanTelemetryMatches", spanMismatch == nullptr },
             { "spanMismatch", spanMismatch ? spanMismatch : "" } };
}

Json BuildVisualPacket( const ReplayAutomationView& replay, SkarnessStateDetail detail )
{
    const ReplayVisualPacket& packet = replay.visualPacket;
    Json payload = { { "header",
                       { { "schemaVersion", packet.header.schemaVersion },
                         { "sourceFrame", packet.header.sourceFrame },
                         { "revealFrame", packet.header.revealFrame },
                         { "targetId", packet.header.targetId.value },
                         { "branchId", packet.header.branchId },
                         { "eventCursor", packet.header.eventCursor },
                         { "topologyVersion", packet.header.topologyVersion },
                         { "publishedFrameCount", packet.header.publishedFrameCount },
                         { "futureNodeCount", packet.header.futureNodeCount },
                         { "ghostRequestCount", packet.header.ghostRequestCount },
                         { "predictionEnabled", packet.header.predictionEnabled },
                         { "predictionBuilding", packet.header.predictionBuilding },
                         { "predictionComplete", packet.header.predictionComplete } } },
                     { "retainedStreamId", packet.retainedPredictionStreamId },
                     { "retainedRevision", packet.retainedPredictionRevision },
                     { "trajectoryRecordCount", packet.trajectoryRecords.size() },
                     { "markerCount", packet.retainedMarkers.size() },
                     { "ghostCount", packet.ghostRequests.size() },
                     { "hasGeometry", packet.HasGeometry() },
                     { "renderGeometry", BuildRenderGeometryEvidence( packet ) } };
    AddFloatBuffer( payload, "combinedLines", packet.combinedLines, detail );
    AddFloatBuffer( payload, "ordinaryLines", packet.ordinaryLines, detail );
    AddFloatBuffer( payload, "priorityLines", packet.priorityLines, detail );
    AddFloatBuffer( payload, "ordinaryRibbonSegments", packet.ordinaryRibbonSegments, detail );
    AddFloatBuffer( payload, "priorityRibbonSegments", packet.priorityRibbonSegments, detail );
    AddFloatBuffer( payload, "expandedRibbonVertices", packet.expandedRibbonVertices, detail );
    AddFloatBuffer( payload, "priorityExpandedRibbonVertices", packet.priorityExpandedRibbonVertices, detail );
    AddFloatBuffer( payload, "retainedOrdinaryLines", packet.retainedPredictionOrdinaryLines, detail );
    AddFloatBuffer( payload, "retainedPriorityLines", packet.retainedPredictionPriorityLines, detail );
    AddFloatBuffer( payload, "retainedOrdinaryRibbonSegments", packet.retainedPredictionOrdinaryRibbonSegments, detail );
    AddFloatBuffer( payload, "retainedPriorityRibbonSegments", packet.retainedPredictionPriorityRibbonSegments, detail );
    AddFloatBuffer( payload, "retainedRibbonVertices", packet.retainedPredictionRibbonVertices, detail );
    AddFloatBuffer( payload, "retainedPriorityRibbonVertices", packet.retainedPredictionPriorityRibbonVertices, detail );
    if ( detail == SkarnessStateDetail::Full )
    {
        Json markers = Json::array();
        for ( const ReplayPredictionRetainedMarker& marker : packet.retainedMarkers )
        {
            markers.push_back( { { "id", marker.id.value },
                                 { "modelRow", marker.modelRow.value },
                                 { "hasEntryPose", marker.hasEntryPose },
                                 { "hasRestPose", marker.hasRestPose },
                                 { "hasHorizonPose", marker.hasHorizonPose },
                                 { "entryPosition", Vec3( marker.entryPosition ) },
                                 { "restPosition", Vec3( marker.restPosition ) },
                                 { "horizonPosition", Vec3( marker.horizonPosition ) } } );
        }
        payload["markers"] = std::move( markers );
        Json ghosts = Json::array();
        for ( const ReplayPredictionGhostDrawRequest& ghost : packet.ghostRequests )
        {
            ghosts.push_back( { { "modelRow", ghost.modelRow.value },
                                { "position", Vec3( ghost.position ) },
                                { "orientation", Quaternion( ghost.orientation ) },
                                { "alpha", ghost.alpha },
                                { "tint", { ghost.tintR, ghost.tintG, ghost.tintB, ghost.tintStrength } } } );
        }
        payload["ghosts"] = std::move( ghosts );
    }
    return payload;
}

Json BuildRenderSubmission( const SkarnessFrameState& state, const ReplayAutomationView& replay )
{
    const ReplayTrajectorySubmissionProbeStats& submission = replay.trajectorySubmission;
    return { { "hasSubmission", submission.hasSubmission },
             { "stableWindowReady", submission.stableWindowReady },
             { "noReserveGrowth", submission.noReserveGrowth },
             { "observedFrameCount", submission.observedFrameCount },
             { "stableFrameCount", submission.stableFrameCount },
             { "stableFrameTarget", submission.stableWindowTargetFrameCount },
             { "segmentCount", submission.segmentCount },
             { "vertexCount", submission.vertexCount },
             { "stableHash", submission.stableHash },
             { "geometryBytes", submission.geometryBytes },
             { "targetId", submission.presentationTargetId },
             { "sourceFrame", submission.presentationSourceFrame },
             { "topologyVersion", submission.presentationTopologyVersion },
             { "futureTreeReady", submission.futureTreeReadyLastFrame },
             { "submittedSegmentCount", state.submittedSegmentCount },
             { "submittedVertexCount", state.submittedVertexCount },
             { "submittedGeometryHash", state.submittedGeometryHash },
             { "submittedGeometryBytes", state.submittedGeometryBytes } };
}

Json BuildLegacyReplay( const SkarnessFrameState& state )
{
    return { { "predictionEnabled", state.predictionEnabled },
             { "predictionBuilding", state.predictionBuilding },
             { "predictionComplete", state.predictionComplete },
             { "predictionDirty", state.predictionDirty },
             { "predictionRestartPending", state.predictionRestartPending },
             { "predictionGenerationPermitted", state.predictionGenerationPermitted },
             { "predictionGeneration", state.predictionGeneration },
             { "predictionSourceFrame", state.predictionSourceFrame },
             { "publishedPredictionTargetId", state.publishedPredictionTargetId },
             { "publishedPredictionFrames", state.publishedPredictionFrames },
             { "publishedPredictionTopologyVersion", state.publishedPredictionTopologyVersion },
             { "pathTargetId", state.pathTargetId },
             { "pastPathVisible", state.pastPathVisible },
             { "trajectoryRecordCount", state.trajectoryRecordCount },
             { "selectedPastRootPointCount", state.selectedPastRootPointCount },
             { "selectedFutureRootPointCount", state.selectedFutureRootPointCount },
             { "contactChildIncomingCount", state.contactChildIncomingCount },
             { "contactChildOutgoingCount", state.contactChildOutgoingCount },
             { "retainedEntryMarkerCount", state.retainedEntryMarkerCount },
             { "retainedEndMarkerCount", state.retainedEndMarkerCount },
             { "drawnCollisionWireframeCount", state.drawnCollisionWireframeCount },
             { "drawnEndingWireframeCount", state.drawnEndingWireframeCount },
             { "collisionWireframePathMismatchCount", state.collisionWireframePathMismatchCount },
             { "endingWireframePathMismatchCount", state.endingWireframePathMismatchCount },
             { "futureNodeCount", state.futureNodeCount },
             { "incompleteContactFrameCount", state.incompleteContactFrameCount },
             { "causeTreeRowCount", state.causeTreeRowCount },
             { "causeTreeRowBuildCount", state.causeTreeRowBuildCount },
             { "causeTreeRowCacheHitCount", state.causeTreeRowCacheHitCount },
             { "causeWindowAvailable", state.causeWindowAvailable },
             { "selectedCauseRow", state.selectedCauseRow },
             { "causeInspectionMode", state.causeInspectionMode },
             { "selectedCauseFrame", state.selectedCauseFrame },
             { "causeSourceFrame", state.causeSourceFrame },
             { "causeTargetFrame", state.causeTargetFrame },
             { "causePresentedFrame", state.causePresentedFrame },
             { "causeSeekSource", state.causeSeekSource },
             { "presentedReplayFrame", state.presentedReplayFrame },
             { "presentedReplayFrameSource", state.presentedReplayFrameSource },
             { "selectedCausePrimaryId", state.selectedCausePrimaryId },
             { "selectedCauseCounterpartId", state.selectedCauseCounterpartId },
             { "causeContactPointCount", state.causeContactPointCount },
             { "submittedCauseContactPointCount", state.submittedCauseContactPointCount },
             { "submittedCauseContactBodyCount", state.submittedCauseContactBodyCount },
             { "inspectionCameraActive", state.inspectionCameraActive },
             { "inspectionCameraFocusKind", state.inspectionCameraFocusKind },
             { "inspectionFocusFadeActive", state.inspectionFocusFadeActive },
             { "inspectionFocusObjectCount", state.inspectionFocusObjectCount },
             { "inspectionPathFocusPrimaryId", state.inspectionPathFocusPrimaryId },
             { "inspectionPathFocusCounterpartId", state.inspectionPathFocusCounterpartId },
             { "inspectionFocusedPathRangeCount", state.inspectionFocusedPathRangeCount },
             { "inspectionContextPathRangeCount", state.inspectionContextPathRangeCount },
             { "inspectionFocusedPathSegmentCount", state.inspectionFocusedPathSegmentCount },
             { "inspectionContextPathSegmentCount", state.inspectionContextPathSegmentCount },
             { "inspectionPathOpacityMismatchCount", state.inspectionPathOpacityMismatchCount },
             { "inspectionPathFocusActive", state.inspectionPathFocusActive },
             { "inspectionBodyMarkerSubmitted", state.inspectionBodyMarkerSubmitted },
             { "selectedCameraHash", state.selectedCameraHash },
             { "cameraPrimaryEye", { state.cameraPrimaryEyeX, state.cameraPrimaryEyeY, state.cameraPrimaryEyeZ } },
             { "cameraPrimaryView", { state.cameraPrimaryViewX, state.cameraPrimaryViewY, state.cameraPrimaryViewZ } },
             { "cameraPrimaryUp", { state.cameraPrimaryUpX, state.cameraPrimaryUpY, state.cameraPrimaryUpZ } },
             { "cameraRenderEye", { state.cameraRenderEyeX, state.cameraRenderEyeY, state.cameraRenderEyeZ } },
             { "inspectionPivot", { state.inspectionPivotX, state.inspectionPivotY, state.inspectionPivotZ } },
             { "cameraRenderRollRadians", state.cameraRenderRollRadians },
             { "visualPacketHasGeometry", state.visualPacketHasGeometry },
             { "trajectorySubmitted", state.trajectorySubmitted },
             { "submittedSegmentCount", state.submittedSegmentCount },
             { "submittedPredictionTargetId", state.submittedPredictionTargetId },
             { "submittedPredictionSourceFrame", state.submittedPredictionSourceFrame },
             { "submittedPredictionTopologyVersion", state.submittedPredictionTopologyVersion },
             { "submittedGeometryHash", state.submittedGeometryHash },
             { "submittedGeometryBytes", state.submittedGeometryBytes },
             { "submittedFutureTreeReady", state.submittedFutureTreeReady } };
}
} // namespace

void BuildSkarnessStateTopics( const SkarnessFrameState& state, const ReplayAutomationView& replay,
                               SkarnessStateDetail detail, SkarnessSerializedStateTopics& outTopics )
{
    const ReplayRecorderStats solver = replay.solverStats;
    const ReplayEventRecorderStats events = replay.eventRecorder.GetStats();
    Store( outTopics, Session, BuildSession( state ), state.sceneGeneration );
    Store( outTopics, SceneObjects, BuildScene( state ), state.sceneGeneration );
    Store( outTopics, Selection, BuildSelection( state ), state.sceneGeneration );
    Store( outTopics, Input, BuildInput( state ) );
    Store( outTopics, Camera, BuildCamera( state ) );
    Store( outTopics, FrameClocks,
           { { "sceneFrame", state.sceneFrame }, { "simulationSeconds", state.simulationSeconds } } );
    Store( outTopics, Timeline, BuildTimeline( replay, detail ), 0, solver.totalFramesCaptured,
           solver.totalFramesEvicted + events.totalEventsEvicted );
    Store( outTopics, PredictionControls, BuildPredictionControls( state ), state.predictionGeneration );
    Store( outTopics, PredictionFrames, BuildPredictionFrames( replay, detail ), state.predictionGeneration,
           replay.activePredictionFrames.size() );
    Store( outTopics, PredictionEvidence, BuildPredictionEvidence( replay, detail ), replay.predictionEvidence.BankEpoch(),
           replay.predictionEvidence.PublishedFrameCount() );
    Store( outTopics, PredictionTopology, BuildTopology( replay, detail ), replay.visualPacket.header.topologyVersion );
    Store( outTopics, PredictionTrajectories, BuildTrajectories( replay, detail ),
           replay.visualPacket.retainedPredictionRevision );
    Store( outTopics, Cause, BuildCause( replay, detail ), replay.causeInspection.generation );
    Store( outTopics, Planning, BuildPlanning( replay, detail ), replay.visualPacket.header.topologyVersion );
    Store( outTopics, VisualPacket, BuildVisualPacket( replay, detail ), replay.visualPacket.retainedPredictionRevision );
    Store( outTopics, RenderSubmission, BuildRenderSubmission( state, replay ),
           replay.trajectorySubmission.presentationTopologyVersion );
    Store( outTopics, LegacyScene, BuildScene( state ), state.sceneGeneration );
    Store( outTopics, LegacyReplay, BuildLegacyReplay( state ), state.predictionGeneration );
}
} // namespace SkullbonezCore::Runtime

#endif
