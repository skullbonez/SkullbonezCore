/*
File: SkullbonezSource/Runtime/Scene/SceneGeneratedControlTransaction.h
Purpose:
  Declares the phase-enforcing generated-scene rebuild transaction.

Summary:
  Scene UI controls can rebuild the generated model pool while keeping the
  active scene selected. A stack-scoped transaction owns request arbitration,
  drain/reset ordering, deterministic repopulation, and detached follow-up
  publication without retaining any borrowed runtime owner.

Glossary:
  Generated control transaction: One accepted count request and its ordered
    rebuild phases.
  Generated UI command: One-frame Scene/Run tab request for generated object
    counts.
  Rebuild action: Returned flags for caller-owned replay/profiler cleanup.
  Action status: Lane R result that blocks all rebuild mutations when the GPU
    drain cannot prove old resource use complete.

Invariants:
  - Every accepted command walks DrainAndReset, Repopulate,
    PublishFollowUps, and Complete in that order.
  - The transaction stores request/policy/result values only; every mutable
    runtime owner is borrowed by Execute and expires before it returns.
  - Generated model/resource mutation starts only after a successful GPU drain;
    an illegal phase transition is Lane F fatal.
  - TestOwnerRequestQueues.cpp proves the cursor matrix, partial-count
    arbitration, failed-drain mutation gate, and active-rebuild follow-ups.

Related:
  - SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "SceneControllerState.h"
#include "SceneGeneratedSetup.h"
#include "SceneSessionState.h"
#include "../Camera/CameraControlState.h"
#include "../../Core/SbResult.h"

#include <algorithm>
#include <cstdint>

namespace SkullbonezCore
{
namespace Rendering
{
class Dx12FrameOwner;
}
namespace Runtime
{
class SceneController;
class SimulationSystem;
class RuntimeTools;
struct SceneGeneratedControlTransactionTestAccess;

class SceneGeneratedControlPhaseCursor
{
  public:
    enum class Phase : uint8_t
    {
        Idle,
        DrainAndReset,
        Repopulate,
        PublishFollowUps,
        Complete,
        Count
    };

    static constexpr bool IsLegalTransition( Phase from, Phase to )
    {
        return ( from == Phase::Idle && to == Phase::DrainAndReset ) ||
               ( from == Phase::DrainAndReset && to == Phase::Repopulate ) ||
               ( from == Phase::Repopulate && to == Phase::PublishFollowUps ) ||
               ( from == Phase::PublishFollowUps && to == Phase::Complete );
    }

    bool TryAdvance( Phase next )
    {

        if ( !IsLegalTransition( m_phase, next ) )
        {
            return false;
        }

        m_phase = next;
        return true;
    }

    Phase Current() const
    {
        return m_phase;
    }

  private:
    Phase m_phase = Phase::Idle;
};

struct SceneGeneratedControlAction
{

    // Lane R: callers must terminate the current command/frame when a GPU
    // drain failed; no generated model/resource mutation has occurred.
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
    bool resetReplayTimeline = false;
    bool scheduleProfileReset = false;
};

struct SceneGeneratedUICommandResult
{
    bool accepted = false;
    SceneGeneratedControlAction action;
};

// Invariant:
// - One accepted count request must drain/reset before repopulation and publish
//   detached follow-ups only after repopulation through the adjacent phase walk.
// - The owner retains request/policy/result values and its cursor, never a
//   borrowed runtime owner. TestOwnerRequestQueues.cpp proves the transition
//   matrix plus the arbitration, failed-drain, and follow-up decisions.
class SceneGeneratedControlTransaction
{
  public:
    SceneGeneratedControlTransaction( const SceneGeneratedControlTransaction& ) = delete;
    SceneGeneratedControlTransaction& operator=( const SceneGeneratedControlTransaction& ) = delete;

    static SceneGeneratedControlTransaction ModelCount( int requestedCount, GeneratedObjectTypeOverride objectTypeOverride,
                                                        int modelCapacity )
    {
        return SceneGeneratedControlTransaction( RequestKind::ModelCount, requestedCount, -1, objectTypeOverride,
                                                 modelCapacity );
    }

    static SceneGeneratedControlTransaction
    SolverBallCount( int requestedCount, GeneratedObjectTypeOverride objectTypeOverride, int modelCapacity )
    {
        return SceneGeneratedControlTransaction( RequestKind::SolverBallCount, requestedCount, -1, objectTypeOverride,
                                                 modelCapacity );
    }

    static SceneGeneratedControlTransaction
    SolverBoxCount( int requestedCount, GeneratedObjectTypeOverride objectTypeOverride, int modelCapacity )
    {
        return SceneGeneratedControlTransaction( RequestKind::SolverBoxCount, requestedCount, -1, objectTypeOverride,
                                                 modelCapacity );
    }

    static SceneGeneratedControlTransaction SolverCounts( int requestedBalls, int requestedBoxes,
                                                          GeneratedObjectTypeOverride objectTypeOverride, int modelCapacity )
    {
        return SceneGeneratedControlTransaction( RequestKind::SolverCounts, requestedBalls, requestedBoxes,
                                                 objectTypeOverride, modelCapacity );
    }

    SceneGeneratedUICommandResult Execute( const SkullbonezCore::Core::EngineConfig& config, SceneController& scene,
                                           SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                                           CameraControlState& camera, SimulationSystem& simulation, RuntimeTools& tools,
                                           Rendering::Dx12FrameOwner* renderFrame );

    SceneGeneratedControlPhaseCursor::Phase Phase() const
    {
        return m_phase.Current();
    }

  private:
    enum class RequestKind : uint8_t
    {
        ModelCount,
        SolverBallCount,
        SolverBoxCount,
        SolverCounts
    };

    SceneGeneratedControlTransaction( RequestKind kind, int requestedPrimary, int requestedSecondary,
                                      GeneratedObjectTypeOverride objectTypeOverride, int modelCapacity )
        : m_kind( kind ), m_requestedPrimary( requestedPrimary ), m_requestedSecondary( requestedSecondary ),
          m_objectTypeOverride( objectTypeOverride ), m_modelCapacity( (std::max)( 0, modelCapacity ) )
    {
    }

    bool ResolveRequest( const SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                         const SceneSessionState& sceneState )
    {

        if ( m_kind != RequestKind::SolverCounts && m_requestedPrimary < 0 )
        {
            return false;
        }

        if ( m_kind == RequestKind::ModelCount )
        {

            // Invariant: model-count and exact solver overrides are mutually
            // exclusive. Repopulate commits this resolved mode atomically.
            m_modelCount = std::clamp( m_requestedPrimary, 0, m_modelCapacity );
            return true;
        }

        if ( m_kind == RequestKind::SolverBallCount )
        {

            // Invariant: the newest accepted box override, possibly from a
            // prior frame, wins over stale scene state for this partial request.
            m_solverBoxes = uiOverrides.solverBoxCountOverride >= 0 ? uiOverrides.solverBoxCountOverride
                                                                    : sceneState.solverBoxCount;

            m_solverBalls = std::clamp( m_requestedPrimary, 0, (std::max)( 0, m_modelCapacity - m_solverBoxes ) );
        }
        else if ( m_kind == RequestKind::SolverBoxCount )
        {

            // Invariant: InputFrame executes ball before box. Read its newest
            // accepted override so the combined request cannot exceed capacity.
            m_solverBalls = uiOverrides.solverBallCountOverride >= 0 ? uiOverrides.solverBallCountOverride
                                                                     : sceneState.solverBallCount;

            m_solverBoxes = std::clamp( m_requestedPrimary, 0, (std::max)( 0, m_modelCapacity - m_solverBalls ) );
        }
        else
        {
            m_solverBalls = m_requestedPrimary;
            m_solverBoxes = m_requestedSecondary;
        }

        // Exact-count stress requests and partial UI requests share one final
        // normalization rule: preserve balls first and trim boxes second.
        m_solverBalls = std::clamp( m_solverBalls, 0, m_modelCapacity );
        m_solverBoxes = std::clamp( m_solverBoxes, 0, m_modelCapacity );

        if ( m_solverBalls + m_solverBoxes > m_modelCapacity )
        {
            m_solverBoxes = (std::max)( 0, m_modelCapacity - m_solverBalls );
        }

        return true;
    }
    SkullbonezCore::Core::SbResult DrainAndReset( SceneController& scene, SimulationSystem& simulation, RuntimeTools& tools,
                                                  Rendering::Dx12FrameOwner* renderFrame );
    void Repopulate( const SkullbonezCore::Core::EngineConfig& config, SceneController& scene,
                     SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides, CameraControlState& camera );
    void PublishFollowUps();
    void AdvanceOrFatal( SceneGeneratedControlPhaseCursor::Phase next, const char* operation );

    // Invariant: this recorded result is the only permission bit consulted
    // immediately before generated topology and owner state may mutate.
    bool RecordDrainResult( const SkullbonezCore::Core::SbResult& result )
    {
        m_result.action.status = result;
        m_drainSucceeded = result.Ok();
        return m_drainSucceeded;
    }

    bool MutationAllowedAfterDrain() const
    {
        return m_phase.Current() == SceneGeneratedControlPhaseCursor::Phase::DrainAndReset && m_drainSucceeded;
    }

    void RecordFollowUps()
    {

        if ( m_rebuildActiveScene )
        {
            m_result.action.resetReplayTimeline = true;
            m_result.action.scheduleProfileReset = true;
        }
    }

    friend struct SceneGeneratedControlTransactionTestAccess;

    RequestKind m_kind;
    int m_requestedPrimary = -1;
    int m_requestedSecondary = -1;
    GeneratedObjectTypeOverride m_objectTypeOverride = GeneratedObjectTypeOverride::Mixed;
    int m_modelCapacity = 0;
    int m_modelCount = -1;
    int m_solverBalls = -1;
    int m_solverBoxes = -1;
    bool m_rebuildActiveScene = false;
    bool m_drainSucceeded = false;
    SceneGeneratedUICommandResult m_result;
    SceneGeneratedControlPhaseCursor m_phase;
};

} // namespace Runtime
} // namespace SkullbonezCore
