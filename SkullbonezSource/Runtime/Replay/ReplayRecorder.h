/*
Purpose:
  Defines bounded replay capture records for simulation debugging.

Invariants:
  - Capture order is chronological even though storage wraps internally.
  - Hash fields are compatibility surface for deterministic validation.
  - Snapshot v4 motion state participates in sparse deltas and hashes; v1-v3
    hash byte order remains unchanged.
  - Owner-action wire values never serialize domain enum ordinals.
  - Retained vector growth shares `replay_recorder_samples`; no record or vector
    receives a private copy of the process-wide hard cap.
*/
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "../../Core/MainMemoryStats.h"
#include "../../Gameplay/TornadoGameplay.h"
#include "../../Maths/Quaternion.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/PhysicsHandles.h"
#include "../../Physics/PhysicsSolverSnapshot.h"
#include "ReplayEventCommand.h"
#include "ReplayToolPackets.h"
#include "ReplayCaptureLimits.h"
#include "ReplayCapturePackets.h"

namespace SkullbonezCore
{
namespace Environment
{
class WorldEnvironment;
} // namespace Environment

namespace Physics
{
class ColliderStore;
class PhysicsEngine;
class PhysicsBodyStore;
} // namespace Physics

namespace Runtime
{
class ReplayArtifactSource;

struct ReplayBranchInfo
{
    uint32_t branchId = 1;
    uint32_t parentBranchId = 0;
    ReplayFrameIndex startFrame = 0;
    ReplayFrameIndex sourceFrame = 0;
    uint64_t sourceSolverHash = 0;
};

enum class ReplayBodyShapeKind : uint8_t
{
    Unknown,
    Sphere,
    Box,
    ConvexHull
};

struct ReplayCameraSample
{
    Math::Vector::Vector3 eye = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 view = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 up = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
};

struct ReplayWorldPresentationSample
{
    float gravity = 0.0f;
    float fluidHeight = 0.0f;
    float fluidDensity = 0.0f;
    bool waterHidden = false;
    bool terrainHidden = false;
    bool fixedStep = false; // Compatibility name: scene/capture lockstep request, not resolved pacing.
    bool scenePhysicsEnabled = true;
    bool sceneTextEnabled = true;
};

struct ReplayBodyPresentationSample
{
    Physics::PhysicsSceneObjectId id;
    Physics::ModelRowHint modelRow; // Optional resolver cache; Physics::PhysicsSceneObjectId remains durable identity.
    char name[64] = {};
    ReplayBodyShapeKind shapeKind = ReplayBodyShapeKind::Unknown;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    float orientation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float mass = 0.0f;
    bool fixed = false;
    bool sleeping = false;
    bool sleepSupported = false;
    bool sleepInhibited = false;
    bool collisionContact = false;
    int sleepIslandVisualId = 0;
    uint16_t contactCount = 0;
    float maxPenetration = 0.0f;
    float normalImpulseSum = 0.0f;
};

struct ReplayVisualBodyMetadata
{
    Physics::PhysicsSceneObjectId id;
    Physics::ModelRowHint modelRow;
    char name[64] = {};
    ReplayBodyShapeKind shapeKind = ReplayBodyShapeKind::Unknown;
    float mass = 0.0f;
    bool fixed = false;
};

struct ReplayVisualBodyState
{
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    float orientation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    bool sleeping = false;
    bool sleepSupported = false;
    bool sleepInhibited = false;
    bool collisionContact = false;
    int sleepIslandVisualId = 0;
    uint16_t contactCount = 0;
    float maxPenetration = 0.0f;
    float normalImpulseSum = 0.0f;
};

struct ReplayVisualBodyDelta
{
    uint32_t metadataIndex = 0;
    ReplayVisualBodyState state;
};

struct ReplayVisualDeltaFrame
{
    bool keyframe = false;
    std::vector<uint32_t> bodyMetadataIndices;
    std::vector<ReplayVisualBodyDelta> changedBodies;
};

// Concept: this is the sole durable per-frame visual extension seam. A new
// replayed visual feature extends this value (or a value it owns), its delta
// capture, hash, and v2 serialization together; it must not create a parallel
// retained timeline.
struct ReplayPresentationSample
{
    ReplayFrameIndex frameIndex = 0;
    ReplayBranchInfo branch;
    uint32_t eventCursor = 0;
    int sceneFrame = 0;
    double simulationSeconds = 0.0;
    float physicsDt = 0.0f;
    ReplayCameraSample camera;
    ReplayWorldPresentationSample world;
    std::vector<ReplayBodyPresentationSample> bodies;
    uint64_t stateHash = 0;
    uint16_t contactCount = 0;
    uint16_t pipelineRecordCount = 0;
    bool checkpointBoundary = false;
};

// Recomputes the durable presentation digest from values owned by a fully
// resolved sample. Artifact readers use it to reject any v3 visual-state row
// that cannot reproduce the writer's exact delta/hash contract.
namespace ReplayRecorderOperations
{
uint64_t ComputePresentationStateHash( const ReplayPresentationSample& sample ) noexcept;
} // namespace ReplayRecorderOperations

struct ReplaySolverBodySample
{
    Physics::PhysicsSceneObjectId id;
    Physics::ModelRowHint modelRow; // Optional resolver cache; Physics::PhysicsSceneObjectId remains durable identity.
    char name[64] = {};
    ReplayBodyShapeKind shapeKind = ReplayBodyShapeKind::Unknown;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    float orientation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float mass = 0.0f;
    float inverseMass = 0.0f;
    Math::Vector::Vector3 rotationalInertia = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 inverseRotationalInertia = Math::Vector::ZERO_VECTOR;
    bool fixed = false;
    bool sleeping = false;
    bool sleepSupported = false;
    bool sleepInhibited = false;
    bool collisionContact = false;
    int sleepIslandVisualId = 0;
    uint16_t contactCount = 0;
    float maxPenetration = 0.0f;
    float normalImpulseSum = 0.0f;
};

// Concept: solver compaction keeps identity, shape, mass, and inertia in a
// dictionary because these fields rarely change but every restored body needs
// them byte-for-byte when reconstructing the public sample.
struct ReplaySolverBodyMetadata
{
    Physics::PhysicsSceneObjectId id;
    Physics::ModelRowHint modelRow;
    char name[64] = {};
    ReplayBodyShapeKind shapeKind = ReplayBodyShapeKind::Unknown;
    float mass = 0.0f;
    float inverseMass = 0.0f;
    Math::Vector::Vector3 rotationalInertia = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 inverseRotationalInertia = Math::Vector::ZERO_VECTOR;
};

// Concept: solver body state is the per-frame portion of a body sample. Fixed,
// sleep, and contact flags stay here because restore and solver hashes observe
// them as frame-local state, not just display metadata.
struct ReplaySolverBodyState
{
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    float orientation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    bool fixed = false;
    bool sleeping = false;
    bool sleepSupported = false;
    bool sleepInhibited = false;
    bool collisionContact = false;
    int sleepIslandVisualId = 0;
    uint16_t contactCount = 0;
    float maxPenetration = 0.0f;
    float normalImpulseSum = 0.0f;
};

struct ReplaySolverBodyDelta
{
    uint32_t metadataIndex = 0;
    ReplaySolverBodyState state;
};

struct ReplaySolverWorldScalarState
{
    uint32_t version = Physics::PHYSICS_SOLVER_SNAPSHOT_VERSION;
    int modelCount = 0;
    int nextSleepIslandVisualId = 1;
    bool sleepEnabled = true;
    bool collisionVisualFrameActive = false;
    Gameplay::TornadoFieldConfig tornadoConfig;
    Gameplay::TornadoSystemConfig tornadoSystemConfig;
    double tornadoSystemElapsedSeconds = 0.0;
    Physics::PhysicsSolverStatsSample solverStats;
};

// Owner: Runtime/ReplaySolverRecorder. Reason: replay checkpoints must compose
// Physics solver caches with Gameplay tornado state while preserving artifact
// field order; neither lower module may include the other. Deletion condition:
// none--this value-only composition is the end-state replay boundary. Review
// evidence: validate_replay_visual_fidelity proves artifact bytes, restore,
// prediction, and one-presentation behavior without inheritance or callbacks.
struct ReplaySolverWorldSnapshot
{
    Physics::PhysicsSolverSnapshot physics;
    Gameplay::TornadoFieldConfig tornadoConfig;
    Gameplay::TornadoSystemConfig tornadoSystemConfig;
    double tornadoSystemElapsedSeconds = 0.0;
    std::vector<float> tornadoCaptureSeconds;
    std::vector<float> tornadoEjectCooldownSeconds;

    void ClearPreservingCapacity() noexcept
    {
        // Lifetime: cancellation is a steady-runtime transition. Preserve all
        // Physics and Gameplay vector storage allocated during reserve setup.
        physics.ClearPreservingCapacity();
        tornadoConfig = {};
        tornadoSystemConfig.enabled = false;
        tornadoSystemConfig.visualizeVelocityField = false;
        tornadoSystemConfig.vortices.clear();
        tornadoSystemElapsedSeconds = 0.0;
        tornadoCaptureSeconds.clear();
        tornadoEjectCooldownSeconds.clear();
    }
};

// Invariant: a full vector payload is used for keyframes and size changes.
// Otherwise changedValues patches the previous reconstructed vector by index.
template <typename T> struct ReplaySolverIndexedValue
{
    uint32_t index = 0;
    T value = {};
};

template <typename T> struct ReplaySolverVectorDelta
{
    bool ApplyTo( std::vector<T>& target, ReplayFrameIndex frameIndex, const char* targetName ) const;

    bool full = false;
    std::vector<T> fullValues;
    std::vector<ReplaySolverIndexedValue<T>> changedValues;
};

// Snapshot vectors mirror PhysicsSolverSnapshot field names so artifact
// save/load can still reconstruct the old dense checkpoint shape.
struct ReplaySolverWorldDeltaFrame
{
    ReplaySolverWorldScalarState scalarState;
    ReplaySolverVectorDelta<float> timeRemaining;
    ReplaySolverVectorDelta<uint8_t> sleepSupportedThisFrame;
    ReplaySolverVectorDelta<uint8_t> sleepInhibitedThisFrame;
    ReplaySolverVectorDelta<uint8_t> sleepState;
    ReplaySolverVectorDelta<uint32_t> sleepCounter;
    ReplaySolverVectorDelta<Math::Vector::Vector3> sleepPoseAnchorPosition;
    ReplaySolverVectorDelta<std::array<float, 4>> sleepPoseAnchorOrientation;
    ReplaySolverVectorDelta<uint8_t> sleepPoseAnchorValid;
    ReplaySolverVectorDelta<uint8_t> underwaterSleepLocked;
    ReplaySolverVectorDelta<float> tornadoCaptureSeconds;
    ReplaySolverVectorDelta<float> tornadoEjectCooldownSeconds;
    ReplaySolverVectorDelta<uint8_t> collisionVisualContacts;
    ReplaySolverVectorDelta<int> sleepIslandVisualId;
    ReplaySolverVectorDelta<int> sleepIslandAssignedVisualId;
    ReplaySolverVectorDelta<std::pair<int, int>> sleepSupportEdges;
    ReplaySolverVectorDelta<int> sleepIslandParent;
    ReplaySolverVectorDelta<uint8_t> sleepIslandRank;
    ReplaySolverVectorDelta<uint8_t> sleepIslandHasAwake;
    ReplaySolverVectorDelta<uint8_t> sleepIslandHasSupportAnchor;
    ReplaySolverVectorDelta<uint8_t> sleepIslandEligible;
    ReplaySolverVectorDelta<uint8_t> sleepIslandCanSleep;
    ReplaySolverVectorDelta<Physics::PhysicsSolverPersistentContactSample> persistentContacts;
    ReplaySolverVectorDelta<Physics::PhysicsSolverContactCacheSample> persistentContactCache;
    ReplaySolverVectorDelta<Physics::PhysicsSolverPointJointSample> pointJoints;
    ReplaySolverVectorDelta<uint8_t> motionEligibilityState;
    ReplaySolverVectorDelta<uint16_t> persistentContactCounts;
    ReplaySolverVectorDelta<uint16_t> persistentRestingContactCounts;
    ReplaySolverVectorDelta<Physics::PhysicsDebugContact> debugContacts;
    ReplaySolverVectorDelta<Physics::PhysicsPipelineRecord> pipelineTrace;
    ReplaySolverVectorDelta<int64_t> collisionCellKeys;
};

// Concept: one compact solver frame shares its ring slot with the public sample
// header. Keyframes are self-contained; non-keyframes reuse prior body/world
// state and only name the body order plus changed payloads for this frame.
struct ReplaySolverDeltaFrame
{
    bool keyframe = false;
    std::vector<uint32_t> bodyMetadataIndices;
    std::vector<ReplaySolverBodyDelta> changedBodies;
    ReplaySolverWorldDeltaFrame world;
};

// Concept: solver samples own authoritative restore state and may project a
// presentation sample, but presentation-only feature payloads do not belong in
// this larger deterministic checkpoint value.
struct ReplaySolverFrameSample
{
    ReplayFrameIndex frameIndex = 0;
    ReplayBranchInfo branch;
    uint32_t eventCursor = 0;
    int sceneFrame = 0;
    double simulationSeconds = 0.0;
    float physicsDt = 0.0f;
    ReplayCameraSample camera;
    ReplayWorldPresentationSample world;
    ReplayLauncherVisualSample launcherVisual;
    ReplaySolverWorldSnapshot worldSnapshot;
    std::vector<ReplaySolverBodySample> bodies;
    uint64_t solverHash = 0;
    uint64_t presentationHash = 0;
    uint16_t contactCount = 0;
    uint16_t pipelineRecordCount = 0;
    bool checkpointBoundary = false;
};

// Deterministic digest of a fully materialized solver sample. Artifact loading
// uses this same function to verify payload integrity before mutating live state.
struct ReplaySolverHashBreakdown
{
    uint64_t world = 0;
    uint64_t counts = 0;
    uint64_t launcher = 0;
    uint64_t snapshot = 0;
    uint64_t bodies = 0;
};

ReplaySolverHashBreakdown ReplaySolverHashBreakdownForSample( const ReplaySolverFrameSample& sample ) noexcept;
uint64_t ReplaySolverHashForSample( const ReplaySolverFrameSample& sample ) noexcept;

struct ReplayCheckpointSummary
{
    ReplayFrameIndex frameIndex = 0;
    uint32_t eventCursor = 0;
    double simulationSeconds = 0.0;
    uint64_t stateHash = 0;
    uint32_t bodyCount = 0;
    uint16_t contactCount = 0;
    uint16_t pipelineRecordCount = 0;
};

struct ReplayEventSample
{
    ReplayFrameIndex frameIndex = 0;
    uint32_t sequence = 0;
    ReplayBranchInfo branch;
    ReplayEventKind kind = ReplayEventKind::Unknown;
    uint16_t payloadVersion = 1;
    uint32_t flags = 0;
    int32_t value0 = 0;
    int32_t value1 = 0;
    int32_t value2 = 0;
    int32_t value3 = 0;
    uint64_t data0 = 0;
    char text[128] = {};
};

struct ReplayEventInput
{
    ReplayFrameIndex frameIndex = 0;
    ReplayBranchInfo branch;
    ReplayEventKind kind = ReplayEventKind::Unknown;
    uint32_t flags = 0;
    int32_t value0 = 0;
    int32_t value1 = 0;
    int32_t value2 = 0;
    int32_t value3 = 0;
    uint64_t data0 = 0;
    const char* text = nullptr;
};

struct ReplayRecorderConfig
{
    bool enabled = false;
    int retentionSeconds = REPLAY_PAST_BUFFER_SECONDS;
    int checkpointIntervalFrames = 30;
    int runtimeBodyCapacity = 0; // Scene/run body cap for scratch reserves and retained-sample growth checks.
    std::string hashLogPath;
};

// Presentation recorder: stores visual scrub samples in a bounded ring buffer.
// Callers always read samples chronologically even though storage wraps.
class ReplayRecorder
{
  public:
    bool Configure( const ReplayRecorderConfig& config );
    void ResetTimeline( const char* sceneLabel );
    void CaptureFrame( const ReplayBranchInfo& branch, uint32_t eventCursor, int sceneFrame, float physicsDt,
                       const ReplayWorldPresentationSample& world, const ReplayCameraSample& camera,
                       Physics::PhysicsEngine& physics, std::span<const char* const> entityDisplayNames );

    // Records the presentation track from an already captured solver sample.
    // Use this when both tracks are enabled so frame capture does one model walk.
    void CaptureFrameFromSolverSample( const ReplaySolverFrameSample& solverSample );
    bool IsEnabled() const;
    ReplayRecorderStats GetStats() const;

    // Adds this track's fixed-capacity storage to the shared replay memory categories.
    void CollectMemoryCategoryBytes( SkullbonezCore::Core::MainMemoryReplayCategoryBytes& categories ) const;
    uint64_t CollectMemoryBytes() const;
    const ReplayPresentationSample* LatestSample() const;
    const ReplayPresentationSample* SampleAtNormalized( float normalized ) const;

  private:
    // Why: cold ArtifactIO must reconstruct compact deltas without making ring
    // internals public. Its commands accept this owner as const and publish only
    // copied chronological values.
    friend class ReplayArtifactSource;
    std::size_t AcquireSampleSlotIndex();
    std::size_t FindOrAddVisualBodyMetadata( const ReplayBodyPresentationSample& body, ReplayFrameIndex frameIndex );
    void StoreVisualFramePayload( std::size_t slotIndex, const ReplayPresentationSample& sample,
                                  const std::vector<ReplayBodyPresentationSample>& bodies, bool forceKeyframe,
                                  bool updateCarry );
    bool ResolveSampleAtOffset( std::size_t offset, ReplayPresentationSample& outSample ) const;
    void PromoteVisualFrameToKeyframe( std::size_t offset );
    void StoreCheckpointSummary( const ReplayPresentationSample& sample, std::size_t bodyCount );

    ReplayRecorderConfig m_config;
    std::vector<ReplayPresentationSample> m_samples;
    std::vector<ReplayVisualDeltaFrame> m_visualFrames;
    std::vector<ReplayVisualBodyMetadata> m_visualBodyMetadata;

    // Model rows are hints, so every fast-path hit is still validated against
    // the stable scene id and immutable metadata before the index is reused.
    std::vector<uint32_t> m_visualMetadataIndexByModelRow;
    std::vector<ReplayVisualBodyState> m_visualCarryStates;
    std::vector<uint8_t> m_visualCarryActive;
    std::vector<uint8_t> m_visualCarrySeenScratch;
    std::vector<ReplayBodyPresentationSample> m_captureBodyScratch;
    std::vector<ReplayCheckpointSummary> m_checkpoints;
    std::vector<uint16_t> m_contactCountScratch;
    std::vector<float> m_maxPenetrationScratch;
    std::vector<float> m_normalImpulseSumScratch;

    // Lifetime: one UI turn may compare several scrub positions plus latest.
    // A small rotating pool preserves those simultaneous borrows without
    // caching one full body vector per retained frame.
    static constexpr std::size_t PRESENTATION_RESOLVE_CACHE_COUNT = 4u;
    mutable std::array<ReplayPresentationSample, PRESENTATION_RESOLVE_CACHE_COUNT> m_resolvedPresentationSamples;
    mutable std::size_t m_nextResolvedPresentationSample = 0u;
    mutable ReplayPresentationSample m_latestResolvedPresentationSample;
    mutable ReplayPresentationSample m_promotedPresentationSample;
    mutable std::vector<ReplayVisualBodyState> m_resolveStateScratch;
    mutable std::vector<uint8_t> m_resolveActiveScratch;
    std::size_t m_sampleHead = 0;
    std::size_t m_sampleCount = 0;
    std::size_t m_checkpointHead = 0;
    std::size_t m_checkpointCount = 0;
    ReplayFrameIndex m_nextFrameIndex = 0;
    uint64_t m_totalFramesCaptured = 0;
    uint64_t m_totalFramesEvicted = 0;
    uint64_t m_latestStateHash = 0;
};

// Solver recorder: stores physics-facing samples with enough cache/stat data for
// replay diagnostics and checkpoint restore verification work.
class ReplaySolverRecorder
{
  public:
    bool Configure( const ReplayRecorderConfig& config );
    void ResetTimeline( const char* sceneLabel );
    void CaptureFrame( const ReplayBranchInfo& branch, uint32_t eventCursor, int sceneFrame, float physicsDt,
                       const ReplayWorldPresentationSample& world, const ReplayCameraSample& camera,
                       const ReplayLauncherVisualSample& launcherVisual, Physics::PhysicsEngine& physics,
                       const Gameplay::TornadoGameplay& tornadoGameplay, std::span<const char* const> entityDisplayNames );
    bool IsEnabled() const;
    ReplayRecorderStats GetStats() const;

    // Adds this track's fixed-capacity storage to the shared replay memory categories.
    void CollectMemoryCategoryBytes( SkullbonezCore::Core::MainMemoryReplayCategoryBytes& categories ) const;
    uint64_t CollectMemoryBytes() const;

    // Visits resolved samples without allocating a copied artifact vector. The
    // templated callable keeps replay iteration typed and prevents a stored
    // void-pointer callback bridge from becoming runtime authority.
    template <typename Visitor> void ForEachSampleChronological( Visitor visitor ) const
    {
        m_scrubResolveCacheValid = false;

        if ( m_sampleCount == 0 || m_samples.empty() )
        {
            return;
        }

        for ( std::size_t i = 0; i < m_sampleCount; ++i )
        {
            if ( ResolveSolverSampleAtOffset( i, m_resolvedSolverSample ) )
            {
                visitor( m_resolvedSolverSample );
            }
        }
    }

    // Visits one body's compact position stream without reconstructing dense
    // solver frames or their world snapshots. Returns false when retained
    // delta data is internally inconsistent.
    template <typename Visitor>
    bool ForEachBodyPositionChronological( Physics::PhysicsSceneObjectId targetId, Visitor visitor ) const
    {
        if ( m_sampleCount == 0 || m_samples.empty() )
        {
            return true;
        }

        if ( m_solverFrames.size() != m_samples.size() )
        {
            return false;
        }

        constexpr uint32_t invalidMetadataIndex = ( std::numeric_limits<uint32_t>::max )();
        uint32_t activeMetadataIndex = invalidMetadataIndex;
        ReplaySolverBodyState activeState;
        bool activeStateValid = false;

        for ( std::size_t offset = 0; offset < m_sampleCount; ++offset )
        {
            const std::size_t frameIndex = ( m_sampleHead + offset ) % m_samples.size();
            const ReplaySolverDeltaFrame& frame = m_solverFrames[frameIndex];
            uint32_t frameMetadataIndex = invalidMetadataIndex;

            for ( uint32_t metadataIndex : frame.bodyMetadataIndices )
            {
                if ( metadataIndex >= m_solverBodyMetadata.size() )
                {
                    return false;
                }

                if ( m_solverBodyMetadata[metadataIndex].id.value == targetId.value )
                {
                    frameMetadataIndex = metadataIndex;
                    break;
                }
            }

            if ( frameMetadataIndex == invalidMetadataIndex )
            {
                activeMetadataIndex = invalidMetadataIndex;
                activeStateValid = false;
                continue;
            }

            bool stateChanged = false;

            for ( const ReplaySolverBodyDelta& delta : frame.changedBodies )
            {
                if ( delta.metadataIndex == frameMetadataIndex )
                {
                    activeState = delta.state;
                    stateChanged = true;
                    break;
                }
            }

            if ( !stateChanged && ( frame.keyframe || !activeStateValid || activeMetadataIndex != frameMetadataIndex ) )
            {
                return false;
            }

            activeMetadataIndex = frameMetadataIndex;
            activeStateValid = true;
            visitor( m_samples[frameIndex].frameIndex, m_solverBodyMetadata[frameMetadataIndex].modelRow,
                     activeState.position );
        }

        return true;
    }
    const ReplaySolverFrameSample* LatestSample() const;
    const ReplaySolverFrameSample* SampleAtNormalized( float normalized ) const;

  private:
    // See ReplayRecorder: this is the same const-only cold materialization seam.
    friend class ReplayArtifactSource;
    std::size_t AcquireSampleSlotIndex();
    std::size_t FindOrAddSolverBodyMetadata( const ReplaySolverBodySample& body, ReplayFrameIndex frameIndex );
    void StoreSolverFramePayload( std::size_t slotIndex, const ReplaySolverFrameSample& sample,
                                  const std::vector<ReplaySolverBodySample>& bodies,
                                  const ReplaySolverWorldSnapshot& worldSnapshot, bool forceKeyframe, bool updateCarry );
    bool ResolveSolverSampleAtOffset( std::size_t offset, ReplaySolverFrameSample& outSample ) const;
    void PromoteSolverFrameToKeyframe( std::size_t offset );
    void StoreCheckpointSummary( const ReplaySolverFrameSample& sample, std::size_t bodyCount );

    ReplayRecorderConfig m_config;
    std::vector<ReplaySolverFrameSample> m_samples;
    std::vector<ReplaySolverDeltaFrame> m_solverFrames;
    std::vector<ReplaySolverBodyMetadata> m_solverBodyMetadata;

    // See the presentation track: row lookup is constant-time, then stable id
    // and metadata validation prevent a stale row hint from aliasing a body.
    std::vector<uint32_t> m_solverMetadataIndexByModelRow;
    std::vector<ReplaySolverBodyState> m_solverCarryStates;
    std::vector<uint8_t> m_solverCarryActive;
    std::vector<uint8_t> m_solverCarrySeenScratch;
    std::vector<ReplaySolverBodySample> m_solverCaptureBodies;
    std::vector<ReplayCheckpointSummary> m_checkpoints;
    std::vector<uint16_t> m_contactCountScratch;
    std::vector<float> m_maxPenetrationScratch;
    std::vector<float> m_normalImpulseSumScratch;
    ReplaySolverWorldSnapshot m_solverCaptureWorldSnapshot;
    ReplaySolverWorldSnapshot m_solverWorldCarrySnapshot;
    bool m_solverWorldCarryActive = false;

    // Lifetime: historical scrub reads and "latest" reads can be compared by
    // pointer-owning callers in the same tick, so they need separate dense
    // reconstruction caches.
    mutable ReplaySolverFrameSample m_resolvedSolverSample;
    mutable ReplaySolverFrameSample m_latestResolvedSolverSample;
    mutable ReplaySolverFrameSample m_promotedSolverSample;
    mutable std::vector<ReplaySolverBodyState> m_solverResolveStateScratch;
    mutable std::vector<uint8_t> m_solverResolveActiveScratch;

    // Invariant: the dense scrub value belongs to exactly one retained offset
    // at one content revision. Capture, reset, and artifact iteration invalidate
    // it before any caller can observe a buffer overwritten for another use.
    mutable bool m_scrubResolveCacheValid = false;
    mutable std::size_t m_scrubResolveCacheOffset = 0;
    mutable uint64_t m_scrubResolveCacheRevision = 0;
    mutable uint64_t m_denseSampleResolveCount = 0;
    uint64_t m_contentRevision = 0;
    std::size_t m_sampleHead = 0;
    std::size_t m_sampleCount = 0;
    std::size_t m_checkpointHead = 0;
    std::size_t m_checkpointCount = 0;
    ReplayFrameIndex m_nextFrameIndex = 0;
    uint64_t m_totalFramesCaptured = 0;
    uint64_t m_totalFramesEvicted = 0;
    uint64_t m_latestSolverHash = 0;
};

// Event recorder: stores runtime intent records in the same retention window as
// replay samples so future rollback can reconstruct more than body poses.
class ReplayEventRecorder
{
  public:
    bool Configure( const ReplayRecorderConfig& config );
    void ResetTimeline( const char* sceneLabel );
    void RecordEvent( const ReplayEventInput& input );
    bool IsEnabled() const;
    ReplayEventRecorderStats GetStats() const;

    // Adds this track's fixed-capacity storage to the shared replay memory categories.
    void CollectMemoryCategoryBytes( SkullbonezCore::Core::MainMemoryReplayCategoryBytes& categories ) const;
    uint64_t CollectMemoryBytes() const;

  private:
    // See ReplayRecorder: event-ring order remains private to cold ArtifactIO.
    friend class ReplayArtifactSource;
    ReplayEventSample& AcquireEventSlot();
    std::size_t EventCapacityFromConfig() const;

    ReplayRecorderConfig m_config;
    std::vector<ReplayEventSample> m_events;
    std::size_t m_eventHead = 0;
    std::size_t m_eventCount = 0;
    uint32_t m_nextSequence = 0;
    uint64_t m_totalEventsCaptured = 0;
    uint64_t m_totalEventsEvicted = 0;
};
} // namespace Runtime
} // namespace SkullbonezCore
