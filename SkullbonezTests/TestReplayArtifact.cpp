//
// File: SkullbonezTests/TestReplayArtifact.cpp
// Purpose:
//   Locks replay artifact round-trip and adversarial chunk-table behavior.
//
// Summary:
//   Tests create presentation and solver artifacts through the production
//   writer, then mutate isolated header/table fields to prove every malformed
//   file is rejected as a recoverable false result without partially published
//   rows. The full-track fixture also restores a v3 point-joint checkpoint after
//   a cold topology rebuild so persisted identity is tested across handle epochs.
//
// Glossary:
//   Chunk table: Fixed-width directory mapping four-byte tags to payload ranges.
//   Canonical encode: Identical retained samples produce byte-identical files.
//   Recoverable rejection: Loader returns false and clears caller-owned output.
//
// Invariants:
//   - Writer output is byte-identical for identical recorder state.
//   - Header versions and every chunk range are validated before payload reads.
//   - Required chunk duplication cannot make the first matching tag authoritative.
//   - A writer-made full artifact round-trips presentation, solver checkpoints,
//     events, hashes, branch identity, and launcher visuals through public loaders.
//   - Point-joint checkpoints use durable body ids and topology order, carry
//     sparse impulse deltas, affect solver hashes, and reproduce the next step
//     after bodies and constraints are recreated with new handles.
//
// Related:
//   - SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp
//   - SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Core/SbDiagnosticStore.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Physics/PhysicsWorldForces.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h"
#include "../SkullbonezSource/Runtime/Scene/SceneEntityStore.h"
#include "../SkullbonezSource/World/Terrain.h"
#include "TestCollisionShapeFixtures.h"

#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Runtime::ReplayBodyPresentationSample;
using SkullbonezCore::Runtime::ReplayBodyShapeKind;
using SkullbonezCore::Runtime::ReplayEventSample;
using SkullbonezCore::Runtime::ReplayFrameIndex;
using SkullbonezCore::Runtime::ReplayPresentationSample;
using SkullbonezCore::Runtime::ReplayRecorder;
using SkullbonezCore::Runtime::ReplayRecorderConfig;
using SkullbonezCore::Runtime::ReplayV2SolverHashSample;
using SkullbonezCore::Runtime::ReplayVisualArchiveSample;
namespace ReplayRecorderOperations = SkullbonezCore::Runtime::ReplayRecorderOperations;

namespace
{
SkullbonezCore::Core::SbDiagnosticStore diagnostics;
}
using SkullbonezCore::Physics::MakeColliderCreateDesc;
using SkullbonezCore::Physics::MakePhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsEngine;
using SkullbonezCore::Physics::PhysicsSceneObjectId;
using SkullbonezCore::Runtime::ReplaySolverBodySample;
using SkullbonezCore::Runtime::ReplaySolverFrameSample;
using SkullbonezCore::Runtime::ReplayV2Artifact;
using SkullbonezCore::Runtime::ReplayV2LoadResult;
using SkullbonezCore::Runtime::ReplayV2SaveResult;
using SkullbonezCore::Threading::LockOrderValidator;
using SkullbonezCore::Threading::WorkerPool;
using SkullbonezTests::CollisionShapeFixtures::SphereShape;
using namespace SkullbonezCore::Runtime;

namespace
{
constexpr std::size_t kVersionOffset = 8u;
constexpr std::size_t kChunkCountOffset = 16u;
constexpr std::size_t kChunkTableOffsetOffset = 24u;
constexpr std::size_t kFileSizeOffset = 32u;
constexpr std::size_t kChunkEntryBytes = 28u;
constexpr std::size_t kChunkSizeOffset = 12u;
constexpr std::size_t kChunkRecordCountOffset = 20u;

// Invariant: the downgrade fixture edits the documented PRES ABI directly.
// These offsets must move with the writer layout constants and its round-trip
// assertions, or the legacy-hash test must fail rather than patch another row.
constexpr std::size_t kChunkPayloadOffset = 4u;
constexpr std::size_t kPresentationFrameHeaderBytes = 92u;
constexpr std::size_t kPresentationBodyBytes = 76u;
constexpr std::size_t kPresentationStateHashOffset = 24u;
constexpr std::size_t kPresentationBodyOrientationOffset = 16u;

std::string ArtifactPath( const char* leaf )
{

    // RuntimeFileWriter intentionally creates one parent directory rather than
    // recursively interpreting an arbitrary path, so the fixture uses one
    // owned directory directly beneath the existing TestOutput root.
    return std::string( "TestOutput/replay_artifact_unit/" ) + leaf;
}

ReplaySolverFrameSample MakeArtifactSample( ReplayFrameIndex frameIndex )
{
    ReplaySolverFrameSample sample;
    sample.frameIndex = frameIndex;
    sample.sceneFrame = 40 + static_cast<int>( frameIndex );
    sample.simulationSeconds = static_cast<double>( frameIndex ) / 120.0;
    sample.physicsDt = 1.0f / 120.0f;
    sample.branch.branchId = 7u;
    sample.branch.parentBranchId = 3u;

    ReplaySolverBodySample body;
    body.id.value = 900u;
    body.modelRow = SkullbonezCore::Physics::MakeModelRowHint( 0 );
    body.shapeKind = ReplayBodyShapeKind::Box;
    body.position = Vector3( static_cast<float>( frameIndex ), 2.0f, -3.0f );
    body.orientation[0] = 0.125f;
    body.orientation[1] = -0.25f;
    body.orientation[2] = 0.5f;
    body.orientation[3] = 0.75f;
    body.linearVelocity = Vector3( 1.0f, 0.0f, 0.0f );
    body.mass = 2.0f;
    body.inverseMass = 0.5f;
    sample.bodies.push_back( body );

    // Invariant: CaptureFrameFromSolverSample trusts the solver-owned hash.
    // Build that hash from the exact presentation projection so a writer-made
    // artifact is valid before the adversarial cases mutate it.
    ReplayPresentationSample presentation;
    presentation.world = sample.world;
    presentation.contactCount = sample.contactCount;
    presentation.pipelineRecordCount = sample.pipelineRecordCount;
    ReplayBodyPresentationSample presentationBody;
    presentationBody.id = body.id;
    presentationBody.modelRow = body.modelRow;
    presentationBody.shapeKind = body.shapeKind;
    presentationBody.position = body.position;
    presentationBody.linearVelocity = body.linearVelocity;
    presentationBody.angularVelocity = body.angularVelocity;
    std::memcpy( presentationBody.orientation, body.orientation, sizeof( body.orientation ) );
    presentationBody.mass = body.mass;
    presentationBody.fixed = body.fixed;
    presentationBody.sleeping = body.sleeping;
    presentationBody.sleepSupported = body.sleepSupported;
    presentationBody.sleepInhibited = body.sleepInhibited;
    presentationBody.collisionContact = body.collisionContact;
    presentationBody.sleepIslandVisualId = body.sleepIslandVisualId;
    presentationBody.contactCount = body.contactCount;
    presentationBody.maxPenetration = body.maxPenetration;
    presentationBody.normalImpulseSum = body.normalImpulseSum;
    presentation.bodies.push_back( presentationBody );
    sample.presentationHash = ReplayRecorderOperations::ComputePresentationStateHash( presentation );
    return sample;
}

ReplayRecorder MakeArtifactRecorder()
{
    ReplayRecorderConfig config;
    config.enabled = true;
    config.retentionSeconds = 1;
    config.checkpointIntervalFrames = 30;
    config.runtimeBodyCapacity = 1;
    ReplayRecorder recorder;
    REQUIRE( recorder.Configure( config ) );
    recorder.ResetTimeline( "unit-artifact" );
    recorder.CaptureFrameFromSolverSample( MakeArtifactSample( 10u ) );
    recorder.CaptureFrameFromSolverSample( MakeArtifactSample( 11u ) );
    return recorder;
}

std::vector<uint8_t> ReadFile( const std::string& path )
{
    std::ifstream input( path, std::ios::binary | std::ios::ate );
    REQUIRE( input.is_open() );
    const std::streamoff size = input.tellg();
    REQUIRE( size > 0 );
    std::vector<uint8_t> bytes( static_cast<std::size_t>( size ) );
    input.seekg( 0, std::ios::beg );
    input.read( reinterpret_cast<char*>( bytes.data() ), size );
    REQUIRE( input.gcount() == size );
    return bytes;
}

void WriteFile( const std::string& path, const std::vector<uint8_t>& bytes )
{
    std::ofstream output( path, std::ios::binary | std::ios::trunc );
    REQUIRE( output.is_open() );
    output.write( reinterpret_cast<const char*>( bytes.data() ), static_cast<std::streamsize>( bytes.size() ) );
    REQUIRE( output.good() );
}

template <typename T> T ReadValue( const std::vector<uint8_t>& bytes, std::size_t offset )
{
    REQUIRE( offset + sizeof( T ) <= bytes.size() );
    T value {};
    std::memcpy( &value, bytes.data() + offset, sizeof( value ) );
    return value;
}

template <typename T> void WriteValue( std::vector<uint8_t>& bytes, std::size_t offset, T value )
{
    REQUIRE( offset + sizeof( T ) <= bytes.size() );
    std::memcpy( bytes.data() + offset, &value, sizeof( value ) );
}

std::size_t FindChunkEntry( const std::vector<uint8_t>& bytes, const char id[4] )
{
    const uint32_t count = ReadValue<uint32_t>( bytes, kChunkCountOffset );
    const uint64_t tableOffset = ReadValue<uint64_t>( bytes, kChunkTableOffsetOffset );

    for ( uint32_t chunkIndex = 0; chunkIndex < count; ++chunkIndex )
    {
        const std::size_t entry = static_cast<std::size_t>( tableOffset ) + chunkIndex * kChunkEntryBytes;
        REQUIRE( entry + kChunkEntryBytes <= bytes.size() );

        if ( std::memcmp( bytes.data() + entry, id, 4u ) == 0 )
        {
            return entry;
        }
    }

    FAIL( "required chunk entry was not found" );
    return 0u;
}

void CheckRejected( const std::string& path, const std::vector<uint8_t>& bytes )
{
    WriteFile( path, bytes );
    std::vector<ReplayPresentationSample> output( 1 );
    CHECK_FALSE( ReplayV2Artifact::LoadPresentation( path.c_str(), output ) );
    CHECK( output.empty() );
}

void DowngradePresentationQuaternionsToV3( std::vector<uint8_t>& bytes,
                                           const std::vector<ReplayPresentationSample>& canonicalSamples )
{
    WriteValue<uint32_t>( bytes, kVersionOffset, 3u );
    const std::size_t entry = FindChunkEntry( bytes, "PRES" );
    const uint64_t payloadOffset = ReadValue<uint64_t>( bytes, entry + kChunkPayloadOffset );
    REQUIRE( ReadValue<uint32_t>( bytes, static_cast<std::size_t>( payloadOffset ) ) == canonicalSamples.size() );

    std::size_t frameOffset = static_cast<std::size_t>( payloadOffset ) + sizeof( uint32_t );

    for ( const ReplayPresentationSample& canonical : canonicalSamples )
    {
        REQUIRE( canonical.bodies.size() == 1u );
        ReplayPresentationSample legacy = canonical;
        SkullbonezCore::Math::Orientation::ConjugateQuaternionVectorPart(
            legacy.bodies[0].orientation[0], legacy.bodies[0].orientation[1], legacy.bodies[0].orientation[2] );
        legacy.stateHash = ReplayRecorderOperations::ComputePresentationStateHash( legacy );
        WriteValue<uint64_t>( bytes, frameOffset + kPresentationStateHashOffset, legacy.stateHash );

        const std::size_t orientationOffset =
            frameOffset + kPresentationFrameHeaderBytes + kPresentationBodyOrientationOffset;
        WriteValue<float>( bytes, orientationOffset + 0u, legacy.bodies[0].orientation[0] );
        WriteValue<float>( bytes, orientationOffset + 4u, legacy.bodies[0].orientation[1] );
        WriteValue<float>( bytes, orientationOffset + 8u, legacy.bodies[0].orientation[2] );
        frameOffset += kPresentationFrameHeaderBytes + kPresentationBodyBytes;
    }
}
} // namespace

TEST_CASE( "Replay artifact codec: presentation round-trip is complete and byte-canonical" )
{
    ReplayRecorder recorder = MakeArtifactRecorder();
    const std::string firstPath = ArtifactPath( "canonical_a.skreplay" );
    const std::string secondPath = ArtifactPath( "canonical_b.skreplay" );
    ReplayV2SaveResult saveResult;
    REQUIRE( ReplayV2Artifact::SavePresentation( recorder, firstPath.c_str(), &saveResult ) );
    REQUIRE( ReplayV2Artifact::SavePresentation( recorder, secondPath.c_str() ) );

    const std::vector<uint8_t> firstBytes = ReadFile( firstPath );
    const std::vector<uint8_t> secondBytes = ReadFile( secondPath );
    CHECK( firstBytes == secondBytes );
    CHECK( ReadValue<uint32_t>( firstBytes, kVersionOffset ) == 5u );
    CHECK( saveResult.sampleCount == 2u );
    CHECK( saveResult.bodyDictionaryCount == 1u );
    CHECK( saveResult.fileBytes == firstBytes.size() );

    std::vector<ReplayPresentationSample> loaded;
    ReplayV2LoadResult loadResult;
    REQUIRE( ReplayV2Artifact::LoadPresentation( firstPath.c_str(), loaded, &loadResult ) );
    REQUIRE( loaded.size() == 2u );
    CHECK( loadResult.firstFrame == 10u );
    CHECK( loadResult.lastFrame == 11u );
    REQUIRE( loaded[0].bodies.size() == 1u );
    CHECK( loaded[0].bodies[0].id.value == 900u );
    CHECK( loaded[1].bodies[0].position.x == doctest::Approx( 11.0f ) );
    CHECK( loaded[0].bodies[0].orientation[0] == doctest::Approx( 0.125f ) );
    CHECK( loaded[0].bodies[0].orientation[1] == doctest::Approx( -0.25f ) );
    CHECK( loaded[0].bodies[0].orientation[2] == doctest::Approx( 0.5f ) );
    CHECK( loaded[0].bodies[0].orientation[3] == doctest::Approx( 0.75f ) );

    // The base writer intentionally omits every optional stream. Each
    // chunk-specific loader must distinguish absence from a valid empty track
    // and clear stale caller storage on the failure path.
    std::vector<ReplaySolverFrameSample> checkpoints( 1 );
    std::vector<ReplayEventSample> events( 1 );
    std::vector<ReplayV2SolverHashSample> hashes( 1 );
    std::vector<ReplayVisualArchiveSample> packets( 1 );
    std::vector<uint8_t> predictionState( 1u, 0xFFu );
    CHECK_FALSE( ReplayV2Artifact::LoadSolverCheckpoints( firstPath.c_str(), checkpoints ) );
    CHECK_FALSE( ReplayV2Artifact::LoadEvents( firstPath.c_str(), events ) );
    CHECK_FALSE( ReplayV2Artifact::LoadSolverHashes( firstPath.c_str(), hashes ) );
    CHECK_FALSE( ReplayV2Artifact::LoadVisualPackets( firstPath.c_str(), packets ) );
    CHECK_FALSE( ReplayV2Artifact::LoadVisualPredictionState( firstPath.c_str(), predictionState ) );
    CHECK( checkpoints.empty() );
    CHECK( events.empty() );
    CHECK( hashes.empty() );
    CHECK( packets.empty() );
    CHECK( predictionState.empty() );
}

TEST_CASE( "Replay artifact codec: v3 quaternion bytes migrate after historical hash validation" )
{
    ReplayRecorder recorder = MakeArtifactRecorder();
    const std::string currentPath = ArtifactPath( "quaternion_v5_source.skreplay" );
    const std::string legacyPath = ArtifactPath( "quaternion_v3_legacy.skreplay" );
    REQUIRE( ReplayV2Artifact::SavePresentation( recorder, currentPath.c_str() ) );

    std::vector<ReplayPresentationSample> canonicalSamples;
    REQUIRE( ReplayV2Artifact::LoadPresentation( currentPath.c_str(), canonicalSamples ) );
    std::vector<uint8_t> bytes = ReadFile( currentPath );
    DowngradePresentationQuaternionsToV3( bytes, canonicalSamples );
    WriteFile( legacyPath, bytes );

    std::vector<ReplayPresentationSample> migrated;
    REQUIRE( ReplayV2Artifact::LoadPresentation( legacyPath.c_str(), migrated ) );
    REQUIRE( migrated.size() == canonicalSamples.size() );
    REQUIRE( migrated[0].bodies.size() == 1u );
    CHECK( migrated[0].bodies[0].orientation[0] == doctest::Approx( 0.125f ) );
    CHECK( migrated[0].bodies[0].orientation[1] == doctest::Approx( -0.25f ) );
    CHECK( migrated[0].bodies[0].orientation[2] == doctest::Approx( 0.5f ) );
    CHECK( migrated[0].bodies[0].orientation[3] == doctest::Approx( 0.75f ) );
    CHECK( migrated[0].stateHash == canonicalSamples[0].stateHash );
}

TEST_CASE( "Replay artifact codec: empty recorder is not a zero-count artifact" )
{
    ReplayRecorderConfig config;
    config.enabled = true;
    config.retentionSeconds = 1;
    config.runtimeBodyCapacity = 1;
    ReplayRecorder empty;
    REQUIRE( empty.Configure( config ) );
    CHECK_FALSE( ReplayV2Artifact::SavePresentation( empty, ArtifactPath( "empty.skreplay" ).c_str() ) );
}

TEST_CASE( "Replay artifact codec: malformed header and table ranges fail closed" )
{
    ReplayRecorder recorder = MakeArtifactRecorder();
    const std::string sourcePath = ArtifactPath( "adversarial_source.skreplay" );
    REQUIRE( ReplayV2Artifact::SavePresentation( recorder, sourcePath.c_str() ) );
    const std::vector<uint8_t> canonical = ReadFile( sourcePath );

    SUBCASE( "truncated payload" )
    {
        std::vector<uint8_t> bytes = canonical;

        bytes.pop_back();
        CheckRejected( ArtifactPath( "truncated.skreplay" ), bytes );
    }
    SUBCASE( "future version" )
    {
        std::vector<uint8_t> bytes = canonical;

        WriteValue<uint32_t>( bytes, kVersionOffset, 6u );
        CheckRejected( ArtifactPath( "future.skreplay" ), bytes );
    }
    SUBCASE( "chunk length past EOF" )
    {
        std::vector<uint8_t> bytes = canonical;

        const std::size_t bodyEntry = FindChunkEntry( bytes, "BODY" );
        WriteValue<uint64_t>( bytes, bodyEntry + kChunkSizeOffset, ( std::numeric_limits<uint64_t>::max )() );
        CheckRejected( ArtifactPath( "past_eof.skreplay" ), bytes );
    }
    SUBCASE( "duplicate BODY tag" )
    {
        std::vector<uint8_t> bytes = canonical;

        const std::size_t manifestEntry = FindChunkEntry( bytes, "MANI" );
        std::memcpy( bytes.data() + manifestEntry, "BODY", 4u );
        CheckRejected( ArtifactPath( "duplicate_tag.skreplay" ), bytes );
    }
    SUBCASE( "zero count disagrees with payload" )
    {
        std::vector<uint8_t> bytes = canonical;

        const std::size_t bodyEntry = FindChunkEntry( bytes, "BODY" );
        WriteValue<uint32_t>( bytes, bodyEntry + kChunkRecordCountOffset, 0u );
        CheckRejected( ArtifactPath( "zero_count.skreplay" ), bytes );
    }
    SUBCASE( "header file size disagrees" )
    {
        std::vector<uint8_t> bytes = canonical;

        WriteValue<uint64_t>( bytes, kFileSizeOffset, static_cast<uint64_t>( bytes.size() + 1u ) );
        CheckRejected( ArtifactPath( "wrong_file_size.skreplay" ), bytes );
    }
}


namespace
{
std::string FullArtifactPath()
{
    return "TestOutput/coverage_floor_unit/full_tracks.skreplay";
}
} // namespace


TEST_CASE( "Coverage floor contract: full replay tracks round-trip owner values" )
{
    auto engineStorage = std::make_unique<PhysicsEngine>();
    PhysicsEngine& engine = *engineStorage;
    engine.Clear();
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        engine.ReserveAuthoredBodyCapacity( 2, 2, 0, 0, 1 );
    }

    const SkullbonezCore::Math::CollisionDetection::CollisionShape shape = SphereShape( 1.0f );
    auto bodyDesc = MakePhysicsBodyCreateDesc( PhysicsSceneObjectId { 501u }, shape, Vector3( 0.0f, 4.0f, 0.0f ),
                                               SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION,
                                               Vector3( 1.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.25f, 0.0f ),
                                               Vector3( 0.8f, 0.8f, 0.8f ), 2.0f, 0.25f, PhysicsBodyMotionKind::Dynamic,
                                               "coverage-artifact-body" );

    auto colliderDesc = MakeColliderCreateDesc( shape, 0.25f, 4u, "coverage-artifact" );
    colliderDesc.sceneObjectId = bodyDesc.sceneObjectId;
    SkullbonezCore::Physics::PhysicsAuthoredBodyRegistration registration;
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        registration = engine.RegisterAuthoredBody( bodyDesc, colliderDesc );
    }
    REQUIRE( registration.IsValid() );

    auto secondBodyDesc = bodyDesc;
    secondBodyDesc.sceneObjectId = PhysicsSceneObjectId { 502u };
    secondBodyDesc.position = Vector3( 0.0f, 7.0f, 0.0f );
    auto secondColliderDesc = colliderDesc;
    secondColliderDesc.sceneObjectId = secondBodyDesc.sceneObjectId;
    SkullbonezCore::Physics::PhysicsAuthoredBodyRegistration secondRegistration;
    SkullbonezCore::Physics::PhysicsPointJointCreateDesc joint;
    SkullbonezCore::Physics::PhysicsConstraintHandle originalJointHandle;
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        secondRegistration = engine.RegisterAuthoredBody( secondBodyDesc, secondColliderDesc );
        REQUIRE( secondRegistration.IsValid() );

        joint.bodyA = registration.body;
        joint.bodyB = secondRegistration.body;
        joint.localAnchorA = Vector3( 0.0f, 1.0f, 0.0f );
        joint.localAnchorB = Vector3( 0.0f, -1.0f, 0.0f );
        joint.slack = 0.125f;
        joint.stiffness = 0.375f;
        joint.damping = 0.625f;
        joint.groupId = 7u;
        joint.flags = 1u;
        originalJointHandle = engine.CreatePointJoint( joint );
        REQUIRE( originalJointHandle.IsValid() );
    }

    SceneEntityStore entities( diagnostics );
    entities.ConfigureCapacity( 2 );
    SceneEntityCreateDesc entity;
    entity.sceneObjectId = bodyDesc.sceneObjectId;
    entity.SetName( "coverage_artifact_body" );
    REQUIRE( entities.PreflightAppend( entity ).Ok() );
    entities.CommitAppend( entity, registration.body );
    entity.sceneObjectId = secondBodyDesc.sceneObjectId;
    entity.SetName( "coverage_artifact_body_2" );
    REQUIRE( entities.PreflightAppend( entity ).Ok() );
    entities.CommitAppend( entity, secondRegistration.body );

    SkullbonezCore::Physics::PhysicsWorldForces forces;
    SkullbonezCore::Core::EngineConfig terrainConfig;
    SkullbonezCore::Geometry::Terrain terrain( -100000.0f, 0.0f, 0.0f, terrainConfig );
    engine.SetTerrainView( terrain.PhysicsView() );
    LockOrderValidator lockOrderValidator;
    WorkerPool workerPool( lockOrderValidator );
    engine.Step( 1.0f / 120.0f, forces, workerPool, SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter {} );

    ReplayRecorderConfig config;
    config.enabled = true;
    config.retentionSeconds = 1;
    config.checkpointIntervalFrames = 2;
    config.runtimeBodyCapacity = 2;
    ReplaySolverRecorder solver;
    ReplayRecorder presentation;
    ReplayEventRecorder events;
    REQUIRE( solver.Configure( config ) );
    REQUIRE( presentation.Configure( config ) );
    REQUIRE( events.Configure( config ) );
    solver.ResetTimeline( "coverage-floor" );
    presentation.ResetTimeline( "coverage-floor" );
    events.ResetTimeline( "coverage-floor" );

    ReplayLauncherVisualSample launcher;
    launcher.fireMode = ReplayLauncherFireMode::Projectile;
    launcher.visualizeRays = true;
    launcher.impulseStrength = 42.0f;
    launcher.projectileSpeed = 84.0f;
    ReplayRayCastLineSample ray;
    ray.start = Vector3( 1.0f, 2.0f, 3.0f );
    ray.end = Vector3( 4.0f, 5.0f, 6.0f );
    ray.ageSeconds = 0.5f;
    ray.active = true;
    ray.hit = true;
    launcher.rayLines.push_back( ray );
    LauncherLaserShotSnapshot shot;
    shot.start = Vector3( -1.0f, 2.0f, 0.0f );
    shot.end = Vector3( 3.0f, 2.0f, 0.0f );
    shot.cameraRight = Vector3( 1.0f, 0.0f, 0.0f );
    shot.cameraUp = Vector3( 0.0f, 1.0f, 0.0f );
    shot.ageSeconds = 0.25f;
    shot.lifetimeSeconds = 1.0f;
    shot.active = true;
    launcher.laserShots.push_back( shot );

    SkullbonezCore::Gameplay::TornadoGameplay tornadoGameplay;
    ReplayBranchInfo captureBranch;
    captureBranch.branchId = 9u;
    captureBranch.parentBranchId = 4u;
    ReplayWorldPresentationSample captureWorld;
    captureWorld.fixedStep = true;
    ReplayCameraSample captureCamera;

    solver.CaptureFrame( captureBranch, 3u, 20, 1.0f / 120.0f, captureWorld, captureCamera, launcher, engine,
                         tornadoGameplay, entities, PhysicsEngine::ReadBodies( engine ),
                         PhysicsEngine::ReadColliders( engine ) );

    const ReplaySolverFrameSample* sample = solver.LatestSample();
    REQUIRE( sample != nullptr );
    REQUIRE( sample->worldSnapshot.physics.pointJoints.size() == 1u );
    const float capturedJointImpulse = sample->worldSnapshot.physics.pointJoints[0].accumulatedImpulse;
    REQUIRE( capturedJointImpulse != 0.0f );
    const uint64_t capturedSolverHash = sample->solverHash;
    presentation.CaptureFrameFromSolverSample( *sample );

    auto changedJointSnapshot = sample->worldSnapshot.physics;
    changedJointSnapshot.pointJoints[0].accumulatedImpulse += 1.0f;
    REQUIRE( engine.RestoreReplaySolverSnapshot( changedJointSnapshot,
                                                 SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt( 2 ) ) );

    ReplaySolverRecorder hashVerifier;
    REQUIRE( hashVerifier.Configure( config ) );
    hashVerifier.ResetTimeline( "coverage-floor-hash" );
    hashVerifier.CaptureFrame( captureBranch, 3u, 20, 1.0f / 120.0f, captureWorld, captureCamera, launcher, engine,
                               tornadoGameplay, entities, PhysicsEngine::ReadBodies( engine ),
                               PhysicsEngine::ReadColliders( engine ) );
    REQUIRE( hashVerifier.LatestSample() != nullptr );
    CHECK( hashVerifier.LatestSample()->solverHash != capturedSolverHash );
    REQUIRE( engine.RestoreReplaySolverSnapshot(
        sample->worldSnapshot.physics, SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt( 2 ) ) );

    const ReplaySolverFrameSample* historical = solver.SampleAtNormalized( 0.0f );
    REQUIRE( historical != nullptr );
    const uint64_t firstResolveCount = solver.GetStats().denseSampleResolveCount;
    CHECK( firstResolveCount == 1u );
    CHECK( solver.SampleAtNormalized( 0.0f ) == historical );
    CHECK( solver.GetStats().denseSampleResolveCount == firstResolveCount );

    REQUIRE( engine.SetBodyVelocity( registration.body, Vector3( 2.0f, 1.0f, -1.0f ), Vector3( 0.1f, 0.2f, 0.3f ), true ) );
    auto changedDeltaSnapshot = sample->worldSnapshot.physics;
    changedDeltaSnapshot.pointJoints[0].accumulatedImpulse += 0.5f;
    const float changedDeltaImpulse = changedDeltaSnapshot.pointJoints[0].accumulatedImpulse;
    REQUIRE( engine.RestoreReplaySolverSnapshot(
        changedDeltaSnapshot, SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt( 2 ) ) );
    solver.CaptureFrame( captureBranch, 4u, 21, 1.0f / 120.0f, captureWorld, captureCamera, launcher, engine,
                         tornadoGameplay, entities, PhysicsEngine::ReadBodies( engine ),
                         PhysicsEngine::ReadColliders( engine ) );

    sample = solver.LatestSample();
    REQUIRE( sample != nullptr );
    presentation.CaptureFrameFromSolverSample( *sample );

    historical = solver.SampleAtNormalized( 0.0f );
    REQUIRE( historical != nullptr );
    CHECK( solver.GetStats().denseSampleResolveCount == firstResolveCount + 1u );
    const ReplaySolverFrameSample* resolvedDelta = solver.SampleAtNormalized( 1.0f );
    REQUIRE( resolvedDelta != nullptr );
    REQUIRE( resolvedDelta->worldSnapshot.physics.pointJoints.size() == 1u );
    CHECK( std::memcmp( &resolvedDelta->worldSnapshot.physics.pointJoints[0].accumulatedImpulse,
                        &changedDeltaImpulse, sizeof( changedDeltaImpulse ) ) == 0 );

    for ( ReplayFrameIndex frame = 0u; frame < 2u; ++frame )
    {
        ReplayEventInput event;
        event.frameIndex = frame;
        event.branch = captureBranch;
        event.kind = ReplayEventKind::OwnerAction;
        event.flags = 5u + static_cast<uint32_t>( frame );
        event.value0 = 100 + static_cast<int32_t>( frame );
        event.data0 = 0xABC000u + frame;
        event.text = frame == 0u ? "first-owner-event" : "second-owner-event";
        events.RecordEvent( event );
    }

    ReplayV2SaveResult save;
    const std::string path = FullArtifactPath();
    REQUIRE( ReplayV2Artifact::SavePresentationWithSolverHashes( presentation, solver, events, path.c_str(), &save ) );
    CHECK( save.sampleCount == 2u );
    CHECK( save.solverHashCount == 2u );
    CHECK( save.solverCheckpointCount == 1u );
    CHECK( save.eventCount == 2u );
    CHECK( save.eventCursorCount == 1u );
    CHECK( save.fileBytes > 0u );

    std::vector<ReplayPresentationSample> loadedPresentation;
    ReplayV2LoadResult presentationResult;
    REQUIRE( ReplayV2Artifact::LoadPresentation( path.c_str(), loadedPresentation, &presentationResult ) );
    REQUIRE( loadedPresentation.size() == 2u );
    CHECK( presentationResult.firstFrame == 0u );
    CHECK( presentationResult.lastFrame == 1u );
    REQUIRE( loadedPresentation.back().bodies.size() == 2u );
    CHECK( loadedPresentation.back().bodies[0].id.value == 501u );
    CHECK( loadedPresentation.back().bodies[0].linearVelocity.x == doctest::Approx( 2.0f ) );

    std::vector<ReplaySolverFrameSample> checkpoints;
    ReplayV2SolverCheckpointLoadResult checkpointResult;
    REQUIRE( ReplayV2Artifact::LoadSolverCheckpoints( path.c_str(), checkpoints, &checkpointResult ) );
    REQUIRE( checkpoints.size() == 1u );
    CHECK( checkpointResult.firstFrame == 0u );
    CHECK( checkpointResult.lastFrame == 0u );
    CHECK( checkpoints[0].branch.branchId == 9u );
    CHECK( checkpoints[0].eventCursor == 3u );
    REQUIRE( checkpoints[0].bodies.size() == 2u );
    CHECK( checkpoints[0].launcherVisual.rayLines.size() == 1u );
    CHECK( checkpoints[0].launcherVisual.laserShots.size() == 1u );
    REQUIRE( checkpoints[0].worldSnapshot.physics.pointJoints.size() == 1u );
    const auto& loadedJoint = checkpoints[0].worldSnapshot.physics.pointJoints[0];
    CHECK( loadedJoint.topologyOrdinal == 0u );
    CHECK( loadedJoint.bodyASceneObjectId == bodyDesc.sceneObjectId );
    CHECK( loadedJoint.bodyBSceneObjectId == secondBodyDesc.sceneObjectId );
    CHECK( loadedJoint.localAnchorA == Vector3( 0.0f, 1.0f, 0.0f ) );
    CHECK( loadedJoint.localAnchorB == Vector3( 0.0f, -1.0f, 0.0f ) );
    CHECK( loadedJoint.slack == doctest::Approx( 0.125f ) );
    CHECK( loadedJoint.stiffness == doctest::Approx( 0.375f ) );
    CHECK( loadedJoint.damping == doctest::Approx( 0.625f ) );
    CHECK( std::memcmp( &loadedJoint.accumulatedImpulse, &capturedJointImpulse, sizeof( capturedJointImpulse ) ) == 0 );
    CHECK( loadedJoint.groupId == 7u );
    CHECK( loadedJoint.flags == 1u );

    // Invariant: disk checkpoints use scene ids plus filtered topology order,
    // not process-local handle generations. Recreate identical topology after
    // Clear, apply the loaded body/solver state, and require the next step to
    // match the original handle epoch bit-for-bit.
    const ReplaySolverFrameSample loadedCheckpoint = checkpoints[0];
    const auto applyLoadedBodies = [&]( SkullbonezCore::Physics::PhysicsBodyHandle bodyA,
                                        SkullbonezCore::Physics::PhysicsBodyHandle bodyB )
    {
        const SkullbonezCore::Physics::PhysicsBodyHandle handles[2] = { bodyA, bodyB };

        for ( std::size_t index = 0; index < loadedCheckpoint.bodies.size(); ++index )
        {
            const ReplaySolverBodySample& body = loadedCheckpoint.bodies[index];
            const SkullbonezCore::Physics::PhysicsBodyRestoreState restore {
                handles[index],
                body.id,
                body.fixed,
                body.position,
                SkullbonezCore::Math::Orientation::Quaternion( body.orientation[0], body.orientation[1],
                                                               body.orientation[2], body.orientation[3] ),
                body.linearVelocity,
                body.angularVelocity,
                body.mass,
                body.inverseMass,
                body.rotationalInertia,
                body.inverseRotationalInertia,
            };
            REQUIRE( engine.RestoreReplayBodyState( restore ) );
        }
    };

    REQUIRE( engine.RestoreReplaySolverSnapshot(
        loadedCheckpoint.worldSnapshot.physics, SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt( 2 ) ) );
    applyLoadedBodies( registration.body, secondRegistration.body );
    engine.Step( 1.0f / 120.0f, forces, workerPool, SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter {} );
    ReplaySolverRecorder expectedAfterRestoreRecorder;
    REQUIRE( expectedAfterRestoreRecorder.Configure( config ) );
    expectedAfterRestoreRecorder.ResetTimeline( "coverage-floor-cold-restore-expected" );
    expectedAfterRestoreRecorder.CaptureFrame(
        captureBranch, 3u, 20, 1.0f / 120.0f, captureWorld, captureCamera, launcher, engine, tornadoGameplay, entities,
        PhysicsEngine::ReadBodies( engine ), PhysicsEngine::ReadColliders( engine ) );
    REQUIRE( expectedAfterRestoreRecorder.LatestSample() != nullptr );
    const uint64_t expectedAfterRestoreHash = expectedAfterRestoreRecorder.LatestSample()->solverHash;

    const SkullbonezCore::Physics::PhysicsBodyHandle originalBodyA = registration.body;
    const SkullbonezCore::Physics::PhysicsBodyHandle originalBodyB = secondRegistration.body;
    engine.Clear();
    engine.SetTerrainView( terrain.PhysicsView() );
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        registration = engine.RegisterAuthoredBody( bodyDesc, colliderDesc );
        secondRegistration = engine.RegisterAuthoredBody( secondBodyDesc, secondColliderDesc );
        REQUIRE( registration.IsValid() );
        REQUIRE( secondRegistration.IsValid() );
        joint.bodyA = registration.body;
        joint.bodyB = secondRegistration.body;
        const SkullbonezCore::Physics::PhysicsConstraintHandle recreatedJointHandle = engine.CreatePointJoint( joint );
        REQUIRE( recreatedJointHandle.IsValid() );
        CHECK( recreatedJointHandle != originalJointHandle );
    }
    CHECK( registration.body != originalBodyA );
    CHECK( secondRegistration.body != originalBodyB );
    REQUIRE( engine.RestoreReplaySolverSnapshot(
        loadedCheckpoint.worldSnapshot.physics, SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt( 2 ) ) );
    applyLoadedBodies( registration.body, secondRegistration.body );
    engine.Step( 1.0f / 120.0f, forces, workerPool, SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter {} );
    entities.Clear();
    entity.sceneObjectId = bodyDesc.sceneObjectId;
    entity.SetName( "coverage_artifact_body" );
    REQUIRE( entities.PreflightAppend( entity ).Ok() );
    entities.CommitAppend( entity, registration.body );
    entity.sceneObjectId = secondBodyDesc.sceneObjectId;
    entity.SetName( "coverage_artifact_body_2" );
    REQUIRE( entities.PreflightAppend( entity ).Ok() );
    entities.CommitAppend( entity, secondRegistration.body );
    ReplaySolverRecorder recreatedAfterRestoreRecorder;
    REQUIRE( recreatedAfterRestoreRecorder.Configure( config ) );
    recreatedAfterRestoreRecorder.ResetTimeline( "coverage-floor-cold-restore-recreated" );
    recreatedAfterRestoreRecorder.CaptureFrame(
        captureBranch, 3u, 20, 1.0f / 120.0f, captureWorld, captureCamera, launcher, engine, tornadoGameplay, entities,
        PhysicsEngine::ReadBodies( engine ), PhysicsEngine::ReadColliders( engine ) );
    REQUIRE( recreatedAfterRestoreRecorder.LatestSample() != nullptr );
    CHECK( recreatedAfterRestoreRecorder.LatestSample()->solverHash == expectedAfterRestoreHash );

    std::vector<ReplayEventSample> loadedEvents;
    ReplayV2EventLoadResult eventResult;
    REQUIRE( ReplayV2Artifact::LoadEvents( path.c_str(), loadedEvents, &eventResult ) );
    REQUIRE( loadedEvents.size() == 2u );
    CHECK( loadedEvents[0].value0 == 100 );
    CHECK( std::string( loadedEvents[1].text ) == "second-owner-event" );

    std::vector<ReplayV2SolverHashSample> hashes;
    ReplayV2SolverHashLoadResult hashResult;
    REQUIRE( ReplayV2Artifact::LoadSolverHashes( path.c_str(), hashes, &hashResult ) );
    REQUIRE( hashes.size() == 2u );
    CHECK( hashes[0].checkpointBoundary );
    CHECK_FALSE( hashes[1].checkpointBoundary );
    CHECK( hashes[1].solverHash != 0u );
    CHECK( hashes[1].presentationHash != 0u );
}
