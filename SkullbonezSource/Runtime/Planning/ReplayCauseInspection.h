/*
File: ReplayCauseInspection.h
Purpose:
  Defines Planning-owned exact-frame transport and solver-detail availability.

Summary:
  Cause rows address either the retained solver ring or the active prediction
  bank. This value contract keeps the chosen frame, source track, and refusal
  state together so later transport cannot silently clamp to a nearby frame.
  ReplayCauseInspection owns one selected-event transition generation, including
  the total-elapsed 1.5-second cubic progress sample, discrete request
  coalescing, pause/return policy, and scalar publication of exact-frame solver
  detail availability consumed by App composition.

Glossary:
  Seek source: Timeline bank that must contain the row's exact frame before
    transport is enabled.
  Solver-detail source: Borrowed contact and pipeline spans stamped with the
    exact replay frame that produced them.
  Transition generation: Monotonic token that prevents an obsolete restore
    completion from changing a newer causal selection.

Invariants:
  - Available results identify one exact frame in the selected source bank.
  - Missing frames refuse transport with `Replay frame expired`.
  - Solver-detail availability is independent of frame transport eligibility.
  - A detail join requires the exact row index, contact identity, and diagnostics
    frame stamp; current or nearest-frame records are never substituted.
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
*/
#pragma once

#include "../Replay/ReplayAuthoringPackets.h"
#include "../Replay/ReplayCapturePackets.h"
#include "../../Physics/PhysicsSolverSnapshot.h"

#include <span>
#include <cstdint>

namespace SkullbonezCore::Runtime
{
struct RunReplayPredictionFrame;

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
    // Invariant: frame stamps both spans. Callers may publish live or retained
    // diagnostics only by naming the exact replay frame that produced them.
    ReplayFrameIndex frame = 0;
    std::span<const Physics::PhysicsSolverPersistentContactSample> contacts;
    std::span<const Physics::PhysicsPipelineRecord> pipelineRecords;
};

struct ReplayCauseSolverDetailResult
{
    ReplayFrameIndex frame = 0;
    ReplayCauseSolverDetailAvailability availability = ReplayCauseSolverDetailAvailability::SolverDetailNotAvailable;
    std::span<const Physics::PhysicsSolverPersistentContactSample> sourceContacts;
    std::span<const Physics::PhysicsPipelineRecord> sourcePipelineRecords;
    int bodyA = -1;
    int bodyB = -1;
    bool terrain = false;
    std::size_t contactRowCount = 0;
    std::size_t pipelineRecordCount = 0;

    bool HasDetail() const noexcept;
    const char* Feedback() const noexcept;
    const Physics::PhysicsSolverPersistentContactSample* ContactRowAt( std::size_t detailRow ) const noexcept;
    const Physics::PhysicsPipelineRecord* PipelineRecordAt( std::size_t detailRecord ) const noexcept;
};

// Builds an allocation-free borrowed view over one stamped diagnostics frame.
// The source spans must outlive use of the returned value and are never retained
// by ReplayCauseInspection.
ReplayCauseSolverDetailResult EvaluateReplayCauseSolverDetail( const RunReplayCauseTreeRow& row,
                                                               const ReplayCauseSeekResult& seek,
                                                               const ReplayCauseSolverDetailSource& source ) noexcept;

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
    bool detailVisible = false;
    bool ownsPause = false;
    bool transportInFlight = false;
    bool transportPending = false;
    bool returnIssued = false;
    float easedProgress = 0.0f;
};

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
    void PublishSolverDetail( uint64_t generation, const ReplayCauseSolverDetailResult& detail ) noexcept;
    void CompleteTransport( uint64_t generation, bool succeeded ) noexcept;
    bool BeginAftermath( bool& outReleasePause ) noexcept;
    ReplayCauseExitAction BeginReturn() noexcept;
    void CompleteReturn() noexcept;
    void Reset() noexcept;
    ReplayCauseInspectionView View() const noexcept;

  private:
    ReplayCauseInspectionView m_state;
    double m_startedAtSeconds = 0.0;
    ReplayFrameIndex m_pendingFrame = 0;
    ReplayFrameIndex m_inFlightFrame = 0;
    uint64_t m_inFlightGeneration = 0;
};

// Pure transition helpers keep cadence and integer rounding independently testable.
float EvaluateReplayCauseTransitionProgress( double elapsedSeconds ) noexcept;
ReplayFrameIndex EvaluateReplayCauseTransitionFrame( ReplayFrameIndex sourceFrame, ReplayFrameIndex targetFrame,
                                                     float easedProgress ) noexcept;
} // namespace SkullbonezCore::Runtime
