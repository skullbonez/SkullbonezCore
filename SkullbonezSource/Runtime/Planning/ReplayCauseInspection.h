/*
File: ReplayCauseInspection.h
Purpose:
  Defines Planning-owned exact-frame transport and solver-detail availability.

Summary:
  Cause rows address either the retained solver ring or the active prediction
  bank. This value contract keeps the chosen frame, source track, and refusal
  state together so later transport cannot silently clamp to a nearby frame.
  ReplayCauseInspection owns one selected-event transition generation, including
  the total-elapsed 1.5-second transport progress sample, discrete request
  coalescing, pause/return policy, exact-frame solver-detail availability, and
  fixed-capacity copies of the feature-neutral contact packet, contact rows,
  and pipeline records consumed synchronously by App composition. It also owns
  the drawer scroll offset and the pure hierarchy-plus-drawer compound layout;
  ReplayAuthoring remains the only retained anchor and resize owner.

Glossary:
  Seek source: Timeline bank that must contain the row's exact frame before
    transport is enabled.
  Solver-detail source: Recorded spans or a segmented prediction-frame view,
    stamped with the exact replay frame that produced them; publication copies
    selected values into Planning.
  Transition generation: Monotonic token that prevents an obsolete restore
    completion from changing a newer causal selection.

Invariants:
  - Available results identify one exact frame in the selected source bank.
  - Missing frames refuse transport with `Replay frame expired`.
  - Solver-detail availability is independent of frame transport eligibility.
  - A detail join requires the exact row index, contact identity, and diagnostics
    frame stamp; current or nearest-frame records are never substituted.
  - Contact presentation and solver detail copy bounded values before restore
    may retire the source ring; published spans point only into Planning-owned
    fixed arrays and remain synchronous views.
  - Drawing, input, Automation, and tests use the same compound layout. The
    drawer stores no x/y position and projects every control from Replay's
    hierarchy anchor, shared height, and one supplied animation progress.
  - Drawer rows and manifold geometry are one focused evidence surface: every
    retarget, aftermath, return, failure, or reset drops published values before
    the independently timed drawer shell can finish closing.
  - At most one transport request is in flight; a newer selection replaces the
    pending request and cannot be completed by an older generation.
  - The published eased sample is the single causal-transition clock consumed
    by both replay-frame selection and CameraCollection presentation.
  - Camera identity remains in ReplayPresentation; this owner retains only the
    transition and pause policy, never a second restore-camera copy.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayAuthoringPackets.h
  - SkullbonezSource/Runtime/Replay/ReplayCapturePackets.h
  - SkullbonezSource/Physics/PhysicsSolverSnapshot.h
  - SkullbonezSource/Rendering/ContactManifoldPresentation.h
*/
#pragma once

#include "../Replay/ReplayAuthoringPackets.h"
#include "../Replay/ReplayCapturePackets.h"
#include "../Prediction/ReplayPredictionSolverEvidenceStore.h"
#include "../../Physics/PhysicsSolverSnapshot.h"
#include "../../Physics/PhysicsStageCapacity.h"
#include "../../Rendering/ContactManifoldPresentation.h"
#include "../../UI/UIDraw.h"

#include <array>
#include <span>
#include <cstdint>

namespace SkullbonezCore::Runtime
{
struct RunReplayPredictionFrame;
struct ReplaySolverFrameSample;

enum class ReplayCauseSeekSource
{
    SolverHistory,
    Prediction
};

enum class ReplayCauseSeekAvailability
{
    Available,
    ReplayFrameExpired
};

struct ReplayCauseSeekResult
{
    // Invariant: frame and source always describe the requested row even when
    // availability refuses transport, so diagnostics never report a clamped substitute.
    ReplayFrameIndex frame = 0;
    ReplayCauseSeekSource source = ReplayCauseSeekSource::SolverHistory;
    ReplayCauseSeekAvailability availability = ReplayCauseSeekAvailability::ReplayFrameExpired;

    bool CanTransport() const noexcept;
    const char* Feedback() const noexcept;
};

enum class ReplayCauseSolverDetailAvailability
{
    Available,
    SolverDetailNotAvailable,
    ReplayFrameExpired
};

struct ReplayCauseSolverDetailSource
{
    // Invariant: frame stamps either the recorded spans or the prediction view.
    // Callers may publish diagnostics only by naming their exact source frame.
    ReplayFrameIndex frame = 0;
    std::span<const Physics::PhysicsSolverPersistentContactSample> contacts;
    std::span<const Physics::PhysicsPipelineRecord> pipelineRecords;
    ReplayPredictionSolverEvidenceFrameView prediction;
};

struct ReplayCauseSolverDetailResult
{
    ReplayFrameIndex frame = 0;
    ReplayCauseSolverDetailAvailability availability = ReplayCauseSolverDetailAvailability::SolverDetailNotAvailable;
    std::span<const Physics::PhysicsSolverPersistentContactSample> sourceContacts;
    std::span<const Physics::PhysicsPipelineRecord> sourcePipelineRecords;
    ReplayPredictionSolverEvidenceFrameView predictionSource;
    int bodyA = -1;
    int bodyB = -1;
    bool terrain = false;
    std::size_t contactRowCount = 0;
    std::size_t pipelineRecordCount = 0;

    bool HasDetail() const noexcept;
    const char* Feedback() const noexcept
    {
        switch ( availability )
        {
        case ReplayCauseSolverDetailAvailability::Available:
            return "";
        case ReplayCauseSolverDetailAvailability::ReplayFrameExpired:
            return "Replay frame expired";
        case ReplayCauseSolverDetailAvailability::SolverDetailNotAvailable:
        default:
            return "Solver detail not available";
        }
    }
    const Physics::PhysicsSolverPersistentContactSample* ContactRowAt( std::size_t detailRow ) const noexcept;
    const Physics::PhysicsPipelineRecord* PipelineRecordAt( std::size_t detailRecord ) const noexcept;
    std::size_t SourceContactCount() const noexcept;
    std::size_t SourcePipelineCount() const noexcept;
    const Physics::PhysicsSolverPersistentContactSample* SourceContactAt( std::size_t index ) const noexcept;
    const Physics::PhysicsPipelineRecord* SourcePipelineAt( std::size_t index ) const noexcept;
};

// Builds an allocation-free borrowed view over one stamped diagnostics frame.
// The source spans must outlive use of the returned value and are never retained
// by ReplayCauseInspection.
ReplayCauseSolverDetailResult EvaluateReplayCauseSolverDetail( const RunReplayCauseTreeRow& row,
                                                               const ReplayCauseSeekResult& seek,
                                                               const ReplayCauseSolverDetailSource& source ) noexcept;

// Projects exact-frame solver values into an owned, feature-neutral Rendering
// packet. Patches above the generic Rendering capacity publish the bounded
// prefix with `truncated`; an empty packet means frame/body evidence could not
// be proven without reconstructing discarded source evidence.
Rendering::ContactManifoldPresentation BuildReplayCauseContactPresentation( const ReplayCauseSolverDetailResult& detail,
                                                                            const ReplaySolverFrameSample& sample ) noexcept;
Rendering::ContactManifoldPresentation BuildReplayCauseContactPresentation( const ReplayCauseSolverDetailResult& detail,
                                                                            const RunReplayPredictionFrame& frame ) noexcept;

ReplayCauseSeekResult EvaluateReplayCauseSeek( const RunReplayCauseTreeRow& row, const ReplayRecorderStats& solverStats,
                                               std::span<const RunReplayPredictionFrame> predictionFrames ) noexcept;

enum class ReplayCauseInspectionMode : uint8_t
{
    Inactive,
    Transporting,
    DetailPaused,
    AftermathFollow,
    Returning
};

struct ReplayCauseTransportRequest
{
    uint64_t generation = 0;
    ReplayFrameIndex sourceFrame = 0;
    ReplayFrameIndex targetFrame = 0;
    ReplayCauseSeekSource source = ReplayCauseSeekSource::SolverHistory;
};

enum class ReplayCauseInspectorTab : uint8_t
{
    Summary,
    RawRecord,
    Iterations
};

struct ReplayCauseInspectionView
{
    ReplayCauseInspectionMode mode = ReplayCauseInspectionMode::Inactive;
    uint64_t generation = 0;
    ReplayFrameIndex sourceFrame = 0;
    ReplayFrameIndex targetFrame = 0;
    ReplayFrameIndex presentedFrame = 0;
    ReplayFrameIndex transportFrame = 0;
    ReplayCauseSeekSource seekSource = ReplayCauseSeekSource::SolverHistory;
    ReplayCauseSolverDetailAvailability
        solverDetailAvailability = ReplayCauseSolverDetailAvailability::SolverDetailNotAvailable;
    int selectedRow = -1;
    std::size_t solverDetailContactRowCount = 0;
    std::size_t solverDetailPipelineRecordCount = 0;
    std::span<const Physics::PhysicsSolverPersistentContactSample> solverDetailContacts;
    std::span<const Physics::PhysicsPipelineRecord> solverDetailPipelineRecords;
    const char* solverDetailFeedback = "Solver detail not available";
    int solverDetailFirstRow = 0;
    Rendering::ContactManifoldPresentation contactPresentation;
    ReplayCauseInspectorTab activeTab = ReplayCauseInspectorTab::Summary;
    bool detailVisible = false;
    bool ownsPause = false;
    bool transportInFlight = false;
    bool transportPending = false;
    bool returnIssued = false;
    float easedProgress = 0.0f;
    float drawerProgress = 0.0f;
};

// These host-decision seams keep keyboard and pointer mapping testable while
// ReplayCauseInspection remains the sole retained transition owner.
bool ShouldBeginReplayCauseAftermath( const ReplayCauseInspectionView& inspection, bool spaceDown ) noexcept;
bool ShouldBeginReplayCauseReturn( const ReplayCauseInspectionView& inspection, bool nonSelectionClick,
                                   bool scrubExit ) noexcept;

inline constexpr int REPLAY_CAUSE_SOLVER_PANEL_VISIBLE_ROWS = 4;
inline constexpr double REPLAY_CAUSE_INSPECTOR_DRAWER_SECONDS = 0.18;
inline constexpr float REPLAY_CAUSE_INSPECTOR_DRAWER_WIDTH = 520.0f;
inline constexpr float REPLAY_CAUSE_INSPECTOR_DRAWER_MIN_WIDTH = 280.0f;
inline constexpr float REPLAY_CAUSE_INSPECTOR_DRAWER_HEADER_HEIGHT = 76.0f;
inline constexpr float REPLAY_CAUSE_INSPECTOR_TAB_HEIGHT = 38.0f;
inline constexpr float REPLAY_CAUSE_INSPECTOR_PADDING = 12.0f;
inline constexpr float REPLAY_CAUSE_INSPECTOR_SHARED_SEAM_WIDTH = 1.0f;
inline constexpr float REPLAY_CAUSE_INSPECTOR_SCROLLBAR_WIDTH = 5.0f;
inline constexpr float REPLAY_CAUSE_INSPECTOR_CLOSE_SIZE = 22.0f;
inline constexpr float REPLAY_CAUSE_SOLVER_PANEL_EMPTY_HEIGHT = 44.0f;
inline constexpr float REPLAY_CAUSE_SOLVER_PANEL_BASE_ROW_HEIGHT = 82.0f;
inline constexpr float REPLAY_CAUSE_SOLVER_PANEL_ITERATION_LINE_HEIGHT = 12.0f;
inline constexpr float REPLAY_CAUSE_SOLVER_PANEL_OPACITY = 0.78f;
inline constexpr int REPLAY_CAUSE_SOLVER_PANEL_ITERATIONS_PER_LINE = 4;
inline constexpr const char*
    REPLAY_CAUSE_SOLVER_PANEL_UNITS = "UNITS: vectors/penetration/correction = scene units; bias/linear writeback = u/s;";
inline constexpr const char*
    REPLAY_CAUSE_SOLVER_PANEL_UNITS_MORE = "angular = rad/s; impulses = mass*u/s; effective masses = mass.";
inline constexpr const char* REPLAY_CAUSE_SOLVER_PANEL_SIGNS = "SIGNS: +penetration = overlap; normal/t1/t2 = world-space;";
inline constexpr const char*
    REPLAY_CAUSE_SOLVER_PANEL_SIGNS_MORE = "signed accT1/accT2 follow t1/t2; CLAMP = frictionLimit reached.";

struct ReplayCauseSolverPanelRowText
{
    // Invariant: this detached projection is the exact text consumed by the
    // panel. Tests can pin value-to-label mapping without a renderer backend.
    char headline[128] = {};
    char basis[256] = {};
    char geometry[256] = {};
    char masses[256] = {};
    char impulses[256] = {};
};

struct ReplayCauseSummaryText
{
    char normalImpulse[48] = {};
    char frictionImpulse[48] = {};
    char penetration[48] = {};
    char effectiveMass[48] = {};
    char identity[160] = {};
    char dynamics[256] = {};
    char policy[256] = {};
};

struct ReplayCauseInspectorLayout
{
    UI::UIRect hierarchy;
    UI::UIRect hierarchyTitle;
    UI::UIRect hierarchyScrollbar;
    UI::UIRect resize;
    UI::UIRect drawer;
    UI::UIRect visibleDrawer;
    UI::UIRect targetDrawer;
    UI::UIRect drawerTitle;
    UI::UIRect drawerClose;
    std::array<UI::UIRect, 3> tabs;
    UI::UIRect content;
    UI::UIRect drawerScrollbar;
    UI::UIRect sharedSeam;
    UI::UIRect compound;
    UI::UIRect targetCompound;
    float rowHeight = REPLAY_CAUSE_SOLVER_PANEL_BASE_ROW_HEIGHT;
    float drawerProgress = 0.0f;
    int visibleRows = 0;
};

// Concept: one projection describes both retained Replay placement and the
// Planning-owned attached drawer. `drawerProgress` is already eased; CHUI3's
// lifecycle supplies it without adding a second placement owner.
ReplayCauseInspectorLayout BuildReplayCauseInspectorLayout( const ReplayCauseInspectionView& inspection,
                                                            const RunReplayCauseTreeState& causeTree, int screenWidth,
                                                            int screenHeight, float drawerProgress ) noexcept;
bool ReplayCauseInspectorContainsPoint( const ReplayCauseInspectorLayout& layout, int x, int y ) noexcept;
bool ReplayCauseInspectorDrawerTitleContainsPoint( const ReplayCauseInspectorLayout& layout, int x, int y ) noexcept;
int ReplayCauseSolverDetailIterationCount( const ReplayCauseInspectionView& inspection, std::size_t contactRow ) noexcept;
ReplayCauseSolverPanelRowText BuildReplayCauseSolverPanelRowText( const ReplayCauseInspectionView& inspection,
                                                                  int rowIndex ) noexcept;
ReplayCauseSummaryText BuildReplayCauseSummaryText( const ReplayCauseInspectionView& inspection, int rowIndex ) noexcept;

struct ReplayCauseExitAction
{
    bool apply = false;
    bool releasePause = false;
};

class ReplayCauseInspection
{
  public:
    bool Select( int rowIndex, const ReplayCauseSeekResult& seek, ReplayFrameIndex presentedFrame,
                 bool simulationAlreadyPaused, double nowSeconds ) noexcept;
    void Advance( double nowSeconds ) noexcept;
    bool TakeTransportRequest( ReplayCauseTransportRequest& outRequest ) noexcept;
    void PublishSolverDetail( uint64_t generation, const ReplayCauseSolverDetailResult& detail,
                              const Rendering::ContactManifoldPresentation& contactPresentation = {} ) noexcept;
    void CompleteTransport( uint64_t generation, bool succeeded ) noexcept;
    bool BeginAftermath( bool& outReleasePause ) noexcept;
    ReplayCauseExitAction BeginReturn() noexcept;
    void CompleteReturn() noexcept;
    bool TickSolverDetailPanelInput( const RunReplayCauseTreeState& causeTree, int mouseX, int mouseY,
                                     bool hasClientPosition, bool pointerBlocked, bool leftPressed, int wheelDelta,
                                     int screenWidth, int screenHeight ) noexcept;
    void Reset() noexcept;
    ReplayCauseInspectionView View() const noexcept;

  private:
    void ClearFocusedSurface() noexcept;
    void SetDrawerTarget( bool open, double nowSeconds ) noexcept;
    void AdvanceDrawer( double nowSeconds ) noexcept;

    ReplayCauseInspectionView m_state;

    // Lifetime: these fixed arrays detach exact-frame evidence before Replay
    // restore can retire its ring. They never grow and their spans remain valid
    // until the next selection or reset.
    std::array<Physics::PhysicsSolverPersistentContactSample, Rendering::CONTACT_MANIFOLD_PRESENTATION_POINT_CAPACITY>
        m_solverDetailContacts {};
    std::array<Physics::PhysicsPipelineRecord, Physics::PHYSICS_MAX_PIPELINE_TRACE_RECORDS> m_solverDetailPipelineRecords {};
    double m_startedAtSeconds = 0.0;
    double m_lastAdvanceSeconds = 0.0;
    double m_drawerStartedAtSeconds = 0.0;
    float m_drawerStartProgress = 0.0f;
    bool m_drawerTargetOpen = false;
    ReplayFrameIndex m_pendingFrame = 0;
    ReplayFrameIndex m_inFlightFrame = 0;
    uint64_t m_inFlightGeneration = 0;
};

// Pure transition helpers keep cadence and integer rounding independently testable.
float EvaluateReplayCauseTransitionProgress( double elapsedSeconds ) noexcept;
ReplayFrameIndex EvaluateReplayCauseTransitionFrame( ReplayFrameIndex sourceFrame, ReplayFrameIndex targetFrame,
                                                     float easedProgress ) noexcept;
} // namespace SkullbonezCore::Runtime
