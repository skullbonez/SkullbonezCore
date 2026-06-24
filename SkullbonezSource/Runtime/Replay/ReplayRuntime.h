/*
File: SkullbonezSource/Runtime/Replay/ReplayRuntime.h
Purpose:
  Owns replay recorders and branch state for the runtime replay subsystem.

Mental model:
  ReplayRuntime is the compatibility boundary while replay behavior moves out
  of Run. Existing Run methods can still reach the legacy recorders through
  explicit accessors, but ownership now belongs to the replay subsystem.
*/
#pragma once

#include "ReplayRecorder.h"
#include "../../Maths/Quaternion.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelCollection;
} // namespace GameObjects

namespace Basics
{
struct ReplayV2SaveResult;

struct RunReplayPredictionBodyBackup
{
    ReplayBodyId id;
    int modelIndex = -1;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    float fixedContactHighlightSeconds = 0.0f;
    bool fixed = false;
};

struct RunReplayPredictionBodySample
{
    ReplayBodyId id;
    int modelIndex = -1;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
};

struct RunReplayPredictionFrame
{
    ReplayFrameIndex frameIndex = 0;
    double simulationSeconds = 0.0;
    float tornadoSystemElapsedSeconds = 0.0f;
    std::vector<RunReplayPredictionBodySample> bodies;
    std::vector<Physics::PhysicsDebugContact> debugContacts;
};

class ReplayRuntime
{
  public:
    struct RecordingConfigResult
    {
        ReplayRecorderConfig presentationConfig;
        ReplayRecorderConfig solverConfig;
        ReplayRecorderStats presentationStats;
        ReplayRecorderStats solverStats;
        ReplayEventRecorderStats eventStats;
    };

    ReplayRecorder& Presentation();
    const ReplayRecorder& Presentation() const;

    ReplaySolverRecorder& Solver();
    const ReplaySolverRecorder& Solver() const;

    ReplayEventRecorder& Events();
    const ReplayEventRecorder& Events() const;

    ReplayBranchInfo& Branch();
    const ReplayBranchInfo& Branch() const;

    RecordingConfigResult ConfigureRecording( bool enabled, int retentionSeconds, const char* hashLogPath );
    void FlushHashLogs();
    void ResetBranch();
    void ResetTimeline( const char* sceneLabel );
    bool IsPresentationEnabled() const;
    bool IsCaptureEnabled() const;
    ReplayRecorderStats PresentationStats() const;
    ReplayRecorderStats SolverStats() const;
    ReplayEventRecorderStats EventStats() const;
    ReplayFrameIndex NextEventFrameIndex() const;
    void CaptureFrame( ReplayCaptureInput input );
    bool ApplyPresentationSampleForRender( GameObjects::GameModelCollection& models,
                                           const ReplayPresentationSample& sample );
    bool ApplySolverSampleForRender( GameObjects::GameModelCollection& models, const ReplaySolverFrameSample& sample );
    bool ApplyPredictionFrameForRender( GameObjects::GameModelCollection& models,
                                        const RunReplayPredictionFrame& frame );
    void RestoreRenderPose( GameObjects::GameModelCollection& models );
    std::vector<uint8_t>& FocusModelMask();
    const std::vector<uint8_t>& FocusModelMask() const;
    bool HasLauncherVisualBackup() const;
    void StoreLauncherVisualBackup( const ReplayLauncherVisualSample& sample );
    const ReplayLauncherVisualSample& LauncherVisualBackup() const;
    void ClearLauncherVisualBackup();
    void RecordEvent( ReplayEventKind kind,
                      ReplayFrameIndex frameIndex,
                      uint32_t flags,
                      int32_t value0,
                      int32_t value1,
                      int32_t value2,
                      int32_t value3,
                      uint64_t data0,
                      const char* text );
    bool SaveSolverReplay( const char* path ) const;
    bool SavePresentationWithSolverHashes( const char* path, ReplayV2SaveResult* result = nullptr ) const;

  private:
    struct RenderPoseBackup
    {
        int modelIndex = -1;
        Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
        Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    };

    ReplayRecorder m_presentation; // Bounded replay presentation recorder for recent-frame inspection.
    ReplaySolverRecorder m_solver; // Same-tick solver-state recorder kept in tandem with presentation replay.
    ReplayEventRecorder m_events;  // Bounded intent/event stream kept beside v2 replay tracks.
    ReplayBranchInfo m_branch;     // Current live replay branch provenance.
    std::vector<RenderPoseBackup> m_renderPoseBackups;
    std::vector<uint8_t> m_focusModelMask;
    ReplayLauncherVisualSample m_launcherVisualBackup;
    bool m_launcherVisualBackupActive = false;
};
} // namespace Basics
} // namespace SkullbonezCore
