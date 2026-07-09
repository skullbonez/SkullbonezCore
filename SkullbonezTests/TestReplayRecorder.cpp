//
// File: SkullbonezTests/TestReplayRecorder.cpp
// Purpose:
//   Lock focused ReplayRecorder ring-buffer, cursor, and configure-time memory
//   contracts.
//
// Mental model:
//   ReplayRecorder is a bounded chronological view over circular storage. The
//   internal head moves when retention fills, but public reads still return
//   samples from oldest retained frame to newest retained frame.
//
// Glossary:
//   Retention frame: One captured replay sample kept inside the bounded window.
//   Scrub cursor: Normalized [0,1] lookup over the retained chronological range.
//   Event cursor: Monotonic replay event-stream position copied into samples.
//
// Invariants:
//   - Chronological copy hides internal ring wrap from callers.
//   - LatestSample() returns the newest retained frame after wrap.
//   - ResetTimeline() clears samples and cursors without reallocating capacity.
//   - Configure() does not pre-reserve body payloads for every future sample.
//
// Related:
//   - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
//   - SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp
//   - fable_plans/01-unit-test-pyramid-progress.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Replay/ReplayRecorder.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayRuntime.h"

#include <vector>

using SkullbonezCore::Basics::ReplayBodyShapeKind;
using SkullbonezCore::Basics::ReplayFrameIndex;
using SkullbonezCore::Basics::ReplayPresentationSample;
using SkullbonezCore::Basics::ReplayRecorder;
using SkullbonezCore::Basics::ReplayRecorderConfig;
using SkullbonezCore::Basics::ReplayRecorderStats;
using SkullbonezCore::Basics::ReplayMemoryPolicyRequest;
using SkullbonezCore::Basics::ReplayMemoryPreset;
using SkullbonezCore::Basics::ReplaySolverBodySample;
using SkullbonezCore::Basics::ReplaySolverFrameSample;
using SkullbonezCore::Basics::ReplaySolverRecorder;
using SkullbonezCore::Basics::ResolveReplayMemoryPolicy;
using SkullbonezCore::Basics::ReplayMemoryPolicy;
using SkullbonezCore::Basics::ReplayMemoryPresetPolicy;
using SkullbonezCore::Math::Vector::Vector3;

namespace
{
constexpr int kReplayTicksPerSecond = 120;

ReplayRecorderConfig SmallRecorderConfig()
{
    ReplayRecorderConfig config;
    config.enabled = true;
    config.retentionSeconds = 1;
    config.checkpointIntervalFrames = 30;
    config.runtimeBodyCapacity = 2;
    return config;
}

ReplaySolverFrameSample MakeSolverSample( ReplayFrameIndex frameIndex )
{
    ReplaySolverFrameSample sample;
    sample.frameIndex = frameIndex;
    sample.branch.branchId = 7u;
    sample.branch.parentBranchId = 3u;
    sample.eventCursor = 1000u + static_cast<uint32_t>( frameIndex );
    sample.sceneFrame = 200 + static_cast<int>( frameIndex );
    sample.simulationSeconds = static_cast<double>( frameIndex ) / static_cast<double>( kReplayTicksPerSecond );
    sample.physicsDt = 1.0f / static_cast<float>( kReplayTicksPerSecond );
    sample.world.gravity = -9.8f;
    sample.presentationHash = 0xCAFE0000ull + frameIndex;
    sample.contactCount = static_cast<uint16_t>( frameIndex % 5u );
    sample.pipelineRecordCount = static_cast<uint16_t>( frameIndex % 7u );

    ReplaySolverBodySample body;
    body.id.value = 500u + static_cast<uint32_t>( frameIndex );
    body.modelIndex = static_cast<int>( frameIndex % 3u );
    body.shapeKind = ReplayBodyShapeKind::Box;
    body.position = Vector3( static_cast<float>( frameIndex ), 2.0f, 3.0f );
    body.linearVelocity = Vector3( 1.0f, 0.0f, 0.0f );
    body.mass = 10.0f;
    body.inverseMass = 0.1f;
    body.contactCount = static_cast<uint16_t>( frameIndex % 5u );
    body.normalImpulseSum = static_cast<float>( frameIndex ) * 0.25f;
    sample.bodies.push_back( body );
    return sample;
}
} // namespace


TEST_CASE( "ReplayRecorder: chronological copy hides presentation ring wrap" )
{
    ReplayRecorder recorder;
    REQUIRE( recorder.Configure( SmallRecorderConfig() ) );
    recorder.ResetTimeline( "unit-replay" );
    const std::size_t capacity = recorder.GetStats().sampleCapacity;
    REQUIRE( capacity == static_cast<std::size_t>( kReplayTicksPerSecond ) );

    // Why: solver-sample mirroring hits the real presentation ring buffer while
    // avoiding the full live model/world capture path that belongs to integration tests.
    const ReplayFrameIndex capturedFrames = static_cast<ReplayFrameIndex>( capacity + 2u );
    for ( ReplayFrameIndex frame = 0; frame < capturedFrames; ++frame )
    {
        recorder.CaptureFrameFromSolverSample( MakeSolverSample( frame ) );
    }

    const ReplayRecorderStats stats = recorder.GetStats();
    CHECK( stats.totalFramesCaptured == capturedFrames );
    CHECK( stats.totalFramesEvicted == 2u );
    CHECK( stats.nextFrameIndex == capturedFrames );
    CHECK( stats.sampleCapacity == capacity );
    CHECK( stats.sampleCount == capacity );
    CHECK( stats.latestStateHash == 0xCAFE0000ull + capturedFrames - 1u );

    std::vector<ReplayPresentationSample> samples;
    recorder.CopySamplesChronological( samples );
    REQUIRE( samples.size() == capacity );
    CHECK( samples.front().frameIndex == 2u );
    CHECK( samples.front().eventCursor == 1002u );
    CHECK( samples.front().bodies.size() == 1u );
    CHECK( samples.front().bodies[0].id.value == 502u );
    CHECK( samples.back().frameIndex == capturedFrames - 1u );
    CHECK( samples.back().eventCursor == 1000u + static_cast<uint32_t>( capturedFrames - 1u ) );
}


TEST_CASE( "ReplayRecorder: Configure does not pre-reserve future sample payloads" )
{
    ReplayRecorderConfig config;
    config.enabled = true;
    config.retentionSeconds = 1;
    config.checkpointIntervalFrames = 30;
    config.runtimeBodyCapacity = 4000;

    ReplayRecorder presentation;
    ReplaySolverRecorder solver;
    REQUIRE( presentation.Configure( config ) );
    REQUIRE( solver.Configure( config ) );

    CHECK( presentation.GetStats().sampleCapacity == static_cast<std::size_t>( kReplayTicksPerSecond ) );
    CHECK( solver.GetStats().sampleCapacity == static_cast<std::size_t>( kReplayTicksPerSecond ) );

    constexpr uint64_t maxConfiguredBytes = 64ull * 1024ull * 1024ull;
    CHECK( presentation.CollectMemoryBytes() < maxConfiguredBytes );
    CHECK( solver.CollectMemoryBytes() < maxConfiguredBytes );
}


TEST_CASE( "ReplayRuntime: replay memory policy trims solver history before presentation" )
{
    ReplayMemoryPolicy defaultPolicy;
    defaultPolicy.requestedRetentionSeconds = 60;
    defaultPolicy = ResolveReplayMemoryPolicy( defaultPolicy );

    CHECK( defaultPolicy.preset == ReplayMemoryPreset::LosslessLook );
    CHECK( defaultPolicy.requestedRetentionSeconds == 60 );
    CHECK( defaultPolicy.presentationRetentionSeconds == 60 );
    CHECK( defaultPolicy.solverRetentionSeconds == 60 );
    CHECK_FALSE( defaultPolicy.budgetClamped );

    ReplayMemoryPolicyRequest request;
    request.presetIndex = static_cast<int>( ReplayMemoryPreset::Compact );
    request.retentionSeconds = 60;
    request.budgetMiB = 48;

    ReplayMemoryPolicy compactPolicy = ReplayMemoryPresetPolicy( ReplayMemoryPreset::Compact );
    compactPolicy.requestedRetentionSeconds = request.retentionSeconds;
    compactPolicy.requestedBudgetMiB = request.budgetMiB;
    compactPolicy = ResolveReplayMemoryPolicy( compactPolicy );

    CHECK( compactPolicy.preset == ReplayMemoryPreset::Compact );
    CHECK( compactPolicy.requestedRetentionSeconds == 60 );
    CHECK( compactPolicy.requestedBudgetMiB == 48 );
    CHECK( compactPolicy.presentationRetentionSeconds == 30 );
    CHECK( compactPolicy.solverRetentionSeconds == 5 );
    CHECK( compactPolicy.budgetClamped );
    CHECK( compactPolicy.solverWindowReduced );

    ReplayRecorderConfig presentationConfig = SmallRecorderConfig();
    presentationConfig.retentionSeconds = compactPolicy.presentationRetentionSeconds;
    ReplayRecorderConfig solverConfig = SmallRecorderConfig();
    solverConfig.retentionSeconds = compactPolicy.solverRetentionSeconds;

    ReplayRecorder presentation;
    ReplaySolverRecorder solver;
    REQUIRE( presentation.Configure( presentationConfig ) );
    REQUIRE( solver.Configure( solverConfig ) );
    CHECK( presentation.GetStats().sampleCapacity == static_cast<std::size_t>( 30 * kReplayTicksPerSecond ) );
    CHECK( solver.GetStats().sampleCapacity == static_cast<std::size_t>( 5 * kReplayTicksPerSecond ) );
}


TEST_CASE( "ReplayRecorder: normalized scrub and latest sample track retained frames" )
{
    ReplayRecorder recorder;
    REQUIRE( recorder.Configure( SmallRecorderConfig() ) );
    const std::size_t capacity = recorder.GetStats().sampleCapacity;
    for ( ReplayFrameIndex frame = 0; frame < static_cast<ReplayFrameIndex>( capacity + 2u ); ++frame )
    {
        recorder.CaptureFrameFromSolverSample( MakeSolverSample( frame ) );
    }

    const ReplayPresentationSample* first = recorder.SampleAtNormalized( -1.0f );
    const ReplayPresentationSample* middle = recorder.SampleAtNormalized( 0.5f );
    const ReplayPresentationSample* last = recorder.SampleAtNormalized( 2.0f );
    const ReplayPresentationSample* latest = recorder.LatestSample();

    REQUIRE( first != nullptr );
    REQUIRE( middle != nullptr );
    REQUIRE( last != nullptr );
    REQUIRE( latest != nullptr );
    CHECK( first->frameIndex == 2u );
    CHECK( middle->frameIndex == 62u );
    CHECK( last->frameIndex == 121u );
    CHECK( latest->frameIndex == 121u );
    CHECK( latest->stateHash == 0xCAFE0000ull + 121u );
}


TEST_CASE( "ReplayRecorder: ResetTimeline clears samples and cursors but keeps capacity" )
{
    ReplayRecorder recorder;
    REQUIRE( recorder.Configure( SmallRecorderConfig() ) );
    const std::size_t capacity = recorder.GetStats().sampleCapacity;
    recorder.CaptureFrameFromSolverSample( MakeSolverSample( 11u ) );
    REQUIRE( recorder.GetStats().sampleCount == 1u );

    recorder.ResetTimeline( "after-reset" );

    const ReplayRecorderStats stats = recorder.GetStats();
    CHECK( stats.sampleCapacity == capacity );
    CHECK( stats.sampleCount == 0u );
    CHECK( stats.nextFrameIndex == 0u );
    CHECK( stats.latestStateHash == 0u );
    CHECK( recorder.LatestSample() == nullptr );
    CHECK( recorder.SampleAtNormalized( 0.5f ) == nullptr );
}
