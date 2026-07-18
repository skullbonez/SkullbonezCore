/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeGeneratedControls.cpp
Purpose:
  Implements live generated-scene rebuild helpers outside Run.

Summary:
  Generated-scene UI rebuilds clear the active generated objects, reset fixed
  step simulation state, reseed the deterministic setup path, and request the
  caller-owned replay/profiler resets that still sit on Run.

Glossary:
  Generated override: UI-selected model count or solver ball/box counts.
  Follow-up action: Returned flags that tell Run to reset replay and profiler
    state after rebuilding generated objects.
  Model capacity: Maximum active model count that generated rebuilds must obey.

Invariants:
  - Rebuilds clear generated runtime state before reseeding object setup.
  - Replay/profiler resets are returned as actions, not performed here.
  - Camera tracking must be clamped after model-count changes.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntimeGeneratedControls.h
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#include "SceneRuntimeGeneratedControls.h"
#include "SceneController.h"
#include "../Tools/RuntimeTools.h"
#include "SceneController.h"
#include "../../Physics/SimulationSystem.h"
#include "../../Rendering/IRenderDeviceLifecycle.h"

#include <algorithm>
#include <cstdio>

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
SceneRuntimeGeneratedControlAction RequestReplayAndProfileReset()
{
    SceneRuntimeGeneratedControlAction action;
    action.resetReplayTimeline = true;
    action.scheduleProfileReset = true;
    return action;
}

SkullbonezCore::Core::SbResult ResetGeneratedRuntimeState( SceneController& scene,
                                                           SceneGeneratedControlResetParticipants reset )
{
    // Hazard: Generated rebuilds destroy model/render state. Flush GPU work
    // first, then clear objects and reset simulation/tool state together.
    if ( reset.renderLifecycle )
    {
        const SkullbonezCore::Core::SbResult flushResult = reset.renderLifecycle->FlushGPU();
        if ( !flushResult.ok )
        {
            // Lane R: no owner below this point may mutate after an uncertain
            // drain. Return the device failure to the input/stress boundary.
            return flushResult;
        }
    }
    scene.Scene().Clear();
    reset.tools.ClearRayCastTestLines();
    reset.simulation.Reset();
    scene.State().currentFrame = 0;
    scene.State().isTestComplete = false;
    return SkullbonezCore::Core::SbResult::Success();
}

SceneGeneratedModelContext BuildGeneratedModelContext( SceneGeneratedControlPolicy policy, SceneController& scene )
{
    return SceneGeneratedModelContext{ scene.State(), policy.config, scene.Scene(), policy.objectTypeOverride };
}

void LogGeneratedControlFailure( const SkullbonezCore::Core::SbResult& result )
{
    // Why: UI rebuild has already cleared mutable scene/model state. Report the
    // recoverable owner and let the caller reset replay/profiler state around
    // the now-current partial topology.
    const char* owner =
        result.error.owner && result.error.owner[0] != '\0' ? result.error.owner : "Runtime/SceneGeneratedControls";
    const char* message =
        result.error.message[0] != '\0' ? result.error.message : "generated-scene rebuild failed without a message";
    fprintf( stderr, "[scene] generated_rebuild_failed owner=%s reason=\"%s\"\n", owner, message );
}
} // namespace

SceneRuntimeGeneratedControlAction ApplyUIModelCountOverride( SceneGeneratedControlPolicy policy,
                                                              SceneGeneratedControlPresentation presentation,
                                                              SceneGeneratedControlResetParticipants reset,
                                                              SceneController& scene,
                                                              int count )
{
    // Invariant: A model-count override is mutually exclusive with solver exact
    // ball/box overrides; only one generated setup mode owns the rebuild.
    const int modelCountOverride = std::clamp( count, 0, policy.modelCapacity );
    if ( !scene.HasCurrentEntry() )
    {
        presentation.uiOverrides.modelCountOverride = modelCountOverride;
        presentation.uiOverrides.solverBallCountOverride = -1;
        presentation.uiOverrides.solverBoxCountOverride = -1;
        return SceneRuntimeGeneratedControlAction{};
    }

    const SkullbonezCore::Core::SbResult resetResult = ResetGeneratedRuntimeState( scene, reset );
    if ( !resetResult.ok )
    {
        SceneRuntimeGeneratedControlAction action;
        action.status = resetResult;
        return action;
    }
    presentation.uiOverrides.modelCountOverride = modelCountOverride;
    presentation.uiOverrides.solverBallCountOverride = -1;
    presentation.uiOverrides.solverBoxCountOverride = -1;
    if ( presentation.uiOverrides.modelCountOverride <= 0 )
    {
        scene.State().modelCount = 0;
        presentation.camera.trackBallRow.value = -1;
        return RequestReplayAndProfileReset();
    }

    const unsigned int seed = scene.State().rngSeed > 0 ? scene.State().rngSeed : 1u;
    scene.State().rngState = seed;
    const SkullbonezCore::Core::SbResult setupResult =
        SceneGeneratedSetup::SetUpSceneEntities( BuildGeneratedModelContext( policy, scene ),
                                                 presentation.uiOverrides.modelCountOverride );
    if ( !setupResult.ok )
    {
        LogGeneratedControlFailure( setupResult );
        scene.State().modelCount = scene.Scene().SceneEntityCount();
        presentation.camera.trackBallRow.value = scene.State().modelCount > 0 ? scene.State().modelCount - 1 : -1;
        return RequestReplayAndProfileReset();
    }
    if ( presentation.camera.trackBallRow.value >= presentation.uiOverrides.modelCountOverride )
    {
        presentation.camera.trackBallRow.value = presentation.uiOverrides.modelCountOverride - 1;
    }
    return RequestReplayAndProfileReset();
}

SceneRuntimeGeneratedControlAction ApplyUISolverObjectCounts( SceneGeneratedControlPolicy policy,
                                                              SceneGeneratedControlPresentation presentation,
                                                              SceneGeneratedControlResetParticipants reset,
                                                              SceneController& scene,
                                                              int balls,
                                                              int boxes )
{
    // Concept: Solver overrides are exact-count generated scenes used by physics
    // diagnostics and baseline runs, so model-count UI override is cleared.
    balls = std::clamp( balls, 0, policy.modelCapacity );
    boxes = std::clamp( boxes, 0, policy.modelCapacity );
    if ( balls + boxes > policy.modelCapacity )
    {
        boxes = (std::max)( 0, policy.modelCapacity - balls );
    }
    if ( !scene.HasCurrentEntry() )
    {
        presentation.uiOverrides.solverBallCountOverride = balls;
        presentation.uiOverrides.solverBoxCountOverride = boxes;
        presentation.uiOverrides.modelCountOverride = -1;
        return SceneRuntimeGeneratedControlAction{};
    }

    const SkullbonezCore::Core::SbResult resetResult = ResetGeneratedRuntimeState( scene, reset );
    if ( !resetResult.ok )
    {
        SceneRuntimeGeneratedControlAction action;
        action.status = resetResult;
        return action;
    }
    presentation.uiOverrides.solverBallCountOverride = balls;
    presentation.uiOverrides.solverBoxCountOverride = boxes;
    presentation.uiOverrides.modelCountOverride = -1;

    const unsigned int seed = scene.State().rngSeed > 0 ? scene.State().rngSeed : 1u;
    scene.State().rngState = seed;
    const SkullbonezCore::Core::SbResult setupResult =
        SceneGeneratedSetup::SetUpSolverObjects( BuildGeneratedModelContext( policy, scene ),
                                                 presentation.uiOverrides.solverBallCountOverride,
                                                 presentation.uiOverrides.solverBoxCountOverride );
    if ( !setupResult.ok )
    {
        LogGeneratedControlFailure( setupResult );
        scene.State().modelCount = scene.Scene().SceneEntityCount();
        presentation.camera.trackBallRow.value = scene.State().modelCount > 0 ? scene.State().modelCount - 1 : -1;
        return RequestReplayAndProfileReset();
    }
    if ( scene.State().modelCount <= 0 )
    {
        presentation.camera.trackBallRow.value = -1;
    }
    else if ( presentation.camera.trackBallRow.value >= scene.State().modelCount )
    {
        presentation.camera.trackBallRow.value = scene.State().modelCount - 1;
    }
    return RequestReplayAndProfileReset();
}

SceneGeneratedUICommandResult ApplySceneGeneratedModelCountUICommand( SceneGeneratedControlPolicy policy,
                                                                      SceneGeneratedControlPresentation presentation,
                                                                      SceneGeneratedControlResetParticipants reset,
                                                                      SceneController& scene,
                                                                      int requestedModelCount )
{
    SceneGeneratedUICommandResult result;
    if ( requestedModelCount < 0 )
    {
        return result;
    }

    result.action = ApplyUIModelCountOverride( policy, presentation, reset, scene, requestedModelCount );
    result.accepted = result.action.status.ok;
    return result;
}

SceneGeneratedUICommandResult
ApplySceneGeneratedSolverBallCountUICommand( SceneGeneratedControlPolicy policy,
                                             SceneGeneratedControlPresentation presentation,
                                             SceneGeneratedControlResetParticipants reset,
                                             SceneController& scene,
                                             int requestedSolverBallCount )
{
    SceneGeneratedUICommandResult result;
    if ( requestedSolverBallCount < 0 )
    {
        return result;
    }

    // Invariant: solver count sliders are partial exact-count requests. The
    // untouched shape count comes from the active override first, then scene state.
    const int boxes = presentation.uiOverrides.solverBoxCountOverride >= 0
                          ? presentation.uiOverrides.solverBoxCountOverride
                          : scene.State().solverBoxCount;
    result.action = ApplyUISolverObjectCounts(
        policy,
        presentation,
        reset,
        scene,
        std::clamp( requestedSolverBallCount, 0, (std::max)( 0, policy.modelCapacity - boxes ) ),
        boxes );
    result.accepted = result.action.status.ok;
    return result;
}

SceneGeneratedUICommandResult
ApplySceneGeneratedSolverBoxCountUICommand( SceneGeneratedControlPolicy policy,
                                            SceneGeneratedControlPresentation presentation,
                                            SceneGeneratedControlResetParticipants reset,
                                            SceneController& scene,
                                            int requestedSolverBoxCount )
{
    SceneGeneratedUICommandResult result;
    if ( requestedSolverBoxCount < 0 )
    {
        return result;
    }

    // Invariant: if the ball slider was handled earlier this frame, the override
    // already holds its accepted value and must constrain the box slider max.
    const int balls = presentation.uiOverrides.solverBallCountOverride >= 0
                          ? presentation.uiOverrides.solverBallCountOverride
                          : scene.State().solverBallCount;
    result.action = ApplyUISolverObjectCounts(
        policy,
        presentation,
        reset,
        scene,
        balls,
        std::clamp( requestedSolverBoxCount, 0, (std::max)( 0, policy.modelCapacity - balls ) ) );
    result.accepted = result.action.status.ok;
    return result;
}

} // namespace Runtime
} // namespace SkullbonezCore
