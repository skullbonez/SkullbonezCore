/*
File: SkullbonezSource/Runtime/Scene/SceneGeneratedControlTransaction.cpp
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
  - SkullbonezSource/Runtime/Scene/SceneGeneratedControlTransaction.h
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#include "SceneGeneratedControlTransaction.h"
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
    const char* owner = result.ErrorOwner() && result.ErrorOwner()[0] != '\0' ? result.ErrorOwner()
                                                                              : "Runtime/SceneGeneratedControls";

    const char* message = result.ErrorMessage()[0] != '\0' ? result.ErrorMessage()
                                                           : "generated-scene rebuild failed without a message";

    fprintf( stderr, "[scene] generated_rebuild_failed owner=%s reason=\"%s\"\n", owner, message );
}
} // namespace

SkullbonezCore::Core::SbResult SceneGeneratedControlTransaction::DrainAndReset( SceneController& scene,
                                                                                SimulationSystem& simulation,
                                                                                RuntimeTools& tools,
                                                                                Rendering::Dx12FrameOwner* renderFrame )
{
    AdvanceOrFatal( SceneGeneratedControlPhaseCursor::Phase::DrainAndReset, "DrainAndReset" );

    if ( !m_rebuildActiveScene )
    {
        const SkullbonezCore::Core::SbResult success = SkullbonezCore::Core::SbResult::Success();
        RecordDrainResult( success );
        return success;
    }

    // Hazard: generated rebuilds destroy model/render state. A failed GPU drain
    // must return before overrides, topology, simulation, or tools mutate.

    if ( renderFrame )
    {
        const SkullbonezCore::Core::SbResult flushResult = renderFrame->FlushGPU();

        if ( !RecordDrainResult( flushResult ) )
        {

            // Lane R: the input/stress boundary reports the device failure and
            // this transaction never enters Repopulate.
            return flushResult;
        }
    }
    else
    {
        RecordDrainResult( SkullbonezCore::Core::SbResult::Success() );
    }

    // Invariant: RecordDrainResult is the single gate between the device
    // result and owner mutation. Failure returns above with this predicate false.

    if ( !MutationAllowedAfterDrain() )
    {
        SB_FATAL( "Runtime/SceneGeneratedControlTransaction",
                  "Generated-scene mutation reached without a successful drain." );
    }

    scene.Scene().Clear();
    tools.ClearRayCastTestLines();
    simulation.Reset();
    scene.State().currentFrame = 0;
    scene.State().isTestComplete = false;
    return SkullbonezCore::Core::SbResult::Success();
}

void SceneGeneratedControlTransaction::Repopulate( const SkullbonezCore::Core::EngineConfig& config, SceneController& scene,
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
    const SkullbonezCore::Core::SbResult setupResult = m_kind == RequestKind::ModelCount
                                                           ? SceneGeneratedSetup::SetUpSceneEntities( scene.State(), config,
                                                                                                      scene.Scene(),
                                                                                                      m_objectTypeOverride,
                                                                                                      m_modelCount )
                                                           : SceneGeneratedSetup::SetUpSolverObjects( scene.State(), config,
                                                                                                      scene.Scene(),
                                                                                                      m_objectTypeOverride,
                                                                                                      m_solverBalls,
                                                                                                      m_solverBoxes );

    if ( !setupResult.Ok() )
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
    RecordFollowUps();
}

void SceneGeneratedControlTransaction::AdvanceOrFatal( SceneGeneratedControlPhaseCursor::Phase next, const char* operation )
{
    const SceneGeneratedControlPhaseCursor::Phase current = m_phase.Current();

    if ( !m_phase.TryAdvance( next ) )
    {

        // Lane F: accepting an out-of-order phase could mutate topology before
        // the GPU drain or publish replay state before repopulation.
        SB_FATAL( "Runtime/SceneGeneratedControlTransaction", "Illegal phase transition. operation=%s current=%u next=%u",
                  operation, static_cast<unsigned int>( current ), static_cast<unsigned int>( next ) );
    }
}

SceneGeneratedUICommandResult
SceneGeneratedControlTransaction::Execute( const SkullbonezCore::Core::EngineConfig& config, SceneController& scene,
                                           SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                                           CameraControlState& camera, SimulationSystem& simulation, RuntimeTools& tools,
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

    if ( !m_result.action.status.Ok() )
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
