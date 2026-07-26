/*
File: SkullbonezSource/Runtime/Automation/InteractionAutomationReportWriter.h
Purpose:
  Owns interaction-probe capture state, offline verification, and final reports.

Summary:
  Bounded Automation-only evidence, reveal scheduling, archive freezing, and
  CPU-only replay projection live here instead of on the sequencer. Runtime
  owners are borrowed only for synchronous verification and serialization.

Mental model:
  The controller sequences runtime actions and publishes typed reveal intent.
  This owner decides when capture starts, records each presented packet, freezes
  the archive, verifies it without presenting again, and serializes the result.

Glossary:
  Evidence row: Machine-readable action, assertion, causal, or visual fact
    recorded by an Automation-only launch.
  Causal proof: Monotonic topology/reveal facts beside exact presented geometry.
  Offline projection: CPU-only reconstruction of captured replay presentation
    used to compare durable state without a second visible run.
  Durable artifact: Saved replay payload reloaded to prove report facts survive
    the writer/reader boundary.

Invariants:
  - Evidence storage exists only in the Automation configuration.
  - The writer never retains runtime-owner references from verification or `Write`.
  - Offline projection cannot submit rendering or start another prediction.
  - Report failure is Lane R and must not overwrite an earlier probe failure.

Related:
  - SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp
  - tools/validate_replay_visual_fidelity.bat
*/
#pragma once

#include "../../Core/PlatformWin32.h"
#include "../../Core/SbResult.h"
#include "../../Maths/Vector3.h"
#include "../Camera/RuntimeCameraMode.h"
#include "../Interaction/RuntimeInteractionController.h"
#include "../Planning/ReplayOverlayRenderer.h"
#include "../Prediction/ReplayPredictionDrawing.h"
#include "../Prediction/ReplayPredictionPackets.h"
#include "../Replay/ReplayTimelinePackets.h"
#include "../Replay/ReplayVisualPacket.h"
#include "../Replay/ReplayVisualPacketFingerprint.h"
#include "../Tools/RuntimeTools.h"

#include <cstdint>
#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Rendering
{
struct RenderSceneSnapshot;
}
namespace UI
{
class InGameUI;
}
namespace Runtime
{
class RuntimeInteractionController;
class RuntimeTools;
class SceneWorld;
struct SceneSessionState;
struct ReplaySolverFrameSample;
struct ReplayAutomationView;
struct CameraControlState;

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
    // Lifetime: the writer consumes this projection synchronously. SceneWorld,
    // lifecycle state, and path text remain separate so report code cannot
    // submit scene requests or recover the lifecycle controller.
    InteractionAutomationRunStatus& status;
    const char* scriptPath;
    const SceneWorld& world;
    const SceneSessionState& scene;
    const char* scenePath;
    const RuntimeTools& runtimeTools;
    const ReplayAutomationView& replay;
    const RuntimeInteractionController& interaction;
    const CameraControlState& camera;
    const UI::InGameUI& ui;
    const Rendering::RenderSceneSnapshot& renderSnapshot;
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

    // Capture commands own all mutable replay evidence. The reveal command
    // returns only the frame intent the sequencer must publish; Finish borrows
    // runtime owners synchronously for the CPU-only offline proof.
    void BeginReplayVisualCapture( std::size_t tickCapacity );
    bool UpdateReplayVisualReveal( int sceneFrame,
                                   int fixedStartFrame,
                                   bool liveAdvanceHeld,
                                   bool revealReady,
                                   InteractionAutomationRunStatus& status,
                                   ReplayFrameIndex& outRevealFrame,
                                   bool& outResetReveal ) noexcept;
    bool CaptureReplayVisualFrame( int sceneFrame,
                                   const ReplayAutomationView& replay,
                                   InteractionAutomationRunStatus& status );
    bool FinishReplayVisualCapture( InteractionAutomationRunStatus& status,
                                    RuntimeTools& runtimeTools,
                                    SceneWorld& world,
                                    const ReplayAutomationView& replay );
    bool ReplayVisualCaptureEnabled() const noexcept;

    // Selection evidence is addressed by the script's two fixed slots. No
    // mutable slot storage is exposed to the automation sequencer.
    void ResetEditorSelectionCaptures() noexcept;
    void CaptureEditorSelection( int slot, uint64_t fingerprint, bool valid ) noexcept;
    bool TryEditorSelectionCapture( int slot, uint64_t& outFingerprint ) const noexcept;
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

  private:
    bool VerifyReplayVisualOfflineProjection( InteractionAutomationRunStatus& status,
                                              RuntimeTools& runtimeTools,
                                              SceneWorld& world,
                                              const ReplaySolverFrameSample* latestSolverSample );

    bool m_written = false;
    char m_path[260] = {};
    std::vector<RunInteractionAutomationReportAction> m_actionReports;
    std::vector<RunInteractionAutomationReportAssertion> m_assertionReports;
    std::vector<std::string> m_screenshots;
    // Lifetime: these rows are bounded Automation evidence. They never escape
    // this writer as mutable state or survive the process that records them.
    std::vector<ReplayVisualFidelityReportTick> m_replayVisualFidelityTicks;
    std::vector<ReplayCausalProofTick> m_replayCausalProofTicks;
    std::vector<ReplayCausalTopologyNodeReport> m_replayCausalTopology;
    std::vector<ReplayVisualTrajectoryDigestState> m_replayVisualTrajectoryDigests;
    std::vector<uint8_t> m_replayVisualPredictionArchive;
    // Lifetime: the offline verifier owns the same retained CPU command-list
    // shape as ReplayRuntime. Keeping its large fixed reserves on this
    // startup-allocated Automation owner avoids function-stack construction.
    EditorTracer m_replayVisualPredictionDrawList;
    ReplayOverlay::ReplayPredictionDrawListState m_replayVisualPredictionDrawListState;
    ReplayVisualPacket m_replayVisualPredictionDrawPacket;
    Math::Vector::Vector3 m_replayVisualPredictionDrawCameraEye = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 m_replayVisualPredictionDrawCameraUp = Math::Vector::ZERO_VECTOR;
    uint64_t m_replayVisualPredictionDrawStreamId = 1;
    uint64_t m_replayVisualPredictionDrawRevision = 0;
    bool m_replayVisualPredictionDrawCameraValid = false;
    int m_replayVisualFidelityStartFrame = -1;
    bool m_replayVisualFidelityCaptureEnabled = false;
    uint64_t m_replayVisualFidelityTrajectoryHash = 0;
    uint64_t m_replayVisualFidelityTrajectoryRecordCount = 0;
    uint64_t m_replayVisualFidelityTrajectoryPointCount = 0;
    bool m_replayVisualFidelityTrajectoryCaptured = false;
    bool m_replayVisualOfflineProjectionComplete = false;
    uint64_t m_editorSelectionCaptureFingerprints[2] = {};
    bool m_editorSelectionCaptureValid[2] = {};
};
} // namespace Runtime
} // namespace SkullbonezCore
