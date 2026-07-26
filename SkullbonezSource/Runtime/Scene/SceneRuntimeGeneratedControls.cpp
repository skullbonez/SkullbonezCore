/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeGeneratedControls.cpp
Purpose:
  Implements the phase-enforcing generated-scene rebuild transaction.

Summary:
  One stack-scoped owner resolves a count request, drains and resets the active
  scene, repopulates deterministic generated objects, and publishes detached
  replay/profiler follow-ups. It retains no borrowed runtime owner.

Glossary:
  Generated override: UI-selected model count or solver ball/box counts.
  Request arbitration: Selection of the untouched solver count from the newest
    accepted UI override before falling back to scene state.
  Follow-up action: Returned flags that tell Run to reset replay and profiler
    state after rebuilding generated objects.
  Model capacity: Maximum active model count that generated rebuilds must obey.

Invariants:
  - Accepted requests follow DrainAndReset, Repopulate, PublishFollowUps, and
    Complete; the transaction makes every other transition Lane F fatal.
  - A failed GPU drain returns Lane R before UI overrides or topology mutate.
  - Replay/profiler resets are detached values published only after repopulation.
  - Camera tracking is clamped against the post-rebuild model count.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntimeGeneratedControls.h
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#include "SceneRuntimeGeneratedControls.h"
#include "SceneController.h"
#include "../Tools/RuntimeTools.h"
#include "../Simulation/SimulationSystem.h"
#include "../../Core/FatalError.h"
#include "../../Rendering/DX12/Dx12FrameOwner.h"

#include <algorithm>
#include <cstdio>

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
void LogGeneratedControlFailure( const SkullbonezCore::Core::SbResult& result )
{
    // Why: The transaction has already cleared mutable scene/model state.
    // Report the recoverable owner and let the caller reset replay/profiler
    // state around the now-current partial topology.
    const char* owner = result.error.owner && result.error.owner[0] != '\0' ? result.error.owner
                                                                            : "Runtime/SceneGeneratedControls";

    const char* message = result.error.message[0] != '\0' ? result.error.message
                                                          : "generated-scene rebuild failed without a message";

    fprintf( stderr, "[scene] generated_rebuild_failed owner=%s reason=\"%s\"\n", owner, message );
}
} // namespace

SceneGeneratedControlTransaction::SceneGeneratedControlTransaction( RequestKind kind,
                                                                    int requestedPrimary,
                                                                    int requestedSecondary,
                                                                    GeneratedObjectTypeOverride objectTypeOverride,
                                                                    int modelCapacity )
    : m_kind( kind ), m_requestedPrimary( requestedPrimary ), m_requestedSecondary( requestedSecondary ),
      m_objectTypeOverride( objectTypeOverride ), m_modelCapacity( (std::max)( 0, modelCapacity ) )
{
}

SceneGeneratedControlTransaction
SceneGeneratedControlTransaction::ModelCount( int requestedCount,
                                              GeneratedObjectTypeOverride objectTypeOverride,
                                              int modelCapacity )
{
    return SceneGeneratedControlTransaction( RequestKind::ModelCount,
                                             requestedCount,
                                             -1,
                                             objectTypeOverride,
                                             modelCapacity );
}

SceneGeneratedControlTransaction
SceneGeneratedControlTransaction::SolverBallCount( int requestedCount,
                                                   GeneratedObjectTypeOverride objectTypeOverride,
                                                   int modelCapacity )
{
    return SceneGeneratedControlTransaction( RequestKind::SolverBallCount,
                                             requestedCount,
                                             -1,
                                             objectTypeOverride,
                                             modelCapacity );
}

SceneGeneratedControlTransaction
SceneGeneratedControlTransaction::SolverBoxCount( int requestedCount,
                                                  GeneratedObjectTypeOverride objectTypeOverride,
                                                  int modelCapacity )
{
    return SceneGeneratedControlTransaction( RequestKind::SolverBoxCount,
                                             requestedCount,
                                             -1,
                                             objectTypeOverride,
                                             modelCapacity );
}

SceneGeneratedControlTransaction
SceneGeneratedControlTransaction::SolverCounts( int requestedBalls,
                                                int requestedBoxes,
                                                GeneratedObjectTypeOverride objectTypeOverride,
                                                int modelCapacity )
{
    return SceneGeneratedControlTransaction( RequestKind::SolverCounts,
                                             requestedBalls,
                                             requestedBoxes,
                                             objectTypeOverride,
                                             modelCapacity );
}

bool SceneGeneratedControlTransaction::ResolveRequest( const SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
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
        // Invariant: a prior accepted box command in the same frame wins over
        // stale scene state when constraining this partial request.
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

SkullbonezCore::Core::SbResult SceneGeneratedControlTransaction::DrainAndReset( SceneController& scene,
                                                                                SimulationSystem& simulation,
                                                                                RuntimeTools& tools,
                                                                                Rendering::Dx12FrameOwner* renderFrame )
{
    AdvanceOrFatal( SceneGeneratedControlPhaseCursor::Phase::DrainAndReset, "DrainAndReset" );
    if ( !m_rebuildActiveScene )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    // Hazard: generated rebuilds destroy model/render state. A failed GPU drain
    // must return before overrides, topology, simulation, or tools mutate.
    if ( renderFrame )
    {
        const SkullbonezCore::Core::SbResult flushResult = renderFrame->FlushGPU();
        if ( !flushResult.ok )
        {
            // Lane R: the input/stress boundary reports the device failure and
            // this transaction never enters Repopulate.
            return flushResult;
        }
    }

    scene.Scene().Clear();
    tools.ClearRayCastTestLines();
    simulation.Reset();
    scene.State().currentFrame = 0;
    scene.State().isTestComplete = false;
    return SkullbonezCore::Core::SbResult::Success();
}

void SceneGeneratedControlTransaction::Repopulate( const SkullbonezCore::Core::EngineConfig& config,
                                                   SceneController& scene,
                                                   SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                                                   CameraControlState& camera )
{
    AdvanceOrFatal( SceneGeneratedControlPhaseCursor::Phase::Repopulate, "Repopulate" );

    if ( m_kind == RequestKind::ModelCount )
    {
        uiOverrides.modelCountOverride = m_modelCount;
        uiOverrides.solverBallCountOverride = -1;
        uiOverrides.solverBoxCountOverride = -1;
    }
    else
    {
        uiOverrides.solverBallCountOverride = m_solverBalls;
        uiOverrides.solverBoxCountOverride = m_solverBoxes;
        uiOverrides.modelCountOverride = -1;
    }

    if ( !m_rebuildActiveScene )
    {
        return;
    }

    if ( m_kind == RequestKind::ModelCount && m_modelCount <= 0 )
    {
        scene.State().modelCount = 0;
        camera.trackBallRow.value = -1;
        return;
    }

    const unsigned int seed = scene.State().rngSeed > 0 ? scene.State().rngSeed : 1u;
    scene.State().rngState = seed;
    const SceneGeneratedModelContext context { scene.State(), config, scene.Scene(), m_objectTypeOverride };

    const SkullbonezCore::Core::SbResult setupResult = m_kind == RequestKind::ModelCount
                                                           ? SceneGeneratedSetup::SetUpSceneEntities( context,
                                                                                                      m_modelCount )
                                                           : SceneGeneratedSetup::SetUpSolverObjects( context,
                                                                                                      m_solverBalls,
                                                                                                      m_solverBoxes );

    if ( !setupResult.ok )
    {
        LogGeneratedControlFailure( setupResult );
        scene.State().modelCount = scene.Scene().SceneEntityCount();
        camera.trackBallRow.value = scene.State().modelCount > 0 ? scene.State().modelCount - 1 : -1;
        return;
    }

    const int cameraModelCount = m_kind == RequestKind::ModelCount ? m_modelCount : scene.State().modelCount;
    if ( cameraModelCount <= 0 )
    {
        camera.trackBallRow.value = -1;
    }
    else if ( camera.trackBallRow.value >= cameraModelCount )
    {
        camera.trackBallRow.value = cameraModelCount - 1;
    }
}

void SceneGeneratedControlTransaction::PublishFollowUps()
{
    AdvanceOrFatal( SceneGeneratedControlPhaseCursor::Phase::PublishFollowUps, "PublishFollowUps" );
    if ( m_rebuildActiveScene )
    {
        m_result.action.resetReplayTimeline = true;
        m_result.action.scheduleProfileReset = true;
    }
}

void SceneGeneratedControlTransaction::AdvanceOrFatal( SceneGeneratedControlPhaseCursor::Phase next,
                                                       const char* operation )
{
    const SceneGeneratedControlPhaseCursor::Phase current = m_phase.Current();
    if ( !m_phase.TryAdvance( next ) )
    {
        // Lane F: accepting an out-of-order phase could mutate topology before
        // the GPU drain or publish replay state before repopulation.
        SB_FATAL( "Runtime/SceneGeneratedControlTransaction",
                  "Illegal phase transition. operation=%s current=%u next=%u",
                  operation,
                  static_cast<unsigned int>( current ),
                  static_cast<unsigned int>( next ) );
    }
}

SceneGeneratedUICommandResult
SceneGeneratedControlTransaction::Execute( const SkullbonezCore::Core::EngineConfig& config,
                                           SceneController& scene,
                                           SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                                           CameraControlState& camera,
                                           SimulationSystem& simulation,
                                           RuntimeTools& tools,
                                           Rendering::Dx12FrameOwner* renderFrame )
{
    // Invalid UI sentinel values do not represent a transaction and therefore
    // do not enter the phase machine.
    if ( !ResolveRequest( uiOverrides, scene.State() ) )
    {
        return m_result;
    }

    m_rebuildActiveScene = scene.HasCurrentEntry();
    m_result.action.status = DrainAndReset( scene, simulation, tools, renderFrame );
    if ( !m_result.action.status.ok )
    {
        return m_result;
    }

    Repopulate( config, scene, uiOverrides, camera );
    PublishFollowUps();
    m_result.accepted = true;
    AdvanceOrFatal( SceneGeneratedControlPhaseCursor::Phase::Complete, "Complete" );
    return m_result;
}

} // namespace Runtime
} // namespace SkullbonezCore
