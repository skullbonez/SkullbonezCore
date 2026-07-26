/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeGeneratedControls.h
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
  Phase cursor: Value state that accepts only the adjacent rebuild walk.
  Rebuild action: Returned flags for caller-owned replay/profiler cleanup.
  Action status: Lane R result that blocks all rebuild mutations when the GPU
    drain cannot prove old resource use complete.
  Model capacity: Active object capacity limit.

Invariants:
  - Every accepted command walks DrainAndReset, Repopulate,
    PublishFollowUps, and Complete in that order.
  - The transaction stores request/policy/result values only; every mutable
    runtime owner is borrowed by Execute and expires before it returns.
  - Generated model/resource mutation starts only after a successful GPU drain;
    an illegal phase transition is Lane F fatal.
  - TestOwnerRequestQueues.cpp exhaustively proves the cursor transition matrix.

Related:
  - SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#pragma once

#include "SceneControllerState.h"
#include "SceneGeneratedSetup.h"
#include "../Camera/CameraControlState.h"
#include "../../Core/SbResult.h"

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
struct SceneSessionState;

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

struct SceneRuntimeGeneratedControlAction
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
    SceneRuntimeGeneratedControlAction action;
};

class SceneGeneratedControlTransaction
{
  public:
    SceneGeneratedControlTransaction( const SceneGeneratedControlTransaction& ) = delete;
    SceneGeneratedControlTransaction& operator=( const SceneGeneratedControlTransaction& ) = delete;

    static SceneGeneratedControlTransaction
    ModelCount( int requestedCount, GeneratedObjectTypeOverride objectTypeOverride, int modelCapacity );
    static SceneGeneratedControlTransaction
    SolverBallCount( int requestedCount, GeneratedObjectTypeOverride objectTypeOverride, int modelCapacity );
    static SceneGeneratedControlTransaction
    SolverBoxCount( int requestedCount, GeneratedObjectTypeOverride objectTypeOverride, int modelCapacity );
    static SceneGeneratedControlTransaction SolverCounts( int requestedBalls,
                                                          int requestedBoxes,
                                                          GeneratedObjectTypeOverride objectTypeOverride,
                                                          int modelCapacity );

    SceneGeneratedUICommandResult Execute( const SkullbonezCore::Core::EngineConfig& config,
                                           SceneController& scene,
                                           SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                                           CameraControlState& camera,
                                           SimulationSystem& simulation,
                                           RuntimeTools& tools,
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

    SceneGeneratedControlTransaction( RequestKind kind,
                                      int requestedPrimary,
                                      int requestedSecondary,
                                      GeneratedObjectTypeOverride objectTypeOverride,
                                      int modelCapacity );

    bool ResolveRequest( const SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                         const SceneSessionState& sceneState );
    SkullbonezCore::Core::SbResult DrainAndReset( SceneController& scene,
                                                  SimulationSystem& simulation,
                                                  RuntimeTools& tools,
                                                  Rendering::Dx12FrameOwner* renderFrame );
    void Repopulate( const SkullbonezCore::Core::EngineConfig& config,
                     SceneController& scene,
                     SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                     CameraControlState& camera );
    void PublishFollowUps();
    void AdvanceOrFatal( SceneGeneratedControlPhaseCursor::Phase next, const char* operation );

    RequestKind m_kind;
    int m_requestedPrimary = -1;
    int m_requestedSecondary = -1;
    GeneratedObjectTypeOverride m_objectTypeOverride = GeneratedObjectTypeOverride::Mixed;
    int m_modelCapacity = 0;
    int m_modelCount = -1;
    int m_solverBalls = -1;
    int m_solverBoxes = -1;
    bool m_rebuildActiveScene = false;
    SceneGeneratedUICommandResult m_result;
    SceneGeneratedControlPhaseCursor m_phase;
};

} // namespace Runtime
} // namespace SkullbonezCore
