//
// File: SkullbonezTests/TestReplayRecorderFullCaptureBoundary.cpp
// Purpose:
//   Locks the ReplayRecorder unit harness boundary around solver-sample mirroring.
//
// Mental model:
//   ReplayRecorder contains both a lightweight solver-sample mirror and a full
//   runtime capture path. The unit harness exercises the mirror path directly;
//   live camera/world/entity/model owner traversal remains integration behavior.
//
// Glossary:
//   Solver-sample mirror: Replay path that copies an already-built solver frame
//     into presentation retention without walking runtime owners again.
//   Full-capture boundary: Runtime owner traversal through cameras, world,
//     scene entities, and collection methods this unit target deliberately omits.
//   Owner hook: Method on a live runtime owner that full replay capture reads.
//
// Invariants:
//   - Solver-sample mirror tests must not call full-capture owner hooks.
//   - Any accidental full-capture call in this unit target fails loudly.
//
// Related:
//   - SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp
//   - SkullbonezTests/TestReplayRecorder.cpp
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Scene/SceneController.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Runtime/CameraCollection.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayRecorder.h"
#include "../SkullbonezSource/World/WorldEnvironment.h"

#include <stdexcept>
#include <string>

using SkullbonezCore::Basics::ReplayFrameIndex;
using SkullbonezCore::Basics::ReplayRecorder;
using SkullbonezCore::Basics::ReplayRecorderConfig;
using SkullbonezCore::Basics::ReplaySolverBodySample;
using SkullbonezCore::Basics::ReplaySolverFrameSample;
using SkullbonezCore::Math::Vector::Vector3;

namespace
{
[[noreturn]] void ThrowUnexpectedReplayIntegrationCall( const char* methodName )
{
    throw std::runtime_error( std::string( "ReplayRecorder unit boundary: unexpected full replay capture call: " ) +
                              methodName );
}

ReplayRecorderConfig OneBodyRecorderConfig()
{
    ReplayRecorderConfig config;
    config.enabled = true;
    config.retentionSeconds = 1;
    config.checkpointIntervalFrames = 30;
    config.runtimeBodyCapacity = 1;
    return config;
}

ReplaySolverFrameSample OneBodySolverSample( ReplayFrameIndex frameIndex )
{
    ReplaySolverFrameSample sample;
    sample.frameIndex = frameIndex;
    sample.presentationHash = 0x12340000ull + frameIndex;
    sample.physicsDt = 1.0f / 120.0f;
    sample.world.gravity = -9.8f;

    ReplaySolverBodySample body;
    body.id.value = 400u + static_cast<uint32_t>( frameIndex );
    body.modelRow = SkullbonezCore::Physics::MakeModelRowHint( 0 );
    body.position = Vector3( 1.0f, 2.0f, 3.0f );
    body.linearVelocity = Vector3( 0.5f, 0.0f, 0.0f );
    body.mass = 2.0f;
    sample.bodies.push_back( body );
    return sample;
}
} // namespace

namespace SkullbonezCore
{
namespace Environment
{
const Math::Vector::Vector3& CameraCollection::GetCameraView() const
{
    // Hazard: reaching any owner hook means the unit target crossed from
    // solver-sample mirroring into full runtime capture without real owners.
    ThrowUnexpectedReplayIntegrationCall( "CameraCollection::GetCameraView" );
}

const Math::Vector::Vector3& CameraCollection::GetCameraTranslation() const
{
    ThrowUnexpectedReplayIntegrationCall( "CameraCollection::GetCameraTranslation" );
}

const Math::Vector::Vector3& CameraCollection::GetCameraUp() const
{
    ThrowUnexpectedReplayIntegrationCall( "CameraCollection::GetCameraUp" );
}

float WorldEnvironment::GetFluidSurfaceHeight() const
{
    ThrowUnexpectedReplayIntegrationCall( "WorldEnvironment::GetFluidSurfaceHeight" );
}

float WorldEnvironment::GetGravity() const
{
    ThrowUnexpectedReplayIntegrationCall( "WorldEnvironment::GetGravity" );
}

float WorldEnvironment::GetFluidDensity() const
{
    ThrowUnexpectedReplayIntegrationCall( "WorldEnvironment::GetFluidDensity" );
}
} // namespace Environment

} // namespace SkullbonezCore

TEST_CASE( "ReplayRecorder: solver mirror does not cross the full-capture boundary" )
{
    ReplayRecorder recorder;
    REQUIRE( recorder.Configure( OneBodyRecorderConfig() ) );

    recorder.CaptureFrameFromSolverSample( OneBodySolverSample( 5u ) );

    const auto stats = recorder.GetStats();
    CHECK( stats.sampleCount == 1u );
    CHECK( stats.nextFrameIndex == 6u );
    CHECK( stats.latestStateHash == 0x12340005ull );
    REQUIRE( recorder.LatestSample() != nullptr );
    CHECK( recorder.LatestSample()->frameIndex == 5u );
    CHECK( recorder.LatestSample()->bodies.size() == 1u );
}
