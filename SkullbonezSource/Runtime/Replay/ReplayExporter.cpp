/*
File: SkullbonezSource/Runtime/Replay/ReplayExporter.cpp
Purpose:
  Writes bounded replay buffers to disk for later debugging and sharing.

Mental model:
  Exporters serialize already-retained replay samples. They do not pull live
  simulation state, advance physics, or mutate replay buffers.

Glossary:
  Replay buffer: Bounded in-memory sequence of retained presentation or solver
    samples.
  Presentation sample: Render-facing pose/state captured from a frame.
  Solver sample: Physics-facing state needed for rollback and diagnostics.
  Artifact: File written for debugging, sharing, or automated inspection.

Invariants:
  - Export order follows the recorder order so artifact diffs stay stable.
  - Missing optional data should serialize explicitly rather than shifting fields.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayExporter.h
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
*/
#include "ReplayExporter.h"

#include "../RuntimeFileWriter.h"

#include <algorithm>
#include <cstdio>

#pragma warning( push, 0 )
#include "../../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

using namespace SkullbonezCore::Basics;
using SkullbonezCore::Math::Vector::Vector3;

namespace
{
using Json = nlohmann::ordered_json;

Json Vec3Json( const Vector3& value )
{
    return Json::array( { value.x, value.y, value.z } );
}

Json OrientationJson( const float orientation[4] )
{
    return Json::array( { orientation[0], orientation[1], orientation[2], orientation[3] } );
}

const char* ShapeKindName( ReplayBodyShapeKind kind )
{
    switch ( kind )
    {
    case ReplayBodyShapeKind::Sphere:
        return "sphere";
    case ReplayBodyShapeKind::Box:
        return "box";
    case ReplayBodyShapeKind::ConvexHull:
        return "convexHull";
    case ReplayBodyShapeKind::Unknown:
    default:
        return "unknown";
    }
}

const char* HashText( uint64_t hash, char ( &buffer )[24] )
{
    sprintf_s( buffer, sizeof( buffer ), "0x%016llX", static_cast<unsigned long long>( hash ) );
    return buffer;
}

Json BranchJson( const ReplayBranchInfo& branch )
{
    char sourceHashBuffer[24] = {};
    Json json;
    json["branchId"] = branch.branchId;
    json["parentBranchId"] = branch.parentBranchId;
    json["startFrame"] = branch.startFrame;
    json["sourceFrame"] = branch.sourceFrame;
    if ( branch.sourceSolverHash != 0 )
    {
        json["sourceSolverHash"] = HashText( branch.sourceSolverHash, sourceHashBuffer );
    }
    else
    {
        json["sourceSolverHash"] = nullptr;
    }
    return json;
}

Json BodyJson( const ReplayBodyPresentationSample& body )
{
    // Invariant: JSON field names are artifact compatibility surface. Prefer
    // adding explicit fields over renaming or reordering existing concepts.
    Json result;
    result["id"] = body.id.value;
    result["modelIndex"] = body.modelIndex;
    result["name"] = body.name;
    result["shape"] = ShapeKindName( body.shapeKind );
    result["position"] = Vec3Json( body.position );
    result["linearVelocity"] = Vec3Json( body.linearVelocity );
    result["angularVelocity"] = Vec3Json( body.angularVelocity );
    result["orientation"] = OrientationJson( body.orientation );
    result["mass"] = body.mass;
    result["fixed"] = body.fixed;
    result["sleeping"] = body.sleeping;
    result["sleepSupported"] = body.sleepSupported;
    result["sleepInhibited"] = body.sleepInhibited;
    result["collisionContact"] = body.collisionContact;
    result["sleepIslandVisualId"] = body.sleepIslandVisualId;
    result["contactCount"] = body.contactCount;
    result["maxPenetration"] = body.maxPenetration;
    result["normalImpulseSum"] = body.normalImpulseSum;
    return result;
}

Json BodyJson( const ReplaySolverBodySample& body )
{
    // Solver JSON includes inertia and cache-adjacent fields that presentation
    // JSON omits; consumers use that distinction to tell visual scrub data from
    // physics restore diagnostics.
    Json result;
    result["id"] = body.id.value;
    result["modelIndex"] = body.modelIndex;
    result["name"] = body.name;
    result["shape"] = ShapeKindName( body.shapeKind );
    result["position"] = Vec3Json( body.position );
    result["linearVelocity"] = Vec3Json( body.linearVelocity );
    result["angularVelocity"] = Vec3Json( body.angularVelocity );
    result["orientation"] = OrientationJson( body.orientation );
    result["mass"] = body.mass;
    result["inverseMass"] = body.inverseMass;
    result["rotationalInertia"] = Vec3Json( body.rotationalInertia );
    result["inverseRotationalInertia"] = Vec3Json( body.inverseRotationalInertia );
    result["fixed"] = body.fixed;
    result["sleeping"] = body.sleeping;
    result["sleepSupported"] = body.sleepSupported;
    result["sleepInhibited"] = body.sleepInhibited;
    result["collisionContact"] = body.collisionContact;
    result["sleepIslandVisualId"] = body.sleepIslandVisualId;
    result["contactCount"] = body.contactCount;
    result["maxPenetration"] = body.maxPenetration;
    result["normalImpulseSum"] = body.normalImpulseSum;
    return result;
}

Json WorldJson( const ReplayWorldPresentationSample& world )
{
    Json result;
    result["gravity"] = world.gravity;
    result["fluidHeight"] = world.fluidHeight;
    result["fluidDensity"] = world.fluidDensity;
    result["waterHidden"] = world.waterHidden;
    result["terrainHidden"] = world.terrainHidden;
    result["fixedStep"] = world.fixedStep;
    result["scenePhysicsEnabled"] = world.scenePhysicsEnabled;
    result["sceneTextEnabled"] = world.sceneTextEnabled;
    return result;
}

Json CameraJson( const ReplayCameraSample& camera )
{
    Json result;
    result["eye"] = Vec3Json( camera.eye );
    result["view"] = Vec3Json( camera.view );
    result["up"] = Vec3Json( camera.up );
    return result;
}

const char* LauncherFireModeName( ReplayLauncherFireMode mode )
{
    switch ( mode )
    {
    case ReplayLauncherFireMode::Projectile:
        return "projectile";
    case ReplayLauncherFireMode::Laser:
    default:
        return "laser";
    }
}

Json LauncherVisualSummaryJson( const ReplayLauncherVisualSample& visual )
{
    Json result;
    result["fireMode"] = LauncherFireModeName( visual.fireMode );
    result["visualizeRays"] = visual.visualizeRays;
    result["rayLineCount"] = visual.rayLines.size();
    result["laserShotCount"] = visual.laserShots.size();
    result["activeRayLineCount"] =
        static_cast<int>( std::count_if( visual.rayLines.begin(),
                                         visual.rayLines.end(),
                                         []( const ReplayRayCastLineSample& line ) { return line.active; } ) );
    result["activeLaserShotCount"] =
        static_cast<int>( std::count_if( visual.laserShots.begin(),
                                         visual.laserShots.end(),
                                         []( const LauncherLaserShotSnapshot& shot ) { return shot.active; } ) );
    return result;
}

Json SolverWorldSnapshotSummaryJson( const ReplaySolverWorldSnapshot& snapshot )
{
    Json result;
    result["version"] = snapshot.version;
    result["modelCount"] = snapshot.modelCount;
    result["fluidCongestionPhase"] = snapshot.fluidCongestionPhase;
    result["sleepEnabled"] = snapshot.sleepEnabled;
    result["sleepingBodyCount"] = static_cast<int>( std::count_if( snapshot.sleepState.begin(),
                                                                   snapshot.sleepState.end(),
                                                                   []( uint8_t value ) { return value != 0; } ) );
    result["persistentContactCount"] = snapshot.persistentContacts.size();
    result["persistentContactCacheCount"] = snapshot.persistentContactCache.size();
    result["debugContactCount"] = snapshot.debugContacts.size();
    result["pipelineRecordCount"] = snapshot.pipelineTrace.size();
    result["collisionCellKeyCount"] = snapshot.collisionCellKeys.size();
    result["tornadoEnabled"] = snapshot.tornadoConfig.enabled;
    return result;
}

Json FrameJson( const ReplayPresentationSample& sample )
{
    char hashBuffer[24] = {};
    Json frame;
    frame["frameIndex"] = sample.frameIndex;
    frame["eventCursor"] = sample.eventCursor;
    frame["sceneFrame"] = sample.sceneFrame;
    frame["simulationSeconds"] = sample.simulationSeconds;
    frame["physicsDt"] = sample.physicsDt;
    frame["stateHash"] = HashText( sample.stateHash, hashBuffer );
    frame["contactCount"] = sample.contactCount;
    frame["pipelineRecordCount"] = sample.pipelineRecordCount;
    frame["checkpointBoundary"] = sample.checkpointBoundary;
    frame["branch"] = BranchJson( sample.branch );
    frame["world"] = WorldJson( sample.world );
    frame["camera"] = CameraJson( sample.camera );
    frame["bodies"] = Json::array();
    for ( const ReplayBodyPresentationSample& body : sample.bodies )
    {
        frame["bodies"].push_back( BodyJson( body ) );
    }
    return frame;
}

Json FrameJson( const ReplaySolverFrameSample& sample )
{
    char solverHashBuffer[24] = {};
    char presentationHashBuffer[24] = {};
    Json frame;
    frame["frameIndex"] = sample.frameIndex;
    frame["eventCursor"] = sample.eventCursor;
    frame["sceneFrame"] = sample.sceneFrame;
    frame["simulationSeconds"] = sample.simulationSeconds;
    frame["physicsDt"] = sample.physicsDt;
    frame["solverHash"] = HashText( sample.solverHash, solverHashBuffer );
    frame["presentationHash"] = HashText( sample.presentationHash, presentationHashBuffer );
    frame["contactCount"] = sample.contactCount;
    frame["pipelineRecordCount"] = sample.pipelineRecordCount;
    frame["checkpointBoundary"] = sample.checkpointBoundary;
    frame["branch"] = BranchJson( sample.branch );
    frame["world"] = WorldJson( sample.world );
    frame["camera"] = CameraJson( sample.camera );
    frame["launcherVisual"] = LauncherVisualSummaryJson( sample.launcherVisual );
    frame["authoritativeSnapshot"] = SolverWorldSnapshotSummaryJson( sample.worldSnapshot );
    frame["bodies"] = Json::array();
    for ( const ReplaySolverBodySample& body : sample.bodies )
    {
        frame["bodies"].push_back( BodyJson( body ) );
    }
    return frame;
}
} // namespace

bool ReplayExporter::Save( const ReplayRecorder& recorder, const char* path )
{
    std::vector<ReplayPresentationSample> samples;
    recorder.CopySamplesChronological( samples );
    if ( samples.empty() )
    {
        return false;
    }

    std::ofstream output;
    if ( !RuntimeFileWriter::OpenTextFile( path, output ) )
    {
        return false;
    }

    const ReplayRecorderStats stats = recorder.GetStats();
    char latestHash[24] = {};

    output << "{\n";
    output << "  \"format\": \"skullbonez.skreplay\",\n";
    output << "  \"version\": 1,\n";
    output << "  \"sampleCount\": " << static_cast<unsigned long long>( samples.size() ) << ",\n";
    output << "  \"sampleCapacity\": " << static_cast<unsigned long long>( stats.sampleCapacity ) << ",\n";
    output << "  \"totalFramesCaptured\": " << static_cast<unsigned long long>( stats.totalFramesCaptured ) << ",\n";
    output << "  \"totalFramesEvicted\": " << static_cast<unsigned long long>( stats.totalFramesEvicted ) << ",\n";
    output << "  \"latestStateHash\": \"" << HashText( stats.latestStateHash, latestHash ) << "\",\n";
    output << "  \"frames\": [\n";

    for ( std::size_t i = 0; i < samples.size(); ++i )
    {
        output << "    " << FrameJson( samples[i] ).dump();
        output << ( i + 1 < samples.size() ? "," : "" ) << "\n";
    }

    output << "  ]\n";
    output << "}\n";
    return output.good();
}

bool ReplayExporter::Save( const ReplaySolverRecorder& recorder, const char* path )
{
    std::vector<ReplaySolverFrameSample> samples;
    recorder.CopySamplesChronological( samples );
    if ( samples.empty() )
    {
        return false;
    }

    std::ofstream output;
    if ( !RuntimeFileWriter::OpenTextFile( path, output ) )
    {
        return false;
    }

    const ReplayRecorderStats stats = recorder.GetStats();
    char latestHash[24] = {};

    output << "{\n";
    output << "  \"format\": \"skullbonez.solver-skreplay\",\n";
    output << "  \"version\": 1,\n";
    output << "  \"sampleCount\": " << static_cast<unsigned long long>( samples.size() ) << ",\n";
    output << "  \"sampleCapacity\": " << static_cast<unsigned long long>( stats.sampleCapacity ) << ",\n";
    output << "  \"totalFramesCaptured\": " << static_cast<unsigned long long>( stats.totalFramesCaptured ) << ",\n";
    output << "  \"totalFramesEvicted\": " << static_cast<unsigned long long>( stats.totalFramesEvicted ) << ",\n";
    output << "  \"latestSolverHash\": \"" << HashText( stats.latestStateHash, latestHash ) << "\",\n";
    output << "  \"frames\": [\n";

    for ( std::size_t i = 0; i < samples.size(); ++i )
    {
        output << "    " << FrameJson( samples[i] ).dump();
        output << ( i + 1 < samples.size() ? "," : "" ) << "\n";
    }

    output << "  ]\n";
    output << "}\n";
    return output.good();
}
