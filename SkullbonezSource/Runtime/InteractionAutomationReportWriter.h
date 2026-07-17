/*
File: SkullbonezSource/Runtime/InteractionAutomationReportWriter.h
Purpose:
  Owns interaction-probe evidence rows and emits the final JSON report.

Summary:
  Bounded Automation-only evidence lives here instead of on the sequencer.
  Final serialization borrows explicit runtime owners only for the call.

Mental model:
  The controller sequences runtime actions; this owner accumulates only bounded
  validation evidence and serializes it through one synchronous typed input at
  process exit.

Glossary:
  Evidence row: Machine-readable action, assertion, causal, or visual fact
    recorded by an Automation-only launch.
  Causal proof: Monotonic topology/reveal facts beside exact presented geometry.
  Durable artifact: Saved replay payload reloaded to prove report facts survive
    the writer/reader boundary.

Invariants:
  - Evidence storage exists only in the Automation configuration.
  - The writer never retains runtime-owner references from `Write`.
  - Report failure is Lane R and must not overwrite an earlier probe failure.

Related:
  - SkullbonezSource/Runtime/InteractionAutomationController.cpp
  - tools/validate_replay_visual_fidelity.bat
*/
#pragma once

#include "../Core/PlatformWin32.h"
#include "../Core/SbResult.h"
#include "../Maths/Vector3.h"
#include "RuntimeCameraMode.h"
#include "RuntimeInteractionController.h"
#include "Replay/ReplayPredictionScheduling.h"
#include "Replay/ReplayScrubber.h"
#include "Replay/ReplayVisualPacket.h"
#include "Replay/ReplayVisualPacketFingerprint.h"

#include <cstdint>
#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace UI
{
class InGameUI;
}
namespace Runtime
{
class RuntimeInteractionController;
class RuntimeTools;
class SceneController;
struct ReplayAutomationView;
struct RunCameraState;

struct InteractionAutomationRunStatus
{
    bool failed = false;
    char failure[512] = {};

    void Fail( const char* message );
    Core::SbResult Result() const;
};

struct RunInteractionAutomationReportAction
{
    int frame = 0;
    char type[64] = {};
    char target[128] = {};
    POINT mouse = {};
    bool hasMouse = false;
    bool consumed = false;
    char detail[256] = {};
};

struct RunInteractionAutomationReportAssertion
{
    int frame = 0;
    char name[64] = {};
    char expected[128] = {};
    char actual[128] = {};
    bool passed = false;
};

struct ReplayVisualFidelityReportTick
{
    int sceneFrame = 0;
    uint64_t revealFrame = 0;
    uint64_t sourceFrame = 0;
    uint64_t semanticHash = 0;
    uint64_t headerStateHash = 0;
    uint64_t trajectoryStateHash = 0;
    uint64_t topologyStateHash = 0;
    uint64_t markerStateHash = 0;
    uint64_t ghostStateHash = 0;
    uint64_t visualStateHash = 0;
    uint64_t exactPacketHash = 0;
    uint32_t schemaVersion = 0;
    uint32_t targetId = 0;
    uint32_t branchId = 0;
    uint32_t eventCursor = 0;
    uint32_t topologyVersion = 0;
    uint32_t publishedFrameCount = 0;
    bool predictionEnabled = false;
    bool predictionBuilding = false;
    bool predictionComplete = false;
    Math::Vector::Vector3 cameraEye = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 cameraUp = Math::Vector::ZERO_VECTOR;
    uint32_t trajectoryRecordCount = 0;
    uint32_t futureNodeCount = 0;
    uint32_t retainedMarkerCount = 0;
    uint32_t ghostRequestCount = 0;
    uint64_t droppedSegmentCount = 0;
    uint64_t replayReserveGrowthEvents = 0;
    bool hasGeometry = false;
    uint64_t combinedLineHash = 0;
    uint64_t combinedLineBytes = 0;
    uint32_t combinedLineVertexCount = 0;
    uint64_t ordinaryLineHash = 0;
    uint64_t priorityLineHash = 0;
    uint64_t priorityLineCanonicalHash = 0;
    uint64_t ordinaryRibbonHash = 0;
    uint64_t priorityRibbonHash = 0;
    uint64_t priorityRibbonCanonicalHash = 0;
    uint64_t vertexHash = 0;
    uint64_t ordinaryVertexHash = 0;
    uint64_t ordinaryLineBytes = 0;
    uint64_t priorityLineBytes = 0;
    uint64_t ordinaryRibbonBytes = 0;
    uint64_t priorityRibbonBytes = 0;
    uint64_t vertexBytes = 0;
    uint64_t ordinaryVertexBytes = 0;
    uint32_t ordinaryLineVertexCount = 0;
    uint32_t priorityLineVertexCount = 0;
    uint32_t ordinaryRibbonSegmentCount = 0;
    uint32_t priorityRibbonSegmentCount = 0;
    uint32_t vertexCount = 0;
    uint32_t ordinaryVertexCount = 0;
    uint32_t segmentCount = 0;
};

struct ReplayCausalProofTick
{
    uint64_t revealFrame = 0;
    uint64_t activeTopologyHash = 0;
    uint32_t activeNodeCount = 0;
    uint32_t revealedRecordCount = 0;
    uint32_t revealedPointCount = 0;
    uint32_t revealedSegmentCount = 0;
    uint32_t entryMarkerCount = 0;
    uint32_t restMarkerCount = 0;
    uint32_t horizonMarkerCount = 0;
    uint32_t ghostRequestCount = 0;
};

struct ReplayCausalTopologyNodeReport
{
    uint32_t id = 0;
    uint32_t parentId = 0;
    uint64_t firstFrame = 0;
    int depth = 0;
    bool contactDerived = false;
};

struct PredictionTrajectoryFingerprint
{
    uint64_t hash = 1469598103934665603ull;
    std::size_t recordCount = 0;
    std::size_t pointCount = 0;

    bool Ready() const
    {
        return recordCount > 0 && pointCount > 0;
    }
};

struct InteractionAutomationReportInputs
{
    InteractionAutomationRunStatus& status;
    const char* scriptPath;
    const SceneController& scene;
    const RuntimeTools& runtimeTools;
    const ReplayAutomationView& replay;
    const RuntimeInteractionController& interaction;
    const RunCameraState& camera;
    const UI::InGameUI& ui;
};

class InteractionAutomationReportWriter
{
  public:
    void Configure( const char* reportPath );
    void ReserveForActions( std::size_t actionCount );
    void AppendAction( int frame,
                       const char* type,
                       const char* target,
                       const POINT* mouse,
                       bool consumed,
                       const char* detail );
    void AppendAssertion( const RunInteractionAutomationReportAssertion& assertion );
    void AddScreenshot( const char* path );
    void BeginReplayVisualCapture( std::size_t tickCapacity );
    Core::SbResult Write( const InteractionAutomationReportInputs& inputs );

    // Report facts are centralized here so live assertions and final JSON use
    // one implementation of every validation-sensitive calculation.
    static std::string FormatPredictionHash( uint64_t hash );
    static PredictionTrajectoryFingerprint BuildPredictionTrajectoryFingerprint( const ReplayAutomationView& replay );
    static bool TryPredictionTargetDisplacement( const ReplayAutomationView& replay,
                                                 float& outDisplacement,
                                                 Math::Vector::Vector3* outFirst = nullptr,
                                                 Math::Vector::Vector3* outLast = nullptr );
    static std::size_t VisiblePredictionFrameCount( const ReplayAutomationView& replay );
    static bool ReplayPredictionPathVisible( const ReplayAutomationView& replay );
    static std::size_t ReplayPastTrajectoryPublishedPointCount( const ReplayAutomationView& replay );
    static bool ReplayPredictionContactsIncomplete( const ReplayAutomationView& replay );
    static bool LiveSolverHashStableAcrossPrediction( const ReplayAutomationView& replay,
                                                      uint64_t* outSourceHash = nullptr,
                                                      uint64_t* outLiveHash = nullptr );
    static const char* CameraModeName( RunCameraMode mode );
    static const char* WorkspaceName( RuntimeWorkspace workspace );
    static const char* OwnerName( WorldInteractionOwner owner );
    static const char* ReplayTrackName( RunReplayTrack track );
    static const char* ReplayPredictionBuildModeName( ReplayPredictionBuildMode mode );
    static uint32_t CanonicalReplayArtifactTopologyVersion( uint32_t liveVersion,
                                                            std::vector<uint32_t>& publishedVersions );
    static ReplayVisualArchiveSample BuildReplayVisualArchiveSample( const ReplayVisualFidelityReportTick& tick,
                                                                     uint32_t canonicalTopologyVersion );

    bool Written() const
    {
        return m_written;
    }
    const char* Path() const
    {
        return m_path;
    }

    std::vector<ReplayVisualFidelityReportTick> replayVisualFidelityTicks;
    std::vector<ReplayCausalProofTick> replayCausalProofTicks;
    std::vector<ReplayCausalTopologyNodeReport> replayCausalTopology;
    std::vector<ReplayVisualTrajectoryDigestState> replayVisualTrajectoryDigests;
    std::vector<uint8_t> replayVisualPredictionArchive;
    int replayVisualFidelityStartFrame = -1;
    bool replayVisualFidelityCaptureEnabled = false;
    uint64_t replayVisualFidelityTrajectoryHash = 0;
    uint64_t replayVisualFidelityTrajectoryRecordCount = 0;
    uint64_t replayVisualFidelityTrajectoryPointCount = 0;
    bool replayVisualFidelityTrajectoryCaptured = false;
    bool replayVisualOfflineProjectionComplete = false;
    uint64_t editorSelectionCaptureFingerprints[2] = {};
    bool editorSelectionCaptureValid[2] = {};

  private:
    bool m_written = false;
    char m_path[260] = {};
    std::vector<RunInteractionAutomationReportAction> m_actionReports;
    std::vector<RunInteractionAutomationReportAssertion> m_assertionReports;
    std::vector<std::string> m_screenshots;
};
} // namespace Runtime
} // namespace SkullbonezCore
