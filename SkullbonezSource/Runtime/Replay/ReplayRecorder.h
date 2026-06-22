/*
File: SkullbonezSource/Runtime/Replay/ReplayRecorder.h
Purpose:
  Defines bounded replay capture records for simulation debugging.

Mental model:
  Replay capture has two bounded tracks. Presentation samples feed immediate
  visual scrubbing. Solver samples keep same-tick body constants, inertia,
  sleep/contact summaries, and hashes for the authoritative rollback path.

Glossary:
  Presentation sample: A compact, render-facing record of one committed physics
  tick. It is useful for inspection, but it is not enough to restore the solver.
  Solver sample: A same-tick physics-state record with extra mass and inertia
  inputs. It is still not a full restore checkpoint until persistent contacts,
  event streams, and hidden solver caches are captured.
  Checkpoint summary: A replay boundary marker with hashes and counts. It is
  deliberately not an authoritative restore checkpoint yet.
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "../Editor/LauncherLaser.h"
#include "ReplaySolverSnapshot.h"
#include "../../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Environment
{
class CameraCollection;
class WorldEnvironment;
} // namespace Environment

namespace GameObjects
{
class GameModelCollection;
} // namespace GameObjects

namespace Basics
{
using ReplayFrameIndex = uint64_t;

struct ReplayBodyId
{
    uint32_t value = 0;
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
    bool fixedStep = false;
    bool scenePhysicsEnabled = true;
    bool sceneTextEnabled = true;
};

struct ReplayBodyPresentationSample
{
    ReplayBodyId id;
    int modelIndex = -1;
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

struct ReplayPresentationSample
{
    ReplayFrameIndex frameIndex = 0;
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

struct ReplaySolverBodySample
{
    ReplayBodyId id;
    int modelIndex = -1;
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

enum class ReplayLauncherFireMode : uint8_t
{
    Laser,
    Projectile
};

struct ReplayRayCastLineSample
{
    Math::Vector::Vector3 start = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 end = Math::Vector::ZERO_VECTOR;
    float ageSeconds = 0.0f;
    bool active = false;
    bool hit = false;
};

struct ReplayLauncherVisualSample
{
    std::vector<ReplayRayCastLineSample> rayLines;
    std::vector<LauncherLaserShotSnapshot> laserShots;
    int nextRayLine = 0;
    int nextLaserShot = 0;
    ReplayLauncherFireMode fireMode = ReplayLauncherFireMode::Laser;
    bool visualizeRays = false;
    float impulseStrength = 0.0f;
    float projectileSpeed = 0.0f;
};

struct ReplaySolverFrameSample
{
    ReplayFrameIndex frameIndex = 0;
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

struct ReplayCheckpointSummary
{
    ReplayFrameIndex frameIndex = 0;
    double simulationSeconds = 0.0;
    uint64_t stateHash = 0;
    uint32_t bodyCount = 0;
    uint16_t contactCount = 0;
    uint16_t pipelineRecordCount = 0;
};

struct ReplayCaptureInput
{
    int sceneFrame = 0;
    double simulationSeconds = 0.0;
    float physicsDt = 0.0f;
    bool fixedStep = false;
    bool scenePhysicsEnabled = true;
    bool sceneTextEnabled = true;
    bool waterHidden = false;
    bool terrainHidden = false;
    Environment::CameraCollection* cameras = nullptr;
    Environment::WorldEnvironment* world = nullptr;
    GameObjects::GameModelCollection* models = nullptr;
    const ReplayLauncherVisualSample* launcherVisual = nullptr;
};

struct ReplayRecorderConfig
{
    bool enabled = false;
    int retentionSeconds = 30;
    int checkpointIntervalFrames = 30;
    std::string hashLogPath;
};

struct ReplayRecorderStats
{
    bool enabled = false;
    uint64_t totalFramesCaptured = 0;
    uint64_t totalFramesEvicted = 0;
    ReplayFrameIndex nextFrameIndex = 0;
    std::size_t sampleCapacity = 0;
    std::size_t sampleCount = 0;
    std::size_t checkpointCapacity = 0;
    std::size_t checkpointCount = 0;
    uint64_t latestStateHash = 0;
};

using ReplaySolverSampleVisitor = void ( * )( const ReplaySolverFrameSample& sample, void* userData );

class ReplayRecorder
{
  public:
    bool Configure( const ReplayRecorderConfig& config );
    void ResetTimeline( const char* sceneLabel );
    void CaptureFrame( const ReplayCaptureInput& input );
    void FlushHashLog();
    bool IsEnabled() const;
    ReplayRecorderStats GetStats() const;
    void CopySamplesChronological( std::vector<ReplayPresentationSample>& outSamples ) const;
    const ReplayPresentationSample* LatestSample() const;
    const ReplayPresentationSample* SampleAtNormalized( float normalized ) const;

  private:
    ReplayPresentationSample& AcquireSampleSlot();
    void StoreCheckpointSummary( const ReplayPresentationSample& sample );
    void WriteHashLogHeader( const char* sceneLabel );
    void WriteHashLogRow( const ReplayPresentationSample& sample );
    std::size_t SampleCapacityFromConfig() const;
    std::size_t CheckpointCapacityFromConfig() const;

    ReplayRecorderConfig m_config;
    std::vector<ReplayPresentationSample> m_samples;
    std::vector<ReplayCheckpointSummary> m_checkpoints;
    std::vector<uint16_t> m_contactCountScratch;
    std::vector<float> m_maxPenetrationScratch;
    std::vector<float> m_normalImpulseSumScratch;
    std::ofstream m_hashLog;
    std::size_t m_sampleHead = 0;
    std::size_t m_sampleCount = 0;
    std::size_t m_checkpointHead = 0;
    std::size_t m_checkpointCount = 0;
    ReplayFrameIndex m_nextFrameIndex = 0;
    uint64_t m_totalFramesCaptured = 0;
    uint64_t m_totalFramesEvicted = 0;
    uint64_t m_latestStateHash = 0;
};

class ReplaySolverRecorder
{
  public:
    bool Configure( const ReplayRecorderConfig& config );
    void ResetTimeline( const char* sceneLabel );
    void CaptureFrame( const ReplayCaptureInput& input );
    void FlushHashLog();
    bool IsEnabled() const;
    ReplayRecorderStats GetStats() const;
    void CopySamplesChronological( std::vector<ReplaySolverFrameSample>& outSamples ) const;
    void ForEachSampleChronological( ReplaySolverSampleVisitor visitor, void* userData ) const;
    const ReplaySolverFrameSample* LatestSample() const;
    const ReplaySolverFrameSample* SampleAtNormalized( float normalized ) const;

  private:
    ReplaySolverFrameSample& AcquireSampleSlot();
    void StoreCheckpointSummary( const ReplaySolverFrameSample& sample );
    void WriteHashLogHeader( const char* sceneLabel );
    void WriteHashLogRow( const ReplaySolverFrameSample& sample );
    std::size_t SampleCapacityFromConfig() const;
    std::size_t CheckpointCapacityFromConfig() const;

    ReplayRecorderConfig m_config;
    std::vector<ReplaySolverFrameSample> m_samples;
    std::vector<ReplayCheckpointSummary> m_checkpoints;
    std::vector<uint16_t> m_contactCountScratch;
    std::vector<float> m_maxPenetrationScratch;
    std::vector<float> m_normalImpulseSumScratch;
    std::ofstream m_hashLog;
    std::size_t m_sampleHead = 0;
    std::size_t m_sampleCount = 0;
    std::size_t m_checkpointHead = 0;
    std::size_t m_checkpointCount = 0;
    ReplayFrameIndex m_nextFrameIndex = 0;
    uint64_t m_totalFramesCaptured = 0;
    uint64_t m_totalFramesEvicted = 0;
    uint64_t m_latestSolverHash = 0;
};
} // namespace Basics
} // namespace SkullbonezCore
