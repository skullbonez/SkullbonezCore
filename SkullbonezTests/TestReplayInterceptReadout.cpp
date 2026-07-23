/*
File: SkullbonezTests/TestReplayInterceptReadout.cpp
Purpose:
  Locks the Replay closest-approach scan and intercept classification.

Summary:
  Synthetic published frame prefixes exercise incremental scanning, stable
  tie-breaking, exact collider-radius thresholds, and prediction reset keys.

Glossary:
  Prefix extension: Publishing additional prediction frames without replacing
    the already-visible prefix.
  Strict intercept: Miss distance smaller than, not equal to, summed radii.

Invariants:
  - Tests use durable scene ids; model rows are only selection hints.
  - Generation, topology, and frame-bank changes discard the old minimum.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayInterceptReadout.h
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Replay/ReplayInterceptReadout.h"

#include <vector>

using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::ZERO_VECTOR;
using SkullbonezCore::Physics::PhysicsSceneObjectId;
using SkullbonezCore::Runtime::ReplayInterceptReadout;
using SkullbonezCore::Runtime::ReplayInterceptUpdateInput;
using SkullbonezCore::Runtime::RunReplayPredictionBodySample;
using SkullbonezCore::Runtime::RunReplayPredictionFrame;

namespace
{
constexpr PhysicsSceneObjectId SHIP_ID{ 11u };
constexpr PhysicsSceneObjectId TARGET_ID{ 22u };

RunReplayPredictionFrame MakeFrame( uint32_t frameIndex,
                                    const Vector3& shipPosition,
                                    const Vector3& targetPosition,
                                    const Vector3& shipVelocity = ZERO_VECTOR,
                                    const Vector3& targetVelocity = ZERO_VECTOR )
{
    RunReplayPredictionFrame frame;
    frame.frameIndex = frameIndex;
    RunReplayPredictionBodySample ship;
    ship.id = SHIP_ID;
    ship.position = shipPosition;
    ship.linearVelocity = shipVelocity;
    frame.bodies.push_back( ship );
    RunReplayPredictionBodySample target;
    target.id = TARGET_ID;
    target.position = targetPosition;
    target.linearVelocity = targetVelocity;
    frame.bodies.push_back( target );
    return frame;
}

ReplayInterceptUpdateInput MakeInput( const std::vector<RunReplayPredictionFrame>& frames )
{
    ReplayInterceptUpdateInput input;
    input.frames = frames;
    input.shipId = SHIP_ID;
    input.targetId = TARGET_ID;
    input.shipRadius = 1.0f;
    input.targetRadius = 1.0f;
    input.generation = 7u;
    input.topologyVersion = 9u;
    input.enabled = true;
    return input;
}
} // namespace

TEST_CASE( "Replay intercept readout scans only prefix extensions and keeps the earlier tie" )
{
    std::vector<RunReplayPredictionFrame> frames;
    frames.push_back( MakeFrame( 0u, Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 5.0f, 0.0f, 0.0f ) ) );
    frames.push_back( MakeFrame( 120u,
                                 Vector3( 2.0f, 0.0f, 0.0f ),
                                 Vector3( 5.0f, 0.0f, 0.0f ),
                                 Vector3( 2.0f, 0.0f, 0.0f ),
                                 Vector3( -1.0f, 0.0f, 0.0f ) ) );

    ReplayInterceptReadout readout;
    readout.Update( MakeInput( frames ) );
    CHECK( readout.View().closestFrame == 120u );
    CHECK( readout.View().missDistance == doctest::Approx( 3.0f ) );
    CHECK( readout.View().relativeSpeed == doctest::Approx( 3.0f ) );
    CHECK( readout.View().etaSeconds == doctest::Approx( 1.0f ) );

    frames.push_back( MakeFrame( 121u, Vector3( 2.0f, 0.0f, 0.0f ), Vector3( 5.0f, 0.0f, 0.0f ) ) );
    readout.Update( MakeInput( frames ) );
    CHECK( readout.View().closestFrame == 120u );
}

TEST_CASE( "Replay intercept classification uses a strict collider-radius threshold" )
{
    std::vector<RunReplayPredictionFrame> frames;
    frames.push_back( MakeFrame( 6u, ZERO_VECTOR, Vector3( 2.0f, 0.0f, 0.0f ) ) );
    ReplayInterceptReadout readout;
    readout.Update( MakeInput( frames ) );
    CHECK( readout.View().valid );
    CHECK_FALSE( readout.View().intercept );

    frames.push_back( MakeFrame( 7u, ZERO_VECTOR, Vector3( 1.5f, 0.0f, 0.0f ) ) );
    readout.Update( MakeInput( frames ) );
    CHECK( readout.View().intercept );
    CHECK( readout.View().missDistance == doctest::Approx( 1.5f ) );
}

TEST_CASE( "Replay intercept scan resets across generation topology and frame-bank changes" )
{
    std::vector<RunReplayPredictionFrame> frames;
    frames.push_back( MakeFrame( 3u, ZERO_VECTOR, Vector3( 1.0f, 0.0f, 0.0f ) ) );
    ReplayInterceptReadout readout;
    ReplayInterceptUpdateInput input = MakeInput( frames );
    readout.Update( input );
    CHECK( readout.View().missDistance == doctest::Approx( 1.0f ) );

    frames[0] = MakeFrame( 3u, ZERO_VECTOR, Vector3( 8.0f, 0.0f, 0.0f ) );
    input = MakeInput( frames );
    ++input.generation;
    readout.Update( input );
    CHECK( readout.View().missDistance == doctest::Approx( 8.0f ) );

    frames[0] = MakeFrame( 3u, ZERO_VECTOR, Vector3( 6.0f, 0.0f, 0.0f ) );
    ++input.topologyVersion;
    input.frames = frames;
    readout.Update( input );
    CHECK( readout.View().missDistance == doctest::Approx( 6.0f ) );

    frames[0] = MakeFrame( 3u, ZERO_VECTOR, Vector3( 4.0f, 0.0f, 0.0f ) );
    input.usingBuildFrames = true;
    input.frames = frames;
    readout.Update( input );
    CHECK( readout.View().missDistance == doctest::Approx( 4.0f ) );
}
