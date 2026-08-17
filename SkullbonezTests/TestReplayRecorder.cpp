//
// File: SkullbonezTests/TestReplayRecorder.cpp
// Purpose:
//   Lock focused ReplayRecorder ring-buffer, cursor, and configure-time memory
//   contracts.
//
// Summary:
//   ReplayRecorder is bounded circular capture storage. ArtifactIO reconstructs
//   retained samples from oldest frame to newest without exposing ring state.
//
// Glossary:
//   Retention frame: One captured replay sample kept inside the bounded window.
//   Scrub cursor: Normalized [0,1] lookup over the retained chronological range.
//   Event cursor: Monotonic replay event-stream position copied into samples.
//
// Invariants:
//   - Artifact materialization hides internal ring wrap from callers.
//   - LatestSample() returns the newest retained frame after wrap.
//   - ResetTimeline() clears samples and cursors without reallocating capacity.
//   - Configure() does not pre-reserve body payloads for every future sample.
//   - ReplayTimeline applies one retention policy to all recorder owners and
//     sequences events only while recording is enabled.
//
// Related:
//   - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
//   - SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Replay/ReplayRecorder.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayArtifactSource.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayCoordination.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayRetainedMemory.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayTimeline.h"
#include "../SkullbonezSource/Runtime/App/ReplayReserveInventory.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionRetainedMemory.h"

#include <memory>
#include <vector>

using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::PHYSICS_SOLVER_SNAPSHOT_RESERVE_HARD_BYTES;
using SkullbonezCore::Runtime::FindReplayGrowthOwnerPolicy;
using SkullbonezCore::Runtime::REPLAY_GROWTH_OWNER_POLICIES;
using SkullbonezCore::Runtime::REPLAY_PREDICTION_RESERVE_HARD_BYTES;
using SkullbonezCore::Runtime::REPLAY_RECORDER_SAMPLE_RESERVE_HARD_BYTES;
using SkullbonezCore::Runtime::REPLAY_RETAINED_OWNERSHIP_RULES;
using SkullbonezCore::Runtime::ReplayArtifactSource;
using SkullbonezCore::Runtime::ReplayBodyShapeKind;
using SkullbonezCore::Runtime::ReplayFrameIndex;
using SkullbonezCore::Runtime::ReplayGrowthExhaustionRule;
using SkullbonezCore::Runtime::ReplayMemoryPolicy;
using SkullbonezCore::Runtime::ReplayMemoryPolicyRequest;
using SkullbonezCore::Runtime::ReplayMemoryPreset;
using SkullbonezCore::Runtime::ReplayPresentationSample;
using SkullbonezCore::Runtime::ReplayRecorder;
using SkullbonezCore::Runtime::ReplayRecorderConfig;
using SkullbonezCore::Runtime::ReplayRecorderStats;
using SkullbonezCore::Runtime::ReplayRetainedDataOwner;
using SkullbonezCore::Runtime::ReplaySceneTimelineResetInput;
using SkullbonezCore::Runtime::ReplaySolverBodySample;
using SkullbonezCore::Runtime::ReplaySolverFrameSample;
using SkullbonezCore::Runtime::ReplaySolverRecorder;
using SkullbonezCore::Runtime::ReplayTimelineOperations::ReplayMemoryPresetPolicy;
using SkullbonezCore::Runtime::ReplayTimelineOperations::ResolveReplayMemoryPolicy;
using SkullbonezCore::Runtime::ReplayTimelineOperations::SceneTimelineGeneratedConfigFlags;
using SkullbonezCore::Runtime::ReplayTimelineOperations::SceneTimelineRecordsGeneratedConfig;
using SkullbonezCore::Runtime::ReplayTimelineOperations::SceneTimelineResetClearsBranch;
using namespace SkullbonezCore::Runtime;

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
    body.modelRow = SkullbonezCore::Physics::MakeModelRowHint( static_cast<int>( frameIndex % 3u ) );
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

ReplaySolverFrameSample MakeStableSolverSample( ReplayFrameIndex frameIndex, int bodyCount )
{
    ReplaySolverFrameSample sample;
    sample.frameIndex = frameIndex;
    sample.presentationHash = 0xBEEF0000ull + frameIndex;
    sample.physicsDt = 1.0f / static_cast<float>( kReplayTicksPerSecond );
    sample.bodies.reserve( static_cast<std::size_t>( bodyCount ) );
    for ( int bodyIndex = 0; bodyIndex < bodyCount; ++bodyIndex )
    {
        ReplaySolverBodySample body;
        body.id.value = 1000u + static_cast<uint32_t>( bodyIndex );
        body.modelRow = SkullbonezCore::Physics::MakeModelRowHint( bodyIndex );
        body.shapeKind = ReplayBodyShapeKind::Box;
        body.position = Vector3( static_cast<float>( bodyIndex ), 2.0f, 3.0f );
        body.mass = 10.0f;
        body.inverseMass = 0.1f;
        sample.bodies.push_back( body );
    }
    return sample;
}
} // namespace


TEST_CASE( "Replay ArtifactIO: materialization hides presentation ring wrap" )
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
    ReplayArtifactSource::MaterializePresentation( recorder, samples );
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


TEST_CASE( "Replay prediction world reset preserves reserved Gameplay snapshot storage" )
{
    ReplaySolverWorldSnapshot world;
    world.tornadoSystemConfig.vortices.reserve( 64u );
    world.tornadoCaptureSeconds.reserve( 1024u );
    world.tornadoEjectCooldownSeconds.reserve( 1024u );
    const auto* vortexStorage = world.tornadoSystemConfig.vortices.data();
    const auto* captureStorage = world.tornadoCaptureSeconds.data();
    const auto* cooldownStorage = world.tornadoEjectCooldownSeconds.data();
    const std::size_t vortexCapacity = world.tornadoSystemConfig.vortices.capacity();
    const std::size_t captureCapacity = world.tornadoCaptureSeconds.capacity();
    const std::size_t cooldownCapacity = world.tornadoEjectCooldownSeconds.capacity();

    world.ClearPreservingCapacity();
    CHECK( world.tornadoSystemConfig.vortices.data() == vortexStorage );
    CHECK( world.tornadoCaptureSeconds.data() == captureStorage );
    CHECK( world.tornadoEjectCooldownSeconds.data() == cooldownStorage );
    CHECK( world.tornadoSystemConfig.vortices.capacity() == vortexCapacity );
    CHECK( world.tornadoCaptureSeconds.capacity() == captureCapacity );
    CHECK( world.tornadoEjectCooldownSeconds.capacity() == cooldownCapacity );

    // Invariant: repeated cancellation invalidation remains allocation-free;
    // the first reset must not merely leave a fresh snapshot with zero reserve.
    world.ClearPreservingCapacity();
    CHECK( world.tornadoSystemConfig.vortices.data() == vortexStorage );
    CHECK( world.tornadoCaptureSeconds.data() == captureStorage );
    CHECK( world.tornadoEjectCooldownSeconds.data() == cooldownStorage );
}


TEST_CASE( "ReplayRecorder: presentation resolution reuses a bounded dense-buffer pool" )
{
    constexpr int bodyCount = 64;
    ReplayRecorderConfig config = SmallRecorderConfig();
    config.runtimeBodyCapacity = bodyCount;

    ReplayRecorder recorder;
    REQUIRE( recorder.Configure( config ) );
    const ReplayFrameIndex retainedFrames = static_cast<ReplayFrameIndex>( recorder.GetStats().sampleCapacity );
    for ( ReplayFrameIndex frame = 0; frame < retainedFrames; ++frame )
    {
        recorder.CaptureFrameFromSolverSample( MakeStableSolverSample( frame, bodyCount ) );
    }

    const ReplayPresentationSample* oldest = recorder.SampleAtNormalized( 0.0f );
    REQUIRE( oldest != nullptr );
    REQUIRE( oldest->bodies.size() == static_cast<std::size_t>( bodyCount ) );
    CHECK( oldest->frameIndex == 0u );

    const ReplayPresentationSample* latest = recorder.LatestSample();
    REQUIRE( latest != nullptr );
    CHECK( latest != oldest );
    CHECK( latest->frameIndex == retainedFrames - 1u );
    // Lifetime: LatestSample must not overwrite a historical pointer retained
    // by the same UI turn; this is why latest has a dedicated reusable buffer.
    CHECK( oldest->frameIndex == 0u );

    // Regression budget: one dense body cache per retained tick exceeded this
    // bound for this fixture. Compact frames plus the fixed working-buffer pool
    // stay comfortably below it.
    constexpr uint64_t maxPresentationBytes = 1ull * 1024ull * 1024ull;
    CHECK( recorder.CollectMemoryBytes() < maxPresentationBytes );
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


TEST_CASE( "ReplayRuntime: retained ownership and growth policies are complete and evidence bounded" )
{
    REQUIRE( REPLAY_RETAINED_OWNERSHIP_RULES.size() == 4u );
    CHECK( REPLAY_RETAINED_OWNERSHIP_RULES[0].owner == ReplayRetainedDataOwner::PresentationRecorder );
    CHECK( REPLAY_RETAINED_OWNERSHIP_RULES[0].retainedAtRuntime );
    CHECK( REPLAY_RETAINED_OWNERSHIP_RULES[0].durableArtifact );
    CHECK( REPLAY_RETAINED_OWNERSHIP_RULES[1].owner == ReplayRetainedDataOwner::SolverRecorder );
    CHECK( REPLAY_RETAINED_OWNERSHIP_RULES[1].retainedAtRuntime );
    CHECK( REPLAY_RETAINED_OWNERSHIP_RULES[1].durableArtifact );
    CHECK( REPLAY_RETAINED_OWNERSHIP_RULES[2].owner == ReplayRetainedDataOwner::PredictionPrefix );
    CHECK( REPLAY_RETAINED_OWNERSHIP_RULES[2].retainedAtRuntime );
    CHECK_FALSE( REPLAY_RETAINED_OWNERSHIP_RULES[2].durableArtifact );
    CHECK( REPLAY_RETAINED_OWNERSHIP_RULES[3].owner == ReplayRetainedDataOwner::V2Artifact );
    CHECK_FALSE( REPLAY_RETAINED_OWNERSHIP_RULES[3].retainedAtRuntime );
    CHECK( REPLAY_RETAINED_OWNERSHIP_RULES[3].durableArtifact );

    REQUIRE( REPLAY_GROWTH_OWNER_POLICIES.size() == 3u );
    for ( const auto& policy : REPLAY_GROWTH_OWNER_POLICIES )
    {
        CHECK( policy.phase == SkullbonezCore::Core::Allocation::RuntimeReservePhase::Replay );
        CHECK( policy.hardBytes > 0 );
        CHECK( policy.measuredHighWaterBytes < static_cast<uint64_t>( policy.hardBytes ) );
        // Invariant: strict-run evidence may move as retained layouts improve,
        // but every approved cap keeps at least 1.5x measured headroom.
        CHECK( static_cast<uint64_t>( policy.hardBytes ) * 2u >= policy.measuredHighWaterBytes * 3u );
        CHECK( FindReplayGrowthOwnerPolicy( policy.ownerName ) == &policy );
    }
    CHECK( REPLAY_GROWTH_OWNER_POLICIES[0].exhaustion == ReplayGrowthExhaustionRule::FatalRetainedState );
    CHECK( REPLAY_GROWTH_OWNER_POLICIES[1].exhaustion == ReplayGrowthExhaustionRule::FatalRetainedState );
    CHECK( REPLAY_GROWTH_OWNER_POLICIES[2].exhaustion == ReplayGrowthExhaustionRule::CancelPredictionBuild );

    CHECK( REPLAY_RECORDER_SAMPLE_RESERVE_HARD_BYTES == 32 * 1024 * 1024 );
    CHECK( PHYSICS_SOLVER_SNAPSHOT_RESERVE_HARD_BYTES == 8 * 1024 * 1024 );
}


TEST_CASE( "Replay coordination: scene timeline reset decisions preserve branch and authored-scene semantics" )
{
    ReplaySceneTimelineResetInput reset;
    reset.modelCount = 5;
    reset.solverBallCount = 3;
    reset.solverBoxCount = 2;
    reset.rngSeed = 1234u;
    reset.sceneObjectCapacity = 8;
    reset.hasUiModelCountOverride = true;
    reset.hasUiSolverCountOverride = true;

    CHECK( SceneTimelineResetClearsBranch( reset ) );
    CHECK( SceneTimelineRecordsGeneratedConfig( reset ) );
    const uint32_t generatedFlags = SceneTimelineGeneratedConfigFlags( reset );
    CHECK( ( generatedFlags & SkullbonezCore::Runtime::REPLAY_GENERATED_SCENE_EXACT_SOLVER_COUNTS ) != 0u );
    CHECK( ( generatedFlags & SkullbonezCore::Runtime::REPLAY_GENERATED_SCENE_UI_MODEL_COUNT ) != 0u );
    CHECK( ( generatedFlags & SkullbonezCore::Runtime::REPLAY_GENERATED_SCENE_UI_SOLVER_COUNTS ) != 0u );

    reset.preserveBranchMetadata = true;
    CHECK_FALSE( SceneTimelineResetClearsBranch( reset ) );

    reset.isSceneMode = true;
    reset.solverBallCount = 0;
    reset.solverBoxCount = 0;
    CHECK_FALSE( SceneTimelineRecordsGeneratedConfig( reset ) );
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


TEST_CASE( "Coverage floor contract: replay timeline applies retention and sequences owner events atomically" )
{
    auto timeline = std::make_unique<ReplayTimeline>();
    CHECK_FALSE( timeline->RecordingConfigured() );
    const ReplayRecordingConfigResult configured = timeline->ConfigureRecording( true, 12, nullptr, 2 );
    CHECK( timeline->RecordingConfigured() );
    CHECK( timeline->RecordingEnabled() );
    CHECK( configured.presentationConfig.retentionSeconds == 12 );
    CHECK( configured.solverConfig.retentionSeconds == 12 );
    CHECK( configured.eventStats.enabled );

    timeline->Reset( "coverage-timeline" );
    ReplayEventCommand command;
    command.kind = ReplayEventKind::OwnerAction;
    command.useNextFrame = true;
    command.flags = 7u;
    command.value0 = 11;
    ReplayBranchInfo branch;
    branch.branchId = 3u;
    timeline->SubmitEvent( command, branch );
    const ReplayEventRecorderStats eventStats = timeline->Events().GetStats();
    CHECK( eventStats.eventCount == 1u );
    CHECK( eventStats.nextSequence == 1u );

    // Record/stop is a gate over already-reserved rings. It neither clears the
    // timeline nor lets stopped owner events advance the event cursor.
    CHECK( timeline->SetRecordingEnabled( false ) );
    CHECK_FALSE( timeline->RecordingEnabled() );
    timeline->SubmitEvent( command, branch );
    CHECK( timeline->Events().GetStats().eventCount == 1u );
    CHECK( timeline->SetRecordingEnabled( true ) );
    CHECK( timeline->RecordingEnabled() );
    timeline->SubmitEvent( command, branch );
    CHECK( timeline->Events().GetStats().eventCount == 2u );

    ReplayMemoryPolicyRequest compact;
    compact.presetIndex = static_cast<int>( ReplayMemoryPreset::Compact );
    const ReplayMemoryPolicyApplyResult changed = timeline->ApplyMemoryPolicyRequest( compact );
    CHECK( changed.changed );
    CHECK( changed.recordersReset );
    CHECK( timeline->MemoryPolicy().solverRetentionSeconds <= timeline->MemoryPolicy().presentationRetentionSeconds );
    const ReplayMemoryPolicyApplyResult unchanged = timeline->ApplyMemoryPolicyRequest( {} );
    CHECK_FALSE( unchanged.changed );
    CHECK_FALSE( unchanged.recordersReset );

    const ReplayTimelineMemoryStats memory = timeline->CollectMemoryStats();
    CHECK( memory.presentationSamples == 0u );
    CHECK( memory.solverSamples == 0u );
    CHECK( memory.eventSamples == 0u );
    timeline->ClearLoadedPresentation();
    CHECK_FALSE( timeline->LoadedPresentation().enabled );
}
