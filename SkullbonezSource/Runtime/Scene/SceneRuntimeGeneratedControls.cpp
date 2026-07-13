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

SkullbonezCore::Core::SbResult ResetGeneratedRuntimeState( SceneRuntimeGeneratedControlContext context )
{
    // Hazard: Generated rebuilds destroy model/render state. Flush GPU work
    // first, then clear objects and reset simulation/tool state together.
    if ( context.renderLifecycle )
    {
        const SkullbonezCore::Core::SbResult flushResult = context.renderLifecycle->FlushGPU();
        if ( !flushResult.ok )
        {
            // Lane R: no owner below this point may mutate after an uncertain
            // drain. Return the device failure to the input/stress boundary.
            return flushResult;
        }
    }
    context.models.Clear();
    context.tools.ClearRayCastTestLines();
    context.simulation.Reset();
    context.scene.currentFrame = 0;
    context.scene.isTestComplete = false;
    return SkullbonezCore::Core::SbResult::Success();
}

SceneGeneratedModelContext BuildGeneratedModelContext( SceneRuntimeGeneratedControlContext context )
{
    return SceneGeneratedModelContext{ context.scene,
                                       context.config,
                                       context.world,
                                       context.terrain,
                                       context.models,
                                       context.controller.Physics(),
                                       context.objectTypeOverride };
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

SceneRuntimeGeneratedControlAction ApplyUIModelCountOverride( SceneRuntimeGeneratedControlContext context, int count )
{
    // Invariant: A model-count override is mutually exclusive with solver exact
    // ball/box overrides; only one generated setup mode owns the rebuild.
    const int modelCountOverride = std::clamp( count, 0, context.modelCapacity );
    if ( !context.controller.HasCurrentEntry() )
    {
        context.uiOverrides.modelCountOverride = modelCountOverride;
        context.uiOverrides.solverBallCountOverride = -1;
        context.uiOverrides.solverBoxCountOverride = -1;
        return SceneRuntimeGeneratedControlAction{};
    }

    const SkullbonezCore::Core::SbResult resetResult = ResetGeneratedRuntimeState( context );
    if ( !resetResult.ok )
    {
        SceneRuntimeGeneratedControlAction action;
        action.status = resetResult;
        return action;
    }
    context.uiOverrides.modelCountOverride = modelCountOverride;
    context.uiOverrides.solverBallCountOverride = -1;
    context.uiOverrides.solverBoxCountOverride = -1;
    if ( context.uiOverrides.modelCountOverride <= 0 )
    {
        context.scene.modelCount = 0;
        context.camera.trackBallRow.value = -1;
        return RequestReplayAndProfileReset();
    }

    const unsigned int seed = context.scene.rngSeed > 0 ? context.scene.rngSeed : 1u;
    context.scene.rngState = seed;
    const SkullbonezCore::Core::SbResult setupResult =
        SceneGeneratedSetup::SetUpSceneEntities( BuildGeneratedModelContext( context ),
                                                 context.uiOverrides.modelCountOverride );
    if ( !setupResult.ok )
    {
        LogGeneratedControlFailure( setupResult );
        context.scene.modelCount = context.models.SceneEntityCount();
        context.camera.trackBallRow.value = context.scene.modelCount > 0 ? context.scene.modelCount - 1 : -1;
        return RequestReplayAndProfileReset();
    }
    if ( context.camera.trackBallRow.value >= context.uiOverrides.modelCountOverride )
    {
        context.camera.trackBallRow.value = context.uiOverrides.modelCountOverride - 1;
    }
    return RequestReplayAndProfileReset();
}

SceneRuntimeGeneratedControlAction
ApplyUISolverObjectCounts( SceneRuntimeGeneratedControlContext context, int balls, int boxes )
{
    // Concept: Solver overrides are exact-count generated scenes used by physics
    // diagnostics and baseline runs, so model-count UI override is cleared.
    balls = std::clamp( balls, 0, context.modelCapacity );
    boxes = std::clamp( boxes, 0, context.modelCapacity );
    if ( balls + boxes > context.modelCapacity )
    {
        boxes = (std::max)( 0, context.modelCapacity - balls );
    }
    if ( !context.controller.HasCurrentEntry() )
    {
        context.uiOverrides.solverBallCountOverride = balls;
        context.uiOverrides.solverBoxCountOverride = boxes;
        context.uiOverrides.modelCountOverride = -1;
        return SceneRuntimeGeneratedControlAction{};
    }

    const SkullbonezCore::Core::SbResult resetResult = ResetGeneratedRuntimeState( context );
    if ( !resetResult.ok )
    {
        SceneRuntimeGeneratedControlAction action;
        action.status = resetResult;
        return action;
    }
    context.uiOverrides.solverBallCountOverride = balls;
    context.uiOverrides.solverBoxCountOverride = boxes;
    context.uiOverrides.modelCountOverride = -1;

    const unsigned int seed = context.scene.rngSeed > 0 ? context.scene.rngSeed : 1u;
    context.scene.rngState = seed;
    const SkullbonezCore::Core::SbResult setupResult =
        SceneGeneratedSetup::SetUpSolverObjects( BuildGeneratedModelContext( context ),
                                                 context.uiOverrides.solverBallCountOverride,
                                                 context.uiOverrides.solverBoxCountOverride );
    if ( !setupResult.ok )
    {
        LogGeneratedControlFailure( setupResult );
        context.scene.modelCount = context.models.SceneEntityCount();
        context.camera.trackBallRow.value = context.scene.modelCount > 0 ? context.scene.modelCount - 1 : -1;
        return RequestReplayAndProfileReset();
    }
    if ( context.scene.modelCount <= 0 )
    {
        context.camera.trackBallRow.value = -1;
    }
    else if ( context.camera.trackBallRow.value >= context.scene.modelCount )
    {
        context.camera.trackBallRow.value = context.scene.modelCount - 1;
    }
    return RequestReplayAndProfileReset();
}

SceneGeneratedUICommandResult ApplySceneGeneratedModelCountUICommand( SceneRuntimeGeneratedControlContext context,
                                                                      int requestedModelCount )
{
    SceneGeneratedUICommandResult result;
    if ( requestedModelCount < 0 )
    {
        return result;
    }

    result.action = ApplyUIModelCountOverride( context, requestedModelCount );
    result.accepted = result.action.status.ok;
    return result;
}

SceneGeneratedUICommandResult ApplySceneGeneratedSolverBallCountUICommand( SceneRuntimeGeneratedControlContext context,
                                                                           int requestedSolverBallCount )
{
    SceneGeneratedUICommandResult result;
    if ( requestedSolverBallCount < 0 )
    {
        return result;
    }

    // Invariant: solver count sliders are partial exact-count requests. The
    // untouched shape count comes from the active override first, then scene state.
    const int boxes = context.uiOverrides.solverBoxCountOverride >= 0 ? context.uiOverrides.solverBoxCountOverride
                                                                      : context.scene.solverBoxCount;
    result.action = ApplyUISolverObjectCounts(
        context,
        std::clamp( requestedSolverBallCount, 0, (std::max)( 0, context.modelCapacity - boxes ) ),
        boxes );
    result.accepted = result.action.status.ok;
    return result;
}

SceneGeneratedUICommandResult ApplySceneGeneratedSolverBoxCountUICommand( SceneRuntimeGeneratedControlContext context,
                                                                          int requestedSolverBoxCount )
{
    SceneGeneratedUICommandResult result;
    if ( requestedSolverBoxCount < 0 )
    {
        return result;
    }

    // Invariant: if the ball slider was handled earlier this frame, the override
    // already holds its accepted value and must constrain the box slider max.
    const int balls = context.uiOverrides.solverBallCountOverride >= 0 ? context.uiOverrides.solverBallCountOverride
                                                                       : context.scene.solverBallCount;
    result.action = ApplyUISolverObjectCounts(
        context,
        balls,
        std::clamp( requestedSolverBoxCount, 0, (std::max)( 0, context.modelCapacity - balls ) ) );
    result.accepted = result.action.status.ok;
    return result;
}

} // namespace Runtime
} // namespace SkullbonezCore
